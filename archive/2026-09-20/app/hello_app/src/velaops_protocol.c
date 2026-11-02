/****************************************************************************
 * VelaOps HMAC v1 协议核心实现。
 ****************************************************************************/

#include "velaops_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "velaops_crypto.h"

#define VELAOPS_ALGORITHM "VELAOPS-HMAC-SHA256"
#define VELAOPS_MIN_SECRET_LENGTH 32
#define VELAOPS_ID_HEX_LENGTH 32
#define VELAOPS_MAX_DEVICE_ID_LENGTH 64

static bool velaops_is_ascii_alnum(char value)
{
  return (value >= '0' && value <= '9') ||
         (value >= 'A' && value <= 'Z') ||
         (value >= 'a' && value <= 'z');
}

static bool velaops_is_lower_hex(char value)
{
  return (value >= '0' && value <= '9') ||
         (value >= 'a' && value <= 'f');
}

static bool velaops_is_fixed_lower_hex(const char *value, size_t length)
{
  size_t index;

  if (value == NULL || strlen(value) != length)
    {
      return false;
    }

  for (index = 0; index < length; index++)
    {
      if (!velaops_is_lower_hex(value[index]))
        {
          return false;
        }
    }

  return true;
}

static bool velaops_valid_device_id(const char *device_id)
{
  size_t index;
  size_t length;

  if (device_id == NULL)
    {
      return false;
    }

  length = strlen(device_id);
  if (length == 0 || length > VELAOPS_MAX_DEVICE_ID_LENGTH ||
      !velaops_is_ascii_alnum(device_id[0]))
    {
      return false;
    }

  for (index = 1; index < length; index++)
    {
      char value = device_id[index];
      if (!velaops_is_ascii_alnum(value) && value != '.' && value != '_' &&
          value != '-')
        {
          return false;
        }
    }

  return true;
}

static velaops_protocol_status_t velaops_validate_metadata(
    const velaops_auth_metadata_t *metadata)
{
  if (metadata == NULL || metadata->version == NULL ||
      strcmp(metadata->version, VELAOPS_PROTOCOL_VERSION) != 0 ||
      !velaops_valid_device_id(metadata->device_id) ||
      !velaops_is_fixed_lower_hex(metadata->request_id,
                                  VELAOPS_ID_HEX_LENGTH) ||
      !velaops_is_fixed_lower_hex(metadata->nonce, VELAOPS_ID_HEX_LENGTH) ||
      metadata->timestamp < 0)
    {
      return VELAOPS_PROTOCOL_INVALID_METADATA;
    }

  return VELAOPS_PROTOCOL_OK;
}

static bool velaops_valid_method(const char *method)
{
  const unsigned char *cursor = (const unsigned char *)method;

  if (cursor == NULL || *cursor == '\0')
    {
      return false;
    }

  while (*cursor != '\0')
    {
      if (*cursor < 'A' || *cursor > 'Z')
        {
          return false;
        }
      cursor++;
    }

  return true;
}

static bool velaops_valid_target(const char *target)
{
  size_t index;
  size_t length;

  if (target == NULL || target[0] != '/')
    {
      return false;
    }

  length = strlen(target);
  if (length > VELAOPS_MAX_TARGET_LENGTH)
    {
      return false;
    }

  for (index = 0; index < length; index++)
    {
      unsigned char value = (unsigned char)target[index];
      if (value > 0x7f || value == '\r' || value == '\n' || value == '#')
        {
          return false;
        }
    }

  return true;
}

static void velaops_hex_encode(const uint8_t *input, size_t input_len,
                               char *output)
{
  static const char digits[] = "0123456789abcdef";
  size_t index;

  for (index = 0; index < input_len; index++)
    {
      output[index * 2] = digits[input[index] >> 4];
      output[index * 2 + 1] = digits[input[index] & 0x0f];
    }
  output[input_len * 2] = '\0';
}

const char *velaops_protocol_status_name(velaops_protocol_status_t status)
{
  static const char *const names[] = {
    "ok",
    "invalid_argument",
    "invalid_metadata",
    "invalid_method",
    "invalid_target",
    "secret_too_short",
    "buffer_too_small",
    "crypto_error",
    "no_memory"
  };

  if ((unsigned int)status >= sizeof(names) / sizeof(names[0]))
    {
      return "unknown";
    }
  return names[status];
}

velaops_protocol_status_t velaops_body_sha256_hex(
    const uint8_t *body, size_t body_len,
    char output[VELAOPS_SHA256_HEX_SIZE])
{
  uint8_t digest[VELAOPS_SHA256_SIZE];

  if (output == NULL || (body == NULL && body_len != 0))
    {
      return VELAOPS_PROTOCOL_INVALID_ARGUMENT;
    }
  if (velaops_crypto_sha256(body, body_len, digest) != 0)
    {
      return VELAOPS_PROTOCOL_CRYPTO_ERROR;
    }

  velaops_hex_encode(digest, sizeof(digest), output);
  return VELAOPS_PROTOCOL_OK;
}

