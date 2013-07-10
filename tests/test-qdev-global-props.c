/*
 *  Test code for qdev global-properties handling
 *
 *  Copyright (c) 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <glib.h>
#include <stdint.h>

#include "hw/qdev.h"


#define TYPE_STATIC_PROPS "static_prop_type"
#define STATIC_TYPE(obj) \
    OBJECT_CHECK(MyType, (obj), TYPE_STATIC_PROPS)

#define PROP_DEFAULT 100

typedef struct MyType {
    DeviceState parent_obj;

    uint32_t prop1;
    uint32_t prop2;
} MyType;

static Property static_props[] = {
    DEFINE_PROP_UINT32("prop1", MyType, prop1, PROP_DEFAULT),
    DEFINE_PROP_UINT32("prop2", MyType, prop2, PROP_DEFAULT),
    DEFINE_PROP_END_OF_LIST()
};

static void static_prop_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = NULL;
    dc->props = static_props;
}

static const TypeInfo static_prop_type = {
    .name = TYPE_STATIC_PROPS,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(MyType),
    .class_init = static_prop_class_init,
};

/* Test simple static property setting to default value */
static void test_static_prop(void)
{
    MyType *mt;

    mt = STATIC_TYPE(object_new(TYPE_STATIC_PROPS));
    qdev_init_nofail(DEVICE(mt));

    g_assert_cmpuint(mt->prop1, ==, PROP_DEFAULT);
}

/* Test setting of static property using global properties */
static void test_static_globalprop(void)
{
    MyType *mt;
    static GlobalProperty props[] = {
        { TYPE_STATIC_PROPS, "prop1", "200" },
        {}
    };

    qdev_prop_register_global_list(props);

    mt = STATIC_TYPE(object_new(TYPE_STATIC_PROPS));
    qdev_init_nofail(DEVICE(mt));

    g_assert_cmpuint(mt->prop1, ==, 200);
    g_assert_cmpuint(mt->prop2, ==, PROP_DEFAULT);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    module_call_init(MODULE_INIT_QOM);
    type_register_static(&static_prop_type);

    g_test_add_func("/qdev/properties/static/default", test_static_prop);
    g_test_add_func("/qdev/properties/static/global", test_static_globalprop);

    g_test_run();

    return 0;
}
