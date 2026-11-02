/****************************************************************************
 * VelaOps 资源主动事件编排器实现。
 ****************************************************************************/

#include "velaops_resource_incident.h"

#include "velaops_health.h"
#include "velaops_local_diagnosis.h"

velaops_resource_incident_status_t velaops_resource_incident_init(
    velaops_resource_incident_t *incident, uint16_t failure_threshold,
    uint16_t recovery_threshold)
{
  velaops_incident_policy_t policy = {
    failure_threshold,
    recovery_threshold
  };

  if (incident == NULL ||
      velaops_incident_tracker_init(&incident->tracker, &policy) !=
          VELAOPS_INCIDENT_OK)
    {
      return VELAOPS_RESOURCE_INCIDENT_INVALID_ARGUMENT;
    }
  return VELAOPS_RESOURCE_INCIDENT_OK;
}

static velaops_resource_incident_status_t velaops_resource_incident_finish(
    velaops_resource_incident_t *incident,
    velaops_diagnosis_status_t diagnosis_status, int64_t observed_at,
    velaops_incident_event_t *event)
{
  velaops_health_snapshot_t snapshot;
  velaops_incident_status_t incident_status;

  snapshot.observed_at = observed_at;
  snapshot.result = diagnosis_status == VELAOPS_DIAGNOSIS_NORMAL ?
                    VELAOPS_HEALTH_HEALTHY : VELAOPS_HEALTH_UNHEALTHY;
  incident_status = velaops_incident_tracker_apply(&incident->tracker,
                                                    &snapshot, event);
  if (incident_status == VELAOPS_INCIDENT_OUT_OF_ORDER)
    {
      return VELAOPS_RESOURCE_INCIDENT_OUT_OF_ORDER;
    }
  if (incident_status != VELAOPS_INCIDENT_OK)
    {
      return VELAOPS_RESOURCE_INCIDENT_INVALID_ARGUMENT;
    }
  return VELAOPS_RESOURCE_INCIDENT_OK;
}

velaops_resource_incident_status_t velaops_resource_incident_apply(
    velaops_resource_incident_t *incident, const char *resource_json,
    int64_t observed_at, velaops_incident_event_t *event,
    char *diagnosis, size_t diagnosis_capacity)
{
  velaops_diagnosis_status_t diagnosis_status;
  velaops_resource_incident_status_t status;

  if (incident == NULL || event == NULL || diagnosis == NULL ||
      diagnosis_capacity == 0 || observed_at < 0)
    {
      return VELAOPS_RESOURCE_INCIDENT_INVALID_ARGUMENT;
    }
  diagnosis[0] = '\0';
  *event = VELAOPS_INCIDENT_EVENT_NONE;
  if (velaops_local_diagnosis_evaluate(resource_json,
                                       &diagnosis_status) != 0)
    {
      return VELAOPS_RESOURCE_INCIDENT_INVALID_EVIDENCE;
    }

  status = velaops_resource_incident_finish(incident, diagnosis_status,
                                            observed_at, event);
  if (status != VELAOPS_RESOURCE_INCIDENT_OK)
    {
      return status;
    }
  if (*event != VELAOPS_INCIDENT_EVENT_NONE &&
      velaops_local_diagnosis_build(resource_json, diagnosis,
                                    diagnosis_capacity) != 0)
    {
      *event = VELAOPS_INCIDENT_EVENT_NONE;
      return VELAOPS_RESOURCE_INCIDENT_ENCODE_ERROR;
    }
  return VELAOPS_RESOURCE_INCIDENT_OK;
}

velaops_resource_incident_status_t velaops_resource_incident_apply_observation(
    velaops_resource_incident_t *incident,
    const velaops_resource_observation_t *observation, int64_t observed_at,
    velaops_incident_event_t *event, char *diagnosis,
    size_t diagnosis_capacity)
{
  velaops_diagnosis_status_t diagnosis_status;
  velaops_resource_incident_status_t status;

  if (incident == NULL || observation == NULL || event == NULL ||
      diagnosis == NULL || diagnosis_capacity == 0 || observed_at < 0)
    {
      return VELAOPS_RESOURCE_INCIDENT_INVALID_ARGUMENT;
    }
  diagnosis[0] = '\0';
  *event = VELAOPS_INCIDENT_EVENT_NONE;
  if (velaops_local_diagnosis_evaluate_observation(observation,
                                                   &diagnosis_status) != 0)
    {
      return VELAOPS_RESOURCE_INCIDENT_INVALID_EVIDENCE;
    }

  status = velaops_resource_incident_finish(incident, diagnosis_status,
                                            observed_at, event);
  if (status != VELAOPS_RESOURCE_INCIDENT_OK)
    {
      return status;
    }
  if (*event != VELAOPS_INCIDENT_EVENT_NONE &&
      velaops_local_diagnosis_build_observation(observation, diagnosis,
                                                diagnosis_capacity) != 0)
    {
      *event = VELAOPS_INCIDENT_EVENT_NONE;
      return VELAOPS_RESOURCE_INCIDENT_ENCODE_ERROR;
    }
  return VELAOPS_RESOURCE_INCIDENT_OK;
}
