/*
 * Unit-tests for Block layer QDict extras
 *
 * Copyright (c) 2013-2018 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "block/qdict.h"
#include "qobject/qlist.h"
#include "qobject/qnum.h"
#include "qapi/error.h"

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

    qobject_unref(copy);
    qobject_unref(dict);
}

static void qdict_flatten_test(void)
{
    QList *e_1 = qlist_new();
    QList *e = qlist_new();
    QDict *e_1_2 = qdict_new();
    QDict *f = qdict_new();
    QList *y = qlist_new();
    QDict *z = qdict_new();
    QDict *root = qdict_new();

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
     *     "g": 4,
     *     "y": [{}],
     *     "z": {"a": []}
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
     *     "g": 4,
     *     "y.0": {},
     *     "z.a": []
     * }
     */

    qdict_put_int(e_1_2, "a", 0);
    qdict_put_int(e_1_2, "b", 1);

    qlist_append_int(e_1, 23);
    qlist_append_int(e_1, 66);
    qlist_append(e_1, e_1_2);
    qlist_append_int(e, 42);
    qlist_append(e, e_1);

    qdict_put_int(f, "c", 2);
    qdict_put_int(f, "d", 3);

    qlist_append(y, qdict_new());

    qdict_put(z, "a", qlist_new());

    qdict_put(root, "e", e);
    qdict_put(root, "f", f);
    qdict_put_int(root, "g", 4);
    qdict_put(root, "y", y);
    qdict_put(root, "z", z);

    qdict_flatten(root);

    g_assert(qdict_get_int(root, "e.0") == 42);
    g_assert(qdict_get_int(root, "e.1.0") == 23);
    g_assert(qdict_get_int(root, "e.1.1") == 66);
    g_assert(qdict_get_int(root, "e.1.2.a") == 0);
    g_assert(qdict_get_int(root, "e.1.2.b") == 1);
    g_assert(qdict_get_int(root, "f.c") == 2);
    g_assert(qdict_get_int(root, "f.d") == 3);
    g_assert(qdict_get_int(root, "g") == 4);
    g_assert(!qdict_size(qdict_get_qdict(root, "y.0")));
    g_assert(qlist_empty(qdict_get_qlist(root, "z.a")));

    g_assert(qdict_size(root) == 10);

    qobject_unref(root);
}

static void qdict_clone_flatten_test(void)
{
    QDict *dict1 = qdict_new();
    QDict *dict2 = qdict_new();
    QDict *cloned_dict1;

    /*
     * Test that we can clone and flatten
     *    { "a": { "b": 42 } }
     * without modifying the clone.
     */

    qdict_put_int(dict2, "b", 42);
    qdict_put(dict1, "a", dict2);

    cloned_dict1 = qdict_clone_shallow(dict1);

    qdict_flatten(dict1);

    g_assert(qdict_size(dict1) == 1);
    g_assert(qdict_get_int(dict1, "a.b") == 42);

    g_assert(qdict_size(cloned_dict1) == 1);
    g_assert(qdict_get_qdict(cloned_dict1, "a") == dict2);

    g_assert(qdict_size(dict2) == 1);
    g_assert(qdict_get_int(dict2, "b") == 42);

    qobject_unref(dict1);
    qobject_unref(cloned_dict1);
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

    dict1 = qobject_to(QDict, qlist_pop(test_list));
    dict2 = qobject_to(QDict, qlist_pop(test_list));
    int1 = qobject_to(QNum, qlist_pop(test_list));

    g_assert(dict1);
    g_assert(dict2);
    g_assert(int1);
    g_assert(qlist_empty(test_list));

    qobject_unref(test_list);

    g_assert(qdict_get_int(dict1, "a") == 42);
    g_assert(qdict_get_int(dict1, "b") == 23);

    g_assert(qdict_size(dict1) == 2);

    qobject_unref(dict1);

    g_assert(qdict_get_int(dict2, "x") == 0);

    g_assert(qdict_size(dict2) == 1);

    qobject_unref(dict2);

    g_assert_cmpint(qnum_get_int(int1), ==, 66);

    qobject_unref(int1);

    g_assert(qdict_get_int(test_dict, "4.y") == 1);
    g_assert(qdict_get_int(test_dict, "o.o") == 7);

    g_assert(qdict_size(test_dict) == 2);

    qobject_unref(test_dict);

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

    int1 = qobject_to(QNum, qlist_pop(test_list));

    g_assert(int1);
    g_assert(qlist_empty(test_list));

    qobject_unref(test_list);

    g_assert_cmpint(qnum_get_int(int1), ==, 42);

    qobject_unref(int1);

    g_assert(qdict_get_int(test_dict, "1") == 23);
    g_assert(qdict_get_int(test_dict, "1.x") == 84);

    g_assert(qdict_size(test_dict) == 2);

    qobject_unref(test_dict);
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

    qobject_unref(dict);

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

    qobject_unref(dict);
}

