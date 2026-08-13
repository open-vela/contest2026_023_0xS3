/****************************************************************************
 * VelaOps Sentinel - Wi-Fi 自动连接管理
 ****************************************************************************/

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <poll.h>

#include <netutils/netlib.h>
#include <wireless/wapi.h>

#include "velaops.h"

/* Wi-Fi 状态 */
enum velaops_wifi_state
{
  WIFI_STATE_DISCONNECTED = 0,
  WIFI_STATE_CONNECTING,
  WIFI_STATE_CONNECTED,
  WIFI_STATE_ERROR
};

/* Wi-Fi 管理上下文 */
struct velaops_wifi
{
  enum velaops_wifi_state state;
  char ssid[33];
  char password[65];
  bool auto_connect;
  int retry_count;
  int max_retries;
  char probe_ip[16];   /* TCP 探测目标 IP（SSH 服务器地址） */
  int probe_port;      /* TCP 探测目标端口 */
  int probe_failures;  /* TCP 探测连续失败计数 */
};

static struct velaops_wifi g_wifi = {
  .state = WIFI_STATE_DISCONNECTED,
  .auto_connect = true,
  .retry_count = 0,
  .max_retries = 5
};

#define WIFI_INTERFACE "wlan0"

/* 检查网络接口状态 */
static bool is_interface_up(const char *interface)
{
  int fd;
  struct ifreq ifr;
  bool up = false;

  fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0)
    {
      return false;
    }

  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, interface, IFNAMSIZ - 1);

  if (ioctl(fd, SIOCGIFFLAGS, &ifr) == 0)
    {
      up = (ifr.ifr_flags & IFF_UP) != 0;
    }

  close(fd);
  return up;
}

/* 获取 IP 地址 */
static bool get_ip_address(const char *interface, char *ip, size_t ip_size)
{
  int fd;
  struct ifreq ifr;
  bool has_ip = false;

  fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0)
    {
      return false;
    }

  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, interface, IFNAMSIZ - 1);

  if (ioctl(fd, SIOCGIFADDR, &ifr) == 0)
    {
      struct sockaddr_in *addr = (struct sockaddr_in *)&ifr.ifr_addr;
      inet_ntop(AF_INET, &addr->sin_addr, ip, ip_size);
      /* The board configuration initially assigns 10.0.0.2 before an AP is
       * joined.  It is a placeholder, not proof of connectivity. */
      has_ip = addr->sin_addr.s_addr != INADDR_ANY &&
               strcmp(ip, "10.0.0.2") != 0;
    }

  close(fd);
  return has_ip;
}

/* 轻量级连通性探测：尝试 TCP connect 到目标地址。
 * 如果接口标志正常但驱动已失活，connect 会快速失败（errno 101/115）。 */

static bool tcp_probe_reachable(const char *ip, int port)
{
  int fd;
  struct sockaddr_in addr;
  struct pollfd pfd;
  int soerr;
  socklen_t slen = sizeof(soerr);
  bool reachable = false;

  fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0)
    {
      return false;
    }

  /* 非阻塞 connect + poll，2 秒超时 */

  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  inet_pton(AF_INET, ip, &addr.sin_addr);

  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0)
    {
      reachable = true;  /* 立即连接成功 */
    }
  else if (errno == EINPROGRESS)
    {
      pfd.fd = fd;
      pfd.events = POLLOUT;
      if (poll(&pfd, 1, 2000) > 0)
        {
          getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &slen);
          reachable = (soerr == 0);
        }
    }

  close(fd);
  return reachable;
}

/* 初始化 Wi-Fi 模块 */
int velaops_wifi_init(void)
{
  memset(&g_wifi, 0, sizeof(g_wifi));
  g_wifi.state = WIFI_STATE_DISCONNECTED;
  g_wifi.auto_connect = true;
  g_wifi.max_retries = 5;

  /* 如果接口已经是 UP 且持有有效 IP（例如启动脚本/NSH 预先配好 Wi-Fi），
   * 把内部状态标记为 CONNECTED。否则 velaops_wifi_monitor() 会误判为
   * 断线并触发 reconnect，从而把正在工作的 SSH 连接撕掉。 */

  {
    char probe_ip[16];

    if (is_interface_up(WIFI_INTERFACE) &&
        get_ip_address(WIFI_INTERFACE, probe_ip, sizeof(probe_ip)))
      {
        g_wifi.state = WIFI_STATE_CONNECTED;
        g_wifi.retry_count = 0;
        printf("WiFi: Module initialized (interface already up, %s)\n",
               probe_ip);
        return 0;
      }
  }

  printf("WiFi: Module initialized\n");
  return 0;
}

