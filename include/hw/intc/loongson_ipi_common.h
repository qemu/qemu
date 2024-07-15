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
