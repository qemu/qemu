/*
 * Intel XScale PXA255/270 GPIO controller emulation.
 *
 * Copyright (c) 2006 Openedhand Ltd.
 * Written by Andrzej Zaborowski <balrog@zabor.org>
 *
 * This code is licensed under the GPL.
 */

#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/arm/pxa.h"

#define PXA2XX_GPIO_BANKS	4

#define TYPE_PXA2XX_GPIO "pxa2xx-gpio"
#define PXA2XX_GPIO(obj) \
    OBJECT_CHECK(PXA2xxGPIOInfo, (obj), TYPE_PXA2XX_GPIO)

typedef struct PXA2xxGPIOInfo PXA2xxGPIOInfo;
struct PXA2xxGPIOInfo {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    qemu_irq irq0, irq1, irqX;
    int lines;
    int ncpu;
    ARMCPU *cpu;

    /* XXX: GNU C vectors are more suitable */
    uint32_t ilevel[PXA2XX_GPIO_BANKS];
    uint32_t olevel[PXA2XX_GPIO_BANKS];
    uint32_t dir[PXA2XX_GPIO_BANKS];
    uint32_t rising[PXA2XX_GPIO_BANKS];
    uint32_t falling[PXA2XX_GPIO_BANKS];
    uint32_t status[PXA2XX_GPIO_BANKS];
    uint32_t gpsr[PXA2XX_GPIO_BANKS];
    uint32_t gafr[PXA2XX_GPIO_BANKS * 2];

    uint32_t prev_level[PXA2XX_GPIO_BANKS];
    qemu_irq handler[PXA2XX_GPIO_BANKS * 32];
    qemu_irq read_notify;
};

static struct {
    enum {
        GPIO_NONE,
        GPLR,
        GPSR,
        GPCR,
        GPDR,
        GRER,
        GFER,
        GEDR,
        GAFR_L,
        GAFR_U,
    } reg;
    int bank;
} pxa2xx_gpio_regs[0x200] = {
    [0 ... 0x1ff] = { GPIO_NONE, 0 },
#define PXA2XX_REG(reg, a0, a1, a2, a3)	\
    [a0] = { reg, 0 }, [a1] = { reg, 1 }, [a2] = { reg, 2 }, [a3] = { reg, 3 },

    PXA2XX_REG(GPLR, 0x000, 0x004, 0x008, 0x100)
    PXA2XX_REG(GPSR, 0x018, 0x01c, 0x020, 0x118)
    PXA2XX_REG(GPCR, 0x024, 0x028, 0x02c, 0x124)
    PXA2XX_REG(GPDR, 0x00c, 0x010, 0x014, 0x10c)
    PXA2XX_REG(GRER, 0x030, 0x034, 0x038, 0x130)
    PXA2XX_REG(GFER, 0x03c, 0x040, 0x044, 0x13c)
    PXA2XX_REG(GEDR, 0x048, 0x04c, 0x050, 0x148)
    PXA2XX_REG(GAFR_L, 0x054, 0x05c, 0x064, 0x06c)
    PXA2XX_REG(GAFR_U, 0x058, 0x060, 0x068, 0x070)
};

static void pxa2xx_gpio_irq_update(PXA2xxGPIOInfo *s)
{
    if (s->status[0] & (1 << 0))
        qemu_irq_raise(s->irq0);
    else
        qemu_irq_lower(s->irq0);

    if (s->status[0] & (1 << 1))
        qemu_irq_raise(s->irq1);
    else
        qemu_irq_lower(s->irq1);

    if ((s->status[0] & ~3) | s->status[1] | s->status[2] | s->status[3])
        qemu_irq_raise(s->irqX);
    else
        qemu_irq_lower(s->irqX);
}

/* Bitmap of pins used as standby and sleep wake-up sources.  */
static const int pxa2xx_gpio_wake[PXA2XX_GPIO_BANKS] = {
    0x8003fe1b, 0x002001fc, 0xec080000, 0x0012007f,
};

