/****************************************************************************
 * Contest 2026 team 023 - VelaOps 串口 LLM 隧道（板端实现）。
 ****************************************************************************/

#ifdef __NuttX__

#include "velaops_serial_tunnel.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#define VELAOPS_TUNNEL_IN_B64 "/tmp/vop-in.b64"
#define VELAOPS_TUNNEL_IN_READY "/tmp/vop-in.ready"
#define VELAOPS_TUNNEL_ACK "/tmp/vop-ack"
#define VELAOPS_TUNNEL_NACK "/tmp/vop-nack"
#define VELAOPS_TUNNEL_REQ_MAX (64 * 1024)
#define VELAOPS_TUNNEL_RESP_MAX (256 * 1024)

/* 单路 USB CDC 半双工复用：请求切成带序号和 CRC 的小块，relay 缺块回 NACK，
 * 板端只重发缺块，因此日志偶发插帧也能自愈；请求被 relay 完整收下后回 ACK，
 * 板端随即停止重发、只等响应。 */
#define VELAOPS_TUNNEL_CHUNK 120
#define VELAOPS_TUNNEL_MAX_ROUNDS 10
#define VELAOPS_TUNNEL_ROUND_WAIT_MS 3500
#define VELAOPS_TUNNEL_FAST_READY_MS 12000
/* 必须大于 relay 的转发超时（180s）：否则本端先放弃并前进，随后会收到那条
 * 陈旧响应造成错位/卡死。留足余量覆盖大上下文 LLM 请求。 */
#define VELAOPS_TUNNEL_LLM_READY_MS 300000

static const char g_b64[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int g_tunnel_xid;

static uint16_t velaops_tunnel_crc16(const char *data, size_t len)
{
  uint16_t crc = 0xffff;
  size_t i;
  int bit;

  for (i = 0; i < len; i++)
    {
      crc ^= (uint8_t)data[i];
      for (bit = 0; bit < 8; bit++)
        {
          crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0x8408)
                          : (uint16_t)(crc >> 1);
        }
    }
  return crc;
}

/* base64 编码到新分配字符串；失败返回 NULL。 */
static char *velaops_tunnel_b64_encode(const char *data, size_t len)
{
  size_t cap = 4 * ((len + 2) / 3) + 1;
  char *out = malloc(cap);
  size_t i;
  size_t o = 0;

  if (out == NULL)
    {
      return NULL;
    }
  for (i = 0; i < len; i += 3)
    {
      unsigned int v = (unsigned int)(unsigned char)data[i] << 16;

      if (i + 1 < len)
        {
          v |= (unsigned int)(unsigned char)data[i + 1] << 8;
        }
      if (i + 2 < len)
        {
          v |= (unsigned int)(unsigned char)data[i + 2];
        }

      out[o++] = g_b64[(v >> 18) & 0x3f];
      out[o++] = g_b64[(v >> 12) & 0x3f];
      out[o++] = i + 1 < len ? g_b64[(v >> 6) & 0x3f] : '=';
      out[o++] = i + 2 < len ? g_b64[v & 0x3f] : '=';
    }
  out[o] = '\0';
  return out;
}

/* 发送帧头（仅首轮）与需要发送的分块（missing==NULL 表示全部）。 */
static int velaops_tunnel_send(int xid, const char *b64, size_t b64len,
                               int nchunks, int with_header,
                               const bool *missing)
{
  int seq;

  if (with_header)
    {
      char header[64];
      int hlen = snprintf(header, sizeof(header), "\n@@VF %d %d %u\n",
                          xid, nchunks, (unsigned int)b64len);

      if (hlen < 0 || (size_t)hlen >= sizeof(header) ||
          write(STDOUT_FILENO, header, (size_t)hlen) != (ssize_t)hlen)
        {
          return -1;
        }
    }

  for (seq = 0; seq < nchunks; seq++)
    {
      size_t offset;
      size_t chunk_len;
      uint16_t crc;
      char line[VELAOPS_TUNNEL_CHUNK + 48];
      int line_len;

      if (missing != NULL && !missing[seq])
        {
          continue;
        }

      offset = (size_t)seq * VELAOPS_TUNNEL_CHUNK;
      chunk_len = b64len - offset;
      if (chunk_len > VELAOPS_TUNNEL_CHUNK)
        {
          chunk_len = VELAOPS_TUNNEL_CHUNK;
        }
      crc = velaops_tunnel_crc16(b64 + offset, chunk_len);
      line_len = snprintf(line, sizeof(line), "@@VC %d %d %04x %.*s\n",
                          xid, seq, (unsigned int)crc, (int)chunk_len,
                          b64 + offset);
      if (line_len < 0 || (size_t)line_len >= sizeof(line) ||
          write(STDOUT_FILENO, line, (size_t)line_len) != (ssize_t)line_len)
        {
          return -1;
        }
    }
  return 0;
}

