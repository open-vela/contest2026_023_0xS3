/****************************************************************************
 * VelaOps Proxy 聚合资源结果适配器。
 ****************************************************************************/

#ifndef VELAOPS_RESOURCE_RESULT_H
#define VELAOPS_RESOURCE_RESULT_H

#include <stdbool.h>
#include "velaops_health.h"

typedef struct
{
  double used_percent;
  double load1;
  double load5;
  double load15;
  int cores;
  bool valid;
} velaops_cpu_observation_t;

typedef struct
{
  char name[32];
  double cpu_percent;
  bool valid;
} velaops_process_observation_t;

typedef struct
{
  velaops_memory_observation_t memory;
  double disk_percent;
  bool service_active;
  bool port_reachable;
  int port_latency_ms;
  velaops_cpu_observation_t cpu;
  velaops_process_observation_t process;
} velaops_resource_observation_t;

int velaops_resource_result_parse(const char *result_json,
                                  velaops_resource_observation_t *result);

#endif /* VELAOPS_RESOURCE_RESULT_H */
