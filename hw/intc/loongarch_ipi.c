/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch IPI interrupt support
 *
 * Copyright (C) 2024 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "hw/boards.h"
#include "qapi/error.h"
#include "hw/intc/loongarch_ipi.h"
#include "hw/qdev-properties.h"
#include "system/kvm.h"
#include "target/loongarch/cpu.h"

static AddressSpace *get_iocsr_as(CPUState *cpu)
{
    return LOONGARCH_CPU(cpu)->env.address_space_iocsr;
}

static int loongarch_ipi_cmp(const void *a, const void *b)
{
   IPICore *ipi_a = (IPICore *)a;
   IPICore *ipi_b = (IPICore *)b;

   return ipi_a->arch_id - ipi_b->arch_id;
}

static int loongarch_cpu_by_arch_id(LoongsonIPICommonState *lics,
                                    int64_t arch_id, int *index, CPUState **pcs)
{
    IPICore ipi, *found;

    ipi.arch_id = arch_id;
    found = bsearch(&ipi, lics->cpu, lics->num_cpu, sizeof(IPICore),
                    loongarch_ipi_cmp);
    if (found && found->cpu) {
        if (index) {
            *index = found - lics->cpu;
        }

        if (pcs) {
            *pcs = found->cpu;
        }

        return MEMTX_OK;
    }

    return MEMTX_ERROR;
}

static IPICore *loongarch_ipi_get_cpu(LoongsonIPICommonState *lics,
                                      DeviceState *dev)
{
    CPUClass *k = CPU_GET_CLASS(dev);
    uint64_t arch_id = k->get_arch_id(CPU(dev));
    int i;

    for (i = 0; i < lics->num_cpu; i++) {
        if (lics->cpu[i].arch_id == arch_id) {
            return &lics->cpu[i];
        }
    }

    return NULL;
}

static void loongarch_ipi_realize(DeviceState *dev, Error **errp)
{
    LoongsonIPICommonState *lics = LOONGSON_IPI_COMMON(dev);
    LoongarchIPIClass *lic = LOONGARCH_IPI_GET_CLASS(dev);
    MachineState *machine = MACHINE(qdev_get_machine());
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    const CPUArchIdList *id_list;
    Error *local_err = NULL;
    int i;

    lic->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    assert(mc->possible_cpu_arch_ids);
    id_list = mc->possible_cpu_arch_ids(machine);
    lics->num_cpu = id_list->len;
    lics->cpu = g_new0(IPICore, lics->num_cpu);
    for (i = 0; i < lics->num_cpu; i++) {
        lics->cpu[i].arch_id = id_list->cpus[i].arch_id;
        lics->cpu[i].cpu = CPU(id_list->cpus[i].cpu);
        lics->cpu[i].ipi = lics;
        qdev_init_gpio_out(dev, &lics->cpu[i].irq, 1);
    }

    if (kvm_irqchip_in_kernel()) {
        kvm_ipi_realize(dev, errp);
    }
}

static void loongarch_ipi_reset_hold(Object *obj, ResetType type)
{
    int i;
    LoongarchIPIClass *lic = LOONGARCH_IPI_GET_CLASS(obj);
    LoongsonIPICommonState *lics = LOONGSON_IPI_COMMON(obj);
    IPICore *core;

    if (lic->parent_phases.hold) {
        lic->parent_phases.hold(obj, type);
    }

    for (i = 0; i < lics->num_cpu; i++) {
        core = lics->cpu + i;
        /* IPI with targeted CPU available however not present */
        if (!core->cpu) {
            continue;
        }

        core->status = 0;
        core->en = 0;
        core->set = 0;
        core->clear = 0;
        memset(core->buf, 0, sizeof(core->buf));
    }

    if (kvm_irqchip_in_kernel()) {
        kvm_ipi_put(obj, 0);
    }
}

static void loongarch_ipi_cpu_plug(HotplugHandler *hotplug_dev,
                                   DeviceState *dev, Error **errp)
{
    LoongsonIPICommonState *lics = LOONGSON_IPI_COMMON(hotplug_dev);
    Object *obj = OBJECT(dev);
    IPICore *core;
    int index;

    if (!object_dynamic_cast(obj, TYPE_LOONGARCH_CPU)) {
        warn_report("LoongArch extioi: Invalid %s device type",
                                       object_get_typename(obj));
        return;
    }

    core = loongarch_ipi_get_cpu(lics, dev);
    if (!core) {
        return;
    }

    core->cpu = CPU(dev);
    index = core - lics->cpu;

    /* connect ipi irq to cpu irq */
    qdev_connect_gpio_out(DEVICE(lics), index, qdev_get_gpio_in(dev, IRQ_IPI));
}

static void loongarch_ipi_cpu_unplug(HotplugHandler *hotplug_dev,
                                     DeviceState *dev, Error **errp)
{
    LoongsonIPICommonState *lics = LOONGSON_IPI_COMMON(hotplug_dev);
    Object *obj = OBJECT(dev);
    IPICore *core;

    if (!object_dynamic_cast(obj, TYPE_LOONGARCH_CPU)) {
        warn_report("LoongArch extioi: Invalid %s device type",
                                       object_get_typename(obj));
        return;
    }

    core = loongarch_ipi_get_cpu(lics, dev);
    if (!core) {
        return;
    }

    core->cpu = NULL;
}

static int loongarch_ipi_pre_save(void *opaque)
{
    if (kvm_irqchip_in_kernel()) {
        return kvm_ipi_get(opaque);
    }

    return 0;
}

static int loongarch_ipi_post_load(void *opaque, int version_id)
{
    if (kvm_irqchip_in_kernel()) {
        return kvm_ipi_put(opaque, version_id);
    }

    return 0;
}

static void loongarch_ipi_class_init(ObjectClass *klass, const void *data)
{
    LoongsonIPICommonClass *licc = LOONGSON_IPI_COMMON_CLASS(klass);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(klass);
    LoongarchIPIClass *lic = LOONGARCH_IPI_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_parent_realize(dc, loongarch_ipi_realize,
                                    &lic->parent_realize);
    resettable_class_set_parent_phases(rc, NULL, loongarch_ipi_reset_hold,
                                       NULL, &lic->parent_phases);
    licc->get_iocsr_as = get_iocsr_as;
    licc->cpu_by_arch_id = loongarch_cpu_by_arch_id;
    hc->plug = loongarch_ipi_cpu_plug;
    hc->unplug = loongarch_ipi_cpu_unplug;
    licc->pre_save = loongarch_ipi_pre_save;
    licc->post_load = loongarch_ipi_post_load;
}

static const TypeInfo loongarch_ipi_types[] = {
    {
        .name               = TYPE_LOONGARCH_IPI,
        .parent             = TYPE_LOONGSON_IPI_COMMON,
        .instance_size      = sizeof(LoongarchIPIState),
        .class_size         = sizeof(LoongarchIPIClass),
        .class_init         = loongarch_ipi_class_init,
        .interfaces         = (const InterfaceInfo[]) {
            { TYPE_HOTPLUG_HANDLER },
            { }
        },
    }
};

DEFINE_TYPES(loongarch_ipi_types)
