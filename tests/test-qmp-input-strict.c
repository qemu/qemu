/*
 * QMP Input Visitor unit-tests (strict mode).
 *
 * Copyright (C) 2011-2012 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *  Paolo Bonzini <pbonzini@redhat.com>
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

static void validate_teardown(TestInputVisitorData *data,
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
Visitor *validate_test_init(TestInputVisitorData *data,
                             const char *json_string, ...)
{
    Visitor *v;
    va_list ap;

    va_start(ap, json_string);
    data->obj = qobject_from_jsonv(json_string, &ap);
    va_end(ap);

    g_assert(data->obj != NULL);

    data->qiv = qmp_input_visitor_new_strict(data->obj);
    g_assert(data->qiv != NULL);

    v = qmp_input_get_visitor(data->qiv);
    g_assert(v != NULL);

    return v;
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

    visit_start_struct(v, (void **)obj, "TestStruct", name, sizeof(TestStruct),
                       &err);
    if (err) {
        goto out;
    }

    visit_type_int(v, &(*obj)->integer, "integer", &err);
    visit_type_bool(v, &(*obj)->boolean, "boolean", &err);
    visit_type_str(v, &(*obj)->string, "string", &err);

    visit_end_struct(v, &err);

out:
    error_propagate(errp, err);
}

static void test_validate_struct(TestInputVisitorData *data,
                                  const void *unused)
{
    TestStruct *p = NULL;
    Error *err = NULL;
    Visitor *v;

    v = validate_test_init(data, "{ 'integer': -42, 'boolean': true, 'string': 'foo' }");

    visit_type_TestStruct(v, &p, NULL, &err);
    g_assert(!err);
    g_free(p->string);
    g_free(p);
}

static void test_validate_struct_nested(TestInputVisitorData *data,
                                         const void *unused)
{
    UserDefNested *udp = NULL;
    Error *err = NULL;
    Visitor *v;

    v = validate_test_init(data, "{ 'string0': 'string0', 'dict1': { 'string1': 'string1', 'dict2': { 'userdef1': { 'integer': 42, 'string': 'string' }, 'string2': 'string2'}}}");

    visit_type_UserDefNested(v, &udp, NULL, &err);
    g_assert(!err);
    qapi_free_UserDefNested(udp);
}

static void test_validate_list(TestInputVisitorData *data,
                                const void *unused)
{
    UserDefOneList *head = NULL;
    Error *err = NULL;
    Visitor *v;

    v = validate_test_init(data, "[ { 'string': 'string0', 'integer': 42 }, { 'string': 'string1', 'integer': 43 }, { 'string': 'string2', 'integer': 44 } ]");

    visit_type_UserDefOneList(v, &head, NULL, &err);
    g_assert(!err);
    qapi_free_UserDefOneList(head);
}

static void test_validate_union(TestInputVisitorData *data,
                                 const void *unused)
{
    UserDefUnion *tmp = NULL;
    Visitor *v;
    Error *err = NULL;

    v = validate_test_init(data, "{ 'type': 'b', 'integer': 41, 'data' : { 'integer': 42 } }");

    visit_type_UserDefUnion(v, &tmp, NULL, &err);
    g_assert(!err);
    qapi_free_UserDefUnion(tmp);
}

static void test_validate_union_flat(TestInputVisitorData *data,
                                     const void *unused)
{
    UserDefFlatUnion *tmp = NULL;
    Visitor *v;
    Error *err = NULL;

    v = validate_test_init(data,
                           "{ 'enum1': 'value1', "
                           "'string': 'str', "
                           "'boolean': true }");
    /* TODO when generator bug is fixed, add 'integer': 41 */

    visit_type_UserDefFlatUnion(v, &tmp, NULL, &err);
    g_assert(!err);
    qapi_free_UserDefFlatUnion(tmp);
}

static void test_validate_union_anon(TestInputVisitorData *data,
                                     const void *unused)
{
    UserDefAnonUnion *tmp = NULL;
    Visitor *v;
    Error *err = NULL;

    v = validate_test_init(data, "42");

    visit_type_UserDefAnonUnion(v, &tmp, NULL, &err);
    g_assert(!err);
    qapi_free_UserDefAnonUnion(tmp);
}

static void test_validate_fail_struct(TestInputVisitorData *data,
                                       const void *unused)
{
    TestStruct *p = NULL;
    Error *err = NULL;
    Visitor *v;

    v = validate_test_init(data, "{ 'integer': -42, 'boolean': true, 'string': 'foo', 'extra': 42 }");

    visit_type_TestStruct(v, &p, NULL, &err);
    g_assert(err);
    if (p) {
        g_free(p->string);
    }
    g_free(p);
}

