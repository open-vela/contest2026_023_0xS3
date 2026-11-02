/****************************************************************************
 * Contest 2026 team 023 - VelaOps 设备端入口。
 ****************************************************************************/

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/random.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <netutils/ntpclient.h>
#include <nuttx/input/buttons.h>
#include <nuttx/leds/userled.h>
#include <fsutils/mkfatfs.h>
#include "velaops_agent_monitor.h"
#include "velaops_agent_tools.h"
#include "velaops_autoconfig.h"
#include "velaops_button_approval.h"
#include "velaops_dashboard.h"
#include "velaops_device_config.h"
#include "velaops_display.h"
#include "velaops_health.h"
#include "velaops_guarded_repair.h"
#include "velaops_local_diagnosis.h"
#include "velaops_memory_result.h"
#include "velaops_proxy_client.h"
#include "velaops_proxy_http_transport.h"
#include "velaops_resource_incident.h"
#include "velaops_serial_tunnel.h"

#define VELAOPS_AUTH_TARGET "/v1/auth/check"
#define VELAOPS_ACTION_TARGET "/v1/actions/execute"
#define VELAOPS_AUTH_BODY "{}"
#define VELAOPS_CHECK_MEMORY_BODY \
  "{\"schema_version\":1,\"action\":\"check_memory\"," \
  "\"target\":\"local-dev\",\"parameters\":{}}"
#define VELAOPS_CHECK_RESOURCES_BODY \
  "{\"schema_version\":1,\"action\":\"check_resources\"," \
  "\"target\":\"local-dev\",\"parameters\":{}}"
#define VELAOPS_MIN_VALID_TIME 1704067200
#define VELAOPS_TIME_SYNC_ATTEMPTS 3
#define VELAOPS_TIME_SYNC_ATTEMPT_SECONDS 20
#define VELAOPS_MEMORY_UNHEALTHY_PERCENT 80.0
#define VELAOPS_BUTTON_DEVICE "/dev/buttons"
#define VELAOPS_LED_DEVICE "/dev/userleds"
#define VELAOPS_MONITOR_INTERVAL_SECONDS 5
/* WiFi 模式下，复位后 ai_agent 建链与看板首个请求同时发起会让 ESP32-S3
 * 掉线，先等 Agent 稳定再巡检；串口模式无此竞态，立即开始。 */
#define VELAOPS_MONITOR_WIFI_STARTUP_DELAY_SECONDS 20
#define VELAOPS_MONITOR_REPAIR_AFTER_FAILURES 6
#define VELAOPS_MONITOR_FAILURE_REPORT_COOLDOWN_SECONDS 90
/* 经串口隧道往返含 NSH 回写，放宽到 15s（仍远小于隧道侧 90s 等待）。 */
#define VELAOPS_HTTP_TIMEOUT_SECONDS 15
/* LLM 诊断进行中时，Agent 的诊断工具/后台采样会与在途大请求共用隧道；此时
 * 把单次请求超时放宽为"耐心排队"，避免 15s 超时直接报 transport_error 拖垮
 * 整轮诊断。正常无诊断时仍用 15s，保证链路异常能快速失败。 */
#define VELAOPS_HTTP_TIMEOUT_BUSY_SECONDS 240
#define VELAOPS_RESOURCE_RESULT_CAPACITY 2048
#define VELAOPS_LOCAL_DIAGNOSIS_CAPACITY 1024
#define VELAOPS_INCIDENT_FAILURE_THRESHOLD 2
#define VELAOPS_INCIDENT_RECOVERY_THRESHOLD 2
#define VELAOPS_APPROVAL_HOLD_MS 2000
#define VELAOPS_APPROVAL_TIMEOUT_MS 120000
#define VELAOPS_APPROVAL_VALIDITY_SECONDS 60
#define VELAOPS_REPAIR_REQUEST_CAPACITY 512
#define VELAOPS_REPAIR_RESULT_CAPACITY 320
#define VELAOPS_REPAIR_VERIFY_ATTEMPTS 5
#define VELAOPS_ASK_QUEUE_PATH "/tmp/vela-ask.txt"
#define VELAOPS_LLM_DONE_PATH "/tmp/velaops-llm-done"
#define VELAOPS_LLM_BUSY_PATH "/tmp/velaops-llm-busy"
#define VELAOPS_REPAIR_BUSY_PATH "/tmp/velaops-repair-busy"
#define VELAOPS_APPROVE_PATH "/tmp/velaops-approve"
#define VELAOPS_APPROVE_HOLD_SECONDS 2
#define VELAOPS_BUTTON_BOOT (1u << 0)
#define VELAOPS_BUTTON_MENU (1u << 1)
#define VELAOPS_BUTTON_PLAY (1u << 2)
#define VELAOPS_BUTTON_DOWN (1u << 3)
#define VELAOPS_BUTTON_UP (1u << 4)
#define VELAOPS_SKILL_PATH "/data/ai_agent/skills/server-incident-response.md"
#define VELAOPS_LLM_PROMPT_CAPACITY 6144
#define VELAOPS_LLM_SKILL_CAPACITY 4096
/* 串口隧道是单连接半双工：Agent 的 LLM 诊断请求与看板巡检并发会让隧道卡死。
 * 入队 LLM 诊断后暂停巡检该窗口，等诊断完成再恢复；可用环境变量覆盖以便联调。 */
#define VELAOPS_LLM_PAUSE_DEFAULT_SECONDS 180

static volatile time_t g_llm_pause_until;

static time_t velaops_llm_pause_seconds(void)
{
  const char *value = getenv("VELAOPS_LLM_PAUSE");
  long seconds;

  if (value != NULL && value[0] != '\0')
    {
      char *end;
      errno = 0;
      seconds = strtol(value, &end, 10);
      if (errno == 0 && end != value && *end == '\0' && seconds >= 0)
        {
          return (time_t)seconds;
        }
    }
  return VELAOPS_LLM_PAUSE_DEFAULT_SECONDS;
}

static int velaops_set_demo_time(const char *value)
{
  struct timespec requested;
  char *end;
  long long seconds;

  errno = 0;
  seconds = strtoll(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' ||
      seconds < VELAOPS_MIN_VALID_TIME)
    {
      fprintf(stderr, "velaops: Unix 时间参数无效\n");
      return EXIT_FAILURE;
    }

  requested.tv_sec = (time_t)seconds;
  requested.tv_nsec = 0;
  if ((long long)requested.tv_sec != seconds ||
      clock_settime(CLOCK_REALTIME, &requested) != 0)
    {
      fprintf(stderr, "velaops: 设置系统时间失败\n");
      return EXIT_FAILURE;
    }

  printf("velaops: 已设置演示时间\n");
  return EXIT_SUCCESS;
}

