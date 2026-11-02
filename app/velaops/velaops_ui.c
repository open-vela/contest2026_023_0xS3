/****************************************************************************
 * apps/examples/velaops/velaops_ui.c
 *
 * VelaOps 320x240 landscape dashboard renderer.
 * ASCII uses the built-in 5x7 font, Chinese labels use a 16x16 subset
 * generated into velaops_cn_font.h.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "velaops_ui.h"
#include "velaops_cn_font.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define FONT_WIDTH  5
#define FONT_HEIGHT 7
#define ADVANCE     6
#define CJK_SIZE    16

#define COL_BG      0x0000
#define COL_PANEL   0x18e3
#define COL_BORDER  0x4208
#define COL_TEXT    0xffff
#define COL_DIM     0x8410
#define COL_GREEN   0x07e0
#define COL_AMBER   0xfd20
#define COL_RED     0xf800
#define COL_CYAN    0x07ff

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void fill_rect(uint16_t *fb, int w, int h,
                      int x, int y, int width, int height, uint16_t color)
{
  int px;
  int py;

  for (py = y; py < y + height; py++)
    {
      if (py < 0 || py >= h)
        {
          continue;
        }

      for (px = x; px < x + width; px++)
        {
          if (px >= 0 && px < w)
            {
              fb[py * w + px] = color;
            }
        }
    }
}

static void glyph_for(char c, uint8_t glyph[FONT_WIDTH])
{
  static const uint8_t letters[][FONT_WIDTH] =
  {
    {0x7e, 0x11, 0x11, 0x11, 0x7e}, {0x7f, 0x49, 0x49, 0x49, 0x36},
    {0x3e, 0x41, 0x41, 0x41, 0x22}, {0x7f, 0x41, 0x41, 0x22, 0x1c},
    {0x7f, 0x49, 0x49, 0x49, 0x41}, {0x7f, 0x09, 0x09, 0x09, 0x01},
    {0x3e, 0x41, 0x49, 0x49, 0x7a}, {0x7f, 0x08, 0x08, 0x08, 0x7f},
    {0x00, 0x41, 0x7f, 0x41, 0x00}, {0x20, 0x40, 0x41, 0x3f, 0x01},
    {0x7f, 0x08, 0x14, 0x22, 0x41}, {0x7f, 0x40, 0x40, 0x40, 0x40},
    {0x7f, 0x02, 0x0c, 0x02, 0x7f}, {0x7f, 0x04, 0x08, 0x10, 0x7f},
    {0x3e, 0x41, 0x41, 0x41, 0x3e}, {0x7f, 0x09, 0x09, 0x09, 0x06},
    {0x3e, 0x41, 0x51, 0x21, 0x5e}, {0x7f, 0x09, 0x19, 0x29, 0x46},
    {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7f, 0x01, 0x01},
    {0x3f, 0x40, 0x40, 0x40, 0x3f}, {0x1f, 0x20, 0x40, 0x20, 0x1f},
    {0x3f, 0x40, 0x38, 0x40, 0x3f}, {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43}
  };
  static const uint8_t lower[][FONT_WIDTH] =
  {
    {0x20, 0x54, 0x54, 0x54, 0x78}, {0x7f, 0x48, 0x44, 0x44, 0x38},
    {0x38, 0x44, 0x44, 0x44, 0x28}, {0x38, 0x44, 0x44, 0x48, 0x7f},
    {0x38, 0x54, 0x54, 0x54, 0x18}, {0x08, 0x7e, 0x09, 0x01, 0x02},
    {0x08, 0x54, 0x54, 0x54, 0x3c}, {0x7f, 0x08, 0x04, 0x04, 0x78},
    {0x00, 0x44, 0x7d, 0x40, 0x00}, {0x20, 0x40, 0x44, 0x3d, 0x00},
    {0x7f, 0x10, 0x28, 0x44, 0x00}, {0x00, 0x41, 0x7f, 0x40, 0x00},
    {0x7c, 0x04, 0x18, 0x04, 0x78}, {0x7c, 0x08, 0x04, 0x04, 0x78},
    {0x38, 0x44, 0x44, 0x44, 0x38}, {0x7c, 0x14, 0x14, 0x14, 0x08},
    {0x08, 0x14, 0x14, 0x14, 0x7c}, {0x7c, 0x08, 0x04, 0x04, 0x08},
    {0x48, 0x54, 0x54, 0x54, 0x24}, {0x04, 0x3f, 0x44, 0x40, 0x20},
    {0x3c, 0x40, 0x40, 0x20, 0x7c}, {0x1c, 0x20, 0x40, 0x20, 0x1c},
    {0x3c, 0x40, 0x30, 0x40, 0x3c}, {0x44, 0x28, 0x10, 0x28, 0x44},
    {0x0c, 0x50, 0x50, 0x50, 0x3c}, {0x44, 0x64, 0x54, 0x4c, 0x44}
  };
  static const uint8_t digits[][FONT_WIDTH] =
  {
    {0x3e, 0x51, 0x49, 0x45, 0x3e}, {0x00, 0x42, 0x7f, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4b, 0x31},
    {0x18, 0x14, 0x12, 0x7f, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3c, 0x4a, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1e}
  };

  memset(glyph, 0, FONT_WIDTH);

  if (c >= 'a' && c <= 'z')
    {
      memcpy(glyph, lower[c - 'a'], FONT_WIDTH);
    }
  else if (c >= 'A' && c <= 'Z')
    {
      memcpy(glyph, letters[c - 'A'], FONT_WIDTH);
    }
  else if (c >= '0' && c <= '9')
    {
      memcpy(glyph, digits[c - '0'], FONT_WIDTH);
    }
  else if (c == '%')
    {
      const uint8_t v[] = {0x23, 0x13, 0x08, 0x64, 0x62};
      memcpy(glyph, v, sizeof(v));
    }
  else if (c == ':')
    {
      const uint8_t v[] = {0x00, 0x36, 0x36, 0x00, 0x00};
      memcpy(glyph, v, sizeof(v));
    }
  else if (c == '.')
    {
      const uint8_t v[] = {0x00, 0x60, 0x60, 0x00, 0x00};
      memcpy(glyph, v, sizeof(v));
    }
  else if (c == '-')
    {
      const uint8_t v[] = {0x08, 0x08, 0x08, 0x08, 0x08};
      memcpy(glyph, v, sizeof(v));
    }
  else if (c == '/')
    {
      const uint8_t v[] = {0x20, 0x10, 0x08, 0x04, 0x02};
      memcpy(glyph, v, sizeof(v));
    }
  else if (c == '=')
    {
      const uint8_t v[] = {0x14, 0x14, 0x14, 0x14, 0x14};
      memcpy(glyph, v, sizeof(v));
    }
  else if (c == '!')
    {
      const uint8_t v[] = {0x00, 0x00, 0x5f, 0x00, 0x00};
      memcpy(glyph, v, sizeof(v));
    }
  else if (c == '?')
    {
      const uint8_t v[] = {0x02, 0x01, 0x51, 0x09, 0x06};
      memcpy(glyph, v, sizeof(v));
    }
  else if (c == ',')
    {
      const uint8_t v[] = {0x00, 0x50, 0x30, 0x00, 0x00};
      memcpy(glyph, v, sizeof(v));
    }
  else if (c == '_')
    {
      const uint8_t v[] = {0x40, 0x40, 0x40, 0x40, 0x40};
      memcpy(glyph, v, sizeof(v));
    }
}

static uint32_t utf8_next(const char **pp)
{
  const unsigned char *p = (const unsigned char *)*pp;
  uint32_t cp;

  if (*p < 0x80)
    {
      cp = *p;
      *pp += 1;
    }
  else if ((*p & 0xe0) == 0xc0 && p[1] != 0)
    {
      cp = ((uint32_t)(p[0] & 0x1f) << 6) | (p[1] & 0x3f);
      *pp += 2;
    }
  else if ((*p & 0xf0) == 0xe0 && p[1] != 0 && p[2] != 0)
    {
      cp = ((uint32_t)(p[0] & 0x0f) << 12) |
           ((uint32_t)(p[1] & 0x3f) << 6) | (p[2] & 0x3f);
      *pp += 3;
    }
  else
    {
      cp = '?';
      *pp += 1;
    }

  return cp;
}

static const uint8_t *cjk_glyph(uint32_t cp)
{
  int lo = 0;
  int hi = (int)(sizeof(g_cjk_font) / sizeof(g_cjk_font[0])) - 1;

  while (lo <= hi)
    {
      int mid = (lo + hi) / 2;

      if (g_cjk_font[mid].codepoint == cp)
        {
          return g_cjk_font[mid].bitmap;
        }

      if (g_cjk_font[mid].codepoint < cp)
        {
          lo = mid + 1;
        }
      else
        {
          hi = mid - 1;
        }
    }

  return NULL;
}

static void draw_cjk(uint16_t *fb, int w, int h, int x, int y,
                     const uint8_t *bmp, uint16_t color)
{
  int gx;
  int gy;

  for (gy = 0; gy < CJK_SIZE; gy++)
    {
      for (gx = 0; gx < CJK_SIZE; gx++)
        {
          if ((bmp[gy * 2 + gx / 8] & (0x80 >> (gx % 8))) != 0)
            {
              fill_rect(fb, w, h, x + gx, y + gy, 1, 1, color);
            }
        }
    }
}

static void draw_text(uint16_t *fb, int w, int h, int x, int y,
                      const char *text, int scale, uint16_t color)
{
  uint8_t glyph[FONT_WIDTH];
  const char *p = text;
  int cx = x;

  while (*p != '\0')
    {
      uint32_t cp = utf8_next(&p);

      if (cp < 0x80)
        {
          /* Center the 5x7 cell inside a 16px CJK row: the ASCII ink is
           * 14px tall at 2x and would otherwise sit on the very top. */

          int dy = (scale == 2) ? 1 : 0;
          int gx;
          int gy;

          glyph_for((char)cp, glyph);
          for (gx = 0; gx < FONT_WIDTH; gx++)
            {
              for (gy = 0; gy < FONT_HEIGHT; gy++)
                {
                  if ((glyph[gx] & (1U << gy)) != 0)
                    {
                      fill_rect(fb, w, h, cx + gx * scale,
                                y + dy + gy * scale, scale, scale, color);
                    }
                }
            }

          cx += ADVANCE * scale;
        }
      else
        {
          const uint8_t *bmp = cjk_glyph(cp);

          if (bmp != NULL)
            {
              draw_cjk(fb, w, h, cx, y, bmp, color);
            }
          else
            {
              draw_text(fb, w, h, cx, y, "?", scale, color);
            }

          cx += CJK_SIZE;
        }
    }
}

