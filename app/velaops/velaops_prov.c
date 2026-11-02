/****************************************************************************
 * apps/examples/velaops/velaops_prov.c
 *
 * SoftAP + DHCP + tiny HTTP setup page. Saves /data/velaops.conf and reboots.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/boardctl.h>

#include <nuttx/board.h>
#include <nuttx/clock.h>
#include <nuttx/lcd/lcd_dev.h>

#include <netutils/dhcpd.h>
#include <netutils/netlib.h>
#include <nuttx/wireless/wireless.h>
#include <pthread.h>

#include "velaops_prov.h"
#include "velaops_conf.h"
#include "velaops_ui.h"
#include "velaops_llm.h"

#define PROV_SSID   "VelaOps-Setup"
#define PROV_PASS   "velaops12345"
#define PROV_URL    "http://192.168.4.1"
#define PROV_REQMAX 8192

/****************************************************************************
 * Private Data
 ****************************************************************************/

static char g_pin[8];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void url_decode(char *dst, const char *src, size_t size)
{
  size_t o = 0;

  while (*src != '\0' && o + 1 < size)
    {
      if (*src == '+')
        {
          dst[o++] = ' ';
          src++;
        }
      else if (*src == '%' && src[1] != '\0' && src[2] != '\0')
        {
          char hex[3] = { src[1], src[2], 0 };

          dst[o++] = (char)strtol(hex, NULL, 16);
          src += 3;
        }
      else
        {
          dst[o++] = *src++;
        }
    }

  dst[o] = '\0';
}

/* Extract "key=value" from an urlencoded body into out. */

static int form_get(const char *body, const char *key, char *out, size_t size)
{
  const char *p = body;
  size_t klen = strlen(key);

  out[0] = '\0';

  while (p != NULL && *p != '\0')
    {
      const char *amp = strchr(p, '&');
      const char *eq = strchr(p, '=');

      if (eq != NULL && (amp == NULL || eq < amp) &&
          (size_t)(eq - p) == klen && strncmp(p, key, klen) == 0)
        {
          char raw[256];
          size_t len = amp != NULL ? (size_t)(amp - eq - 1) :
                       strlen(eq + 1);

          if (len >= sizeof(raw))
            {
              len = sizeof(raw) - 1;
            }

          memcpy(raw, eq + 1, len);
          raw[len] = '\0';
          url_decode(out, raw, size);
          return 1;
        }

      p = amp != NULL ? amp + 1 : NULL;
    }

  return 0;
}

static void make_pin(void)
{
  unsigned int value;

  srand((unsigned int)clock_systime_ticks());
  value = 1000 + (unsigned int)(rand() % 9000);
  snprintf(g_pin, sizeof(g_pin), "%u", value);
}

static int lcd_draw_prov(void)
{
  struct fb_videoinfo_s vi;
  struct lcddev_area_s area;
  uint16_t *fb;
  int fd;
  int ret;

  fd = open("/dev/lcd0", O_RDWR);
  if (fd < 0)
    {
      return -1;
    }

  memset(&vi, 0, sizeof(vi));
  if (ioctl(fd, LCDDEVIO_GETVIDEOINFO, (unsigned long)(uintptr_t)&vi) < 0)
    {
      close(fd);
      return -1;
    }

  ioctl(fd, LCDDEVIO_SETPOWER, 1);

  fb = calloc((size_t)vi.xres * vi.yres, sizeof(uint16_t));
  if (fb == NULL)
    {
      close(fd);
      return -1;
    }

  velaops_ui_render_prov(fb, vi.xres, vi.yres, PROV_SSID, PROV_PASS,
                         PROV_URL, g_pin);

  memset(&area, 0, sizeof(area));
  area.row_end = vi.yres - 1;
  area.col_end = vi.xres - 1;
  area.stride = vi.xres * sizeof(uint16_t);
  area.data = (uint8_t *)fb;

  ret = ioctl(fd, LCDDEVIO_PUTAREA, (unsigned long)(uintptr_t)&area);

  free(fb);
  close(fd);
  return ret;
}

/****************************************************************************
 * Minimal DHCP server (192.168.4.1/24, single lease 192.168.4.2)
 ****************************************************************************/

#define DHCP_CLIENT_IP  0xc0a80402
#define DHCP_SERVER_IP  0xc0a80401
#define DHCP_MAGIC0     0x63
#define DHCP_MAGIC1     0x82
#define DHCP_MAGIC2     0x53
#define DHCP_MAGIC3     0x63

