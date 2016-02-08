/*
 * QTest testcase for the IB700 watchdog
 *
 * Copyright (c) 2014 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib.h>
#include "libqtest.h"
#include "qemu/timer.h"

static void qmp_check_no_event(void)
{
    QDict *resp = qmp("{'execute':'query-status'}");
    g_assert(qdict_haskey(resp, "return"));
    QDECREF(resp);
}

static QDict *qmp_get_event(const char *name)
{
    QDict *event = qmp("");
    QDict *data;
    g_assert(qdict_haskey(event, "event"));
    g_assert(!strcmp(qdict_get_str(event, "event"), name));

    if (qdict_haskey(event, "data")) {
        data = qdict_get_qdict(event, "data");
        QINCREF(data);
    } else {
        data = NULL;
    }

    QDECREF(event);
    return data;
}

static QDict *ib700_program_and_wait(QTestState *s)
{
    clock_step(NANOSECONDS_PER_SECOND * 40);
    qmp_check_no_event();

    /* 2 second limit */
    outb(0x443, 14);

    /* Ping */
    clock_step(NANOSECONDS_PER_SECOND);
    qmp_check_no_event();
    outb(0x443, 14);

    /* Disable */
    clock_step(NANOSECONDS_PER_SECOND);
    qmp_check_no_event();
    outb(0x441, 1);
    clock_step(3 * NANOSECONDS_PER_SECOND);
    qmp_check_no_event();

    /* Enable and let it fire */
    outb(0x443, 13);
    clock_step(3 * NANOSECONDS_PER_SECOND);
    qmp_check_no_event();
    clock_step(2 * NANOSECONDS_PER_SECOND);
    return qmp_get_event("WATCHDOG");
}


static void ib700_pause(void)
{
    QDict *d;
    QTestState *s = qtest_start("-watchdog-action pause -device ib700");
    qtest_irq_intercept_in(s, "ioapic");
    d = ib700_program_and_wait(s);
    g_assert(!strcmp(qdict_get_str(d, "action"), "pause"));
    QDECREF(d);
    d = qmp_get_event("STOP");
    QDECREF(d);
    qtest_end();
}

static void ib700_reset(void)
{
    QDict *d;
    QTestState *s = qtest_start("-watchdog-action reset -device ib700");
    qtest_irq_intercept_in(s, "ioapic");
    d = ib700_program_and_wait(s);
    g_assert(!strcmp(qdict_get_str(d, "action"), "reset"));
    QDECREF(d);
    d = qmp_get_event("RESET");
    QDECREF(d);
    qtest_end();
}

static void ib700_shutdown(void)
{
    QDict *d;
    QTestState *s = qtest_start("-watchdog-action reset -no-reboot -device ib700");
    qtest_irq_intercept_in(s, "ioapic");
    d = ib700_program_and_wait(s);
    g_assert(!strcmp(qdict_get_str(d, "action"), "reset"));
    QDECREF(d);
    d = qmp_get_event("SHUTDOWN");
    QDECREF(d);
    qtest_end();
}

static void ib700_none(void)
{
    QDict *d;
    QTestState *s = qtest_start("-watchdog-action none -device ib700");
    qtest_irq_intercept_in(s, "ioapic");
    d = ib700_program_and_wait(s);
    g_assert(!strcmp(qdict_get_str(d, "action"), "none"));
    QDECREF(d);
    qtest_end();
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/wdt_ib700/pause", ib700_pause);
    qtest_add_func("/wdt_ib700/reset", ib700_reset);
    qtest_add_func("/wdt_ib700/shutdown", ib700_shutdown);
    qtest_add_func("/wdt_ib700/none", ib700_none);

    ret = g_test_run();

    return ret;
}
