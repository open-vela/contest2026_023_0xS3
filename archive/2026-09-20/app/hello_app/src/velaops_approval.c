/****************************************************************************
 * VelaOps 实体长按批准状态机实现。
 ****************************************************************************/

#include "velaops_approval.h"

#include <stddef.h>

int velaops_approval_init(velaops_approval_t *approval, int64_t started_ms,
                          uint32_t hold_ms, uint32_t timeout_ms)
{
  if (approval == NULL || started_ms < 0 || hold_ms == 0 ||
      timeout_ms < hold_ms)
    {
      return -1;
    }
  approval->started_ms = started_ms;
  approval->last_sample_ms = started_ms;
  approval->pressed_since_ms = 0;
  approval->hold_ms = hold_ms;
  approval->timeout_ms = timeout_ms;
  approval->pressed = false;
  approval->terminal_status = VELAOPS_APPROVAL_PENDING;
  return 0;
}

velaops_approval_status_t velaops_approval_sample(
    velaops_approval_t *approval, int64_t observed_ms, bool pressed)
{
  if (approval == NULL || observed_ms < 0)
    {
      return VELAOPS_APPROVAL_INVALID_ARGUMENT;
    }
  if (approval->terminal_status != VELAOPS_APPROVAL_PENDING)
    {
      return approval->terminal_status;
    }
  if (observed_ms < approval->last_sample_ms)
    {
      return VELAOPS_APPROVAL_OUT_OF_ORDER;
    }
  approval->last_sample_ms = observed_ms;

  /* 到期边界优先拒绝，避免恰好在窗口结束时产生含糊批准。 */
  if ((uint64_t)(observed_ms - approval->started_ms) >= approval->timeout_ms)
    {
      approval->terminal_status = VELAOPS_APPROVAL_EXPIRED;
      return approval->terminal_status;
    }

  if (!pressed)
    {
      approval->pressed = false;
      approval->pressed_since_ms = 0;
      return VELAOPS_APPROVAL_PENDING;
    }
  if (!approval->pressed)
    {
      approval->pressed = true;
      approval->pressed_since_ms = observed_ms;
      return VELAOPS_APPROVAL_PENDING;
    }
  if ((uint64_t)(observed_ms - approval->pressed_since_ms) >=
      approval->hold_ms)
    {
      approval->terminal_status = VELAOPS_APPROVAL_GRANTED;
    }
  return approval->terminal_status;
}
