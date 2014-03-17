/*
 * QMP Input Visitor unit-tests.
 *
 * Copyright (C) 2011 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <glib.h>
#include <stdarg.h>

#include "qemu-common.h"
#include "qapi/qmp-input-visitor.h"
#include "test-qapi-types.h"
#include "test-qapi-visit.h"
#include "qapi/qmp/types.h"

typedef struct TestInputVisitorData {
    QObject *obj;
    QmpInputVisitor *qiv;
} TestInputVisitorData;

static void visitor_input_teardown(TestInputVisitorData *data,
                                   const void *unused)
{
    qobject_decref(data->obj);
    data->obj = NULL;

    if (data->qiv) {
        qmp_input_visitor_cleanup(data->qiv);
        data->qiv = NULL;
    }
}

/* This is provided instead of a test setup function so that the JSON
   string used by the tests are kept in the test functions (and not
   int main()) */
static GCC_FMT_ATTR(2, 3)
Visitor *visitor_input_test_init(TestInputVisitorData *data,
                                 const char *json_string, ...)
{
    Visitor *v;
    va_list ap;

    va_start(ap, json_string);
    data->obj = qobject_from_jsonv(json_string, &ap);
    va_end(ap);

    g_assert(data->obj != NULL);

    data->qiv = qmp_input_visitor_new(data->obj);
    g_assert(data->qiv != NULL);

    v = qmp_input_get_visitor(data->qiv);
    g_assert(v != NULL);

    return v;
}

/* similar to visitor_input_test_init(), but does not expect a string
 * literal/format json_string argument and so can be used for
 * programatically generated strings (and we can't pass in programatically
 * generated strings via %s format parameters since qobject_from_jsonv()
 * will wrap those in double-quotes and treat the entire object as a
 * string)
 */
static Visitor *visitor_input_test_init_raw(TestInputVisitorData *data,
                                            const char *json_string)
{
    Visitor *v;

    data->obj = qobject_from_json(json_string);

    g_assert(data->obj != NULL);

    data->qiv = qmp_input_visitor_new(data->obj);
    g_assert(data->qiv != NULL);

    v = qmp_input_get_visitor(data->qiv);
    g_assert(v != NULL);

    return v;
}

static void test_visitor_in_int(TestInputVisitorData *data,
                                const void *unused)
{
    int64_t res = 0, value = -42;
    Error *errp = NULL;
    Visitor *v;

    v = visitor_input_test_init(data, "%" PRId64, value);

    visit_type_int(v, &res, NULL, &errp);
    g_assert(!errp);
    g_assert_cmpint(res, ==, value);
}

static void test_visitor_in_int_overflow(TestInputVisitorData *data,
                                         const void *unused)
{
    int64_t res = 0;
    Error *errp = NULL;
    Visitor *v;

    /* this will overflow a Qint/int64, so should be deserialized into
     * a QFloat/double field instead, leading to an error if we pass it
     * to visit_type_int. confirm this.
     */
    v = visitor_input_test_init(data, "%f", DBL_MAX);

    visit_type_int(v, &res, NULL, &errp);
    g_assert(errp);
    error_free(errp);
}

static void test_visitor_in_bool(TestInputVisitorData *data,
                                 const void *unused)
{
    Error *errp = NULL;
    bool res = false;
    Visitor *v;

    v = visitor_input_test_init(data, "true");

    visit_type_bool(v, &res, NULL, &errp);
    g_assert(!errp);
    g_assert_cmpint(res, ==, true);
}

static void test_visitor_in_number(TestInputVisitorData *data,
                                   const void *unused)
{
    double res = 0, value = 3.14;
    Error *errp = NULL;
    Visitor *v;

    v = visitor_input_test_init(data, "%f", value);

    visit_type_number(v, &res, NULL, &errp);
    g_assert(!errp);
    g_assert_cmpfloat(res, ==, value);
}

static void test_visitor_in_string(TestInputVisitorData *data,
                                   const void *unused)
{
    char *res = NULL, *value = (char *) "Q E M U";
    Error *errp = NULL;
    Visitor *v;

    v = visitor_input_test_init(data, "%s", value);

    visit_type_str(v, &res, NULL, &errp);
    g_assert(!errp);
    g_assert_cmpstr(res, ==, value);

    g_free(res);
}

