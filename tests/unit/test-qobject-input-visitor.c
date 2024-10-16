/*
 * QObject Input Visitor unit-tests.
 *
 * Copyright (C) 2011-2016 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *  Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qapi/qapi-visit-introspect.h"
#include "qapi/qobject-input-visitor.h"
#include "test-qapi-visit.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qnull.h"
#include "qapi/qmp/qnum.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qmp/qjson.h"
#include "test-qapi-introspect.h"
#include "qapi/qapi-introspect.h"

typedef struct TestInputVisitorData {
    QObject *obj;
    Visitor *qiv;
} TestInputVisitorData;

static void visitor_input_teardown(TestInputVisitorData *data,
                                   const void *unused)
{
    qobject_unref(data->obj);
    data->obj = NULL;

    if (data->qiv) {
        visit_free(data->qiv);
        data->qiv = NULL;
    }
}

/* The various test_init functions are provided instead of a test setup
   function so that the JSON string used by the tests are kept in the test
   functions (and not in main()). */

static Visitor *test_init_internal(TestInputVisitorData *data, bool keyval,
                                   QObject *obj)
{
    visitor_input_teardown(data, NULL);

    data->obj = obj;

    if (keyval) {
        data->qiv = qobject_input_visitor_new_keyval(data->obj);
    } else {
        data->qiv = qobject_input_visitor_new(data->obj);
    }
    g_assert(data->qiv);
    return data->qiv;
}

static G_GNUC_PRINTF(3, 4)
Visitor *visitor_input_test_init_full(TestInputVisitorData *data,
                                      bool keyval,
                                      const char *json_string, ...)
{
    Visitor *v;
    va_list ap;

    va_start(ap, json_string);
    v = test_init_internal(data, keyval,
                           qobject_from_vjsonf_nofail(json_string, ap));
    va_end(ap);
    return v;
}

static G_GNUC_PRINTF(2, 3)
Visitor *visitor_input_test_init(TestInputVisitorData *data,
                                 const char *json_string, ...)
{
    Visitor *v;
    va_list ap;

    va_start(ap, json_string);
    v = test_init_internal(data, false,
                           qobject_from_vjsonf_nofail(json_string, ap));
    va_end(ap);
    return v;
}

/* similar to visitor_input_test_init(), but does not expect a string
 * literal/format json_string argument and so can be used for
 * programmatically generated strings (and we can't pass in programmatically
 * generated strings via %s format parameters since qobject_from_jsonv()
 * will wrap those in double-quotes and treat the entire object as a
 * string)
 */
static Visitor *visitor_input_test_init_raw(TestInputVisitorData *data,
                                            const char *json_string)
{
    return test_init_internal(data, false,
                              qobject_from_json(json_string, &error_abort));
}

static void test_visitor_in_int(TestInputVisitorData *data,
                                const void *unused)
{
    int64_t res = 0;
    double dbl;
    int value = -42;
    Visitor *v;

    v = visitor_input_test_init(data, "%d", value);

    visit_type_int(v, NULL, &res, &error_abort);
    g_assert_cmpint(res, ==, value);

    visit_type_number(v, NULL, &dbl, &error_abort);
    g_assert_cmpfloat(dbl, ==, -42.0);
}

static void test_visitor_in_uint(TestInputVisitorData *data,
                                const void *unused)
{
    uint64_t res = 0;
    int64_t i64;
    double dbl;
    int value = 42;
    Visitor *v;

    v = visitor_input_test_init(data, "%d", value);

    visit_type_uint64(v, NULL, &res, &error_abort);
    g_assert_cmpuint(res, ==, (uint64_t)value);

    visit_type_int(v, NULL, &i64, &error_abort);
    g_assert_cmpint(i64, ==, value);

    visit_type_number(v, NULL, &dbl, &error_abort);
    g_assert_cmpfloat(dbl, ==, value);

    /* BUG: value between INT64_MIN and -1 accepted modulo 2^64 */
    v = visitor_input_test_init(data, "%d", -value);

    visit_type_uint64(v, NULL, &res, &error_abort);
    g_assert_cmpuint(res, ==, (uint64_t)-value);

    v = visitor_input_test_init(data, "18446744073709551574");

    visit_type_uint64(v, NULL, &res, &error_abort);
    g_assert_cmpuint(res, ==, 18446744073709551574U);

    visit_type_number(v, NULL, &dbl, &error_abort);
    g_assert_cmpfloat(dbl, ==, 18446744073709552000.0);
}

static void test_visitor_in_int_overflow(TestInputVisitorData *data,
                                         const void *unused)
{
    int64_t res = 0;
    Error *err = NULL;
    Visitor *v;

    /*
     * This will overflow a QNUM_I64, so should be deserialized into a
     * QNUM_DOUBLE field instead, leading to an error if we pass it to
     * visit_type_int().  Confirm this.
     */
    v = visitor_input_test_init(data, "%f", DBL_MAX);

    visit_type_int(v, NULL, &res, &err);
    error_free_or_abort(&err);
}

static void test_visitor_in_int_keyval(TestInputVisitorData *data,
                                       const void *unused)
{
    int64_t res = 0, value = -42;
    Error *err = NULL;
    Visitor *v;

    v = visitor_input_test_init_full(data, true, "%" PRId64, value);
    visit_type_int(v, NULL, &res, &err);
    error_free_or_abort(&err);
}

static void test_visitor_in_int_str_keyval(TestInputVisitorData *data,
                                           const void *unused)
{
    int64_t res = 0, value = -42;
    Visitor *v;

    v = visitor_input_test_init_full(data, true, "\"-42\"");

    visit_type_int(v, NULL, &res, &error_abort);
    g_assert_cmpint(res, ==, value);
}

