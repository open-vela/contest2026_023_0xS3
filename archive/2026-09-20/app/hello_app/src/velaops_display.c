/****************************************************************************
 * VelaOps ESP32-S3-EYE ST7789 LCD 设备适配。
 *
 * 此文件只负责 LCD 生命周期和整帧提交，界面布局由 dashboard 层维护。
 ****************************************************************************/

#include "velaops_display.h"
#include "velaops_dashboard.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <nuttx/lcd/lcd_dev.h>

#define VELAOPS_DISPLAY_DEVICE "/dev/lcd0"

struct velaops_display_s
{
  int fd;
  struct fb_videoinfo_s video;
  uint16_t *pixels;
};

static int velaops_display_flush(velaops_display_t *display)
{
  struct lcddev_area_s area;

  memset(&area, 0, sizeof(area));
  area.row_start = 0;
  area.row_end = VELAOPS_DISPLAY_HEIGHT - 1;
  area.col_start = 0;
  area.col_end = VELAOPS_DISPLAY_WIDTH - 1;
  area.stride = VELAOPS_DISPLAY_WIDTH * sizeof(*display->pixels);
  area.data = (uint8_t *)display->pixels;
  if (ioctl(display->fd, LCDDEVIO_PUTAREA,
            (unsigned long)(uintptr_t)&area) < 0)
    {
      return -errno;
    }

  return 0;
}

velaops_display_t *velaops_display_open(void)
{
  velaops_display_t *display;

  display = calloc(1, sizeof(*display));
  if (display == NULL)
    {
      return NULL;
    }

  display->fd = open(VELAOPS_DISPLAY_DEVICE, O_RDWR);
  if (display->fd < 0 ||
      ioctl(display->fd, LCDDEVIO_GETVIDEOINFO,
            (unsigned long)(uintptr_t)&display->video) < 0 ||
      display->video.xres != VELAOPS_DISPLAY_WIDTH ||
      display->video.yres != VELAOPS_DISPLAY_HEIGHT)
    {
      if (display->fd >= 0)
        {
          close(display->fd);
        }

      free(display);
      return NULL;
    }

  display->pixels = calloc(VELAOPS_DISPLAY_WIDTH * VELAOPS_DISPLAY_HEIGHT,
                           sizeof(*display->pixels));
  if (display->pixels == NULL)
    {
      close(display->fd);
      free(display);
      return NULL;
    }

  return display;
}

void velaops_display_close(velaops_display_t *display)
{
  if (display != NULL)
    {
      close(display->fd);
      free(display->pixels);
      free(display);
    }
}

int velaops_display_show(velaops_display_t *display,
                         const velaops_display_state_t *state,
                         unsigned int page)
{
  int result;

  if (display == NULL)
    {
      return -EINVAL;
    }

  result = velaops_dashboard_render(
      display->pixels, VELAOPS_DISPLAY_WIDTH * VELAOPS_DISPLAY_HEIGHT,
      state, page);
  if (result < 0)
    {
      return result;
    }

  return velaops_display_flush(display);
}

int velaops_display_show_test(velaops_display_t *display)
{
  int result;

  if (display == NULL)
    {
      return -EINVAL;
    }

  result = velaops_dashboard_render_test(
      display->pixels, VELAOPS_DISPLAY_WIDTH * VELAOPS_DISPLAY_HEIGHT);
  if (result < 0)
    {
      return result;
    }

  return velaops_display_flush(display);
}

int velaops_display_show_message(velaops_display_t *display,
                                 const char *title, const char *text,
                                 int blink_phase)
{
  int result;

  if (display == NULL)
    {
      return -EINVAL;
    }

  result = velaops_dashboard_render_message(
      display->pixels, VELAOPS_DISPLAY_WIDTH * VELAOPS_DISPLAY_HEIGHT,
      title, text, blink_phase);
  if (result < 0)
    {
      return result;
    }

  return velaops_display_flush(display);
}

int velaops_display_next_page(velaops_display_t *display,
                              const velaops_display_state_t *state,
                              unsigned int *page)
{
  if (page == NULL)
    {
      return -EINVAL;
    }

  *page = (*page + 1) % VELAOPS_DISPLAY_PAGE_COUNT;
  return velaops_display_show(display, state, *page);
}
