/****************************************************************************
 * apps/examples/velaops/velaops_llm.c
 *
 * Minimal HTTPS client for an OpenAI-compatible chat endpoint (MiMo).
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/platform.h"

#include "velaops_llm.h"
#include "velaops_pool.h"
#include "velaops_conf.h"

int velaops_llm_prealloc(void)
{
  if (velaops_pool_init(256 * 1024) < 0)
    {
      return -1;
    }

  mbedtls_platform_set_calloc_free(velaops_pool_calloc,
                                   velaops_pool_dealloc);
  return 0;
}

#define LLM_HOST_MAX   96
#define LLM_PATH_MAX   96
#define LLM_BODY_MAX   2048
#define LLM_RESP_MAX   8192

struct llm_conn_s
{
  int fd;
};

extern uint32_t esp_random(void);

static int llm_entropy(void *ctx, unsigned char *out, size_t len)
{
  size_t i;

  (void)ctx;

  for (i = 0; i < len; i += 4)
    {
      uint32_t value = esp_random();
      size_t n = len - i < 4 ? len - i : 4;

      memcpy(out + i, &value, n);
    }

  return 0;
}

static int llm_seed_rng(mbedtls_ctr_drbg_context *ctx)
{
  static const unsigned char pers[] = "velaops";

  return mbedtls_ctr_drbg_seed(ctx, llm_entropy, NULL, pers,
                               sizeof(pers) - 1);
}

static int llm_bio_send(void *ctx, const unsigned char *buf, size_t len)
{
  FAR struct llm_conn_s *conn = ctx;
  ssize_t n;

  do
    {
      n = send(conn->fd, buf, len, 0);
    }
  while (n < 0 && errno == EINTR);

  if (n < 0)
    {
      return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }

  return (int)n;
}

static int llm_bio_recv(void *ctx, unsigned char *buf, size_t len)
{
  FAR struct llm_conn_s *conn = ctx;
  ssize_t n;

  do
    {
      n = recv(conn->fd, buf, len, 0);
    }
  while (n < 0 && errno == EINTR);

  if (n == 0)
    {
      return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
    }

  if (n < 0)
    {
      return MBEDTLS_ERR_SSL_WANT_READ;
    }

  return (int)n;
}

static void json_escape(const char *in, char *out, size_t size)
{
  size_t o = 0;

  while (*in != '\0' && o + 8 < size)
    {
      switch (*in)
        {
          case '"':
          case '\\':
            out[o++] = '\\';
            out[o++] = *in++;
            break;

          case '\n':
            out[o++] = '\\';
            out[o++] = 'n';
            in++;
            break;

          case '\r':
            in++;
            break;

          default:
            out[o++] = *in++;
            break;
        }
    }

  out[o] = '\0';
}

static int llm_parse_url(const char *url, char *host, size_t hostsize,
                         char *port, size_t portsize,
                         char *path, size_t pathsize)
{
  const char *p;
  size_t len;

  if (strncmp(url, "https://", 8) != 0)
    {
      printf("velaops: llm url must be https\n");
      return -1;
    }

  p = url + 8;

  len = strcspn(p, ":/");
  if (len == 0 || len >= hostsize)
    {
      return -1;
    }

  memcpy(host, p, len);
  host[len] = '\0';
  p += len;

  strcpy(port, "443");

  if (*p == ':')
    {
      size_t n;

      p++;
      n = strcspn(p, "/");

      if (n > 0 && n < portsize)
        {
          memcpy(port, p, n);
          port[n] = '\0';
        }

      p += n;
    }

  if (*p == '\0')
    {
      snprintf(path, pathsize, "/v1");
    }
  else
    {
      snprintf(path, pathsize, "%s", p);
    }

  return 0;
}

static const char *llm_find_content(const char *resp, char *out, size_t size)
{
  const char *p = strstr(resp, "\"content\"");
  size_t o = 0;

  if (p == NULL)
    {
      p = strstr(resp, "\"error\"");
      if (p != NULL)
        {
          const char *m = strstr(p, "\"message\"");

          if (m != NULL)
            {
              m = strchr(m, ':');
              if (m != NULL)
                {
                  m = strchr(m, '"');
                  if (m != NULL)
                    {
                      m++;
                      while (*m != '\0' && *m != '"' && o + 1 < size)
                        {
                          out[o++] = *m++;
                        }

                      out[o] = '\0';
                      return out;
                    }
                }
            }
        }

      return NULL;
    }

  p = strchr(p, ':');
  if (p == NULL)
    {
      return NULL;
    }

  p = strchr(p, '"');
  if (p == NULL)
    {
      return NULL;
    }

  p++;

  while (*p != '\0' && o + 1 < size)
    {
      if (*p == '\\' && p[1] != '\0')
        {
          p++;

          if (*p == 'n')
            {
              out[o++] = ' ';
            }
          else if (*p == 'u')
            {
              p += 5;
              continue;
            }
          else
            {
              out[o++] = *p;
            }

          p++;
          continue;
        }

      if (*p == '"')
        {
          break;
        }

      out[o++] = *p++;
    }

  out[o] = '\0';
  return out;
}

static void llm_strip_markdown(char *text)
{
  char *src = text;
  char *dst = text;

  while (*src != '\0')
    {
      if (*src == '*' && src[1] == '*')
        {
          src += 2;
          continue;
        }

      if (*src == '`')
        {
          src++;
          continue;
        }

      *dst++ = *src++;
    }

  *dst = '\0';
}

int velaops_llm_chat(const char *url, const char *model, const char *key,
                     const char *prompt, char *out, size_t outsize)
{
  struct addrinfo hints;
  struct addrinfo *res = NULL;
  static struct llm_conn_s conn;
  static mbedtls_ssl_context ssl;
  static mbedtls_ssl_config conf;
  static mbedtls_entropy_context entropy;
  static mbedtls_ctr_drbg_context ctr_drbg;
  static char host[LLM_HOST_MAX];
  static char port[8];
  static char path[LLM_PATH_MAX];
  static char esc[1024];
  static char ipcfg[32];
  struct sockaddr_in addr4;
  struct sockaddr *saddr = NULL;
  socklen_t saddrlen = 0;
  char *body = NULL;
  char *resp = NULL;
  char *hdr = NULL;
  size_t used;
  size_t total = 0;
  int ret = -1;
  int rlen;

  memset(&conn, 0, sizeof(conn));
  mbedtls_ssl_init(&ssl);
  mbedtls_ssl_config_init(&conf);
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&ctr_drbg);

  out[0] = '\0';

  if (llm_parse_url(url, host, sizeof(host), port, sizeof(port),
                    path, sizeof(path)) < 0)
    {
      goto out;
    }

  printf("velaops: llm %s %s%s\n", host, port, path);

  ipcfg[0] = '\0';
  velaops_conf_get("llm_ip", ipcfg, sizeof(ipcfg));

  memset(&addr4, 0, sizeof(addr4));
  addr4.sin_family = AF_INET;
  addr4.sin_port = htons((uint16_t)atoi(port));

  if (ipcfg[0] != '\0' &&
      inet_pton(AF_INET, ipcfg, &addr4.sin_addr) == 1)
    {
      saddr = (struct sockaddr *)&addr4;
      saddrlen = sizeof(addr4);
      printf("velaops: llm ip %s%c", ipcfg, 10);
    }
  else
    {
      memset(&hints, 0, sizeof(hints));
      hints.ai_family = AF_INET;
      hints.ai_socktype = SOCK_STREAM;

      if (getaddrinfo(host, port, &hints, &res) != 0 || res == NULL)
        {
          printf("velaops: llm dns failed\n");
          goto out;
        }

      {
        char ipstr[INET_ADDRSTRLEN];

        inet_ntop(AF_INET, &((struct sockaddr_in *)res->ai_addr)->sin_addr,
                  ipstr, sizeof(ipstr));
        printf("velaops: llm dns ok ip=%s%c", ipstr, 10);
        velaops_conf_set("llm_ip", ipstr);
      }

      saddr = res->ai_addr;
      saddrlen = res->ai_addrlen;
    }

  conn.fd = socket(AF_INET, SOCK_STREAM, 0);
  if (conn.fd < 0)
    {
      printf("velaops: llm socket failed %d\n", errno);
      goto out;
    }

  {
    struct timeval tv;

    tv.tv_sec = 8;
    tv.tv_usec = 0;
    setsockopt(conn.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(conn.fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  }

  if (connect(conn.fd, saddr, saddrlen) < 0)
    {
      printf("velaops: llm connect failed %d\n", errno);
      goto out;
    }

  json_escape(prompt, esc, sizeof(esc));
  body = velaops_pool_calloc(1, LLM_BODY_MAX);
  resp = velaops_pool_calloc(1, LLM_RESP_MAX);
  hdr  = velaops_pool_calloc(1, 768);

  if (body == NULL || resp == NULL || hdr == NULL)
    {
      printf("velaops: llm oom\n");
      goto out;
    }

  used = snprintf(body, LLM_BODY_MAX,
                  "{\"model\":\"%s\",\"messages\":[{\"role\":\"user\","
                  "\"content\":\"%s\"}],\"max_tokens\":220}",
                  model, esc);

  if (used >= LLM_BODY_MAX)
    {
      printf("velaops: llm prompt too long\n");
      goto out;
    }

  snprintf(hdr, 768,
           "POST %s/chat/completions HTTP/1.1\r\n"
           "Host: %s\r\n"
           "Content-Type: application/json\r\n"
           "Authorization: Bearer %s\r\n"
           "Content-Length: %u\r\n"
           "Connection: close\r\n\r\n",
           path, host, key, (unsigned)used);

  if (llm_seed_rng(&ctr_drbg) != 0)
    {
      printf("velaops: llm rng failed%c", 10);
      goto out;
    }

  if (mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                                  MBEDTLS_SSL_TRANSPORT_STREAM,
                                  MBEDTLS_SSL_PRESET_DEFAULT) != 0)
    {
      printf("velaops: llm tls config failed\n");
      goto out;
    }

  mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
  mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);

  mbedtls_ssl_conf_min_tls_version(&conf, MBEDTLS_SSL_VERSION_TLS1_2);
  mbedtls_ssl_conf_max_tls_version(&conf, MBEDTLS_SSL_VERSION_TLS1_2);

  {
    static const int llm_ciphersuites[] =
    {
      MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
      MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256,
      MBEDTLS_TLS_RSA_WITH_AES_128_GCM_SHA256,
      MBEDTLS_TLS_RSA_WITH_AES_128_CBC_SHA256,
      0
    };

    mbedtls_ssl_conf_ciphersuites(&conf, llm_ciphersuites);
  }

  if (mbedtls_ssl_setup(&ssl, &conf) != 0)
    {
      printf("velaops: llm tls setup failed\n");
      goto out;
    }

  if (mbedtls_ssl_set_hostname(&ssl, host) != 0)
    {
      printf("velaops: llm tls hostname failed\n");
      goto out;
    }

  mbedtls_ssl_set_bio(&ssl, &conn, llm_bio_send, llm_bio_recv, NULL);

  ret = mbedtls_ssl_handshake(&ssl);
  if (ret != 0)
    {
      char err[96];

      mbedtls_strerror(ret, err, sizeof(err));
      printf("velaops: llm handshake failed %d %s\n", ret, err);
      ret = -1;
      goto out;
    }

  {
    size_t len = strlen(hdr);
    size_t sent = 0;

    while (sent < len)
      {
        int n = mbedtls_ssl_write(&ssl, (const unsigned char *)hdr + sent,
                                  len - sent);

        if (n <= 0)
          {
            printf("velaops: llm send hdr failed %d\n", n);
            ret = -1;
            goto out;
          }

        sent += (size_t)n;
      }

    sent = 0;

    while (sent < used)
      {
        int n = mbedtls_ssl_write(&ssl, (const unsigned char *)body + sent,
                                  used - sent);

        if (n <= 0)
          {
            printf("velaops: llm send body failed %d\n", n);
            ret = -1;
            goto out;
          }

        sent += (size_t)n;
      }
  }

  for (;;)
    {
      if (total >= LLM_RESP_MAX - 1)
        {
          break;
        }

      rlen = mbedtls_ssl_read(&ssl, (unsigned char *)resp + total,
                              LLM_RESP_MAX - 1 - total);

      if (rlen == MBEDTLS_ERR_SSL_WANT_READ ||
          rlen == MBEDTLS_ERR_SSL_WANT_WRITE)
        {
          continue;
        }

      if (rlen <= 0)
        {
          break;
        }

      total += (size_t)rlen;
    }

  resp[total] = '\0';
  printf("velaops: llm resp %u bytes\n", (unsigned)total);

  if (llm_find_content(resp, out, outsize) == NULL)
    {
      printf("velaops: llm no content\n");
      ret = -1;
      goto out;
    }

  llm_strip_markdown(out);
  ret = 0;

out:
  if (body != NULL)
    {
      velaops_pool_dealloc(body);
    }

  if (resp != NULL)
    {
      velaops_pool_dealloc(resp);
    }

  if (hdr != NULL)
    {
      velaops_pool_dealloc(hdr);
    }

  mbedtls_ssl_free(&ssl);
  mbedtls_ssl_config_free(&conf);
  mbedtls_ctr_drbg_free(&ctr_drbg);
  mbedtls_entropy_free(&entropy);

  if (conn.fd > 0)
    {
      close(conn.fd);
    }

  if (res != NULL)
    {
      freeaddrinfo(res);
    }

  return ret;
}
