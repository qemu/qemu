/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU Loongson 7A1000 I/O interrupt controller.
 *
 * Copyright (C) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "hw/irq.h"
#include "hw/intc/loongarch_pch_pic.h"
#include "trace.h"
#include "qapi/error.h"

static void pch_pic_update_irq(LoongArchPICCommonState *s, uint64_t mask,
                               int level)
{
    uint64_t val;
    int irq;

    if (level) {
        val = mask & s->intirr & ~s->int_mask;
        if (val) {
            irq = ctz64(val);
            s->intisr |= MAKE_64BIT_MASK(irq, 1);
            qemu_set_irq(s->parent_irq[s->htmsi_vector[irq]], 1);
        }
    } else {
        /*
         * intirr means requested pending irq
         * do not clear pending irq for edge-triggered on lowering edge
         */
        val = mask & s->intisr & ~s->intirr;
        if (val) {
            irq = ctz64(val);
            s->intisr &= ~MAKE_64BIT_MASK(irq, 1);
            qemu_set_irq(s->parent_irq[s->htmsi_vector[irq]], 0);
        }
    }
}

static void pch_pic_irq_handler(void *opaque, int irq, int level)
{
    LoongArchPICCommonState *s = LOONGARCH_PIC_COMMON(opaque);
    uint64_t mask = 1ULL << irq;

    assert(irq < s->irq_num);
    trace_loongarch_pch_pic_irq_handler(irq, level);

    if (s->intedge & mask) {
        /* Edge triggered */
        if (level) {
            if ((s->last_intirr & mask) == 0) {
                /* marked pending on a rising edge */
                s->intirr |= mask;
            }
            s->last_intirr |= mask;
        } else {
            s->last_intirr &= ~mask;
        }
    } else {
        /* Level triggered */
        if (level) {
            s->intirr |= mask;
            s->last_intirr |= mask;
        } else {
            s->intirr &= ~mask;
            s->last_intirr &= ~mask;
        }
    }
    pch_pic_update_irq(s, mask, level);
}

static uint64_t loongarch_pch_pic_low_readw(void *opaque, hwaddr addr,
                                            unsigned size)
{
    LoongArchPICCommonState *s = LOONGARCH_PIC_COMMON(opaque);
    uint64_t val = 0;
    uint32_t offset = addr & 0xfff;

    switch (offset) {
    case PCH_PIC_INT_ID_LO:
        val = PCH_PIC_INT_ID_VAL;
        break;
    case PCH_PIC_INT_ID_HI:
        /*
         * With 7A1000 manual
         *   bit  0-15 pch irqchip version
         *   bit 16-31 irq number supported with pch irqchip
         */
        val = deposit32(PCH_PIC_INT_ID_VER, 16, 16, s->irq_num - 1);
        break;
    case PCH_PIC_INT_MASK_LO:
        val = (uint32_t)s->int_mask;
        break;
    case PCH_PIC_INT_MASK_HI:
        val = s->int_mask >> 32;
        break;
    case PCH_PIC_INT_EDGE_LO:
        val = (uint32_t)s->intedge;
        break;
    case PCH_PIC_INT_EDGE_HI:
        val = s->intedge >> 32;
        break;
    case PCH_PIC_HTMSI_EN_LO:
        val = (uint32_t)s->htmsi_en;
        break;
    case PCH_PIC_HTMSI_EN_HI:
        val = s->htmsi_en >> 32;
        break;
    case PCH_PIC_AUTO_CTRL0_LO:
    case PCH_PIC_AUTO_CTRL0_HI:
    case PCH_PIC_AUTO_CTRL1_LO:
    case PCH_PIC_AUTO_CTRL1_HI:
        break;
    default:
        break;
    }

    trace_loongarch_pch_pic_low_readw(size, addr, val);
    return val;
}

static uint64_t get_writew_val(uint64_t value, uint32_t target, bool hi)
{
    uint64_t mask = 0xffffffff00000000;
    uint64_t data = target;

    return hi ? (value & ~mask) | (data << 32) : (value & mask) | data;
}

