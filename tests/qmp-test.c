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

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("qmp/protocol", test_qmp_protocol);

    return g_test_run();
}
