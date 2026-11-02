/****************************************************************************
 * apps/examples/velaops/velaops_health.h
 *
 * Whole-server health scoring.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef VELAOPS_HEALTH_H
#define VELAOPS_HEALTH_H

#include "velaops_ssh.h"

typedef struct
{
  float cpu_w;
  float cpu_c;
  float mem_w;
  float mem_c;
  float swap_w;
  float swap_c;
  float disk_w;
  float disk_c;
  float inode_w;
  float inode_c;
  float temp_w;
  float temp_c;
  float load_w;                 /* multiplier of core count */
  float load_c;
  int   alert_score;
  int   recover_score;
} velaops_health_cfg_t;

typedef struct
{
  int total;
  int s_avail;
  int s_res;
  int s_svc;
  int s_net;
  int s_stab;
  int critical;
  int port_new;
  int port_lost;
  int failed_count;             /* failed units after ignore filtering */
  int kill_suggest;
  char kill_name[VELAOPS_NAME_LEN];
  char reason[96];
} velaops_health_result_t;

void velaops_health_defaults(velaops_health_cfg_t *cfg);

int velaops_health_csv_has(const char *csv, const char *item);

void velaops_health_score(const velaops_snapshot_t *snap,
                          const velaops_health_cfg_t *cfg,
                          const char *base_ports,
                          const char *ignore_ports,
                          const char *ignore_units,
                          velaops_health_result_t *result);

#endif /* VELAOPS_HEALTH_H */
