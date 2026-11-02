/****************************************************************************
 * VelaOps TF 卡凭据自动配置（上电自动联网 → 配置 → 进 Agent）。
 ****************************************************************************/

#ifndef VELAOPS_AUTOCONFIG_H
#define VELAOPS_AUTOCONFIG_H

#include <stddef.h>

#define VELAOPS_CRED_WIFI_SSID_CAPACITY 33
#define VELAOPS_CRED_WIFI_PASSWORD_CAPACITY 64
#define VELAOPS_CRED_API_KEY_CAPACITY 96
#define VELAOPS_CRED_DEMO_HOST_CAPACITY 48
#define VELAOPS_CRED_SECRET_CAPACITY 129
#define VELAOPS_CRED_TRANSPORT_CAPACITY 12

/* 传输模式：wifi=设备 WiFi 直连开发机；serial=业务 HTTP 全走 USB 串口隧道。
 * 用 TRANSPORT=serial 选择，默认 wifi，便于 openvela 修复 WiFi 后一键切回。 */
#define VELAOPS_TRANSPORT_WIFI "wifi"
#define VELAOPS_TRANSPORT_SERIAL "serial"

/* TF 卡凭据文件：KEY=VALUE 文本，'#' 开头为注释，允许空行与行尾空白。 */
typedef struct
{
  char wifi_ssid[VELAOPS_CRED_WIFI_SSID_CAPACITY];
  char wifi_password[VELAOPS_CRED_WIFI_PASSWORD_CAPACITY];
  char api_key[VELAOPS_CRED_API_KEY_CAPACITY];
  char demo_host[VELAOPS_CRED_DEMO_HOST_CAPACITY];
  char demo_port[8];
  char device_secret[VELAOPS_CRED_SECRET_CAPACITY];
  char transport[VELAOPS_CRED_TRANSPORT_CAPACITY];
  int has_api_key;
  int has_demo_host;
  int has_device_secret;
} velaops_credentials_t;

/* 解析凭据文件内容（主机侧单测与设备侧共用）。返回 0 表示至少包含
 * WIFI_SSID 与 WIFI_PASSWORD 两项必填字段。 */
int velaops_autoconfig_parse_credentials(const char *content,
                                         velaops_credentials_t *credentials);

/* TRANSPORT=serial 时返回 1（业务走串口隧道），否则 0（WiFi 直连）。 */
int velaops_credentials_use_serial(const velaops_credentials_t *credentials);

/* 设备上执行完整自动配置流程；verbose=1 打印进度（CLI 手动调用），
 * 上电自启路径同样打印以便串口观察。返回 0 表示全流程成功。 */
int velaops_autoconfig_run(int verbose);

#ifdef __NuttX__
/* 以内核任务方式启动 builtin 入口，栈大小显式指定。
 * CONFIG_POSIX_SPAWN_DEFAULT_STACKSIZE 只有 2KB，task_spawn 直传入口会
 * 用小栈建任务，printf/网络调用会溢出并破坏堆；这里统一给足栈。 */
int velaops_spawn_with_stack(int (*entry)(int, char **), const char *name,
                             char *const argv[], size_t stacksize);
#endif

#endif /* VELAOPS_AUTOCONFIG_H */