static int velaops_ensure_time(void)
{
  int attempt;
  int waited;

  if (time(NULL) >= VELAOPS_MIN_VALID_TIME)
    {
      return 0;
    }
  /* 该版本 NTP daemon 首次失败后会按指数退避重试并保持 RUNNING 状态，
   * 此时再次 ntpc_start() 会因为状态已是 RUNNING 而直接返回，应用只能
   * 空等到退避结束。因此每一轮先 ntpc_stop() 复位 daemon 再重新 start，
   * 才能立即发出新的 NTP 请求。ESP32-S3 首个 UDP 包又可能因 ARP 尚未
   * 建立而丢失，需要多轮覆盖。
   */

  for (attempt = 0; attempt < VELAOPS_TIME_SYNC_ATTEMPTS; attempt++)
    {
      printf("velaops: 正在同步系统时间 (%d/%d)\n", attempt + 1,
             VELAOPS_TIME_SYNC_ATTEMPTS);
      (void)ntpc_stop();
      if (ntpc_start() < 0)
        {
          sleep(2);
          continue;
        }
      for (waited = 0; waited < VELAOPS_TIME_SYNC_ATTEMPT_SECONDS; waited++)
        {
          if (time(NULL) >= VELAOPS_MIN_VALID_TIME)
            {
              return 0;
            }
          sleep(1);
        }
      (void)ntpc_stop();
    }
  return time(NULL) >= VELAOPS_MIN_VALID_TIME ? 0 : -1;
}

static void velaops_hex_encode(const uint8_t *input, size_t input_len,
                               char *output)
{
  static const char digits[] = "0123456789abcdef";
  size_t index;

  for (index = 0; index < input_len; index++)
    {
      output[index * 2] = digits[input[index] >> 4];
      output[index * 2 + 1] = digits[input[index] & 0x0f];
    }
  output[input_len * 2] = '\0';
}

static int velaops_make_metadata(const char *device_id,
                                 velaops_auth_metadata_t *metadata,
                                 char request_id[33], char nonce[33])
{
  uint8_t random_bytes[32];
  time_t now;

  if (velaops_ensure_time() != 0)
    {
      return -1;
    }
  now = time(NULL);
  /* 当前板级配置只启用了硬件 TRNG 对应的 /dev/random。显式请求该
   * 安全随机源，避免 getrandom() 默认访问未启用的 /dev/urandom。
   */

  if (getrandom(random_bytes, sizeof(random_bytes), GRND_RANDOM) !=
      sizeof(random_bytes))
    {
      return -2;
    }
  velaops_hex_encode(random_bytes, 16, request_id);
  velaops_hex_encode(random_bytes + 16, 16, nonce);
  metadata->version = VELAOPS_PROTOCOL_VERSION;
  metadata->device_id = device_id;
  metadata->request_id = request_id;
  metadata->timestamp = (int64_t)now;
  metadata->nonce = nonce;
  return 0;
}

static int velaops_get_memory_health(const char *result_json,
                                     const char **health)
{
  velaops_memory_observation_t observation;
  velaops_health_snapshot_t snapshot;

  if (velaops_memory_result_parse(result_json, &observation) !=
      VELAOPS_MEMORY_RESULT_OK ||
      velaops_evaluate_memory(&observation,
                              VELAOPS_MEMORY_UNHEALTHY_PERCENT,
                              (int64_t)time(NULL), &snapshot) !=
      VELAOPS_HEALTH_OK)
    {
      fprintf(stderr, "velaops: Proxy 内存快照不可信\n");
      return -1;
    }

  *health = snapshot.result == VELAOPS_HEALTH_HEALTHY ?
            "healthy" : "unhealthy";
  return 0;
}

static int velaops_post_request(const char *target, const char *body,
                                const char *operation, bool evaluate_memory,
                                bool quiet,
                                velaops_display_state_t *display_state,
                                char *result_output,
                                size_t result_output_capacity)
{
  velaops_device_config_t config;
  velaops_proxy_http_context_t http_context;
  velaops_proxy_client_t client;
  velaops_auth_metadata_t metadata;
  velaops_proxy_response_t response;
  velaops_config_status_t config_status;
  velaops_proxy_client_status_t client_status;
  char request_id[33];
  char nonce[33];
  int metadata_status;
  int exit_status = EXIT_FAILURE;
  const char *health = NULL;

  if (display_state != NULL)
    {
      display_state->online = 0;
    }

  config_status = velaops_device_config_load(VELAOPS_CONFIG_FILE, &config);
  if (config_status != VELAOPS_CONFIG_OK)
    {
      if (!quiet)
        {
          fprintf(stderr, "velaops: 配置加载失败: %s\n",
                  velaops_config_status_name(config_status));
        }
      return EXIT_FAILURE;
    }
  metadata_status = velaops_make_metadata(config.device_id, &metadata,
                                           request_id, nonce);
  if (metadata_status != 0)
    {
      if (!quiet)
        {
          fprintf(stderr, "velaops: %s未就绪\n",
                  metadata_status == -2 ? "安全随机源" : "系统时间");
        }
      goto cleanup;
    }

  http_context.host = config.host;
  http_context.port = config.port;
  /* 诊断或实体批准/修复进行中：都使用"耐心排队"的长超时，避免突发请求
   * 被 15s 短超时打断而报 transport_error。 */
  http_context.timeout_seconds =
      (access(VELAOPS_LLM_BUSY_PATH, F_OK) == 0 ||
       access(VELAOPS_REPAIR_BUSY_PATH, F_OK) == 0) ?
      VELAOPS_HTTP_TIMEOUT_BUSY_SECONDS : VELAOPS_HTTP_TIMEOUT_SECONDS;
  client.device_id = config.device_id;
  client.secret = (const uint8_t *)config.secret;
  client.secret_len = strlen(config.secret);
  client.transport = velaops_proxy_http_transport;
  client.transport_context = &http_context;

  client_status = velaops_proxy_client_post_json(
      &client, target, (const uint8_t *)body, strlen(body),
      &metadata, &response);
  if (client_status != VELAOPS_PROXY_CLIENT_OK)
    {
      if (!quiet)
        {
          fprintf(stderr, "velaops: Proxy 请求失败: %s\n",
                  velaops_proxy_client_status_name(client_status));
        }
      goto cleanup;
    }
  if (!response.ok)
    {
      if (!quiet)
        {
          fprintf(stderr,
                  "velaops: Proxy 拒绝: http=%d code=%s retryable=%s "
                  "message=%s\n",
                  response.http_status, response.error_code,
                  response.retryable ? "true" : "false",
                  response.error_message);
        }
      goto cleanup;
    }

  if (result_output != NULL)
    {
      size_t result_length = strlen(response.result_json);

      if (result_output_capacity == 0 ||
          result_length >= result_output_capacity)
        {
          if (!quiet)
            {
              fprintf(stderr, "velaops: Agent 证据缓冲区不足\n");
            }
          goto cleanup;
        }
      memcpy(result_output, response.result_json, result_length + 1);
    }

  if (display_state != NULL)
    {
      display_state->online = 1;
    }

  if (evaluate_memory &&
      velaops_get_memory_health(response.result_json, &health) != 0)
    {
      goto cleanup;
    }
  if (display_state != NULL && evaluate_memory)
    {
      velaops_health_snapshot_t snapshot;
      velaops_resource_observation_t resources;

      if (velaops_memory_result_parse(response.result_json,
                                      &display_state->memory) !=
          VELAOPS_MEMORY_RESULT_OK ||
          velaops_evaluate_memory(&display_state->memory,
                                  VELAOPS_MEMORY_UNHEALTHY_PERCENT,
                                  (int64_t)time(NULL), &snapshot) !=
          VELAOPS_HEALTH_OK)
        {
          display_state->online = 0;
          goto cleanup;
        }
      display_state->health = snapshot.result;
      display_state->observed_at = snapshot.observed_at;

      /* 看板要显示内存/磁盘/服务/端口全套卡片；check_resources 的 result
       * 已包含这些字段，这里一并解析，避免看板只剩状态灯而数据全为 "--"。 */
      if (velaops_resource_result_parse(response.result_json, &resources) == 0)
        {
          display_state->resources = resources;
          display_state->memory = resources.memory;
          display_state->has_resources = 1;
        }
    }
  if (display_state != NULL && !evaluate_memory)
    {
      velaops_resource_observation_t resources;
      velaops_health_snapshot_t snapshot;

      if (velaops_resource_result_parse(response.result_json, &resources) != 0 ||
          velaops_evaluate_memory(&resources.memory,
                                  VELAOPS_MEMORY_UNHEALTHY_PERCENT,
                                  (int64_t)time(NULL), &snapshot) !=
          VELAOPS_HEALTH_OK)
        {
          fprintf(stderr, "velaops: 资源解析/健康校验失败\n");
          display_state->online = 0;
          goto cleanup;
        }
      display_state->resources = resources;
      display_state->memory = resources.memory;
      display_state->health = snapshot.result;
      display_state->observed_at = snapshot.observed_at;
      display_state->has_resources = 1;
    }
  if (operation != NULL)
    {
      printf("velaops: %s成功 request_id=%s", operation, request_id);
      if (health != NULL)
        {
          printf(" health=%s", health);
        }
      printf(" result=%s\n", response.result_json);
    }
  exit_status = EXIT_SUCCESS;

cleanup:
  velaops_device_config_clear(&config);
  return exit_status;
}