static void qdict_join_test(void)
{
    QDict *dict1, *dict2;
    bool overwrite = false;
    int i;

    dict1 = qdict_new();
    dict2 = qdict_new();

    /* Test everything once without overwrite and once with */
    do {
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
        g_assert(qdict_get(dict1, "foo")->base.refcnt == 1);
        g_assert(qdict_get(dict1, "bar")->base.refcnt == 1);

        if (!overwrite) {
            g_assert(qdict_get(dict2, "foo")->base.refcnt == 1);
        }

        /* Clean up */
        qdict_del(dict1, "foo");
        qdict_del(dict1, "bar");

        if (!overwrite) {
            qdict_del(dict2, "foo");
        }
    } while (overwrite ^= true);

    qobject_unref(dict1);
    qobject_unref(dict2);
}

static void qdict_crumple_test_recursive(void)
{
    QDict *src, *dst, *rule, *vnc, *acl, *listen;
    QDict *empty, *empty_dict, *empty_list_0;
    QList *rules, *empty_list, *empty_dict_a;

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
    qdict_put(src, "empty.dict.a", qlist_new());
    qdict_put(src, "empty.list.0", qdict_new());

    dst = qobject_to(QDict, qdict_crumple(src, &error_abort));
    g_assert(dst);
    g_assert_cmpint(qdict_size(dst), ==, 2);

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

    rule = qobject_to(QDict, qlist_pop(rules));
    g_assert(rule);
    g_assert_cmpint(qdict_size(rule), ==, 2);
    g_assert_cmpstr("fred", ==, qdict_get_str(rule, "match"));
    g_assert_cmpstr("allow", ==, qdict_get_str(rule, "policy"));
    qobject_unref(rule);

    rule = qobject_to(QDict, qlist_pop(rules));
    g_assert(rule);
    g_assert_cmpint(qdict_size(rule), ==, 2);
    g_assert_cmpstr("bob", ==, qdict_get_str(rule, "match"));
    g_assert_cmpstr("deny", ==, qdict_get_str(rule, "policy"));
    qobject_unref(rule);

    /* With recursive crumpling, we should see all names unescaped */
    g_assert_cmpstr("acl0", ==, qdict_get_str(vnc, "acl.name"));
    g_assert_cmpstr("acl0", ==, qdict_get_str(acl, "rule.name"));

    empty = qdict_get_qdict(dst, "empty");
    g_assert(empty);
    g_assert_cmpint(qdict_size(empty), ==, 2);
    empty_dict = qdict_get_qdict(empty, "dict");
    g_assert(empty_dict);
    g_assert_cmpint(qdict_size(empty_dict), ==, 1);
    empty_dict_a = qdict_get_qlist(empty_dict, "a");
    g_assert(empty_dict_a && qlist_empty(empty_dict_a));
    empty_list = qdict_get_qlist(empty, "list");
    g_assert(empty_list);
    g_assert_cmpint(qlist_size(empty_list), ==, 1);
    empty_list_0 = qobject_to(QDict, qlist_pop(empty_list));
    g_assert(empty_list_0);
    g_assert_cmpint(qdict_size(empty_list_0), ==, 0);
    qobject_unref(empty_list_0);

    qobject_unref(src);
    qobject_unref(dst);
}

