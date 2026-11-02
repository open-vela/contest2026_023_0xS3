/****************************************************************************
 * VelaOps 240x240 运维看板渲染器。
 *
 * 参考成熟运维工具（top/htop/glances）的信息层级：顶部状态条给出总体健康
 * 与数据新鲜度，正文用大号数值 + 条形/柱形仪表突出关键指标，页脚给出操作
 * 提示。资源三页外再提供 LLM 结论三页：
 *   0) 概览：总体状态 + CPU/内存/磁盘三柱仪表 + 服务/端口速览
 *   1) 性能：CPU 使用率 + 负载 + 历史曲线，内存用量与明细
 *   2) 运维：服务状态、端口连通性、磁盘占用与数据更新时间
 ****************************************************************************/

#include "velaops_dashboard.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define FONT_WIDTH 5
#define FONT_HEIGHT 7
#define ADVANCE (FONT_WIDTH + 1)
#define HEADER_SEP_Y 36
#define CONTENT_TOP 44

typedef struct
{
  uint16_t *pixels;
} dashboard_canvas_t;

typedef enum
{
  DASH_OFFLINE = 0,
  DASH_CRITICAL,
  DASH_WARNING,
  DASH_HEALTHY
} dash_status_t;

static uint16_t rgb565(unsigned int red, unsigned int green,
                       unsigned int blue)
{
  return (uint16_t)(((red & 0xf8) << 8) | ((green & 0xfc) << 3) |
                    (blue >> 3));
}

/* ESP32-S3-EYE 小尺寸 LCD 的暗部抬升较明显，低亮度蓝色仍会发灰并降低
 * 文字对比度。背景与内容区统一使用纯黑，只用中性深灰绘制分隔和进度槽。
 */

static uint16_t color_background(void) { return rgb565(0, 0, 0); }
static uint16_t color_highlight(void) { return rgb565(36, 36, 40); }
static uint16_t color_text(void) { return rgb565(255, 255, 255); }
static uint16_t color_muted(void) { return rgb565(150, 150, 150); }
static uint16_t color_cyan(void) { return rgb565(0, 190, 255); }
static uint16_t color_green(void) { return rgb565(0, 220, 96); }
static uint16_t color_amber(void) { return rgb565(255, 174, 48); }
static uint16_t color_red(void) { return rgb565(255, 56, 72); }

static void fill_rect(dashboard_canvas_t *canvas, int x, int y, int width,
                      int height, uint16_t color)
{
  int px;
  int py;

  for (py = y; py < y + height; py++)
    {
      if (py < 0 || py >= VELAOPS_DISPLAY_HEIGHT)
        {
          continue;
        }

      for (px = x; px < x + width; px++)
        {
          if (px >= 0 && px < VELAOPS_DISPLAY_WIDTH)
            {
              canvas->pixels[py * VELAOPS_DISPLAY_WIDTH + px] = color;
            }
        }
    }
}

static void dot(dashboard_canvas_t *canvas, int center_x, int center_y,
                int radius, uint16_t color)
{
  int x;
  int y;

  for (y = -radius; y <= radius; y++)
    {
      for (x = -radius; x <= radius; x++)
        {
          if (x * x + y * y <= radius * radius)
            {
              fill_rect(canvas, center_x + x, center_y + y, 1, 1, color);
            }
        }
    }
}