/* 屏显线程与监控线程都以 5s 节拍调用 fetcher，共享一份短 TTL 缓存：
 * 既避免双线程并发打 Proxy，也把后台取证日志压到零。 */
static pthread_mutex_t g_agent_fetch_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_agent_fetch_cache[VELAOPS_RESOURCE_RESULT_CAPACITY];
static int64_t g_agent_fetch_cache_at = -1;
#define VELAOPS_AGENT_FETCH_CACHE_SECONDS 4

static int velaops_fetch_resources_for_agent(char *output,
                                             size_t output_capacity)
{
  int64_t now = (int64_t)time(NULL);
  int status;

  if (output == NULL || output_capacity == 0)
    {
      return EXIT_FAILURE;
    }
  pthread_mutex_lock(&g_agent_fetch_lock);
  if (g_agent_fetch_cache_at >= 0 &&
      now - g_agent_fetch_cache_at < VELAOPS_AGENT_FETCH_CACHE_SECONDS)
    {
      strncpy(output, g_agent_fetch_cache, output_capacity - 1);
      output[output_capacity - 1] = '\0';
      pthread_mutex_unlock(&g_agent_fetch_lock);
      return EXIT_SUCCESS;
    }
  /* LLM 诊断期间：Agent 后台 5s 采样优先复用最近缓存（可过期），避免与大
   * LLM 请求抢隧道；但没有缓存时仍照常请求（由传输层全局互斥排队），不能
   * 因为诊断而让 Agent 自己的诊断工具取不到证据。 */
  if (access(VELAOPS_LLM_BUSY_PATH, F_OK) == 0 &&
      g_agent_fetch_cache_at >= 0)
    {
      strncpy(output, g_agent_fetch_cache, output_capacity - 1);
      output[output_capacity - 1] = '\0';
      pthread_mutex_unlock(&g_agent_fetch_lock);
      return EXIT_SUCCESS;
    }
  status = velaops_post_request(VELAOPS_ACTION_TARGET,
                                VELAOPS_CHECK_RESOURCES_BODY,
                                NULL, false, true, NULL,
                                output, output_capacity);
  if (status == EXIT_SUCCESS)
    {
      strncpy(g_agent_fetch_cache, output, sizeof(g_agent_fetch_cache) - 1);
      g_agent_fetch_cache[sizeof(g_agent_fetch_cache) - 1] = '\0';
      g_agent_fetch_cache_at = now;
    }
  pthread_mutex_unlock(&g_agent_fetch_lock);
  return status;
}

static void velaops_popup_write(const char *title, const char *text);
static void velaops_repair_busy_set(bool on);

static int velaops_repair_demo_service(char *output, size_t output_capacity)
{
  velaops_button_approval_result_t approval_result;
  uint8_t approval_random[16];
  char approval_id[33];
  char request_body[VELAOPS_REPAIR_REQUEST_CAPACITY];
  char resources[VELAOPS_RESOURCE_RESULT_CAPACITY];
  bool recovered;
  bool request_sent;
  time_t approved_at;
  unsigned int attempt;

  if (output == NULL || output_capacity == 0)
    {
      return ERROR;
    }
  if (velaops_ensure_time() != 0)
    {
      return velaops_guarded_repair_format_result(
          "not_started", false, "approval_unavailable", 0,
          output, output_capacity);
    }

  printf("velaops: 请在 120 秒内按住 BOOT 2 秒批准重启 demo 服务\n");
  /* 明确"批准什么动作 + 怎么按"：前 12 字符一行，后 12 字符一行。 */
  velaops_popup_write("APPROVAL", "RESTART DEMOBOOT 2S");
  /* 等待/执行期间置 repair-busy：看板据此不消弹窗、不抢隧道；结束时清除，
   * 看板自动回到资源看板。 */
  velaops_repair_busy_set(true);
  /* 按键由看板按键线程统一读取（长按达标后写 VELAOPS_APPROVE_PATH），这里只
   * 等待该信号；避免两个线程同时读 /dev/buttons 导致本侧读不到长按。 */
  unlink(VELAOPS_APPROVE_PATH);
  approval_result = VELAOPS_BUTTON_TIMEOUT;
  {
    unsigned int waited;

    for (waited = 0; waited < VELAOPS_APPROVAL_TIMEOUT_MS; waited += 200)
      {
        if (access(VELAOPS_APPROVE_PATH, F_OK) == 0)
          {
            unlink(VELAOPS_APPROVE_PATH);
            approval_result = VELAOPS_BUTTON_APPROVED;
            break;
          }
        usleep(200000);
      }
  }
  if (approval_result == VELAOPS_BUTTON_TIMEOUT)
    {
      velaops_repair_busy_set(false);
      return velaops_guarded_repair_format_result(
          "not_started", false, "approval_timeout", 0,
          output, output_capacity);
    }
  if (approval_result != VELAOPS_BUTTON_APPROVED ||
      getrandom(approval_random, sizeof(approval_random), GRND_RANDOM) !=
      sizeof(approval_random))
    {
      velaops_repair_busy_set(false);
      return velaops_guarded_repair_format_result(
          "not_started", false, "approval_unavailable", 0,
          output, output_capacity);
    }

  /* 先等一拍：让看板在途巡检请求收尾（repair-busy 已置位，后续不再发新巡检），
   * 减少重启请求与巡检在单条隧道上的时序冲突。 */
  sleep(2);
  approved_at = time(NULL);
  velaops_hex_encode(approval_random, sizeof(approval_random), approval_id);

  /* 批准后直接重启（不再显示 IN PROGRESS）。重启请求的回包可能因传输抖动
   * 丢失，但服务端其实已执行；因此**无论请求返回值如何都继续做只读复核**，
   * 用独立证据判定成败。复核成功后清除 repair-busy，看板自动回到资源看板。 */
  request_sent = velaops_guarded_repair_build_request(
                     approval_id, (int64_t)approved_at,
                     (int64_t)approved_at + VELAOPS_APPROVAL_VALIDITY_SECONDS,
                     request_body, sizeof(request_body)) == 0 &&
                 velaops_post_request(VELAOPS_ACTION_TARGET, request_body,
                                      "白名单服务重启", false, false, NULL,
                                      NULL, 0) == EXIT_SUCCESS;

  for (attempt = 1; attempt <= VELAOPS_REPAIR_VERIFY_ATTEMPTS; attempt++)
    {
      /* systemd active 可能早于监听端口就绪，使用新的 request ID 有界复核。 */
      sleep(1);
      if (velaops_post_request(
              VELAOPS_ACTION_TARGET, VELAOPS_CHECK_RESOURCES_BODY,
              NULL, false, false, NULL, resources, sizeof(resources)) ==
          EXIT_SUCCESS &&
          velaops_guarded_repair_verify(resources, &recovered) == 0 &&
          recovered)
        {
          velaops_repair_busy_set(false);
          return velaops_guarded_repair_format_result(
              "completed", true, "recovered", attempt,
              output, output_capacity);
        }
    }

  velaops_repair_busy_set(false);
  if (!request_sent)
    {
      return velaops_guarded_repair_format_result(
          "unknown", false, "execution_failed", 0,
          output, output_capacity);
    }
  return velaops_guarded_repair_format_result(
      "completed", false, "verification_failed",
      VELAOPS_REPAIR_VERIFY_ATTEMPTS, output, output_capacity);
}

