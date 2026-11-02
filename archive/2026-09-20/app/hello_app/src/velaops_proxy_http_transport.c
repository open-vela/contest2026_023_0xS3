/****************************************************************************
 * VelaOps Proxy Client 的局域网 HTTP 传输适配。
 ****************************************************************************/

#ifndef __NuttX__
#  ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200112L
#  endif
#endif

#include "velaops_proxy_http_transport.h"

#include <pthread.h>
#include <time.h>

#include <errno.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <syslog.h>
#include <unistd.h>

#define VELAOPS_HTTP_REQUEST_CAPACITY 2048
#define VELAOPS_HTTP_HEADER_CAPACITY 2048
#define VELAOPS_HTTP_DEFAULT_TIMEOUT_SECONDS 5
#define VELAOPS_HTTP_LOCK_TIMEOUT_SECONDS 240

/* 板端所有走隧道的 HTTP 请求串行化：看板巡检、Agent 只读工具、后台采样共用
 * 同一条半双工隧道，必须排队，避免互相抢道导致帧交错/隧道卡死。
 * 用有界加锁：持有者最长只在一次请求内占用（诊断期 120s），超时则放弃本次
 * 请求而不是永久阻塞整条看板。 */
static pthread_mutex_t g_http_transport_lock = PTHREAD_MUTEX_INITIALIZER;

static int velaops_http_lock(void)
{
  struct timespec deadline;

  if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
    {
      return pthread_mutex_lock(&g_http_transport_lock);
    }
  deadline.tv_sec += VELAOPS_HTTP_LOCK_TIMEOUT_SECONDS;
  return pthread_mutex_timedlock(&g_http_transport_lock, &deadline) == 0 ?
         0 : -1;
}

/* 供 ai_agent 的 LLM HTTP 路径调用：让 LLM 大请求与团队隧道请求共用同一把锁，
 * 避免单条半双工隧道上互相抢道。超时未能取锁则降级为不加锁（并记录），避免
 * 把 LLM 调用卡死。 */
static int g_llm_tunnel_locked;

void velaops_tunnel_lock(void)
{
  g_llm_tunnel_locked = velaops_http_lock() == 0;
  if (!g_llm_tunnel_locked)
    {
      syslog(LOG_ERR, "velaops: LLM 隧道加锁超时，降级放行\n");
    }
}

void velaops_tunnel_unlock(void)
{
  if (g_llm_tunnel_locked)
    {
      g_llm_tunnel_locked = 0;
      pthread_mutex_unlock(&g_http_transport_lock);
    }
}

static bool velaops_safe_header_text(const char *text)
{
  const unsigned char *cursor = (const unsigned char *)text;

  if (text == NULL || *text == '\0')
    {
      return false;
    }
  while (*cursor != '\0')
    {
      if (*cursor < 0x20 || *cursor > 0x7e)
        {
          return false;
        }
      cursor++;
    }
  return true;
}

static int velaops_append(char *buffer, size_t capacity, size_t *length,
                          const char *format, const char *first,
                          const char *second)
{
  int written = snprintf(buffer + *length, capacity - *length, format,
                         first, second);

  if (written < 0 || (size_t)written >= capacity - *length)
    {
      return -1;
    }
  *length += (size_t)written;
  return 0;
}

static int velaops_connect(const velaops_proxy_http_context_t *context)
{
  struct addrinfo hints;
  struct addrinfo *addresses = NULL;
  struct addrinfo *address;
  struct timeval timeout;
  int fd = -1;
  int seconds;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(context->host, context->port, &hints, &addresses) != 0)
    {
      return -1;
    }

  seconds = context->timeout_seconds > 0 ? context->timeout_seconds :
            VELAOPS_HTTP_DEFAULT_TIMEOUT_SECONDS;
  timeout.tv_sec = seconds;
  timeout.tv_usec = 0;
  for (address = addresses; address != NULL; address = address->ai_next)
    {
      fd = socket(address->ai_family, address->ai_socktype,
                  address->ai_protocol);
      if (fd < 0)
        {
          continue;
        }
      setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
      setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
      if (connect(fd, address->ai_addr, address->ai_addrlen) == 0)
        {
          break;
        }
      close(fd);
      fd = -1;
    }
  freeaddrinfo(addresses);
  return fd;
}

static int velaops_send_all(int fd, const void *data, size_t length)
{
  const uint8_t *cursor = data;

  while (length > 0)
    {
      ssize_t sent = send(fd, cursor, length, 0);
      if (sent < 0 && errno == EINTR)
        {
          continue;
        }
      if (sent <= 0)
        {
          return -1;
        }
      cursor += sent;
      length -= (size_t)sent;
    }
  return 0;
}

static char *velaops_find_header_end(char *buffer, size_t length)
{
  size_t index;

  for (index = 0; index + 3 < length; index++)
    {
      if (memcmp(buffer + index, "\r\n\r\n", 4) == 0)
        {
          return buffer + index + 4;
        }
    }
  return NULL;
}

