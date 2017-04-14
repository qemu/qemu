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

#include "qemu-common.h"
#include "qapi/error.h"
#include "qapi/qobject-input-visitor.h"
#include "test-qapi-types.h"
#include "test-qapi-visit.h"
#include "qapi/qmp/types.h"
#include "qapi/qmp/qjson.h"
#include "test-qmp-introspect.h"
#include "qmp-introspect.h"
#include "qapi-visit.h"

typedef struct TestInputVisitorData {
    QObject *obj;
    Visitor *qiv;
} TestInputVisitorData;

static void visitor_input_teardown(TestInputVisitorData *data,
                                   const void *unused)
{
    qobject_decref(data->obj);
    data->obj = NULL;

    if (data->qiv) {
        visit_free(data->qiv);
        data->qiv = NULL;
    }
}

/* The various test_init functions are provided instead of a test setup
   function so that the JSON string used by the tests are kept in the test
   functions (and not in main()). */
static Visitor *visitor_input_test_init_internal(TestInputVisitorData *data,
                                                 bool keyval,
                                                 const char *json_string,
                                                 va_list *ap)
{
    visitor_input_teardown(data, NULL);

    data->obj = qobject_from_jsonv(json_string, ap, &error_abort);
    g_assert(data->obj);

    if (keyval) {
        data->qiv = qobject_input_visitor_new_keyval(data->obj);
    } else {
        data->qiv = qobject_input_visitor_new(data->obj);
    }
    g_assert(data->qiv);
    return data->qiv;
}

static GCC_FMT_ATTR(3, 4)
Visitor *visitor_input_test_init_full(TestInputVisitorData *data,
                                      bool keyval,
                                      const char *json_string, ...)
{
    Visitor *v;
    va_list ap;

    va_start(ap, json_string);
    v = visitor_input_test_init_internal(data, keyval, json_string, &ap);
    va_end(ap);
    return v;
}

static GCC_FMT_ATTR(2, 3)
Visitor *visitor_input_test_init(TestInputVisitorData *data,
                                 const char *json_string, ...)
{
    Visitor *v;
    va_list ap;

    va_start(ap, json_string);
    v = visitor_input_test_init_internal(data, false, json_string, &ap);
    va_end(ap);
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
    return visitor_input_test_init_internal(data, false, json_string, NULL);
}

static void test_visitor_in_int(TestInputVisitorData *data,
                                const void *unused)
{
    int64_t res = 0;
    int value = -42;
    Visitor *v;

    v = visitor_input_test_init(data, "%d", value);

    visit_type_int(v, NULL, &res, &error_abort);
    g_assert_cmpint(res, ==, value);
}

static void test_visitor_in_uint(TestInputVisitorData *data,
                                const void *unused)
{
    Error *err = NULL;
    uint64_t res = 0;
    int value = 42;
    Visitor *v;

    v = visitor_input_test_init(data, "%d", value);

    visit_type_uint64(v, NULL, &res, &error_abort);
    g_assert_cmpuint(res, ==, (uint64_t)value);

    /* BUG: value between INT64_MIN and -1 accepted modulo 2^64 */

    v = visitor_input_test_init(data, "%d", -value);

    visit_type_uint64(v, NULL, &res, &error_abort);
    g_assert_cmpuint(res, ==, (uint64_t)-value);

    /* BUG: value between INT64_MAX+1 and UINT64_MAX rejected */

    v = visitor_input_test_init(data, "18446744073709551574");

    visit_type_uint64(v, NULL, &res, &err);
    error_free_or_abort(&err);
}

