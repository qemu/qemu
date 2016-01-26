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
#include "hw/hw.h"
#include "hw/qdev.h"
#include "hw/isa/isa.h"
#include "sysemu/kvm.h"
#include "linux/kvm.h"
#include "target-i386/hyperv.h"
#include "kvm_i386.h"

#define HV_TEST_DEV_MAX_SINT_ROUTES 64

struct HypervTestDev {
    ISADevice parent_obj;
    MemoryRegion sint_control;
    HvSintRoute *sint_route[HV_TEST_DEV_MAX_SINT_ROUTES];
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

static int alloc_sint_route_index(HypervTestDev *dev)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(dev->sint_route); i++) {
        if (dev->sint_route[i] == NULL) {
            return i;
        }
    }
    return -1;
}

static void free_sint_route_index(HypervTestDev *dev, int i)
{
    assert(i >= 0 && i < ARRAY_SIZE(dev->sint_route));
    dev->sint_route[i] = NULL;
}

static int find_sint_route_index(HypervTestDev *dev, uint32_t vcpu_id,
                                 uint32_t sint)
{
    HvSintRoute *sint_route;
    int i;

    for (i = 0; i < ARRAY_SIZE(dev->sint_route); i++) {
        sint_route = dev->sint_route[i];
        if (sint_route && sint_route->vcpu_id == vcpu_id &&
            sint_route->sint == sint) {
            return i;
        }
    }
    return -1;
}

static void hv_synic_test_dev_control(HypervTestDev *dev, uint32_t ctl,
                                      uint32_t vcpu_id, uint32_t sint)
{
    int i;
    HvSintRoute *sint_route;

    switch (ctl) {
    case HV_TEST_DEV_SINT_ROUTE_CREATE:
        i = alloc_sint_route_index(dev);
        assert(i >= 0);
        sint_route = kvm_hv_sint_route_create(vcpu_id, sint, NULL);
        assert(sint_route);
        dev->sint_route[i] = sint_route;
        break;
    case HV_TEST_DEV_SINT_ROUTE_DESTROY:
        i = find_sint_route_index(dev, vcpu_id, sint);
        assert(i >= 0);
        sint_route = dev->sint_route[i];
        kvm_hv_sint_route_destroy(sint_route);
        free_sint_route_index(dev, i);
        break;
    case HV_TEST_DEV_SINT_ROUTE_SET_SINT:
        i = find_sint_route_index(dev, vcpu_id, sint);
        assert(i >= 0);
        sint_route = dev->sint_route[i];
        kvm_hv_sint_route_set_sint(sint_route);
        break;
    default:
        break;
    }
}

static void hv_test_dev_control(void *opaque, hwaddr addr, uint64_t data,
                                uint32_t len)
{
    HypervTestDev *dev = HYPERV_TEST_DEV(opaque);
    uint8_t ctl;

    ctl = (data >> 16ULL) & 0xFF;
    switch (ctl) {
    case HV_TEST_DEV_SINT_ROUTE_CREATE:
    case HV_TEST_DEV_SINT_ROUTE_DESTROY:
    case HV_TEST_DEV_SINT_ROUTE_SET_SINT: {
        uint8_t sint = data & 0xFF;
        uint8_t vcpu_id = (data >> 8ULL) & 0xFF;
        hv_synic_test_dev_control(dev, ctl, vcpu_id, sint);
        break;
    }
    default:
        break;
    }
}

static const MemoryRegionOps synic_test_sint_ops = {
    .write = hv_test_dev_control,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void hv_test_dev_realizefn(DeviceState *d, Error **errp)
{
    ISADevice *isa = ISA_DEVICE(d);
    HypervTestDev *dev = HYPERV_TEST_DEV(d);
    MemoryRegion *io = isa_address_space_io(isa);

    memset(dev->sint_route, 0, sizeof(dev->sint_route));
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
