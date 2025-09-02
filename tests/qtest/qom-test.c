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
#include "qobject/qstring.h"
#include "qemu/cutils.h"
#include "libqtest.h"

#define RAM_NAME "node0"
#define RAM_SIZE 65536

static int verbosity_level;

/*
 * Verify that the /object/RAM_NAME 'size' property is RAM_SIZE.
 */
static void test_list_get_value(QTestState *qts)
{
    QDict *args = qdict_new();
    g_autoptr(QDict) response = NULL;
    g_autoptr(QList) paths = qlist_new();
    QListEntry *entry, *prop_entry;
    const char *prop_name;
    QList *properties, *return_list;
    QDict *obj;

    qlist_append_str(paths, "/objects/" RAM_NAME);
    qdict_put_obj(args, "paths", QOBJECT(qlist_copy(paths)));
    response = qtest_qmp(qts, "{ 'execute': 'qom-list-get',"
                              "  'arguments': %p }", args);
    g_assert(response);
    g_assert(qdict_haskey(response, "return"));
    return_list = qobject_to(QList, qdict_get(response, "return"));

    entry = QTAILQ_FIRST(&return_list->head);
    obj = qobject_to(QDict, qlist_entry_obj(entry));
    g_assert(qdict_haskey(obj, "properties"));
    properties = qobject_to(QList, qdict_get(obj, "properties"));

    QLIST_FOREACH_ENTRY(properties, prop_entry) {
        QDict *prop = qobject_to(QDict, qlist_entry_obj(prop_entry));

        g_assert(qdict_haskey(prop, "name"));
        g_assert(qdict_haskey(prop, "value"));

        prop_name = qdict_get_str(prop, "name");
        if (!strcmp(prop_name, "type")) {
            g_assert_cmpstr(qdict_get_str(prop, "value"), ==,
                            "memory-backend-ram");

        } else if (!strcmp(prop_name, "size")) {
            g_assert_cmpint(qdict_get_int(prop, "value"), ==, RAM_SIZE);
        }
    }
}

static void test_list_get(QTestState *qts, QList *paths)
{
    QListEntry *entry, *prop_entry, *path_entry;
    g_autoptr(QDict) response = NULL;
    QDict *args = qdict_new();
    QDict *prop;
    QList *return_list;

    if (verbosity_level >= 2) {
        g_test_message("Obtaining properties for paths:");
        QLIST_FOREACH_ENTRY(paths, path_entry) {
            QString *qstr = qobject_to(QString, qlist_entry_obj(path_entry));
            g_test_message("  %s", qstring_get_str(qstr));
        }
    }

    qdict_put_obj(args, "paths", QOBJECT(qlist_copy(paths)));
    response = qtest_qmp(qts, "{ 'execute': 'qom-list-get',"
                              "  'arguments': %p }", args);
    g_assert(response);
    g_assert(qdict_haskey(response, "return"));
    return_list = qobject_to(QList, qdict_get(response, "return"));
    g_assert(!qlist_empty(return_list));

    path_entry = QTAILQ_FIRST(&paths->head);
    QLIST_FOREACH_ENTRY(return_list, entry) {
        QDict *obj = qobject_to(QDict, qlist_entry_obj(entry));
        g_assert(qdict_haskey(obj, "properties"));
        QList *properties = qobject_to(QList, qdict_get(obj, "properties"));
        bool has_child = false;

        QLIST_FOREACH_ENTRY(properties, prop_entry) {
            prop = qobject_to(QDict, qlist_entry_obj(prop_entry));
            g_assert(qdict_haskey(prop, "name"));
            g_assert(qdict_haskey(prop, "type"));
            has_child |= strstart(qdict_get_str(prop, "type"), "child<", NULL);
        }

        if (has_child) {
            /* build a list of child paths */
            QString *qstr = qobject_to(QString, qlist_entry_obj(path_entry));
            const char *path = qstring_get_str(qstr);
            g_autoptr(QList) child_paths = qlist_new();

            QLIST_FOREACH_ENTRY(properties, prop_entry) {
                prop = qobject_to(QDict, qlist_entry_obj(prop_entry));
                if (strstart(qdict_get_str(prop, "type"), "child<", NULL)) {
                    g_autofree char *child_path = g_strdup_printf(
                        "%s/%s", path, qdict_get_str(prop, "name"));
                    qlist_append_str(child_paths, child_path);
                }
            }

            /* fetch props for all children with one qom-list-get call */
            test_list_get(qts, child_paths);
        }

        path_entry = QTAILQ_NEXT(path_entry, next);
    }
}

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
        test_properties(qts, children->data, g_test_slow());
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
    g_autoptr(QList) paths = qlist_new();

    qts = qtest_initf("-machine %s -object memory-backend-ram,id=%s,size=%d",
                      machine, RAM_NAME, RAM_SIZE);

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

    qlist_append_str(paths, "/");
    test_list_get(qts, paths);
    test_list_get_value(qts);

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
