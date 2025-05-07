/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU Loongson 7A1000 I/O interrupt controller.
 *
 * Copyright (C) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
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

static uint64_t pch_pic_read(void *opaque, hwaddr addr, uint64_t field_mask)
{
    LoongArchPICCommonState *s = LOONGARCH_PIC_COMMON(opaque);
    uint64_t val = 0;
    uint32_t offset;

    offset = addr & 7;
    addr -= offset;
    switch (addr) {
    case PCH_PIC_INT_ID:
        val = s->id.data;
        break;
    case PCH_PIC_INT_MASK:
        val = s->int_mask;
        break;
    case PCH_PIC_INT_EDGE:
        val = s->intedge;
        break;
    case PCH_PIC_HTMSI_EN:
        val = s->htmsi_en;
        break;
    case PCH_PIC_AUTO_CTRL0:
    case PCH_PIC_AUTO_CTRL1:
        /* PCH PIC connect to EXTIOI always, discard auto_ctrl access */
        break;
    case PCH_PIC_INT_STATUS:
        val = s->intisr & (~s->int_mask);
        break;
    case PCH_PIC_INT_POL:
        val = s->int_polarity;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pch_pic_read: Bad address 0x%"PRIx64"\n", addr);
        break;
    }

    return (val >> (offset * 8)) & field_mask;
}

static uint64_t loongarch_pch_pic_read(void *opaque, hwaddr addr,
                                       unsigned size)
{
    uint64_t val = 0;

    switch (size) {
    case 1:
        val = pch_pic_read(opaque, addr, UCHAR_MAX);
        break;
    case 2:
        val = pch_pic_read(opaque, addr, USHRT_MAX);
        break;
    case 4:
        val = pch_pic_read(opaque, addr, UINT_MAX);
        break;
    case 8:
        val = pch_pic_read(opaque, addr, UINT64_MAX);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "loongarch_pch_pic_read: Bad size %d\n", size);
        break;
    }

    return val;
}

static uint64_t loongarch_pch_pic_low_readw(void *opaque, hwaddr addr,
                                            unsigned size)
{
    uint64_t val;

    val = loongarch_pch_pic_read(opaque, addr, size);
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
    uint32_t old_valid, data = (uint32_t)value;
    uint64_t old, int_mask;

    trace_loongarch_pch_pic_low_writew(size, addr, data);

    switch (addr) {
    case PCH_PIC_INT_MASK:
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
    case PCH_PIC_INT_MASK + 4:
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
    case PCH_PIC_INT_EDGE:
        s->intedge = get_writew_val(s->intedge, data, 0);
        break;
    case PCH_PIC_INT_EDGE + 4:
        s->intedge = get_writew_val(s->intedge, data, 1);
        break;
    case PCH_PIC_INT_CLEAR:
        if (s->intedge & data) {
            s->intirr &= (~data);
            pch_pic_update_irq(s, data, 0);
            s->intisr &= (~data);
        }
        break;
    case PCH_PIC_INT_CLEAR + 4:
        value <<= 32;
        if (s->intedge & value) {
            s->intirr &= (~value);
            pch_pic_update_irq(s, value, 0);
            s->intisr &= (~value);
        }
        break;
    case PCH_PIC_HTMSI_EN:
        s->htmsi_en = get_writew_val(s->htmsi_en, data, 0);
        break;
    case PCH_PIC_HTMSI_EN + 4:
        s->htmsi_en = get_writew_val(s->htmsi_en, data, 1);
        break;
    case PCH_PIC_AUTO_CTRL0:
    case PCH_PIC_AUTO_CTRL0 + 4:
    case PCH_PIC_AUTO_CTRL1:
    case PCH_PIC_AUTO_CTRL1 + 4:
        /* discard auto_ctrl access */
        break;
    default:
        break;
    }
}

static uint64_t loongarch_pch_pic_high_readw(void *opaque, hwaddr addr,
                                        unsigned size)
{
    uint64_t val;

    addr += PCH_PIC_INT_STATUS;
    val = loongarch_pch_pic_read(opaque, addr, size);
    trace_loongarch_pch_pic_high_readw(size, addr, val);
    return val;
}

static void loongarch_pch_pic_high_writew(void *opaque, hwaddr addr,
                                     uint64_t value, unsigned size)
{
    LoongArchPICCommonState *s = LOONGARCH_PIC_COMMON(opaque);
    uint32_t data = (uint32_t)value;

    addr += PCH_PIC_INT_STATUS;
    trace_loongarch_pch_pic_high_writew(size, addr, data);

    switch (addr) {
    case PCH_PIC_INT_POL:
        s->int_polarity = get_writew_val(s->int_polarity, data, 0);
        break;
    case PCH_PIC_INT_POL + 4:
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
    int64_t offset_tmp;

    addr += PCH_PIC_ROUTE_ENTRY;
    switch (addr) {
    case PCH_PIC_HTMSI_VEC ... PCH_PIC_HTMSI_VEC_END:
        offset_tmp = addr - PCH_PIC_HTMSI_VEC;
        if (offset_tmp >= 0 && offset_tmp < 64) {
            val = s->htmsi_vector[offset_tmp];
        }
        break;
    case PCH_PIC_ROUTE_ENTRY ... PCH_PIC_ROUTE_ENTRY_END:
        offset_tmp = addr - PCH_PIC_ROUTE_ENTRY;
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

    addr += PCH_PIC_ROUTE_ENTRY;
    trace_loongarch_pch_pic_writeb(size, addr, data);

    switch (addr) {
    case PCH_PIC_HTMSI_VEC ... PCH_PIC_HTMSI_VEC_END:
        offset_tmp = addr - PCH_PIC_HTMSI_VEC;
        if (offset_tmp >= 0 && offset_tmp < 64) {
            s->htmsi_vector[offset_tmp] = (uint8_t)(data & 0xff);
        }
        break;
    case PCH_PIC_ROUTE_ENTRY ... PCH_PIC_ROUTE_ENTRY_END:
        offset_tmp = addr - PCH_PIC_ROUTE_ENTRY;
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

static void loongarch_pic_reset_hold(Object *obj, ResetType type)
{
    LoongarchPICClass *lpc = LOONGARCH_PIC_GET_CLASS(obj);

    if (lpc->parent_phases.hold) {
        lpc->parent_phases.hold(obj, type);
    }
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

static void loongarch_pic_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    LoongarchPICClass *lpc = LOONGARCH_PIC_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    resettable_class_set_parent_phases(rc, NULL, loongarch_pic_reset_hold,
                                       NULL, &lpc->parent_phases);
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