static void glyph_for(char character, uint8_t glyph[FONT_WIDTH])
{
  static const uint8_t letters[][FONT_WIDTH] =
  {
    {0x1e, 0x05, 0x05, 0x1e, 0}, {0x1f, 0x15, 0x15, 0x0a, 0},
    {0x0e, 0x11, 0x11, 0x0a, 0}, {0x1f, 0x11, 0x11, 0x0e, 0},
    {0x1f, 0x15, 0x15, 0x11, 0}, {0x1f, 0x05, 0x05, 0x01, 0},
    {0x0e, 0x11, 0x15, 0x1d, 0}, {0x1f, 0x04, 0x04, 0x1f, 0},
    {0x11, 0x1f, 0x11, 0, 0},    {0x08, 0x10, 0x10, 0x0f, 0},
    {0x1f, 0x04, 0x0a, 0x11, 0}, {0x1f, 0x10, 0x10, 0x10, 0},
    {0x1f, 0x02, 0x04, 0x02, 0x1f}, {0x1f, 0x02, 0x04, 0x1f, 0},
    {0x0e, 0x11, 0x11, 0x0e, 0}, {0x1f, 0x05, 0x05, 0x02, 0},
    {0x0e, 0x11, 0x19, 0x1e, 0}, {0x1f, 0x05, 0x0d, 0x12, 0},
    {0x12, 0x15, 0x15, 0x09, 0}, {0x01, 0x1f, 0x01, 0, 0},
    {0x0f, 0x10, 0x10, 0x0f, 0}, {0x07, 0x08, 0x10, 0x08, 0x07},
    {0x1f, 0x08, 0x04, 0x08, 0x1f}, {0x11, 0x0a, 0x04, 0x0a, 0x11},
    {0x03, 0x04, 0x18, 0x04, 0x03}, {0x19, 0x15, 0x13, 0x11, 0}
  };
  static const uint8_t digits[][FONT_WIDTH] =
  {
    {0x0e, 0x11, 0x11, 0x0e, 0}, {0x12, 0x1f, 0x10, 0, 0},
    {0x19, 0x15, 0x15, 0x12, 0}, {0x11, 0x15, 0x15, 0x0a, 0},
    {0x07, 0x04, 0x1f, 0x04, 0}, {0x17, 0x15, 0x15, 0x09, 0},
    {0x0e, 0x15, 0x15, 0x08, 0}, {0x01, 0x01, 0x1d, 0x03, 0},
    {0x0a, 0x15, 0x15, 0x0a, 0}, {0x02, 0x15, 0x15, 0x0e, 0}
  };

  memset(glyph, 0, FONT_WIDTH);
  if (character >= 'a' && character <= 'z')
    {
      character = (char)(character - 'a' + 'A');
    }
  if (character >= 'A' && character <= 'Z')
    {
      memcpy(glyph, letters[character - 'A'], FONT_WIDTH);
    }
  else if (character >= '0' && character <= '9')
    {
      memcpy(glyph, digits[character - '0'], FONT_WIDTH);
    }
  else if (character == '%')
    {
      const uint8_t value[] = {0x23, 0x13, 0x08, 0x64, 0x62};
      memcpy(glyph, value, sizeof(value));
    }
  else if (character == ':')
    {
      glyph[1] = 0x0a;
      glyph[3] = 0x0a;
    }
  else if (character == '.')
    {
      glyph[1] = 0x10;
    }
  else if (character == '-')
    {
      glyph[1] = 0x04;
      glyph[2] = 0x04;
    }
  else if (character == '/')
    {
      glyph[0] = 0x10;
      glyph[1] = 0x08;
      glyph[2] = 0x04;
      glyph[3] = 0x02;
      glyph[4] = 0x01;
    }
  else if (character == '=')
    {
      glyph[1] = 0x0a;
      glyph[3] = 0x0a;
    }
  else if (character == '!')
    {
      glyph[1] = 0x17;
      glyph[3] = 0x11;
    }
}

static void draw_text(dashboard_canvas_t *canvas, int x, int y,
                      const char *text, int scale, uint16_t color)
{
  uint8_t glyph[FONT_WIDTH];
  size_t index;
  int gx;
  int gy;

  for (index = 0; text[index] != '\0'; index++)
    {
      glyph_for(text[index], glyph);
      for (gx = 0; gx < FONT_WIDTH; gx++)
        {
          for (gy = 0; gy < FONT_HEIGHT; gy++)
            {
              if ((glyph[gx] & (1U << gy)) != 0)
                {
                  fill_rect(canvas, x + (int)index * ADVANCE * scale +
                            gx * scale, y + gy * scale, scale, scale, color);
                }
            }
        }
    }
}

