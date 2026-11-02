/****************************************************************************
 * VelaOps Proxy API Client 主机单元测试。
 ****************************************************************************/

#include "velaops_proxy_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPECTED(value) do { if (!(value)) fail(#value, __LINE__); } while (0)

typedef struct
{
  int http_status;
  const char *response;
  const char *expected_target;
  const char *expected_body;
  bool request_checked;
} mock_transport_t;

static const uint8_t secret[] = "test-secret-0123456789abcdefABCD";
static const char request_body[] = "{}";
static const velaops_auth_metadata_t metadata = {
  .version = "1",
  .device_id = "eye-001",
  .request_id = "0123456789abcdef0123456789abcdef",
  .timestamp = 1787582400,
  .nonce = "abcdef0123456789abcdef0123456789"
};

static void fail(const char *expression, int line)
{
  fprintf(stderr, "FAIL line %d: %s\n", line, expression);
  exit(EXIT_FAILURE);
}

static const char *find_header(const velaops_http_header_t *headers,
                               const char *name)
{
  while (headers->name != NULL)
    {
      if (strcmp(headers->name, name) == 0)
        {
          return headers->value;
        }
      headers++;
    }
  return NULL;
}

static int mock_transport(void *context, const char *method, const char *target,
                          const velaops_http_header_t *headers,
                          const uint8_t *body, size_t body_len,
                          char *response, size_t response_capacity,
                          size_t *response_len)
{
  mock_transport_t *mock = context;
  const char *expected_target = mock->expected_target != NULL ?
                                mock->expected_target : "/v1/auth/check";
  const char *expected_body = mock->expected_body != NULL ?
                              mock->expected_body : request_body;
  size_t length = strlen(mock->response);

  EXPECTED(strcmp(method, "POST") == 0);
  EXPECTED(strcmp(target, expected_target) == 0);
  EXPECTED(body_len == strlen(expected_body));
  EXPECTED(memcmp(body, expected_body, body_len) == 0);
  EXPECTED(strcmp(find_header(headers, "Content-Type"), "application/json") == 0);
  EXPECTED(strcmp(find_header(headers, VELAOPS_HEADER_DEVICE_ID), "eye-001") == 0);
  EXPECTED(strcmp(find_header(headers, VELAOPS_HEADER_TIMESTAMP), "1787582400") == 0);
  EXPECTED(strlen(find_header(headers, VELAOPS_HEADER_SIGNATURE)) == 64);
  EXPECTED(length < response_capacity);

  memcpy(response, mock->response, length);
  *response_len = length;
  mock->request_checked = true;
  return mock->http_status;
}

static velaops_proxy_client_t make_client(mock_transport_t *mock)
{
  velaops_proxy_client_t client = {
    .device_id = "eye-001",
    .secret = secret,
    .secret_len = sizeof(secret) - 1,
    .transport = mock_transport,
    .transport_context = mock
  };
  return client;
}

static void test_success_response(void)
{
  mock_transport_t mock = {
    .http_status = 200,
    .response = "{\"schema_version\":1,\"request_id\":"
                "\"0123456789abcdef0123456789abcdef\",\"ok\":true,"
                "\"result\":{\"device_id\":\"eye-001\"}}"
  };
  velaops_proxy_client_t client = make_client(&mock);
  velaops_proxy_response_t response;

  EXPECTED(velaops_proxy_client_post_json(
               &client, "/v1/auth/check", (const uint8_t *)request_body,
               strlen(request_body), &metadata, &response) ==
           VELAOPS_PROXY_CLIENT_OK);
  EXPECTED(mock.request_checked);
  EXPECTED(response.http_status == 200);
  EXPECTED(response.ok);
  EXPECTED(response.has_request_id);
  EXPECTED(strcmp(response.result_json, "{\"device_id\":\"eye-001\"}") == 0);
}