static int text_width(const char *text, int scale)
{
  const char *p = text;
  int width = 0;

  while (*p != '\0')
    {
      uint32_t cp = utf8_next(&p);
      width += (cp < 0x80) ? ADVANCE * scale : CJK_SIZE;
    }

  return width;
}

static void draw_text_right(uint16_t *fb, int w, int h, int right, int y,
                            const char *text, int scale, uint16_t color)
{
  draw_text(fb, w, h, right - text_width(text, scale), y, text, scale,
            color);
}

static void draw_text_center(uint16_t *fb, int w, int h, int cx, int y,
                             const char *text, int scale, uint16_t color)
{
  draw_text(fb, w, h, cx - text_width(text, scale) / 2, y, text, scale,
            color);
}

static void draw_bar(uint16_t *fb, int w, int h, int x, int y,
                     int width, float percent, uint16_t color)
{
  int filled;

  if (percent < 0.0f)
    {
      percent = 0.0f;
    }

  if (percent > 100.0f)
    {
      percent = 100.0f;
    }

  fill_rect(fb, w, h, x, y, width, 8, COL_BORDER);
  filled = (int)((float)(width - 2) * percent / 100.0f);
  fill_rect(fb, w, h, x + 1, y + 1, filled, 6, color);
}

static uint16_t level_color(float percent)
{
  if (percent >= 90.0f)
    {
      return COL_RED;
    }

  if (percent >= 75.0f)
    {
      return COL_AMBER;
    }

  return COL_GREEN;
}