static void test_visitor_in_int_str_fail(TestInputVisitorData *data,
                                         const void *unused)
{
    int64_t res = 0;
    Visitor *v;
    Error *err = NULL;

    v = visitor_input_test_init(data, "\"-42\"");

    visit_type_int(v, NULL, &res, &err);
    error_free_or_abort(&err);
}

static void test_visitor_in_bool(TestInputVisitorData *data,
                                 const void *unused)
{
    bool res = false;
    Visitor *v;

    v = visitor_input_test_init(data, "true");

    visit_type_bool(v, NULL, &res, &error_abort);
    g_assert_cmpint(res, ==, true);
}

static void test_visitor_in_bool_keyval(TestInputVisitorData *data,
                                        const void *unused)
{
    bool res = false;
    Error *err = NULL;
    Visitor *v;

    v = visitor_input_test_init_full(data, true, "true");

    visit_type_bool(v, NULL, &res, &err);
    error_free_or_abort(&err);
}

static void test_visitor_in_bool_str_keyval(TestInputVisitorData *data,
                                            const void *unused)
{
    bool res = false;
    Visitor *v;

    v = visitor_input_test_init_full(data, true, "\"on\"");

    visit_type_bool(v, NULL, &res, &error_abort);
    g_assert_cmpint(res, ==, true);
}

static void test_visitor_in_bool_str_fail(TestInputVisitorData *data,
                                          const void *unused)
{
    bool res = false;
    Visitor *v;
    Error *err = NULL;

    v = visitor_input_test_init(data, "\"true\"");

    visit_type_bool(v, NULL, &res, &err);
    error_free_or_abort(&err);
}

static void test_visitor_in_number(TestInputVisitorData *data,
                                   const void *unused)
{
    double res = 0, value = 3.14;
    Visitor *v;

    v = visitor_input_test_init(data, "%f", value);

    visit_type_number(v, NULL, &res, &error_abort);
    g_assert_cmpfloat(res, ==, value);
}

static void test_visitor_in_large_number(TestInputVisitorData *data,
                                         const void *unused)
{
    Error *err = NULL;
    double res = 0;
    int64_t i64;
    uint64_t u64;
    Visitor *v;

    v = visitor_input_test_init(data, "-18446744073709551616"); /* -2^64 */

    visit_type_number(v, NULL, &res, &error_abort);
    g_assert_cmpfloat(res, ==, -18446744073709552e3);

    visit_type_int(v, NULL, &i64, &err);
    error_free_or_abort(&err);

    visit_type_uint64(v, NULL, &u64, &err);
    error_free_or_abort(&err);
}

static void test_visitor_in_number_keyval(TestInputVisitorData *data,
                                          const void *unused)
{
    double res = 0, value = 3.14;
    Error *err = NULL;
    Visitor *v;

    v = visitor_input_test_init_full(data, true, "%f", value);

    visit_type_number(v, NULL, &res, &err);
    error_free_or_abort(&err);
}

static void test_visitor_in_number_str_keyval(TestInputVisitorData *data,
                                              const void *unused)
{
    double res = 0, value = 3.14;
    Visitor *v;
    Error *err = NULL;

    v = visitor_input_test_init_full(data, true, "\"3.14\"");

    visit_type_number(v, NULL, &res, &error_abort);
    g_assert_cmpfloat(res, ==, value);

    v = visitor_input_test_init_full(data, true, "\"inf\"");

    visit_type_number(v, NULL, &res, &err);
    error_free_or_abort(&err);
}

static void test_visitor_in_number_str_fail(TestInputVisitorData *data,
                                            const void *unused)
{
    double res = 0;
    Visitor *v;
    Error *err = NULL;

    v = visitor_input_test_init(data, "\"3.14\"");

    visit_type_number(v, NULL, &res, &err);
    error_free_or_abort(&err);
}

static void test_visitor_in_size_str_keyval(TestInputVisitorData *data,
                                            const void *unused)
{
    uint64_t res, value = 500 * 1024 * 1024;
    Visitor *v;

    v = visitor_input_test_init_full(data, true, "\"500M\"");

    visit_type_size(v, NULL, &res, &error_abort);
    g_assert_cmpfloat(res, ==, value);
}

static void test_visitor_in_size_str_fail(TestInputVisitorData *data,
                                          const void *unused)
{
    uint64_t res = 0;
    Visitor *v;
    Error *err = NULL;

    v = visitor_input_test_init(data, "\"500M\"");

    visit_type_size(v, NULL, &res, &err);
    error_free_or_abort(&err);
}

static void test_visitor_in_string(TestInputVisitorData *data,
                                   const void *unused)
{
    char *res = NULL, *value = (char *) "Q E M U";
    Visitor *v;

    v = visitor_input_test_init(data, "%s", value);

    visit_type_str(v, NULL, &res, &error_abort);
    g_assert_cmpstr(res, ==, value);

    g_free(res);
}

static void test_visitor_in_enum(TestInputVisitorData *data,
                                 const void *unused)
{
    Visitor *v;
    EnumOne i;

    for (i = 0; i < ENUM_ONE__MAX; i++) {
        EnumOne res = -1;

        v = visitor_input_test_init(data, "%s", EnumOne_str(i));

        visit_type_EnumOne(v, NULL, &res, &error_abort);
        g_assert_cmpint(i, ==, res);
    }
}


static void test_visitor_in_struct(TestInputVisitorData *data,
                                   const void *unused)
{
    TestStruct *p = NULL;
    Visitor *v;

    v = visitor_input_test_init(data, "{ 'integer': -42, 'boolean': true, 'string': 'foo' }");

    visit_type_TestStruct(v, NULL, &p, &error_abort);
    g_assert_cmpint(p->integer, ==, -42);
    g_assert(p->boolean == true);
    g_assert_cmpstr(p->string, ==, "foo");

    g_free(p->string);
    g_free(p);
}

