/*
 * QEMU Universal Flash Storage (UFS) Controller
 *
 * Copyright (c) 2023 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Written by Jeuk Kim <jeuk20.kim@samsung.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/**
 * Reference Specs: https://www.jedec.org/, 4.0
 *
 * Usage
 * -----
 *
 * Add options:
 *      -drive file=<file>,if=none,id=<drive_id>
 *      -device ufs,serial=<serial>,id=<bus_name>, \
 *              nutrs=<N[optional]>,nutmrs=<N[optional]>
 *      -device ufs-lu,drive=<drive_id>,bus=<bus_name>
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "scsi/constants.h"
#include "hw/irq.h"
#include "trace.h"
#include "ufs.h"

/* The QEMU-UFS device follows spec version 4.0 */
#define UFS_SPEC_VER 0x0400
#define UFS_MAX_NUTRS 32
#define UFS_MAX_NUTMRS 8
#define UFS_MCQ_QCFGPTR 2

/* Each value represents the temperature in celsius as (value - 80) */
#define UFS_TEMPERATURE 120
#define UFS_TOO_HIGH_TEMP_BOUNDARY 160
#define UFS_TOO_LOW_TEMP_BOUNDARY 60

static void ufs_exec_req(UfsRequest *req);
static void ufs_clear_req(UfsRequest *req);

static inline uint64_t ufs_mcq_reg_addr(UfsHc *u, int qid)
{
    /* Submission Queue MCQ Registers offset (400h) */
    return (UFS_MCQ_QCFGPTR * 0x200) + qid * 0x40;
}

static inline uint64_t ufs_mcq_op_reg_addr(UfsHc *u, int qid)
{
    /* MCQ Operation & Runtime Registers offset (1000h) */
    return UFS_MCQ_OPR_START + qid * 48;
}

static inline uint64_t ufs_reg_size(UfsHc *u)
{
    /* Total UFS HCI Register size in bytes */
    return ufs_mcq_op_reg_addr(u, 0) + sizeof(u->mcq_op_reg);
}

static inline bool ufs_is_mcq_reg(UfsHc *u, uint64_t addr, unsigned size)
{
    uint64_t mcq_reg_addr;

    if (!u->params.mcq) {
        return false;
    }

    mcq_reg_addr = ufs_mcq_reg_addr(u, 0);
    return (addr >= mcq_reg_addr &&
            addr + size <= mcq_reg_addr + sizeof(u->mcq_reg));
}

static inline bool ufs_is_mcq_op_reg(UfsHc *u, uint64_t addr, unsigned size)
{
    uint64_t mcq_op_reg_addr;

    if (!u->params.mcq) {
        return false;
    }

    mcq_op_reg_addr = ufs_mcq_op_reg_addr(u, 0);
    return (addr >= mcq_op_reg_addr &&
            addr + size <= mcq_op_reg_addr + sizeof(u->mcq_op_reg));
}

static MemTxResult ufs_addr_read(UfsHc *u, hwaddr addr, void *buf, int size)
{
    hwaddr hi = addr + size - 1;

    if (hi < addr) {
        return MEMTX_DECODE_ERROR;
    }

    if (!FIELD_EX32(u->reg.cap, CAP, 64AS) && (hi >> 32)) {
        return MEMTX_DECODE_ERROR;
    }

    return pci_dma_read(PCI_DEVICE(u), addr, buf, size);
}

static MemTxResult ufs_addr_write(UfsHc *u, hwaddr addr, const void *buf,
                                  int size)
{
    hwaddr hi = addr + size - 1;
    if (hi < addr) {
        return MEMTX_DECODE_ERROR;
    }

    if (!FIELD_EX32(u->reg.cap, CAP, 64AS) && (hi >> 32)) {
        return MEMTX_DECODE_ERROR;
    }

    return pci_dma_write(PCI_DEVICE(u), addr, buf, size);
}

static inline hwaddr ufs_get_utrd_addr(UfsHc *u, uint32_t slot)
{
    hwaddr utrl_base_addr = (((hwaddr)u->reg.utrlbau) << 32) + u->reg.utrlba;
    hwaddr utrd_addr = utrl_base_addr + slot * sizeof(UtpTransferReqDesc);

    return utrd_addr;
}

static inline hwaddr ufs_get_req_upiu_base_addr(const UtpTransferReqDesc *utrd)
{
    uint32_t cmd_desc_base_addr_lo =
        le32_to_cpu(utrd->command_desc_base_addr_lo);
    uint32_t cmd_desc_base_addr_hi =
        le32_to_cpu(utrd->command_desc_base_addr_hi);

    return (((hwaddr)cmd_desc_base_addr_hi) << 32) + cmd_desc_base_addr_lo;
}

static inline hwaddr ufs_get_rsp_upiu_base_addr(const UtpTransferReqDesc *utrd)
{
    hwaddr req_upiu_base_addr = ufs_get_req_upiu_base_addr(utrd);
    uint32_t rsp_upiu_byte_off =
        le16_to_cpu(utrd->response_upiu_offset) * sizeof(uint32_t);
    return req_upiu_base_addr + rsp_upiu_byte_off;
}

static MemTxResult ufs_dma_read_utrd(UfsRequest *req)
{
    UfsHc *u = req->hc;
    hwaddr utrd_addr = ufs_get_utrd_addr(u, req->slot);
    MemTxResult ret;

    ret = ufs_addr_read(u, utrd_addr, &req->utrd, sizeof(req->utrd));
    if (ret) {
        trace_ufs_err_dma_read_utrd(req->slot, utrd_addr);
    }
    return ret;
}

static MemTxResult ufs_dma_read_req_upiu(UfsRequest *req)
{
    UfsHc *u = req->hc;
    hwaddr req_upiu_base_addr = ufs_get_req_upiu_base_addr(&req->utrd);
    UtpUpiuReq *req_upiu = &req->req_upiu;
    uint32_t copy_size;
    uint16_t data_segment_length;
    MemTxResult ret;

    /*
     * To know the size of the req_upiu, we need to read the
     * data_segment_length in the header first.
     */
    ret = ufs_addr_read(u, req_upiu_base_addr, &req_upiu->header,
                        sizeof(UtpUpiuHeader));
    if (ret) {
        trace_ufs_err_dma_read_req_upiu(req->slot, req_upiu_base_addr);
        return ret;
    }
    data_segment_length = be16_to_cpu(req_upiu->header.data_segment_length);

    copy_size = sizeof(UtpUpiuHeader) + UFS_TRANSACTION_SPECIFIC_FIELD_SIZE +
                data_segment_length;

    if (copy_size > sizeof(req->req_upiu)) {
        copy_size = sizeof(req->req_upiu);
    }

    ret = ufs_addr_read(u, req_upiu_base_addr, &req->req_upiu, copy_size);
    if (ret) {
        trace_ufs_err_dma_read_req_upiu(req->slot, req_upiu_base_addr);
    }
    return ret;
}

static MemTxResult ufs_dma_read_prdt(UfsRequest *req)
{
    UfsHc *u = req->hc;
    uint16_t prdt_len = le16_to_cpu(req->utrd.prd_table_length);
    uint16_t prdt_byte_off =
        le16_to_cpu(req->utrd.prd_table_offset) * sizeof(uint32_t);
    uint32_t prdt_size = prdt_len * sizeof(UfshcdSgEntry);
    g_autofree UfshcdSgEntry *prd_entries = NULL;
    hwaddr req_upiu_base_addr, prdt_base_addr;
    int err;

    assert(!req->sg);

    if (prdt_size == 0) {
        return MEMTX_OK;
    }
    prd_entries = g_new(UfshcdSgEntry, prdt_size);

    req_upiu_base_addr = ufs_get_req_upiu_base_addr(&req->utrd);
    prdt_base_addr = req_upiu_base_addr + prdt_byte_off;

    err = ufs_addr_read(u, prdt_base_addr, prd_entries, prdt_size);
    if (err) {
        trace_ufs_err_dma_read_prdt(req->slot, prdt_base_addr);
        return err;
    }

    req->sg = g_malloc0(sizeof(QEMUSGList));
    pci_dma_sglist_init(req->sg, PCI_DEVICE(u), prdt_len);
    req->data_len = 0;

    for (uint16_t i = 0; i < prdt_len; ++i) {
        hwaddr data_dma_addr = le64_to_cpu(prd_entries[i].addr);
        uint32_t data_byte_count = le32_to_cpu(prd_entries[i].size) + 1;
        qemu_sglist_add(req->sg, data_dma_addr, data_byte_count);
        req->data_len += data_byte_count;
    }
    return MEMTX_OK;
}

static MemTxResult ufs_dma_read_upiu(UfsRequest *req)
{
    MemTxResult ret;

    /*
     * In case of MCQ, UTRD has already been read from a SQ, so skip it.
     */
    if (!ufs_mcq_req(req)) {
        ret = ufs_dma_read_utrd(req);
        if (ret) {
            return ret;
        }
    }

    ret = ufs_dma_read_req_upiu(req);
    if (ret) {
        return ret;
    }

    ret = ufs_dma_read_prdt(req);
    if (ret) {
        return ret;
    }

    return 0;
}

static MemTxResult ufs_dma_write_utrd(UfsRequest *req)
{
    UfsHc *u = req->hc;
    hwaddr utrd_addr = ufs_get_utrd_addr(u, req->slot);
    MemTxResult ret;

    ret = ufs_addr_write(u, utrd_addr, &req->utrd, sizeof(req->utrd));
    if (ret) {
        trace_ufs_err_dma_write_utrd(req->slot, utrd_addr);
    }
    return ret;
}

