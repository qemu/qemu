/*
 * QMP Input Visitor unit-tests (strict mode).
 *
 * Copyright (C) 2011-2012, 2015 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *  Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib.h>

#include "qemu-common.h"
#include "qapi/error.h"
#include "qapi/qmp-input-visitor.h"
#include "test-qapi-types.h"
#include "test-qapi-visit.h"
#include "qapi/qmp/types.h"
#include "test-qmp-introspect.h"
#include "qmp-introspect.h"
#include "qapi-visit.h"

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

/* The various test_init functions are provided instead of a test setup
   function so that the JSON string used by the tests are kept in the test
   functions (and not in main()). */
static Visitor *validate_test_init_internal(TestInputVisitorData *data,
                                            const char *json_string,
                                            va_list *ap)
{
    Visitor *v;

    validate_teardown(data, NULL);

    data->obj = qobject_from_jsonv(json_string, ap);
    g_assert(data->obj);

    data->qiv = qmp_input_visitor_new_strict(data->obj);
    g_assert(data->qiv);

    v = qmp_input_get_visitor(data->qiv);
    g_assert(v);

    return v;
}

static GCC_FMT_ATTR(2, 3)
Visitor *validate_test_init(TestInputVisitorData *data,
                             const char *json_string, ...)
{
    Visitor *v;
    va_list ap;

    va_start(ap, json_string);
    v = validate_test_init_internal(data, json_string, &ap);
    va_end(ap);
    return v;
}

/* similar to validate_test_init(), but does not expect a string
 * literal/format json_string argument and so can be used for
 * programatically generated strings (and we can't pass in programatically
 * generated strings via %s format parameters since qobject_from_jsonv()
 * will wrap those in double-quotes and treat the entire object as a
 * string)
 */
static Visitor *validate_test_init_raw(TestInputVisitorData *data,
                                       const char *json_string)
{
    return validate_test_init_internal(data, json_string, NULL);
}


static void test_validate_struct(TestInputVisitorData *data,
                                  const void *unused)
{
    TestStruct *p = NULL;
    Visitor *v;

    v = validate_test_init(data, "{ 'integer': -42, 'boolean': true, 'string': 'foo' }");

    visit_type_TestStruct(v, NULL, &p, &error_abort);
    g_free(p->string);
    g_free(p);
}

static void test_validate_struct_nested(TestInputVisitorData *data,
                                         const void *unused)
{
    UserDefTwo *udp = NULL;
    Visitor *v;

    v = validate_test_init(data, "{ 'string0': 'string0', "
                           "'dict1': { 'string1': 'string1', "
                           "'dict2': { 'userdef': { 'integer': 42, "
                           "'string': 'string' }, 'string': 'string2'}}}");

    visit_type_UserDefTwo(v, NULL, &udp, &error_abort);
    qapi_free_UserDefTwo(udp);
}

static void test_validate_list(TestInputVisitorData *data,
                                const void *unused)
{
    UserDefOneList *head = NULL;
    Visitor *v;

    v = validate_test_init(data, "[ { 'string': 'string0', 'integer': 42 }, { 'string': 'string1', 'integer': 43 }, { 'string': 'string2', 'integer': 44 } ]");

    visit_type_UserDefOneList(v, NULL, &head, &error_abort);
    qapi_free_UserDefOneList(head);
}

static void test_validate_union_native_list(TestInputVisitorData *data,
                                            const void *unused)
{
    UserDefNativeListUnion *tmp = NULL;
    Visitor *v;

    v = validate_test_init(data, "{ 'type': 'integer', 'data' : [ 1, 2 ] }");

    visit_type_UserDefNativeListUnion(v, NULL, &tmp, &error_abort);
    qapi_free_UserDefNativeListUnion(tmp);
}

static void test_validate_union_flat(TestInputVisitorData *data,
                                     const void *unused)
{
    UserDefFlatUnion *tmp = NULL;
    Visitor *v;

    v = validate_test_init(data,
                           "{ 'enum1': 'value1', "
                           "'integer': 41, "
                           "'string': 'str', "
                           "'boolean': true }");

    visit_type_UserDefFlatUnion(v, NULL, &tmp, &error_abort);
    qapi_free_UserDefFlatUnion(tmp);
}

static void test_validate_alternate(TestInputVisitorData *data,
                                    const void *unused)
{
    UserDefAlternate *tmp = NULL;
    Visitor *v;

    v = validate_test_init(data, "42");

    visit_type_UserDefAlternate(v, NULL, &tmp, &error_abort);
    qapi_free_UserDefAlternate(tmp);
}

static void test_validate_fail_struct(TestInputVisitorData *data,
                                       const void *unused)
{
    TestStruct *p = NULL;
    Error *err = NULL;
    Visitor *v;

    v = validate_test_init(data, "{ 'integer': -42, 'boolean': true, 'string': 'foo', 'extra': 42 }");

    visit_type_TestStruct(v, NULL, &p, &err);
    error_free_or_abort(&err);
    if (p) {
        g_free(p->string);
    }
    g_free(p);
}