static void pxa2xx_gpio_set(void *opaque, int line, int level)
{
    PXA2xxGPIOInfo *s = (PXA2xxGPIOInfo *) opaque;
    CPUState *cpu = CPU(s->cpu);
    int bank;
    uint32_t mask;

    if (line >= s->lines) {
        printf("%s: No GPIO pin %i\n", __FUNCTION__, line);
        return;
    }

    bank = line >> 5;
    mask = 1U << (line & 31);

    if (level) {
        s->status[bank] |= s->rising[bank] & mask &
                ~s->ilevel[bank] & ~s->dir[bank];
        s->ilevel[bank] |= mask;
    } else {
        s->status[bank] |= s->falling[bank] & mask &
                s->ilevel[bank] & ~s->dir[bank];
        s->ilevel[bank] &= ~mask;
    }

    if (s->status[bank] & mask)
        pxa2xx_gpio_irq_update(s);

    /* Wake-up GPIOs */
    if (cpu->halted && (mask & ~s->dir[bank] & pxa2xx_gpio_wake[bank])) {
        cpu_interrupt(cpu, CPU_INTERRUPT_EXITTB);
    }
}

static void pxa2xx_gpio_handler_update(PXA2xxGPIOInfo *s) {
    uint32_t level, diff;
    int i, bit, line;
    for (i = 0; i < PXA2XX_GPIO_BANKS; i ++) {
        level = s->olevel[i] & s->dir[i];

        for (diff = s->prev_level[i] ^ level; diff; diff ^= 1 << bit) {
            bit = ffs(diff) - 1;
            line = bit + 32 * i;
            qemu_set_irq(s->handler[line], (level >> bit) & 1);
        }

        s->prev_level[i] = level;
    }
}

static uint64_t pxa2xx_gpio_read(void *opaque, hwaddr offset,
                                 unsigned size)
{
    PXA2xxGPIOInfo *s = (PXA2xxGPIOInfo *) opaque;
    uint32_t ret;
    int bank;
    if (offset >= 0x200)
        return 0;

    bank = pxa2xx_gpio_regs[offset].bank;
    switch (pxa2xx_gpio_regs[offset].reg) {
    case GPDR:		/* GPIO Pin-Direction registers */
        return s->dir[bank];

    case GPSR:		/* GPIO Pin-Output Set registers */
        printf("%s: Read from a write-only register " REG_FMT "\n",
                        __FUNCTION__, offset);
        return s->gpsr[bank];	/* Return last written value.  */

    case GPCR:		/* GPIO Pin-Output Clear registers */
        printf("%s: Read from a write-only register " REG_FMT "\n",
                        __FUNCTION__, offset);
        return 31337;		/* Specified as unpredictable in the docs.  */

    case GRER:		/* GPIO Rising-Edge Detect Enable registers */
        return s->rising[bank];

    case GFER:		/* GPIO Falling-Edge Detect Enable registers */
        return s->falling[bank];

    case GAFR_L:	/* GPIO Alternate Function registers */
        return s->gafr[bank * 2];

    case GAFR_U:	/* GPIO Alternate Function registers */
        return s->gafr[bank * 2 + 1];

    case GPLR:		/* GPIO Pin-Level registers */
        ret = (s->olevel[bank] & s->dir[bank]) |
                (s->ilevel[bank] & ~s->dir[bank]);
        qemu_irq_raise(s->read_notify);
        return ret;

    case GEDR:		/* GPIO Edge Detect Status registers */
        return s->status[bank];

    default:
        hw_error("%s: Bad offset " REG_FMT "\n", __FUNCTION__, offset);
    }

    return 0;
}

static void pxa2xx_gpio_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned size)
{
    PXA2xxGPIOInfo *s = (PXA2xxGPIOInfo *) opaque;
    int bank;
    if (offset >= 0x200)
        return;

    bank = pxa2xx_gpio_regs[offset].bank;
    switch (pxa2xx_gpio_regs[offset].reg) {
    case GPDR:		/* GPIO Pin-Direction registers */
        s->dir[bank] = value;
        pxa2xx_gpio_handler_update(s);
        break;

    case GPSR:		/* GPIO Pin-Output Set registers */
        s->olevel[bank] |= value;
        pxa2xx_gpio_handler_update(s);
        s->gpsr[bank] = value;
        break;

    case GPCR:		/* GPIO Pin-Output Clear registers */
        s->olevel[bank] &= ~value;
        pxa2xx_gpio_handler_update(s);
        break;

    case GRER:		/* GPIO Rising-Edge Detect Enable registers */
        s->rising[bank] = value;
        break;

    case GFER:		/* GPIO Falling-Edge Detect Enable registers */
        s->falling[bank] = value;
        break;

    case GAFR_L:	/* GPIO Alternate Function registers */
        s->gafr[bank * 2] = value;
        break;

    case GAFR_U:	/* GPIO Alternate Function registers */
        s->gafr[bank * 2 + 1] = value;
        break;

    case GEDR:		/* GPIO Edge Detect Status registers */
        s->status[bank] &= ~value;
        pxa2xx_gpio_irq_update(s);
        break;

    default:
        hw_error("%s: Bad offset " REG_FMT "\n", __FUNCTION__, offset);
    }
}

