/*
 * SBSA Generic Watchdog Timer (GWDT) testcase
 *
 * Copyright (C) 2026 Red Hat
 *
 * Authors:
 *  Igor Mammedov <imammedo@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/timer.h"
#include "libqtest-single.h"
#include "qobject/qdict.h"

#define GWDT_REFRESH_BASE   0x0f000000
#define GWDT_CONTROL_BASE   0x0f001000
#define GWDT_WRR            0x000
#define GWDT_WCS            0x000
#define GWDT_WORL           0x008
#define GWDT_WORH           0x00C
#define GWDT_WCVL           0x010
#define GWDT_WCVH           0x014
#define GWDT_W_IIDR         0xFCC

#define GWDT_WCS_EN         BIT(0)
#define GWDT_WCS_WS0        BIT(1)
#define GWDT_WCS_WS1        BIT(2)

#define GWDT_ID_VALUE       0x1043B

static uint32_t ctrl_read(uint32_t offset)
{
    return readl(GWDT_CONTROL_BASE + offset);
}

static void ctrl_write(uint32_t offset, uint32_t value)
{
    writel(GWDT_CONTROL_BASE + offset, value);
}

static void refresh_write(uint32_t offset, uint32_t value)
{
    writel(GWDT_REFRESH_BASE + offset, value);
}

static void reset(void)
{
    qtest_system_reset(global_qtest);
}

/* Convert watchdog ticks to nanoseconds */
static int64_t gwdt_ticks(uint32_t ticks)
{
    QDict *resp = qmp("{ 'execute': 'qom-get', 'arguments':"
                      "{ 'path': '/machine/peripheral/gwdt',"
                      "'property': 'clock-frequency' }}");
    uint64_t freq = qdict_get_uint(resp, "return");
    qobject_unref(resp);
    return muldiv64(ticks, NANOSECONDS_PER_SECOND, freq);
}

static void test_identification(void)
{
    g_assert_cmphex(ctrl_read(GWDT_W_IIDR), ==, GWDT_ID_VALUE);
    g_assert_cmphex(readl(GWDT_REFRESH_BASE + GWDT_W_IIDR), ==, GWDT_ID_VALUE);
}

static void test_enable_disable(void)
{
    uint32_t wcs;

    reset();

    ctrl_write(GWDT_WORL, 0);
    ctrl_write(GWDT_WORH, 0xF0000000);

    ctrl_write(GWDT_WCS, GWDT_WCS_EN);
    wcs = ctrl_read(GWDT_WCS);
    g_assert_cmphex(wcs & GWDT_WCS_EN, ==, GWDT_WCS_EN);

    ctrl_write(GWDT_WCS, 0);
    wcs = ctrl_read(GWDT_WCS);
    g_assert_cmphex(wcs & GWDT_WCS_EN, ==, 0);

    /* make sure no timer is armed after disable */
    clock_step_next();
    wcs = ctrl_read(GWDT_WCS);
    g_assert_cmphex(wcs & GWDT_WCS_WS0, ==, 0);
}

static void test_refresh(void)
{
    uint32_t wcs;
    const uint32_t wor = 100000;
    const int64_t timeout = gwdt_ticks(wor);

    reset();

    ctrl_write(GWDT_WORL, wor);
    ctrl_write(GWDT_WORH, 0);
    ctrl_write(GWDT_WCS, GWDT_WCS_EN);

    /* advance to before the deadline */
    clock_step(timeout - 1);
    wcs = ctrl_read(GWDT_WCS);
    g_assert_cmphex(wcs & GWDT_WCS_WS0, ==, 0);

    /* extend deadline and advance now to deadline - 1 */
    refresh_write(GWDT_WRR, 0);
    clock_step(timeout - 1);
    wcs = ctrl_read(GWDT_WCS);
    g_assert_cmphex(wcs & GWDT_WCS_WS0, ==, 0);

    /* make sure that gwdt triggers on refresh deadline */
    clock_step(1);
    wcs = ctrl_read(GWDT_WCS);
    g_assert_cmphex(wcs & GWDT_WCS_WS0, ==, GWDT_WCS_WS0);
}

