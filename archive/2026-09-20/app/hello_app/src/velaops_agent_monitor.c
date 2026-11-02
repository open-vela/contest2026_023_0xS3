/****************************************************************************
 * VelaOps AI Agent 主动巡检适配器实现。
 ****************************************************************************/

#include "velaops_agent_monitor.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "core/message_bus.h"

#define VELAOPS_AGENT_MONITOR_INTERVAL_SECONDS 5
#define VELAOPS_AGENT_MONITOR_STACK_SIZE 32768
#define VELAOPS_AGENT_MONITOR_RESOURCE_CAPACITY 2048
#define VELAOPS_AGENT_MONITOR_DIAGNOSIS_CAPACITY 1024
#define VELAOPS_AGENT_MONITOR_PROMPT_CAPACITY 320
#define VELAOPS_AGENT_MONITOR_FAILURE_THRESHOLD 2
#define VELAOPS_AGENT_MONITOR_RECOVERY_THRESHOLD 2
#define VELAOPS_AGENT_SKILL_PATH   "/data/ai_agent/skills/server-incident-response.md"

struct velaops_agent_monitor_thread_context_s
{
  velaops_resource_fetcher_t fetcher;
  char initial_resource_json[VELAOPS_AGENT_MONITOR_RESOURCE_CAPACITY];
};

static pthread_mutex_t g_start_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_started;

static int velaops_agent_monitor_format_prompt(
    velaops_incident_event_t event, uint32_t generation,
    char *output, size_t output_capacity)
{
  const char *event_name;
  int length;

  if (output == NULL || output_capacity == 0)
    {
      return ERROR;
    }
  if (event == VELAOPS_INCIDENT_EVENT_OPENED)
    {
      event_name = "opened";
    }
  else if (event == VELAOPS_INCIDENT_EVENT_RECOVERED)
    {
      event_name = "recovered";
    }
  else
    {
      return ERROR;
    }

  /*
   * 事件只携带本地可信的类型和代次，不拼接 Proxy 返回内容。Agent 必须通过
   * 固定只读工具重新取证，服务端字符串因此无法越过 Skill 边界。
   */
  length = snprintf(
      output, output_capacity,
      "VelaOps proactive event type=%s generation=%lu. Read "
      VELAOPS_AGENT_SKILL_PATH
      " completely, call its fixed read-only resource tool for fresh "
      "evidence, and return only the skill's JSON.",
      event_name, (unsigned long)generation);
  return length >= 0 && (size_t)length < output_capacity ? OK : ERROR;
}

static velaops_agent_monitor_status_t velaops_agent_monitor_dispatch(
    velaops_agent_monitor_t *monitor)
{
  char prompt[VELAOPS_AGENT_MONITOR_PROMPT_CAPACITY];

  if (monitor->pending_event == VELAOPS_INCIDENT_EVENT_NONE)
    {
      return VELAOPS_AGENT_MONITOR_OK;
    }
  if (velaops_agent_monitor_format_prompt(
          monitor->pending_event, monitor->pending_generation,
          prompt, sizeof(prompt)) != OK ||
      monitor->sink(prompt) != 0)
    {
      return VELAOPS_AGENT_MONITOR_DISPATCH_ERROR;
    }
  monitor->pending_event = VELAOPS_INCIDENT_EVENT_NONE;
  return VELAOPS_AGENT_MONITOR_OK;
}

int velaops_agent_monitor_init(velaops_agent_monitor_t *monitor,
                               uint16_t failure_threshold,
                               uint16_t recovery_threshold,
                               velaops_agent_event_sink_t sink)
{
  if (monitor == NULL || sink == NULL ||
      velaops_resource_incident_init(
          &monitor->incident, failure_threshold, recovery_threshold) !=
          VELAOPS_RESOURCE_INCIDENT_OK)
    {
      return ERROR;
    }
  monitor->sink = sink;
  monitor->pending_event = VELAOPS_INCIDENT_EVENT_NONE;
  monitor->pending_generation = 0;
  return OK;
}

