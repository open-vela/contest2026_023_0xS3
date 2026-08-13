#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>

#include <nuttx/video/fb.h>

#include "velaops.h"

static int g_fd = -1;
static uint16_t *g_fb;
static struct fb_videoinfo_s g_video;
static struct fb_planeinfo_s g_plane;
static unsigned int g_page;
static pthread_mutex_t g_draw_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_draw_cond = PTHREAD_COND_INITIALIZER;
static pthread_t g_draw_thread;
static bool g_draw_running;
static bool g_draw_pending;
static struct velaops_profile g_last_profile;
static struct velaops_metrics g_last_metrics;
static char g_last_status[64];
static bool g_have_frame;

#define DASHBOARD_THREAD_STACK_SIZE 16384

static void *dashboard_worker(void *arg);

static uint16_t rgb(unsigned int r, unsigned int g, unsigned int b)
{
  return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
}

static void rect(int x, int y, int w, int h, uint16_t color)
{
  int xx;
  int yy;
  if (g_fb == NULL) return;
  for (yy = y; yy < y + h && yy < g_video.yres; yy++)
    for (xx = x; xx < x + w && xx < g_video.xres; xx++)
      if (xx >= 0 && yy >= 0)
        *(uint16_t *)((uint8_t *)g_fb + yy * g_plane.stride + xx * 2) = color;
}

