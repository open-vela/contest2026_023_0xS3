/****************************************************************************
 * VelaOps Sentinel - 扩展监控指标采集
 *
 * 优化：将 7 次 SSH exec 合并为 2 次（CPU 需要两次采样），
 * 大幅降低在不稳定 Wi-Fi 链路上的失败概率。
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "velaops.h"

/* CPU 采样结构 */
struct cpu_sample
{
  unsigned long long idle;
  unsigned long long total;
};

/* 解析 CPU 统计信息 */
static int parse_cpu(const char *text, struct cpu_sample *sample)
{
  unsigned long long v[10] = {0};
  int n = sscanf(text, "cpu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                 &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7],
                 &v[8], &v[9]);
  int i;
  if (n < 4) return -1;
  sample->idle = v[3] + v[4];
  sample->total = 0;
  for (i = 0; i < n; i++) sample->total += v[i];
  return 0;
}

/* 解析内存值 */
static unsigned long mem_value(const char *text, const char *name)
{
  const char *p = strstr(text, name);
  unsigned long value = 0;
  if (p != NULL) sscanf(p + strlen(name), ": %lu", &value);
  return value;
}

/* 在组合输出中查找以 prefix 开头的行 */
static const char *find_line(const char *text, const char *prefix)
{
  const char *p = text;
  while (p != NULL)
    {
      if (strncmp(p, prefix, strlen(prefix)) == 0)
        return p;
      p = strchr(p, '\n');
      if (p != NULL) p++;
    }
  return NULL;
}

/* 采集所有指标 — 最小化 SSH 交互次数 */
int velaops_metrics_collect(struct velaops_ssh *client,
                            struct velaops_metrics *metrics)
{
  /* 组合命令：一次 exec 获取所有非 CPU 指标 + CPU 第一次采样 */
  static const char collect_cmd[] =
    "echo '===STAT==='; head -n 1 /proc/stat;"
    " echo '===MEMINFO==='; grep -E 'MemTotal|MemAvailable|MemFree|Buffers|Cached' /proc/meminfo;"
    " echo '===LOADAVG==='; cat /proc/loadavg;"
    " echo '===NETDEV==='; awk 'NR>2 && $1 !~ /lo:/ {print; exit}' /proc/net/dev;"
    " echo '===UPTIME==='; cat /proc/uptime;"
    " echo '===DF==='; df -kP / | tail -1;"
    " echo '===PS==='; ps | wc -l";

  char output[4096];
  struct cpu_sample first;
  struct cpu_sample second;
  unsigned long long delta_total;
  unsigned long long delta_idle;
  const char *p;

  /* 不清零 — 保留上次有效值，仅更新成功采集的字段 */

  /* 第一次 SSH exec：采集所有指标 + CPU 第一次采样 */
  if (velaops_ssh_exec(client, collect_cmd, output, sizeof(output)) < 0)
    {
      /* SSH 失败 — 如果已有有效数据则保留，否则返回失败 */
      return metrics->valid ? 0 : -1;
    }

  /* 解析 CPU 第一次采样 */
  p = find_line(output, "===STAT===");
  if (p == NULL) return -1;
  p = strchr(p, '\n');
  if (p == NULL) return -1;
  p++; /* 跳过标记行，指向 cpu 行 */
  if (parse_cpu(p, &first) < 0) return -1;

  /* 解析内存 */
  p = find_line(output, "===MEMINFO===");
  if (p != NULL)
    {
      metrics->memory_total_kb = mem_value(p, "MemTotal");
      metrics->memory_available_kb = mem_value(p, "MemAvailable");
      if (metrics->memory_available_kb == 0)
        metrics->memory_available_kb = mem_value(p, "MemFree") +
          mem_value(p, "Buffers") + mem_value(p, "Cached");
      if (metrics->memory_total_kb > 0)
        metrics->memory_percent = 100.0f *
          (metrics->memory_total_kb - metrics->memory_available_kb) /
          metrics->memory_total_kb;
    }

  /* 解析负载 */
  p = find_line(output, "===LOADAVG===");
  if (p != NULL)
    {
      p = strchr(p, '\n');
      if (p != NULL)
        {
          float load1, load5, load15;
          if (sscanf(p + 1, "%f %f %f", &load1, &load5, &load15) == 3)
            {
              metrics->load_1min = load1;
              metrics->load_5min = load5;
              metrics->load_15min = load15;
            }
        }
    }

  /* 解析网络 */
  p = find_line(output, "===NETDEV===");
  if (p != NULL)
    {
      p = strchr(p, '\n');
      if (p != NULL)
        {
          char iface[32];
          unsigned long rx, tx;
          if (sscanf(p + 1, "%s %lu %*u %*u %*u %*u %*u %*u %*u %lu",
                     iface, &rx, &tx) == 3)
            {
              metrics->net_rx_bytes = rx;
              metrics->net_tx_bytes = tx;
            }
        }
    }

  /* 解析运行时间 */
  p = find_line(output, "===UPTIME===");
  if (p != NULL)
    {
      p = strchr(p, '\n');
      if (p != NULL) sscanf(p + 1, "%lu", &metrics->uptime_seconds);
    }

  /* 解析磁盘 */
  p = find_line(output, "===DF===");
  if (p != NULL)
    {
      p = strchr(p, '\n');
      if (p != NULL)
        {
          char fs[64], mp[64];
          unsigned long total, used, avail;
          int pct;
          if (sscanf(p + 1, "%s %lu %lu %lu %d%% %s",
                     fs, &total, &used, &avail, &pct, mp) == 6)
            {
              metrics->disk_total_kb = total;
              metrics->disk_used_kb = used;
              metrics->disk_available_kb = avail;
              metrics->disk_percent = (float)pct;
            }
        }
    }

  /* 解析进程数 */
  p = find_line(output, "===PS===");
  if (p != NULL)
    {
      p = strchr(p, '\n');
      if (p != NULL) metrics->process_count = atoi(p + 1);
    }

  /* CPU 需要两次采样，间隔 1 秒 */
  sleep(1);
  if (velaops_ssh_exec(client, "head -n 1 /proc/stat", output,
                       sizeof(output)) < 0)
    {
      /* CPU 采集失败不致命，其他指标仍然有效 */
      metrics->valid = true;
      return 0;
    }

  if (parse_cpu(output, &second) < 0)
    {
      metrics->valid = true;
      return 0;
    }

  delta_total = second.total - first.total;
  delta_idle = second.idle - first.idle;
  metrics->cpu_percent = delta_total == 0 ? 0 :
    100.0f * (float)(delta_total - delta_idle) / (float)delta_total;

  /* 验证关键指标 — 如果内存数据无效但有其他数据，仍然标记为有效 */
  if (metrics->memory_total_kb == 0 && !metrics->valid)
    return -1;

  metrics->valid = true;
  return 0;
}
