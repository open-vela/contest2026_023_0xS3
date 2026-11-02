/****************************************************************************
 * VelaOps Proxy 内存结果适配器主机测试。
 ****************************************************************************/

#include "velaops_memory_result.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPECTED(value) do { if (!(value)) fail(#value, __LINE__); } while (0)

static void fail(const char *expression, int line)
{
  fprintf(stderr, "FAIL line %d: %s\n", line, expression);
  exit(EXIT_FAILURE);
}

static void test_valid_result(void)
{
  const char json[] =
      "{\"total_bytes\":16478060544,\"available_bytes\":11209646080,"
      "\"used_bytes\":5268414464,\"used_percent\":31.97}";
  velaops_memory_observation_t observation;

  EXPECTED(velaops_memory_result_parse(json, &observation) ==
           VELAOPS_MEMORY_RESULT_OK);
  EXPECTED(observation.total_bytes == UINT64_C(16478060544));
  EXPECTED(observation.available_bytes == UINT64_C(11209646080));
  EXPECTED(observation.used_bytes == UINT64_C(5268414464));
  EXPECTED(observation.used_percent == 31.97);
}

static void test_accepts_check_resources_container(void)
{
  const char json[] =
      "{\"memory\":{\"total_bytes\":16478060544,"
      "\"available_bytes\":11209646080,\"used_bytes\":5268414464,"
      "\"used_percent\":31.97},\"disk\":{\"alias\":\"root\"}}";
  velaops_memory_observation_t observation;

  EXPECTED(velaops_memory_result_parse(json, &observation) ==
           VELAOPS_MEMORY_RESULT_OK);
  EXPECTED(observation.total_bytes == UINT64_C(16478060544));
  EXPECTED(observation.used_percent == 31.97);
}

static void test_rejects_schema_drift(void)
{
  velaops_memory_observation_t observation;
  const char *invalid[] = {
    "{\"total_bytes\":100,\"available_bytes\":50,"
      "\"used_bytes\":50}",
    "{\"total_bytes\":100,\"available_bytes\":50,"
      "\"used_bytes\":50,\"used_percent\":50,\"extra\":1}",
    "{\"total_bytes\":100,\"total_bytes\":100,"
      "\"available_bytes\":50,\"used_bytes\":50,\"used_percent\":50}",
    "{\"total_bytes\":100.5,\"available_bytes\":50,"
      "\"used_bytes\":50,\"used_percent\":50}",
    "{\"total_bytes\":100,\"available_bytes\":-1,"
      "\"used_bytes\":101,\"used_percent\":101}",
    "{\"total_bytes\":9007199254740992,\"available_bytes\":1,"
      "\"used_bytes\":1,\"used_percent\":1}"
  };
  size_t index;

  for (index = 0; index < sizeof(invalid) / sizeof(invalid[0]); index++)
    {
      EXPECTED(velaops_memory_result_parse(invalid[index], &observation) ==
               VELAOPS_MEMORY_RESULT_INVALID_FIELD);
    }
}

static void test_invalid_input_does_not_write_output(void)
{
  velaops_memory_observation_t observation = {1, 2, 3, 4.0};
  velaops_memory_observation_t before = observation;

  EXPECTED(velaops_memory_result_parse("[]", &observation) ==
           VELAOPS_MEMORY_RESULT_INVALID_JSON);
  EXPECTED(velaops_memory_result_parse("{} trailing", &observation) ==
           VELAOPS_MEMORY_RESULT_INVALID_JSON);
  EXPECTED(memcmp(&observation, &before, sizeof(observation)) == 0);
  EXPECTED(velaops_memory_result_parse(NULL, &observation) ==
           VELAOPS_MEMORY_RESULT_INVALID_ARGUMENT);
}

int main(void)
{
  test_valid_result();
  test_accepts_check_resources_container();
  test_rejects_schema_drift();
  test_invalid_input_does_not_write_output();
  puts("PASS: VelaOps memory result adapter tests");
  return EXIT_SUCCESS;
}
