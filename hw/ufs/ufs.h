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

typedef enum UfsRequestState {
    UFS_REQUEST_IDLE = 0,
    UFS_REQUEST_READY = 1,
    UFS_REQUEST_RUNNING = 2,
    UFS_REQUEST_COMPLETE = 3,
    UFS_REQUEST_ERROR = 4,
} UfsRequestState;

typedef enum UfsReqResult {
    UFS_REQUEST_SUCCESS = 0,
    UFS_REQUEST_FAIL = 1,
} UfsReqResult;

typedef struct UfsRequest {
    struct UfsHc *hc;
    UfsRequestState state;
    int slot;

    UtpTransferReqDesc utrd;
    UtpUpiuReq req_upiu;
    UtpUpiuRsp rsp_upiu;

    /* for scsi command */
    QEMUSGList *sg;
} UfsRequest;

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
    UfsRequest *req_list;

    DeviceDescriptor device_desc;
    GeometryDescriptor geometry_desc;
    Attributes attributes;
    Flags flags;

    qemu_irq irq;
    QEMUBH *doorbell_bh;
    QEMUBH *complete_bh;
} UfsHc;

#define TYPE_UFS "ufs"
#define UFS(obj) OBJECT_CHECK(UfsHc, (obj), TYPE_UFS)

typedef enum UfsQueryFlagPerm {
    UFS_QUERY_FLAG_NONE = 0x0,
    UFS_QUERY_FLAG_READ = 0x1,
    UFS_QUERY_FLAG_SET = 0x2,
    UFS_QUERY_FLAG_CLEAR = 0x4,
    UFS_QUERY_FLAG_TOGGLE = 0x8,
} UfsQueryFlagPerm;

typedef enum UfsQueryAttrPerm {
    UFS_QUERY_ATTR_NONE = 0x0,
    UFS_QUERY_ATTR_READ = 0x1,
    UFS_QUERY_ATTR_WRITE = 0x2,
} UfsQueryAttrPerm;

#endif /* HW_UFS_UFS_H */