static void test_visitor_in_int_overflow(TestInputVisitorData *data,
                                         const void *unused)
{
    int64_t res = 0;
    Error *err = NULL;
    Visitor *v;

    /* this will overflow a Qint/int64, so should be deserialized into
     * a QFloat/double field instead, leading to an error if we pass it
     * to visit_type_int. confirm this.
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

    v = visitor_input_test_init_full(data, true, "\"3.14\"");

    visit_type_number(v, NULL, &res, &error_abort);
    g_assert_cmpfloat(res, ==, value);
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

    for (i = 0; EnumOne_lookup[i]; i++) {
        EnumOne res = -1;

        v = visitor_input_test_init(data, "%s", EnumOne_lookup[i]);

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
    UserDefTwo *udp = NULL;
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
    g_assert(udp->dict1->has_dict3 == false);

    qapi_free_UserDefTwo(udp);
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
        char string[12];

        snprintf(string, sizeof(string), "string%d", i);
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

static void test_visitor_in_any(TestInputVisitorData *data,
                                const void *unused)
{
    QObject *res = NULL;
    Visitor *v;
    QInt *qint;
    QBool *qbool;
    QString *qstring;
    QDict *qdict;
    QObject *qobj;

    v = visitor_input_test_init(data, "-42");
    visit_type_any(v, NULL, &res, &error_abort);
    qint = qobject_to_qint(res);
    g_assert(qint);
    g_assert_cmpint(qint_get_int(qint), ==, -42);
    qobject_decref(res);

    v = visitor_input_test_init(data, "{ 'integer': -42, 'boolean': true, 'string': 'foo' }");
    visit_type_any(v, NULL, &res, &error_abort);
    qdict = qobject_to_qdict(res);
    g_assert(qdict && qdict_size(qdict) == 3);
    qobj = qdict_get(qdict, "integer");
    g_assert(qobj);
    qint = qobject_to_qint(qobj);
    g_assert(qint);
    g_assert_cmpint(qint_get_int(qint), ==, -42);
    qobj = qdict_get(qdict, "boolean");
    g_assert(qobj);
    qbool = qobject_to_qbool(qobj);
    g_assert(qbool);
    g_assert(qbool_get_bool(qbool) == true);
    qobj = qdict_get(qdict, "string");
    g_assert(qobj);
    qstring = qobject_to_qstring(qobj);
    g_assert(qstring);
    g_assert_cmpstr(qstring_get_str(qstring), ==, "foo");
    qobject_decref(res);
}

static void test_visitor_in_null(TestInputVisitorData *data,
                                 const void *unused)
{
    Visitor *v;
    Error *err = NULL;
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
    visit_type_null(v, "a", &error_abort);
    visit_type_null(v, "b", &err);
    error_free_or_abort(&err);
    visit_type_str(v, "c", &tmp, &err);
    g_assert(!tmp);
    error_free_or_abort(&err);
    visit_check_struct(v, &error_abort);
    visit_end_struct(v, NULL);
}

static void test_visitor_in_union_flat(TestInputVisitorData *data,
                                       const void *unused)
{
    Visitor *v;
    UserDefFlatUnion *tmp;
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

    qapi_free_UserDefFlatUnion(tmp);
}

static void test_visitor_in_alternate(TestInputVisitorData *data,
                                      const void *unused)
{
    Visitor *v;
    Error *err = NULL;
    UserDefAlternate *tmp;
    WrapAlternate *wrap;

    v = visitor_input_test_init(data, "42");
    visit_type_UserDefAlternate(v, NULL, &tmp, &error_abort);
    g_assert_cmpint(tmp->type, ==, QTYPE_QINT);
    g_assert_cmpint(tmp->u.i, ==, 42);
    qapi_free_UserDefAlternate(tmp);

    v = visitor_input_test_init(data, "'string'");
    visit_type_UserDefAlternate(v, NULL, &tmp, &error_abort);
    g_assert_cmpint(tmp->type, ==, QTYPE_QSTRING);
    g_assert_cmpstr(tmp->u.s, ==, "string");
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

    v = visitor_input_test_init(data, "false");
    visit_type_UserDefAlternate(v, NULL, &tmp, &err);
    error_free_or_abort(&err);
    qapi_free_UserDefAlternate(tmp);

    v = visitor_input_test_init(data, "{ 'alt': 42 }");
    visit_type_WrapAlternate(v, NULL, &wrap, &error_abort);
    g_assert_cmpint(wrap->alt->type, ==, QTYPE_QINT);
    g_assert_cmpint(wrap->alt->u.i, ==, 42);
    qapi_free_WrapAlternate(wrap);

    v = visitor_input_test_init(data, "{ 'alt': 'string' }");
    visit_type_WrapAlternate(v, NULL, &wrap, &error_abort);
    g_assert_cmpint(wrap->alt->type, ==, QTYPE_QSTRING);
    g_assert_cmpstr(wrap->alt->u.s, ==, "string");
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
    AltStrBool *asb;
    AltStrNum *asn;
    AltNumStr *ans;
    AltStrInt *asi;
    AltIntNum *ain;
    AltNumInt *ani;

    /* Parsing an int */

    v = visitor_input_test_init(data, "42");
    visit_type_AltStrBool(v, NULL, &asb, &err);
    error_free_or_abort(&err);
    qapi_free_AltStrBool(asb);

    v = visitor_input_test_init(data, "42");
    visit_type_AltStrNum(v, NULL, &asn, &error_abort);
    g_assert_cmpint(asn->type, ==, QTYPE_QFLOAT);
    g_assert_cmpfloat(asn->u.n, ==, 42);
    qapi_free_AltStrNum(asn);

    v = visitor_input_test_init(data, "42");
    visit_type_AltNumStr(v, NULL, &ans, &error_abort);
    g_assert_cmpint(ans->type, ==, QTYPE_QFLOAT);
    g_assert_cmpfloat(ans->u.n, ==, 42);
    qapi_free_AltNumStr(ans);

    v = visitor_input_test_init(data, "42");
    visit_type_AltStrInt(v, NULL, &asi, &error_abort);
    g_assert_cmpint(asi->type, ==, QTYPE_QINT);
    g_assert_cmpint(asi->u.i, ==, 42);
    qapi_free_AltStrInt(asi);

    v = visitor_input_test_init(data, "42");
    visit_type_AltIntNum(v, NULL, &ain, &error_abort);
    g_assert_cmpint(ain->type, ==, QTYPE_QINT);
    g_assert_cmpint(ain->u.i, ==, 42);
    qapi_free_AltIntNum(ain);

    v = visitor_input_test_init(data, "42");
    visit_type_AltNumInt(v, NULL, &ani, &error_abort);
    g_assert_cmpint(ani->type, ==, QTYPE_QINT);
    g_assert_cmpint(ani->u.i, ==, 42);
    qapi_free_AltNumInt(ani);

    /* Parsing a double */

    v = visitor_input_test_init(data, "42.5");
    visit_type_AltStrBool(v, NULL, &asb, &err);
    error_free_or_abort(&err);
    qapi_free_AltStrBool(asb);

    v = visitor_input_test_init(data, "42.5");
    visit_type_AltStrNum(v, NULL, &asn, &error_abort);
    g_assert_cmpint(asn->type, ==, QTYPE_QFLOAT);
    g_assert_cmpfloat(asn->u.n, ==, 42.5);
    qapi_free_AltStrNum(asn);

    v = visitor_input_test_init(data, "42.5");
    visit_type_AltNumStr(v, NULL, &ans, &error_abort);
    g_assert_cmpint(ans->type, ==, QTYPE_QFLOAT);
    g_assert_cmpfloat(ans->u.n, ==, 42.5);
    qapi_free_AltNumStr(ans);

    v = visitor_input_test_init(data, "42.5");
    visit_type_AltStrInt(v, NULL, &asi, &err);
    error_free_or_abort(&err);
    qapi_free_AltStrInt(asi);

    v = visitor_input_test_init(data, "42.5");
    visit_type_AltIntNum(v, NULL, &ain, &error_abort);
    g_assert_cmpint(ain->type, ==, QTYPE_QFLOAT);
    g_assert_cmpfloat(ain->u.n, ==, 42.5);
    qapi_free_AltIntNum(ain);

    v = visitor_input_test_init(data, "42.5");
    visit_type_AltNumInt(v, NULL, &ani, &error_abort);
    g_assert_cmpint(ani->type, ==, QTYPE_QFLOAT);
    g_assert_cmpfloat(ani->u.n, ==, 42.5);
    qapi_free_AltNumInt(ani);
}