/* 读取 relay 的 NACK（缺失序号列表，空格/逗号分隔），返回缺失块数。 */
static int velaops_tunnel_read_nack(bool *missing, int nchunks)
{
  char buffer[1024];
  FILE *file;
  size_t length;
  char *cursor;
  int count = 0;

  file = fopen(VELAOPS_TUNNEL_NACK, "r");
  if (file == NULL)
    {
      return -1;
    }
  length = fread(buffer, 1, sizeof(buffer) - 1, file);
  buffer[length] = '\0';
  fclose(file);
  unlink(VELAOPS_TUNNEL_NACK);

  memset(missing, 0, (size_t)nchunks);
  cursor = buffer;
  while (*cursor != '\0')
    {
      char *end;
      long value = strtol(cursor, &end, 10);

      if (end == cursor)
        {
          break;
        }
      cursor = end;
      if (value >= 0 && value < nchunks)
        {
          missing[value] = true;
          count++;
        }
      while (*cursor == ',' || *cursor == ' ' || *cursor == '\n')
        {
          cursor++;
        }
    }
  return count;
}

static int velaops_tunnel_b64_value(char c)
{
  const char *p = strchr(g_b64, c);

  return p == NULL ? -1 : (int)(p - g_b64);
}

/* 解码 base64 文件到动态缓冲；返回字节数或 -1。 */
static ssize_t velaops_tunnel_load_response(char **out)
{
  FILE *file;
  long size;
  char *b64;
  char *data;
  size_t i;
  size_t o = 0;

  file = fopen(VELAOPS_TUNNEL_IN_B64, "r");
  if (file == NULL)
    {
      return -1;
    }
  fseek(file, 0, SEEK_END);
  size = ftell(file);
  fseek(file, 0, SEEK_SET);
  if (size <= 0 || size > VELAOPS_TUNNEL_RESP_MAX)
    {
      fclose(file);
      return -1;
    }

  b64 = malloc((size_t)size + 1);
  data = malloc((size_t)size + 1);
  if (b64 == NULL || data == NULL)
    {
      free(b64);
      free(data);
      fclose(file);
      return -1;
    }
  if (fread(b64, 1, (size_t)size, file) != (size_t)size)
    {
      free(b64);
      free(data);
      fclose(file);
      return -1;
    }
  fclose(file);
  b64[size] = '\0';

  for (i = 0; i < (size_t)size; i++)
    {
      int a;
      int b;
      int c;
      int d;
      unsigned int v;

      if (b64[i] == '=' || b64[i] == '\n' || b64[i] == '\r' ||
          b64[i] == ' ' || b64[i] == '\t')
        {
          continue;
        }
      a = velaops_tunnel_b64_value(b64[i]);
      b = (i + 1 < (size_t)size) ? velaops_tunnel_b64_value(b64[i + 1]) : -1;
      c = (i + 2 < (size_t)size) ? velaops_tunnel_b64_value(b64[i + 2]) : -1;
      d = (i + 3 < (size_t)size) ? velaops_tunnel_b64_value(b64[i + 3]) : -1;
      if (a < 0 || b < 0)
        {
          break;
        }
      v = ((unsigned int)a << 18) | ((unsigned int)b << 12) |
          ((c < 0 ? 0u : (unsigned int)c) << 6) |
          (d < 0 ? 0u : (unsigned int)d);
      data[o++] = (char)((v >> 16) & 0xff);
      if (c >= 0)
        {
          data[o++] = (char)((v >> 8) & 0xff);
        }
      if (d >= 0)
        {
          data[o++] = (char)(v & 0xff);
        }
      i += 3;
    }

  free(b64);
  *out = data;
  return (ssize_t)o;
}

