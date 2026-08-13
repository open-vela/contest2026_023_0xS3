#ifndef VELAOPS_H
#define VELAOPS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define VELAOPS_HOST_MAX 64
#define VELAOPS_USER_MAX 32
#define VELAOPS_SECRET_MAX 128
#define VELAOPS_FINGERPRINT_MAX 96
#define VELAOPS_PRIVATE_KEY_MAX 4096
#define VELAOPS_SERVER_NAME_MAX 32
#define VELAOPS_MAX_SERVERS 4

enum velaops_auth_kind
{
  VELAOPS_AUTH_PASSWORD = 1,
  VELAOPS_AUTH_PRIVATE_KEY = 2
};

struct velaops_profile
{
  char host[VELAOPS_HOST_MAX];
  char user[VELAOPS_USER_MAX];
  uint16_t port;
  uint8_t auth_kind;
  char fingerprint[VELAOPS_FINGERPRINT_MAX];
  char secret[VELAOPS_SECRET_MAX];
  char private_key_path[256];
};

struct velaops_server_config
{
  char name[VELAOPS_SERVER_NAME_MAX];
  struct velaops_profile profile;
  bool active;
  time_t last_used;
};

struct velaops_connection_pool
{
  struct velaops_ssh *connections[VELAOPS_MAX_SERVERS];
  int count;
  int max_connections;
};

struct velaops_metrics
{
  /* 基本指标 */
  float cpu_percent;
  float memory_percent;
  unsigned long memory_total_kb;
  unsigned long memory_available_kb;

  /* 扩展指标 */
  unsigned long disk_total_kb;
  unsigned long disk_used_kb;
  unsigned long disk_available_kb;
  float disk_percent;

  float load_1min;
  float load_5min;
  float load_15min;

  unsigned long net_rx_bytes;
  unsigned long net_tx_bytes;

  int process_count;
  unsigned long uptime_seconds;

  bool valid;
};

/* 系统状态 */
enum velaops_state
{
  STATE_IDLE = 0,
  STATE_CONNECTING,
  STATE_CONNECTED,
  STATE_MONITORING,
  STATE_ALERT,
  STATE_ERROR
};

/* 系统事件 */
enum velaops_event
{
  EVENT_CONNECT = 0,
  EVENT_CONNECTED,
  EVENT_DISCONNECT,
  EVENT_ALERT,
  EVENT_ACKNOWLEDGE,
  EVENT_ERROR,
  EVENT_TIMEOUT
};

/* LED 状态 */
enum velaops_led_state
{
  LED_STATE_OFF = 0,
  LED_STATE_GREEN_ON,
  LED_STATE_GREEN_BLINK,
  LED_STATE_YELLOW_ON,
  LED_STATE_RED_ON,
  LED_STATE_RED_BLINK
};

enum velaops_alert_level
{
  ALERT_LEVEL_INFO = 0,
  ALERT_LEVEL_WARNING,
  ALERT_LEVEL_CRITICAL
};

struct velaops_alert
{
  char rule_name[32];
  char message[128];
  enum velaops_alert_level level;
  time_t timestamp;
  bool acknowledged;
};

void velaops_secure_zero(void *data, unsigned int size);
int velaops_config_prompt(struct velaops_profile *profile, char *pin,
                          unsigned int pin_size);
int velaops_vault_save(const char *path, const struct velaops_profile *profile,
                       const char *pin);
int velaops_vault_load(const char *path, struct velaops_profile *profile,
                       const char *pin);

/* 存储管理 */
int velaops_storage_init(void);
bool velaops_storage_file_exists(const char *path);
const char *velaops_storage_get_vault_path(void);
const char *velaops_storage_get_wifi_path(void);
const char *velaops_storage_get_config_path(void);
bool velaops_storage_vault_exists(void);
int velaops_storage_save_wifi_config(const char *ssid, const char *password);
int velaops_storage_load_wifi_config(char *ssid, unsigned int ssid_size,
                                     char *password, unsigned int password_size);

/* Wi-Fi 管理 */
int velaops_wifi_init(void);
int velaops_wifi_prompt_and_connect(void);
int velaops_wifi_configure(const char *ssid, const char *password);
int velaops_wifi_connect(void);
int velaops_wifi_disconnect(void);
int velaops_wifi_reconnect(void);
bool velaops_wifi_is_connected(void);
int velaops_wifi_get_ip(char *ip, size_t ip_size);
int velaops_wifi_auto_connect(void);
void velaops_wifi_monitor(void);
void velaops_wifi_set_probe_target(const char *ip, int port);

struct velaops_ssh;
struct velaops_ssh *velaops_ssh_connect(const struct velaops_profile *profile,
                                        char *fingerprint,
                                        unsigned int fingerprint_size);
int velaops_ssh_auth_password(struct velaops_ssh *client, const char *password);
int velaops_ssh_auth_publickey(struct velaops_ssh *client,
                               const char *private_key_path);
int velaops_ssh_exec(struct velaops_ssh *client, const char *command,
                     char *output, unsigned int output_size);
bool velaops_ssh_is_connected(struct velaops_ssh *client);
void velaops_ssh_close(struct velaops_ssh *client);

/* 连接管理 */
int velaops_connpool_init(struct velaops_connection_pool *pool);
struct velaops_ssh *velaops_connpool_get(struct velaops_connection_pool *pool,
                                         const struct velaops_profile *profile);
void velaops_connpool_release(struct velaops_connection_pool *pool,
                              struct velaops_ssh *client);
void velaops_connpool_cleanup(struct velaops_connection_pool *pool);

/* 多服务器配置 */
int velaops_server_list_load(struct velaops_server_config *servers,
                             int max_servers);
int velaops_server_list_save(const struct velaops_server_config *servers,
                             int count);
int velaops_server_add(struct velaops_server_config *servers, int *count,
                       const struct velaops_server_config *new_server);
int velaops_server_remove(struct velaops_server_config *servers, int *count,
                          int index);

int velaops_metrics_collect(struct velaops_ssh *client,
                            struct velaops_metrics *metrics);
int velaops_rules_evaluate(const struct velaops_metrics *metrics,
                           struct velaops_alert *alerts, int max_alerts);
const char *velaops_alert_level_str(enum velaops_alert_level level);
void velaops_alert_print(const struct velaops_alert *alert);
bool velaops_has_critical_alerts(const struct velaops_alert *alerts,
                                 int count);
int velaops_dashboard_open(void);
void velaops_dashboard_draw(const struct velaops_profile *profile,
                            const struct velaops_metrics *metrics,
                            const char *status);
void velaops_dashboard_next_page(void);
void velaops_dashboard_close(void);

/* 状态机管理 */
int velaops_state_init(void);
int velaops_state_handle_event(enum velaops_event event);
enum velaops_state velaops_state_get_current(void);
enum velaops_state velaops_state_get_previous(void);
time_t velaops_state_get_duration(void);
int velaops_state_get_alert_count(void);
bool velaops_state_is_monitoring(void);
bool velaops_state_has_alerts(void);
bool velaops_state_is_error(void);
void velaops_state_reset(void);
void velaops_state_stop(void);
bool velaops_state_is_running(void);

/* 按键输入 */
int velaops_input_init(void);
void velaops_input_process(void);
void velaops_input_close(void);

/* LED 控制 */
int velaops_led_init(void);
int velaops_led_set_state(enum velaops_led_state state);
void velaops_led_update(void);
void velaops_led_close(void);

#endif