static MemTxResult ufs_dma_write_rsp_upiu(UfsRequest *req)
{
    UfsHc *u = req->hc;
    hwaddr rsp_upiu_base_addr = ufs_get_rsp_upiu_base_addr(&req->utrd);
    uint32_t rsp_upiu_byte_len =
        le16_to_cpu(req->utrd.response_upiu_length) * sizeof(uint32_t);
    uint16_t data_segment_length =
        be16_to_cpu(req->rsp_upiu.header.data_segment_length);
    uint32_t copy_size = sizeof(UtpUpiuHeader) +
                         UFS_TRANSACTION_SPECIFIC_FIELD_SIZE +
                         data_segment_length;
    MemTxResult ret;

    if (copy_size > rsp_upiu_byte_len) {
        copy_size = rsp_upiu_byte_len;
    }

    if (copy_size > sizeof(req->rsp_upiu)) {
        copy_size = sizeof(req->rsp_upiu);
    }

    ret = ufs_addr_write(u, rsp_upiu_base_addr, &req->rsp_upiu, copy_size);
    if (ret) {
        trace_ufs_err_dma_write_rsp_upiu(req->slot, rsp_upiu_base_addr);
    }
    return ret;
}

static MemTxResult ufs_dma_write_upiu(UfsRequest *req)
{
    MemTxResult ret;

    ret = ufs_dma_write_rsp_upiu(req);
    if (ret) {
        return ret;
    }

    return ufs_dma_write_utrd(req);
}

static void ufs_irq_check(UfsHc *u)
{
    PCIDevice *pci = PCI_DEVICE(u);

    if ((u->reg.is & UFS_INTR_MASK) & u->reg.ie) {
        trace_ufs_irq_raise();
        pci_irq_assert(pci);
    } else {
        trace_ufs_irq_lower();
        pci_irq_deassert(pci);
    }
}

static void ufs_process_db(UfsHc *u, uint32_t val)
{
    DECLARE_BITMAP(doorbell, UFS_MAX_NUTRS);
    uint32_t slot;
    uint32_t nutrs = u->params.nutrs;
    UfsRequest *req;

    val &= ~u->reg.utrldbr;
    if (!val) {
        return;
    }

    doorbell[0] = val;
    slot = find_first_bit(doorbell, nutrs);

    while (slot < nutrs) {
        req = &u->req_list[slot];
        if (req->state == UFS_REQUEST_ERROR) {
            trace_ufs_err_utrl_slot_error(req->slot);
            return;
        }

        if (req->state != UFS_REQUEST_IDLE) {
            trace_ufs_err_utrl_slot_busy(req->slot);
            return;
        }

        trace_ufs_process_db(slot);
        req->state = UFS_REQUEST_READY;
        slot = find_next_bit(doorbell, nutrs, slot + 1);
    }

    qemu_bh_schedule(u->doorbell_bh);
}

static void ufs_process_uiccmd(UfsHc *u, uint32_t val)
{
    trace_ufs_process_uiccmd(val, u->reg.ucmdarg1, u->reg.ucmdarg2,
                             u->reg.ucmdarg3);
    /*
     * Only the essential uic commands for running drivers on Linux and Windows
     * are implemented.
     */
    switch (val) {
    case UFS_UIC_CMD_DME_LINK_STARTUP:
        u->reg.hcs = FIELD_DP32(u->reg.hcs, HCS, DP, 1);
        u->reg.hcs = FIELD_DP32(u->reg.hcs, HCS, UTRLRDY, 1);
        u->reg.hcs = FIELD_DP32(u->reg.hcs, HCS, UTMRLRDY, 1);
        u->reg.ucmdarg2 = UFS_UIC_CMD_RESULT_SUCCESS;
        break;
    /* TODO: Revisit it when Power Management is implemented */
    case UFS_UIC_CMD_DME_HIBER_ENTER:
        u->reg.is = FIELD_DP32(u->reg.is, IS, UHES, 1);
        u->reg.hcs = FIELD_DP32(u->reg.hcs, HCS, UPMCRS, UFS_PWR_LOCAL);
        u->reg.ucmdarg2 = UFS_UIC_CMD_RESULT_SUCCESS;
        break;
    case UFS_UIC_CMD_DME_HIBER_EXIT:
        u->reg.is = FIELD_DP32(u->reg.is, IS, UHXS, 1);
        u->reg.hcs = FIELD_DP32(u->reg.hcs, HCS, UPMCRS, UFS_PWR_LOCAL);
        u->reg.ucmdarg2 = UFS_UIC_CMD_RESULT_SUCCESS;
        break;
    default:
        u->reg.ucmdarg2 = UFS_UIC_CMD_RESULT_FAILURE;
    }

    u->reg.is = FIELD_DP32(u->reg.is, IS, UCCS, 1);

    ufs_irq_check(u);
}

static void ufs_mcq_init_req(UfsHc *u, UfsRequest *req, UfsSq *sq)
{
    memset(req, 0, sizeof(*req));

    req->hc = u;
    req->state = UFS_REQUEST_IDLE;
    req->slot = UFS_INVALID_SLOT;
    req->sq = sq;
}

static void ufs_mcq_process_sq(void *opaque)
{
    UfsSq *sq = opaque;
    UfsHc *u = sq->u;
    UfsSqEntry sqe;
    UfsRequest *req;
    hwaddr addr;
    uint16_t head = ufs_mcq_sq_head(u, sq->sqid);
    int err;

    while (!(ufs_mcq_sq_empty(u, sq->sqid) || QTAILQ_EMPTY(&sq->req_list))) {
        addr = sq->addr + head;
        err = ufs_addr_read(sq->u, addr, (void *)&sqe, sizeof(sqe));
        if (err) {
            trace_ufs_err_dma_read_sq(sq->sqid, addr);
            return;
        }

        head = (head + sizeof(sqe)) % (sq->size * sizeof(sqe));
        ufs_mcq_update_sq_head(u, sq->sqid, head);

        req = QTAILQ_FIRST(&sq->req_list);
        QTAILQ_REMOVE(&sq->req_list, req, entry);

        ufs_mcq_init_req(sq->u, req, sq);
        memcpy(&req->utrd, &sqe, sizeof(req->utrd));

        req->state = UFS_REQUEST_RUNNING;
        ufs_exec_req(req);
    }
}

static void ufs_mcq_process_cq(void *opaque)
{
    UfsCq *cq = opaque;
    UfsHc *u = cq->u;
    UfsRequest *req, *next;
    MemTxResult ret;
    uint32_t tail = ufs_mcq_cq_tail(u, cq->cqid);

    QTAILQ_FOREACH_SAFE(req, &cq->req_list, entry, next)
    {
        ufs_dma_write_rsp_upiu(req);

        req->cqe.utp_addr =
            ((uint64_t)req->utrd.command_desc_base_addr_hi << 32ULL) |
            req->utrd.command_desc_base_addr_lo;
        req->cqe.utp_addr |= req->sq->sqid;
        req->cqe.resp_len = req->utrd.response_upiu_length;
        req->cqe.resp_off = req->utrd.response_upiu_offset;
        req->cqe.prdt_len = req->utrd.prd_table_length;
        req->cqe.prdt_off = req->utrd.prd_table_offset;
        req->cqe.status = req->utrd.header.dword_2 & 0xf;
        req->cqe.error = 0;

        ret = ufs_addr_write(u, cq->addr + tail, &req->cqe, sizeof(req->cqe));
        if (ret) {
            trace_ufs_err_dma_write_cq(cq->cqid, cq->addr + tail);
        }
        QTAILQ_REMOVE(&cq->req_list, req, entry);

        tail = (tail + sizeof(req->cqe)) % (cq->size * sizeof(req->cqe));
        ufs_mcq_update_cq_tail(u, cq->cqid, tail);

        ufs_clear_req(req);
        QTAILQ_INSERT_TAIL(&req->sq->req_list, req, entry);
    }

    if (!ufs_mcq_cq_empty(u, cq->cqid)) {
        u->mcq_op_reg[cq->cqid].cq_int.is =
            FIELD_DP32(u->mcq_op_reg[cq->cqid].cq_int.is, CQIS, TEPS, 1);

        u->reg.is = FIELD_DP32(u->reg.is, IS, CQES, 1);
        ufs_irq_check(u);
    }
}

static bool ufs_mcq_create_sq(UfsHc *u, uint8_t qid, uint32_t attr)
{
    UfsMcqReg *reg = &u->mcq_reg[qid];
    UfsSq *sq;
    uint8_t cqid = FIELD_EX32(attr, SQATTR, CQID);

    if (qid >= u->params.mcq_maxq) {
        trace_ufs_err_mcq_create_sq_invalid_sqid(qid);
        return false;
    }

    if (u->sq[qid]) {
        trace_ufs_err_mcq_create_sq_already_exists(qid);
        return false;
    }

    if (!u->cq[cqid]) {
        trace_ufs_err_mcq_create_sq_invalid_cqid(qid);
        return false;
    }

    sq = g_malloc0(sizeof(*sq));
    sq->u = u;
    sq->sqid = qid;
    sq->cq = u->cq[cqid];
    sq->addr = ((uint64_t)reg->squba << 32) | reg->sqlba;
    sq->size = ((FIELD_EX32(attr, SQATTR, SIZE) + 1) << 2) / sizeof(UfsSqEntry);

    sq->bh = qemu_bh_new_guarded(ufs_mcq_process_sq, sq,
                                 &DEVICE(u)->mem_reentrancy_guard);
    sq->req = g_new0(UfsRequest, sq->size);
    QTAILQ_INIT(&sq->req_list);
    for (int i = 0; i < sq->size; i++) {
        ufs_mcq_init_req(u, &sq->req[i], sq);
        QTAILQ_INSERT_TAIL(&sq->req_list, &sq->req[i], entry);
    }

    u->sq[qid] = sq;

    trace_ufs_mcq_create_sq(sq->sqid, sq->cq->cqid, sq->addr, sq->size);
    return true;
}

