/*
 * QMP command test cases
 *
 * Copyright (c) 2017 Red Hat Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqos/libqtest.h"
#include "qapi/error.h"
#include "qapi/qapi-visit-introspect.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qobject-input-visitor.h"

const char common_args[] = "-nodefaults -machine none";

/* Query smoke tests */

static int query_error_class(const char *cmd)
{
    static struct {
        const char *cmd;
        int err_class;
    } fails[] = {
        /* Success depends on build configuration: */
#ifndef CONFIG_SPICE
        { "query-spice", ERROR_CLASS_COMMAND_NOT_FOUND },
#endif
#ifndef CONFIG_TCG
        { "query-replay", ERROR_CLASS_COMMAND_NOT_FOUND },
#endif
#ifndef CONFIG_VNC
        { "query-vnc", ERROR_CLASS_GENERIC_ERROR },
        { "query-vnc-servers", ERROR_CLASS_GENERIC_ERROR },
#endif
#ifndef CONFIG_REPLICATION
        { "query-xen-replication-status", ERROR_CLASS_COMMAND_NOT_FOUND },
#endif
        /* Likewise, and require special QEMU command-line arguments: */
        { "query-acpi-ospm-status", ERROR_CLASS_GENERIC_ERROR },
        { "query-balloon", ERROR_CLASS_DEVICE_NOT_ACTIVE },
        { "query-hotpluggable-cpus", ERROR_CLASS_GENERIC_ERROR },
        { "query-vm-generation-id", ERROR_CLASS_GENERIC_ERROR },
        { NULL, -1 }
    };
    int i;

    for (i = 0; fails[i].cmd; i++) {
        if (!strcmp(cmd, fails[i].cmd)) {
            return fails[i].err_class;
        }
    }
    return -1;
}

static void test_query(const void *data)
{
    const char *cmd = data;
    int expected_error_class = query_error_class(cmd);
    QDict *resp, *error;
    const char *error_class;
    QTestState *qts;

    qts = qtest_init(common_args);

    resp = qtest_qmp(qts, "{ 'execute': %s }", cmd);
    error = qdict_get_qdict(resp, "error");
    error_class = error ? qdict_get_str(error, "class") : NULL;

    if (expected_error_class < 0) {
        g_assert(qdict_haskey(resp, "return"));
    } else {
        g_assert(error);
        g_assert_cmpint(qapi_enum_parse(&QapiErrorClass_lookup, error_class,
                                        -1, &error_abort),
                        ==, expected_error_class);
    }
    qobject_unref(resp);

    qtest_quit(qts);
}

static bool query_is_ignored(const char *cmd)
{
    const char *ignored[] = {
        /* Not actually queries: */
        "add-fd",
        /* Success depends on target arch: */
        "query-cpu-definitions",  /* arm, i386, ppc, s390x */
        "query-gic-capabilities", /* arm */
        /* Success depends on target-specific build configuration: */
        "query-pci",              /* CONFIG_PCI */
        /* Success depends on launching SEV guest */
        "query-sev-launch-measure",
        /* Success depends on Host or Hypervisor SEV support */
        "query-sev",
        "query-sev-capabilities",
        "query-sgx",
        "query-sgx-capabilities",
        NULL
    };
    int i;

    for (i = 0; ignored[i]; i++) {
        if (!strcmp(cmd, ignored[i])) {
            return true;
        }
    }
    return false;
}

typedef struct {
    SchemaInfoList *list;
    GHashTable *hash;
} QmpSchema;

static void qmp_schema_init(QmpSchema *schema)
{
    QDict *resp;
    Visitor *qiv;
    SchemaInfoList *tail;
    QTestState *qts;

    qts = qtest_init(common_args);

    resp = qtest_qmp(qts, "{ 'execute': 'query-qmp-schema' }");

    qiv = qobject_input_visitor_new(qdict_get(resp, "return"));
    visit_type_SchemaInfoList(qiv, NULL, &schema->list, &error_abort);
    visit_free(qiv);

    qobject_unref(resp);
    qtest_quit(qts);

    schema->hash = g_hash_table_new(g_str_hash, g_str_equal);

    /* Build @schema: hash table mapping entity name to SchemaInfo */
    for (tail = schema->list; tail; tail = tail->next) {
        g_hash_table_insert(schema->hash, tail->value->name, tail->value);
    }
}

static SchemaInfo *qmp_schema_lookup(QmpSchema *schema, const char *name)
{
    return g_hash_table_lookup(schema->hash, name);
}

static void qmp_schema_cleanup(QmpSchema *schema)
{
    qapi_free_SchemaInfoList(schema->list);
    g_hash_table_destroy(schema->hash);
}

static bool object_type_has_mandatory_members(SchemaInfo *type)
{
    SchemaInfoObjectMemberList *tail;

    g_assert(type->meta_type == SCHEMA_META_TYPE_OBJECT);

    for (tail = type->u.object.members; tail; tail = tail->next) {
        if (!tail->value->has_q_default) {
            return true;
        }
    }

    return false;
}

static void add_query_tests(QmpSchema *schema)
{
    SchemaInfoList *tail;
    SchemaInfo *si, *arg_type, *ret_type;
    char *test_name;

    /* Test the query-like commands */
    for (tail = schema->list; tail; tail = tail->next) {
        si = tail->value;
        if (si->meta_type != SCHEMA_META_TYPE_COMMAND) {
            continue;
        }

        if (query_is_ignored(si->name)) {
            continue;
        }

        arg_type = qmp_schema_lookup(schema, si->u.command.arg_type);
        if (object_type_has_mandatory_members(arg_type)) {
            continue;
        }

        ret_type = qmp_schema_lookup(schema, si->u.command.ret_type);
        if (ret_type->meta_type == SCHEMA_META_TYPE_OBJECT
            && !ret_type->u.object.members) {
            continue;
        }

        test_name = g_strdup_printf("qmp/%s", si->name);
        qtest_add_data_func(test_name, si->name, test_query);
        g_free(test_name);
    }
}

