/*
 * QInt unit-tests.
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
#include "qemu-common.h"

/*
 * Public Interface test-cases
 *
 * (with some violations to access 'private' data)
 */

START_TEST(qint_from_int_test)
{
    QInt *qi;
    const int value = -42;

    qi = qint_from_int(value);
    fail_unless(qi != NULL);
    fail_unless(qi->value == value);
    fail_unless(qi->base.refcnt == 1);
    fail_unless(qobject_type(QOBJECT(qi)) == QTYPE_QINT);

    // destroy doesn't exit yet
    qemu_free(qi);
}
END_TEST

START_TEST(qint_destroy_test)
{
    QInt *qi = qint_from_int(0);
    QDECREF(qi);
}
END_TEST

START_TEST(qint_from_int64_test)
{
    QInt *qi;
    const int64_t value = 0x1234567890abcdefLL;

    qi = qint_from_int(value);
    fail_unless((int64_t) qi->value == value);

    QDECREF(qi);
}
END_TEST

START_TEST(qint_get_int_test)
{
    QInt *qi;
    const int value = 123456;

    qi = qint_from_int(value);
    fail_unless(qint_get_int(qi) == value);

    QDECREF(qi);
}
END_TEST

START_TEST(qobject_to_qint_test)
{
    QInt *qi;

    qi = qint_from_int(0);
    fail_unless(qobject_to_qint(QOBJECT(qi)) == qi);

    QDECREF(qi);
}
END_TEST

static Suite *qint_suite(void)
{
    Suite *s;
    TCase *qint_public_tcase;

    s = suite_create("QInt test-suite");

    qint_public_tcase = tcase_create("Public Interface");
    suite_add_tcase(s, qint_public_tcase);
    tcase_add_test(qint_public_tcase, qint_from_int_test);
    tcase_add_test(qint_public_tcase, qint_destroy_test);
    tcase_add_test(qint_public_tcase, qint_from_int64_test);
    tcase_add_test(qint_public_tcase, qint_get_int_test);
    tcase_add_test(qint_public_tcase, qobject_to_qint_test);

    return s;
}

int main(void)
{
	int nf;
	Suite *s;
	SRunner *sr;

	s = qint_suite();
	sr = srunner_create(s);

	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
