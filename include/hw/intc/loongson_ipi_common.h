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
#include "exec/memattrs.h"
#include "system/memory.h"

#define IPI_MBX_NUM           4

#define TYPE_LOONGSON_IPI_COMMON "loongson_ipi_common"
OBJECT_DECLARE_TYPE(LoongsonIPICommonState,
                    LoongsonIPICommonClass, LOONGSON_IPI_COMMON)

typedef struct IPICore {
    LoongsonIPICommonState *ipi;
    uint32_t status;
    uint32_t en;
    uint32_t set;
    uint32_t clear;
    /* 64bit buf divide into 2 32-bit buf */
    uint32_t buf[IPI_MBX_NUM * 2];
    qemu_irq irq;
    uint64_t arch_id;
    CPUState *cpu;
} IPICore;

struct LoongsonIPICommonState {
    SysBusDevice parent_obj;

    MemoryRegion ipi_iocsr_mem;
    MemoryRegion ipi64_iocsr_mem;
    uint32_t num_cpu;
    IPICore *cpu;
};

struct LoongsonIPICommonClass {
    SysBusDeviceClass parent_class;

    DeviceRealize parent_realize;
    DeviceUnrealize parent_unrealize;
    AddressSpace *(*get_iocsr_as)(CPUState *cpu);
    int (*cpu_by_arch_id)(LoongsonIPICommonState *lics, int64_t id,
                          int *index, CPUState **pcs);
    int (*pre_save)(void *opaque);
    int (*post_load)(void *opaque, int version_id);
};

MemTxResult loongson_ipi_core_readl(void *opaque, hwaddr addr, uint64_t *data,
                                    unsigned size, MemTxAttrs attrs);
MemTxResult loongson_ipi_core_writel(void *opaque, hwaddr addr, uint64_t val,
                                     unsigned size, MemTxAttrs attrs);

/* Mainy used by iocsr read and write */
#define SMP_IPI_MAILBOX         0x1000ULL

#define CORE_STATUS_OFF         0x0
#define CORE_EN_OFF             0x4
#define CORE_SET_OFF            0x8
#define CORE_CLEAR_OFF          0xc
#define CORE_BUF_20             0x20
#define CORE_BUF_28             0x28
#define CORE_BUF_30             0x30
#define CORE_BUF_38             0x38
#define IOCSR_IPI_SEND          0x40
#define IOCSR_MAIL_SEND         0x48
#define IOCSR_ANY_SEND          0x158

#define MAIL_SEND_ADDR          (SMP_IPI_MAILBOX + IOCSR_MAIL_SEND)
#define MAIL_SEND_OFFSET        0
#define ANY_SEND_OFFSET         (IOCSR_ANY_SEND - IOCSR_MAIL_SEND)

#endif
