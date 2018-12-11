/*
 * QOM interface test.
 *
 * Copyright (C) 2013 Red Hat Inc.
 *
 * Authors:
 *  Igor Mammedov <imammedo@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */
#include "qemu/osdep.h"

#include "qom/object.h"
#include "qemu/module.h"


#define TYPE_TEST_IF "test-interface"
#define TEST_IF_CLASS(klass) \
     OBJECT_CLASS_CHECK(TestIfClass, (klass), TYPE_TEST_IF)
#define TEST_IF_GET_CLASS(obj) \
     OBJECT_GET_CLASS(TestIfClass, (obj), TYPE_TEST_IF)
#define TEST_IF(obj) \
     INTERFACE_CHECK(TestIf, (obj), TYPE_TEST_IF)

typedef struct TestIf TestIf;

typedef struct TestIfClass {
    InterfaceClass parent_class;

    uint32_t test;
} TestIfClass;

static const TypeInfo test_if_info = {
    .name          = TYPE_TEST_IF,
    .parent        = TYPE_INTERFACE,
    .class_size = sizeof(TestIfClass),
};

#define PATTERN 0xFAFBFCFD

static void test_class_init(ObjectClass *oc, void *data)
{
    TestIfClass *tc = TEST_IF_CLASS(oc);

    g_assert(tc);
    tc->test = PATTERN;
}

#define TYPE_DIRECT_IMPL "direct-impl"

static const TypeInfo direct_impl_info = {
    .name = TYPE_DIRECT_IMPL,
    .parent = TYPE_OBJECT,
    .class_init = test_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_TEST_IF },
        { }
    }
};

#define TYPE_INTERMEDIATE_IMPL "intermediate-impl"

static const TypeInfo intermediate_impl_info = {
    .name = TYPE_INTERMEDIATE_IMPL,
    .parent = TYPE_DIRECT_IMPL,
};

static void test_interface_impl(const char *type)
{
    Object *obj = object_new(type);
    TestIf *iobj = TEST_IF(obj);
    TestIfClass *ioc = TEST_IF_GET_CLASS(iobj);

    g_assert(iobj);
    g_assert(ioc->test == PATTERN);
    object_unref(obj);
}

static void interface_direct_test(void)
{
    test_interface_impl(TYPE_DIRECT_IMPL);
}

static void interface_intermediate_test(void)
{
    test_interface_impl(TYPE_INTERMEDIATE_IMPL);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    module_call_init(MODULE_INIT_QOM);
    type_register_static(&test_if_info);
    type_register_static(&direct_impl_info);
    type_register_static(&intermediate_impl_info);

    g_test_add_func("/qom/interface/direct_impl", interface_direct_test);
    g_test_add_func("/qom/interface/intermediate_impl",
                    interface_intermediate_test);

    return g_test_run();
}
