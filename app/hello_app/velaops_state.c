/****************************************************************************
 * VelaOps Sentinel - 状态机管理
 ****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#include "velaops.h"

/* 状态转换结构 */
struct velaops_state_transition
{
  enum velaops_state current_state;
  enum velaops_event event;
  enum velaops_state next_state;
};

/* 状态机上下文 */
struct velaops_state_machine
{
  enum velaops_state current_state;
  enum velaops_state previous_state;
  time_t state_entry_time;
  int alert_count;
  bool running;
};

/* 状态转换表 */
static const struct velaops_state_transition transitions[] = {
  {STATE_IDLE, EVENT_CONNECT, STATE_CONNECTING},
  {STATE_CONNECTING, EVENT_CONNECTED, STATE_CONNECTED},
  {STATE_CONNECTING, EVENT_ERROR, STATE_ERROR},
  {STATE_CONNECTING, EVENT_TIMEOUT, STATE_ERROR},
  {STATE_CONNECTED, EVENT_CONNECT, STATE_MONITORING},
  {STATE_CONNECTED, EVENT_DISCONNECT, STATE_IDLE},
  {STATE_CONNECTED, EVENT_ERROR, STATE_ERROR},
  {STATE_MONITORING, EVENT_ALERT, STATE_ALERT},
  {STATE_MONITORING, EVENT_DISCONNECT, STATE_IDLE},
  {STATE_MONITORING, EVENT_ERROR, STATE_ERROR},
  {STATE_ALERT, EVENT_ACKNOWLEDGE, STATE_MONITORING},
  {STATE_ALERT, EVENT_DISCONNECT, STATE_IDLE},
  {STATE_ALERT, EVENT_ERROR, STATE_ERROR},
  {STATE_ERROR, EVENT_CONNECT, STATE_IDLE},
  {STATE_ERROR, EVENT_DISCONNECT, STATE_IDLE}
};

#define NUM_TRANSITIONS (sizeof(transitions) / sizeof(transitions[0]))

/* 状态机实例 */
static struct velaops_state_machine g_state_machine = {
  .current_state = STATE_IDLE,
  .previous_state = STATE_IDLE,
  .state_entry_time = 0,
  .alert_count = 0,
  .running = false
};

/* 获取状态名称 */
static const char *state_name(enum velaops_state state)
{
  switch (state)
    {
      case STATE_IDLE:
        return "IDLE";
      case STATE_CONNECTING:
        return "CONNECTING";
      case STATE_CONNECTED:
        return "CONNECTED";
      case STATE_MONITORING:
        return "MONITORING";
      case STATE_ALERT:
        return "ALERT";
      case STATE_ERROR:
        return "ERROR";
      default:
        return "UNKNOWN";
    }
}

/* 获取事件名称 */
static const char *event_name(enum velaops_event event)
{
  switch (event)
    {
      case EVENT_CONNECT:
        return "CONNECT";
      case EVENT_CONNECTED:
        return "CONNECTED";
      case EVENT_DISCONNECT:
        return "DISCONNECT";
      case EVENT_ALERT:
        return "ALERT";
      case EVENT_ACKNOWLEDGE:
        return "ACKNOWLEDGE";
      case EVENT_ERROR:
        return "ERROR";
      case EVENT_TIMEOUT:
        return "TIMEOUT";
      default:
        return "UNKNOWN";
    }
}

/* 初始化状态机 */
int velaops_state_init(void)
{
  g_state_machine.current_state = STATE_IDLE;
  g_state_machine.previous_state = STATE_IDLE;
  g_state_machine.state_entry_time = time(NULL);
  g_state_machine.alert_count = 0;
  g_state_machine.running = true;

  printf("State machine: Initialized, state=%s\n",
         state_name(g_state_machine.current_state));
  return 0;
}

/* 处理事件 */
int velaops_state_handle_event(enum velaops_event event)
{
  int i;
  enum velaops_state old_state = g_state_machine.current_state;

  /* 查找状态转换 */
  for (i = 0; i < NUM_TRANSITIONS; i++)
    {
      if (transitions[i].current_state == old_state &&
          transitions[i].event == event)
        {
          /* 执行状态转换 */
          g_state_machine.previous_state = old_state;
          g_state_machine.current_state = transitions[i].next_state;
          g_state_machine.state_entry_time = time(NULL);

          printf("State machine: %s -> %s (event: %s)\n",
                 state_name(old_state),
                 state_name(g_state_machine.current_state),
                 event_name(event));

          /* 处理特殊状态 */
          if (g_state_machine.current_state == STATE_ALERT)
            {
              g_state_machine.alert_count++;
            }

          return 0;
        }
    }

  printf("State machine: No transition for event %s in state %s\n",
         event_name(event), state_name(old_state));
  return -1;
}

/* 获取当前状态 */
enum velaops_state velaops_state_get_current(void)
{
  return g_state_machine.current_state;
}

/* 获取前一个状态 */
enum velaops_state velaops_state_get_previous(void)
{
  return g_state_machine.previous_state;
}

/* 获取状态持续时间 */
time_t velaops_state_get_duration(void)
{
  return time(NULL) - g_state_machine.state_entry_time;
}

/* 获取告警数量 */
int velaops_state_get_alert_count(void)
{
  return g_state_machine.alert_count;
}

/* 检查是否在监控状态 */
bool velaops_state_is_monitoring(void)
{
  return g_state_machine.current_state == STATE_MONITORING;
}

/* 检查是否有告警 */
bool velaops_state_has_alerts(void)
{
  return g_state_machine.current_state == STATE_ALERT;
}

/* 检查是否在错误状态 */
bool velaops_state_is_error(void)
{
  return g_state_machine.current_state == STATE_ERROR;
}

/* 重置状态机 */
void velaops_state_reset(void)
{
  g_state_machine.current_state = STATE_IDLE;
  g_state_machine.previous_state = STATE_IDLE;
  g_state_machine.state_entry_time = time(NULL);
  g_state_machine.alert_count = 0;

  printf("State machine: Reset to IDLE\n");
}

/* 停止状态机 */
void velaops_state_stop(void)
{
  g_state_machine.running = false;
  printf("State machine: Stopped\n");
}

/* 检查状态机是否运行 */
bool velaops_state_is_running(void)
{
  return g_state_machine.running;
}