static void test_native_list_integer_helper(TestInputVisitorData *data,
                                            const void *unused,
                                            UserDefNativeListUnionKind kind)
{
    UserDefNativeListUnion *cvalue = NULL;
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

    visit_type_UserDefNativeListUnion(v, NULL, &cvalue, &error_abort);
    g_assert(cvalue != NULL);
    g_assert_cmpint(cvalue->type, ==, kind);

    switch (kind) {
    case USER_DEF_NATIVE_LIST_UNION_KIND_INTEGER: {
        intList *elem = NULL;
        for (i = 0, elem = cvalue->u.integer.data;
             elem; elem = elem->next, i++) {
            g_assert_cmpint(elem->value, ==, i);
        }
        break;
    }
    case USER_DEF_NATIVE_LIST_UNION_KIND_S8: {
        int8List *elem = NULL;
        for (i = 0, elem = cvalue->u.s8.data; elem; elem = elem->next, i++) {
            g_assert_cmpint(elem->value, ==, i);
        }
        break;
    }
    case USER_DEF_NATIVE_LIST_UNION_KIND_S16: {
        int16List *elem = NULL;
        for (i = 0, elem = cvalue->u.s16.data; elem; elem = elem->next, i++) {
            g_assert_cmpint(elem->value, ==, i);
        }
        break;
    }
    case USER_DEF_NATIVE_LIST_UNION_KIND_S32: {
        int32List *elem = NULL;
        for (i = 0, elem = cvalue->u.s32.data; elem; elem = elem->next, i++) {
            g_assert_cmpint(elem->value, ==, i);
        }
        break;
    }
    case USER_DEF_NATIVE_LIST_UNION_KIND_S64: {
        int64List *elem = NULL;
        for (i = 0, elem = cvalue->u.s64.data; elem; elem = elem->next, i++) {
            g_assert_cmpint(elem->value, ==, i);
        }
        break;
    }
    case USER_DEF_NATIVE_LIST_UNION_KIND_U8: {
        uint8List *elem = NULL;
        for (i = 0, elem = cvalue->u.u8.data; elem; elem = elem->next, i++) {
            g_assert_cmpint(elem->value, ==, i);
        }
        break;
    }
    case USER_DEF_NATIVE_LIST_UNION_KIND_U16: {
        uint16List *elem = NULL;
        for (i = 0, elem = cvalue->u.u16.data; elem; elem = elem->next, i++) {
            g_assert_cmpint(elem->value, ==, i);
        }
        break;
    }
    case USER_DEF_NATIVE_LIST_UNION_KIND_U32: {
        uint32List *elem = NULL;
        for (i = 0, elem = cvalue->u.u32.data; elem; elem = elem->next, i++) {
            g_assert_cmpint(elem->value, ==, i);
        }
        break;
    }
    case USER_DEF_NATIVE_LIST_UNION_KIND_U64: {
        uint64List *elem = NULL;
        for (i = 0, elem = cvalue->u.u64.data; elem; elem = elem->next, i++) {
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

    visit_type_UserDefNativeListUnion(v, NULL, &cvalue, &error_abort);
    g_assert(cvalue != NULL);
    g_assert_cmpint(cvalue->type, ==, USER_DEF_NATIVE_LIST_UNION_KIND_BOOLEAN);

    for (i = 0, elem = cvalue->u.boolean.data; elem; elem = elem->next, i++) {
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

    visit_type_UserDefNativeListUnion(v, NULL, &cvalue, &error_abort);
    g_assert(cvalue != NULL);
    g_assert_cmpint(cvalue->type, ==, USER_DEF_NATIVE_LIST_UNION_KIND_STRING);

    for (i = 0, elem = cvalue->u.string.data; elem; elem = elem->next, i++) {
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

    visit_type_UserDefNativeListUnion(v, NULL, &cvalue, &error_abort);
    g_assert(cvalue != NULL);
    g_assert_cmpint(cvalue->type, ==, USER_DEF_NATIVE_LIST_UNION_KIND_NUMBER);

    for (i = 0, elem = cvalue->u.number.data; elem; elem = elem->next, i++) {
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
    visit_start_alternate(v, "alternate", &alt, sizeof(*alt), false, &err);
    error_free_or_abort(&err);
    visit_optional(v, "optional", &present);
    g_assert(!present);
    visit_type_enum(v, "enum", &en, EnumOne_lookup, &err);
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
    visit_type_null(v, "null", &err);
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

static void test_visitor_in_fail_union_native_list(TestInputVisitorData *data,
                                                   const void *unused)
{
    UserDefNativeListUnion *tmp = NULL;
    Error *err = NULL;
    Visitor *v;

    v = visitor_input_test_init(data,
                                "{ 'type': 'integer', 'data' : [ 'string' ] }");

    visit_type_UserDefNativeListUnion(v, NULL, &tmp, &err);
    error_free_or_abort(&err);
    g_assert(!tmp);
}

static void test_visitor_in_fail_union_flat(TestInputVisitorData *data,
                                            const void *unused)
{
    UserDefFlatUnion *tmp = NULL;
    Error *err = NULL;
    Visitor *v;

    v = visitor_input_test_init(data, "{ 'string': 'c', 'integer': 41, 'boolean': true }");

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
                                              const char *schema_json)
{
    SchemaInfoList *schema = NULL;
    Visitor *v;

    v = visitor_input_test_init_raw(data, schema_json);

    visit_type_SchemaInfoList(v, NULL, &schema, &error_abort);
    g_assert(schema);

    qapi_free_SchemaInfoList(schema);
}

static void test_visitor_in_qmp_introspect(TestInputVisitorData *data,
                                           const void *unused)
{
    do_test_visitor_in_qmp_introspect(data, test_qmp_schema_json);
    do_test_visitor_in_qmp_introspect(data, qmp_schema_json);
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
    input_visitor_test_add("/visitor/input/list",
                           NULL, test_visitor_in_list);
    input_visitor_test_add("/visitor/input/any",
                           NULL, test_visitor_in_any);
    input_visitor_test_add("/visitor/input/null",
                           NULL, test_visitor_in_null);
    input_visitor_test_add("/visitor/input/union-flat",
                           NULL, test_visitor_in_union_flat);
    input_visitor_test_add("/visitor/input/alternate",
                           NULL, test_visitor_in_alternate);
    input_visitor_test_add("/visitor/input/errors",
                           NULL, test_visitor_in_errors);
    input_visitor_test_add("/visitor/input/wrong-type",
                           NULL, test_visitor_in_wrong_type);
    input_visitor_test_add("/visitor/input/alternate-number",
                           NULL, test_visitor_in_alternate_number);
    input_visitor_test_add("/visitor/input/native_list/int",
                           NULL, test_visitor_in_native_list_int);
    input_visitor_test_add("/visitor/input/native_list/int8",
                           NULL, test_visitor_in_native_list_int8);
    input_visitor_test_add("/visitor/input/native_list/int16",
                           NULL, test_visitor_in_native_list_int16);
    input_visitor_test_add("/visitor/input/native_list/int32",
                           NULL, test_visitor_in_native_list_int32);
    input_visitor_test_add("/visitor/input/native_list/int64",
                           NULL, test_visitor_in_native_list_int64);
    input_visitor_test_add("/visitor/input/native_list/uint8",
                           NULL, test_visitor_in_native_list_uint8);
    input_visitor_test_add("/visitor/input/native_list/uint16",
                           NULL, test_visitor_in_native_list_uint16);
    input_visitor_test_add("/visitor/input/native_list/uint32",
                           NULL, test_visitor_in_native_list_uint32);
    input_visitor_test_add("/visitor/input/native_list/uint64",
                           NULL, test_visitor_in_native_list_uint64);
    input_visitor_test_add("/visitor/input/native_list/bool",
                           NULL, test_visitor_in_native_list_bool);
    input_visitor_test_add("/visitor/input/native_list/str",
                           NULL, test_visitor_in_native_list_string);
    input_visitor_test_add("/visitor/input/native_list/number",
                           NULL, test_visitor_in_native_list_number);
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
    input_visitor_test_add("/visitor/input/fail/union-native-list",
                           NULL, test_visitor_in_fail_union_native_list);
    input_visitor_test_add("/visitor/input/qmp-introspect",
                           NULL, test_visitor_in_qmp_introspect);

    g_test_run();

    return 0;
}
