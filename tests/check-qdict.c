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
#include "qapi/qmp/qstring.h"
#include "qapi/error.h"
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

    QDECREF(qdict);
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
    qn = qobject_to_qnum(ent->value);
    g_assert_cmpint(qnum_get_int(qn), ==, num);

    QDECREF(qdict);
}

static void qdict_destroy_simple_test(void)
{
    QDict *qdict;

    qdict = qdict_new();
    qdict_put_int(qdict, "num", 0);
    qdict_put_str(qdict, "str", "foo");

    QDECREF(qdict);
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

    qn = qobject_to_qnum(obj);
    g_assert_cmpint(qnum_get_int(qn), ==, value);

    QDECREF(tests_dict);
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

    QDECREF(tests_dict);
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

    QDECREF(tests_dict);
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

    QDECREF(tests_dict);
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

    QDECREF(tests_dict);
}

static void qdict_defaults_test(void)
{
    QDict *dict, *copy;

    dict = qdict_new();
    copy = qdict_new();

    qdict_set_default_str(dict, "foo", "abc");
    qdict_set_default_str(dict, "foo", "def");
    g_assert_cmpstr(qdict_get_str(dict, "foo"), ==, "abc");
    qdict_set_default_str(dict, "bar", "ghi");

    qdict_copy_default(copy, dict, "foo");
    g_assert_cmpstr(qdict_get_str(copy, "foo"), ==, "abc");
    qdict_set_default_str(copy, "bar", "xyz");
    qdict_copy_default(copy, dict, "bar");
    g_assert_cmpstr(qdict_get_str(copy, "bar"), ==, "xyz");

    QDECREF(copy);
    QDECREF(dict);
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

    qdict_put_int(tests_dict, key, 0);
    g_assert(qdict_haskey(tests_dict, key) == 1);

    QDECREF(tests_dict);
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

    qdict_put_int(dict1, "a", 0);
    qdict_put_int(dict1, "b", 1);

    qlist_append_int(list1, 23);
    qlist_append_int(list1, 66);
    qlist_append(list1, dict1);
    qlist_append_int(list2, 42);
    qlist_append(list2, list1);

    qdict_put_int(dict2, "c", 2);
    qdict_put_int(dict2, "d", 3);
    qdict_put(dict3, "e", list2);
    qdict_put(dict3, "f", dict2);
    qdict_put_int(dict3, "g", 4);

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
    QNum *int1;
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

    qdict_put_int(test_dict, "1.x", 0);
    qdict_put_int(test_dict, "4.y", 1);
    qdict_put_int(test_dict, "0.a", 42);
    qdict_put_int(test_dict, "o.o", 7);
    qdict_put_int(test_dict, "0.b", 23);
    qdict_put_int(test_dict, "2", 66);

    qdict_array_split(test_dict, &test_list);

    dict1 = qobject_to_qdict(qlist_pop(test_list));
    dict2 = qobject_to_qdict(qlist_pop(test_list));
    int1 = qobject_to_qnum(qlist_pop(test_list));

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

    g_assert_cmpint(qnum_get_int(int1), ==, 66);

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

    qdict_put_int(test_dict, "0", 42);
    qdict_put_int(test_dict, "1", 23);
    qdict_put_int(test_dict, "1.x", 84);

    qdict_array_split(test_dict, &test_list);

    int1 = qobject_to_qnum(qlist_pop(test_list));

    g_assert(int1);
    g_assert(qlist_empty(test_list));

    QDECREF(test_list);

    g_assert_cmpint(qnum_get_int(int1), ==, 42);

    QDECREF(int1);

    g_assert(qdict_get_int(test_dict, "1") == 23);
    g_assert(qdict_get_int(test_dict, "1.x") == 84);

    g_assert(qdict_size(test_dict) == 2);

    QDECREF(test_dict);
}

