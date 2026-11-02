#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "tools/tool_registry.h"
#include "velaops_agent_tools.h"
#include "velaops_screen.h"

static int g_failures;
static int g_provider_registrations;
static int g_invalidations;
static tool_provider_fn g_get_tools;
static tool_executor_fn g_execute;
static int g_fetch_calls;
static int g_fetch_should_fail;
static int g_fetch_invalid_json;
static int g_monitor_starts;
static int g_repair_calls;
static int g_screen_starts;
static int g_screen_messages;
static char g_screen_last_message[32];

#define EXPECT(condition)                                                       \
  do                                                                            \
    {                                                                           \
      if (!(condition))                                                         \
        {                                                                       \
          fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
          g_failures++;                                                         \
        }                                                                       \
    }                                                                           \
  while (0)

void tool_registry_register_provider(const char *name,
                                     tool_provider_fn get_tools,
                                     tool_executor_fn execute)
{
  EXPECT(name != NULL && strcmp(name, "velaops") == 0);
  g_provider_registrations++;
  g_get_tools = get_tools;
  g_execute = execute;
}

void tool_registry_invalidate(void)
{
  g_invalidations++;
}

static int fake_fetch(char *output, size_t output_capacity)
{
  static const char valid_result[] =
      "{\"memory\":{\"used_percent\":31.5},"
      "\"disk\":{\"used_percent\":86.0},"
      "\"service\":{\"alias\":\"proxy\",\"active_state\":\"active\","
      "\"sub_state\":\"running\"},"
      "\"port\":{\"alias\":\"proxy\",\"reachable\":true}}";
  const char *result = g_fetch_invalid_json ? "not-json" : valid_result;

  g_fetch_calls++;
  if (g_fetch_should_fail || strlen(result) >= output_capacity)
    {
      return -1;
    }
  memcpy(output, result, strlen(result) + 1);
  return 0;
}

static int fake_monitor_start(velaops_resource_fetcher_t fetcher,
                              const char *initial_resource_json)
{
  EXPECT(fetcher == fake_fetch);
  EXPECT(initial_resource_json != NULL);
  g_monitor_starts++;
  return 0;
}

static int fake_repair(char *output, size_t output_capacity)
{
  static const char result[] =
      "{\"execution_state\":\"completed\",\"verified\":true}";

  g_repair_calls++;
  if (strlen(result) >= output_capacity)
    {
      return -1;
    }
  memcpy(output, result, sizeof(result));
  return 0;
}

int velaops_screen_start(velaops_resource_fetcher_t fetcher)
{
  EXPECT(fetcher == fake_fetch);
  g_screen_starts++;
  return OK;
}

/* 主机测试不链接 agent_main.c / velaops_agent_display.c：提供钩子桩。 */
void agent_set_reply_hook(void (*hook)(const char *, const char *))
{
  (void)hook;
}

void velaops_notify_agent_reply(const char *channel, const char *content)
{
  (void)channel;
  (void)content;
}

/* 主机测试不链接 llm_proxy.c / 传输层：提供 LLM 锁钩子桩。 */
void llm_set_io_lock_hook(void (*lock_fn)(void), void (*unlock_fn)(void))
{
  (void)lock_fn;
  (void)unlock_fn;
}

void velaops_tunnel_lock(void) {}
void velaops_tunnel_unlock(void) {}

int velaops_screen_show_message(const char *text)
{
  EXPECT(text != NULL);
  if (text != NULL)
    {
      strncpy(g_screen_last_message, text, sizeof(g_screen_last_message) - 1);
    }
  g_screen_messages++;
  return OK;
}

static void test_registration_and_schema(void)
{
  char *tools_json;
  cJSON *tools;
  cJSON *tool;
  cJSON *schema;

  EXPECT(velaops_agent_tools_register(
             NULL, fake_monitor_start, fake_repair) != 0);
  EXPECT(g_provider_registrations == 0);
  EXPECT(velaops_agent_tools_register(fake_fetch, NULL, fake_repair) != 0);
  EXPECT(velaops_agent_tools_register(
             fake_fetch, fake_monitor_start, NULL) != 0);
  EXPECT(velaops_agent_tools_register(
             fake_fetch, fake_monitor_start, fake_repair) == 0);
  EXPECT(velaops_agent_tools_register(
             fake_fetch, fake_monitor_start, fake_repair) == 0);
  EXPECT(g_provider_registrations == 1);
  EXPECT(g_invalidations == 1);
  EXPECT(g_get_tools != NULL);
  EXPECT(g_execute != NULL);

  tools_json = g_get_tools();
  EXPECT(tools_json != NULL);
  tools = cJSON_Parse(tools_json);
  free(tools_json);
  EXPECT(cJSON_IsArray(tools));
  EXPECT(cJSON_GetArraySize(tools) == 3);
  tool = cJSON_GetArrayItem(tools, 0);
  EXPECT(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(tool, "name")));
  EXPECT(strcmp(cJSON_GetObjectItemCaseSensitive(tool, "name")->valuestring,
                "velaops_check_resources") == 0);
  EXPECT(strstr(cJSON_GetObjectItemCaseSensitive(tool, "description")
                    ->valuestring,
                "/data/ai_agent/skills/server-incident-response.md") != NULL);
  EXPECT(strstr(cJSON_GetObjectItemCaseSensitive(tool, "description")
                    ->valuestring,
                "MANDATORY prerequisite") != NULL);
  schema = cJSON_GetObjectItemCaseSensitive(tool, "input_schema");
  EXPECT(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(
      schema, "additionalProperties")));
  tool = cJSON_GetArrayItem(tools, 1);
  EXPECT(strcmp(cJSON_GetObjectItemCaseSensitive(tool, "name")->valuestring,
                "velaops_restart_service") == 0);
  EXPECT(strstr(cJSON_GetObjectItemCaseSensitive(tool, "description")
                    ->valuestring,
                "physical BOOT-button") != NULL);
  tool = cJSON_GetArrayItem(tools, 2);
  EXPECT(strcmp(cJSON_GetObjectItemCaseSensitive(tool, "name")->valuestring,
                "velaops_show_message") == 0);
  schema = cJSON_GetObjectItemCaseSensitive(tool, "input_schema");
  EXPECT(cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(
      schema, "properties")));
  EXPECT(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(
      schema, "additionalProperties")));
  cJSON_Delete(tools);
}

