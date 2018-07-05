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
#include "qapi/error.h"
#include "qapi/qapi-visit-introspect.h"
#include "qapi/qapi-visit-misc.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/util.h"
#include "qapi/visitor.h"
#include "qapi/qmp/qstring.h"

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

static void test_malformed(QTestState *qts)
{
    QDict *resp;

    /* Not even a dictionary */
    resp = qtest_qmp(qts, "null");
    g_assert_cmpstr(get_error_class(resp), ==, "GenericError");
    qobject_unref(resp);

    /* No "execute" key */
    resp = qtest_qmp(qts, "{}");
    g_assert_cmpstr(get_error_class(resp), ==, "GenericError");
    qobject_unref(resp);

    /* "execute" isn't a string */
    resp = qtest_qmp(qts, "{ 'execute': true }");
    g_assert_cmpstr(get_error_class(resp), ==, "GenericError");
    qobject_unref(resp);

    /* "arguments" isn't a dictionary */
    resp = qtest_qmp(qts, "{ 'execute': 'no-such-cmd', 'arguments': [] }");
    g_assert_cmpstr(get_error_class(resp), ==, "GenericError");
    qobject_unref(resp);

    /* extra key */
    resp = qtest_qmp(qts, "{ 'execute': 'no-such-cmd', 'extra': true }");
    g_assert_cmpstr(get_error_class(resp), ==, "GenericError");
    qobject_unref(resp);
}

static void test_qmp_protocol(void)
{
    QDict *resp, *q, *ret;
    QList *capabilities;
    QTestState *qts;

    qts = qtest_init_without_qmp_handshake(false, common_args);

    /* Test greeting */
    resp = qtest_qmp_receive(qts);
    q = qdict_get_qdict(resp, "QMP");
    g_assert(q);
    test_version(qdict_get(q, "version"));
    capabilities = qdict_get_qlist(q, "capabilities");
    g_assert(capabilities && qlist_empty(capabilities));
    qobject_unref(resp);

    /* Test valid command before handshake */
    resp = qtest_qmp(qts, "{ 'execute': 'query-version' }");
    g_assert_cmpstr(get_error_class(resp), ==, "CommandNotFound");
    qobject_unref(resp);

    /* Test malformed commands before handshake */
    test_malformed(qts);

    /* Test handshake */
    resp = qtest_qmp(qts, "{ 'execute': 'qmp_capabilities' }");
    ret = qdict_get_qdict(resp, "return");
    g_assert(ret && !qdict_size(ret));
    qobject_unref(resp);

    /* Test repeated handshake */
    resp = qtest_qmp(qts, "{ 'execute': 'qmp_capabilities' }");
    g_assert_cmpstr(get_error_class(resp), ==, "CommandNotFound");
    qobject_unref(resp);

    /* Test valid command */
    resp = qtest_qmp(qts, "{ 'execute': 'query-version' }");
    test_version(qdict_get(resp, "return"));
    qobject_unref(resp);

    /* Test malformed commands */
    test_malformed(qts);

    /* Test 'id' */
    resp = qtest_qmp(qts, "{ 'execute': 'query-name', 'id': 'cookie#1' }");
    ret = qdict_get_qdict(resp, "return");
    g_assert(ret);
    g_assert_cmpstr(qdict_get_try_str(resp, "id"), ==, "cookie#1");
    qobject_unref(resp);

    /* Test command failure with 'id' */
    resp = qtest_qmp(qts, "{ 'execute': 'human-monitor-command', 'id': 2 }");
    g_assert_cmpstr(get_error_class(resp), ==, "GenericError");
    g_assert_cmpint(qdict_get_int(resp, "id"), ==, 2);
    qobject_unref(resp);

    qtest_quit(qts);
}

/* Out-of-band tests */

char tmpdir[] = "/tmp/qmp-test-XXXXXX";
char *fifo_name;

static void setup_blocking_cmd(void)
{
    if (!mkdtemp(tmpdir)) {
        g_error("mkdtemp: %s", strerror(errno));
    }
    fifo_name = g_strdup_printf("%s/fifo", tmpdir);
    if (mkfifo(fifo_name, 0666)) {
        g_error("mkfifo: %s", strerror(errno));
    }
}

static void cleanup_blocking_cmd(void)
{
    unlink(fifo_name);
    rmdir(tmpdir);
}

static void send_cmd_that_blocks(QTestState *s, const char *id)
{
    qtest_async_qmp(s, "{ 'execute': 'blockdev-add',  'id': %s,"
                    " 'arguments': {"
                    " 'driver': 'blkdebug', 'node-name': %s,"
                    " 'config': %s,"
                    " 'image': { 'driver': 'null-co' } } }",
                    id, id, fifo_name);
}

static void unblock_blocked_cmd(void)
{
    int fd = open(fifo_name, O_WRONLY);
    g_assert(fd >= 0);
    close(fd);
}

static void send_oob_cmd_that_fails(QTestState *s, const char *id)
{
    qtest_async_qmp(s, "{ 'exec-oob': 'migrate-pause', 'id': %s }", id);
}

static void recv_cmd_id(QTestState *s, const char *id)
{
    QDict *resp = qtest_qmp_receive(s);

    g_assert_cmpstr(qdict_get_try_str(resp, "id"), ==, id);
    qobject_unref(resp);
}

