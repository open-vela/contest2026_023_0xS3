/****************************************************************************
 * apps/examples/velaops/velaops_ssh.c
 *
 * Read-only evidence collection over an on-board libssh2 session.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <libssh2.h>

#include "velaops_pool.h"
#include "velaops_ssh.h"

static int g_ssh_inited;

#define COLLECT_BUF 12288
#define SSH_POOL_SIZE (256 * 1024)

int velaops_ssh_prealloc(void)
{
  return velaops_pool_init(SSH_POOL_SIZE);
}

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int tcp_connect(const char *host, const char *port)
{
  struct addrinfo hints;
  struct addrinfo *res = NULL;
  struct addrinfo *ai;
  int fd = -1;
  int ret;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family   = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  ret = getaddrinfo(host, port, &hints, &res);
  if (ret != 0 || res == NULL)
    {
      return -1;
    }

  for (ai = res; ai != NULL; ai = ai->ai_next)
    {
      fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
      if (fd < 0)
        {
          continue;
        }

      if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
        {
          break;
        }

      close(fd);
      fd = -1;
    }

  freeaddrinfo(res);
  return fd;
}

/* Connect and authenticate. The caller owns *session / *fd on success. */

static int open_authed_session(const char *host, const char *port,
                               const char *user, const char *pass,
                               LIBSSH2_SESSION **session, int *fd)
{
  int attempt;
  int rc;

  for (attempt = 0; attempt < 3; attempt++)
    {
      *fd = tcp_connect(host, port);
      if (*fd < 0)
        {
          printf("velaops: ssh connect failed\n");
          return -1;
        }

      *session = libssh2_session_init_ex(velaops_pool_alloc,
                                         velaops_pool_free,
                                         velaops_pool_realloc, NULL);
      if (*session == NULL)
        {
          printf("velaops: session init failed%c", 10);
          close(*fd);
          *fd = -1;
          return -1;
        }

      libssh2_session_set_blocking(*session, 1);
      libssh2_session_set_timeout(*session, 30000);

      libssh2_session_method_pref(*session, LIBSSH2_METHOD_KEX,
                                  "diffie-hellman-group14-sha256");
      libssh2_session_method_pref(*session, LIBSSH2_METHOD_CRYPT_CS,
                                  "aes128-ctr");
      libssh2_session_method_pref(*session, LIBSSH2_METHOD_CRYPT_SC,
                                  "aes128-ctr");
      libssh2_session_method_pref(*session, LIBSSH2_METHOD_MAC_CS,
                                  "hmac-sha2-256");
      libssh2_session_method_pref(*session, LIBSSH2_METHOD_MAC_SC,
                                  "hmac-sha2-256");

      rc = libssh2_session_handshake(*session, *fd);
      if (rc == 0)
        {
          break;
        }

      {
        char *msg = NULL;
        int mlen = 0;

        libssh2_session_last_error(*session, &msg, &mlen, 0);
        printf("velaops: ssh handshake failed: %d msg=%s try=%d\n",
               rc, msg != NULL ? msg : "?", attempt + 1);
      }

      libssh2_session_free(*session);
      *session = NULL;
      close(*fd);
      *fd = -1;

      sleep(1);
    }

  if (*session == NULL)
    {
      return -1;
    }

  if (pass != NULL && strcmp(pass, "-") == 0)
    {
      rc = libssh2_userauth_publickey_fromfile(*session, user,
                                               "/data/velaops_key.pub",
                                               "/data/velaops_key", NULL);
    }
  else
    {
      rc = libssh2_userauth_password(*session, user, pass);
    }

  if (rc != 0)
    {
      printf("velaops: ssh auth failed: %d (pass '-' means "
             "/data/velaops_key)\n", rc);
      goto err;
    }

  return 0;

err:
  libssh2_session_free(*session);
  *session = NULL;
  close(*fd);
  *fd = -1;
  return -1;
}

/* Single systemd unit name, no separators or spaces. */

static int unit_safe(const char *name)
{
  const char *p;

  if (name == NULL || *name == '\0')
    {
      return 0;
    }

  for (p = name; *p != '\0'; p++)
    {
      if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
          (*p >= '0' && *p <= '9') || *p == '.' || *p == '_' ||
          *p == '-' || *p == '@')
        {
          continue;
        }

      return 0;
    }

  return 1;
}

