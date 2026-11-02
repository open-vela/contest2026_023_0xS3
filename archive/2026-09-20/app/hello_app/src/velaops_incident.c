/****************************************************************************
 * VelaOps Incident 去抖状态机实现。
 ****************************************************************************/

#include "velaops_incident.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static uint16_t velaops_saturating_increment(uint16_t value)
{
  return value == UINT16_MAX ? value : (uint16_t)(value + 1);
}

static void velaops_open_incident(velaops_incident_tracker_t *tracker,
                                  velaops_incident_event_t *event)
{
  tracker->state = VELAOPS_INCIDENT_OPEN;
  tracker->consecutive_successes = 0;
  if (tracker->generation != UINT32_MAX)
    {
      tracker->generation++;
    }
  *event = VELAOPS_INCIDENT_EVENT_OPENED;
}

static void velaops_start_failure(velaops_incident_tracker_t *tracker,
                                  velaops_incident_event_t *event)
{
  tracker->consecutive_failures = 1;
  tracker->consecutive_successes = 0;
  if (tracker->policy.failure_threshold == 1)
    {
      velaops_open_incident(tracker, event);
    }
  else
    {
      tracker->state = VELAOPS_INCIDENT_SUSPECTED;
    }
}

velaops_incident_status_t velaops_incident_tracker_init(
    velaops_incident_tracker_t *tracker,
    const velaops_incident_policy_t *policy)
{
  if (tracker == NULL || policy == NULL || policy->failure_threshold == 0 ||
      policy->recovery_threshold == 0)
    {
      return VELAOPS_INCIDENT_INVALID_ARGUMENT;
    }

  memset(tracker, 0, sizeof(*tracker));
  tracker->policy = *policy;
  tracker->state = VELAOPS_INCIDENT_HEALTHY;
  return VELAOPS_INCIDENT_OK;
}

velaops_incident_status_t velaops_incident_tracker_apply(
    velaops_incident_tracker_t *tracker,
    const velaops_health_snapshot_t *snapshot,
    velaops_incident_event_t *event)
{
  if (tracker == NULL || snapshot == NULL || event == NULL ||
      snapshot->observed_at < 0 ||
      (snapshot->result != VELAOPS_HEALTH_HEALTHY &&
       snapshot->result != VELAOPS_HEALTH_UNHEALTHY) ||
      tracker->state < VELAOPS_INCIDENT_HEALTHY ||
      tracker->state > VELAOPS_INCIDENT_RECOVERED ||
      tracker->policy.failure_threshold == 0 ||
      tracker->policy.recovery_threshold == 0)
    {
      return VELAOPS_INCIDENT_INVALID_ARGUMENT;
    }
  if (tracker->has_observation &&
      snapshot->observed_at < tracker->last_observed_at)
    {
      return VELAOPS_INCIDENT_OUT_OF_ORDER;
    }

  *event = VELAOPS_INCIDENT_EVENT_NONE;
  tracker->last_observed_at = snapshot->observed_at;
  tracker->has_observation = true;

  switch (tracker->state)
    {
      case VELAOPS_INCIDENT_HEALTHY:
        if (snapshot->result == VELAOPS_HEALTH_UNHEALTHY)
          {
            velaops_start_failure(tracker, event);
          }
        break;

      case VELAOPS_INCIDENT_SUSPECTED:
        if (snapshot->result == VELAOPS_HEALTH_HEALTHY)
          {
            tracker->state = VELAOPS_INCIDENT_HEALTHY;
            tracker->consecutive_failures = 0;
          }
        else
          {
            tracker->consecutive_failures = velaops_saturating_increment(
                tracker->consecutive_failures);
            if (tracker->consecutive_failures >=
                tracker->policy.failure_threshold)
              {
                velaops_open_incident(tracker, event);
              }
          }
        break;

      case VELAOPS_INCIDENT_OPEN:
        if (snapshot->result == VELAOPS_HEALTH_UNHEALTHY)
          {
            /* 同一事故持续异常时不重复开单，并中断恢复计数。 */

            tracker->consecutive_successes = 0;
          }
        else
          {
            tracker->consecutive_successes = velaops_saturating_increment(
                tracker->consecutive_successes);
            if (tracker->consecutive_successes >=
                tracker->policy.recovery_threshold)
              {
                tracker->state = VELAOPS_INCIDENT_RECOVERED;
                tracker->consecutive_failures = 0;
                *event = VELAOPS_INCIDENT_EVENT_RECOVERED;
              }
          }
        break;

      case VELAOPS_INCIDENT_RECOVERED:
        tracker->consecutive_successes = 0;
        if (snapshot->result == VELAOPS_HEALTH_HEALTHY)
          {
            tracker->state = VELAOPS_INCIDENT_HEALTHY;
          }
        else
          {
            velaops_start_failure(tracker, event);
          }
        break;

      default:
        return VELAOPS_INCIDENT_INVALID_ARGUMENT;
    }

  return VELAOPS_INCIDENT_OK;
}
