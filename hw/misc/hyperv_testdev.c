/*
 * QEMU KVM Hyper-V test device to support Hyper-V kvm-unit-tests
 *
 * Copyright (C) 2015 Andrey Smetanin <asmetanin@virtuozzo.com>
 *
 * Authors:
 *  Andrey Smetanin <asmetanin@virtuozzo.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/queue.h"
#include "hw/qdev.h"
#include "hw/isa/isa.h"
#include "target/i386/hyperv.h"

typedef struct TestSintRoute {
    QLIST_ENTRY(TestSintRoute) le;
    uint8_t vp_index;
    uint8_t sint;
    HvSintRoute *sint_route;
} TestSintRoute;

struct HypervTestDev {
    ISADevice parent_obj;
    MemoryRegion sint_control;
    QLIST_HEAD(, TestSintRoute) sint_routes;
};
typedef struct HypervTestDev HypervTestDev;

#define TYPE_HYPERV_TEST_DEV "hyperv-testdev"
#define HYPERV_TEST_DEV(obj) \
        OBJECT_CHECK(HypervTestDev, (obj), TYPE_HYPERV_TEST_DEV)

enum {
    HV_TEST_DEV_SINT_ROUTE_CREATE = 1,
    HV_TEST_DEV_SINT_ROUTE_DESTROY,
    HV_TEST_DEV_SINT_ROUTE_SET_SINT
};

static void sint_route_create(HypervTestDev *dev,
                              uint8_t vp_index, uint8_t sint)
{
    TestSintRoute *sint_route;

    sint_route = g_new0(TestSintRoute, 1);
    assert(sint_route);

    sint_route->vp_index = vp_index;
    sint_route->sint = sint;

    sint_route->sint_route = kvm_hv_sint_route_create(vp_index, sint, NULL, NULL);
    assert(sint_route->sint_route);

    QLIST_INSERT_HEAD(&dev->sint_routes, sint_route, le);
}

static TestSintRoute *sint_route_find(HypervTestDev *dev,
                                      uint8_t vp_index, uint8_t sint)
{
    TestSintRoute *sint_route;

    QLIST_FOREACH(sint_route, &dev->sint_routes, le) {
        if (sint_route->vp_index == vp_index && sint_route->sint == sint) {
            return sint_route;
        }
    }
    assert(false);
    return NULL;
}

static void sint_route_destroy(HypervTestDev *dev,
                               uint8_t vp_index, uint8_t sint)
{
    TestSintRoute *sint_route;

    sint_route = sint_route_find(dev, vp_index, sint);
    QLIST_REMOVE(sint_route, le);
    kvm_hv_sint_route_destroy(sint_route->sint_route);
    g_free(sint_route);
}

static void sint_route_set_sint(HypervTestDev *dev,
                                uint8_t vp_index, uint8_t sint)
{
    TestSintRoute *sint_route;

    sint_route = sint_route_find(dev, vp_index, sint);

    kvm_hv_sint_route_set_sint(sint_route->sint_route);
}

static uint64_t hv_test_dev_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void hv_test_dev_write(void *opaque, hwaddr addr, uint64_t data,
                                uint32_t len)
{
    HypervTestDev *dev = HYPERV_TEST_DEV(opaque);
    uint8_t sint = data & 0xFF;
    uint8_t vp_index = (data >> 8ULL) & 0xFF;
    uint8_t ctl = (data >> 16ULL) & 0xFF;

    switch (ctl) {
    case HV_TEST_DEV_SINT_ROUTE_CREATE:
        sint_route_create(dev, vp_index, sint);
        break;
    case HV_TEST_DEV_SINT_ROUTE_DESTROY:
        sint_route_destroy(dev, vp_index, sint);
        break;
    case HV_TEST_DEV_SINT_ROUTE_SET_SINT:
        sint_route_set_sint(dev, vp_index, sint);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps synic_test_sint_ops = {
    .read = hv_test_dev_read,
    .write = hv_test_dev_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void hv_test_dev_realizefn(DeviceState *d, Error **errp)
{
    ISADevice *isa = ISA_DEVICE(d);
    HypervTestDev *dev = HYPERV_TEST_DEV(d);
    MemoryRegion *io = isa_address_space_io(isa);

    QLIST_INIT(&dev->sint_routes);
    memory_region_init_io(&dev->sint_control, OBJECT(dev),
                          &synic_test_sint_ops, dev,
                          "hyperv-testdev-ctl", 4);
    memory_region_add_subregion(io, 0x3000, &dev->sint_control);
}

static void hv_test_dev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->realize = hv_test_dev_realizefn;
}

static const TypeInfo hv_test_dev_info = {
    .name           = TYPE_HYPERV_TEST_DEV,
    .parent         = TYPE_ISA_DEVICE,
    .instance_size  = sizeof(HypervTestDev),
    .class_init     = hv_test_dev_class_init,
};

static void hv_test_dev_register_types(void)
{
    type_register_static(&hv_test_dev_info);
}
type_init(hv_test_dev_register_types);
