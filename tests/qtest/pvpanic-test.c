/*
 * QTest testcase for PV Panic
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qobject/qdict.h"
#include "hw/misc/pvpanic.h"

static void test_panic_nopause(void)
{
    uint8_t val;
    QDict *response, *data;
    QTestState *qts;

    qts = qtest_init("-device pvpanic -action panic=none");

    val = qtest_inb(qts, 0x505);
    g_assert_cmpuint(val, ==, PVPANIC_EVENTS);

    qtest_outb(qts, 0x505, 0x1);

    response = qtest_qmp_eventwait_ref(qts, "GUEST_PANICKED");
    g_assert(qdict_haskey(response, "data"));
    data = qdict_get_qdict(response, "data");
    g_assert(qdict_haskey(data, "action"));
    g_assert_cmpstr(qdict_get_str(data, "action"), ==, "run");
    qobject_unref(response);

    qtest_quit(qts);
}

static void test_panic(void)
{
    uint8_t val;
    QDict *response, *data;
    QTestState *qts;

    qts = qtest_init("-device pvpanic -action panic=pause");

    val = qtest_inb(qts, 0x505);
    g_assert_cmpuint(val, ==, PVPANIC_EVENTS);

    qtest_outb(qts, 0x505, 0x1);

    response = qtest_qmp_eventwait_ref(qts, "GUEST_PANICKED");
    g_assert(qdict_haskey(response, "data"));
    data = qdict_get_qdict(response, "data");
    g_assert(qdict_haskey(data, "action"));
    g_assert_cmpstr(qdict_get_str(data, "action"), ==, "pause");
    qobject_unref(response);

    qtest_quit(qts);
}

static void test_pvshutdown(void)
{
    uint8_t val;
    QDict *response, *data;
    QTestState *qts;

    qts = qtest_init("-device pvpanic");

    val = qtest_inb(qts, 0x505);
    g_assert_cmpuint(val, ==, PVPANIC_EVENTS);

    qtest_outb(qts, 0x505, PVPANIC_SHUTDOWN);

    response = qtest_qmp_eventwait_ref(qts, "GUEST_PVSHUTDOWN");
    qobject_unref(response);

    response = qtest_qmp_eventwait_ref(qts, "SHUTDOWN");
    g_assert(qdict_haskey(response, "data"));
    data = qdict_get_qdict(response, "data");
    g_assert(qdict_haskey(data, "guest"));
    g_assert(qdict_get_bool(data, "guest"));
    g_assert(qdict_haskey(data, "reason"));
    g_assert_cmpstr(qdict_get_str(data, "reason"), ==, "guest-shutdown");
    qobject_unref(response);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/pvpanic/panic", test_panic);
    qtest_add_func("/pvpanic/panic-nopause", test_panic_nopause);
    qtest_add_func("/pvpanic/pvshutdown", test_pvshutdown);

    return g_test_run();
}
