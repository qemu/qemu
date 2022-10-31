/*
 * QMP protocol test cases
 *
 * Copyright (c) 2017-2018 Red Hat Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qapi/error.h"
#include "qapi/qapi-visit-control.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qmp/qstring.h"

const char common_args[] = "-nodefaults -machine none";

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

static void assert_recovered(QTestState *qts)
{
    QDict *resp;

    resp = qtest_qmp(qts, "{ 'execute': 'no-such-cmd' }");
    qmp_expect_error_and_unref(resp, "CommandNotFound");
}

static void test_malformed(QTestState *qts)
{
    QDict *resp;

    /* syntax error */
    qtest_qmp_send_raw(qts, "{]\n");
    resp = qtest_qmp_receive_dict(qts);
    qmp_expect_error_and_unref(resp, "GenericError");
    assert_recovered(qts);

    /* lexical error: impossible byte outside string */
    qtest_qmp_send_raw(qts, "{\xFF");
    resp = qtest_qmp_receive_dict(qts);
    qmp_expect_error_and_unref(resp, "GenericError");
    assert_recovered(qts);

    /* lexical error: funny control character outside string */
    qtest_qmp_send_raw(qts, "{\x01");
    resp = qtest_qmp_receive_dict(qts);
    qmp_expect_error_and_unref(resp, "GenericError");
    assert_recovered(qts);

    /* lexical error: impossible byte in string */
    qtest_qmp_send_raw(qts, "{'bad \xFF");
    resp = qtest_qmp_receive_dict(qts);
    qmp_expect_error_and_unref(resp, "GenericError");
    assert_recovered(qts);

    /* lexical error: control character in string */
    qtest_qmp_send_raw(qts, "{'execute': 'nonexistent', 'id':'\n");
    resp = qtest_qmp_receive_dict(qts);
    qmp_expect_error_and_unref(resp, "GenericError");
    assert_recovered(qts);

    /* lexical error: interpolation */
    qtest_qmp_send_raw(qts, "%%p");
    resp = qtest_qmp_receive_dict(qts);
    qmp_expect_error_and_unref(resp, "GenericError");
    assert_recovered(qts);

    /* Not even a dictionary */
    resp = qtest_qmp(qts, "null");
    qmp_expect_error_and_unref(resp, "GenericError");

    /* No "execute" key */
    resp = qtest_qmp(qts, "{}");
    qmp_expect_error_and_unref(resp, "GenericError");

    /* "execute" isn't a string */
    resp = qtest_qmp(qts, "{ 'execute': true }");
    qmp_expect_error_and_unref(resp, "GenericError");

    /* "arguments" isn't a dictionary */
    resp = qtest_qmp(qts, "{ 'execute': 'no-such-cmd', 'arguments': [] }");
    qmp_expect_error_and_unref(resp, "GenericError");

    /* extra key */
    resp = qtest_qmp(qts, "{ 'execute': 'no-such-cmd', 'extra': true }");
    qmp_expect_error_and_unref(resp, "GenericError");
}

static void test_qmp_protocol(void)
{
    QDict *resp, *q, *ret;
    QList *capabilities;
    QTestState *qts;

    qts = qtest_init_without_qmp_handshake(common_args);

    /* Test greeting */
    resp = qtest_qmp_receive_dict(qts);
    q = qdict_get_qdict(resp, "QMP");
    g_assert(q);
    test_version(qdict_get(q, "version"));
    capabilities = qdict_get_qlist(q, "capabilities");
    g_assert(capabilities);
    qobject_unref(resp);

    /* Test valid command before handshake */
    resp = qtest_qmp(qts, "{ 'execute': 'query-version' }");
    qmp_expect_error_and_unref(resp, "CommandNotFound");

    /* Test malformed commands before handshake */
    test_malformed(qts);

    /* Test handshake */
    resp = qtest_qmp(qts, "{ 'execute': 'qmp_capabilities' }");
    ret = qdict_get_qdict(resp, "return");
    g_assert(ret && !qdict_size(ret));
    qobject_unref(resp);

    /* Test repeated handshake */
    resp = qtest_qmp(qts, "{ 'execute': 'qmp_capabilities' }");
    qmp_expect_error_and_unref(resp, "CommandNotFound");

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
    g_assert_cmpint(qdict_get_int(resp, "id"), ==, 2);
    qmp_expect_error_and_unref(resp, "GenericError");

    qtest_quit(qts);
}

