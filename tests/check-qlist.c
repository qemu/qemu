/*
 * QList unit-tests.
 *
 * Copyright (C) 2009 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */
#include "qemu/osdep.h"

#include "qapi/qmp/qnum.h"
#include "qapi/qmp/qlist.h"

/*
 * Public Interface test-cases
 *
 * (with some violations to access 'private' data)
 */

static void qlist_new_test(void)
{
    QList *qlist;

    qlist = qlist_new();
    g_assert(qlist != NULL);
    g_assert(qlist->base.refcnt == 1);
    g_assert(qobject_type(QOBJECT(qlist)) == QTYPE_QLIST);

    qobject_unref(qlist);
}

static void qlist_append_test(void)
{
    QNum *qi;
    QList *qlist;
    QListEntry *entry;

    qi = qnum_from_int(42);

    qlist = qlist_new();
    qlist_append(qlist, qi);

    entry = QTAILQ_FIRST(&qlist->head);
    g_assert(entry != NULL);
    g_assert(entry->value == QOBJECT(qi));

    qobject_unref(qlist);
}

static void qobject_to_qlist_test(void)
{
    QList *qlist;

    qlist = qlist_new();

    g_assert(qobject_to(QList, QOBJECT(qlist)) == qlist);

    qobject_unref(qlist);
}

static int iter_called;
static const int iter_max = 42;

static void iter_func(QObject *obj, void *opaque)
{
    QNum *qi;
    int64_t val;

    g_assert(opaque == NULL);

    qi = qobject_to(QNum, obj);
    g_assert(qi != NULL);

    g_assert(qnum_get_try_int(qi, &val));
    g_assert_cmpint(val, >=, 0);
    g_assert_cmpint(val, <=, iter_max);

    iter_called++;
}

static void qlist_iter_test(void)
{
    int i;
    QList *qlist;

    qlist = qlist_new();

    for (i = 0; i < iter_max; i++)
        qlist_append_int(qlist, i);

    iter_called = 0;
    qlist_iter(qlist, iter_func, NULL);

    g_assert(iter_called == iter_max);

    qobject_unref(qlist);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/public/new", qlist_new_test);
    g_test_add_func("/public/append", qlist_append_test);
    g_test_add_func("/public/to_qlist", qobject_to_qlist_test);
    g_test_add_func("/public/iter", qlist_iter_test);

    return g_test_run();
}