static void loongarch_pch_pic_low_writew(void *opaque, hwaddr addr,
                                         uint64_t value, unsigned size)
{
    LoongArchPICCommonState *s = LOONGARCH_PIC_COMMON(opaque);
    uint32_t offset, old_valid, data = (uint32_t)value;
    uint64_t old, int_mask;
    offset = addr & 0xfff;

    trace_loongarch_pch_pic_low_writew(size, addr, data);

    switch (offset) {
    case PCH_PIC_INT_MASK_LO:
        old = s->int_mask;
        s->int_mask = get_writew_val(old, data, 0);
        old_valid = (uint32_t)old;
        if (old_valid & ~data) {
            pch_pic_update_irq(s, (old_valid & ~data), 1);
        }
        if (~old_valid & data) {
            pch_pic_update_irq(s, (~old_valid & data), 0);
        }
        break;
    case PCH_PIC_INT_MASK_HI:
        old = s->int_mask;
        s->int_mask = get_writew_val(old, data, 1);
        old_valid = (uint32_t)(old >> 32);
        int_mask = old_valid & ~data;
        if (int_mask) {
            pch_pic_update_irq(s, int_mask << 32, 1);
        }
        int_mask = ~old_valid & data;
        if (int_mask) {
            pch_pic_update_irq(s, int_mask << 32, 0);
        }
        break;
    case PCH_PIC_INT_EDGE_LO:
        s->intedge = get_writew_val(s->intedge, data, 0);
        break;
    case PCH_PIC_INT_EDGE_HI:
        s->intedge = get_writew_val(s->intedge, data, 1);
        break;
    case PCH_PIC_INT_CLEAR_LO:
        if (s->intedge & data) {
            s->intirr &= (~data);
            pch_pic_update_irq(s, data, 0);
            s->intisr &= (~data);
        }
        break;
    case PCH_PIC_INT_CLEAR_HI:
        value <<= 32;
        if (s->intedge & value) {
            s->intirr &= (~value);
            pch_pic_update_irq(s, value, 0);
            s->intisr &= (~value);
        }
        break;
    case PCH_PIC_HTMSI_EN_LO:
        s->htmsi_en = get_writew_val(s->htmsi_en, data, 0);
        break;
    case PCH_PIC_HTMSI_EN_HI:
        s->htmsi_en = get_writew_val(s->htmsi_en, data, 1);
        break;
    case PCH_PIC_AUTO_CTRL0_LO:
    case PCH_PIC_AUTO_CTRL0_HI:
    case PCH_PIC_AUTO_CTRL1_LO:
    case PCH_PIC_AUTO_CTRL1_HI:
        break;
    default:
        break;
    }
}

static uint64_t loongarch_pch_pic_high_readw(void *opaque, hwaddr addr,
                                        unsigned size)
{
    LoongArchPICCommonState *s = LOONGARCH_PIC_COMMON(opaque);
    uint64_t val = 0;
    uint32_t offset = addr & 0xfff;

    switch (offset) {
    case STATUS_LO_START:
        val = (uint32_t)(s->intisr & (~s->int_mask));
        break;
    case STATUS_HI_START:
        val = (s->intisr & (~s->int_mask)) >> 32;
        break;
    case POL_LO_START:
        val = (uint32_t)s->int_polarity;
        break;
    case POL_HI_START:
        val = s->int_polarity >> 32;
        break;
    default:
        break;
    }

    trace_loongarch_pch_pic_high_readw(size, addr, val);
    return val;
}

static void loongarch_pch_pic_high_writew(void *opaque, hwaddr addr,
                                     uint64_t value, unsigned size)
{
    LoongArchPICCommonState *s = LOONGARCH_PIC_COMMON(opaque);
    uint32_t offset, data = (uint32_t)value;
    offset = addr & 0xfff;

    trace_loongarch_pch_pic_high_writew(size, addr, data);

    switch (offset) {
    case STATUS_LO_START:
        s->intisr = get_writew_val(s->intisr, data, 0);
        break;
    case STATUS_HI_START:
        s->intisr = get_writew_val(s->intisr, data, 1);
        break;
    case POL_LO_START:
        s->int_polarity = get_writew_val(s->int_polarity, data, 0);
        break;
    case POL_HI_START:
        s->int_polarity = get_writew_val(s->int_polarity, data, 1);
        break;
    default:
        break;
    }
}

static uint64_t loongarch_pch_pic_readb(void *opaque, hwaddr addr,
                                        unsigned size)
{
    LoongArchPICCommonState *s = LOONGARCH_PIC_COMMON(opaque);
    uint64_t val = 0;
    uint32_t offset = (addr & 0xfff) + PCH_PIC_ROUTE_ENTRY_OFFSET;
    int64_t offset_tmp;

    switch (offset) {
    case PCH_PIC_HTMSI_VEC_OFFSET ... PCH_PIC_HTMSI_VEC_END:
        offset_tmp = offset - PCH_PIC_HTMSI_VEC_OFFSET;
        if (offset_tmp >= 0 && offset_tmp < 64) {
            val = s->htmsi_vector[offset_tmp];
        }
        break;
    case PCH_PIC_ROUTE_ENTRY_OFFSET ... PCH_PIC_ROUTE_ENTRY_END:
        offset_tmp = offset - PCH_PIC_ROUTE_ENTRY_OFFSET;
        if (offset_tmp >= 0 && offset_tmp < 64) {
            val = s->route_entry[offset_tmp];
        }
        break;
    default:
        break;
    }

    trace_loongarch_pch_pic_readb(size, addr, val);
    return val;
}

