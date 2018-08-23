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
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qlist.h"
#include "libqtest.h"

const char common_args[] = "-nodefaults -machine none";

static QList *qom_list_types(const char *implements, bool abstract)
{
    QDict *resp;
    QList *ret;
    QDict *args = qdict_new();

    qdict_put_bool(args, "abstract", abstract);
    if (implements) {
        qdict_put_str(args, "implements", implements);
    }
    resp = qmp("{'execute': 'qom-list-types',"
               " 'arguments': %p }", args);
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

static QList *device_type_list(bool abstract)
{
    return qom_list_types("device", abstract);
}

static void test_one_device(const char *type)
{
    QDict *resp;
    char *help;
    char *qom_tree_start, *qom_tree_end;
    char *qtree_start, *qtree_end;

    g_test_message("Testing device '%s'", type);

    qom_tree_start = hmp("info qom-tree");
    qtree_start = hmp("info qtree");

    resp = qmp("{'execute': 'device-list-properties',"
               " 'arguments': {'typename': %s}}",
               type);
    qobject_unref(resp);

    help = hmp("device_add \"%s,help\"", type);
    g_free(help);

    /*
     * Some devices leave dangling pointers in QOM behind.
     * "info qom-tree" or "info qtree" have a good chance at crashing then.
     * Also make sure that the tree did not change.
     */
    qom_tree_end = hmp("info qom-tree");
    g_assert_cmpstr(qom_tree_start, ==, qom_tree_end);
    g_free(qom_tree_start);
    g_free(qom_tree_end);

    qtree_end = hmp("info qtree");
    g_assert_cmpstr(qtree_start, ==, qtree_end);
    g_free(qtree_start);
    g_free(qtree_end);
}

static void test_device_intro_list(void)
{
    QList *types;
    char *help;

    qtest_start(common_args);

    types = device_type_list(true);
    qobject_unref(types);

    help = hmp("device_add help");
    g_free(help);

    qtest_end();
}

/*
 * Ensure all entries returned by qom-list-types implements=<parent>
 * have <parent> as a parent.
 */
static void test_qom_list_parents(const char *parent)
{
    QList *types;
    QListEntry *e;
    QDict *index;

    types = qom_list_types(parent, true);
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

    qtest_start(common_args);

    all_types = qom_list_types(NULL, true);
    non_abstract = qom_list_types(NULL, false);

    QLIST_FOREACH_ENTRY(all_types, e) {
        QDict *d = qobject_to(QDict, qlist_entry_obj(e));
        const char *name = qdict_get_str(d, "name");
        bool abstract = qdict_haskey(d, "abstract") ?
                        qdict_get_bool(d, "abstract") :
                        false;
        bool expected_abstract = !type_list_find(non_abstract, name);

        g_assert(abstract == expected_abstract);
    }

    test_qom_list_parents("object");
    test_qom_list_parents("device");
    test_qom_list_parents("sys-bus-device");

    qobject_unref(all_types);
    qobject_unref(non_abstract);
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

static void test_device_intro_concrete(const void *args)
{
    QList *types;
    QListEntry *entry;
    const char *type;

    qtest_start(args);
    types = device_type_list(false);

    QLIST_FOREACH_ENTRY(types, entry) {
        type = qdict_get_try_str(qobject_to(QDict, qlist_entry_obj(entry)),
                                 "name");
        g_assert(type);
        test_one_device(type);
    }

    qobject_unref(types);
    qtest_end();
    g_free((void *)args);
}

static void test_abstract_interfaces(void)
{
    QList *all_types;
    QListEntry *e;
    QDict *index;

    qtest_start(common_args);

    all_types = qom_list_types("interface", true);
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
    qtest_end();
}

static void add_machine_test_case(const char *mname)
{
    char *path, *args;

    /* Ignore blacklisted machines */
    if (g_str_equal("xenfv", mname) || g_str_equal("xenpv", mname)) {
        return;
    }

    path = g_strdup_printf("device/introspect/concrete/defaults/%s", mname);
    args = g_strdup_printf("-M %s", mname);
    qtest_add_data_func(path, args, test_device_intro_concrete);
    g_free(path);

    path = g_strdup_printf("device/introspect/concrete/nodefaults/%s", mname);
    args = g_strdup_printf("-nodefaults -M %s", mname);
    qtest_add_data_func(path, args, test_device_intro_concrete);
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
                            g_strdup(common_args), test_device_intro_concrete);
    } else {
        qtest_cb_for_every_machine(add_machine_test_case, true);
    }

    return g_test_run();
}