static void dhcp_reply(int sock, const uint8_t *req, int reqlen, uint8_t type)
{
  uint8_t out[320];
  uint8_t *o;
  uint32_t value;
  struct sockaddr_in dst;
  int sret;

  (void)reqlen;

  memset(out, 0, sizeof(out));
  out[0] = 2;                       /* BOOTREPLY */
  out[1] = 1;                       /* Ethernet */
  out[2] = 6;                       /* MAC length */
  memcpy(&out[4], &req[4], 4);      /* xid */
  memcpy(&out[10], &req[10], 2);    /* flags */

  value = htonl(DHCP_CLIENT_IP);
  memcpy(&out[16], &value, 4);      /* yiaddr */
  value = htonl(DHCP_SERVER_IP);
  memcpy(&out[20], &value, 4);      /* siaddr */
  memcpy(&out[28], &req[28], 16);   /* chaddr */

  out[236] = DHCP_MAGIC0;
  out[237] = DHCP_MAGIC1;
  out[238] = DHCP_MAGIC2;
  out[239] = DHCP_MAGIC3;

  o = &out[240];
  *o++ = 53; *o++ = 1; *o++ = type;                /* message type */
  *o++ = 54; *o++ = 4;
  value = htonl(DHCP_SERVER_IP); memcpy(o, &value, 4); o += 4;
  *o++ = 51; *o++ = 4;
  value = htonl(3600); memcpy(o, &value, 4); o += 4;
  *o++ = 1; *o++ = 4;
  value = htonl(0xffffff00); memcpy(o, &value, 4); o += 4;  /* netmask */
  *o++ = 3; *o++ = 4;
  value = htonl(DHCP_SERVER_IP); memcpy(o, &value, 4); o += 4; /* router */
  *o++ = 6; *o++ = 4;
  value = htonl(DHCP_SERVER_IP); memcpy(o, &value, 4); o += 4; /* dns */
  *o++ = 255;

  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_port = htons(68);
  dst.sin_addr.s_addr = htonl(INADDR_BROADCAST);

  sret = sendto(sock, out, (size_t)(o - out), 0,
                (struct sockaddr *)&dst, sizeof(dst));

  if (sret < 0)
    {
      printf("velaops: dhcp sendto failed: %d\n", errno);
    }
  else
    {
      printf("velaops: dhcp offer sent (%d bytes)\n", sret);
    }
}

static int dhcp_open(void)
{
  struct sockaddr_in addr;
  int sock;
  int one = 1;

  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
    {
      printf("velaops: dhcp socket failed: %d\n", errno);
      return -1;
    }

  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
  setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, "wlan1", 5);

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(67);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
      printf("velaops: dhcp bind failed: %d\n", errno);
      close(sock);
      return -1;
    }

  printf("velaops: dhcp server on udp/67\n");
  return sock;
}

static void dhcp_poll(int sock)
{
  struct sockaddr_in from;
  socklen_t flen = sizeof(from);
  uint8_t buf[600];
  ssize_t n;
  uint8_t type = 0;
  uint8_t *o;
  uint32_t xid = 0;

  n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &flen);
  if (n < 240 || buf[0] != 1)
    {
      return;
    }

  memcpy(&xid, &buf[4], 4);
  o = &buf[240];

  while (o < buf + n && *o != 255)
    {
      if (*o == 0)
        {
          o++;
          continue;
        }

      if (o[0] == 53 && o[1] >= 1)
        {
          type = o[2];
        }

      o += 2 + o[1];
    }

  printf("velaops: dhcp rx type=%u xid=%08lx\n",
         (unsigned)type, (unsigned long)ntohl(xid));

  if (type == 1)
    {
      dhcp_reply(sock, buf, (int)n, 2);   /* DISCOVER -> OFFER */
    }
  else if (type == 3)
    {
      dhcp_reply(sock, buf, (int)n, 5);   /* REQUEST  -> ACK   */
    }
}

static bool g_ap_started;
static volatile int g_rescan_pending;

static void ap_apply_ssid(void);

static int ap_start(void)
{
  struct iwreq iwr;
  struct in_addr addr;
  struct in_addr mask;
  int fd;
  int ret;

  printf("velaops: starting SoftAP %s\n", PROV_SSID);

  fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0)
    {
      printf("velaops: ap socket failed: %d\n", errno);
      return -1;
    }

  netlib_ifdown("wlan1");

  memset(&iwr, 0, sizeof(iwr));
  strncpy(iwr.ifr_name, "wlan1", IFNAMSIZ - 1);
  iwr.u.mode = IW_MODE_MASTER;
  ret = ioctl(fd, SIOCSIWMODE, (unsigned long)(uintptr_t)&iwr);
  printf("velaops: ap mode ret=%d\n", ret);

  ret = netlib_ifup("wlan1");
  printf("velaops: ap ifup ret=%d\n", ret);

  /* The setup AP is open on purpose: the page itself is PIN protected and
   * the AP only lives during provisioning. WPA2 on the ESP32-S3 SoftAP was
   * unstable with this NuttX port. */

  ap_apply_ssid();
  g_ap_started = true;

  memset(&addr, 0, sizeof(addr));
  inet_pton(AF_INET, "192.168.4.1", &addr);
  memset(&mask, 0, sizeof(mask));
  inet_pton(AF_INET, "255.255.255.0", &mask);

  netlib_set_ipv4addr("wlan1", &addr);
  netlib_set_ipv4netmask("wlan1", &mask);

  close(fd);
  return 0;
}

