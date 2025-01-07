/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch IPI interrupt support
 *
 * Copyright (C) 2024 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "hw/boards.h"
#include "qapi/error.h"
#include "hw/intc/loongarch_ipi.h"
#include "hw/qdev-properties.h"
#include "target/loongarch/cpu.h"

static AddressSpace *get_iocsr_as(CPUState *cpu)
{
    return LOONGARCH_CPU(cpu)->env.address_space_iocsr;
}

static int archid_cmp(const void *a, const void *b)
{
   CPUArchId *archid_a = (CPUArchId *)a;
   CPUArchId *archid_b = (CPUArchId *)b;

   return archid_a->arch_id - archid_b->arch_id;
}

static CPUArchId *find_cpu_by_archid(MachineState *ms, uint32_t id)
{
    CPUArchId apic_id, *found_cpu;

    apic_id.arch_id = id;
    found_cpu = bsearch(&apic_id, ms->possible_cpus->cpus,
                        ms->possible_cpus->len,
                        sizeof(*ms->possible_cpus->cpus),
                        archid_cmp);

    return found_cpu;
}

static CPUState *loongarch_cpu_by_arch_id(int64_t arch_id)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    CPUArchId *archid;

    archid = find_cpu_by_archid(machine, arch_id);
    if (archid) {
        return CPU(archid->cpu);
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
}

static const Property loongarch_ipi_properties[] = {
    DEFINE_PROP_UINT32("num-cpu", LoongsonIPICommonState, num_cpu, 1),
};

static void loongarch_ipi_class_init(ObjectClass *klass, void *data)
{
    LoongsonIPICommonClass *licc = LOONGSON_IPI_COMMON_CLASS(klass);
    LoongarchIPIClass *lic = LOONGARCH_IPI_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_parent_realize(dc, loongarch_ipi_realize,
                                    &lic->parent_realize);
    device_class_set_props(dc, loongarch_ipi_properties);
    licc->get_iocsr_as = get_iocsr_as;
    licc->cpu_by_arch_id = loongarch_cpu_by_arch_id;
}

static const TypeInfo loongarch_ipi_types[] = {
    {
        .name               = TYPE_LOONGARCH_IPI,
        .parent             = TYPE_LOONGSON_IPI_COMMON,
        .instance_size      = sizeof(LoongarchIPIState),
        .class_size         = sizeof(LoongarchIPIClass),
        .class_init         = loongarch_ipi_class_init,
    }
};

DEFINE_TYPES(loongarch_ipi_types)
