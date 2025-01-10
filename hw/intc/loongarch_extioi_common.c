/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Loongson extioi interrupt controller emulation
 * Copyright (C) 2024 Loongson Technology Corporation Limited
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/intc/loongarch_extioi_common.h"
#include "migration/vmstate.h"

static void loongarch_extioi_common_realize(DeviceState *dev, Error **errp)
{
    LoongArchExtIOICommonState *s = (LoongArchExtIOICommonState *)dev;
    MachineState *machine = MACHINE(qdev_get_machine());
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    const CPUArchIdList *id_list;
    int i;

    assert(mc->possible_cpu_arch_ids);
    id_list = mc->possible_cpu_arch_ids(machine);
    s->num_cpu = id_list->len;
    s->cpu = g_new0(ExtIOICore, s->num_cpu);
    if (s->cpu == NULL) {
        error_setg(errp, "Memory allocation for ExtIOICore faile");
        return;
    }

    for (i = 0; i < s->num_cpu; i++) {
        s->cpu[i].arch_id = id_list->cpus[i].arch_id;
        s->cpu[i].cpu = CPU(id_list->cpus[i].cpu);
    }
}

static int loongarch_extioi_common_pre_save(void *opaque)
{
    LoongArchExtIOICommonState *s = (LoongArchExtIOICommonState *)opaque;
    LoongArchExtIOICommonClass *lecc = LOONGARCH_EXTIOI_COMMON_GET_CLASS(s);

    if (lecc->pre_save) {
        return lecc->pre_save(s);
    }

    return 0;
}

static int loongarch_extioi_common_post_load(void *opaque, int version_id)
{
    LoongArchExtIOICommonState *s = (LoongArchExtIOICommonState *)opaque;
    LoongArchExtIOICommonClass *lecc = LOONGARCH_EXTIOI_COMMON_GET_CLASS(s);

    if (lecc->post_load) {
        return lecc->post_load(s, version_id);
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
    .name = "loongarch.extioi",
    .version_id = 3,
    .minimum_version_id = 3,
    .pre_save  = loongarch_extioi_common_pre_save,
    .post_load = loongarch_extioi_common_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(bounce, LoongArchExtIOICommonState,
                             EXTIOI_IRQS_GROUP_COUNT),
        VMSTATE_UINT32_ARRAY(nodetype, LoongArchExtIOICommonState,
                             EXTIOI_IRQS_NODETYPE_COUNT / 2),
        VMSTATE_UINT32_ARRAY(enable, LoongArchExtIOICommonState,
                             EXTIOI_IRQS / 32),
        VMSTATE_UINT32_ARRAY(isr, LoongArchExtIOICommonState,
                             EXTIOI_IRQS / 32),
        VMSTATE_UINT32_ARRAY(ipmap, LoongArchExtIOICommonState,
                             EXTIOI_IRQS_IPMAP_SIZE / 4),
        VMSTATE_UINT32_ARRAY(coremap, LoongArchExtIOICommonState,
                             EXTIOI_IRQS / 4),
        VMSTATE_STRUCT_VARRAY_POINTER_UINT32(cpu, LoongArchExtIOICommonState,
                             num_cpu, vmstate_extioi_core, ExtIOICore),
        VMSTATE_UINT32(features, LoongArchExtIOICommonState),
        VMSTATE_UINT32(status, LoongArchExtIOICommonState),
        VMSTATE_END_OF_LIST()
    }
};

static const Property extioi_properties[] = {
    DEFINE_PROP_BIT("has-virtualization-extension", LoongArchExtIOICommonState,
                    features, EXTIOI_HAS_VIRT_EXTENSION, 0),
};

static void loongarch_extioi_common_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    LoongArchExtIOICommonClass *lecc = LOONGARCH_EXTIOI_COMMON_CLASS(klass);

    device_class_set_parent_realize(dc, loongarch_extioi_common_realize,
                                    &lecc->parent_realize);
    device_class_set_props(dc, extioi_properties);
    dc->vmsd = &vmstate_loongarch_extioi;
}

static const TypeInfo loongarch_extioi_common_types[] = {
    {
        .name               = TYPE_LOONGARCH_EXTIOI_COMMON,
        .parent             = TYPE_SYS_BUS_DEVICE,
        .instance_size      = sizeof(LoongArchExtIOICommonState),
        .class_size         = sizeof(LoongArchExtIOICommonClass),
        .class_init         = loongarch_extioi_common_class_init,
        .abstract           = true,
    }
};

DEFINE_TYPES(loongarch_extioi_common_types)