velaops_protocol_status_t velaops_build_canonical_request(
    const char *method, const char *target,
    const uint8_t *body, size_t body_len,
    const velaops_auth_metadata_t *metadata,
    char *output, size_t output_capacity, size_t *output_len)
{
  char body_hash[VELAOPS_SHA256_HEX_SIZE];
  char timestamp[24];
  size_t required;
  int timestamp_length;
  velaops_protocol_status_t status;

  if (output_len == NULL || (body == NULL && body_len != 0))
    {
      return VELAOPS_PROTOCOL_INVALID_ARGUMENT;
    }
  status = velaops_validate_metadata(metadata);
  if (status != VELAOPS_PROTOCOL_OK)
    {
      return status;
    }
  if (!velaops_valid_method(method))
    {
      return VELAOPS_PROTOCOL_INVALID_METHOD;
    }
  if (!velaops_valid_target(target))
    {
      return VELAOPS_PROTOCOL_INVALID_TARGET;
    }

  status = velaops_body_sha256_hex(body, body_len, body_hash);
  if (status != VELAOPS_PROTOCOL_OK)
    {
      return status;
    }
  timestamp_length = snprintf(timestamp, sizeof(timestamp), "%lld",
                              (long long)metadata->timestamp);
  if (timestamp_length < 0 || (size_t)timestamp_length >= sizeof(timestamp))
    {
      return VELAOPS_PROTOCOL_INVALID_METADATA;
    }

  /* 8 个换行分隔 9 个字段，结尾不加换行。 */
  required = strlen(VELAOPS_ALGORITHM) + strlen(metadata->version) +
             strlen(metadata->device_id) + strlen(metadata->request_id) +
             (size_t)timestamp_length + strlen(metadata->nonce) +
             strlen(method) + strlen(target) + strlen(body_hash) + 8;
  *output_len = required;

  if (output == NULL || output_capacity <= required)
    {
      return VELAOPS_PROTOCOL_BUFFER_TOO_SMALL;
    }

  if (snprintf(output, output_capacity,
               "%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s",
               VELAOPS_ALGORITHM, metadata->version, metadata->device_id,
               metadata->request_id, timestamp, metadata->nonce, method,
               target, body_hash) != (int)required)
    {
      return VELAOPS_PROTOCOL_BUFFER_TOO_SMALL;
    }

  return VELAOPS_PROTOCOL_OK;
}

velaops_protocol_status_t velaops_calculate_signature(
    const uint8_t *secret, size_t secret_len,
    const char *method, const char *target,
    const uint8_t *body, size_t body_len,
    const velaops_auth_metadata_t *metadata,
    char output[VELAOPS_SIGNATURE_HEX_SIZE])
{
  char *canonical;
  size_t canonical_len = 0;
  uint8_t digest[VELAOPS_SHA256_SIZE];
  velaops_protocol_status_t status;

  if (output == NULL || secret == NULL)
    {
      return VELAOPS_PROTOCOL_INVALID_ARGUMENT;
    }
  if (secret_len < VELAOPS_MIN_SECRET_LENGTH)
    {
      return VELAOPS_PROTOCOL_SECRET_TOO_SHORT;
    }

  status = velaops_build_canonical_request(method, target, body, body_len,
                                            metadata, NULL, 0,
                                            &canonical_len);
  if (status != VELAOPS_PROTOCOL_BUFFER_TOO_SMALL)
    {
      return status;
    }

  canonical = (char *)malloc(canonical_len + 1);
  if (canonical == NULL)
    {
      return VELAOPS_PROTOCOL_NO_MEMORY;
    }
  status = velaops_build_canonical_request(method, target, body, body_len,
                                            metadata, canonical,
                                            canonical_len + 1,
                                            &canonical_len);
  if (status == VELAOPS_PROTOCOL_OK &&
      velaops_crypto_hmac_sha256(secret, secret_len,
                                 (const uint8_t *)canonical, canonical_len,
                                 digest) != 0)
    {
      status = VELAOPS_PROTOCOL_CRYPTO_ERROR;
    }
  free(canonical);

  if (status == VELAOPS_PROTOCOL_OK)
    {
      velaops_hex_encode(digest, sizeof(digest), output);
    }
  return status;
}

velaops_protocol_status_t velaops_verify_signature(
    const char *signature,
    const uint8_t *secret, size_t secret_len,
    const char *method, const char *target,
    const uint8_t *body, size_t body_len,
    const velaops_auth_metadata_t *metadata,
    bool *verified)
{
  char expected[VELAOPS_SIGNATURE_HEX_SIZE];
  unsigned char difference = 0;
  size_t index;
  velaops_protocol_status_t status;

  if (verified == NULL ||
      !velaops_is_fixed_lower_hex(signature, VELAOPS_SHA256_SIZE * 2))
    {
      return VELAOPS_PROTOCOL_INVALID_ARGUMENT;
    }
  *verified = false;

  status = velaops_calculate_signature(secret, secret_len, method, target,
                                       body, body_len, metadata, expected);
  if (status != VELAOPS_PROTOCOL_OK)
    {
      return status;
    }

  /* 不在第一个不同字节处退出，避免签名比较泄露时序信息。 */
  for (index = 0; index < VELAOPS_SHA256_SIZE * 2; index++)
    {
      difference |= (unsigned char)(signature[index] ^ expected[index]);
    }
  *verified = difference == 0;
  return VELAOPS_PROTOCOL_OK;
}
