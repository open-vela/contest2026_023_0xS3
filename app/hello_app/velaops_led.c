/****************************************************************************
 * VelaOps Sentinel - LED 状态指示
 ****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <nuttx/leds/userled.h>

#include "velaops.h"

/* LED 控制上下文 */
struct velaops_led_context
{
  int fd;
  enum velaops_led_state current_state;
  bool blink_phase;
  time_t last_blink_time;
  userled_set_t supported;
};

/* LED 控制实例 */
static struct velaops_led_context g_led = {
  .fd = -1,
  .current_state = LED_STATE_OFF,
  .blink_phase = false,
  .last_blink_time = 0
};

/* 闪烁间隔（秒） */
#define BLINK_INTERVAL 1

/* 初始化 LED 控制 */
int velaops_led_init(void)
{
  g_led.fd = open("/dev/userleds", O_WRONLY);
  if (g_led.fd < 0)
    {
      printf("LED: Failed to open userleds device\n");
      return -1;
    }

  g_led.current_state = LED_STATE_OFF;
  g_led.blink_phase = false;
  g_led.last_blink_time = 0;

  if (ioctl(g_led.fd, ULEDIOC_SUPPORTED,
            (unsigned long)(uintptr_t)&g_led.supported) < 0)
    {
      close(g_led.fd);
      g_led.fd = -1;
      return -1;
    }

  /* 关闭所有 LED */
  velaops_led_set_state(LED_STATE_OFF);

  printf("LED: Initialized\n");
  return 0;
}

/* 设置 LED 状态 */
int velaops_led_set_state(enum velaops_led_state state)
{
  userled_set_t ledset;

  if (g_led.fd < 0)
    {
      return -1;
    }

  g_led.current_state = state;
  ledset = state == LED_STATE_OFF ? 0 : g_led.supported;
  if ((state == LED_STATE_GREEN_BLINK || state == LED_STATE_RED_BLINK) &&
      !g_led.blink_phase)
    ledset = 0;

  return ioctl(g_led.fd, ULEDIOC_SETALL, (unsigned long)ledset);
}

/* 更新 LED 闪烁状态 */
static void update_blink(void)
{
  time_t now = time(NULL);

  if (now - g_led.last_blink_time >= BLINK_INTERVAL)
    {
      g_led.blink_phase = !g_led.blink_phase;
      g_led.last_blink_time = now;

      /* 重新应用当前状态 */
      velaops_led_set_state(g_led.current_state);
    }
}

/* 根据系统状态更新 LED */
void velaops_led_update(void)
{
  int system_state = velaops_state_get_current();
  enum velaops_led_state wanted;

  /* 更新闪烁状态 */
  if (g_led.current_state == LED_STATE_GREEN_BLINK ||
      g_led.current_state == LED_STATE_RED_BLINK)
    {
      update_blink();
    }

  /* The board exposes one user LED, so colors map to off/on/blink. */
  switch (system_state)
    {
      case STATE_IDLE:
        wanted = LED_STATE_OFF;
        break;

      case STATE_CONNECTING:
        wanted = LED_STATE_GREEN_BLINK;
        break;

      case STATE_CONNECTED:
        wanted = LED_STATE_GREEN_ON;
        break;

      case STATE_MONITORING:
        wanted = LED_STATE_GREEN_ON;
        break;

      case STATE_ALERT:
        wanted = LED_STATE_RED_BLINK;
        break;

      case STATE_ERROR:
        wanted = LED_STATE_RED_ON;
        break;

      default:
        wanted = LED_STATE_OFF;
        break;
    }

  if (wanted != g_led.current_state) velaops_led_set_state(wanted);
}

/* 关闭 LED 控制 */
void velaops_led_close(void)
{
  if (g_led.fd >= 0)
    {
      /* 关闭所有 LED */
      velaops_led_set_state(LED_STATE_OFF);
      close(g_led.fd);
      g_led.fd = -1;
    }
  printf("LED: Closed\n");
}
