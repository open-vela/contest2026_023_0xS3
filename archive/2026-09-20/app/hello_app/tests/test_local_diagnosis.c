/****************************************************************************
 * VelaOps 本地规则诊断器主机测试。
 ****************************************************************************/

#include "velaops_local_diagnosis.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#define EXPECTED(value) do { if (!(value)) fail(#value, __LINE__); } while (0)

static const char result_template[] =
    "{\"memory\":{\"total_bytes\":1000,\"available_bytes\":600,"
    "\"used_bytes\":400,\"used_percent\":%.2f},"
    "\"disk\":{\"used_percent\":%.2f},"
    "\"service\":{\"active_state\":\"%s\"},"
    "\"port\":{\"reachable\":%s,\"latency_ms\":3}}";

static void fail(const char *expression, int line)
{
  fprintf(stderr, "FAIL line %d: %s\n", line, expression);
  exit(EXIT_FAILURE);
}

static cJSON *diagnose(double memory, double disk, const char *service,
                       const char *reachable)
{
  char input[512];
  char output[1024];
  cJSON *root;

  EXPECTED(snprintf(input, sizeof(input), result_template, memory, disk,
                    service, reachable) > 0);
  EXPECTED(velaops_local_diagnosis_build(input, output, sizeof(output)) == 0);
  EXPECTED(strlen(output) <= 700);
  root = cJSON_Parse(output);
  EXPECTED(cJSON_IsObject(root));
  EXPECTED(cJSON_GetArraySize(root) == 7);
  EXPECTED(cJSON_GetObjectItemCaseSensitive(root,
                                            "schema_version")->valueint == 1);
  EXPECTED(cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(root,
                                                          "evidence")));
  EXPECTED(cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(
      root, "root_cause_candidates")));
  EXPECTED(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(
      root, "root_cause_candidates")) == 0);
  EXPECTED(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(
      cJSON_GetObjectItemCaseSensitive(root, "recommended_action"),
      "requires_physical_approval")));
  return root;
}

static void expect_status_action(cJSON *root, const char *status,
                                 const char *action)
{
  cJSON *recommendation = cJSON_GetObjectItemCaseSensitive(
      root, "recommended_action");

  EXPECTED(strcmp(cJSON_GetObjectItemCaseSensitive(root, "status")
                      ->valuestring,
                  status) == 0);
  EXPECTED(strcmp(cJSON_GetObjectItemCaseSensitive(recommendation, "action")
                      ->valuestring,
                  action) == 0);
  EXPECTED(strncmp(cJSON_GetObjectItemCaseSensitive(root, "summary")
                       ->valuestring,
                   "本地规则降级：", strlen("本地规则降级：")) == 0);
}

static void test_normal_and_warning_thresholds(void)
{
  cJSON *root = diagnose(79.99, 84.99, "active", "true");

  expect_status_action(root, "normal", "none");
  EXPECTED(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(
      root, "evidence")) == 2);
  cJSON_Delete(root);

  root = diagnose(80.0, 90.0, "active", "true");
  expect_status_action(root, "warning", "none");
  EXPECTED(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(
      root, "evidence")) == 2);
  cJSON_Delete(root);
}

static void test_critical_takes_priority(void)
{
  cJSON *root = diagnose(90.0, 95.0, "inactive", "false");
  cJSON *recommendation;

  expect_status_action(root, "critical", "restart_service");
  recommendation = cJSON_GetObjectItemCaseSensitive(
      root, "recommended_action");
  EXPECTED(strcmp(cJSON_GetObjectItemCaseSensitive(recommendation, "target")
                      ->valuestring,
                  "proxy") == 0);
  EXPECTED(strcmp(cJSON_GetObjectItemCaseSensitive(recommendation, "risk")
                      ->valuestring,
                  "change") == 0);
  cJSON_Delete(root);
}

static void test_invalid_evidence_fails_closed(void)
{
  char output[1024];
  char too_small[8];
  cJSON *root;

  EXPECTED(velaops_local_diagnosis_build("{}", output, sizeof(output)) == 0);
  root = cJSON_Parse(output);
  EXPECTED(cJSON_IsObject(root));
  expect_status_action(root, "unknown", "retry_check");
  EXPECTED(cJSON_GetObjectItemCaseSensitive(root, "confidence")
               ->valuedouble == 0.0);
  cJSON_Delete(root);

  EXPECTED(velaops_local_diagnosis_build(NULL, output, sizeof(output)) == 0);
  EXPECTED(velaops_local_diagnosis_build("{}", too_small,
                                        sizeof(too_small)) != 0);
  EXPECTED(velaops_local_diagnosis_build("{}", NULL, 0) != 0);
}

int main(void)
{
  test_normal_and_warning_thresholds();
  test_critical_takes_priority();
  test_invalid_evidence_fails_closed();
  puts("PASS: VelaOps local diagnosis tests");
  return EXIT_SUCCESS;
}