static void test_validate_fail_struct_nested(TestInputVisitorData *data,
                                              const void *unused)
{
    UserDefNested *udp = NULL;
    Error *err = NULL;
    Visitor *v;

    v = validate_test_init(data, "{ 'string0': 'string0', 'dict1': { 'string1': 'string1', 'dict2': { 'userdef1': { 'integer': 42, 'string': 'string', 'extra': [42, 23, {'foo':'bar'}] }, 'string2': 'string2'}}}");

    visit_type_UserDefNested(v, &udp, NULL, &err);
    g_assert(err);
    qapi_free_UserDefNested(udp);
}

static void test_validate_fail_list(TestInputVisitorData *data,
                                     const void *unused)
{
    UserDefOneList *head = NULL;
    Error *err = NULL;
    Visitor *v;

    v = validate_test_init(data, "[ { 'string': 'string0', 'integer': 42 }, { 'string': 'string1', 'integer': 43 }, { 'string': 'string2', 'integer': 44, 'extra': 'ggg' } ]");

    visit_type_UserDefOneList(v, &head, NULL, &err);
    g_assert(err);
    qapi_free_UserDefOneList(head);
}

static void test_validate_fail_union(TestInputVisitorData *data,
                                      const void *unused)
{
    UserDefUnion *tmp = NULL;
    Error *err = NULL;
    Visitor *v;

    v = validate_test_init(data, "{ 'type': 'b', 'data' : { 'integer': 42 } }");

    visit_type_UserDefUnion(v, &tmp, NULL, &err);
    g_assert(err);
    qapi_free_UserDefUnion(tmp);
}

static void test_validate_fail_union_flat(TestInputVisitorData *data,
                                          const void *unused)
{
    UserDefFlatUnion *tmp = NULL;
    Error *err = NULL;
    Visitor *v;

    v = validate_test_init(data, "{ 'string': 'c', 'integer': 41, 'boolean': true }");

    visit_type_UserDefFlatUnion(v, &tmp, NULL, &err);
    g_assert(err);
    qapi_free_UserDefFlatUnion(tmp);
}

static void test_validate_fail_union_anon(TestInputVisitorData *data,
                                          const void *unused)
{
    UserDefAnonUnion *tmp = NULL;
    Visitor *v;
    Error *err = NULL;

    v = validate_test_init(data, "3.14");

    visit_type_UserDefAnonUnion(v, &tmp, NULL, &err);
    g_assert(err);
    qapi_free_UserDefAnonUnion(tmp);
}

static void validate_test_add(const char *testpath,
                               TestInputVisitorData *data,
                               void (*test_func)(TestInputVisitorData *data, const void *user_data))
{
    g_test_add(testpath, TestInputVisitorData, data, NULL, test_func,
               validate_teardown);
}

int main(int argc, char **argv)
{
    TestInputVisitorData testdata;

    g_test_init(&argc, &argv, NULL);

    validate_test_add("/visitor/input-strict/pass/struct",
                       &testdata, test_validate_struct);
    validate_test_add("/visitor/input-strict/pass/struct-nested",
                       &testdata, test_validate_struct_nested);
    validate_test_add("/visitor/input-strict/pass/list",
                       &testdata, test_validate_list);
    validate_test_add("/visitor/input-strict/pass/union",
                       &testdata, test_validate_union);
    validate_test_add("/visitor/input-strict/pass/union-flat",
                       &testdata, test_validate_union_flat);
    validate_test_add("/visitor/input-strict/pass/union-anon",
                       &testdata, test_validate_union_anon);
    validate_test_add("/visitor/input-strict/fail/struct",
                       &testdata, test_validate_fail_struct);
    validate_test_add("/visitor/input-strict/fail/struct-nested",
                       &testdata, test_validate_fail_struct_nested);
    validate_test_add("/visitor/input-strict/fail/list",
                       &testdata, test_validate_fail_list);
    validate_test_add("/visitor/input-strict/fail/union",
                       &testdata, test_validate_fail_union);
    validate_test_add("/visitor/input-strict/fail/union-flat",
                       &testdata, test_validate_fail_union_flat);
    validate_test_add("/visitor/input-strict/fail/union-anon",
                       &testdata, test_validate_fail_union_anon);

    g_test_run();

    return 0;
}
