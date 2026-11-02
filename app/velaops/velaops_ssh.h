/****************************************************************************
 * apps/examples/velaops/velaops_ssh.h
 *
 * Read-only server evidence collection over on-board SSH.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef VELAOPS_SSH_H
#define VELAOPS_SSH_H

#define VELAOPS_MAX_PORTS   32
#define VELAOPS_MAX_FAILED  5
#define VELAOPS_NAME_LEN    20
#define VELAOPS_CMD_LEN     200

typedef struct
{
  int online;

  float cpu;                    /* percent, -1 unknown */
  float mem;
  float swap;
  float disk;
  float inode;
  float load1;
  float temp;
  int cores;

  int zombies;
  int oom;                      /* OOM events in the last 5 minutes */

  int svc_failed;               /* systemd failed units (not ignored) */
  char failed[VELAOPS_MAX_FAILED][24];
  int svc_active;               /* active systemd units */

  int port_count;
  char ports[VELAOPS_MAX_PORTS][6];
  int estab;
  int tw;
  unsigned long nic_err;
  unsigned long nic_drop;

  int ntp;                      /* 1 synced, 0 not, -1 unknown */

  int top_pid;
  float top_cpu;
  char top_name[VELAOPS_NAME_LEN];

  unsigned long uptime_sec;
  char bootid[40];

  /* Optional "focus" checks kept from the old configuration. */

  int watch_total;
  int watch_ok;
  int watch_count;
  unsigned char watch_active[8];
  int ports_watch_total;
  int ports_watch_ok;

  char uptime[48];
  char clock[16];
} velaops_snapshot_t;

/* Reserve the libssh2 arena while the heap is still unfragmented. */

int velaops_ssh_prealloc(void);

/* Run the fixed read-only evidence command set. */

int velaops_ssh_collect(const char *host, const char *port,
                        const char *user, const char *pass,
                        const char *services_csv,
                        const char *ports_csv,
                        velaops_snapshot_t *out);

/* Restart one whitelisted service. Returns 0 when the command succeeded. */

int velaops_ssh_restart(const char *host, const char *port,
                        const char *user, const char *pass,
                        const char *service);

/* start/stop/restart one systemd unit. Returns 0 on success. */

int velaops_ssh_unit(const char *host, const char *port,
                     const char *user, const char *pass,
                     const char *unit, const char *verb);

/* Run one non-interactive shell command (already vetted by the caller).
 * Copies the tail of stdout/stderr into out. Returns 0 when exit status
 * is zero.
 */

int velaops_ssh_exec(const char *host, const char *port,
                     const char *user, const char *pass,
                     const char *command, char *out, size_t outsize);

/* 1 when the command matches the local destructive-pattern block list. */

int velaops_ssh_command_blocked(const char *command);

/* Terminate all processes with an exact name (already allow-listed).
 * Returns 0 when none are left running.
 */

int velaops_ssh_kill(const char *host, const char *port,
                     const char *user, const char *pass,
                     const char *name);

#endif /* VELAOPS_SSH_H */