static void test_visitor_in_struct_nested(TestInputVisitorData *data,
                                          const void *unused)
{
    g_autoptr(UserDefTwo) udp = NULL;
    Visitor *v;

    v = visitor_input_test_init(data, "{ 'string0': 'string0', "
                                "'dict1': { 'string1': 'string1', "
                                "'dict2': { 'userdef': { 'integer': 42, "
                                "'string': 'string' }, 'string': 'string2'}}}");

    visit_type_UserDefTwo(v, NULL, &udp, &error_abort);

    g_assert_cmpstr(udp->string0, ==, "string0");
    g_assert_cmpstr(udp->dict1->string1, ==, "string1");
    g_assert_cmpint(udp->dict1->dict2->userdef->integer, ==, 42);
    g_assert_cmpstr(udp->dict1->dict2->userdef->string, ==, "string");
    g_assert_cmpstr(udp->dict1->dict2->string, ==, "string2");
    g_assert(!udp->dict1->dict3);
}

static void test_visitor_in_list(TestInputVisitorData *data,
                                 const void *unused)
{
    UserDefOneList *item, *head = NULL;
    Visitor *v;
    int i;

    v = visitor_input_test_init(data, "[ { 'string': 'string0', 'integer': 42 }, { 'string': 'string1', 'integer': 43 }, { 'string': 'string2', 'integer': 44 } ]");

    visit_type_UserDefOneList(v, NULL, &head, &error_abort);
    g_assert(head != NULL);

    for (i = 0, item = head; item; item = item->next, i++) {
        g_autofree char *string = g_strdup_printf("string%d", i);

        g_assert_cmpstr(item->value->string, ==, string);
        g_assert_cmpint(item->value->integer, ==, 42 + i);
    }

    qapi_free_UserDefOneList(head);
    head = NULL;

    /* An empty list is valid */
    v = visitor_input_test_init(data, "[]");
    visit_type_UserDefOneList(v, NULL, &head, &error_abort);
    g_assert(!head);
}

static void test_visitor_in_list_struct(TestInputVisitorData *data,
                                        const void *unused)
{
    const char *int_member[] = {
        "integer", "s8", "s16", "s32", "s64", "u8", "u16", "u32", "u64" };
    g_autoptr(GString) json = g_string_new("");
    int i, j;
    const char *sep;
    g_autoptr(ArrayStruct) arrs = NULL;
    Visitor *v;
    intList *int_list;
    int8List *s8_list;
    int16List *s16_list;
    int32List *s32_list;
    int64List *s64_list;
    uint8List *u8_list;
    uint16List *u16_list;
    uint32List *u32_list;
    uint64List *u64_list;
    numberList *num_list;
    boolList *bool_list;
    strList *str_list;

    g_string_append_printf(json, "{");

    for (i = 0; i < G_N_ELEMENTS(int_member); i++) {
        g_string_append_printf(json, "'%s': [", int_member[i]);
        sep = "";
        for (j = 0; j < 32; j++) {
            g_string_append_printf(json, "%s%d", sep, j);
            sep = ", ";
        }
        g_string_append_printf(json, "], ");
    }

    g_string_append_printf(json, "'number': [");
    sep = "";
    for (i = 0; i < 32; i++) {
        g_string_append_printf(json, "%s%f", sep, (double)i / 3);
        sep = ", ";
    }
    g_string_append_printf(json, "], ");

    g_string_append_printf(json, "'boolean': [");
    sep = "";
    for (i = 0; i < 32; i++) {
        g_string_append_printf(json, "%s%s",
                               sep, i % 3 == 0 ? "true" : "false");
        sep = ", ";
    }
    g_string_append_printf(json, "], ");

    g_string_append_printf(json, "'string': [");
    sep = "";
    for (i = 0; i < 32; i++) {
        g_string_append_printf(json, "%s'%d'", sep, i);
        sep = ", ";
    }
    g_string_append_printf(json, "]");

    g_string_append_printf(json, "}");

    v = visitor_input_test_init_raw(data, json->str);
    visit_type_ArrayStruct(v, NULL, &arrs, &error_abort);

    i = 0;
    for (int_list = arrs->integer; int_list; int_list = int_list->next) {
        g_assert_cmpint(int_list->value, ==, i);
        i++;
    }

    i = 0;
    for (s8_list = arrs->s8; s8_list; s8_list = s8_list->next) {
        g_assert_cmpint(s8_list->value, ==, i);
        i++;
    }

    i = 0;
    for (s16_list = arrs->s16; s16_list; s16_list = s16_list->next) {
        g_assert_cmpint(s16_list->value, ==, i);
        i++;
    }

    i = 0;
    for (s32_list = arrs->s32; s32_list; s32_list = s32_list->next) {
        g_assert_cmpint(s32_list->value, ==, i);
        i++;
    }

    i = 0;
    for (s64_list = arrs->s64; s64_list; s64_list = s64_list->next) {
        g_assert_cmpint(s64_list->value, ==, i);
        i++;
    }

    i = 0;
    for (u8_list = arrs->u8; u8_list; u8_list = u8_list->next) {
        g_assert_cmpint(u8_list->value, ==, i);
        i++;
    }

    i = 0;
    for (u16_list = arrs->u16; u16_list; u16_list = u16_list->next) {
        g_assert_cmpint(u16_list->value, ==, i);
        i++;
    }

    i = 0;
    for (u32_list = arrs->u32; u32_list; u32_list = u32_list->next) {
        g_assert_cmpint(u32_list->value, ==, i);
        i++;
    }

    i = 0;
    for (u64_list = arrs->u64; u64_list; u64_list = u64_list->next) {
        g_assert_cmpint(u64_list->value, ==, i);
        i++;
    }

    i = 0;
    for (num_list = arrs->number; num_list; num_list = num_list->next) {
        char expected[32], actual[32];

        sprintf(expected, "%.6f", (double)i / 3);
        sprintf(actual, "%.6f", num_list->value);
        g_assert_cmpstr(expected, ==, actual);
        i++;
    }

    i = 0;
    for (bool_list = arrs->boolean; bool_list; bool_list = bool_list->next) {
        g_assert_cmpint(bool_list->value, ==, i % 3 == 0);
        i++;
    }

    i = 0;
    for (str_list = arrs->string; str_list; str_list = str_list->next) {
        char expected[32];

        sprintf(expected, "%d", i);
        g_assert_cmpstr(str_list->value, ==, expected);
        i++;
    }
}

