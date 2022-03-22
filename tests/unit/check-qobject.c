/*
 * Generic QObject unit-tests.
 *
 * Copyright (C) 2017 Red Hat Inc.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qmp/qnull.h"
#include "qapi/qmp/qnum.h"
#include "qapi/qmp/qstring.h"
#include "qemu-common.h"

#include <math.h>

/* Marks the end of the test_equality() argument list.
 * We cannot use NULL there because that is a valid argument. */
static QObject test_equality_end_of_arguments;

/**
 * Test whether all variadic QObject *arguments are equal (@expected
 * is true) or whether they are all not equal (@expected is false).
 * Every QObject is tested to be equal to itself (to test
 * reflexivity), all tests are done both ways (to test symmetry), and
 * transitivity is not assumed but checked (each object is compared to
 * every other one).
 *
 * Note that qobject_is_equal() is not really an equivalence relation,
 * so this function may not be used for all objects (reflexivity is
 * not guaranteed, e.g. in the case of a QNum containing NaN).
 *
 * The @_ argument is required because a boolean may not be the last
 * argument before a variadic argument list (C11 7.16.1.4 para. 4).
 */
static void do_test_equality(bool expected, int _, ...)
{
    va_list ap_count, ap_extract;
    QObject **args;
    int arg_count = 0;
    int i, j;

    va_start(ap_count, _);
    va_copy(ap_extract, ap_count);
    while (va_arg(ap_count, QObject *) != &test_equality_end_of_arguments) {
        arg_count++;
    }
    va_end(ap_count);

    args = g_new(QObject *, arg_count);
    for (i = 0; i < arg_count; i++) {
        args[i] = va_arg(ap_extract, QObject *);
    }
    va_end(ap_extract);

    for (i = 0; i < arg_count; i++) {
        g_assert(qobject_is_equal(args[i], args[i]) == true);

        for (j = i + 1; j < arg_count; j++) {
            g_assert(qobject_is_equal(args[i], args[j]) == expected);
        }
    }

    g_free(args);
}

#define check_equal(...) \
    do_test_equality(true, 0, __VA_ARGS__, &test_equality_end_of_arguments)
#define check_unequal(...) \
    do_test_equality(false, 0, __VA_ARGS__, &test_equality_end_of_arguments)

static void do_free_all(int _, ...)
{
    va_list ap;
    QObject *obj;

    va_start(ap, _);
    while ((obj = va_arg(ap, QObject *)) != NULL) {
        qobject_unref(obj);
    }
    va_end(ap);
}

#define free_all(...) \
    do_free_all(0, __VA_ARGS__, NULL)

static void qobject_is_equal_null_test(void)
{
    check_unequal(qnull(), NULL);
}

static void qobject_is_equal_num_test(void)
{
    QNum *u0, *i0, *d0, *dnan, *um42, *im42, *dm42;

    u0 = qnum_from_uint(0u);
    i0 = qnum_from_int(0);
    d0 = qnum_from_double(0.0);
    dnan = qnum_from_double(NAN);
    um42 = qnum_from_uint((uint64_t)-42);
    im42 = qnum_from_int(-42);
    dm42 = qnum_from_double(-42.0);

    /* Integers representing a mathematically equal number should
     * compare equal */
    check_equal(u0, i0);
    /* Doubles, however, are always unequal to integers */
    check_unequal(u0, d0);
    check_unequal(i0, d0);

    /* Do not assume any object is equal to itself -- note however
     * that NaN cannot occur in a JSON object anyway. */
    g_assert(qobject_is_equal(QOBJECT(dnan), QOBJECT(dnan)) == false);

    /* No unsigned overflow */
    check_unequal(um42, im42);
    check_unequal(um42, dm42);
    check_unequal(im42, dm42);

    free_all(u0, i0, d0, dnan, um42, im42, dm42);
}

