/****************************************************************************
 * VelaOps Proxy 内存结果 JSON 适配器实现。
 ****************************************************************************/

#include "velaops_memory_result.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cJSON.h"

#define VELAOPS_JSON_SAFE_INTEGER_MAX 9007199254740991.0

static bool velaops_has_exact_fields(const cJSON *root)
{
  const char *const names[] = {
    "total_bytes", "available_bytes", "used_bytes", "used_percent"
  };
  bool seen[sizeof(names) / sizeof(names[0])] = {false};
  const cJSON *item;
  size_t count = 0;
  size_t index;

  cJSON_ArrayForEach(item, root)
    {
      bool known = false;

      for (index = 0; index < sizeof(names) / sizeof(names[0]); index++)
        {
          if (item->string != NULL && strcmp(item->string, names[index]) == 0)
            {
              if (seen[index])
                {
                  return false;
                }
              seen[index] = true;
              known = true;
              break;
            }
        }
      if (!known)
        {
          return false;
        }
      count++;
    }
  return count == sizeof(names) / sizeof(names[0]);
}

static bool velaops_json_uint64(const cJSON *root, const char *name,
                                uint64_t *value)
{
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
  double number;

  if (!cJSON_IsNumber(item))
    {
      return false;
    }
  number = item->valuedouble;
  if (!isfinite(number) || number < 0.0 ||
      number > VELAOPS_JSON_SAFE_INTEGER_MAX || floor(number) != number)
    {
      return false;
    }
  *value = (uint64_t)number;
  return true;
}

velaops_memory_result_status_t velaops_memory_result_parse(
    const char *result_json,
    velaops_memory_observation_t *observation)
{
  velaops_memory_observation_t parsed;
  const cJSON *used_percent;
  cJSON *root;
  cJSON *node;

  if (result_json == NULL || observation == NULL)
    {
      return VELAOPS_MEMORY_RESULT_INVALID_ARGUMENT;
    }
  root = cJSON_ParseWithOpts(result_json, NULL, true);
  if (!cJSON_IsObject(root))
    {
      cJSON_Delete(root);
      return VELAOPS_MEMORY_RESULT_INVALID_JSON;
    }

  /* 兼容两种 result 形态：check_memory 的扁平四字段，以及
   * check_resources 把内存包在 "memory" 子对象里。 */
  node = root;
  if (cJSON_GetObjectItemCaseSensitive(root, "total_bytes") == NULL)
    {
      cJSON *memory = cJSON_GetObjectItemCaseSensitive(root, "memory");
      if (cJSON_IsObject(memory))
        {
          node = memory;
        }
    }

  used_percent = cJSON_GetObjectItemCaseSensitive(node, "used_percent");
  if (!velaops_has_exact_fields(node) ||
      !velaops_json_uint64(node, "total_bytes", &parsed.total_bytes) ||
      !velaops_json_uint64(node, "available_bytes", &parsed.available_bytes) ||
      !velaops_json_uint64(node, "used_bytes", &parsed.used_bytes) ||
      !cJSON_IsNumber(used_percent) || !isfinite(used_percent->valuedouble))
    {
      cJSON_Delete(root);
      return VELAOPS_MEMORY_RESULT_INVALID_FIELD;
    }
  parsed.used_percent = used_percent->valuedouble;
  cJSON_Delete(root);
  *observation = parsed;
  return VELAOPS_MEMORY_RESULT_OK;
}
