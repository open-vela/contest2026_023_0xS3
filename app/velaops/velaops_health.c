/****************************************************************************
 * apps/examples/velaops/velaops_health.c
 *
 * Whole-server health scoring and baseline drift.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "velaops_health.h"

int velaops_health_csv_has(const char *csv, const char *item)
{
  const char *p = csv;

  if (csv == NULL || item == NULL || item[0] == '\0')
    {
      return 0;
    }

  while (p != NULL && *p != '\0')
    {
      const char *comma = strchr(p, ',');
      size_t len = comma != NULL ? (size_t)(comma - p) : strlen(p);

      if (len == strlen(item) && strncmp(p, item, len) == 0)
        {
          return 1;
        }

      p = comma != NULL ? comma + 1 : NULL;
    }

  return 0;
}

void velaops_health_defaults(velaops_health_cfg_t *cfg)
{
  cfg->cpu_w = 85.0f;
  cfg->cpu_c = 95.0f;
  cfg->mem_w = 85.0f;
  cfg->mem_c = 92.0f;
  cfg->swap_w = 50.0f;
  cfg->swap_c = 80.0f;
  cfg->disk_w = 85.0f;
  cfg->disk_c = 92.0f;
  cfg->inode_w = 85.0f;
  cfg->inode_c = 92.0f;
  cfg->temp_w = 75.0f;
  cfg->temp_c = 85.0f;
  cfg->load_w = 1.5f;
  cfg->load_c = 3.0f;
  cfg->alert_score = 70;
  cfg->recover_score = 80;
}

static int metric_score(float value, float warn, float crit)
{
  if (value < 0.0f)
    {
      return -1;
    }

  if (value <= warn)
    {
      return 100;
    }

  if (value >= crit)
    {
      return 0;
    }

  return (int)(100.0f * (crit - value) / (crit - warn));
}

static void take_min(int *score, int candidate)
{
  if (candidate >= 0 && ( *score < 0 || candidate < *score))
    {
      *score = candidate;
    }
}

static int port_is_ephemeral(const char *port)
{
  int value = atoi(port);

  return value >= 32768;
}

static int port_in_list(char ports[][6], int count, const char *port)
{
  int i;

  for (i = 0; i < count; i++)
    {
      if (strcmp(ports[i], port) == 0)
        {
          return 1;
        }
    }

  return 0;
}

static int port_in_csv(const char *csv, const char *port)
{
  return velaops_health_csv_has(csv, port);
}

static void drift_ports(const velaops_snapshot_t *snap,
                        const char *base_ports, const char *ignore_ports,
                        velaops_health_result_t *r)
{
  const char *p = base_ports;
  int i;

  r->port_new = 0;
  r->port_lost = 0;

  for (i = 0; i < snap->port_count; i++)
    {
      const char *port = snap->ports[i];

      if (port_is_ephemeral(port) || port_in_csv(ignore_ports, port))
        {
          continue;
        }

      if (!port_in_csv(base_ports, port))
        {
          r->port_new++;
        }
    }

  while (p != NULL && *p != '\0')
    {
      const char *comma = strchr(p, ',');
      size_t len = comma != NULL ? (size_t)(comma - p) : strlen(p);
      char tmp[6];

      if (len > 0 && len < sizeof(tmp))
        {
          memcpy(tmp, p, len);
          tmp[len] = '\0';

          if (!port_in_csv(ignore_ports, tmp) &&
              !port_in_list((char (*)[6])snap->ports, snap->port_count, tmp))
            {
              r->port_lost++;
            }
        }

      p = comma != NULL ? comma + 1 : NULL;
    }
}

void velaops_health_score(const velaops_snapshot_t *snap,
                          const velaops_health_cfg_t *cfg,
                          const char *base_ports,
                          const char *ignore_ports,
                          const char *ignore_units,
                          velaops_health_result_t *r)
{
  int res = -1;
  float load = -1.0f;
  int i;

  memset(r, 0, sizeof(*r));

  if (!snap->online)
    {
      r->s_avail = 0;
      r->s_res = 0;
      r->s_svc = 0;
      r->s_net = 0;
      r->s_stab = 0;
      r->total = 0;
      r->critical = 1;
      snprintf(r->reason, sizeof(r->reason), "服务器离线");
      return;
    }

  r->s_avail = snap->uptime_sec < 300 ? 90 : 100;

  take_min(&res, metric_score(snap->cpu, cfg->cpu_w, cfg->cpu_c));
  take_min(&res, metric_score(snap->mem, cfg->mem_w, cfg->mem_c));
  take_min(&res, metric_score(snap->swap, cfg->swap_w, cfg->swap_c));
  take_min(&res, metric_score(snap->disk, cfg->disk_w, cfg->disk_c));
  take_min(&res, metric_score(snap->inode, cfg->inode_w, cfg->inode_c));
  take_min(&res, metric_score(snap->temp, cfg->temp_w, cfg->temp_c));

  if (snap->cores > 0 && snap->load1 >= 0.0f)
    {
      load = snap->load1 / (float)snap->cores * 100.0f;
      take_min(&res, metric_score(load, cfg->load_w * 100.0f,
                                  cfg->load_c * 100.0f));
    }

  r->s_res = res < 0 ? 100 : res;

  r->failed_count = 0;

  for (i = 0; i < snap->svc_failed && i < VELAOPS_MAX_FAILED; i++)
    {
      if (!velaops_health_csv_has(ignore_units, snap->failed[i]))
        {
          r->failed_count++;
        }
    }

  if (r->failed_count == 0)
    {
      r->s_svc = 100;
    }
  else if (r->failed_count == 1)
    {
      r->s_svc = 65;
    }
  else if (r->failed_count == 2)
    {
      r->s_svc = 45;
    }
  else
    {
      r->s_svc = 25;
    }

  drift_ports(snap, base_ports, ignore_ports, r);

  r->s_net = 100 - r->port_lost * 40 - r->port_new * 5;
  if (r->s_net < 0)
    {
      r->s_net = 0;
    }

  r->s_stab = 100;
  if (snap->oom > 0)
    {
      r->s_stab = 0;
    }
  else if (snap->zombies > 10)
    {
      r->s_stab = 60;
    }
  else if (snap->ntp == 0)
    {
      r->s_stab = 90;
    }

  r->total = (r->s_avail * 25 + r->s_res * 30 + r->s_svc * 25 +
              r->s_net * 10 + r->s_stab * 10) / 100;

  r->critical = 0;

  if ((snap->cpu >= cfg->cpu_c) || (snap->mem >= cfg->mem_c) ||
      (snap->disk >= cfg->disk_c) || (snap->inode >= cfg->inode_c) ||
      (snap->temp >= cfg->temp_c) || snap->oom > 0 || r->port_lost > 0)
    {
      r->critical = 1;
      if (r->total > 40)
        {
          r->total = 40;
        }
    }

  if (snap->top_cpu >= 50.0f && snap->top_name[0] != '\0')
    {
      r->kill_suggest = 1;
      strlcpy(r->kill_name, snap->top_name, sizeof(r->kill_name));
    }

  if (snap->oom > 0)
    {
      snprintf(r->reason, sizeof(r->reason), "检测到 OOM（近 5 分钟）");
    }
  else if (r->port_lost > 0)
    {
      snprintf(r->reason, sizeof(r->reason), "基线端口丢失 %d 个",
               r->port_lost);
    }
  else if (r->failed_count > 0)
    {
      snprintf(r->reason, sizeof(r->reason), "失败单元 %d 个：%.20s",
               r->failed_count, snap->failed[0]);
    }
  else if (snap->disk >= cfg->disk_c)
    {
      snprintf(r->reason, sizeof(r->reason), "磁盘空间告急 %.0f%%",
               (double)snap->disk);
    }
  else if (snap->inode >= cfg->inode_c)
    {
      snprintf(r->reason, sizeof(r->reason), "inode 告急 %.0f%%",
               (double)snap->inode);
    }
  else if (snap->temp >= cfg->temp_c)
    {
      snprintf(r->reason, sizeof(r->reason), "温度过高 %.0fC",
               (double)snap->temp);
    }
  else if (snap->cpu >= cfg->cpu_w)
    {
      snprintf(r->reason, sizeof(r->reason), "CPU 使用率 %.0f%%",
               (double)snap->cpu);
    }
  else if (snap->mem >= cfg->mem_w)
    {
      snprintf(r->reason, sizeof(r->reason), "内存使用率 %.0f%%",
               (double)snap->mem);
    }
  else if (r->port_new > 0)
    {
      snprintf(r->reason, sizeof(r->reason), "新增监听端口 %d 个",
               r->port_new);
    }
  else
    {
      snprintf(r->reason, sizeof(r->reason), "运行正常");
    }
}