velaops_agent_monitor_status_t velaops_agent_monitor_sample(
    velaops_agent_monitor_t *monitor, const char *resource_json,
    int64_t observed_at)
{
  char diagnosis[VELAOPS_AGENT_MONITOR_DIAGNOSIS_CAPACITY];
  velaops_incident_event_t event;
  velaops_resource_incident_status_t status;

  if (monitor == NULL || resource_json == NULL || observed_at < 0)
    {
      return VELAOPS_AGENT_MONITOR_INVALID_ARGUMENT;
    }

  /* 队列暂满时保留事件，在消费新证据前优先重试，避免边沿静默丢失。 */
  if (velaops_agent_monitor_dispatch(monitor) != VELAOPS_AGENT_MONITOR_OK)
    {
      return VELAOPS_AGENT_MONITOR_DISPATCH_ERROR;
    }

  status = velaops_resource_incident_apply(
      &monitor->incident, resource_json, observed_at, &event,
      diagnosis, sizeof(diagnosis));
  if (status == VELAOPS_RESOURCE_INCIDENT_INVALID_EVIDENCE ||
      status == VELAOPS_RESOURCE_INCIDENT_ENCODE_ERROR)
    {
      return VELAOPS_AGENT_MONITOR_INVALID_EVIDENCE;
    }
  if (status == VELAOPS_RESOURCE_INCIDENT_OUT_OF_ORDER)
    {
      return VELAOPS_AGENT_MONITOR_OUT_OF_ORDER;
    }
  if (status != VELAOPS_RESOURCE_INCIDENT_OK)
    {
      return VELAOPS_AGENT_MONITOR_INVALID_ARGUMENT;
    }
  if (event == VELAOPS_INCIDENT_EVENT_NONE)
    {
      return VELAOPS_AGENT_MONITOR_OK;
    }

  monitor->pending_event = event;
  monitor->pending_generation = monitor->incident.tracker.generation;
  return velaops_agent_monitor_dispatch(monitor);
}

static int velaops_agent_monitor_bus_sink(const char *prompt)
{
  agent_msg_t message = {0};
  int status;

  strncpy(message.channel, "cli", sizeof(message.channel) - 1);
  strncpy(message.chat_id, "console", sizeof(message.chat_id) - 1);
  message.content = strdup(prompt);
  if (message.content == NULL)
    {
      return ERROR;
    }
  status = message_bus_push_inbound(&message);
  if (status != OK)
    {
      free(message.content);
      return ERROR;
    }
  printf("velaops: proactive_agent_event queued\n");
  return OK;
}

static void *velaops_agent_monitor_worker(void *argument)
{
  struct velaops_agent_monitor_thread_context_s *context = argument;
  velaops_agent_monitor_t monitor;
  char resources[VELAOPS_AGENT_MONITOR_RESOURCE_CAPACITY];

  if (velaops_agent_monitor_init(
          &monitor, VELAOPS_AGENT_MONITOR_FAILURE_THRESHOLD,
          VELAOPS_AGENT_MONITOR_RECOVERY_THRESHOLD,
          velaops_agent_monitor_bus_sink) != OK)
    {
      free(context);
      return NULL;
    }

  (void)velaops_agent_monitor_sample(
      &monitor, context->initial_resource_json, (int64_t)time(NULL));
  for (;;)
    {
      sleep(VELAOPS_AGENT_MONITOR_INTERVAL_SECONDS);
      if (context->fetcher(resources, sizeof(resources)) == 0)
        {
          resources[sizeof(resources) - 1] = '\0';
          (void)velaops_agent_monitor_sample(
              &monitor, resources, (int64_t)time(NULL));
        }
    }
  return NULL;
}

int velaops_agent_monitor_start(velaops_resource_fetcher_t fetcher,
                                const char *initial_resource_json)
{
  struct velaops_agent_monitor_thread_context_s *context;
  pthread_attr_t attributes;
  pthread_t thread;
  size_t length;
  int status = ERROR;

  if (fetcher == NULL || initial_resource_json == NULL)
    {
      return ERROR;
    }
  length = strlen(initial_resource_json);
  if (length >= VELAOPS_AGENT_MONITOR_RESOURCE_CAPACITY)
    {
      return ERROR;
    }

  pthread_mutex_lock(&g_start_lock);
  if (g_started)
    {
      pthread_mutex_unlock(&g_start_lock);
      return OK;
    }
  context = calloc(1, sizeof(*context));
  if (context == NULL)
    {
      pthread_mutex_unlock(&g_start_lock);
      return ERROR;
    }
  context->fetcher = fetcher;
  memcpy(context->initial_resource_json, initial_resource_json, length + 1);

  pthread_attr_init(&attributes);
  pthread_attr_setstacksize(&attributes, VELAOPS_AGENT_MONITOR_STACK_SIZE);
  if (pthread_create(&thread, &attributes,
                     velaops_agent_monitor_worker, context) == 0)
    {
      pthread_detach(thread);
      g_started = true;
      status = OK;
    }
  else
    {
      free(context);
    }
  pthread_attr_destroy(&attributes);
  pthread_mutex_unlock(&g_start_lock);
  return status;
}
