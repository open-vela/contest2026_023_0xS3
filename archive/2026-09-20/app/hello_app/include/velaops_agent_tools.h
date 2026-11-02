/****************************************************************************
 * VelaOps AI Agent 工具适配器。
 *
 * 通过 ai_agent 的外部 Tool Provider 接口暴露队伍自有能力，
 * 避免修改 packages/ai_agent 上游目录。
 ****************************************************************************/

#ifndef VELAOPS_AGENT_TOOLS_H
#define VELAOPS_AGENT_TOOLS_H

#include <stddef.h>

typedef int (*velaops_resource_fetcher_t)(char *output,
                                          size_t output_capacity);
typedef int (*velaops_agent_monitor_starter_t)(
    velaops_resource_fetcher_t fetcher, const char *initial_resource_json);
typedef int (*velaops_service_repairer_t)(char *output,
                                          size_t output_capacity);

/*
 * 注册固定只读取证与实体批准修复工具。
 * fetcher 只能返回已校验的 Proxy result JSON；monitor_starter 只在一次成功
 * 取证调用后触发，确保 Agent 生命周期已经就绪；repairer 不接受 LLM 参数；
 * 函数可重复调用。
 */

int velaops_agent_tools_register(
    velaops_resource_fetcher_t fetcher,
    velaops_agent_monitor_starter_t monitor_starter,
    velaops_service_repairer_t repairer);

#endif /* VELAOPS_AGENT_TOOLS_H */
