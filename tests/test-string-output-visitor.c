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

#include "qemu/osdep.h"

#include "qemu-common.h"
#include "qapi/error.h"
#include "qapi/string-output-visitor.h"
#include "test-qapi-types.h"
#include "test-qapi-visit.h"
#include "qapi/qmp/types.h"

typedef struct TestOutputVisitorData {
    StringOutputVisitor *sov;
    Visitor *ov;
    bool human;
} TestOutputVisitorData;

static void visitor_output_setup(TestOutputVisitorData *data,
                                 const void *unused)
{
    data->human = false;
    data->sov = string_output_visitor_new(data->human);
    g_assert(data->sov != NULL);

    data->ov = string_output_get_visitor(data->sov);
    g_assert(data->ov != NULL);
}

static void visitor_output_setup_human(TestOutputVisitorData *data,
                                       const void *unused)
{
    data->human = true;
    data->sov = string_output_visitor_new(data->human);
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
    int64_t value = 42;
    Error *err = NULL;
    char *str;

    visit_type_int(data->ov, NULL, &value, &err);
    g_assert(!err);

    str = string_output_get_string(data->sov);
    g_assert(str != NULL);
    if (data->human) {
        g_assert_cmpstr(str, ==, "42 (0x2a)");
    } else {
        g_assert_cmpstr(str, ==, "42");
    }
    g_free(str);
}

static void test_visitor_out_intList(TestOutputVisitorData *data,
                                     const void *unused)
{
    int64_t value[] = {0, 1, 9, 10, 16, 15, 14,
        3, 4, 5, 6, 11, 12, 13, 21, 22, INT64_MAX - 1, INT64_MAX};
    intList *list = NULL, **tmp = &list;
    int i;
    Error *err = NULL;
    char *str;

    for (i = 0; i < sizeof(value) / sizeof(value[0]); i++) {
        *tmp = g_malloc0(sizeof(**tmp));
        (*tmp)->value = value[i];
        tmp = &(*tmp)->next;
    }

    visit_type_intList(data->ov, NULL, &list, &err);
    g_assert(err == NULL);

    str = string_output_get_string(data->sov);
    g_assert(str != NULL);
    if (data->human) {
        g_assert_cmpstr(str, ==,
            "0-1,3-6,9-16,21-22,9223372036854775806-9223372036854775807 "
            "(0x0-0x1,0x3-0x6,0x9-0x10,0x15-0x16,"
            "0x7ffffffffffffffe-0x7fffffffffffffff)");
    } else {
        g_assert_cmpstr(str, ==,
            "0-1,3-6,9-16,21-22,9223372036854775806-9223372036854775807");
    }
    g_free(str);
    while (list) {
        intList *tmp2;
        tmp2 = list->next;
        g_free(list);
        list = tmp2;
    }
}

static void test_visitor_out_bool(TestOutputVisitorData *data,
                                  const void *unused)
{
    Error *err = NULL;
    bool value = true;
    char *str;

    visit_type_bool(data->ov, NULL, &value, &err);
    g_assert(!err);

    str = string_output_get_string(data->sov);
    g_assert(str != NULL);
    g_assert_cmpstr(str, ==, "true");
    g_free(str);
}

static void test_visitor_out_number(TestOutputVisitorData *data,
                                    const void *unused)
{
    double value = 3.14;
    Error *err = NULL;
    char *str;

    visit_type_number(data->ov, NULL, &value, &err);
    g_assert(!err);

    str = string_output_get_string(data->sov);
    g_assert(str != NULL);
    g_assert_cmpstr(str, ==, "3.140000");
    g_free(str);
}

static void test_visitor_out_string(TestOutputVisitorData *data,
                                    const void *unused)
{
    char *string = (char *) "Q E M U";
    const char *string_human = "\"Q E M U\"";
    Error *err = NULL;
    char *str;

    visit_type_str(data->ov, NULL, &string, &err);
    g_assert(!err);

    str = string_output_get_string(data->sov);
    g_assert(str != NULL);
    if (data->human) {
        g_assert_cmpstr(str, ==, string_human);
    } else {
        g_assert_cmpstr(str, ==, string);
    }
    g_free(str);
}

