#include "hw/arm/ipod_touch_mbx.h"

static uint64_t ipod_touch_mbx_read(void *opaque, hwaddr addr, unsigned size)
{
    printf("%s: read from location 0x%08x\n", __func__, addr);
    switch(addr)
    {
        case 0x12c:
            return 0x40;
        case 0xf00:
            return (2 << 0x10) | (1 << 0x18); // seems to be some kind of identifier
        case 0x1020:
            return 0x10000;
        default:
            break;
    }
    return 0;
}

static void ipod_touch_mbx_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    IPodTouchMBXState *s = (IPodTouchMBXState *)opaque;
    fprintf(stderr, "%s: writing 0x%08x to 0x%08x\n", __func__, val, addr);
}

static const MemoryRegionOps ipod_touch_mbx_ops = {
    .read = ipod_touch_mbx_read,
    .write = ipod_touch_mbx_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ipod_touch_mbx_init(Object *obj)
{
    IPodTouchMBXState *s = IPOD_TOUCH_MBX(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &ipod_touch_mbx_ops, s, TYPE_IPOD_TOUCH_MBX, 0x1000000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void ipod_touch_mbx_class_init(ObjectClass *klass, void *data)
{
    
}

static const TypeInfo ipod_touch_mbx_type_info = {
    .name = TYPE_IPOD_TOUCH_MBX,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IPodTouchMBXState),
    .instance_init = ipod_touch_mbx_init,
    .class_init = ipod_touch_mbx_class_init,
};

static void ipod_touch_mbx_register_types(void)
{
    type_register_static(&ipod_touch_mbx_type_info);
}

type_init(ipod_touch_mbx_register_types)