static int text_width(const char *text, int scale)
{
  return (int)strlen(text) * ADVANCE * scale;
}

static void draw_text_right(dashboard_canvas_t *canvas, int right, int y,
                            const char *text, int scale, uint16_t color)
{
  draw_text(canvas, right - text_width(text, scale), y, text, scale, color);
}

static void draw_text_center(dashboard_canvas_t *canvas, int x, int width,
                             int y, const char *text, int scale,
                             uint16_t color)
{
  int offset = (width - text_width(text, scale)) / 2;

  draw_text(canvas, x + (offset > 0 ? offset : 0), y, text, scale, color);
}

/* ── 状态与配色 ─────────────────────────────────────────────── */

static dash_status_t dash_overall(const velaops_display_state_t *state)
{
  double memory = 0.0;
  double cpu = 0.0;
  double disk = 0.0;
  int critical = 0;

  if (!state->online)
    {
      return DASH_OFFLINE;
    }
  if (!state->has_resources)
    {
      return DASH_WARNING;
    }

  memory = state->memory.used_percent;
  disk = state->resources.disk_percent;
  cpu = state->resources.cpu.valid ? state->resources.cpu.used_percent : 0.0;

  critical = !state->resources.service_active ||
             !state->resources.port_reachable ||
             memory >= 90.0 || disk >= 92.0 || cpu >= 90.0 ||
             state->health == VELAOPS_HEALTH_UNHEALTHY;
  if (critical)
    {
      return DASH_CRITICAL;
    }
  if (memory >= 75.0 || disk >= 80.0 || cpu >= 75.0)
    {
      return DASH_WARNING;
    }
  return DASH_HEALTHY;
}

static const char *dash_status_word(dash_status_t status)
{
  switch (status)
    {
      case DASH_OFFLINE:
        return "OFFLINE";
      case DASH_CRITICAL:
        return "CRITICAL";
      case DASH_WARNING:
        return "WARNING";
      default:
        return "HEALTHY";
    }
}

static uint16_t dash_status_color(dash_status_t status)
{
  switch (status)
    {
      case DASH_OFFLINE:
      case DASH_CRITICAL:
        return color_red();
      case DASH_WARNING:
        return color_amber();
      default:
        return color_green();
    }
}

static double clamp_percent(double value)
{
  if (value < 0.0)
    {
      return 0.0;
    }
  if (value > 100.0)
    {
      return 100.0;
    }
  return value;
}

static uint16_t level_color(double percent, double warning, double critical)
{
  if (percent >= critical)
    {
      return color_red();
    }
  if (percent >= warning)
    {
      return color_amber();
    }
  return color_cyan();
}

/* ── 图元 ───────────────────────────────────────────────────── */

static void draw_bar(dashboard_canvas_t *canvas, int x, int y, int width,
                     int height, double percent, uint16_t color)
{
  int filled = (int)((double)(width - 2) * clamp_percent(percent) / 100.0 +
                     0.5);

  fill_rect(canvas, x, y, width, height, color_highlight());
  if (filled > 0)
    {
      fill_rect(canvas, x + 1, y + 1, filled, height - 2, color);
    }
}

static void draw_gauge(dashboard_canvas_t *canvas, int x, int y, int width,
                       int height, double percent, uint16_t color)
{
  int filled = (int)((double)(height - 2) * clamp_percent(percent) / 100.0 +
                     0.5);

  fill_rect(canvas, x, y, width, height, color_highlight());
  if (filled > 0)
    {
      fill_rect(canvas, x + 1, y + height - 1 - filled, width - 2, filled,
                color);
    }
}

