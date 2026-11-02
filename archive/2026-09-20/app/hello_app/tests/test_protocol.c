/****************************************************************************
 * VelaOps HMAC v1 主机单元测试。
 ****************************************************************************/

#include "velaops_protocol.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPECTED(value) do { if (!(value)) fail(#value, __LINE__); } while (0)

static const char secret[] = "test-secret-0123456789abcdefABCD";
static const char body[] = "{\"action\":\"check_service\",\"target\":\"server-01\"}";
static const char expected_hash[] =
    "0bf6a4efbdd2cbe3659f712ede0a276c3bc1a30ac9061bdf84a5940d1eda3bd2";
static const char expected_canonical[] =
    "VELAOPS-HMAC-SHA256\n"
    "1\n"
    "eye-001\n"
    "0123456789abcdef0123456789abcdef\n"
    "1787582400\n"
    "abcdef0123456789abcdef0123456789\n"
    "POST\n"
    "/v1/actions/execute?dry_run=false\n"
    "0bf6a4efbdd2cbe3659f712ede0a276c3bc1a30ac9061bdf84a5940d1eda3bd2";
static const char expected_signature[] =
    "2f6508ab642c4924b0544540a3342e5293e66a1cec28c82b49129a7231a877a3";

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

static void test_fixed_vector(void)
{
  char hash[VELAOPS_SHA256_HEX_SIZE];
  char signature[VELAOPS_SIGNATURE_HEX_SIZE];
  char canonical[512];
  size_t canonical_len = 0;

  EXPECTED(velaops_body_sha256_hex((const uint8_t *)body, strlen(body), hash) ==
           VELAOPS_PROTOCOL_OK);
  EXPECTED(strcmp(hash, expected_hash) == 0);
  EXPECTED(velaops_build_canonical_request(
               "POST", "/v1/actions/execute?dry_run=false",
               (const uint8_t *)body, strlen(body), &metadata,
               canonical, sizeof(canonical), &canonical_len) ==
           VELAOPS_PROTOCOL_OK);
  EXPECTED(canonical_len == strlen(expected_canonical));
  EXPECTED(strcmp(canonical, expected_canonical) == 0);
  EXPECTED(velaops_calculate_signature(
               (const uint8_t *)secret, strlen(secret), "POST",
               "/v1/actions/execute?dry_run=false",
               (const uint8_t *)body, strlen(body), &metadata, signature) ==
           VELAOPS_PROTOCOL_OK);
  EXPECTED(strcmp(signature, expected_signature) == 0);
}

static void test_verification_and_tampering(void)
{
  bool verified = false;
  const char tampered[] =
      "{\"action\":\"check_service\",\"target\":\"server-01\"} ";

  EXPECTED(velaops_verify_signature(
               expected_signature, (const uint8_t *)secret, strlen(secret),
               "POST", "/v1/actions/execute?dry_run=false",
               (const uint8_t *)body, strlen(body), &metadata, &verified) ==
           VELAOPS_PROTOCOL_OK);
  EXPECTED(verified);
  EXPECTED(velaops_verify_signature(
               expected_signature, (const uint8_t *)secret, strlen(secret),
               "POST", "/v1/actions/execute?dry_run=false",
               (const uint8_t *)tampered, strlen(tampered), &metadata,
               &verified) == VELAOPS_PROTOCOL_OK);
  EXPECTED(!verified);
}

static void test_validation_boundaries(void)
{
  char signature[VELAOPS_SIGNATURE_HEX_SIZE];
  size_t required = 0;
  velaops_auth_metadata_t invalid = metadata;

  EXPECTED(velaops_calculate_signature(
               (const uint8_t *)"short", 5, "POST", "/v1/actions",
               NULL, 0, &metadata, signature) ==
           VELAOPS_PROTOCOL_SECRET_TOO_SHORT);
  EXPECTED(velaops_build_canonical_request(
               "post", "/v1/actions", NULL, 0, &metadata,
               NULL, 0, &required) == VELAOPS_PROTOCOL_INVALID_METHOD);
  EXPECTED(velaops_build_canonical_request(
               "POST", "/v1/actions\nforged", NULL, 0, &metadata,
               NULL, 0, &required) == VELAOPS_PROTOCOL_INVALID_TARGET);
  EXPECTED(velaops_build_canonical_request(
               "POST", "/v1/actions#fragment", NULL, 0, &metadata,
               NULL, 0, &required) == VELAOPS_PROTOCOL_INVALID_TARGET);

  invalid.request_id = "ABCDEF0123456789abcdef0123456789";
  EXPECTED(velaops_build_canonical_request(
               "POST", "/v1/actions", NULL, 0, &invalid,
               NULL, 0, &required) == VELAOPS_PROTOCOL_INVALID_METADATA);
}

int main(void)
{
  test_fixed_vector();
  test_verification_and_tampering();
  test_validation_boundaries();
  puts("PASS: VelaOps HMAC v1 C protocol tests");
  return EXIT_SUCCESS;
}
