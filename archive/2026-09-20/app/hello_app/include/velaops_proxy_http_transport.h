/****************************************************************************
 * VelaOps Proxy Client 的局域网 HTTP 传输适配。
 ****************************************************************************/

#ifndef VELAOPS_PROXY_HTTP_TRANSPORT_H
#define VELAOPS_PROXY_HTTP_TRANSPORT_H

#include "velaops_proxy_client.h"

typedef struct
{
  const char *host;
  const char *port;
  int timeout_seconds;
} velaops_proxy_http_context_t;

/* 成功时返回 HTTP 状态码，失败时返回负数。 */
int velaops_proxy_http_transport(
    void *context, const char *method, const char *target,
    const velaops_http_header_t *headers,
    const uint8_t *body, size_t body_len,
    char *response, size_t response_capacity, size_t *response_len);

#endif /* VELAOPS_PROXY_HTTP_TRANSPORT_H */
