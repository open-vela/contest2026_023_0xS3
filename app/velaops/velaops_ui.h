/****************************************************************************
 * apps/examples/velaops/velaops_ui.h
 *
 * VelaOps 320x240 landscape dashboard renderer.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef VELAOPS_UI_H
#define VELAOPS_UI_H

#include <stdint.h>

#define VELAOPS_UI_PAGES 6

enum velaops_ui_page
{
  VELAOPS_PAGE_OVERVIEW = 0,
  VELAOPS_PAGE_PERFORMANCE,
  VELAOPS_PAGE_OPS,
  VELAOPS_PAGE_SUMMARY,
  VELAOPS_PAGE_ROOTCAUSE,
  VELAOPS_PAGE_ACTION,
};

typedef struct
{
  const char *host;
  int online;

  float cpu;                    /* percent, <0 = unknown */
  float mem;
  float disk;
  float load1;

  float cpu_hist[48];
  int cpu_hist_len;

  int services_total;
  int services_ok;
  int ports_total;
  int ports_ok;
  int alerts;

  int score;
  int s_avail;
  int s_res;
  int s_svc;
  int s_net;
  int s_stab;
  int health_critical;

  float swap;
  float inode;
  float temp;
  int cores;
  int zombies;

  int svc_failed;
  int svc_active_units;
  char svc_failed0[24];
  char svc_failed1[24];
  char svc_failed2[24];

  int port_count;
  int port_new;
  int port_lost;
  int estab;
  int tw;
  unsigned long nic_err;
  unsigned long nic_drop;

  int top_pid;
  char top_name[20];
  float top_cpu;

  int ai_suggested;
  const char *ai_reason;
  const char *ai_command;
  const char *cmd_output;

  const char *alert_reason;

  const char *summary;          /* ASCII, wrapped by renderer */
  const char *root_cause;
  const char *action_state;
  const char *action_service;
  const char *action_result;

  const char *clock;            /* "12:34:56" */
  const char *uptime;           /* "UP 3D 02:03" */
} velaops_ui_state_t;

int velaops_ui_render(uint16_t *fb, int width, int height,
                      const velaops_ui_state_t *state, int page);

int velaops_ui_render_alert(uint16_t *fb, int width, int height,
                            const char *title, const char *text,
                            int blink_phase);

int velaops_ui_render_confirm(uint16_t *fb, int width, int height,
                              const char *headline, const char *detail,
                              int blink_phase);

int velaops_ui_render_prov(uint16_t *fb, int width, int height,
                           const char *ssid, const char *pass,
                           const char *url, const char *pin);

typedef struct
{
  int led_on;
  int buzzer_on;
  int key_down[4];
  int key_count[4];
} velaops_hw_state_t;

int velaops_ui_render_hwtest(uint16_t *fb, int width, int height,
                             const velaops_hw_state_t *state);

#endif /* VELAOPS_UI_H */
