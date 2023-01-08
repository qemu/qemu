#include "hw/arm/ipod_touch_unknown1.h"

static uint64_t ipod_touch_unknown1_read(void *opaque, hwaddr addr, unsigned size)
{
    //fprintf(stderr, "%s: offset = 0x%08x\n", __func__, addr);

    switch (addr) {
        case 0x140:
            return 0x2;
        case 0x144:
            return 0x3;
        default:
            break;
    }

    return 0;
}

static void ipod_touch_unknown1_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps ipod_touch_unknown1_ops = {
    .read = ipod_touch_unknown1_read,
    .write = ipod_touch_unknown1_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ipod_touch_unknown1_init(Object *obj)
{
    IPodTouchUnknown1State *s = IPOD_TOUCH_UNKNOWN1(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &ipod_touch_unknown1_ops, s, TYPE_IPOD_TOUCH_UNKNOWN1, 0x200);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void ipod_touch_unknown1_class_init(ObjectClass *klass, void *data)
{
    
}

static const TypeInfo ipod_touch_unknown1_type_info = {
    .name = TYPE_IPOD_TOUCH_UNKNOWN1,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IPodTouchUnknown1State),
    .instance_init = ipod_touch_unknown1_init,
    .class_init = ipod_touch_unknown1_class_init,
};

static void ipod_touch_unknown1_register_types(void)
{
    type_register_static(&ipod_touch_unknown1_type_info);
}

type_init(ipod_touch_unknown1_register_types)