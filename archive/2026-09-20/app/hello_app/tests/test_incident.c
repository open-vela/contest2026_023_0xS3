/****************************************************************************
 * VelaOps Incident 去抖状态机主机测试。
 ****************************************************************************/

#include "velaops_incident.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPECTED(value) do { if (!(value)) fail(#value, __LINE__); } while (0)

static void fail(const char *expression, int line)
{
  fprintf(stderr, "FAIL line %d: %s\n", line, expression);
  exit(EXIT_FAILURE);
}

static velaops_incident_event_t apply(velaops_incident_tracker_t *tracker,
                                      int64_t observed_at,
                                      velaops_health_result_t result)
{
  velaops_health_snapshot_t snapshot = {observed_at, result};
  velaops_incident_event_t event;

  EXPECTED(velaops_incident_tracker_apply(tracker, &snapshot, &event) ==
           VELAOPS_INCIDENT_OK);
  return event;
}

static velaops_incident_tracker_t make_tracker(uint16_t failures,
                                                uint16_t successes)
{
  velaops_incident_policy_t policy = {failures, successes};
  velaops_incident_tracker_t tracker;

  EXPECTED(velaops_incident_tracker_init(&tracker, &policy) ==
           VELAOPS_INCIDENT_OK);
  return tracker;
}

static void test_failure_debounce_and_deduplication(void)
{
  velaops_incident_tracker_t tracker = make_tracker(3, 2);

  EXPECTED(apply(&tracker, 100, VELAOPS_HEALTH_UNHEALTHY) ==
           VELAOPS_INCIDENT_EVENT_NONE);
  EXPECTED(tracker.state == VELAOPS_INCIDENT_SUSPECTED);
  EXPECTED(apply(&tracker, 101, VELAOPS_HEALTH_UNHEALTHY) ==
           VELAOPS_INCIDENT_EVENT_NONE);
  EXPECTED(apply(&tracker, 102, VELAOPS_HEALTH_UNHEALTHY) ==
           VELAOPS_INCIDENT_EVENT_OPENED);
  EXPECTED(tracker.state == VELAOPS_INCIDENT_OPEN);
  EXPECTED(tracker.generation == 1);

  EXPECTED(apply(&tracker, 103, VELAOPS_HEALTH_UNHEALTHY) ==
           VELAOPS_INCIDENT_EVENT_NONE);
  EXPECTED(apply(&tracker, 104, VELAOPS_HEALTH_UNHEALTHY) ==
           VELAOPS_INCIDENT_EVENT_NONE);
  EXPECTED(tracker.generation == 1);
}

static void test_flapping_does_not_open_incident(void)
{
  velaops_incident_tracker_t tracker = make_tracker(3, 2);

  apply(&tracker, 1, VELAOPS_HEALTH_UNHEALTHY);
  apply(&tracker, 2, VELAOPS_HEALTH_UNHEALTHY);
  EXPECTED(apply(&tracker, 3, VELAOPS_HEALTH_HEALTHY) ==
           VELAOPS_INCIDENT_EVENT_NONE);
  EXPECTED(tracker.state == VELAOPS_INCIDENT_HEALTHY);
  EXPECTED(tracker.consecutive_failures == 0);
  EXPECTED(tracker.generation == 0);
}

static void test_recovery_requires_consecutive_successes(void)
{
  velaops_incident_tracker_t tracker = make_tracker(2, 2);

  apply(&tracker, 1, VELAOPS_HEALTH_UNHEALTHY);
  EXPECTED(apply(&tracker, 2, VELAOPS_HEALTH_UNHEALTHY) ==
           VELAOPS_INCIDENT_EVENT_OPENED);
  EXPECTED(apply(&tracker, 3, VELAOPS_HEALTH_HEALTHY) ==
           VELAOPS_INCIDENT_EVENT_NONE);
  apply(&tracker, 4, VELAOPS_HEALTH_UNHEALTHY);
  EXPECTED(tracker.consecutive_successes == 0);
  apply(&tracker, 5, VELAOPS_HEALTH_HEALTHY);
  EXPECTED(apply(&tracker, 6, VELAOPS_HEALTH_HEALTHY) ==
           VELAOPS_INCIDENT_EVENT_RECOVERED);
  EXPECTED(tracker.state == VELAOPS_INCIDENT_RECOVERED);
  EXPECTED(apply(&tracker, 7, VELAOPS_HEALTH_HEALTHY) ==
           VELAOPS_INCIDENT_EVENT_NONE);
  EXPECTED(tracker.state == VELAOPS_INCIDENT_HEALTHY);
}

static void test_reopen_gets_new_generation(void)
{
  velaops_incident_tracker_t tracker = make_tracker(1, 1);

  EXPECTED(apply(&tracker, 1, VELAOPS_HEALTH_UNHEALTHY) ==
           VELAOPS_INCIDENT_EVENT_OPENED);
  EXPECTED(apply(&tracker, 2, VELAOPS_HEALTH_HEALTHY) ==
           VELAOPS_INCIDENT_EVENT_RECOVERED);
  EXPECTED(apply(&tracker, 3, VELAOPS_HEALTH_UNHEALTHY) ==
           VELAOPS_INCIDENT_EVENT_OPENED);
  EXPECTED(tracker.generation == 2);
}

static void test_invalid_and_out_of_order_input_is_non_mutating(void)
{
  velaops_incident_policy_t invalid_policy = {0, 1};
  velaops_incident_tracker_t tracker;
  velaops_incident_tracker_t before;
  velaops_health_snapshot_t snapshot = {9, VELAOPS_HEALTH_HEALTHY};
  velaops_incident_event_t event = VELAOPS_INCIDENT_EVENT_OPENED;

  EXPECTED(velaops_incident_tracker_init(NULL, &invalid_policy) ==
           VELAOPS_INCIDENT_INVALID_ARGUMENT);
  EXPECTED(velaops_incident_tracker_init(&tracker, &invalid_policy) ==
           VELAOPS_INCIDENT_INVALID_ARGUMENT);

  tracker = make_tracker(2, 1);
  apply(&tracker, 10, VELAOPS_HEALTH_UNHEALTHY);
  before = tracker;
  EXPECTED(velaops_incident_tracker_apply(&tracker, &snapshot, &event) ==
           VELAOPS_INCIDENT_OUT_OF_ORDER);
  EXPECTED(memcmp(&tracker, &before, sizeof(tracker)) == 0);
  EXPECTED(event == VELAOPS_INCIDENT_EVENT_OPENED);

  snapshot.observed_at = -1;
  EXPECTED(velaops_incident_tracker_apply(&tracker, &snapshot, &event) ==
           VELAOPS_INCIDENT_INVALID_ARGUMENT);
  EXPECTED(memcmp(&tracker, &before, sizeof(tracker)) == 0);

  tracker.state = (velaops_incident_state_t)99;
  before = tracker;
  snapshot.observed_at = 11;
  EXPECTED(velaops_incident_tracker_apply(&tracker, &snapshot, &event) ==
           VELAOPS_INCIDENT_INVALID_ARGUMENT);
  EXPECTED(memcmp(&tracker, &before, sizeof(tracker)) == 0);
}

int main(void)
{
  test_failure_debounce_and_deduplication();
  test_flapping_does_not_open_incident();
  test_recovery_requires_consecutive_successes();
  test_reopen_gets_new_generation();
  test_invalid_and_out_of_order_input_is_non_mutating();
  puts("PASS: VelaOps Incident state machine tests");
  return EXIT_SUCCESS;
}