/* 屏显数值统一用整数拼接，不依赖 libc 的浮点/长整型 printf。NuttX 上
 * 带长度修饰的 printf 与浮点格式化在该屏显路径下曾触发难以定位的崩溃，
 * 整数格式化既稳定又省栈。 */

static char *format_percent1(char *buffer, size_t capacity, double percent)
{
  int scaled = (int)(clamp_percent(percent) * 10.0 + 0.5);

  snprintf(buffer, capacity, "%d.%d%%", scaled / 10, scaled % 10);
  return buffer;
}

static char *format_load(char *buffer, size_t capacity, double load)
{
  int scaled = (int)(load * 100.0 + 0.5);

  if (scaled < 0)
    {
      scaled = 0;
    }
  snprintf(buffer, capacity, "%d.%02d", scaled / 100, scaled % 100);
  return buffer;
}

static char *format_gb(char *buffer, size_t capacity, uint64_t bytes)
{
  if (bytes >= UINT64_C(1024) * 1024 * 1024)
    {
      unsigned long tenths =
          (unsigned long)((bytes * 10U + (UINT64_C(1) << 29)) >> 30);

      snprintf(buffer, capacity, "%lu.%luG", tenths / 10, tenths % 10);
    }
  else
    {
      snprintf(buffer, capacity, "%luM",
               (unsigned long)(bytes >> 20));
    }
  return buffer;
}

/* ── 顶部状态条 ─────────────────────────────────────────────── */

static void draw_header(dashboard_canvas_t *canvas,
                        const velaops_display_state_t *state,
                        unsigned int page)
{
  dash_status_t status = dash_overall(state);
  uint16_t accent = dash_status_color(status);
  unsigned int index;

  dot(canvas, 15, 15, 5, accent);
  draw_text(canvas, 27, 8, "VELAOPS", 2, color_text());
  draw_text_right(canvas, 228, 11, dash_status_word(status), 1, accent);
  draw_text(canvas, 12, 26,
            state->online && state->has_resources ? "REALTIME MONITOR" :
                                                    "WAITING FOR DATA",
            1, color_muted());

  for (index = 0; index < VELAOPS_DISPLAY_PAGE_COUNT; index++)
    {
      dot(canvas, 208 + (int)index * 12, 29, index == page ? 3 : 2,
          index == page ? color_cyan() : color_highlight());
    }

  fill_rect(canvas, 10, HEADER_SEP_Y, 220, 1, color_highlight());
}

/* ── 第 0 页：概览 ──────────────────────────────────────────── */

static void draw_metric_card(dashboard_canvas_t *canvas, int x, int y,
                             int width, const char *label, double percent,
                             uint16_t color, int valid)
{
  char value[12];

  draw_text_center(canvas, x, width, y, label, 1, color_muted());
  draw_gauge(canvas, x + width / 2 - 8, y + 16, 16, 46, valid ? percent : 0.0,
             valid ? color : color_highlight());
  if (valid)
    {
      snprintf(value, sizeof(value), "%d%%",
               (int)(clamp_percent(percent) + 0.5));
    }
  else
    {
      snprintf(value, sizeof(value), "--");
    }
  draw_text_center(canvas, x, width, y + 68, value, 2, color_text());
}

