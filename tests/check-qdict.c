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

#include "qemu/osdep.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qnum.h"
#include "qapi/qmp/qstring.h"

/*
 * Public Interface test-cases
 *
 * (with some violations to access 'private' data)
 */

static void qdict_new_test(void)
{
    QDict *qdict;

    qdict = qdict_new();
    g_assert(qdict != NULL);
    g_assert(qdict_size(qdict) == 0);
    g_assert(qdict->base.refcnt == 1);
    g_assert(qobject_type(QOBJECT(qdict)) == QTYPE_QDICT);

    qobject_unref(qdict);
}

static void qdict_put_obj_test(void)
{
    QNum *qn;
    QDict *qdict;
    QDictEntry *ent;
    const int num = 42;

    qdict = qdict_new();

    // key "" will have tdb hash 12345
    qdict_put_int(qdict, "", num);

    g_assert(qdict_size(qdict) == 1);
    ent = QLIST_FIRST(&qdict->table[12345 % QDICT_BUCKET_MAX]);
    qn = qobject_to(QNum, ent->value);
    g_assert_cmpint(qnum_get_int(qn), ==, num);

    qobject_unref(qdict);
}

static void qdict_destroy_simple_test(void)
{
    QDict *qdict;

    qdict = qdict_new();
    qdict_put_int(qdict, "num", 0);
    qdict_put_str(qdict, "str", "foo");

    qobject_unref(qdict);
}

static void qdict_get_test(void)
{
    QNum *qn;
    QObject *obj;
    const int value = -42;
    const char *key = "test";
    QDict *tests_dict = qdict_new();

    qdict_put_int(tests_dict, key, value);

    obj = qdict_get(tests_dict, key);
    g_assert(obj != NULL);

    qn = qobject_to(QNum, obj);
    g_assert_cmpint(qnum_get_int(qn), ==, value);

    qobject_unref(tests_dict);
}

static void qdict_get_int_test(void)
{
    int ret;
    const int value = 100;
    const char *key = "int";
    QDict *tests_dict = qdict_new();

    qdict_put_int(tests_dict, key, value);

    ret = qdict_get_int(tests_dict, key);
    g_assert(ret == value);

    qobject_unref(tests_dict);
}

static void qdict_get_try_int_test(void)
{
    int ret;
    const int value = 100;
    const char *key = "int";
    QDict *tests_dict = qdict_new();

    qdict_put_int(tests_dict, key, value);
    qdict_put_str(tests_dict, "string", "test");

    ret = qdict_get_try_int(tests_dict, key, 0);
    g_assert(ret == value);

    ret = qdict_get_try_int(tests_dict, "missing", -42);
    g_assert_cmpuint(ret, ==, -42);

    ret = qdict_get_try_int(tests_dict, "string", -42);
    g_assert_cmpuint(ret, ==, -42);

    qobject_unref(tests_dict);
}

static void qdict_get_str_test(void)
{
    const char *p;
    const char *key = "key";
    const char *str = "string";
    QDict *tests_dict = qdict_new();

    qdict_put_str(tests_dict, key, str);

    p = qdict_get_str(tests_dict, key);
    g_assert(p != NULL);
    g_assert(strcmp(p, str) == 0);

    qobject_unref(tests_dict);
}

static void qdict_get_try_str_test(void)
{
    const char *p;
    const char *key = "key";
    const char *str = "string";
    QDict *tests_dict = qdict_new();

    qdict_put_str(tests_dict, key, str);

    p = qdict_get_try_str(tests_dict, key);
    g_assert(p != NULL);
    g_assert(strcmp(p, str) == 0);

    qobject_unref(tests_dict);
}

static void qdict_haskey_not_test(void)
{
    QDict *tests_dict = qdict_new();
    g_assert(qdict_haskey(tests_dict, "test") == 0);

    qobject_unref(tests_dict);
}

static void qdict_haskey_test(void)
{
    const char *key = "test";
    QDict *tests_dict = qdict_new();

    qdict_put_int(tests_dict, key, 0);
    g_assert(qdict_haskey(tests_dict, key) == 1);

    qobject_unref(tests_dict);
}

static void qdict_del_test(void)
{
    const char *key = "key test";
    QDict *tests_dict = qdict_new();

    qdict_put_str(tests_dict, key, "foo");
    g_assert(qdict_size(tests_dict) == 1);

    qdict_del(tests_dict, key);

    g_assert(qdict_size(tests_dict) == 0);
    g_assert(qdict_haskey(tests_dict, key) == 0);

    qobject_unref(tests_dict);
}

static void qobject_to_qdict_test(void)
{
    QDict *tests_dict = qdict_new();
    g_assert(qobject_to(QDict, QOBJECT(tests_dict)) == tests_dict);

    qobject_unref(tests_dict);
}

