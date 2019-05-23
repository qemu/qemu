/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 * Refactoring for Pi2 Copyright (c) 2015, Microsoft. Written by Andrew Baumann.
 * This code is licensed under the GNU GPLv2 and later.
 * Heavily based on pl190.c, copyright terms below:
 *
 * Arm PrimeCell PL190 Vector Interrupt Controller
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "hw/intc/bcm2835_ic.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define GPU_IRQS 64
#define ARM_IRQS 8

#define IRQ_PENDING_BASIC       0x00 /* IRQ basic pending */
#define IRQ_PENDING_1           0x04 /* IRQ pending 1 */
#define IRQ_PENDING_2           0x08 /* IRQ pending 2 */
#define FIQ_CONTROL             0x0C /* FIQ register */
#define IRQ_ENABLE_1            0x10 /* Interrupt enable register 1 */
#define IRQ_ENABLE_2            0x14 /* Interrupt enable register 2 */
#define IRQ_ENABLE_BASIC        0x18 /* Base interrupt enable register */
#define IRQ_DISABLE_1           0x1C /* Interrupt disable register 1 */
#define IRQ_DISABLE_2           0x20 /* Interrupt disable register 2 */
#define IRQ_DISABLE_BASIC       0x24 /* Base interrupt disable register */

/* Update interrupts.  */
static void bcm2835_ic_update(BCM2835ICState *s)
{
    bool set = false;

    if (s->fiq_enable) {
        if (s->fiq_select >= GPU_IRQS) {
            /* ARM IRQ */
            set = extract32(s->arm_irq_level, s->fiq_select - GPU_IRQS, 1);
        } else {
            set = extract64(s->gpu_irq_level, s->fiq_select, 1);
        }
    }
    qemu_set_irq(s->fiq, set);

    set = (s->gpu_irq_level & s->gpu_irq_enable)
        || (s->arm_irq_level & s->arm_irq_enable);
    qemu_set_irq(s->irq, set);

}

static void bcm2835_ic_set_gpu_irq(void *opaque, int irq, int level)
{
    BCM2835ICState *s = opaque;

    assert(irq >= 0 && irq < 64);
    s->gpu_irq_level = deposit64(s->gpu_irq_level, irq, 1, level != 0);
    bcm2835_ic_update(s);
}

static void bcm2835_ic_set_arm_irq(void *opaque, int irq, int level)
{
    BCM2835ICState *s = opaque;

    assert(irq >= 0 && irq < 8);
    s->arm_irq_level = deposit32(s->arm_irq_level, irq, 1, level != 0);
    bcm2835_ic_update(s);
}

static const int irq_dups[] = { 7, 9, 10, 18, 19, 53, 54, 55, 56, 57, 62 };

static uint64_t bcm2835_ic_read(void *opaque, hwaddr offset, unsigned size)
{
    BCM2835ICState *s = opaque;
    uint32_t res = 0;
    uint64_t gpu_pending = s->gpu_irq_level & s->gpu_irq_enable;
    int i;

    switch (offset) {
    case IRQ_PENDING_BASIC:
        /* bits 0-7: ARM irqs */
        res = s->arm_irq_level & s->arm_irq_enable;

        /* bits 8 & 9: pending registers 1 & 2 */
        res |= (((uint32_t)gpu_pending) != 0) << 8;
        res |= ((gpu_pending >> 32) != 0) << 9;

        /* bits 10-20: selected GPU IRQs */
        for (i = 0; i < ARRAY_SIZE(irq_dups); i++) {
            res |= extract64(gpu_pending, irq_dups[i], 1) << (i + 10);
        }
        break;
    case IRQ_PENDING_1:
        res = gpu_pending;
        break;
    case IRQ_PENDING_2:
        res = gpu_pending >> 32;
        break;
    case FIQ_CONTROL:
        res = (s->fiq_enable << 7) | s->fiq_select;
        break;
    case IRQ_ENABLE_1:
        res = s->gpu_irq_enable;
        break;
    case IRQ_ENABLE_2:
        res = s->gpu_irq_enable >> 32;
        break;
    case IRQ_ENABLE_BASIC:
        res = s->arm_irq_enable;
        break;
    case IRQ_DISABLE_1:
        res = ~s->gpu_irq_enable;
        break;
    case IRQ_DISABLE_2:
        res = ~s->gpu_irq_enable >> 32;
        break;
    case IRQ_DISABLE_BASIC:
        res = ~s->arm_irq_enable;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset %"HWADDR_PRIx"\n",
                      __func__, offset);
        return 0;
    }

    return res;
}

