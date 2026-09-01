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
 * Covers QMP device-list-properties and HMP device_add help (if
 * CONFIG_HMP).  We currently don't check that the output makes sense,
 * only that QEMU survives.  Useful since we've had an astounding
 * number of crash bugs around here.
 */

#include "qemu/osdep.h"
#include "qobject/qstring.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"
#include "libqtest.h"

const char common_args[] = "-nodefaults -machine none";

static QList *qom_list_types(QTestState * qts, const char *implements,
                             bool abstract)
{
    QDict *resp;
    QList *ret;
    QDict *args = qdict_new();

    qdict_put_bool(args, "abstract", abstract);
    if (implements) {
        qdict_put_str(args, "implements", implements);
    }
    resp = qtest_qmp(qts, "{'execute': 'qom-list-types', 'arguments': %p }",
                     args);
    g_assert(qdict_haskey(resp, "return"));
    ret = qdict_get_qlist(resp, "return");
    qobject_ref(ret);
    qobject_unref(resp);
    return ret;
}

/* Build a name -> ObjectTypeInfo index from a ObjectTypeInfo list */
static QDict *qom_type_index(QList *types)
{
    QDict *index = qdict_new();
    QListEntry *e;

    QLIST_FOREACH_ENTRY(types, e) {
        QDict *d = qobject_to(QDict, qlist_entry_obj(e));
        const char *name = qdict_get_str(d, "name");
        qobject_ref(d);
        qdict_put(index, name, d);
    }
    return index;
}

/* Check if @parent is present in the parent chain of @type */
static bool qom_has_parent(QDict *index, const char *type, const char *parent)
{
    while (type) {
        QDict *d = qdict_get_qdict(index, type);
        const char *p = d && qdict_haskey(d, "parent") ?
                        qdict_get_str(d, "parent") :
                        NULL;

        if (!strcmp(type, parent)) {
            return true;
        }

        type = p;
    }

    return false;
}

/* Find an entry on a list returned by qom-list-types */
static QDict *type_list_find(QList *types, const char *name)
{
    QListEntry *e;

    QLIST_FOREACH_ENTRY(types, e) {
        QDict *d = qobject_to(QDict, qlist_entry_obj(e));
        const char *ename = qdict_get_str(d, "name");
        if (!strcmp(ename, name)) {
            return d;
        }
    }

    return NULL;
}

static QList *device_type_list(QTestState *qts, bool abstract)
{
    return qom_list_types(qts, "device", abstract);
}

/*
 * Recursively walk the QOM composition tree via qom-list and build a
 * string representation.  This serves two purposes: detecting dangling
 * pointers (qom-list would crash QEMU) and detecting leaked objects
 * (by comparing the output before and after device introspection).
 */
static void qom_tree_walk(QTestState *qts, const char *path, GString *result)
{
    QDict *resp;
    QList *list;
    QListEntry *e;
    GList *children = NULL;

    resp = qtest_qmp(qts, "{'execute': 'qom-list',"
                     " 'arguments': {'path': %s}}", path);
    g_assert(qdict_haskey(resp, "return"));
    list = qdict_get_qlist(resp, "return");

    QLIST_FOREACH_ENTRY(list, e) {
        QDict *prop = qobject_to(QDict, qlist_entry_obj(e));
        const char *type = qdict_get_str(prop, "type");
        if (g_str_has_prefix(type, "child<")) {
            const char *name = qdict_get_str(prop, "name");
            children = g_list_prepend(children, g_strdup(name));
        }
    }

    children = g_list_sort_with_data(children, (GCompareDataFunc)g_strcmp0,
                                     NULL);

    for (GList *l = children; l; l = l->next) {
        const char *name = l->data;
        g_autofree char *child_path = (!strcmp(path, "/"))
            ? g_strdup_printf("/%s", name)
            : g_strdup_printf("%s/%s", path, name);

        g_string_append_printf(result, "%s\n", child_path);
        qom_tree_walk(qts, child_path, result);
    }

    g_list_free_full(children, g_free);
    qobject_unref(resp);
}

