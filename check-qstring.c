/*
 * QString unit-tests.
 *
 * Copyright (C) 2009 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 */
#include <check.h>

#include "qstring.h"
#include "qemu-common.h"

/*
 * Public Interface test-cases
 *
 * (with some violations to access 'private' data)
 */

START_TEST(qstring_from_str_test)
{
    QString *qstring;
    const char *str = "QEMU";

    qstring = qstring_from_str(str);
    fail_unless(qstring != NULL);
    fail_unless(qstring->base.refcnt == 1);
    fail_unless(strcmp(str, qstring->string) == 0);
    fail_unless(qobject_type(QOBJECT(qstring)) == QTYPE_QSTRING);

    // destroy doesn't exit yet
    qemu_free(qstring->string);
    qemu_free(qstring);
}
END_TEST

START_TEST(qstring_destroy_test)
{
    QString *qstring = qstring_from_str("destroy test");
    QDECREF(qstring);
}
END_TEST

START_TEST(qstring_get_str_test)
{
    QString *qstring;
    const char *ret_str;
    const char *str = "QEMU/KVM";

    qstring = qstring_from_str(str);
    ret_str = qstring_get_str(qstring);
    fail_unless(strcmp(ret_str, str) == 0);

    QDECREF(qstring);
}
END_TEST

START_TEST(qobject_to_qstring_test)
{
    QString *qstring;

    qstring = qstring_from_str("foo");
    fail_unless(qobject_to_qstring(QOBJECT(qstring)) == qstring);

    QDECREF(qstring);
}
END_TEST

static Suite *qstring_suite(void)
{
    Suite *s;
    TCase *qstring_public_tcase;

    s = suite_create("QString test-suite");

    qstring_public_tcase = tcase_create("Public Interface");
    suite_add_tcase(s, qstring_public_tcase);
    tcase_add_test(qstring_public_tcase, qstring_from_str_test);
    tcase_add_test(qstring_public_tcase, qstring_destroy_test);
    tcase_add_test(qstring_public_tcase, qstring_get_str_test);
    tcase_add_test(qstring_public_tcase, qobject_to_qstring_test);

    return s;
}

int main(void)
{
	int nf;
	Suite *s;
	SRunner *sr;

	s = qstring_suite();
	sr = srunner_create(s);

	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
