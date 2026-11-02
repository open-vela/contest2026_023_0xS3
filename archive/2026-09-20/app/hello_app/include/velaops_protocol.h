/****************************************************************************
 * VelaOps HMAC v1 协议核心。
 *
 * 本层只负责字段校验、规范串构造和签名，不依赖 HTTP 或业务逻辑。
 ****************************************************************************/

#ifndef VELAOPS_PROTOCOL_H
#define VELAOPS_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VELAOPS_PROTOCOL_VERSION "1"
#define VELAOPS_SIGNATURE_HEX_SIZE 65
#define VELAOPS_SHA256_HEX_SIZE 65
#define VELAOPS_MAX_TARGET_LENGTH 2048

#define VELAOPS_HEADER_VERSION "X-VelaOps-Version"
#define VELAOPS_HEADER_DEVICE_ID "X-VelaOps-Device"
#define VELAOPS_HEADER_REQUEST_ID "X-VelaOps-Request-ID"
#define VELAOPS_HEADER_TIMESTAMP "X-VelaOps-Timestamp"
#define VELAOPS_HEADER_NONCE "X-VelaOps-Nonce"
#define VELAOPS_HEADER_SIGNATURE "X-VelaOps-Signature"

typedef enum
{
  VELAOPS_PROTOCOL_OK = 0,
  VELAOPS_PROTOCOL_INVALID_ARGUMENT,
  VELAOPS_PROTOCOL_INVALID_METADATA,
  VELAOPS_PROTOCOL_INVALID_METHOD,
  VELAOPS_PROTOCOL_INVALID_TARGET,
  VELAOPS_PROTOCOL_SECRET_TOO_SHORT,
  VELAOPS_PROTOCOL_BUFFER_TOO_SMALL,
  VELAOPS_PROTOCOL_CRYPTO_ERROR,
  VELAOPS_PROTOCOL_NO_MEMORY
} velaops_protocol_status_t;

typedef struct
{
  const char *version;
  const char *device_id;
  const char *request_id;
  int64_t timestamp;
  const char *nonce;
} velaops_auth_metadata_t;

/* 返回稳定的英文状态名，方便日志和测试断言。 */
const char *velaops_protocol_status_name(velaops_protocol_status_t status);

velaops_protocol_status_t velaops_body_sha256_hex(
    const uint8_t *body, size_t body_len,
    char output[VELAOPS_SHA256_HEX_SIZE]);

/*
 * 构造不含结尾 NUL 的规范串。output_len 总是返回所需字节数。
 * output 可为 NULL，用于先查询缓冲区大小。
 */
velaops_protocol_status_t velaops_build_canonical_request(
    const char *method, const char *target,
    const uint8_t *body, size_t body_len,
    const velaops_auth_metadata_t *metadata,
    char *output, size_t output_capacity, size_t *output_len);

velaops_protocol_status_t velaops_calculate_signature(
    const uint8_t *secret, size_t secret_len,
    const char *method, const char *target,
    const uint8_t *body, size_t body_len,
    const velaops_auth_metadata_t *metadata,
    char output[VELAOPS_SIGNATURE_HEX_SIZE]);

velaops_protocol_status_t velaops_verify_signature(
    const char *signature,
    const uint8_t *secret, size_t secret_len,
    const char *method, const char *target,
    const uint8_t *body, size_t body_len,
    const velaops_auth_metadata_t *metadata,
    bool *verified);

#endif /* VELAOPS_PROTOCOL_H */