static int velaops_run_local_diagnosis(void)
{
  char resources[VELAOPS_RESOURCE_RESULT_CAPACITY];
  char diagnosis[VELAOPS_LOCAL_DIAGNOSIS_CAPACITY];
  int fetch_status;

  fetch_status = velaops_post_request(VELAOPS_ACTION_TARGET,
                                      VELAOPS_CHECK_RESOURCES_BODY,
                                      NULL, false, false, NULL,
                                      resources, sizeof(resources));
  if (velaops_local_diagnosis_build(
          fetch_status == EXIT_SUCCESS ? resources : NULL,
          diagnosis, sizeof(diagnosis)) != 0)
    {
      fprintf(stderr, "velaops: 本地诊断编码失败\n");
      return EXIT_FAILURE;
    }

  /* 最后一行始终是机器可解析的降级诊断。取证失败时仍输出 unknown，
   * 同时保留失败退出码，便于脚本区分真实成功和安全降级。
   */

  printf("%s\n", diagnosis);
  return fetch_status;
}

/* 单次 LLM 诊断：把 Skill 全文与设备已采集的证据一起塞进一次 ask，明确禁止
 * 调用任何工具。这样每次交互只产生一次大 HTTP 请求（实测首次请求稳定），
 * 避开 NuttX+esp32s3 在连续第二次大请求时打断 WiFi 关联的问题。 */
static int velaops_queue_llm_diagnosis(const char *resources)
{
  static char prompt[VELAOPS_LLM_PROMPT_CAPACITY];
  FILE *file;
  int written;

  /* ai_agent 的文件 ask 通道只读第一行，故提示必须为单行，由 Agent 自行取证。
   * display 后勿跟空格：子串 "play " 会被其 NL 快速通道误判成音乐播放。 */
  (void)resources;
  written = snprintf(
      prompt, sizeof(prompt),
      "VelaOps proactive incident: diagnose the server now. Follow the "
      "server-incident-response Skill. Call velaops_check_resources once for "
      "fresh evidence, then reply with EXACTLY ONE minified JSON object "
      "(no prose, no code fence) containing schema_version, status, summary, "
      "display, evidence, root_cause_candidates, recommended_action and "
      "confidence. The `display` value must be a concise English phrase of at "
      "most 15 printable ASCII characters naming the key process/cause or the "
      "next action (for example CPU HIGH PYTHON, DISK FULL, SVC DOWN, RESTART "
      "DEMO); it is shown on an ASCII-only LCD. All summary/reason text must "
      "be English. Do not greet, do not list capabilities, do not ask "
      "questions.");
  if (written < 0 || (size_t)written >= sizeof(prompt))
    {
      fprintf(stderr, "velaops: LLM 提示词超出容量\n");
      return -1;
    }

  file = fopen(VELAOPS_ASK_QUEUE_PATH, "w");
  if (file == NULL)
    {
      fprintf(stderr, "velaops: 无法写入 ask 队列\n");
      return -1;
    }
  fwrite(prompt, 1, strlen(prompt), file);
  fclose(file);

  /* 让看板巡检在 LLM 诊断期间退避，避免与 Agent 抢单条隧道。回归由 Agent
   * 回发钩子写 /tmp/velaops-llm-done 触发；这里的秒数是兜底上限，防止
   * 模型异常/无回复时看板永久停摆。 */
  unlink(VELAOPS_LLM_DONE_PATH);
  {
    FILE *busy = fopen(VELAOPS_LLM_BUSY_PATH, "w");

    if (busy != NULL)
      {
        fputs("busy", busy);
        fclose(busy);
      }
  }
  g_llm_pause_until = time(NULL) + velaops_llm_pause_seconds();
  return 0;
}

struct velaops_button_context_s
{
  int fd;
  btn_buttonset_t supported;
  btn_buttonset_t previous;
  unsigned int *page;
  velaops_display_t *display;
  velaops_display_state_t *state;
  pthread_mutex_t *display_lock;
  int *alarm_active;
};

/* 异常告警框：优先显示 Agent/LLM 推送到事件文件的内容，否则用本地规则摘要。
 * 文本必须是可打印 ASCII（屏显 5x7 字库限制），最长约 24 字符，分两行。 */
/* 写弹窗请求文件。格式：首行为标题（可选），其余为正文（<=24 字符）。
 * 看板轮询该文件并显示为闪烁框；供 Agent 工具与批准/修复流程共用。 */
static void velaops_popup_write(const char *title, const char *text)
{
  FILE *file = fopen("/tmp/velaops-popup.txt", "w");

  if (file != NULL)
    {
      fprintf(file, "%s\n%s", title != NULL ? title : "ALERT",
              text != NULL ? text : "");
      fclose(file);
    }
}

static void velaops_repair_busy_set(bool on)
{
  if (on)
    {
      FILE *file = fopen(VELAOPS_REPAIR_BUSY_PATH, "w");

      if (file != NULL)
        {
          fputs("repair", file);
          fclose(file);
        }
    }
  else
    {
      unlink(VELAOPS_REPAIR_BUSY_PATH);
    }
}

static bool velaops_monitor_consume_popup(char *title, size_t title_capacity,
                                          char *buffer, size_t capacity)
{
  char raw[96];
  FILE *file;
  size_t length;
  char *newline;

  file = fopen("/tmp/velaops-popup.txt", "r");
  if (file == NULL)
    {
      return false;
    }
  length = fread(raw, 1, sizeof(raw) - 1, file);
  fclose(file);
  raw[length] = '\0';
  unlink("/tmp/velaops-popup.txt");
  if (length == 0)
    {
      return false;
    }

  /* 兼容两种写法：仅有正文（默认标题 ALERT），或 “标题\n正文”。 */
  newline = strchr(raw, '\n');
  if (newline != NULL)
    {
      *newline = '\0';
      snprintf(title, title_capacity, "%.*s", (int)(title_capacity - 1), raw);
      snprintf(buffer, capacity, "%.*s", (int)(capacity - 1), newline + 1);
    }
  else
    {
      snprintf(title, title_capacity, "ALERT");
      snprintf(buffer, capacity, "%.*s", (int)(capacity - 1), raw);
    }
  return buffer[0] != '\0';
}

