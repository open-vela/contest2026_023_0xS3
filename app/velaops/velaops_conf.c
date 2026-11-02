/****************************************************************************
 * apps/examples/velaops/velaops_conf.c
 *
 * Configuration store kept in two raw flash erase blocks with CRC.
 *
 * Layout (per slot, one erase block):
 *   0x00  'V' 'L' 'O' 'P'
 *   0x04  sequence number (little endian)
 *   0x08  payload length   (little endian, 16 bit)
 *   0x0A  crc32 of payload (little endian)
 *   0x0E  reserved
 *   0x10  payload (text, "key=value\n" lines)
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <nuttx/mtd/mtd.h>

#include "velaops_conf.h"

#ifndef CONFIG_ESP32S3_STORAGE_MTD_OFFSET
#  define CONFIG_ESP32S3_STORAGE_MTD_OFFSET 0x180000
#endif

#ifndef CONFIG_ESP32S3_STORAGE_MTD_SIZE
#  define CONFIG_ESP32S3_STORAGE_MTD_SIZE 0x100000
#endif

/* Defined in arch/xtensa/src/esp32s3/esp32s3_spiflash_mtd.h */

struct mtd_dev_s *esp32s3_spiflash_alloc_mtdpart(uint32_t mtd_offset,
                                                 uint32_t mtd_size,
                                                 bool encrypted);

#define CONF_MAGIC      "VLOP"
#define CONF_HDR_SIZE   16
#define CONF_MAX_PAYLOAD 1000
#define CONF_SLOT_COUNT 2
#define CONF_FLAG_BLOCK 2

#define FLAG_MAGIC      "VLFL"

static uint32_t conf_crc32(const uint8_t *data, size_t len)
{
  uint32_t crc = 0xffffffffu;
  size_t i;
  int bit;

  for (i = 0; i < len; i++)
    {
      crc ^= data[i];

      for (bit = 0; bit < 8; bit++)
        {
          if (crc & 1u)
            {
              crc = (crc >> 1) ^ 0xedb88320u;
            }
          else
            {
              crc >>= 1;
            }
        }
    }

  return ~crc;
}

static struct mtd_dev_s *conf_mtd(void)
{
  static struct mtd_dev_s *mtd;

  if (mtd == NULL)
    {
      mtd = esp32s3_spiflash_alloc_mtdpart(CONFIG_ESP32S3_STORAGE_MTD_OFFSET,
                                           CONFIG_ESP32S3_STORAGE_MTD_SIZE,
                                           false);
      if (mtd == NULL)
        {
          printf("velaops: conf no mtd partition%c", 10);
        }
    }

  return mtd;
}

static int conf_geometry(uint32_t *erase_size, uint32_t *erase_blocks)
{
  struct mtd_dev_s *mtd = conf_mtd();
  struct mtd_geometry_s geo;

  if (mtd == NULL)
    {
      return -1;
    }

  memset(&geo, 0, sizeof(geo));

  if (mtd->ioctl(mtd, MTDIOC_GEOMETRY, (unsigned long)(uintptr_t)&geo) < 0 ||
      geo.erasesize == 0 || geo.neraseblocks < 3)
    {
      printf("velaops: conf geo errno=%d bs=%u es=%u nb=%u%c",
             errno, (unsigned)geo.blocksize, (unsigned)geo.erasesize,
             (unsigned)geo.neraseblocks, 10);
      return -1;
    }

  *erase_size = geo.erasesize;
  *erase_blocks = geo.neraseblocks;
  return 0;
}

static int conf_block_read(int block, uint8_t *buf, uint32_t size)
{
  struct mtd_dev_s *mtd = conf_mtd();
  ssize_t n;

  if (mtd == NULL)
    {
      return -1;
    }

  n = mtd->read(mtd, (off_t)block * (off_t)size, size, buf);

  return (n == (ssize_t)size) ? 0 : -1;
}

static int conf_block_write(int block, const uint8_t *buf, uint32_t size)
{
  struct mtd_dev_s *mtd = conf_mtd();
  struct mtd_erase_s erase;
  ssize_t n;

  if (mtd == NULL)
    {
      return -1;
    }

  memset(&erase, 0, sizeof(erase));
  erase.startblock = block;
  erase.nblocks = 1;

  if (mtd->ioctl(mtd, MTDIOC_ERASESECTORS,
                 (unsigned long)(uintptr_t)&erase) < 0)
    {
      printf("velaops: conf erase blk=%d failed errno=%d%c", block, errno, 10);
      return -1;
    }

  n = mtd->write(mtd, (off_t)block * (off_t)size, size, buf);
  if (n != (ssize_t)size)
    {
      printf("velaops: conf write blk=%d n=%d errno=%d%c",
             block, (int)n, errno, 10);
    }

  return (n == (ssize_t)size) ? 0 : -1;
}

