/****************************************************************************
 * VelaOps 健康快照与本地规则。
 ****************************************************************************/

#ifndef VELAOPS_HEALTH_H
#define VELAOPS_HEALTH_H

#include <stdint.h>

typedef enum
{
  VELAOPS_HEALTH_HEALTHY = 0,
  VELAOPS_HEALTH_UNHEALTHY
} velaops_health_result_t;

typedef struct
{
  int64_t observed_at;
  velaops_health_result_t result;
} velaops_health_snapshot_t;

typedef struct
{
  uint64_t total_bytes;
  uint64_t available_bytes;
  uint64_t used_bytes;
  double used_percent;
} velaops_memory_observation_t;

typedef enum
{
  VELAOPS_HEALTH_OK = 0,
  VELAOPS_HEALTH_INVALID_ARGUMENT,
  VELAOPS_HEALTH_INVALID_OBSERVATION
} velaops_health_status_t;

/* 阈值在 (0, 100] 内，达到阈值即判定异常。 */
velaops_health_status_t velaops_evaluate_memory(
    const velaops_memory_observation_t *observation,
    double unhealthy_threshold_percent,
    int64_t observed_at,
    velaops_health_snapshot_t *snapshot);

#endif /* VELAOPS_HEALTH_H */