static void velaops_monitor_alarm_summary(
    const velaops_resource_observation_t *resources, char *buffer,
    size_t capacity)
{
  if (!resources->service_active)
    {
      snprintf(buffer, capacity, "SERVICE DOWN");
    }
  else if (!resources->port_reachable)
    {
      snprintf(buffer, capacity, "PORT CLOSED");
    }
  else if (resources->disk_percent >= 90.0)
    {
      snprintf(buffer, capacity, "DISK %d%% HIGH",
               (int)(resources->disk_percent + 0.5));
    }
  else if (resources->memory.used_percent >= 80.0)
    {
      snprintf(buffer, capacity, "MEM %d%% HIGH",
               (int)(resources->memory.used_percent + 0.5));
    }
  else if (resources->cpu.valid && resources->cpu.used_percent >= 85.0)
    {
      const char *base = resources->process.valid ?
                         strrchr(resources->process.name, '/') : NULL;

      base = base != NULL ? base + 1 :
             (resources->process.valid ? resources->process.name : NULL);
      if (base != NULL && base[0] != '\0')
        {
          snprintf(buffer, capacity, "%.12s %d%%", base,
                   (int)(resources->process.cpu_percent + 0.5));
        }
      else
        {
          snprintf(buffer, capacity, "CPU %d%% HIGH",
                   (int)(resources->cpu.used_percent + 0.5));
        }
    }
  else
    {
      snprintf(buffer, capacity, "RESOURCE ALERT");
    }
}

/* 巡检连续失败：只记录，不触碰网络接口（运行期改接口历来只会让情况更糟）。
 * 串口模式下多为 relay 未运行；WiFi 模式下为链路掉线。 */
static void velaops_monitor_repair_network(bool force)
{
  static time_t last_report;
  time_t now = time(NULL);

  (void)force;
  if (last_report != 0 && now >= last_report &&
      now - last_report < VELAOPS_MONITOR_FAILURE_REPORT_COOLDOWN_SECONDS)
    {
      return;
    }
  last_report = now;
  printf("velaops monitor: 巡检连续失败，请检查串口 relay 或 WiFi 链路\n");
}

/* 串口模式（设备配置指向隧道）无建链竞态，立即巡检；WiFi 模式延迟启动。 */
static bool velaops_monitor_is_serial(void)
{
  velaops_device_config_t config;
  bool serial = false;

  if (velaops_device_config_load(VELAOPS_CONFIG_FILE, &config) ==
      VELAOPS_CONFIG_OK)
    {
      serial = strcmp(config.host, VELAOPS_TUNNEL_HOST) == 0;
      velaops_device_config_clear(&config);
    }
  return serial;
}

static unsigned int velaops_monitor_startup_delay(void)
{
  return velaops_monitor_is_serial() ?
         0 : VELAOPS_MONITOR_WIFI_STARTUP_DELAY_SECONDS;
}

/* 板载 LED（ESP32-S3-EYE 经 /dev/userleds 暴露 1 个灯）：随告警框闪烁。 */
static void velaops_monitor_led(int fd, int on)
{
  struct userled_s led;

  if (fd < 0)
    {
      return;
    }
  led.ul_led = 0;
  led.ul_on = on != 0;
  (void)ioctl(fd, ULEDIOC_SETLED, (unsigned long)(uintptr_t)&led);
}

static void *velaops_button_worker(void *argument)
{
  struct velaops_button_context_s *context = argument;
  btn_buttonset_t sample;
  time_t press_started = 0;
  int approved = 0;

  for (;;)
    {
      if (read(context->fd, &sample, sizeof(sample)) == sizeof(sample))
        {
          int pressed = (sample & context->supported) != 0;

          if (!pressed)
            {
              press_started = 0;
              approved = 0;
            }
          else if (press_started == 0)
            {
              press_started = time(NULL);
              approved = 0;
            }

          /* 批准/修复进行中：统一由本线程读按键；长按达到阈值即写批准信号，
           * 修复流程据此放行——避免修复线程与看板线程抢读 /dev/buttons。 */
          if (pressed && !approved &&
              access(VELAOPS_REPAIR_BUSY_PATH, F_OK) == 0 &&
              (sample & VELAOPS_BUTTON_BOOT) != 0 &&
              time(NULL) - press_started >= VELAOPS_APPROVE_HOLD_SECONDS)
            {
              FILE *file = fopen(VELAOPS_APPROVE_PATH, "w");

              if (file != NULL)
                {
                  fputs("ok", file);
                  fclose(file);
                }
              approved = 1;
              /* 长按达标即视为批准：立刻消掉批准弹窗回到看板，重启在后台继续。 */
              pthread_mutex_lock(context->display_lock);
              if (context->alarm_active != NULL)
                {
                  *context->alarm_active = 0;
                }
              pthread_mutex_unlock(context->display_lock);
            }

          if (pressed && (context->previous & sample) == 0)
            {
              pthread_mutex_lock(context->display_lock);
              if (access(VELAOPS_REPAIR_BUSY_PATH, F_OK) == 0 ||
                  access(VELAOPS_LLM_BUSY_PATH, F_OK) == 0)
                {
                  /* 批准/修复/诊断进行中：这次按键是给"长按批准"或诊断用的，
                   * 看板不要抢着消除弹窗，否则用户一按提示就消失、体验很差。 */
                }
              else if (context->alarm_active != NULL &&
                       *context->alarm_active != 0)
                {
                  /* 告警弹窗优先：短按 BOOT 先消除弹窗，回到看板。 */
                  *context->alarm_active = 0;
                }
              else if ((sample & VELAOPS_BUTTON_UP) != 0)
                {
                  *context->page = *context->page == 0 ?
                                   VELAOPS_DISPLAY_PAGE_COUNT - 1 :
                                   *context->page - 1;
                  velaops_display_show(context->display, context->state,
                                       *context->page);
                }
              else
                {
                  /* BOOT 短按翻页（ADC 四键在 S3 上暂不可用，保底方案）。 */
                  *context->page =
                      (*context->page + 1) % VELAOPS_DISPLAY_PAGE_COUNT;
                  velaops_display_show(context->display, context->state,
                                       *context->page);
                }
              pthread_mutex_unlock(context->display_lock);
            }
          context->previous = sample;
        }
      usleep(50000);
    }
  return NULL;
}

