/*
 * QFloat unit-tests.
 *
 * Copyright IBM, Corp. 2009
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */
#include <check.h>

#include "qfloat.h"
#include "qemu-common.h"

/*
 * Public Interface test-cases
 *
 * (with some violations to access 'private' data)
 */

START_TEST(qfloat_from_double_test)
{
    QFloat *qf;
    const double value = -42.23423;

    qf = qfloat_from_double(value);
    fail_unless(qf != NULL);
    fail_unless(qf->value == value);
    fail_unless(qf->base.refcnt == 1);
    fail_unless(qobject_type(QOBJECT(qf)) == QTYPE_QFLOAT);

    // destroy doesn't exit yet
    g_free(qf);
}
END_TEST

START_TEST(qfloat_destroy_test)
{
    QFloat *qf = qfloat_from_double(0.0);
    QDECREF(qf);
}
END_TEST

static Suite *qfloat_suite(void)
{
    Suite *s;
    TCase *qfloat_public_tcase;

    s = suite_create("QFloat test-suite");

    qfloat_public_tcase = tcase_create("Public Interface");
    suite_add_tcase(s, qfloat_public_tcase);
    tcase_add_test(qfloat_public_tcase, qfloat_from_double_test);
    tcase_add_test(qfloat_public_tcase, qfloat_destroy_test);

    return s;
}

int main(void)
{
    int nf;
    Suite *s;
    SRunner *sr;

    s = qfloat_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    nf = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
