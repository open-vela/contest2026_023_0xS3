/****************************************************************************
 * VelaOps 资源主动事件编排器。
 *
 * 将资源诊断映射为 Incident 去抖输入，只在开单和恢复边沿输出诊断。
 ****************************************************************************/

#ifndef VELAOPS_RESOURCE_INCIDENT_H
#define VELAOPS_RESOURCE_INCIDENT_H

#include <stddef.h>
#include <stdint.h>

#include "velaops_incident.h"
#include "velaops_resource_result.h"

typedef enum
{
  VELAOPS_RESOURCE_INCIDENT_OK = 0,
  VELAOPS_RESOURCE_INCIDENT_INVALID_ARGUMENT,
  VELAOPS_RESOURCE_INCIDENT_INVALID_EVIDENCE,
  VELAOPS_RESOURCE_INCIDENT_OUT_OF_ORDER,
  VELAOPS_RESOURCE_INCIDENT_ENCODE_ERROR
} velaops_resource_incident_status_t;

typedef struct
{
  velaops_incident_tracker_t tracker;
} velaops_resource_incident_t;

velaops_resource_incident_status_t velaops_resource_incident_init(
    velaops_resource_incident_t *incident, uint16_t failure_threshold,
    uint16_t recovery_threshold);

velaops_resource_incident_status_t velaops_resource_incident_apply(
    velaops_resource_incident_t *incident, const char *resource_json,
    int64_t observed_at, velaops_incident_event_t *event,
    char *diagnosis, size_t diagnosis_capacity);

/* 传入已解析资源，避免重复解析 JSON（单次巡检只解析一次）。 */
velaops_resource_incident_status_t velaops_resource_incident_apply_observation(
    velaops_resource_incident_t *incident,
    const velaops_resource_observation_t *observation,
    int64_t observed_at, velaops_incident_event_t *event,
    char *diagnosis, size_t diagnosis_capacity);

#endif /* VELAOPS_RESOURCE_INCIDENT_H */
