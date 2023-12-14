/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Loongson 3A5000 ext interrupt controller emulation
 *
 * Copyright (C) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/log.h"
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
        s->coreisr[cpu][irq_index] |= irq_mask;
        found = find_first_bit(s->sw_isr[cpu][ipnum], EXTIOI_IRQS);
        set_bit(irq, s->sw_isr[cpu][ipnum]);
        if (found < EXTIOI_IRQS) {
            /* other irq is handling, need not update parent irq level */
            return;
        }
    } else {
        s->coreisr[cpu][irq_index] &= ~irq_mask;
        clear_bit(irq, s->sw_isr[cpu][ipnum]);
        found = find_first_bit(s->sw_isr[cpu][ipnum], EXTIOI_IRQS);
        if (found < EXTIOI_IRQS) {
            /* other irq is handling, need not update parent irq level */
            return;
        }
    }
    qemu_set_irq(s->parent_irq[cpu][ipnum], level);
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
        *data = s->coreisr[cpu][index];
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

static MemTxResult extioi_writew(void *opaque, hwaddr addr,
                          uint64_t val, unsigned size,
                          MemTxAttrs attrs)
{
    LoongArchExtIOI *s = LOONGARCH_EXTIOI(opaque);
    int i, cpu, index, old_data, irq;
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
        /*
         * loongarch only support little endian,
         * so we paresd the value with little endian.
         */
        val = cpu_to_le64(val);
        for (i = 0; i < 4; i++) {
            uint8_t ipnum;
            ipnum = val & 0xff;
            ipnum = ctz32(ipnum);
            ipnum = (ipnum >= 4) ? 0 : ipnum;
            s->sw_ipmap[index * 4 + i] = ipnum;
            val = val >> 8;
        }

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
        old_data = s->coreisr[cpu][index];
        s->coreisr[cpu][index] = old_data & ~val;
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

            if (test_bit(irq, (unsigned long *)s->isr)) {
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

static const VMStateDescription vmstate_loongarch_extioi = {
    .name = TYPE_LOONGARCH_EXTIOI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(bounce, LoongArchExtIOI, EXTIOI_IRQS_GROUP_COUNT),
        VMSTATE_UINT32_2DARRAY(coreisr, LoongArchExtIOI, EXTIOI_CPUS,
                               EXTIOI_IRQS_GROUP_COUNT),
        VMSTATE_UINT32_ARRAY(nodetype, LoongArchExtIOI,
                             EXTIOI_IRQS_NODETYPE_COUNT / 2),
        VMSTATE_UINT32_ARRAY(enable, LoongArchExtIOI, EXTIOI_IRQS / 32),
        VMSTATE_UINT32_ARRAY(isr, LoongArchExtIOI, EXTIOI_IRQS / 32),
        VMSTATE_UINT32_ARRAY(ipmap, LoongArchExtIOI, EXTIOI_IRQS_IPMAP_SIZE / 4),
        VMSTATE_UINT32_ARRAY(coremap, LoongArchExtIOI, EXTIOI_IRQS / 4),
        VMSTATE_UINT8_ARRAY(sw_ipmap, LoongArchExtIOI, EXTIOI_IRQS_IPMAP_SIZE),
        VMSTATE_UINT8_ARRAY(sw_coremap, LoongArchExtIOI, EXTIOI_IRQS),

        VMSTATE_END_OF_LIST()
    }
};

static void loongarch_extioi_instance_init(Object *obj)
{
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);
    LoongArchExtIOI *s = LOONGARCH_EXTIOI(obj);
    int i, cpu, pin;

    for (i = 0; i < EXTIOI_IRQS; i++) {
        sysbus_init_irq(dev, &s->irq[i]);
    }

    qdev_init_gpio_in(DEVICE(obj), extioi_setirq, EXTIOI_IRQS);

    for (cpu = 0; cpu < EXTIOI_CPUS; cpu++) {
        memory_region_init_io(&s->extioi_iocsr_mem[cpu], OBJECT(s), &extioi_ops,
                              s, "extioi_iocsr", 0x900);
        sysbus_init_mmio(dev, &s->extioi_iocsr_mem[cpu]);
        for (pin = 0; pin < LS3A_INTC_IP; pin++) {
            qdev_init_gpio_out(DEVICE(obj), &s->parent_irq[cpu][pin], 1);
        }
    }
    memory_region_init_io(&s->extioi_system_mem, OBJECT(s), &extioi_ops,
                          s, "extioi_system_mem", 0x900);
    sysbus_init_mmio(dev, &s->extioi_system_mem);
}

static void loongarch_extioi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_loongarch_extioi;
}

static const TypeInfo loongarch_extioi_info = {
    .name          = TYPE_LOONGARCH_EXTIOI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = loongarch_extioi_instance_init,
    .instance_size = sizeof(struct LoongArchExtIOI),
    .class_init    = loongarch_extioi_class_init,
};

static void loongarch_extioi_register_types(void)
{
    type_register_static(&loongarch_extioi_info);
}

type_init(loongarch_extioi_register_types)