static void loongarch_pch_pic_writeb(void *opaque, hwaddr addr,
                                     uint64_t data, unsigned size)
{
    LoongArchPICCommonState *s = LOONGARCH_PIC_COMMON(opaque);
    int32_t offset_tmp;
    uint32_t offset = (addr & 0xfff) + PCH_PIC_ROUTE_ENTRY_OFFSET;

    trace_loongarch_pch_pic_writeb(size, addr, data);

    switch (offset) {
    case PCH_PIC_HTMSI_VEC_OFFSET ... PCH_PIC_HTMSI_VEC_END:
        offset_tmp = offset - PCH_PIC_HTMSI_VEC_OFFSET;
        if (offset_tmp >= 0 && offset_tmp < 64) {
            s->htmsi_vector[offset_tmp] = (uint8_t)(data & 0xff);
        }
        break;
    case PCH_PIC_ROUTE_ENTRY_OFFSET ... PCH_PIC_ROUTE_ENTRY_END:
        offset_tmp = offset - PCH_PIC_ROUTE_ENTRY_OFFSET;
        if (offset_tmp >= 0 && offset_tmp < 64) {
            s->route_entry[offset_tmp] = (uint8_t)(data & 0xff);
        }
        break;
    default:
        break;
    }
}

static const MemoryRegionOps loongarch_pch_pic_reg32_low_ops = {
    .read = loongarch_pch_pic_low_readw,
    .write = loongarch_pch_pic_low_writew,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const MemoryRegionOps loongarch_pch_pic_reg32_high_ops = {
    .read = loongarch_pch_pic_high_readw,
    .write = loongarch_pch_pic_high_writew,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const MemoryRegionOps loongarch_pch_pic_reg8_ops = {
    .read = loongarch_pch_pic_readb,
    .write = loongarch_pch_pic_writeb,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void loongarch_pch_pic_reset(DeviceState *d)
{
    LoongArchPICCommonState *s = LOONGARCH_PIC_COMMON(d);
    int i;

    s->int_mask = -1;
    s->htmsi_en = 0x0;
    s->intedge  = 0x0;
    s->intclr   = 0x0;
    s->auto_crtl0 = 0x0;
    s->auto_crtl1 = 0x0;
    for (i = 0; i < 64; i++) {
        s->route_entry[i] = 0x1;
        s->htmsi_vector[i] = 0x0;
    }
    s->intirr = 0x0;
    s->intisr = 0x0;
    s->last_intirr = 0x0;
    s->int_polarity = 0x0;
}

static void loongarch_pic_realize(DeviceState *dev, Error **errp)
{
    LoongArchPICCommonState *s = LOONGARCH_PIC_COMMON(dev);
    LoongarchPICClass *lpc = LOONGARCH_PIC_GET_CLASS(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    Error *local_err = NULL;

    lpc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    qdev_init_gpio_out(dev, s->parent_irq, s->irq_num);
    qdev_init_gpio_in(dev, pch_pic_irq_handler, s->irq_num);
    memory_region_init_io(&s->iomem32_low, OBJECT(dev),
                          &loongarch_pch_pic_reg32_low_ops,
                          s, PCH_PIC_NAME(.reg32_part1), 0x100);
    memory_region_init_io(&s->iomem8, OBJECT(dev), &loongarch_pch_pic_reg8_ops,
                          s, PCH_PIC_NAME(.reg8), 0x2a0);
    memory_region_init_io(&s->iomem32_high, OBJECT(dev),
                          &loongarch_pch_pic_reg32_high_ops,
                          s, PCH_PIC_NAME(.reg32_part2), 0xc60);
    sysbus_init_mmio(sbd, &s->iomem32_low);
    sysbus_init_mmio(sbd, &s->iomem8);
    sysbus_init_mmio(sbd, &s->iomem32_high);

}

static void loongarch_pic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    LoongarchPICClass *lpc = LOONGARCH_PIC_CLASS(klass);

    device_class_set_legacy_reset(dc, loongarch_pch_pic_reset);
    device_class_set_parent_realize(dc, loongarch_pic_realize,
                                    &lpc->parent_realize);
}

static const TypeInfo loongarch_pic_types[] = {
   {
        .name               = TYPE_LOONGARCH_PIC,
        .parent             = TYPE_LOONGARCH_PIC_COMMON,
        .instance_size      = sizeof(LoongarchPICState),
        .class_size         = sizeof(LoongarchPICClass),
        .class_init         = loongarch_pic_class_init,
    }
};

DEFINE_TYPES(loongarch_pic_types)
