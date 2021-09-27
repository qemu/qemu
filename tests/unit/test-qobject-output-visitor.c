/*
 * QObject Output Visitor unit-tests.
 *
 * Copyright (C) 2011-2016 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qemu-common.h"
#include "qapi/error.h"
#include "qapi/qobject-output-visitor.h"
#include "test-qapi-visit.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qmp/qnull.h"
#include "qapi/qmp/qnum.h"
#include "qapi/qmp/qstring.h"

typedef struct TestOutputVisitorData {
    Visitor *ov;
    QObject *obj;
} TestOutputVisitorData;

static void visitor_output_setup(TestOutputVisitorData *data,
                                 const void *unused)
{
    data->ov = qobject_output_visitor_new(&data->obj);
    g_assert(data->ov);
}

static void visitor_output_teardown(TestOutputVisitorData *data,
                                    const void *unused)
{
    visit_free(data->ov);
    data->ov = NULL;
    qobject_unref(data->obj);
    data->obj = NULL;
}

static QObject *visitor_get(TestOutputVisitorData *data)
{
    visit_complete(data->ov, &data->obj);
    g_assert(data->obj);
    return data->obj;
}

static void visitor_reset(TestOutputVisitorData *data)
{
    visitor_output_teardown(data, NULL);
    visitor_output_setup(data, NULL);
}

static void test_visitor_out_int(TestOutputVisitorData *data,
                                 const void *unused)
{
    int64_t value = -42;
    int64_t val;
    QNum *qnum;

    visit_type_int(data->ov, NULL, &value, &error_abort);

    qnum = qobject_to(QNum, visitor_get(data));
    g_assert(qnum);
    g_assert(qnum_get_try_int(qnum, &val));
    g_assert_cmpint(val, ==, value);
}

static void test_visitor_out_bool(TestOutputVisitorData *data,
                                  const void *unused)
{
    bool value = true;
    QBool *qbool;

    visit_type_bool(data->ov, NULL, &value, &error_abort);

    qbool = qobject_to(QBool, visitor_get(data));
    g_assert(qbool);
    g_assert(qbool_get_bool(qbool) == value);
}

static void test_visitor_out_number(TestOutputVisitorData *data,
                                    const void *unused)
{
    double value = 3.14;
    QNum *qnum;

    visit_type_number(data->ov, NULL, &value, &error_abort);

    qnum = qobject_to(QNum, visitor_get(data));
    g_assert(qnum);
    g_assert(qnum_get_double(qnum) == value);
}

static void test_visitor_out_string(TestOutputVisitorData *data,
                                    const void *unused)
{
    char *string = (char *) "Q E M U";
    QString *qstr;

    visit_type_str(data->ov, NULL, &string, &error_abort);

    qstr = qobject_to(QString, visitor_get(data));
    g_assert(qstr);
    g_assert_cmpstr(qstring_get_str(qstr), ==, string);
}

static void test_visitor_out_no_string(TestOutputVisitorData *data,
                                       const void *unused)
{
    char *string = NULL;
    QString *qstr;

    /* A null string should return "" */
    visit_type_str(data->ov, NULL, &string, &error_abort);

    qstr = qobject_to(QString, visitor_get(data));
    g_assert(qstr);
    g_assert_cmpstr(qstring_get_str(qstr), ==, "");
}

static void test_visitor_out_enum(TestOutputVisitorData *data,
                                  const void *unused)
{
    EnumOne i;
    QString *qstr;

    for (i = 0; i < ENUM_ONE__MAX; i++) {
        visit_type_EnumOne(data->ov, "unused", &i, &error_abort);

        qstr = qobject_to(QString, visitor_get(data));
        g_assert(qstr);
        g_assert_cmpstr(qstring_get_str(qstr), ==, EnumOne_str(i));
        visitor_reset(data);
    }
}

static void test_visitor_out_struct(TestOutputVisitorData *data,
                                    const void *unused)
{
    TestStruct test_struct = { .integer = 42,
                               .boolean = false,
                               .string = (char *) "foo"};
    TestStruct *p = &test_struct;
    QDict *qdict;

    visit_type_TestStruct(data->ov, NULL, &p, &error_abort);

    qdict = qobject_to(QDict, visitor_get(data));
    g_assert(qdict);
    g_assert_cmpint(qdict_size(qdict), ==, 3);
    g_assert_cmpint(qdict_get_int(qdict, "integer"), ==, 42);
    g_assert_cmpint(qdict_get_bool(qdict, "boolean"), ==, false);
    g_assert_cmpstr(qdict_get_str(qdict, "string"), ==, "foo");
}

