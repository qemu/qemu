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
#include <glib.h>

#include "qapi/qmp/qint.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"
#include "qemu-common.h"

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

    // destroy doesn't exit yet
    g_free(qdict);
}

static void qdict_put_obj_test(void)
{
    QInt *qi;
    QDict *qdict;
    QDictEntry *ent;
    const int num = 42;

    qdict = qdict_new();

    // key "" will have tdb hash 12345
    qdict_put_obj(qdict, "", QOBJECT(qint_from_int(num)));

    g_assert(qdict_size(qdict) == 1);
    ent = QLIST_FIRST(&qdict->table[12345 % QDICT_BUCKET_MAX]);
    qi = qobject_to_qint(ent->value);
    g_assert(qint_get_int(qi) == num);

    // destroy doesn't exit yet
    QDECREF(qi);
    g_free(ent->key);
    g_free(ent);
    g_free(qdict);
}

static void qdict_destroy_simple_test(void)
{
    QDict *qdict;

    qdict = qdict_new();
    qdict_put_obj(qdict, "num", QOBJECT(qint_from_int(0)));
    qdict_put_obj(qdict, "str", QOBJECT(qstring_from_str("foo")));

    QDECREF(qdict);
}

static void qdict_get_test(void)
{
    QInt *qi;
    QObject *obj;
    const int value = -42;
    const char *key = "test";
    QDict *tests_dict = qdict_new();

    qdict_put(tests_dict, key, qint_from_int(value));

    obj = qdict_get(tests_dict, key);
    g_assert(obj != NULL);

    qi = qobject_to_qint(obj);
    g_assert(qint_get_int(qi) == value);

    QDECREF(tests_dict);
}

static void qdict_get_int_test(void)
{
    int ret;
    const int value = 100;
    const char *key = "int";
    QDict *tests_dict = qdict_new();

    qdict_put(tests_dict, key, qint_from_int(value));

    ret = qdict_get_int(tests_dict, key);
    g_assert(ret == value);

    QDECREF(tests_dict);
}

static void qdict_get_try_int_test(void)
{
    int ret;
    const int value = 100;
    const char *key = "int";
    QDict *tests_dict = qdict_new();

    qdict_put(tests_dict, key, qint_from_int(value));

    ret = qdict_get_try_int(tests_dict, key, 0);
    g_assert(ret == value);

    QDECREF(tests_dict);
}

static void qdict_get_str_test(void)
{
    const char *p;
    const char *key = "key";
    const char *str = "string";
    QDict *tests_dict = qdict_new();

    qdict_put(tests_dict, key, qstring_from_str(str));

    p = qdict_get_str(tests_dict, key);
    g_assert(p != NULL);
    g_assert(strcmp(p, str) == 0);

    QDECREF(tests_dict);
}

static void qdict_get_try_str_test(void)
{
    const char *p;
    const char *key = "key";
    const char *str = "string";
    QDict *tests_dict = qdict_new();

    qdict_put(tests_dict, key, qstring_from_str(str));

    p = qdict_get_try_str(tests_dict, key);
    g_assert(p != NULL);
    g_assert(strcmp(p, str) == 0);

    QDECREF(tests_dict);
}

static void qdict_haskey_not_test(void)
{
    QDict *tests_dict = qdict_new();
    g_assert(qdict_haskey(tests_dict, "test") == 0);

    QDECREF(tests_dict);
}

static void qdict_haskey_test(void)
{
    const char *key = "test";
    QDict *tests_dict = qdict_new();

    qdict_put(tests_dict, key, qint_from_int(0));
    g_assert(qdict_haskey(tests_dict, key) == 1);

    QDECREF(tests_dict);
}

static void qdict_del_test(void)
{
    const char *key = "key test";
    QDict *tests_dict = qdict_new();

    qdict_put(tests_dict, key, qstring_from_str("foo"));
    g_assert(qdict_size(tests_dict) == 1);

    qdict_del(tests_dict, key);

    g_assert(qdict_size(tests_dict) == 0);
    g_assert(qdict_haskey(tests_dict, key) == 0);

    QDECREF(tests_dict);
}

static void qobject_to_qdict_test(void)
{
    QDict *tests_dict = qdict_new();
    g_assert(qobject_to_qdict(QOBJECT(tests_dict)) == tests_dict);

    QDECREF(tests_dict);
}

static void qdict_iterapi_test(void)
{
    int count;
    const QDictEntry *ent;
    QDict *tests_dict = qdict_new();

    g_assert(qdict_first(tests_dict) == NULL);

    qdict_put(tests_dict, "key1", qint_from_int(1));
    qdict_put(tests_dict, "key2", qint_from_int(2));
    qdict_put(tests_dict, "key3", qint_from_int(3));

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

    QDECREF(tests_dict);
}

