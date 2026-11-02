/****************************************************************************
 * VelaOps 本地规则诊断器实现。
 ****************************************************************************/

#include "velaops_local_diagnosis.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "velaops_resource_result.h"

#define VELAOPS_MEMORY_WARNING_PERCENT 80.0
/* 演示服务器磁盘基线就在 85% 附近长期驻留，85% 阈值会让主动事件永久触发；
 * 事件判定用 90%，屏显配色仍保留 85% 的视觉提醒。 */
#define VELAOPS_DISK_WARNING_PERCENT 90.0
/* CPU 使用率告警阈值：压测/满载时触发主动事件与 LLM 诊断。 */
#define VELAOPS_CPU_WARNING_PERCENT 85.0

static velaops_diagnosis_status_t velaops_evaluate_observation(
    const velaops_resource_observation_t *observation)
{
  if (!observation->service_active || !observation->port_reachable)
    {
      return VELAOPS_DIAGNOSIS_CRITICAL;
    }
  if (observation->memory.used_percent >= VELAOPS_MEMORY_WARNING_PERCENT ||
      observation->disk_percent >= VELAOPS_DISK_WARNING_PERCENT ||
      (observation->cpu.valid &&
       observation->cpu.used_percent >= VELAOPS_CPU_WARNING_PERCENT))
    {
      return VELAOPS_DIAGNOSIS_WARNING;
    }
  return VELAOPS_DIAGNOSIS_NORMAL;
}

int velaops_local_diagnosis_evaluate(
    const char *resource_json, velaops_diagnosis_status_t *status)
{
  velaops_resource_observation_t observation;

  if (status == NULL)
    {
      return -1;
    }
  *status = VELAOPS_DIAGNOSIS_UNKNOWN;
  if (resource_json == NULL ||
      velaops_resource_result_parse(resource_json, &observation) != 0)
    {
      return -1;
    }
  *status = velaops_evaluate_observation(&observation);
  return 0;
}

int velaops_local_diagnosis_evaluate_observation(
    const velaops_resource_observation_t *observation,
    velaops_diagnosis_status_t *status)
{
  if (observation == NULL || status == NULL)
    {
      return -1;
    }
  *status = velaops_evaluate_observation(observation);
  return 0;
}

static int velaops_add_evidence(cJSON *evidence, const char *metric,
                                const char *value, const char *reason)
{
  cJSON *item = cJSON_CreateObject();

  if (item == NULL ||
      cJSON_AddStringToObject(item, "metric", metric) == NULL ||
      cJSON_AddStringToObject(item, "value", value) == NULL ||
      cJSON_AddStringToObject(item, "reason", reason) == NULL)
    {
      cJSON_Delete(item);
      return -1;
    }
  cJSON_AddItemToArray(evidence, item);
  return 0;
}

static int velaops_add_recommendation(cJSON *root, const char *action,
                                      const char *target, const char *risk)
{
  cJSON *recommendation = cJSON_CreateObject();

  if (recommendation == NULL ||
      cJSON_AddStringToObject(recommendation, "action", action) == NULL ||
      cJSON_AddStringToObject(recommendation, "target", target) == NULL ||
      cJSON_AddStringToObject(recommendation, "risk", risk) == NULL ||
      cJSON_AddTrueToObject(recommendation,
                           "requires_physical_approval") == NULL)
    {
      cJSON_Delete(recommendation);
      return -1;
    }
  cJSON_AddItemToObject(root, "recommended_action", recommendation);
  return 0;
}

static int velaops_encode(cJSON *root, char *output, size_t output_capacity)
{
  char *encoded = cJSON_PrintUnformatted(root);
  size_t length;

  if (encoded == NULL)
    {
      return -1;
    }
  length = strlen(encoded);
  if (length >= output_capacity)
    {
      cJSON_free(encoded);
      return -1;
    }
  memcpy(output, encoded, length + 1);
  cJSON_free(encoded);
  return 0;
}