static void qobject_is_equal_bool_test(void)
{
    QBool *btrue_0, *btrue_1, *bfalse_0, *bfalse_1;

    btrue_0 = qbool_from_bool(true);
    btrue_1 = qbool_from_bool(true);
    bfalse_0 = qbool_from_bool(false);
    bfalse_1 = qbool_from_bool(false);

    check_equal(btrue_0, btrue_1);
    check_equal(bfalse_0, bfalse_1);
    check_unequal(btrue_0, bfalse_0);

    free_all(btrue_0, btrue_1, bfalse_0, bfalse_1);
}

static void qobject_is_equal_string_test(void)
{
    QString *str_base, *str_whitespace_0, *str_whitespace_1, *str_whitespace_2;
    QString *str_whitespace_3, *str_case, *str_built;

    str_base = qstring_from_str("foo");
    str_whitespace_0 = qstring_from_str(" foo");
    str_whitespace_1 = qstring_from_str("foo ");
    str_whitespace_2 = qstring_from_str("foo\b");
    str_whitespace_3 = qstring_from_str("fooo\b");
    str_case = qstring_from_str("Foo");

    /* Should yield "foo" */
    str_built = qstring_from_substr("buffoon", 3, 6);

    check_unequal(str_base, str_whitespace_0, str_whitespace_1,
                  str_whitespace_2, str_whitespace_3, str_case);

    check_equal(str_base, str_built);

    free_all(str_base, str_whitespace_0, str_whitespace_1, str_whitespace_2,
             str_whitespace_3, str_case, str_built);
}

static void qobject_is_equal_list_test(void)
{
    QList *list_0, *list_1, *list_cloned;
    QList *list_reordered, *list_longer, *list_shorter;

    list_0 = qlist_new();
    list_1 = qlist_new();
    list_reordered = qlist_new();
    list_longer = qlist_new();
    list_shorter = qlist_new();

    qlist_append_int(list_0, 1);
    qlist_append_int(list_0, 2);
    qlist_append_int(list_0, 3);

    qlist_append_int(list_1, 1);
    qlist_append_int(list_1, 2);
    qlist_append_int(list_1, 3);

    qlist_append_int(list_reordered, 1);
    qlist_append_int(list_reordered, 3);
    qlist_append_int(list_reordered, 2);

    qlist_append_int(list_longer, 1);
    qlist_append_int(list_longer, 2);
    qlist_append_int(list_longer, 3);
    qlist_append_null(list_longer);

    qlist_append_int(list_shorter, 1);
    qlist_append_int(list_shorter, 2);

    list_cloned = qlist_copy(list_0);

    check_equal(list_0, list_1, list_cloned);
    check_unequal(list_0, list_reordered, list_longer, list_shorter);

    /* With a NaN in it, the list should no longer compare equal to
     * itself */
    qlist_append(list_0, qnum_from_double(NAN));
    g_assert(qobject_is_equal(QOBJECT(list_0), QOBJECT(list_0)) == false);

    free_all(list_0, list_1, list_cloned, list_reordered, list_longer,
             list_shorter);
}

