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
#include "system/kvm.h"
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

    if (kvm_irqchip_in_kernel()) {
        kvm_set_irq(kvm_state, irq, !!level);
        return;
    }

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
        val = cpu_to_le64(s->id.data);
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
    case PCH_PIC_HTMSI_VEC ... PCH_PIC_HTMSI_VEC_END:
        val = ldq_le_p(&s->htmsi_vector[addr - PCH_PIC_HTMSI_VEC]);
        break;
    case PCH_PIC_ROUTE_ENTRY ... PCH_PIC_ROUTE_ENTRY_END:
        val = ldq_le_p(&s->route_entry[addr - PCH_PIC_ROUTE_ENTRY]);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pch_pic_read: Bad address 0x%"PRIx64"\n", addr);
        break;
    }

    return (val >> (offset * 8)) & field_mask;
}

static void pch_pic_write(void *opaque, hwaddr addr, uint64_t value,
                          uint64_t field_mask)
{
    LoongArchPICCommonState *s = LOONGARCH_PIC_COMMON(opaque);
    uint32_t offset;
    uint64_t old, mask, data;
    void *ptemp;

    offset = addr & 7;
    addr -= offset;
    mask = field_mask << (offset * 8);
    data = (value & field_mask) << (offset * 8);
    switch (addr) {
    case PCH_PIC_INT_MASK:
        old = s->int_mask;
        s->int_mask = (old & ~mask) | data;
        if (old & ~data) {
            pch_pic_update_irq(s, old & ~data, 1);
        }

        if (~old & data) {
            pch_pic_update_irq(s, ~old & data, 0);
        }
        break;
    case PCH_PIC_INT_EDGE:
        s->intedge = (s->intedge & ~mask) | data;
        break;
    case PCH_PIC_INT_CLEAR:
        if (s->intedge & data) {
            s->intirr &= ~data;
            pch_pic_update_irq(s, data, 0);
            s->intisr &= ~data;
        }
        break;
    case PCH_PIC_HTMSI_EN:
        s->htmsi_en = (s->htmsi_en & ~mask) | data;
        break;
    case PCH_PIC_AUTO_CTRL0:
    case PCH_PIC_AUTO_CTRL1:
        /* Discard auto_ctrl access */
        break;
    case PCH_PIC_INT_POL:
        s->int_polarity = (s->int_polarity & ~mask) | data;
        break;
    case PCH_PIC_HTMSI_VEC ... PCH_PIC_HTMSI_VEC_END:
        ptemp = &s->htmsi_vector[addr - PCH_PIC_HTMSI_VEC];
        stq_le_p(ptemp, (ldq_le_p(ptemp) & ~mask) | data);
        break;
    case PCH_PIC_ROUTE_ENTRY ... PCH_PIC_ROUTE_ENTRY_END:
        ptemp = (uint64_t *)&s->route_entry[addr - PCH_PIC_ROUTE_ENTRY];
        stq_le_p(ptemp, (ldq_le_p(ptemp) & ~mask) | data);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pch_pic_write: Bad address 0x%"PRIx64"\n", addr);
        break;
    }
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

    trace_loongarch_pch_pic_read(size, addr, val);
    return val;
}

static void loongarch_pch_pic_write(void *opaque, hwaddr addr,
                                    uint64_t value, unsigned size)
{
    trace_loongarch_pch_pic_write(size, addr, value);

    switch (size) {
    case 1:
        pch_pic_write(opaque, addr, value, UCHAR_MAX);
        break;
    case 2:
        pch_pic_write(opaque, addr, value, USHRT_MAX);
        break;
        break;
    case 4:
        pch_pic_write(opaque, addr, value, UINT_MAX);
        break;
    case 8:
        pch_pic_write(opaque, addr, value, UINT64_MAX);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "loongarch_pch_pic_write: Bad size %d\n", size);
        break;
    }
}

static const MemoryRegionOps loongarch_pch_pic_ops = {
    .read = loongarch_pch_pic_read,
    .write = loongarch_pch_pic_write,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
        /*
         * PCH PIC device would not work correctly if the guest was doing
         * unaligned access. This might not be a limitation on the real
         * device but in practice there is no reason for a guest to access
         * this device unaligned.
         */
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void loongarch_pic_reset_hold(Object *obj, ResetType type)
{
    LoongarchPICClass *lpc = LOONGARCH_PIC_GET_CLASS(obj);

    if (lpc->parent_phases.hold) {
        lpc->parent_phases.hold(obj, type);
    }

    if (kvm_irqchip_in_kernel()) {
        kvm_pic_put(obj, 0);
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

    if (kvm_irqchip_in_kernel()) {
        kvm_pic_realize(dev, errp);
    } else {
        memory_region_init_io(&s->iomem, OBJECT(dev),
                              &loongarch_pch_pic_ops,
                              s, TYPE_LOONGARCH_PIC, VIRT_PCH_REG_SIZE);
        sysbus_init_mmio(sbd, &s->iomem);
    }
}

static int loongarch_pic_pre_save(LoongArchPICCommonState *opaque)
{
    if (kvm_irqchip_in_kernel()) {
        return kvm_pic_get(opaque);
    }

    return 0;
}

static int loongarch_pic_post_load(LoongArchPICCommonState *opaque,
                                   int version_id)
{
    if (kvm_irqchip_in_kernel()) {
        return kvm_pic_put(opaque, version_id);
    }

    return 0;
}

static void loongarch_pic_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    LoongarchPICClass *lpc = LOONGARCH_PIC_CLASS(klass);
    LoongArchPICCommonClass *lpcc = LOONGARCH_PIC_COMMON_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    resettable_class_set_parent_phases(rc, NULL, loongarch_pic_reset_hold,
                                       NULL, &lpc->parent_phases);
    device_class_set_parent_realize(dc, loongarch_pic_realize,
                                    &lpc->parent_realize);
    lpcc->pre_save = loongarch_pic_pre_save;
    lpcc->post_load = loongarch_pic_post_load;
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