static void test_visitor_in_enum(TestInputVisitorData *data,
                                 const void *unused)
{
    Error *errp = NULL;
    Visitor *v;
    EnumOne i;

    for (i = 0; EnumOne_lookup[i]; i++) {
        EnumOne res = -1;

        v = visitor_input_test_init(data, "%s", EnumOne_lookup[i]);

        visit_type_EnumOne(v, &res, NULL, &errp);
        g_assert(!errp);
        g_assert_cmpint(i, ==, res);

        visitor_input_teardown(data, NULL);
    }

    data->obj = NULL;
    data->qiv = NULL;
}

typedef struct TestStruct
{
    int64_t integer;
    bool boolean;
    char *string;
} TestStruct;

static void visit_type_TestStruct(Visitor *v, TestStruct **obj,
                                  const char *name, Error **errp)
{
    Error *err = NULL;
    if (!error_is_set(errp)) {
        visit_start_struct(v, (void **)obj, "TestStruct", name, sizeof(TestStruct),
                           &err);
        if (!err) {
            visit_type_int(v, &(*obj)->integer, "integer", &err);
            visit_type_bool(v, &(*obj)->boolean, "boolean", &err);
            visit_type_str(v, &(*obj)->string, "string", &err);

            /* Always call end_struct if start_struct succeeded.  */
            error_propagate(errp, err);
            err = NULL;
            visit_end_struct(v, &err);
        }
        error_propagate(errp, err);
    }
}

static void test_visitor_in_struct(TestInputVisitorData *data,
                                   const void *unused)
{
    TestStruct *p = NULL;
    Error *errp = NULL;
    Visitor *v;

    v = visitor_input_test_init(data, "{ 'integer': -42, 'boolean': true, 'string': 'foo' }");

    visit_type_TestStruct(v, &p, NULL, &errp);
    g_assert(!errp);
    g_assert_cmpint(p->integer, ==, -42);
    g_assert(p->boolean == true);
    g_assert_cmpstr(p->string, ==, "foo");

    g_free(p->string);
    g_free(p);
}

static void check_and_free_str(char *str, const char *cmp)
{
    g_assert_cmpstr(str, ==, cmp);
    g_free(str);
}

static void test_visitor_in_struct_nested(TestInputVisitorData *data,
                                          const void *unused)
{
    UserDefNested *udp = NULL;
    Error *errp = NULL;
    Visitor *v;

    v = visitor_input_test_init(data, "{ 'string0': 'string0', 'dict1': { 'string1': 'string1', 'dict2': { 'userdef1': { 'integer': 42, 'string': 'string' }, 'string2': 'string2'}}}");

    visit_type_UserDefNested(v, &udp, NULL, &errp);
    g_assert(!errp);

    check_and_free_str(udp->string0, "string0");
    check_and_free_str(udp->dict1.string1, "string1");
    g_assert_cmpint(udp->dict1.dict2.userdef1->base->integer, ==, 42);
    check_and_free_str(udp->dict1.dict2.userdef1->string, "string");
    check_and_free_str(udp->dict1.dict2.string2, "string2");
    g_assert(udp->dict1.has_dict3 == false);

    g_free(udp->dict1.dict2.userdef1);
    g_free(udp);
}

static void test_visitor_in_list(TestInputVisitorData *data,
                                 const void *unused)
{
    UserDefOneList *item, *head = NULL;
    Error *errp = NULL;
    Visitor *v;
    int i;

    v = visitor_input_test_init(data, "[ { 'string': 'string0', 'integer': 42 }, { 'string': 'string1', 'integer': 43 }, { 'string': 'string2', 'integer': 44 } ]");

    visit_type_UserDefOneList(v, &head, NULL, &errp);
    g_assert(!errp);
    g_assert(head != NULL);

    for (i = 0, item = head; item; item = item->next, i++) {
        char string[12];

        snprintf(string, sizeof(string), "string%d", i);
        g_assert_cmpstr(item->value->string, ==, string);
        g_assert_cmpint(item->value->base->integer, ==, 42 + i);
    }

    qapi_free_UserDefOneList(head);
}