static int save_config(const char *body)
{
  char fields[8][160];
  static const char *keys[8] =
  {
    "wifi_ssid", "wifi_pass", "llm_url", "llm_model",
    "llm_key", "ssh_host", "ssh_user", "ssh_pass"
  };
  static const char *names[8] =
  {
    "wifi_ssid", "wifi_pass", "llm_url", "llm_model",
    "llm_key", "ssh_host", "ssh_user", "ssh_pass"
  };
  char services[160];
  char ports[160];
  char repair[160];
  char thr_cpu[8];
  char thr_mem[8];
  char thr_disk[8];
  char alert_score[8];
  char recover_score[8];
  char llm_ip[32];
  char dns[32];
  char ignore_ports[64];
  char ignore_units[96];
  char kill_allow[64];
  char port[16];
  char static_ip[32];
  char static_gw[32];
  char static_mask[32];
  char file[1024];
  int i;
  int len;

  for (i = 0; i < 8; i++)
    {
      form_get(body, keys[i], fields[i], sizeof(fields[i]));
    }

  form_get(body, "services", services, sizeof(services));
  form_get(body, "ports", ports, sizeof(ports));
  form_get(body, "repair", repair, sizeof(repair));
  form_get(body, "thr_cpu", thr_cpu, sizeof(thr_cpu));
  form_get(body, "thr_mem", thr_mem, sizeof(thr_mem));
  form_get(body, "thr_disk", thr_disk, sizeof(thr_disk));
  form_get(body, "alert_score", alert_score, sizeof(alert_score));
  form_get(body, "recover_score", recover_score, sizeof(recover_score));
  form_get(body, "ignore_ports", ignore_ports, sizeof(ignore_ports));
  form_get(body, "ignore_units", ignore_units, sizeof(ignore_units));
  form_get(body, "kill_allow", kill_allow, sizeof(kill_allow));
  form_get(body, "llm_ip", llm_ip, sizeof(llm_ip));
  form_get(body, "dns", dns, sizeof(dns));
  form_get(body, "ssh_port", port, sizeof(port));
  form_get(body, "static_ip", static_ip, sizeof(static_ip));
  form_get(body, "static_gw", static_gw, sizeof(static_gw));
  form_get(body, "static_mask", static_mask, sizeof(static_mask));

  printf("velaops: cfg ssid='%s' host='%s' port='%s' modelsz=%d\n",
         fields[0], fields[5], port, (int)strlen(fields[3]));

  if (fields[0][0] == '\0' || fields[5][0] == '\0' || port[0] == '\0')
    {
      printf("velaops: config missing ssid='%s' host='%s' port='%s'\n",
             fields[0], fields[5], port);
      return -1;
    }

  len = snprintf(file, sizeof(file),
                 "wifi_ssid=%s\n"
                 "wifi_pass=%s\n"
                 "llm_url=%s\n"
                 "llm_model=%s\n"
                 "llm_key=%s\n"
                 "ssh_host=%s\n"
                 "ssh_port=%s\n"
                 "ssh_user=%s\n"
                 "ssh_pass=%s\n"
                 "services=%s\n"
                 "ports=%s\n"
                 "repair=%s\n"
                 "thr_cpu=%s\n"
                 "thr_mem=%s\n"
                 "thr_disk=%s\n"
                 "alert_score=%s\n"
                 "recover_score=%s\n"
                 "ignore_ports=%s\n"
                 "ignore_units=%s\n"
                 "kill_allow=%s\n"
                 "llm_ip=%s\n"
                 "dns=%s\n"
                 "static_ip=%s\n"
                 "static_gw=%s\n"
                 "static_mask=%s\n",
                 fields[0], fields[1], fields[2], fields[3],
                 fields[4], fields[5], port, fields[6], fields[7],
                 services, ports, repair,
                 thr_cpu, thr_mem, thr_disk, alert_score, recover_score,
                 ignore_ports, ignore_units, kill_allow, llm_ip, dns,
                 static_ip, static_gw, static_mask);

  if (len <= 0 || len >= (int)sizeof(file))
    {
      printf("velaops: config too long len=%d\n", len);
      return -1;
    }

  if (velaops_conf_save(file) != 0)
    {
      printf("velaops: config store failed%c", 10);
      return -1;
    }

  printf("velaops: config saved (%d bytes)\n", len);
  return 0;
}

static const char *page_head =
  "<!doctype html><html><head><meta charset=utf-8>"
  "<meta name=viewport content='width=device-width,initial-scale=1'>"
  "<title>VelaOps 配网</title><style>"
  "body{background:#0b0f14;color:#e8eef5;font-family:system-ui,sans-serif;"
  "margin:0;padding:18px} h1{font-size:20px;margin:0 0 4px}"
  ".sub{color:#8a97a6;font-size:13px;margin-bottom:16px}"
  "fieldset{border:1px solid #24303d;border-radius:10px;margin:0 0 14px;"
  "padding:12px} legend{color:#6ee7a8;font-size:13px;padding:0 6px}"
  "label{display:block;font-size:12px;color:#9fb0c0;margin:10px 0 4px}"
  "input{width:100%;box-sizing:border-box;background:#111820;color:#e8eef5;"
  "border:1px solid #2b3947;border-radius:8px;padding:10px;font-size:14px}"
  "button{width:100%;padding:14px;border:0;border-radius:10px;"
  "background:#16a34a;color:#fff;font-size:16px;font-weight:600;margin-top:6px}"
  ".pin{color:#facc15}</style></head><body>"
  "<h1>VelaOps 配网</h1>"
  "<div class=sub>设备热点已就绪，填写后保存，设备将自动重启进入看板。</div>"
  "<form method=post action=/save>";

