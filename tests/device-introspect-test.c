/*
 * Device introspection test cases
 *
 * Copyright (c) 2015 Red Hat Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>,
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/*
 * Covers QMP device-list-properties and HMP device_add help.  We
 * currently don't check that their output makes sense, only that QEMU
 * survives.  Useful since we've had an astounding number of crash
 * bugs around here.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qapi/qmp/qstring.h"
#include "libqtest.h"

const char common_args[] = "-nodefaults -machine none";

static QList *device_type_list(bool abstract)
{
    QDict *resp;
    QList *ret;

    resp = qmp("{'execute': 'qom-list-types',"
               " 'arguments': {'implements': 'device', 'abstract': %i}}",
               abstract);
    g_assert(qdict_haskey(resp, "return"));
    ret = qdict_get_qlist(resp, "return");
    QINCREF(ret);
    QDECREF(resp);
    return ret;
}

static void test_one_device(const char *type)
{
    QDict *resp;
    char *help, *qom_tree;

    resp = qmp("{'execute': 'device-list-properties',"
               " 'arguments': {'typename': %s}}",
               type);
    QDECREF(resp);

    help = hmp("device_add \"%s,help\"", type);
    g_free(help);

    /*
     * Some devices leave dangling pointers in QOM behind.
     * "info qom-tree" has a good chance at crashing then
     */
    qom_tree = hmp("info qom-tree");
    g_free(qom_tree);
}

static void test_device_intro_list(void)
{
    QList *types;
    char *help;

    qtest_start(common_args);

    types = device_type_list(true);
    QDECREF(types);

    help = hmp("device_add help");
    g_free(help);

    qtest_end();
}

static void test_device_intro_none(void)
{
    qtest_start(common_args);
    test_one_device("nonexistent");
    qtest_end();
}

static void test_device_intro_abstract(void)
{
    qtest_start(common_args);
    test_one_device("device");
    qtest_end();
}

static void test_device_intro_concrete(void)
{
    QList *types;
    QListEntry *entry;
    const char *type;

    qtest_start(common_args);
    types = device_type_list(false);

    QLIST_FOREACH_ENTRY(types, entry) {
        type = qdict_get_try_str(qobject_to_qdict(qlist_entry_obj(entry)),
                                "name");
        g_assert(type);
        test_one_device(type);
    }

    QDECREF(types);
    qtest_end();
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("device/introspect/list", test_device_intro_list);
    qtest_add_func("device/introspect/none", test_device_intro_none);
    qtest_add_func("device/introspect/abstract", test_device_intro_abstract);
    qtest_add_func("device/introspect/concrete", test_device_intro_concrete);

    return g_test_run();
}