static void test_visitor_in_any(TestInputVisitorData *data,
                                const void *unused)
{
    QObject *res = NULL;
    Visitor *v;
    QNum *qnum;
    QBool *qbool;
    QString *qstring;
    QDict *qdict;
    QObject *qobj;
    int64_t val;

    v = visitor_input_test_init(data, "-42");
    visit_type_any(v, NULL, &res, &error_abort);
    qnum = qobject_to(QNum, res);
    g_assert(qnum);
    g_assert(qnum_get_try_int(qnum, &val));
    g_assert_cmpint(val, ==, -42);
    qobject_unref(res);

    v = visitor_input_test_init(data, "{ 'integer': -42, 'boolean': true, 'string': 'foo' }");
    visit_type_any(v, NULL, &res, &error_abort);
    qdict = qobject_to(QDict, res);
    g_assert(qdict && qdict_size(qdict) == 3);
    qobj = qdict_get(qdict, "integer");
    g_assert(qobj);
    qnum = qobject_to(QNum, qobj);
    g_assert(qnum);
    g_assert(qnum_get_try_int(qnum, &val));
    g_assert_cmpint(val, ==, -42);
    qobj = qdict_get(qdict, "boolean");
    g_assert(qobj);
    qbool = qobject_to(QBool, qobj);
    g_assert(qbool);
    g_assert(qbool_get_bool(qbool) == true);
    qobj = qdict_get(qdict, "string");
    g_assert(qobj);
    qstring = qobject_to(QString, qobj);
    g_assert(qstring);
    g_assert_cmpstr(qstring_get_str(qstring), ==, "foo");
    qobject_unref(res);
}

static void test_visitor_in_null(TestInputVisitorData *data,
                                 const void *unused)
{
    Visitor *v;
    Error *err = NULL;
    QNull *null;
    char *tmp;

    /*
     * FIXME: Since QAPI doesn't know the 'null' type yet, we can't
     * test visit_type_null() by reading into a QAPI struct then
     * checking that it was populated correctly.  The best we can do
     * for now is ensure that we consumed null from the input, proven
     * by the fact that we can't re-read the key; and that we detect
     * when input is not null.
     */

    v = visitor_input_test_init_full(data, false,
                                     "{ 'a': null, 'b': '' }");
    visit_start_struct(v, NULL, NULL, 0, &error_abort);
    visit_type_null(v, "a", &null, &error_abort);
    g_assert(qobject_type(QOBJECT(null)) == QTYPE_QNULL);
    qobject_unref(null);
    visit_type_null(v, "b", &null, &err);
    error_free_or_abort(&err);
    g_assert(!null);
    visit_type_str(v, "c", &tmp, &err);
    error_free_or_abort(&err);
    g_assert(!tmp);
    visit_check_struct(v, &error_abort);
    visit_end_struct(v, NULL);
}

static void test_visitor_in_union_flat(TestInputVisitorData *data,
                                       const void *unused)
{
    Visitor *v;
    g_autoptr(UserDefFlatUnion) tmp = NULL;
    UserDefUnionBase *base;

    v = visitor_input_test_init(data,
                                "{ 'enum1': 'value1', "
                                "'integer': 41, "
                                "'string': 'str', "
                                "'boolean': true }");

    visit_type_UserDefFlatUnion(v, NULL, &tmp, &error_abort);
    g_assert_cmpint(tmp->enum1, ==, ENUM_ONE_VALUE1);
    g_assert_cmpstr(tmp->string, ==, "str");
    g_assert_cmpint(tmp->integer, ==, 41);
    g_assert_cmpint(tmp->u.value1.boolean, ==, true);

    base = qapi_UserDefFlatUnion_base(tmp);
    g_assert(&base->enum1 == &tmp->enum1);
}

static void test_visitor_in_union_in_union(TestInputVisitorData *data,
                                           const void *unused)
{
    Visitor *v;
    g_autoptr(TestUnionInUnion) tmp = NULL;

    v = visitor_input_test_init(data,
                                "{ 'type': 'value-a', "
                                "  'type-a': 'value-a1', "
                                "  'integer': 2, "
                                "  'name': 'fish' }");

    visit_type_TestUnionInUnion(v, NULL, &tmp, &error_abort);
    g_assert_cmpint(tmp->type, ==, TEST_UNION_ENUM_VALUE_A);
    g_assert_cmpint(tmp->u.value_a.type_a, ==, TEST_UNION_ENUM_A_VALUE_A1);
    g_assert_cmpint(tmp->u.value_a.u.value_a1.integer, ==, 2);
    g_assert_cmpint(strcmp(tmp->u.value_a.u.value_a1.name, "fish"), ==, 0);

    qapi_free_TestUnionInUnion(tmp);

    v = visitor_input_test_init(data,
                                "{ 'type': 'value-a', "
                                "  'type-a': 'value-a2', "
                                "  'integer': 1729, "
                                "  'size': 87539319 }");

    visit_type_TestUnionInUnion(v, NULL, &tmp, &error_abort);
    g_assert_cmpint(tmp->type, ==, TEST_UNION_ENUM_VALUE_A);
    g_assert_cmpint(tmp->u.value_a.type_a, ==, TEST_UNION_ENUM_A_VALUE_A2);
    g_assert_cmpint(tmp->u.value_a.u.value_a2.integer, ==, 1729);
    g_assert_cmpint(tmp->u.value_a.u.value_a2.size, ==, 87539319);

    qapi_free_TestUnionInUnion(tmp);

    v = visitor_input_test_init(data,
                                "{ 'type': 'value-b', "
                                "  'integer': 1729, "
                                "  'onoff': true }");

    visit_type_TestUnionInUnion(v, NULL, &tmp, &error_abort);
    g_assert_cmpint(tmp->type, ==, TEST_UNION_ENUM_VALUE_B);
    g_assert_cmpint(tmp->u.value_b.integer, ==, 1729);
    g_assert_cmpint(tmp->u.value_b.onoff, ==, true);
}