static void test_object_add_failure_modes(void)
{
    QTestState *qts;
    QDict *resp;

    /* attempt to create an object without props */
    qts = qtest_init(common_args);
    resp = qtest_qmp(qts, "{'execute': 'object-add', 'arguments':"
                     " {'qom-type': 'memory-backend-ram', 'id': 'ram1' } }");
    g_assert_nonnull(resp);
    qmp_expect_error_and_unref(resp, "GenericError");

    /* attempt to create an object without qom-type */
    resp = qtest_qmp(qts, "{'execute': 'object-add', 'arguments':"
                     " {'id': 'ram1' } }");
    g_assert_nonnull(resp);
    qmp_expect_error_and_unref(resp, "GenericError");

    /* attempt to delete an object that does not exist */
    resp = qtest_qmp(qts, "{'execute': 'object-del', 'arguments':"
                     " {'id': 'ram1' } }");
    g_assert_nonnull(resp);
    qmp_expect_error_and_unref(resp, "GenericError");

    /* attempt to create 2 objects with duplicate id */
    resp = qtest_qmp(qts, "{'execute': 'object-add', 'arguments':"
                     " {'qom-type': 'memory-backend-ram', 'id': 'ram1',"
                     " 'size': 1048576 } }");
    g_assert_nonnull(resp);
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);

    resp = qtest_qmp(qts, "{'execute': 'object-add', 'arguments':"
                     " {'qom-type': 'memory-backend-ram', 'id': 'ram1',"
                     " 'size': 1048576 } }");
    g_assert_nonnull(resp);
    qmp_expect_error_and_unref(resp, "GenericError");

    /* delete ram1 object */
    resp = qtest_qmp(qts, "{'execute': 'object-del', 'arguments':"
                     " {'id': 'ram1' } }");
    g_assert_nonnull(resp);
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);

    /* attempt to create an object with a property of a wrong type */
    resp = qtest_qmp(qts, "{'execute': 'object-add', 'arguments':"
                     " {'qom-type': 'memory-backend-ram', 'id': 'ram1',"
                     " 'size': '1048576' } }");
    g_assert_nonnull(resp);
    /* now do it right */
    qmp_expect_error_and_unref(resp, "GenericError");

    resp = qtest_qmp(qts, "{'execute': 'object-add', 'arguments':"
                     " {'qom-type': 'memory-backend-ram', 'id': 'ram1',"
                     " 'size': 1048576 } }");
    g_assert_nonnull(resp);
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);

    /* delete ram1 object */
    resp = qtest_qmp(qts, "{'execute': 'object-del', 'arguments':"
                     " {'id': 'ram1' } }");
    g_assert_nonnull(resp);
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);

    /* attempt to create an object without the id */
    resp = qtest_qmp(qts, "{'execute': 'object-add', 'arguments':"
                     " {'qom-type': 'memory-backend-ram',"
                     " 'size': 1048576 } }");
    g_assert_nonnull(resp);
    qmp_expect_error_and_unref(resp, "GenericError");

    /* now do it right */
    resp = qtest_qmp(qts, "{'execute': 'object-add', 'arguments':"
                     " {'qom-type': 'memory-backend-ram', 'id': 'ram1',"
                     " 'size': 1048576 } }");
    g_assert_nonnull(resp);
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);

    /* delete ram1 object */
    resp = qtest_qmp(qts, "{'execute': 'object-del', 'arguments':"
                     " {'id': 'ram1' } }");
    g_assert_nonnull(resp);
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);

    /* attempt to set a non existing property */
    resp = qtest_qmp(qts, "{'execute': 'object-add', 'arguments':"
                     " {'qom-type': 'memory-backend-ram', 'id': 'ram1',"
                     " 'sized': 1048576 } }");
    g_assert_nonnull(resp);
    qmp_expect_error_and_unref(resp, "GenericError");

    /* now do it right */
    resp = qtest_qmp(qts, "{'execute': 'object-add', 'arguments':"
                     " {'qom-type': 'memory-backend-ram', 'id': 'ram1',"
                     " 'size': 1048576 } }");
    g_assert_nonnull(resp);
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);

    /* delete ram1 object without id */
    resp = qtest_qmp(qts, "{'execute': 'object-del', 'arguments':"
                     " {'ida': 'ram1' } }");
    g_assert_nonnull(resp);
    qobject_unref(resp);

    /* delete ram1 object */
    resp = qtest_qmp(qts, "{'execute': 'object-del', 'arguments':"
                     " {'id': 'ram1' } }");
    g_assert_nonnull(resp);
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);

    /* delete ram1 object that does not exist anymore*/
    resp = qtest_qmp(qts, "{'execute': 'object-del', 'arguments':"
                     " {'id': 'ram1' } }");
    g_assert_nonnull(resp);
    qmp_expect_error_and_unref(resp, "GenericError");

    qtest_quit(qts);
}

int main(int argc, char *argv[])
{
    QmpSchema schema;
    int ret;

    g_test_init(&argc, &argv, NULL);

    qmp_schema_init(&schema);
    add_query_tests(&schema);

    qtest_add_func("qmp/object-add-failure-modes",
                   test_object_add_failure_modes);

    ret = g_test_run();

    qmp_schema_cleanup(&schema);
    return ret;
}