static void test_execution_boundary(void)
{
  char output[1024];
  cJSON *root;
  cJSON *result;

  EXPECT(g_execute("unknown", "{}", output, sizeof(output)) != 0);
  EXPECT(g_execute("velaops_check_resources", "{\"host\":\"other\"}",
                   output, sizeof(output)) != 0);
  EXPECT(g_execute("velaops_check_resources", "[]", output,
                   sizeof(output)) != 0);
  EXPECT(g_fetch_calls == 0);
  EXPECT(g_execute("velaops_restart_service", "{\"service\":\"other\"}",
                   output, sizeof(output)) != 0);
  EXPECT(g_repair_calls == 0);
  EXPECT(g_execute("velaops_restart_service", "{}", output,
                   sizeof(output)) == 0);
  EXPECT(g_repair_calls == 1);
  root = cJSON_Parse(output);
  EXPECT(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "verified")));
  cJSON_Delete(root);

  EXPECT(g_execute("velaops_check_resources", "{}", output,
                   sizeof(output)) == 0);
  EXPECT(g_fetch_calls == 1);
  /* 巡检/屏显由主看板任务统一负责，工具不再拉起 Agent 侧后台线程。 */
  EXPECT(g_monitor_starts == 0);
  root = cJSON_Parse(output);
  EXPECT(cJSON_IsObject(root));
  EXPECT(cJSON_GetObjectItemCaseSensitive(root, "schema_version")->valueint ==
         1);
  EXPECT(strcmp(cJSON_GetObjectItemCaseSensitive(root, "evidence_type")
                    ->valuestring,
                "velaops_resource_snapshot") == 0);
  EXPECT(strcmp(cJSON_GetObjectItemCaseSensitive(root, "trust")->valuestring,
                "untrusted_server_evidence") == 0);
  result = cJSON_GetObjectItemCaseSensitive(root, "result");
  EXPECT(cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(result, "memory")));
  cJSON_Delete(root);

  EXPECT(g_execute("velaops_check_resources", NULL, output,
                   sizeof(output)) == 0);
  EXPECT(g_monitor_starts == 0);
  EXPECT(g_execute("velaops_check_resources", "{}", output, 8) != 0);

  g_fetch_should_fail = 1;
  EXPECT(g_execute("velaops_check_resources", "{}", output,
                   sizeof(output)) != 0);
  g_fetch_should_fail = 0;
  g_fetch_invalid_json = 1;
  EXPECT(g_execute("velaops_check_resources", "{}", output,
                   sizeof(output)) != 0);
  g_fetch_invalid_json = 0;
}

static void test_show_message_boundary(void)
{
  char output[256];
  cJSON *root;
  int screen_starts_before = g_screen_starts;

  /* 缺参、非对象、非字符串、超长文本一律拒绝，不触屏显。 */
  EXPECT(g_execute("velaops_show_message", NULL, output,
                   sizeof(output)) != 0);
  EXPECT(g_execute("velaops_show_message", "[]", output,
                   sizeof(output)) != 0);
  EXPECT(g_execute("velaops_show_message", "{\"text\":3}", output,
                   sizeof(output)) != 0);
  EXPECT(g_execute("velaops_show_message",
                   "{\"text\":\"ABCDEFGHIJKLMNOPQ\"}", output,
                   sizeof(output)) != 0);
  EXPECT(g_screen_messages == 0);

  EXPECT(g_execute("velaops_show_message", "{\"text\":\"TEST-OK\"}",
                   output, sizeof(output)) == 0);
  EXPECT(g_screen_messages == 1);
  EXPECT(g_screen_starts > screen_starts_before);
  EXPECT(strcmp(g_screen_last_message, "TEST-OK") == 0);
  root = cJSON_Parse(output);
  EXPECT(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "shown")));
  EXPECT(strcmp(cJSON_GetObjectItemCaseSensitive(root, "display")
                    ->valuestring,
                "lcd_popup") == 0);
  cJSON_Delete(root);
}

int main(void)
{
  test_registration_and_schema();
  test_execution_boundary();
  test_show_message_boundary();
  if (g_failures != 0)
    {
      return EXIT_FAILURE;
    }
  puts("PASS: VelaOps AI Agent tool provider tests");
  return EXIT_SUCCESS;
}