static void test_visitor_in_alternate(TestInputVisitorData *data,
                                      const void *unused)
{
    Visitor *v;
    UserDefAlternate *tmp;
    WrapAlternate *wrap;

    v = visitor_input_test_init(data, "42");
    visit_type_UserDefAlternate(v, NULL, &tmp, &error_abort);
    g_assert_cmpint(tmp->type, ==, QTYPE_QNUM);
    g_assert_cmpint(tmp->u.i, ==, 42);
    qapi_free_UserDefAlternate(tmp);

    v = visitor_input_test_init(data, "'value1'");
    visit_type_UserDefAlternate(v, NULL, &tmp, &error_abort);
    g_assert_cmpint(tmp->type, ==, QTYPE_QSTRING);
    g_assert_cmpint(tmp->u.e, ==, ENUM_ONE_VALUE1);
    qapi_free_UserDefAlternate(tmp);

    v = visitor_input_test_init(data, "null");
    visit_type_UserDefAlternate(v, NULL, &tmp, &error_abort);
    g_assert_cmpint(tmp->type, ==, QTYPE_QNULL);
    qapi_free_UserDefAlternate(tmp);

    v = visitor_input_test_init(data, "{'integer':1, 'string':'str', "
                                "'enum1':'value1', 'boolean':true}");
    visit_type_UserDefAlternate(v, NULL, &tmp, &error_abort);
    g_assert_cmpint(tmp->type, ==, QTYPE_QDICT);
    g_assert_cmpint(tmp->u.udfu.integer, ==, 1);
    g_assert_cmpstr(tmp->u.udfu.string, ==, "str");
    g_assert_cmpint(tmp->u.udfu.enum1, ==, ENUM_ONE_VALUE1);
    g_assert_cmpint(tmp->u.udfu.u.value1.boolean, ==, true);
    g_assert_cmpint(tmp->u.udfu.u.value1.has_a_b, ==, false);
    qapi_free_UserDefAlternate(tmp);

    v = visitor_input_test_init(data, "{ 'alt': 42 }");
    visit_type_WrapAlternate(v, NULL, &wrap, &error_abort);
    g_assert_cmpint(wrap->alt->type, ==, QTYPE_QNUM);
    g_assert_cmpint(wrap->alt->u.i, ==, 42);
    qapi_free_WrapAlternate(wrap);

    v = visitor_input_test_init(data, "{ 'alt': 'value1' }");
    visit_type_WrapAlternate(v, NULL, &wrap, &error_abort);
    g_assert_cmpint(wrap->alt->type, ==, QTYPE_QSTRING);
    g_assert_cmpint(wrap->alt->u.e, ==, ENUM_ONE_VALUE1);
    qapi_free_WrapAlternate(wrap);

    v = visitor_input_test_init(data, "{ 'alt': {'integer':1, 'string':'str', "
                                "'enum1':'value1', 'boolean':true} }");
    visit_type_WrapAlternate(v, NULL, &wrap, &error_abort);
    g_assert_cmpint(wrap->alt->type, ==, QTYPE_QDICT);
    g_assert_cmpint(wrap->alt->u.udfu.integer, ==, 1);
    g_assert_cmpstr(wrap->alt->u.udfu.string, ==, "str");
    g_assert_cmpint(wrap->alt->u.udfu.enum1, ==, ENUM_ONE_VALUE1);
    g_assert_cmpint(wrap->alt->u.udfu.u.value1.boolean, ==, true);
    g_assert_cmpint(wrap->alt->u.udfu.u.value1.has_a_b, ==, false);
    qapi_free_WrapAlternate(wrap);
}

static void test_visitor_in_alternate_number(TestInputVisitorData *data,
                                             const void *unused)
{
    Visitor *v;
    Error *err = NULL;
    AltEnumBool *aeb;
    AltEnumNum *aen;
    AltNumEnum *ans;
    AltEnumInt *asi;
    AltListInt *ali;

    /* Parsing an int */

    v = visitor_input_test_init(data, "42");
    visit_type_AltEnumBool(v, NULL, &aeb, &err);
    error_free_or_abort(&err);
    qapi_free_AltEnumBool(aeb);

    v = visitor_input_test_init(data, "42");
    visit_type_AltEnumNum(v, NULL, &aen, &error_abort);
    g_assert_cmpint(aen->type, ==, QTYPE_QNUM);
    g_assert_cmpfloat(aen->u.n, ==, 42);
    qapi_free_AltEnumNum(aen);

    v = visitor_input_test_init(data, "42");
    visit_type_AltNumEnum(v, NULL, &ans, &error_abort);
    g_assert_cmpint(ans->type, ==, QTYPE_QNUM);
    g_assert_cmpfloat(ans->u.n, ==, 42);
    qapi_free_AltNumEnum(ans);

    v = visitor_input_test_init(data, "42");
    visit_type_AltEnumInt(v, NULL, &asi, &error_abort);
    g_assert_cmpint(asi->type, ==, QTYPE_QNUM);
    g_assert_cmpint(asi->u.i, ==, 42);
    qapi_free_AltEnumInt(asi);

    v = visitor_input_test_init(data, "42");
    visit_type_AltListInt(v, NULL, &ali, &error_abort);
    g_assert_cmpint(ali->type, ==, QTYPE_QNUM);
    g_assert_cmpint(ali->u.i, ==, 42);
    qapi_free_AltListInt(ali);

    /* Parsing a double */

    v = visitor_input_test_init(data, "42.5");
    visit_type_AltEnumBool(v, NULL, &aeb, &err);
    error_free_or_abort(&err);
    qapi_free_AltEnumBool(aeb);

    v = visitor_input_test_init(data, "42.5");
    visit_type_AltEnumNum(v, NULL, &aen, &error_abort);
    g_assert_cmpint(aen->type, ==, QTYPE_QNUM);
    g_assert_cmpfloat(aen->u.n, ==, 42.5);
    qapi_free_AltEnumNum(aen);

    v = visitor_input_test_init(data, "42.5");
    visit_type_AltNumEnum(v, NULL, &ans, &error_abort);
    g_assert_cmpint(ans->type, ==, QTYPE_QNUM);
    g_assert_cmpfloat(ans->u.n, ==, 42.5);
    qapi_free_AltNumEnum(ans);

    v = visitor_input_test_init(data, "42.5");
    visit_type_AltEnumInt(v, NULL, &asi, &err);
    error_free_or_abort(&err);
    qapi_free_AltEnumInt(asi);
}

