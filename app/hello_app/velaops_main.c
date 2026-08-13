#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include "velaops.h"

static int run_dashboard_demo(void)
{
  struct velaops_profile profile;
  struct velaops_metrics metrics;
  unsigned int tick = 0;

  memset(&profile, 0, sizeof(profile));
  memset(&metrics, 0, sizeof(metrics));
  snprintf(profile.host, sizeof(profile.host), "DEMO-SERVER");
  snprintf(profile.user, sizeof(profile.user), "VELAOPS");
  profile.port = 22;
  metrics.valid = true;
  metrics.cpu_percent = 38;
  metrics.memory_percent = 62;
  metrics.memory_total_kb = 8 * 1024 * 1024;
  metrics.memory_available_kb = 3 * 1024 * 1024;
  metrics.disk_percent = 47;
  metrics.load_1min = 0.8f;
  metrics.load_5min = 0.6f;
  metrics.load_15min = 0.4f;
  metrics.process_count = 136;
  metrics.uptime_seconds = 7260;
  metrics.net_rx_bytes = 42 * 1024 * 1024;
  metrics.net_tx_bytes = 18 * 1024 * 1024;

  if (velaops_dashboard_open() < 0)
    {
      puts("Dashboard demo: /dev/fb0 unavailable.");
      return 1;
    }

  velaops_state_init();
  velaops_input_init();
  velaops_led_init();
  puts("Dashboard demo running. BOOT short press changes page; Ctrl-C stops.");
  for (;;)
    {
      const char *status = tick % 20 >= 15 ? "ALERT CPU HIGH" : "MONITORING OK";
      metrics.cpu_percent = 30 + tick % 61;
      velaops_dashboard_draw(&profile, &metrics, status);
      velaops_led_update();
      usleep(250000);
      tick++;
    }
}

static int confirm_fingerprint(const char *fingerprint)
{
  char answer[8];
  struct termios oldt;
  struct termios newt;

  printf("Server public-key fingerprint:\n  %s\n", fingerprint);
  printf("Trust this server? Type yes: ");
  fflush(stdout);

  /* 确保 stdin 在规范模式 */
  if (tcgetattr(0, &oldt) == 0)
    {
      newt = oldt;
      newt.c_lflag |= ICANON | ECHO;
      tcsetattr(0, TCSANOW, &newt);
    }

  if (fgets(answer, sizeof(answer), stdin) == NULL)
    {
      tcsetattr(0, TCSANOW, &oldt);
      return -1;
    }

  tcsetattr(0, TCSANOW, &oldt);
  answer[strcspn(answer, "\r\n")] = '\0';
  return strcmp(answer, "yes") == 0 ? 0 : -1;
}

static int read_pin(char *pin, unsigned int size)
{
  struct termios oldt;
  struct termios newt;
  bool changed = false;

  printf("Vault PIN: ");
  fflush(stdout);

  if (tcgetattr(0, &oldt) == 0)
    {
      newt = oldt;
      newt.c_lflag |= ICANON;  /* 启用规范模式 */
      newt.c_lflag &= ~ECHO;   /* 禁用回显 */
      changed = tcsetattr(0, TCSANOW, &newt) == 0;
    }

  if (fgets(pin, size, stdin) == NULL)
    {
      if (changed) tcsetattr(0, TCSANOW, &oldt);
      return -1;
    }

  if (changed)
    {
      tcsetattr(0, TCSANOW, &oldt);
      putchar('\n');
    }

  pin[strcspn(pin, "\r\n")] = '\0';
  return pin[0] == '\0' ? -1 : 0;
}