static void draw_overview(dashboard_canvas_t *canvas,
                          const velaops_display_state_t *state)
{
  dash_status_t status = dash_overall(state);
  uint16_t accent = dash_status_color(status);
  double disk = state->has_resources ? state->resources.disk_percent : 0.0;
  int cpu_valid = state->has_resources && state->resources.cpu.valid;
  double cpu = cpu_valid ? state->resources.cpu.used_percent : 0.0;
  double memory = state->has_resources ? state->memory.used_percent : 0.0;
  char line[32];

  draw_text(canvas, 12, CONTENT_TOP, "SYSTEM STATUS", 1, color_muted());
  draw_text(canvas, 12, CONTENT_TOP + 12, dash_status_word(status), 3,
            accent);

  draw_metric_card(canvas, 10, 104, 68, "CPU", cpu,
                   level_color(cpu, 75.0, 90.0), cpu_valid);
  draw_metric_card(canvas, 86, 104, 68, "MEM", memory,
                   level_color(memory, 75.0, 90.0), state->has_resources);
  draw_metric_card(canvas, 162, 104, 68, "DISK", disk,
                   level_color(disk, 80.0, 92.0), state->has_resources);

  /* 页脚：服务与端口速览。 */
  fill_rect(canvas, 10, 194, 220, 1, color_highlight());
  if (state->has_resources)
    {
      int active = state->resources.service_active;
      int reachable = state->resources.port_reachable;

      dot(canvas, 16, 208, 4, active ? color_green() : color_red());
      draw_text(canvas, 26, 205,
                active ? "SERVICE ACTIVE" : "SERVICE DOWN", 1,
                active ? color_text() : color_red());
      dot(canvas, 16, 224, 4, reachable ? color_green() : color_red());
      snprintf(line, sizeof(line), "PORT %s %dMS",
               reachable ? "OPEN" : "CLOSED",
               state->resources.port_latency_ms);
      draw_text(canvas, 26, 221, line, 1,
                reachable ? color_text() : color_red());
    }
  else
    {
      draw_text(canvas, 12, 205, "SERVICE NO DATA", 1, color_muted());
      draw_text(canvas, 12, 221, "PORT NO DATA", 1, color_muted());
    }
}

/* ── 第 1 页：性能 ──────────────────────────────────────────── */

static void draw_sparkline(dashboard_canvas_t *canvas, int x, int y,
                           int width, int height,
                           const velaops_display_state_t *state)
{
  int count = state->cpu_history_len;
  int start;
  int index;
  int columns;

  fill_rect(canvas, x, y + height - 1, width, 1, color_highlight());
  if (count < 2)
    {
      return;
    }

  columns = count < width ? count : width;
  start = count - columns;
  for (index = 0; index < columns; index++)
    {
      double value = clamp_percent(state->cpu_history[start + index]);
      int bar = (int)((double)(height - 2) * value / 100.0 + 0.5);

      if (bar > 0)
        {
          fill_rect(canvas, x + index, y + height - 1 - bar, 1, bar,
                    color_cyan());
        }
    }
}

static void draw_performance(dashboard_canvas_t *canvas,
                             const velaops_display_state_t *state)
{
  int cpu_valid = state->has_resources && state->resources.cpu.valid;
  double cpu = cpu_valid ? state->resources.cpu.used_percent : 0.0;
  double memory = state->has_resources ? state->memory.used_percent : 0.0;
  char value[16];
  char line[64];
  char used[16];
  char total[16];

  /* CPU 区块 */
  draw_text(canvas, 12, CONTENT_TOP, "CPU UTILIZATION", 1, color_muted());
  if (cpu_valid)
    {
      format_percent1(value, sizeof(value), cpu);
    }
  else
    {
      snprintf(value, sizeof(value), "--");
    }
  draw_text(canvas, 12, CONTENT_TOP + 12, value, 3,
            level_color(cpu, 75.0, 90.0));
  draw_bar(canvas, 12, CONTENT_TOP + 44, 216, 12, cpu,
           level_color(cpu, 75.0, 90.0));

  if (cpu_valid)
    {
      char load1[16];
      char load5[16];
      char load15[16];

      format_load(load1, sizeof(load1), state->resources.cpu.load1);
      format_load(load5, sizeof(load5), state->resources.cpu.load5);
      format_load(load15, sizeof(load15), state->resources.cpu.load15);
      snprintf(line, sizeof(line), "LOAD %s %s %s", load1, load5, load15);
      draw_text(canvas, 12, CONTENT_TOP + 62, line, 1, color_text());
      snprintf(line, sizeof(line), "%ld CORES",
               (long)state->resources.cpu.cores);
      draw_text_right(canvas, 228, CONTENT_TOP + 62, line, 1, color_muted());
    }
  else
    {
      draw_text(canvas, 12, CONTENT_TOP + 62, "NO CPU DATA", 1,
                color_muted());
    }
  draw_sparkline(canvas, 12, CONTENT_TOP + 76, 216, 26, state);

  /* 内存区块 */
  fill_rect(canvas, 10, 156, 220, 1, color_highlight());
  draw_text(canvas, 12, 164, "MEMORY", 1, color_muted());
  if (state->has_resources)
    {
      format_percent1(value, sizeof(value), memory);
    }
  else
    {
      snprintf(value, sizeof(value), "--");
    }
  draw_text(canvas, 12, 176, value, 3, level_color(memory, 75.0, 90.0));

  if (state->has_resources)
    {
      format_gb(total, sizeof(total), state->memory.total_bytes);
      format_gb(used, sizeof(used), state->memory.used_bytes);
      snprintf(line, sizeof(line), "USED %s / %s", used, total);
      draw_text(canvas, 12, 208, line, 1, color_text());
    }
  else
    {
      draw_text(canvas, 12, 208, "USED -- / --", 1, color_muted());
    }
  draw_bar(canvas, 12, 222, 216, 10, memory, level_color(memory, 75.0, 90.0));
}

