/****************************************************************************
 * openvela 目标端 mbedTLS 加密适配。
 ****************************************************************************/

#include "velaops_crypto.h"

#include <mbedtls/md.h>

int velaops_crypto_sha256(const uint8_t *data, size_t data_len,
                          uint8_t output[VELAOPS_SHA256_SIZE])
{
  const mbedtls_md_info_t *info;
  const uint8_t empty = 0;

  info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (info == NULL || output == NULL)
    {
      return -1;
    }
  return mbedtls_md(info, data != NULL ? data : &empty, data_len, output);
}

int velaops_crypto_hmac_sha256(const uint8_t *key, size_t key_len,
                               const uint8_t *data, size_t data_len,
                               uint8_t output[VELAOPS_SHA256_SIZE])
{
  const mbedtls_md_info_t *info;
  const uint8_t empty = 0;

  info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (info == NULL || key == NULL || output == NULL)
    {
      return -1;
    }
  return mbedtls_md_hmac(info, key, key_len,
                         data != NULL ? data : &empty, data_len, output);
}
