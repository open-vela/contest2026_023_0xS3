/****************************************************************************
 * VelaOps Sentinel - 按键输入处理
 ****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <time.h>

#include <nuttx/input/buttons.h>

#include "velaops.h"

/* 按键事件类型 */
enum velaops_btn_event
{
  BTN_EVENT_NONE = 0,
  BTN_EVENT_SHORT_PRESS,    /* 短按 */
  BTN_EVENT_LONG_PRESS,     /* 长按 */
  BTN_EVENT_DOUBLE_PRESS    /* 双击 */
};

/* 按键状态 */
struct velaops_btn_state
{
  int fd;
  bool pressed;
  bool raw_pressed;
  uint64_t raw_changed_ms;
  uint64_t press_time_ms;
  int press_count;
  uint64_t last_press_time_ms;
  btn_buttonset_t supported;
};

/* 按键状态实例 */
static struct velaops_btn_state g_btn = {
  .fd = -1,
  .pressed = false,
  .raw_pressed = false,
  .raw_changed_ms = 0,
  .press_time_ms = 0,
  .press_count = 0,
  .last_press_time_ms = 0
};

static pthread_t g_input_thread;
static volatile bool g_input_running;

static void *velaops_input_worker(void *arg);

#define DEBOUNCE_MS 40
#define LONG_PRESS_MS 1500
#define DOUBLE_PRESS_MS 400
#define INPUT_THREAD_STACK_SIZE 8192

static uint64_t monotonic_ms(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* 初始化按键输入 */
int velaops_input_init(void)
{
  pthread_attr_t attr;
  int ret;

  g_btn.fd = open("/dev/buttons", O_RDONLY | O_NONBLOCK);
  if (g_btn.fd < 0)
    {
      printf("Input: Failed to open buttons device\n");
      return -1;
    }

  if (ioctl(g_btn.fd, BTNIOC_SUPPORTED,
            (unsigned long)(uintptr_t)&g_btn.supported) < 0)
    {
      printf("Input: BTNIOC_SUPPORTED failed\n");
      close(g_btn.fd);
      g_btn.fd = -1;
      return -1;
    }

  g_btn.pressed = false;
  g_btn.raw_pressed = false;
  g_btn.raw_changed_ms = monotonic_ms();
  g_btn.press_time_ms = 0;
  g_btn.press_count = 0;
  g_btn.last_press_time_ms = 0;

  /* Keep input detection independent of the busy SSH polling loop.  Give the
   * worker an explicit stack instead of relying on the very small NuttX
   * pthread default; display I/O itself is owned by the dashboard worker. */

  ret = pthread_attr_init(&attr);
  if (ret != 0)
    {
      printf("Input: Failed to initialize thread attributes: %d\n", ret);
      close(g_btn.fd);
      g_btn.fd = -1;
      return -1;
    }

  ret = pthread_attr_setstacksize(&attr, INPUT_THREAD_STACK_SIZE);
  if (ret != 0)
    {
      printf("Input: Failed to set polling stack size: %d\n", ret);
      pthread_attr_destroy(&attr);
      close(g_btn.fd);
      g_btn.fd = -1;
      return -1;
    }

  g_input_running = true;
  ret = pthread_create(&g_input_thread, &attr, velaops_input_worker, NULL);
  pthread_attr_destroy(&attr);
  if (ret != 0)
    {
      printf("Input: Failed to start polling thread: %d\n", ret);
      g_input_running = false;
      close(g_btn.fd);
      g_btn.fd = -1;
      return -1;
    }

  printf("Input: Initialized\n");
  return 0;
}

/* 检查按键状态 */
static bool check_button_pressed(void)
{
  btn_buttonset_t buttons = 0;
  ssize_t nread;

  if (g_btn.fd < 0) return false;
  nread = read(g_btn.fd, &buttons, sizeof(buttons));
  return nread == sizeof(buttons) && (buttons & g_btn.supported) != 0;
}

/* 读取按键事件 */
enum velaops_btn_event velaops_input_read(void)
{
  bool raw_pressed = check_button_pressed();
  uint64_t now = monotonic_ms();
  enum velaops_btn_event event = BTN_EVENT_NONE;

  if (raw_pressed != g_btn.raw_pressed)
    {
      g_btn.raw_pressed = raw_pressed;
      g_btn.raw_changed_ms = now;
    }

  if (now - g_btn.raw_changed_ms < DEBOUNCE_MS ||
      g_btn.raw_pressed == g_btn.pressed)
    {
      return BTN_EVENT_NONE;
    }

  /* 检测按键按下 */
  if (g_btn.raw_pressed)
    {
      g_btn.pressed = true;
      g_btn.press_time_ms = now;
      g_btn.press_count++;
    }
  /* 检测按键释放 */
  else
    {
      g_btn.pressed = false;
      uint64_t press_duration = now - g_btn.press_time_ms;

      /* 判断长按 */
      if (press_duration >= LONG_PRESS_MS)
        {
          event = BTN_EVENT_LONG_PRESS;
          g_btn.press_count = 0;
        }
      /* 判断双击 */
      else if (g_btn.last_press_time_ms != 0 &&
               now - g_btn.last_press_time_ms <= DOUBLE_PRESS_MS &&
               g_btn.press_count >= 2)
        {
          event = BTN_EVENT_DOUBLE_PRESS;
          g_btn.press_count = 0;
        }
      /* 短按 */
      else
        {
          event = BTN_EVENT_SHORT_PRESS;
        }

      g_btn.last_press_time_ms = now;
    }

  return event;
}

static void *velaops_input_worker(void *arg)
{
  (void)arg;

  while (g_input_running)
    {
      velaops_input_process();
      usleep(20000);
    }

  return NULL;
}

/* 处理按键事件 */
void velaops_input_process(void)
{
  enum velaops_btn_event btn_event = velaops_input_read();

  switch (btn_event)
    {
      case BTN_EVENT_SHORT_PRESS:
        printf("Input: Short press detected\n");
        /* 切换显示页面 */
        velaops_dashboard_next_page();
        break;

      case BTN_EVENT_LONG_PRESS:
        printf("Input: Long press detected\n");
        /* 确认告警 */
        if (velaops_state_has_alerts())
          {
            velaops_state_handle_event(EVENT_ACKNOWLEDGE);
          }
        break;

      case BTN_EVENT_DOUBLE_PRESS:
        printf("Input: Double press detected\n");
        /* Rapid page presses must remain a display action.  Disconnecting
         * here made normal navigation unexpectedly tear down a session. */
        velaops_dashboard_next_page();
        break;

      case BTN_EVENT_NONE:
      default:
        /* 无事件 */
        break;
    }
}

/* 关闭按键输入 */
void velaops_input_close(void)
{
  if (g_input_running)
    {
      g_input_running = false;
      pthread_join(g_input_thread, NULL);
    }

  if (g_btn.fd >= 0)
    {
      close(g_btn.fd);
      g_btn.fd = -1;
    }
  printf("Input: Closed\n");
}