static void test_visitor_out_no_string(TestOutputVisitorData *data,
                                       const void *unused)
{
    char *string = NULL;
    Error *err = NULL;
    char *str;

    /* A null string should return "" */
    visit_type_str(data->ov, NULL, &string, &err);
    g_assert(!err);

    str = string_output_get_string(data->sov);
    g_assert(str != NULL);
    if (data->human) {
        g_assert_cmpstr(str, ==, "<null>");
    } else {
        g_assert_cmpstr(str, ==, "");
    }
    g_free(str);
}

static void test_visitor_out_enum(TestOutputVisitorData *data,
                                  const void *unused)
{
    Error *err = NULL;
    char *str;
    EnumOne i;

    for (i = 0; i < ENUM_ONE__MAX; i++) {
        char *str_human;

        visit_type_EnumOne(data->ov, "unused", &i, &err);
        g_assert(!err);

        str_human = g_strdup_printf("\"%s\"", EnumOne_lookup[i]);

        str = string_output_get_string(data->sov);
        g_assert(str != NULL);
        if (data->human) {
            g_assert_cmpstr(str, ==, str_human);
        } else {
            g_assert_cmpstr(str, ==, EnumOne_lookup[i]);
        }
        g_free(str_human);
	g_free(str);
    }
}

static void test_visitor_out_enum_errors(TestOutputVisitorData *data,
                                         const void *unused)
{
    EnumOne i, bad_values[] = { ENUM_ONE__MAX, -1 };
    Error *err;

    for (i = 0; i < ARRAY_SIZE(bad_values) ; i++) {
        err = NULL;
        visit_type_EnumOne(data->ov, "unused", &bad_values[i], &err);
        g_assert(err);
        error_free(err);
    }
}

static void
output_visitor_test_add(const char *testpath,
                        TestOutputVisitorData *data,
                        void (*test_func)(TestOutputVisitorData *data,
                                          const void *user_data),
                        bool human)
{
    g_test_add(testpath, TestOutputVisitorData, data,
               human ? visitor_output_setup_human : visitor_output_setup,
               test_func, visitor_output_teardown);
}

int main(int argc, char **argv)
{
    TestOutputVisitorData out_visitor_data;

    g_test_init(&argc, &argv, NULL);

    output_visitor_test_add("/string-visitor/output/int",
                            &out_visitor_data, test_visitor_out_int, false);
    output_visitor_test_add("/string-visitor/output/int-human",
                            &out_visitor_data, test_visitor_out_int, true);
    output_visitor_test_add("/string-visitor/output/bool",
                            &out_visitor_data, test_visitor_out_bool, false);
    output_visitor_test_add("/string-visitor/output/bool-human",
                            &out_visitor_data, test_visitor_out_bool, true);
    output_visitor_test_add("/string-visitor/output/number",
                            &out_visitor_data, test_visitor_out_number, false);
    output_visitor_test_add("/string-visitor/output/number-human",
                            &out_visitor_data, test_visitor_out_number, true);
    output_visitor_test_add("/string-visitor/output/string",
                            &out_visitor_data, test_visitor_out_string, false);
    output_visitor_test_add("/string-visitor/output/string-human",
                            &out_visitor_data, test_visitor_out_string, true);
    output_visitor_test_add("/string-visitor/output/no-string",
                            &out_visitor_data, test_visitor_out_no_string,
                            false);
    output_visitor_test_add("/string-visitor/output/no-string-human",
                            &out_visitor_data, test_visitor_out_no_string,
                            true);
    output_visitor_test_add("/string-visitor/output/enum",
                            &out_visitor_data, test_visitor_out_enum, false);
    output_visitor_test_add("/string-visitor/output/enum-human",
                            &out_visitor_data, test_visitor_out_enum, true);
    output_visitor_test_add("/string-visitor/output/enum-errors",
                            &out_visitor_data, test_visitor_out_enum_errors,
                            false);
    output_visitor_test_add("/string-visitor/output/enum-errors-human",
                            &out_visitor_data, test_visitor_out_enum_errors,
                            true);
    output_visitor_test_add("/string-visitor/output/intList",
                            &out_visitor_data, test_visitor_out_intList, false);
    output_visitor_test_add("/string-visitor/output/intList-human",
                            &out_visitor_data, test_visitor_out_intList, true);

    g_test_run();

    return 0;
}