/* Compact 5x7 uppercase/digit font; unsupported characters become spaces. */
static const char glyphs[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.%:-_/";
static const uint8_t font[][5] = {
 {0,0,0,0,0},{0x7e,9,9,9,0x7e},{0x7f,0x49,0x49,0x49,0x36},
 {0x3e,0x41,0x41,0x41,0x22},{0x7f,0x41,0x41,0x22,0x1c},
 {0x7f,0x49,0x49,0x49,0x41},{0x7f,9,9,9,1},{0x3e,0x41,0x49,0x49,0x7a},
 {0x7f,8,8,8,0x7f},{0x41,0x41,0x7f,0x41,0x41},{0x20,0x40,0x41,0x3f,1},
 {0x7f,8,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
 {0x7f,2,0x0c,2,0x7f},{0x7f,4,8,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},
 {0x7f,9,9,9,6},{0x3e,0x41,0x51,0x21,0x5e},{0x7f,9,0x19,0x29,0x46},
 {0x46,0x49,0x49,0x49,0x31},{1,1,0x7f,1,1},{0x3f,0x40,0x40,0x40,0x3f},
 {0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},
 {0x63,0x14,8,0x14,0x63},{7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43},
 {0x3e,0x51,0x49,0x45,0x3e},{0x42,0x41,0x7f,0x40,0x40},{0x62,0x51,0x49,0x49,0x46},
 {0x22,0x41,0x49,0x49,0x36},{0x18,0x14,0x12,0x7f,0x10},{0x2f,0x49,0x49,0x49,0x31},
 {0x3e,0x49,0x49,0x49,0x32},{1,0x71,9,5,3},{0x36,0x49,0x49,0x49,0x36},
 {0x26,0x49,0x49,0x49,0x3e},{0,0x60,0x60,0,0},{0x62,0x64,8,0x13,0x23},
 {0,0,0x36,0x36,0},{8,8,8,8,8},{0,0x36,0x36,0,0},{8,0x10,0x20,0x40,0}
};

static void text(int x, int y, const char *s, uint16_t color, int scale)
{
  const char *p;
  int col;
  int row;
  while (*s)
    {
      char c = *s >= 'a' && *s <= 'z' ? *s - 32 : *s;
      p = strchr(glyphs, c);
      if (p != NULL)
        for (col = 0; col < 5; col++)
          for (row = 0; row < 7; row++)
            if (font[p - glyphs][col] & (1 << row))
              rect(x + col * scale, y + row * scale, scale, scale, color);
      x += 6 * scale;
      s++;
    }
}

int velaops_dashboard_open(void)
{
  pthread_attr_t attr;
  int ret;

  g_fd = open("/dev/fb0", O_RDWR);
  if (g_fd < 0 || ioctl(g_fd, FBIOGET_VIDEOINFO, &g_video) < 0 ||
      ioctl(g_fd, FBIOGET_PLANEINFO, &g_plane) < 0)
    {
      if (g_fd >= 0) close(g_fd);
      g_fd = -1;
      return -1;
    }
  g_fb = mmap(NULL, g_plane.fblen, PROT_READ | PROT_WRITE,
              MAP_SHARED, g_fd, 0);
  if (g_fb == MAP_FAILED)
    {
      g_fb = NULL;
      close(g_fd);
      g_fd = -1;
      return -1;
    }

  g_page = 0;
  g_have_frame = false;
  g_draw_pending = false;
  g_draw_running = true;

  ret = pthread_attr_init(&attr);
  if (ret != 0 ||
      pthread_attr_setstacksize(&attr, DASHBOARD_THREAD_STACK_SIZE) != 0 ||
      pthread_create(&g_draw_thread, &attr, dashboard_worker, NULL) != 0)
    {
      if (ret == 0) pthread_attr_destroy(&attr);
      g_draw_running = false;
      munmap(g_fb, g_plane.fblen);
      close(g_fd);
      g_fb = NULL;
      g_fd = -1;
      return -1;
    }
  pthread_attr_destroy(&attr);

  printf("Dashboard: %ux%u, format=%u, bpp=%u, stride=%u, fblen=%lu\n",
         g_video.xres, g_video.yres, g_video.fmt, g_plane.bpp,
         g_plane.stride, (unsigned long)g_plane.fblen);
  return 0;
}

static void dashboard_draw_locked(const struct velaops_profile *p,
                                  const struct velaops_metrics *m,
                                  const char *status)
{
  char line[80];
  int cpu = m->valid ? (int)(m->cpu_percent + .5f) : 0;
  int mem = m->valid ? (int)(m->memory_percent + .5f) : 0;
  uint16_t bg = rgb(0, 0, 0);
  uint16_t fg = rgb(255, 255, 255);
  uint16_t cyan = rgb(0, 255, 255);
  uint16_t green = rgb(0, 255, 80);
  uint16_t status_color = green;

  if (strstr(status, "ALERT") != NULL || strstr(status, "ERROR") != NULL ||
      strstr(status, "FAILED") != NULL || strstr(status, "LOST") != NULL ||
      strstr(status, "CHANGED") != NULL)
    {
      status_color = rgb(255, 48, 48);
    }

  rect(0, 0, g_video.xres, g_video.yres, bg);
  rect(0, 0, g_video.xres, 38, rgb(0, 16, 32));
  text(12, 10, "VELAOPS", fg, 3);
  snprintf(line, sizeof(line), "%.24s@%.40s:%u", p->user, p->host, p->port);
  text(12, 50, line, fg, 1);
  text(12, 70, status, status_color, 1);
  rect(12, 86, 216, 2, rgb(48, 48, 48));
  if (g_page == 0)
    {
      snprintf(line, sizeof(line), "CPU %d%%", cpu);
      text(12, 100, line, fg, 2);
      rect(12, 120, 216, 14, rgb(32, 32, 32));
      rect(12, 120, 216 * cpu / 100, 14, cyan);
      snprintf(line, sizeof(line), "MEM %d%%", mem);
      text(12, 150, line, fg, 2);
      rect(12, 170, 216, 14, rgb(32, 32, 32));
      rect(12, 170, 216 * mem / 100, 14, green);
      snprintf(line, sizeof(line), "%lu/%lu MB",
               (m->memory_total_kb - m->memory_available_kb) / 1024,
               m->memory_total_kb / 1024);
      text(12, 192, line, fg, 1);
      text(184, 222, "1/2", cyan, 1);
    }
  else
    {
      snprintf(line, sizeof(line), "DISK %d%%", (int)m->disk_percent);
      text(12, 96, line, fg, 2);
      snprintf(line, sizeof(line), "LOAD %.1F %.1F %.1F", m->load_1min,
               m->load_5min, m->load_15min);
      text(12, 124, line, cyan, 1);
      snprintf(line, sizeof(line), "PROCS %d", m->process_count);
      text(12, 146, line, fg, 1);
      snprintf(line, sizeof(line), "UP %lu MIN", m->uptime_seconds / 60);
      text(12, 168, line, fg, 1);
      snprintf(line, sizeof(line), "RX %lu KB", m->net_rx_bytes / 1024);
      text(12, 190, line, fg, 1);
      snprintf(line, sizeof(line), "TX %lu KB", m->net_tx_bytes / 1024);
      text(12, 210, line, fg, 1);
      text(184, 222, "2/2", cyan, 1);
    }

  /* The LCD framebuffer is backed by ordinary memory.  Updating the mmap
   * contents alone does not transfer pixels to the ST7789 controller, so
   * explicitly flush the completed frame to the panel.
   */

  {
    struct fb_area_s area;

    area.x = 0;
    area.y = 0;
    area.w = g_video.xres;
    area.h = g_video.yres;
    if (ioctl(g_fd, FBIO_UPDATE, &area) < 0)
      {
        perror("Dashboard FBIO_UPDATE");
      }
  }
}

/* Own all framebuffer and panel I/O in one generously-sized worker.  Callers
 * only update the cached display model and wake this thread. */

static void *dashboard_worker(void *arg)
{
  (void)arg;

  pthread_mutex_lock(&g_draw_lock);
  while (g_draw_running)
    {
      while (g_draw_running && !g_draw_pending)
        {
          pthread_cond_wait(&g_draw_cond, &g_draw_lock);
        }

      if (g_draw_running && g_have_frame && g_fd >= 0)
        {
          g_draw_pending = false;
          dashboard_draw_locked(&g_last_profile, &g_last_metrics,
                                g_last_status);
        }
    }
  pthread_mutex_unlock(&g_draw_lock);
  return NULL;
}

void velaops_dashboard_draw(const struct velaops_profile *p,
                            const struct velaops_metrics *m,
                            const char *status)
{
  pthread_mutex_lock(&g_draw_lock);
  g_last_profile = *p;
  g_last_metrics = *m;
  snprintf(g_last_status, sizeof(g_last_status), "%s", status);
  g_have_frame = true;
  g_draw_pending = true;
  pthread_cond_signal(&g_draw_cond);
  pthread_mutex_unlock(&g_draw_lock);
}

void velaops_dashboard_next_page(void)
{
  pthread_mutex_lock(&g_draw_lock);
  g_page = (g_page + 1) % 2;
  if (g_have_frame && g_fd >= 0)
    {
      g_draw_pending = true;
      pthread_cond_signal(&g_draw_cond);
    }
  pthread_mutex_unlock(&g_draw_lock);
}

void velaops_dashboard_close(void)
{
  pthread_mutex_lock(&g_draw_lock);
  g_draw_running = false;
  pthread_cond_signal(&g_draw_cond);
  pthread_mutex_unlock(&g_draw_lock);

  pthread_join(g_draw_thread, NULL);

  pthread_mutex_lock(&g_draw_lock);
  if (g_fb != NULL && g_fb != MAP_FAILED) munmap(g_fb, g_plane.fblen);
  if (g_fd >= 0) close(g_fd);
  g_fb = NULL;
  g_fd = -1;
  g_have_frame = false;
  pthread_mutex_unlock(&g_draw_lock);
}
