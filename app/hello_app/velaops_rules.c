/****************************************************************************
 * VelaOps Sentinel - 本地规则引擎
 ****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>

#include "velaops.h"

/* 规则操作符 */
enum velaops_rule_op
{
  RULE_OP_GT,   /* 大于 */
  RULE_OP_LT,   /* 小于 */
  RULE_OP_EQ,   /* 等于 */
  RULE_OP_GTE,  /* 大于等于 */
  RULE_OP_LTE   /* 小于等于 */
};

/* 规则类型 */
enum velaops_rule_type
{
  RULE_TYPE_THRESHOLD,  /* 阈值规则 */
  RULE_TYPE_TREND,      /* 趋势规则 */
  RULE_TYPE_COMPOSITE   /* 组合规则 */
};

/* 规则结构 */
struct velaops_rule
{
  char name[32];
  char metric[16];
  enum velaops_rule_type type;
  enum velaops_rule_op op;
  float threshold;
  enum velaops_alert_level level;
  bool enabled;
};

/* 默认规则 */
static struct velaops_rule default_rules[] = {
  {
    .name = "high_cpu",
    .metric = "cpu_percent",
    .type = RULE_TYPE_THRESHOLD,
    .op = RULE_OP_GT,
    .threshold = 90.0f,
    .level = ALERT_LEVEL_WARNING,
    .enabled = true
  },
  {
    .name = "critical_cpu",
    .metric = "cpu_percent",
    .type = RULE_TYPE_THRESHOLD,
    .op = RULE_OP_GT,
    .threshold = 95.0f,
    .level = ALERT_LEVEL_CRITICAL,
    .enabled = true
  },
  {
    .name = "high_memory",
    .metric = "memory_percent",
    .type = RULE_TYPE_THRESHOLD,
    .op = RULE_OP_GT,
    .threshold = 85.0f,
    .level = ALERT_LEVEL_WARNING,
    .enabled = true
  },
  {
    .name = "low_disk",
    .metric = "disk_percent",
    .type = RULE_TYPE_THRESHOLD,
    .op = RULE_OP_GT,
    .threshold = 90.0f,
    .level = ALERT_LEVEL_WARNING,
    .enabled = true
  },
  {
    .name = "high_load",
    .metric = "load_1min",
    .type = RULE_TYPE_THRESHOLD,
    .op = RULE_OP_GT,
    .threshold = 4.0f,
    .level = ALERT_LEVEL_WARNING,
    .enabled = true
  }
};

#define NUM_DEFAULT_RULES (sizeof(default_rules) / sizeof(default_rules[0]))

/* 获取指标值 */
static float get_metric_value(const struct velaops_metrics *metrics,
                              const char *metric)
{
  if (strcmp(metric, "cpu_percent") == 0)
    return metrics->cpu_percent;
  else if (strcmp(metric, "memory_percent") == 0)
    return metrics->memory_percent;
  else if (strcmp(metric, "disk_percent") == 0)
    return metrics->disk_percent;
  else if (strcmp(metric, "load_1min") == 0)
    return metrics->load_1min;
  else if (strcmp(metric, "load_5min") == 0)
    return metrics->load_5min;
  else if (strcmp(metric, "load_15min") == 0)
    return metrics->load_15min;
  else
    return 0.0f;
}

/* 评估规则 */
static bool evaluate_rule(const struct velaops_rule *rule,
                          const struct velaops_metrics *metrics)
{
  float value = get_metric_value(metrics, rule->metric);

  switch (rule->op)
    {
      case RULE_OP_GT:
        return value > rule->threshold;
      case RULE_OP_LT:
        return value < rule->threshold;
      case RULE_OP_EQ:
        return value == rule->threshold;
      case RULE_OP_GTE:
        return value >= rule->threshold;
      case RULE_OP_LTE:
        return value <= rule->threshold;
      default:
        return false;
    }
}

/* 评估所有规则 */
int velaops_rules_evaluate(const struct velaops_metrics *metrics,
                           struct velaops_alert *alerts, int max_alerts)
{
  int alert_count = 0;
  int i;

  if (metrics == NULL || alerts == NULL || max_alerts <= 0)
    {
      return 0;
    }

  /* 评估默认规则 */
  for (i = 0; i < NUM_DEFAULT_RULES && alert_count < max_alerts; i++)
    {
      if (default_rules[i].enabled &&
          evaluate_rule(&default_rules[i], metrics))
        {
          /* 生成告警 */
          strncpy(alerts[alert_count].rule_name, default_rules[i].name,
                  sizeof(alerts[alert_count].rule_name) - 1);
          alerts[alert_count].level = default_rules[i].level;
          alerts[alert_count].timestamp = time(NULL);
          alerts[alert_count].acknowledged = false;

          /* 生成告警消息 */
          snprintf(alerts[alert_count].message,
                   sizeof(alerts[alert_count].message),
                   "%.31s: %.15s = %.1f (threshold: %.1f)",
                   default_rules[i].name,
                   default_rules[i].metric,
                   get_metric_value(metrics, default_rules[i].metric),
                   default_rules[i].threshold);

          alert_count++;
        }
    }

  return alert_count;
}

/* 获取告警级别字符串 */
const char *velaops_alert_level_str(enum velaops_alert_level level)
{
  switch (level)
    {
      case ALERT_LEVEL_INFO:
        return "INFO";
      case ALERT_LEVEL_WARNING:
        return "WARNING";
      case ALERT_LEVEL_CRITICAL:
        return "CRITICAL";
      default:
        return "UNKNOWN";
    }
}

/* 打印告警 */
void velaops_alert_print(const struct velaops_alert *alert)
{
  if (alert == NULL)
    {
      return;
    }

  printf("[%s] %s: %s\n",
         velaops_alert_level_str(alert->level),
         alert->rule_name,
         alert->message);
}

/* 检查是否有严重告警 */
bool velaops_has_critical_alerts(const struct velaops_alert *alerts,
                                 int count)
{
  int i;

  if (alerts == NULL)
    {
      return false;
    }

  for (i = 0; i < count; i++)
    {
      if (alerts[i].level == ALERT_LEVEL_CRITICAL &&
          !alerts[i].acknowledged)
        {
          return true;
        }
    }

  return false;
}
