#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <netinet/tcp.h>

#include <libssh/libssh.h>
#include <libssh/callbacks.h>

#include "velaops.h"

struct velaops_ssh
{
  ssh_session session;
  ssh_channel channel;  /* 缓存的通道，复用以避免通道 ID 冲突 */
  bool library_initialized;
  int transport_fd;
};

static int64_t elapsed_milliseconds(const struct timespec *started)
{
  struct timespec now;

  clock_gettime(CLOCK_MONOTONIC, &now);
  return (int64_t)(now.tv_sec - started->tv_sec) * 1000 +
         (now.tv_nsec - started->tv_nsec) / 1000000;
}

struct velaops_ssh *velaops_ssh_connect(const struct velaops_profile *profile,
                                        char *fingerprint,
                                        unsigned int fingerprint_size)
{
  struct velaops_ssh *client = calloc(1, sizeof(*client));
  ssh_key key = NULL;
  unsigned char *hash = NULL;
  size_t hash_size = 0;
  char *hex = NULL;
  /* The ESP32-S3 Wi-Fi driver occasionally needs several TCP retransmission
   * intervals under load.  Eight seconds produced false KEX/auth failures on
   * an otherwise reachable LAN host, so keep a bounded but tolerant timeout. */
  long timeout = 60;
  unsigned int port = profile->port;
  struct sockaddr_in address;
  int fd = -1;

  if (client == NULL) goto fail;
  client->transport_fd = -1;
  /* The ESP32-S3 port does not provide mbedTLS pthread mutex hooks. VelaOps
   * owns libssh from one application thread, so the official no-op backend
   * is the correct callback set for this lifecycle. */
  if (ssh_threads_set_callbacks(ssh_threads_get_noop()) != SSH_OK)
    {
      puts("libssh thread callback setup failed");
      goto fail;
    }
  if (ssh_init() != SSH_OK)
    {
      puts("libssh initialization failed");
      goto fail;
    }
  client->library_initialized = true;
  if ((client->session = ssh_new()) == NULL) goto fail;

  /* Numeric IPv4 is the MVP configuration path.  Establish it with NuttX's
   * native socket API and give the connected descriptor to libssh.  This
   * avoids libssh's generic getaddrinfo/multi-address connector, which is not
   * compatible with this ESP32-S3 NuttX network port. */
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = htons(profile->port);
  if (inet_pton(AF_INET, profile->host, &address.sin_addr) == 1)
    {
      int one = 1;

      fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      if (fd < 0 || connect(fd, (struct sockaddr *)&address,
                            sizeof(address)) < 0)
        {
          printf("Native TCP connect failed: errno=%d (%s)\n",
                 errno, strerror(errno));
          if (fd >= 0) close(fd);
          goto fail;
        }

      /* SSH consists of many small control packets.  On this NuttX/lwIP
       * target some of them can remain queued behind Nagle long enough for
       * OpenSSH's pre-auth grace period to expire, despite send() succeeding.
       * Request immediate transmission for KEX/auth/channel messages. */
      if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) < 0)
        {
          printf("Native TCP_NODELAY setup failed: errno=%d (%s)\n",
                 errno, strerror(errno));
        }

      printf("Native TCP connection established to %s:%u\n",
             profile->host, profile->port);
      client->transport_fd = fd;
    }

  /* Try ECDH instead of curve25519 to see if the KEX algorithm affects
   * the ESP32-S3 WiFi firmware crash during SSH KEX. */
  if (ssh_options_set(client->session, SSH_OPTIONS_KEY_EXCHANGE,
                      "ecdh-sha2-nistp256") != SSH_OK)
    {
      printf("libssh KEX algorithm set failed: %s\n",
             ssh_get_error(client->session));
      /* Continue with default algorithm */
    }

  if (ssh_options_set(client->session, SSH_OPTIONS_HOST, profile->host) != SSH_OK ||
      ssh_options_set(client->session, SSH_OPTIONS_USER, profile->user) != SSH_OK ||
      ssh_options_set(client->session, SSH_OPTIONS_PORT, &port) != SSH_OK ||
      ssh_options_set(client->session, SSH_OPTIONS_TIMEOUT, &timeout) != SSH_OK ||
      (client->transport_fd >= 0 &&
       ssh_options_set(client->session, SSH_OPTIONS_FD,
                       &client->transport_fd) != SSH_OK))
    {
      printf("libssh option error: %s\n", ssh_get_error(client->session));
      goto fail;
    }

  /* SSH_OPTIONS_FD transfers practical ownership to the libssh session: its
   * socket teardown closes the descriptor.  Do not close it a second time in
   * the VelaOps wrapper. */
  if (client->transport_fd >= 0)
    client->transport_fd = -1;

  /* NuttX lwIP socket 可能需要短暂稳定时间。
   * 不加这个延迟时，SSH KEX 的 ECDH 回复有时会丢失。
   * 200ms 经测试可同时解决 KEX 和 auth 的时序问题。 */

  usleep(200000);

  if (ssh_connect(client->session) != SSH_OK)
    {
      printf("libssh connect error: %s\n", ssh_get_error(client->session));
      goto fail;
    }

  if (ssh_get_server_publickey(client->session, &key) != SSH_OK ||
      ssh_get_publickey_hash(key, SSH_PUBLICKEY_HASH_SHA256,
                             &hash, &hash_size) != SSH_OK)
    {
      printf("libssh host-key error: %s\n", ssh_get_error(client->session));
      goto fail;
    }
  hex = ssh_get_hexa(hash, hash_size);
  if (hex == NULL) goto fail;
  snprintf(fingerprint, fingerprint_size, "SHA256:%s", hex);
  ssh_string_free_char(hex);
  ssh_clean_pubkey_hash(&hash);
  ssh_key_free(key);
  return client;

