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

static void qobject_is_equal_null_test(void)
{
    check_unequal(qnull(), NULL);
}

static void qobject_is_equal_num_test(void)
{
    g_autoptr(QNum) u0 = qnum_from_uint(0u);
    g_autoptr(QNum) i0 = qnum_from_int(0);
    g_autoptr(QNum) d0 = qnum_from_double(0.0);
    g_autoptr(QNum) dnan = qnum_from_double(NAN);
    g_autoptr(QNum) um42 = qnum_from_uint((uint64_t)-42);
    g_autoptr(QNum) im42 = qnum_from_int(-42);
    g_autoptr(QNum) dm42 = qnum_from_double(-42.0);


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
}

static void qobject_is_equal_bool_test(void)
{
    g_autoptr(QBool) btrue_0 = qbool_from_bool(true);
    g_autoptr(QBool) btrue_1 = qbool_from_bool(true);
    g_autoptr(QBool) bfalse_0 = qbool_from_bool(false);
    g_autoptr(QBool) bfalse_1 = qbool_from_bool(false);

    check_equal(btrue_0, btrue_1);
    check_equal(bfalse_0, bfalse_1);
    check_unequal(btrue_0, bfalse_0);
}

static void qobject_is_equal_string_test(void)
{
    g_autoptr(QString) str_base = qstring_from_str("foo");
    g_autoptr(QString) str_whitespace_0 = qstring_from_str(" foo");
    g_autoptr(QString) str_whitespace_1 = qstring_from_str("foo ");
    g_autoptr(QString) str_whitespace_2 = qstring_from_str("foo\b");
    g_autoptr(QString) str_whitespace_3 = qstring_from_str("fooo\b");
    g_autoptr(QString) str_case = qstring_from_str("Foo");
    /* Should yield "foo" */
    g_autoptr(QString) str_built = qstring_from_substr("buffoon", 3, 6);

    check_unequal(str_base, str_whitespace_0, str_whitespace_1,
                  str_whitespace_2, str_whitespace_3, str_case);

    check_equal(str_base, str_built);
}

static void qobject_is_equal_list_test(void)
{
    g_autoptr(QList) list_0 = qlist_new();
    g_autoptr(QList) list_1 = qlist_new();
    g_autoptr(QList) list_reordered = qlist_new();
    g_autoptr(QList) list_longer = qlist_new();
    g_autoptr(QList) list_shorter = qlist_new();
    g_autoptr(QList) list_cloned = NULL;

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
}

static void qobject_is_equal_dict_test(void)
{
    g_autoptr(QDict) dict_cloned = NULL;
    g_autoptr(QDict) dict_0 = qdict_new();
    g_autoptr(QDict) dict_1 = qdict_new();
    g_autoptr(QDict) dict_different_key = qdict_new();
    g_autoptr(QDict) dict_different_value = qdict_new();
    g_autoptr(QDict) dict_different_null_key = qdict_new();
    g_autoptr(QDict) dict_longer = qdict_new();
    g_autoptr(QDict) dict_shorter = qdict_new();
    g_autoptr(QDict) dict_nested = qdict_new();

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

    /* Containing an NaN value will make this dict compare unequal to
     * itself */
    qdict_put(dict_0, "NaN", qnum_from_double(NAN));
    g_assert(qobject_is_equal(QOBJECT(dict_0), QOBJECT(dict_0)) == false);
}

static void qobject_is_equal_conversion_test(void)
{
    g_autoptr(QNum) u0 = qnum_from_uint(0u);
    g_autoptr(QNum) i0 = qnum_from_int(0);
    g_autoptr(QNum) d0 = qnum_from_double(0.0);
    g_autoptr(QString) s0 = qstring_from_str("0");
    g_autoptr(QString) s_empty = qstring_new();
    g_autoptr(QBool) bfalse = qbool_from_bool(false);

    /* No automatic type conversion */
    check_unequal(u0, s0, s_empty, bfalse, qnull(), NULL);
    check_unequal(i0, s0, s_empty, bfalse, qnull(), NULL);
    check_unequal(d0, s0, s_empty, bfalse, qnull(), NULL);
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