static void test_wcv_load(void)
{
    uint32_t wcs;
    const uint32_t wor = 10000;
    const int64_t timeout = gwdt_ticks(wor);
    int64_t now, deadline;

    reset();

    ctrl_write(GWDT_WORL, wor);
    ctrl_write(GWDT_WORH, 0);
    ctrl_write(GWDT_WCS, GWDT_WCS_EN);

    /* explicit WCV refresh */
    now = clock_step(1);
    deadline = now + 2 * timeout;
    ctrl_write(GWDT_WCVH, deadline >> 32);
    ctrl_write(GWDT_WCVL, deadline);
    wcs = ctrl_read(GWDT_WCS);
    g_assert_cmphex(wcs & GWDT_WCS_WS0, ==, 0);

    /* initial WOR deadline, no WS0 yet as WCV load pushed it further */
    clock_step(timeout);
    wcs = ctrl_read(GWDT_WCS);
    g_assert_cmphex(wcs & GWDT_WCS_WS0, ==, 0);

    /* right before WCV deadline, no WS0 */
    clock_step(timeout - 1);
    wcs = ctrl_read(GWDT_WCS);
    g_assert_cmphex(wcs & GWDT_WCS_WS0, ==, 0);

    /* advance to WCV deadline, WS0 is triggered */
    clock_step(1);
    wcs = ctrl_read(GWDT_WCS);
    g_assert_cmphex(wcs & GWDT_WCS_WS0, ==, GWDT_WCS_WS0);
}

static void test_ws0_timeout(void)
{
    uint32_t wcs;

    reset();

    ctrl_write(GWDT_WORL, 10000);
    ctrl_write(GWDT_WORH, 0);

    ctrl_write(GWDT_WCS, GWDT_WCS_EN);
    wcs = ctrl_read(GWDT_WCS);
    g_assert_cmphex(wcs & GWDT_WCS_WS0, ==, 0);

    /* advance clock to WS0 deadline */
    clock_step_next();
    wcs = ctrl_read(GWDT_WCS);
    g_assert_cmphex(wcs & GWDT_WCS_WS0, ==, GWDT_WCS_WS0);
}

static void test_ws1_timeout(void)
{
    uint32_t wcs;

    reset();

    ctrl_write(GWDT_WORL, 10000);
    ctrl_write(GWDT_WORH, 0);
    ctrl_write(GWDT_WCS, GWDT_WCS_EN);

    /* WS0 deadline test */
    clock_step_next();
    wcs = ctrl_read(GWDT_WCS);
    g_assert_cmphex(wcs & GWDT_WCS_WS0, ==, GWDT_WCS_WS0);
    g_assert_cmphex(wcs & GWDT_WCS_WS1, ==, 0);

    /* WS1 deadline test */
    clock_step_next();
    wcs = ctrl_read(GWDT_WCS);
    g_assert_cmphex(wcs & GWDT_WCS_WS0, ==, GWDT_WCS_WS0);
    g_assert_cmphex(wcs & GWDT_WCS_WS1, ==, GWDT_WCS_WS1);
}

int main(int argc, char **argv)
{
    int ret;
    const char *arch = qtest_get_arch();

    g_test_init(&argc, &argv, NULL);

    if (strcmp(arch, "aarch64") || !qtest_has_machine("virt")) {
        g_test_skip("'aarch64/virt' machine is not available");
        return 0;
    }

    qtest_add_func("/sbsa-gwdt/identification", test_identification);
    qtest_add_func("/sbsa-gwdt/enable-disable", test_enable_disable);
    qtest_add_func("/sbsa-gwdt/refresh", test_refresh);
    qtest_add_func("/sbsa-gwdt/wcv-load", test_wcv_load);
    qtest_add_func("/sbsa-gwdt/ws0-timeout", test_ws0_timeout);
    qtest_add_func("/sbsa-gwdt/ws1-timeout", test_ws1_timeout);

    qtest_start("-M virt -cpu max -device sbsa-gwdt,id=gwdt "
                "-watchdog-action none");
    ret = g_test_run();
    qtest_end();

    return ret;
}
