#include "velaops_dashboard.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static uint32_t pixel_hash(const uint16_t *pixels, size_t count)
{
  uint32_t hash = 2166136261U;
  size_t index;

  for (index = 0; index < count; index++)
    {
      hash ^= pixels[index];
      hash *= 16777619U;
    }

  return hash;
}

static void write_preview(const char *directory, unsigned int page,
                          const uint16_t *pixels)
{
  char path[256];
  FILE *output;
  unsigned char header[54] =
  {
    'B', 'M', 0x36, 0xa3, 0x02, 0, 0, 0, 0, 0, 54, 0, 0, 0,
    40, 0, 0, 0, 240, 0, 0, 0, 240, 0, 0, 0, 1, 0, 24, 0,
    0, 0, 0, 0, 0, 0xa3, 0x02, 0, 0x13, 0x0b, 0, 0,
    0x13, 0x0b, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
  };
  int x;
  int y;

  if (directory == NULL)
    {
      return;
    }

  snprintf(path, sizeof(path), "%s/page-%u.bmp", directory, page + 1);
  output = fopen(path, "wb");
  assert(output != NULL);
  assert(fwrite(header, sizeof(header), 1, output) == 1);

  /* BMP 从底行开始存储，像素顺序为 BGR。 */
  for (y = VELAOPS_DISPLAY_HEIGHT - 1; y >= 0; y--)
    {
      for (x = 0; x < VELAOPS_DISPLAY_WIDTH; x++)
        {
          uint16_t pixel = pixels[y * VELAOPS_DISPLAY_WIDTH + x];
          unsigned char bgr[3];

          bgr[0] = (unsigned char)((pixel & 0x1f) * 255 / 31);
          bgr[1] = (unsigned char)(((pixel >> 5) & 0x3f) * 255 / 63);
          bgr[2] = (unsigned char)(((pixel >> 11) & 0x1f) * 255 / 31);
          assert(fwrite(bgr, sizeof(bgr), 1, output) == 1);
        }
    }

  assert(fclose(output) == 0);
}

int main(void)
{
  const size_t count = VELAOPS_DISPLAY_WIDTH * VELAOPS_DISPLAY_HEIGHT;
  uint16_t *pixels = calloc(count, sizeof(*pixels));
  uint32_t hashes[VELAOPS_DISPLAY_PAGE_COUNT];
  uint32_t online_hash;
  velaops_display_state_t state = {0};
  const char *preview_directory = getenv("VELAOPS_PREVIEW_DIR");
  unsigned int page;

  assert(pixels != NULL);
  state.online = 1;
  state.health = VELAOPS_HEALTH_HEALTHY;
  state.has_resources = 1;
  state.memory.total_bytes = 16ULL * 1024 * 1024 * 1024;
  state.memory.used_bytes = 5095ULL * 1024 * 1024;
  state.memory.available_bytes = 11289ULL * 1024 * 1024;
  state.memory.used_percent = 31.1;
  state.resources.disk_percent = 42.6;
  state.resources.service_active = true;
  state.resources.port_reachable = true;
  state.resources.port_latency_ms = 18;
  state.resources.cpu.valid = 1;
  state.resources.cpu.used_percent = 23.4;
  state.resources.cpu.cores = 8;
  state.resources.cpu.load1 = 0.82;
  state.resources.cpu.load5 = 0.64;
  state.resources.cpu.load15 = 0.51;
  state.observed_at = (int64_t)time(NULL) - 5;

  for (unsigned int sample = 0;
       sample < VELAOPS_DISPLAY_CPU_HISTORY; sample++)
    {
      state.cpu_history[sample] =
          (float)(30.0 + 25.0 * ((sample * 7) % 11) / 10.0);
    }
  state.cpu_history_len = VELAOPS_DISPLAY_CPU_HISTORY;

  for (page = 0; page < VELAOPS_DISPLAY_PAGE_COUNT; page++)
    {
      assert(velaops_dashboard_render(pixels, count, &state, page) == 0);
      hashes[page] = pixel_hash(pixels, count);
      assert(hashes[page] != 0);
      write_preview(preview_directory, page, pixels);
    }

  assert(hashes[0] != hashes[1]);
  assert(hashes[1] != hashes[2]);
  assert(hashes[0] != hashes[2]);
  online_hash = hashes[0];

  assert(velaops_dashboard_render_test(pixels, count) == 0);
  assert(pixel_hash(pixels, count) != online_hash);
  assert(velaops_dashboard_render_test(NULL, count) < 0);

  state.online = 0;
  state.has_resources = 0;
  assert(velaops_dashboard_render(pixels, count, &state, 0) == 0);
  assert(pixel_hash(pixels, count) != online_hash);

  assert(velaops_dashboard_render(NULL, count, &state, 0) < 0);
  assert(velaops_dashboard_render(pixels, count - 1, &state, 0) < 0);
  assert(velaops_dashboard_render(pixels, count, NULL, 0) < 0);
  assert(velaops_dashboard_render(pixels, count, &state,
                                  VELAOPS_DISPLAY_PAGE_COUNT) < 0);

  free(pixels);
  puts("dashboard tests passed");
  return 0;
}
