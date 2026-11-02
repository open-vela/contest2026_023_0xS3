/****************************************************************************
 * VelaOps Agent 侧屏显管理器实现。
 ****************************************************************************/

#include "velaops_screen.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/input/buttons.h>

#include "velaops_display.h"
#include "velaops_health.h"
#include "velaops_resource_result.h"

/* 该线程内会跑完整 TLS/HTTP 取证（mbedtls），32KB 栈有溢出风险，给足 64KB。 */
#define VELAOPS_SCREEN_STACK_SIZE 65536
#define VELAOPS_SCREEN_TICK_MS 250
#define VELAOPS_SCREEN_REFRESH_TICKS 20
#define VELAOPS_SCREEN_BLINK_TICKS 2
#define VELAOPS_SCREEN_RESOURCE_CAPACITY 2048
#define VELAOPS_SCREEN_MESSAGE_CAPACITY 25
#define VELAOPS_SCREEN_MEMORY_UNHEALTHY_PERCENT 80.0
#define VELAOPS_SCREEN_BUTTON_DEVICE "/dev/buttons"
#define VELAOPS_SCREEN_MESSAGE_TITLE "AGENT MESSAGE"

struct velaops_screen_context_s
{
  velaops_resource_fetcher_t fetcher;
};

static pthread_mutex_t g_start_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_started;

/* UI 态由屏显线程绘制、工具线程写入，统一用一把锁保护。 */
static pthread_mutex_t g_ui_lock = PTHREAD_MUTEX_INITIALIZER;
static velaops_display_state_t g_state;
static unsigned int g_page;
static char g_message[VELAOPS_SCREEN_MESSAGE_CAPACITY];
static bool g_popup_active;

static void velaops_screen_sanitize_message(const char *text, char *output,
                                            size_t output_capacity)
{
  size_t written = 0;
  size_t index;

  for (index = 0; text[index] != '\0' &&
                  written + 1 < output_capacity; index++)
    {
      char character = text[index];

      /* 字库只有 5x7 ASCII 点阵：非可打印 ASCII 一律替换为 '-'，
       * 避免提示框出现乱码块。 */
      if (character < 0x20 || character > 0x7e)
        {
          character = '-';
        }
      else if (character >= 'a' && character <= 'z')
        {
          character = (char)(character - 'a' + 'A');
        }
      output[written++] = character;
    }
  output[written] = '\0';
}

static void velaops_screen_apply_resources(const char *resource_json)
{
  velaops_resource_observation_t resources;
  velaops_health_snapshot_t snapshot;

  pthread_mutex_lock(&g_ui_lock);
  if (velaops_resource_result_parse(resource_json, &resources) == 0 &&
      velaops_evaluate_memory(&resources.memory,
                              VELAOPS_SCREEN_MEMORY_UNHEALTHY_PERCENT,
                              (int64_t)time(NULL), &snapshot) ==
          VELAOPS_HEALTH_OK)
    {
      g_state.resources = resources;
      g_state.memory = resources.memory;
      g_state.health = snapshot.result;
      g_state.observed_at = snapshot.observed_at;
      g_state.has_resources = 1;
      g_state.online = 1;
    }
  else
    {
      /* 解析失败只下线状态标记，保留上一次有效数据继续展示。 */
      g_state.online = 0;
    }
  pthread_mutex_unlock(&g_ui_lock);
}

static void velaops_screen_render(velaops_display_t *display,
                                  unsigned int blink_phase)
{
  velaops_display_state_t state;
  unsigned int page;
  char message[VELAOPS_SCREEN_MESSAGE_CAPACITY];
  bool popup;

  pthread_mutex_lock(&g_ui_lock);
  popup = g_popup_active;
  state = g_state;
  page = g_page;
  memcpy(message, g_message, sizeof(message));
  pthread_mutex_unlock(&g_ui_lock);

  if (popup)
    {
      (void)velaops_display_show_message(
          display, VELAOPS_SCREEN_MESSAGE_TITLE, message, (int)blink_phase);
    }
  else
    {
      (void)velaops_display_show(display, &state, page);
    }
}

static void velaops_screen_handle_button(bool pressed, bool *was_pressed,
                                         bool *force_render)
{
  /* 只在上升沿响应：长按批准由修复流程自己处理，这里只做短按交互。 */
  if (pressed == *was_pressed)
    {
      return;
    }
  *was_pressed = pressed;
  if (!pressed)
    {
      return;
    }

  pthread_mutex_lock(&g_ui_lock);
  if (g_popup_active)
    {
      g_popup_active = false;
    }
  else
    {
      g_page = (g_page + 1) % VELAOPS_DISPLAY_PAGE_COUNT;
    }
  pthread_mutex_unlock(&g_ui_lock);
  *force_render = true;
}

