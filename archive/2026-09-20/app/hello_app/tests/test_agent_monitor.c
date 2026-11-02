#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/message_bus.h"
#include "velaops_agent_monitor.h"

static int g_failures;
static int g_sink_calls;
static int g_sink_failures_remaining;
static char g_last_prompt[512];

#define EXPECT(condition)                                                       \
  do                                                                            \
    {                                                                           \
      if (!(condition))                                                         \
        {                                                                       \
          fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
          g_failures++;                                                         \
        }                                                                       \
    }                                                                           \
  while (0)

int message_bus_push_inbound(const agent_msg_t *message)
{
  (void)message;
  return OK;
}

static int fake_sink(const char *prompt)
{
  g_sink_calls++;
  if (g_sink_failures_remaining > 0)
    {
      g_sink_failures_remaining--;
      return -1;
    }
  snprintf(g_last_prompt, sizeof(g_last_prompt), "%s", prompt);
  return 0;
}

static const char warning_result[] =
    "{\"memory\":{\"total_bytes\":1000,\"available_bytes\":700,"
    "\"used_bytes\":300,\"used_percent\":30.0},"
    "\"disk\":{\"alias\":\"root\",\"total_bytes\":1000,"
    "\"used_bytes\":920,\"free_bytes\":80,\"used_percent\":92.0},"
    "\"service\":{\"alias\":\"proxy\",\"load_state\":\"loaded\","
    "\"active_state\":\"active\",\"sub_state\":\"running\","
    "\"result\":\"success\",\"main_exit_status\":0},"
    "\"port\":{\"alias\":\"proxy\",\"reachable\":true,"
    "\"latency_ms\":1}}";

static const char normal_result[] =
    "{\"memory\":{\"total_bytes\":1000,\"available_bytes\":700,"
    "\"used_bytes\":300,\"used_percent\":30.0},"
    "\"disk\":{\"alias\":\"root\",\"total_bytes\":1000,"
    "\"used_bytes\":500,\"free_bytes\":500,\"used_percent\":50.0},"
    "\"service\":{\"alias\":\"proxy\",\"load_state\":\"loaded\","
    "\"active_state\":\"active\",\"sub_state\":\"running\","
    "\"result\":\"success\",\"main_exit_status\":0},"
    "\"port\":{\"alias\":\"proxy\",\"reachable\":true,"
    "\"latency_ms\":1}}";

static void reset_sink(void)
{
  g_sink_calls = 0;
  g_sink_failures_remaining = 0;
  g_last_prompt[0] = '\0';
}

static void test_open_dedupe_and_recover(void)
{
  velaops_agent_monitor_t monitor;

  reset_sink();
  EXPECT(velaops_agent_monitor_init(&monitor, 2, 2, fake_sink) == 0);
  EXPECT(velaops_agent_monitor_sample(&monitor, warning_result, 1) ==
         VELAOPS_AGENT_MONITOR_OK);
  EXPECT(g_sink_calls == 0);
  EXPECT(velaops_agent_monitor_sample(&monitor, warning_result, 2) ==
         VELAOPS_AGENT_MONITOR_OK);
  EXPECT(g_sink_calls == 1);
  EXPECT(strstr(g_last_prompt, "type=opened generation=1") != NULL);
  EXPECT(strstr(g_last_prompt,
                "/data/ai_agent/skills/server-incident-response.md") != NULL);
  EXPECT(strstr(g_last_prompt, "92.0") == NULL);

  EXPECT(velaops_agent_monitor_sample(&monitor, warning_result, 3) ==
         VELAOPS_AGENT_MONITOR_OK);
  EXPECT(g_sink_calls == 1);
  EXPECT(velaops_agent_monitor_sample(&monitor, normal_result, 4) ==
         VELAOPS_AGENT_MONITOR_OK);
  EXPECT(velaops_agent_monitor_sample(&monitor, normal_result, 5) ==
         VELAOPS_AGENT_MONITOR_OK);
  EXPECT(g_sink_calls == 2);
  EXPECT(strstr(g_last_prompt, "type=recovered generation=1") != NULL);
}

static void test_failed_dispatch_is_retried(void)
{
  velaops_agent_monitor_t monitor;

  reset_sink();
  g_sink_failures_remaining = 1;
  EXPECT(velaops_agent_monitor_init(&monitor, 1, 1, fake_sink) == 0);
  EXPECT(velaops_agent_monitor_sample(&monitor, warning_result, 10) ==
         VELAOPS_AGENT_MONITOR_DISPATCH_ERROR);
  EXPECT(g_sink_calls == 1);
  EXPECT(velaops_agent_monitor_sample(&monitor, warning_result, 11) ==
         VELAOPS_AGENT_MONITOR_OK);
  EXPECT(g_sink_calls == 2);
  EXPECT(strstr(g_last_prompt, "type=opened generation=1") != NULL);
}

static void test_invalid_inputs(void)
{
  velaops_agent_monitor_t monitor;

  reset_sink();
  EXPECT(velaops_agent_monitor_init(NULL, 1, 1, fake_sink) != 0);
  EXPECT(velaops_agent_monitor_init(&monitor, 1, 1, NULL) != 0);
  EXPECT(velaops_agent_monitor_init(&monitor, 1, 1, fake_sink) == 0);
  EXPECT(velaops_agent_monitor_sample(&monitor, NULL, 1) ==
         VELAOPS_AGENT_MONITOR_INVALID_ARGUMENT);
  EXPECT(velaops_agent_monitor_sample(&monitor, "{}", 1) ==
         VELAOPS_AGENT_MONITOR_INVALID_EVIDENCE);
  EXPECT(g_sink_calls == 0);
}

int main(void)
{
  test_open_dedupe_and_recover();
  test_failed_dispatch_is_retried();
  test_invalid_inputs();
  if (g_failures != 0)
    {
      return EXIT_FAILURE;
    }
  puts("PASS: VelaOps AI Agent monitor tests");
  return EXIT_SUCCESS;
}