/* 配置 Wi-Fi */
int velaops_wifi_configure(const char *ssid, const char *password)
{
  if (ssid == NULL || password == NULL)
    {
      return -1;
    }

  strncpy(g_wifi.ssid, ssid, sizeof(g_wifi.ssid) - 1);
  g_wifi.ssid[sizeof(g_wifi.ssid) - 1] = '\0';
  strncpy(g_wifi.password, password, sizeof(g_wifi.password) - 1);
  g_wifi.password[sizeof(g_wifi.password) - 1] = '\0';

  printf("WiFi: Configured for SSID: %s\n", g_wifi.ssid);
  return 0;
}

/* 设置 TCP 连通性探测目标（SSH 服务器地址） */
void velaops_wifi_set_probe_target(const char *ip, int port)
{
  if (ip != NULL)
    {
      strncpy(g_wifi.probe_ip, ip, sizeof(g_wifi.probe_ip) - 1);
      g_wifi.probe_ip[sizeof(g_wifi.probe_ip) - 1] = '\0';
      g_wifi.probe_port = port > 0 ? port : 22;
      printf("WiFi: Probe target set to %s:%d\n",
             g_wifi.probe_ip, g_wifi.probe_port);
    }
}

/* Connect using the same WPA2/CCMP sequence as:
 *   wapi psk wlan0 <password> 3
 *   wapi essid wlan0 <ssid> 1
 * followed by DHCP. */
int velaops_wifi_connect(void)
{
  int sock;
  int ret;

  if (g_wifi.ssid[0] == '\0')
    {
      printf("WiFi: No SSID configured\n");
      return -1;
    }

  printf("WiFi: Connecting to %s...\n", g_wifi.ssid);
  g_wifi.state = WIFI_STATE_CONNECTING;

  ret = netlib_ifup(WIFI_INTERFACE);
  if (ret < 0)
    {
      printf("WiFi: ifup failed: %d\n", ret);
      goto failed;
    }

  sock = wapi_make_socket();
  if (sock < 0)
    {
      printf("WiFi: WAPI socket failed: %d\n", sock);
      goto failed;
    }

  ret = wpa_driver_wext_set_auth_param(sock, WIFI_INTERFACE,
                                       IW_AUTH_WPA_VERSION,
                                       IW_AUTH_WPA_VERSION_WPA2);
  if (ret >= 0)
    ret = wpa_driver_wext_set_auth_param(sock, WIFI_INTERFACE,
                                         IW_AUTH_CIPHER_PAIRWISE,
                                         IW_AUTH_CIPHER_CCMP);
  if (ret >= 0)
    ret = wpa_driver_wext_set_key_ext(sock, WIFI_INTERFACE, WPA_ALG_CCMP,
                                      g_wifi.password,
                                      strlen(g_wifi.password));
  if (ret >= 0)
    ret = wapi_set_essid(sock, WIFI_INTERFACE, g_wifi.ssid, WAPI_ESSID_ON);
  close(sock);

  if (ret < 0)
    {
      printf("WiFi: WPA2 configuration failed: %d\n", ret);
      goto failed;
    }

  ret = netlib_obtain_ipv4addr(WIFI_INTERFACE);
  if (ret < 0)
    {
      printf("WiFi: DHCP failed: %d\n", ret);
      goto failed;
    }

  char ip[16];
  if (get_ip_address(WIFI_INTERFACE, ip, sizeof(ip)))
    {
      printf("WiFi: Connected with IP: %s\n", ip);

      /* 禁用 Wi-Fi 省电模式（modem sleep）。
       * ESP32-S3 的省电模式在密集 TCP 操作后会导致驱动进入
       * 不可恢复的状态，必须禁用以保证 SSH 长连接稳定。 */

      sock = wapi_make_socket();
      if (sock >= 0)
        {
          int ps_ret = wapi_set_power_save(sock, WIFI_INTERFACE, false);
          if (ps_ret == 0)
            {
              printf("WiFi: Power save disabled\n");
            }
          else
            {
              printf("WiFi: Failed to disable power save: %d\n", ps_ret);
            }

          close(sock);
        }

      g_wifi.state = WIFI_STATE_CONNECTED;
      g_wifi.retry_count = 0;

      return 0;
    }
  else
    {
      printf("WiFi: Failed to get IP address\n");
      g_wifi.state = WIFI_STATE_ERROR;
      return -1;
    }

failed:
  g_wifi.state = WIFI_STATE_ERROR;
  return -1;
}

