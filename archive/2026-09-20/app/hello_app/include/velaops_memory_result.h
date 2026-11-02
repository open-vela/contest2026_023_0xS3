/****************************************************************************
 * VelaOps Proxy 内存结果 JSON 适配器。
 ****************************************************************************/

#ifndef VELAOPS_MEMORY_RESULT_H
#define VELAOPS_MEMORY_RESULT_H

#include "velaops_health.h"

typedef enum
{
  VELAOPS_MEMORY_RESULT_OK = 0,
  VELAOPS_MEMORY_RESULT_INVALID_ARGUMENT,
  VELAOPS_MEMORY_RESULT_INVALID_JSON,
  VELAOPS_MEMORY_RESULT_INVALID_FIELD
} velaops_memory_result_status_t;

/* 只接受 check_memory v1 的四个精确字段，拒绝扩展或重复字段。 */
velaops_memory_result_status_t velaops_memory_result_parse(
    const char *result_json,
    velaops_memory_observation_t *observation);

#endif /* VELAOPS_MEMORY_RESULT_H */
