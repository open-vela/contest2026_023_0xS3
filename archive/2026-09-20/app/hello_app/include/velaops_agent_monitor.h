/****************************************************************************
 * VelaOps AI Agent 主动巡检适配器。
 *
 * 领域核心只接收已校验资源 JSON 和事件出口；运行时线程与消息总线封装在实现层，
 * 其他业务模块不依赖 ai_agent 内部消息结构。
 ****************************************************************************/

#ifndef VELAOPS_AGENT_MONITOR_H
#define VELAOPS_AGENT_MONITOR_H

#include <stddef.h>
#include <stdint.h>

#include "velaops_agent_tools.h"
#include "velaops_resource_incident.h"

typedef int (*velaops_agent_event_sink_t)(const char *prompt);

typedef enum
{
  VELAOPS_AGENT_MONITOR_OK = 0,
  VELAOPS_AGENT_MONITOR_INVALID_ARGUMENT,
  VELAOPS_AGENT_MONITOR_INVALID_EVIDENCE,
  VELAOPS_AGENT_MONITOR_OUT_OF_ORDER,
  VELAOPS_AGENT_MONITOR_DISPATCH_ERROR
} velaops_agent_monitor_status_t;

typedef struct
{
  velaops_resource_incident_t incident;
  velaops_agent_event_sink_t sink;
  velaops_incident_event_t pending_event;
  uint32_t pending_generation;
} velaops_agent_monitor_t;

int velaops_agent_monitor_init(velaops_agent_monitor_t *monitor,
                               uint16_t failure_threshold,
                               uint16_t recovery_threshold,
                               velaops_agent_event_sink_t sink);

/* 资源证据不会进入提示词，避免 Proxy 字符串成为提示词注入载体。 */
velaops_agent_monitor_status_t velaops_agent_monitor_sample(
    velaops_agent_monitor_t *monitor, const char *resource_json,
    int64_t observed_at);

/*
 * 只允许从一次成功的 Agent 工具调用中启动。该时点消息总线和 Agent loop 已就绪；
 * 函数幂等，后台每 5 秒复用固定白名单 fetcher 采样。
 */
int velaops_agent_monitor_start(velaops_resource_fetcher_t fetcher,
                                const char *initial_resource_json);

#endif /* VELAOPS_AGENT_MONITOR_H */
