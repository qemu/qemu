/*
 * String Input Visitor unit-tests.
 *
 * Copyright (C) 2012 Red Hat Inc.
 *
 * Authors:
 *  Paolo Bonzini <pbonzini@redhat.com> (based on test-qmp-input-visitor)
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
    StringInputVisitor *siv;
} TestInputVisitorData;

static void visitor_input_teardown(TestInputVisitorData *data,
                                   const void *unused)
{
    if (data->siv) {
        string_input_visitor_cleanup(data->siv);
        data->siv = NULL;
    }
}

/* This is provided instead of a test setup function so that the JSON
   string used by the tests are kept in the test functions (and not
   int main()) */
static
Visitor *visitor_input_test_init(TestInputVisitorData *data,
                                 const char *string)
{
    Visitor *v;

    data->siv = string_input_visitor_new(string);
    g_assert(data->siv != NULL);

    v = string_input_get_visitor(data->siv);
    g_assert(v != NULL);

    return v;
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

    visitor_input_teardown(data, unused);

    v = visitor_input_test_init(data, "not an int");

    visit_type_int(v, NULL, &res, &err);
    error_free_or_abort(&err);
}

static void test_visitor_in_intList(TestInputVisitorData *data,
                                    const void *unused)
{
    int64_t value[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 20};
    int16List *res = NULL, *tmp;
    Error *err = NULL;
    Visitor *v;
    int i = 0;

    v = visitor_input_test_init(data, "1,2,0,2-4,20,5-9,1-8");

    visit_type_int16List(v, NULL, &res, &error_abort);
    tmp = res;
    while (i < sizeof(value) / sizeof(value[0])) {
        g_assert(tmp);
        g_assert_cmpint(tmp->value, ==, value[i++]);
        tmp = tmp->next;
    }
    g_assert(!tmp);

    qapi_free_int16List(res);

    visitor_input_teardown(data, unused);

    v = visitor_input_test_init(data, "not an int list");

    visit_type_int16List(v, NULL, &res, &err);
    error_free_or_abort(&err);
    g_assert(!res);
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
    visitor_input_teardown(data, unused);

    v = visitor_input_test_init(data, "yes");

    visit_type_bool(v, NULL, &res, &err);
    g_assert(!err);
    g_assert_cmpint(res, ==, true);
    visitor_input_teardown(data, unused);

    v = visitor_input_test_init(data, "on");

    visit_type_bool(v, NULL, &res, &err);
    g_assert(!err);
    g_assert_cmpint(res, ==, true);
    visitor_input_teardown(data, unused);

    v = visitor_input_test_init(data, "false");

    visit_type_bool(v, NULL, &res, &err);
    g_assert(!err);
    g_assert_cmpint(res, ==, false);
    visitor_input_teardown(data, unused);

    v = visitor_input_test_init(data, "no");

    visit_type_bool(v, NULL, &res, &err);
    g_assert(!err);
    g_assert_cmpint(res, ==, false);
    visitor_input_teardown(data, unused);

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

        visitor_input_teardown(data, NULL);
    }

    data->siv = NULL;
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
        visitor_input_teardown(data, NULL);

        v = visitor_input_test_init(data, buf);
        visit_type_intList(v, NULL, &ilres, NULL);
        visitor_input_teardown(data, NULL);

        v = visitor_input_test_init(data, buf);
        visit_type_bool(v, NULL, &bres, NULL);
        visitor_input_teardown(data, NULL);

        v = visitor_input_test_init(data, buf);
        visit_type_number(v, NULL, &nres, NULL);
        visitor_input_teardown(data, NULL);

        v = visitor_input_test_init(data, buf);
        sres = NULL;
        visit_type_str(v, NULL, &sres, NULL);
        g_free(sres);
        visitor_input_teardown(data, NULL);

        v = visitor_input_test_init(data, buf);
        visit_type_EnumOne(v, NULL, &eres, NULL);
        visitor_input_teardown(data, NULL);
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
