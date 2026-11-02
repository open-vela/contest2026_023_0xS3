/****************************************************************************
 * VelaOps Proxy Client 的 openvela HTTPS 传输适配。
 ****************************************************************************/

#include "velaops_proxy_tls_transport.h"

#include <stddef.h>

#include "infra/vela_tls.h"

#define VELAOPS_MAX_HTTP_HEADERS 16

int velaops_proxy_tls_transport(
    void *context, const char *method, const char *target,
    const velaops_http_header_t *headers,
    const uint8_t *body, size_t body_len,
    char *response, size_t response_capacity, size_t *response_len)
{
  const velaops_proxy_tls_context_t *tls_context = context;
  vela_header_t tls_headers[VELAOPS_MAX_HTTP_HEADERS + 1];
  size_t header_count = 0;
  size_t received = 0;
  int status;

  if (tls_context == NULL || tls_context->host == NULL ||
      tls_context->port == NULL || tls_context->ca_pem == NULL ||
      tls_context->ca_pem[0] == '\0' || method == NULL || target == NULL ||
      headers == NULL || response == NULL || response_capacity < 2 ||
      response_len == NULL || (body == NULL && body_len != 0))
    {
      return VELA_TLS_ERR_CONNECT;
    }

  while (headers[header_count].name != NULL)
    {
      if (header_count >= VELAOPS_MAX_HTTP_HEADERS ||
          headers[header_count].value == NULL)
        {
          return VELA_TLS_ERR_OVERFLOW;
        }
      tls_headers[header_count].name = headers[header_count].name;
      tls_headers[header_count].value = headers[header_count].value;
      header_count++;
    }
  tls_headers[header_count].name = NULL;
  tls_headers[header_count].value = NULL;

  status = vela_https_request_with_ca(
      tls_context->host, tls_context->port, method, target, tls_headers,
      (const char *)body, body_len, tls_context->ca_pem,
      response, response_capacity, &received);
  if (status < 0)
    {
      return status;
    }

  /* vela_tls 会截断过大响应；宁可拒绝边界长度，也不解析半截 JSON。 */
  if (received >= response_capacity - 1)
    {
      return VELA_TLS_ERR_OVERFLOW;
    }
  *response_len = received;
  return status;
}