/* Accept only safe systemd unit / port list characters. */

static int csv_safe(const char *csv)
{
  const char *p;

  for (p = csv; *p != '\0'; p++)
    {
      if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
          (*p >= '0' && *p <= '9') || *p == '.' || *p == '_' ||
          *p == '-' || *p == '@' || *p == ',' || *p == ':')
        {
          continue;
        }

      return 0;
    }

  return 1;
}

static void build_command(const char *services_csv, char *cmd, size_t size)
{
  static const char base[] =
    "export LC_ALL=C;"
    "echo LOAD=$(cut -d' ' -f1-3 /proc/loadavg);"
    "echo MEM=$(awk '/MemTotal/{t=$2}/MemAvailable/{a=$2}"
    "END{print t\" \"a}' /proc/meminfo);"
    "echo SWAP=$(awk '/SwapTotal/{t=$2}/SwapFree/{f=$2}"
    "END{print t\" \"f}' /proc/meminfo);"
    "echo DISK=$(df -P / | awk 'NR==2{gsub(\"%\",\"\",$5);print $5}');"
    "echo INODE=$(df -iP / | awk 'NR==2{gsub(\"%\",\"\",$5);print $5}');"
    "echo CORES=$(nproc 2>/dev/null || echo 1);"
    "echo S1=$(head -1 /proc/stat);"
    "sleep 1;"
    "echo S2=$(head -1 /proc/stat);"
    "echo UPTIME=$(cut -d' ' -f1 /proc/uptime);"
    "echo BOOTID=$(cat /proc/sys/kernel/random/boot_id 2>/dev/null);"
    "echo FAILED=$(systemctl list-units --type=service --state=failed "
    "--no-legend --plain 2>/dev/null | awk '{print $1}' | head -5 "
    "| tr '\\n' ',');"
    "echo ACTIVE=$(systemctl list-units --type=service --state=active "
    "--no-legend --plain 2>/dev/null | wc -l);"
    "echo PORTS=$(ss -ltnH 2>/dev/null | awk '{print $4}' "
    "| sed 's/.*://' | sort -n | uniq | head -40 | tr '\\n' ',');"
    "echo ESTAB=$(ss -s 2>/dev/null | grep -o 'estab [0-9]*' "
    "| awk '{print $2}');"
    "echo TW=$(ss -s 2>/dev/null | grep -o 'timewait [0-9]*' "
    "| awk '{print $2}');"
    "echo NIC=$(awk '{gsub(\":\",\" \")} NR>2 && $1!=\"lo\" "
    "{e+=$4+$12;d+=$5+$13} END{print e+0 \":\" d+0}' /proc/net/dev);"
    "echo TEMP=$(cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null);"
    "echo TOP=$(ps -eo pid,pcpu,comm --sort=-pcpu --no-headers "
    "2>/dev/null | awk '$3!=\"ps\" && $3!=\"sh\" && $3!=\"awk\" "
    "&& $3!=\"sort\" && $3!=\"head\" && $3!=\"cut\" && $3!=\"sed\" "
    "&& $3!=\"grep\" && $3!=\"tr\" && $3!=\"wc\" && $3!=\"cat\" "
    "&& $3!=\"df\" && $3!=\"sleep\" {print; exit}');"
    "echo NTP=$(timedatectl show -p NTPSynchronized --value "
    "2>/dev/null);"
    "echo OOM=$(journalctl -k --since '-5 min' --no-pager 2>/dev/null "
    "| grep -ci 'out of memory');"
    "echo ZOMBIE=$(ps -eo stat --no-headers 2>/dev/null | grep -c '^Z');"
    "echo CLK=$(date +%H:%M:%S);";

  strlcpy(cmd, base, size);

  if (services_csv != NULL && services_csv[0] != '\0')
    {
      size_t used = strlen(cmd);

      snprintf(cmd + used, size - used,
               "echo WATCHSVC=$(for s in $(echo %s | tr ',' ' '); "
               "do systemctl is-active \"$s\" 2>/dev/null | head -1; "
               "done | tr '\\n' ',');",
               services_csv);
    }
}