static bool ufs_mcq_delete_sq(UfsHc *u, uint8_t qid)
{
    UfsSq *sq;

    if (qid >= u->params.mcq_maxq) {
        trace_ufs_err_mcq_delete_sq_invalid_sqid(qid);
        return false;
    }

    if (!u->sq[qid]) {
        trace_ufs_err_mcq_delete_sq_not_exists(qid);
        return false;
    }

    sq = u->sq[qid];

    qemu_bh_delete(sq->bh);
    g_free(sq->req);
    g_free(sq);
    u->sq[qid] = NULL;
    return true;
}

static bool ufs_mcq_create_cq(UfsHc *u, uint8_t qid, uint32_t attr)
{
    UfsMcqReg *reg = &u->mcq_reg[qid];
    UfsCq *cq;

    if (qid >= u->params.mcq_maxq) {
        trace_ufs_err_mcq_create_cq_invalid_cqid(qid);
        return false;
    }

    if (u->cq[qid]) {
        trace_ufs_err_mcq_create_cq_already_exists(qid);
        return false;
    }

    cq = g_malloc0(sizeof(*cq));
    cq->u = u;
    cq->cqid = qid;
    cq->addr = ((uint64_t)reg->cquba << 32) | reg->cqlba;
    cq->size = ((FIELD_EX32(attr, CQATTR, SIZE) + 1) << 2) / sizeof(UfsCqEntry);

    cq->bh = qemu_bh_new_guarded(ufs_mcq_process_cq, cq,
                                 &DEVICE(u)->mem_reentrancy_guard);
    QTAILQ_INIT(&cq->req_list);

    u->cq[qid] = cq;

    trace_ufs_mcq_create_cq(cq->cqid, cq->addr, cq->size);
    return true;
}

static bool ufs_mcq_delete_cq(UfsHc *u, uint8_t qid)
{
    UfsCq *cq;

    if (qid >= u->params.mcq_maxq) {
        trace_ufs_err_mcq_delete_cq_invalid_cqid(qid);
        return false;
    }

    if (!u->cq[qid]) {
        trace_ufs_err_mcq_delete_cq_not_exists(qid);
        return false;
    }

    for (int i = 0; i < ARRAY_SIZE(u->sq); i++) {
        if (u->sq[i] && u->sq[i]->cq->cqid == qid) {
            trace_ufs_err_mcq_delete_cq_sq_not_deleted(i, qid);
            return false;
        }
    }

    cq = u->cq[qid];

    qemu_bh_delete(cq->bh);
    g_free(cq);
    u->cq[qid] = NULL;
    return true;
}

static void ufs_write_reg(UfsHc *u, hwaddr offset, uint32_t data, unsigned size)
{
    switch (offset) {
    case A_IS:
        u->reg.is &= ~data;
        ufs_irq_check(u);
        break;
    case A_IE:
        u->reg.ie = data;
        ufs_irq_check(u);
        break;
    case A_HCE:
        if (!FIELD_EX32(u->reg.hce, HCE, HCE) && FIELD_EX32(data, HCE, HCE)) {
            u->reg.hcs = FIELD_DP32(u->reg.hcs, HCS, UCRDY, 1);
            u->reg.hce = FIELD_DP32(u->reg.hce, HCE, HCE, 1);
        } else if (FIELD_EX32(u->reg.hce, HCE, HCE) &&
                   !FIELD_EX32(data, HCE, HCE)) {
            u->reg.hcs = 0;
            u->reg.hce = FIELD_DP32(u->reg.hce, HCE, HCE, 0);
        }
        break;
    case A_UTRLBA:
        u->reg.utrlba = data & R_UTRLBA_UTRLBA_MASK;
        break;
    case A_UTRLBAU:
        u->reg.utrlbau = data;
        break;
    case A_UTRLDBR:
        ufs_process_db(u, data);
        u->reg.utrldbr |= data;
        break;
    case A_UTRLRSR:
        u->reg.utrlrsr = data;
        break;
    case A_UTRLCNR:
        u->reg.utrlcnr &= ~data;
        break;
    case A_UTMRLBA:
        u->reg.utmrlba = data & R_UTMRLBA_UTMRLBA_MASK;
        break;
    case A_UTMRLBAU:
        u->reg.utmrlbau = data;
        break;
    case A_UICCMD:
        ufs_process_uiccmd(u, data);
        break;
    case A_UCMDARG1:
        u->reg.ucmdarg1 = data;
        break;
    case A_UCMDARG2:
        u->reg.ucmdarg2 = data;
        break;
    case A_UCMDARG3:
        u->reg.ucmdarg3 = data;
        break;
    case A_CONFIG:
        u->reg.config = data;
        break;
    case A_MCQCONFIG:
        u->reg.mcqconfig = data;
        break;
    case A_UTRLCLR:
    case A_UTMRLDBR:
    case A_UTMRLCLR:
    case A_UTMRLRSR:
        trace_ufs_err_unsupport_register_offset(offset);
        break;
    default:
        trace_ufs_err_invalid_register_offset(offset);
        break;
    }
}

static void ufs_write_mcq_reg(UfsHc *u, hwaddr offset, uint32_t data,
                              unsigned size)
{
    int qid = offset / sizeof(UfsMcqReg);
    UfsMcqReg *reg = &u->mcq_reg[qid];

    switch (offset % sizeof(UfsMcqReg)) {
    case A_SQATTR:
        if (!FIELD_EX32(reg->sqattr, SQATTR, SQEN) &&
            FIELD_EX32(data, SQATTR, SQEN)) {
            if (!ufs_mcq_create_sq(u, qid, data)) {
                break;
            }
        } else if (FIELD_EX32(reg->sqattr, SQATTR, SQEN) &&
                   !FIELD_EX32(data, SQATTR, SQEN)) {
            if (!ufs_mcq_delete_sq(u, qid)) {
                break;
            }
        }
        reg->sqattr = data;
        break;
    case A_SQLBA:
        reg->sqlba = data;
        break;
    case A_SQUBA:
        reg->squba = data;
        break;
    case A_SQCFG:
        reg->sqcfg = data;
        break;
    case A_CQATTR:
        if (!FIELD_EX32(reg->cqattr, CQATTR, CQEN) &&
            FIELD_EX32(data, CQATTR, CQEN)) {
            if (!ufs_mcq_create_cq(u, qid, data)) {
                break;
            }
        } else if (FIELD_EX32(reg->cqattr, CQATTR, CQEN) &&
                   !FIELD_EX32(data, CQATTR, CQEN)) {
            if (!ufs_mcq_delete_cq(u, qid)) {
                break;
            }
        }
        reg->cqattr = data;
        break;
    case A_CQLBA:
        reg->cqlba = data;
        break;
    case A_CQUBA:
        reg->cquba = data;
        break;
    case A_CQCFG:
        reg->cqcfg = data;
        break;
    case A_SQDAO:
    case A_SQISAO:
    case A_CQDAO:
    case A_CQISAO:
        trace_ufs_err_unsupport_register_offset(offset);
        break;
    default:
        trace_ufs_err_invalid_register_offset(offset);
        break;
    }
}

static void ufs_mcq_process_db(UfsHc *u, uint8_t qid, uint32_t db)
{
    UfsSq *sq;

    if (qid >= u->params.mcq_maxq) {
        trace_ufs_err_mcq_db_wr_invalid_sqid(qid);
        return;
    }

    sq = u->sq[qid];
    if (sq->size * sizeof(UfsSqEntry) <= db) {
        trace_ufs_err_mcq_db_wr_invalid_db(qid, db);
        return;
    }

    ufs_mcq_update_sq_tail(u, sq->sqid, db);
    qemu_bh_schedule(sq->bh);
}

static void ufs_write_mcq_op_reg(UfsHc *u, hwaddr offset, uint32_t data,
                                 unsigned size)
{
    int qid = offset / sizeof(UfsMcqOpReg);
    UfsMcqOpReg *opr = &u->mcq_op_reg[qid];

    switch (offset % sizeof(UfsMcqOpReg)) {
    case offsetof(UfsMcqOpReg, sq.tp):
        if (opr->sq.tp != data) {
            ufs_mcq_process_db(u, qid, data);
        }
        opr->sq.tp = data;
        break;
    case offsetof(UfsMcqOpReg, cq.hp):
        opr->cq.hp = data;
        ufs_mcq_update_cq_head(u, qid, data);
        break;
    case offsetof(UfsMcqOpReg, cq_int.is):
        opr->cq_int.is &= ~data;
        break;
    default:
        trace_ufs_err_invalid_register_offset(offset);
        break;
    }
}

static uint64_t ufs_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    UfsHc *u = (UfsHc *)opaque;
    uint32_t *ptr;
    uint64_t value;
    uint64_t offset;

    if (addr + size <= sizeof(u->reg)) {
        offset = addr;
        ptr = (uint32_t *)&u->reg;
    } else if (ufs_is_mcq_reg(u, addr, size)) {
        offset = addr - ufs_mcq_reg_addr(u, 0);
        ptr = (uint32_t *)&u->mcq_reg;
    } else if (ufs_is_mcq_op_reg(u, addr, size)) {
        offset = addr - ufs_mcq_op_reg_addr(u, 0);
        ptr = (uint32_t *)&u->mcq_op_reg;
    } else {
        trace_ufs_err_invalid_register_offset(addr);
        return 0;
    }

    value = ptr[offset >> 2];
    trace_ufs_mmio_read(addr, value, size);
    return value;
}

static void ufs_mmio_write(void *opaque, hwaddr addr, uint64_t data,
                           unsigned size)
{
    UfsHc *u = (UfsHc *)opaque;

    trace_ufs_mmio_write(addr, data, size);

    if (addr + size <= sizeof(u->reg)) {
        ufs_write_reg(u, addr, data, size);
    } else if (ufs_is_mcq_reg(u, addr, size)) {
        ufs_write_mcq_reg(u, addr - ufs_mcq_reg_addr(u, 0), data, size);
    } else if (ufs_is_mcq_op_reg(u, addr, size)) {
        ufs_write_mcq_op_reg(u, addr - ufs_mcq_op_reg_addr(u, 0), data, size);
    } else {
        trace_ufs_err_invalid_register_offset(addr);
    }
}