static void test_visitor_out_struct_nested(TestOutputVisitorData *data,
                                           const void *unused)
{
    int64_t value = 42;
    UserDefTwo *ud2;
    QDict *qdict, *dict1, *dict2, *dict3, *userdef;
    const char *string = "user def string";
    const char *strings[] = { "forty two", "forty three", "forty four",
                              "forty five" };

    ud2 = g_malloc0(sizeof(*ud2));
    ud2->string0 = g_strdup(strings[0]);

    ud2->dict1 = g_malloc0(sizeof(*ud2->dict1));
    ud2->dict1->string1 = g_strdup(strings[1]);

    ud2->dict1->dict2 = g_malloc0(sizeof(*ud2->dict1->dict2));
    ud2->dict1->dict2->userdef = g_new0(UserDefOne, 1);
    ud2->dict1->dict2->userdef->string = g_strdup(string);
    ud2->dict1->dict2->userdef->integer = value;
    ud2->dict1->dict2->string = g_strdup(strings[2]);

    ud2->dict1->dict3 = g_malloc0(sizeof(*ud2->dict1->dict3));
    ud2->dict1->has_dict3 = true;
    ud2->dict1->dict3->userdef = g_new0(UserDefOne, 1);
    ud2->dict1->dict3->userdef->string = g_strdup(string);
    ud2->dict1->dict3->userdef->integer = value;
    ud2->dict1->dict3->string = g_strdup(strings[3]);

    visit_type_UserDefTwo(data->ov, "unused", &ud2, &error_abort);

    qdict = qobject_to(QDict, visitor_get(data));
    g_assert(qdict);
    g_assert_cmpint(qdict_size(qdict), ==, 2);
    g_assert_cmpstr(qdict_get_str(qdict, "string0"), ==, strings[0]);

    dict1 = qdict_get_qdict(qdict, "dict1");
    g_assert_cmpint(qdict_size(dict1), ==, 3);
    g_assert_cmpstr(qdict_get_str(dict1, "string1"), ==, strings[1]);

    dict2 = qdict_get_qdict(dict1, "dict2");
    g_assert_cmpint(qdict_size(dict2), ==, 2);
    g_assert_cmpstr(qdict_get_str(dict2, "string"), ==, strings[2]);
    userdef = qdict_get_qdict(dict2, "userdef");
    g_assert_cmpint(qdict_size(userdef), ==, 2);
    g_assert_cmpint(qdict_get_int(userdef, "integer"), ==, value);
    g_assert_cmpstr(qdict_get_str(userdef, "string"), ==, string);

    dict3 = qdict_get_qdict(dict1, "dict3");
    g_assert_cmpint(qdict_size(dict3), ==, 2);
    g_assert_cmpstr(qdict_get_str(dict3, "string"), ==, strings[3]);
    userdef = qdict_get_qdict(dict3, "userdef");
    g_assert_cmpint(qdict_size(userdef), ==, 2);
    g_assert_cmpint(qdict_get_int(userdef, "integer"), ==, value);
    g_assert_cmpstr(qdict_get_str(userdef, "string"), ==, string);

    qapi_free_UserDefTwo(ud2);
}

static void test_visitor_out_list(TestOutputVisitorData *data,
                                  const void *unused)
{
    const char *value_str = "list value";
    TestStruct *value;
    TestStructList *head = NULL;
    const int max_items = 10;
    bool value_bool = true;
    int value_int = 10;
    QListEntry *entry;
    QList *qlist;
    int i;

    /* Build the list in reverse order... */
    for (i = 0; i < max_items; i++) {
        value = g_malloc0(sizeof(*value));
        value->integer = value_int + (max_items - i - 1);
        value->boolean = value_bool;
        value->string = g_strdup(value_str);

        QAPI_LIST_PREPEND(head, value);
    }

    visit_type_TestStructList(data->ov, NULL, &head, &error_abort);

    qlist = qobject_to(QList, visitor_get(data));
    g_assert(qlist);
    g_assert(!qlist_empty(qlist));

    /* ...and ensure that the visitor sees it in order */
    i = 0;
    QLIST_FOREACH_ENTRY(qlist, entry) {
        QDict *qdict;

        qdict = qobject_to(QDict, entry->value);
        g_assert(qdict);
        g_assert_cmpint(qdict_size(qdict), ==, 3);
        g_assert_cmpint(qdict_get_int(qdict, "integer"), ==, value_int + i);
        g_assert_cmpint(qdict_get_bool(qdict, "boolean"), ==, value_bool);
        g_assert_cmpstr(qdict_get_str(qdict, "string"), ==, value_str);
        i++;
    }
    g_assert_cmpint(i, ==, max_items);

    qapi_free_TestStructList(head);
}

