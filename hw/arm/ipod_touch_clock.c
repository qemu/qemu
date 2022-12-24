#include "hw/arm/ipod_touch_clock.h"

static void s5l8900_clock_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    IPodTouchClockState *s = (struct IPodTouchClockState *) opaque;

    switch (addr) {
        case CLOCK_CONFIG0:
            s->config0 = val;
            break;
        case CLOCK_CONFIG1:
            s->config1 = val;
            break;
        case CLOCK_CONFIG2:
            s->config2 = val;
            break;
        case CLOCK_CONFIG3:
            s->config3 = val;
            break;
        case CLOCK_CONFIG4:
            s->config4 = val;
            break;
        case CLOCK_CONFIG5:
            s->config5 = val;
            break;

        case CLOCK_PLL0CON:
            s->pll0con = val;
            break;
        case CLOCK_PLL1CON:
            s->pll1con = val;
            break;
        case CLOCK_PLL2CON:
            s->pll2con = val;
            break;
        case CLOCK_PLL3CON:
            s->pll3con = val;
            break;
        case CLOCK_PLL0LCNT:
            s->pll0lcnt = val;
            break;
        case CLOCK_PLL1LCNT:
            s->pll1lcnt = val;
            break;
        case CLOCK_PLL2LCNT:
            s->pll2lcnt = val;
            break;
        case CLOCK_PLL3LCNT:
            s->pll3lcnt = val;
            break;
        case CLOCK_PLLLOCK:
            hw_error("%s: Forbidden write to PLLLOCK register 0x%08x\n", __func__, addr);
            break;
        case CLOCK_PLLMODE:
            s->pllmode = val;
            break;
        case CLOCK_PWRCON0:
            s->pwrcon0 = val;
            break;
        case CLOCK_PWRCON1:
            s->pwrcon1 = val;
            break;
        case CLOCK_PWRCON2:
            s->pwrcon2 = val;
            break;
        case CLOCK_PWRCON3:
            s->pwrcon3 = val;
            break;
        case CLOCK_PWRCON4:
            s->pwrcon4 = val;
            break;
      default:
            hw_error("%s: writing value 0x%08x to unknown clock register 0x%08x\n", __func__, val, addr);
    }
}

static uint64_t s5l8900_clock_read(void *opaque, hwaddr addr, unsigned size)
{
    IPodTouchClockState *s = (struct IPodTouchClockState *) opaque;

    switch (addr) {
        case CLOCK_CONFIG0:
            return s->config0;
        case CLOCK_CONFIG1:
            return s->config1;
        case CLOCK_CONFIG2:
            return s->config2;
        case CLOCK_PLL0CON:
            return s->pll0con;
        case CLOCK_PLL1CON:
            return s->pll1con;
        case CLOCK_PLL2CON:
            return s->pll2con;
        case CLOCK_PLL3CON:
            return s->pll3con;
        case CLOCK_PLLLOCK:
            return (1 | 2 | 4 | 8); // all PLLs are locked
        case CLOCK_PLLMODE:
            return s->pllmode;
        case CLOCK_PWRCON0:
            return s->pwrcon0;
        case CLOCK_PWRCON1:
            return s->pwrcon1;
        case CLOCK_PWRCON2:
            return s->pwrcon2;
        case CLOCK_PWRCON3:
            return s->pwrcon3;
        case CLOCK_PWRCON4:
            return s->pwrcon4;
      default:
            hw_error("%s: reading from unknown clock register 0x%08x\n", __func__, addr);
    }
    return 0;
}

static const MemoryRegionOps clock_ops = {
    .read = s5l8900_clock_read,
    .write = s5l8900_clock_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void s5l8900_clock_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    DeviceState *dev = DEVICE(sbd);
    IPodTouchClockState *s = IPOD_TOUCH_CLOCK(dev);

    memory_region_init_io(&s->iomem, obj, &clock_ops, s, "clock", 0x80);
}

static void s5l8900_clock_class_init(ObjectClass *klass, void *data)
{

}

static const TypeInfo ipod_touch_clock_info = {
    .name          = TYPE_IPOD_TOUCH_CLOCK,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IPodTouchClockState),
    .instance_init = s5l8900_clock_init,
    .class_init    = s5l8900_clock_class_init,
};

static void ipod_touch_machine_types(void)
{
    type_register_static(&ipod_touch_clock_info);
}

type_init(ipod_touch_machine_types)