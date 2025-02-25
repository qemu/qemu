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
#define UFS_MAX_MCQ_QNUM 32
#define UFS_BLOCK_SIZE_SHIFT 12
#define UFS_BLOCK_SIZE (1 << UFS_BLOCK_SIZE_SHIFT)

typedef struct UfsBusClass {
    BusClass parent_class;
    bool (*parent_check_address)(BusState *bus, DeviceState *dev, Error **errp);
} UfsBusClass;

typedef struct UfsBus {
    BusState parent_bus;
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

#define UFS_INVALID_SLOT (-1)
typedef struct UfsRequest {
    struct UfsHc *hc;
    UfsRequestState state;
    int slot; /* -1 when it's a MCQ request */

    UtpTransferReqDesc utrd;
    UtpUpiuReq req_upiu;
    UtpUpiuRsp rsp_upiu;

    /* for scsi command */
    QEMUSGList *sg;
    uint32_t data_len;

    /* for MCQ */
    struct UfsSq *sq;
    struct UfsCqEntry cqe;
    QTAILQ_ENTRY(UfsRequest) entry;
} UfsRequest;

static inline bool ufs_mcq_req(UfsRequest *req)
{
    return req->sq != NULL;
}

struct UfsLu;
typedef UfsReqResult (*UfsScsiOp)(struct UfsLu *, UfsRequest *);

typedef struct UfsLu {
    DeviceState qdev;
    uint8_t lun;
    UnitDescriptor unit_desc;
    SCSIBus bus;
    SCSIDevice *scsi_dev;
    BlockConf conf;
    UfsScsiOp scsi_op;
} UfsLu;

typedef struct UfsParams {
    char *serial;
    uint8_t nutrs; /* Number of UTP Transfer Request Slots */
    uint8_t nutmrs; /* Number of UTP Task Management Request Slots */
    bool mcq; /* Multiple Command Queue support */
    uint8_t mcq_qcfgptr; /* MCQ Queue Configuration Pointer in MCQCAP */
    uint8_t mcq_maxq; /* MCQ Maximum number of Queues */
} UfsParams;

/*
 * MCQ Properties
 */
typedef struct UfsSq {
    struct UfsHc *u;
    uint8_t sqid;
    struct UfsCq *cq;
    uint64_t addr;
    uint16_t size; /* A number of entries (qdepth) */

    QEMUBH *bh; /* Bottom half to process requests in async */
    UfsRequest *req;
    QTAILQ_HEAD(, UfsRequest) req_list; /* Free request list */
} UfsSq;

typedef struct UfsCq {
    struct UfsHc *u;
    uint8_t cqid;
    uint64_t addr;
    uint16_t size; /* A number of entries (qdepth) */

    QEMUBH *bh;
    QTAILQ_HEAD(, UfsRequest) req_list;
} UfsCq;

typedef struct UfsHc {
    PCIDevice parent_obj;
    UfsBus bus;
    MemoryRegion iomem;
    UfsReg reg;
    UfsMcqReg mcq_reg[UFS_MAX_MCQ_QNUM];
    UfsMcqOpReg mcq_op_reg[UFS_MAX_MCQ_QNUM];
    UfsParams params;
    uint32_t reg_size;
    UfsRequest *req_list;

    UfsLu *lus[UFS_MAX_LUS];
    UfsLu report_wlu;
    UfsLu dev_wlu;
    UfsLu boot_wlu;
    UfsLu rpmb_wlu;
    DeviceDescriptor device_desc;
    GeometryDescriptor geometry_desc;
    Attributes attributes;
    Flags flags;

    qemu_irq irq;
    QEMUBH *doorbell_bh;
    QEMUBH *complete_bh;

    /* MCQ properties */
    UfsSq *sq[UFS_MAX_MCQ_QNUM];
    UfsCq *cq[UFS_MAX_MCQ_QNUM];

    uint8_t temperature;
} UfsHc;

static inline uint32_t ufs_mcq_sq_tail(UfsHc *u, uint32_t qid)
{
    return u->mcq_op_reg[qid].sq.tp;
}

static inline void ufs_mcq_update_sq_tail(UfsHc *u, uint32_t qid, uint32_t db)
{
    u->mcq_op_reg[qid].sq.tp = db;
}

static inline uint32_t ufs_mcq_sq_head(UfsHc *u, uint32_t qid)
{
    return u->mcq_op_reg[qid].sq.hp;
}

static inline void ufs_mcq_update_sq_head(UfsHc *u, uint32_t qid, uint32_t db)
{
    u->mcq_op_reg[qid].sq.hp = db;
}

static inline bool ufs_mcq_sq_empty(UfsHc *u, uint32_t qid)
{
    return ufs_mcq_sq_tail(u, qid) == ufs_mcq_sq_head(u, qid);
}

static inline uint32_t ufs_mcq_cq_tail(UfsHc *u, uint32_t qid)
{
    return u->mcq_op_reg[qid].cq.tp;
}

static inline void ufs_mcq_update_cq_tail(UfsHc *u, uint32_t qid, uint32_t db)
{
    u->mcq_op_reg[qid].cq.tp = db;
}

static inline uint32_t ufs_mcq_cq_head(UfsHc *u, uint32_t qid)
{
    return u->mcq_op_reg[qid].cq.hp;
}

static inline void ufs_mcq_update_cq_head(UfsHc *u, uint32_t qid, uint32_t db)
{
    u->mcq_op_reg[qid].cq.hp = db;
}

static inline bool ufs_mcq_cq_empty(UfsHc *u, uint32_t qid)
{
    return ufs_mcq_cq_tail(u, qid) == ufs_mcq_cq_head(u, qid);
}

#define TYPE_UFS "ufs"
#define UFS(obj) OBJECT_CHECK(UfsHc, (obj), TYPE_UFS)

#define TYPE_UFS_LU "ufs-lu"
#define UFSLU(obj) OBJECT_CHECK(UfsLu, (obj), TYPE_UFS_LU)

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

void ufs_build_upiu_header(UfsRequest *req, uint8_t trans_type, uint8_t flags,
                           uint8_t response, uint8_t scsi_status,
                           uint16_t data_segment_length);
void ufs_build_query_response(UfsRequest *req);
void ufs_complete_req(UfsRequest *req, UfsReqResult req_result);
void ufs_init_wlu(UfsLu *wlu, uint8_t wlun);
#endif /* HW_UFS_UFS_H */
