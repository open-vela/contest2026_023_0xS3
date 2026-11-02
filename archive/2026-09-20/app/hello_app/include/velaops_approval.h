/****************************************************************************
 * VelaOps 实体长按批准状态机。
 ****************************************************************************/

#ifndef VELAOPS_APPROVAL_H
#define VELAOPS_APPROVAL_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  VELAOPS_APPROVAL_PENDING = 0,
  VELAOPS_APPROVAL_GRANTED,
  VELAOPS_APPROVAL_EXPIRED,
  VELAOPS_APPROVAL_INVALID_ARGUMENT,
  VELAOPS_APPROVAL_OUT_OF_ORDER
} velaops_approval_status_t;

typedef struct
{
  int64_t started_ms;
  int64_t last_sample_ms;
  int64_t pressed_since_ms;
  uint32_t hold_ms;
  uint32_t timeout_ms;
  bool pressed;
  velaops_approval_status_t terminal_status;
} velaops_approval_t;

int velaops_approval_init(velaops_approval_t *approval, int64_t started_ms,
                          uint32_t hold_ms, uint32_t timeout_ms);

/* 只有连续按住达到 hold_ms 才批准；松开会重新计时。 */
velaops_approval_status_t velaops_approval_sample(
    velaops_approval_t *approval, int64_t observed_ms, bool pressed);

#endif /* VELAOPS_APPROVAL_H */
