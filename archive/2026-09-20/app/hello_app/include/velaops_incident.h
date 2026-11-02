/****************************************************************************
 * VelaOps Incident 去抖状态机。
 *
 * 本模块只处理领域状态，不依赖网络、存储、UI 或 NuttX。
 ****************************************************************************/

#ifndef VELAOPS_INCIDENT_H
#define VELAOPS_INCIDENT_H

#include <stdbool.h>
#include <stdint.h>

#include "velaops_health.h"

typedef enum
{
  VELAOPS_INCIDENT_HEALTHY = 0,
  VELAOPS_INCIDENT_SUSPECTED,
  VELAOPS_INCIDENT_OPEN,
  VELAOPS_INCIDENT_RECOVERED
} velaops_incident_state_t;

typedef enum
{
  VELAOPS_INCIDENT_EVENT_NONE = 0,
  VELAOPS_INCIDENT_EVENT_OPENED,
  VELAOPS_INCIDENT_EVENT_RECOVERED
} velaops_incident_event_t;

typedef enum
{
  VELAOPS_INCIDENT_OK = 0,
  VELAOPS_INCIDENT_INVALID_ARGUMENT,
  VELAOPS_INCIDENT_OUT_OF_ORDER
} velaops_incident_status_t;

typedef struct
{
  uint16_t failure_threshold;
  uint16_t recovery_threshold;
} velaops_incident_policy_t;

typedef struct
{
  velaops_incident_policy_t policy;
  velaops_incident_state_t state;
  uint16_t consecutive_failures;
  uint16_t consecutive_successes;
  uint32_t generation;
  int64_t last_observed_at;
  bool has_observation;
} velaops_incident_tracker_t;

/* 阈值必须大于 0；初始状态始终为 HEALTHY。 */
velaops_incident_status_t velaops_incident_tracker_init(
    velaops_incident_tracker_t *tracker,
    const velaops_incident_policy_t *policy);

/* 乱序快照被拒绝且不修改 tracker。event 只报告本次边沿。 */
velaops_incident_status_t velaops_incident_tracker_apply(
    velaops_incident_tracker_t *tracker,
    const velaops_health_snapshot_t *snapshot,
    velaops_incident_event_t *event);

#endif /* VELAOPS_INCIDENT_H */
