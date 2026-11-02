/****************************************************************************
 * VelaOps Proxy Client 的 openvela HTTPS 传输适配。
 ****************************************************************************/

#ifndef VELAOPS_PROXY_TLS_TRANSPORT_H
#define VELAOPS_PROXY_TLS_TRANSPORT_H

#include "velaops_proxy_client.h"

typedef struct
{
  const char *host;
  const char *port;
  const char *ca_pem;
} velaops_proxy_tls_context_t;

int velaops_proxy_tls_transport(
    void *context, const char *method, const char *target,
    const velaops_http_header_t *headers,
    const uint8_t *body, size_t body_len,
    char *response, size_t response_capacity, size_t *response_len);

#endif /* VELAOPS_PROXY_TLS_TRANSPORT_H */
