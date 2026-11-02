/****************************************************************************
 * VelaOps BOOT 按键批准平台适配器实现。
 ****************************************************************************/

#include "velaops_button_approval.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/input/buttons.h>

#include "velaops_approval.h"

#define VELAOPS_BUTTON_DEVICE "/dev/buttons"
#define VELAOPS_BUTTON_POLL_US 50000

static int64_t velaops_monotonic_ms(void)
{
  struct timespec now;

  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    {
      return -1;
    }
  return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

velaops_button_approval_result_t velaops_button_wait_for_long_press(
    unsigned int hold_ms, unsigned int timeout_ms)
{
  velaops_approval_t approval;
  velaops_approval_status_t status;
  btn_buttonset_t supported;
  btn_buttonset_t sample = 0;
  int64_t now_ms;
  int fd;

  fd = open(VELAOPS_BUTTON_DEVICE, O_RDONLY | O_NONBLOCK);
  now_ms = velaops_monotonic_ms();
  if (fd < 0 || now_ms < 0 ||
      ioctl(fd, BTNIOC_SUPPORTED, (unsigned long)(uintptr_t)&supported) < 0 ||
      supported == 0 ||
      velaops_approval_init(&approval, now_ms, hold_ms, timeout_ms) != 0)
    {
      if (fd >= 0)
        {
          close(fd);
        }
      return VELAOPS_BUTTON_ERROR;
    }

  for (;;)
    {
      ssize_t count = read(fd, &sample, sizeof(sample));

      if (count != sizeof(sample) &&
          !(count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)))
        {
          close(fd);
          return VELAOPS_BUTTON_ERROR;
        }
      now_ms = velaops_monotonic_ms();
      status = velaops_approval_sample(
          &approval, now_ms, (sample & supported) != 0);
      if (status == VELAOPS_APPROVAL_GRANTED)
        {
          close(fd);
          return VELAOPS_BUTTON_APPROVED;
        }
      if (status == VELAOPS_APPROVAL_EXPIRED)
        {
          close(fd);
          return VELAOPS_BUTTON_TIMEOUT;
        }
      if (status != VELAOPS_APPROVAL_PENDING)
        {
          close(fd);
          return VELAOPS_BUTTON_ERROR;
        }
      usleep(VELAOPS_BUTTON_POLL_US);
    }
}
