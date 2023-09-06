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

typedef struct UfsBusClass {
    BusClass parent_class;
    bool (*parent_check_address)(BusState *bus, DeviceState *dev, Error **errp);
} UfsBusClass;

typedef struct UfsBus {
    SCSIBus parent_bus;
} UfsBus;

#define TYPE_UFS_BUS "ufs-bus"
DECLARE_OBJ_CHECKERS(UfsBus, UfsBusClass, UFS_BUS, TYPE_UFS_BUS)

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
    UFS_REQUEST_NO_COMPLETE = 2,
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

typedef struct UfsLu {
    SCSIDevice qdev;
    uint8_t lun;
    UnitDescriptor unit_desc;
} UfsLu;

typedef struct UfsWLu {
    SCSIDevice qdev;
    uint8_t lun;
} UfsWLu;

typedef struct UfsParams {
    char *serial;
    uint8_t nutrs; /* Number of UTP Transfer Request Slots */
    uint8_t nutmrs; /* Number of UTP Task Management Request Slots */
} UfsParams;

typedef struct UfsHc {
    PCIDevice parent_obj;
    UfsBus bus;
    MemoryRegion iomem;
    UfsReg reg;
    UfsParams params;
    uint32_t reg_size;
    UfsRequest *req_list;

    UfsLu *lus[UFS_MAX_LUS];
    UfsWLu *report_wlu;
    UfsWLu *dev_wlu;
    UfsWLu *boot_wlu;
    UfsWLu *rpmb_wlu;
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

#define TYPE_UFS_LU "ufs-lu"
#define UFSLU(obj) OBJECT_CHECK(UfsLu, (obj), TYPE_UFS_LU)

#define TYPE_UFS_WLU "ufs-wlu"
#define UFSWLU(obj) OBJECT_CHECK(UfsWLu, (obj), TYPE_UFS_WLU)

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

static inline bool is_wlun(uint8_t lun)
{
    return (lun == UFS_UPIU_REPORT_LUNS_WLUN ||
            lun == UFS_UPIU_UFS_DEVICE_WLUN || lun == UFS_UPIU_BOOT_WLUN ||
            lun == UFS_UPIU_RPMB_WLUN);
}

#endif /* HW_UFS_UFS_H */