/* ── 第 2 页：运维 ──────────────────────────────────────────── */

static void draw_ops_row(dashboard_canvas_t *canvas, int y, const char *label,
                         const char *value, uint16_t value_color)
{
  draw_text(canvas, 12, y, label, 1, color_muted());
  draw_text_right(canvas, 228, y, value, 2, value_color);
}

static void draw_ops(dashboard_canvas_t *canvas,
                     const velaops_display_state_t *state)
{
  char line[24];
  double disk = state->has_resources ? state->resources.disk_percent : 0.0;

  if (!state->has_resources)
    {
      draw_text(canvas, 12, CONTENT_TOP, "OPS MONITOR", 1, color_muted());
      draw_text(canvas, 12, CONTENT_TOP + 16, "NO DATA", 3, color_muted());
      draw_text(canvas, 12, 200, "WAITING FOR FIRST POLL", 1, color_muted());
      return;
    }

  draw_text(canvas, 12, CONTENT_TOP, "SERVICE", 1, color_muted());
  if (state->resources.service_active)
    {
      draw_text(canvas, 12, CONTENT_TOP + 14, "ACTIVE", 3, color_green());
    }
  else
    {
      draw_text(canvas, 12, CONTENT_TOP + 14, "DOWN", 3, color_red());
    }
  fill_rect(canvas, 10, 92, 220, 1, color_highlight());

  draw_ops_row(canvas, 100, "PROXY PORT",
               state->resources.port_reachable ? "OPEN" : "CLOSED",
               state->resources.port_reachable ? color_green() : color_red());
  snprintf(line, sizeof(line), "%d MS", state->resources.port_latency_ms);
  draw_ops_row(canvas, 118, "LATENCY", line,
               state->resources.port_latency_ms > 500 ? color_amber() :
                                                        color_cyan());
  fill_rect(canvas, 10, 138, 220, 1, color_highlight());

  format_percent1(line, sizeof(line), disk);
  draw_ops_row(canvas, 146, "DISK USED", line,
               level_color(disk, 80.0, 92.0));
  draw_bar(canvas, 12, 168, 216, 10, disk, level_color(disk, 80.0, 92.0));

  draw_text(canvas, 12, 190, "BOOT: NEXT PAGE", 1, color_muted());
}

