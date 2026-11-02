/****************************************************************************
 * VelaOps 加密适配层：目标端和主机测试分别提供实现。
 ****************************************************************************/

#ifndef VELAOPS_CRYPTO_H
#define VELAOPS_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#define VELAOPS_SHA256_SIZE 32

int velaops_crypto_sha256(const uint8_t *data, size_t data_len,
                          uint8_t output[VELAOPS_SHA256_SIZE]);

int velaops_crypto_hmac_sha256(const uint8_t *key, size_t key_len,
                               const uint8_t *data, size_t data_len,
                               uint8_t output[VELAOPS_SHA256_SIZE]);

#endif /* VELAOPS_CRYPTO_H */
