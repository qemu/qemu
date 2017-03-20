/*
 * String Input Visitor unit-tests.
 *
 * Copyright (C) 2012 Red Hat Inc.
 *
 * Authors:
 *  Paolo Bonzini <pbonzini@redhat.com> (based on test-qobject-input-visitor)
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qemu-common.h"
#include "qapi/error.h"
#include "qapi/string-input-visitor.h"
#include "test-qapi-types.h"
#include "test-qapi-visit.h"
#include "qapi/qmp/types.h"

typedef struct TestInputVisitorData {
    Visitor *v;
} TestInputVisitorData;

static void visitor_input_teardown(TestInputVisitorData *data,
                                   const void *unused)
{
    if (data->v) {
        visit_free(data->v);
        data->v = NULL;
    }
}

/* This is provided instead of a test setup function so that the JSON
   string used by the tests are kept in the test functions (and not
   int main()) */
static
Visitor *visitor_input_test_init(TestInputVisitorData *data,
                                 const char *string)
{
    visitor_input_teardown(data, NULL);

    data->v = string_input_visitor_new(string);
    g_assert(data->v);
    return data->v;
}

static void test_visitor_in_int(TestInputVisitorData *data,
                                const void *unused)
{
    int64_t res = 0, value = -42;
    Error *err = NULL;
    Visitor *v;

    v = visitor_input_test_init(data, "-42");

    visit_type_int(v, NULL, &res, &err);
    g_assert(!err);
    g_assert_cmpint(res, ==, value);

    v = visitor_input_test_init(data, "not an int");

    visit_type_int(v, NULL, &res, &err);
    error_free_or_abort(&err);

    v = visitor_input_test_init(data, "");

    visit_type_int(v, NULL, &res, &err);
    error_free_or_abort(&err);
}

static void check_ilist(Visitor *v, int64_t *expected, size_t n)
{
    int64List *res = NULL;
    int64List *tail;
    int i;

    visit_type_int64List(v, NULL, &res, &error_abort);
    tail = res;
    for (i = 0; i < n; i++) {
        g_assert(tail);
        g_assert_cmpint(tail->value, ==, expected[i]);
        tail = tail->next;
    }
    g_assert(!tail);

    qapi_free_int64List(res);
}

static void check_ulist(Visitor *v, uint64_t *expected, size_t n)
{
    uint64List *res = NULL;
    uint64List *tail;
    int i;

    /* BUG: unsigned numbers above INT64_MAX don't work */
    for (i = 0; i < n; i++) {
        if (expected[i] > INT64_MAX) {
            Error *err = NULL;
            visit_type_uint64List(v, NULL, &res, &err);
            error_free_or_abort(&err);
            return;
        }
    }

    visit_type_uint64List(v, NULL, &res, &error_abort);
    tail = res;
    for (i = 0; i < n; i++) {
        g_assert(tail);
        g_assert_cmpuint(tail->value, ==, expected[i]);
        tail = tail->next;
    }
    g_assert(!tail);

    qapi_free_uint64List(res);
}

static void test_visitor_in_intList(TestInputVisitorData *data,
                                    const void *unused)
{
    /* Note: the visitor *sorts* ranges *unsigned* */
    int64_t expect1[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 20 };
    int64_t expect2[] = { 32767, -32768, -32767 };
    int64_t expect3[] = { INT64_MAX, INT64_MIN };
    uint64_t expect4[] = { UINT64_MAX };
    Error *err = NULL;
    int64List *res = NULL;
    int64List *tail;
    Visitor *v;
    int64_t val;

    /* Valid lists */

    v = visitor_input_test_init(data, "1,2,0,2-4,20,5-9,1-8");
    check_ilist(v, expect1, ARRAY_SIZE(expect1));

    v = visitor_input_test_init(data, "32767,-32768--32767");
    check_ilist(v, expect2, ARRAY_SIZE(expect2));

    v = visitor_input_test_init(data,
                                "-9223372036854775808,9223372036854775807");
    check_ilist(v, expect3, ARRAY_SIZE(expect3));

    v = visitor_input_test_init(data, "18446744073709551615");
    check_ulist(v, expect4, ARRAY_SIZE(expect4));

    /* Empty list */

    v = visitor_input_test_init(data, "");
    visit_type_int64List(v, NULL, &res, &error_abort);
    g_assert(!res);

    /* Not a list */

    v = visitor_input_test_init(data, "not an int list");

    visit_type_int64List(v, NULL, &res, &err);
    error_free_or_abort(&err);
    g_assert(!res);

    /* Unvisited list tail */

    v = visitor_input_test_init(data, "0,2-3");

    /* Would be simpler if the visitor genuinely supported virtual walks */
    visit_start_list(v, NULL, (GenericList **)&res, sizeof(*res),
                     &error_abort);
    tail = res;
    visit_type_int64(v, NULL, &tail->value, &error_abort);
    g_assert_cmpint(tail->value, ==, 0);
    tail = (int64List *)visit_next_list(v, (GenericList *)tail, sizeof(*res));
    g_assert(tail);
    visit_type_int64(v, NULL, &tail->value, &error_abort);
    g_assert_cmpint(tail->value, ==, 2);
    tail = (int64List *)visit_next_list(v, (GenericList *)tail, sizeof(*res));
    g_assert(tail);

    visit_check_list(v, &err);
    error_free_or_abort(&err);
    visit_end_list(v, (void **)&res);

    qapi_free_int64List(res);

    /* Visit beyond end of list */
    v = visitor_input_test_init(data, "0");

    visit_start_list(v, NULL, (GenericList **)&res, sizeof(*res),
                     &error_abort);
    tail = res;
    visit_type_int64(v, NULL, &tail->value, &err);
    g_assert_cmpint(tail->value, ==, 0);
    visit_type_int64(v, NULL, &val, &err);
    g_assert_cmpint(val, ==, 1); /* BUG */
    visit_check_list(v, &error_abort);
    visit_end_list(v, (void **)&res);

    qapi_free_int64List(res);
}