static void test_visitor_out_list_qapi_free(TestOutputVisitorData *data,
                                            const void *unused)
{
    UserDefTwo *value;
    UserDefTwoList *head = NULL;
    const char string[] = "foo bar";
    int i, max_count = 1024;

    for (i = 0; i < max_count; i++) {
        value = g_malloc0(sizeof(*value));

        value->string0 = g_strdup(string);
        value->dict1 = g_new0(UserDefTwoDict, 1);
        value->dict1->string1 = g_strdup(string);
        value->dict1->dict2 = g_new0(UserDefTwoDictDict, 1);
        value->dict1->dict2->userdef = g_new0(UserDefOne, 1);
        value->dict1->dict2->userdef->string = g_strdup(string);
        value->dict1->dict2->userdef->integer = 42;
        value->dict1->dict2->string = g_strdup(string);
        value->dict1->has_dict3 = false;

        QAPI_LIST_PREPEND(head, value);
    }

    qapi_free_UserDefTwoList(head);
}

static void test_visitor_out_any(TestOutputVisitorData *data,
                                 const void *unused)
{
    QObject *qobj;
    QNum *qnum;
    QBool *qbool;
    QString *qstring;
    QDict *qdict;
    int64_t val;

    qobj = QOBJECT(qnum_from_int(-42));
    visit_type_any(data->ov, NULL, &qobj, &error_abort);
    qnum = qobject_to(QNum, visitor_get(data));
    g_assert(qnum);
    g_assert(qnum_get_try_int(qnum, &val));
    g_assert_cmpint(val, ==, -42);
    qobject_unref(qobj);

    visitor_reset(data);
    qdict = qdict_new();
    qdict_put_int(qdict, "integer", -42);
    qdict_put_bool(qdict, "boolean", true);
    qdict_put_str(qdict, "string", "foo");
    qobj = QOBJECT(qdict);
    visit_type_any(data->ov, NULL, &qobj, &error_abort);
    qobject_unref(qobj);
    qdict = qobject_to(QDict, visitor_get(data));
    g_assert(qdict);
    qnum = qobject_to(QNum, qdict_get(qdict, "integer"));
    g_assert(qnum);
    g_assert(qnum_get_try_int(qnum, &val));
    g_assert_cmpint(val, ==, -42);
    qbool = qobject_to(QBool, qdict_get(qdict, "boolean"));
    g_assert(qbool);
    g_assert(qbool_get_bool(qbool) == true);
    qstring = qobject_to(QString, qdict_get(qdict, "string"));
    g_assert(qstring);
    g_assert_cmpstr(qstring_get_str(qstring), ==, "foo");
}

static void test_visitor_out_union_flat(TestOutputVisitorData *data,
                                        const void *unused)
{
    QDict *qdict;

    UserDefFlatUnion *tmp = g_malloc0(sizeof(UserDefFlatUnion));
    tmp->enum1 = ENUM_ONE_VALUE1;
    tmp->string = g_strdup("str");
    tmp->integer = 41;
    tmp->u.value1.boolean = true;

    visit_type_UserDefFlatUnion(data->ov, NULL, &tmp, &error_abort);
    qdict = qobject_to(QDict, visitor_get(data));
    g_assert(qdict);
    g_assert_cmpstr(qdict_get_str(qdict, "enum1"), ==, "value1");
    g_assert_cmpstr(qdict_get_str(qdict, "string"), ==, "str");
    g_assert_cmpint(qdict_get_int(qdict, "integer"), ==, 41);
    g_assert_cmpint(qdict_get_bool(qdict, "boolean"), ==, true);

    qapi_free_UserDefFlatUnion(tmp);
}