static int wifi_read_line(const char *label, char *buffer, size_t size,
                          bool hidden)
{
  struct termios oldt;
  struct termios newt;
  bool changed = false;

  printf("%s", label);
  fflush(stdout);
  if (tcgetattr(STDIN_FILENO, &oldt) == 0)
    {
      newt = oldt;
      newt.c_lflag |= ICANON;
      if (hidden) newt.c_lflag &= ~ECHO;
      else newt.c_lflag |= ECHO;
      changed = tcsetattr(STDIN_FILENO, TCSANOW, &newt) == 0;
    }

  if (fgets(buffer, size, stdin) == NULL)
    {
      if (changed) tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
      return -1;
    }

  if (changed)
    {
      tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
      if (hidden) putchar('\n');
    }

  buffer[strcspn(buffer, "\r\n")] = '\0';
  return buffer[0] == '\0' ? -1 : 0;
}

int velaops_wifi_prompt_and_connect(void)
{
  char ssid[33];
  char password[65];
  int ret;

  puts("\nVelaOps Wi-Fi first-run configuration (WPA2-PSK)");
  if (wifi_read_line("Wi-Fi SSID: ", ssid, sizeof(ssid), false) < 0 ||
      wifi_read_line("Wi-Fi password: ", password, sizeof(password), true) < 0)
    return -1;

  if (strlen(password) < 8 || velaops_wifi_configure(ssid, password) < 0)
    {
      velaops_secure_zero(password, sizeof(password));
      puts("WiFi: WPA2 password must contain at least 8 characters.");
      return -1;
    }

  ret = velaops_wifi_connect();
  if (ret == 0)
    ret = velaops_storage_save_wifi_config(ssid, password);
  velaops_secure_zero(password, sizeof(password));
  return ret;
}

/* 断开 Wi-Fi：真正重置网络接口，使后续 ifup 能重置驱动状态。
 * 注意：netlib_ifdown 可能会清除 Wi-Fi 驱动内部的 SSID/密码，
 * 所以在操作前后保存并恢复 g_wifi 中的凭据。 */
int velaops_wifi_disconnect(void)
{
  int sock;
  char saved_ssid[33];
  char saved_password[65];

  printf("WiFi: Disconnecting...\n");

  /* 保存凭据 */

  memcpy(saved_ssid, g_wifi.ssid, sizeof(saved_ssid));
  memcpy(saved_password, g_wifi.password, sizeof(saved_password));

  /* 断开 WPA 关联 */

  sock = wapi_make_socket();
  if (sock >= 0)
    {
      wpa_driver_wext_disconnect(sock, WIFI_INTERFACE);
      close(sock);
    }

  /* 关闭网络接口 */

  netlib_ifdown(WIFI_INTERFACE);

  /* 恢复凭据（netlib_ifdown 可能清除了驱动中的 SSID） */

  memcpy(g_wifi.ssid, saved_ssid, sizeof(g_wifi.ssid));
  memcpy(g_wifi.password, saved_password, sizeof(g_wifi.password));

  g_wifi.state = WIFI_STATE_DISCONNECTED;
  return 0;
}

/* 重新连接：完整断开 → 等待驱动恢复 → 重新连接 */
int velaops_wifi_reconnect(void)
{
  printf("WiFi: Reconnecting...\n");

  /* 如果不知道 SSID/密码（例如 Wi-Fi 由启动脚本或 NSH 预先配好），
   * 千万不要执行断开。ifdown 会把正在工作的网络接口连同 SSH 连接
   * 一起撕掉，而这里没有凭据把它恢复回来。 */

  if (g_wifi.ssid[0] == '\0')
    {
      printf("WiFi: No saved credentials; skipping destructive reconnect.\n");
      g_wifi.state = WIFI_STATE_CONNECTED;
      return -1;
    }

  /* 完整断开：WPA disconnect + ifdown */

  velaops_wifi_disconnect();

  /* 等待驱动完成内部清理 */

  sleep(2);

  /* 重新连接：ifup + WPA + DHCP + 禁用省电 */

  return velaops_wifi_connect();
}

/* 强制重置网络接口：当普通重连失败时，执行更彻底的恢复 */
static int wifi_force_reset(void)
{
  printf("WiFi: Force resetting interface...\n");

  velaops_wifi_disconnect();
  sleep(5);  /* 更长的等待，让驱动完全重置 */

  return velaops_wifi_connect();
}