static void test_validate_fail_struct_nested(TestInputVisitorData *data,
                                              const void *unused)
{
    UserDefTwo *udp = NULL;
    Error *err = NULL;
    Visitor *v;

    v = validate_test_init(data, "{ 'string0': 'string0', 'dict1': { 'string1': 'string1', 'dict2': { 'userdef1': { 'integer': 42, 'string': 'string', 'extra': [42, 23, {'foo':'bar'}] }, 'string2': 'string2'}}}");

    visit_type_UserDefTwo(v, NULL, &udp, &err);
    error_free_or_abort(&err);
    qapi_free_UserDefTwo(udp);
}

static void test_validate_fail_list(TestInputVisitorData *data,
                                     const void *unused)
{
    UserDefOneList *head = NULL;
    Error *err = NULL;
    Visitor *v;

    v = validate_test_init(data, "[ { 'string': 'string0', 'integer': 42 }, { 'string': 'string1', 'integer': 43 }, { 'string': 'string2', 'integer': 44, 'extra': 'ggg' } ]");

    visit_type_UserDefOneList(v, NULL, &head, &err);
    error_free_or_abort(&err);
    qapi_free_UserDefOneList(head);
}

static void test_validate_fail_union_native_list(TestInputVisitorData *data,
                                                 const void *unused)
{
    UserDefNativeListUnion *tmp = NULL;
    Error *err = NULL;
    Visitor *v;

    v = validate_test_init(data,
                           "{ 'type': 'integer', 'data' : [ 'string' ] }");

    visit_type_UserDefNativeListUnion(v, NULL, &tmp, &err);
    error_free_or_abort(&err);
    qapi_free_UserDefNativeListUnion(tmp);
}

static void test_validate_fail_union_flat(TestInputVisitorData *data,
                                          const void *unused)
{
    UserDefFlatUnion *tmp = NULL;
    Error *err = NULL;
    Visitor *v;

    v = validate_test_init(data, "{ 'string': 'c', 'integer': 41, 'boolean': true }");

    visit_type_UserDefFlatUnion(v, NULL, &tmp, &err);
    error_free_or_abort(&err);
    qapi_free_UserDefFlatUnion(tmp);
}

static void test_validate_fail_union_flat_no_discrim(TestInputVisitorData *data,
                                                     const void *unused)
{
    UserDefFlatUnion2 *tmp = NULL;
    Error *err = NULL;
    Visitor *v;

    /* test situation where discriminator field ('enum1' here) is missing */
    v = validate_test_init(data, "{ 'integer': 42, 'string': 'c', 'string1': 'd', 'string2': 'e' }");

    visit_type_UserDefFlatUnion2(v, NULL, &tmp, &err);
    error_free_or_abort(&err);
    qapi_free_UserDefFlatUnion2(tmp);
}

static void test_validate_fail_alternate(TestInputVisitorData *data,
                                         const void *unused)
{
    UserDefAlternate *tmp;
    Visitor *v;
    Error *err = NULL;

    v = validate_test_init(data, "3.14");

    visit_type_UserDefAlternate(v, NULL, &tmp, &err);
    error_free_or_abort(&err);
    qapi_free_UserDefAlternate(tmp);
}

static void do_test_validate_qmp_introspect(TestInputVisitorData *data,
                                            const char *schema_json)
{
    SchemaInfoList *schema = NULL;
    Visitor *v;

    v = validate_test_init_raw(data, schema_json);

    visit_type_SchemaInfoList(v, NULL, &schema, &error_abort);
    g_assert(schema);

    qapi_free_SchemaInfoList(schema);
}

static void test_validate_qmp_introspect(TestInputVisitorData *data,
                                           const void *unused)
{
    do_test_validate_qmp_introspect(data, test_qmp_schema_json);
    do_test_validate_qmp_introspect(data, qmp_schema_json);
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
    validate_test_add("/visitor/input-strict/pass/union-flat",
                      &testdata, test_validate_union_flat);
    validate_test_add("/visitor/input-strict/pass/alternate",
                      &testdata, test_validate_alternate);
    validate_test_add("/visitor/input-strict/pass/union-native-list",
                      &testdata, test_validate_union_native_list);
    validate_test_add("/visitor/input-strict/fail/struct",
                      &testdata, test_validate_fail_struct);
    validate_test_add("/visitor/input-strict/fail/struct-nested",
                      &testdata, test_validate_fail_struct_nested);
    validate_test_add("/visitor/input-strict/fail/list",
                      &testdata, test_validate_fail_list);
    validate_test_add("/visitor/input-strict/fail/union-flat",
                      &testdata, test_validate_fail_union_flat);
    validate_test_add("/visitor/input-strict/fail/union-flat-no-discriminator",
                      &testdata, test_validate_fail_union_flat_no_discrim);
    validate_test_add("/visitor/input-strict/fail/alternate",
                      &testdata, test_validate_fail_alternate);
    validate_test_add("/visitor/input-strict/fail/union-native-list",
                      &testdata, test_validate_fail_union_native_list);
    validate_test_add("/visitor/input-strict/pass/qmp-introspect",
                      &testdata, test_validate_qmp_introspect);

    g_test_run();

    return 0;
}
