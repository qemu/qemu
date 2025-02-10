/*
 * QLit unit-tests.
 *
 * Copyright (C) 2017 Red Hat Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qobject/qbool.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"
#include "qobject/qlit.h"
#include "qobject/qnum.h"
#include "qobject/qstring.h"

static QLitObject qlit = QLIT_QDICT(((QLitDictEntry[]) {
    { "foo", QLIT_QNUM(42) },
    { "bar", QLIT_QSTR("hello world") },
    { "baz", QLIT_QNULL },
    { "bee", QLIT_QLIST(((QLitObject[]) {
        QLIT_QNUM(43),
        QLIT_QNUM(44),
        QLIT_QBOOL(true),
        { },
    }))},
    { },
}));

static QLitObject qlit_foo = QLIT_QDICT(((QLitDictEntry[]) {
    { "foo", QLIT_QNUM(42) },
    { },
}));

static QObject *make_qobject(void)
{
    QDict *qdict = qdict_new();
    QList *list = qlist_new();

    qdict_put_int(qdict, "foo", 42);
    qdict_put_str(qdict, "bar", "hello world");
    qdict_put_null(qdict, "baz");

    qlist_append_int(list, 43);
    qlist_append_int(list, 44);
    qlist_append_bool(list, true);
    qdict_put(qdict, "bee", list);

    return QOBJECT(qdict);
}

static void qlit_equal_qobject_test(void)
{
    QObject *qobj = make_qobject();

    g_assert(qlit_equal_qobject(&qlit, qobj));

    g_assert(!qlit_equal_qobject(&qlit_foo, qobj));

    qdict_put(qobject_to(QDict, qobj), "bee", qlist_new());
    g_assert(!qlit_equal_qobject(&qlit, qobj));

    qobject_unref(qobj);
}

static void qobject_from_qlit_test(void)
{
    QObject *obj, *qobj = qobject_from_qlit(&qlit);
    QDict *qdict;
    QList *bee;

    qdict = qobject_to(QDict, qobj);
    g_assert_cmpint(qdict_get_int(qdict, "foo"), ==, 42);
    g_assert_cmpstr(qdict_get_str(qdict, "bar"), ==, "hello world");
    g_assert(qobject_type(qdict_get(qdict, "baz")) == QTYPE_QNULL);

    bee = qdict_get_qlist(qdict, "bee");
    obj = qlist_pop(bee);
    g_assert_cmpint(qnum_get_int(qobject_to(QNum, obj)), ==, 43);
    qobject_unref(obj);
    obj = qlist_pop(bee);
    g_assert_cmpint(qnum_get_int(qobject_to(QNum, obj)), ==, 44);
    qobject_unref(obj);
    obj = qlist_pop(bee);
    g_assert(qbool_get_bool(qobject_to(QBool, obj)));
    qobject_unref(obj);

    qobject_unref(qobj);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/qlit/equal_qobject", qlit_equal_qobject_test);
    g_test_add_func("/qlit/qobject_from_qlit", qobject_from_qlit_test);

    return g_test_run();
}