#define TEXT_LINE_MAX 80

static int wrap_text(const char *text, char lines[][TEXT_LINE_MAX],
                     int max_lines, int max_width)
{
  int count = 0;
  const char *p = text;

  while (*p != '\0' && count < max_lines)
    {
      const char *start = p;
      const char *last_space = NULL;
      int width = 0;
      int len;

      while (*p != '\0' && *p != '\n')
        {
          const char *save = p;
          uint32_t cp = utf8_next(&p);
          int cw = (cp < 0x80) ? ADVANCE * 2 : CJK_SIZE;

          if (width + cw > max_width)
            {
              p = save;
              break;
            }

          width += cw;

          if (cp == ' ')
            {
              last_space = save;
            }

          if ((size_t)(p - start) >= TEXT_LINE_MAX - 4)
            {
              break;
            }
        }

      if (*p != '\0' && *p != '\n' && last_space != NULL &&
          last_space > start)
        {
          p = last_space;
        }

      len = (int)(p - start);

      while (len > 0 && start[len - 1] == ' ')
        {
          len--;
        }

      memcpy(lines[count], start, len);
      lines[count][len] = '\0';
      count++;

      if (*p == ' ' || *p == '\n')
        {
          p++;
        }
    }

  return count;
}

static const char *overall_word(const velaops_ui_state_t *s, uint16_t *color)
{
  if (!s->online)
    {
      *color = COL_RED;
      return "离线";
    }

  if (s->cpu < 0.0f)
    {
      *color = COL_AMBER;
      return "无数据";
    }

  if (s->health_critical || s->score < 60)
    {
      *color = COL_RED;
      return "严重";
    }

  if (s->score < 70)
    {
      *color = COL_RED;
      return "告警";
    }

  if (s->score < 85 || s->cpu >= 75.0f || s->mem >= 75.0f ||
      s->disk >= 80.0f)
    {
      *color = COL_AMBER;
      return "注意";
    }

  *color = COL_GREEN;
  return "正常";
}