static void test_visitor_out_alternate(TestOutputVisitorData *data,
                                       const void *unused)
{
    UserDefAlternate *tmp;
    QNum *qnum;
    QString *qstr;
    QDict *qdict;
    int64_t val;

    tmp = g_new0(UserDefAlternate, 1);
    tmp->type = QTYPE_QNUM;
    tmp->u.i = 42;

    visit_type_UserDefAlternate(data->ov, NULL, &tmp, &error_abort);
    qnum = qobject_to(QNum, visitor_get(data));
    g_assert(qnum);
    g_assert(qnum_get_try_int(qnum, &val));
    g_assert_cmpint(val, ==, 42);

    qapi_free_UserDefAlternate(tmp);

    visitor_reset(data);
    tmp = g_new0(UserDefAlternate, 1);
    tmp->type = QTYPE_QSTRING;
    tmp->u.e = ENUM_ONE_VALUE1;

    visit_type_UserDefAlternate(data->ov, NULL, &tmp, &error_abort);
    qstr = qobject_to(QString, visitor_get(data));
    g_assert(qstr);
    g_assert_cmpstr(qstring_get_str(qstr), ==, "value1");

    qapi_free_UserDefAlternate(tmp);

    visitor_reset(data);
    tmp = g_new0(UserDefAlternate, 1);
    tmp->type = QTYPE_QNULL;
    tmp->u.n = qnull();

    visit_type_UserDefAlternate(data->ov, NULL, &tmp, &error_abort);
    g_assert_cmpint(qobject_type(visitor_get(data)), ==, QTYPE_QNULL);

    qapi_free_UserDefAlternate(tmp);

    visitor_reset(data);
    tmp = g_new0(UserDefAlternate, 1);
    tmp->type = QTYPE_QDICT;
    tmp->u.udfu.integer = 1;
    tmp->u.udfu.string = g_strdup("str");
    tmp->u.udfu.enum1 = ENUM_ONE_VALUE1;
    tmp->u.udfu.u.value1.boolean = true;

    visit_type_UserDefAlternate(data->ov, NULL, &tmp, &error_abort);
    qdict = qobject_to(QDict, visitor_get(data));
    g_assert(qdict);
    g_assert_cmpint(qdict_size(qdict), ==, 4);
    g_assert_cmpint(qdict_get_int(qdict, "integer"), ==, 1);
    g_assert_cmpstr(qdict_get_str(qdict, "string"), ==, "str");
    g_assert_cmpstr(qdict_get_str(qdict, "enum1"), ==, "value1");
    g_assert_cmpint(qdict_get_bool(qdict, "boolean"), ==, true);

    qapi_free_UserDefAlternate(tmp);
}

static void test_visitor_out_null(TestOutputVisitorData *data,
                                  const void *unused)
{
    QNull *null = NULL;
    QDict *qdict;
    QObject *nil;

    visit_start_struct(data->ov, NULL, NULL, 0, &error_abort);
    visit_type_null(data->ov, "a", &null, &error_abort);
    visit_check_struct(data->ov, &error_abort);
    visit_end_struct(data->ov, NULL);
    qdict = qobject_to(QDict, visitor_get(data));
    g_assert(qdict);
    g_assert_cmpint(qdict_size(qdict), ==, 1);
    nil = qdict_get(qdict, "a");
    g_assert(nil);
    g_assert(qobject_type(nil) == QTYPE_QNULL);
}