static const MemoryRegionOps pxa_gpio_ops = {
    .read = pxa2xx_gpio_read,
    .write = pxa2xx_gpio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

DeviceState *pxa2xx_gpio_init(hwaddr base,
                              ARMCPU *cpu, DeviceState *pic, int lines)
{
    CPUState *cs = CPU(cpu);
    DeviceState *dev;

    dev = qdev_create(NULL, TYPE_PXA2XX_GPIO);
    qdev_prop_set_int32(dev, "lines", lines);
    qdev_prop_set_int32(dev, "ncpu", cs->cpu_index);
    qdev_init_nofail(dev);

    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
                    qdev_get_gpio_in(pic, PXA2XX_PIC_GPIO_0));
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 1,
                    qdev_get_gpio_in(pic, PXA2XX_PIC_GPIO_1));
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 2,
                    qdev_get_gpio_in(pic, PXA2XX_PIC_GPIO_X));

    return dev;
}

static int pxa2xx_gpio_initfn(SysBusDevice *sbd)
{
    DeviceState *dev = DEVICE(sbd);
    PXA2xxGPIOInfo *s = PXA2XX_GPIO(dev);

    s->cpu = ARM_CPU(qemu_get_cpu(s->ncpu));

    qdev_init_gpio_in(dev, pxa2xx_gpio_set, s->lines);
    qdev_init_gpio_out(dev, s->handler, s->lines);

    memory_region_init_io(&s->iomem, OBJECT(s), &pxa_gpio_ops, s, "pxa2xx-gpio", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq0);
    sysbus_init_irq(sbd, &s->irq1);
    sysbus_init_irq(sbd, &s->irqX);

    return 0;
}

/*
 * Registers a callback to notify on GPLR reads.  This normally
 * shouldn't be needed but it is used for the hack on Spitz machines.
 */
void pxa2xx_gpio_read_notifier(DeviceState *dev, qemu_irq handler)
{
    PXA2xxGPIOInfo *s = PXA2XX_GPIO(dev);

    s->read_notify = handler;
}

static const VMStateDescription vmstate_pxa2xx_gpio_regs = {
    .name = "pxa2xx-gpio",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(lines, PXA2xxGPIOInfo),
        VMSTATE_UINT32_ARRAY(ilevel, PXA2xxGPIOInfo, PXA2XX_GPIO_BANKS),
        VMSTATE_UINT32_ARRAY(olevel, PXA2xxGPIOInfo, PXA2XX_GPIO_BANKS),
        VMSTATE_UINT32_ARRAY(dir, PXA2xxGPIOInfo, PXA2XX_GPIO_BANKS),
        VMSTATE_UINT32_ARRAY(rising, PXA2xxGPIOInfo, PXA2XX_GPIO_BANKS),
        VMSTATE_UINT32_ARRAY(falling, PXA2xxGPIOInfo, PXA2XX_GPIO_BANKS),
        VMSTATE_UINT32_ARRAY(status, PXA2xxGPIOInfo, PXA2XX_GPIO_BANKS),
        VMSTATE_UINT32_ARRAY(gafr, PXA2xxGPIOInfo, PXA2XX_GPIO_BANKS * 2),
        VMSTATE_END_OF_LIST(),
    },
};

static Property pxa2xx_gpio_properties[] = {
    DEFINE_PROP_INT32("lines", PXA2xxGPIOInfo, lines, 0),
    DEFINE_PROP_INT32("ncpu", PXA2xxGPIOInfo, ncpu, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void pxa2xx_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = pxa2xx_gpio_initfn;
    dc->desc = "PXA2xx GPIO controller";
    dc->props = pxa2xx_gpio_properties;
}

static const TypeInfo pxa2xx_gpio_info = {
    .name          = TYPE_PXA2XX_GPIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PXA2xxGPIOInfo),
    .class_init    = pxa2xx_gpio_class_init,
};

static void pxa2xx_gpio_register_types(void)
{
    type_register_static(&pxa2xx_gpio_info);
}

type_init(pxa2xx_gpio_register_types)