static void send_all(int fd, const char *data, size_t len)
{
  int retries = 0;

  while (len > 0)
    {
      size_t chunk = len > 512 ? 512 : len;
      ssize_t n = send(fd, data, chunk, 0);

      if (n <= 0)
        {
          if (retries++ > 200)
            {
              printf("velaops: send stalled errno=%d left=%u\n", errno,
                     (unsigned)len);
              break;
            }

          usleep(20000);
          continue;
        }

      retries = 0;
      data += n;
      len -= (size_t)n;
    }
}

static void ap_apply_ssid(void)
{
  struct iwreq iwr;
  int fd;

  fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0)
    {
      return;
    }

  memset(&iwr, 0, sizeof(iwr));
  strncpy(iwr.ifr_name, "wlan1", IFNAMSIZ - 1);
  iwr.u.essid.pointer = (FAR void *)PROV_SSID;
  iwr.u.essid.length = strlen(PROV_SSID);
  iwr.u.essid.flags = IW_ESSID_ON;

  printf("velaops: ap ssid ret=%d\n",
         ioctl(fd, SIOCSIWESSID, (unsigned long)(uintptr_t)&iwr));
  close(fd);
}

#define SCAN_MAX 12
#define SCAN_FILE "/data/scan.txt"

static char g_scan[SCAN_MAX][34];
static pthread_mutex_t g_scan_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_scan_count;

static void scan_escape(const char *in, char *out, size_t size)
{
  size_t o = 0;

  while (*in != '\0' && o + 8 < size)
    {
      if (*in == '<' || *in == '>' || *in == '&' || *in == '\'' ||
          *in == '"')
        {
          o += (size_t)snprintf(out + o, size - o, "&#%d;", (int)*in);
          in++;
        }
      else
        {
          out[o++] = *in++;
        }
    }

  out[o] = '\0';
}

static int scan_has(const char *ssid)
{
  int i;

  for (i = 0; i < g_scan_count; i++)
    {
      if (strcmp(g_scan[i], ssid) == 0)
        {
          return 1;
        }
    }

  return 0;
}

static void scan_aps(int run);

static volatile int g_scan_done;

static void *scan_thread(void *arg)
{
  (void)arg;
  scan_aps(1);
  g_scan_done = 1;
  return NULL;
}

static volatile int g_scan_phase_done;

static void *scan_watchdog(void *arg)
{
  int i;

  (void)arg;

  for (i = 0; i < 75; i++)
    {
      if (g_scan_phase_done)
        {
          return NULL;
        }

      usleep(200000);
    }

  printf("velaops: scan watchdog reset%c", 10);
  boardctl(BOARDIOC_RESET, 0);
  return NULL;
}

int velaops_scan_to_file(void)
{
  pthread_t tid;
  pthread_attr_t attr;

  printf("velaops: scan phase%c", 10);

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 4096);

  if (pthread_create(&tid, &attr, scan_watchdog, NULL) == 0)
    {
      pthread_detach(tid);
    }

  netlib_ifup("wlan0");
  system("wapi scan wlan0 > " SCAN_FILE " 2>&1");
  netlib_ifdown("wlan0");

  g_scan_phase_done = 1;
  return 0;
}

static void scan_aps(int run)
{
  FILE *fp;
  char line[192];
  int header = 1;

  g_scan_count = 0;

  if (run)
    {
      printf("velaops: scanning wifi%c", 10);
      system("wapi scan wlan0 > " SCAN_FILE " 2>&1");
    }

  fp = fopen(SCAN_FILE, "r");
  if (fp == NULL)
    {
      return;
    }

  pthread_mutex_lock(&g_scan_lock);
  g_scan_count = 0;

  while (fgets(line, sizeof(line), fp) != NULL && g_scan_count < SCAN_MAX)
    {
      char *p = line;
      char *ssid;
      size_t len;
      int tabs = 0;

      if (header)
        {
          header = 0;
          continue;
        }

      while (*p != '\0' && tabs < 4)
        {
          if (*p == '\t')
            {
              tabs++;
            }

          p++;
        }

      if (tabs < 4)
        {
          continue;
        }

      ssid = p;
      len = strcspn(ssid, "\r\n");

      if (len == 0 || len > 33)
        {
          continue;
        }

      ssid[len] = '\0';

      if (scan_has(ssid))
        {
          continue;
        }

      strcpy(g_scan[g_scan_count], ssid);
      g_scan_count++;
    }

  fclose(fp);
  pthread_mutex_unlock(&g_scan_lock);
  printf("velaops: scan found %d aps\n", g_scan_count);
}

