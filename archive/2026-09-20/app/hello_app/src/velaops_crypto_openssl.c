/****************************************************************************
 * 主机测试加密适配，不参与 openvela 固件编译。
 ****************************************************************************/

#include "velaops_crypto.h"

#include <limits.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

int velaops_crypto_sha256(const uint8_t *data, size_t data_len,
                          uint8_t output[VELAOPS_SHA256_SIZE])
{
  unsigned int output_len = 0;
  const uint8_t empty = 0;

  if (output == NULL || data_len > UINT_MAX ||
      EVP_Digest(data != NULL ? data : &empty, data_len, output, &output_len,
                 EVP_sha256(), NULL) != 1)
    {
      return -1;
    }
  return output_len == VELAOPS_SHA256_SIZE ? 0 : -1;
}

int velaops_crypto_hmac_sha256(const uint8_t *key, size_t key_len,
                               const uint8_t *data, size_t data_len,
                               uint8_t output[VELAOPS_SHA256_SIZE])
{
  unsigned int output_len = 0;
  const uint8_t empty = 0;

  if (key == NULL || output == NULL || key_len > INT_MAX ||
      HMAC(EVP_sha256(), key, (int)key_len,
           data != NULL ? data : &empty, data_len,
           output, &output_len) == NULL)
    {
      return -1;
    }
  return output_len == VELAOPS_SHA256_SIZE ? 0 : -1;
}
