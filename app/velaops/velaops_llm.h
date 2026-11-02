/****************************************************************************
 * apps/examples/velaops/velaops_llm.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef VELAOPS_LLM_H
#define VELAOPS_LLM_H

#include <stddef.h>

/* Route mbedTLS allocations through the preallocated pool. */

int velaops_llm_prealloc(void);

/* POST one chat completion request to an OpenAI-compatible endpoint.
 * Returns 0 and stores the assistant text in out on success.
 */

int velaops_llm_chat(const char *url, const char *model, const char *key,
                     const char *prompt, char *out, size_t outsize);

#endif /* VELAOPS_LLM_H */