static void serve_page(int fd)
{
  static char extra[6144];
  char resp[256];
  char esc[160];
  char v_ssid[64];
  char v_wpass[80];
  char v_url[128];
  char v_model[64];
  char v_key[96];
  char v_ip[32];
  char v_dns[32];
  char v_host[64];
  char v_port[8];
  char v_user[32];
  char v_pass[96];
  char v_svcs[96];
  char v_ports[64];
  char v_repair[96];
  char v_thr_cpu[8];
  char v_thr_mem[8];
  char v_thr_disk[8];
  char v_alert[8];
  char v_recover[8];
  char v_ign_ports[64];
  char v_ign_units[96];
  char v_kill[64];
  char v_sip[32];
  char v_sgw[32];
  char v_smask[32];
  int used;
  int i;

  /* Prefill from the last saved configuration; fall back to sensible
   * defaults so the form can be submitted without retyping. */

  v_ssid[0] = 0;
  v_wpass[0] = 0;
  v_url[0] = 0;
  v_model[0] = 0;
  v_key[0] = 0;
  v_ip[0] = 0;
  v_dns[0] = 0;
  v_host[0] = 0;
  v_port[0] = 0;
  v_user[0] = 0;
  v_pass[0] = 0;
  v_svcs[0] = 0;
  v_ports[0] = 0;
  v_repair[0] = 0;
  v_thr_cpu[0] = 0;
  v_thr_mem[0] = 0;
  v_thr_disk[0] = 0;
  v_alert[0] = 0;
  v_recover[0] = 0;
  v_ign_ports[0] = 0;
  v_ign_units[0] = 0;
  v_kill[0] = 0;
  v_sip[0] = 0;
  v_sgw[0] = 0;
  v_smask[0] = 0;

  velaops_conf_get("wifi_ssid", v_ssid, sizeof(v_ssid));
  velaops_conf_get("wifi_pass", v_wpass, sizeof(v_wpass));
  velaops_conf_get("llm_url", v_url, sizeof(v_url));
  velaops_conf_get("llm_model", v_model, sizeof(v_model));
  velaops_conf_get("llm_key", v_key, sizeof(v_key));
  velaops_conf_get("llm_ip", v_ip, sizeof(v_ip));
  velaops_conf_get("dns", v_dns, sizeof(v_dns));
  velaops_conf_get("ssh_host", v_host, sizeof(v_host));
  velaops_conf_get("ssh_port", v_port, sizeof(v_port));
  velaops_conf_get("ssh_user", v_user, sizeof(v_user));
  velaops_conf_get("ssh_pass", v_pass, sizeof(v_pass));
  velaops_conf_get("services", v_svcs, sizeof(v_svcs));
  velaops_conf_get("ports", v_ports, sizeof(v_ports));
  velaops_conf_get("repair", v_repair, sizeof(v_repair));
  velaops_conf_get("thr_cpu", v_thr_cpu, sizeof(v_thr_cpu));
  velaops_conf_get("thr_mem", v_thr_mem, sizeof(v_thr_mem));
  velaops_conf_get("thr_disk", v_thr_disk, sizeof(v_thr_disk));
  velaops_conf_get("alert_score", v_alert, sizeof(v_alert));
  velaops_conf_get("recover_score", v_recover, sizeof(v_recover));
  velaops_conf_get("ignore_ports", v_ign_ports, sizeof(v_ign_ports));
  velaops_conf_get("ignore_units", v_ign_units, sizeof(v_ign_units));
  velaops_conf_get("kill_allow", v_kill, sizeof(v_kill));
  velaops_conf_get("static_ip", v_sip, sizeof(v_sip));
  velaops_conf_get("static_gw", v_sgw, sizeof(v_sgw));
  velaops_conf_get("static_mask", v_smask, sizeof(v_smask));

  if (v_url[0] == 0)
    {
      strcpy(v_url, "https://token-plan-cn.xiaomimimo.com/v1");
    }

  if (v_model[0] == 0)
    {
      strcpy(v_model, "mimo-v2.6-flash");
    }

  if (v_host[0] == 0)
    {
      strcpy(v_host, "183.66.27.51");
    }

  if (v_port[0] == 0)
    {
      strcpy(v_port, "22");
    }

  if (v_user[0] == 0)
    {
      strcpy(v_user, "root");
    }

  if (v_svcs[0] == 0)
    {
      strcpy(v_svcs, "nginx,php-fpm-81,mysqld,ssh");
    }

  if (v_ports[0] == 0)
    {
      strcpy(v_ports, "22,80,3306,3000");
    }

  if (v_repair[0] == 0)
    {
      strcpy(v_repair, "nginx,php-fpm-81");
    }

  if (v_thr_cpu[0] == 0)
    {
      strcpy(v_thr_cpu, "85");
    }

  if (v_thr_mem[0] == 0)
    {
      strcpy(v_thr_mem, "85");
    }

  if (v_thr_disk[0] == 0)
    {
      strcpy(v_thr_disk, "85");
    }

  if (v_alert[0] == 0)
    {
      strcpy(v_alert, "70");
    }

  if (v_recover[0] == 0)
    {
      strcpy(v_recover, "80");
    }

  if (v_ign_ports[0] == 0)
    {
      strcpy(v_ign_ports, "111");
    }

  if (v_ip[0] == 0)
    {
      strcpy(v_ip, "202.69.4.23");
    }

  if (v_dns[0] == 0)
    {
      strcpy(v_dns, "223.5.5.5");
    }

  if (v_kill[0] == 0)
    {
      strcpy(v_kill, "cpu-burner");
    }

  if (v_sgw[0] == 0)
    {
      strcpy(v_sgw, "192.168.1.1");
    }

  if (v_smask[0] == 0)
    {
      strcpy(v_smask, "255.255.255.0");
    }

  if (v_sip[0] == 0)
    {
      strcpy(v_sip, "192.168.1.250");
    }

  used = snprintf(extra, sizeof(extra),
           "<fieldset><legend>设备 PIN</legend>"
           "<div class=sub>请输入屏幕上的 4 位 PIN</div>"
           "<div class=sub>开机时按住 OK 或 BOOT 可进入配网</div>"
           "<input name=pin required inputmode=numeric maxlength=4></fieldset>"
           "<fieldset><legend>WiFi</legend>"
           "<label>附近网络</label>"
           "<select onchange=\"if(this.value)"
           "document.getElementById('ssid').value=this.value\">"
           "<option value=''>-- 手动输入 --</option>");

  for (i = 0; i < g_scan_count && used > 0 && used < (int)sizeof(extra);
       i++)
    {
      scan_escape(g_scan[i], esc, sizeof(esc));
      used += snprintf(extra + used, sizeof(extra) - (size_t)used,
                       "<option value=\"%s\">%s</option>", esc, esc);
    }

  scan_escape(v_ssid, esc, sizeof(esc));
  used += snprintf(extra + used, sizeof(extra) - (size_t)used,
           "</select><label>SSID</label>"
           "<input id=ssid name=wifi_ssid required value=\"%s\">", esc);

  scan_escape(v_wpass, esc, sizeof(esc));
  used += snprintf(extra + used, sizeof(extra) - (size_t)used,
           "<label>密码</label>"
           "<input name=wifi_pass type=password value=\"%s\">", esc);

  scan_escape(v_dns, esc, sizeof(esc));
  used += snprintf(extra + used, sizeof(extra) - (size_t)used,
           "<label>DNS 服务器</label><input name=dns value=\"%s\">"
           "</fieldset>", esc);

  scan_escape(v_url, esc, sizeof(esc));
  used += snprintf(extra + used, sizeof(extra) - (size_t)used,
           "<fieldset><legend>大模型 (MiMo / OpenAI 兼容)</legend>"
           "<label>Base URL</label><input name=llm_url value=\"%s\">", esc);

  scan_escape(v_model, esc, sizeof(esc));
  used += snprintf(extra + used, sizeof(extra) - (size_t)used,
           "<label>模型</label><input name=llm_model value=\"%s\">", esc);

  scan_escape(v_key, esc, sizeof(esc));
  used += snprintf(extra + used, sizeof(extra) - (size_t)used,
           "<label>API Key</label>"
           "<input name=llm_key type=password value=\"%s\">", esc);

  scan_escape(v_ip, esc, sizeof(esc));
  used += snprintf(extra + used, sizeof(extra) - (size_t)used,
           "<label>模型 IP（可选，绕过 DNS）</label>"
           "<input name=llm_ip value=\"%s\"></fieldset>", esc);

  scan_escape(v_host, esc, sizeof(esc));
  used += snprintf(extra + used, sizeof(extra) - (size_t)used,
           "<fieldset><legend>监控服务器 (SSH)</legend>"
           "<label>主机</label><input name=ssh_host required value=\"%s\">", esc);

  scan_escape(v_port, esc, sizeof(esc));
  used += snprintf(extra + used, sizeof(extra) - (size_t)used,
           "<label>端口</label><input name=ssh_port value=\"%s\">", esc);

  scan_escape(v_user, esc, sizeof(esc));
  used += snprintf(extra + used, sizeof(extra) - (size_t)used,
           "<label>用户</label><input name=ssh_user value=\"%s\">", esc);

  scan_escape(v_pass, esc, sizeof(esc));
  used += snprintf(extra + used, sizeof(extra) - (size_t)used,
           "<label>密码</label>"
           "<input name=ssh_pass type=password value=\"%s\"></fieldset>", esc);

  scan_escape(v_svcs, esc, sizeof(esc));
  used += snprintf(extra + used, sizeof(extra) - (size_t)used,
           "<fieldset><legend>整体监控与告警</legend>"
           "<div class=sub>默认监控整台服务器：systemd 失败单元、监听端口、"
           "资源与稳定性。以下字段均为可选细化。</div>"
           "<div class=sub>AI 可建议 kill/重启/执行任意命令；"
           "屏幕会显示完整命令，长按 OK 才执行。</div>"
           "<label>重点服务（逗号分隔）</label>"
           "<input name=services value=\"%s\">", esc);

  scan_escape(v_ports, esc, sizeof(esc));
  used += snprintf(extra + used, sizeof(extra) - (size_t)used,
           "<label>重点端口（逗号分隔）</label><input name=ports value=\"%s\">", esc);

  scan_escape(v_repair, esc, sizeof(esc));
  used += snprintf(extra + used, sizeof(extra) - (size_t)used,
           "<label>允许重启的服务白名单</label><input name=repair value=\"%s\">", esc);

  scan_escape(v_thr_cpu, esc, sizeof(esc));
  used += snprintf(extra + used, sizeof(extra) - (size_t)used,
           "<label>CPU 告警阈值 (%%)</label><input name=thr_cpu value=\"%s\">", esc);

  scan_escape(v_thr_mem, esc, sizeof(esc));
  used += snprintf(extra + used, sizeof(extra) - (size_t)used,
           "<label>内存告警阈值 (%%)</label><input name=thr_mem value=\"%s\">", esc);

  scan_escape(v_thr_disk, esc, sizeof(esc));
  used += snprintf(extra + used, sizeof(extra) - (size_t)used,
           "<label>磁盘告警阈值 (%%)</label><input name=thr_disk value=\"%s\">", esc);

  scan_escape(v_alert, esc, sizeof(esc));
  used += snprintf(extra + used, sizeof(extra) - (size_t)used,
           "<label>告警健康分</label><input name=alert_score value=\"%s\">", esc);

  scan_escape(v_recover, esc, sizeof(esc));
  used += snprintf(extra + used, sizeof(extra) - (size_t)used,
           "<label>恢复健康分</label><input name=recover_score value=\"%s\">", esc);

  scan_escape(v_ign_ports, esc, sizeof(esc));
  used += snprintf(extra + used, sizeof(extra) - (size_t)used,
           "<label>忽略端口（逗号分隔）</label><input name=ignore_ports value=\"%s\">", esc);

  scan_escape(v_ign_units, esc, sizeof(esc));
  used += snprintf(extra + used, sizeof(extra) - (size_t)used,
           "<label>忽略单元（逗号分隔）</label><input name=ignore_units value=\"%s\">", esc);

  scan_escape(v_kill, esc, sizeof(esc));
  used += snprintf(extra + used, sizeof(extra) - (size_t)used,
           "<label>允许终止的进程名</label><input name=kill_allow value=\"%s\">"
           "</fieldset>"
           "<fieldset><legend>静态 IP（可选，DHCP 失败时使用）</legend>"
           "<label>IP</label><input name=static_ip value=\"%s\">"
           "<label>网关</label><input name=static_gw value=\"%s\">"
           "<label>掩码</label><input name=static_mask value=\"%s\">"
           "</fieldset>"
           "<button type=submit>保存并重启</button></form>"
           "<div class=sub style=\"margin-top:10px\">"
           "<a href=/reset-config style=\"color:#facc15\">"
           "恢复推荐默认值（清空已保存配置）</a></div>", esc,
           v_sip, v_sgw, v_smask);

  if (used < 0 || used >= (int)sizeof(extra))
    {
      printf("velaops: page form truncated used=%d%c", used, 10);
    }

  snprintf(resp, sizeof(resp),
           "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
           "Connection: close\r\nContent-Length: %u\r\n\r\n",
           (unsigned)(strlen(page_head) + strlen(extra) +
                      strlen("</body></html>")));

  send_all(fd, resp, strlen(resp));
  send_all(fd, page_head, strlen(page_head));
  send_all(fd, extra, strlen(extra));
  send_all(fd, "</body></html>", 14);
}