static const MemoryRegionOps ufs_mmio_ops = {
    .read = ufs_mmio_read,
    .write = ufs_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void ufs_update_ee_status(UfsHc *u)
{
    uint16_t ee_status = be16_to_cpu(u->attributes.exception_event_status);
    uint8_t high_temp_thresh = u->attributes.device_too_high_temp_boundary;
    uint8_t low_temp_thresh = u->attributes.device_too_low_temp_boundary;

    if (u->temperature >= high_temp_thresh) {
        ee_status |= MASK_EE_TOO_HIGH_TEMP;
    } else {
        ee_status &= ~MASK_EE_TOO_HIGH_TEMP;
    }

    if (u->temperature <= low_temp_thresh) {
        ee_status |= MASK_EE_TOO_LOW_TEMP;
    } else {
        ee_status &= ~MASK_EE_TOO_LOW_TEMP;
    }

    u->attributes.exception_event_status = cpu_to_be16(ee_status);
}

static bool ufs_check_exception_event_alert(UfsHc *u, uint8_t trans_type)
{
    uint16_t ee_control = be16_to_cpu(u->attributes.exception_event_control);
    uint16_t ee_status;

    if (trans_type != UFS_UPIU_TRANSACTION_RESPONSE) {
        return false;
    }

    ufs_update_ee_status(u);

    ee_status = be16_to_cpu(u->attributes.exception_event_status);

    return ee_control & ee_status;
}

void ufs_build_upiu_header(UfsRequest *req, uint8_t trans_type, uint8_t flags,
                           uint8_t response, uint8_t scsi_status,
                           uint16_t data_segment_length)
{
    memcpy(&req->rsp_upiu.header, &req->req_upiu.header, sizeof(UtpUpiuHeader));
    req->rsp_upiu.header.trans_type = trans_type;
    req->rsp_upiu.header.flags = flags;
    req->rsp_upiu.header.response = response;
    req->rsp_upiu.header.scsi_status = scsi_status;
    req->rsp_upiu.header.device_inf =
        ufs_check_exception_event_alert(req->hc, trans_type);
    req->rsp_upiu.header.data_segment_length = cpu_to_be16(data_segment_length);
}

void ufs_build_query_response(UfsRequest *req)
{
    req->rsp_upiu.qr.opcode = req->req_upiu.qr.opcode;
    req->rsp_upiu.qr.idn = req->req_upiu.qr.idn;
    req->rsp_upiu.qr.index = req->req_upiu.qr.index;
    req->rsp_upiu.qr.selector = req->req_upiu.qr.selector;
}

static UfsReqResult ufs_exec_scsi_cmd(UfsRequest *req)
{
    UfsHc *u = req->hc;
    uint8_t lun = req->req_upiu.header.lun;

    UfsLu *lu = NULL;

    trace_ufs_exec_scsi_cmd(req->slot, lun, req->req_upiu.sc.cdb[0]);

    if (!is_wlun(lun) && (lun >= UFS_MAX_LUS || u->lus[lun] == NULL)) {
        trace_ufs_err_scsi_cmd_invalid_lun(lun);
        return UFS_REQUEST_FAIL;
    }

    switch (lun) {
    case UFS_UPIU_REPORT_LUNS_WLUN:
        lu = &u->report_wlu;
        break;
    case UFS_UPIU_UFS_DEVICE_WLUN:
        lu = &u->dev_wlu;
        break;
    case UFS_UPIU_BOOT_WLUN:
        lu = &u->boot_wlu;
        break;
    case UFS_UPIU_RPMB_WLUN:
        lu = &u->rpmb_wlu;
        break;
    default:
        lu = u->lus[lun];
    }

    return lu->scsi_op(lu, req);
}

static UfsReqResult ufs_exec_nop_cmd(UfsRequest *req)
{
    trace_ufs_exec_nop_cmd(req->slot);
    ufs_build_upiu_header(req, UFS_UPIU_TRANSACTION_NOP_IN, 0, 0, 0, 0);
    return UFS_REQUEST_SUCCESS;
}

/*
 * This defines the permission of flags based on their IDN. There are some
 * things that are declared read-only, which is inconsistent with the ufs spec,
 * because we want to return an error for features that are not yet supported.
 */
static const int flag_permission[UFS_QUERY_FLAG_IDN_COUNT] = {
    [UFS_QUERY_FLAG_IDN_FDEVICEINIT] = UFS_QUERY_FLAG_READ | UFS_QUERY_FLAG_SET,
    /* Write protection is not supported */
    [UFS_QUERY_FLAG_IDN_PERMANENT_WPE] = UFS_QUERY_FLAG_READ,
    [UFS_QUERY_FLAG_IDN_PWR_ON_WPE] = UFS_QUERY_FLAG_READ,
    [UFS_QUERY_FLAG_IDN_BKOPS_EN] = UFS_QUERY_FLAG_READ | UFS_QUERY_FLAG_SET |
                                    UFS_QUERY_FLAG_CLEAR |
                                    UFS_QUERY_FLAG_TOGGLE,
    [UFS_QUERY_FLAG_IDN_LIFE_SPAN_MODE_ENABLE] =
        UFS_QUERY_FLAG_READ | UFS_QUERY_FLAG_SET | UFS_QUERY_FLAG_CLEAR |
        UFS_QUERY_FLAG_TOGGLE,
    /* Purge Operation is not supported */
    [UFS_QUERY_FLAG_IDN_PURGE_ENABLE] = UFS_QUERY_FLAG_NONE,
    /* Refresh Operation is not supported */
    [UFS_QUERY_FLAG_IDN_REFRESH_ENABLE] = UFS_QUERY_FLAG_NONE,
    /* Physical Resource Removal is not supported */
    [UFS_QUERY_FLAG_IDN_FPHYRESOURCEREMOVAL] = UFS_QUERY_FLAG_READ,
    [UFS_QUERY_FLAG_IDN_BUSY_RTC] = UFS_QUERY_FLAG_READ,
    [UFS_QUERY_FLAG_IDN_PERMANENTLY_DISABLE_FW_UPDATE] = UFS_QUERY_FLAG_READ,
    /* Write Booster is not supported */
    [UFS_QUERY_FLAG_IDN_WB_EN] = UFS_QUERY_FLAG_READ,
    [UFS_QUERY_FLAG_IDN_WB_BUFF_FLUSH_EN] = UFS_QUERY_FLAG_READ,
    [UFS_QUERY_FLAG_IDN_WB_BUFF_FLUSH_DURING_HIBERN8] = UFS_QUERY_FLAG_READ,
};

static inline QueryRespCode ufs_flag_check_idn_valid(uint8_t idn, int op)
{
    if (idn >= UFS_QUERY_FLAG_IDN_COUNT) {
        return UFS_QUERY_RESULT_INVALID_IDN;
    }

    if (!(flag_permission[idn] & op)) {
        if (op == UFS_QUERY_FLAG_READ) {
            trace_ufs_err_query_flag_not_readable(idn);
            return UFS_QUERY_RESULT_NOT_READABLE;
        }
        trace_ufs_err_query_flag_not_writable(idn);
        return UFS_QUERY_RESULT_NOT_WRITEABLE;
    }

    return UFS_QUERY_RESULT_SUCCESS;
}

static const int attr_permission[UFS_QUERY_ATTR_IDN_COUNT] = {
    /* booting is not supported */
    [UFS_QUERY_ATTR_IDN_BOOT_LU_EN] = UFS_QUERY_ATTR_READ,
    [UFS_QUERY_ATTR_IDN_POWER_MODE] = UFS_QUERY_ATTR_READ,
    [UFS_QUERY_ATTR_IDN_ACTIVE_ICC_LVL] =
        UFS_QUERY_ATTR_READ | UFS_QUERY_ATTR_WRITE,
    [UFS_QUERY_ATTR_IDN_OOO_DATA_EN] = UFS_QUERY_ATTR_READ,
    [UFS_QUERY_ATTR_IDN_BKOPS_STATUS] = UFS_QUERY_ATTR_READ,
    [UFS_QUERY_ATTR_IDN_PURGE_STATUS] = UFS_QUERY_ATTR_READ,
    [UFS_QUERY_ATTR_IDN_MAX_DATA_IN] =
        UFS_QUERY_ATTR_READ | UFS_QUERY_ATTR_WRITE,
    [UFS_QUERY_ATTR_IDN_MAX_DATA_OUT] =
        UFS_QUERY_ATTR_READ | UFS_QUERY_ATTR_WRITE,
    [UFS_QUERY_ATTR_IDN_DYN_CAP_NEEDED] = UFS_QUERY_ATTR_READ,
    [UFS_QUERY_ATTR_IDN_REF_CLK_FREQ] =
        UFS_QUERY_ATTR_READ | UFS_QUERY_ATTR_WRITE,
    [UFS_QUERY_ATTR_IDN_CONF_DESC_LOCK] = UFS_QUERY_ATTR_READ,
    [UFS_QUERY_ATTR_IDN_MAX_NUM_OF_RTT] =
        UFS_QUERY_ATTR_READ | UFS_QUERY_ATTR_WRITE,
    [UFS_QUERY_ATTR_IDN_EE_CONTROL] =
        UFS_QUERY_ATTR_READ | UFS_QUERY_ATTR_WRITE,
    [UFS_QUERY_ATTR_IDN_EE_STATUS] = UFS_QUERY_ATTR_READ,
    [UFS_QUERY_ATTR_IDN_SECONDS_PASSED] = UFS_QUERY_ATTR_WRITE,
    [UFS_QUERY_ATTR_IDN_CNTX_CONF] = UFS_QUERY_ATTR_READ,
    [UFS_QUERY_ATTR_IDN_FFU_STATUS] = UFS_QUERY_ATTR_READ,
    [UFS_QUERY_ATTR_IDN_PSA_STATE] = UFS_QUERY_ATTR_READ | UFS_QUERY_ATTR_WRITE,
    [UFS_QUERY_ATTR_IDN_PSA_DATA_SIZE] =
        UFS_QUERY_ATTR_READ | UFS_QUERY_ATTR_WRITE,
    [UFS_QUERY_ATTR_IDN_REF_CLK_GATING_WAIT_TIME] = UFS_QUERY_ATTR_READ,
    [UFS_QUERY_ATTR_IDN_CASE_ROUGH_TEMP] = UFS_QUERY_ATTR_READ,
    [UFS_QUERY_ATTR_IDN_HIGH_TEMP_BOUND] = UFS_QUERY_ATTR_READ,
    [UFS_QUERY_ATTR_IDN_LOW_TEMP_BOUND] = UFS_QUERY_ATTR_READ,
    [UFS_QUERY_ATTR_IDN_THROTTLING_STATUS] = UFS_QUERY_ATTR_READ,
    [UFS_QUERY_ATTR_IDN_WB_FLUSH_STATUS] = UFS_QUERY_ATTR_READ,
    [UFS_QUERY_ATTR_IDN_AVAIL_WB_BUFF_SIZE] = UFS_QUERY_ATTR_READ,
    [UFS_QUERY_ATTR_IDN_WB_BUFF_LIFE_TIME_EST] = UFS_QUERY_ATTR_READ,
    [UFS_QUERY_ATTR_IDN_CURR_WB_BUFF_SIZE] = UFS_QUERY_ATTR_READ,
    /* refresh operation is not supported */
    [UFS_QUERY_ATTR_IDN_REFRESH_STATUS] = UFS_QUERY_ATTR_READ,
    [UFS_QUERY_ATTR_IDN_REFRESH_FREQ] = UFS_QUERY_ATTR_READ,
    [UFS_QUERY_ATTR_IDN_REFRESH_UNIT] = UFS_QUERY_ATTR_READ,
};

static inline QueryRespCode ufs_attr_check_idn_valid(uint8_t idn, int op)
{
    if (idn >= UFS_QUERY_ATTR_IDN_COUNT) {
        return UFS_QUERY_RESULT_INVALID_IDN;
    }

    if (!(attr_permission[idn] & op)) {
        if (op == UFS_QUERY_ATTR_READ) {
            trace_ufs_err_query_attr_not_readable(idn);
            return UFS_QUERY_RESULT_NOT_READABLE;
        }
        trace_ufs_err_query_attr_not_writable(idn);
        return UFS_QUERY_RESULT_NOT_WRITEABLE;
    }

    return UFS_QUERY_RESULT_SUCCESS;
}

static QueryRespCode ufs_exec_query_flag(UfsRequest *req, int op)
{
    UfsHc *u = req->hc;
    uint8_t idn = req->req_upiu.qr.idn;
    uint32_t value;
    QueryRespCode ret;

    ret = ufs_flag_check_idn_valid(idn, op);
    if (ret) {
        return ret;
    }

    if (idn == UFS_QUERY_FLAG_IDN_FDEVICEINIT) {
        value = 0;
    } else if (op == UFS_QUERY_FLAG_READ) {
        value = *(((uint8_t *)&u->flags) + idn);
    } else if (op == UFS_QUERY_FLAG_SET) {
        value = 1;
    } else if (op == UFS_QUERY_FLAG_CLEAR) {
        value = 0;
    } else if (op == UFS_QUERY_FLAG_TOGGLE) {
        value = *(((uint8_t *)&u->flags) + idn);
        value = !value;
    } else {
        trace_ufs_err_query_invalid_opcode(op);
        return UFS_QUERY_RESULT_INVALID_OPCODE;
    }

    *(((uint8_t *)&u->flags) + idn) = value;
    req->rsp_upiu.qr.value = cpu_to_be32(value);
    return UFS_QUERY_RESULT_SUCCESS;
}

static inline uint8_t ufs_read_device_temp(UfsHc *u)
{
    uint8_t feat_sup = u->device_desc.ufs_features_support;
    bool high_temp_sup, low_temp_sup, high_temp_en, low_temp_en;
    uint16_t ee_control = be16_to_cpu(u->attributes.exception_event_control);

    high_temp_sup = feat_sup & UFS_DEV_HIGH_TEMP_NOTIF;
    low_temp_sup = feat_sup & UFS_DEV_LOW_TEMP_NOTIF;
    high_temp_en = ee_control & MASK_EE_TOO_HIGH_TEMP;
    low_temp_en = ee_control & MASK_EE_TOO_LOW_TEMP;

    if ((high_temp_sup && high_temp_en) ||
        (low_temp_sup && low_temp_en)) {
        return u->temperature;
    }

    return 0;
}

static uint32_t ufs_read_attr_value(UfsHc *u, uint8_t idn)
{
    switch (idn) {
    case UFS_QUERY_ATTR_IDN_BOOT_LU_EN:
        return u->attributes.boot_lun_en;
    case UFS_QUERY_ATTR_IDN_POWER_MODE:
        return u->attributes.current_power_mode;
    case UFS_QUERY_ATTR_IDN_ACTIVE_ICC_LVL:
        return u->attributes.active_icc_level;
    case UFS_QUERY_ATTR_IDN_OOO_DATA_EN:
        return u->attributes.out_of_order_data_en;
    case UFS_QUERY_ATTR_IDN_BKOPS_STATUS:
        return u->attributes.background_op_status;
    case UFS_QUERY_ATTR_IDN_PURGE_STATUS:
        return u->attributes.purge_status;
    case UFS_QUERY_ATTR_IDN_MAX_DATA_IN:
        return u->attributes.max_data_in_size;
    case UFS_QUERY_ATTR_IDN_MAX_DATA_OUT:
        return u->attributes.max_data_out_size;
    case UFS_QUERY_ATTR_IDN_DYN_CAP_NEEDED:
        return be32_to_cpu(u->attributes.dyn_cap_needed);
    case UFS_QUERY_ATTR_IDN_REF_CLK_FREQ:
        return u->attributes.ref_clk_freq;
    case UFS_QUERY_ATTR_IDN_CONF_DESC_LOCK:
        return u->attributes.config_descr_lock;
    case UFS_QUERY_ATTR_IDN_MAX_NUM_OF_RTT:
        return u->attributes.max_num_of_rtt;
    case UFS_QUERY_ATTR_IDN_EE_CONTROL:
        return be16_to_cpu(u->attributes.exception_event_control);
    case UFS_QUERY_ATTR_IDN_EE_STATUS:
        ufs_update_ee_status(u);
        return be16_to_cpu(u->attributes.exception_event_status);
    case UFS_QUERY_ATTR_IDN_SECONDS_PASSED:
        return be32_to_cpu(u->attributes.seconds_passed);
    case UFS_QUERY_ATTR_IDN_CNTX_CONF:
        return be16_to_cpu(u->attributes.context_conf);
    case UFS_QUERY_ATTR_IDN_FFU_STATUS:
        return u->attributes.device_ffu_status;
    case UFS_QUERY_ATTR_IDN_PSA_STATE:
        return be32_to_cpu(u->attributes.psa_state);
    case UFS_QUERY_ATTR_IDN_PSA_DATA_SIZE:
        return be32_to_cpu(u->attributes.psa_data_size);
    case UFS_QUERY_ATTR_IDN_REF_CLK_GATING_WAIT_TIME:
        return u->attributes.ref_clk_gating_wait_time;
    case UFS_QUERY_ATTR_IDN_CASE_ROUGH_TEMP:
        u->attributes.device_case_rough_temperature = ufs_read_device_temp(u);
        return u->attributes.device_case_rough_temperature;
    case UFS_QUERY_ATTR_IDN_HIGH_TEMP_BOUND:
        return u->attributes.device_too_high_temp_boundary;
    case UFS_QUERY_ATTR_IDN_LOW_TEMP_BOUND:
        return u->attributes.device_too_low_temp_boundary;
    case UFS_QUERY_ATTR_IDN_THROTTLING_STATUS:
        return u->attributes.throttling_status;
    case UFS_QUERY_ATTR_IDN_WB_FLUSH_STATUS:
        return u->attributes.wb_buffer_flush_status;
    case UFS_QUERY_ATTR_IDN_AVAIL_WB_BUFF_SIZE:
        return u->attributes.available_wb_buffer_size;
    case UFS_QUERY_ATTR_IDN_WB_BUFF_LIFE_TIME_EST:
        return u->attributes.wb_buffer_life_time_est;
    case UFS_QUERY_ATTR_IDN_CURR_WB_BUFF_SIZE:
        return be32_to_cpu(u->attributes.current_wb_buffer_size);
    case UFS_QUERY_ATTR_IDN_REFRESH_STATUS:
        return u->attributes.refresh_status;
    case UFS_QUERY_ATTR_IDN_REFRESH_FREQ:
        return u->attributes.refresh_freq;
    case UFS_QUERY_ATTR_IDN_REFRESH_UNIT:
        return u->attributes.refresh_unit;
    }
    return 0;
}

static QueryRespCode ufs_write_attr_value(UfsHc *u, uint8_t idn, uint32_t value)
{
    switch (idn) {
    case UFS_QUERY_ATTR_IDN_ACTIVE_ICC_LVL:
        if (value > UFS_QUERY_ATTR_ACTIVE_ICC_MAXVALUE) {
            return UFS_QUERY_RESULT_INVALID_VALUE;
        }
        u->attributes.active_icc_level = value;
        break;
    case UFS_QUERY_ATTR_IDN_MAX_DATA_IN:
        u->attributes.max_data_in_size = value;
        break;
    case UFS_QUERY_ATTR_IDN_MAX_DATA_OUT:
        u->attributes.max_data_out_size = value;
        break;
    case UFS_QUERY_ATTR_IDN_REF_CLK_FREQ:
        u->attributes.ref_clk_freq = value;
        break;
    case UFS_QUERY_ATTR_IDN_MAX_NUM_OF_RTT:
        u->attributes.max_num_of_rtt = value;
        break;
    case UFS_QUERY_ATTR_IDN_EE_CONTROL:
        u->attributes.exception_event_control = cpu_to_be16(value);
        break;
    case UFS_QUERY_ATTR_IDN_SECONDS_PASSED:
        u->attributes.seconds_passed = cpu_to_be32(value);
        break;
    case UFS_QUERY_ATTR_IDN_PSA_STATE:
        u->attributes.psa_state = value;
        break;
    case UFS_QUERY_ATTR_IDN_PSA_DATA_SIZE:
        u->attributes.psa_data_size = cpu_to_be32(value);
        break;
    }
    return UFS_QUERY_RESULT_SUCCESS;
}

static QueryRespCode ufs_exec_query_attr(UfsRequest *req, int op)
{
    UfsHc *u = req->hc;
    uint8_t idn = req->req_upiu.qr.idn;
    uint32_t value;
    QueryRespCode ret;

    ret = ufs_attr_check_idn_valid(idn, op);
    if (ret) {
        return ret;
    }

    if (op == UFS_QUERY_ATTR_READ) {
        value = ufs_read_attr_value(u, idn);
        ret = UFS_QUERY_RESULT_SUCCESS;
    } else {
        value = be32_to_cpu(req->req_upiu.qr.value);
        ret = ufs_write_attr_value(u, idn, value);
    }
    req->rsp_upiu.qr.value = cpu_to_be32(value);
    return ret;
}

static const RpmbUnitDescriptor rpmb_unit_desc = {
    .length = sizeof(RpmbUnitDescriptor),
    .descriptor_idn = 2,
    .unit_index = UFS_UPIU_RPMB_WLUN,
    .lu_enable = 0,
};

static QueryRespCode ufs_read_unit_desc(UfsRequest *req)
{
    UfsHc *u = req->hc;
    uint8_t lun = req->req_upiu.qr.index;

    if (lun != UFS_UPIU_RPMB_WLUN &&
        (lun >= UFS_MAX_LUS || u->lus[lun] == NULL)) {
        trace_ufs_err_query_invalid_index(req->req_upiu.qr.opcode, lun);
        return UFS_QUERY_RESULT_INVALID_INDEX;
    }

    if (lun == UFS_UPIU_RPMB_WLUN) {
        memcpy(&req->rsp_upiu.qr.data, &rpmb_unit_desc, rpmb_unit_desc.length);
    } else {
        memcpy(&req->rsp_upiu.qr.data, &u->lus[lun]->unit_desc,
               sizeof(u->lus[lun]->unit_desc));
    }

    return UFS_QUERY_RESULT_SUCCESS;
}

static inline StringDescriptor manufacturer_str_desc(void)
{
    StringDescriptor desc = {
        .length = 0x12,
        .descriptor_idn = UFS_QUERY_DESC_IDN_STRING,
    };
    desc.UC[0] = cpu_to_be16('R');
    desc.UC[1] = cpu_to_be16('E');
    desc.UC[2] = cpu_to_be16('D');
    desc.UC[3] = cpu_to_be16('H');
    desc.UC[4] = cpu_to_be16('A');
    desc.UC[5] = cpu_to_be16('T');
    return desc;
}

static inline StringDescriptor product_name_str_desc(void)
{
    StringDescriptor desc = {
        .length = 0x22,
        .descriptor_idn = UFS_QUERY_DESC_IDN_STRING,
    };
    desc.UC[0] = cpu_to_be16('Q');
    desc.UC[1] = cpu_to_be16('E');
    desc.UC[2] = cpu_to_be16('M');
    desc.UC[3] = cpu_to_be16('U');
    desc.UC[4] = cpu_to_be16(' ');
    desc.UC[5] = cpu_to_be16('U');
    desc.UC[6] = cpu_to_be16('F');
    desc.UC[7] = cpu_to_be16('S');
    return desc;
}

static inline StringDescriptor product_rev_level_str_desc(void)
{
    StringDescriptor desc = {
        .length = 0x0a,
        .descriptor_idn = UFS_QUERY_DESC_IDN_STRING,
    };
    desc.UC[0] = cpu_to_be16('0');
    desc.UC[1] = cpu_to_be16('0');
    desc.UC[2] = cpu_to_be16('0');
    desc.UC[3] = cpu_to_be16('1');
    return desc;
}

static const StringDescriptor null_str_desc = {
    .length = 0x02,
    .descriptor_idn = UFS_QUERY_DESC_IDN_STRING,
};

static QueryRespCode ufs_read_string_desc(UfsRequest *req)
{
    UfsHc *u = req->hc;
    uint8_t index = req->req_upiu.qr.index;
    StringDescriptor desc;

    if (index == u->device_desc.manufacturer_name) {
        desc = manufacturer_str_desc();
        memcpy(&req->rsp_upiu.qr.data, &desc, desc.length);
    } else if (index == u->device_desc.product_name) {
        desc = product_name_str_desc();
        memcpy(&req->rsp_upiu.qr.data, &desc, desc.length);
    } else if (index == u->device_desc.serial_number) {
        memcpy(&req->rsp_upiu.qr.data, &null_str_desc, null_str_desc.length);
    } else if (index == u->device_desc.oem_id) {
        memcpy(&req->rsp_upiu.qr.data, &null_str_desc, null_str_desc.length);
    } else if (index == u->device_desc.product_revision_level) {
        desc = product_rev_level_str_desc();
        memcpy(&req->rsp_upiu.qr.data, &desc, desc.length);
    } else {
        trace_ufs_err_query_invalid_index(req->req_upiu.qr.opcode, index);
        return UFS_QUERY_RESULT_INVALID_INDEX;
    }
    return UFS_QUERY_RESULT_SUCCESS;
}

static inline InterconnectDescriptor interconnect_desc(void)
{
    InterconnectDescriptor desc = {
        .length = sizeof(InterconnectDescriptor),
        .descriptor_idn = UFS_QUERY_DESC_IDN_INTERCONNECT,
    };
    desc.bcd_unipro_version = cpu_to_be16(0x180);
    desc.bcd_mphy_version = cpu_to_be16(0x410);
    return desc;
}

static QueryRespCode ufs_read_desc(UfsRequest *req)
{
    UfsHc *u = req->hc;
    QueryRespCode status;
    uint8_t idn = req->req_upiu.qr.idn;
    uint8_t selector = req->req_upiu.qr.selector;
    uint16_t length = be16_to_cpu(req->req_upiu.qr.length);
    InterconnectDescriptor desc;
    if (selector != 0) {
        return UFS_QUERY_RESULT_INVALID_SELECTOR;
    }
    switch (idn) {
    case UFS_QUERY_DESC_IDN_DEVICE:
        memcpy(&req->rsp_upiu.qr.data, &u->device_desc, sizeof(u->device_desc));
        status = UFS_QUERY_RESULT_SUCCESS;
        break;
    case UFS_QUERY_DESC_IDN_UNIT:
        status = ufs_read_unit_desc(req);
        break;
    case UFS_QUERY_DESC_IDN_GEOMETRY:
        memcpy(&req->rsp_upiu.qr.data, &u->geometry_desc,
               sizeof(u->geometry_desc));
        status = UFS_QUERY_RESULT_SUCCESS;
        break;
    case UFS_QUERY_DESC_IDN_INTERCONNECT: {
        desc = interconnect_desc();
        memcpy(&req->rsp_upiu.qr.data, &desc, sizeof(InterconnectDescriptor));
        status = UFS_QUERY_RESULT_SUCCESS;
        break;
    }
    case UFS_QUERY_DESC_IDN_STRING:
        status = ufs_read_string_desc(req);
        break;
    case UFS_QUERY_DESC_IDN_POWER:
        /* mocking of power descriptor is not supported */
        memset(&req->rsp_upiu.qr.data, 0, sizeof(PowerParametersDescriptor));
        req->rsp_upiu.qr.data[0] = sizeof(PowerParametersDescriptor);
        req->rsp_upiu.qr.data[1] = UFS_QUERY_DESC_IDN_POWER;
        status = UFS_QUERY_RESULT_SUCCESS;
        break;
    case UFS_QUERY_DESC_IDN_HEALTH:
        /* mocking of health descriptor is not supported */
        memset(&req->rsp_upiu.qr.data, 0, sizeof(DeviceHealthDescriptor));
        req->rsp_upiu.qr.data[0] = sizeof(DeviceHealthDescriptor);
        req->rsp_upiu.qr.data[1] = UFS_QUERY_DESC_IDN_HEALTH;
        status = UFS_QUERY_RESULT_SUCCESS;
        break;
    default:
        length = 0;
        trace_ufs_err_query_invalid_idn(req->req_upiu.qr.opcode, idn);
        status = UFS_QUERY_RESULT_INVALID_IDN;
    }

    if (length > req->rsp_upiu.qr.data[0]) {
        length = req->rsp_upiu.qr.data[0];
    }
    req->rsp_upiu.qr.length = cpu_to_be16(length);

    return status;
}

static QueryRespCode ufs_exec_query_read(UfsRequest *req)
{
    QueryRespCode status;
    switch (req->req_upiu.qr.opcode) {
    case UFS_UPIU_QUERY_OPCODE_NOP:
        status = UFS_QUERY_RESULT_SUCCESS;
        break;
    case UFS_UPIU_QUERY_OPCODE_READ_DESC:
        status = ufs_read_desc(req);
        break;
    case UFS_UPIU_QUERY_OPCODE_READ_ATTR:
        status = ufs_exec_query_attr(req, UFS_QUERY_ATTR_READ);
        break;
    case UFS_UPIU_QUERY_OPCODE_READ_FLAG:
        status = ufs_exec_query_flag(req, UFS_QUERY_FLAG_READ);
        break;
    default:
        trace_ufs_err_query_invalid_opcode(req->req_upiu.qr.opcode);
        status = UFS_QUERY_RESULT_INVALID_OPCODE;
        break;
    }

    return status;
}

static QueryRespCode ufs_exec_query_write(UfsRequest *req)
{
    QueryRespCode status;
    switch (req->req_upiu.qr.opcode) {
    case UFS_UPIU_QUERY_OPCODE_NOP:
        status = UFS_QUERY_RESULT_SUCCESS;
        break;
    case UFS_UPIU_QUERY_OPCODE_WRITE_DESC:
        /* write descriptor is not supported */
        status = UFS_QUERY_RESULT_NOT_WRITEABLE;
        break;
    case UFS_UPIU_QUERY_OPCODE_WRITE_ATTR:
        status = ufs_exec_query_attr(req, UFS_QUERY_ATTR_WRITE);
        break;
    case UFS_UPIU_QUERY_OPCODE_SET_FLAG:
        status = ufs_exec_query_flag(req, UFS_QUERY_FLAG_SET);
        break;
    case UFS_UPIU_QUERY_OPCODE_CLEAR_FLAG:
        status = ufs_exec_query_flag(req, UFS_QUERY_FLAG_CLEAR);
        break;
    case UFS_UPIU_QUERY_OPCODE_TOGGLE_FLAG:
        status = ufs_exec_query_flag(req, UFS_QUERY_FLAG_TOGGLE);
        break;
    default:
        trace_ufs_err_query_invalid_opcode(req->req_upiu.qr.opcode);
        status = UFS_QUERY_RESULT_INVALID_OPCODE;
        break;
    }

    return status;
}

static UfsReqResult ufs_exec_query_cmd(UfsRequest *req)
{
    uint8_t query_func = req->req_upiu.header.query_func;
    uint16_t data_segment_length;
    QueryRespCode status;

    trace_ufs_exec_query_cmd(req->slot, req->req_upiu.qr.opcode);
    if (query_func == UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST) {
        status = ufs_exec_query_read(req);
    } else if (query_func == UFS_UPIU_QUERY_FUNC_STANDARD_WRITE_REQUEST) {
        status = ufs_exec_query_write(req);
    } else {
        status = UFS_QUERY_RESULT_GENERAL_FAILURE;
    }

    data_segment_length = be16_to_cpu(req->rsp_upiu.qr.length);
    ufs_build_upiu_header(req, UFS_UPIU_TRANSACTION_QUERY_RSP, 0, status, 0,
                          data_segment_length);
    ufs_build_query_response(req);

    if (status != UFS_QUERY_RESULT_SUCCESS) {
        return UFS_REQUEST_FAIL;
    }
    return UFS_REQUEST_SUCCESS;
}

static void ufs_exec_req(UfsRequest *req)
{
    UfsReqResult req_result;

    if (ufs_dma_read_upiu(req)) {
        return;
    }

    switch (req->req_upiu.header.trans_type) {
    case UFS_UPIU_TRANSACTION_NOP_OUT:
        req_result = ufs_exec_nop_cmd(req);
        break;
    case UFS_UPIU_TRANSACTION_COMMAND:
        req_result = ufs_exec_scsi_cmd(req);
        break;
    case UFS_UPIU_TRANSACTION_QUERY_REQ:
        req_result = ufs_exec_query_cmd(req);
        break;
    default:
        trace_ufs_err_invalid_trans_code(req->slot,
                                         req->req_upiu.header.trans_type);
        req_result = UFS_REQUEST_FAIL;
    }

    /*
     * The ufs_complete_req for scsi commands is handled by the
     * ufs_scsi_command_complete() callback function. Therefore, to avoid
     * duplicate processing, ufs_complete_req() is not called for scsi commands.
     */
    if (req_result != UFS_REQUEST_NO_COMPLETE) {
        ufs_complete_req(req, req_result);
    }
}

static void ufs_process_req(void *opaque)
{
    UfsHc *u = opaque;
    UfsRequest *req;
    int slot;

    for (slot = 0; slot < u->params.nutrs; slot++) {
        req = &u->req_list[slot];

        if (req->state != UFS_REQUEST_READY) {
            continue;
        }
        trace_ufs_process_req(slot);
        req->state = UFS_REQUEST_RUNNING;

        ufs_exec_req(req);
    }
}

void ufs_complete_req(UfsRequest *req, UfsReqResult req_result)
{
    UfsHc *u = req->hc;
    assert(req->state == UFS_REQUEST_RUNNING);

    if (req_result == UFS_REQUEST_SUCCESS) {
        req->utrd.header.dword_2 = cpu_to_le32(UFS_OCS_SUCCESS);
    } else {
        req->utrd.header.dword_2 = cpu_to_le32(UFS_OCS_INVALID_CMD_TABLE_ATTR);
    }

    req->state = UFS_REQUEST_COMPLETE;

    if (ufs_mcq_req(req)) {
        trace_ufs_mcq_complete_req(req->sq->sqid);
        QTAILQ_INSERT_TAIL(&req->sq->cq->req_list, req, entry);
        qemu_bh_schedule(req->sq->cq->bh);
    } else {
        trace_ufs_complete_req(req->slot);
        qemu_bh_schedule(u->complete_bh);
    }
}

static void ufs_clear_req(UfsRequest *req)
{
    if (req->sg != NULL) {
        qemu_sglist_destroy(req->sg);
        g_free(req->sg);
        req->sg = NULL;
        req->data_len = 0;
    }

    memset(&req->utrd, 0, sizeof(req->utrd));
    memset(&req->req_upiu, 0, sizeof(req->req_upiu));
    memset(&req->rsp_upiu, 0, sizeof(req->rsp_upiu));
}

static void ufs_sendback_req(void *opaque)
{
    UfsHc *u = opaque;
    UfsRequest *req;
    int slot;

    for (slot = 0; slot < u->params.nutrs; slot++) {
        req = &u->req_list[slot];

        if (req->state != UFS_REQUEST_COMPLETE) {
            continue;
        }

        if (ufs_dma_write_upiu(req)) {
            req->state = UFS_REQUEST_ERROR;
            continue;
        }

        /*
         * TODO: UTP Transfer Request Interrupt Aggregation Control is not yet
         * supported
         */
        if (le32_to_cpu(req->utrd.header.dword_2) != UFS_OCS_SUCCESS ||
            le32_to_cpu(req->utrd.header.dword_0) & UFS_UTP_REQ_DESC_INT_CMD) {
            u->reg.is = FIELD_DP32(u->reg.is, IS, UTRCS, 1);
        }

        u->reg.utrldbr &= ~(1 << slot);
        u->reg.utrlcnr |= (1 << slot);

        trace_ufs_sendback_req(req->slot);

        ufs_clear_req(req);
        req->state = UFS_REQUEST_IDLE;
    }

    ufs_irq_check(u);
}

static bool ufs_check_constraints(UfsHc *u, Error **errp)
{
    if (u->params.nutrs > UFS_MAX_NUTRS) {
        error_setg(errp, "nutrs must be less than or equal to %d",
                   UFS_MAX_NUTRS);
        return false;
    }

    if (u->params.nutmrs > UFS_MAX_NUTMRS) {
        error_setg(errp, "nutmrs must be less than or equal to %d",
                   UFS_MAX_NUTMRS);
        return false;
    }

    if (u->params.mcq_maxq >= UFS_MAX_MCQ_QNUM) {
        error_setg(errp, "mcq-maxq must be less than %d", UFS_MAX_MCQ_QNUM);
        return false;
    }

    return true;
}

static void ufs_init_pci(UfsHc *u, PCIDevice *pci_dev)
{
    uint8_t *pci_conf = pci_dev->config;

    pci_conf[PCI_INTERRUPT_PIN] = 1;
    pci_config_set_prog_interface(pci_conf, 0x1);

    memory_region_init_io(&u->iomem, OBJECT(u), &ufs_mmio_ops, u, "ufs",
                          u->reg_size);
    pci_register_bar(pci_dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &u->iomem);
    u->irq = pci_allocate_irq(pci_dev);
}

static void ufs_init_state(UfsHc *u)
{
    u->req_list = g_new0(UfsRequest, u->params.nutrs);

    for (int i = 0; i < u->params.nutrs; i++) {
        u->req_list[i].hc = u;
        u->req_list[i].slot = i;
        u->req_list[i].sg = NULL;
        u->req_list[i].state = UFS_REQUEST_IDLE;
    }

    u->doorbell_bh = qemu_bh_new_guarded(ufs_process_req, u,
                                         &DEVICE(u)->mem_reentrancy_guard);
    u->complete_bh = qemu_bh_new_guarded(ufs_sendback_req, u,
                                         &DEVICE(u)->mem_reentrancy_guard);

    if (u->params.mcq) {
        memset(u->sq, 0, sizeof(u->sq));
        memset(u->cq, 0, sizeof(u->cq));
    }
}

static void ufs_init_hc(UfsHc *u)
{
    uint32_t cap = 0;
    uint32_t mcqconfig = 0;
    uint32_t mcqcap = 0;

    u->reg_size = pow2ceil(ufs_reg_size(u));

    memset(&u->reg, 0, sizeof(u->reg));
    memset(&u->mcq_reg, 0, sizeof(u->mcq_reg));
    memset(&u->mcq_op_reg, 0, sizeof(u->mcq_op_reg));
    cap = FIELD_DP32(cap, CAP, NUTRS, (u->params.nutrs - 1));
    cap = FIELD_DP32(cap, CAP, RTT, 2);
    cap = FIELD_DP32(cap, CAP, NUTMRS, (u->params.nutmrs - 1));
    cap = FIELD_DP32(cap, CAP, AUTOH8, 0);
    cap = FIELD_DP32(cap, CAP, 64AS, 1);
    cap = FIELD_DP32(cap, CAP, OODDS, 0);
    cap = FIELD_DP32(cap, CAP, UICDMETMS, 0);
    cap = FIELD_DP32(cap, CAP, CS, 0);
    cap = FIELD_DP32(cap, CAP, LSDBS, 0);
    cap = FIELD_DP32(cap, CAP, MCQS, u->params.mcq);
    u->reg.cap = cap;

    if (u->params.mcq) {
        mcqconfig = FIELD_DP32(mcqconfig, MCQCONFIG, MAC, 0x1f);
        u->reg.mcqconfig = mcqconfig;

        mcqcap = FIELD_DP32(mcqcap, MCQCAP, MAXQ, u->params.mcq_maxq - 1);
        mcqcap = FIELD_DP32(mcqcap, MCQCAP, RRP, 1);
        mcqcap = FIELD_DP32(mcqcap, MCQCAP, QCFGPTR, UFS_MCQ_QCFGPTR);
        u->reg.mcqcap = mcqcap;

        for (int i = 0; i < ARRAY_SIZE(u->mcq_reg); i++) {
            uint64_t addr = ufs_mcq_op_reg_addr(u, i);
            u->mcq_reg[i].sqdao = addr;
            u->mcq_reg[i].sqisao = addr + sizeof(UfsMcqSqReg);
            addr += sizeof(UfsMcqSqReg);
            u->mcq_reg[i].cqdao = addr + sizeof(UfsMcqSqIntReg);
            addr += sizeof(UfsMcqSqIntReg);
            u->mcq_reg[i].cqisao = addr + sizeof(UfsMcqCqReg);
        }
    }
    u->reg.ver = UFS_SPEC_VER;

    memset(&u->device_desc, 0, sizeof(DeviceDescriptor));
    u->device_desc.length = sizeof(DeviceDescriptor);
    u->device_desc.descriptor_idn = UFS_QUERY_DESC_IDN_DEVICE;
    u->device_desc.device_sub_class = 0x01;
    u->device_desc.number_lu = 0x00;
    u->device_desc.number_wlu = 0x04;
    /* TODO: Revisit it when Power Management is implemented */
    u->device_desc.init_power_mode = 0x01; /* Active Mode */
    u->device_desc.high_priority_lun = 0x7F; /* Same Priority */
    u->device_desc.spec_version = cpu_to_be16(UFS_SPEC_VER);
    u->device_desc.manufacturer_name = 0x00;
    u->device_desc.product_name = 0x01;
    u->device_desc.serial_number = 0x02;
    u->device_desc.oem_id = 0x03;
    u->device_desc.ud_0_base_offset = 0x16;
    u->device_desc.ud_config_p_length = 0x1A;
    u->device_desc.device_rtt_cap = 0x02;
    u->device_desc.ufs_features_support = UFS_DEV_HIGH_TEMP_NOTIF |
        UFS_DEV_LOW_TEMP_NOTIF;
    u->device_desc.queue_depth = u->params.nutrs;
    u->device_desc.product_revision_level = 0x04;
    u->device_desc.extended_ufs_features_support =
        cpu_to_be32(UFS_DEV_HIGH_TEMP_NOTIF | UFS_DEV_LOW_TEMP_NOTIF);

    memset(&u->geometry_desc, 0, sizeof(GeometryDescriptor));
    u->geometry_desc.length = sizeof(GeometryDescriptor);
    u->geometry_desc.descriptor_idn = UFS_QUERY_DESC_IDN_GEOMETRY;
    u->geometry_desc.max_number_lu = (UFS_MAX_LUS == 32) ? 0x1 : 0x0;
    u->geometry_desc.segment_size = cpu_to_be32(0x2000); /* 4MB: 8192 * 512B */
    u->geometry_desc.allocation_unit_size = 0x1; /* 4MB: 1 segment */
    u->geometry_desc.min_addr_block_size = 0x8; /* 4KB */
    u->geometry_desc.max_in_buffer_size = 0x8;
    u->geometry_desc.max_out_buffer_size = 0x8;
    u->geometry_desc.rpmb_read_write_size = 0x40;
    u->geometry_desc.data_ordering =
        0x0; /* out-of-order data transfer is not supported */
    u->geometry_desc.max_context_id_number = 0x5;
    u->geometry_desc.supported_memory_types = cpu_to_be16(0x8001);

    memset(&u->attributes, 0, sizeof(u->attributes));
    u->attributes.max_data_in_size = 0x08;
    u->attributes.max_data_out_size = 0x08;
    u->attributes.ref_clk_freq = 0x01; /* 26 MHz */
    /* configure descriptor is not supported */
    u->attributes.config_descr_lock = 0x01;
    u->attributes.max_num_of_rtt = 0x02;
    u->attributes.device_too_high_temp_boundary = UFS_TOO_HIGH_TEMP_BOUNDARY;
    u->attributes.device_too_low_temp_boundary = UFS_TOO_LOW_TEMP_BOUNDARY;

    memset(&u->flags, 0, sizeof(u->flags));
    u->flags.permanently_disable_fw_update = 1;

    /*
     * The temperature value is fixed to UFS_TEMPERATURE and does not change
     * dynamically
     */
    u->temperature = UFS_TEMPERATURE;
}

static void ufs_realize(PCIDevice *pci_dev, Error **errp)
{
    UfsHc *u = UFS(pci_dev);

    if (!ufs_check_constraints(u, errp)) {
        return;
    }

    qbus_init(&u->bus, sizeof(UfsBus), TYPE_UFS_BUS, &pci_dev->qdev,
              u->parent_obj.qdev.id);

    ufs_init_state(u);
    ufs_init_hc(u);
    ufs_init_pci(u, pci_dev);

    ufs_init_wlu(&u->report_wlu, UFS_UPIU_REPORT_LUNS_WLUN);
    ufs_init_wlu(&u->dev_wlu, UFS_UPIU_UFS_DEVICE_WLUN);
    ufs_init_wlu(&u->boot_wlu, UFS_UPIU_BOOT_WLUN);
    ufs_init_wlu(&u->rpmb_wlu, UFS_UPIU_RPMB_WLUN);
}

static void ufs_exit(PCIDevice *pci_dev)
{
    UfsHc *u = UFS(pci_dev);

    qemu_free_irq(u->irq);

    qemu_bh_delete(u->doorbell_bh);
    qemu_bh_delete(u->complete_bh);

    for (int i = 0; i < u->params.nutrs; i++) {
        ufs_clear_req(&u->req_list[i]);
    }
    g_free(u->req_list);

    for (int i = 0; i < ARRAY_SIZE(u->sq); i++) {
        if (u->sq[i]) {
            ufs_mcq_delete_sq(u, i);
        }
    }
    for (int i = 0; i < ARRAY_SIZE(u->cq); i++) {
        if (u->cq[i]) {
            ufs_mcq_delete_cq(u, i);
        }
    }
}

static const Property ufs_props[] = {
    DEFINE_PROP_STRING("serial", UfsHc, params.serial),
    DEFINE_PROP_UINT8("nutrs", UfsHc, params.nutrs, 32),
    DEFINE_PROP_UINT8("nutmrs", UfsHc, params.nutmrs, 8),
    DEFINE_PROP_BOOL("mcq", UfsHc, params.mcq, false),
    DEFINE_PROP_UINT8("mcq-maxq", UfsHc, params.mcq_maxq, 2),
};

static const VMStateDescription ufs_vmstate = {
    .name = "ufs",
    .unmigratable = 1,
};

static void ufs_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(oc);

    pc->realize = ufs_realize;
    pc->exit = ufs_exit;
    pc->vendor_id = PCI_VENDOR_ID_REDHAT;
    pc->device_id = PCI_DEVICE_ID_REDHAT_UFS;
    pc->class_id = PCI_CLASS_STORAGE_UFS;

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->desc = "Universal Flash Storage";
    device_class_set_props(dc, ufs_props);
    dc->vmsd = &ufs_vmstate;
}