/* 读取一个完整 HTTP 请求（依赖 Content-Length）。成功返回 0。 */
static int velaops_tunnel_read_request(int fd, char **out, size_t *out_len)
{
  char *buf = malloc(8192);
  size_t cap = 8192;
  size_t len = 0;
  size_t header_end = 0;
  long content_len = -1;

  if (buf == NULL)
    {
      return -1;
    }

  for (;;)
    {
      ssize_t n;

      if (len + 1 >= cap && cap < VELAOPS_TUNNEL_REQ_MAX)
        {
          char *grown;
          size_t new_cap = cap * 2 > VELAOPS_TUNNEL_REQ_MAX
                               ? VELAOPS_TUNNEL_REQ_MAX
                               : cap * 2;

          grown = realloc(buf, new_cap);
          if (grown == NULL)
            {
              free(buf);
              return -1;
            }
          buf = grown;
          cap = new_cap;
        }
      if (len + 1 >= cap)
        {
          free(buf);
          return -1;
        }

      n = recv(fd, buf + len, cap - 1 - len, 0);
      if (n < 0 && errno == EINTR)
        {
          continue;
        }
      if (n < 0)
        {
          free(buf);
          return -1;
        }
      if (n == 0)
        {
          break;
        }
      len += (size_t)n;
      buf[len] = '\0';

      if (header_end == 0)
        {
          char *p = strstr(buf, "\r\n\r\n");

          if (p != NULL)
            {
              char *cl;

              header_end = (size_t)(p - buf) + 4;
              cl = strcasestr(buf, "content-length:");
              if (cl != NULL && cl < p)
                {
                  content_len = strtol(cl + strlen("content-length:"),
                                       NULL, 10);
                }
              else
                {
                  content_len = 0;
                }
            }
        }

      if (header_end != 0 && content_len >= 0 &&
          len >= header_end + (size_t)content_len)
        {
          break;
        }
    }

  if (header_end == 0)
    {
      free(buf);
      return -1;
    }
  *out = buf;
  *out_len = len;
  return 0;
}

static void velaops_tunnel_send_error(int fd)
{
  static const char msg[] =
    "HTTP/1.1 504 Gateway Timeout\r\n"
    "Content-Length: 0\r\nConnection: close\r\n\r\n";

  (void)write(fd, msg, sizeof(msg) - 1);
}

