/*
 * QDict unit-tests.
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
#include "qdict.h"
#include "qstring.h"
#include "qemu-common.h"

/*
 * Public Interface test-cases
 *
 * (with some violations to access 'private' data)
 */

START_TEST(qdict_new_test)
{
    QDict *qdict;

    qdict = qdict_new();
    fail_unless(qdict != NULL);
    fail_unless(qdict_size(qdict) == 0);
    fail_unless(qdict->base.refcnt == 1);
    fail_unless(qobject_type(QOBJECT(qdict)) == QTYPE_QDICT);

    // destroy doesn't exit yet
    free(qdict);
}
END_TEST

START_TEST(qdict_put_obj_test)
{
    QInt *qi;
    QDict *qdict;
    QDictEntry *ent;
    const int num = 42;

    qdict = qdict_new();

    // key "" will have tdb hash 12345
    qdict_put_obj(qdict, "", QOBJECT(qint_from_int(num)));

    fail_unless(qdict_size(qdict) == 1);
    ent = QLIST_FIRST(&qdict->table[12345 % QDICT_BUCKET_MAX]);
    qi = qobject_to_qint(ent->value);
    fail_unless(qint_get_int(qi) == num);

    // destroy doesn't exit yet
    QDECREF(qi);
    qemu_free(ent->key);
    qemu_free(ent);
    qemu_free(qdict);
}
END_TEST

START_TEST(qdict_destroy_simple_test)
{
    QDict *qdict;

    qdict = qdict_new();
    qdict_put_obj(qdict, "num", QOBJECT(qint_from_int(0)));
    qdict_put_obj(qdict, "str", QOBJECT(qstring_from_str("foo")));

    QDECREF(qdict);
}
END_TEST

static QDict *tests_dict = NULL;

static void qdict_setup(void)
{
    tests_dict = qdict_new();
    fail_unless(tests_dict != NULL);
}

static void qdict_teardown(void)
{
    QDECREF(tests_dict);
    tests_dict = NULL;
}

START_TEST(qdict_get_test)
{
    QInt *qi;
    QObject *obj;
    const int value = -42;
    const char *key = "test";

    qdict_put(tests_dict, key, qint_from_int(value));

    obj = qdict_get(tests_dict, key);
    fail_unless(obj != NULL);

    qi = qobject_to_qint(obj);
    fail_unless(qint_get_int(qi) == value);
}
END_TEST

START_TEST(qdict_get_int_test)
{
    int ret;
    const int value = 100;
    const char *key = "int";

    qdict_put(tests_dict, key, qint_from_int(value));

    ret = qdict_get_int(tests_dict, key);
    fail_unless(ret == value);
}
END_TEST

START_TEST(qdict_get_try_int_test)
{
    int ret;
    const int value = 100;
    const char *key = "int";

    qdict_put(tests_dict, key, qint_from_int(value));

    ret = qdict_get_try_int(tests_dict, key, 0);
    fail_unless(ret == value);
}
END_TEST

START_TEST(qdict_get_str_test)
{
    const char *p;
    const char *key = "key";
    const char *str = "string";

    qdict_put(tests_dict, key, qstring_from_str(str));

    p = qdict_get_str(tests_dict, key);
    fail_unless(p != NULL);
    fail_unless(strcmp(p, str) == 0);
}
END_TEST

START_TEST(qdict_get_try_str_test)
{
    const char *p;
    const char *key = "key";
    const char *str = "string";

    qdict_put(tests_dict, key, qstring_from_str(str));

    p = qdict_get_try_str(tests_dict, key);
    fail_unless(p != NULL);
    fail_unless(strcmp(p, str) == 0);
}
END_TEST

START_TEST(qdict_haskey_not_test)
{
    fail_unless(qdict_haskey(tests_dict, "test") == 0);
}
END_TEST

START_TEST(qdict_haskey_test)
{
    const char *key = "test";

    qdict_put(tests_dict, key, qint_from_int(0));
    fail_unless(qdict_haskey(tests_dict, key) == 1);
}
END_TEST

START_TEST(qdict_del_test)
{
    const char *key = "key test";

    qdict_put(tests_dict, key, qstring_from_str("foo"));
    fail_unless(qdict_size(tests_dict) == 1);

    qdict_del(tests_dict, key);

    fail_unless(qdict_size(tests_dict) == 0);
    fail_unless(qdict_haskey(tests_dict, key) == 0);
}
END_TEST

START_TEST(qobject_to_qdict_test)
{
    fail_unless(qobject_to_qdict(QOBJECT(tests_dict)) == tests_dict);
}
END_TEST

START_TEST(qdict_iterapi_test)
{
    int count;
    const QDictEntry *ent;

    fail_unless(qdict_first(tests_dict) == NULL);

    qdict_put(tests_dict, "key1", qint_from_int(1));
    qdict_put(tests_dict, "key2", qint_from_int(2));
    qdict_put(tests_dict, "key3", qint_from_int(3));

    count = 0;
    for (ent = qdict_first(tests_dict); ent; ent = qdict_next(tests_dict, ent)){
        fail_unless(qdict_haskey(tests_dict, qdict_entry_key(ent)) == 1);
        count++;
    }

    fail_unless(count == qdict_size(tests_dict));

    /* Do it again to test restarting */
    count = 0;
    for (ent = qdict_first(tests_dict); ent; ent = qdict_next(tests_dict, ent)){
        fail_unless(qdict_haskey(tests_dict, qdict_entry_key(ent)) == 1);
        count++;
    }

    fail_unless(count == qdict_size(tests_dict));
}
END_TEST

