/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Loongson 3A5000 ext interrupt controller emulation
 *
 * Copyright (C) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/loongarch/virt.h"
#include "system/address-spaces.h"
#include "system/kvm.h"
#include "hw/intc/loongarch_extioi.h"
#include "trace.h"

static int extioi_get_index_from_archid(LoongArchExtIOICommonState *s,
                                        uint64_t arch_id)
{
    int i;

    for (i = 0; i < s->num_cpu; i++) {
        if (s->cpu[i].arch_id == arch_id) {
            break;
        }
    }

    if ((i < s->num_cpu) && s->cpu[i].cpu) {
        return i;
    }

    return -1;
}

static void extioi_update_irq(LoongArchExtIOICommonState *s, int irq, int level)
{
    int ipnum, cpu, found, irq_index, irq_mask;

    ipnum = s->sw_ipmap[irq / 32];
    cpu = s->sw_coremap[irq];
    irq_index = irq / 32;
    irq_mask = 1 << (irq & 0x1f);

    if (level) {
        /* if not enable return false */
        if (((s->enable[irq_index]) & irq_mask) == 0) {
            return;
        }
        s->cpu[cpu].coreisr[irq_index] |= irq_mask;
        found = find_first_bit(s->cpu[cpu].sw_isr[ipnum], EXTIOI_IRQS);
        set_bit(irq, s->cpu[cpu].sw_isr[ipnum]);
        if (found < EXTIOI_IRQS) {
            /* other irq is handling, need not update parent irq level */
            return;
        }
    } else {
        s->cpu[cpu].coreisr[irq_index] &= ~irq_mask;
        clear_bit(irq, s->cpu[cpu].sw_isr[ipnum]);
        found = find_first_bit(s->cpu[cpu].sw_isr[ipnum], EXTIOI_IRQS);
        if (found < EXTIOI_IRQS) {
            /* other irq is handling, need not update parent irq level */
            return;
        }
    }
    qemu_set_irq(s->cpu[cpu].parent_irq[ipnum], level);
}

static void extioi_setirq(void *opaque, int irq, int level)
{
    LoongArchExtIOICommonState *s = LOONGARCH_EXTIOI_COMMON(opaque);
    trace_loongarch_extioi_setirq(irq, level);
    if (level) {
        set_bit32(irq, s->isr);
    } else {
        clear_bit32(irq, s->isr);
    }
    extioi_update_irq(s, irq, level);
}

static MemTxResult extioi_readw(void *opaque, hwaddr addr, uint64_t *data,
                                unsigned size, MemTxAttrs attrs)
{
    LoongArchExtIOICommonState *s = LOONGARCH_EXTIOI_COMMON(opaque);
    unsigned long offset = addr & 0xffff;
    uint32_t index, cpu;

    switch (offset) {
    case EXTIOI_NODETYPE_START ... EXTIOI_NODETYPE_END - 1:
        index = (offset - EXTIOI_NODETYPE_START) >> 2;
        *data = s->nodetype[index];
        break;
    case EXTIOI_IPMAP_START ... EXTIOI_IPMAP_END - 1:
        index = (offset - EXTIOI_IPMAP_START) >> 2;
        *data = s->ipmap[index];
        break;
    case EXTIOI_ENABLE_START ... EXTIOI_ENABLE_END - 1:
        index = (offset - EXTIOI_ENABLE_START) >> 2;
        *data = s->enable[index];
        break;
    case EXTIOI_BOUNCE_START ... EXTIOI_BOUNCE_END - 1:
        index = (offset - EXTIOI_BOUNCE_START) >> 2;
        *data = s->bounce[index];
        break;
    case EXTIOI_COREISR_START ... EXTIOI_COREISR_END - 1:
        index = (offset - EXTIOI_COREISR_START) >> 2;
        /* using attrs to get current cpu index */
        cpu = attrs.requester_id;
        *data = s->cpu[cpu].coreisr[index];
        break;
    case EXTIOI_COREMAP_START ... EXTIOI_COREMAP_END - 1:
        index = (offset - EXTIOI_COREMAP_START) >> 2;
        *data = s->coremap[index];
        break;
    default:
        break;
    }

    trace_loongarch_extioi_readw(addr, *data);
    return MEMTX_OK;
}

static inline void extioi_enable_irq(LoongArchExtIOICommonState *s, int index,\
                                     uint32_t mask, int level)
{
    uint32_t val;
    int irq;

    val = mask & s->isr[index];
    irq = ctz32(val);
    while (irq != 32) {
        /*
         * enable bit change from 0 to 1,
         * need to update irq by pending bits
         */
        extioi_update_irq(s, irq + index * 32, level);
        val &= ~(1 << irq);
        irq = ctz32(val);
    }
}

