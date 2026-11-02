/****************************************************************************
 * VelaOps Proxy API Client。
 *
 * 请求签名、协议响应解析与 HTTPS 传输解耦。传输层由调用者注入。
 ****************************************************************************/

#ifndef VELAOPS_PROXY_CLIENT_H
#define VELAOPS_PROXY_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "velaops_protocol.h"

#define VELAOPS_PROXY_RESPONSE_CAPACITY 4096
#define VELAOPS_PROXY_RESULT_CAPACITY 2048
#define VELAOPS_PROXY_ERROR_CODE_CAPACITY 64
#define VELAOPS_PROXY_ERROR_MESSAGE_CAPACITY 192

typedef struct
{
  const char *name;
  const char *value;
} velaops_http_header_t;

/* 成功时返回 HTTP 状态码，传输失败时返回负数。 */
typedef int (*velaops_proxy_transport_t)(
    void *context, const char *method, const char *target,
    const velaops_http_header_t *headers,
    const uint8_t *body, size_t body_len,
    char *response, size_t response_capacity, size_t *response_len);

typedef enum
{
  VELAOPS_PROXY_CLIENT_OK = 0,
  VELAOPS_PROXY_CLIENT_INVALID_ARGUMENT,
  VELAOPS_PROXY_CLIENT_PROTOCOL_ERROR,
  VELAOPS_PROXY_CLIENT_TRANSPORT_ERROR,
  VELAOPS_PROXY_CLIENT_RESPONSE_TOO_LARGE,
  VELAOPS_PROXY_CLIENT_INVALID_RESPONSE
} velaops_proxy_client_status_t;

typedef struct
{
  const char *device_id;
  const uint8_t *secret;
  size_t secret_len;
  velaops_proxy_transport_t transport;
  void *transport_context;
} velaops_proxy_client_t;

typedef struct
{
  int http_status;
  bool ok;
  bool retryable;
  bool has_request_id;
  char request_id[33];
  char error_code[VELAOPS_PROXY_ERROR_CODE_CAPACITY];
  char error_message[VELAOPS_PROXY_ERROR_MESSAGE_CAPACITY];
  char result_json[VELAOPS_PROXY_RESULT_CAPACITY];
} velaops_proxy_response_t;

const char *velaops_proxy_client_status_name(
    velaops_proxy_client_status_t status);

/* 发送已签名 JSON POST，并严格校验 Proxy v1 响应包络。 */
velaops_proxy_client_status_t velaops_proxy_client_post_json(
    const velaops_proxy_client_t *client,
    const char *target,
    const uint8_t *body, size_t body_len,
    const velaops_auth_metadata_t *metadata,
    velaops_proxy_response_t *response);

#endif /* VELAOPS_PROXY_CLIENT_H */