static float parse_cpu_pair(const char *s1, const char *s2)
{
  unsigned long long v1[8] = { 0 };
  unsigned long long v2[8] = { 0 };
  unsigned long long idle1;
  unsigned long long idle2;
  unsigned long long sum1 = 0;
  unsigned long long sum2 = 0;
  const char *p;
  int i;

  p = s1;
  for (i = 0; i < 8 && p != NULL; i++)
    {
      char *end;

      p = strpbrk(p, "0123456789");
      if (p == NULL)
        {
          break;
        }

      v1[i] = strtoull(p, &end, 10);
      p = end;
    }

  p = s2;
  for (i = 0; i < 8 && p != NULL; i++)
    {
      char *end;

      p = strpbrk(p, "0123456789");
      if (p == NULL)
        {
          break;
        }

      v2[i] = strtoull(p, &end, 10);
      p = end;
    }

  idle1 = v1[3] + v1[4];
  idle2 = v2[3] + v2[4];

  for (i = 0; i < 8; i++)
    {
      sum1 += v1[i];
      sum2 += v2[i];
    }

  if (sum2 <= sum1)
    {
      return -1.0f;
    }

  return (float)((sum2 - sum1) - (idle2 - idle1)) * 100.0f /
         (float)(sum2 - sum1);
}

static void parse_port_list(const char *val, velaops_snapshot_t *out)
{
  const char *p = val;

  while (p != NULL && *p != '\0')
    {
      const char *comma = strchr(p, ',');
      size_t len = comma != NULL ? (size_t)(comma - p) : strlen(p);
      char tmp[6];

      if (len > 0 && len < sizeof(tmp) &&
          out->port_count < VELAOPS_MAX_PORTS)
        {
          int i;
          int dup = 0;

          memcpy(tmp, p, len);
          tmp[len] = '\0';

          for (i = 0; i < out->port_count; i++)
            {
              if (strcmp(out->ports[i], tmp) == 0)
                {
                  dup = 1;
                  break;
                }
            }

          if (!dup)
            {
              strcpy(out->ports[out->port_count++], tmp);
            }
        }

      p = comma != NULL ? comma + 1 : NULL;
    }
}

static void parse_failed_list(const char *val, velaops_snapshot_t *out)
{
  const char *p = val;

  while (p != NULL && *p != '\0' &&
         out->svc_failed < VELAOPS_MAX_FAILED)
    {
      const char *comma = strchr(p, ',');
      size_t len = comma != NULL ? (size_t)(comma - p) : strlen(p);

      if (len > 0 && len < 24)
        {
          memcpy(out->failed[out->svc_failed], p, len);
          out->failed[out->svc_failed][len] = '\0';
          out->svc_failed++;
        }

      p = comma != NULL ? comma + 1 : NULL;
    }
}

static void parse_watch_services(const char *val, velaops_snapshot_t *out)
{
  const char *p = val;
  int total = 0;
  int ok = 0;

  while (p != NULL && *p != '\0')
    {
      const char *comma = strchr(p, ',');
      size_t len = comma != NULL ? (size_t)(comma - p) : strlen(p);
      int active = (len >= 6 && strncmp(p, "active", 6) == 0);

      total++;
      if (active)
        {
          ok++;
        }

      if (out->watch_count < 8)
        {
          out->watch_active[out->watch_count++] = active ? 1 : 0;
        }

      p = comma != NULL ? comma + 1 : NULL;
    }

  out->watch_total = total;
  out->watch_ok = ok;
}

static void parse_watch_ports(const char *csv, velaops_snapshot_t *out)
{
  const char *p = csv;
  int total = 0;
  int ok = 0;

  while (p != NULL && *p != '\0')
    {
      const char *comma = strchr(p, ',');
      size_t len = comma != NULL ? (size_t)(comma - p) : strlen(p);
      char tmp[6];

      if (len > 0 && len < sizeof(tmp))
        {
          int i;
          int found = 0;

          memcpy(tmp, p, len);
          tmp[len] = '\0';
          total++;

          for (i = 0; i < out->port_count; i++)
            {
              if (strcmp(out->ports[i], tmp) == 0)
                {
                  found = 1;
                  break;
                }
            }

          if (found)
            {
              ok++;
            }
        }

      p = comma != NULL ? comma + 1 : NULL;
    }

  out->ports_watch_total = total;
  out->ports_watch_ok = ok;
}