static void qdict_crumple_test_empty(void)
{
    QDict *src, *dst;

    src = qdict_new();

    dst = qobject_to(QDict, qdict_crumple(src, &error_abort));
    g_assert(dst);
    g_assert_cmpint(qdict_size(dst), ==, 0);

    qobject_unref(src);
    qobject_unref(dst);
}

static int qdict_count_entries(QDict *dict)
{
    const QDictEntry *e;
    int count = 0;

    for (e = qdict_first(dict); e; e = qdict_next(dict, e)) {
        count++;
    }

    return count;
}

static void qdict_rename_keys_test(void)
{
    QDict *dict = qdict_new();
    QDict *copy;
    QDictRenames *renames;
    Error *local_err = NULL;

    qdict_put_str(dict, "abc", "foo");
    qdict_put_str(dict, "abcdef", "bar");
    qdict_put_int(dict, "number", 42);
    qdict_put_bool(dict, "flag", true);
    qdict_put_null(dict, "nothing");

    /* Empty rename list */
    renames = (QDictRenames[]) {
        { NULL, "this can be anything" }
    };
    copy = qdict_clone_shallow(dict);
    qdict_rename_keys(copy, renames, &error_abort);

    g_assert_cmpstr(qdict_get_str(copy, "abc"), ==, "foo");
    g_assert_cmpstr(qdict_get_str(copy, "abcdef"), ==, "bar");
    g_assert_cmpint(qdict_get_int(copy, "number"), ==, 42);
    g_assert_cmpint(qdict_get_bool(copy, "flag"), ==, true);
    g_assert(qobject_type(qdict_get(copy, "nothing")) == QTYPE_QNULL);
    g_assert_cmpint(qdict_count_entries(copy), ==, 5);

    qobject_unref(copy);

    /* Simple rename of all entries */
    renames = (QDictRenames[]) {
        { "abc",        "str1" },
        { "abcdef",     "str2" },
        { "number",     "int" },
        { "flag",       "bool" },
        { "nothing",    "null" },
        { NULL , NULL }
    };
    copy = qdict_clone_shallow(dict);
    qdict_rename_keys(copy, renames, &error_abort);

    g_assert(!qdict_haskey(copy, "abc"));
    g_assert(!qdict_haskey(copy, "abcdef"));
    g_assert(!qdict_haskey(copy, "number"));
    g_assert(!qdict_haskey(copy, "flag"));
    g_assert(!qdict_haskey(copy, "nothing"));

    g_assert_cmpstr(qdict_get_str(copy, "str1"), ==, "foo");
    g_assert_cmpstr(qdict_get_str(copy, "str2"), ==, "bar");
    g_assert_cmpint(qdict_get_int(copy, "int"), ==, 42);
    g_assert_cmpint(qdict_get_bool(copy, "bool"), ==, true);
    g_assert(qobject_type(qdict_get(copy, "null")) == QTYPE_QNULL);
    g_assert_cmpint(qdict_count_entries(copy), ==, 5);

    qobject_unref(copy);

    /* Renames are processed top to bottom */
    renames = (QDictRenames[]) {
        { "abc",        "tmp" },
        { "abcdef",     "abc" },
        { "number",     "abcdef" },
        { "flag",       "number" },
        { "nothing",    "flag" },
        { "tmp",        "nothing" },
        { NULL , NULL }
    };
    copy = qdict_clone_shallow(dict);
    qdict_rename_keys(copy, renames, &error_abort);

    g_assert_cmpstr(qdict_get_str(copy, "nothing"), ==, "foo");
    g_assert_cmpstr(qdict_get_str(copy, "abc"), ==, "bar");
    g_assert_cmpint(qdict_get_int(copy, "abcdef"), ==, 42);
    g_assert_cmpint(qdict_get_bool(copy, "number"), ==, true);
    g_assert(qobject_type(qdict_get(copy, "flag")) == QTYPE_QNULL);
    g_assert(!qdict_haskey(copy, "tmp"));
    g_assert_cmpint(qdict_count_entries(copy), ==, 5);

    qobject_unref(copy);

    /* Conflicting rename */
    renames = (QDictRenames[]) {
        { "abcdef",     "abc" },
        { NULL , NULL }
    };
    copy = qdict_clone_shallow(dict);
    qdict_rename_keys(copy, renames, &local_err);

    error_free_or_abort(&local_err);

    g_assert_cmpstr(qdict_get_str(copy, "abc"), ==, "foo");
    g_assert_cmpstr(qdict_get_str(copy, "abcdef"), ==, "bar");
    g_assert_cmpint(qdict_get_int(copy, "number"), ==, 42);
    g_assert_cmpint(qdict_get_bool(copy, "flag"), ==, true);
    g_assert(qobject_type(qdict_get(copy, "nothing")) == QTYPE_QNULL);
    g_assert_cmpint(qdict_count_entries(copy), ==, 5);

    qobject_unref(copy);

    /* Renames in an empty dict */
    renames = (QDictRenames[]) {
        { "abcdef",     "abc" },
        { NULL , NULL }
    };

    qobject_unref(dict);
    dict = qdict_new();

    qdict_rename_keys(dict, renames, &error_abort);
    g_assert(qdict_first(dict) == NULL);

    qobject_unref(dict);
}

