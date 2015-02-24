/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 * This code is licensed under the GNU GPLv2 and later.
 */

/* Heavily based on pl190.c, copyright terms below. */

/*
 * Arm PrimeCell PL190 Vector Interrupt Controller
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "hw/sysbus.h"

#define IR_B 2
#define IR_1 0
#define IR_2 1

#define TYPE_BCM2835_IC "bcm2835_ic"
#define BCM2835_IC(obj) OBJECT_CHECK(bcm2835_ic_state, (obj), TYPE_BCM2835_IC)


typedef struct bcm2835_ic_state {
    SysBusDevice busdev;
    MemoryRegion iomem;

    uint32_t level[3];
    uint32_t irq_enable[3];
    int fiq_enable;
    int fiq_select;
    qemu_irq irq;
    qemu_irq fiq;
} bcm2835_ic_state;

/* Update interrupts.  */
static void bcm2835_ic_update(bcm2835_ic_state *s)
{
    int set;
    int i;

    set = 0;
    if (s->fiq_enable) {
        set = s->level[s->fiq_select >> 5] & (1u << (s->fiq_select & 0x1f));
    }
    qemu_set_irq(s->fiq, set);

    set = 0;
    for (i = 0; i < 3; i++) {
        set |= (s->level[i] & s->irq_enable[i]);
    }
    qemu_set_irq(s->irq, set);

}

static void bcm2835_ic_set_irq(void *opaque, int irq, int level)
{
    bcm2835_ic_state *s = (bcm2835_ic_state *)opaque;

    if (irq >= 0 && irq <= 71) {
        if (level) {
                s->level[irq >> 5] |= 1u << (irq & 0x1f);
        } else {
                s->level[irq >> 5] &= ~(1u << (irq & 0x1f));
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
            "bcm2835_ic_set_irq: Bad irq %d\n", irq);
    }

    bcm2835_ic_update(s);
}

static const int irq_dups[] = { 7, 9, 10, 18, 19, 53, 54, 55, 56, 57, 62, -1 };

static uint64_t bcm2835_ic_read(void *opaque, hwaddr offset,
    unsigned size)
{
    bcm2835_ic_state *s = (bcm2835_ic_state *)opaque;
    int i;
    int p = 0;
    uint32_t res = 0;

    switch (offset) {
    case 0x00:  /* IRQ basic pending */
        /* bits 0-7 - ARM irqs */
        res = (s->level[IR_B] & s->irq_enable[IR_B]) & 0xff;
        for (i = 0; i < 64; i++) {
            if (i == irq_dups[p]) {
                /* bits 10-20 - selected GPU irqs */
                if (s->level[i >> 5] & s->irq_enable[i >> 5]
                    & (1u << (i & 0x1f))) {
                    res |= (1u << (10 + p));
                }
                p++;
            } else {
                /* bits 8-9 - one or more bits set in pending registers 1-2 */
                if (s->level[i >> 5] & s->irq_enable[i >> 5]
                    & (1u << (i & 0x1f))) {
                    res |= (1u << (8 + (i >> 5)));
                }
            }
        }
        break;
    case 0x04:  /* IRQ pending 1 */
        res = s->level[IR_1] & s->irq_enable[IR_1];
        break;
    case 0x08:  /* IRQ pending 2 */
        res = s->level[IR_2] & s->irq_enable[IR_2];
        break;
    case 0x0C:  /* FIQ register */
        res = (s->fiq_enable << 7) | s->fiq_select;
        break;
    case 0x10:  /* Interrupt enable register 1 */
        res = s->irq_enable[IR_1];
        break;
    case 0x14:  /* Interrupt enable register 2 */
        res = s->irq_enable[IR_2];
        break;
    case 0x18:  /* Base interrupt enable register */
        res = s->irq_enable[IR_B];
        break;
    case 0x1C:  /* Interrupt disable register 1 */
        res = ~s->irq_enable[IR_1];
        break;
    case 0x20:  /* Interrupt disable register 2 */
        res = ~s->irq_enable[IR_2];
        break;
    case 0x24:  /* Base interrupt disable register */
        res = ~s->irq_enable[IR_B];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
            "bcm2835_ic_read: Bad offset %x\n", (int)offset);
        return 0;
    }

    return res;
}

static void bcm2835_ic_write(void *opaque, hwaddr offset,
    uint64_t val, unsigned size)
{
    bcm2835_ic_state *s = (bcm2835_ic_state *)opaque;

    switch (offset) {
    case 0x0C:  /* FIQ register */
        s->fiq_select = (val & 0x7f);
        s->fiq_enable = (val >> 7) & 0x1;
        break;
    case 0x10:  /* Interrupt enable register 1 */
        s->irq_enable[IR_1] |= val;
        break;
    case 0x14:  /* Interrupt enable register 2 */
        s->irq_enable[IR_2] |= val;
        break;
    case 0x18:  /* Base interrupt enable register */
        s->irq_enable[IR_B] |= (val & 0xff);
        break;
    case 0x1C:  /* Interrupt disable register 1 */
        s->irq_enable[IR_1] &= ~val;
        break;
    case 0x20:  /* Interrupt disable register 2 */
        s->irq_enable[IR_2] &= ~val;
        break;
    case 0x24:  /* Base interrupt disable register */
        s->irq_enable[IR_B] &= (~val & 0xff);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
            "bcm2835_ic_write: Bad offset %x\n", (int)offset);
        return;
    }
    bcm2835_ic_update(s);
}

static const MemoryRegionOps bcm2835_ic_ops = {
    .read = bcm2835_ic_read,
    .write = bcm2835_ic_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void bcm2835_ic_reset(DeviceState *d)
{
    bcm2835_ic_state *s = BCM2835_IC(d);
    int i;

    for (i = 0; i < 3; i++) {
        s->irq_enable[i] = 0;
    }
    s->fiq_enable = 0;
    s->fiq_select = 0;
}

static int bcm2835_ic_init(SysBusDevice *sbd)
{
    DeviceState *dev = DEVICE(sbd);
    bcm2835_ic_state *s = BCM2835_IC(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &bcm2835_ic_ops, s,
        TYPE_BCM2835_IC, 0x200);
    sysbus_init_mmio(sbd, &s->iomem);

    qdev_init_gpio_in(dev, bcm2835_ic_set_irq, 72);
    sysbus_init_irq(sbd, &s->irq);
    sysbus_init_irq(sbd, &s->fiq);
    return 0;
}

static const VMStateDescription vmstate_bcm2835_ic = {
    .name = TYPE_BCM2835_IC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(level, bcm2835_ic_state, 3),
        VMSTATE_UINT32_ARRAY(irq_enable, bcm2835_ic_state, 3),
        VMSTATE_INT32(fiq_enable, bcm2835_ic_state),
        VMSTATE_INT32(fiq_select, bcm2835_ic_state),
        VMSTATE_END_OF_LIST()
    }
};

static void bcm2835_ic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = bcm2835_ic_init;
    dc->reset = bcm2835_ic_reset;
    dc->vmsd = &vmstate_bcm2835_ic;
}

static TypeInfo bcm2835_ic_info = {
    .name          = TYPE_BCM2835_IC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(bcm2835_ic_state),
    .class_init    = bcm2835_ic_class_init,
};

static void bcm2835_ic_register_types(void)
{
    type_register_static(&bcm2835_ic_info);
}

type_init(bcm2835_ic_register_types)
