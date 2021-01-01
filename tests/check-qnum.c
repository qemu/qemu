/*
 * QNum unit-tests.
 *
 * Copyright (C) 2009 Red Hat Inc.
 * Copyright IBM, Corp. 2009
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *  Anthony Liguori <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qapi/qmp/qnum.h"
#include "qemu-common.h"

/*
 * Public Interface test-cases
 *
 * (with some violations to access 'private' data)
 */

static void qnum_from_int_test(void)
{
    QNum *qn;
    const int value = -42;

    qn = qnum_from_int(value);
    g_assert(qn != NULL);
    g_assert_cmpint(qn->kind, ==, QNUM_I64);
    g_assert_cmpint(qn->u.i64, ==, value);
    g_assert_cmpint(qn->base.refcnt, ==, 1);
    g_assert_cmpint(qobject_type(QOBJECT(qn)), ==, QTYPE_QNUM);

    qobject_unref(qn);
}

static void qnum_from_uint_test(void)
{
    QNum *qn;
    const uint64_t value = UINT64_MAX;

    qn = qnum_from_uint(value);
    g_assert(qn != NULL);
    g_assert_cmpint(qn->kind, ==, QNUM_U64);
    g_assert(qn->u.u64 == value);
    g_assert(qn->base.refcnt == 1);
    g_assert(qobject_type(QOBJECT(qn)) == QTYPE_QNUM);

    qobject_unref(qn);
}

static void qnum_from_double_test(void)
{
    QNum *qn;
    const double value = -42.23423;

    qn = qnum_from_double(value);
    g_assert(qn != NULL);
    g_assert_cmpint(qn->kind, ==, QNUM_DOUBLE);
    g_assert_cmpfloat(qn->u.dbl, ==, value);
    g_assert_cmpint(qn->base.refcnt, ==, 1);
    g_assert_cmpint(qobject_type(QOBJECT(qn)), ==, QTYPE_QNUM);

    qobject_unref(qn);
}

static void qnum_from_int64_test(void)
{
    QNum *qn;
    const int64_t value = 0x1234567890abcdefLL;

    qn = qnum_from_int(value);
    g_assert_cmpint((int64_t) qn->u.i64, ==, value);

    qobject_unref(qn);
}

static void qnum_get_int_test(void)
{
    QNum *qn;
    const int value = 123456;

    qn = qnum_from_int(value);
    g_assert_cmpint(qnum_get_int(qn), ==, value);

    qobject_unref(qn);
}

static void qnum_get_uint_test(void)
{
    QNum *qn;
    const int value = 123456;
    uint64_t val;
    int64_t ival;

    qn = qnum_from_uint(value);
    g_assert(qnum_get_try_uint(qn, &val));
    g_assert_cmpuint(val, ==, value);
    qobject_unref(qn);

    qn = qnum_from_int(value);
    g_assert(qnum_get_try_uint(qn, &val));
    g_assert_cmpuint(val, ==, value);
    qobject_unref(qn);

    /* invalid cases */
    qn = qnum_from_int(-1);
    g_assert(!qnum_get_try_uint(qn, &val));
    qobject_unref(qn);

    qn = qnum_from_uint(-1ULL);
    g_assert(!qnum_get_try_int(qn, &ival));
    qobject_unref(qn);

    qn = qnum_from_double(0.42);
    g_assert(!qnum_get_try_uint(qn, &val));
    qobject_unref(qn);
}

static void qobject_to_qnum_test(void)
{
    QNum *qn;

    qn = qnum_from_int(0);
    g_assert(qobject_to(QNum, QOBJECT(qn)) == qn);
    qobject_unref(qn);

    qn = qnum_from_double(0);
    g_assert(qobject_to(QNum, QOBJECT(qn)) == qn);
    qobject_unref(qn);
}

static void qnum_to_string_test(void)
{
    QNum *qn;
    char *tmp;

    qn = qnum_from_int(123456);
    tmp = qnum_to_string(qn);
    g_assert_cmpstr(tmp, ==, "123456");
    g_free(tmp);
    qobject_unref(qn);

    qn = qnum_from_double(0.42);
    tmp = qnum_to_string(qn);
    g_assert_cmpstr(tmp, ==, "0.41999999999999998");
    g_free(tmp);
    qobject_unref(qn);

    qn = qnum_from_double(2.718281828459045);
    tmp = qnum_to_string(qn);
    g_assert_cmpstr(tmp, ==, "2.7182818284590451");
    g_free(tmp);
    qobject_unref(qn);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/qnum/from_int", qnum_from_int_test);
    g_test_add_func("/qnum/from_uint", qnum_from_uint_test);
    g_test_add_func("/qnum/from_double", qnum_from_double_test);
    g_test_add_func("/qnum/from_int64", qnum_from_int64_test);
    g_test_add_func("/qnum/get_int", qnum_get_int_test);
    g_test_add_func("/qnum/get_uint", qnum_get_uint_test);
    g_test_add_func("/qnum/to_qnum", qobject_to_qnum_test);
    g_test_add_func("/qnum/to_string", qnum_to_string_test);

    return g_test_run();
}
