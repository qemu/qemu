/*
 * QTest testcase for the BCM2835 Interrupt Controller
 *
 * Copyright (c) 2026 Bin Guo <guobin@linux.alibaba.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"

#define IC_BASE     0x3f00b200
#define FIQ_CONTROL (IC_BASE + 0x0c)

static void test_fiq_select_out_of_range(void)
{
    uint32_t val;

    /*
     * Only FIQ sources 0..71 exist.  Source 96 used to trigger an assertion
     * in bcm2835_ic_update() because extract32(arm_irq_level, 32, 1) was
     * called with start >= 32.  Make sure the write is rejected and QEMU
     * keeps running.
     */
    writel(FIQ_CONTROL, 0xe0); /* fiq_select = 96, fiq_enable = 1 */
    val = readl(FIQ_CONTROL);
    g_assert_cmpint(val, ==, 0);

    /* The first source past the ARM IRQ range should also be rejected. */
    writel(FIQ_CONTROL, 0xc8); /* fiq_select = 72, fiq_enable = 1 */
    val = readl(FIQ_CONTROL);
    g_assert_cmpint(val, ==, 0);
}

static void test_fiq_select_valid(void)
{
    uint32_t val;

    /* Select the highest valid ARM IRQ source (64 + 7 = 71). */
    writel(FIQ_CONTROL, 0xc7); /* fiq_select = 71, fiq_enable = 1 */
    val = readl(FIQ_CONTROL);
    g_assert_cmpint(val, ==, 0xc7);

    /* Select the highest valid GPU IRQ source. */
    writel(FIQ_CONTROL, 0x3f); /* fiq_select = 63, fiq_enable = 0 */
    val = readl(FIQ_CONTROL);
    g_assert_cmpint(val, ==, 0x3f);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/bcm2835/bcm2835-ic/fiq-select-out-of-range",
                   test_fiq_select_out_of_range);
    qtest_add_func("/bcm2835/bcm2835-ic/fiq-select-valid",
                   test_fiq_select_valid);

    qtest_start("-machine raspi3b");
    ret = g_test_run();
    qtest_end();

    return ret;
}