static void test_visitor_out_list_struct(TestOutputVisitorData *data,
                                         const void *unused)
{
    const char *int_member[] = {
        "integer", "s8", "s16", "s32", "s64", "u8", "u16", "u32", "u64" };
    g_autoptr(ArrayStruct) arrs = g_new0(ArrayStruct, 1);
    int i, j;
    QDict *qdict;
    QList *qlist;
    QListEntry *e;

    for (i = 31; i >= 0; i--) {
        QAPI_LIST_PREPEND(arrs->integer, i);
    }

    for (i = 31; i >= 0; i--) {
        QAPI_LIST_PREPEND(arrs->s8, i);
    }

    for (i = 31; i >= 0; i--) {
        QAPI_LIST_PREPEND(arrs->s16, i);
    }

    for (i = 31; i >= 0; i--) {
        QAPI_LIST_PREPEND(arrs->s32, i);
    }

    for (i = 31; i >= 0; i--) {
        QAPI_LIST_PREPEND(arrs->s64, i);
    }

    for (i = 31; i >= 0; i--) {
        QAPI_LIST_PREPEND(arrs->u8, i);
    }

    for (i = 31; i >= 0; i--) {
        QAPI_LIST_PREPEND(arrs->u16, i);
    }

    for (i = 31; i >= 0; i--) {
        QAPI_LIST_PREPEND(arrs->u32, i);
    }

    for (i = 31; i >= 0; i--) {
        QAPI_LIST_PREPEND(arrs->u64, i);
    }

    for (i = 31; i >= 0; i--) {
        QAPI_LIST_PREPEND(arrs->number, (double)i / 3);
    }

    for (i = 31; i >= 0; i--) {
        QAPI_LIST_PREPEND(arrs->boolean, QEMU_IS_ALIGNED(i, 3));
    }

    for (i = 31; i >= 0; i--) {
        QAPI_LIST_PREPEND(arrs->string, g_strdup_printf("%d", i));
    }

    visit_type_ArrayStruct(data->ov, NULL, &arrs, &error_abort);

    qdict = qobject_to(QDict, visitor_get(data));
    g_assert(qdict);

    for (i = 0; i < G_N_ELEMENTS(int_member); i++) {
        qlist = qdict_get_qlist(qdict, int_member[i]);
        g_assert(qlist);
        j = 0;
        QLIST_FOREACH_ENTRY(qlist, e) {
            QNum *qvalue = qobject_to(QNum, qlist_entry_obj(e));
            g_assert(qvalue);
            g_assert_cmpint(qnum_get_int(qvalue), ==, j);
            j++;
        }
    }

    qlist = qdict_get_qlist(qdict, "number");
    g_assert(qlist);
    i = 0;
    QLIST_FOREACH_ENTRY(qlist, e) {
        QNum *qvalue = qobject_to(QNum, qlist_entry_obj(e));
        char expected[32], actual[32];

        g_assert(qvalue);
        sprintf(expected, "%.6f", (double)i / 3);
        sprintf(actual, "%.6f", qnum_get_double(qvalue));
        g_assert_cmpstr(actual, ==, expected);
        i++;
    }

    qlist = qdict_get_qlist(qdict, "boolean");
    g_assert(qlist);
    i = 0;
    QLIST_FOREACH_ENTRY(qlist, e) {
        QBool *qvalue = qobject_to(QBool, qlist_entry_obj(e));
        g_assert(qvalue);
        g_assert_cmpint(qbool_get_bool(qvalue), ==, i % 3 == 0);
        i++;
    }

    qlist = qdict_get_qlist(qdict, "string");
    g_assert(qlist);
    i = 0;
    QLIST_FOREACH_ENTRY(qlist, e) {
        QString *qvalue = qobject_to(QString, qlist_entry_obj(e));
        char expected[32];

        g_assert(qvalue);
        sprintf(expected, "%d", i);
        g_assert_cmpstr(qstring_get_str(qvalue), ==, expected);
        i++;
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

    output_visitor_test_add("/visitor/output/int",
                            &out_visitor_data, test_visitor_out_int);
    output_visitor_test_add("/visitor/output/bool",
                            &out_visitor_data, test_visitor_out_bool);
    output_visitor_test_add("/visitor/output/number",
                            &out_visitor_data, test_visitor_out_number);
    output_visitor_test_add("/visitor/output/string",
                            &out_visitor_data, test_visitor_out_string);
    output_visitor_test_add("/visitor/output/no-string",
                            &out_visitor_data, test_visitor_out_no_string);
    output_visitor_test_add("/visitor/output/enum",
                            &out_visitor_data, test_visitor_out_enum);
    output_visitor_test_add("/visitor/output/struct",
                            &out_visitor_data, test_visitor_out_struct);
    output_visitor_test_add("/visitor/output/struct-nested",
                            &out_visitor_data, test_visitor_out_struct_nested);
    output_visitor_test_add("/visitor/output/list",
                            &out_visitor_data, test_visitor_out_list);
    output_visitor_test_add("/visitor/output/any",
                            &out_visitor_data, test_visitor_out_any);
    output_visitor_test_add("/visitor/output/list-qapi-free",
                            &out_visitor_data, test_visitor_out_list_qapi_free);
    output_visitor_test_add("/visitor/output/union-flat",
                            &out_visitor_data, test_visitor_out_union_flat);
    output_visitor_test_add("/visitor/output/alternate",
                            &out_visitor_data, test_visitor_out_alternate);
    output_visitor_test_add("/visitor/output/null",
                            &out_visitor_data, test_visitor_out_null);
    output_visitor_test_add("/visitor/output/list_struct",
                            &out_visitor_data, test_visitor_out_list_struct);

    g_test_run();

    return 0;
}