static void test_visitor_in_union(TestInputVisitorData *data,
                                  const void *unused)
{
    Visitor *v;
    Error *err = NULL;
    UserDefUnion *tmp;

    v = visitor_input_test_init(data, "{ 'type': 'b', 'integer': 41, 'data' : { 'integer': 42 } }");

    visit_type_UserDefUnion(v, &tmp, NULL, &err);
    g_assert(err == NULL);
    g_assert_cmpint(tmp->kind, ==, USER_DEF_UNION_KIND_B);
    g_assert_cmpint(tmp->integer, ==, 41);
    g_assert_cmpint(tmp->b->integer, ==, 42);
    qapi_free_UserDefUnion(tmp);
}

static void test_visitor_in_union_flat(TestInputVisitorData *data,
                                       const void *unused)
{
    Visitor *v;
    Error *err = NULL;
    UserDefFlatUnion *tmp;

    v = visitor_input_test_init(data,
                                "{ 'enum1': 'value1', "
                                "'string': 'str', "
                                "'boolean': true }");
    /* TODO when generator bug is fixed, add 'integer': 41 */

    visit_type_UserDefFlatUnion(v, &tmp, NULL, &err);
    g_assert(err == NULL);
    g_assert_cmpint(tmp->kind, ==, ENUM_ONE_VALUE1);
    g_assert_cmpstr(tmp->string, ==, "str");
    /* TODO g_assert_cmpint(tmp->integer, ==, 41); */
    g_assert_cmpint(tmp->value1->boolean, ==, true);
    qapi_free_UserDefFlatUnion(tmp);
}

static void test_visitor_in_union_anon(TestInputVisitorData *data,
                                       const void *unused)
{
    Visitor *v;
    Error *err = NULL;
    UserDefAnonUnion *tmp;

    v = visitor_input_test_init(data, "42");

    visit_type_UserDefAnonUnion(v, &tmp, NULL, &err);
    g_assert(err == NULL);
    g_assert_cmpint(tmp->kind, ==, USER_DEF_ANON_UNION_KIND_I);
    g_assert_cmpint(tmp->i, ==, 42);
    qapi_free_UserDefAnonUnion(tmp);
}

static void test_native_list_integer_helper(TestInputVisitorData *data,
                                            const void *unused,
                                            UserDefNativeListUnionKind kind)
{
    UserDefNativeListUnion *cvalue = NULL;
    Error *err = NULL;
    Visitor *v;
    GString *gstr_list = g_string_new("");
    GString *gstr_union = g_string_new("");
    int i;

    for (i = 0; i < 32; i++) {
        g_string_append_printf(gstr_list, "%d", i);
        if (i != 31) {
            g_string_append(gstr_list, ", ");
        }
    }
    g_string_append_printf(gstr_union,  "{ 'type': '%s', 'data': [ %s ] }",
                           UserDefNativeListUnionKind_lookup[kind],
                           gstr_list->str);
    v = visitor_input_test_init_raw(data,  gstr_union->str);

    visit_type_UserDefNativeListUnion(v, &cvalue, NULL, &err);
    g_assert(err == NULL);
    g_assert(cvalue != NULL);
    g_assert_cmpint(cvalue->kind, ==, kind);

    switch (kind) {
    case USER_DEF_NATIVE_LIST_UNION_KIND_INTEGER: {
        intList *elem = NULL;
        for (i = 0, elem = cvalue->integer; elem; elem = elem->next, i++) {
            g_assert_cmpint(elem->value, ==, i);
        }
        break;
    }
    case USER_DEF_NATIVE_LIST_UNION_KIND_S8: {
        int8List *elem = NULL;
        for (i = 0, elem = cvalue->s8; elem; elem = elem->next, i++) {
            g_assert_cmpint(elem->value, ==, i);
        }
        break;
    }
    case USER_DEF_NATIVE_LIST_UNION_KIND_S16: {
        int16List *elem = NULL;
        for (i = 0, elem = cvalue->s16; elem; elem = elem->next, i++) {
            g_assert_cmpint(elem->value, ==, i);
        }
        break;
    }
    case USER_DEF_NATIVE_LIST_UNION_KIND_S32: {
        int32List *elem = NULL;
        for (i = 0, elem = cvalue->s32; elem; elem = elem->next, i++) {
            g_assert_cmpint(elem->value, ==, i);
        }
        break;
    }
    case USER_DEF_NATIVE_LIST_UNION_KIND_S64: {
        int64List *elem = NULL;
        for (i = 0, elem = cvalue->s64; elem; elem = elem->next, i++) {
            g_assert_cmpint(elem->value, ==, i);
        }
        break;
    }
    case USER_DEF_NATIVE_LIST_UNION_KIND_U8: {
        uint8List *elem = NULL;
        for (i = 0, elem = cvalue->u8; elem; elem = elem->next, i++) {
            g_assert_cmpint(elem->value, ==, i);
        }
        break;
    }
    case USER_DEF_NATIVE_LIST_UNION_KIND_U16: {
        uint16List *elem = NULL;
        for (i = 0, elem = cvalue->u16; elem; elem = elem->next, i++) {
            g_assert_cmpint(elem->value, ==, i);
        }
        break;
    }
    case USER_DEF_NATIVE_LIST_UNION_KIND_U32: {
        uint32List *elem = NULL;
        for (i = 0, elem = cvalue->u32; elem; elem = elem->next, i++) {
            g_assert_cmpint(elem->value, ==, i);
        }
        break;
    }
    case USER_DEF_NATIVE_LIST_UNION_KIND_U64: {
        uint64List *elem = NULL;
        for (i = 0, elem = cvalue->u64; elem; elem = elem->next, i++) {
            g_assert_cmpint(elem->value, ==, i);
        }
        break;
    }
    default:
        g_assert_not_reached();
    }

    g_string_free(gstr_union, true);
    g_string_free(gstr_list, true);
    qapi_free_UserDefNativeListUnion(cvalue);
}