fail:
  if (hex != NULL) ssh_string_free_char(hex);
  if (hash != NULL) ssh_clean_pubkey_hash(&hash);
  if (key != NULL) ssh_key_free(key);
  velaops_ssh_close(client);
  return NULL;
}

int velaops_ssh_auth_password(struct velaops_ssh *client, const char *password)
{
  struct timespec started;
  int64_t elapsed_ms;
  int result;

  /* The NuttX socket backend can legitimately return SSH_AUTH_AGAIN while
   * the password request is being flushed or while it is waiting for the
   * server reply.  This is continuation state, not an authentication
   * rejection: call the same API again so libssh can resume its pending
   * SSH_PENDING_CALL_AUTH_PASSWORD operation. */
  clock_gettime(CLOCK_MONOTONIC, &started);
  for (;;)
    {
      result = ssh_userauth_password(client->session, NULL, password);
      if (result != SSH_AUTH_AGAIN)
        {
          break;
        }

      elapsed_ms = elapsed_milliseconds(&started);
      if (elapsed_ms >= 30000)
        {
          puts("libssh authentication timed out waiting for server response");
          return -1;
        }

      usleep(20000);
    }

  if (result != SSH_AUTH_SUCCESS)
    printf("libssh authentication error: %s (code %d)\n",
           ssh_get_error(client->session), result);
  return result == SSH_AUTH_SUCCESS ? 0 : -1;
}

int velaops_ssh_auth_publickey(struct velaops_ssh *client,
                               const char *private_key_path)
{
  ssh_key private_key = NULL;
  int result;

  if (private_key_path == NULL || private_key_path[0] == '\0')
    {
      printf("SSH: No private key path specified\n");
      return -1;
    }

  printf("SSH: Loading private key from %s\n", private_key_path);

  /* 从文件加载私钥 */
  result = ssh_pki_import_privkey_file(private_key_path, NULL, NULL, NULL,
                                       &private_key);
  if (result != SSH_OK)
    {
      printf("SSH: Failed to load private key: %s\n",
             ssh_get_error(client->session));
      return -1;
    }

  /* 使用私钥进行认证 */
  result = ssh_userauth_publickey(client->session, NULL, private_key);
  ssh_key_free(private_key);

  if (result != SSH_AUTH_SUCCESS)
    {
      printf("SSH: Public key authentication failed: %s (code %d)\n",
             ssh_get_error(client->session), result);
      return -1;
    }

  printf("SSH: Public key authentication successful\n");
  return 0;
}

