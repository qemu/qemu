/*
 * QTests for Nuvoton NPCM7xx Timer Watchdog Modules.
 *
 * Copyright 2020 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"

#include "libqtest.h"
#include "qobject/qdict.h"

#define WTCR_OFFSET     0x1c
#define REF_HZ          (25000000)

/* WTCR bit fields */
#define WTCLK(rv)       ((rv) << 10)
#define WTE             BIT(7)
#define WTIE            BIT(6)
#define WTIS(rv)        ((rv) << 4)
#define WTIF            BIT(3)
#define WTRF            BIT(2)
#define WTRE            BIT(1)
#define WTR             BIT(0)

typedef struct Watchdog {
    int irq;
    uint64_t base_addr;
} Watchdog;

static const Watchdog watchdog_list[] = {
    {
        .irq        = 47,
        .base_addr  = 0xf0008000
    },
    {
        .irq        = 48,
        .base_addr  = 0xf0009000
    },
    {
        .irq        = 49,
        .base_addr  = 0xf000a000
    }
};

static int watchdog_index(const Watchdog *wd)
{
    ptrdiff_t diff = wd - watchdog_list;

    g_assert(diff >= 0 && diff < ARRAY_SIZE(watchdog_list));

    return diff;
}

static uint32_t watchdog_read_wtcr(QTestState *qts, const Watchdog *wd)
{
    return qtest_readl(qts, wd->base_addr + WTCR_OFFSET);
}

static void watchdog_write_wtcr(QTestState *qts, const Watchdog *wd,
        uint32_t value)
{
    qtest_writel(qts, wd->base_addr + WTCR_OFFSET, value);
}

static uint32_t watchdog_prescaler(QTestState *qts, const Watchdog *wd)
{
    switch (extract32(watchdog_read_wtcr(qts, wd), 10, 2)) {
    case 0:
        return 1;
    case 1:
        return 256;
    case 2:
        return 2048;
    case 3:
        return 65536;
    default:
        g_assert_not_reached();
    }
}

static QDict *get_watchdog_action(QTestState *qts)
{
    QDict *ev = qtest_qmp_eventwait_ref(qts, "WATCHDOG");
    QDict *data;

    data = qdict_get_qdict(ev, "data");
    qobject_ref(data);
    qobject_unref(ev);
    return data;
}

#define RESET_CYCLES 1024
static uint32_t watchdog_interrupt_cycles(QTestState *qts, const Watchdog *wd)
{
    uint32_t wtis = extract32(watchdog_read_wtcr(qts, wd), 4, 2);
    return 1 << (14 + 2 * wtis);
}

static int64_t watchdog_calculate_steps(uint32_t count, uint32_t prescale)
{
    return (NANOSECONDS_PER_SECOND / REF_HZ) * count * prescale;
}

static int64_t watchdog_interrupt_steps(QTestState *qts, const Watchdog *wd)
{
    return watchdog_calculate_steps(watchdog_interrupt_cycles(qts, wd),
            watchdog_prescaler(qts, wd));
}

/* Check wtcr can be reset to default value */
static void test_init(gconstpointer watchdog)
{
    const Watchdog *wd = watchdog;
    QTestState *qts = qtest_init("-machine quanta-gsj");

    qtest_irq_intercept_in(qts, "/machine/soc/a9mpcore/gic");

    watchdog_write_wtcr(qts, wd, WTCLK(1) | WTRF | WTIF | WTR);
    g_assert_cmphex(watchdog_read_wtcr(qts, wd), ==, WTCLK(1));

    qtest_quit(qts);
}