#ifndef _WIN32

/* Out-of-band tests */

char *tmpdir;
char *fifo_name;

static void setup_blocking_cmd(void)
{
    GError *err = NULL;
    tmpdir = g_dir_make_tmp("qmp-test-XXXXXX", &err);
    g_assert_no_error(err);

    fifo_name = g_strdup_printf("%s/fifo", tmpdir);
    if (mkfifo(fifo_name, 0666)) {
        g_error("mkfifo: %s", strerror(errno));
    }
}

static void cleanup_blocking_cmd(void)
{
    unlink(fifo_name);
    rmdir(tmpdir);
    g_free(tmpdir);
}

static void send_cmd_that_blocks(QTestState *s, const char *id)
{
    qtest_qmp_send(s, "{ 'execute': 'blockdev-add',  'id': %s,"
                   " 'arguments': {"
                   " 'driver': 'blkdebug', 'node-name': %s,"
                   " 'config': %s,"
                   " 'image': { 'driver': 'null-co', 'read-zeroes': true } } }",
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
    qtest_qmp_send(s, "{ 'exec-oob': 'migrate-pause', 'id': %s }", id);
}

static void recv_cmd_id(QTestState *s, const char *id)
{
    QDict *resp = qtest_qmp_receive_dict(s);

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

    qts = qtest_init_without_qmp_handshake(common_args);

    /* Check the greeting message. */
    resp = qtest_qmp_receive_dict(qts);
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
    resp = qtest_qmp(qts, "{ 'exec-oob': 'query-cpus-fast' }");
    g_assert(qdict_haskey(resp, "error"));
    qobject_unref(resp);

    /* OOB command overtakes slow in-band command */
    setup_blocking_cmd();
    send_cmd_that_blocks(qts, "ib-blocks-1");
    qtest_qmp_send(qts, "{ 'execute': 'query-name', 'id': 'ib-quick-1' }");
    send_oob_cmd_that_fails(qts, "oob-1");
    recv_cmd_id(qts, "oob-1");
    unblock_blocked_cmd();
    recv_cmd_id(qts, "ib-blocks-1");
    recv_cmd_id(qts, "ib-quick-1");

    /* Even malformed in-band command fails in-band */
    send_cmd_that_blocks(qts, "blocks-2");
    qtest_qmp_send(qts, "{ 'id': 'err-2' }");
    unblock_blocked_cmd();
    recv_cmd_id(qts, "blocks-2");
    recv_cmd_id(qts, "err-2");
    cleanup_blocking_cmd();

    qtest_quit(qts);
}

#endif /* _WIN32 */

/* Preconfig tests */

static void test_qmp_preconfig(void)
{
    QDict *rsp, *ret;
    QTestState *qs = qtest_initf("%s --preconfig", common_args);

    /* preconfig state */
    /* enabled commands, no error expected  */
    g_assert(!qmp_rsp_is_err(qtest_qmp(qs, "{ 'execute': 'query-commands' }")));

    /* forbidden commands, expected error */
    g_assert(qmp_rsp_is_err(qtest_qmp(qs, "{ 'execute': 'query-cpus-fast' }")));

    /* check that query-status returns preconfig state */
    rsp = qtest_qmp(qs, "{ 'execute': 'query-status' }");
    ret = qdict_get_qdict(rsp, "return");
    g_assert(ret);
    g_assert_cmpstr(qdict_get_try_str(ret, "status"), ==, "prelaunch");
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
    g_assert(!qmp_rsp_is_err(qtest_qmp(qs, "{ 'execute': 'query-cpus-fast' }")));

    qtest_quit(qs);
}

static void test_qmp_missing_any_arg(void)
{
    QTestState *qts;
    QDict *resp;

    qts = qtest_init(common_args);
    resp = qtest_qmp(qts, "{'execute': 'qom-set', 'arguments':"
                     " { 'path': '/machine', 'property': 'rtc-time' } }");
    g_assert_nonnull(resp);
    qmp_expect_error_and_unref(resp, "GenericError");
    qtest_quit(qts);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("qmp/protocol", test_qmp_protocol);
#ifndef _WIN32
    /* This case calls mkfifo() which does not exist on win32 */
    qtest_add_func("qmp/oob", test_qmp_oob);
#endif
    qtest_add_func("qmp/preconfig", test_qmp_preconfig);
    qtest_add_func("qmp/missing-any-arg", test_qmp_missing_any_arg);

    return g_test_run();
}