static void draw_header(uint16_t *fb, int w, int h,
                        const velaops_ui_state_t *s, int page)
{
  uint16_t color;
  const char *word = overall_word(s, &color);
  char buf[24];

  fill_rect(fb, w, h, 0, 0, w, 28, COL_PANEL);
  fill_rect(fb, w, h, 0, 28, w, 1, COL_BORDER);

  draw_text(fb, w, h, 8, 6, "VelaOps", 2, COL_GREEN);

  fill_rect(fb, w, h, 100, 12, 8, 8, color);
  draw_text(fb, w, h, 112, 6, word, 2, color);

  snprintf(buf, sizeof(buf), "第%d/6页", page + 1);
  draw_text_right(fb, w, h, w - 8, 6, buf, 2, COL_TEXT);
}

static void draw_footer(uint16_t *fb, int w, int h,
                        const velaops_ui_state_t *s)
{
  fill_rect(fb, w, h, 0, h - 20, w, 1, COL_BORDER);
  draw_text(fb, w, h, 8, h - 18, "UP/DN翻页 BACK静音", 2, COL_DIM);
  draw_text_right(fb, w, h, w - 8, h - 18,
                  s->clock != NULL ? s->clock : "", 2, COL_DIM);
}

static void draw_metric_card(uint16_t *fb, int w, int h, int x, int y,
                             const char *label, float value)
{
  char buf[12];
  uint16_t color = value < 0.0f ? COL_DIM : level_color(value);

  fill_rect(fb, w, h, x, y, 96, 78, COL_PANEL);
  fill_rect(fb, w, h, x, y, 96, 1, COL_BORDER);
  fill_rect(fb, w, h, x, y + 77, 96, 1, COL_BORDER);

  draw_text(fb, w, h, x + 8, y + 8, label, 1, COL_DIM);

  if (value < 0.0f)
    {
      snprintf(buf, sizeof(buf), "--");
      draw_text_center(fb, w, h, x + 48, y + 30, buf, 3, COL_DIM);
    }
  else
    {
      snprintf(buf, sizeof(buf), "%d%%", (int)(value + 0.5f));
      draw_text_center(fb, w, h, x + 48, y + 30, buf, 3, color);
      draw_bar(fb, w, h, x + 10, y + 62, 76, value, color);
    }
}

static void draw_overview(uint16_t *fb, int w, int h,
                          const velaops_ui_state_t *s)
{
  static const char *const labels[5] =
  {
    "可用", "资源", "服务", "网络", "稳定"
  };

  char lines[2][TEXT_LINE_MAX];
  char buf[64];
  int values[5];
  uint16_t color;
  int i;
  int count;

  values[0] = s->s_avail;
  values[1] = s->s_res;
  values[2] = s->s_svc;
  values[3] = s->s_net;
  values[4] = s->s_stab;

  color = s->health_critical ? COL_RED :
          (s->score < 70 ? COL_AMBER : COL_GREEN);

  draw_text(fb, w, h, 10, 36, "健康分", 2, COL_DIM);

  snprintf(buf, sizeof(buf), "%d", s->score);
  draw_text(fb, w, h, 10, 60, buf, 4, color);

  snprintf(buf, sizeof(buf), "%s", s->host != NULL ? s->host : "-");
  draw_text(fb, w, h, 10, 92, buf, 1, COL_DIM);

  for (i = 0; i < 5; i++)
    {
      int y = 38 + i * 22;

      draw_text(fb, w, h, 116, y, labels[i], 1, COL_DIM);
      draw_bar(fb, w, h, 160, y + 2, w - 208,
               (float)values[i], values[i] >= 80 ? COL_GREEN :
               (values[i] >= 60 ? COL_AMBER : COL_RED));
      snprintf(buf, sizeof(buf), "%d", values[i]);
      draw_text_right(fb, w, h, w - 8, y, buf, 2, COL_TEXT);
    }

  fill_rect(fb, w, h, 8, 152, w - 16, 1, COL_BORDER);

  draw_text(fb, w, h, 10, 156, "主要问题", 1, COL_DIM);

  count = wrap_text(s->alert_reason != NULL ? s->alert_reason : "-",
                    lines, 2, w - 20);
  for (i = 0; i < count; i++)
    {
      draw_text(fb, w, h, 10, 176 + i * 18, lines[i], 2,
                s->health_critical ? COL_RED : COL_TEXT);
    }
}

static void draw_sparkline(uint16_t *fb, int w, int h, int x, int y,
                           int width, int height,
                           const float *values, int count)
{
  int i;
  int last_x = -1;
  int last_y = -1;

  fill_rect(fb, w, h, x, y, width, height, COL_PANEL);
  fill_rect(fb, w, h, x, y + height / 2, width, 1, COL_BORDER);

  for (i = 0; i < count && i < width; i++)
    {
      int vx = x + i * width / (count > 1 ? count - 1 : 1);
      int vy = y + height - 1 -
               (int)(values[i] * (height - 1) / 100.0f);
      int px;
      int py;
      int y0;
      int y1;

      if (vy < y)
        {
          vy = y;
        }

      if (last_x >= 0)
        {
          y0 = last_y < vy ? last_y : vy;
          y1 = last_y < vy ? vy : last_y;

          for (py = y0; py <= y1; py++)
            {
              fill_rect(fb, w, h, vx, py, 1, 1, COL_CYAN);
            }
        }

      for (px = (last_x >= 0 ? last_x : vx); px <= vx; px++)
        {
          fill_rect(fb, w, h, px, vy, 1, 1, COL_CYAN);
        }

      last_x = vx;
      last_y = vy;
    }
}

