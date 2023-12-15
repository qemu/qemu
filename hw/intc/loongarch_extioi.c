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
#include "hw/sysbus.h"
#include "hw/loongarch/virt.h"
#include "hw/qdev-properties.h"
#include "exec/address-spaces.h"
#include "hw/intc/loongarch_extioi.h"
#include "migration/vmstate.h"
#include "trace.h"


static void extioi_update_irq(LoongArchExtIOI *s, int irq, int level)
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
    LoongArchExtIOI *s = LOONGARCH_EXTIOI(opaque);
    trace_loongarch_extioi_setirq(irq, level);
    if (level) {
        /*
         * s->isr should be used in vmstate structure,
         * but it not support 'unsigned long',
         * so we have to switch it.
         */
        set_bit(irq, (unsigned long *)s->isr);
    } else {
        clear_bit(irq, (unsigned long *)s->isr);
    }
    extioi_update_irq(s, irq, level);
}

static MemTxResult extioi_readw(void *opaque, hwaddr addr, uint64_t *data,
                                unsigned size, MemTxAttrs attrs)
{
    LoongArchExtIOI *s = LOONGARCH_EXTIOI(opaque);
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

static inline void extioi_enable_irq(LoongArchExtIOI *s, int index,\
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

static inline void extioi_update_sw_coremap(LoongArchExtIOI *s, int irq,
                                            uint64_t val, bool notify)
{
    int i, cpu;

    /*
     * loongarch only support little endian,
     * so we paresd the value with little endian.
     */
    val = cpu_to_le64(val);

    for (i = 0; i < 4; i++) {
        cpu = val & 0xff;
        cpu = ctz32(cpu);
        cpu = (cpu >= 4) ? 0 : cpu;
        val = val >> 8;

        if (s->sw_coremap[irq + i] == cpu) {
            continue;
        }

        if (notify && test_bit(irq, (unsigned long *)s->isr)) {
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

static inline void extioi_update_sw_ipmap(LoongArchExtIOI *s, int index,
                                          uint64_t val)
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
    LoongArchExtIOI *s = LOONGARCH_EXTIOI(opaque);
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

static void loongarch_extioi_realize(DeviceState *dev, Error **errp)
{
    LoongArchExtIOI *s = LOONGARCH_EXTIOI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    int i, pin;

    if (s->num_cpu == 0) {
        error_setg(errp, "num-cpu must be at least 1");
        return;
    }

    for (i = 0; i < EXTIOI_IRQS; i++) {
        sysbus_init_irq(sbd, &s->irq[i]);
    }

    qdev_init_gpio_in(dev, extioi_setirq, EXTIOI_IRQS);
    memory_region_init_io(&s->extioi_system_mem, OBJECT(s), &extioi_ops,
                          s, "extioi_system_mem", 0x900);
    sysbus_init_mmio(sbd, &s->extioi_system_mem);
    s->cpu = g_new0(ExtIOICore, s->num_cpu);
    if (s->cpu == NULL) {
        error_setg(errp, "Memory allocation for ExtIOICore faile");
        return;
    }

    for (i = 0; i < s->num_cpu; i++) {
        for (pin = 0; pin < LS3A_INTC_IP; pin++) {
            qdev_init_gpio_out(dev, &s->cpu[i].parent_irq[pin], 1);
        }
    }
}

static void loongarch_extioi_finalize(Object *obj)
{
    LoongArchExtIOI *s = LOONGARCH_EXTIOI(obj);

    g_free(s->cpu);
}

static int vmstate_extioi_post_load(void *opaque, int version_id)
{
    LoongArchExtIOI *s = LOONGARCH_EXTIOI(opaque);
    int i, start_irq;

    for (i = 0; i < (EXTIOI_IRQS / 4); i++) {
        start_irq = i * 4;
        extioi_update_sw_coremap(s, start_irq, s->coremap[i], false);
    }

    for (i = 0; i < (EXTIOI_IRQS_IPMAP_SIZE / 4); i++) {
        extioi_update_sw_ipmap(s, i, s->ipmap[i]);
    }

    return 0;
}

static const VMStateDescription vmstate_extioi_core = {
    .name = "extioi-core",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(coreisr, ExtIOICore, EXTIOI_IRQS_GROUP_COUNT),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_loongarch_extioi = {
    .name = TYPE_LOONGARCH_EXTIOI,
    .version_id = 2,
    .minimum_version_id = 2,
    .post_load = vmstate_extioi_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(bounce, LoongArchExtIOI, EXTIOI_IRQS_GROUP_COUNT),
        VMSTATE_UINT32_ARRAY(nodetype, LoongArchExtIOI,
                             EXTIOI_IRQS_NODETYPE_COUNT / 2),
        VMSTATE_UINT32_ARRAY(enable, LoongArchExtIOI, EXTIOI_IRQS / 32),
        VMSTATE_UINT32_ARRAY(isr, LoongArchExtIOI, EXTIOI_IRQS / 32),
        VMSTATE_UINT32_ARRAY(ipmap, LoongArchExtIOI, EXTIOI_IRQS_IPMAP_SIZE / 4),
        VMSTATE_UINT32_ARRAY(coremap, LoongArchExtIOI, EXTIOI_IRQS / 4),

        VMSTATE_STRUCT_VARRAY_POINTER_UINT32(cpu, LoongArchExtIOI, num_cpu,
                         vmstate_extioi_core, ExtIOICore),
        VMSTATE_END_OF_LIST()
    }
};

static Property extioi_properties[] = {
    DEFINE_PROP_UINT32("num-cpu", LoongArchExtIOI, num_cpu, 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void loongarch_extioi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = loongarch_extioi_realize;
    device_class_set_props(dc, extioi_properties);
    dc->vmsd = &vmstate_loongarch_extioi;
}

static const TypeInfo loongarch_extioi_info = {
    .name          = TYPE_LOONGARCH_EXTIOI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(struct LoongArchExtIOI),
    .class_init    = loongarch_extioi_class_init,
    .instance_finalize = loongarch_extioi_finalize,
};

static void loongarch_extioi_register_types(void)
{
    type_register_static(&loongarch_extioi_info);
}

type_init(loongarch_extioi_register_types)