/* LLM 回复拆成三页，避免在 5x7 字库上压缩成一条难读的长句。 */
static void draw_llm_page(dashboard_canvas_t *canvas, unsigned int subpage)
{
  const char *titles[] = {"LLM SUMMARY", "LLM ROOT CAUSE", "LLM ACTION"};
  char line[128] = {0};
  FILE *file;
  unsigned int index;

  draw_text(canvas, 12, CONTENT_TOP, titles[subpage], 1, color_muted());
  file = fopen("/tmp/velaops-llm-pages.txt", "r");
  if (file == NULL)
    {
      draw_text(canvas, 12, CONTENT_TOP + 22, "WAITING FOR LLM", 2,
                color_muted());
      return;
    }

  for (index = 0; index <= subpage; index++)
    {
      if (fgets(line, sizeof(line), file) == NULL)
        {
          line[0] = '\0';
          break;
        }
    }
  fclose(file);
  line[strcspn(line, "\r\n")] = '\0';
  line[18] = '\0';
  if (line[0] == '\0')
    {
      snprintf(line, sizeof(line), "NO %s", subpage == 0 ? "SUMMARY" :
               subpage == 1 ? "ROOT CAUSE" : "ACTION");
    }
  draw_text(canvas, 12, CONTENT_TOP + 24, line, 2, color_text());
  draw_text(canvas, 12, 190, "BOOT: LLM PAGES", 1, color_muted());
}

int velaops_dashboard_render(uint16_t *pixels, size_t pixel_count,
                             const velaops_display_state_t *state,
                             unsigned int page)
{
  dashboard_canvas_t canvas;

  if (pixels == NULL || state == NULL ||
      pixel_count < VELAOPS_DISPLAY_WIDTH * VELAOPS_DISPLAY_HEIGHT ||
      page >= VELAOPS_DISPLAY_PAGE_COUNT)
    {
      return -EINVAL;
    }

  canvas.pixels = pixels;
  fill_rect(&canvas, 0, 0, VELAOPS_DISPLAY_WIDTH, VELAOPS_DISPLAY_HEIGHT,
            color_background());
  draw_header(&canvas, state, page);

  if (page == 0)
    {
      draw_overview(&canvas, state);
    }
  else if (page == 1)
    {
      draw_performance(&canvas, state);
    }
  else if (page == 2)
    {
      draw_ops(&canvas, state);
    }
  else
    {
      draw_llm_page(&canvas, page - 3);
    }

  return 0;
}

void velaops_dashboard_push_cpu(velaops_display_state_t *state)
{
  if (state == NULL || !state->resources.cpu.valid)
    {
      return;
    }

  if (state->cpu_history_len < VELAOPS_DISPLAY_CPU_HISTORY)
    {
      state->cpu_history[state->cpu_history_len++] =
          (float)state->resources.cpu.used_percent;
    }
  else
    {
      memmove(&state->cpu_history[0], &state->cpu_history[1],
              sizeof(state->cpu_history[0]) *
                  (VELAOPS_DISPLAY_CPU_HISTORY - 1));
      state->cpu_history[VELAOPS_DISPLAY_CPU_HISTORY - 1] =
          (float)state->resources.cpu.used_percent;
    }
}