static void test_visitor_in_alternate_list(TestInputVisitorData *data,
                                 const void *unused)
{
    intList *item;
    Visitor *v;
    AltListInt *ali;
    int i;

    v = visitor_input_test_init(data, "[ 42, 43, 44 ]");
    visit_type_AltListInt(v, NULL, &ali, &error_abort);
    g_assert(ali != NULL);

    g_assert_cmpint(ali->type, ==, QTYPE_QLIST);
    for (i = 0, item = ali->u.l; item; item = item->next, i++) {
        g_assert_cmpint(item->value, ==, 42 + i);
    }

    qapi_free_AltListInt(ali);
    ali = NULL;

    /* An empty list is valid */
    v = visitor_input_test_init(data, "[]");
    visit_type_AltListInt(v, NULL, &ali, &error_abort);
    g_assert(ali != NULL);

    g_assert_cmpint(ali->type, ==, QTYPE_QLIST);
    g_assert(!ali->u.l);
    qapi_free_AltListInt(ali);
    ali = NULL;
}

static void input_visitor_test_add(const char *testpath,
                                   const void *user_data,
                                   void (*test_func)(TestInputVisitorData *data,
                                                     const void *user_data))
{
    g_test_add(testpath, TestInputVisitorData, user_data, NULL, test_func,
               visitor_input_teardown);
}

static void test_visitor_in_errors(TestInputVisitorData *data,
                                   const void *unused)
{
    TestStruct *p = NULL;
    Error *err = NULL;
    Visitor *v;
    strList *q = NULL;
    UserDefTwo *r = NULL;
    WrapAlternate *s = NULL;

    v = visitor_input_test_init(data, "{ 'integer': false, 'boolean': 'foo', "
                                "'string': -42 }");

    visit_type_TestStruct(v, NULL, &p, &err);
    error_free_or_abort(&err);
    g_assert(!p);

    v = visitor_input_test_init(data, "[ '1', '2', false, '3' ]");
    visit_type_strList(v, NULL, &q, &err);
    error_free_or_abort(&err);
    assert(!q);

    v = visitor_input_test_init(data, "{ 'str':'hi' }");
    visit_type_UserDefTwo(v, NULL, &r, &err);
    error_free_or_abort(&err);
    assert(!r);

    v = visitor_input_test_init(data, "{ }");
    visit_type_WrapAlternate(v, NULL, &s, &err);
    error_free_or_abort(&err);
    assert(!s);
}

static void test_visitor_in_wrong_type(TestInputVisitorData *data,
                                       const void *unused)
{
    TestStruct *p = NULL;
    Visitor *v;
    strList *q = NULL;
    int64_t i;
    Error *err = NULL;

    /* Make sure arrays and structs cannot be confused */

    v = visitor_input_test_init(data, "[]");
    visit_type_TestStruct(v, NULL, &p, &err);
    error_free_or_abort(&err);
    g_assert(!p);

    v = visitor_input_test_init(data, "{}");
    visit_type_strList(v, NULL, &q, &err);
    error_free_or_abort(&err);
    assert(!q);

    /* Make sure primitives and struct cannot be confused */

    v = visitor_input_test_init(data, "1");
    visit_type_TestStruct(v, NULL, &p, &err);
    error_free_or_abort(&err);
    g_assert(!p);

    v = visitor_input_test_init(data, "{}");
    visit_type_int(v, NULL, &i, &err);
    error_free_or_abort(&err);

    /* Make sure primitives and arrays cannot be confused */

    v = visitor_input_test_init(data, "1");
    visit_type_strList(v, NULL, &q, &err);
    error_free_or_abort(&err);
    assert(!q);

    v = visitor_input_test_init(data, "[]");
    visit_type_int(v, NULL, &i, &err);
    error_free_or_abort(&err);
}

static void test_visitor_in_fail_struct(TestInputVisitorData *data,
                                        const void *unused)
{
    TestStruct *p = NULL;
    Error *err = NULL;
    Visitor *v;

    v = visitor_input_test_init(data, "{ 'integer': -42, 'boolean': true, 'string': 'foo', 'extra': 42 }");

    visit_type_TestStruct(v, NULL, &p, &err);
    error_free_or_abort(&err);
    g_assert(!p);
}

static void test_visitor_in_fail_struct_nested(TestInputVisitorData *data,
                                               const void *unused)
{
    UserDefTwo *udp = NULL;
    Error *err = NULL;
    Visitor *v;

    v = visitor_input_test_init(data, "{ 'string0': 'string0', 'dict1': { 'string1': 'string1', 'dict2': { 'userdef1': { 'integer': 42, 'string': 'string', 'extra': [42, 23, {'foo':'bar'}] }, 'string2': 'string2'}}}");

    visit_type_UserDefTwo(v, NULL, &udp, &err);
    error_free_or_abort(&err);
    g_assert(!udp);
}