static inline void extioi_update_sw_coremap(LoongArchExtIOICommonState *s,
                                            int irq, uint64_t val, bool notify)
{
    int i, cpu, cpuid;

    /*
     * loongarch only support little endian,
     * so we paresd the value with little endian.
     */
    val = cpu_to_le64(val);

    for (i = 0; i < 4; i++) {
        cpuid = val & 0xff;
        val = val >> 8;

        if (!(s->status & BIT(EXTIOI_ENABLE_CPU_ENCODE))) {
            cpuid = ctz32(cpuid);
            cpuid = (cpuid >= 4) ? 0 : cpuid;
        }

        cpu = extioi_get_index_from_archid(s, cpuid);
        if (cpu < 0) {
            continue;
        }

        if (s->sw_coremap[irq + i] == cpu) {
            continue;
        }

        if (notify && test_bit32(irq + i, s->isr)) {
            /*
             * lower irq at old cpu and raise irq at new cpu
             */
            extioi_update_irq(s, irq + i, 0);
            s->sw_coremap[irq + i] = cpu;
            extioi_update_irq(s, irq + i, 1);
        } else {
            s->sw_coremap[irq + i] = cpu;
        }
    }
}

static inline void extioi_update_sw_ipmap(LoongArchExtIOICommonState *s,
                                          int index, uint64_t val)
{
    int i;
    uint8_t ipnum;

    /*
     * loongarch only support little endian,
     * so we paresd the value with little endian.
     */
    val = cpu_to_le64(val);
    for (i = 0; i < 4; i++) {
        ipnum = val & 0xff;
        ipnum = ctz32(ipnum);
        ipnum = (ipnum >= 4) ? 0 : ipnum;
        s->sw_ipmap[index * 4 + i] = ipnum;
        val = val >> 8;
    }
}

static MemTxResult extioi_writew(void *opaque, hwaddr addr,
                          uint64_t val, unsigned size,
                          MemTxAttrs attrs)
{
    LoongArchExtIOICommonState *s = LOONGARCH_EXTIOI_COMMON(opaque);
    int cpu, index, old_data, irq;
    uint32_t offset;

    trace_loongarch_extioi_writew(addr, val);
    offset = addr & 0xffff;

    switch (offset) {
    case EXTIOI_NODETYPE_START ... EXTIOI_NODETYPE_END - 1:
        index = (offset - EXTIOI_NODETYPE_START) >> 2;
        s->nodetype[index] = val;
        break;
    case EXTIOI_IPMAP_START ... EXTIOI_IPMAP_END - 1:
        /*
         * ipmap cannot be set at runtime, can be set only at the beginning
         * of intr driver, need not update upper irq level
         */
        index = (offset - EXTIOI_IPMAP_START) >> 2;
        s->ipmap[index] = val;
        extioi_update_sw_ipmap(s, index, val);
        break;
    case EXTIOI_ENABLE_START ... EXTIOI_ENABLE_END - 1:
        index = (offset - EXTIOI_ENABLE_START) >> 2;
        old_data = s->enable[index];
        s->enable[index] = val;

        /* unmask irq */
        val = s->enable[index] & ~old_data;
        extioi_enable_irq(s, index, val, 1);

        /* mask irq */
        val = ~s->enable[index] & old_data;
        extioi_enable_irq(s, index, val, 0);
        break;
    case EXTIOI_BOUNCE_START ... EXTIOI_BOUNCE_END - 1:
        /* do not emulate hw bounced irq routing */
        index = (offset - EXTIOI_BOUNCE_START) >> 2;
        s->bounce[index] = val;
        break;
    case EXTIOI_COREISR_START ... EXTIOI_COREISR_END - 1:
        index = (offset - EXTIOI_COREISR_START) >> 2;
        /* using attrs to get current cpu index */
        cpu = attrs.requester_id;
        old_data = s->cpu[cpu].coreisr[index];
        s->cpu[cpu].coreisr[index] = old_data & ~val;
        /* write 1 to clear interrupt */
        old_data &= val;
        irq = ctz32(old_data);
        while (irq != 32) {
            extioi_update_irq(s, irq + index * 32, 0);
            old_data &= ~(1 << irq);
            irq = ctz32(old_data);
        }
        break;
    case EXTIOI_COREMAP_START ... EXTIOI_COREMAP_END - 1:
        irq = offset - EXTIOI_COREMAP_START;
        index = irq / 4;
        s->coremap[index] = val;

        extioi_update_sw_coremap(s, irq, val, true);
        break;
    default:
        break;
    }
    return MEMTX_OK;
}

