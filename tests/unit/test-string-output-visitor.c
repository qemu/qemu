/*
 * String Output Visitor unit-tests.
 *
 * Copyright (C) 2012 Red Hat Inc.
 *
 * Authors:
 *  Paolo Bonzini <pbonzini@redhat.com> (based on test-qobject-output-visitor)
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qapi/string-output-visitor.h"
#include "test-qapi-visit.h"

typedef struct TestOutputVisitorData {
    Visitor *ov;
    char *str;
    bool human;
} TestOutputVisitorData;

static void visitor_output_setup_internal(TestOutputVisitorData *data,
                                          bool human)
{
    data->human = human;
    data->ov = string_output_visitor_new(human, &data->str);
    g_assert(data->ov);
}

static void visitor_output_setup(TestOutputVisitorData *data,
                                 const void *unused)
{
    return visitor_output_setup_internal(data, false);
}

static void visitor_output_setup_human(TestOutputVisitorData *data,
                                       const void *unused)
{
    return visitor_output_setup_internal(data, true);
}

static void visitor_output_teardown(TestOutputVisitorData *data,
                                    const void *unused)
{
    visit_free(data->ov);
    data->ov = NULL;
    g_free(data->str);
    data->str = NULL;
}

static char *visitor_get(TestOutputVisitorData *data)
{
    visit_complete(data->ov, &data->str);
    g_assert(data->str);
    return data->str;
}

static void visitor_reset(TestOutputVisitorData *data)
{
    bool human = data->human;

    visitor_output_teardown(data, NULL);
    visitor_output_setup_internal(data, human);
}

static void test_visitor_out_int(TestOutputVisitorData *data,
                                 const void *unused)
{
    int64_t value = 42;
    char *str;

    visit_type_int(data->ov, NULL, &value, &error_abort);

    str = visitor_get(data);
    if (data->human) {
        g_assert_cmpstr(str, ==, "42 (0x2a)");
    } else {
        g_assert_cmpstr(str, ==, "42");
    }
}

static void test_visitor_out_intList(TestOutputVisitorData *data,
                                     const void *unused)
{
    int64_t value[] = {0, 1, 9, 10, 16, 15, 14,
        3, 4, 5, 6, 11, 12, 13, 21, 22, INT64_MAX - 1, INT64_MAX};
    intList *list = NULL, **tail = &list;
    int i;
    Error *err = NULL;
    char *str;

    for (i = 0; i < ARRAY_SIZE(value); i++) {
        QAPI_LIST_APPEND(tail, value[i]);
    }

    visit_type_intList(data->ov, NULL, &list, &err);
    g_assert(err == NULL);

    str = visitor_get(data);
    if (data->human) {
        g_assert_cmpstr(str, ==,
            "0-1,3-6,9-16,21-22,9223372036854775806-9223372036854775807 "
            "(0x0-0x1,0x3-0x6,0x9-0x10,0x15-0x16,"
            "0x7ffffffffffffffe-0x7fffffffffffffff)");
    } else {
        g_assert_cmpstr(str, ==,
            "0-1,3-6,9-16,21-22,9223372036854775806-9223372036854775807");
    }
    qapi_free_intList(list);
}

static void test_visitor_out_bool(TestOutputVisitorData *data,
                                  const void *unused)
{
    bool value = true;
    char *str;

    visit_type_bool(data->ov, NULL, &value, &error_abort);

    str = visitor_get(data);
    g_assert_cmpstr(str, ==, "true");
}

static void test_visitor_out_number(TestOutputVisitorData *data,
                                    const void *unused)
{
    double value = 3.1415926535897932;
    char *str;

    visit_type_number(data->ov, NULL, &value, &error_abort);

    str = visitor_get(data);
    g_assert_cmpstr(str, ==, "3.1415926535897931");
}

static void test_visitor_out_string(TestOutputVisitorData *data,
                                    const void *unused)
{
    char *string = (char *) "Q E M U";
    const char *string_human = "\"Q E M U\"";
    char *str;

    visit_type_str(data->ov, NULL, &string, &error_abort);

    str = visitor_get(data);
    if (data->human) {
        g_assert_cmpstr(str, ==, string_human);
    } else {
        g_assert_cmpstr(str, ==, string);
    }
}

static void test_visitor_out_no_string(TestOutputVisitorData *data,
                                       const void *unused)
{
    char *string = NULL;
    char *str;

    /* A null string should return "" */
    visit_type_str(data->ov, NULL, &string, &error_abort);

    str = visitor_get(data);
    if (data->human) {
        g_assert_cmpstr(str, ==, "<null>");
    } else {
        g_assert_cmpstr(str, ==, "");
    }
}

static void test_visitor_out_enum(TestOutputVisitorData *data,
                                  const void *unused)
{
    char *str;
    EnumOne i;

    for (i = 0; i < ENUM_ONE__MAX; i++) {
        visit_type_EnumOne(data->ov, "unused", &i, &error_abort);

        str = visitor_get(data);
        if (data->human) {
            char *str_human = g_strdup_printf("\"%s\"", EnumOne_str(i));

            g_assert_cmpstr(str, ==, str_human);
            g_free(str_human);
        } else {
            g_assert_cmpstr(str, ==, EnumOne_str(i));
        }
        visitor_reset(data);
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
    output_visitor_test_add("/string-visitor/output/intList",
                            &out_visitor_data, test_visitor_out_intList, false);
    output_visitor_test_add("/string-visitor/output/intList-human",
                            &out_visitor_data, test_visitor_out_intList, true);

    g_test_run();

    return 0;
}
