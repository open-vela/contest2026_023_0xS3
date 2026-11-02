/****************************************************************************
 * VelaOps 健康快照与本地规则实现。
 ****************************************************************************/

#include "velaops_health.h"

#include <math.h>
#include <stddef.h>

#define VELAOPS_PERCENT_TOLERANCE 0.02

velaops_health_status_t velaops_evaluate_memory(
    const velaops_memory_observation_t *observation,
    double unhealthy_threshold_percent,
    int64_t observed_at,
    velaops_health_snapshot_t *snapshot)
{
  double calculated_percent;

  if (observation == NULL || snapshot == NULL || observed_at < 0 ||
      !isfinite(unhealthy_threshold_percent) ||
      unhealthy_threshold_percent <= 0.0 ||
      unhealthy_threshold_percent > 100.0)
    {
      return VELAOPS_HEALTH_INVALID_ARGUMENT;
    }
  if (observation->total_bytes == 0 ||
      observation->available_bytes > observation->total_bytes ||
      observation->used_bytes !=
          observation->total_bytes - observation->available_bytes ||
      !isfinite(observation->used_percent) ||
      observation->used_percent < 0.0 || observation->used_percent > 100.0)
    {
      return VELAOPS_HEALTH_INVALID_OBSERVATION;
    }

  calculated_percent = (double)observation->used_bytes * 100.0 /
                       (double)observation->total_bytes;
  if (fabs(calculated_percent - observation->used_percent) >
      VELAOPS_PERCENT_TOLERANCE)
    {
      /* Proxy 的字节数与百分比必须互相印证，避免单字段异常
       * 触发错误的 Incident 或掩盖真实资源压力。
       */

      return VELAOPS_HEALTH_INVALID_OBSERVATION;
    }

  snapshot->observed_at = observed_at;
  snapshot->result = observation->used_percent >=
                     unhealthy_threshold_percent ?
                     VELAOPS_HEALTH_UNHEALTHY : VELAOPS_HEALTH_HEALTHY;
  return VELAOPS_HEALTH_OK;
}
