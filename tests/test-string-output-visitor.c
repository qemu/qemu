/*
 * String Output Visitor unit-tests.
 *
 * Copyright (C) 2012 Red Hat Inc.
 *
 * Authors:
 *  Paolo Bonzini <pbonzini@redhat.com> (based on test-qmp-output-visitor)
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <glib.h>

#include "qapi/string-output-visitor.h"
#include "test-qapi-types.h"
#include "test-qapi-visit.h"
#include "qemu-objects.h"

typedef struct TestOutputVisitorData {
    StringOutputVisitor *sov;
    Visitor *ov;
} TestOutputVisitorData;

static void visitor_output_setup(TestOutputVisitorData *data,
                                 const void *unused)
{
    data->sov = string_output_visitor_new();
    g_assert(data->sov != NULL);

    data->ov = string_output_get_visitor(data->sov);
    g_assert(data->ov != NULL);
}

static void visitor_output_teardown(TestOutputVisitorData *data,
                                    const void *unused)
{
    string_output_visitor_cleanup(data->sov);
    data->sov = NULL;
    data->ov = NULL;
}

static void test_visitor_out_int(TestOutputVisitorData *data,
                                 const void *unused)
{
    int64_t value = -42;
    Error *errp = NULL;
    char *str;

    visit_type_int(data->ov, &value, NULL, &errp);
    g_assert(error_is_set(&errp) == 0);

    str = string_output_get_string(data->sov);
    g_assert(str != NULL);
    g_assert_cmpstr(str, ==, "-42");
    g_free(str);
}

static void test_visitor_out_bool(TestOutputVisitorData *data,
                                  const void *unused)
{
    Error *errp = NULL;
    bool value = true;
    char *str;

    visit_type_bool(data->ov, &value, NULL, &errp);
    g_assert(error_is_set(&errp) == 0);

    str = string_output_get_string(data->sov);
    g_assert(str != NULL);
    g_assert_cmpstr(str, ==, "true");
    g_free(str);
}

static void test_visitor_out_number(TestOutputVisitorData *data,
                                    const void *unused)
{
    double value = 3.14;
    Error *errp = NULL;
    char *str;

    visit_type_number(data->ov, &value, NULL, &errp);
    g_assert(error_is_set(&errp) == 0);

    str = string_output_get_string(data->sov);
    g_assert(str != NULL);
    g_assert_cmpstr(str, ==, "3.14");
    g_free(str);
}

static void test_visitor_out_string(TestOutputVisitorData *data,
                                    const void *unused)
{
    char *string = (char *) "Q E M U";
    Error *errp = NULL;
    char *str;

    visit_type_str(data->ov, &string, NULL, &errp);
    g_assert(error_is_set(&errp) == 0);

    str = string_output_get_string(data->sov);
    g_assert(str != NULL);
    g_assert_cmpstr(str, ==, string);
    g_free(str);
}

static void test_visitor_out_no_string(TestOutputVisitorData *data,
                                       const void *unused)
{
    char *string = NULL;
    Error *errp = NULL;
    char *str;

    /* A null string should return "" */
    visit_type_str(data->ov, &string, NULL, &errp);
    g_assert(error_is_set(&errp) == 0);

    str = string_output_get_string(data->sov);
    g_assert(str != NULL);
    g_assert_cmpstr(str, ==, "");
    g_free(str);
}

static void test_visitor_out_enum(TestOutputVisitorData *data,
                                  const void *unused)
{
    Error *errp = NULL;
    char *str;
    EnumOne i;

    for (i = 0; i < ENUM_ONE_MAX; i++) {
        visit_type_EnumOne(data->ov, &i, "unused", &errp);
        g_assert(!error_is_set(&errp));

        str = string_output_get_string(data->sov);
        g_assert(str != NULL);
        g_assert_cmpstr(str, ==, EnumOne_lookup[i]);
	g_free(str);
    }
}

static void test_visitor_out_enum_errors(TestOutputVisitorData *data,
                                         const void *unused)
{
    EnumOne i, bad_values[] = { ENUM_ONE_MAX, -1 };
    Error *errp;

    for (i = 0; i < ARRAY_SIZE(bad_values) ; i++) {
        errp = NULL;
        visit_type_EnumOne(data->ov, &bad_values[i], "unused", &errp);
        g_assert(error_is_set(&errp) == true);
        error_free(errp);
    }
}

static void output_visitor_test_add(const char *testpath,
                                    TestOutputVisitorData *data,
                                    void (*test_func)(TestOutputVisitorData *data, const void *user_data))
{
    g_test_add(testpath, TestOutputVisitorData, data, visitor_output_setup,
               test_func, visitor_output_teardown);
}

int main(int argc, char **argv)
{
    TestOutputVisitorData out_visitor_data;

    g_test_init(&argc, &argv, NULL);

    output_visitor_test_add("/string-visitor/output/int",
                            &out_visitor_data, test_visitor_out_int);
    output_visitor_test_add("/string-visitor/output/bool",
                            &out_visitor_data, test_visitor_out_bool);
    output_visitor_test_add("/string-visitor/output/number",
                            &out_visitor_data, test_visitor_out_number);
    output_visitor_test_add("/string-visitor/output/string",
                            &out_visitor_data, test_visitor_out_string);
    output_visitor_test_add("/string-visitor/output/no-string",
                            &out_visitor_data, test_visitor_out_no_string);
    output_visitor_test_add("/string-visitor/output/enum",
                            &out_visitor_data, test_visitor_out_enum);
    output_visitor_test_add("/string-visitor/output/enum-errors",
                            &out_visitor_data, test_visitor_out_enum_errors);

    g_test_run();

    return 0;
}