static void qdict_iterapi_test(void)
{
    int count;
    const QDictEntry *ent;
    QDict *tests_dict = qdict_new();

    g_assert(qdict_first(tests_dict) == NULL);

    qdict_put_int(tests_dict, "key1", 1);
    qdict_put_int(tests_dict, "key2", 2);
    qdict_put_int(tests_dict, "key3", 3);

    count = 0;
    for (ent = qdict_first(tests_dict); ent; ent = qdict_next(tests_dict, ent)){
        g_assert(qdict_haskey(tests_dict, qdict_entry_key(ent)) == 1);
        count++;
    }

    g_assert(count == qdict_size(tests_dict));

    /* Do it again to test restarting */
    count = 0;
    for (ent = qdict_first(tests_dict); ent; ent = qdict_next(tests_dict, ent)){
        g_assert(qdict_haskey(tests_dict, qdict_entry_key(ent)) == 1);
        count++;
    }

    g_assert(count == qdict_size(tests_dict));

    qobject_unref(tests_dict);
}

/*
 * Errors test-cases
 */

static void qdict_put_exists_test(void)
{
    int value;
    const char *key = "exists";
    QDict *tests_dict = qdict_new();

    qdict_put_int(tests_dict, key, 1);
    qdict_put_int(tests_dict, key, 2);

    value = qdict_get_int(tests_dict, key);
    g_assert(value == 2);

    g_assert(qdict_size(tests_dict) == 1);

    qobject_unref(tests_dict);
}

static void qdict_get_not_exists_test(void)
{
    QDict *tests_dict = qdict_new();
    g_assert(qdict_get(tests_dict, "foo") == NULL);

    qobject_unref(tests_dict);
}

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

    if (fscanf(file, "%127s%127s", key, value) == EOF) {
        return NULL;
    }
    remove_dots(key);
    return qstring_from_str(value);
}

#define reset_file(file)    fseek(file, 0L, SEEK_SET)

static void qdict_stress_test(void)
{
    size_t lines;
    char key[128];
    FILE *test_file;
    QDict *qdict;
    QString *value;
    const char *test_file_path = "qdict-test-data.txt";

    test_file = fopen(test_file_path, "r");
    g_assert(test_file != NULL);

    // Create the dict
    qdict = qdict_new();
    g_assert(qdict != NULL);

    // Add everything from the test file
    for (lines = 0;; lines++) {
        value = read_line(test_file, key);
        if (!value)
            break;

        qdict_put(qdict, key, value);
    }
    g_assert(qdict_size(qdict) == lines);

    // Check if everything is really in there
    reset_file(test_file);
    for (;;) {
        const char *str1, *str2;

        value = read_line(test_file, key);
        if (!value)
            break;

        str1 = qstring_get_str(value);

        str2 = qdict_get_str(qdict, key);
        g_assert(str2 != NULL);

        g_assert(strcmp(str1, str2) == 0);

        qobject_unref(value);
    }

    // Delete everything
    reset_file(test_file);
    for (;;) {
        value = read_line(test_file, key);
        if (!value)
            break;

        qdict_del(qdict, key);
        qobject_unref(value);

        g_assert(qdict_haskey(qdict, key) == 0);
    }
    fclose(test_file);

    g_assert(qdict_size(qdict) == 0);
    qobject_unref(qdict);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/public/new", qdict_new_test);
    g_test_add_func("/public/put_obj", qdict_put_obj_test);
    g_test_add_func("/public/destroy_simple", qdict_destroy_simple_test);

    /* Continue, but now with fixtures */
    g_test_add_func("/public/get", qdict_get_test);
    g_test_add_func("/public/get_int", qdict_get_int_test);
    g_test_add_func("/public/get_try_int", qdict_get_try_int_test);
    g_test_add_func("/public/get_str", qdict_get_str_test);
    g_test_add_func("/public/get_try_str", qdict_get_try_str_test);
    g_test_add_func("/public/haskey_not", qdict_haskey_not_test);
    g_test_add_func("/public/haskey", qdict_haskey_test);
    g_test_add_func("/public/del", qdict_del_test);
    g_test_add_func("/public/to_qdict", qobject_to_qdict_test);
    g_test_add_func("/public/iterapi", qdict_iterapi_test);

    g_test_add_func("/errors/put_exists", qdict_put_exists_test);
    g_test_add_func("/errors/get_not_exists", qdict_get_not_exists_test);

    /* The Big one */
    if (g_test_slow()) {
        g_test_add_func("/stress/test", qdict_stress_test);
    }

    return g_test_run();
}
