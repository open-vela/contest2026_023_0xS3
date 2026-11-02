/****************************************************************************
 * VelaOps 设备私密配置主机单元测试。
 ****************************************************************************/

#include "velaops_device_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPECTED(value) do { if (!(value)) fail(#value, __LINE__); } while (0)

static void fail(const char *expression, int line)
{
  fprintf(stderr, "FAIL line %d: %s\n", line, expression);
  exit(EXIT_FAILURE);
}

static void test_loads_valid_config(void)
{
  velaops_device_config_t config;

  EXPECTED(velaops_device_config_load(
               "fixtures/device_config.json", &config) == VELAOPS_CONFIG_OK);
  EXPECTED(strcmp(config.host, "proxy.local") == 0);
  EXPECTED(strcmp(config.port, "8443") == 0);
  EXPECTED(strcmp(config.device_id, "eye-001") == 0);
  EXPECTED(strlen(config.secret) >= 32);
  velaops_device_config_clear(&config);
  EXPECTED(config.secret[0] == '\0');
}

static void test_rejects_unknown_fields(void)
{
  velaops_device_config_t config;

  EXPECTED(velaops_device_config_load(
               "fixtures/device_config_unknown.json", &config) ==
           VELAOPS_CONFIG_INVALID_FIELD);
  EXPECTED(config.secret[0] == '\0');
}

static void test_reports_missing_files(void)
{
  velaops_device_config_t config;

  EXPECTED(velaops_device_config_load(
               "fixtures/missing.json", &config) == VELAOPS_CONFIG_IO_ERROR);
}

int main(void)
{
  test_loads_valid_config();
  test_rejects_unknown_fields();
  test_reports_missing_files();
  puts("PASS: VelaOps device config tests");
  return EXIT_SUCCESS;
}
