/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Loongson extioi interrupt controller emulation
 * Copyright (C) 2024 Loongson Technology Corporation Limited
 */

static void loongarch_extioi_common_realize(DeviceState *dev, Error **errp)
{
    LoongArchExtIOICommonState *s = (LoongArchExtIOICommonState *)dev;

    if (s->num_cpu == 0) {
        error_setg(errp, "num-cpu must be at least 1");
        return;
    }
}

static int loongarch_extioi_common_post_load(void *opaque, int version_id)
{
    return vmstate_extioi_post_load(opaque, version_id);
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
    DEFINE_PROP_UINT32("num-cpu", LoongArchExtIOICommonState, num_cpu, 1),
    DEFINE_PROP_BIT("has-virtualization-extension", LoongArchExtIOICommonState,
                    features, EXTIOI_HAS_VIRT_EXTENSION, 0),
    DEFINE_PROP_END_OF_LIST(),
};