int main(int argc, char *argv[])
{
  /* NTP 不稳定时由开发机注入当前 Unix 时间，避免比赛演示被校时阻塞。
   * 该入口不接收密钥，也不改变 HMAC 和防重放校验。
   */

  if (argc == 3 && strcmp(argv[1], "set-time") == 0)
    {
      return velaops_set_demo_time(argv[2]);
    }

  if (argc >= 3 && strcmp(argv[1], "ask-inject") == 0)
    {
      /* 串口上 nsh 与 agent CLI 抢占输入，直接发 ask 经常被抢走。
       * 此子命令把文本写入队列文件，由 ai_agent 的 ask_queue 线程读走，
       * 等价于 vela> ask。单次 fwrite 落盘，避免读侧看到半行。 */
      char message[512];
      size_t length = 0;
      FILE *file;
      int i;

      for (i = 2; i < argc && length < sizeof(message) - 2; i++)
        {
          int written = snprintf(message + length,
                                 sizeof(message) - length,
                                 "%s%s", i > 2 ? " " : "", argv[i]);
          if (written < 0)
            {
              break;
            }

          length += (size_t)written < sizeof(message) - length
                        ? (size_t)written
                        : sizeof(message) - length - 1;
        }

      message[length++] = '\n';
      file = fopen("/tmp/vela-ask.txt", "w");
      if (file == NULL)
        {
          fprintf(stderr, "velaops: 无法写入 /tmp/vela-ask.txt\n");
          return EXIT_FAILURE;
        }

      fwrite(message, 1, length, file);
      fclose(file);

      /* 联调按钮需要可重复、可验收的即时反馈；同时保留 ask 队列让
       * Agent 继续处理完整事件。看板会用唯一 LCD 句柄消费这个弹窗。 */
      if (strstr(message, "屏幕显示联调测试") != NULL)
        {
          file = fopen("/tmp/velaops-popup.txt", "w");
          if (file != NULL)
            {
              fwrite("TEST-OK", 1, 7, file);
              fclose(file);
            }
        }
      return EXIT_SUCCESS;
    }

  if (argc == 2 && strcmp(argv[1], "display-test") == 0)
    {
      velaops_display_t *display = velaops_display_open();
      int result;

      if (display == NULL)
        {
          fprintf(stderr, "velaops: 无法打开 /dev/lcd0\n");
          return EXIT_FAILURE;
        }

      result = velaops_display_show_test(display);
      velaops_display_close(display);
      return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

  if (argc == 2 && strcmp(argv[1], "auth-check") == 0)
    {
      return velaops_post_request(VELAOPS_AUTH_TARGET, VELAOPS_AUTH_BODY,
                                  "认证", false, false, NULL, NULL, 0);
    }

  if (argc == 2 && strcmp(argv[1], "autoconfig") == 0)
    {
      /* TF 卡凭据自动配置：上电由 bringup 拉起，也可手动执行。 */
      return velaops_autoconfig_run(1) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

  if (argc == 3 && strcmp(argv[1], "fmtsd") == 0)
    {
      /* 限定扇区数把 TF 卡格式化为无分区表的 FAT：全卡格式化在
       * 大容量卡上按 512B 单块写要几十分钟，取前 N 扇区即可。
       * 用 FAT16：小容量卡上 FAT32 需 ≥65525 簇会报 ENFILE。 */

      struct fat_format_s fmt = FAT_FORMAT_INITIALIZER;

      fmt.ff_fattype  = 16;
      fmt.ff_nsectors = (uint32_t)strtoul(argv[2], NULL, 10);
      printf("fmtsd: 开始格式化 /dev/mmcsd1 前 %lu 扇区\n",
             (unsigned long)fmt.ff_nsectors);
      if (mkfatfs("/dev/mmcsd1", &fmt) < 0)
        {
          fprintf(stderr, "fmtsd: mkfatfs failed: %d\n", errno);
          return EXIT_FAILURE;
        }

      /* 写后回读自检：写卡偶发静默丢数据，直接看扇区0是否真有
       * FAT 引导签名（0x55AA 在 510/511 偏移）。延迟与多次回读用于
       * 区分卡编程延迟和真丢数据。 */
      {
        unsigned char sector[512];
        int fd;
        int probe;

        for (probe = 0; probe < 3; probe++)
          {
            sleep(1);
            fd = open("/dev/mmcsd1", O_RDONLY);
            if (fd >= 0 &&
                read(fd, sector, sizeof(sector)) == sizeof(sector))
              {
                printf("fmtsd: 回读%d sector0 jump=%02x sig=%02x%02x\n",
                       probe, sector[0], sector[511], sector[510]);
              }
            else
              {
                printf("fmtsd: 回读%d 失败 errno=%d\n", probe, errno);
              }

            if (fd >= 0)
              {
                close(fd);
              }
          }
      }

      printf("fmtsd: 完成\n");
      return EXIT_SUCCESS;
    }

  if (argc == 2 && strcmp(argv[1], "sdtest") == 0)
    {
      /* 原始块设备写读自检：判定写卡丢数据是硬件/驱动层还是文件系统层。
       * 分别对扇区 0（引导区）和扇区 10 写特征图案并回读比对。 */

      static unsigned char sector[512];
      static const long targets[] = {0, 10};
      int round;

      for (round = 0; round < 2; round++)
        {
          ssize_t n;
          long target = targets[round];
          int fd;
          int i;
          int ok;

          for (i = 0; i < 512; i++)
            {
              sector[i] = (unsigned char)(0xa0 + round + (i & 0x0f));
            }

          fd = open("/dev/mmcsd1", O_RDWR);
          if (fd < 0)
            {
              printf("sdtest: open 失败 errno=%d\n", errno);
              return EXIT_FAILURE;
            }

          if (lseek(fd, target * 512, SEEK_SET) != target * 512 ||
              write(fd, sector, sizeof(sector)) != (ssize_t)sizeof(sector))
            {
              printf("sdtest: 写失败 errno=%d\n", errno);
              close(fd);
              return EXIT_FAILURE;
            }

          close(fd);

          /* 重新打开回读，避免命中任何读缓存。 */
          fd = open("/dev/mmcsd1", O_RDONLY);
          memset(sector, 0, sizeof(sector));
          n = (fd >= 0 && lseek(fd, target * 512, SEEK_SET) == target * 512)
                ? read(fd, sector, sizeof(sector))
                : -1;
          if (fd >= 0)
            {
              close(fd);
            }

          ok = (n == (ssize_t)sizeof(sector));
          for (i = 0; ok && i < 512; i++)
            {
              if (sector[i] != (unsigned char)(0xa0 + round + (i & 0x0f)))
                {
                  ok = 0;
                }
            }

          printf("sdtest: sector=%ld read=%d 首4字节=%02x%02x%02x%02x %s\n",
                 target, (int)n, sector[0], sector[1], sector[2], sector[3],
                 ok ? "OK" : "MISMATCH");
        }

      return EXIT_SUCCESS;
    }

  if (argc == 2 && strcmp(argv[1], "check-memory") == 0)
    {
      /* CLI 只暴露预编译的结构化 Action，不接受用户传入任意
       * Action 名、目标或 JSON，从设备边界阻断命令注入。
       */

      return velaops_post_request(VELAOPS_ACTION_TARGET,
                                  VELAOPS_CHECK_MEMORY_BODY, "内存巡检", true,
                                  false, NULL, NULL, 0);
    }

  if (argc == 2 && strcmp(argv[1], "agent-install") == 0)
    {
      if (velaops_agent_tools_register(velaops_fetch_resources_for_agent,
                                       velaops_agent_monitor_start,
                                       velaops_repair_demo_service) !=
          OK)
        {
          fprintf(stderr, "velaops: AI Agent 工具注册失败\n");
          return EXIT_FAILURE;
        }
      printf("velaops: AI Agent 只读工具已注册\n");
      return EXIT_SUCCESS;
    }

  if (argc == 2 && strcmp(argv[1], "repair-demo") == 0)
    {
      char result[VELAOPS_REPAIR_RESULT_CAPACITY];

      if (velaops_repair_demo_service(result, sizeof(result)) != 0)
        {
          fprintf(stderr, "velaops: 修复结果编码失败\n");
          return EXIT_FAILURE;
        }
      printf("%s\n", result);
      return EXIT_SUCCESS;
    }

  if (argc == 2 && strcmp(argv[1], "tunnel") == 0)
    {
      /* 串口 LLM 隧道（路线 1）：ai_agent 的 LLM 后端指向 127.0.0.1:18080，
       * 本任务把请求经 USB 串口交给开发机 relay，绕开 WiFi。常驻不返回。 */
      return velaops_serial_tunnel_run();
    }

  if (argc == 2 && strcmp(argv[1], "diagnose-local") == 0)
    {
      return velaops_run_local_diagnosis();
    }

  if (argc == 2 && strcmp(argv[1], "diagnose-llm") == 0)
    {
      /* 设备先走稳定的只读通路取证，再把 Skill 全文 + 证据作为一次 ask
       * 入队，交给 LLM 单次推理出结构化诊断（不产生第二轮大请求）。 */
      char resources[VELAOPS_RESOURCE_RESULT_CAPACITY];

      if (velaops_post_request(VELAOPS_ACTION_TARGET,
                               VELAOPS_CHECK_RESOURCES_BODY,
                               NULL, false, true, NULL,
                               resources, sizeof(resources)) != EXIT_SUCCESS)
        {
          fprintf(stderr, "velaops: 取证失败，无法生成 LLM 诊断\n");
          return EXIT_FAILURE;
        }
      if (velaops_queue_llm_diagnosis(resources) != 0)
        {
          return EXIT_FAILURE;
        }
      printf("velaops: 已入队单次 LLM 诊断请求\n");
      return EXIT_SUCCESS;
    }

  if (argc == 2 && strcmp(argv[1], "monitor") == 0)
    {
      velaops_display_t *display;
      velaops_display_state_t state = {0};
      velaops_resource_incident_t resource_incident;
      btn_buttonset_t supported;
      unsigned int page = 0;
      unsigned int consecutive_failures = 0;
      time_t next_refresh = 0;
      int button_fd;
      pthread_t button_thread;
      pthread_mutex_t display_lock = PTHREAD_MUTEX_INITIALIZER;
      struct velaops_button_context_s button_context;
      int alarm_active = 0;
      int was_alarm = 0;
      int force_render = 1;
      int blink_phase = 0;
      int blink_tick = 0;
      int led_fd;
      int attempt;
      int link_state = 0;   /* 0=等待首个成功连接 1=已连接 2=连接中断 */
      int was_link = 1;
      int was_repair = 0;
      time_t next_redraw = 0;
      char alarm_text[25] = {0};
      char alarm_title[20] = "ALERT";

      if (velaops_resource_incident_init(
              &resource_incident, VELAOPS_INCIDENT_FAILURE_THRESHOLD,
              VELAOPS_INCIDENT_RECOVERY_THRESHOLD) !=
          VELAOPS_RESOURCE_INCIDENT_OK)
        {
          fprintf(stderr, "velaops: 无法初始化主动事件状态\n");
          return EXIT_FAILURE;
        }

      /* 看板现在开机即起，可能与板级初始化（相机等）并发，/dev/lcd0 尚未就绪
       * 时会 open 失败；带重试，失败也不退出，避免整块屏留白。 */
      display = NULL;
      for (attempt = 0; attempt < 40 && display == NULL; attempt++)
        {
          display = velaops_display_open();
          if (display == NULL)
            {
              usleep(500000);
            }
        }
      if (display == NULL)
        {
          fprintf(stderr, "velaops: 无法打开 /dev/lcd0\n");
          return EXIT_FAILURE;
        }

      /* 板载 LED 与告警框同步闪烁；缺失不影响看板。 */
      led_fd = open(VELAOPS_LED_DEVICE, O_WRONLY);
      if (led_fd < 0)
        {
          printf("velaops: 无法打开 %s（告警不闪灯）\n", VELAOPS_LED_DEVICE);
        }
      velaops_monitor_led(led_fd, 0);

      button_fd = open(VELAOPS_BUTTON_DEVICE, O_RDONLY | O_NONBLOCK);
      if (button_fd < 0 || ioctl(button_fd, BTNIOC_SUPPORTED,
                                 (unsigned long)(uintptr_t)&supported) < 0)
        {
          fprintf(stderr, "velaops: 无法打开 /dev/buttons\n");
          if (button_fd >= 0)
            {
              close(button_fd);
            }
          velaops_display_close(display);
          return EXIT_FAILURE;
        }
      button_context.fd = button_fd;
      button_context.supported = supported;
      button_context.previous = 0;
      button_context.page = &page;
      button_context.display = display;
      button_context.state = &state;
      button_context.display_lock = &display_lock;
      button_context.alarm_active = &alarm_active;
      if (pthread_create(&button_thread, NULL, velaops_button_worker,
                         &button_context) != 0)
        {
          fprintf(stderr, "velaops: 无法启动按键线程\n");
          close(button_fd);
          velaops_display_close(display);
          return EXIT_FAILURE;
        }
      /* 开机画面：先显示品牌启动框，避免上电后 LCD 长时间白屏；随后主循环
       * 会按连接状态显示 CONNECTION 页或资源看板。 */
      velaops_display_show_message(display, "VELAOPS", "BOOTING", 0);
      sleep(4);

      next_refresh = time(NULL) + velaops_monitor_startup_delay();
      for (;;)
        {
          /* 周期重绘：即使巡检尚未出数，也保证屏上有内容并能自愈偶发花屏
           * （例如板级初始化与看板并发时的短暂干扰）。 */
          if (time(NULL) >= next_redraw)
            {
              force_render = 1;
              next_redraw = time(NULL) + 2;
            }

          if (time(NULL) >= next_refresh)
            {
              /* LLM 诊断 / 实体批准修复期间让路：隧道同一时刻只能服务一个请求，
               * 若与看板巡检并发会让批准后的重启请求超时失败。 */
              if ((g_llm_pause_until > 0 &&
                   time(NULL) < g_llm_pause_until) ||
                  access(VELAOPS_LLM_BUSY_PATH, F_OK) == 0 ||
                  access(VELAOPS_REPAIR_BUSY_PATH, F_OK) == 0)
                {
                  /* Agent 回发钩子标记诊断完成即放行，否则等到兜底上限。 */
                  if (access(VELAOPS_LLM_DONE_PATH, F_OK) == 0)
                    {
                      unlink(VELAOPS_LLM_DONE_PATH);
                      unlink(VELAOPS_LLM_BUSY_PATH);
                      g_llm_pause_until = 0;
                      next_refresh = time(NULL);

                      /* LLM 页已写出则自动跳到 LLM SUMMARY；弹窗仍保留。 */
                      if (VELAOPS_DISPLAY_PAGE_COUNT > 3 &&
                          access("/tmp/velaops-llm-pages.txt", F_OK) == 0)
                        {
                          pthread_mutex_lock(&display_lock);
                          page = 3;
                          force_render = 1;
                          velaops_display_show(display, &state, page);
                          pthread_mutex_unlock(&display_lock);
                        }
                    }
                  else
                    {
                      /* 1s 粒度复查完成信号；若设成兜底截止时间，循环在到期前
                       * 不会再进这里，等于只能等满上限。 */
                      next_refresh = time(NULL) + 1;
                    }
                }
              else
                {
                  velaops_display_state_t refreshed;

                  /* 走出暂停分支说明诊断已结束（完成或兜底超时）。 */
                  unlink(VELAOPS_LLM_BUSY_PATH);
                  char resources[VELAOPS_RESOURCE_RESULT_CAPACITY];
                  char diagnosis[VELAOPS_LOCAL_DIAGNOSIS_CAPACITY];
                  velaops_incident_event_t incident_event;
                  velaops_resource_incident_status_t incident_status;
                  int request_status;

                  /* 网络请求可能阻塞数秒。只在线程私有副本上更新，完成后再
                   * 一次性交给显示线程，避免 BOOT 翻页读到半更新状态。 */
                  pthread_mutex_lock(&display_lock);
                  refreshed = state;
                  pthread_mutex_unlock(&display_lock);

                  request_status = velaops_post_request(
                      VELAOPS_ACTION_TARGET, VELAOPS_CHECK_RESOURCES_BODY,
                      NULL, false, false, &refreshed,
                      resources, sizeof(resources));
                   if (request_status == EXIT_SUCCESS)
                     {
                       consecutive_failures = 0;
                       link_state = 1;
                       incident_status =
                          velaops_resource_incident_apply_observation(
                              &resource_incident, &refreshed.resources,
                              refreshed.observed_at, &incident_event,
                              diagnosis, sizeof(diagnosis));
                      if (incident_status == VELAOPS_RESOURCE_INCIDENT_OK &&
                          incident_event != VELAOPS_INCIDENT_EVENT_NONE)
                        {
                          printf("velaops: proactive_event type=%s "
                                 "generation=%lu diagnosis=%s\n",
                                 incident_event ==
                                 VELAOPS_INCIDENT_EVENT_OPENED ?
                                 "opened" : "recovered",
                                 (unsigned long)
                                 resource_incident.tracker.generation,
                                 diagnosis);

                          /* 异常开单：自动弹出闪烁告警框，先用本地规则摘要；
                           * Agent/LLM 返回后经事件文件替换为建议文本。恢复正常
                           * 则自动收起。短按 BOOT 可随时消除。 */
                          pthread_mutex_lock(&display_lock);
                          if (incident_event ==
                              VELAOPS_INCIDENT_EVENT_OPENED)
                            {
                              velaops_monitor_alarm_summary(
                                  &refreshed.resources, alarm_text,
                                  sizeof(alarm_text));
                              alarm_active = 1;
                            }
                          else
                            {
                              alarm_active = 0;
                            }
                          pthread_mutex_unlock(&display_lock);

                          /* 主动异常：用已经取到的证据主动发起一次单轮 LLM
                           * 诊断（不额外请求，避免连续大流量）。 */
                          if (incident_event ==
                                  VELAOPS_INCIDENT_EVENT_OPENED &&
                              velaops_queue_llm_diagnosis(resources) == 0)
                            {
                              printf("velaops: 已主动入队单次 LLM 诊断请求\n");
                            }
                        }

                      /* 联调触发：`echo x > /tmp/velaops-diagnose` 后，由看板
                       * 任务用刚取到的证据入队一次单轮 LLM 诊断。放在看板任务里
                       * 执行，避免 NSH 任务与看板并发访问网络。 */
                      if (access("/tmp/velaops-diagnose", F_OK) == 0)
                        {
                          unlink("/tmp/velaops-diagnose");
                          if (velaops_queue_llm_diagnosis(resources) == 0)
                            {
                              printf("velaops: 已入队单次 LLM 诊断请求\n");
                            }
                          else
                            {
                              printf("velaops: LLM 诊断入队失败\n");
                            }
                        }
                    }
                  else
                    {
                      /* 连续失败两次即视为连接中断，屏上给出提示。 */
                      if (consecutive_failures >= 1)
                        {
                          link_state = 2;
                        }
                      if (++consecutive_failures >=
                          VELAOPS_MONITOR_REPAIR_AFTER_FAILURES)
                        {
                          velaops_monitor_repair_network(false);
                          consecutive_failures = 0;
                        }
                    }
                  /* 只在成功巡检时追加 CPU 历史，失败轮保持曲线连续。 */
                  if (request_status == EXIT_SUCCESS)
                    {
                      velaops_dashboard_push_cpu(&refreshed);
                    }
                  pthread_mutex_lock(&display_lock);
                  state = refreshed;
                  pthread_mutex_unlock(&display_lock);
                  force_render = 1;
                  next_refresh =
                      time(NULL) + VELAOPS_MONITOR_INTERVAL_SECONDS;
                }
            }

          /* Agent/LLM 或批准/修复流程推送到事件文件的消息：显示为闪烁框。 */
          {
            char pushed[25];
            char pushed_title[20];

            if (velaops_monitor_consume_popup(pushed_title,
                                              sizeof(pushed_title), pushed,
                                              sizeof(pushed)))
              {
                pthread_mutex_lock(&display_lock);
                memcpy(alarm_text, pushed, sizeof(alarm_text));
                alarm_text[sizeof(alarm_text) - 1] = '\0';
                memcpy(alarm_title, pushed_title, sizeof(alarm_title));
                alarm_title[sizeof(alarm_title) - 1] = '\0';
                alarm_active = 1;
                pthread_mutex_unlock(&display_lock);
                force_render = 1;
              }
          }

          /* 修复结束（repair-busy 消失）：清掉弹窗，回到资源看板。 */
          {
            int repair_now = access(VELAOPS_REPAIR_BUSY_PATH, F_OK) == 0;

            if (was_repair && !repair_now)
              {
                pthread_mutex_lock(&display_lock);
                alarm_active = 0;
                pthread_mutex_unlock(&display_lock);
                force_render = 1;
              }
            was_repair = repair_now;
          }

          /* 告警框或连接状态框激活时按节奏闪烁（板载 LED 同步）。 */
          if (alarm_active || link_state != 1)
            {
              if (++blink_tick >= 2)
                {
                  blink_tick = 0;
                  blink_phase ^= 1;
                  force_render = 1;
                }
            }
          else if (was_alarm || was_link)
            {
              force_render = 1;
            }

          if (force_render)
            {
              pthread_mutex_lock(&display_lock);
              velaops_display_state_t snapshot = state;
              unsigned int page_snapshot = page;
              int active = alarm_active;
              int linked = link_state;
              char text[25];
              char title[20];

              memcpy(text, alarm_text, sizeof(text));
              memcpy(title, alarm_title, sizeof(title));
              pthread_mutex_unlock(&display_lock);

              if (active)
                {
                  velaops_display_show_message(display, title, text,
                                               blink_phase);
                  velaops_monitor_led(led_fd, blink_phase);
                }
              else if (linked != 1)
                {
                  /* 未连上/连接中断：显示连接状态页，避免用户看到空看板。 */
                  char link[24];

                  snprintf(link, sizeof(link), "%s %s",
                           velaops_monitor_is_serial() ? "SERIAL" : "WIFI",
                           linked == 0 ? "WAIT RELAY" : "LINK DOWN");
                  velaops_display_show_message(display, "CONNECTION", link,
                                               blink_phase);
                  velaops_monitor_led(led_fd, blink_phase);
                }
              else
                {
                  velaops_display_show(display, &snapshot, page_snapshot);
                  velaops_monitor_led(led_fd, 0);
                }
              force_render = 0;
            }
          was_alarm = alarm_active;
          was_link = (link_state != 1);
          usleep(200000);
        }
    }

  fprintf(stderr,
          "用法: velaops <auth-check|autoconfig|fmtsd SECTORS|sdtest|check-memory|"
          "agent-install|"
          "diagnose-local|diagnose-llm|monitor|tunnel|"
          "repair-demo|"
          "display-test|"
          "set-time EPOCH>\n");
  return EXIT_FAILURE;
}
