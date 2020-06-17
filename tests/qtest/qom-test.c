/*
 * QTest testcase for QOM
 *
 * Copyright (c) 2013 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qemu-common.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qlist.h"
#include "qemu/cutils.h"
#include "libqtest.h"

static void test_properties(QTestState *qts, const char *path, bool recurse)
{
    char *child_path;
    QDict *response, *tuple, *tmp;
    QList *list;
    QListEntry *entry;

    g_test_message("Obtaining properties of %s", path);
    response = qtest_qmp(qts, "{ 'execute': 'qom-list',"
                              "  'arguments': { 'path': %s } }", path);
    g_assert(response);

    if (!recurse) {
        qobject_unref(response);
        return;
    }

    g_assert(qdict_haskey(response, "return"));
    list = qobject_to(QList, qdict_get(response, "return"));
    QLIST_FOREACH_ENTRY(list, entry) {
        tuple = qobject_to(QDict, qlist_entry_obj(entry));
        bool is_child = strstart(qdict_get_str(tuple, "type"), "child<", NULL);
        bool is_link = strstart(qdict_get_str(tuple, "type"), "link<", NULL);

        if (is_child || is_link) {
            child_path = g_strdup_printf("%s/%s",
                                         path, qdict_get_str(tuple, "name"));
            test_properties(qts, child_path, is_child);
            g_free(child_path);
        } else {
            const char *prop = qdict_get_str(tuple, "name");
            g_test_message("Testing property %s.%s", path, prop);
            tmp = qtest_qmp(qts,
                            "{ 'execute': 'qom-get',"
                            "  'arguments': { 'path': %s, 'property': %s } }",
                            path, prop);
            /* qom-get may fail but should not, e.g., segfault. */
            g_assert(tmp);
            qobject_unref(tmp);
        }
    }
    qobject_unref(response);
}

static void test_machine(gconstpointer data)
{
    const char *machine = data;
    QDict *response;
    QTestState *qts;

    qts = qtest_initf("-machine %s", machine);

    test_properties(qts, "/machine", true);

    response = qtest_qmp(qts, "{ 'execute': 'quit' }");
    g_assert(qdict_haskey(response, "return"));
    qobject_unref(response);

    qtest_quit(qts);
    g_free((void *)machine);
}

static void add_machine_test_case(const char *mname)
{
    char *path;

    path = g_strdup_printf("qom/%s", mname);
    qtest_add_data_func(path, g_strdup(mname), test_machine);
    g_free(path);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_cb_for_every_machine(add_machine_test_case, g_test_quick());

    return g_test_run();
}
