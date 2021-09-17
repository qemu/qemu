/*
 * QAPI Clone Visitor unit-tests.
 *
 * Copyright (C) 2016 Red Hat Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qemu-common.h"
#include "qapi/clone-visitor.h"
#include "test-qapi-visit.h"

static void test_clone_struct(void)
{
    UserDefOne *src, *dst;

    src = g_new0(UserDefOne, 1);
    src->integer = 42;
    src->string = g_strdup("Hello");
    src->has_enum1 = false;
    src->enum1 = ENUM_ONE_VALUE2;

    dst = QAPI_CLONE(UserDefOne, src);
    g_assert(dst);
    g_assert_cmpint(dst->integer, ==, 42);
    g_assert(dst->string != src->string);
    g_assert_cmpstr(dst->string, ==, "Hello");
    g_assert_cmpint(dst->has_enum1, ==, false);
    /* Our implementation does this, but it is not required:
    g_assert_cmpint(dst->enum1, ==, ENUM_ONE_VALUE2);
    */

    qapi_free_UserDefOne(src);
    qapi_free_UserDefOne(dst);
}

static void test_clone_alternate(void)
{
    AltEnumBool *b_src, *s_src, *b_dst, *s_dst;

    b_src = g_new0(AltEnumBool, 1);
    b_src->type = QTYPE_QBOOL;
    b_src->u.b = true;
    s_src = g_new0(AltEnumBool, 1);
    s_src->type = QTYPE_QSTRING;
    s_src->u.e = ENUM_ONE_VALUE1;

    b_dst = QAPI_CLONE(AltEnumBool, b_src);
    g_assert(b_dst);
    g_assert_cmpint(b_dst->type, ==, b_src->type);
    g_assert_cmpint(b_dst->u.b, ==, b_src->u.b);
    s_dst = QAPI_CLONE(AltEnumBool, s_src);
    g_assert(s_dst);
    g_assert_cmpint(s_dst->type, ==, s_src->type);
    g_assert_cmpint(s_dst->u.e, ==, s_src->u.e);

    qapi_free_AltEnumBool(b_src);
    qapi_free_AltEnumBool(s_src);
    qapi_free_AltEnumBool(b_dst);
    qapi_free_AltEnumBool(s_dst);
}

static void test_clone_list(void)
{
    uint8List *src = NULL, *dst;
    uint8List *tmp = NULL;
    int i;

    /* Build list in reverse */
    for (i = 10; i; i--) {
        QAPI_LIST_PREPEND(src, i);
    }

    dst = QAPI_CLONE(uint8List, src);
    for (tmp = dst, i = 1; i <= 10; i++) {
        g_assert(tmp);
        g_assert_cmpint(tmp->value, ==, i);
        tmp = tmp->next;
    }
    g_assert(!tmp);

    qapi_free_uint8List(src);
    qapi_free_uint8List(dst);
}

static void test_clone_empty(void)
{
    Empty2 *src, *dst;

    src = g_new0(Empty2, 1);
    dst = QAPI_CLONE(Empty2, src);
    g_assert(dst);
    qapi_free_Empty2(src);
    qapi_free_Empty2(dst);
}

static void test_clone_complex1(void)
{
    UserDefFlatUnion *src, *dst;

    src = g_new0(UserDefFlatUnion, 1);
    src->integer = 123;
    src->string = g_strdup("abc");
    src->enum1 = ENUM_ONE_VALUE1;
    src->u.value1.boolean = true;

    dst = QAPI_CLONE(UserDefFlatUnion, src);
    g_assert(dst);

    g_assert_cmpint(dst->integer, ==, 123);
    g_assert_cmpstr(dst->string, ==, "abc");
    g_assert_cmpint(dst->enum1, ==, ENUM_ONE_VALUE1);
    g_assert(dst->u.value1.boolean);
    g_assert(!dst->u.value1.has_a_b);
    g_assert_cmpint(dst->u.value1.a_b, ==, 0);

    qapi_free_UserDefFlatUnion(src);
    qapi_free_UserDefFlatUnion(dst);
}

