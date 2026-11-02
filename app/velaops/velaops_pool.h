/****************************************************************************
 * apps/examples/velaops/velaops_pool.h
 *
 * Fixed arena backing libssh2 and mbedTLS allocations.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef VELAOPS_POOL_H
#define VELAOPS_POOL_H

#include <stddef.h>

int velaops_pool_init(size_t size);

/* libssh2 allocator callbacks. */

void *velaops_pool_alloc(size_t size, void **abstract);
void velaops_pool_free(void *ptr, void **abstract);
void *velaops_pool_realloc(void *ptr, size_t size, void **abstract);

/* mbedTLS platform allocator callbacks. */

void *velaops_pool_calloc(size_t nmemb, size_t size);
void velaops_pool_dealloc(void *ptr);

#endif /* VELAOPS_POOL_H */