static char *qom_tree_str(QTestState *qts)
{
    GString *result = g_string_new("");
    qom_tree_walk(qts, "/", result);
    return g_string_free(result, FALSE);
}

static void test_one_device(QTestState *qts, const char *type)
{
    QDict *resp;

    g_test_message("Testing device '%s'", type);

    resp = qtest_qmp(qts, "{'execute': 'device-list-properties',"
                          " 'arguments': {'typename': %s}}",
               type);
    qobject_unref(resp);

#ifdef CONFIG_HMP
    {
        g_autofree char *escaped = NULL;
        g_autoptr(GRegex) comma = NULL;

        comma = g_regex_new(",", 0, 0, NULL);
        escaped = g_regex_replace_literal(comma, type, -1, 0, ",,", 0, NULL);
        g_free(qtest_hmp(qts, "device_add \"%s,help\"", escaped));
    }
#endif
}

static void test_device_intro_list(void)
{
    QList *types;
    QTestState *qts;

    qts = qtest_init(common_args);

    types = device_type_list(qts, true);
    qobject_unref(types);

#ifdef CONFIG_HMP
    {
        g_free(qtest_hmp(qts, "device_add help"));
    }
#endif

    qtest_quit(qts);
}

/*
 * Ensure all entries returned by qom-list-types implements=<parent>
 * have <parent> as a parent.
 */
static void test_qom_list_parents(QTestState *qts, const char *parent)
{
    QList *types;
    QListEntry *e;
    QDict *index;

    types = qom_list_types(qts, parent, true);
    index = qom_type_index(types);

    QLIST_FOREACH_ENTRY(types, e) {
        QDict *d = qobject_to(QDict, qlist_entry_obj(e));
        const char *name = qdict_get_str(d, "name");

        g_assert(qom_has_parent(index, name, parent));
    }

    qobject_unref(types);
    qobject_unref(index);
}

static void test_qom_list_fields(void)
{
    QList *all_types;
    QList *non_abstract;
    QListEntry *e;
    QTestState *qts;

    qts = qtest_init(common_args);

    all_types = qom_list_types(qts, NULL, true);
    non_abstract = qom_list_types(qts, NULL, false);

    QLIST_FOREACH_ENTRY(all_types, e) {
        QDict *d = qobject_to(QDict, qlist_entry_obj(e));
        const char *name = qdict_get_str(d, "name");
        bool abstract = qdict_haskey(d, "abstract") ?
                        qdict_get_bool(d, "abstract") :
                        false;
        bool expected_abstract = !type_list_find(non_abstract, name);

        g_assert(abstract == expected_abstract);
    }

    test_qom_list_parents(qts, "object");
    test_qom_list_parents(qts, "device");
    test_qom_list_parents(qts, "sys-bus-device");

    qobject_unref(all_types);
    qobject_unref(non_abstract);
    qtest_quit(qts);
}

static void test_device_intro_none(void)
{
    QTestState *qts = qtest_init(common_args);
    g_autofree char *qom_tree_start = qom_tree_str(qts);
    g_autofree char *qom_tree_end = NULL;
#ifdef CONFIG_HMP
    g_autofree char *qtree_start = qtest_hmp(qts, "info qtree");
    g_autofree char *qtree_end = NULL;
#endif

    test_one_device(qts, "nonexistent");

    /* Make sure that really nothing changed in the trees */
    qom_tree_end = qom_tree_str(qts);
    g_assert_cmpstr(qom_tree_start, ==, qom_tree_end);
#ifdef CONFIG_HMP
    qtree_end = qtest_hmp(qts, "info qtree");
    g_assert_cmpstr(qtree_start, ==, qtree_end);
#endif

    qtest_quit(qts);
}

