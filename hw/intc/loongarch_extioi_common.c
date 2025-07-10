/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Loongson extioi interrupt controller emulation
 * Copyright (C) 2024 Loongson Technology Corporation Limited
 */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/intc/loongarch_extioi_common.h"
#include "migration/vmstate.h"
#include "target/loongarch/cpu.h"

static ExtIOICore *loongarch_extioi_get_cpu(LoongArchExtIOICommonState *s,
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

static void loongarch_extioi_cpu_plug(HotplugHandler *hotplug_dev,
                                      DeviceState *dev, Error **errp)
{
    LoongArchExtIOICommonState *s = LOONGARCH_EXTIOI_COMMON(hotplug_dev);
    Object *obj = OBJECT(dev);
    ExtIOICore *core;
    int pin, index;

    if (!object_dynamic_cast(obj, TYPE_LOONGARCH_CPU)) {
        warn_report("LoongArch extioi: Invalid %s device type",
                                       object_get_typename(obj));
        return;
    }

    core = loongarch_extioi_get_cpu(s, dev);
    if (!core) {
        return;
    }

    core->cpu = CPU(dev);
    index = core - s->cpu;

    /*
     * connect extioi irq to the cpu irq
     * cpu_pin[LS3A_INTC_IP + 2 : 2] <= intc_pin[LS3A_INTC_IP : 0]
     */
    for (pin = 0; pin < LS3A_INTC_IP; pin++) {
        qdev_connect_gpio_out(DEVICE(s), index * LS3A_INTC_IP + pin,
                              qdev_get_gpio_in(dev, pin + 2));
    }
}

static void loongarch_extioi_cpu_unplug(HotplugHandler *hotplug_dev,
                                        DeviceState *dev, Error **errp)
{
    LoongArchExtIOICommonState *s = LOONGARCH_EXTIOI_COMMON(hotplug_dev);
    Object *obj = OBJECT(dev);
    ExtIOICore *core;

    if (!object_dynamic_cast(obj, TYPE_LOONGARCH_CPU)) {
        warn_report("LoongArch extioi: Invalid %s device type",
                                       object_get_typename(obj));
        return;
    }

    core = loongarch_extioi_get_cpu(s, dev);
    if (!core) {
        return;
    }

    core->cpu = NULL;
}

static void loongarch_extioi_common_realize(DeviceState *dev, Error **errp)
{
    LoongArchExtIOICommonState *s = (LoongArchExtIOICommonState *)dev;
    MachineState *machine = MACHINE(qdev_get_machine());
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    const CPUArchIdList *id_list;
    int i, pin;

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

        for (pin = 0; pin < LS3A_INTC_IP; pin++) {
            qdev_init_gpio_out(dev, &s->cpu[i].parent_irq[pin], 1);
        }
    }
}

static void loongarch_extioi_common_unrealize(DeviceState *dev)
{
    LoongArchExtIOICommonState *s = LOONGARCH_EXTIOI_COMMON(dev);

    g_free(s->cpu);
}

static void loongarch_extioi_common_reset_hold(Object *obj, ResetType type)
{
    LoongArchExtIOICommonClass *lecc = LOONGARCH_EXTIOI_COMMON_GET_CLASS(obj);
    LoongArchExtIOICommonState *s = LOONGARCH_EXTIOI_COMMON(obj);
    ExtIOICore *core;
    int i;

    if (lecc->parent_phases.hold) {
        lecc->parent_phases.hold(obj, type);
    }

    /* Clear HW registers for the board */
    memset(s->nodetype, 0, sizeof(s->nodetype));
    memset(s->bounce, 0, sizeof(s->bounce));
    memset(s->isr, 0, sizeof(s->isr));
    memset(s->enable, 0, sizeof(s->enable));
    memset(s->ipmap, 0, sizeof(s->ipmap));
    memset(s->coremap, 0, sizeof(s->coremap));
    memset(s->sw_pending, 0, sizeof(s->sw_pending));
    memset(s->sw_ipmap, 0, sizeof(s->sw_ipmap));
    memset(s->sw_coremap, 0, sizeof(s->sw_coremap));

    for (i = 0; i < s->num_cpu; i++) {
        core = s->cpu + i;
        /* EXTIOI with targeted CPU available however not present */
        if (!core->cpu) {
            continue;
        }

        /* Clear HW registers for CPUs */
        memset(core->coreisr, 0, sizeof(core->coreisr));
        memset(core->sw_isr, 0, sizeof(core->sw_isr));
    }

    s->status = 0;
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

static void loongarch_extioi_common_class_init(ObjectClass *klass,
                                               const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    LoongArchExtIOICommonClass *lecc = LOONGARCH_EXTIOI_COMMON_CLASS(klass);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    device_class_set_parent_realize(dc, loongarch_extioi_common_realize,
                                    &lecc->parent_realize);
    device_class_set_parent_unrealize(dc, loongarch_extioi_common_unrealize,
                                      &lecc->parent_unrealize);
    resettable_class_set_parent_phases(rc, NULL,
                                       loongarch_extioi_common_reset_hold,
                                       NULL, &lecc->parent_phases);
    device_class_set_props(dc, extioi_properties);
    dc->vmsd = &vmstate_loongarch_extioi;
    hc->plug = loongarch_extioi_cpu_plug;
    hc->unplug = loongarch_extioi_cpu_unplug;
}

static const TypeInfo loongarch_extioi_common_types[] = {
    {
        .name               = TYPE_LOONGARCH_EXTIOI_COMMON,
        .parent             = TYPE_SYS_BUS_DEVICE,
        .instance_size      = sizeof(LoongArchExtIOICommonState),
        .class_size         = sizeof(LoongArchExtIOICommonClass),
        .class_init         = loongarch_extioi_common_class_init,
        .interfaces         = (const InterfaceInfo[]) {
            { TYPE_HOTPLUG_HANDLER },
            { }
        },
        .abstract           = true,
    }
};

DEFINE_TYPES(loongarch_extioi_common_types)