static void draw_metric_pair(uint16_t *fb, int w, int h, int x, int y,
                             const char *label, float value)
{
  char buf[24];

  draw_text(fb, w, h, x, y, label, 2, COL_DIM);

  if (value >= 0.0f)
    {
      snprintf(buf, sizeof(buf), "%d%%", (int)(value + 0.5f));
      draw_text_right(fb, w, h, x + 140, y, buf, 2, level_color(value));
    }

  draw_bar(fb, w, h, x, y + 18, 140, value < 0.0f ? 0.0f : value,
           level_color(value < 0.0f ? 0.0f : value));
}

static void draw_performance(uint16_t *fb, int w, int h,
                             const velaops_ui_state_t *s)
{
  char buf[40];

  draw_text(fb, w, h, 10, 36, "CPU 历史", 2, COL_DIM);
  draw_sparkline(fb, w, h, 10, 54, w - 20, 56,
                 s->cpu_hist, s->cpu_hist_len);

  draw_metric_pair(fb, w, h, 10, 116, "内存", s->mem);
  draw_metric_pair(fb, w, h, 170, 116, "磁盘", s->disk);
  draw_metric_pair(fb, w, h, 10, 146, "SWAP", s->swap);
  draw_metric_pair(fb, w, h, 170, 146, "INODE", s->inode);

  snprintf(buf, sizeof(buf), "CPU %d%%  负载 %.2f  核 %d",
           (int)(s->cpu + 0.5f), (double)s->load1, s->cores);
  draw_text(fb, w, h, 10, 180, buf, 2, COL_TEXT);

  snprintf(buf, sizeof(buf), "温度 %.1fC  僵尸进程 %d  NTP", (double)s->temp,
           s->zombies);
  draw_text(fb, w, h, 10, 198, buf, 2, COL_DIM);
}

static void draw_ops(uint16_t *fb, int w, int h,
                     const velaops_ui_state_t *s)
{
  char buf[48];
  int y = 64;
  int i;

  draw_text(fb, w, h, 10, 36, "服务与单元", 2, COL_DIM);
  fill_rect(fb, w, h, 8, 56, w - 16, 1, COL_BORDER);

  if (s->svc_failed > 0)
    {
      const char *names[3] =
      {
        s->svc_failed0, s->svc_failed1, s->svc_failed2
      };

      for (i = 0; i < s->svc_failed && i < 3; i++)
        {
          if (names[i][0] == '\0')
            {
              break;
            }

          draw_text(fb, w, h, 12, y, names[i], 2, COL_TEXT);
          draw_text_right(fb, w, h, w - 12, y, "失败", 2, COL_RED);
          y += 22;
        }
    }
  else
    {
      draw_text(fb, w, h, 12, y, "所有 systemd 单元正常", 2, COL_GREEN);
      y += 26;
    }

  fill_rect(fb, w, h, 8, 150, w - 16, 1, COL_BORDER);

  snprintf(buf, sizeof(buf), "活动单元 %d  失败 %d", s->svc_active_units,
           s->svc_failed);
  draw_text(fb, w, h, 10, 162, buf, 2, COL_TEXT);

  if (s->services_total > 0)
    {
      snprintf(buf, sizeof(buf), "重点服务 %d/%d 正常", s->services_ok,
               s->services_total);
      draw_text(fb, w, h, 10, 190, buf, 2, COL_DIM);
    }
  else
    {
      draw_text(fb, w, h, 10, 190, "未配置重点服务（全量监控）", 2,
                COL_DIM);
    }
}

static void draw_text_page(uint16_t *fb, int w, int h, const char *title,
                           const char *body, uint16_t title_color)
{
  char lines[16][TEXT_LINE_MAX];
  int count;
  int i;

  draw_text(fb, w, h, 10, 36, title, 2, title_color);
  fill_rect(fb, w, h, 8, 56, w - 16, 1, COL_BORDER);

  count = wrap_text(body != NULL ? body : "-", lines, 8, w - 20);
  for (i = 0; i < count; i++)
    {
      draw_text(fb, w, h, 10, 60 + i * 17, lines[i], 2, COL_TEXT);
    }
}