static void test_device_intro_abstract(void)
{
    QTestState *qts = qtest_init(common_args);
    g_autofree char *qom_tree_start = qom_tree_str(qts);
    g_autofree char *qom_tree_end = NULL;
#ifdef CONFIG_HMP
    g_autofree char *qtree_start = qtest_hmp(qts, "info qtree");
    g_autofree char *qtree_end = NULL;
#endif

    test_one_device(qts, "device");

    /* Make sure that really nothing changed in the trees */
    qom_tree_end = qom_tree_str(qts);
    g_assert_cmpstr(qom_tree_start, ==, qom_tree_end);
#ifdef CONFIG_HMP
    qtree_end = qtest_hmp(qts, "info qtree");
    g_assert_cmpstr(qtree_start, ==, qtree_end);
#endif

    qtest_quit(qts);
}

static void test_device_intro_concrete(const void *args)
{
    QList *types;
    QListEntry *entry;
    const char *type;
    QTestState *qts = qtest_init(args);
    g_autofree char *qom_tree_start = qom_tree_str(qts);
    g_autofree char *qom_tree_end = NULL;
#ifdef CONFIG_HMP
    g_autofree char *qtree_start = qtest_hmp(qts, "info qtree");
    g_autofree char *qtree_end = NULL;
#endif

    types = device_type_list(qts, false);

    QLIST_FOREACH_ENTRY(types, entry) {
        type = qdict_get_try_str(qobject_to(QDict, qlist_entry_obj(entry)),
                                 "name");
        g_assert(type);
        test_one_device(qts, type);
    }

    /*
     * Some devices leave dangling pointers in QOM behind.
     * Walking the QOM tree via qom-list has a good chance at crashing then.
     * Also make sure that the tree did not change.
     */
    qom_tree_end = qom_tree_str(qts);
    g_assert_cmpstr(qom_tree_start, ==, qom_tree_end);
#ifdef CONFIG_HMP
    qtree_end = qtest_hmp(qts, "info qtree");
    g_assert_cmpstr(qtree_start, ==, qtree_end);
#endif

    qobject_unref(types);
    qtest_quit(qts);
}

static void test_abstract_interfaces(void)
{
    QList *all_types;
    QListEntry *e;
    QDict *index;
    QTestState *qts;

    qts = qtest_init(common_args);

    all_types = qom_list_types(qts, "interface", true);
    index = qom_type_index(all_types);

    QLIST_FOREACH_ENTRY(all_types, e) {
        QDict *d = qobject_to(QDict, qlist_entry_obj(e));
        const char *name = qdict_get_str(d, "name");

        /*
         * qom-list-types implements=interface returns all types
         * that implement _any_ interface (not just interface
         * types), so skip the ones that don't have "interface"
         * on the parent type chain.
         */
        if (!qom_has_parent(index, name, "interface")) {
            /* Not an interface type */
            continue;
        }

        g_assert(qdict_haskey(d, "abstract") && qdict_get_bool(d, "abstract"));
    }

    qobject_unref(all_types);
    qobject_unref(index);
    qtest_quit(qts);
}

static void add_machine_test_case(const char *mname)
{
    char *path, *args;

    path = g_strdup_printf("device/introspect/concrete/defaults/%s", mname);
    args = g_strdup_printf("-M %s", mname);
    qtest_add_data_func_full(path, args, test_device_intro_concrete, g_free);
    g_free(path);

    path = g_strdup_printf("device/introspect/concrete/nodefaults/%s", mname);
    args = g_strdup_printf("-nodefaults -M %s", mname);
    qtest_add_data_func_full(path, args, test_device_intro_concrete, g_free);
    g_free(path);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("device/introspect/list", test_device_intro_list);
    qtest_add_func("device/introspect/list-fields", test_qom_list_fields);
    qtest_add_func("device/introspect/none", test_device_intro_none);
    qtest_add_func("device/introspect/abstract", test_device_intro_abstract);
    qtest_add_func("device/introspect/abstract-interfaces", test_abstract_interfaces);
    if (g_test_quick()) {
        qtest_add_data_func("device/introspect/concrete/defaults/none",
                            common_args, test_device_intro_concrete);
    } else {
        qtest_cb_for_every_machine(add_machine_test_case, true);
    }

    return g_test_run();
}
