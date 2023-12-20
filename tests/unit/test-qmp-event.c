/*
 * qapi event unit-tests.
 *
 * Copyright (c) 2014 Wenchao Xia
 *
 * Authors:
 *  Wenchao Xia   <wenchaoqemu@gmail.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include "qapi/compat-policy.h"
#include "qapi/error.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qnum.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qmp-event.h"
#include "test-qapi-events.h"
#include "test-qapi-emit-events.h"

static QDict *expected_event;

void test_qapi_event_emit(test_QAPIEvent event, QDict *d)
{
    QDict *t;
    int64_t s, ms;

    g_assert(expected_event);

    /* Verify that we have timestamp, then remove it to compare other fields */
    t = qdict_get_qdict(d, "timestamp");
    g_assert(t);
    s = qdict_get_try_int(t, "seconds", -2);
    ms = qdict_get_try_int(t, "microseconds", -2);
    if (s == -1) {
        g_assert(ms == -1);
    } else {
        g_assert(s >= 0);
        g_assert(ms >= 0 && ms <= 999999);
    }
    g_assert(qdict_size(t) == 2);

    qdict_del(d, "timestamp");

    g_assert(qobject_is_equal(QOBJECT(d), QOBJECT(expected_event)));
    qobject_unref(expected_event);
    expected_event = NULL;
}

static void test_event_a(void)
{
    expected_event = qdict_from_jsonf_nofail("{ 'event': 'EVENT_A' }");
    qapi_event_send_event_a();
    g_assert(!expected_event);
}

static void test_event_b(void)
{
    expected_event = qdict_from_jsonf_nofail("{ 'event': 'EVENT_B' }");
    qapi_event_send_event_b();
    g_assert(!expected_event);
}

static void test_event_c(void)
{
    UserDefOne b = { .integer = 2, .string = (char *)"test1" };

    expected_event = qdict_from_jsonf_nofail(
        "{ 'event': 'EVENT_C', 'data': {"
        " 'a': 1, 'b': { 'integer': 2, 'string': 'test1' }, 'c': 'test2' } }");
    qapi_event_send_event_c(true, 1, &b, "test2");
    g_assert(!expected_event);
}

/* Complex type */
static void test_event_d(void)
{
    UserDefOne struct1 = {
        .integer = 2, .string = (char *)"test1",
        .has_enum1 = true, .enum1 = ENUM_ONE_VALUE1,
    };
    EventStructOne a = {
        .struct1 = &struct1,
        .string = (char *)"test2",
        .has_enum2 = true,
        .enum2 = ENUM_ONE_VALUE2,
    };

    expected_event = qdict_from_jsonf_nofail(
        "{ 'event': 'EVENT_D', 'data': {"
        " 'a': {"
        "  'struct1': { 'integer': 2, 'string': 'test1', 'enum1': 'value1' },"
        "  'string': 'test2', 'enum2': 'value2' },"
        " 'b': 'test3', 'enum3': 'value3' } }");
    qapi_event_send_event_d(&a, "test3", NULL, true, ENUM_ONE_VALUE3);
    g_assert(!expected_event);
}

static void test_event_deprecated(void)
{
    expected_event = qdict_from_jsonf_nofail("{ 'event': 'TEST_EVENT_FEATURES1' }");

    memset(&compat_policy, 0, sizeof(compat_policy));

    qapi_event_send_test_event_features1();
    g_assert(!expected_event);

    compat_policy.has_deprecated_output = true;
    compat_policy.deprecated_output = COMPAT_POLICY_OUTPUT_HIDE;
    qapi_event_send_test_event_features1();
}

static void test_event_deprecated_data(void)
{
    memset(&compat_policy, 0, sizeof(compat_policy));

    expected_event = qdict_from_jsonf_nofail("{ 'event': 'TEST_EVENT_FEATURES0',"
                                           " 'data': { 'foo': 42 } }");
    qapi_event_send_test_event_features0(42);
    g_assert(!expected_event);


    compat_policy.has_deprecated_output = true;
    compat_policy.deprecated_output = COMPAT_POLICY_OUTPUT_HIDE;
    expected_event = qdict_from_jsonf_nofail("{ 'event': 'TEST_EVENT_FEATURES0' }");
    qapi_event_send_test_event_features0(42);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/event/event_a", test_event_a);
    g_test_add_func("/event/event_b", test_event_b);
    g_test_add_func("/event/event_c", test_event_c);
    g_test_add_func("/event/event_d", test_event_d);
    g_test_add_func("/event/deprecated", test_event_deprecated);
    g_test_add_func("/event/deprecated_data", test_event_deprecated_data);
    g_test_run();

    return 0;
}