static void draw_summary(uint16_t *fb, int w, int h,
                         const velaops_ui_state_t *s)
{
  char buf[64];

  draw_text_page(fb, w, h, "AI 诊断", s->summary, COL_CYAN);

  snprintf(buf, sizeof(buf), "健康分 %d  告警 %d  模型 MIMO", s->score,
           s->alerts);
  draw_text(fb, w, h, 10, 202, buf, 2, COL_DIM);
}

static void draw_rootcause(uint16_t *fb, int w, int h,
                           const velaops_ui_state_t *s)
{
  char buf[64];
  char lines[3][TEXT_LINE_MAX];
  int count;
  int i;

  draw_text(fb, w, h, 10, 36, "网络与端口", 2, COL_DIM);
  fill_rect(fb, w, h, 8, 56, w - 16, 1, COL_BORDER);

  snprintf(buf, sizeof(buf), "监听端口 %d 个", s->port_count);
  draw_text(fb, w, h, 10, 66, buf, 2, COL_TEXT);

  snprintf(buf, sizeof(buf), "新增 %d", s->port_new);
  draw_text(fb, w, h, 10, 92, buf, 2,
            s->port_new > 0 ? COL_AMBER : COL_DIM);

  snprintf(buf, sizeof(buf), "丢失 %d", s->port_lost);
  draw_text_right(fb, w, h, w - 10, 92, buf, 2,
                  s->port_lost > 0 ? COL_RED : COL_DIM);

  snprintf(buf, sizeof(buf), "ESTAB %d  TIME_WAIT %d", s->estab, s->tw);
  draw_text(fb, w, h, 10, 116, buf, 2, COL_DIM);

  snprintf(buf, sizeof(buf), "网卡错误 %lu  丢包 %lu", s->nic_err,
           s->nic_drop);
  draw_text(fb, w, h, 10, 136, buf, 2, COL_DIM);

  fill_rect(fb, w, h, 8, 156, w - 16, 1, COL_BORDER);

  if (s->top_name[0] != '\0')
    {
      snprintf(buf, sizeof(buf), "TOP 进程 %s (pid %d) %.0f%%", s->top_name,
               s->top_pid, (double)s->top_cpu);
    }
  else
    {
      snprintf(buf, sizeof(buf), "TOP 进程 -");
    }

  count = wrap_text(buf, lines, 2, w - 20);
  for (i = 0; i < count; i++)
    {
      draw_text(fb, w, h, 10, 160 + i * 18, lines[i], 2, COL_TEXT);
    }

  if (s->port_new > 0 || s->port_lost > 0)
    {
      draw_text(fb, w, h, 10, 196, "端口漂移可忽略项请在配置中维护", 1,
                COL_DIM);
    }
}

static void draw_action(uint16_t *fb, int w, int h,
                        const velaops_ui_state_t *s)
{
  char lines[4][TEXT_LINE_MAX];
  char buf[48];
  int has_cmd = s->ai_command != NULL && s->ai_command[0] != '\0';
  int waiting = s->action_state != NULL &&
                strcmp(s->action_state, "WAITING") == 0;
  int count;
  int i;

  draw_text(fb, w, h, 10, 36, "处置", 2, COL_GREEN);
  fill_rect(fb, w, h, 8, 52, w - 16, 1, COL_BORDER);

  draw_text(fb, w, h, 10, 56, "状态", 2, COL_DIM);
  draw_text(fb, w, h, 60, 56,
            waiting ? "等待批准" :
            (s->action_state != NULL ? s->action_state : "空闲"), 2,
            waiting ? COL_AMBER : COL_TEXT);

  draw_text(fb, w, h, 10, 74, "目标", 2, COL_DIM);
  draw_text(fb, w, h, 60, 74,
            s->action_service != NULL ? s->action_service : "-", 2,
            COL_TEXT);

  draw_text(fb, w, h, 10, 92, "结果", 2, COL_DIM);
  draw_text(fb, w, h, 60, 92,
            s->action_result != NULL ? s->action_result : "-", 2,
            COL_TEXT);

  fill_rect(fb, w, h, 8, 110, w - 16, 1, COL_BORDER);

  if (s->ai_reason != NULL && s->ai_reason[0] != '\0')
    {
      draw_text(fb, w, h, 10, 116, s->ai_suggested ? "AI建议" : "判定理由",
                2, s->ai_suggested ? COL_CYAN : COL_DIM);

      count = wrap_text(s->ai_reason, lines, has_cmd ? 1 : 2, w - 20);
      for (i = 0; i < count; i++)
        {
          draw_text(fb, w, h, 10, 136 + i * 18, lines[i], 2, COL_TEXT);
        }
    }

  if (has_cmd)
    {
      draw_text(fb, w, h, 10, 156, "待执行命令（长按OK执行）", 2,
                COL_AMBER);

      count = wrap_text(s->ai_command, lines,
                        (s->cmd_output != NULL &&
                         s->cmd_output[0] != '\0') ? 3 : 4,
                        (w - 20) * 2);
      for (i = 0; i < count; i++)
        {
          draw_text(fb, w, h, 10, 174 + i * 10, lines[i], 1, COL_TEXT);
        }
    }

  if (s->cmd_output != NULL && s->cmd_output[0] != '\0')
    {
      count = wrap_text(s->cmd_output, lines, 1, (w - 20) * 2);
      if (count > 0)
        {
          draw_text(fb, w, h, 10, 204, lines[0], 1, COL_DIM);
        }
    }
  else if (!has_cmd)
    {
      snprintf(buf, sizeof(buf), "健康分 %d", s->score);
      draw_text(fb, w, h, 10, 174, buf, 2, COL_DIM);
    }
}

