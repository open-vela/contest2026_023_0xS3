/****************************************************************************
 * VelaOps 固定白名单修复请求与独立复核规则实现。
 ****************************************************************************/

#include "velaops_guarded_repair.h"

#include <stdio.h>
#include <string.h>

#include "velaops_resource_result.h"

#define VELAOPS_APPROVAL_MAX_VALIDITY_SECONDS 120

static bool velaops_is_hex_id(const char *value)
{
  size_t index;

  if (value == NULL || strlen(value) != 32)
    {
      return false;
    }
  for (index = 0; index < 32; index++)
    {
      if (!((value[index] >= '0' && value[index] <= '9') ||
            (value[index] >= 'a' && value[index] <= 'f')))
        {
          return false;
        }
    }
  return true;
}

int velaops_guarded_repair_build_request(
    const char *approval_id, int64_t approved_at, int64_t expires_at,
    char *output, size_t output_capacity)
{
  int length;

  if (!velaops_is_hex_id(approval_id) || approved_at < 0 ||
      expires_at <= approved_at ||
      expires_at - approved_at > VELAOPS_APPROVAL_MAX_VALIDITY_SECONDS ||
      output == NULL || output_capacity == 0)
    {
      return -1;
    }
  length = snprintf(
      output, output_capacity,
      "{\"schema_version\":1,\"action\":\"restart_service\","
      "\"target\":\"local-dev\",\"parameters\":{\"service\":\"demo\"},"
      "\"approval\":{\"approval_id\":\"%s\",\"approved_at\":%lld,"
      "\"expires_at\":%lld,\"source\":\"physical_button\"}}",
      approval_id, (long long)approved_at, (long long)expires_at);
  return length >= 0 && (size_t)length < output_capacity ? 0 : -1;
}

int velaops_guarded_repair_verify(const char *resource_json, bool *recovered)
{
  velaops_resource_observation_t resources;

  if (recovered == NULL ||
      velaops_resource_result_parse(resource_json, &resources) != 0)
    {
      return -1;
    }
  *recovered = resources.service_active && resources.port_reachable;
  return 0;
}

int velaops_guarded_repair_format_result(
    const char *execution_state, bool verified, const char *status,
    unsigned int attempts, char *output, size_t output_capacity)
{
  int length;

  if (execution_state == NULL || status == NULL || output == NULL ||
      output_capacity == 0 ||
      (strcmp(execution_state, "not_started") != 0 &&
       strcmp(execution_state, "unknown") != 0 &&
       strcmp(execution_state, "completed") != 0) ||
      (strcmp(status, "recovered") != 0 &&
       strcmp(status, "approval_timeout") != 0 &&
       strcmp(status, "approval_unavailable") != 0 &&
       strcmp(status, "execution_failed") != 0 &&
       strcmp(status, "verification_failed") != 0))
    {
      return -1;
    }
  length = snprintf(
      output, output_capacity,
      "{\"schema_version\":1,\"action\":\"restart_service\","
      "\"target\":\"demo\",\"approval_source\":\"physical_button\","
      "\"execution_state\":\"%s\",\"verified\":%s,\"status\":\"%s\","
      "\"verification_attempts\":%u}",
      execution_state, verified ? "true" : "false", status, attempts);
  return length >= 0 && (size_t)length < output_capacity ? 0 : -1;
}