static void send_status(int fd, int code, const char *msg)
{
  char body[256];
  char head[160];
  int len;

  len = snprintf(body, sizeof(body), "<html><body><h3>%s</h3></body></html>",
                 msg);

  snprintf(head, sizeof(head),
           "HTTP/1.1 %d X\r\nContent-Type: text/html; charset=utf-8\r\n"
           "Connection: close\r\nContent-Length: %d\r\n\r\n", code, len);

  send_all(fd, head, strlen(head));
  send_all(fd, body, (size_t)len);
}

static void handle_client(int fd, char *buf)
{
  struct timeval tv;
  ssize_t total = 0;
  ssize_t n;
  char *hdr;
  char *body;
  char *lenptr;
  int content_len = 0;

  /* Never let a silent or half-open client block the setup page forever. */

  tv.tv_sec = 2;
  tv.tv_usec = 0;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  while (total < PROV_REQMAX - 1)
    {
      n = recv(fd, buf + total, PROV_REQMAX - 1 - total, 0);
      if (n <= 0)
        {
          break;
        }

      total += n;
      buf[total] = '\0';

      tv.tv_sec = 8;
      setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

      if (strstr(buf, "\r\n\r\n") != NULL)
        {
          break;
        }
    }

  printf("velaops: request %d bytes\n", (int)total);

  if (total <= 0)
    {
      return;
    }

  buf[total] = '\0';
  hdr = buf;
  body = strstr(buf, "\r\n\r\n");

  if (body == NULL)
    {
      send_status(fd, 400, "bad request");
      return;
    }

  *body = '\0';
  body += 4;

  if (strncmp(hdr, "GET / ", 6) == 0)
    {
      serve_page(fd);
      return;
    }

  if (strncmp(hdr, "GET /diag", 9) == 0)
    {
      char qurl[200];
      char qkey[200];
      char qprompt[160];
      char text[512];
      char *qs;
      int rc;

      qurl[0] = 0;
      qkey[0] = 0;
      snprintf(qprompt, sizeof(qprompt), "%s", "ping");

      qs = strchr(hdr, '?');
      if (qs != NULL)
        {
          char *sp;

          qs++;
          sp = strchr(qs, ' ');
          if (sp != NULL)
            {
              *sp = 0;
            }

          form_get(qs, "u", qurl, sizeof(qurl));
          form_get(qs, "k", qkey, sizeof(qkey));
          form_get(qs, "q", qprompt, sizeof(qprompt));
        }

      if (qurl[0] == 0)
        {
          velaops_conf_get("llm_url", qurl, sizeof(qurl));
        }

      if (qkey[0] == 0)
        {
          velaops_conf_get("llm_key", qkey, sizeof(qkey));
        }

      rc = velaops_llm_chat(qurl, "mimo-v2.6-flash", qkey, qprompt,
                            text, sizeof(text));
      printf("velaops: diag rc=%d text=%s%c", rc, text, 10);
      send_status(fd, 200, text[0] != 0 ? text : "diag failed");
      return;
    }

  if (strncmp(hdr, "GET /reset-config", 17) == 0)
    {
      velaops_conf_clear();
      printf("velaops: config cleared by user%c", 10);
      serve_page(fd);
      return;
    }

  if (strncmp(hdr, "GET /__disabled", 15) == 0)
    {
      static const char body[] =
        "<!doctype html><html><head><meta charset=utf-8>"
        "<meta http-equiv=\"refresh\" content=\"14\">"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<style>body{background:#0b0f14;color:#e8eef5;font-family:system-ui,"
        "sans-serif;padding:24px}h1{font-size:18px}</style></head><body>"
        "<h1>正在重新扫描附近网络…</h1><div style='color:#8a97a6;font-size:14px'>"
        "设备将重启并重新扫描，约 15 秒后请重新连接热点 VelaOps-Setup 再刷新本页。"
        "</div></body></html>";
      char head[160];
      int flag;

      snprintf(head, sizeof(head),
               "HTTP/1.1 200 OK%c%c"
               "Content-Type: text/html; charset=utf-8%c%c"
               "Connection: close%c%c"
               "Content-Length: %u%c%c%c%c",
               13, 10, 13, 10, 13, 10,
               (unsigned)strlen(body), 13, 10, 13, 10);

      send_all(fd, head, strlen(head));
      send_all(fd, body, strlen(body));

      flag = open("/data/.setup", O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (flag >= 0)
        {
          close(flag);
        }

      flag = open("/data/.rescan", O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (flag >= 0)
        {
          close(flag);
        }

      g_rescan_pending = 1;
      return;
    }

  if (strncmp(hdr, "POST /save", 10) != 0)
    {
      send_status(fd, 404, "not found");
      return;
    }

  lenptr = strstr(hdr, "Content-Length:");
  if (lenptr != NULL)
    {
      content_len = atoi(lenptr + 15);
    }

  if (content_len > 0 && (body - buf) + content_len > total)
    {
      while (total < PROV_REQMAX - 1 &&
             (body - buf) + content_len > total)
        {
          n = recv(fd, buf + total, PROV_REQMAX - 1 - total, 0);
          if (n <= 0)
            {
              break;
            }

          total += n;
          buf[total] = '\0';
        }
    }

  {
    char pin[16];

    form_get(body, "pin", pin, sizeof(pin));
    if (strcmp(pin, g_pin) != 0)
      {
        send_status(fd, 403, "PIN 错误");
        return;
      }
  }

  if (save_config(body) == 0)
    {
      send_status(fd, 200, "已保存，设备即将重启…");
      sleep(2);
      boardctl(BOARDIOC_RESET, 0);
    }
  else
    {
      send_status(fd, 500, "保存失败，请检查必填项");
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int velaops_config_present(void)
{
  return velaops_conf_present();
}

int velaops_prov_run(void)
{
  struct sockaddr_in addr;
  int ls;
  int dsock;
  int one = 1;
  time_t started;
  time_t last_tick = 0;

  make_pin();
  lcd_draw_prov();

  scan_aps(0);

  if (ap_start() != 0)
    {
      printf("velaops: AP start failed%c", 10);
    }

  dsock = dhcp_open();

  ls = socket(AF_INET, SOCK_STREAM, 0);
  if (ls < 0)
    {
      printf("velaops: socket failed: %d\n", errno);
      return -1;
    }

  setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(80);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(ls, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
      printf("velaops: bind 80 failed: %d\n", errno);
      close(ls);
      return -1;
    }

  listen(ls, 8);
  printf("velaops: setup page on %s (pin %s), listening on port 80\n",
         PROV_URL, g_pin);

  started = time(NULL);

  while (1)
    {
      fd_set rfds;
      struct timeval tv;
      int maxfd = ls;
      int ret;

      FD_ZERO(&rfds);
      FD_SET(ls, &rfds);

      if (dsock >= 0)
        {
          FD_SET(dsock, &rfds);

          if (dsock > maxfd)
            {
              maxfd = dsock;
            }
        }

      tv.tv_sec = 2;
      tv.tv_usec = 0;

      ret = select(maxfd + 1, &rfds, NULL, NULL, &tv);

      if (ret > 0 && dsock >= 0 && FD_ISSET(dsock, &rfds))
        {
          dhcp_poll(dsock);
        }

      if (ret > 0 && FD_ISSET(ls, &rfds))
        {
          int cfd = accept(ls, NULL, NULL);

          if (cfd >= 0)
            {
              char *buf = malloc(PROV_REQMAX);

              printf("velaops: client connected\n");

              if (buf != NULL)
                {
                  handle_client(cfd, buf);
                  free(buf);
                }

              /* Flush the queued response before the socket goes away. */

              shutdown(cfd, SHUT_WR);
              usleep(300000);
              close(cfd);

              if (g_rescan_pending)
                {
                  g_rescan_pending = 0;
                  sleep(1);
                  boardctl(BOARDIOC_RESET, 0);
                }
            }
          else
            {
              printf("velaops: accept failed: %d\n", errno);
            }
        }

      {
        time_t now = time(NULL);

        if (now - started >= last_tick + 60)
          {
            last_tick = now - started;
            printf("velaops: setup alive %d s%c", (int)last_tick, 10);
          }
      }
    }

  close(ls);

  if (dsock >= 0)
    {
      close(dsock);
    }

  return 0;
}