static int velaops_parse_response(char *raw, size_t raw_len,
                                  char *response, size_t response_capacity,
                                  size_t *response_len)
{
  char *body = velaops_find_header_end(raw, raw_len);
  char *line;
  char *line_end;
  size_t content_length = 0;
  bool has_content_length = false;
  int status;

  if (body == NULL || sscanf(raw, "HTTP/1.%*c %d", &status) != 1 ||
      status < 100 || status > 599)
    {
      return -1;
    }
  line = strstr(raw, "\r\n");
  if (line == NULL)
    {
      return -1;
    }
  line += 2;
  while (line < body - 2)
    {
      char *colon;
      line_end = strstr(line, "\r\n");
      if (line_end == NULL || line_end >= body)
        {
          return -1;
        }
      colon = memchr(line, ':', (size_t)(line_end - line));
      if (colon != NULL &&
          (size_t)(colon - line) == strlen("Content-Length") &&
          strncasecmp(line, "Content-Length", strlen("Content-Length")) == 0)
        {
          char *value = colon + 1;
          char *end;
          unsigned long parsed;

          while (value < line_end && *value == ' ')
            {
              value++;
            }
          errno = 0;
          parsed = strtoul(value, &end, 10);
          if (has_content_length || errno != 0 || end != line_end)
            {
              return -1;
            }
          content_length = (size_t)parsed;
          has_content_length = true;
        }
      line = line_end + 2;
    }
  if (!has_content_length || content_length >= response_capacity ||
      (size_t)(body - raw) + content_length != raw_len)
    {
      return -1;
    }
  memcpy(response, body, content_length);
  *response_len = content_length;
  return status;
}

int velaops_proxy_http_transport(
    void *context, const char *method, const char *target,
    const velaops_http_header_t *headers,
    const uint8_t *body, size_t body_len,
    char *response, size_t response_capacity, size_t *response_len)
{
  const velaops_proxy_http_context_t *http_context = context;
  const velaops_http_header_t *header;
  char request[VELAOPS_HTTP_REQUEST_CAPACITY];
  char content_length[24];
  char *raw = NULL;
  size_t request_len = 0;
  size_t raw_capacity;
  size_t raw_len = 0;
  int fd = -1;
  int status = -1;

  if (http_context == NULL || !velaops_safe_header_text(http_context->host) ||
      !velaops_safe_header_text(http_context->port) || method == NULL ||
      target == NULL || target[0] != '/' || strchr(target, '\r') != NULL ||
      strchr(target, '\n') != NULL || headers == NULL || response == NULL ||
      response_capacity < 2 || response_len == NULL ||
      (body == NULL && body_len != 0))
    {
      return -1;
    }
  /* 串行化入口：同一时刻只允许一个隧道请求；有界等待，避免永久阻塞。 */
  if (velaops_http_lock() != 0)
    {
      return -1;
    }

  if (velaops_append(request, sizeof(request), &request_len,
                     "%s %s HTTP/1.1\r\n", method, target) != 0 ||
      velaops_append(request, sizeof(request), &request_len,
                     "Host: %s:%s\r\n", http_context->host,
                     http_context->port) != 0)
    {
      goto cleanup;
    }
  for (header = headers; header->name != NULL; header++)
    {
      if (!velaops_safe_header_text(header->name) ||
          !velaops_safe_header_text(header->value) ||
          strchr(header->name, ':') != NULL ||
          velaops_append(request, sizeof(request), &request_len,
                         "%s: %s\r\n", header->name, header->value) != 0)
        {
          goto cleanup;
        }
    }
  snprintf(content_length, sizeof(content_length), "%lu",
           (unsigned long)body_len);
  if (velaops_append(request, sizeof(request), &request_len,
                     "Content-Length: %s\r\nConnection: close\r\n\r\n",
                     content_length, "") != 0)
    {
      goto cleanup;
    }

  raw_capacity = VELAOPS_HTTP_HEADER_CAPACITY + response_capacity;
  raw = malloc(raw_capacity + 1);
  if (raw == NULL || (fd = velaops_connect(http_context)) < 0 ||
      velaops_send_all(fd, request, request_len) != 0 ||
      velaops_send_all(fd, body, body_len) != 0)
    {
      goto cleanup;
    }
  while (raw_len < raw_capacity)
    {
      ssize_t received = recv(fd, raw + raw_len, raw_capacity - raw_len, 0);
      if (received < 0 && errno == EINTR)
        {
          /* ai_agent 同时运行定时器和网络线程，阻塞接收可被
           * 无关信号打断。信号不等于传输失败，应继续读取。
           */

          continue;
        }
      if (received < 0)
        {
          goto cleanup;
        }
      if (received == 0)
        {
          break;
        }
      raw_len += (size_t)received;
    }
  if (raw_len == raw_capacity)
    {
      goto cleanup;
    }
  raw[raw_len] = '\0';
  status = velaops_parse_response(raw, raw_len, response, response_capacity,
                                  response_len);

cleanup:
  if (fd >= 0)
    {
      close(fd);
    }
  free(raw);
  pthread_mutex_unlock(&g_http_transport_lock);
  return status;
}