static void test_visitor_in_bool(TestInputVisitorData *data,
                                 const void *unused)
{
    Error *err = NULL;
    bool res = false;
    Visitor *v;

    v = visitor_input_test_init(data, "true");

    visit_type_bool(v, NULL, &res, &err);
    g_assert(!err);
    g_assert_cmpint(res, ==, true);

    v = visitor_input_test_init(data, "yes");

    visit_type_bool(v, NULL, &res, &err);
    g_assert(!err);
    g_assert_cmpint(res, ==, true);

    v = visitor_input_test_init(data, "on");

    visit_type_bool(v, NULL, &res, &err);
    g_assert(!err);
    g_assert_cmpint(res, ==, true);

    v = visitor_input_test_init(data, "false");

    visit_type_bool(v, NULL, &res, &err);
    g_assert(!err);
    g_assert_cmpint(res, ==, false);

    v = visitor_input_test_init(data, "no");

    visit_type_bool(v, NULL, &res, &err);
    g_assert(!err);
    g_assert_cmpint(res, ==, false);

    v = visitor_input_test_init(data, "off");

    visit_type_bool(v, NULL, &res, &err);
    g_assert(!err);
    g_assert_cmpint(res, ==, false);
}

static void test_visitor_in_number(TestInputVisitorData *data,
                                   const void *unused)
{
    double res = 0, value = 3.14;
    Error *err = NULL;
    Visitor *v;

    v = visitor_input_test_init(data, "3.14");

    visit_type_number(v, NULL, &res, &err);
    g_assert(!err);
    g_assert_cmpfloat(res, ==, value);
}

static void test_visitor_in_string(TestInputVisitorData *data,
                                   const void *unused)
{
    char *res = NULL, *value = (char *) "Q E M U";
    Error *err = NULL;
    Visitor *v;

    v = visitor_input_test_init(data, value);

    visit_type_str(v, NULL, &res, &err);
    g_assert(!err);
    g_assert_cmpstr(res, ==, value);

    g_free(res);
}

static void test_visitor_in_enum(TestInputVisitorData *data,
                                 const void *unused)
{
    Error *err = NULL;
    Visitor *v;
    EnumOne i;

    for (i = 0; EnumOne_lookup[i]; i++) {
        EnumOne res = -1;

        v = visitor_input_test_init(data, EnumOne_lookup[i]);

        visit_type_EnumOne(v, NULL, &res, &err);
        g_assert(!err);
        g_assert_cmpint(i, ==, res);
    }
}

/* Try to crash the visitors */
static void test_visitor_in_fuzz(TestInputVisitorData *data,
                                 const void *unused)
{
    int64_t ires;
    intList *ilres;
    bool bres;
    double nres;
    char *sres;
    EnumOne eres;
    Visitor *v;
    unsigned int i;
    char buf[10000];

    for (i = 0; i < 100; i++) {
        unsigned int j;

        j = g_test_rand_int_range(0, sizeof(buf) - 1);

        buf[j] = '\0';

        if (j != 0) {
            for (j--; j != 0; j--) {
                buf[j - 1] = (char)g_test_rand_int_range(0, 256);
            }
        }

        v = visitor_input_test_init(data, buf);
        visit_type_int(v, NULL, &ires, NULL);

        v = visitor_input_test_init(data, buf);
        visit_type_intList(v, NULL, &ilres, NULL);
        qapi_free_intList(ilres);

        v = visitor_input_test_init(data, buf);
        visit_type_bool(v, NULL, &bres, NULL);

        v = visitor_input_test_init(data, buf);
        visit_type_number(v, NULL, &nres, NULL);

        v = visitor_input_test_init(data, buf);
        sres = NULL;
        visit_type_str(v, NULL, &sres, NULL);
        g_free(sres);

        v = visitor_input_test_init(data, buf);
        visit_type_EnumOne(v, NULL, &eres, NULL);
    }
}

static void input_visitor_test_add(const char *testpath,
                                   TestInputVisitorData *data,
                                   void (*test_func)(TestInputVisitorData *data, const void *user_data))
{
    g_test_add(testpath, TestInputVisitorData, data, NULL, test_func,
               visitor_input_teardown);
}

int main(int argc, char **argv)
{
    TestInputVisitorData in_visitor_data;

    g_test_init(&argc, &argv, NULL);

    input_visitor_test_add("/string-visitor/input/int",
                           &in_visitor_data, test_visitor_in_int);
    input_visitor_test_add("/string-visitor/input/intList",
                           &in_visitor_data, test_visitor_in_intList);
    input_visitor_test_add("/string-visitor/input/bool",
                           &in_visitor_data, test_visitor_in_bool);
    input_visitor_test_add("/string-visitor/input/number",
                           &in_visitor_data, test_visitor_in_number);
    input_visitor_test_add("/string-visitor/input/string",
                            &in_visitor_data, test_visitor_in_string);
    input_visitor_test_add("/string-visitor/input/enum",
                            &in_visitor_data, test_visitor_in_enum);
    input_visitor_test_add("/string-visitor/input/fuzz",
                            &in_visitor_data, test_visitor_in_fuzz);

    g_test_run();

    return 0;
}
