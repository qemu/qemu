#include "hw/arm/ipod_touch_chipid.h"

static uint64_t ipod_touch_chipid_read(void *opaque, hwaddr addr, unsigned size)
{
    //fprintf(stderr, "%s: offset = 0x%08x\n", __func__, addr);

    switch (addr) {
        case CHIPID_UNKNOWN1:
            return 0;
        case CHIPID_INFO:
            return (0x8720 << 16) | 0x1;
        case CHIPID_UNKNOWN2:
            return 0;
        case CHIPID_UNKNOWN3:
            return 0;
        default:
            hw_error("%s: reading from unknown chip ID register 0x%08x\n", __func__, addr);
    }

    return 0;
}

static const MemoryRegionOps ipod_touch_chipid_ops = {
    .read = ipod_touch_chipid_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ipod_touch_chipid_init(Object *obj)
{
    IPodTouchChipIDState *s = IPOD_TOUCH_CHIPID(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &ipod_touch_chipid_ops, s, TYPE_IPOD_TOUCH_CHIPID, 0x14);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void ipod_touch_chipid_class_init(ObjectClass *klass, void *data)
{
    
}

static const TypeInfo ipod_touch_chipid_type_info = {
    .name = TYPE_IPOD_TOUCH_CHIPID,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IPodTouchChipIDState),
    .instance_init = ipod_touch_chipid_init,
    .class_init = ipod_touch_chipid_class_init,
};

static void ipod_touch_chipid_register_types(void)
{
    type_register_static(&ipod_touch_chipid_type_info);
}

type_init(ipod_touch_chipid_register_types)