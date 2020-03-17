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

#include "qemu-common.h"
#include "qapi/error.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qnum.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qmp-event.h"
#include "test-qapi-events.h"
#include "test-qapi-emit-events.h"

typedef struct TestEventData {
    QDict *expect;
    bool emitted;
} TestEventData;

TestEventData *test_event_data;
static GMutex test_event_lock;

void test_qapi_event_emit(test_QAPIEvent event, QDict *d)
{
    QDict *t;
    int64_t s, ms;

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

    g_assert(qobject_is_equal(QOBJECT(d), QOBJECT(test_event_data->expect)));
    test_event_data->emitted = true;
}

static void event_prepare(TestEventData *data,
                          const void *unused)
{
    /* Global variable test_event_data was used to pass the expectation, so
       test cases can't be executed at same time. */
    g_mutex_lock(&test_event_lock);
    test_event_data = data;
}

static void event_teardown(TestEventData *data,
                           const void *unused)
{
    test_event_data = NULL;
    g_mutex_unlock(&test_event_lock);
}

static void event_test_add(const char *testpath,
                           void (*test_func)(TestEventData *data,
                                             const void *user_data))
{
    g_test_add(testpath, TestEventData, NULL, event_prepare, test_func,
               event_teardown);
}


/* Test cases */

static void test_event_a(TestEventData *data,
                         const void *unused)
{
    data->expect = qdict_from_jsonf_nofail("{ 'event': 'EVENT_A' }");
    qapi_event_send_event_a();
    g_assert(data->emitted);
    qobject_unref(data->expect);
}

static void test_event_b(TestEventData *data,
                         const void *unused)
{
    data->expect = qdict_from_jsonf_nofail("{ 'event': 'EVENT_B' }");
    qapi_event_send_event_b();
    g_assert(data->emitted);
    qobject_unref(data->expect);
}

static void test_event_c(TestEventData *data,
                         const void *unused)
{
    UserDefOne b = { .integer = 2, .string = (char *)"test1" };

    data->expect = qdict_from_jsonf_nofail(
        "{ 'event': 'EVENT_C', 'data': {"
        " 'a': 1, 'b': { 'integer': 2, 'string': 'test1' }, 'c': 'test2' } }");
    qapi_event_send_event_c(true, 1, true, &b, "test2");
    g_assert(data->emitted);
    qobject_unref(data->expect);
}

/* Complex type */
static void test_event_d(TestEventData *data,
                         const void *unused)
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

    data->expect = qdict_from_jsonf_nofail(
        "{ 'event': 'EVENT_D', 'data': {"
        " 'a': {"
        "  'struct1': { 'integer': 2, 'string': 'test1', 'enum1': 'value1' },"
        "  'string': 'test2', 'enum2': 'value2' },"
        " 'b': 'test3', 'enum3': 'value3' } }");
    qapi_event_send_event_d(&a, "test3", false, NULL, true, ENUM_ONE_VALUE3);
    g_assert(data->emitted);
    qobject_unref(data->expect);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    event_test_add("/event/event_a", test_event_a);
    event_test_add("/event/event_b", test_event_b);
    event_test_add("/event/event_c", test_event_c);
    event_test_add("/event/event_d", test_event_d);
    g_test_run();

    return 0;
}
