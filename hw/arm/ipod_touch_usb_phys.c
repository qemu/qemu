#include "hw/arm/ipod_touch_usb_phys.h"

static uint64_t ipod_touch_usb_phys_read(void *opaque, hwaddr addr, unsigned size)
{
    IPodTouchUSBPhysState *s = (IPodTouchUSBPhysState *) opaque;

    switch(addr)
    {
    case REG_OPHYPWR:
        return s->usb_ophypwr;
    case REG_OPHYCLK:
        return s->usb_ophyclk;
    case REG_ORSTCON:
        return s->usb_orstcon;
    case REG_UNKNOWN1:
        return s->usb_unknown1;
    case REG_OPHYTUNE:
        return s->usb_ophytune;
    default:
        return 0x0;
        //hw_error("%s: read invalid location 0x%08x\n", __func__, addr);
    }
}

static void ipod_touch_usb_phys_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    IPodTouchUSBPhysState *s = (IPodTouchUSBPhysState *) opaque;

    switch(addr)
    {
    case REG_OPHYPWR:
        s->usb_ophypwr = val;
        return;
    case REG_OPHYCLK:
        s->usb_ophyclk = val;
        return;
    case REG_ORSTCON:
        s->usb_orstcon = val;
        return;
    case REG_UNKNOWN1:
        s->usb_unknown1 = val;
        return;
    case REG_OPHYTUNE:
        s->usb_ophytune = val;
        return;

    default:
        //hw_error("%s: write invalid location 0x%08x.\n", __func__, addr);
        return;
    }
}

static const MemoryRegionOps ipod_touch_usb_phys_ops = {
    .read = ipod_touch_usb_phys_read,
    .write = ipod_touch_usb_phys_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ipod_touch_usb_phys_init(Object *obj)
{
    IPodTouchUSBPhysState *s = IPOD_TOUCH_USB_PHYS(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &ipod_touch_usb_phys_ops, s, TYPE_IPOD_TOUCH_USB_PHYS, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void ipod_touch_usb_phys_class_init(ObjectClass *klass, void *data)
{
    
}

static const TypeInfo ipod_touch_usb_phys_type_info = {
    .name = TYPE_IPOD_TOUCH_USB_PHYS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IPodTouchUSBPhysState),
    .instance_init = ipod_touch_usb_phys_init,
    .class_init = ipod_touch_usb_phys_class_init,
};

static void ipod_touch_usb_phys_register_types(void)
{
    type_register_static(&ipod_touch_usb_phys_type_info);
}

type_init(ipod_touch_usb_phys_register_types)