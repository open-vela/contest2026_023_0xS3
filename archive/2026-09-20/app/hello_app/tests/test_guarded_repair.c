#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "velaops_guarded_repair.h"

static int failures;

#define EXPECT(condition)                                                       \
  do                                                                            \
    {                                                                           \
      if (!(condition))                                                         \
        {                                                                       \
          fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
          failures++;                                                           \
        }                                                                       \
    }                                                                           \
  while (0)

static const char healthy_resources[] =
    "{\"memory\":{\"total_bytes\":1000,\"available_bytes\":700,"
    "\"used_bytes\":300,\"used_percent\":30.0},"
    "\"disk\":{\"used_percent\":50.0},"
    "\"service\":{\"active_state\":\"active\"},"
    "\"port\":{\"reachable\":true,\"latency_ms\":1}}";

static const char unhealthy_resources[] =
    "{\"memory\":{\"total_bytes\":1000,\"available_bytes\":700,"
    "\"used_bytes\":300,\"used_percent\":30.0},"
    "\"disk\":{\"used_percent\":50.0},"
    "\"service\":{\"active_state\":\"active\"},"
    "\"port\":{\"reachable\":false,\"latency_ms\":1}}";

static void test_builds_fixed_request(void)
{
  char output[512];
  cJSON *root;
  cJSON *parameters;
  cJSON *approval;

  EXPECT(velaops_guarded_repair_build_request(
             "0123456789abcdef0123456789abcdef", 1000, 1060,
             output, sizeof(output)) == 0);
  root = cJSON_Parse(output);
  EXPECT(cJSON_IsObject(root));
  EXPECT(strcmp(cJSON_GetObjectItemCaseSensitive(root, "action")->valuestring,
                "restart_service") == 0);
  EXPECT(strcmp(cJSON_GetObjectItemCaseSensitive(root, "target")->valuestring,
                "local-dev") == 0);
  parameters = cJSON_GetObjectItemCaseSensitive(root, "parameters");
  EXPECT(strcmp(cJSON_GetObjectItemCaseSensitive(
                    parameters, "service")->valuestring, "demo") == 0);
  approval = cJSON_GetObjectItemCaseSensitive(root, "approval");
  EXPECT(strcmp(cJSON_GetObjectItemCaseSensitive(
                    approval, "source")->valuestring, "physical_button") == 0);
  cJSON_Delete(root);

  EXPECT(velaops_guarded_repair_build_request("not-hex", 1000, 1060,
                                               output, sizeof(output)) != 0);
  EXPECT(velaops_guarded_repair_build_request(
             "0123456789abcdef0123456789abcdef", 1000, 1121,
             output, sizeof(output)) != 0);
  EXPECT(velaops_guarded_repair_build_request(
             "0123456789abcdef0123456789abcdef", 1000, 1060,
             output, 16) != 0);
}

static void test_independent_verification(void)
{
  bool recovered = false;

  EXPECT(velaops_guarded_repair_verify(healthy_resources, &recovered) == 0);
  EXPECT(recovered);
  EXPECT(velaops_guarded_repair_verify(unhealthy_resources, &recovered) == 0);
  EXPECT(!recovered);
  EXPECT(velaops_guarded_repair_verify("{}", &recovered) != 0);
}

static void test_formats_bounded_result(void)
{
  char output[320];
  cJSON *root;

  EXPECT(velaops_guarded_repair_format_result(
             "completed", true, "recovered", 2,
             output, sizeof(output)) == 0);
  root = cJSON_Parse(output);
  EXPECT(cJSON_IsObject(root));
  EXPECT(strcmp(cJSON_GetObjectItemCaseSensitive(
                    root, "execution_state")->valuestring,
                "completed") == 0);
  EXPECT(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "verified")));
  EXPECT(cJSON_GetObjectItemCaseSensitive(
             root, "verification_attempts")->valueint == 2);
  cJSON_Delete(root);
  EXPECT(velaops_guarded_repair_format_result(
             "not_started", false, "arbitrary", 0,
             output, sizeof(output)) != 0);
  EXPECT(velaops_guarded_repair_format_result(
             "bad", false, "approval_timeout", 0,
             output, sizeof(output)) != 0);
}

int main(void)
{
  test_builds_fixed_request();
  test_independent_verification();
  test_formats_bounded_result();
  if (failures != 0)
    {
      return EXIT_FAILURE;
    }
  puts("PASS: VelaOps guarded repair tests");
  return EXIT_SUCCESS;
}
