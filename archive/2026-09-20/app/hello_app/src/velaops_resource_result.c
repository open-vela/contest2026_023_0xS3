/****************************************************************************
 * VelaOps Proxy 聚合资源结果适配器实现。
 ****************************************************************************/

#include "velaops_resource_result.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"

static const cJSON *velaops_object(const cJSON *root, const char *name)
{
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
  return cJSON_IsObject(item) ? item : NULL;
}

static bool velaops_number(const cJSON *root, const char *name, double *value)
{
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
  if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble))
    {
      return false;
    }
  *value = item->valuedouble;
  return true;
}

int velaops_resource_result_parse(const char *result_json,
                                  velaops_resource_observation_t *result)
{
  cJSON *root;
  const cJSON *memory;
  const cJSON *disk;
  const cJSON *service;
  const cJSON *port;
  const cJSON *item;
  velaops_resource_observation_t parsed = {0};
  double value;

  if (result_json == NULL || result == NULL)
    {
      return -1;
    }
  root = cJSON_ParseWithOpts(result_json, NULL, true);
  memory = cJSON_IsObject(root) ? velaops_object(root, "memory") : NULL;
  disk = cJSON_IsObject(root) ? velaops_object(root, "disk") : NULL;
  service = cJSON_IsObject(root) ? velaops_object(root, "service") : NULL;
  port = cJSON_IsObject(root) ? velaops_object(root, "port") : NULL;
  if (memory == NULL || disk == NULL || service == NULL || port == NULL ||
      !velaops_number(memory, "total_bytes", &value) || value < 0 || floor(value) != value)
    {
      cJSON_Delete(root);
      return -1;
    }
  parsed.memory.total_bytes = (uint64_t)value;
  if (!velaops_number(memory, "available_bytes", &value) || value < 0 || floor(value) != value)
    {
      cJSON_Delete(root);
      return -1;
    }
  parsed.memory.available_bytes = (uint64_t)value;
  if (!velaops_number(memory, "used_bytes", &value) || value < 0 || floor(value) != value)
    {
      cJSON_Delete(root);
      return -1;
    }
  parsed.memory.used_bytes = (uint64_t)value;
  if (!velaops_number(memory, "used_percent", &parsed.memory.used_percent) ||
      !velaops_number(disk, "used_percent", &parsed.disk_percent) ||
      parsed.memory.available_bytes > parsed.memory.total_bytes ||
      parsed.memory.used_bytes > parsed.memory.total_bytes ||
      parsed.memory.used_percent < 0.0 || parsed.memory.used_percent > 100.0 ||
      parsed.disk_percent < 0.0 || parsed.disk_percent > 100.0)
    {
      cJSON_Delete(root);
      return -1;
    }
  item = cJSON_GetObjectItemCaseSensitive(service, "active_state");
  if (!cJSON_IsString(item) || item->valuestring == NULL)
    {
      cJSON_Delete(root);
      return -1;
    }
  parsed.service_active = strcmp(item->valuestring, "active") == 0;
  item = cJSON_GetObjectItemCaseSensitive(port, "reachable");
  if (!cJSON_IsBool(item) || !velaops_number(port, "latency_ms", &value))
    {
      cJSON_Delete(root);
      return -1;
    }
  parsed.port_reachable = cJSON_IsTrue(item);
  if (value < 0 || value > 2147483647.0 || floor(value) != value)
    {
      cJSON_Delete(root);
      return -1;
    }
  parsed.port_latency_ms = (int)value;

  /* CPU 为可选扩展：老代理不返回时保持 valid=0（parsed 已整体清零），看板
   * 据此显示 "--"，不影响既有校验。字段非法时同样只标记无效。 */
  {
    const cJSON *cpu = velaops_object(root, "cpu");
    velaops_cpu_observation_t parsed_cpu = {0};
    double cores;

    if (cpu != NULL &&
        velaops_number(cpu, "used_percent", &parsed_cpu.used_percent) &&
        velaops_number(cpu, "load1", &parsed_cpu.load1) &&
        velaops_number(cpu, "load5", &parsed_cpu.load5) &&
        velaops_number(cpu, "load15", &parsed_cpu.load15) &&
        velaops_number(cpu, "cores", &cores) && cores >= 1 &&
        floor(cores) == cores && cores <= 1024 &&
        parsed_cpu.used_percent >= 0.0 && parsed_cpu.used_percent <= 100.0 &&
        parsed_cpu.load1 >= 0.0 && parsed_cpu.load5 >= 0.0 &&
        parsed_cpu.load15 >= 0.0)
      {
        parsed_cpu.cores = (int)cores;
        parsed_cpu.valid = true;
        parsed.cpu = parsed_cpu;
      }
  }

  /* 顶层进程（可选）：让 LLM 指出"是谁在吃 CPU"。缺失/非法不影响解析。 */
  {
    const cJSON *process = velaops_object(root, "top_process");
    const cJSON *name = process != NULL ?
                        cJSON_GetObjectItemCaseSensitive(process, "name") :
                        NULL;
    double percent;

    if (cJSON_IsString(name) && name->valuestring != NULL &&
        velaops_number(process, "cpu_percent", &percent) &&
        percent >= 0.0 && percent <= 100.0 * 1024.0)
      {
        snprintf(parsed.process.name, sizeof(parsed.process.name), "%s",
                 name->valuestring);
        parsed.process.cpu_percent = percent;
        parsed.process.valid = true;
      }
  }

  cJSON_Delete(root);
  *result = parsed;
  return 0;
}
