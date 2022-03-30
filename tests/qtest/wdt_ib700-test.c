/*
 * QTest testcase for the IB700 watchdog
 *
 * Copyright (c) 2014 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qapi/qmp/qdict.h"
#include "qemu/timer.h"

static void qmp_check_no_event(QTestState *s)
{
    QDict *resp = qtest_qmp(s, "{'execute':'query-status'}");
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);
}

static QDict *ib700_program_and_wait(QTestState *s)
{
    QDict *event, *data;

    qtest_clock_step(s, NANOSECONDS_PER_SECOND * 40);
    qmp_check_no_event(s);

    /* 2 second limit */
    qtest_outb(s, 0x443, 14);

    /* Ping */
    qtest_clock_step(s, NANOSECONDS_PER_SECOND);
    qmp_check_no_event(s);
    qtest_outb(s, 0x443, 14);

    /* Disable */
    qtest_clock_step(s, NANOSECONDS_PER_SECOND);
    qmp_check_no_event(s);
    qtest_outb(s, 0x441, 1);
    qtest_clock_step(s, 3 * NANOSECONDS_PER_SECOND);
    qmp_check_no_event(s);

    /* Enable and let it fire */
    qtest_outb(s, 0x443, 13);
    qtest_clock_step(s, 3 * NANOSECONDS_PER_SECOND);
    qmp_check_no_event(s);
    qtest_clock_step(s, 2 * NANOSECONDS_PER_SECOND);
    event = qtest_qmp_eventwait_ref(s, "WATCHDOG");
    data = qdict_get_qdict(event, "data");
    qobject_ref(data);
    qobject_unref(event);
    return data;
}


static void ib700_pause(void)
{
    QDict *d;
    QTestState *s = qtest_init("-watchdog-action pause -device ib700");

    qtest_irq_intercept_in(s, "ioapic");
    d = ib700_program_and_wait(s);
    g_assert(!strcmp(qdict_get_str(d, "action"), "pause"));
    qobject_unref(d);
    qtest_qmp_eventwait(s, "STOP");
    qtest_quit(s);
}

static void ib700_reset(void)
{
    QDict *d;
    QTestState *s = qtest_init("-watchdog-action reset -device ib700");

    qtest_irq_intercept_in(s, "ioapic");
    d = ib700_program_and_wait(s);
    g_assert(!strcmp(qdict_get_str(d, "action"), "reset"));
    qobject_unref(d);
    qtest_qmp_eventwait(s, "RESET");
    qtest_quit(s);
}

static void ib700_shutdown(void)
{
    QDict *d;
    QTestState *s;

    s = qtest_init("-watchdog-action reset -no-reboot -device ib700");
    qtest_irq_intercept_in(s, "ioapic");
    d = ib700_program_and_wait(s);
    g_assert(!strcmp(qdict_get_str(d, "action"), "reset"));
    qobject_unref(d);
    qtest_qmp_eventwait(s, "SHUTDOWN");
    qtest_quit(s);
}

static void ib700_none(void)
{
    QDict *d;
    QTestState *s = qtest_init("-watchdog-action none -device ib700");

    qtest_irq_intercept_in(s, "ioapic");
    d = ib700_program_and_wait(s);
    g_assert(!strcmp(qdict_get_str(d, "action"), "none"));
    qobject_unref(d);
    qtest_quit(s);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/wdt_ib700/pause", ib700_pause);
    qtest_add_func("/wdt_ib700/reset", ib700_reset);
    qtest_add_func("/wdt_ib700/shutdown", ib700_shutdown);
    qtest_add_func("/wdt_ib700/none", ib700_none);

    return g_test_run();
}
