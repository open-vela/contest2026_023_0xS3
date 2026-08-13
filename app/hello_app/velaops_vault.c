#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>

#include <mbedtls/aes.h>
#include <mbedtls/gcm.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>

#include "velaops.h"

#define VAULT_MAGIC 0x31534f56u /* VOS1 */
#define VAULT_ITERATIONS 20000

struct vault_file
{
  uint32_t magic;
  uint32_t iterations;
  uint8_t salt[16];
  uint8_t nonce[12];
  uint8_t tag[16];
  uint8_t ciphertext[sizeof(struct velaops_profile)];
};

static int derive_key(const char *pin, const uint8_t *salt,
                      uint32_t iterations, uint8_t key[32])
{
  int ret = mbedtls_pkcs5_pbkdf2_hmac_ext(
    MBEDTLS_MD_SHA256,
    (const unsigned char *)pin, strlen(pin),
    salt, 16,
    iterations,
    32,
    key);
  return ret == 0 ? 0 : -1;
}

int velaops_vault_save(const char *path, const struct velaops_profile *profile,
                       const char *pin)
{
  struct vault_file file;
  mbedtls_gcm_context gcm;
  uint8_t key[32];
  int fd = -1;
  int ret = -1;

  memset(&file, 0, sizeof(file));
  file.magic = VAULT_MAGIC;
  file.iterations = VAULT_ITERATIONS;
  if (getrandom(file.salt, sizeof(file.salt), 0) != sizeof(file.salt) ||
      getrandom(file.nonce, sizeof(file.nonce), 0) != sizeof(file.nonce) ||
      derive_key(pin, file.salt, file.iterations, key) != 0)
    goto out;

  mbedtls_gcm_init(&gcm);
  if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256) == 0 &&
      mbedtls_gcm_crypt_and_tag(
        &gcm, MBEDTLS_GCM_ENCRYPT,
        sizeof(*profile),
        file.nonce, sizeof(file.nonce),
        NULL, 0,
        (const unsigned char *)profile,
        file.ciphertext,
        sizeof(file.tag), file.tag) == 0)
    {
      fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
      if (fd >= 0 && write(fd, &file, sizeof(file)) == sizeof(file)) ret = 0;
    }
  mbedtls_gcm_free(&gcm);
out:
  if (fd >= 0) close(fd);
  velaops_secure_zero(key, sizeof(key));
  velaops_secure_zero(&file, sizeof(file));
  return ret;
}

int velaops_vault_load(const char *path, struct velaops_profile *profile,
                       const char *pin)
{
  struct vault_file file;
  mbedtls_gcm_context gcm;
  uint8_t key[32];
  int fd = open(path, O_RDONLY);
  int ret = -1;

  if (fd < 0 || read(fd, &file, sizeof(file)) != sizeof(file) ||
      file.magic != VAULT_MAGIC || file.iterations != VAULT_ITERATIONS ||
      derive_key(pin, file.salt, file.iterations, key) != 0)
    goto out;

  mbedtls_gcm_init(&gcm);
  if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256) == 0 &&
      mbedtls_gcm_auth_decrypt(
        &gcm,
        sizeof(*profile),
        file.nonce, sizeof(file.nonce),
        NULL, 0,
        file.tag, sizeof(file.tag),
        file.ciphertext,
        (unsigned char *)profile) == 0)
    ret = 0;
  mbedtls_gcm_free(&gcm);
out:
  if (fd >= 0) close(fd);
  velaops_secure_zero(key, sizeof(key));
  velaops_secure_zero(&file, sizeof(file));
  return ret;
}
