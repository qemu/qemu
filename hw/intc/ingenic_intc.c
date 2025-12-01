/*
 * Ingenic Core Interrupt Controller emulation
 *
 * Copyright (c) 2024 OpenSensor Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This implements the Ingenic core interrupt controller found in T41 SoC.
 * The controller routes peripheral interrupts to the MIPS CPU.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "qom/object.h"

/* Register offsets for bank 0 (IRQs 0-31) */
#define INTC_ISR0       0x00    /* Interrupt Status Register bank 0 */
#define INTC_IMR0       0x04    /* Interrupt Mask Register bank 0 */
#define INTC_IMSR0      0x08    /* Interrupt Mask Set Register bank 0 */
#define INTC_IMCR0      0x0c    /* Interrupt Mask Clear Register bank 0 */
#define INTC_IPR0       0x10    /* Interrupt Pending Register bank 0 */

/* Register offsets for bank 1 (IRQs 32-63) */
#define INTC_ISR1       0x20    /* Interrupt Status Register bank 1 */
#define INTC_IMR1       0x24    /* Interrupt Mask Register bank 1 */
#define INTC_IMSR1      0x28    /* Interrupt Mask Set Register bank 1 */
#define INTC_IMCR1      0x2c    /* Interrupt Mask Clear Register bank 1 */
#define INTC_IPR1       0x30    /* Interrupt Pending Register bank 1 */

#define INTC_NUM_IRQS   64      /* Number of interrupt sources (2 banks of 32) */

#define TYPE_INGENIC_INTC "ingenic-intc"
OBJECT_DECLARE_SIMPLE_TYPE(IngenicIntcState, INGENIC_INTC)

struct IngenicIntcState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq parent_irq;    /* Output to CPU IRQ 2 */

    /* Registers - two banks of 32 IRQs each */
    uint32_t isr[2];        /* Interrupt status (pending from devices) */
    uint32_t imr[2];        /* Interrupt mask (1 = masked) */

    /* Input IRQ lines from peripherals */
    qemu_irq irq_inputs[INTC_NUM_IRQS];
};

static void ingenic_intc_update(IngenicIntcState *s)
{
    /* IPR = ISR & ~IMR (pending and not masked) */
    uint32_t pending0 = s->isr[0] & ~s->imr[0];
    uint32_t pending1 = s->isr[1] & ~s->imr[1];
    qemu_set_irq(s->parent_irq, (pending0 || pending1) ? 1 : 0);
}

static void ingenic_intc_set_irq(void *opaque, int irq, int level)
{
    IngenicIntcState *s = INGENIC_INTC(opaque);
    int bank = irq / 32;
    int bit = irq % 32;

    if (irq < 0 || irq >= INTC_NUM_IRQS) {
        return;
    }

    if (level) {
        s->isr[bank] |= (1 << bit);
    } else {
        s->isr[bank] &= ~(1 << bit);
    }

    ingenic_intc_update(s);
}

static uint64_t ingenic_intc_read(void *opaque, hwaddr offset, unsigned size)
{
    IngenicIntcState *s = INGENIC_INTC(opaque);
    uint64_t val = 0;

    switch (offset) {
    /* Bank 0 registers */
    case INTC_ISR0:
        val = s->isr[0];
        break;
    case INTC_IMR0:
        val = s->imr[0];
        break;
    case INTC_IPR0:
        val = s->isr[0] & ~s->imr[0];
        break;
    /* Bank 1 registers */
    case INTC_ISR1:
        val = s->isr[1];
        break;
    case INTC_IMR1:
        val = s->imr[1];
        break;
    case INTC_IPR1:
        val = s->isr[1] & ~s->imr[1];
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "ingenic_intc: read from unknown reg 0x%lx\n",
                      (unsigned long)offset);
        break;
    }

    return val;
}

static void ingenic_intc_write(void *opaque, hwaddr offset, uint64_t val,
                               unsigned size)
{
    IngenicIntcState *s = INGENIC_INTC(opaque);

    switch (offset) {
    /* Bank 0 registers */
    case INTC_IMR0:
        s->imr[0] = val;
        break;
    case INTC_IMSR0:
        s->imr[0] |= val;
        break;
    case INTC_IMCR0:
        s->imr[0] &= ~val;
        break;
    /* Bank 1 registers */
    case INTC_IMR1:
        s->imr[1] = val;
        break;
    case INTC_IMSR1:
        s->imr[1] |= val;
        break;
    case INTC_IMCR1:
        s->imr[1] &= ~val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "ingenic_intc: write to unknown reg 0x%lx\n",
                      (unsigned long)offset);
        break;
    }

    ingenic_intc_update(s);
}

static const MemoryRegionOps ingenic_intc_ops = {
    .read = ingenic_intc_read,
    .write = ingenic_intc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void ingenic_intc_reset(DeviceState *dev)
{
    IngenicIntcState *s = INGENIC_INTC(dev);

    s->isr[0] = 0;
    s->isr[1] = 0;
    s->imr[0] = 0xffffffff;  /* All interrupts masked by default */
    s->imr[1] = 0xffffffff;
}

static void ingenic_intc_realize(DeviceState *dev, Error **errp)
{
    IngenicIntcState *s = INGENIC_INTC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &ingenic_intc_ops, s,
                          "ingenic-intc", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);

    /* Output IRQ to CPU */
    sysbus_init_irq(sbd, &s->parent_irq);

    /* Input IRQs from peripherals */
    qdev_init_gpio_in(dev, ingenic_intc_set_irq, INTC_NUM_IRQS);
}

static void ingenic_intc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = ingenic_intc_realize;
    device_class_set_legacy_reset(dc, ingenic_intc_reset);
}

static const TypeInfo ingenic_intc_info = {
    .name          = TYPE_INGENIC_INTC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IngenicIntcState),
    .class_init    = ingenic_intc_class_init,
};

static void ingenic_intc_register_types(void)
{
    type_register_static(&ingenic_intc_info);
}

type_init(ingenic_intc_register_types)

