/*
 * QTest testcase for netfilter
 *
 * Copyright (c) 2015 FUJITSU LIMITED
 * Author: Yang Hongyang <yanghy@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"

/* add a netfilter to a netdev and then remove it */
static void add_one_netfilter(void)
{
    QDict *response;

    response = qmp("{'execute': 'object-add',"
                   " 'arguments': {"
                   "   'qom-type': 'filter-buffer',"
                   "   'id': 'qtest-f0',"
                   "   'props': {"
                   "     'netdev': 'qtest-bn0',"
                   "     'queue': 'rx',"
                   "     'interval': 1000"
                   "}}}");

    g_assert(response);
    g_assert(!qdict_haskey(response, "error"));
    QDECREF(response);

    response = qmp("{'execute': 'object-del',"
                   " 'arguments': {"
                   "   'id': 'qtest-f0'"
                   "}}");
    g_assert(response);
    g_assert(!qdict_haskey(response, "error"));
    QDECREF(response);
}

/* add a netfilter to a netdev and then remove the netdev */
static void remove_netdev_with_one_netfilter(void)
{
    QDict *response;

    response = qmp("{'execute': 'object-add',"
                   " 'arguments': {"
                   "   'qom-type': 'filter-buffer',"
                   "   'id': 'qtest-f0',"
                   "   'props': {"
                   "     'netdev': 'qtest-bn0',"
                   "     'queue': 'rx',"
                   "     'interval': 1000"
                   "}}}");

    g_assert(response);
    g_assert(!qdict_haskey(response, "error"));
    QDECREF(response);

    response = qmp("{'execute': 'netdev_del',"
                   " 'arguments': {"
                   "   'id': 'qtest-bn0'"
                   "}}");
    g_assert(response);
    g_assert(!qdict_haskey(response, "error"));
    QDECREF(response);

    /* add back the netdev */
    response = qmp("{'execute': 'netdev_add',"
                   " 'arguments': {"
                   "   'type': 'user',"
                   "   'id': 'qtest-bn0'"
                   "}}");
    g_assert(response);
    g_assert(!qdict_haskey(response, "error"));
    QDECREF(response);
}

/* add multi(2) netfilters to a netdev and then remove them */
static void add_multi_netfilter(void)
{
    QDict *response;

    response = qmp("{'execute': 'object-add',"
                   " 'arguments': {"
                   "   'qom-type': 'filter-buffer',"
                   "   'id': 'qtest-f0',"
                   "   'props': {"
                   "     'netdev': 'qtest-bn0',"
                   "     'queue': 'rx',"
                   "     'interval': 1000"
                   "}}}");

    g_assert(response);
    g_assert(!qdict_haskey(response, "error"));
    QDECREF(response);

    response = qmp("{'execute': 'object-add',"
                   " 'arguments': {"
                   "   'qom-type': 'filter-buffer',"
                   "   'id': 'qtest-f1',"
                   "   'props': {"
                   "     'netdev': 'qtest-bn0',"
                   "     'queue': 'rx',"
                   "     'interval': 1000"
                   "}}}");

    g_assert(response);
    g_assert(!qdict_haskey(response, "error"));
    QDECREF(response);

    response = qmp("{'execute': 'object-del',"
                   " 'arguments': {"
                   "   'id': 'qtest-f0'"
                   "}}");
    g_assert(response);
    g_assert(!qdict_haskey(response, "error"));
    QDECREF(response);

    response = qmp("{'execute': 'object-del',"
                   " 'arguments': {"
                   "   'id': 'qtest-f1'"
                   "}}");
    g_assert(response);
    g_assert(!qdict_haskey(response, "error"));
    QDECREF(response);
}

/* add multi(2) netfilters to a netdev and then remove the netdev */
static void remove_netdev_with_multi_netfilter(void)
{
    QDict *response;

    response = qmp("{'execute': 'object-add',"
                   " 'arguments': {"
                   "   'qom-type': 'filter-buffer',"
                   "   'id': 'qtest-f0',"
                   "   'props': {"
                   "     'netdev': 'qtest-bn0',"
                   "     'queue': 'rx',"
                   "     'interval': 1000"
                   "}}}");

    g_assert(response);
    g_assert(!qdict_haskey(response, "error"));
    QDECREF(response);

    response = qmp("{'execute': 'object-add',"
                   " 'arguments': {"
                   "   'qom-type': 'filter-buffer',"
                   "   'id': 'qtest-f1',"
                   "   'props': {"
                   "     'netdev': 'qtest-bn0',"
                   "     'queue': 'rx',"
                   "     'interval': 1000"
                   "}}}");

    g_assert(response);
    g_assert(!qdict_haskey(response, "error"));
    QDECREF(response);

    response = qmp("{'execute': 'netdev_del',"
                   " 'arguments': {"
                   "   'id': 'qtest-bn0'"
                   "}}");
    g_assert(response);
    g_assert(!qdict_haskey(response, "error"));
    QDECREF(response);

    /* add back the netdev */
    response = qmp("{'execute': 'netdev_add',"
                   " 'arguments': {"
                   "   'type': 'user',"
                   "   'id': 'qtest-bn0'"
                   "}}");
    g_assert(response);
    g_assert(!qdict_haskey(response, "error"));
    QDECREF(response);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/netfilter/addremove_one", add_one_netfilter);
    qtest_add_func("/netfilter/remove_netdev_one",
                   remove_netdev_with_one_netfilter);
    qtest_add_func("/netfilter/addremove_multi", add_multi_netfilter);
    qtest_add_func("/netfilter/remove_netdev_multi",
                   remove_netdev_with_multi_netfilter);

    qtest_start("-netdev user,id=qtest-bn0 -device e1000,netdev=qtest-bn0");
    ret = g_test_run();

    qtest_end();

    return ret;
}