static void qdict_array_entries_test(void)
{
    QDict *dict = qdict_new();

    g_assert_cmpint(qdict_array_entries(dict, "foo."), ==, 0);

    qdict_put_int(dict, "bar", 0);
    qdict_put_int(dict, "baz.0", 0);
    g_assert_cmpint(qdict_array_entries(dict, "foo."), ==, 0);

    qdict_put_int(dict, "foo.1", 0);
    g_assert_cmpint(qdict_array_entries(dict, "foo."), ==, -EINVAL);
    qdict_put_int(dict, "foo.0", 0);
    g_assert_cmpint(qdict_array_entries(dict, "foo."), ==, 2);
    qdict_put_int(dict, "foo.bar", 0);
    g_assert_cmpint(qdict_array_entries(dict, "foo."), ==, -EINVAL);
    qdict_del(dict, "foo.bar");

    qdict_put_int(dict, "foo.2.a", 0);
    qdict_put_int(dict, "foo.2.b", 0);
    qdict_put_int(dict, "foo.2.c", 0);
    g_assert_cmpint(qdict_array_entries(dict, "foo."), ==, 3);
    g_assert_cmpint(qdict_array_entries(dict, ""), ==, -EINVAL);

    QDECREF(dict);

    dict = qdict_new();
    qdict_put_int(dict, "1", 0);
    g_assert_cmpint(qdict_array_entries(dict, ""), ==, -EINVAL);
    qdict_put_int(dict, "0", 0);
    g_assert_cmpint(qdict_array_entries(dict, ""), ==, 2);
    qdict_put_int(dict, "bar", 0);
    g_assert_cmpint(qdict_array_entries(dict, ""), ==, -EINVAL);
    qdict_del(dict, "bar");

    qdict_put_int(dict, "2.a", 0);
    qdict_put_int(dict, "2.b", 0);
    qdict_put_int(dict, "2.c", 0);
    g_assert_cmpint(qdict_array_entries(dict, ""), ==, 3);

    QDECREF(dict);
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
        qdict_put_int(dict2, "foo", 42);

        for (i = 0; i < 2; i++) {
            qdict_join(dict1, dict2, overwrite);

            g_assert(qdict_size(dict1) == 1);
            g_assert(qdict_size(dict2) == 0);

            g_assert(qdict_get_int(dict1, "foo") == 42);
        }

        /* Test non-empty source and destination without conflict */
        qdict_put_int(dict2, "bar", 23);

        qdict_join(dict1, dict2, overwrite);

        g_assert(qdict_size(dict1) == 2);
        g_assert(qdict_size(dict2) == 0);

        g_assert(qdict_get_int(dict1, "foo") == 42);
        g_assert(qdict_get_int(dict1, "bar") == 23);

        /* Test conflict */
        qdict_put_int(dict2, "foo", 84);

        qdict_join(dict1, dict2, overwrite);

        g_assert(qdict_size(dict1) == 2);
        g_assert(qdict_size(dict2) == !overwrite);

        g_assert(qdict_get_int(dict1, "foo") == (overwrite ? 84 : 42));
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

static void qdict_crumple_test_recursive(void)
{
    QDict *src, *dst, *rule, *vnc, *acl, *listen;
    QList *rules;

    src = qdict_new();
    qdict_put_str(src, "vnc.listen.addr", "127.0.0.1");
    qdict_put_str(src, "vnc.listen.port", "5901");
    qdict_put_str(src, "vnc.acl.rules.0.match", "fred");
    qdict_put_str(src, "vnc.acl.rules.0.policy", "allow");
    qdict_put_str(src, "vnc.acl.rules.1.match", "bob");
    qdict_put_str(src, "vnc.acl.rules.1.policy", "deny");
    qdict_put_str(src, "vnc.acl.default", "deny");
    qdict_put_str(src, "vnc.acl..name", "acl0");
    qdict_put_str(src, "vnc.acl.rule..name", "acl0");

    dst = qobject_to_qdict(qdict_crumple(src, &error_abort));
    g_assert(dst);
    g_assert_cmpint(qdict_size(dst), ==, 1);

    vnc = qdict_get_qdict(dst, "vnc");
    g_assert(vnc);
    g_assert_cmpint(qdict_size(vnc), ==, 3);

    listen = qdict_get_qdict(vnc, "listen");
    g_assert(listen);
    g_assert_cmpint(qdict_size(listen), ==, 2);
    g_assert_cmpstr("127.0.0.1", ==, qdict_get_str(listen, "addr"));
    g_assert_cmpstr("5901", ==, qdict_get_str(listen, "port"));

    acl = qdict_get_qdict(vnc, "acl");
    g_assert(acl);
    g_assert_cmpint(qdict_size(acl), ==, 3);

    rules = qdict_get_qlist(acl, "rules");
    g_assert(rules);
    g_assert_cmpint(qlist_size(rules), ==, 2);

    rule = qobject_to_qdict(qlist_pop(rules));
    g_assert(rule);
    g_assert_cmpint(qdict_size(rule), ==, 2);
    g_assert_cmpstr("fred", ==, qdict_get_str(rule, "match"));
    g_assert_cmpstr("allow", ==, qdict_get_str(rule, "policy"));
    QDECREF(rule);

    rule = qobject_to_qdict(qlist_pop(rules));
    g_assert(rule);
    g_assert_cmpint(qdict_size(rule), ==, 2);
    g_assert_cmpstr("bob", ==, qdict_get_str(rule, "match"));
    g_assert_cmpstr("deny", ==, qdict_get_str(rule, "policy"));
    QDECREF(rule);

    /* With recursive crumpling, we should see all names unescaped */
    g_assert_cmpstr("acl0", ==, qdict_get_str(vnc, "acl.name"));
    g_assert_cmpstr("acl0", ==, qdict_get_str(acl, "rule.name"));

    QDECREF(src);
    QDECREF(dst);
}

static void qdict_crumple_test_empty(void)
{
    QDict *src, *dst;

    src = qdict_new();

    dst = (QDict *)qdict_crumple(src, &error_abort);

    g_assert_cmpint(qdict_size(dst), ==, 0);

    QDECREF(src);
    QDECREF(dst);
}

static void qdict_crumple_test_bad_inputs(void)
{
    QDict *src;
    Error *error = NULL;

    src = qdict_new();
    /* rule.0 can't be both a string and a dict */
    qdict_put_str(src, "rule.0", "fred");
    qdict_put_str(src, "rule.0.policy", "allow");

    g_assert(qdict_crumple(src, &error) == NULL);
    g_assert(error != NULL);
    error_free(error);
    error = NULL;
    QDECREF(src);

    src = qdict_new();
    /* rule can't be both a list and a dict */
    qdict_put_str(src, "rule.0", "fred");
    qdict_put_str(src, "rule.a", "allow");

    g_assert(qdict_crumple(src, &error) == NULL);
    g_assert(error != NULL);
    error_free(error);
    error = NULL;
    QDECREF(src);

    src = qdict_new();
    /* The input should be flat, ie no dicts or lists */
    qdict_put(src, "rule.a", qdict_new());
    qdict_put_str(src, "rule.b", "allow");

    g_assert(qdict_crumple(src, &error) == NULL);
    g_assert(error != NULL);
    error_free(error);
    error = NULL;
    QDECREF(src);

    src = qdict_new();
    /* List indexes must not have gaps */
    qdict_put_str(src, "rule.0", "deny");
    qdict_put_str(src, "rule.3", "allow");

    g_assert(qdict_crumple(src, &error) == NULL);
    g_assert(error != NULL);
    error_free(error);
    error = NULL;
    QDECREF(src);

    src = qdict_new();
    /* List indexes must be in %zu format */
    qdict_put_str(src, "rule.0", "deny");
    qdict_put_str(src, "rule.+1", "allow");

    g_assert(qdict_crumple(src, &error) == NULL);
    g_assert(error != NULL);
    error_free(error);
    error = NULL;
    QDECREF(src);
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
    g_test_add_func("/public/defaults", qdict_defaults_test);
    g_test_add_func("/public/haskey_not", qdict_haskey_not_test);
    g_test_add_func("/public/haskey", qdict_haskey_test);
    g_test_add_func("/public/del", qdict_del_test);
    g_test_add_func("/public/to_qdict", qobject_to_qdict_test);
    g_test_add_func("/public/iterapi", qdict_iterapi_test);
    g_test_add_func("/public/flatten", qdict_flatten_test);
    g_test_add_func("/public/array_split", qdict_array_split_test);
    g_test_add_func("/public/array_entries", qdict_array_entries_test);
    g_test_add_func("/public/join", qdict_join_test);

    g_test_add_func("/errors/put_exists", qdict_put_exists_test);
    g_test_add_func("/errors/get_not_exists", qdict_get_not_exists_test);

    g_test_add_func("/public/crumple/recursive",
                    qdict_crumple_test_recursive);
    g_test_add_func("/public/crumple/empty",
                    qdict_crumple_test_empty);
    g_test_add_func("/public/crumple/bad_inputs",
                    qdict_crumple_test_bad_inputs);

    /* The Big one */
    if (g_test_slow()) {
        g_test_add_func("/stress/test", qdict_stress_test);
    }

    return g_test_run();
}
