/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch direct interrupt controller.
 *
 * Copyright (C) 2025 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/intc/loongarch_pch_msi.h"
#include "hw/intc/loongarch_pch_pic.h"
#include "hw/intc/loongarch_dintc.h"
#include "hw/pci/msi.h"
#include "hw/misc/unimp.h"
#include "migration/vmstate.h"
#include "trace.h"
#include "hw/qdev-properties.h"
#include "target/loongarch/cpu.h"
#include "qemu/error-report.h"
#include "system/hw_accel.h"

/* msg addr field */
FIELD(MSG_ADDR, IRQ_NUM, 4, 8)
FIELD(MSG_ADDR, CPU_NUM, 12, 8)
FIELD(MSG_ADDR, FIX, 28, 12)

static uint64_t loongarch_dintc_mem_read(void *opaque,
                                        hwaddr addr, unsigned size)
{
    return 0;
}

static void do_set_vcpu_dintc_irq(CPUState *cs, run_on_cpu_data data)
{
    int irq = data.host_int;
    CPULoongArchState *env;

    env = &LOONGARCH_CPU(cs)->env;
    cpu_synchronize_state(cs);
    set_bit(irq, (unsigned long *)&env->CSR_MSGIS);
}

static void loongarch_dintc_mem_write(void *opaque, hwaddr addr,
                                     uint64_t val, unsigned size)
{
    int irq_num, cpu_num = 0;
    LoongArchDINTCState *s = LOONGARCH_DINTC(opaque);
    uint64_t msg_addr = addr + VIRT_DINTC_BASE;
    CPUState *cs;

    cpu_num = FIELD_EX64(msg_addr, MSG_ADDR, CPU_NUM);
    cs = cpu_by_arch_id(cpu_num);
    irq_num = FIELD_EX64(msg_addr, MSG_ADDR, IRQ_NUM);

    async_run_on_cpu(cs, do_set_vcpu_dintc_irq,
                         RUN_ON_CPU_HOST_INT(irq_num));
    qemu_set_irq(s->cpu[cpu_num].parent_irq, 1);
}

static const MemoryRegionOps loongarch_dintc_ops = {
    .read = loongarch_dintc_mem_read,
    .write = loongarch_dintc_mem_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void loongarch_dintc_realize(DeviceState *dev, Error **errp)
{
    LoongArchDINTCState *s = LOONGARCH_DINTC(dev);
    LoongArchDINTCClass *lac = LOONGARCH_DINTC_GET_CLASS(dev);
    MachineState *machine = MACHINE(qdev_get_machine());
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    const CPUArchIdList  *id_list;
    int i;

    Error *local_err = NULL;
    lac->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    assert(mc->possible_cpu_arch_ids);
    id_list = mc->possible_cpu_arch_ids(machine);
    s->num_cpu = id_list->len;
    s->cpu = g_new(DINTCCore, s->num_cpu);
    if (s->cpu == NULL) {
        error_setg(errp, "Memory allocation for DINTCCore fail");
        return;
    }

    for (i = 0; i < s->num_cpu; i++) {
        s->cpu[i].arch_id = id_list->cpus[i].arch_id;
        s->cpu[i].cpu = CPU(id_list->cpus[i].cpu);
        qdev_init_gpio_out(dev, &s->cpu[i].parent_irq, 1);
    }

    return;
}

static void loongarch_dintc_unrealize(DeviceState *dev)
{
    LoongArchDINTCState *s = LOONGARCH_DINTC(dev);
    g_free(s->cpu);
}

static void loongarch_dintc_init(Object *obj)
{
    LoongArchDINTCState *s = LOONGARCH_DINTC(obj);
    SysBusDevice *shd = SYS_BUS_DEVICE(obj);
    memory_region_init_io(&s->dintc_mmio, OBJECT(s), &loongarch_dintc_ops,
                          s, TYPE_LOONGARCH_DINTC, VIRT_DINTC_SIZE);
    sysbus_init_mmio(shd, &s->dintc_mmio);
    msi_nonbroken = true;
    return;
}

static DINTCCore *loongarch_dintc_get_cpu(LoongArchDINTCState *s,
                                        DeviceState *dev)
{
    CPUClass *k = CPU_GET_CLASS(dev);
    uint64_t arch_id = k->get_arch_id(CPU(dev));
    int i;

    for (i = 0; i < s->num_cpu; i++) {
        if (s->cpu[i].arch_id == arch_id) {
            return &s->cpu[i];
        }
    }

    return NULL;
}

static void loongarch_dintc_cpu_plug(HotplugHandler *hotplug_dev,
                                   DeviceState *dev, Error **errp)
{
    LoongArchDINTCState *s = LOONGARCH_DINTC(hotplug_dev);
    Object *obj = OBJECT(dev);
    DINTCCore *core;
    int index;

    if (!object_dynamic_cast(obj, TYPE_LOONGARCH_CPU)) {
        warn_report("LoongArch DINTC: Invalid %s device type",
                                       object_get_typename(obj));
        return;
    }
    core = loongarch_dintc_get_cpu(s, dev);
    if (!core) {
        return;
    }

    core->cpu = CPU(dev);
    index = core - s->cpu;

    /* connect dintc msg irq to cpu irq */
    qdev_connect_gpio_out(DEVICE(s), index, qdev_get_gpio_in(dev, INT_DMSI));
    return;
}

static void loongarch_dintc_cpu_unplug(HotplugHandler *hotplug_dev,
                                     DeviceState *dev, Error **errp)
{
    LoongArchDINTCState *s = LOONGARCH_DINTC(hotplug_dev);
    Object *obj = OBJECT(dev);
    DINTCCore *core;

    if (!object_dynamic_cast(obj, TYPE_LOONGARCH_CPU)) {
        warn_report("LoongArch DINTC: Invalid %s device type",
                                       object_get_typename(obj));
        return;
    }

    core = loongarch_dintc_get_cpu(s, dev);

    if (!core) {
        return;
    }

    core->cpu = NULL;
}

static void loongarch_dintc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(klass);
    LoongArchDINTCClass *lac = LOONGARCH_DINTC_CLASS(klass);

    dc->unrealize = loongarch_dintc_unrealize;
    device_class_set_parent_realize(dc, loongarch_dintc_realize,
                                    &lac->parent_realize);
    hc->plug = loongarch_dintc_cpu_plug;
    hc->unplug = loongarch_dintc_cpu_unplug;
}

static const TypeInfo loongarch_dintc_info = {
    .name          = TYPE_LOONGARCH_DINTC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LoongArchDINTCState),
    .instance_init = loongarch_dintc_init,
    .class_size    = sizeof(LoongArchDINTCClass),
    .class_init    = loongarch_dintc_class_init,
    .interfaces    = (const InterfaceInfo[]) {
        { TYPE_HOTPLUG_HANDLER },
        { }
    },
};

static void loongarch_dintc_register_types(void)
{
    type_register_static(&loongarch_dintc_info);
}

type_init(loongarch_dintc_register_types)