static const MemoryRegionOps extioi_ops = {
    .read_with_attrs = extioi_readw,
    .write_with_attrs = extioi_writew,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 8,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static MemTxResult extioi_virt_readw(void *opaque, hwaddr addr, uint64_t *data,
                                     unsigned size, MemTxAttrs attrs)
{
    LoongArchExtIOICommonState *s = LOONGARCH_EXTIOI_COMMON(opaque);

    switch (addr) {
    case EXTIOI_VIRT_FEATURES:
        *data = s->features;
        break;
    case EXTIOI_VIRT_CONFIG:
        *data = s->status;
        break;
    default:
        g_assert_not_reached();
    }

    return MEMTX_OK;
}

static MemTxResult extioi_virt_writew(void *opaque, hwaddr addr,
                          uint64_t val, unsigned size,
                          MemTxAttrs attrs)
{
    LoongArchExtIOICommonState *s = LOONGARCH_EXTIOI_COMMON(opaque);

    switch (addr) {
    case EXTIOI_VIRT_FEATURES:
        return MEMTX_ACCESS_ERROR;

    case EXTIOI_VIRT_CONFIG:
        /*
         * extioi features can only be set at disabled status
         */
        if ((s->status & BIT(EXTIOI_ENABLE)) && val) {
            return MEMTX_ACCESS_ERROR;
        }

        s->status = val & s->features;
        break;
    default:
        g_assert_not_reached();
    }
    return MEMTX_OK;
}

static const MemoryRegionOps extioi_virt_ops = {
    .read_with_attrs = extioi_virt_readw,
    .write_with_attrs = extioi_virt_writew,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 8,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void loongarch_extioi_realize(DeviceState *dev, Error **errp)
{
    LoongArchExtIOICommonState *s = LOONGARCH_EXTIOI_COMMON(dev);
    LoongArchExtIOIClass *lec = LOONGARCH_EXTIOI_GET_CLASS(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    Error *local_err = NULL;
    int i;

    lec->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (s->features & BIT(EXTIOI_HAS_VIRT_EXTENSION)) {
        s->features |= EXTIOI_VIRT_HAS_FEATURES;
    } else {
        s->status |= BIT(EXTIOI_ENABLE);
    }

    if (kvm_irqchip_in_kernel()) {
        kvm_extioi_realize(dev, errp);
    } else {
        for (i = 0; i < EXTIOI_IRQS; i++) {
            sysbus_init_irq(sbd, &s->irq[i]);
        }

        qdev_init_gpio_in(dev, extioi_setirq, EXTIOI_IRQS);
        memory_region_init_io(&s->extioi_system_mem, OBJECT(s), &extioi_ops,
                              s, "extioi_system_mem", 0x900);
        sysbus_init_mmio(sbd, &s->extioi_system_mem);
        if (s->features & BIT(EXTIOI_HAS_VIRT_EXTENSION)) {
            memory_region_init_io(&s->virt_extend, OBJECT(s), &extioi_virt_ops,
                                  s, "extioi_virt", EXTIOI_VIRT_SIZE);
            sysbus_init_mmio(sbd, &s->virt_extend);
        }
    }
}

static void loongarch_extioi_reset_hold(Object *obj, ResetType type)
{
    LoongArchExtIOIClass *lec = LOONGARCH_EXTIOI_GET_CLASS(obj);

    if (lec->parent_phases.hold) {
        lec->parent_phases.hold(obj, type);
    }

    if (kvm_irqchip_in_kernel()) {
        kvm_extioi_put(obj, 0);
    }
}

static int vmstate_extioi_pre_save(void *opaque)
{
    if (kvm_irqchip_in_kernel()) {
        return kvm_extioi_get(opaque);
    }

    return 0;
}

static int vmstate_extioi_post_load(void *opaque, int version_id)
{
    LoongArchExtIOICommonState *s = LOONGARCH_EXTIOI_COMMON(opaque);
    int i, start_irq;

    if (kvm_irqchip_in_kernel()) {
        return kvm_extioi_put(opaque, version_id);
    }

    for (i = 0; i < (EXTIOI_IRQS / 4); i++) {
        start_irq = i * 4;
        extioi_update_sw_coremap(s, start_irq, s->coremap[i], false);
    }

    for (i = 0; i < (EXTIOI_IRQS_IPMAP_SIZE / 4); i++) {
        extioi_update_sw_ipmap(s, i, s->ipmap[i]);
    }

    return 0;
}

static void loongarch_extioi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    LoongArchExtIOIClass *lec = LOONGARCH_EXTIOI_CLASS(klass);
    LoongArchExtIOICommonClass *lecc = LOONGARCH_EXTIOI_COMMON_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    device_class_set_parent_realize(dc, loongarch_extioi_realize,
                                    &lec->parent_realize);
    resettable_class_set_parent_phases(rc, NULL, loongarch_extioi_reset_hold,
                                       NULL, &lec->parent_phases);
    lecc->pre_save  = vmstate_extioi_pre_save;
    lecc->post_load = vmstate_extioi_post_load;
}

static const TypeInfo loongarch_extioi_types[] = {
    {
        .name          = TYPE_LOONGARCH_EXTIOI,
        .parent        = TYPE_LOONGARCH_EXTIOI_COMMON,
        .instance_size = sizeof(LoongArchExtIOIState),
        .class_size    = sizeof(LoongArchExtIOIClass),
        .class_init    = loongarch_extioi_class_init,
    }
};

DEFINE_TYPES(loongarch_extioi_types)
