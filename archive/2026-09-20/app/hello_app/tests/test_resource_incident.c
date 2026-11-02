/****************************************************************************
 * VelaOps 资源主动事件编排器主机测试。
 ****************************************************************************/

#include "velaops_resource_incident.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPECTED(value) do { if (!(value)) fail(#value, __LINE__); } while (0)

static const char normal_result[] =
    "{\"memory\":{\"total_bytes\":1000,\"available_bytes\":600,"
    "\"used_bytes\":400,\"used_percent\":40},"
    "\"disk\":{\"used_percent\":20},"
    "\"service\":{\"active_state\":\"active\"},"
    "\"port\":{\"reachable\":true,\"latency_ms\":3}}";

static const char warning_result[] =
    "{\"memory\":{\"total_bytes\":1000,\"available_bytes\":100,"
    "\"used_bytes\":900,\"used_percent\":90},"
    "\"disk\":{\"used_percent\":20},"
    "\"service\":{\"active_state\":\"active\"},"
    "\"port\":{\"reachable\":true,\"latency_ms\":3}}";

static void fail(const char *expression, int line)
{
  fprintf(stderr, "FAIL line %d: %s\n", line, expression);
  exit(EXIT_FAILURE);
}

static velaops_incident_event_t apply(velaops_resource_incident_t *incident,
                                      const char *json, int64_t observed_at,
                                      char diagnosis[1024])
{
  velaops_incident_event_t event;

  EXPECTED(velaops_resource_incident_apply(
               incident, json, observed_at, &event, diagnosis, 1024) ==
           VELAOPS_RESOURCE_INCIDENT_OK);
  return event;
}

static void test_debounce_deduplicate_and_recover(void)
{
  velaops_resource_incident_t incident;
  char diagnosis[1024];

  EXPECTED(velaops_resource_incident_init(&incident, 2, 2) ==
           VELAOPS_RESOURCE_INCIDENT_OK);
  EXPECTED(apply(&incident, warning_result, 1, diagnosis) ==
           VELAOPS_INCIDENT_EVENT_NONE);
  EXPECTED(diagnosis[0] == '\0');
  EXPECTED(apply(&incident, warning_result, 2, diagnosis) ==
           VELAOPS_INCIDENT_EVENT_OPENED);
  EXPECTED(strstr(diagnosis, "\"status\":\"warning\"") != NULL);
  EXPECTED(incident.tracker.generation == 1);

  EXPECTED(apply(&incident, warning_result, 3, diagnosis) ==
           VELAOPS_INCIDENT_EVENT_NONE);
  EXPECTED(diagnosis[0] == '\0');
  EXPECTED(apply(&incident, normal_result, 4, diagnosis) ==
           VELAOPS_INCIDENT_EVENT_NONE);
  EXPECTED(apply(&incident, normal_result, 5, diagnosis) ==
           VELAOPS_INCIDENT_EVENT_RECOVERED);
  EXPECTED(strstr(diagnosis, "\"status\":\"normal\"") != NULL);
}

static void test_invalid_evidence_does_not_change_state(void)
{
  velaops_resource_incident_t incident;
  velaops_resource_incident_t before;
  velaops_incident_event_t event = VELAOPS_INCIDENT_EVENT_OPENED;
  char diagnosis[1024] = "unchanged";

  EXPECTED(velaops_resource_incident_init(&incident, 2, 2) ==
           VELAOPS_RESOURCE_INCIDENT_OK);
  before = incident;
  EXPECTED(velaops_resource_incident_apply(
               &incident, "{}", 1, &event, diagnosis, sizeof(diagnosis)) ==
           VELAOPS_RESOURCE_INCIDENT_INVALID_EVIDENCE);
  EXPECTED(memcmp(&incident, &before, sizeof(incident)) == 0);
  EXPECTED(event == VELAOPS_INCIDENT_EVENT_NONE);
  EXPECTED(diagnosis[0] == '\0');
}

static void test_rejects_invalid_and_out_of_order_input(void)
{
  velaops_resource_incident_t incident;
  velaops_incident_event_t event;
  char diagnosis[1024];

  EXPECTED(velaops_resource_incident_init(NULL, 2, 2) ==
           VELAOPS_RESOURCE_INCIDENT_INVALID_ARGUMENT);
  EXPECTED(velaops_resource_incident_init(&incident, 0, 2) ==
           VELAOPS_RESOURCE_INCIDENT_INVALID_ARGUMENT);
  EXPECTED(velaops_resource_incident_init(&incident, 1, 1) ==
           VELAOPS_RESOURCE_INCIDENT_OK);
  EXPECTED(apply(&incident, warning_result, 10, diagnosis) ==
           VELAOPS_INCIDENT_EVENT_OPENED);
  EXPECTED(velaops_resource_incident_apply(
               &incident, warning_result, 9, &event, diagnosis,
               sizeof(diagnosis)) == VELAOPS_RESOURCE_INCIDENT_OUT_OF_ORDER);
}

int main(void)
{
  test_debounce_deduplicate_and_recover();
  test_invalid_evidence_does_not_change_state();
  test_rejects_invalid_and_out_of_order_input();
  puts("PASS: VelaOps resource incident tests");
  return EXIT_SUCCESS;
}