/* Check a watchdog can generate interrupt and reset actions */
static void test_reset_action(gconstpointer watchdog)
{
    const Watchdog *wd = watchdog;
    QTestState *qts = qtest_init("-machine quanta-gsj");
    QDict *ad;

    qtest_irq_intercept_in(qts, "/machine/soc/a9mpcore/gic");

    watchdog_write_wtcr(qts, wd,
            WTCLK(0) | WTE | WTRF | WTRE | WTIF | WTIE | WTR);
    g_assert_cmphex(watchdog_read_wtcr(qts, wd), ==,
            WTCLK(0) | WTE | WTRE | WTIE);

    /* Check a watchdog can generate an interrupt */
    qtest_clock_step(qts, watchdog_interrupt_steps(qts, wd));
    g_assert_cmphex(watchdog_read_wtcr(qts, wd), ==,
            WTCLK(0) | WTE | WTIF | WTIE | WTRE);
    g_assert_true(qtest_get_irq(qts, wd->irq));

    /* Check a watchdog can generate a reset signal */
    qtest_clock_step(qts, watchdog_calculate_steps(RESET_CYCLES,
                watchdog_prescaler(qts, wd)));
    ad = get_watchdog_action(qts);
    /* The signal is a reset signal */
    g_assert_false(strcmp(qdict_get_str(ad, "action"), "reset"));
    qobject_unref(ad);
    qtest_qmp_eventwait(qts, "RESET");
    /*
     * Make sure WTCR is reset to default except for WTRF bit which shouldn't
     * be reset.
     */
    g_assert_cmphex(watchdog_read_wtcr(qts, wd), ==, WTCLK(1) | WTRF);
    qtest_quit(qts);
}

/* Check a watchdog works with all possible WTCLK prescalers and WTIS cycles */
static void test_prescaler(gconstpointer watchdog)
{
    const Watchdog *wd = watchdog;
    int inc = g_test_quick() ? 3 : 1;

    for (int wtclk = 0; wtclk < 4; wtclk += inc) {
        for (int wtis = 0; wtis < 4; wtis += inc) {
            QTestState *qts = qtest_init("-machine quanta-gsj");

            qtest_irq_intercept_in(qts, "/machine/soc/a9mpcore/gic");
            watchdog_write_wtcr(qts, wd,
                    WTCLK(wtclk) | WTE | WTIF | WTIS(wtis) | WTIE | WTR);
            /*
             * The interrupt doesn't fire until watchdog_interrupt_steps()
             * cycles passed
             */
            qtest_clock_step(qts, watchdog_interrupt_steps(qts, wd) - 1);
            g_assert_false(watchdog_read_wtcr(qts, wd) & WTIF);
            g_assert_false(qtest_get_irq(qts, wd->irq));
            qtest_clock_step(qts, 1);
            g_assert_true(watchdog_read_wtcr(qts, wd) & WTIF);
            g_assert_true(qtest_get_irq(qts, wd->irq));

            qtest_quit(qts);
        }
    }
}

/*
 * Check a watchdog doesn't fire if corresponding flags (WTIE and WTRE) are not
 * set.
 */
static void test_enabling_flags(gconstpointer watchdog)
{
    const Watchdog *wd = watchdog;
    QTestState *qts;
    QDict *rsp;

    /* Neither WTIE or WTRE is set, no interrupt or reset should happen */
    qts = qtest_init("-machine quanta-gsj");
    qtest_irq_intercept_in(qts, "/machine/soc/a9mpcore/gic");
    watchdog_write_wtcr(qts, wd, WTCLK(0) | WTE | WTIF | WTRF | WTR);
    qtest_clock_step(qts, watchdog_interrupt_steps(qts, wd));
    g_assert_true(watchdog_read_wtcr(qts, wd) & WTIF);
    g_assert_false(qtest_get_irq(qts, wd->irq));
    qtest_clock_step(qts, watchdog_calculate_steps(RESET_CYCLES,
                watchdog_prescaler(qts, wd)));
    g_assert_true(watchdog_read_wtcr(qts, wd) & WTIF);
    g_assert_false(watchdog_read_wtcr(qts, wd) & WTRF);
    qtest_quit(qts);

    /* Only WTIE is set, interrupt is triggered but reset should not happen */
    qts = qtest_init("-machine quanta-gsj");
    qtest_irq_intercept_in(qts, "/machine/soc/a9mpcore/gic");
    watchdog_write_wtcr(qts, wd, WTCLK(0) | WTE | WTIF | WTIE | WTRF | WTR);
    qtest_clock_step(qts, watchdog_interrupt_steps(qts, wd));
    g_assert_true(watchdog_read_wtcr(qts, wd) & WTIF);
    g_assert_true(qtest_get_irq(qts, wd->irq));
    qtest_clock_step(qts, watchdog_calculate_steps(RESET_CYCLES,
                watchdog_prescaler(qts, wd)));
    g_assert_true(watchdog_read_wtcr(qts, wd) & WTIF);
    g_assert_false(watchdog_read_wtcr(qts, wd) & WTRF);
    qtest_quit(qts);

    /* Only WTRE is set, interrupt is triggered but reset should not happen */
    qts = qtest_init("-machine quanta-gsj");
    qtest_irq_intercept_in(qts, "/machine/soc/a9mpcore/gic");
    watchdog_write_wtcr(qts, wd, WTCLK(0) | WTE | WTIF | WTRE | WTRF | WTR);
    qtest_clock_step(qts, watchdog_interrupt_steps(qts, wd));
    g_assert_true(watchdog_read_wtcr(qts, wd) & WTIF);
    g_assert_false(qtest_get_irq(qts, wd->irq));
    qtest_clock_step(qts, watchdog_calculate_steps(RESET_CYCLES,
                watchdog_prescaler(qts, wd)));
    rsp = get_watchdog_action(qts);
    g_assert_false(strcmp(qdict_get_str(rsp, "action"), "reset"));
    qobject_unref(rsp);
    qtest_qmp_eventwait(qts, "RESET");
    qtest_quit(qts);

    /*
     * The case when both flags are set is already tested in
     * test_reset_action().
     */
}

