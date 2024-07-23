/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Loongson ipi interrupt header files
 *
 * Copyright (C) 2021 Loongson Technology Corporation Limited
 */

#ifndef HW_LOONGSON_IPI_H
#define HW_LOONGSON_IPI_H

#include "qom/object.h"
#include "hw/intc/loongson_ipi_common.h"
#include "hw/sysbus.h"

#define TYPE_LOONGSON_IPI "loongson_ipi"
OBJECT_DECLARE_TYPE(LoongsonIPIState, LoongsonIPIClass, LOONGSON_IPI)

struct LoongsonIPIClass {
    LoongsonIPICommonClass parent_class;

    DeviceRealize parent_realize;
    DeviceUnrealize parent_unrealize;
};

struct LoongsonIPIState {
    LoongsonIPICommonState parent_obj;

    MemoryRegion *ipi_mmio_mem;
};

#endif