static bool ufs_bus_check_address(BusState *qbus, DeviceState *qdev,
                                  Error **errp)
{
    if (strcmp(object_get_typename(OBJECT(qdev)), TYPE_UFS_LU) != 0) {
        error_setg(errp, "%s cannot be connected to ufs-bus",
                   object_get_typename(OBJECT(qdev)));
        return false;
    }

    return true;
}

static char *ufs_bus_get_dev_path(DeviceState *dev)
{
    BusState *bus = qdev_get_parent_bus(dev);

    return qdev_get_dev_path(bus->parent);
}

static void ufs_bus_class_init(ObjectClass *class, void *data)
{
    BusClass *bc = BUS_CLASS(class);
    bc->get_dev_path = ufs_bus_get_dev_path;
    bc->check_address = ufs_bus_check_address;
}

static const TypeInfo ufs_info = {
    .name = TYPE_UFS,
    .parent = TYPE_PCI_DEVICE,
    .class_init = ufs_class_init,
    .instance_size = sizeof(UfsHc),
    .interfaces = (InterfaceInfo[]){ { INTERFACE_PCIE_DEVICE }, {} },
};

static const TypeInfo ufs_bus_info = {
    .name = TYPE_UFS_BUS,
    .parent = TYPE_BUS,
    .class_init = ufs_bus_class_init,
    .class_size = sizeof(UfsBusClass),
    .instance_size = sizeof(UfsBus),
};

static void ufs_register_types(void)
{
    type_register_static(&ufs_info);
    type_register_static(&ufs_bus_info);
}

type_init(ufs_register_types)