static void test_qmp_oob(void)
{
    QTestState *qts;
    QDict *resp, *q;
    const QListEntry *entry;
    QList *capabilities;
    QString *qstr;

    qts = qtest_init_without_qmp_handshake(true, common_args);

    /* Check the greeting message. */
    resp = qtest_qmp_receive(qts);
    q = qdict_get_qdict(resp, "QMP");
    g_assert(q);
    capabilities = qdict_get_qlist(q, "capabilities");
    g_assert(capabilities && !qlist_empty(capabilities));
    entry = qlist_first(capabilities);
    g_assert(entry);
    qstr = qobject_to(QString, entry->value);
    g_assert(qstr);
    g_assert_cmpstr(qstring_get_str(qstr), ==, "oob");
    qobject_unref(resp);

    /* Try a fake capability, it should fail. */
    resp = qtest_qmp(qts,
                     "{ 'execute': 'qmp_capabilities', "
                     "  'arguments': { 'enable': [ 'cap-does-not-exist' ] } }");
    g_assert(qdict_haskey(resp, "error"));
    qobject_unref(resp);

    /* Now, enable OOB in current QMP session, it should succeed. */
    resp = qtest_qmp(qts,
                     "{ 'execute': 'qmp_capabilities', "
                     "  'arguments': { 'enable': [ 'oob' ] } }");
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);

    /*
     * Try any command that does not support OOB but with OOB flag. We
     * should get failure.
     */
    resp = qtest_qmp(qts, "{ 'exec-oob': 'query-cpus' }");
    g_assert(qdict_haskey(resp, "error"));
    qobject_unref(resp);

    /* OOB command overtakes slow in-band command */
    setup_blocking_cmd();
    send_cmd_that_blocks(qts, "ib-blocks-1");
    qtest_async_qmp(qts, "{ 'execute': 'query-name', 'id': 'ib-quick-1' }");
    send_oob_cmd_that_fails(qts, "oob-1");
    recv_cmd_id(qts, "oob-1");
    unblock_blocked_cmd();
    recv_cmd_id(qts, "ib-blocks-1");
    recv_cmd_id(qts, "ib-quick-1");

    /* Even malformed in-band command fails in-band */
    send_cmd_that_blocks(qts, "blocks-2");
    qtest_async_qmp(qts, "{ 'id': 'err-2' }");
    unblock_blocked_cmd();
    recv_cmd_id(qts, "blocks-2");
    recv_cmd_id(qts, "err-2");
    cleanup_blocking_cmd();

    qtest_quit(qts);
}

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
    qobject_unref(resp);

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
        /* Success depends on launching SEV guest */
        "query-sev-launch-measure",
        /* Success depends on Host or Hypervisor SEV support */
        "query-sev",
        "query-sev-capabilities",
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

    qobject_unref(resp);
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
    char *test_name;

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
        g_free(test_name);
    }
}

/* Preconfig tests */

static void test_qmp_preconfig(void)
{
    QDict *rsp, *ret;
    QTestState *qs = qtest_startf("%s --preconfig", common_args);

    /* preconfig state */
    /* enabled commands, no error expected  */
    g_assert(!qmp_rsp_is_err(qtest_qmp(qs, "{ 'execute': 'query-commands' }")));

    /* forbidden commands, expected error */
    g_assert(qmp_rsp_is_err(qtest_qmp(qs, "{ 'execute': 'query-cpus' }")));

    /* check that query-status returns preconfig state */
    rsp = qtest_qmp(qs, "{ 'execute': 'query-status' }");
    ret = qdict_get_qdict(rsp, "return");
    g_assert(ret);
    g_assert_cmpstr(qdict_get_try_str(ret, "status"), ==, "preconfig");
    qobject_unref(rsp);

    /* exit preconfig state */
    g_assert(!qmp_rsp_is_err(qtest_qmp(qs, "{ 'execute': 'x-exit-preconfig' }")));
    qtest_qmp_eventwait(qs, "RESUME");

    /* check that query-status returns running state */
    rsp = qtest_qmp(qs, "{ 'execute': 'query-status' }");
    ret = qdict_get_qdict(rsp, "return");
    g_assert(ret);
    g_assert_cmpstr(qdict_get_try_str(ret, "status"), ==, "running");
    qobject_unref(rsp);

    /* check that x-exit-preconfig returns error after exiting preconfig */
    g_assert(qmp_rsp_is_err(qtest_qmp(qs, "{ 'execute': 'x-exit-preconfig' }")));

    /* enabled commands, no error expected  */
    g_assert(!qmp_rsp_is_err(qtest_qmp(qs, "{ 'execute': 'query-cpus' }")));

    qtest_quit(qs);
}

int main(int argc, char *argv[])
{
    QmpSchema schema;
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("qmp/protocol", test_qmp_protocol);
    qtest_add_func("qmp/oob", test_qmp_oob);
    qmp_schema_init(&schema);
    add_query_tests(&schema);
    qtest_add_func("qmp/preconfig", test_qmp_preconfig);

    ret = g_test_run();

    qmp_schema_cleanup(&schema);
    return ret;
}
