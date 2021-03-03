/*
 * QTest testcase for the query-netdev
 *
 * Copyright Yandex N.V., 2019
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include "libqos/libqtest.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qlist.h"

/*
 * Events can get in the way of responses we are actually waiting for.
 */
GCC_FMT_ATTR(2, 3)
static QObject *wait_command(QTestState *who, const char *command, ...)
{
    va_list ap;
    QDict *response;
    QObject *result;

    va_start(ap, command);
    qtest_qmp_vsend(who, command, ap);
    va_end(ap);

    response = qtest_qmp_receive(who);

    result = qdict_get(response, "return");
    g_assert(result);
    qobject_ref(result);
    qobject_unref(response);

    return result;
}

static void qmp_query_netdev_no_error(QTestState *qts, size_t netdevs_count)
{
    QObject *resp;
    QList *netdevs;

    resp = wait_command(qts, "{'execute': 'query-netdev'}");

    netdevs = qobject_to(QList, resp);
    g_assert(netdevs);
    g_assert(qlist_size(netdevs) == netdevs_count);

    qobject_unref(resp);
}

static void test_query_netdev(void)
{
    const char *arch = qtest_get_arch();
    QObject *resp;
    QTestState *state;

    /* Choosing machine for platforms without default one */
    if (g_str_equal(arch, "arm") ||
        g_str_equal(arch, "aarch64")) {
        state = qtest_init(
            "-nodefaults "
            "-M virt "
            "-netdev user,id=slirp0");
    } else if (g_str_equal(arch, "tricore")) {
        state = qtest_init(
            "-nodefaults "
            "-M tricore_testboard "
            "-netdev user,id=slirp0");
    } else if (g_str_equal(arch, "avr")) {
        state = qtest_init(
            "-nodefaults "
            "-M mega2560 "
            "-netdev user,id=slirp0");
    } else if (g_str_equal(arch, "rx")) {
        state = qtest_init(
            "-nodefaults "
            "-M gdbsim-r5f562n8 "
            "-netdev user,id=slirp0");
    } else {
        state = qtest_init(
            "-nodefaults "
            "-netdev user,id=slirp0");
    }
    g_assert(state);

    qmp_query_netdev_no_error(state, 1);

    resp = wait_command(state,
        "{'execute': 'netdev_add', 'arguments': {"
        " 'id': 'slirp1',"
        " 'type': 'user'}}");
    qobject_unref(resp);

    qmp_query_netdev_no_error(state, 2);

    resp = wait_command(state,
        "{'execute': 'netdev_del', 'arguments': {"
        " 'id': 'slirp1'}}");
    qobject_unref(resp);

    qmp_query_netdev_no_error(state, 1);

    qtest_quit(state);
}

int main(int argc, char **argv)
{
    int ret = 0;
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/net/qapi/query_netdev", test_query_netdev);

    ret = g_test_run();

    return ret;
}
