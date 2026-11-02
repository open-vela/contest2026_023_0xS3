/****************************************************************************
 * VelaOps Agent 侧屏显管理器。
 *
 * 在 ai_agent 进程内提供常亮资源看板与消息提示框：正常时每 5 秒把白名单
 * 资源刷新到 LCD，收到 Agent 工具下发的消息时切换为闪烁提示框，短按
 * BOOT 关闭提示框并恢复看板。
 ****************************************************************************/

#ifndef VELAOPS_SCREEN_H
#define VELAOPS_SCREEN_H

#include "velaops_agent_tools.h"

/*
 * 幂等启动屏显线程；只在一次成功的 Agent 工具调用后触发，复用固定白名单
 * fetcher。与 `velaops monitor` CLI 互斥，同一时间只允许一个 LCD 使用者。
 */
int velaops_screen_start(velaops_resource_fetcher_t fetcher);

/*
 * 弹出消息提示框。仅保留可打印 ASCII（字库为 5x7 ASCII 点阵），小写自动
 * 转大写，最多 24 字符；提示框持续闪烁直到短按 BOOT 关闭。
 * 线程安全，可从工具执行线程调用。
 */
int velaops_screen_show_message(const char *text);

#endif /* VELAOPS_SCREEN_H */
