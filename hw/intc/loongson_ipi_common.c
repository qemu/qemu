/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Loongson IPI interrupt common support
 *
 * Copyright (C) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/intc/loongson_ipi_common.h"

static const TypeInfo loongarch_ipi_common_types[] = {
    {
        .name               = TYPE_LOONGSON_IPI_COMMON,
        .parent             = TYPE_SYS_BUS_DEVICE,
        .instance_size      = sizeof(LoongsonIPICommonState),
        .class_size         = sizeof(LoongsonIPICommonClass),
        .abstract           = true,
    }
};

DEFINE_TYPES(loongarch_ipi_common_types)
