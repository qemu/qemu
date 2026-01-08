#include "qemu/osdep.h"
#include "hw/core/qdev-properties.h"
#include "qom/object.h"
#include "qapi/error.h"
#include "qapi/visitor.h"


#define TYPE_MY_DEV "my-dev"
typedef struct MyDev MyDev;
DECLARE_INSTANCE_CHECKER(MyDev, STATIC_TYPE,
                         TYPE_MY_DEV)

struct MyDev {
    DeviceState parent_obj;

    uint32_t prop_u32;
    char *prop_string;
    uint32_t *prop_array_u32;
    uint32_t prop_array_u32_nb;
};

static const Property my_dev_props[] = {
    DEFINE_PROP_UINT32("u32", MyDev, prop_u32, 100),
    DEFINE_PROP_STRING("string", MyDev, prop_string),
    DEFINE_PROP_ARRAY("array-u32", MyDev, prop_array_u32_nb, prop_array_u32,
                     qdev_prop_uint32, uint32_t),
};

static void my_dev_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = NULL;
    device_class_set_props(dc, my_dev_props);
}

static const TypeInfo my_dev_type_info = {
    .name = TYPE_MY_DEV,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(MyDev),
    .class_init = my_dev_class_init,
};

/*
 * Initialize a fake machine, being prepared for future tests.
 *
 * Realization of anonymous qdev (with no parent object) requires both
 * the machine object and its "unattached" container to be at least present.
 */
static void test_init_machine(void)
{
    /* This is a fake machine - it doesn't need to be a machine object */
    Object *machine = object_property_add_new_container(
        object_get_root(), "machine");

    /* This container must exist for anonymous qdevs to realize() */
    object_property_add_new_container(machine, "unattached");
}

static void test_qdev_free_properties(void)
{
    MyDev *mt;

    mt = STATIC_TYPE(object_new(TYPE_MY_DEV));
    object_set_props(OBJECT(mt), &error_fatal,
                     "string", "something",
                     "array-u32", "12,13",
                     NULL);
    qdev_realize(DEVICE(mt), NULL, &error_fatal);

    g_assert_cmpuint(mt->prop_u32, ==, 100);
    g_assert_cmpstr(mt->prop_string, ==, "something");
    g_assert_cmpuint(mt->prop_array_u32_nb, ==, 2);
    g_assert_cmpuint(mt->prop_array_u32[0], ==, 12);
    g_assert_cmpuint(mt->prop_array_u32[1], ==, 13);

    object_unparent(OBJECT(mt));
    object_unref(mt);
}


int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    module_call_init(MODULE_INIT_QOM);
    type_register_static(&my_dev_type_info);
    test_init_machine();

    g_test_add_func("/qdev/free-properties",
                    test_qdev_free_properties);

    g_test_run();

    return 0;
}