static void qdict_crumple_test_bad_inputs(void)
{
    QDict *src, *nested;
    Error *error = NULL;

    src = qdict_new();
    /* rule.0 can't be both a string and a dict */
    qdict_put_str(src, "rule.0", "fred");
    qdict_put_str(src, "rule.0.policy", "allow");

    g_assert(qdict_crumple(src, &error) == NULL);
    error_free_or_abort(&error);
    qobject_unref(src);

    src = qdict_new();
    /* rule can't be both a list and a dict */
    qdict_put_str(src, "rule.0", "fred");
    qdict_put_str(src, "rule.a", "allow");

    g_assert(qdict_crumple(src, &error) == NULL);
    error_free_or_abort(&error);
    qobject_unref(src);

    src = qdict_new();
    /* The input should be flat, ie no dicts or lists */
    nested = qdict_new();
    qdict_put(nested, "x", qdict_new());
    qdict_put(src, "rule.a", nested);
    qdict_put_str(src, "rule.b", "allow");

    g_assert(qdict_crumple(src, &error) == NULL);
    error_free_or_abort(&error);
    qobject_unref(src);

    src = qdict_new();
    /* List indexes must not have gaps */
    qdict_put_str(src, "rule.0", "deny");
    qdict_put_str(src, "rule.3", "allow");

    g_assert(qdict_crumple(src, &error) == NULL);
    error_free_or_abort(&error);
    qobject_unref(src);

    src = qdict_new();
    /* List indexes must be in %zu format */
    qdict_put_str(src, "rule.0", "deny");
    qdict_put_str(src, "rule.+1", "allow");

    g_assert(qdict_crumple(src, &error) == NULL);
    error_free_or_abort(&error);
    qobject_unref(src);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/public/defaults", qdict_defaults_test);
    g_test_add_func("/public/flatten", qdict_flatten_test);
    g_test_add_func("/public/clone_flatten", qdict_clone_flatten_test);
    g_test_add_func("/public/array_split", qdict_array_split_test);
    g_test_add_func("/public/array_entries", qdict_array_entries_test);
    g_test_add_func("/public/join", qdict_join_test);
    g_test_add_func("/public/crumple/recursive",
                    qdict_crumple_test_recursive);
    g_test_add_func("/public/crumple/empty",
                    qdict_crumple_test_empty);
    g_test_add_func("/public/crumple/bad_inputs",
                    qdict_crumple_test_bad_inputs);

    g_test_add_func("/public/rename_keys", qdict_rename_keys_test);

    return g_test_run();
}