bool velaops_ssh_is_connected(struct velaops_ssh *client)
{
  if (client == NULL || client->session == NULL)
    {
      return false;
    }

  /* 检查会话状态 */
  return ssh_is_connected(client->session) != 0;
}

/* 确保通道可用：如果缓存通道不存在或已关闭，创建并打开新通道 */
static int ensure_channel(struct velaops_ssh *client)
{
  struct timespec started;
  int result;

  /* 检查 SSH 会话是否还活着 */
  if (!ssh_is_connected(client->session))
    {
      printf("SSH exec: session not connected\n");
      return -1;
    }

  if (client->channel != NULL && ssh_channel_is_open(client->channel))
    return 0;

  /* 清理旧通道 */
  if (client->channel != NULL)
    {
      ssh_channel_free(client->channel);
      client->channel = NULL;
    }

  client->channel = ssh_channel_new(client->session);
  if (client->channel == NULL)
    {
      printf("SSH exec: channel creation failed\n");
      return -1;
    }

  clock_gettime(CLOCK_MONOTONIC, &started);
  do
    {
      result = ssh_channel_open_session(client->channel);
      if (result == SSH_AGAIN)
        usleep(20000);
    }
  while (result == SSH_AGAIN && elapsed_milliseconds(&started) < 15000);

  if (result != SSH_OK)
    {
      printf("SSH exec: channel open failed%s: %s\n",
             result == SSH_AGAIN ? " (timeout)" : "",
             ssh_get_error(client->session));
      ssh_channel_free(client->channel);
      client->channel = NULL;
      return -1;
    }

  ssh_channel_set_blocking(client->channel, 1);
  return 0;
}

int velaops_ssh_exec(struct velaops_ssh *client, const char *command,
                     char *output, unsigned int output_size)
{
  struct timespec started;
  unsigned int used = 0;
  int result;
  int n;

  printf("SSH exec: %.60s%s\n", command,
         strlen(command) > 60 ? "..." : "");

  /* 确保通道可用 */
  if (ensure_channel(client) < 0)
    goto fail;

  clock_gettime(CLOCK_MONOTONIC, &started);
  do
    {
      result = ssh_channel_request_exec(client->channel, command);
      if (result == SSH_AGAIN)
        usleep(20000);
    }
  while (result == SSH_AGAIN && elapsed_milliseconds(&started) < 30000);

  if (result != SSH_OK)
    {
      printf("SSH exec: request exec failed%s: %s\n",
             result == SSH_AGAIN ? " (timeout)" : "",
             ssh_get_error(client->session));
      /* 通道可能已损坏，清理后下次重建 */
      ssh_channel_close(client->channel);
      ssh_channel_free(client->channel);
      client->channel = NULL;
      goto fail;
    }

  /* 读取输出 */
  clock_gettime(CLOCK_MONOTONIC, &started);
  while (used + 1 < output_size)
    {
      n = ssh_channel_read(client->channel, output + used,
                           output_size - used - 1, 0);
      if (n == SSH_AGAIN)
        {
          if (elapsed_milliseconds(&started) >= 30000)
            {
              puts("SSH exec: read timeout");
              goto fail;
            }
          usleep(20000);
          continue;
        }
      if (n < 0)
        {
          printf("SSH exec: read failed: %s\n",
                 ssh_get_error(client->session));
          ssh_channel_close(client->channel);
          ssh_channel_free(client->channel);
          client->channel = NULL;
          goto fail;
        }
      if (n == 0)
        {
          break;
        }
      used += n;
      clock_gettime(CLOCK_MONOTONIC, &started);
    }

  output[used] = '\0';
  /* 不关闭通道 — 复用它 */
  return 0;
fail:
  if (output_size > 0) output[0] = '\0';
  return -1;
}

void velaops_ssh_close(struct velaops_ssh *client)
{
  if (client == NULL) return;
  if (client->channel != NULL)
    {
      if (ssh_channel_is_open(client->channel))
        ssh_channel_close(client->channel);
      ssh_channel_free(client->channel);
      client->channel = NULL;
    }
  if (client->session != NULL)
    {
      ssh_disconnect(client->session);
      ssh_free(client->session);
    }
  if (client->transport_fd >= 0) close(client->transport_fd);
  if (client->library_initialized) ssh_finalize();
  free(client);
}