static void test_visitor_in_fail_struct_in_list(TestInputVisitorData *data,
                                                const void *unused)
{
    UserDefOneList *head = NULL;
    Error *err = NULL;
    Visitor *v;

    v = visitor_input_test_init(data, "[ { 'string': 'string0', 'integer': 42 }, { 'string': 'string1', 'integer': 43 }, { 'string': 'string2', 'integer': 44, 'extra': 'ggg' } ]");

    visit_type_UserDefOneList(v, NULL, &head, &err);
    error_free_or_abort(&err);
    g_assert(!head);
}

static void test_visitor_in_fail_struct_missing(TestInputVisitorData *data,
                                                const void *unused)
{
    Error *err = NULL;
    Visitor *v;
    QObject *any;
    QNull *null;
    GenericAlternate *alt;
    bool present;
    int en;
    int64_t i64;
    uint32_t u32;
    int8_t i8;
    char *str;
    double dbl;

    v = visitor_input_test_init(data, "{ 'sub': [ {} ] }");
    visit_start_struct(v, NULL, NULL, 0, &error_abort);
    visit_start_struct(v, "struct", NULL, 0, &err);
    error_free_or_abort(&err);
    visit_start_list(v, "list", NULL, 0, &err);
    error_free_or_abort(&err);
    visit_start_alternate(v, "alternate", &alt, sizeof(*alt), &err);
    error_free_or_abort(&err);
    visit_optional(v, "optional", &present);
    g_assert(!present);
    visit_type_enum(v, "enum", &en, &EnumOne_lookup, &err);
    error_free_or_abort(&err);
    visit_type_int(v, "i64", &i64, &err);
    error_free_or_abort(&err);
    visit_type_uint32(v, "u32", &u32, &err);
    error_free_or_abort(&err);
    visit_type_int8(v, "i8", &i8, &err);
    error_free_or_abort(&err);
    visit_type_str(v, "i8", &str, &err);
    error_free_or_abort(&err);
    visit_type_number(v, "dbl", &dbl, &err);
    error_free_or_abort(&err);
    visit_type_any(v, "any", &any, &err);
    error_free_or_abort(&err);
    visit_type_null(v, "null", &null, &err);
    error_free_or_abort(&err);
    visit_start_list(v, "sub", NULL, 0, &error_abort);
    visit_start_struct(v, NULL, NULL, 0, &error_abort);
    visit_type_int(v, "i64", &i64, &err);
    error_free_or_abort(&err);
    visit_end_struct(v, NULL);
    visit_end_list(v, NULL);
    visit_end_struct(v, NULL);
}

static void test_visitor_in_fail_list(TestInputVisitorData *data,
                                      const void *unused)
{
    int64_t i64 = -1;
    Error *err = NULL;
    Visitor *v;

    /* Unvisited list tail */

    v = visitor_input_test_init(data, "[ 1, 2, 3 ]");

    visit_start_list(v, NULL, NULL, 0, &error_abort);
    visit_type_int(v, NULL, &i64, &error_abort);
    g_assert_cmpint(i64, ==, 1);
    visit_type_int(v, NULL, &i64, &error_abort);
    g_assert_cmpint(i64, ==, 2);
    visit_check_list(v, &err);
    error_free_or_abort(&err);
    visit_end_list(v, NULL);

    /* Visit beyond end of list */
    v = visitor_input_test_init(data, "[]");

    visit_start_list(v, NULL, NULL, 0, &error_abort);
    visit_type_int(v, NULL, &i64, &err);
    error_free_or_abort(&err);
    visit_end_list(v, NULL);
}

static void test_visitor_in_fail_list_nested(TestInputVisitorData *data,
                                             const void *unused)
{
    int64_t i64 = -1;
    Error *err = NULL;
    Visitor *v;

    /* Unvisited nested list tail */

    v = visitor_input_test_init(data, "[ 0, [ 1, 2, 3 ] ]");

    visit_start_list(v, NULL, NULL, 0, &error_abort);
    visit_type_int(v, NULL, &i64, &error_abort);
    g_assert_cmpint(i64, ==, 0);
    visit_start_list(v, NULL, NULL, 0, &error_abort);
    visit_type_int(v, NULL, &i64, &error_abort);
    g_assert_cmpint(i64, ==, 1);
    visit_check_list(v, &err);
    error_free_or_abort(&err);
    visit_end_list(v, NULL);
    visit_check_list(v, &error_abort);
    visit_end_list(v, NULL);
}

static void test_visitor_in_fail_union_flat(TestInputVisitorData *data,
                                            const void *unused)
{
    UserDefFlatUnion *tmp = NULL;
    Error *err = NULL;
    Visitor *v;

    v = visitor_input_test_init(data, "{ 'enum1': 'value2', 'string': 'c', 'integer': 41, 'boolean': true }");

    visit_type_UserDefFlatUnion(v, NULL, &tmp, &err);
    error_free_or_abort(&err);
    g_assert(!tmp);
}

static void test_visitor_in_fail_union_flat_no_discrim(TestInputVisitorData *data,
                                                       const void *unused)
{
    UserDefFlatUnion2 *tmp = NULL;
    Error *err = NULL;
    Visitor *v;

    /* test situation where discriminator field ('enum1' here) is missing */
    v = visitor_input_test_init(data, "{ 'integer': 42, 'string': 'c', 'string1': 'd', 'string2': 'e' }");

    visit_type_UserDefFlatUnion2(v, NULL, &tmp, &err);
    error_free_or_abort(&err);
    g_assert(!tmp);
}

