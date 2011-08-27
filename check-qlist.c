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
#include <check.h>

#include "qint.h"
#include "qlist.h"

/*
 * Public Interface test-cases
 *
 * (with some violations to access 'private' data)
 */

START_TEST(qlist_new_test)
{
    QList *qlist;

    qlist = qlist_new();
    fail_unless(qlist != NULL);
    fail_unless(qlist->base.refcnt == 1);
    fail_unless(qobject_type(QOBJECT(qlist)) == QTYPE_QLIST);

    // destroy doesn't exist yet
    g_free(qlist);
}
END_TEST

START_TEST(qlist_append_test)
{
    QInt *qi;
    QList *qlist;
    QListEntry *entry;

    qi = qint_from_int(42);

    qlist = qlist_new();
    qlist_append(qlist, qi);

    entry = QTAILQ_FIRST(&qlist->head);
    fail_unless(entry != NULL);
    fail_unless(entry->value == QOBJECT(qi));

    // destroy doesn't exist yet
    QDECREF(qi);
    g_free(entry);
    g_free(qlist);
}
END_TEST

START_TEST(qobject_to_qlist_test)
{
    QList *qlist;

    qlist = qlist_new();

    fail_unless(qobject_to_qlist(QOBJECT(qlist)) == qlist);

    // destroy doesn't exist yet
    g_free(qlist);
}
END_TEST

START_TEST(qlist_destroy_test)
{
    int i;
    QList *qlist;

    qlist = qlist_new();

    for (i = 0; i < 42; i++)
        qlist_append(qlist, qint_from_int(i));

    QDECREF(qlist);
}
END_TEST

static int iter_called;
static const int iter_max = 42;

static void iter_func(QObject *obj, void *opaque)
{
    QInt *qi;

    fail_unless(opaque == NULL);

    qi = qobject_to_qint(obj);
    fail_unless(qi != NULL);
    fail_unless((qint_get_int(qi) >= 0) && (qint_get_int(qi) <= iter_max));

    iter_called++;
}

START_TEST(qlist_iter_test)
{
    int i;
    QList *qlist;

    qlist = qlist_new();

    for (i = 0; i < iter_max; i++)
        qlist_append(qlist, qint_from_int(i));

    iter_called = 0;
    qlist_iter(qlist, iter_func, NULL);

    fail_unless(iter_called == iter_max);

    QDECREF(qlist);
}
END_TEST

static Suite *QList_suite(void)
{
    Suite *s;
    TCase *qlist_public_tcase;

    s = suite_create("QList suite");

    qlist_public_tcase = tcase_create("Public Interface");
    suite_add_tcase(s, qlist_public_tcase);
    tcase_add_test(qlist_public_tcase, qlist_new_test);
    tcase_add_test(qlist_public_tcase, qlist_append_test);
    tcase_add_test(qlist_public_tcase, qobject_to_qlist_test);
    tcase_add_test(qlist_public_tcase, qlist_destroy_test);
    tcase_add_test(qlist_public_tcase, qlist_iter_test);

    return s;
}

int main(void)
{
	int nf;
	Suite *s;
	SRunner *sr;

	s = QList_suite();
	sr = srunner_create(s);

	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
