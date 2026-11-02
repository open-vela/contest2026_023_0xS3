/****************************************************************************
 * VelaOps Proxy 聚合资源结果适配器主机测试。
 ****************************************************************************/

#include "velaops_resource_result.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPECTED(value) do { if (!(value)) fail(#value, __LINE__); } while (0)

static const char valid_result[] =
    "{\"memory\":{\"total_bytes\":1000,\"available_bytes\":600,"
    "\"used_bytes\":400,\"used_percent\":40.0},"
    "\"disk\":{\"alias\":\"root\",\"total_bytes\":2000,"
    "\"used_bytes\":500,\"free_bytes\":1500,\"used_percent\":25.0},"
    "\"service\":{\"alias\":\"proxy\",\"load_state\":\"loaded\","
    "\"active_state\":\"active\",\"sub_state\":\"running\","
    "\"result\":\"success\",\"main_exit_status\":0},"
    "\"port\":{\"alias\":\"proxy-http\",\"reachable\":true,"
    "\"latency_ms\":3},"
    "\"cpu\":{\"used_percent\":12.5,\"cores\":8,\"load1\":0.5,"
    "\"load5\":0.4,\"load15\":0.3}}";

static void fail(const char *expression, int line)
{
  fprintf(stderr, "FAIL line %d: %s\n", line, expression);
  exit(EXIT_FAILURE);
}

static void test_parses_dashboard_fields(void)
{
  velaops_resource_observation_t result;

  EXPECTED(velaops_resource_result_parse(valid_result, &result) == 0);
  EXPECTED(result.memory.total_bytes == 1000);
  EXPECTED(result.memory.available_bytes == 600);
  EXPECTED(result.memory.used_bytes == 400);
  EXPECTED(result.memory.used_percent == 40.0);
  EXPECTED(result.disk_percent == 25.0);
  EXPECTED(result.service_active);
  EXPECTED(result.port_reachable);
  EXPECTED(result.port_latency_ms == 3);
  EXPECTED(result.cpu.valid);
  EXPECTED(result.cpu.used_percent == 12.5);
  EXPECTED(result.cpu.cores == 8);
  EXPECTED(result.cpu.load1 == 0.5);
  EXPECTED(result.cpu.load5 == 0.4);
  EXPECTED(result.cpu.load15 == 0.3);
}

static void test_cpu_is_optional(void)
{
  velaops_resource_observation_t result;

  EXPECTED(velaops_resource_result_parse(
               "{\"memory\":{\"total_bytes\":1000,\"available_bytes\":600,"
               "\"used_bytes\":400,\"used_percent\":40.0},"
               "\"disk\":{\"used_percent\":25.0},"
               "\"service\":{\"active_state\":\"active\"},"
               "\"port\":{\"reachable\":true,\"latency_ms\":3}}",
               &result) == 0);
  EXPECTED(!result.cpu.valid);
}

static void test_invalid_result_does_not_replace_last_snapshot(void)
{
  velaops_resource_observation_t result = {
    .memory = {100, 60, 40, 40.0},
    .disk_percent = 25.0,
    .service_active = true,
    .port_reachable = true,
    .port_latency_ms = 3
  };
  velaops_resource_observation_t before = result;

  EXPECTED(velaops_resource_result_parse(
               "{\"memory\":{},\"disk\":{},\"service\":{},\"port\":{}}",
               &result) < 0);
  EXPECTED(memcmp(&result, &before, sizeof(result)) == 0);
}

static void test_rejects_impossible_values(void)
{
  velaops_resource_observation_t result;
  const char invalid[] =
      "{\"memory\":{\"total_bytes\":100,\"available_bytes\":101,"
      "\"used_bytes\":40,\"used_percent\":101},"
      "\"disk\":{\"used_percent\":-1},"
      "\"service\":{\"active_state\":\"active\"},"
      "\"port\":{\"reachable\":true,\"latency_ms\":-1}}";

  EXPECTED(velaops_resource_result_parse(invalid, &result) < 0);
}

int main(void)
{
  test_parses_dashboard_fields();
  test_cpu_is_optional();
  test_invalid_result_does_not_replace_last_snapshot();
  test_rejects_impossible_values();
  puts("PASS: VelaOps resource result adapter tests");
  return EXIT_SUCCESS;
}
