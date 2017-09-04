/*
 * QMP protocol test cases
 *
 * Copyright (c) 2017 Red Hat Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>,
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qapi-visit.h"
#include "qapi/error.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/util.h"
#include "qapi/visitor.h"

const char common_args[] = "-nodefaults -machine none";

static const char *get_error_class(QDict *resp)
{
    QDict *error = qdict_get_qdict(resp, "error");
    const char *desc = qdict_get_try_str(error, "desc");

    g_assert(desc);
    return error ? qdict_get_try_str(error, "class") : NULL;
}

static void test_version(QObject *version)
{
    Visitor *v;
    VersionInfo *vinfo;

    g_assert(version);
    v = qobject_input_visitor_new(version);
    visit_type_VersionInfo(v, "version", &vinfo, &error_abort);
    qapi_free_VersionInfo(vinfo);
    visit_free(v);
}

static void test_malformed(void)
{
    QDict *resp;

    /* Not even a dictionary */
    resp = qmp("null");
    g_assert_cmpstr(get_error_class(resp), ==, "GenericError");
    QDECREF(resp);

    /* No "execute" key */
    resp = qmp("{}");
    g_assert_cmpstr(get_error_class(resp), ==, "GenericError");
    QDECREF(resp);

    /* "execute" isn't a string */
    resp = qmp("{ 'execute': true }");
    g_assert_cmpstr(get_error_class(resp), ==, "GenericError");
    QDECREF(resp);

    /* "arguments" isn't a dictionary */
    resp = qmp("{ 'execute': 'no-such-cmd', 'arguments': [] }");
    g_assert_cmpstr(get_error_class(resp), ==, "GenericError");
    QDECREF(resp);

    /* extra key */
    resp = qmp("{ 'execute': 'no-such-cmd', 'extra': true }");
    g_assert_cmpstr(get_error_class(resp), ==, "GenericError");
    QDECREF(resp);
}

static void test_qmp_protocol(void)
{
    QDict *resp, *q, *ret;
    QList *capabilities;

    global_qtest = qtest_init_without_qmp_handshake(common_args);

    /* Test greeting */
    resp = qmp_receive();
    q = qdict_get_qdict(resp, "QMP");
    g_assert(q);
    test_version(qdict_get(q, "version"));
    capabilities = qdict_get_qlist(q, "capabilities");
    g_assert(capabilities && qlist_empty(capabilities));
    QDECREF(resp);

    /* Test valid command before handshake */
    resp = qmp("{ 'execute': 'query-version' }");
    g_assert_cmpstr(get_error_class(resp), ==, "CommandNotFound");
    QDECREF(resp);

    /* Test malformed commands before handshake */
    test_malformed();

    /* Test handshake */
    resp = qmp("{ 'execute': 'qmp_capabilities' }");
    ret = qdict_get_qdict(resp, "return");
    g_assert(ret && !qdict_size(ret));
    QDECREF(resp);

    /* Test repeated handshake */
    resp = qmp("{ 'execute': 'qmp_capabilities' }");
    g_assert_cmpstr(get_error_class(resp), ==, "CommandNotFound");
    QDECREF(resp);

    /* Test valid command */
    resp = qmp("{ 'execute': 'query-version' }");
    test_version(qdict_get(resp, "return"));
    QDECREF(resp);

    /* Test malformed commands */
    test_malformed();

    /* Test 'id' */
    resp = qmp("{ 'execute': 'query-name', 'id': 'cookie#1' }");
    ret = qdict_get_qdict(resp, "return");
    g_assert(ret);
    g_assert_cmpstr(qdict_get_try_str(resp, "id"), ==, "cookie#1");
    QDECREF(resp);

    /* Test command failure with 'id' */
    resp = qmp("{ 'execute': 'human-monitor-command', 'id': 2 }");
    g_assert_cmpstr(get_error_class(resp), ==, "GenericError");
    g_assert_cmpint(qdict_get_int(resp, "id"), ==, 2);
    QDECREF(resp);

    qtest_end();
}

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

    qtest_start(common_args);

    resp = qmp("{ 'execute': %s }", cmd);
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
    QDECREF(resp);

    qtest_end();
}

static bool query_is_blacklisted(const char *cmd)
{
    const char *blacklist[] = {
        /* Not actually queries: */
        "add-fd",
        /* Success depends on target arch: */
        "query-cpu-definitions",  /* arm, i386, ppc, s390x */
        "query-gic-capabilities", /* arm */
        /* Success depends on target-specific build configuration: */
        "query-pci",              /* CONFIG_PCI */
        NULL
    };
    int i;

    for (i = 0; blacklist[i]; i++) {
        if (!strcmp(cmd, blacklist[i])) {
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

    qtest_start(common_args);
    resp = qmp("{ 'execute': 'query-qmp-schema' }");

    qiv = qobject_input_visitor_new(qdict_get(resp, "return"));
    visit_type_SchemaInfoList(qiv, NULL, &schema->list, &error_abort);
    visit_free(qiv);

    QDECREF(resp);
    qtest_end();

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
    const char *test_name;

    /* Test the query-like commands */
    for (tail = schema->list; tail; tail = tail->next) {
        si = tail->value;
        if (si->meta_type != SCHEMA_META_TYPE_COMMAND) {
            continue;
        }

        if (query_is_blacklisted(si->name)) {
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
    }
}

int main(int argc, char *argv[])
{
    QmpSchema schema;
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("qmp/protocol", test_qmp_protocol);
    qmp_schema_init(&schema);
    add_query_tests(&schema);

    ret = g_test_run();

    qmp_schema_cleanup(&schema);
    return ret;
}