static void bcm2835_ic_write(void *opaque, hwaddr offset, uint64_t val,
                             unsigned size)
{
    BCM2835ICState *s = opaque;

    switch (offset) {
    case FIQ_CONTROL:
        s->fiq_select = extract32(val, 0, 7);
        s->fiq_enable = extract32(val, 7, 1);
        break;
    case IRQ_ENABLE_1:
        s->gpu_irq_enable |= val;
        break;
    case IRQ_ENABLE_2:
        s->gpu_irq_enable |= val << 32;
        break;
    case IRQ_ENABLE_BASIC:
        s->arm_irq_enable |= val & 0xff;
        break;
    case IRQ_DISABLE_1:
        s->gpu_irq_enable &= ~val;
        break;
    case IRQ_DISABLE_2:
        s->gpu_irq_enable &= ~(val << 32);
        break;
    case IRQ_DISABLE_BASIC:
        s->arm_irq_enable &= ~val & 0xff;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset %"HWADDR_PRIx"\n",
                      __func__, offset);
        return;
    }
    bcm2835_ic_update(s);
}

static const MemoryRegionOps bcm2835_ic_ops = {
    .read = bcm2835_ic_read,
    .write = bcm2835_ic_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void bcm2835_ic_reset(DeviceState *d)
{
    BCM2835ICState *s = BCM2835_IC(d);

    s->gpu_irq_enable = 0;
    s->arm_irq_enable = 0;
    s->fiq_enable = false;
    s->fiq_select = 0;
}

static void bcm2835_ic_init(Object *obj)
{
    BCM2835ICState *s = BCM2835_IC(obj);

    memory_region_init_io(&s->iomem, obj, &bcm2835_ic_ops, s, TYPE_BCM2835_IC,
                          0x200);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);

    qdev_init_gpio_in_named(DEVICE(s), bcm2835_ic_set_gpu_irq,
                            BCM2835_IC_GPU_IRQ, GPU_IRQS);
    qdev_init_gpio_in_named(DEVICE(s), bcm2835_ic_set_arm_irq,
                            BCM2835_IC_ARM_IRQ, ARM_IRQS);

    sysbus_init_irq(SYS_BUS_DEVICE(s), &s->irq);
    sysbus_init_irq(SYS_BUS_DEVICE(s), &s->fiq);
}

static const VMStateDescription vmstate_bcm2835_ic = {
    .name = TYPE_BCM2835_IC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(gpu_irq_level, BCM2835ICState),
        VMSTATE_UINT64(gpu_irq_enable, BCM2835ICState),
        VMSTATE_UINT8(arm_irq_level, BCM2835ICState),
        VMSTATE_UINT8(arm_irq_enable, BCM2835ICState),
        VMSTATE_BOOL(fiq_enable, BCM2835ICState),
        VMSTATE_UINT8(fiq_select, BCM2835ICState),
        VMSTATE_END_OF_LIST()
    }
};

static void bcm2835_ic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = bcm2835_ic_reset;
    dc->vmsd = &vmstate_bcm2835_ic;
}

static TypeInfo bcm2835_ic_info = {
    .name          = TYPE_BCM2835_IC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835ICState),
    .class_init    = bcm2835_ic_class_init,
    .instance_init = bcm2835_ic_init,
};

static void bcm2835_ic_register_types(void)
{
    type_register_static(&bcm2835_ic_info);
}

type_init(bcm2835_ic_register_types)
