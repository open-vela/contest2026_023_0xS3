/****************************************************************************
 * 主机测试用 vela_tls 假实现。
 ****************************************************************************/

#include "infra/vela_tls.h"

#include <stdbool.h>
#include <string.h>

typedef struct
{
  int status;
  const char *body;
  bool called;
} fake_vela_tls_state_t;

fake_vela_tls_state_t g_fake_vela_tls;

int vela_https_request_with_ca(
    const char *host, const char *port, const char *method,
    const char *path, const vela_header_t *headers,
    const char *body, size_t body_len, const char *ca_pem,
    char *resp_buf, size_t resp_cap, size_t *out_body_len)
{
  size_t length = strlen(g_fake_vela_tls.body);

  if (strcmp(host, "proxy.local") != 0 || strcmp(port, "8443") != 0 ||
      strcmp(method, "POST") != 0 || strcmp(path, "/v1/auth/check") != 0 ||
      headers == NULL || body == NULL || body_len != 2 ||
      ca_pem == NULL || strcmp(ca_pem, "test-ca") != 0 ||
      memcmp(body, "{}", 2) != 0)
    {
      return VELA_TLS_ERR_WRITE;
    }
  if (length >= resp_cap)
    {
      length = resp_cap - 1;
    }
  memcpy(resp_buf, g_fake_vela_tls.body, length);
  resp_buf[length] = '\0';
  if (out_body_len != NULL)
    {
      *out_body_len = length;
    }
  g_fake_vela_tls.called = true;
  return g_fake_vela_tls.status;
}