static void test_visitor_in_native_list_int(TestInputVisitorData *data,
                                            const void *unused)
{
    test_native_list_integer_helper(data, unused,
                                    USER_DEF_NATIVE_LIST_UNION_KIND_INTEGER);
}

static void test_visitor_in_native_list_int8(TestInputVisitorData *data,
                                             const void *unused)
{
    test_native_list_integer_helper(data, unused,
                                    USER_DEF_NATIVE_LIST_UNION_KIND_S8);
}

static void test_visitor_in_native_list_int16(TestInputVisitorData *data,
                                              const void *unused)
{
    test_native_list_integer_helper(data, unused,
                                    USER_DEF_NATIVE_LIST_UNION_KIND_S16);
}

static void test_visitor_in_native_list_int32(TestInputVisitorData *data,
                                              const void *unused)
{
    test_native_list_integer_helper(data, unused,
                                    USER_DEF_NATIVE_LIST_UNION_KIND_S32);
}

static void test_visitor_in_native_list_int64(TestInputVisitorData *data,
                                              const void *unused)
{
    test_native_list_integer_helper(data, unused,
                                    USER_DEF_NATIVE_LIST_UNION_KIND_S64);
}

static void test_visitor_in_native_list_uint8(TestInputVisitorData *data,
                                             const void *unused)
{
    test_native_list_integer_helper(data, unused,
                                    USER_DEF_NATIVE_LIST_UNION_KIND_U8);
}

static void test_visitor_in_native_list_uint16(TestInputVisitorData *data,
                                               const void *unused)
{
    test_native_list_integer_helper(data, unused,
                                    USER_DEF_NATIVE_LIST_UNION_KIND_U16);
}

static void test_visitor_in_native_list_uint32(TestInputVisitorData *data,
                                               const void *unused)
{
    test_native_list_integer_helper(data, unused,
                                    USER_DEF_NATIVE_LIST_UNION_KIND_U32);
}

static void test_visitor_in_native_list_uint64(TestInputVisitorData *data,
                                               const void *unused)
{
    test_native_list_integer_helper(data, unused,
                                    USER_DEF_NATIVE_LIST_UNION_KIND_U64);
}

