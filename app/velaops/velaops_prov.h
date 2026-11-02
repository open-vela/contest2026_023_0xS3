/****************************************************************************
 * apps/examples/velaops/velaops_prov.h
 *
 * First-boot provisioning: SoftAP + DHCP + HTTP form.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef VELAOPS_PROV_H
#define VELAOPS_PROV_H

#define VELAOPS_CFG_PATH "/data/velaops.conf"

/* Serve the setup page on the SoftAP until the form is saved. */

int velaops_prov_run(void);

/* 1 when a usable configuration file exists. */

int velaops_config_present(void);

/* Read "key=value" from the saved configuration.  Returns 0 on success. */

int velaops_conf_get(const char *key, char *out, size_t size);

/* Run one Wi-Fi scan and cache the AP list for the setup page. */

int velaops_scan_to_file(void);

#endif /* VELAOPS_PROV_H */
