/*
 * QTest testcase for QOM
 *
 * Copyright (c) 2013 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qobject/qdict.h"
#include "qobject/qlist.h"
#include "qemu/cutils.h"
#include "libqtest.h"

static int verbosity_level;

static void test_properties(QTestState *qts, const char *path, bool recurse)
{
    char *child_path;
    QDict *response, *tuple, *tmp;
    QList *list;
    QListEntry *entry;
    GSList *children = NULL, *links = NULL;

    if (verbosity_level >= 2) {
        g_test_message("Obtaining properties of %s", path);
    }
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
            if (is_child) {
                children = g_slist_prepend(children, child_path);
            } else {
                links = g_slist_prepend(links, child_path);
            }
        } else {
            const char *prop = qdict_get_str(tuple, "name");
            if (verbosity_level >= 3) {
                g_test_message("-> %s", prop);
            }
            tmp = qtest_qmp(qts,
                            "{ 'execute': 'qom-get',"
                            "  'arguments': { 'path': %s, 'property': %s } }",
                            path, prop);
            /* qom-get may fail but should not, e.g., segfault. */
            g_assert(tmp);
            qobject_unref(tmp);
        }
    }

    while (links) {
        test_properties(qts, links->data, false);
        g_free(links->data);
        links = g_slist_delete_link(links, links);
    }
    while (children) {
        test_properties(qts, children->data, true);
        g_free(children->data);
        children = g_slist_delete_link(children, children);
    }

    qobject_unref(response);
}

static void test_machine(gconstpointer data)
{
    const char *machine = data;
    QDict *response;
    QTestState *qts;

    qts = qtest_initf("-machine %s", machine);

    if (g_test_slow()) {
        /* Make sure we can get the machine class properties: */
        g_autofree char *qom_machine = g_strdup_printf("%s-machine", machine);

        response = qtest_qmp(qts, "{ 'execute': 'qom-list-properties',"
                                  "  'arguments': { 'typename': %s } }",
                             qom_machine);
        g_assert(response);
        qobject_unref(response);
    }

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
    char *v_env = getenv("V");

    if (v_env) {
        verbosity_level = atoi(v_env);
    }

    g_test_init(&argc, &argv, NULL);

    qtest_cb_for_every_machine(add_machine_test_case, g_test_quick());

    return g_test_run();
}
