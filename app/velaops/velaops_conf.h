/****************************************************************************
 * apps/examples/velaops/velaops_conf.h
 *
 * Configuration store on raw flash blocks (no file system).
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef VELAOPS_CONF_H
#define VELAOPS_CONF_H

#include <stddef.h>

#define VELAOPS_FLAG_SETUP   0x01  /* enter setup mode on next boot */
#define VELAOPS_FLAG_RESCAN  0x02  /* run a wifi scan on next boot */

/* Read the stored configuration text.  Returns 0 on success. */

int velaops_conf_load(char *out, size_t size);

/* Store the configuration text (A/B slots, CRC protected). */

int velaops_conf_save(const char *text);

/* Read one "key=value" entry from the stored configuration. */

int velaops_conf_get(const char *key, char *out, size_t size);

/* Replace or append one "key=value" entry. */

int velaops_conf_set(const char *key, const char *value);

/* Remove one entry. Returns 0 even when the key was absent. */

int velaops_conf_del(const char *key);

/* 1 when a valid configuration is stored. */

int velaops_conf_present(void);

/* Erase both configuration slots. */

int velaops_conf_clear(void);

/* Boot flags kept in their own block. */

int velaops_conf_flags_get(void);
int velaops_conf_flags_set(int bits);
int velaops_conf_flags_clear(void);

#endif /* VELAOPS_CONF_H */