static void test_visitor_in_fail_alternate(TestInputVisitorData *data,
                                           const void *unused)
{
    UserDefAlternate *tmp;
    Visitor *v;
    Error *err = NULL;

    v = visitor_input_test_init(data, "3.14");

    visit_type_UserDefAlternate(v, NULL, &tmp, &err);
    error_free_or_abort(&err);
    g_assert(!tmp);
}

static void do_test_visitor_in_qmp_introspect(TestInputVisitorData *data,
                                              const QLitObject *qlit)
{
    g_autoptr(SchemaInfoList) schema = NULL;
    QObject *obj = qobject_from_qlit(qlit);
    Visitor *v;

    v = qobject_input_visitor_new(obj);

    visit_type_SchemaInfoList(v, NULL, &schema, &error_abort);
    g_assert(schema);

    qobject_unref(obj);
    visit_free(v);
}

static void test_visitor_in_qmp_introspect(TestInputVisitorData *data,
                                           const void *unused)
{
    do_test_visitor_in_qmp_introspect(data, &test_qmp_schema_qlit);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    input_visitor_test_add("/visitor/input/int",
                           NULL, test_visitor_in_int);
    input_visitor_test_add("/visitor/input/uint",
                           NULL, test_visitor_in_uint);
    input_visitor_test_add("/visitor/input/int_overflow",
                           NULL, test_visitor_in_int_overflow);
    input_visitor_test_add("/visitor/input/int_keyval",
                           NULL, test_visitor_in_int_keyval);
    input_visitor_test_add("/visitor/input/int_str_keyval",
                           NULL, test_visitor_in_int_str_keyval);
    input_visitor_test_add("/visitor/input/int_str_fail",
                           NULL, test_visitor_in_int_str_fail);
    input_visitor_test_add("/visitor/input/bool",
                           NULL, test_visitor_in_bool);
    input_visitor_test_add("/visitor/input/bool_keyval",
                           NULL, test_visitor_in_bool_keyval);
    input_visitor_test_add("/visitor/input/bool_str_keyval",
                           NULL, test_visitor_in_bool_str_keyval);
    input_visitor_test_add("/visitor/input/bool_str_fail",
                           NULL, test_visitor_in_bool_str_fail);
    input_visitor_test_add("/visitor/input/number",
                           NULL, test_visitor_in_number);
    input_visitor_test_add("/visitor/input/large_number",
                           NULL, test_visitor_in_large_number);
    input_visitor_test_add("/visitor/input/number_keyval",
                           NULL, test_visitor_in_number_keyval);
    input_visitor_test_add("/visitor/input/number_str_keyval",
                           NULL, test_visitor_in_number_str_keyval);
    input_visitor_test_add("/visitor/input/number_str_fail",
                           NULL, test_visitor_in_number_str_fail);
    input_visitor_test_add("/visitor/input/size_str_keyval",
                           NULL, test_visitor_in_size_str_keyval);
    input_visitor_test_add("/visitor/input/size_str_fail",
                           NULL, test_visitor_in_size_str_fail);
    input_visitor_test_add("/visitor/input/string",
                           NULL, test_visitor_in_string);
    input_visitor_test_add("/visitor/input/enum",
                           NULL, test_visitor_in_enum);
    input_visitor_test_add("/visitor/input/struct",
                           NULL, test_visitor_in_struct);
    input_visitor_test_add("/visitor/input/struct-nested",
                           NULL, test_visitor_in_struct_nested);
    input_visitor_test_add("/visitor/input/list2",
                           NULL, test_visitor_in_list_struct);
    input_visitor_test_add("/visitor/input/list",
                           NULL, test_visitor_in_list);
    input_visitor_test_add("/visitor/input/any",
                           NULL, test_visitor_in_any);
    input_visitor_test_add("/visitor/input/null",
                           NULL, test_visitor_in_null);
    input_visitor_test_add("/visitor/input/union-flat",
                           NULL, test_visitor_in_union_flat);
    input_visitor_test_add("/visitor/input/union-in-union",
                           NULL, test_visitor_in_union_in_union);
    input_visitor_test_add("/visitor/input/alternate",
                           NULL, test_visitor_in_alternate);
    input_visitor_test_add("/visitor/input/errors",
                           NULL, test_visitor_in_errors);
    input_visitor_test_add("/visitor/input/wrong-type",
                           NULL, test_visitor_in_wrong_type);
    input_visitor_test_add("/visitor/input/alternate-number",
                           NULL, test_visitor_in_alternate_number);
    input_visitor_test_add("/visitor/input/alternate-list",
                           NULL, test_visitor_in_alternate_list);
    input_visitor_test_add("/visitor/input/fail/struct",
                           NULL, test_visitor_in_fail_struct);
    input_visitor_test_add("/visitor/input/fail/struct-nested",
                           NULL, test_visitor_in_fail_struct_nested);
    input_visitor_test_add("/visitor/input/fail/struct-in-list",
                           NULL, test_visitor_in_fail_struct_in_list);
    input_visitor_test_add("/visitor/input/fail/struct-missing",
                           NULL, test_visitor_in_fail_struct_missing);
    input_visitor_test_add("/visitor/input/fail/list",
                           NULL, test_visitor_in_fail_list);
    input_visitor_test_add("/visitor/input/fail/list-nested",
                           NULL, test_visitor_in_fail_list_nested);
    input_visitor_test_add("/visitor/input/fail/union-flat",
                           NULL, test_visitor_in_fail_union_flat);
    input_visitor_test_add("/visitor/input/fail/union-flat-no-discriminator",
                           NULL, test_visitor_in_fail_union_flat_no_discrim);
    input_visitor_test_add("/visitor/input/fail/alternate",
                           NULL, test_visitor_in_fail_alternate);
    input_visitor_test_add("/visitor/input/qapi-introspect",
                           NULL, test_visitor_in_qmp_introspect);

    g_test_run();

    return 0;
}