static void qdict_flatten_test(void)
{
    QList *list1 = qlist_new();
    QList *list2 = qlist_new();
    QDict *dict1 = qdict_new();
    QDict *dict2 = qdict_new();
    QDict *dict3 = qdict_new();

    /*
     * Test the flattening of
     *
     * {
     *     "e": [
     *         42,
     *         [
     *             23,
     *             66,
     *             {
     *                 "a": 0,
     *                 "b": 1
     *             }
     *         ]
     *     ],
     *     "f": {
     *         "c": 2,
     *         "d": 3,
     *     },
     *     "g": 4
     * }
     *
     * to
     *
     * {
     *     "e.0": 42,
     *     "e.1.0": 23,
     *     "e.1.1": 66,
     *     "e.1.2.a": 0,
     *     "e.1.2.b": 1,
     *     "f.c": 2,
     *     "f.d": 3,
     *     "g": 4
     * }
     */

    qdict_put(dict1, "a", qint_from_int(0));
    qdict_put(dict1, "b", qint_from_int(1));

    qlist_append_obj(list1, QOBJECT(qint_from_int(23)));
    qlist_append_obj(list1, QOBJECT(qint_from_int(66)));
    qlist_append_obj(list1, QOBJECT(dict1));
    qlist_append_obj(list2, QOBJECT(qint_from_int(42)));
    qlist_append_obj(list2, QOBJECT(list1));

    qdict_put(dict2, "c", qint_from_int(2));
    qdict_put(dict2, "d", qint_from_int(3));
    qdict_put_obj(dict3, "e", QOBJECT(list2));
    qdict_put_obj(dict3, "f", QOBJECT(dict2));
    qdict_put(dict3, "g", qint_from_int(4));

    qdict_flatten(dict3);

    g_assert(qdict_get_int(dict3, "e.0") == 42);
    g_assert(qdict_get_int(dict3, "e.1.0") == 23);
    g_assert(qdict_get_int(dict3, "e.1.1") == 66);
    g_assert(qdict_get_int(dict3, "e.1.2.a") == 0);
    g_assert(qdict_get_int(dict3, "e.1.2.b") == 1);
    g_assert(qdict_get_int(dict3, "f.c") == 2);
    g_assert(qdict_get_int(dict3, "f.d") == 3);
    g_assert(qdict_get_int(dict3, "g") == 4);

    g_assert(qdict_size(dict3) == 8);

    QDECREF(dict3);
}

static void qdict_array_split_test(void)
{
    QDict *test_dict = qdict_new();
    QDict *dict1, *dict2;
    QInt *int1;
    QList *test_list;

    /*
     * Test the split of
     *
     * {
     *     "1.x": 0,
     *     "4.y": 1,
     *     "0.a": 42,
     *     "o.o": 7,
     *     "0.b": 23,
     *     "2": 66
     * }
     *
     * to
     *
     * [
     *     {
     *         "a": 42,
     *         "b": 23
     *     },
     *     {
     *         "x": 0
     *     },
     *     66
     * ]
     *
     * and
     *
     * {
     *     "4.y": 1,
     *     "o.o": 7
     * }
     *
     * (remaining in the old QDict)
     *
     * This example is given in the comment of qdict_array_split().
     */

    qdict_put(test_dict, "1.x", qint_from_int(0));
    qdict_put(test_dict, "4.y", qint_from_int(1));
    qdict_put(test_dict, "0.a", qint_from_int(42));
    qdict_put(test_dict, "o.o", qint_from_int(7));
    qdict_put(test_dict, "0.b", qint_from_int(23));
    qdict_put(test_dict, "2", qint_from_int(66));

    qdict_array_split(test_dict, &test_list);

    dict1 = qobject_to_qdict(qlist_pop(test_list));
    dict2 = qobject_to_qdict(qlist_pop(test_list));
    int1 = qobject_to_qint(qlist_pop(test_list));

    g_assert(dict1);
    g_assert(dict2);
    g_assert(int1);
    g_assert(qlist_empty(test_list));

    QDECREF(test_list);

    g_assert(qdict_get_int(dict1, "a") == 42);
    g_assert(qdict_get_int(dict1, "b") == 23);

    g_assert(qdict_size(dict1) == 2);

    QDECREF(dict1);

    g_assert(qdict_get_int(dict2, "x") == 0);

    g_assert(qdict_size(dict2) == 1);

    QDECREF(dict2);

    g_assert(qint_get_int(int1) == 66);

    QDECREF(int1);

    g_assert(qdict_get_int(test_dict, "4.y") == 1);
    g_assert(qdict_get_int(test_dict, "o.o") == 7);

    g_assert(qdict_size(test_dict) == 2);

    QDECREF(test_dict);


    /*
     * Test the split of
     *
     * {
     *     "0": 42,
     *     "1": 23,
     *     "1.x": 84
     * }
     *
     * to
     *
     * [
     *     42
     * ]
     *
     * and
     *
     * {
     *     "1": 23,
     *     "1.x": 84
     * }
     *
     * That is, test whether splitting stops if there is both an entry with key
     * of "%u" and other entries with keys prefixed "%u." for the same index.
     */

    test_dict = qdict_new();

    qdict_put(test_dict, "0", qint_from_int(42));
    qdict_put(test_dict, "1", qint_from_int(23));
    qdict_put(test_dict, "1.x", qint_from_int(84));

    qdict_array_split(test_dict, &test_list);

    int1 = qobject_to_qint(qlist_pop(test_list));

    g_assert(int1);
    g_assert(qlist_empty(test_list));

    QDECREF(test_list);

    g_assert(qint_get_int(int1) == 42);

    QDECREF(int1);

    g_assert(qdict_get_int(test_dict, "1") == 23);
    g_assert(qdict_get_int(test_dict, "1.x") == 84);

    g_assert(qdict_size(test_dict) == 2);

    QDECREF(test_dict);
}