/* 检查连接状态 */
bool velaops_wifi_is_connected(void)
{
  char ip[16];
  return is_interface_up(WIFI_INTERFACE) &&
         get_ip_address(WIFI_INTERFACE, ip, sizeof(ip));
}

/* 获取 IP 地址 */
int velaops_wifi_get_ip(char *ip, size_t ip_size)
{
  if (get_ip_address(WIFI_INTERFACE, ip, ip_size))
    {
      return 0;
    }
  return -1;
}

/* 自动连接（从持久存储加载配置） */
int velaops_wifi_auto_connect(void)
{
  char ssid[33];
  char password[65];

  printf("WiFi: Attempting auto-connect...\n");

  /* 尝试从存储加载配置 */
  if (velaops_storage_load_wifi_config(ssid, sizeof(ssid),
                                       password, sizeof(password)) == 0)
    {
      printf("WiFi: Found saved config for SSID: %s\n", ssid);
      velaops_wifi_configure(ssid, password);
      return velaops_wifi_connect();
    }
  else
    {
      printf("WiFi: No saved config found\n");
      return -1;
    }
}

/* 监控连接状态，断线自动重连。
 * 使用 TCP 探测检测驱动失活（接口 UP 但实际不工作的情况）。 */
void velaops_wifi_monitor(void)
{
  if (!g_wifi.auto_connect)
    {
      return;
    }

  if (g_wifi.state == WIFI_STATE_CONNECTED)
    {
      /* 第一层：检查接口标志和 IP */

      if (!velaops_wifi_is_connected())
        {
          printf("WiFi: Connection lost (no IP)\n");
          g_wifi.state = WIFI_STATE_DISCONNECTED;
          goto try_reconnect;
        }

      /* 第二层：如果配置了探测目标，用 TCP 探测验证实际连通性。
       * 接口可能显示 UP/RUNNING 且有 IP，但 ESP32-S3 Wi-Fi 驱动
       * 已经失活（errno 101 ENETUNREACH），只有 TCP connect 能检测到。
       * 需要连续 3 次失败才触发重连，避免瞬时网络抖动导致误判。 */

      if (g_wifi.probe_ip[0] != '\0')
        {
          if (!tcp_probe_reachable(g_wifi.probe_ip, g_wifi.probe_port))
            {
              g_wifi.probe_failures++;
              printf("WiFi: TCP probe failed (%d/3)\n",
                     g_wifi.probe_failures);
              if (g_wifi.probe_failures >= 3)
                {
                  printf("WiFi: Driver unresponsive after 3 probe failures\n");
                  g_wifi.probe_failures = 0;
                  g_wifi.state = WIFI_STATE_DISCONNECTED;
                  goto try_reconnect;
                }
              return;  /* 还没到阈值，下次再试 */
            }
          else
            {
              g_wifi.probe_failures = 0;  /* 探测成功，重置计数 */
            }
        }

      return;  /* 一切正常 */
    }

  if (g_wifi.state == WIFI_STATE_DISCONNECTED ||
      g_wifi.state == WIFI_STATE_ERROR)
    {
      goto try_reconnect;
    }

  return;

try_reconnect:
  if (g_wifi.retry_count < g_wifi.max_retries)
    {
      int delay;
      g_wifi.retry_count++;

      /* 指数退避：1s, 2s, 4s, 8s, 16s */

      delay = 1 << (g_wifi.retry_count - 1);
      if (delay > 16)
        {
          delay = 16;
        }

      printf("WiFi: Reconnecting (attempt %d/%d, delay %ds)...\n",
             g_wifi.retry_count, g_wifi.max_retries, delay);
      sleep(delay);

      if (velaops_wifi_reconnect() == 0)
        {
          printf("WiFi: Reconnected successfully\n");
        }
    }
  else
    {
      /* 达到最大重试次数，执行强制重置 */

      printf("WiFi: Max retries reached, attempting force reset...\n");
      g_wifi.retry_count = 0;

      if (wifi_force_reset() == 0)
        {
          printf("WiFi: Force reset successful\n");
        }
      else
        {
          printf("WiFi: Force reset failed, will retry next cycle\n");
          g_wifi.state = WIFI_STATE_ERROR;
          g_wifi.max_retries = 8;  /* 后续轮次给更多机会 */
        }
    }
}