static void test_visitor_in_native_list_bool(TestInputVisitorData *data,
                                            const void *unused)
{
    UserDefNativeListUnion *cvalue = NULL;
    boolList *elem = NULL;
    Error *err = NULL;
    Visitor *v;
    GString *gstr_list = g_string_new("");
    GString *gstr_union = g_string_new("");
    int i;

    for (i = 0; i < 32; i++) {
        g_string_append_printf(gstr_list, "%s",
                               (i % 3 == 0) ? "true" : "false");
        if (i != 31) {
            g_string_append(gstr_list, ", ");
        }
    }
    g_string_append_printf(gstr_union,  "{ 'type': 'boolean', 'data': [ %s ] }",
                           gstr_list->str);
    v = visitor_input_test_init_raw(data,  gstr_union->str);

    visit_type_UserDefNativeListUnion(v, &cvalue, NULL, &err);
    g_assert(err == NULL);
    g_assert(cvalue != NULL);
    g_assert_cmpint(cvalue->kind, ==, USER_DEF_NATIVE_LIST_UNION_KIND_BOOLEAN);

    for (i = 0, elem = cvalue->boolean; elem; elem = elem->next, i++) {
        g_assert_cmpint(elem->value, ==, (i % 3 == 0) ? 1 : 0);
    }

    g_string_free(gstr_union, true);
    g_string_free(gstr_list, true);
    qapi_free_UserDefNativeListUnion(cvalue);
}

static void test_visitor_in_native_list_string(TestInputVisitorData *data,
                                               const void *unused)
{
    UserDefNativeListUnion *cvalue = NULL;
    strList *elem = NULL;
    Error *err = NULL;
    Visitor *v;
    GString *gstr_list = g_string_new("");
    GString *gstr_union = g_string_new("");
    int i;

    for (i = 0; i < 32; i++) {
        g_string_append_printf(gstr_list, "'%d'", i);
        if (i != 31) {
            g_string_append(gstr_list, ", ");
        }
    }
    g_string_append_printf(gstr_union,  "{ 'type': 'string', 'data': [ %s ] }",
                           gstr_list->str);
    v = visitor_input_test_init_raw(data,  gstr_union->str);

    visit_type_UserDefNativeListUnion(v, &cvalue, NULL, &err);
    g_assert(err == NULL);
    g_assert(cvalue != NULL);
    g_assert_cmpint(cvalue->kind, ==, USER_DEF_NATIVE_LIST_UNION_KIND_STRING);

    for (i = 0, elem = cvalue->string; elem; elem = elem->next, i++) {
        gchar str[8];
        sprintf(str, "%d", i);
        g_assert_cmpstr(elem->value, ==, str);
    }

    g_string_free(gstr_union, true);
    g_string_free(gstr_list, true);
    qapi_free_UserDefNativeListUnion(cvalue);
}

#define DOUBLE_STR_MAX 16

static void test_visitor_in_native_list_number(TestInputVisitorData *data,
                                               const void *unused)
{
    UserDefNativeListUnion *cvalue = NULL;
    numberList *elem = NULL;
    Error *err = NULL;
    Visitor *v;
    GString *gstr_list = g_string_new("");
    GString *gstr_union = g_string_new("");
    int i;

    for (i = 0; i < 32; i++) {
        g_string_append_printf(gstr_list, "%f", (double)i / 3);
        if (i != 31) {
            g_string_append(gstr_list, ", ");
        }
    }
    g_string_append_printf(gstr_union,  "{ 'type': 'number', 'data': [ %s ] }",
                           gstr_list->str);
    v = visitor_input_test_init_raw(data,  gstr_union->str);

    visit_type_UserDefNativeListUnion(v, &cvalue, NULL, &err);
    g_assert(err == NULL);
    g_assert(cvalue != NULL);
    g_assert_cmpint(cvalue->kind, ==, USER_DEF_NATIVE_LIST_UNION_KIND_NUMBER);

    for (i = 0, elem = cvalue->number; elem; elem = elem->next, i++) {
        GString *double_expected = g_string_new("");
        GString *double_actual = g_string_new("");

        g_string_printf(double_expected, "%.6f", (double)i / 3);
        g_string_printf(double_actual, "%.6f", elem->value);
        g_assert_cmpstr(double_expected->str, ==, double_actual->str);

        g_string_free(double_expected, true);
        g_string_free(double_actual, true);
    }

    g_string_free(gstr_union, true);
    g_string_free(gstr_list, true);
    qapi_free_UserDefNativeListUnion(cvalue);
}