int main(int argc, char **argv)
{
  struct velaops_profile profile;
  struct velaops_metrics metrics;
  struct velaops_ssh *ssh = NULL;
  char pin[64] = {0};
  char fingerprint[VELAOPS_FINGERPRINT_MAX];
  bool loaded = false;
  bool cli_mode = false;  /* true when credentials come from argv */
  int dashboard = 0;  /* 默认启用 LCD 看板 */
  int rc = 1;

  memset(&profile, 0, sizeof(profile));
  memset(&metrics, 0, sizeof(metrics));

  puts("VelaOps Sentinel - SSH monitor MVP");

  if (argc == 2 && strcmp(argv[1], "--demo") == 0)
    return run_dashboard_demo();

  velaops_state_init();
  velaops_input_init();
  velaops_led_init();
  dashboard = velaops_dashboard_open();
  if (dashboard == 0)
    velaops_dashboard_draw(&profile, &metrics, "STARTING");

  /* 初始化存储系统 */
  if (velaops_storage_init() < 0)
    {
      puts("Storage initialization failed.");
      goto out;
    }
  if (dashboard == 0)
    velaops_dashboard_draw(&profile, &metrics, "STORAGE OK");

  /* 初始化 Wi-Fi 模块 */
  if (velaops_wifi_init() < 0)
    {
      puts("Wi-Fi initialization failed.");
      goto out;
    }
  if (dashboard == 0)
    velaops_dashboard_draw(&profile, &metrics, "NETWORK CHECK");

  /* 尝试自动连接 Wi-Fi */
  if (!velaops_wifi_is_connected())
    {
      puts("Wi-Fi not connected. Attempting auto-connect...");
      if (velaops_wifi_auto_connect() < 0)
        {
          puts("Wi-Fi auto-connect unavailable; starting first-run setup.");
          if (velaops_wifi_prompt_and_connect() < 0)
            {
              puts("Wi-Fi setup failed. Check SSID/password and retry.");
              goto out;
            }
        }
      else
        {
          puts("Wi-Fi connected successfully.");
        }
    }
  else
    {
      puts("Wi-Fi already connected.");
    }

  /* 获取 vault 路径 */
  const char *vault_path = velaops_storage_get_vault_path();

  /* 检查命令行参数或使用默认配置 */
  if (argc >= 4)
    {
      /* 从命令行参数获取配置: velaops <host> <user> <password> [port] */
      strncpy(profile.host, argv[1], sizeof(profile.host) - 1);
      strncpy(profile.user, argv[2], sizeof(profile.user) - 1);
      strncpy(profile.secret, argv[3], sizeof(profile.secret) - 1);
      profile.port = (argc >= 5) ? atoi(argv[4]) : 22;
      profile.auth_kind = VELAOPS_AUTH_PASSWORD;
      if (read_pin(pin, sizeof(pin)) < 0)
        {
          puts("Vault PIN input failed.");
          goto out;
        }
      loaded = false;  /* 需要保存到 vault */
      cli_mode = true;
      printf("Using command-line config: %s@%s:%d\n", profile.user, profile.host, profile.port);
    }
  else if (velaops_storage_vault_exists())
    {
      if (read_pin(pin, sizeof(pin)) < 0) goto out;
      if (velaops_vault_load(vault_path, &profile, pin) < 0)
        {
          puts("Vault unlock failed.");
          goto out;
        }
      loaded = true;
    }
  else if (velaops_config_prompt(&profile, pin, sizeof(pin)) < 0)
    {
      puts("Invalid configuration.");
      puts("Usage: velaops <host> <user> <password> [port]");
      goto out;
    }

  /* 设置 Wi-Fi 连通性探测目标为 SSH 服务器地址。
   * 这让 Wi-Fi monitor 能通过 TCP 探测检测驱动失活
   * （接口 UP 但实际不工作的情况）。 */

  velaops_wifi_set_probe_target(profile.host, profile.port);

  /* 连接 SSH，最多重试 3 次（ESP32-S3 Wi-Fi 驱动不稳定，首次连接可能失败） */
  {
    int ssh_conn_attempts = 0;
    while (ssh_conn_attempts < 3)
      {
        velaops_state_handle_event(EVENT_CONNECT);
        velaops_led_update();
        if (dashboard == 0)
          velaops_dashboard_draw(&profile, &metrics, "SSH CONNECTING");
        ssh = velaops_ssh_connect(&profile, fingerprint, sizeof(fingerprint));
        if (ssh != NULL)
          break;

        ssh_conn_attempts++;
        printf("SSH transport connection failed (attempt %d/3)\n",
               ssh_conn_attempts);
        if (ssh_conn_attempts < 3)
          {
            /* 检查 Wi-Fi 是否仍然连接 */
            if (!velaops_wifi_is_connected())
              {
                puts("Wi-Fi connection lost. Attempting reconnect...");
                velaops_wifi_reconnect();
              }
            sleep(3);
          }
      }
    if (ssh == NULL)
      {
        puts("SSH transport connection failed after 3 attempts.");
        goto out;
      }
  }

  /* 验证服务器指纹 */
  if (profile.fingerprint[0] == '\0')
    {
      if (cli_mode)
        {
          /* CLI 模式自动信任首次连接的服务器指纹 */
          printf("Auto-trusting server fingerprint (CLI mode): %s\n",
                 fingerprint);
        }
      else
        {
          if (dashboard == 0)
            velaops_dashboard_draw(&profile, &metrics, "VERIFY SERVER KEY");
          if (confirm_fingerprint(fingerprint) < 0)
            {
              puts("Server key was not trusted; connection stopped.");
              goto out;
            }
        }
      snprintf(profile.fingerprint, sizeof(profile.fingerprint), "%s",
               fingerprint);
    }
  else if (strcmp(profile.fingerprint, fingerprint) != 0)
    {
      puts("SECURITY ERROR: server public key changed; connection stopped.");
      if (dashboard == 0) velaops_dashboard_draw(&profile, &metrics, "KEY CHANGED");
      goto out;
    }

  /* 密码认证，最多重试 3 次 */
  if (profile.auth_kind == VELAOPS_AUTH_PASSWORD)
    {
      int auth_attempts = 0;
      int auth_success = 0;
      while (auth_attempts < 3 && !auth_success)
        {
          if (velaops_ssh_auth_password(ssh, profile.secret) == 0)
            {
              auth_success = 1;
            }
          else
            {
              auth_attempts++;
              printf("SSH authentication failed (attempt %d/3)\n", auth_attempts);
              if (auth_attempts < 3)
                {
                  printf("Retrying...\n");
                  sleep(1);
                }
            }
        }
      if (!auth_success)
        {
          puts("SSH authentication failed after 3 attempts.");
          if (dashboard == 0) velaops_dashboard_draw(&profile, &metrics, "AUTH FAILED");
          goto out;
        }
    }
  else if (profile.auth_kind == VELAOPS_AUTH_PRIVATE_KEY)
    {
      if (velaops_ssh_auth_publickey(ssh, profile.private_key_path) < 0)
        {
          puts("SSH public-key authentication failed.");
          goto out;
        }
    }
  else
    {
      puts("Unsupported authentication method.");
      if (dashboard == 0) velaops_dashboard_draw(&profile, &metrics, "AUTH FAILED");
      goto out;
    }

  /* 保存配置（如果是首次配置） */
  if (!loaded)
    {
      if (velaops_vault_save(vault_path, &profile, pin) < 0)
        puts("Warning: encrypted vault could not be saved; session continues.");
      else
        puts("Encrypted credential vault saved (AES-256-GCM/PBKDF2).");
    }

  /* 清除 PIN（密码保留在 profile 中供重连使用） */
  velaops_secure_zero(pin, sizeof(pin));

  velaops_state_handle_event(EVENT_CONNECTED);
  velaops_state_handle_event(EVENT_CONNECT);
  velaops_led_update();

  /* 主监控循环 */
  puts("SSH connected. Refreshing CPU/memory every 5 seconds; Ctrl-C to stop.");
  int consecutive_failures = 0;
  const int max_failures = 5;
  for (;;)
    {
      struct velaops_alert alerts[8];
      int alert_count;

      /* 注意：不在每轮都调用 velaops_wifi_monitor()。
       * 它会对 SSH 服务器发起 TCP 探测，每 4 秒一条额外的 TCP 连接。
       * ESP32-S3 Wi-Fi 驱动在密集 TCP 操作下不稳定，持续探测反而会
       * 引入多余的链路扰动。只有 SSH 采集失败时才进入 Wi-Fi 检查。 */

      if (velaops_metrics_collect(ssh, &metrics) == 0)
        {
          consecutive_failures = 0; /* 重置失败计数 */
          printf("CPU %.1f%%  MEM %.1f%% (%lu/%lu MB)\n",
                 metrics.cpu_percent, metrics.memory_percent,
                 (metrics.memory_total_kb - metrics.memory_available_kb) / 1024,
                 metrics.memory_total_kb / 1024);
          alert_count = velaops_rules_evaluate(&metrics, alerts, 8);
          if (alert_count > 0)
            {
              int i;
              for (i = 0; i < alert_count; i++) velaops_alert_print(&alerts[i]);
              if (!velaops_state_has_alerts())
                velaops_state_handle_event(EVENT_ALERT);
            }
          velaops_led_update();
          if (dashboard == 0)
            velaops_dashboard_draw(&profile, &metrics,
                                   alert_count ? "ALERT CHECK CONSOLE" :
                                                 "MONITORING OK");
        }
      else
        {
          consecutive_failures++;
          printf("Metric collection failed (%d/%d)\n",
                 consecutive_failures, max_failures);
          if (dashboard == 0)
            velaops_dashboard_draw(&profile, &metrics, "METRIC ERROR");

          if (consecutive_failures >= max_failures)
            {
              puts("Too many consecutive failures. Connection may be lost.");
              if (dashboard == 0)
                velaops_dashboard_draw(&profile, &metrics, "CONNECTION LOST");

              /* 仅在 SSH 连续失败时检查 Wi-Fi，避免持续探测扰动链路 */
              velaops_wifi_monitor();

              /* 检查 Wi-Fi 连接 */
              if (!velaops_wifi_is_connected())
                {
                  puts("Wi-Fi connection lost. Attempting reconnect...");
                  if (velaops_wifi_reconnect() < 0)
                    {
                      puts("Wi-Fi reconnect failed.");
                    }
                }

              /* 尝试重新连接 SSH */
              velaops_ssh_close(ssh);
              ssh = velaops_ssh_connect(&profile, fingerprint, sizeof(fingerprint));
              if (ssh == NULL)
                {
                  puts("SSH reconnect failed. Retrying...");
                  sleep(3);
                  continue;
                }
              /* 重新认证 */
              if (profile.auth_kind == VELAOPS_AUTH_PASSWORD)
                {
                  if (velaops_ssh_auth_password(ssh, profile.secret) != 0)
                    {
                      puts("SSH re-auth failed. Retrying...");
                      velaops_ssh_close(ssh);
                      ssh = NULL;
                      sleep(3);
                      continue;
                    }
                }
              puts("SSH reconnected and authenticated.");
              consecutive_failures = 0;
            }
        }
      {
        int i;
        for (i = 0; i < 16; i++)
          {
            velaops_led_update();
            usleep(250000);
          }
      }
    }

  rc = 0;
out:
  velaops_secure_zero(profile.secret, sizeof(profile.secret));
  velaops_secure_zero(pin, sizeof(pin));
  velaops_ssh_close(ssh);
  velaops_input_close();
  if (dashboard == 0) velaops_dashboard_close();
  velaops_led_close();
  velaops_state_stop();
  return rc;
}
