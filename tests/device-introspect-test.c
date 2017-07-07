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
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qdict.h"
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
    QINCREF(ret);
    QDECREF(resp);
    return ret;
}

/* Find an entry on a list returned by qom-list-types */
static QDict *type_list_find(QList *types, const char *name)
{
    QListEntry *e;

    QLIST_FOREACH_ENTRY(types, e) {
        QDict *d = qobject_to_qdict(qlist_entry_obj(e));
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

static void test_qom_list_fields(void)
{
    QList *all_types;
    QList *non_abstract;
    QListEntry *e;

    qtest_start(common_args);

    all_types = qom_list_types(NULL, true);
    non_abstract = qom_list_types(NULL, false);

    QLIST_FOREACH_ENTRY(all_types, e) {
        QDict *d = qobject_to_qdict(qlist_entry_obj(e));
        const char *name = qdict_get_str(d, "name");
        bool abstract = qdict_haskey(d, "abstract") ?
                        qdict_get_bool(d, "abstract") :
                        false;
        bool expected_abstract = !type_list_find(non_abstract, name);

        g_assert(abstract == expected_abstract);
    }

    QDECREF(all_types);
    QDECREF(non_abstract);
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

static void test_abstract_interfaces(void)
{
    QList *all_types;
    QList *obj_types;
    QListEntry *e;

    qtest_start(common_args);
    /*
     * qom-list-types implements=interface returns all types that
     * implement _any_ interface (not just interface types), but
     * we can filter them out because interfaces don't implement
     * the "object" type.
     */
    all_types = qom_list_types("interface", true);
    obj_types = qom_list_types("object", true);

    QLIST_FOREACH_ENTRY(all_types, e) {
        QDict *d = qobject_to_qdict(qlist_entry_obj(e));

        if (type_list_find(obj_types, qdict_get_str(d, "name"))) {
            /* Not an interface type */
            continue;
        }

        g_assert(qdict_haskey(d, "abstract") && qdict_get_bool(d, "abstract"));
    }

    QDECREF(all_types);
    QDECREF(obj_types);
    qtest_end();
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("device/introspect/list", test_device_intro_list);
    qtest_add_func("device/introspect/list-fields", test_qom_list_fields);
    qtest_add_func("device/introspect/none", test_device_intro_none);
    qtest_add_func("device/introspect/abstract", test_device_intro_abstract);
    qtest_add_func("device/introspect/concrete", test_device_intro_concrete);
    qtest_add_func("device/introspect/abstract-interfaces", test_abstract_interfaces);

    return g_test_run();
}
