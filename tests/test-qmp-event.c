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
#include <glib.h>

#include "qemu-common.h"
#include "test-qapi-types.h"
#include "test-qapi-visit.h"
#include "test-qapi-event.h"
#include "qapi/qmp/types.h"
#include "qapi/qmp/qint.h"
#include "qapi/qmp/qobject.h"
#include "qapi/qmp-event.h"

typedef struct TestEventData {
    QDict *expect;
} TestEventData;

typedef struct QDictCmpData {
    QDict *expect;
    bool result;
} QDictCmpData;

TestEventData *test_event_data;
static CompatGMutex test_event_lock;

/* Only compares bool, int, string */
static
void qdict_cmp_do_simple(const char *key, QObject *obj1, void *opaque)

{
    QObject *obj2;
    QDictCmpData d_new, *d = opaque;

    if (!d->result) {
        return;
    }

    obj2 = qdict_get(d->expect, key);
    if (!obj2) {
        d->result = false;
        return;
    }

    if (qobject_type(obj1) != qobject_type(obj2)) {
        d->result = false;
        return;
    }

    switch (qobject_type(obj1)) {
    case QTYPE_QBOOL:
        d->result = (qbool_get_bool(qobject_to_qbool(obj1)) ==
                     qbool_get_bool(qobject_to_qbool(obj2)));
        return;
    case QTYPE_QINT:
        d->result = (qint_get_int(qobject_to_qint(obj1)) ==
                     qint_get_int(qobject_to_qint(obj2)));
        return;
    case QTYPE_QSTRING:
        d->result = g_strcmp0(qstring_get_str(qobject_to_qstring(obj1)),
                              qstring_get_str(qobject_to_qstring(obj2))) == 0;
        return;
    case QTYPE_QDICT:
        d_new.expect = qobject_to_qdict(obj2);
        d_new.result = true;
        qdict_iter(qobject_to_qdict(obj1), qdict_cmp_do_simple, &d_new);
        d->result = d_new.result;
        return;
    default:
        abort();
    }
}

static bool qdict_cmp_simple(QDict *a, QDict *b)
{
    QDictCmpData d;

    d.expect = b;
    d.result = true;
    qdict_iter(a, qdict_cmp_do_simple, &d);
    return d.result;
}

/* This function is hooked as final emit function, which can verify the
   correctness. */
static void event_test_emit(test_QAPIEvent event, QDict *d, Error **errp)
{
    QObject *obj;
    QDict *t;
    int64_t s, ms;

    /* Verify that we have timestamp, then remove it to compare other fields */
    obj = qdict_get(d, "timestamp");
    g_assert(obj);
    t = qobject_to_qdict(obj);
    g_assert(t);
    obj = qdict_get(t, "seconds");
    g_assert(obj && qobject_type(obj) == QTYPE_QINT);
    s = qint_get_int(qobject_to_qint(obj));
    obj = qdict_get(t, "microseconds");
    g_assert(obj && qobject_type(obj) == QTYPE_QINT);
    ms = qint_get_int(qobject_to_qint(obj));
    if (s == -1) {
        g_assert(ms == -1);
    } else {
        g_assert(ms >= 0 && ms <= 999999);
    }
    g_assert(qdict_size(t) == 2);

    qdict_del(d, "timestamp");

    g_assert(qdict_cmp_simple(d, test_event_data->expect));

}

static void event_prepare(TestEventData *data,
                          const void *unused)
{
    /* Global variable test_event_data was used to pass the expectation, so
       test cases can't be executed at same time. */
    g_mutex_lock(&test_event_lock);

    data->expect = qdict_new();
    test_event_data = data;
}

static void event_teardown(TestEventData *data,
                           const void *unused)
{
    QDECREF(data->expect);
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
    QDict *d;
    d = data->expect;
    qdict_put(d, "event", qstring_from_str("EVENT_A"));
    qapi_event_send_event_a(&error_abort);
}

static void test_event_b(TestEventData *data,
                         const void *unused)
{
    QDict *d;
    d = data->expect;
    qdict_put(d, "event", qstring_from_str("EVENT_B"));
    qapi_event_send_event_b(&error_abort);
}

static void test_event_c(TestEventData *data,
                         const void *unused)
{
    QDict *d, *d_data, *d_b;

    UserDefOne b;
    b.integer = 2;
    b.string = g_strdup("test1");
    b.has_enum1 = false;

    d_b = qdict_new();
    qdict_put(d_b, "integer", qint_from_int(2));
    qdict_put(d_b, "string", qstring_from_str("test1"));

    d_data = qdict_new();
    qdict_put(d_data, "a", qint_from_int(1));
    qdict_put(d_data, "b", d_b);
    qdict_put(d_data, "c", qstring_from_str("test2"));

    d = data->expect;
    qdict_put(d, "event", qstring_from_str("EVENT_C"));
    qdict_put(d, "data", d_data);

    qapi_event_send_event_c(true, 1, true, &b, "test2", &error_abort);

    g_free(b.string);
}

/* Complex type */
static void test_event_d(TestEventData *data,
                         const void *unused)
{
    UserDefOne struct1;
    EventStructOne a;
    QDict *d, *d_data, *d_a, *d_struct1;

    struct1.integer = 2;
    struct1.string = g_strdup("test1");
    struct1.has_enum1 = true;
    struct1.enum1 = ENUM_ONE_VALUE1;

    a.struct1 = &struct1;
    a.string = g_strdup("test2");
    a.has_enum2 = true;
    a.enum2 = ENUM_ONE_VALUE2;

    d_struct1 = qdict_new();
    qdict_put(d_struct1, "integer", qint_from_int(2));
    qdict_put(d_struct1, "string", qstring_from_str("test1"));
    qdict_put(d_struct1, "enum1", qstring_from_str("value1"));

    d_a = qdict_new();
    qdict_put(d_a, "struct1", d_struct1);
    qdict_put(d_a, "string", qstring_from_str("test2"));
    qdict_put(d_a, "enum2", qstring_from_str("value2"));

    d_data = qdict_new();
    qdict_put(d_data, "a", d_a);
    qdict_put(d_data, "b", qstring_from_str("test3"));
    qdict_put(d_data, "enum3", qstring_from_str("value3"));

    d = data->expect;
    qdict_put(d, "event", qstring_from_str("EVENT_D"));
    qdict_put(d, "data", d_data);

    qapi_event_send_event_d(&a, "test3", false, NULL, true, ENUM_ONE_VALUE3,
                           &error_abort);

    g_free(struct1.string);
    g_free(a.string);
}

int main(int argc, char **argv)
{
#if !GLIB_CHECK_VERSION(2, 31, 0)
    if (!g_thread_supported()) {
       g_thread_init(NULL);
    }
#endif

    qmp_event_set_func_emit(event_test_emit);

    g_test_init(&argc, &argv, NULL);

    event_test_add("/event/event_a", test_event_a);
    event_test_add("/event/event_b", test_event_b);
    event_test_add("/event/event_c", test_event_c);
    event_test_add("/event/event_d", test_event_d);
    g_test_run();

    return 0;
}
