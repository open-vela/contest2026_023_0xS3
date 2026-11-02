/****************************************************************************
 * apps/examples/velaops/velaops_pool.c
 *
 * Simple first-fit arena. WiFi churns the main heap down to small
 * fragments, so the TLS/SSH stack gets memory from a block reserved at
 * start-up while large blocks still exist.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "velaops_pool.h"

#define POOL_ALIGN        8
#define POOL_MAGIC_FREE   0xa5a5f00dul
#define POOL_MAGIC_USED   0x5a5a0ff0ul

struct pool_block_s
{
  uint32_t size;
  uint32_t magic;
};

static uint8_t *g_pool;
static size_t   g_pool_len;

static void pool_dump(const char *why, void *ptr)
{
  (void)why;
  (void)ptr;
}

static void *pool_alloc(size_t count)
{
  size_t need = (count + POOL_ALIGN - 1) & ~(size_t)(POOL_ALIGN - 1);
  uint8_t *p;
  uint8_t *end;

  if (g_pool == NULL || need == 0)
    {
      return NULL;
    }

  p = g_pool;
  end = g_pool + g_pool_len;

  while (p + sizeof(struct pool_block_s) <= end)
    {
      struct pool_block_s *blk = (struct pool_block_s *)p;

      if (blk->magic != POOL_MAGIC_FREE && blk->magic != POOL_MAGIC_USED)
        {
          pool_dump("corrupt block", p);
          return NULL;
        }

      if (blk->size == 0 || blk->size > g_pool_len)
        {
          pool_dump("corrupt size", p);
          return NULL;
        }

      if (blk->magic == POOL_MAGIC_FREE && blk->size >= need)
        {
          if (blk->size >= need + sizeof(struct pool_block_s) + POOL_ALIGN)
            {
              struct pool_block_s *rest =
                (struct pool_block_s *)(p + sizeof(*blk) + need);

              rest->size = blk->size - need - sizeof(*blk);
              rest->magic = POOL_MAGIC_FREE;
              blk->size = need;
            }

          blk->magic = POOL_MAGIC_USED;
          return p + sizeof(*blk);
        }

      p += sizeof(*blk) + blk->size;
    }

  return NULL;
}

static void pool_free(void *ptr)
{
  struct pool_block_s *blk;
  uint8_t *p;
  uint8_t *end;

  if (ptr == NULL || g_pool == NULL)
    {
      return;
    }

  if ((uint8_t *)ptr < g_pool + sizeof(struct pool_block_s) ||
      (uint8_t *)ptr >= g_pool + g_pool_len)
    {
      pool_dump("foreign free", ptr);
      return;
    }

  blk = (struct pool_block_s *)((uint8_t *)ptr - sizeof(*blk));
  if (blk->magic != POOL_MAGIC_USED)
    {
      pool_dump("bad free", ptr);
      return;
    }

  blk->magic = POOL_MAGIC_FREE;

  p = g_pool;
  end = g_pool + g_pool_len;

  while (p + sizeof(struct pool_block_s) <= end)
    {
      struct pool_block_s *cur = (struct pool_block_s *)p;
      uint8_t *nxtp;

      if (cur->magic != POOL_MAGIC_FREE && cur->magic != POOL_MAGIC_USED)
        {
          pool_dump("corrupt chain", p);
          return;
        }

      if (cur->size == 0 || cur->size > g_pool_len)
        {
          pool_dump("corrupt chain size", p);
          return;
        }

      nxtp = p + sizeof(*cur) + cur->size;

      if (cur->magic == POOL_MAGIC_FREE &&
          nxtp + sizeof(*cur) <= end)
        {
          struct pool_block_s *nxt = (struct pool_block_s *)nxtp;

          if (nxt->magic == POOL_MAGIC_FREE && nxt->size != 0 &&
              nxt->size <= g_pool_len)
            {
              cur->size += sizeof(*cur) + nxt->size;
              continue;
            }
        }

      p = nxtp;
    }
}

int velaops_pool_init(size_t size)
{
  struct pool_block_s *blk;

  if (g_pool != NULL)
    {
      return 0;
    }

  g_pool = malloc(size);
  if (g_pool == NULL)
    {
      printf("velaops: pool alloc failed%c", 10);
      return -1;
    }

  g_pool_len = size;
  blk = (struct pool_block_s *)g_pool;
  blk->size = size - sizeof(*blk);
  blk->magic = POOL_MAGIC_FREE;
  return 0;
}

void *velaops_pool_alloc(size_t size, void **abstract)
{
  (void)abstract;
  return pool_alloc(size);
}

void velaops_pool_free(void *ptr, void **abstract)
{
  (void)abstract;
  pool_free(ptr);
}

void *velaops_pool_realloc(void *ptr, size_t size, void **abstract)
{
  struct pool_block_s *blk;
  size_t old;
  void *out;

  (void)abstract;

  if (ptr == NULL)
    {
      return pool_alloc(size);
    }

  if ((uint8_t *)ptr < g_pool + sizeof(struct pool_block_s) ||
      (uint8_t *)ptr >= g_pool + g_pool_len)
    {
      pool_dump("foreign realloc", ptr);
      return NULL;
    }

  blk = (struct pool_block_s *)((uint8_t *)ptr - sizeof(*blk));
  if (blk->magic != POOL_MAGIC_USED)
    {
      pool_dump("bad realloc", ptr);
      return NULL;
    }

  if (blk->size >= size)
    {
      return ptr;
    }

  out = pool_alloc(size);
  if (out == NULL)
    {
      return NULL;
    }

  old = blk->size;
  memcpy(out, ptr, old);
  pool_free(ptr);
  return out;
}

void *velaops_pool_calloc(size_t nmemb, size_t size)
{
  size_t total;
  void *ptr;

  if (nmemb != 0 && size > SIZE_MAX / nmemb)
    {
      return NULL;
    }

  total = nmemb * size;
  ptr = pool_alloc(total);

  if (ptr != NULL)
    {
      memset(ptr, 0, total);
    }

  return ptr;
}

void velaops_pool_dealloc(void *ptr)
{
  pool_free(ptr);
}