static void test_structured_error(void)
{
  mock_transport_t mock = {
    .http_status = 401,
    .response = "{\"schema_version\":1,\"ok\":false,\"error\":{"
                "\"code\":\"invalid_auth\",\"message\":\"请求认证失败\","
                "\"retryable\":false}}"
  };
  velaops_proxy_client_t client = make_client(&mock);
  velaops_proxy_response_t response;

  EXPECTED(velaops_proxy_client_post_json(
               &client, "/v1/auth/check", (const uint8_t *)request_body,
               strlen(request_body), &metadata, &response) ==
           VELAOPS_PROXY_CLIENT_OK);
  EXPECTED(!response.ok);
  EXPECTED(!response.retryable);
  EXPECTED(strcmp(response.error_code, "invalid_auth") == 0);
}

static void test_action_request_and_result(void)
{
  static const char action_body[] =
      "{\"schema_version\":1,\"action\":\"check_memory\","
      "\"target\":\"local-dev\",\"parameters\":{}}";
  mock_transport_t mock = {
    .http_status = 200,
    .response = "{\"schema_version\":1,\"request_id\":"
                "\"0123456789abcdef0123456789abcdef\",\"ok\":true,"
                "\"result\":{\"used_percent\":42.5}}",
    .expected_target = "/v1/actions/execute",
    .expected_body = action_body
  };
  velaops_proxy_client_t client = make_client(&mock);
  velaops_proxy_response_t response;

  EXPECTED(velaops_proxy_client_post_json(
               &client, mock.expected_target, (const uint8_t *)action_body,
               strlen(action_body), &metadata, &response) ==
           VELAOPS_PROXY_CLIENT_OK);
  EXPECTED(mock.request_checked);
  EXPECTED(response.ok);
  EXPECTED(strcmp(response.result_json, "{\"used_percent\":42.5}") == 0);
}

static void test_rejects_untrusted_response(void)
{
  mock_transport_t mock = {
    .http_status = 200,
    .response = "{\"schema_version\":1,\"request_id\":"
                "\"ffffffffffffffffffffffffffffffff\",\"ok\":true,"
                "\"result\":{}}"
  };
  velaops_proxy_client_t client = make_client(&mock);
  velaops_proxy_response_t response;

  EXPECTED(velaops_proxy_client_post_json(
               &client, "/v1/auth/check", (const uint8_t *)request_body,
               strlen(request_body), &metadata, &response) ==
           VELAOPS_PROXY_CLIENT_INVALID_RESPONSE);

  mock.response = "{\"schema_version\":2,\"ok\":true,\"result\":{}}";
  EXPECTED(velaops_proxy_client_post_json(
               &client, "/v1/auth/check", (const uint8_t *)request_body,
               strlen(request_body), &metadata, &response) ==
           VELAOPS_PROXY_CLIENT_INVALID_RESPONSE);

  mock.http_status = 200;
  mock.response = "{\"schema_version\":1,\"ok\":false,\"error\":{"
                  "\"code\":\"invalid_auth\",\"message\":\"bad\","
                  "\"retryable\":false}}";
  EXPECTED(velaops_proxy_client_post_json(
               &client, "/v1/auth/check", (const uint8_t *)request_body,
               strlen(request_body), &metadata, &response) ==
           VELAOPS_PROXY_CLIENT_INVALID_RESPONSE);
}

static void test_transport_failure(void)
{
  mock_transport_t mock = {.http_status = -2, .response = ""};
  velaops_proxy_client_t client = make_client(&mock);
  velaops_proxy_response_t response;

  EXPECTED(velaops_proxy_client_post_json(
               &client, "/v1/auth/check", (const uint8_t *)request_body,
               strlen(request_body), &metadata, &response) ==
           VELAOPS_PROXY_CLIENT_TRANSPORT_ERROR);
}

int main(void)
{
  test_success_response();
  test_structured_error();
  test_action_request_and_result();
  test_rejects_untrusted_response();
  test_transport_failure();
  puts("PASS: VelaOps Proxy API Client tests");
  return EXIT_SUCCESS;
}
