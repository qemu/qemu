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

#include "hw/core/qdev.h"
#include "hw/scsi/scsi.h"
#include "block/ufs.h"
#include "scsi/constants.h"
#include "system/dma.h"

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
    struct UfsHc *hc;
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
    /* The PRDT describes less data than the command moves */
    UFS_REQUEST_MISMATCH_DATA_BUF = 3,
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
    SCSIRequest *sreq;
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

/*
 * Fault injection modes for x-fault-inject-mode property.
 * Controls how the held transfer request and subsequent Task Management
 * Request (TMR) are resolved by the controller:
 *
 * UFS_FAULT_INJECT_ABORT_SUCCESS: TMR ABORT_TASK aborts the held request,
 *                                 clears its state, and returns SUCCESS
 *                                 (COMPL).
 * UFS_FAULT_INJECT_ABORT_FAILED:  TMR ABORT_TASK returns
 *                                 TASK_MANAGEMENT_FUNC_FAILED, forcing host
 *                                 driver to escalate to Host Reset.
 * UFS_FAULT_INJECT_IN_TRANSITION: TMR QUERY_TASK releases the held request to
 *                                 execute normally through the SCSI layer and
 *                                 returns the real task status (SUCCEEDED while
 *                                 the command is still in flight);
 *                                 ABORT_TASK cancels the held request and
 *                                 returns FUNCTION_COMPLETE.
 * UFS_FAULT_INJECT_TIMEOUT:       TMR ABORT_TASK does not complete or clear
 *                                 doorbell, triggering hardware-level TMR
 *                                 timeout.
 * UFS_FAULT_INJECT_SSU_TIMEOUT:   Intercepts SCSI START_STOP (0x1B) commands
 *                                 targeting the UFS Device WLUN when
 *                                 x-fault-inject-tag is armed.
 */
typedef enum UfsFaultInjectMode {
    UFS_FAULT_INJECT_ABORT_SUCCESS = 0,
    UFS_FAULT_INJECT_ABORT_FAILED,
    UFS_FAULT_INJECT_IN_TRANSITION,
    UFS_FAULT_INJECT_TIMEOUT,
    UFS_FAULT_INJECT_SSU_TIMEOUT,
    UFS_FAULT_INJECT_MODE_MAX,
} UfsFaultInjectMode;

/*
 * Sentinel values for x-fault-inject-tag property.
 * Task Tags in UFS are 8-bit (0..255). Values >= 256 are reserved as
 * sentinel flags to preserve full testability of Tag 0.
 *
 * UFS_FAULT_INJECT_TAG_NONE: Disable fault injection (default).
 * UFS_FAULT_INJECT_TAG_ANY:  Automatically intercept the first user-space
 *                            READ_10, WRITE_10, READ_16, or WRITE_16 command
 *                            targeting LBA >= UFS_FAULT_INJECT_MIN_LBA.
 */
#define UFS_FAULT_INJECT_TAG_NONE 0xffffffff
#define UFS_FAULT_INJECT_TAG_ANY  0xfffffffe

/*
 * Minimum Logical Block Address (LBA) for automatic fault injection.
 * Skip early disk blocks (LBA 0..511, e.g. MBR, GPT partition tables, EFI,
 * and filesystem superblocks) to ensure guest OS boot-time disk discovery
 * and partition scanning complete cleanly without being held.
 */
#define UFS_FAULT_INJECT_MIN_LBA 512

typedef struct UfsParams {
    char *serial;
    uint8_t nutrs; /* Number of UTP Transfer Request Slots */
    uint8_t nutmrs; /* Number of UTP Task Management Request Slots */
    bool mcq; /* Multiple Command Queue support */
    uint8_t mcq_qcfgptr; /* MCQ Queue Configuration Pointer in MCQCAP */
    uint8_t mcq_maxq; /* MCQ Maximum number of Queues */
    uint32_t wb_max_size; /* WB Maximum allocation units */
    uint32_t wb_min_size; /* WB Minimum allocation units */
    uint32_t fault_inject_tag;
    UfsFaultInjectMode fault_inject_mode;
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

/*
 * Extended features
 */
typedef struct UfsWb {
    uint64_t max_bytes;
    uint64_t min_bytes;
    uint64_t curr_bytes;
    uint64_t used_bytes;
    uint64_t resize_bytes;

    uint64_t fifo_max_bytes;
    uint64_t fifo_curr_bytes;

    uint64_t pinned_max_bytes;
    uint64_t non_pinned_min_bytes;
    uint64_t pinned_curr_bytes;
    uint64_t pinned_used_bytes;
    uint64_t pinned_total_written_bytes;
} UfsWb;

typedef struct UfsHc {
    DeviceState *dev;
    AddressSpace *dma_as;
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

    bool resetting;

    /* Extended features */
    UfsWb wb;

    uint8_t temperature;

    QEMUTimer idle_timer;

    uint32_t hid_fragment_count; /* Remaining fragmented 4KB units */
    uint32_t hid_defrag_total; /* Requested units at defrag start */
    uint32_t hid_defrag_remaining; /* Requested units left to move */

    /* Test and fault injection properties */
    uint32_t active_fault_inject_tag;
    UfsRequest *held_req;
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

static inline bool ufs_mcq_cq_full(UfsHc *u, uint32_t qid)
{
    uint32_t tail = ufs_mcq_cq_tail(u, qid);
    UfsCq *cq = u->cq[qid];
    uint16_t cq_size;

    if (!cq) {
        return false;
    }

    cq_size = cq->size;

    tail = (tail + sizeof(UfsCqEntry)) % (sizeof(UfsCqEntry) * cq_size);
    return tail == ufs_mcq_cq_head(u, qid);
}

static inline uint64_t ufs_unit_to_byte(UfsHc *u, uint32_t unit)
{
    return (uint64_t)unit * u->geometry_desc.allocation_unit_size *
           be32_to_cpu(u->geometry_desc.segment_size) * BDRV_SECTOR_SIZE;
}

static inline uint32_t ufs_byte_to_unit(UfsHc *u, uint64_t byte)
{
    return byte / BDRV_SECTOR_SIZE /
           be32_to_cpu(u->geometry_desc.segment_size) /
           u->geometry_desc.allocation_unit_size;
}

static inline bool ufs_is_write_req(UfsRequest *req)
{
    uint8_t cmd = req->req_upiu.sc.cdb[0];

    /* UFS 4.1 Specifiaction doesn't support WRITE_12 */
    return (cmd == WRITE_6) || (cmd == WRITE_10) || (cmd == WRITE_16);
}

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
bool ufs_fit_data_buffer(UfsRequest *req, uint64_t len);
void ufs_wb_update_avail_buffer(UfsHc *u);
void ufs_init_wlu(UfsLu *wlu, uint8_t wlun);
bool ufs_realize(UfsHc *u, DeviceState *dev, AddressSpace *dma_as,
                 Error **errp);
void ufs_unrealize(UfsHc *u);

extern const PropertyInfo ufs_prop_fault_inject_tag;
extern const PropertyInfo ufs_prop_fault_inject_mode;
extern const QEnumLookup UfsFaultInjectMode_lookup;

#endif /* HW_UFS_UFS_H */
