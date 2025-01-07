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

static void loongarch_ipi_class_init(ObjectClass *klass, void *data)
{
    LoongsonIPICommonClass *licc = LOONGSON_IPI_COMMON_CLASS(klass);
    LoongarchIPIClass *lic = LOONGARCH_IPI_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_parent_realize(dc, loongarch_ipi_realize,
                                    &lic->parent_realize);
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
