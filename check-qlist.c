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
#include <glib.h>

#include "qint.h"
#include "qlist.h"

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

    // destroy doesn't exist yet
    g_free(qlist);
}

static void qlist_append_test(void)
{
    QInt *qi;
    QList *qlist;
    QListEntry *entry;

    qi = qint_from_int(42);

    qlist = qlist_new();
    qlist_append(qlist, qi);

    entry = QTAILQ_FIRST(&qlist->head);
    g_assert(entry != NULL);
    g_assert(entry->value == QOBJECT(qi));

    // destroy doesn't exist yet
    QDECREF(qi);
    g_free(entry);
    g_free(qlist);
}

static void qobject_to_qlist_test(void)
{
    QList *qlist;

    qlist = qlist_new();

    g_assert(qobject_to_qlist(QOBJECT(qlist)) == qlist);

    // destroy doesn't exist yet
    g_free(qlist);
}

static void qlist_destroy_test(void)
{
    int i;
    QList *qlist;

    qlist = qlist_new();

    for (i = 0; i < 42; i++)
        qlist_append(qlist, qint_from_int(i));

    QDECREF(qlist);
}

static int iter_called;
static const int iter_max = 42;

static void iter_func(QObject *obj, void *opaque)
{
    QInt *qi;

    g_assert(opaque == NULL);

    qi = qobject_to_qint(obj);
    g_assert(qi != NULL);
    g_assert((qint_get_int(qi) >= 0) && (qint_get_int(qi) <= iter_max));

    iter_called++;
}

static void qlist_iter_test(void)
{
    int i;
    QList *qlist;

    qlist = qlist_new();

    for (i = 0; i < iter_max; i++)
        qlist_append(qlist, qint_from_int(i));

    iter_called = 0;
    qlist_iter(qlist, iter_func, NULL);

    g_assert(iter_called == iter_max);

    QDECREF(qlist);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/public/new", qlist_new_test);
    g_test_add_func("/public/append", qlist_append_test);
    g_test_add_func("/public/to_qlist", qobject_to_qlist_test);
    g_test_add_func("/public/destroy", qlist_destroy_test);
    g_test_add_func("/public/iter", qlist_iter_test);

    return g_test_run();
}