static void qobject_is_equal_dict_test(void)
{
    QDict *dict_0, *dict_1, *dict_cloned;
    QDict *dict_different_key, *dict_different_value, *dict_different_null_key;
    QDict *dict_longer, *dict_shorter, *dict_nested;
    QDict *dict_crumpled;

    dict_0 = qdict_new();
    dict_1 = qdict_new();
    dict_different_key = qdict_new();
    dict_different_value = qdict_new();
    dict_different_null_key = qdict_new();
    dict_longer = qdict_new();
    dict_shorter = qdict_new();
    dict_nested = qdict_new();

    qdict_put_int(dict_0, "f.o", 1);
    qdict_put_int(dict_0, "bar", 2);
    qdict_put_int(dict_0, "baz", 3);
    qdict_put_null(dict_0, "null");

    qdict_put_int(dict_1, "f.o", 1);
    qdict_put_int(dict_1, "bar", 2);
    qdict_put_int(dict_1, "baz", 3);
    qdict_put_null(dict_1, "null");

    qdict_put_int(dict_different_key, "F.o", 1);
    qdict_put_int(dict_different_key, "bar", 2);
    qdict_put_int(dict_different_key, "baz", 3);
    qdict_put_null(dict_different_key, "null");

    qdict_put_int(dict_different_value, "f.o", 42);
    qdict_put_int(dict_different_value, "bar", 2);
    qdict_put_int(dict_different_value, "baz", 3);
    qdict_put_null(dict_different_value, "null");

    qdict_put_int(dict_different_null_key, "f.o", 1);
    qdict_put_int(dict_different_null_key, "bar", 2);
    qdict_put_int(dict_different_null_key, "baz", 3);
    qdict_put_null(dict_different_null_key, "none");

    qdict_put_int(dict_longer, "f.o", 1);
    qdict_put_int(dict_longer, "bar", 2);
    qdict_put_int(dict_longer, "baz", 3);
    qdict_put_int(dict_longer, "xyz", 4);
    qdict_put_null(dict_longer, "null");

    qdict_put_int(dict_shorter, "f.o", 1);
    qdict_put_int(dict_shorter, "bar", 2);
    qdict_put_int(dict_shorter, "baz", 3);

    qdict_put(dict_nested, "f", qdict_new());
    qdict_put_int(qdict_get_qdict(dict_nested, "f"), "o", 1);
    qdict_put_int(dict_nested, "bar", 2);
    qdict_put_int(dict_nested, "baz", 3);
    qdict_put_null(dict_nested, "null");

    dict_cloned = qdict_clone_shallow(dict_0);

    check_equal(dict_0, dict_1, dict_cloned);
    check_unequal(dict_0, dict_different_key, dict_different_value,
                  dict_different_null_key, dict_longer, dict_shorter,
                  dict_nested);

    dict_crumpled = qobject_to(QDict, qdict_crumple(dict_1, &error_abort));
    check_equal(dict_crumpled, dict_nested);

    qdict_flatten(dict_nested);
    check_equal(dict_0, dict_nested);

    /* Containing an NaN value will make this dict compare unequal to
     * itself */
    qdict_put(dict_0, "NaN", qnum_from_double(NAN));
    g_assert(qobject_is_equal(QOBJECT(dict_0), QOBJECT(dict_0)) == false);

    free_all(dict_0, dict_1, dict_cloned, dict_different_key,
             dict_different_value, dict_different_null_key, dict_longer,
             dict_shorter, dict_nested, dict_crumpled);
}

static void qobject_is_equal_conversion_test(void)
{
    QNum *u0, *i0, *d0;
    QString *s0, *s_empty;
    QBool *bfalse;

    u0 = qnum_from_uint(0u);
    i0 = qnum_from_int(0);
    d0 = qnum_from_double(0.0);
    s0 = qstring_from_str("0");
    s_empty = qstring_new();
    bfalse = qbool_from_bool(false);

    /* No automatic type conversion */
    check_unequal(u0, s0, s_empty, bfalse, qnull(), NULL);
    check_unequal(i0, s0, s_empty, bfalse, qnull(), NULL);
    check_unequal(d0, s0, s_empty, bfalse, qnull(), NULL);

    free_all(u0, i0, d0, s0, s_empty, bfalse);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/public/qobject_is_equal_null",
                    qobject_is_equal_null_test);
    g_test_add_func("/public/qobject_is_equal_num", qobject_is_equal_num_test);
    g_test_add_func("/public/qobject_is_equal_bool",
                    qobject_is_equal_bool_test);
    g_test_add_func("/public/qobject_is_equal_string",
                    qobject_is_equal_string_test);
    g_test_add_func("/public/qobject_is_equal_list",
                    qobject_is_equal_list_test);
    g_test_add_func("/public/qobject_is_equal_dict",
                    qobject_is_equal_dict_test);
    g_test_add_func("/public/qobject_is_equal_conversion",
                    qobject_is_equal_conversion_test);

    return g_test_run();
}