static void snap_init(velaops_snapshot_t *out)
{
  memset(out, 0, sizeof(*out));
  out->cpu = -1.0f;
  out->mem = -1.0f;
  out->swap = -1.0f;
  out->disk = -1.0f;
  out->inode = -1.0f;
  out->load1 = -1.0f;
  out->temp = -1.0f;
  out->ntp = -1;
}

static void parse_kv(char *buf, const char *watch_ports_csv,
                     velaops_snapshot_t *out)
{
  char s1[160] = "";
  char s2[160] = "";
  char *p = buf;

  while (p != NULL && *p != '\0')
    {
      char *line = p;
      char *nl = strchr(p, '\n');

      if (nl != NULL)
        {
          *nl = '\0';
          p = nl + 1;
        }
      else
        {
          p += strlen(p);
        }

      if (strncmp(line, "LOAD=", 5) == 0)
        {
          sscanf(line + 5, "%f", &out->load1);
        }
      else if (strncmp(line, "MEM=", 4) == 0)
        {
          unsigned long total = 0;
          unsigned long avail = 0;

          sscanf(line + 4, "%lu %lu", &total, &avail);
          out->mem = total != 0 ?
                     (float)(total - avail) * 100.0f / (float)total :
                     -1.0f;
        }
      else if (strncmp(line, "SWAP=", 5) == 0)
        {
          unsigned long total = 0;
          unsigned long avail = 0;

          sscanf(line + 5, "%lu %lu", &total, &avail);
          out->swap = total != 0 ?
                      (float)(total - avail) * 100.0f / (float)total :
                      0.0f;
        }
      else if (strncmp(line, "DISK=", 5) == 0)
        {
          out->disk = (float)atoi(line + 5);
        }
      else if (strncmp(line, "INODE=", 6) == 0)
        {
          out->inode = (float)atoi(line + 6);
        }
      else if (strncmp(line, "CORES=", 6) == 0)
        {
          out->cores = atoi(line + 6);
        }
      else if (strncmp(line, "S1=", 3) == 0)
        {
          strlcpy(s1, line + 3, sizeof(s1));
        }
      else if (strncmp(line, "S2=", 3) == 0)
        {
          strlcpy(s2, line + 3, sizeof(s2));
        }
      else if (strncmp(line, "UPTIME=", 7) == 0)
        {
          out->uptime_sec = (unsigned long)strtoul(line + 7, NULL, 10);
        }
      else if (strncmp(line, "BOOTID=", 7) == 0)
        {
          strlcpy(out->bootid, line + 7, sizeof(out->bootid));
        }
      else if (strncmp(line, "FAILED=", 7) == 0)
        {
          parse_failed_list(line + 7, out);
        }
      else if (strncmp(line, "ACTIVE=", 7) == 0)
        {
          out->svc_active = atoi(line + 7);
        }
      else if (strncmp(line, "PORTS=", 6) == 0)
        {
          parse_port_list(line + 6, out);
        }
      else if (strncmp(line, "ESTAB=", 6) == 0)
        {
          out->estab = atoi(line + 6);
        }
      else if (strncmp(line, "TW=", 3) == 0)
        {
          out->tw = atoi(line + 3);
        }
      else if (strncmp(line, "NIC=", 4) == 0)
        {
          sscanf(line + 4, "%lu:%lu", &out->nic_err, &out->nic_drop);
        }
      else if (strncmp(line, "TEMP=", 5) == 0)
        {
          if (line[5] != '\0')
            {
              out->temp = (float)atol(line + 5) / 1000.0f;
            }
        }
      else if (strncmp(line, "TOP=", 4) == 0)
        {
          sscanf(line + 4, "%d %f %19s", &out->top_pid,
                 &out->top_cpu, out->top_name);
        }
      else if (strncmp(line, "NTP=", 4) == 0)
        {
          if (strcmp(line + 4, "yes") == 0)
            {
              out->ntp = 1;
            }
          else if (strcmp(line + 4, "no") == 0)
            {
              out->ntp = 0;
            }
        }
      else if (strncmp(line, "OOM=", 4) == 0)
        {
          out->oom = atoi(line + 4);
        }
      else if (strncmp(line, "ZOMBIE=", 7) == 0)
        {
          out->zombies = atoi(line + 7);
        }
      else if (strncmp(line, "WATCHSVC=", 9) == 0)
        {
          parse_watch_services(line + 9, out);
        }
      else if (strncmp(line, "CLK=", 4) == 0)
        {
          strlcpy(out->clock, line + 4, sizeof(out->clock));
        }
    }

  if (s1[0] != '\0' && s2[0] != '\0')
    {
      out->cpu = parse_cpu_pair(s1, s2);
    }

  if (watch_ports_csv != NULL && watch_ports_csv[0] != '\0')
    {
      parse_watch_ports(watch_ports_csv, out);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int velaops_ssh_collect(const char *host, const char *port,
                        const char *user, const char *pass,
                        const char *services_csv,
                        const char *ports_csv,
                        velaops_snapshot_t *out)
{
  LIBSSH2_SESSION *session = NULL;
  LIBSSH2_CHANNEL *channel = NULL;
  char cmd[2048];
  char *buf = NULL;
  int fd = -1;
  int rc;
  int total = 0;

  snap_init(out);

  if ((services_csv != NULL && !csv_safe(services_csv)) ||
      (ports_csv != NULL && !csv_safe(ports_csv)))
    {
      printf("velaops: invalid service/port list\n");
      return -1;
    }

  static char g_collect_buf[COLLECT_BUF];

  buf = g_collect_buf;

  if (!g_ssh_inited)
    {
      if (libssh2_init(0) != 0)
        {
          printf("velaops: libssh2_init failed%c", 10);
          return -1;
        }

      g_ssh_inited = 1;
    }

  rc = open_authed_session(host, port, user, pass, &session, &fd);
  if (rc != 0)
    {
          return -1;
    }

  build_command(services_csv, cmd, sizeof(cmd));

  channel = libssh2_channel_open_session(session);
  if (channel == NULL)
    {
      printf("velaops: channel open failed\n");
      goto out;
    }

  rc = libssh2_channel_exec(channel, cmd);
  if (rc != 0)
    {
      printf("velaops: exec failed: %d\n", rc);
      goto out;
    }

  for (;;)
    {
      ssize_t n;

      if (total >= COLLECT_BUF - 1)
        {
          break;
        }

      n = libssh2_channel_read(channel, buf + total,
                               (size_t)(COLLECT_BUF - 1 - total));
      if (n > 0)
        {
          total += (int)n;
        }
      else if (n == LIBSSH2_ERROR_EAGAIN)
        {
          continue;
        }
      else
        {
          break;
        }
    }

  buf[total] = '\0';

  out->online = 1;
  parse_kv(buf, ports_csv, out);

  printf("velaops: collect ok cpu=%.0f mem=%.0f disk=%.0f load=%.2f "
         "fail=%d ports=%d\n",
         (double)out->cpu, (double)out->mem, (double)out->disk,
         (double)out->load1, out->svc_failed, out->port_count);

out:
  if (channel != NULL)
    {
      libssh2_channel_free(channel);
    }

  if (session != NULL)
    {
      libssh2_session_disconnect(session, "bye");
      libssh2_session_free(session);
    }

  if (fd >= 0)
    {
      close(fd);
    }

  return out->online ? 0 : -1;
}

int velaops_ssh_restart(const char *host, const char *port,
                        const char *user, const char *pass,
                        const char *service)
{
  LIBSSH2_SESSION *session = NULL;
  LIBSSH2_CHANNEL *channel = NULL;
  char cmd[160];
  char buf[256];
  int fd = -1;
  int rc;
  int status = -1;

  if (!unit_safe(service))
    {
      printf("velaops: invalid service name\n");
      return -1;
    }

  if (open_authed_session(host, port, user, pass, &session, &fd) != 0)
    {
      return -1;
    }

  snprintf(cmd, sizeof(cmd),
           "systemctl restart %s || sudo -n systemctl restart %s",
           service, service);

  channel = libssh2_channel_open_session(session);
  if (channel == NULL)
    {
      printf("velaops: restart channel failed\n");
      goto out;
    }

  rc = libssh2_channel_exec(channel, cmd);
  if (rc != 0)
    {
      printf("velaops: restart exec failed: %d\n", rc);
      goto out;
    }

  for (;;)
    {
      ssize_t n = libssh2_channel_read(channel, buf, sizeof(buf));

      if (n > 0)
        {
          continue;
        }

      if (n == LIBSSH2_ERROR_EAGAIN)
        {
          continue;
        }

      break;
    }

  status = libssh2_channel_get_exit_status(channel);

out:
  if (channel != NULL)
    {
      libssh2_channel_free(channel);
    }

  libssh2_session_disconnect(session, "bye");
  libssh2_session_free(session);
  close(fd);

  printf("velaops: restart %s status=%d\n", service, status);
  return status == 0 ? 0 : -1;
}

static int exec_command(const char *host, const char *port,
                        const char *user, const char *pass,
                        const char *cmd, char *out, size_t outsize,
                        int *exit_status)
{
  LIBSSH2_SESSION *session = NULL;
  LIBSSH2_CHANNEL *channel = NULL;
  char chunk[128];
  size_t total = 0;
  int fd = -1;
  int rc;
  int status = -1;

  if (out != NULL && outsize > 0)
    {
      out[0] = '\0';
    }

  if (open_authed_session(host, port, user, pass, &session, &fd) != 0)
    {
      return -1;
    }

  channel = libssh2_channel_open_session(session);
  if (channel == NULL)
    {
      printf("velaops: exec channel failed%c", 10);
      goto out;
    }

  rc = libssh2_channel_exec(channel, cmd);
  if (rc != 0)
    {
      printf("velaops: exec start failed: %d%c", rc, 10);
      goto out;
    }

  for (;;)
    {
      ssize_t n = libssh2_channel_read(channel, chunk, sizeof(chunk));

      if (n > 0)
        {
          if (out != NULL && outsize > 1)
            {
              size_t keep = outsize - 1;

              if ((size_t)n >= keep)
                {
                  memcpy(out, chunk + (size_t)n - keep, keep);
                  total = keep;
                }
              else if (total + (size_t)n > keep)
                {
                  size_t drop = total + (size_t)n - keep;

                  memmove(out, out + drop, total - drop);
                  total -= drop;
                  memcpy(out + total, chunk, (size_t)n);
                  total += (size_t)n;
                }
              else
                {
                  memcpy(out + total, chunk, (size_t)n);
                  total += (size_t)n;
                }

              out[total] = '\0';
            }
        }
      else if (n == LIBSSH2_ERROR_EAGAIN)
        {
          continue;
        }
      else
        {
          break;
        }
    }

  status = libssh2_channel_get_exit_status(channel);

out:
  if (channel != NULL)
    {
      libssh2_channel_free(channel);
    }

  if (session != NULL)
    {
      libssh2_session_disconnect(session, "bye");
      libssh2_session_free(session);
    }

  if (fd >= 0)
    {
      close(fd);
    }

  if (exit_status != NULL)
    {
      *exit_status = status;
    }

  return status == 0 ? 0 : -1;
}

int velaops_ssh_unit(const char *host, const char *port,
                     const char *user, const char *pass,
                     const char *unit, const char *verb)
{
  char cmd[256];
  int status = -1;

  if (!unit_safe(unit) ||
      (strcmp(verb, "start") != 0 && strcmp(verb, "stop") != 0 &&
       strcmp(verb, "restart") != 0))
    {
      printf("velaops: invalid unit action\n");
      return -1;
    }

  snprintf(cmd, sizeof(cmd),
           "systemctl %s %s || sudo -n systemctl %s %s",
           verb, unit, verb, unit);

  exec_command(host, port, user, pass, cmd, NULL, 0, &status);
  printf("velaops: unit %s %s status=%d%c", verb, unit, status, 10);
  return status == 0 ? 0 : -1;
}

int velaops_ssh_command_blocked(const char *command)
{
  static const char *const bad[] =
  {
    "rm -rf /", "rm -fr /", "mkfs", ":(){", "of=/dev/",
    "systemctl reboot", "systemctl poweroff", "systemctl halt",
    " shutdown", " poweroff", " reboot", " halt"
  };
  char low[VELAOPS_CMD_LEN];
  size_t i;
  int n;

  if (command == NULL)
    {
      return 1;
    }

  n = (int)strlen(command);
  if (n <= 0 || n >= (int)sizeof(low))
    {
      return 1;
    }

  for (i = 0; i < (size_t)n; i++)
    {
      char c = command[i];

      low[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }

  low[n] = '\0';

  for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    {
      const char *hit = strstr(low, bad[i]);

      if (hit == NULL)
        {
          continue;
        }

      if ((strcmp(bad[i], "rm -rf /") == 0 ||
           strcmp(bad[i], "rm -fr /") == 0) &&
          hit[8] != '\0' && hit[8] != ' ' && hit[8] != '*')
        {
          continue;
        }

      printf("velaops: blocked command pattern '%s'%c", bad[i], 10);
      return 1;
    }

  return 0;
}

int velaops_ssh_exec(const char *host, const char *port,
                     const char *user, const char *pass,
                     const char *command, char *out, size_t outsize)
{
  char escaped[2 * VELAOPS_CMD_LEN];
  char cmd[2 * VELAOPS_CMD_LEN + 64];
  size_t used = 0;
  const char *p;
  int status = -1;

  if (command == NULL || command[0] == '\0' ||
      strlen(command) >= VELAOPS_CMD_LEN)
    {
      return -1;
    }

  if (velaops_ssh_command_blocked(command))
    {
      printf("velaops: command rejected by local policy%c", 10);
      return -2;
    }

  for (p = command; *p != '\0'; p++)
    {
      if (*p < 0x20 || *p > 0x7e)
        {
          printf("velaops: command has invalid characters\n");
          return -1;
        }

      if (*p == '\'')
        {
          memcpy(escaped + used, "'\\''", 4);
          used += 4;
        }
      else
        {
          escaped[used++] = *p;
        }
    }

  escaped[used] = '\0';

  snprintf(cmd, sizeof(cmd), "timeout 20 sh -c '%s' 2>&1", escaped);
  exec_command(host, port, user, pass, cmd, out, outsize, &status);
  printf("velaops: exec status=%d cmd=%s%c", status, command, 10);
  return status == 0 ? 0 : -1;
}

int velaops_ssh_kill(const char *host, const char *port,
                     const char *user, const char *pass,
                     const char *name)
{
  LIBSSH2_SESSION *session = NULL;
  LIBSSH2_CHANNEL *channel = NULL;
  char cmd[192];
  char buf[128];
  char clean[24];
  size_t i;
  int len = 0;
  int fd = -1;
  int rc;
  int left = -1;
  int status = -1;

  if (name == NULL || name[0] == '\0')
    {
      return -1;
    }

  for (i = 0; name[i] != '\0' && i < sizeof(clean) - 1; i++)
    {
      char c = name[i];

      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
        {
          printf("velaops: unsafe process name\n");
          return -1;
        }

      clean[i] = c;
    }

  clean[i] = '\0';

  if (open_authed_session(host, port, user, pass, &session, &fd) != 0)
    {
      return -1;
    }

  snprintf(cmd, sizeof(cmd),
           "pkill -TERM -x '%s' 2>/dev/null; sleep 2; "
           "pgrep -x '%s' 2>/dev/null | wc -l", clean, clean);

  channel = libssh2_channel_open_session(session);
  if (channel == NULL)
    {
      printf("velaops: kill channel failed\n");
      goto out;
    }

  rc = libssh2_channel_exec(channel, cmd);
  if (rc != 0)
    {
      printf("velaops: kill exec failed: %d\n", rc);
      goto out;
    }

  for (;;)
    {
      ssize_t n;

      if (len >= (int)sizeof(buf) - 1)
        {
          break;
        }

      n = libssh2_channel_read(channel, buf + len,
                               sizeof(buf) - 1 - (size_t)len);
      if (n > 0)
        {
          len += (int)n;
        }
      else if (n == LIBSSH2_ERROR_EAGAIN)
        {
          continue;
        }
      else
        {
          break;
        }
    }

  buf[len] = '\0';
  left = atoi(buf);
  status = libssh2_channel_get_exit_status(channel);

out:
  if (channel != NULL)
    {
      libssh2_channel_free(channel);
    }

  libssh2_session_disconnect(session, "bye");
  libssh2_session_free(session);
  close(fd);

  printf("velaops: kill %s left=%d status=%d\n", clean, left, status);
  return (left == 0) ? 0 : -1;
}
