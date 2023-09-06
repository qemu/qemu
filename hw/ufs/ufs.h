/*
 * QEMU UFS
 *
 * Copyright (c) 2023 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Written by Jeuk Kim <jeuk20.kim@samsung.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_UFS_UFS_H
#define HW_UFS_UFS_H

#include "hw/pci/pci_device.h"
#include "hw/scsi/scsi.h"
#include "block/ufs.h"

#define UFS_MAX_LUS 32
#define UFS_BLOCK_SIZE 4096

typedef struct UfsParams {
    char *serial;
    uint8_t nutrs; /* Number of UTP Transfer Request Slots */
    uint8_t nutmrs; /* Number of UTP Task Management Request Slots */
} UfsParams;

typedef struct UfsHc {
    PCIDevice parent_obj;
    MemoryRegion iomem;
    UfsReg reg;
    UfsParams params;
    uint32_t reg_size;

    qemu_irq irq;
    QEMUBH *doorbell_bh;
    QEMUBH *complete_bh;
} UfsHc;

#define TYPE_UFS "ufs"
#define UFS(obj) OBJECT_CHECK(UfsHc, (obj), TYPE_UFS)

#endif /* HW_UFS_UFS_H */