int velaops_dashboard_render_test(uint16_t *pixels, size_t pixel_count)
{
  static const char *labels[] =
  {
    "RED", "GREEN", "BLUE", "CYAN", "MAGENTA", "YELLOW", "WHITE"
  };
  uint16_t colors[] =
  {
    rgb565(255, 0, 0), rgb565(0, 255, 0), rgb565(0, 0, 255),
    rgb565(0, 255, 255), rgb565(255, 0, 255), rgb565(255, 255, 0),
    rgb565(255, 255, 255)
  };
  dashboard_canvas_t canvas;
  unsigned int index;

  if (pixels == NULL ||
      pixel_count < VELAOPS_DISPLAY_WIDTH * VELAOPS_DISPLAY_HEIGHT)
    {
      return -EINVAL;
    }

  canvas.pixels = pixels;
  fill_rect(&canvas, 0, 0, VELAOPS_DISPLAY_WIDTH, VELAOPS_DISPLAY_HEIGHT,
            rgb565(0, 0, 0));
  draw_text(&canvas, 12, 10, "RGB565 COLOR TEST", 2,
            rgb565(255, 255, 255));
  fill_rect(&canvas, 10, 31, 220, 1, rgb565(255, 255, 255));

  for (index = 0; index < sizeof(colors) / sizeof(colors[0]); index++)
    {
      int y = 42 + (int)index * 23;

      draw_text(&canvas, 12, y + 4, labels[index], 1,
                rgb565(255, 255, 255));
      fill_rect(&canvas, 76, y, 154, 15, colors[index]);
    }

  draw_text(&canvas, 12, 207, "GRAY", 1, rgb565(255, 255, 255));
  fill_rect(&canvas, 76, 205, 30, 20, rgb565(0, 0, 0));
  fill_rect(&canvas, 106, 205, 31, 20, rgb565(64, 64, 64));
  fill_rect(&canvas, 137, 205, 31, 20, rgb565(128, 128, 128));
  fill_rect(&canvas, 168, 205, 31, 20, rgb565(192, 192, 192));
  fill_rect(&canvas, 199, 205, 31, 20, rgb565(255, 255, 255));
  fill_rect(&canvas, 10, 233, 220, 1, rgb565(255, 255, 255));
  return 0;
}

static void draw_text_centered(dashboard_canvas_t *canvas, int y,
                               const char *text, int scale, uint16_t color)
{
  int x = (VELAOPS_DISPLAY_WIDTH - text_width(text, scale)) / 2;

  if (x < 0)
    {
      x = 0;
    }
  draw_text(canvas, x, y, text, scale, color);
}

int velaops_dashboard_render_message(uint16_t *pixels, size_t pixel_count,
                                     const char *title, const char *text,
                                     int blink_phase)
{
  dashboard_canvas_t canvas;
  char line[2][13];
  uint16_t border;
  size_t length;
  size_t first;
  int lines;

  if (pixels == NULL || title == NULL || text == NULL ||
      pixel_count < VELAOPS_DISPLAY_WIDTH * VELAOPS_DISPLAY_HEIGHT)
    {
      return -EINVAL;
    }

  /* 边框在两种高对比颜色间交替，由调用方按固定节奏重绘实现闪烁。 */
  border = blink_phase != 0 ? color_amber() : color_red();
  canvas.pixels = pixels;
  fill_rect(&canvas, 0, 0, VELAOPS_DISPLAY_WIDTH, VELAOPS_DISPLAY_HEIGHT,
            color_background());

  /* 消息框边框：四条 3 像素色带。 */
  fill_rect(&canvas, 10, 36, 220, 3, border);
  fill_rect(&canvas, 10, 201, 220, 3, border);
  fill_rect(&canvas, 10, 36, 3, 168, border);
  fill_rect(&canvas, 227, 36, 3, 168, border);

  draw_text_centered(&canvas, 52, title, 2, border);
  fill_rect(&canvas, 22, 74, 196, 1, color_highlight());

  /* 正文最多两行，每行最多 12 个字符；字库无中文，非 ASCII 在屏显层之前
   * 已被净化。 */
  length = strlen(text);
  if (length > 24)
    {
      length = 24;
    }
  first = length <= 12 ? length : 12;
  memcpy(line[0], text, first);
  line[0][first] = '\0';
  lines = 1;
  if (length > 12)
    {
      memcpy(line[1], text + 12, length - 12);
      line[1][length - 12] = '\0';
      lines = 2;
    }

  if (lines == 1)
    {
      draw_text_centered(&canvas, 108, line[0], 3, color_text());
    }
  else
    {
      draw_text_centered(&canvas, 92, line[0], 3, color_text());
      draw_text_centered(&canvas, 120, line[1], 3, color_text());
    }

  draw_text_centered(&canvas, 168, "PRESS BOOT TO CLOSE", 1, color_muted());
  return 0;
}