static void *velaops_screen_worker(void *argument)
{
  struct velaops_screen_context_s *context = argument;
  char resources[VELAOPS_SCREEN_RESOURCE_CAPACITY];
  velaops_display_t *display = NULL;
  btn_buttonset_t supported = 0;
  btn_buttonset_t sample = 0;
  bool button_pressed = false;
  bool force_render = true;
  unsigned int tick = 0;
  int button_fd = -1;

  for (;;)
    {
      bool popup;

      usleep(VELAOPS_SCREEN_TICK_MS * 1000);
      tick++;

      if (display == NULL)
        {
          display = velaops_display_open();
          if (display == NULL)
            {
              /* LCD 暂不可用时不阻塞采样，下个节拍继续尝试。 */
              force_render = true;
              continue;
            }
        }

      if (button_fd < 0)
        {
          button_fd = open(VELAOPS_SCREEN_BUTTON_DEVICE,
                           O_RDONLY | O_NONBLOCK);
          if (button_fd >= 0 &&
              ioctl(button_fd, BTNIOC_SUPPORTED,
                    (unsigned long)(uintptr_t)&supported) < 0)
            {
              supported = 0;
            }
        }
      if (button_fd >= 0 && supported != 0)
        {
          ssize_t count;

          /* 按键驱动的 read 是“当前状态快照”而非队列，每次都会立即返回，
           * 绝不能循环排空，否则线程会死循环占满 CPU。每节拍只读一次。 */
          count = read(button_fd, &sample, sizeof(sample));
          if (count != (ssize_t)sizeof(sample))
            {
              close(button_fd);
              button_fd = -1;
              sample = 0;
            }
          else
            {
              velaops_screen_handle_button((sample & supported) != 0,
                                           &button_pressed, &force_render);
            }
        }

      if (tick % VELAOPS_SCREEN_REFRESH_TICKS == 0)
        {
          if (context->fetcher(resources, sizeof(resources)) == 0)
            {
              resources[sizeof(resources) - 1] = '\0';
              velaops_screen_apply_resources(resources);
            }
          else
            {
              pthread_mutex_lock(&g_ui_lock);
              g_state.online = 0;
              pthread_mutex_unlock(&g_ui_lock);
            }
          force_render = true;
        }

      pthread_mutex_lock(&g_ui_lock);
      popup = g_popup_active;
      pthread_mutex_unlock(&g_ui_lock);

      /* 提示框按固定节拍重绘实现闪烁；看板只在刷新/翻页/关闭提示框后重绘，
       * 避免整帧高频写入 LCD。 */
      if (popup ? (tick % VELAOPS_SCREEN_BLINK_TICKS == 0) : force_render)
        {
          velaops_screen_render(display,
                                (tick / VELAOPS_SCREEN_BLINK_TICKS) & 1U);
          force_render = false;
        }
    }

  return NULL;
}

int velaops_screen_start(velaops_resource_fetcher_t fetcher)
{
  struct velaops_screen_context_s *context;
  pthread_attr_t attributes;
  pthread_t thread;
  int status = ERROR;

  if (fetcher == NULL)
    {
      return ERROR;
    }

  pthread_mutex_lock(&g_start_lock);
  if (g_started)
    {
      pthread_mutex_unlock(&g_start_lock);
      return OK;
    }
  context = calloc(1, sizeof(*context));
  if (context == NULL)
    {
      pthread_mutex_unlock(&g_start_lock);
      return ERROR;
    }
  context->fetcher = fetcher;

  pthread_attr_init(&attributes);
  pthread_attr_setstacksize(&attributes, VELAOPS_SCREEN_STACK_SIZE);
  if (pthread_create(&thread, &attributes, velaops_screen_worker,
                     context) == 0)
    {
      pthread_detach(thread);
      g_started = true;
      status = OK;
      printf("velaops: screen manager started\n");
    }
  else
    {
      free(context);
    }
  pthread_attr_destroy(&attributes);
  pthread_mutex_unlock(&g_start_lock);
  return status;
}

int velaops_screen_show_message(const char *text)
{
  char sanitized[VELAOPS_SCREEN_MESSAGE_CAPACITY];

  if (text == NULL)
    {
      return ERROR;
    }
  velaops_screen_sanitize_message(text, sanitized, sizeof(sanitized));
  if (sanitized[0] == '\0')
    {
      return ERROR;
    }

  pthread_mutex_lock(&g_ui_lock);
  memcpy(g_message, sanitized, sizeof(g_message));
  g_popup_active = true;
  pthread_mutex_unlock(&g_ui_lock);
  return OK;
}
