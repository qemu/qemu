#include "hw/arm/ipod_touch_scaler_csc.h"

static uint64_t ipod_touch_scaler_csc_read(void *opaque, hwaddr addr, unsigned size)
{
    //fprintf(stderr, "%s: offset = 0x%08x\n", __func__, addr);

    switch (addr) {
        default:
            return 0;
    }

    return 0;
}

static void ipod_touch_scaler_csc_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    IPodTouchScalerCSCState *s = IPOD_TOUCH_SCALER_CSC(opaque);
    //printf("%s (base %d): writing 0x%08x to 0x%08x\n", __func__, s->base, data, addr);
}

static const MemoryRegionOps ipod_touch_scaler_csc_ops = {
    .read = ipod_touch_scaler_csc_read,
    .write = ipod_touch_scaler_csc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ipod_touch_scaler_csc_init(Object *obj)
{
    IPodTouchScalerCSCState *s = IPOD_TOUCH_SCALER_CSC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &ipod_touch_scaler_csc_ops, s, TYPE_IPOD_TOUCH_SCALER_CSC, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void ipod_touch_scaler_csc_class_init(ObjectClass *klass, void *data)
{
    
}

static const TypeInfo ipod_touch_scaler_csc_type_info = {
    .name = TYPE_IPOD_TOUCH_SCALER_CSC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IPodTouchScalerCSCState),
    .instance_init = ipod_touch_scaler_csc_init,
    .class_init = ipod_touch_scaler_csc_class_init,
};

static void ipod_touch_scaler_csc_register_types(void)
{
    type_register_static(&ipod_touch_scaler_csc_type_info);
}

type_init(ipod_touch_scaler_csc_register_types)