/*
 * Errors test-cases
 */

START_TEST(qdict_put_exists_test)
{
    int value;
    const char *key = "exists";

    qdict_put(tests_dict, key, qint_from_int(1));
    qdict_put(tests_dict, key, qint_from_int(2));

    value = qdict_get_int(tests_dict, key);
    fail_unless(value == 2);

    fail_unless(qdict_size(tests_dict) == 1);
}
END_TEST

START_TEST(qdict_get_not_exists_test)
{
    fail_unless(qdict_get(tests_dict, "foo") == NULL);
}
END_TEST

/*
 * Stress test-case
 *
 * This is a lot big for a unit-test, but there is no other place
 * to have it.
 */

static void remove_dots(char *string)
{
    char *p = strchr(string, ':');
    if (p)
        *p = '\0';
}

static QString *read_line(FILE *file, char *key)
{
    char value[128];

    if (fscanf(file, "%s%s", key, value) == EOF)
        return NULL;
    remove_dots(key);
    return qstring_from_str(value);
}

#define reset_file(file)    fseek(file, 0L, SEEK_SET)

START_TEST(qdict_stress_test)
{
    size_t lines;
    char key[128];
    FILE *test_file;
    QDict *qdict;
    QString *value;
    const char *test_file_path = "qdict-test-data.txt";

    test_file = fopen(test_file_path, "r");
    fail_unless(test_file != NULL);

    // Create the dict
    qdict = qdict_new();
    fail_unless(qdict != NULL);

    // Add everything from the test file
    for (lines = 0;; lines++) {
        value = read_line(test_file, key);
        if (!value)
            break;

        qdict_put(qdict, key, value);
    }
    fail_unless(qdict_size(qdict) == lines);

    // Check if everything is really in there
    reset_file(test_file);
    for (;;) {
        const char *str1, *str2;

        value = read_line(test_file, key);
        if (!value)
            break;

        str1 = qstring_get_str(value);

        str2 = qdict_get_str(qdict, key);
        fail_unless(str2 != NULL);

        fail_unless(strcmp(str1, str2) == 0);

        QDECREF(value);
    }

    // Delete everything
    reset_file(test_file);
    for (;;) {
        value = read_line(test_file, key);
        if (!value)
            break;

        qdict_del(qdict, key);
        QDECREF(value);

        fail_unless(qdict_haskey(qdict, key) == 0);
    }
    fclose(test_file);

    fail_unless(qdict_size(qdict) == 0);
    QDECREF(qdict);
}
END_TEST

static Suite *qdict_suite(void)
{
    Suite *s;
    TCase *qdict_public_tcase;
    TCase *qdict_public2_tcase;
    TCase *qdict_stress_tcase;
    TCase *qdict_errors_tcase;

    s = suite_create("QDict test-suite");

    qdict_public_tcase = tcase_create("Public Interface");
    suite_add_tcase(s, qdict_public_tcase);
    tcase_add_test(qdict_public_tcase, qdict_new_test);
    tcase_add_test(qdict_public_tcase, qdict_put_obj_test);
    tcase_add_test(qdict_public_tcase, qdict_destroy_simple_test);

    /* Continue, but now with fixtures */
    qdict_public2_tcase = tcase_create("Public Interface (2)");
    suite_add_tcase(s, qdict_public2_tcase);
    tcase_add_checked_fixture(qdict_public2_tcase, qdict_setup, qdict_teardown);
    tcase_add_test(qdict_public2_tcase, qdict_get_test);
    tcase_add_test(qdict_public2_tcase, qdict_get_int_test);
    tcase_add_test(qdict_public2_tcase, qdict_get_try_int_test);
    tcase_add_test(qdict_public2_tcase, qdict_get_str_test);
    tcase_add_test(qdict_public2_tcase, qdict_get_try_str_test);
    tcase_add_test(qdict_public2_tcase, qdict_haskey_not_test);
    tcase_add_test(qdict_public2_tcase, qdict_haskey_test);
    tcase_add_test(qdict_public2_tcase, qdict_del_test);
    tcase_add_test(qdict_public2_tcase, qobject_to_qdict_test);
    tcase_add_test(qdict_public2_tcase, qdict_iterapi_test);

    qdict_errors_tcase = tcase_create("Errors");
    suite_add_tcase(s, qdict_errors_tcase);
    tcase_add_checked_fixture(qdict_errors_tcase, qdict_setup, qdict_teardown);
    tcase_add_test(qdict_errors_tcase, qdict_put_exists_test);
    tcase_add_test(qdict_errors_tcase, qdict_get_not_exists_test);

    /* The Big one */
    qdict_stress_tcase = tcase_create("Stress Test");
    suite_add_tcase(s, qdict_stress_tcase);
    tcase_add_test(qdict_stress_tcase, qdict_stress_test);

    return s;
}

int main(void)
{
	int nf;
	Suite *s;
	SRunner *sr;

	s = qdict_suite();
	sr = srunner_create(s);

	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
