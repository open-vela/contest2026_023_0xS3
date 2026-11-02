/****************************************************************************
 * VelaOps BOOT 按键批准平台适配器。
 ****************************************************************************/

#ifndef VELAOPS_BUTTON_APPROVAL_H
#define VELAOPS_BUTTON_APPROVAL_H

typedef enum
{
  VELAOPS_BUTTON_APPROVED = 0,
  VELAOPS_BUTTON_TIMEOUT,
  VELAOPS_BUTTON_ERROR
} velaops_button_approval_result_t;

velaops_button_approval_result_t velaops_button_wait_for_long_press(
    unsigned int hold_ms, unsigned int timeout_ms);

#endif /* VELAOPS_BUTTON_APPROVAL_H */
