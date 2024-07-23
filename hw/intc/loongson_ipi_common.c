/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Loongson IPI interrupt common support
 *
 * Copyright (C) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/intc/loongson_ipi_common.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

static const VMStateDescription vmstate_ipi_core = {
    .name = "ipi-single",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(status, IPICore),
        VMSTATE_UINT32(en, IPICore),
        VMSTATE_UINT32(set, IPICore),
        VMSTATE_UINT32(clear, IPICore),
        VMSTATE_UINT32_ARRAY(buf, IPICore, IPI_MBX_NUM * 2),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_loongson_ipi_common = {
    .name = "loongson_ipi",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_VARRAY_POINTER_UINT32(cpu, LoongsonIPICommonState,
                                             num_cpu, vmstate_ipi_core,
                                             IPICore),
        VMSTATE_END_OF_LIST()
    }
};

static Property ipi_common_properties[] = {
    DEFINE_PROP_UINT32("num-cpu", LoongsonIPICommonState, num_cpu, 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void loongson_ipi_common_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, ipi_common_properties);
    dc->vmsd = &vmstate_loongson_ipi_common;
}

static const TypeInfo loongarch_ipi_common_types[] = {
    {
        .name               = TYPE_LOONGSON_IPI_COMMON,
        .parent             = TYPE_SYS_BUS_DEVICE,
        .instance_size      = sizeof(LoongsonIPICommonState),
        .class_size         = sizeof(LoongsonIPICommonClass),
        .class_init         = loongson_ipi_common_class_init,
        .abstract           = true,
    }
};

DEFINE_TYPES(loongarch_ipi_common_types)
