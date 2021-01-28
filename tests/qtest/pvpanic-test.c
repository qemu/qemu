/*
 * QTest testcase for PV Panic
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqos/libqtest.h"
#include "qapi/qmp/qdict.h"

static void test_panic_nopause(void)
{
    uint8_t val;
    QDict *response, *data;
    QTestState *qts;

    qts = qtest_init("-device pvpanic -action panic=none");

    val = qtest_inb(qts, 0x505);
    g_assert_cmpuint(val, ==, 3);

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
    g_assert_cmpuint(val, ==, 3);

    qtest_outb(qts, 0x505, 0x1);

    response = qtest_qmp_eventwait_ref(qts, "GUEST_PANICKED");
    g_assert(qdict_haskey(response, "data"));
    data = qdict_get_qdict(response, "data");
    g_assert(qdict_haskey(data, "action"));
    g_assert_cmpstr(qdict_get_str(data, "action"), ==, "pause");
    qobject_unref(response);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/pvpanic/panic", test_panic);
    qtest_add_func("/pvpanic/panic-nopause", test_panic_nopause);

    ret = g_test_run();

    return ret;
}