static int velaops_local_diagnosis_encode(
    const velaops_resource_observation_t *observation, int valid,
    char *output, size_t output_capacity)
{
  cJSON *root;
  cJSON *evidence;
  cJSON *causes;
  const char *status;
  const char *summary;
  const char *action = "none";
  const char *target = "";
  const char *risk = "read_only";
  char memory_value[24];
  char disk_value[24];
  char cpu_value[24];
  velaops_diagnosis_status_t diagnosis_status = VELAOPS_DIAGNOSIS_UNKNOWN;
  int result = -1;

  if (output == NULL || output_capacity == 0)
    {
      return -1;
    }
  output[0] = '\0';
  if (valid)
    {
      diagnosis_status = velaops_evaluate_observation(observation);
    }

  root = cJSON_CreateObject();
  evidence = cJSON_CreateArray();
  causes = cJSON_CreateArray();
  if (root == NULL || evidence == NULL || causes == NULL)
    {
      cJSON_Delete(root);
      cJSON_Delete(evidence);
      cJSON_Delete(causes);
      return -1;
    }

  if (!valid)
    {
      status = "unknown";
      summary = "本地规则降级：资源证据不可用";
      action = "retry_check";
      if (velaops_add_evidence(evidence, "result", "invalid",
                               "资源证据缺失或格式错误") != 0)
        {
          goto cleanup;
        }
    }
  else if (diagnosis_status == VELAOPS_DIAGNOSIS_CRITICAL)
    {
      status = "critical";
      summary = "本地规则降级：代理服务不可用";
      action = "restart_service";
      target = "proxy";
      risk = "change";
      if ((!observation->service_active &&
           velaops_add_evidence(evidence, "result.service.active_state",
                                "not_active", "服务状态不为active") != 0) ||
          (!observation->port_reachable &&
           velaops_add_evidence(evidence, "result.port.reachable",
                                "false", "代理端口不可达") != 0))
        {
          goto cleanup;
        }
    }
  else if (diagnosis_status == VELAOPS_DIAGNOSIS_WARNING)
    {
      status = "warning";
      summary = "本地规则降级：资源达到告警阈值";
      snprintf(memory_value, sizeof(memory_value), "%.2f",
               observation->memory.used_percent);
      snprintf(disk_value, sizeof(disk_value), "%.2f",
               observation->disk_percent);
      snprintf(cpu_value, sizeof(cpu_value), "%.2f",
               observation->cpu.used_percent);
      if ((observation->memory.used_percent >=
               VELAOPS_MEMORY_WARNING_PERCENT &&
           velaops_add_evidence(evidence, "result.memory.used_percent",
                                memory_value, "达到80%告警阈值") != 0) ||
           (observation->disk_percent >= VELAOPS_DISK_WARNING_PERCENT &&
            velaops_add_evidence(evidence, "result.disk.used_percent",
                                 disk_value, "达到90%告警阈值") != 0) ||
           (observation->cpu.valid &&
            observation->cpu.used_percent >= VELAOPS_CPU_WARNING_PERCENT &&
            velaops_add_evidence(evidence, "result.cpu.used_percent",
                                 cpu_value, "达到85%告警阈值") != 0))
        {
          goto cleanup;
        }
    }
  else
    {
      status = "normal";
      summary = "本地规则降级：服务器资源正常";
      snprintf(memory_value, sizeof(memory_value), "%.2f",
               observation->memory.used_percent);
      snprintf(disk_value, sizeof(disk_value), "%.2f",
               observation->disk_percent);
      if (velaops_add_evidence(evidence, "result.memory.used_percent",
                               memory_value, "低于80%告警阈值") != 0 ||
          velaops_add_evidence(evidence, "result.disk.used_percent",
                               disk_value, "低于90%告警阈值") != 0)
        {
          goto cleanup;
        }
    }

  if (cJSON_AddNumberToObject(root, "schema_version", 1) == NULL ||
      cJSON_AddStringToObject(root, "status", status) == NULL ||
      cJSON_AddStringToObject(root, "summary", summary) == NULL)
    {
      goto cleanup;
    }
  cJSON_AddItemToObject(root, "evidence", evidence);
  evidence = NULL;
  cJSON_AddItemToObject(root, "root_cause_candidates", causes);
  causes = NULL;
  if (velaops_add_recommendation(root, action, target, risk) != 0 ||
      cJSON_AddNumberToObject(root, "confidence", valid ? 1.0 : 0.0) == NULL)
    {
      goto cleanup;
    }
  result = velaops_encode(root, output, output_capacity);

cleanup:
  cJSON_Delete(evidence);
  cJSON_Delete(causes);
  cJSON_Delete(root);
  return result;
}

int velaops_local_diagnosis_build(const char *resource_json, char *output,
                                  size_t output_capacity)
{
  velaops_resource_observation_t observation;
  int valid = 0;

  if (resource_json != NULL)
    {
      valid = velaops_resource_result_parse(resource_json, &observation) == 0;
    }
  return velaops_local_diagnosis_encode(&observation, valid, output,
                                        output_capacity);
}

int velaops_local_diagnosis_build_observation(
    const velaops_resource_observation_t *observation, char *output,
    size_t output_capacity)
{
  return velaops_local_diagnosis_encode(observation, observation != NULL,
                                        output, output_capacity);
}
