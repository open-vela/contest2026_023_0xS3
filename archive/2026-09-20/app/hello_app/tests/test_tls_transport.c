/****************************************************************************
 * openvela HTTPS 传输适配主机测试。
 ****************************************************************************/

#include "velaops_proxy_tls_transport.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "infra/vela_tls.h"

#define EXPECTED(value) do { if (!(value)) fail(#value, __LINE__); } while (0)

typedef struct
{
  int status;
  const char *body;
  bool called;
} fake_vela_tls_state_t;

extern fake_vela_tls_state_t g_fake_vela_tls;

static void fail(const char *expression, int line)
{
  fprintf(stderr, "FAIL line %d: %s\n", line, expression);
  exit(EXIT_FAILURE);
}

static void test_tls_adapter(void)
{
  velaops_proxy_tls_context_t context = {
    .host = "proxy.local",
    .port = "8443",
    .ca_pem = "test-ca"
  };
  velaops_http_header_t headers[] = {
    {"Content-Type", "application/json"},
    {NULL, NULL}
  };
  char response[128];
  size_t response_len = 0;
  int status;

  g_fake_vela_tls.status = 200;
  g_fake_vela_tls.body = "{\"ok\":true}";
  g_fake_vela_tls.called = false;
  status = velaops_proxy_tls_transport(
      &context, "POST", "/v1/auth/check", headers,
      (const uint8_t *)"{}", 2, response, sizeof(response), &response_len);
  EXPECTED(status == 200);
  EXPECTED(g_fake_vela_tls.called);
  EXPECTED(response_len == strlen(g_fake_vela_tls.body));
  EXPECTED(strcmp(response, g_fake_vela_tls.body) == 0);
}

static void test_rejects_truncation(void)
{
  velaops_proxy_tls_context_t context = {
    .host = "proxy.local",
    .port = "8443",
    .ca_pem = "test-ca"
  };
  velaops_http_header_t headers[] = {{NULL, NULL}};
  char response[8];
  size_t response_len = 0;

  g_fake_vela_tls.status = 200;
  g_fake_vela_tls.body = "123456789";
  EXPECTED(velaops_proxy_tls_transport(
               &context, "POST", "/v1/auth/check", headers,
               (const uint8_t *)"{}", 2, response, sizeof(response),
               &response_len) == VELA_TLS_ERR_OVERFLOW);
}

int main(void)
{
  test_tls_adapter();
  test_rejects_truncation();
  puts("PASS: VelaOps openvela TLS transport tests");
  return EXIT_SUCCESS;
}