static void qdict_join_test(void)
{
    QDict *dict1, *dict2;
    bool overwrite = false;
    int i;

    dict1 = qdict_new();
    dict2 = qdict_new();


    /* Test everything once without overwrite and once with */
    do
    {
        /* Test empty dicts */
        qdict_join(dict1, dict2, overwrite);

        g_assert(qdict_size(dict1) == 0);
        g_assert(qdict_size(dict2) == 0);


        /* First iteration: Test movement */
        /* Second iteration: Test empty source and non-empty destination */
        qdict_put(dict2, "foo", qint_from_int(42));

        for (i = 0; i < 2; i++) {
            qdict_join(dict1, dict2, overwrite);

            g_assert(qdict_size(dict1) == 1);
            g_assert(qdict_size(dict2) == 0);

            g_assert(qdict_get_int(dict1, "foo") == 42);
        }


        /* Test non-empty source and destination without conflict */
        qdict_put(dict2, "bar", qint_from_int(23));

        qdict_join(dict1, dict2, overwrite);

        g_assert(qdict_size(dict1) == 2);
        g_assert(qdict_size(dict2) == 0);

        g_assert(qdict_get_int(dict1, "foo") == 42);
        g_assert(qdict_get_int(dict1, "bar") == 23);


        /* Test conflict */
        qdict_put(dict2, "foo", qint_from_int(84));

        qdict_join(dict1, dict2, overwrite);

        g_assert(qdict_size(dict1) == 2);
        g_assert(qdict_size(dict2) == !overwrite);

        g_assert(qdict_get_int(dict1, "foo") == overwrite ? 84 : 42);
        g_assert(qdict_get_int(dict1, "bar") == 23);

        if (!overwrite) {
            g_assert(qdict_get_int(dict2, "foo") == 84);
        }


        /* Check the references */
        g_assert(qdict_get(dict1, "foo")->refcnt == 1);
        g_assert(qdict_get(dict1, "bar")->refcnt == 1);

        if (!overwrite) {
            g_assert(qdict_get(dict2, "foo")->refcnt == 1);
        }


        /* Clean up */
        qdict_del(dict1, "foo");
        qdict_del(dict1, "bar");

        if (!overwrite) {
            qdict_del(dict2, "foo");
        }
    }
    while (overwrite ^= true);


    QDECREF(dict1);
    QDECREF(dict2);
}

/*
 * Errors test-cases
 */

static void qdict_put_exists_test(void)
{
    int value;
    const char *key = "exists";
    QDict *tests_dict = qdict_new();

    qdict_put(tests_dict, key, qint_from_int(1));
    qdict_put(tests_dict, key, qint_from_int(2));

    value = qdict_get_int(tests_dict, key);
    g_assert(value == 2);

    g_assert(qdict_size(tests_dict) == 1);

    QDECREF(tests_dict);
}

static void qdict_get_not_exists_test(void)
{
    QDict *tests_dict = qdict_new();
    g_assert(qdict_get(tests_dict, "foo") == NULL);

    QDECREF(tests_dict);
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

        g_assert(qdict_haskey(qdict, key) == 0);
    }
    fclose(test_file);

    g_assert(qdict_size(qdict) == 0);
    QDECREF(qdict);
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
    g_test_add_func("/public/flatten", qdict_flatten_test);
    g_test_add_func("/public/array_split", qdict_array_split_test);
    g_test_add_func("/public/join", qdict_join_test);

    g_test_add_func("/errors/put_exists", qdict_put_exists_test);
    g_test_add_func("/errors/get_not_exists", qdict_get_not_exists_test);

    /* The Big one */
    if (g_test_slow()) {
        g_test_add_func("/stress/test", qdict_stress_test);
    }

    return g_test_run();
}