static void input_visitor_test_add(const char *testpath,
                                   TestInputVisitorData *data,
                                   void (*test_func)(TestInputVisitorData *data, const void *user_data))
{
    g_test_add(testpath, TestInputVisitorData, data, NULL, test_func,
               visitor_input_teardown);
}

static void test_visitor_in_errors(TestInputVisitorData *data,
                                   const void *unused)
{
    TestStruct *p = NULL;
    Error *errp = NULL;
    Visitor *v;

    v = visitor_input_test_init(data, "{ 'integer': false, 'boolean': 'foo', 'string': -42 }");

    visit_type_TestStruct(v, &p, NULL, &errp);
    g_assert(errp);
    g_assert(p->string == NULL);

    error_free(errp);
    g_free(p->string);
    g_free(p);
}

int main(int argc, char **argv)
{
    TestInputVisitorData in_visitor_data;

    g_test_init(&argc, &argv, NULL);

    input_visitor_test_add("/visitor/input/int",
                           &in_visitor_data, test_visitor_in_int);
    input_visitor_test_add("/visitor/input/int_overflow",
                           &in_visitor_data, test_visitor_in_int_overflow);
    input_visitor_test_add("/visitor/input/bool",
                           &in_visitor_data, test_visitor_in_bool);
    input_visitor_test_add("/visitor/input/number",
                           &in_visitor_data, test_visitor_in_number);
    input_visitor_test_add("/visitor/input/string",
                            &in_visitor_data, test_visitor_in_string);
    input_visitor_test_add("/visitor/input/enum",
                            &in_visitor_data, test_visitor_in_enum);
    input_visitor_test_add("/visitor/input/struct",
                            &in_visitor_data, test_visitor_in_struct);
    input_visitor_test_add("/visitor/input/struct-nested",
                            &in_visitor_data, test_visitor_in_struct_nested);
    input_visitor_test_add("/visitor/input/list",
                            &in_visitor_data, test_visitor_in_list);
    input_visitor_test_add("/visitor/input/union",
                            &in_visitor_data, test_visitor_in_union);
    input_visitor_test_add("/visitor/input/union-flat",
                            &in_visitor_data, test_visitor_in_union_flat);
    input_visitor_test_add("/visitor/input/union-anon",
                            &in_visitor_data, test_visitor_in_union_anon);
    input_visitor_test_add("/visitor/input/errors",
                            &in_visitor_data, test_visitor_in_errors);
    input_visitor_test_add("/visitor/input/native_list/int",
                            &in_visitor_data,
                            test_visitor_in_native_list_int);
    input_visitor_test_add("/visitor/input/native_list/int8",
                            &in_visitor_data,
                            test_visitor_in_native_list_int8);
    input_visitor_test_add("/visitor/input/native_list/int16",
                            &in_visitor_data,
                            test_visitor_in_native_list_int16);
    input_visitor_test_add("/visitor/input/native_list/int32",
                            &in_visitor_data,
                            test_visitor_in_native_list_int32);
    input_visitor_test_add("/visitor/input/native_list/int64",
                            &in_visitor_data,
                            test_visitor_in_native_list_int64);
    input_visitor_test_add("/visitor/input/native_list/uint8",
                            &in_visitor_data,
                            test_visitor_in_native_list_uint8);
    input_visitor_test_add("/visitor/input/native_list/uint16",
                            &in_visitor_data,
                            test_visitor_in_native_list_uint16);
    input_visitor_test_add("/visitor/input/native_list/uint32",
                            &in_visitor_data,
                            test_visitor_in_native_list_uint32);
    input_visitor_test_add("/visitor/input/native_list/uint64",
                            &in_visitor_data, test_visitor_in_native_list_uint64);
    input_visitor_test_add("/visitor/input/native_list/bool",
                            &in_visitor_data, test_visitor_in_native_list_bool);
    input_visitor_test_add("/visitor/input/native_list/str",
                            &in_visitor_data, test_visitor_in_native_list_string);
    input_visitor_test_add("/visitor/input/native_list/number",
                            &in_visitor_data, test_visitor_in_native_list_number);

    g_test_run();

    return 0;
}