static void test_clone_complex2(void)
{
    WrapAlternate *src, *dst;

    src = g_new0(WrapAlternate, 1);
    src->alt = g_new(UserDefAlternate, 1);
    src->alt->type = QTYPE_QDICT;
    src->alt->u.udfu.integer = 42;
    /* Clone intentionally converts NULL into "" for strings */
    src->alt->u.udfu.string = NULL;
    src->alt->u.udfu.enum1 = ENUM_ONE_VALUE3;
    src->alt->u.udfu.u.value3.intb = 99;
    src->alt->u.udfu.u.value3.has_a_b = true;
    src->alt->u.udfu.u.value3.a_b = true;

    dst = QAPI_CLONE(WrapAlternate, src);
    g_assert(dst);
    g_assert(dst->alt);
    g_assert_cmpint(dst->alt->type, ==, QTYPE_QDICT);
    g_assert_cmpint(dst->alt->u.udfu.integer, ==, 42);
    g_assert_cmpstr(dst->alt->u.udfu.string, ==, "");
    g_assert_cmpint(dst->alt->u.udfu.enum1, ==, ENUM_ONE_VALUE3);
    g_assert_cmpint(dst->alt->u.udfu.u.value3.intb, ==, 99);
    g_assert_cmpint(dst->alt->u.udfu.u.value3.has_a_b, ==, true);
    g_assert_cmpint(dst->alt->u.udfu.u.value3.a_b, ==, true);

    qapi_free_WrapAlternate(src);
    qapi_free_WrapAlternate(dst);
}

static void test_clone_complex3(void)
{
    UserDefOneList *src, *dst, *tail;
    UserDefOne *elt;

    src = NULL;
    elt = g_new0(UserDefOne, 1);
    elt->integer = 3;
    elt->string = g_strdup("three");
    elt->has_enum1 = true;
    elt->enum1 = ENUM_ONE_VALUE3;
    QAPI_LIST_PREPEND(src, elt);
    elt = g_new0(UserDefOne, 1);
    elt->integer = 2;
    elt->string = g_strdup("two");
    QAPI_LIST_PREPEND(src, elt);
    elt = g_new0(UserDefOne, 1);
    elt->integer = 1;
    elt->string = g_strdup("one");
    QAPI_LIST_PREPEND(src, elt);

    dst = QAPI_CLONE(UserDefOneList, src);

    g_assert(dst);
    tail = dst;
    elt = tail->value;
    g_assert_cmpint(elt->integer, ==, 1);
    g_assert_cmpstr(elt->string, ==, "one");
    g_assert(!elt->has_enum1);
    tail = tail->next;
    elt = tail->value;
    g_assert_cmpint(elt->integer, ==, 2);
    g_assert_cmpstr(elt->string, ==, "two");
    g_assert(!elt->has_enum1);
    tail = tail->next;
    elt = tail->value;
    g_assert_cmpint(elt->integer, ==, 3);
    g_assert_cmpstr(elt->string, ==, "three");
    g_assert(elt->has_enum1);
    g_assert_cmpint(elt->enum1, ==, ENUM_ONE_VALUE3);
    g_assert(!tail->next);

    qapi_free_UserDefOneList(src);
    qapi_free_UserDefOneList(dst);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/visitor/clone/struct", test_clone_struct);
    g_test_add_func("/visitor/clone/alternate", test_clone_alternate);
    g_test_add_func("/visitor/clone/list", test_clone_list);
    g_test_add_func("/visitor/clone/empty", test_clone_empty);
    g_test_add_func("/visitor/clone/complex1", test_clone_complex1);
    g_test_add_func("/visitor/clone/complex2", test_clone_complex2);
    g_test_add_func("/visitor/clone/complex3", test_clone_complex3);

    return g_test_run();
}