static int conf_slot_parse(const uint8_t *slot, uint32_t size,
                           uint32_t *seq, char *out, size_t outsize)
{
  uint16_t len;
  uint32_t crc;

  if (memcmp(slot, CONF_MAGIC, 4) != 0)
    {
      return -1;
    }

  memcpy(seq, &slot[4], 4);
  memcpy(&len, &slot[8], 2);
  memcpy(&crc, &slot[10], 4);

  if (len == 0 || len > CONF_MAX_PAYLOAD ||
      (uint32_t)CONF_HDR_SIZE + len > size)
    {
      return -1;
    }

  if (conf_crc32(&slot[CONF_HDR_SIZE], len) != crc)
    {
      return -1;
    }

  if (out != NULL && outsize > 0)
    {
      size_t copy = len < outsize - 1 ? len : outsize - 1;

      memcpy(out, &slot[CONF_HDR_SIZE], copy);
      out[copy] = '\0';
    }

  return 0;
}

static uint8_t g_conf_slot[4096];

static uint8_t *conf_slot_alloc(uint32_t erase_size)
{
  if (erase_size <= sizeof(g_conf_slot))
    {
      return g_conf_slot;
    }

  return malloc(erase_size);
}

static void conf_slot_free(uint8_t *slot)
{
  if (slot != g_conf_slot)
    {
      conf_slot_free(slot);
    }
}

static int conf_load_internal(char *out, size_t size, uint32_t *seq_out)
{
  uint32_t erase_size;
  uint32_t blocks;
  uint8_t *slot;
  uint32_t best_seq = 0;
  static char best[CONF_MAX_PAYLOAD + 1];
  static char tmp[CONF_MAX_PAYLOAD + 1];
  int have = 0;
  int i;

  if (conf_geometry(&erase_size, &blocks) < 0)
    {
      return -1;
    }

  slot = conf_slot_alloc(erase_size);
  if (slot == NULL)
    {
      return -1;
    }

  for (i = 0; i < CONF_SLOT_COUNT; i++)
    {
      uint32_t seq;

      if (conf_block_read(i, slot, erase_size) < 0)
        {
          continue;
        }

      if (conf_slot_parse(slot, erase_size, &seq, tmp, sizeof(tmp)) == 0)
        {
          if (!have || seq > best_seq)
            {
              best_seq = seq;
              strcpy(best, tmp);
              have = 1;
            }
        }
    }

  conf_slot_free(slot);

  if (!have)
    {
      return -1;
    }

  if (out != NULL && size > 0)
    {
      strncpy(out, best, size - 1);
      out[size - 1] = '\0';
    }

  if (seq_out != NULL)
    {
      *seq_out = best_seq;
    }

  return 0;
}

int velaops_conf_load(char *out, size_t size)
{
  return conf_load_internal(out, size, NULL);
}

int velaops_conf_save(const char *text)
{
  uint32_t erase_size;
  uint32_t blocks;
  uint32_t seq = 0;
  uint8_t *slot;
  size_t len = strlen(text);
  int target;
  int ret = -1;

  if (len == 0 || len > CONF_MAX_PAYLOAD)
    {
      return -1;
    }

  if (conf_geometry(&erase_size, &blocks) < 0)
    {
      printf("velaops: conf geometry failed%c", 10);
      return -1;
    }

  /* Next sequence number goes into the slot that is not the newest. */

  if (conf_load_internal(NULL, 0, &seq) == 0)
    {
      target = (seq % CONF_SLOT_COUNT == 0) ? 1 : 0;
    }
  else
    {
      seq = 0;
      target = 0;
    }

  slot = conf_slot_alloc(erase_size);
  if (slot == NULL)
    {
      return -1;
    }

  memset(slot, 0, erase_size);
  memcpy(slot, CONF_MAGIC, 4);
  seq++;
  memcpy(&slot[4], &seq, 4);
  {
    uint16_t l16 = (uint16_t)len;
    uint32_t crc = conf_crc32((const uint8_t *)text, len);

    memcpy(&slot[8], &l16, 2);
    memcpy(&slot[10], &crc, 4);
  }

  memcpy(&slot[CONF_HDR_SIZE], text, len);

  if (conf_block_write(target, slot, erase_size) == 0 &&
      conf_block_read(target, slot, erase_size) == 0)
    {
      uint32_t check_seq;
      static char check[CONF_MAX_PAYLOAD + 1];

      if (conf_slot_parse(slot, erase_size, &check_seq,
                          check, sizeof(check)) == 0 &&
          strcmp(check, text) == 0)
        {
          ret = 0;
        }
    }

  conf_slot_free(slot);
  return ret;
}

int velaops_conf_get(const char *key, char *out, size_t size)
{
  static char buf[CONF_MAX_PAYLOAD + 1];
  size_t klen = strlen(key);
  char *line;
  char *cursor;

  if (size == 0)
    {
      return -1;
    }

  out[0] = '\0';

  if (velaops_conf_load(buf, sizeof(buf)) < 0)
    {
      return -1;
    }

  cursor = buf;

  while (cursor != NULL && *cursor != '\0')
    {
      line = cursor;
      cursor = strchr(cursor, '\n');

      if (cursor != NULL)
        {
          *cursor++ = '\0';
        }

      if (strncmp(line, key, klen) == 0 && line[klen] == '=')
        {
          size_t len = strlen(&line[klen + 1]);

          if (len >= size)
            {
              len = size - 1;
            }

          memcpy(out, &line[klen + 1], len);
          out[len] = '\0';
          return 0;
        }
    }

  return -1;
}