static void draw_hwtest(uint16_t *fb, int w, int h,
                        const velaops_hw_state_t *s)
{
  static const char *const names[4] =
  {
    "UP", "DOWN", "OK", "BACK"
  };

  char line[40];
  int i;

  fill_rect(fb, w, h, 0, 0, w, h, COL_BG);
  fill_rect(fb, w, h, 0, 0, w, 30, COL_PANEL);
  fill_rect(fb, w, h, 0, 30, w, 1, COL_BORDER);
  draw_text(fb, w, h, 8, 6, "维拉哨兵", 2, COL_GREEN);
  draw_text_right(fb, w, h, w - 8, 6, "硬件自检", 2, COL_AMBER);

  snprintf(line, sizeof(line), "LED %s", s->led_on ? "亮" : "灭");
  draw_text(fb, w, h, 10, 42, line, 2, s->led_on ? COL_GREEN : COL_DIM);

  snprintf(line, sizeof(line), "蜂鸣器 %s", s->buzzer_on ? "响" : "静");
  draw_text(fb, w, h, 160, 42, line, 2, s->buzzer_on ? COL_RED : COL_DIM);

  for (i = 0; i < 4; i++)
    {
      int col = i % 2;
      int row = i / 2;
      int x = 10 + col * 155;
      int y = 74 + row * 70;
      uint16_t border = s->key_down[i] ? COL_GREEN : COL_BORDER;

      fill_rect(fb, w, h, x, y, 145, 62, COL_PANEL);
      fill_rect(fb, w, h, x, y, 145, 2, border);
      fill_rect(fb, w, h, x, y + 60, 145, 2, border);
      fill_rect(fb, w, h, x, y, 2, 62, border);
      fill_rect(fb, w, h, x + 143, y, 2, 62, border);

      draw_text(fb, w, h, x + 12, y + 8, names[i], 2,
                s->key_down[i] ? COL_GREEN : COL_TEXT);

      snprintf(line, sizeof(line), "%s %d",
               s->key_down[i] ? "按下" : "松开", s->key_count[i]);
      draw_text(fb, w, h, x + 12, y + 32, line, 2, COL_DIM);
    }

  draw_text_center(fb, w, h, w / 2, 220, "按键自检 长按OK结束", 1,
                   COL_DIM);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int velaops_ui_render(uint16_t *fb, int width, int height,
                      const velaops_ui_state_t *s, int page)
{
  if (fb == NULL || s == NULL || width < 320 || height < 240)
    {
      return -1;
    }

  fill_rect(fb, width, height, 0, 0, width, height, COL_BG);
  draw_header(fb, width, height, s, page);

  switch (page % VELAOPS_UI_PAGES)
    {
      case VELAOPS_PAGE_OVERVIEW:
        draw_overview(fb, width, height, s);
        break;
      case VELAOPS_PAGE_PERFORMANCE:
        draw_performance(fb, width, height, s);
        break;
      case VELAOPS_PAGE_OPS:
        draw_ops(fb, width, height, s);
        break;
      case VELAOPS_PAGE_SUMMARY:
        draw_summary(fb, width, height, s);
        break;
      case VELAOPS_PAGE_ROOTCAUSE:
        draw_rootcause(fb, width, height, s);
        break;
      case VELAOPS_PAGE_ACTION:
      default:
        draw_action(fb, width, height, s);
        break;
    }

  draw_footer(fb, width, height, s);
  return 0;
}

int velaops_ui_render_alert(uint16_t *fb, int width, int height,
                            const char *title, const char *text,
                            int blink_phase)
{
  char lines[8][TEXT_LINE_MAX];
  int count;
  int i;
  int box_x = 12;
  int box_y = 62;
  int box_w = width - 24;
  int box_h = 128;
  uint16_t border = blink_phase ? COL_RED : COL_PANEL;

  if (fb == NULL || title == NULL || text == NULL)
    {
      return -1;
    }

  fill_rect(fb, width, height, 0, 0, width, height, COL_BG);

  fill_rect(fb, width, height, box_x, box_y, box_w, box_h, COL_PANEL);
  fill_rect(fb, width, height, box_x, box_y, box_w, 2, border);
  fill_rect(fb, width, height, box_x, box_y + box_h - 2, box_w, 2, border);
  fill_rect(fb, width, height, box_x, box_y, 2, box_h, border);
  fill_rect(fb, width, height, box_x + box_w - 2, box_y, 2, box_h, border);

  draw_text_center(fb, width, height, width / 2, box_y + 12, title, 2,
                   COL_RED);

  count = wrap_text(text, lines, 3, width - 86);
  for (i = 0; i < count; i++)
    {
      draw_text(fb, width, height, box_x + 14, box_y + 44 + i * 18,
                lines[i], 2, COL_TEXT);
    }

  draw_text_center(fb, width, height, width / 2, box_y + box_h - 22,
                   "BACK关闭 长按OK执行", 2, COL_AMBER);
  return 0;
}

int velaops_ui_render_confirm(uint16_t *fb, int width, int height,
                              const char *headline, const char *detail,
                              int blink_phase)
{
  char lines[5][TEXT_LINE_MAX];
  int box_x = 10;
  int box_y = 34;
  int box_w;
  int box_h = 176;
  uint16_t border = blink_phase ? COL_AMBER : COL_RED;
  int count;
  int i;

  if (fb == NULL || headline == NULL || detail == NULL)
    {
      return -1;
    }

  box_w = width - 20;

  fill_rect(fb, width, height, 0, 0, width, height, COL_BG);
  fill_rect(fb, width, height, box_x, box_y, box_w, box_h, COL_PANEL);
  fill_rect(fb, width, height, box_x, box_y, box_w, 2, border);
  fill_rect(fb, width, height, box_x, box_y + box_h - 2, box_w, 2, border);
  fill_rect(fb, width, height, box_x, box_y, 2, box_h, border);
  fill_rect(fb, width, height, box_x + box_w - 2, box_y, 2, box_h, border);

  draw_text_center(fb, width, height, width / 2, box_y + 10,
                   "确认执行", 2, COL_RED);

  count = wrap_text(headline, lines, 2, (width - 44) * 2);
  for (i = 0; i < count; i++)
    {
      draw_text(fb, width, height, box_x + 12, box_y + 32 + i * 18,
                lines[i], 1, COL_TEXT);
    }

  fill_rect(fb, width, height, box_x + 10, box_y + 70, box_w - 20, 1,
            COL_BORDER);

  count = wrap_text(detail, lines, 4, (width - 44) * 2);
  for (i = 0; i < count; i++)
    {
      draw_text(fb, width, height, box_x + 12, box_y + 76 + i * 18,
                lines[i], 1, COL_CYAN);
    }

  draw_text_center(fb, width, height, width / 2, box_y + box_h - 18,
                   "长按OK确认执行  BACK取消", 1, COL_AMBER);
  return 0;
}

int velaops_ui_render_prov(uint16_t *fb, int width, int height,
                           const char *ssid, const char *pass,
                           const char *url, const char *pin)
{
  int y = 70;

  if (fb == NULL || ssid == NULL)
    {
      return -1;
    }

  fill_rect(fb, width, height, 0, 0, width, height, COL_BG);
  fill_rect(fb, width, height, 0, 0, width, 28, COL_PANEL);
  fill_rect(fb, width, height, 0, 28, width, 1, COL_BORDER);

  draw_text(fb, width, height, 8, 6, "维拉哨兵", 1, COL_GREEN);
  draw_text_right(fb, width, height, width - 8, 6, "配网", 2, COL_AMBER);

  draw_text_center(fb, width, height, width / 2, 42, "配网模式", 2,
                   COL_TEXT);

  draw_text(fb, width, height, 10, y, "热点", 2, COL_DIM);
  draw_text(fb, width, height, 70, y, ssid, 2, COL_CYAN);

  y += 26;
  draw_text(fb, width, height, 10, y, "密码", 2, COL_DIM);
  draw_text(fb, width, height, 70, y, pass, 2, COL_CYAN);

  y += 26;
  draw_text(fb, width, height, 10, y, "网址", 2, COL_DIM);
  draw_text(fb, width, height, 70, y, url, 2, COL_TEXT);

  y += 30;
  draw_text(fb, width, height, 10, y, "PIN", 2, COL_DIM);
  draw_text(fb, width, height, 70, y, pin, 3, COL_AMBER);

  fill_rect(fb, width, height, 0, height - 20, width, 1, COL_BORDER);
  draw_text_center(fb, width, height, width / 2, height - 16,
                   "保存后自动重启", 2, COL_DIM);
  return 0;
}

int velaops_ui_render_hwtest(uint16_t *fb, int width, int height,
                             const velaops_hw_state_t *s)
{
  if (fb == NULL || s == NULL || width < 320 || height < 240)
    {
      return -1;
    }

  draw_hwtest(fb, width, height, s);
  return 0;
}
