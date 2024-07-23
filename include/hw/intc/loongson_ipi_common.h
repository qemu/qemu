/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Loongson ipi interrupt header files
 *
 * Copyright (C) 2021 Loongson Technology Corporation Limited
 */

#ifndef HW_LOONGSON_IPI_COMMON_H
#define HW_LOONGSON_IPI_COMMON_H

#include "qom/object.h"
#include "hw/sysbus.h"

#define TYPE_LOONGSON_IPI_COMMON "loongson_ipi_common"
OBJECT_DECLARE_TYPE(LoongsonIPICommonState,
                    LoongsonIPICommonClass, LOONGSON_IPI_COMMON)

struct LoongsonIPICommonState {
    SysBusDevice parent_obj;
};

struct LoongsonIPICommonClass {
    SysBusDeviceClass parent_class;
};

#endif
