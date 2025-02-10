/*
 * QAPI Forwarding Visitor unit-tests.
 *
 * Copyright (C) 2021 Red Hat Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qapi/forward-visitor.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/error.h"
#include "qobject/qobject.h"
#include "qobject/qdict.h"
#include "test-qapi-visit.h"
#include "qemu/keyval.h"

typedef bool GenericVisitor (Visitor *, const char *, void **, Error **);
#define CAST_VISIT_TYPE(fn) ((GenericVisitor *)(fn))

/*
 * Parse @srcstr and wrap it with a ForwardFieldVisitor converting "src" to
 * "dst". Check that visiting the result with "src" name fails, and return
 * the result of visiting "dst".
 */
static void *visit_with_forward(const char *srcstr, GenericVisitor *fn)
{
    bool help = false;
    QDict *src = keyval_parse(srcstr, NULL, &help, &error_abort);
    Visitor *v, *alias_v;
    Error *err = NULL;
    void *result = NULL;

    v = qobject_input_visitor_new_keyval(QOBJECT(src));
    visit_start_struct(v, NULL, NULL, 0, &error_abort);

    alias_v = visitor_forward_field(v, "dst", "src");
    fn(alias_v, "src", &result, &err);
    error_free_or_abort(&err);
    assert(!result);
    fn(alias_v, "dst", &result, &err);
    assert(err == NULL);
    visit_free(alias_v);

    visit_end_struct(v, NULL);
    visit_free(v);
    qobject_unref(QOBJECT(src));
    return result;
}

static void test_forward_any(void)
{
    QObject *src = visit_with_forward("src.integer=42,src.string=Hello,src.enum1=value2",
                                      CAST_VISIT_TYPE(visit_type_any));
    Visitor *v = qobject_input_visitor_new_keyval(src);
    Error *err = NULL;
    UserDefOne *dst;

    visit_type_UserDefOne(v, NULL, &dst, &err);
    assert(err == NULL);
    visit_free(v);

    g_assert_cmpint(dst->integer, ==, 42);
    g_assert_cmpstr(dst->string, ==, "Hello");
    g_assert_cmpint(dst->has_enum1, ==, true);
    g_assert_cmpint(dst->enum1, ==, ENUM_ONE_VALUE2);
    qapi_free_UserDefOne(dst);
    qobject_unref(QOBJECT(src));
}

static void test_forward_size(void)
{
    /*
     * visit_type_size does not return a pointer, so visit_with_forward
     * cannot be used.
     */
    bool help = false;
    QDict *src = keyval_parse("src=1.5M", NULL, &help, &error_abort);
    Visitor *v, *alias_v;
    Error *err = NULL;
    uint64_t result = 0;

    v = qobject_input_visitor_new_keyval(QOBJECT(src));
    visit_start_struct(v, NULL, NULL, 0, &error_abort);

    alias_v = visitor_forward_field(v, "dst", "src");
    visit_type_size(alias_v, "src", &result, &err);
    error_free_or_abort(&err);
    visit_type_size(alias_v, "dst", &result, &err);
    assert(result == 3 << 19);
    assert(err == NULL);
    visit_free(alias_v);

    visit_end_struct(v, NULL);
    visit_free(v);
    qobject_unref(QOBJECT(src));
}

static void test_forward_number(void)
{
    /*
     * visit_type_number does not return a pointer, so visit_with_forward
     * cannot be used.
     */
    bool help = false;
    QDict *src = keyval_parse("src=1.5", NULL, &help, &error_abort);
    Visitor *v, *alias_v;
    Error *err = NULL;
    double result = 0.0;

    v = qobject_input_visitor_new_keyval(QOBJECT(src));
    visit_start_struct(v, NULL, NULL, 0, &error_abort);

    alias_v = visitor_forward_field(v, "dst", "src");
    visit_type_number(alias_v, "src", &result, &err);
    error_free_or_abort(&err);
    visit_type_number(alias_v, "dst", &result, &err);
    assert(result == 1.5);
    assert(err == NULL);
    visit_free(alias_v);

    visit_end_struct(v, NULL);
    visit_free(v);
    qobject_unref(QOBJECT(src));
}

static void test_forward_string(void)
{
    char *dst = visit_with_forward("src=Hello",
                                   CAST_VISIT_TYPE(visit_type_str));

    g_assert_cmpstr(dst, ==, "Hello");
    g_free(dst);
}

static void test_forward_struct(void)
{
    UserDefOne *dst = visit_with_forward("src.integer=42,src.string=Hello",
                                         CAST_VISIT_TYPE(visit_type_UserDefOne));

    g_assert_cmpint(dst->integer, ==, 42);
    g_assert_cmpstr(dst->string, ==, "Hello");
    g_assert_cmpint(dst->has_enum1, ==, false);
    qapi_free_UserDefOne(dst);
}

static void test_forward_alternate(void)
{
    AltStrObj *s_dst = visit_with_forward("src=hello",
                                          CAST_VISIT_TYPE(visit_type_AltStrObj));
    AltStrObj *o_dst = visit_with_forward("src.integer=42,src.boolean=true,src.string=world",
                                          CAST_VISIT_TYPE(visit_type_AltStrObj));

    g_assert_cmpint(s_dst->type, ==, QTYPE_QSTRING);
    g_assert_cmpstr(s_dst->u.s, ==, "hello");
    g_assert_cmpint(o_dst->type, ==, QTYPE_QDICT);
    g_assert_cmpint(o_dst->u.o.integer, ==, 42);
    g_assert_cmpint(o_dst->u.o.boolean, ==, true);
    g_assert_cmpstr(o_dst->u.o.string, ==, "world");

    qapi_free_AltStrObj(s_dst);
    qapi_free_AltStrObj(o_dst);
}

static void test_forward_list(void)
{
    uint8List *dst = visit_with_forward("src.0=1,src.1=2,src.2=3,src.3=4",
                                        CAST_VISIT_TYPE(visit_type_uint8List));
    uint8List *tmp;
    int i;

    for (tmp = dst, i = 1; i <= 4; i++) {
        g_assert(tmp);
        g_assert_cmpint(tmp->value, ==, i);
        tmp = tmp->next;
    }
    g_assert(!tmp);
    qapi_free_uint8List(dst);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/visitor/forward/struct", test_forward_struct);
    g_test_add_func("/visitor/forward/alternate", test_forward_alternate);
    g_test_add_func("/visitor/forward/string", test_forward_string);
    g_test_add_func("/visitor/forward/size", test_forward_size);
    g_test_add_func("/visitor/forward/number", test_forward_number);
    g_test_add_func("/visitor/forward/any", test_forward_any);
    g_test_add_func("/visitor/forward/list", test_forward_list);

    return g_test_run();
}