int velaops_serial_tunnel_run(void)
{
  struct sockaddr_in addr;
  struct timeval tv;
  int listen_fd;
  int option = 1;
  int bind_err;

  syslog(LOG_ERR, "velaops tunnel: starting\n");

  listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0)
    {
      syslog(LOG_ERR, "velaops tunnel: socket failed: %d\n", errno);
      return -1;
    }
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(VELAOPS_TUNNEL_PORT);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bind_err = bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr));
  if (bind_err < 0 || listen(listen_fd, 4) < 0)
    {
      syslog(LOG_ERR, "velaops tunnel: bind/listen failed: %d (bind=%d)\n",
             errno, bind_err);
      close(listen_fd);
      return -1;
    }

  syslog(LOG_ERR, "velaops tunnel: listening 127.0.0.1:%d\n",
         VELAOPS_TUNNEL_PORT);

  tv.tv_sec = 30;
  tv.tv_usec = 0;

  for (;;)
    {
      int client = accept(listen_fd, NULL, NULL);
      char *request = NULL;
      size_t request_len = 0;
      char *response = NULL;
      ssize_t response_len;
      int waited;

      if (client < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }
          sleep(1);
          continue;
        }
      setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

      /* 自愈：新连接到来先清空上一轮可能残留的帧状态，避免读到陈旧
       * 响应/ACK 或把上一轮的残留当成本轮结果。 */
      unlink(VELAOPS_TUNNEL_IN_READY);
      unlink(VELAOPS_TUNNEL_IN_B64);
      unlink(VELAOPS_TUNNEL_ACK);
      unlink(VELAOPS_TUNNEL_NACK);

      if (velaops_tunnel_read_request(client, &request, &request_len) != 0)
        {
          close(client);
          continue;
        }

      /* 分块发送 + ACK/NACK 自愈；请求类型决定等待响应的时长。 */
      waited = 0;
      {
        int is_llm = strstr(request, "/v1/chat/completions") != NULL;
        char *b64 = velaops_tunnel_b64_encode(request, request_len);
        size_t b64len;
        int nchunks;
        bool *missing;
        int xid;
        int attempt;
        int acked = 0;

        free(request);
        request = NULL;

        if (b64 == NULL)
          {
            velaops_tunnel_send_error(client);
            close(client);
            continue;
          }
        b64len = strlen(b64);
        nchunks = (int)((b64len + VELAOPS_TUNNEL_CHUNK - 1) /
                        VELAOPS_TUNNEL_CHUNK);
        if (nchunks < 1)
          {
            nchunks = 1;
          }
        missing = malloc((size_t)nchunks);
        if (missing == NULL)
          {
            free(b64);
            velaops_tunnel_send_error(client);
            close(client);
            continue;
          }

        xid = ++g_tunnel_xid;
        /* 关键：清掉上一轮的响应/就绪位，否则会读到旧响应（request_id 不符）。 */
        unlink(VELAOPS_TUNNEL_IN_READY);
        unlink(VELAOPS_TUNNEL_IN_B64);
        for (attempt = 0;
             attempt < VELAOPS_TUNNEL_MAX_ROUNDS && !acked; attempt++)
          {
            bool first = (attempt == 0);
            int ms;

            unlink(VELAOPS_TUNNEL_ACK);
            unlink(VELAOPS_TUNNEL_NACK);
            if (velaops_tunnel_send(xid, b64, b64len, nchunks, first,
                                    first ? NULL : missing) != 0)
              {
                break;
              }

            for (ms = 0; ms < VELAOPS_TUNNEL_ROUND_WAIT_MS; ms += 200)
              {
                if (access(VELAOPS_TUNNEL_ACK, F_OK) == 0)
                  {
                    acked = 1;
                    break;
                  }
                /* ACK 丢了但请求其实已被 relay 收下并回了响应：直接视为成功，
                 * 避免重发同一请求（同 nonce）被 Proxy 判 replay。 */
                if (access(VELAOPS_TUNNEL_IN_READY, F_OK) == 0)
                  {
                    acked = 1;
                    break;
                  }
                if (access(VELAOPS_TUNNEL_NACK, F_OK) == 0)
                  {
                    break;
                  }
                usleep(200000);
              }
            if (acked)
              {
                break;
              }
            if (access(VELAOPS_TUNNEL_NACK, F_OK) == 0 &&
                velaops_tunnel_read_nack(missing, nchunks) > 0)
              {
                continue;
              }
            /* 连 NACK 都没收到，视为整体丢失，重发全部。 */
            memset(missing, 1, (size_t)nchunks);
          }
        free(b64);
        free(missing);

        if (!acked)
          {
            syslog(LOG_ERR, "velaops tunnel: no ack from relay\n");
            velaops_tunnel_send_error(client);
            close(client);
            continue;
          }

        /* 已收妥，等 relay 注入响应（LLM 可能较慢）。 */
        {
          int limit = is_llm ? VELAOPS_TUNNEL_LLM_READY_MS
                             : VELAOPS_TUNNEL_FAST_READY_MS;
          int ms;

          for (ms = 0; ms < limit; ms += 200)
            {
              struct stat st;

              if (stat(VELAOPS_TUNNEL_IN_READY, &st) == 0)
                {
                  waited = 1;
                  break;
                }
              usleep(200000);
            }
        }
      }
      if (!waited)
        {
          syslog(LOG_ERR, "velaops tunnel: relay response timeout\n");
          velaops_tunnel_send_error(client);
          close(client);
          continue;
        }

      response_len = velaops_tunnel_load_response(&response);
      if (response_len > 0)
        {
          size_t sent = 0;

          while (sent < (size_t)response_len)
            {
              ssize_t n = write(client, response + sent,
                                (size_t)response_len - sent);
              if (n <= 0)
                {
                  break;
                }
              sent += (size_t)n;
            }
          syslog(LOG_ERR, "velaops tunnel: wrote LLM response %ld bytes\n",
                 (long)response_len);
        }
      else
        {
          velaops_tunnel_send_error(client);
        }
      if (response != NULL)
        {
          free(response);
        }
      unlink(VELAOPS_TUNNEL_IN_READY);
      unlink(VELAOPS_TUNNEL_IN_B64);
      close(client);
    }
}

#endif /* __NuttX__ */
