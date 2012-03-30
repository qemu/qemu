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

#include "qapi/qmp-input-visitor.h"
#include "test-qapi-types.h"
#include "test-qapi-visit.h"
#include "qemu-objects.h"

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

static void test_visitor_in_int(TestInputVisitorData *data,
                                const void *unused)
{
    int64_t res = 0, value = -42;
    Error *errp = NULL;
    Visitor *v;

    v = visitor_input_test_init(data, "%" PRId64, value);

    visit_type_int(v, &res, NULL, &errp);
    g_assert(!error_is_set(&errp));
    g_assert_cmpint(res, ==, value);
}

static void test_visitor_in_bool(TestInputVisitorData *data,
                                 const void *unused)
{
    Error *errp = NULL;
    bool res = false;
    Visitor *v;

    v = visitor_input_test_init(data, "true");

    visit_type_bool(v, &res, NULL, &errp);
    g_assert(!error_is_set(&errp));
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
    g_assert(!error_is_set(&errp));
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
    g_assert(!error_is_set(&errp));
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
        g_assert(!error_is_set(&errp));
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
    visit_start_struct(v, (void **)obj, "TestStruct", name, sizeof(TestStruct),
                       errp);

    visit_type_int(v, &(*obj)->integer, "integer", errp);
    visit_type_bool(v, &(*obj)->boolean, "boolean", errp);
    visit_type_str(v, &(*obj)->string, "string", errp);

    visit_end_struct(v, errp);
}

static void test_visitor_in_struct(TestInputVisitorData *data,
                                   const void *unused)
{
    TestStruct *p = NULL;
    Error *errp = NULL;
    Visitor *v;

    v = visitor_input_test_init(data, "{ 'integer': -42, 'boolean': true, 'string': 'foo' }");

    visit_type_TestStruct(v, &p, NULL, &errp);
    g_assert(!error_is_set(&errp));
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
    g_assert(!error_is_set(&errp));

    check_and_free_str(udp->string0, "string0");
    check_and_free_str(udp->dict1.string1, "string1");
    g_assert_cmpint(udp->dict1.dict2.userdef1->integer, ==, 42);
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
    g_assert(!error_is_set(&errp));
    g_assert(head != NULL);

    for (i = 0, item = head; item; item = item->next, i++) {
        char string[12];

        snprintf(string, sizeof(string), "string%d", i);
        g_assert_cmpstr(item->value->string, ==, string);
        g_assert_cmpint(item->value->integer, ==, 42 + i);
    }

    qapi_free_UserDefOneList(head);
}

static void test_visitor_in_union(TestInputVisitorData *data,
                                  const void *unused)
{
    Visitor *v;
    Error *err = NULL;
    UserDefUnion *tmp;

    v = visitor_input_test_init(data, "{ 'type': 'b', 'data' : { 'integer': 42 } }");

    visit_type_UserDefUnion(v, &tmp, NULL, &err);
    g_assert(err == NULL);
    g_assert_cmpint(tmp->kind, ==, USER_DEF_UNION_KIND_B);
    g_assert_cmpint(tmp->b->integer, ==, 42);
    qapi_free_UserDefUnion(tmp);
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
    g_assert(error_is_set(&errp));
    g_assert(p->string == NULL);

    g_free(p->string);
    g_free(p);
}

int main(int argc, char **argv)
{
    TestInputVisitorData in_visitor_data;

    g_test_init(&argc, &argv, NULL);

    input_visitor_test_add("/visitor/input/int",
                           &in_visitor_data, test_visitor_in_int);
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
    input_visitor_test_add("/visitor/input/errors",
                            &in_visitor_data, test_visitor_in_errors);

    g_test_run();

    return 0;
}
