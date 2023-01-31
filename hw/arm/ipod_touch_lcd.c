#include "hw/arm/ipod_touch_lcd.h"

static uint64_t ipod_touch_lcd_read(void *opaque, hwaddr addr, unsigned size)
{
    fprintf(stderr, "%s: read from location 0x%08x\n", __func__, addr);

    IPodTouchLCDState *s = (IPodTouchLCDState *)opaque;
    switch(addr)
    {
        case 0x0:
            return 2;
        case 0x1b10:
            return 2;
        default:
            // hw_error("%s: read invalid location 0x%08x.\n", __func__, addr);
            break;
    }
    return 0;
}

static void ipod_touch_lcd_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    IPodTouchLCDState *s = (IPodTouchLCDState *)opaque;
    // fprintf(stderr, "%s: writing 0x%08x to 0x%08x\n", __func__, val, addr);
}

static const MemoryRegionOps lcd_ops = {
    .read = ipod_touch_lcd_read,
    .write = ipod_touch_lcd_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ipod_touch_lcd_realize(DeviceState *dev, Error **errp)
{
    
}

static void ipod_touch_lcd_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    DeviceState *dev = DEVICE(sbd);
    IPodTouchLCDState *s = IPOD_TOUCH_LCD(dev);

    memory_region_init_io(&s->iomem, obj, &lcd_ops, s, "lcd", 0x10000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static void ipod_touch_lcd_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = ipod_touch_lcd_realize;
}

static const TypeInfo ipod_touch_lcd_info = {
    .name          = TYPE_IPOD_TOUCH_LCD,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IPodTouchLCDState),
    .instance_init = ipod_touch_lcd_init,
    .class_init    = ipod_touch_lcd_class_init,
};

static void ipod_touch_machine_types(void)
{
    type_register_static(&ipod_touch_lcd_info);
}

type_init(ipod_touch_machine_types)