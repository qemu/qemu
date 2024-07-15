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

#define IPI_MBX_NUM           4

#define TYPE_LOONGSON_IPI "loongson_ipi"
OBJECT_DECLARE_TYPE(LoongsonIPIState, LoongsonIPIClass, LOONGSON_IPI)

typedef struct IPICore {
    LoongsonIPIState *ipi;
    uint32_t status;
    uint32_t en;
    uint32_t set;
    uint32_t clear;
    /* 64bit buf divide into 2 32bit buf */
    uint32_t buf[IPI_MBX_NUM * 2];
    qemu_irq irq;
} IPICore;

struct LoongsonIPIClass {
    LoongsonIPICommonClass parent_class;

    DeviceRealize parent_realize;
    DeviceUnrealize parent_unrealize;
};

struct LoongsonIPIState {
    LoongsonIPICommonState parent_obj;

    MemoryRegion *ipi_mmio_mem;
    MemoryRegion ipi_iocsr_mem;
    MemoryRegion ipi64_iocsr_mem;
    uint32_t num_cpu;
    IPICore *cpu;
};

#endif
