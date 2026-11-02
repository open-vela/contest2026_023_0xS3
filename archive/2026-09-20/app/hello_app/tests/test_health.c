/****************************************************************************
 * VelaOps 健康规则主机测试。
 ****************************************************************************/

#include "velaops_health.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define EXPECTED(value) do { if (!(value)) fail(#value, __LINE__); } while (0)

static void fail(const char *expression, int line)
{
  fprintf(stderr, "FAIL line %d: %s\n", line, expression);
  exit(EXIT_FAILURE);
}

static velaops_memory_observation_t observation(double percent)
{
  velaops_memory_observation_t value = {
    .total_bytes = 10000,
    .available_bytes = (uint64_t)(10000.0 - percent * 100.0),
    .used_bytes = (uint64_t)(percent * 100.0),
    .used_percent = percent
  };
  return value;
}

static void test_threshold_boundary(void)
{
  velaops_memory_observation_t value = observation(79.0);
  velaops_health_snapshot_t snapshot;

  EXPECTED(velaops_evaluate_memory(&value, 80.0, 100, &snapshot) ==
           VELAOPS_HEALTH_OK);
  EXPECTED(snapshot.result == VELAOPS_HEALTH_HEALTHY);
  EXPECTED(snapshot.observed_at == 100);

  value = observation(80.0);
  EXPECTED(velaops_evaluate_memory(&value, 80.0, 101, &snapshot) ==
           VELAOPS_HEALTH_OK);
  EXPECTED(snapshot.result == VELAOPS_HEALTH_UNHEALTHY);
}

static void test_invalid_observation(void)
{
  velaops_memory_observation_t value = observation(40.0);
  velaops_health_snapshot_t snapshot;

  value.used_bytes++;
  EXPECTED(velaops_evaluate_memory(&value, 80.0, 1, &snapshot) ==
           VELAOPS_HEALTH_INVALID_OBSERVATION);

  value = observation(40.0);
  value.used_percent = 41.0;
  EXPECTED(velaops_evaluate_memory(&value, 80.0, 1, &snapshot) ==
           VELAOPS_HEALTH_INVALID_OBSERVATION);

  value = observation(40.0);
  value.used_percent = NAN;
  EXPECTED(velaops_evaluate_memory(&value, 80.0, 1, &snapshot) ==
           VELAOPS_HEALTH_INVALID_OBSERVATION);
}

static void test_invalid_arguments_do_not_write_output(void)
{
  velaops_memory_observation_t value = observation(40.0);
  velaops_health_snapshot_t snapshot = {123, VELAOPS_HEALTH_UNHEALTHY};

  EXPECTED(velaops_evaluate_memory(&value, 0.0, 1, &snapshot) ==
           VELAOPS_HEALTH_INVALID_ARGUMENT);
  EXPECTED(snapshot.observed_at == 123);
  EXPECTED(snapshot.result == VELAOPS_HEALTH_UNHEALTHY);
  EXPECTED(velaops_evaluate_memory(&value, 80.0, -1, &snapshot) ==
           VELAOPS_HEALTH_INVALID_ARGUMENT);
  EXPECTED(velaops_evaluate_memory(NULL, 80.0, 1, &snapshot) ==
           VELAOPS_HEALTH_INVALID_ARGUMENT);
}

int main(void)
{
  test_threshold_boundary();
  test_invalid_observation();
  test_invalid_arguments_do_not_write_output();
  puts("PASS: VelaOps health rule tests");
  return EXIT_SUCCESS;
}