static int conf_rewrite(const char *skip_key, const char *set_key,
                        const char *set_value)
{
  static char text[CONF_MAX_PAYLOAD + 1];
  static char out[CONF_MAX_PAYLOAD + 1];
  size_t skip_len = skip_key != NULL ? strlen(skip_key) : 0;
  size_t olen = 0;
  char *cur;
  int src;

  if (velaops_conf_load(text, sizeof(text)) < 0)
    {
      text[0] = '\0';
    }

  cur = text;

  while (*cur != '\0')
    {
      char *nl = strchr(cur, '\n');
      size_t len = nl != NULL ? (size_t)(nl - cur) : strlen(cur);
      int drop = skip_key != NULL && len > skip_len &&
                 cur[skip_len] == '=' &&
                 strncmp(cur, skip_key, skip_len) == 0;

      if (!drop)
        {
          if (olen + len + 1 >= sizeof(out))
            {
              return -1;
            }

          memcpy(out + olen, cur, len);
          olen += len;
          out[olen++] = '\n';
        }

      cur = nl != NULL ? nl + 1 : cur + len;
    }

  if (set_key != NULL)
    {
      size_t klen = strlen(set_key);
      size_t vlen = strlen(set_value);

      if (olen + klen + vlen + 3 > sizeof(out))
        {
          return -1;
        }

      memcpy(out + olen, set_key, klen);
      olen += klen;
      out[olen++] = '=';
      memcpy(out + olen, set_value, vlen);
      olen += vlen;
      out[olen++] = '\n';
    }

  out[olen] = '\0';
  src = olen > 0 ? velaops_conf_save(out) : velaops_conf_clear();
  return src == 0 ? 0 : -1;
}

int velaops_conf_set(const char *key, const char *value)
{
  if (key == NULL || value == NULL)
    {
      return -1;
    }

  return conf_rewrite(key, key, value);
}

int velaops_conf_del(const char *key)
{
  if (key == NULL)
    {
      return -1;
    }

  return conf_rewrite(key, NULL, NULL);
}

int velaops_conf_present(void)
{
  char buf[CONF_MAX_PAYLOAD + 1];

  return velaops_conf_load(buf, sizeof(buf)) == 0 ? 1 : 0;
}

int velaops_conf_clear(void)
{
  uint32_t erase_size;
  uint32_t blocks;
  uint8_t *slot;
  int i;
  int ret = 0;

  if (conf_geometry(&erase_size, &blocks) < 0)
    {
      return -1;
    }

  slot = conf_slot_alloc(erase_size);
  if (slot == NULL)
    {
      return -1;
    }

  memset(slot, 0xff, erase_size);

  for (i = 0; i < CONF_SLOT_COUNT; i++)
    {
      if (conf_block_write(i, slot, erase_size) < 0)
        {
          ret = -1;
        }
    }

  conf_slot_free(slot);
  return ret;
}

int velaops_conf_flags_get(void)
{
  uint32_t erase_size;
  uint32_t blocks;
  uint8_t *slot;
  int value = 0;

  if (conf_geometry(&erase_size, &blocks) < 0)
    {
      return 0;
    }

  slot = conf_slot_alloc(erase_size);
  if (slot == NULL)
    {
      return 0;
    }

  if (conf_block_read(CONF_FLAG_BLOCK, slot, erase_size) == 0 &&
      memcmp(slot, FLAG_MAGIC, 4) == 0)
    {
      value = slot[4];
    }

  conf_slot_free(slot);
  return value;
}

int velaops_conf_flags_set(int bits)
{
  uint32_t erase_size;
  uint32_t blocks;
  uint8_t *slot;
  int ret;

  if (conf_geometry(&erase_size, &blocks) < 0)
    {
      return -1;
    }

  ret = velaops_conf_flags_get() | bits;

  slot = conf_slot_alloc(erase_size);
  if (slot == NULL)
    {
      return -1;
    }

  memset(slot, 0, erase_size);
  memcpy(slot, FLAG_MAGIC, 4);
  slot[4] = (uint8_t)ret;

  ret = conf_block_write(CONF_FLAG_BLOCK, slot, erase_size);
  conf_slot_free(slot);
  return ret;
}

int velaops_conf_flags_clear(void)
{
  uint32_t erase_size;
  uint32_t blocks;
  uint8_t *slot;
  int ret;

  if (conf_geometry(&erase_size, &blocks) < 0)
    {
      return -1;
    }

  slot = conf_slot_alloc(erase_size);
  if (slot == NULL)
    {
      return -1;
    }

  memset(slot, 0, erase_size);
  memcpy(slot, FLAG_MAGIC, 4);
  slot[4] = 0;

  ret = conf_block_write(CONF_FLAG_BLOCK, slot, erase_size);
  conf_slot_free(slot);
  return ret;
}
