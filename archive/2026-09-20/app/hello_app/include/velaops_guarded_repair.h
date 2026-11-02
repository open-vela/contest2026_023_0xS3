/****************************************************************************
 * VelaOps 固定白名单修复请求与独立复核规则。
 ****************************************************************************/

#ifndef VELAOPS_GUARDED_REPAIR_H
#define VELAOPS_GUARDED_REPAIR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int velaops_guarded_repair_build_request(
    const char *approval_id, int64_t approved_at, int64_t expires_at,
    char *output, size_t output_capacity);

/* 复核只认可重新取得的聚合资源证据，不信任变更 Action 的退出码。 */
int velaops_guarded_repair_verify(const char *resource_json, bool *recovered);

int velaops_guarded_repair_format_result(
    const char *execution_state, bool verified, const char *status,
    unsigned int attempts, char *output, size_t output_capacity);

#endif /* VELAOPS_GUARDED_REPAIR_H */
