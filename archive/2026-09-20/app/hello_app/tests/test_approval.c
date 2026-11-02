#include <stdio.h>
#include <stdlib.h>

#include "velaops_approval.h"

static int failures;

#define EXPECT(condition)                                                       \
  do                                                                            \
    {                                                                           \
      if (!(condition))                                                         \
        {                                                                       \
          fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
          failures++;                                                           \
        }                                                                       \
    }                                                                           \
  while (0)

static void test_continuous_hold_grants(void)
{
  velaops_approval_t approval;

  EXPECT(velaops_approval_init(&approval, 1000, 2000, 30000) == 0);
  EXPECT(velaops_approval_sample(&approval, 1100, true) ==
         VELAOPS_APPROVAL_PENDING);
  EXPECT(velaops_approval_sample(&approval, 3099, true) ==
         VELAOPS_APPROVAL_PENDING);
  EXPECT(velaops_approval_sample(&approval, 3100, true) ==
         VELAOPS_APPROVAL_GRANTED);
  EXPECT(velaops_approval_sample(&approval, 3200, false) ==
         VELAOPS_APPROVAL_GRANTED);
}

static void test_release_resets_hold(void)
{
  velaops_approval_t approval;

  EXPECT(velaops_approval_init(&approval, 0, 2000, 30000) == 0);
  EXPECT(velaops_approval_sample(&approval, 100, true) ==
         VELAOPS_APPROVAL_PENDING);
  EXPECT(velaops_approval_sample(&approval, 1500, false) ==
         VELAOPS_APPROVAL_PENDING);
  EXPECT(velaops_approval_sample(&approval, 1600, true) ==
         VELAOPS_APPROVAL_PENDING);
  EXPECT(velaops_approval_sample(&approval, 3599, true) ==
         VELAOPS_APPROVAL_PENDING);
  EXPECT(velaops_approval_sample(&approval, 3600, true) ==
         VELAOPS_APPROVAL_GRANTED);
}

static void test_timeout_and_order_fail_closed(void)
{
  velaops_approval_t approval;

  EXPECT(velaops_approval_init(&approval, 100, 2000, 3000) == 0);
  EXPECT(velaops_approval_sample(&approval, 200, true) ==
         VELAOPS_APPROVAL_PENDING);
  EXPECT(velaops_approval_sample(&approval, 199, true) ==
         VELAOPS_APPROVAL_OUT_OF_ORDER);
  EXPECT(velaops_approval_sample(&approval, 3100, true) ==
         VELAOPS_APPROVAL_EXPIRED);
  EXPECT(velaops_approval_sample(&approval, 4000, true) ==
         VELAOPS_APPROVAL_EXPIRED);
}

static void test_invalid_configuration(void)
{
  velaops_approval_t approval;

  EXPECT(velaops_approval_init(NULL, 0, 1, 1) != 0);
  EXPECT(velaops_approval_init(&approval, -1, 1, 1) != 0);
  EXPECT(velaops_approval_init(&approval, 0, 0, 1) != 0);
  EXPECT(velaops_approval_init(&approval, 0, 2, 1) != 0);
  EXPECT(velaops_approval_sample(NULL, 0, false) ==
         VELAOPS_APPROVAL_INVALID_ARGUMENT);
}

int main(void)
{
  test_continuous_hold_grants();
  test_release_resets_hold();
  test_timeout_and_order_fail_closed();
  test_invalid_configuration();
  if (failures != 0)
    {
      return EXIT_FAILURE;
    }
  puts("PASS: VelaOps physical approval tests");
  return EXIT_SUCCESS;
}
