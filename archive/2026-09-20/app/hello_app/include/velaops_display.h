/****************************************************************************
 * VelaOps 设备端服务器状态显示。
 ****************************************************************************/

#ifndef VELAOPS_DISPLAY_H
#define VELAOPS_DISPLAY_H

#include "velaops_health.h"
#include "velaops_resource_result.h"

#define VELAOPS_DISPLAY_WIDTH 240
#define VELAOPS_DISPLAY_HEIGHT 240
#define VELAOPS_DISPLAY_PAGE_COUNT 6
#define VELAOPS_DISPLAY_CPU_HISTORY 48

typedef struct
{
  velaops_memory_observation_t memory;
  velaops_health_result_t health;
  int64_t observed_at;
  int online;
  velaops_resource_observation_t resources;
  int has_resources;
  float cpu_history[VELAOPS_DISPLAY_CPU_HISTORY];
  int cpu_history_len;
} velaops_display_state_t;

typedef struct velaops_display_s velaops_display_t;

velaops_display_t *velaops_display_open(void);
void velaops_display_close(velaops_display_t *display);
int velaops_display_show(velaops_display_t *display,
                         const velaops_display_state_t *state,
                         unsigned int page);
int velaops_display_show_test(velaops_display_t *display);
int velaops_display_show_message(velaops_display_t *display,
                                 const char *title, const char *text,
                                 int blink_phase);
int velaops_display_next_page(velaops_display_t *display,
                               const velaops_display_state_t *state,
                               unsigned int *page);

#endif /* VELAOPS_DISPLAY_H */
