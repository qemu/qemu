/*
 * calypso_inth.c — Calypso INTH (Interrupt Handler)
 *
 * Two-level interrupt controller at 0xFFFFFA00.
 * 32 IRQ inputs, priority-based arbitration, IRQ/FIQ routing via ILR.
 *
 * Register map (16-bit, offsets from base):
 *   0x00        IT_REG1   (pending bits [15:0], read-only)
 *   0x02        IT_REG2   (pending bits [31:16], read-only)
 *   0x04        MASK_IT_REG1 (mask low)
 *   0x06        MASK_IT_REG2 (mask high)
 *   0x20..0x5F  ILR[0..31] (2 bytes each: bits[4:0]=prio, bit[8]=FIQ)
 *   0x80        IRQ_NUM   (current IRQ number, read-only)
 *   0x82        FIQ_NUM   (current FIQ number, read-only)
 *   0x84        IRQ_CTRL  (write 1 to acknowledge current IRQ)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "calypso_inth.h"

/* ---- Priority arbitration ---- */

static void calypso_inth_update(CalypsoINTHState *s)
{
    uint32_t active = s->pending & ~s->mask;
    int best_irq = -1;
    int best_prio = 0x7F;
    int is_fiq = 0;

    for (int i = 0; i < CALYPSO_INTH_NUM_IRQS; i++) {
        if (active & (1u << i)) {
            int prio = s->ilr[i] & 0x1F;
            if (prio < best_prio) {
                best_prio = prio;
                best_irq = i;
                is_fiq = (s->ilr[i] >> 8) & 1;
            }
        }
    }

    if (best_irq >= 0) {
        s->ith_v = best_irq;
        if (is_fiq) {
            qemu_irq_raise(s->parent_fiq);
            qemu_irq_lower(s->parent_irq);
        } else {
            qemu_irq_raise(s->parent_irq);
            qemu_irq_lower(s->parent_fiq);
        }
    } else {
        s->ith_v = 0;
        qemu_irq_lower(s->parent_irq);
        qemu_irq_lower(s->parent_fiq);
    }
}

/* ---- GPIO input handler (one per IRQ line) ---- */

static void calypso_inth_set_irq(void *opaque, int irq, int level)
{
    CalypsoINTHState *s = CALYPSO_INTH(opaque);

    if (level) {
        s->pending |= (1u << irq);
    } else {
        s->pending &= ~(1u << irq);
    }
    calypso_inth_update(s);
}

/* ---- MMIO read/write ---- */

static uint64_t calypso_inth_read(void *opaque, hwaddr offset, unsigned size)
{
    CalypsoINTHState *s = CALYPSO_INTH(opaque);

    switch (offset) {
    case 0x00: /* IT_REG1 — pending bits [15:0] */
        return s->pending & 0xFFFF;
    case 0x02: /* IT_REG2 — pending bits [31:16] */
        return (s->pending >> 16) & 0xFFFF;
    case 0x04: /* MASK_IT_REG1 */
        return s->mask & 0xFFFF;
    case 0x06: /* MASK_IT_REG2 */
        return (s->mask >> 16) & 0xFFFF;
    case 0x80: /* IRQ_NUM */
        return s->ith_v;
    case 0x82: /* FIQ_NUM */
        return s->ith_v;
    case 0x84: /* IRQ_CTRL */
        return 0;
    default:
        if (offset >= 0x20 && offset < 0x60) {
            int idx = (offset - 0x20) / 2;
            return s->ilr[idx];
        }
        qemu_log_mask(LOG_UNIMP, "calypso_inth: unimplemented read at 0x%02x\n",
                       (unsigned)offset);
        return 0;
    }
}

static void calypso_inth_write(void *opaque, hwaddr offset, uint64_t value,
                                unsigned size)
{
    CalypsoINTHState *s = CALYPSO_INTH(opaque);

    switch (offset) {
    case 0x04: /* MASK_IT_REG1 */
        s->mask = (s->mask & 0xFFFF0000) | (value & 0xFFFF);
        calypso_inth_update(s);
        break;
    case 0x06: /* MASK_IT_REG2 */
        s->mask = (s->mask & 0x0000FFFF) | ((value & 0xFFFF) << 16);
        calypso_inth_update(s);
        break;
    case 0x84: /* IRQ_CTRL — acknowledge current IRQ */
        if (s->ith_v < CALYPSO_INTH_NUM_IRQS) {
            s->pending &= ~(1u << s->ith_v);
        }
        calypso_inth_update(s);
        break;
    default:
        if (offset >= 0x20 && offset < 0x60) {
            int idx = (offset - 0x20) / 2;
            s->ilr[idx] = value & 0x1FFF;
        }
        break;
    }
}

static const MemoryRegionOps calypso_inth_ops = {
    .read = calypso_inth_read,
    .write = calypso_inth_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = { .min_access_size = 2, .max_access_size = 2 },
};

/* ---- QOM lifecycle ---- */

static void calypso_inth_realize(DeviceState *dev, Error **errp)
{
    CalypsoINTHState *s = CALYPSO_INTH(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &calypso_inth_ops, s,
                          "calypso-inth", 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);

    /* Two output lines: IRQ and FIQ to CPU */
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->parent_irq);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->parent_fiq);

    /* 32 input IRQ lines */
    qdev_init_gpio_in(dev, calypso_inth_set_irq, CALYPSO_INTH_NUM_IRQS);
}

static void calypso_inth_reset(DeviceState *dev)
{
    CalypsoINTHState *s = CALYPSO_INTH(dev);

    s->pending = 0;
    s->mask = 0xFFFFFFFF;  /* All masked at reset */
    s->ith_v = 0;
    memset(s->ilr, 0, sizeof(s->ilr));
}

static void calypso_inth_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = calypso_inth_realize;
    device_class_set_legacy_reset(dc, calypso_inth_reset);
    dc->desc = "Calypso INTH interrupt controller";
}

static const TypeInfo calypso_inth_info = {
    .name          = TYPE_CALYPSO_INTH,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(CalypsoINTHState),
    .class_init    = calypso_inth_class_init,
};

static void calypso_inth_register_types(void)
{
    type_register_static(&calypso_inth_info);
}

type_init(calypso_inth_register_types)