/* Check a watchdog can pause and resume by setting WTE bits */
static void test_pause(gconstpointer watchdog)
{
    const Watchdog *wd = watchdog;
    QTestState *qts;
    int64_t remaining_steps, steps;

    qts = qtest_init("-machine quanta-gsj");
    qtest_irq_intercept_in(qts, "/machine/soc/a9mpcore/gic");
    watchdog_write_wtcr(qts, wd, WTCLK(0) | WTE | WTIF | WTIE | WTRF | WTR);
    remaining_steps = watchdog_interrupt_steps(qts, wd);
    g_assert_cmphex(watchdog_read_wtcr(qts, wd), ==, WTCLK(0) | WTE | WTIE);

    /* Run for half of the execution period. */
    steps = remaining_steps / 2;
    remaining_steps -= steps;
    qtest_clock_step(qts, steps);

    /* Pause the watchdog */
    watchdog_write_wtcr(qts, wd, WTCLK(0) | WTIE);
    g_assert_cmphex(watchdog_read_wtcr(qts, wd), ==, WTCLK(0) | WTIE);

    /* Run for a long period of time, the watchdog shouldn't fire */
    qtest_clock_step(qts, steps << 4);
    g_assert_cmphex(watchdog_read_wtcr(qts, wd), ==, WTCLK(0) | WTIE);
    g_assert_false(qtest_get_irq(qts, wd->irq));

    /* Resume the watchdog */
    watchdog_write_wtcr(qts, wd, WTCLK(0) | WTE | WTIE);
    g_assert_cmphex(watchdog_read_wtcr(qts, wd), ==, WTCLK(0) | WTE | WTIE);

    /* Run for the reset of the execution period, the watchdog should fire */
    qtest_clock_step(qts, remaining_steps);
    g_assert_cmphex(watchdog_read_wtcr(qts, wd), ==,
            WTCLK(0) | WTE | WTIF | WTIE);
    g_assert_true(qtest_get_irq(qts, wd->irq));

    qtest_quit(qts);
}

static void watchdog_add_test(const char *name, const Watchdog* wd,
        GTestDataFunc fn)
{
    g_autofree char *full_name = g_strdup_printf(
            "npcm7xx_watchdog_timer[%d]/%s", watchdog_index(wd), name);
    qtest_add_data_func(full_name, wd, fn);
}
#define add_test(name, td) watchdog_add_test(#name, td, test_##name)

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_set_nonfatal_assertions();

    for (int i = 0; i < ARRAY_SIZE(watchdog_list); ++i) {
        const Watchdog *wd = &watchdog_list[i];

        add_test(init, wd);
        add_test(reset_action, wd);
        add_test(prescaler, wd);
        add_test(enabling_flags, wd);
        add_test(pause, wd);
    }

    return g_test_run();
}
