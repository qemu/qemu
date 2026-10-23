/*
 * QTest testcase for UFS
 *
 * Copyright (c) 2023 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "libqtest.h"
#include "libqos/qgraph.h"
#include "libqos/pci.h"
#include "scsi/constants.h"
#include "block/ufs.h"
#include "qemu/bitmap.h"
#include "qemu/bswap.h"

#define DWORD_BYTE 4
/* Test images sizes in Bytes */
#define TEST_IMAGE_SIZE (64 * 1024 * 1024)
/* Timeout for various operations, in seconds. */
#define TIMEOUT_SECONDS 10
/* Maximum PRD entry count */
#define MAX_PRD_ENTRY_COUNT 10
#define PRD_ENTRY_DATA_SIZE 4096
/* Constants to build upiu */
#define UTP_COMMAND_DESCRIPTOR_SIZE 4096
#define UTP_RESPONSE_UPIU_OFFSET 1024
#define UTP_PRDT_UPIU_OFFSET 2048
#define UTRD_TEST_SLOT 0
#define UFS_MAX_CMD_DESC 32
/* Constants for MCQ */
#define TEST_QID 0
#define QUEUE_SIZE 32
#define UFS_MCQ_MAX_QNUM 32
#define ACPI_PCIHP_ADDR 0xae00
#define PCI_EJ_BASE 0x0008

typedef struct QUfs QUfs;

struct QUfs {
    QOSGraphObject obj;
    QPCIDevice dev;
    QPCIBar bar;

    DECLARE_BITMAP(cmd_desc_bitmap, UFS_MAX_CMD_DESC);
    uint64_t cmd_desc_addr;
    uint64_t data_buffer_addr;

    bool enabled;
    bool support_mcq;

    /* for legacy doorbell mode */
    uint64_t utrlba;

    /* for mcq mode */
    uint32_t maxq;
    uint64_t sqlba[UFS_MCQ_MAX_QNUM];
    uint64_t cqlba[UFS_MCQ_MAX_QNUM];
    uint64_t sqdao[UFS_MCQ_MAX_QNUM];
    uint64_t cqdao[UFS_MCQ_MAX_QNUM];
};

static inline uint32_t ufs_rreg(QUfs *ufs, size_t offset)
{
    return qpci_io_readl(&ufs->dev, ufs->bar, offset);
}

static inline void ufs_wreg(QUfs *ufs, size_t offset, uint32_t value)
{
    qpci_io_writel(&ufs->dev, ufs->bar, offset, value);
}

static int alloc_cmd_desc_slot(QUfs *ufs)
{
    int slot = find_first_zero_bit(ufs->cmd_desc_bitmap, UFS_MAX_CMD_DESC);
    if (slot == UFS_MAX_CMD_DESC) {
        g_assert_not_reached();
    }
    set_bit(slot, ufs->cmd_desc_bitmap);
    return slot;
}

static void release_cmd_desc_slot(QUfs *ufs, int slot)
{
    if (!test_bit(slot, ufs->cmd_desc_bitmap)) {
        g_assert_not_reached();
    }
    clear_bit(slot, ufs->cmd_desc_bitmap);
}

static void ufs_wait_for_irq(QUfs *ufs)
{
    uint64_t end_time;
    uint32_t is;
    /* Wait for device to reset as the linux driver does. */
    end_time = g_get_monotonic_time() + TIMEOUT_SECONDS * G_TIME_SPAN_SECOND;
    do {
        qtest_clock_step(ufs->dev.bus->qts, 100);
        is = ufs_rreg(ufs, A_IS);
    } while (is == 0 && g_get_monotonic_time() < end_time);
}

static UtpTransferReqDesc ufs_build_req_utrd(uint64_t command_desc_base_addr,
                                             uint32_t data_direction,
                                             uint16_t prd_table_length)
{
    UtpTransferReqDesc req = { 0 };

    req.header.dword_0 =
        cpu_to_le32(1 << 28 | data_direction | UFS_UTP_REQ_DESC_INT_CMD);
    req.header.dword_2 = cpu_to_le32(UFS_OCS_INVALID_COMMAND_STATUS);

    req.command_desc_base_addr_hi = cpu_to_le32(command_desc_base_addr >> 32);
    req.command_desc_base_addr_lo =
        cpu_to_le32(command_desc_base_addr & 0xffffffff);
    req.response_upiu_offset =
        cpu_to_le16(UTP_RESPONSE_UPIU_OFFSET / sizeof(uint32_t));
    req.response_upiu_length = cpu_to_le16(sizeof(UtpUpiuRsp));
    req.prd_table_offset = cpu_to_le16(UTP_PRDT_UPIU_OFFSET / sizeof(uint32_t));
    req.prd_table_length = cpu_to_le16(prd_table_length);
    return req;
}

static enum UtpOcsCodes
__ufs_send_transfer_request_doorbell(QUfs *ufs, uint8_t lun,
                                     const UtpTransferReqDesc *utrd)
{
    uint64_t utrd_addr =
        ufs->utrlba + UTRD_TEST_SLOT * sizeof(UtpTransferReqDesc);
    UtpTransferReqDesc utrd_result;

    qtest_memwrite(ufs->dev.bus->qts, utrd_addr, utrd, sizeof(*utrd));

    /* Ring the doorbell */
    ufs_wreg(ufs, A_UTRLDBR, 1);
    ufs_wait_for_irq(ufs);
    g_assert_true(FIELD_EX32(ufs_rreg(ufs, A_IS), IS, UTRCS));
    ufs_wreg(ufs, A_IS, FIELD_DP32(0, IS, UTRCS, 1));

    /* Handle completed command */
    qtest_memread(ufs->dev.bus->qts, utrd_addr, &utrd_result,
                  sizeof(utrd_result));
    return le32_to_cpu(utrd_result.header.dword_2) & 0xf;
}

static enum UtpOcsCodes
__ufs_send_transfer_request_mcq(QUfs *ufs, uint8_t lun,
                                const UtpTransferReqDesc *utrd)
{
    uint32_t sqtp = ufs_rreg(ufs, ufs->sqdao[TEST_QID] + 0x4);
    uint64_t utrd_addr = ufs->sqlba[TEST_QID] + sqtp;
    uint32_t cqhp;
    uint64_t cqentry_addr;
    UfsCqEntry cqentry;

    qtest_memwrite(ufs->dev.bus->qts, utrd_addr, utrd, sizeof(*utrd));

    /* Insert a new entry into the submission queue */
    sqtp = ufs_rreg(ufs, ufs->sqdao[TEST_QID] + 0x4);
    sqtp = (sqtp + sizeof(UfsSqEntry)) % (QUEUE_SIZE * sizeof(UfsSqEntry));
    ufs_wreg(ufs, ufs->sqdao[TEST_QID] + 0x4, sqtp);
    ufs_wait_for_irq(ufs);
    g_assert_true(FIELD_EX32(ufs_rreg(ufs, A_IS), IS, CQES));
    ufs_wreg(ufs, A_IS, FIELD_DP32(0, IS, CQES, 1));

    /* Handle the completed command from the completion queue */
    cqhp = ufs_rreg(ufs, ufs->cqdao[TEST_QID]);
    cqentry_addr = ufs->cqlba[TEST_QID] + cqhp;
    qtest_memread(ufs->dev.bus->qts, cqentry_addr, &cqentry, sizeof(cqentry));
    cqhp = (cqhp + sizeof(UfsCqEntry)) % (QUEUE_SIZE * sizeof(UfsCqEntry));
    ufs_wreg(ufs, ufs->cqdao[TEST_QID], cqhp);

    return cqentry.status;
}

static enum UtpOcsCodes
ufs_send_transfer_request_sync(QUfs *ufs, uint8_t lun,
                               const UtpTransferReqDesc *utrd)
{
    if (ufs->support_mcq) {
        return __ufs_send_transfer_request_mcq(ufs, lun, utrd);
    }

    return __ufs_send_transfer_request_doorbell(ufs, lun, utrd);
}

static enum UtpOcsCodes ufs_send_nop_out(QUfs *ufs, UtpUpiuRsp *rsp_out)
{
    int cmd_desc_slot = alloc_cmd_desc_slot(ufs);
    uint64_t req_upiu_addr =
        ufs->cmd_desc_addr + cmd_desc_slot * UTP_COMMAND_DESCRIPTOR_SIZE;
    uint64_t rsp_upiu_addr = req_upiu_addr + UTP_RESPONSE_UPIU_OFFSET;

    /* Build up request upiu */
    UtpUpiuReq req_upiu = { 0 };
    req_upiu.header.trans_type = UFS_UPIU_TRANSACTION_NOP_OUT;
    req_upiu.header.task_tag = cmd_desc_slot;
    qtest_memwrite(ufs->dev.bus->qts, req_upiu_addr, &req_upiu,
                   sizeof(req_upiu));

    /* Build up utp transfer request descriptor */
    UtpTransferReqDesc utrd =
        ufs_build_req_utrd(req_upiu_addr, UFS_UTP_NO_DATA_TRANSFER, 0);

    /* Send Transfer Request */
    enum UtpOcsCodes ret = ufs_send_transfer_request_sync(ufs, 0, &utrd);

    qtest_memread(ufs->dev.bus->qts, rsp_upiu_addr, rsp_out, sizeof(*rsp_out));
    release_cmd_desc_slot(ufs, cmd_desc_slot);
    return ret;
}

static bool ufs_mcq_sq_has_space(QUfs *ufs)
{
    uint32_t sqhp = ufs_rreg(ufs, ufs->sqdao[TEST_QID]);
    uint32_t sqtp = ufs_rreg(ufs, ufs->sqdao[TEST_QID] + 0x4);
    uint32_t next_sqtp =
        (sqtp + sizeof(UfsSqEntry)) % (QUEUE_SIZE * sizeof(UfsSqEntry));
    return next_sqtp != sqhp;
}

static void
__ufs_send_transfer_request_mcq_async(QUfs *ufs, uint8_t lun,
                                      const UtpTransferReqDesc *utrd)
{
    uint32_t sqtp;
    uint64_t utrd_addr;

    /* Wait for SQ space */
    while (!ufs_mcq_sq_has_space(ufs)) {
        qtest_clock_step(ufs->dev.bus->qts, 100);
    }

    sqtp = ufs_rreg(ufs, ufs->sqdao[TEST_QID] + 0x4);
    utrd_addr = ufs->sqlba[TEST_QID] + sqtp;
    qtest_memwrite(ufs->dev.bus->qts, utrd_addr, utrd, sizeof(*utrd));
    sqtp = (sqtp + sizeof(UfsSqEntry)) % (QUEUE_SIZE * sizeof(UfsSqEntry));
    ufs_wreg(ufs, ufs->sqdao[TEST_QID] + 0x4, sqtp);
}

static int ufs_mcq_send_nop_out_async(QUfs *ufs)
{
    int cmd_desc_slot = alloc_cmd_desc_slot(ufs);
    uint64_t req_upiu_addr =
        ufs->cmd_desc_addr + cmd_desc_slot * UTP_COMMAND_DESCRIPTOR_SIZE;

    /* Build up request upiu */
    UtpUpiuReq req_upiu = { 0 };
    req_upiu.header.trans_type = UFS_UPIU_TRANSACTION_NOP_OUT;
    req_upiu.header.task_tag = cmd_desc_slot;
    qtest_memwrite(ufs->dev.bus->qts, req_upiu_addr, &req_upiu,
                   sizeof(req_upiu));

    /* Build up utp transfer request descriptor */
    UtpTransferReqDesc utrd =
        ufs_build_req_utrd(req_upiu_addr, UFS_UTP_NO_DATA_TRANSFER, 0);

    /* Send Transfer Request */
    __ufs_send_transfer_request_mcq_async(ufs, 0, &utrd);

    return cmd_desc_slot;
}

static int ufs_mcq_poll_cq(QUfs *ufs, UfsCqEntry *cqe, uint16_t n_cqe)
{
    uint32_t cqhp, cqtp;
    uint64_t cqe_addr;
    int ix = 0;

    cqhp = ufs_rreg(ufs, ufs->cqdao[TEST_QID]);
    cqtp = ufs_rreg(ufs, ufs->cqdao[TEST_QID] + 0x4);

    while (cqhp != cqtp && ix < n_cqe) {
        /* read completion entry */
        cqe_addr = ufs->cqlba[TEST_QID] + cqhp;
        qtest_memread(ufs->dev.bus->qts, cqe_addr, &cqe[ix], sizeof(cqe[ix]));

        /* advance completion queue head pointer */
        cqhp = (cqhp + sizeof(UfsCqEntry)) % (QUEUE_SIZE * sizeof(UfsCqEntry));
        ix++;
    }

    ufs_wreg(ufs, ufs->cqdao[TEST_QID], cqhp);

    return ix;
}

static enum UtpOcsCodes ufs_send_query(QUfs *ufs, uint8_t query_function,
                                       uint8_t query_opcode, uint8_t idn,
                                       uint8_t index, uint8_t selector,
                                       uint32_t attr_value, UtpUpiuRsp *rsp_out)
{
    int cmd_desc_slot = alloc_cmd_desc_slot(ufs);
    uint64_t req_upiu_addr =
        ufs->cmd_desc_addr + cmd_desc_slot * UTP_COMMAND_DESCRIPTOR_SIZE;
    uint64_t rsp_upiu_addr = req_upiu_addr + UTP_RESPONSE_UPIU_OFFSET;

    /* Build up request upiu */
    UtpUpiuReq req_upiu = { 0 };
    req_upiu.header.trans_type = UFS_UPIU_TRANSACTION_QUERY_REQ;
    req_upiu.header.query_func = query_function;
    req_upiu.header.task_tag = cmd_desc_slot;
    /*
     * QEMU UFS does not currently support Write descriptor,
     * so the value of data_segment_length is always 0.
     */
    req_upiu.header.data_segment_length = 0;
    req_upiu.qr.opcode = query_opcode;
    req_upiu.qr.idn = idn;
    req_upiu.qr.index = index;
    req_upiu.qr.selector = selector;
    req_upiu.qr.value = cpu_to_be32(attr_value);
    req_upiu.qr.length = UFS_QUERY_DESC_MAX_SIZE;
    qtest_memwrite(ufs->dev.bus->qts, req_upiu_addr, &req_upiu,
                   sizeof(req_upiu));

    /* Build up utp transfer request descriptor */
    UtpTransferReqDesc utrd =
        ufs_build_req_utrd(req_upiu_addr, UFS_UTP_NO_DATA_TRANSFER, 0);

    /* Send Transfer Request */
    enum UtpOcsCodes ret = ufs_send_transfer_request_sync(ufs, 0, &utrd);

    qtest_memread(ufs->dev.bus->qts, rsp_upiu_addr, rsp_out, sizeof(*rsp_out));
    release_cmd_desc_slot(ufs, cmd_desc_slot);
    return ret;
}

static enum UtpOcsCodes
ufs_send_scsi_command(QUfs *ufs, uint8_t lun, const uint8_t *cdb,
                      const uint8_t *data_in, size_t data_in_len,
                      uint8_t *data_out, size_t data_out_len,
                      UtpUpiuRsp *rsp_out)

{
    /* Build up PRDT */
    UfshcdSgEntry entries[MAX_PRD_ENTRY_COUNT] = {
        0,
    };
    uint8_t flags;
    uint16_t prd_table_length, i;
    uint32_t data_direction, data_len;
    int cmd_desc_slot = alloc_cmd_desc_slot(ufs);
    uint64_t req_upiu_addr =
        ufs->cmd_desc_addr + cmd_desc_slot * UTP_COMMAND_DESCRIPTOR_SIZE;
    uint64_t prdt_addr = req_upiu_addr + UTP_PRDT_UPIU_OFFSET;

    g_assert_true(data_in_len < MAX_PRD_ENTRY_COUNT * PRD_ENTRY_DATA_SIZE);
    g_assert_true(data_out_len < MAX_PRD_ENTRY_COUNT * PRD_ENTRY_DATA_SIZE);
    if (data_in_len > 0) {
        g_assert_nonnull(data_in);
        data_direction = UFS_UTP_HOST_TO_DEVICE;
        data_len = data_in_len;
        flags = UFS_UPIU_CMD_FLAGS_WRITE;
    } else if (data_out_len > 0) {
        g_assert_nonnull(data_out);
        data_direction = UFS_UTP_DEVICE_TO_HOST;
        data_len = data_out_len;
        flags = UFS_UPIU_CMD_FLAGS_READ;
    } else {
        data_direction = UFS_UTP_NO_DATA_TRANSFER;
        data_len = 0;
        flags = UFS_UPIU_CMD_FLAGS_NONE;
    }
    prd_table_length = DIV_ROUND_UP(data_len, PRD_ENTRY_DATA_SIZE);

    qtest_memset(ufs->dev.bus->qts, ufs->data_buffer_addr, 0,
                 MAX_PRD_ENTRY_COUNT * PRD_ENTRY_DATA_SIZE);
    if (data_in_len) {
        qtest_memwrite(ufs->dev.bus->qts, ufs->data_buffer_addr, data_in,
                       data_in_len);
    }

    for (i = 0; i < prd_table_length; i++) {
        entries[i].addr =
            cpu_to_le64(ufs->data_buffer_addr + i * sizeof(UfshcdSgEntry));
        if (i + 1 != prd_table_length) {
            entries[i].size = cpu_to_le32(PRD_ENTRY_DATA_SIZE - 1);
        } else {
            entries[i].size = cpu_to_le32(
                data_len - (PRD_ENTRY_DATA_SIZE * (prd_table_length - 1)) - 1);
        }
    }
    qtest_memwrite(ufs->dev.bus->qts, prdt_addr, entries,
                   prd_table_length * sizeof(UfshcdSgEntry));

    uint64_t rsp_upiu_addr = req_upiu_addr + UTP_RESPONSE_UPIU_OFFSET;

    /* Build up request upiu */
    UtpUpiuReq req_upiu = { 0 };
    req_upiu.header.trans_type = UFS_UPIU_TRANSACTION_COMMAND;
    req_upiu.header.flags = flags;
    req_upiu.header.lun = lun;
    req_upiu.header.task_tag = cmd_desc_slot;
    req_upiu.sc.exp_data_transfer_len = cpu_to_be32(data_len);
    memcpy(req_upiu.sc.cdb, cdb, UFS_CDB_SIZE);
    qtest_memwrite(ufs->dev.bus->qts, req_upiu_addr, &req_upiu,
                   sizeof(req_upiu));

    /* Build up utp transfer request descriptor */
    UtpTransferReqDesc utrd =
        ufs_build_req_utrd(req_upiu_addr, data_direction, prd_table_length);

    /* Send Transfer Request */
    enum UtpOcsCodes ret = ufs_send_transfer_request_sync(ufs, lun, &utrd);

    qtest_memread(ufs->dev.bus->qts, rsp_upiu_addr, rsp_out, sizeof(*rsp_out));
    if (data_out_len) {
        qtest_memread(ufs->dev.bus->qts, ufs->data_buffer_addr, data_out,
                      data_out_len);
    }
    release_cmd_desc_slot(ufs, cmd_desc_slot);
    return ret;
}

/**
 * Initialize Ufs host controller and logical unit.
 * After running this function, you can make a transfer request to the UFS.
 */
static void ufs_init(QUfs *ufs, QGuestAllocator *alloc)
{
    uint64_t end_time;
    uint32_t nutrs;
    uint32_t hcs, is, ucmdarg2, cap;
    uint32_t hce = 0, ie = 0;
    enum UtpOcsCodes ocs;
    UtpUpiuRsp rsp_upiu;

    ufs->bar = qpci_iomap(&ufs->dev, 0, NULL);
    qpci_device_enable(&ufs->dev);

    /* Start host controller initialization */
    hce = FIELD_DP32(hce, HCE, HCE, 1);
    ufs_wreg(ufs, A_HCE, hce);

    /* Wait for device to reset */
    end_time = g_get_monotonic_time() + TIMEOUT_SECONDS * G_TIME_SPAN_SECOND;
    do {
        qtest_clock_step(ufs->dev.bus->qts, 100);
        hce = FIELD_EX32(ufs_rreg(ufs, A_HCE), HCE, HCE);
    } while (hce == 0 && g_get_monotonic_time() < end_time);
    g_assert_cmpuint(hce, ==, 1);

    /* Enable interrupt */
    ie = FIELD_DP32(ie, IE, UCCE, 1);
    ie = FIELD_DP32(ie, IE, UHESE, 1);
    ie = FIELD_DP32(ie, IE, UHXSE, 1);
    ie = FIELD_DP32(ie, IE, UPMSE, 1);
    ufs_wreg(ufs, A_IE, ie);

    /* Send DME_LINK_STARTUP uic command */
    hcs = ufs_rreg(ufs, A_HCS);
    g_assert_true(FIELD_EX32(hcs, HCS, UCRDY));

    ufs_wreg(ufs, A_UCMDARG1, 0);
    ufs_wreg(ufs, A_UCMDARG2, 0);
    ufs_wreg(ufs, A_UCMDARG3, 0);
    ufs_wreg(ufs, A_UICCMD, UFS_UIC_CMD_DME_LINK_STARTUP);

    is = ufs_rreg(ufs, A_IS);
    g_assert_true(FIELD_EX32(is, IS, UCCS));
    ufs_wreg(ufs, A_IS, FIELD_DP32(0, IS, UCCS, 1));

    ucmdarg2 = ufs_rreg(ufs, A_UCMDARG2);
    g_assert_cmpuint(ucmdarg2, ==, 0);
    is = ufs_rreg(ufs, A_IS);
    g_assert_cmpuint(is, ==, 0);
    hcs = ufs_rreg(ufs, A_HCS);
    g_assert_true(FIELD_EX32(hcs, HCS, DP));
    g_assert_true(FIELD_EX32(hcs, HCS, UTRLRDY));
    g_assert_true(FIELD_EX32(hcs, HCS, UCRDY));

    /* Check MCQ support */
    cap = ufs_rreg(ufs, A_CAP);
    ufs->support_mcq = FIELD_EX32(cap, CAP, MCQS);

    /* Enable all interrupt functions */
    ie = FIELD_DP32(ie, IE, UTRCE, 1);
    ie = FIELD_DP32(ie, IE, UEE, 1);
    ie = FIELD_DP32(ie, IE, UPMSE, 1);
    ie = FIELD_DP32(ie, IE, UHXSE, 1);
    ie = FIELD_DP32(ie, IE, UHESE, 1);
    ie = FIELD_DP32(ie, IE, UTMRCE, 1);
    ie = FIELD_DP32(ie, IE, UCCE, 1);
    ie = FIELD_DP32(ie, IE, DFEE, 1);
    ie = FIELD_DP32(ie, IE, HCFEE, 1);
    ie = FIELD_DP32(ie, IE, SBFEE, 1);
    ie = FIELD_DP32(ie, IE, CEFEE, 1);
    if (ufs->support_mcq) {
        ie = FIELD_DP32(ie, IE, CQEE, 1);
    }
    ufs_wreg(ufs, A_IE, ie);
    ufs_wreg(ufs, A_UTRIACR, 0);

    /* Enable transfer request */
    bitmap_zero(ufs->cmd_desc_bitmap, UFS_MAX_CMD_DESC);
    ufs->cmd_desc_addr =
        guest_alloc(alloc, UFS_MAX_CMD_DESC * UTP_COMMAND_DESCRIPTOR_SIZE);
    ufs->data_buffer_addr =
        guest_alloc(alloc, MAX_PRD_ENTRY_COUNT * PRD_ENTRY_DATA_SIZE);

    if (ufs->support_mcq) {
        uint32_t mcqcap, qid, qcfgptr, mcq_reg_offset;
        uint32_t cqattr = 0, sqattr = 0;

        mcqcap = ufs_rreg(ufs, A_MCQCAP);
        qcfgptr = FIELD_EX32(mcqcap, MCQCAP, QCFGPTR);
        ufs->maxq = FIELD_EX32(mcqcap, MCQCAP, MAXQ) + 1;
        for (qid = 0; qid < ufs->maxq; ++qid) {
            ufs->sqlba[qid] =
                guest_alloc(alloc, QUEUE_SIZE * sizeof(UtpTransferReqDesc));
            ufs->cqlba[qid] =
                guest_alloc(alloc, QUEUE_SIZE * sizeof(UtpTransferReqDesc));
            mcq_reg_offset = qcfgptr * 0x200 + qid * 0x40;

            ufs_wreg(ufs, mcq_reg_offset + A_SQLBA,
                     ufs->sqlba[qid] & 0xffffffff);
            ufs_wreg(ufs, mcq_reg_offset + A_SQUBA, ufs->sqlba[qid] >> 32);
            ufs_wreg(ufs, mcq_reg_offset + A_CQLBA,
                     ufs->cqlba[qid] & 0xffffffff);
            ufs_wreg(ufs, mcq_reg_offset + A_CQUBA, ufs->cqlba[qid] >> 32);

            /* Enable Completion Queue */
            cqattr = FIELD_DP32(cqattr, CQATTR, CQEN, 1);
            cqattr = FIELD_DP32(cqattr, CQATTR, SIZE,
                                QUEUE_SIZE * sizeof(UtpTransferReqDesc) /
                                    DWORD_BYTE);
            ufs_wreg(ufs, mcq_reg_offset + A_CQATTR, cqattr);

            /* Enable Submission Queue */
            sqattr = FIELD_DP32(sqattr, SQATTR, SQEN, 1);
            sqattr = FIELD_DP32(sqattr, SQATTR, SIZE,
                                QUEUE_SIZE * sizeof(UtpTransferReqDesc) /
                                    DWORD_BYTE);
            sqattr = FIELD_DP32(sqattr, SQATTR, CQID, qid);
            ufs_wreg(ufs, mcq_reg_offset + A_SQATTR, sqattr);

            /* Cache head & tail pointer */
            ufs->sqdao[qid] = ufs_rreg(ufs, mcq_reg_offset + A_SQDAO);
            ufs->cqdao[qid] = ufs_rreg(ufs, mcq_reg_offset + A_CQDAO);
        }
    } else {
        nutrs = FIELD_EX32(cap, CAP, NUTRS) + 1;
        ufs->utrlba = guest_alloc(alloc, nutrs * sizeof(UtpTransferReqDesc));

        ufs_wreg(ufs, A_UTRLBA, ufs->utrlba & 0xffffffff);
        ufs_wreg(ufs, A_UTRLBAU, ufs->utrlba >> 32);
        ufs_wreg(ufs, A_UTRLRSR, 1);
    }

    /* Send nop out to test transfer request */
    ocs = ufs_send_nop_out(ufs, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);

    /* Set fDeviceInit flag via query request */
    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_WRITE_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_SET_FLAG,
                         UFS_QUERY_FLAG_IDN_FDEVICEINIT, 0, 0, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);

    /* Wait for device to reset */
    end_time = g_get_monotonic_time() + TIMEOUT_SECONDS * G_TIME_SPAN_SECOND;
    do {
        qtest_clock_step(ufs->dev.bus->qts, 100);
        ocs =
            ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                           UFS_UPIU_QUERY_OPCODE_READ_FLAG,
                           UFS_QUERY_FLAG_IDN_FDEVICEINIT, 0, 0, 0, &rsp_upiu);
        g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
        g_assert_cmpuint(rsp_upiu.header.response, ==,
                         UFS_COMMAND_RESULT_SUCCESS);
    } while (be32_to_cpu(rsp_upiu.qr.value) != 0 &&
             g_get_monotonic_time() < end_time);
    g_assert_cmpuint(be32_to_cpu(rsp_upiu.qr.value), ==, 0);

    ufs->enabled = true;
}

static void ufs_exit(QUfs *ufs, QGuestAllocator *alloc)
{
    if (ufs->enabled) {
        if (ufs->support_mcq) {
            for (uint32_t qid = 0; qid < ufs->maxq; ++qid) {
                guest_free(alloc, ufs->sqlba[qid]);
                guest_free(alloc, ufs->cqlba[qid]);
            }
        } else {
            guest_free(alloc, ufs->utrlba);
        }

        guest_free(alloc, ufs->cmd_desc_addr);
        guest_free(alloc, ufs->data_buffer_addr);
    }

    qpci_iounmap(&ufs->dev, ufs->bar);
}

static void *ufs_get_driver(void *obj, const char *interface)
{
    QUfs *ufs = obj;

    if (!g_strcmp0(interface, "pci-device")) {
        return &ufs->dev;
    }

    fprintf(stderr, "%s not present in ufs\n", interface);
    g_assert_not_reached();
}

static void *ufs_create(void *pci_bus, QGuestAllocator *alloc, void *addr)
{
    QUfs *ufs = g_new0(QUfs, 1);
    QPCIBus *bus = pci_bus;

    qpci_device_init(&ufs->dev, bus, addr);
    ufs->obj.get_driver = ufs_get_driver;

    return &ufs->obj;
}

static void ufstest_reg_read(void *obj, void *data, QGuestAllocator *alloc)
{
    QUfs *ufs = obj;
    uint32_t cap;

    ufs->bar = qpci_iomap(&ufs->dev, 0, NULL);
    qpci_device_enable(&ufs->dev);

    cap = ufs_rreg(ufs, A_CAP);
    g_assert_cmpuint(FIELD_EX32(cap, CAP, NUTRS), ==, 31);
    g_assert_cmpuint(FIELD_EX32(cap, CAP, NUTMRS), ==, 7);
    g_assert_cmpuint(FIELD_EX32(cap, CAP, 64AS), ==, 1);

    qpci_iounmap(&ufs->dev, ufs->bar);
}

static void ufstest_acpi_eject(void *obj, void *data, QGuestAllocator *alloc)
{
    QUfs *ufs = obj;
    QTestState *qts = ufs->dev.bus->qts;

    qtest_outl(qts, ACPI_PCIHP_ADDR + PCI_EJ_BASE, 1 << 4);
    qtest_qmp_assert_success(qts, "{ 'execute': 'query-status' }");
    g_usleep(3 * G_USEC_PER_SEC);
    qtest_qmp_assert_success(qts, "{ 'execute': 'query-status' }");
}

static void ufstest_init(void *obj, void *data, QGuestAllocator *alloc)
{
    QUfs *ufs = obj;

    uint8_t buf[4096] = { 0 };
    const uint8_t report_luns_cdb[UFS_CDB_SIZE] = {
        /* allocation length 4096 */
        REPORT_LUNS, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00,        0x00, 0x10, 0x00, 0x00, 0x00
    };
    const uint8_t test_unit_ready_cdb[UFS_CDB_SIZE] = {
        TEST_UNIT_READY,
    };
    const uint8_t request_sense_cdb[UFS_CDB_SIZE] = {
        REQUEST_SENSE,
    };
    enum UtpOcsCodes ocs;
    UtpUpiuRsp rsp_upiu;

    ufs_init(ufs, alloc);

    /* Check REPORT_LUNS */
    ocs = ufs_send_scsi_command(ufs, 0, report_luns_cdb, NULL, 0, buf,
                                sizeof(buf), &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.scsi_status, ==, GOOD);
    /* LUN LIST LENGTH should be 8, in big endian */
    g_assert_cmpuint(buf[3], ==, 8);
    /* There is one logical unit whose lun is 0 */
    g_assert_cmpuint(buf[9], ==, 0);

    /* Clear Unit Attention */
    ocs = ufs_send_scsi_command(ufs, 0, request_sense_cdb, NULL, 0, buf,
                                sizeof(buf), &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.scsi_status, ==, CHECK_CONDITION);

    /* Check TEST_UNIT_READY */
    ocs = ufs_send_scsi_command(ufs, 0, test_unit_ready_cdb, NULL, 0, NULL, 0,
                                &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.scsi_status, ==, GOOD);

    ufs_exit(ufs, alloc);
}

static void ufstest_read_write(void *obj, void *data, QGuestAllocator *alloc)
{
    QUfs *ufs = obj;
    uint8_t read_buf[4096] = { 0 };
    uint8_t write_buf[4096] = { 0 };
    const uint8_t read_capacity_cdb[UFS_CDB_SIZE] = {
        /* allocation length 4096 */
        SERVICE_ACTION_IN_16,
        SAI_READ_CAPACITY_16,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x10,
        0x00,
        0x00,
        0x00
    };
    const uint8_t request_sense_cdb[UFS_CDB_SIZE] = {
        REQUEST_SENSE,
    };
    const uint8_t read_cdb[UFS_CDB_SIZE] = {
        /* READ(10) to LBA 0, transfer length 1 */
        READ_10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00
    };
    const uint8_t write_cdb[UFS_CDB_SIZE] = {
        /* WRITE(10) to LBA 0, transfer length 1 */
        WRITE_10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00
    };
    uint32_t block_size;
    enum UtpOcsCodes ocs;
    UtpUpiuRsp rsp_upiu;
    const int test_lun = 1;

    ufs_init(ufs, alloc);

    /* Clear Unit Attention */
    ocs = ufs_send_scsi_command(ufs, test_lun, request_sense_cdb, NULL, 0,
                                read_buf, sizeof(read_buf), &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.scsi_status, ==, CHECK_CONDITION);

    /* Read capacity */
    ocs = ufs_send_scsi_command(ufs, test_lun, read_capacity_cdb, NULL, 0,
                                read_buf, sizeof(read_buf), &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.scsi_status, ==,
                     UFS_COMMAND_RESULT_SUCCESS);
    block_size = ldl_be_p(&read_buf[8]);
    g_assert_cmpuint(block_size, ==, 4096);

    /* Write data */
    memset(write_buf, 0xab, block_size);
    ocs = ufs_send_scsi_command(ufs, test_lun, write_cdb, write_buf, block_size,
                                NULL, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.scsi_status, ==,
                     UFS_COMMAND_RESULT_SUCCESS);

    /* Read data and verify */
    ocs = ufs_send_scsi_command(ufs, test_lun, read_cdb, NULL, 0, read_buf,
                                block_size, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.scsi_status, ==,
                     UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpint(memcmp(read_buf, write_buf, block_size), ==, 0);

    ufs_exit(ufs, alloc);
}

static void ufstest_no_prdt(void *obj, void *data, QGuestAllocator *alloc)
{
    QUfs *ufs = obj;
    uint8_t buf[4096];
    const uint8_t inquiry_cdb[UFS_CDB_SIZE] = {
        /* INQUIRY, allocation length 36 */
        INQUIRY, 0x00, 0x00, 0x00, 0x24
    };
    const uint8_t write_cdb[UFS_CDB_SIZE] = {
        /* WRITE(10) to LBA 0, transfer length 1 */
        WRITE_10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00
    };
    enum UtpOcsCodes ocs;
    UtpUpiuRsp rsp_upiu;
    const int test_lun = 1;

    ufs_init(ufs, alloc);

    /* A pending unit attention is reported even without a PRDT */
    ocs = ufs_send_scsi_command(ufs, test_lun, write_cdb, NULL, 0, NULL, 0,
                                &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.scsi_status, ==, CHECK_CONDITION);
    g_assert_cmpuint(rsp_upiu.sr.sense_data[2] & 0xf, ==, UNIT_ATTENTION);

    /* Commands that move data but come without a PRDT fail */
    ocs = ufs_send_scsi_command(ufs, test_lun, inquiry_cdb, NULL, 0, NULL, 0,
                                &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_MISMATCH_DATA_BUF_SIZE);

    ocs = ufs_send_scsi_command(ufs, test_lun, write_cdb, NULL, 0, NULL, 0,
                                &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_MISMATCH_DATA_BUF_SIZE);

    /* The logical unit keeps working */
    ocs = ufs_send_scsi_command(ufs, test_lun, inquiry_cdb, NULL, 0, buf, 0x24,
                                &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.scsi_status, ==, GOOD);
    g_assert_cmpuint(buf[0] & 0x1f, ==, TYPE_DISK);

    ufs_exit(ufs, alloc);
}

static void ufstest_sync_cache(void *obj, void *data, QGuestAllocator *alloc)
{
    QUfs *ufs = obj;
    uint8_t buf[4096];
    const uint8_t request_sense_cdb[UFS_CDB_SIZE] = {
        REQUEST_SENSE,
    };
    const uint8_t sync_cache_10_cdb[UFS_CDB_SIZE] = {
        SYNCHRONIZE_CACHE,
    };
    const uint8_t sync_cache_16_cdb[UFS_CDB_SIZE] = {
        SYNCHRONIZE_CACHE_16,
    };
    const uint8_t sync_cache_16_immed_cdb[UFS_CDB_SIZE] = {
        SYNCHRONIZE_CACHE_16, 0x02 /* IMMED */
    };
    enum UtpOcsCodes ocs;
    UtpUpiuRsp rsp_upiu;
    const int test_lun = 1;

    ufs_init(ufs, alloc);

    /* Clear Unit Attention */
    ocs = ufs_send_scsi_command(ufs, test_lun, request_sense_cdb, NULL, 0,
                                buf, sizeof(buf), &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);

    ocs = ufs_send_scsi_command(ufs, test_lun, sync_cache_10_cdb, NULL, 0,
                                NULL, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.scsi_status, ==, GOOD);

    ocs = ufs_send_scsi_command(ufs, test_lun, sync_cache_16_cdb, NULL, 0,
                                NULL, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.scsi_status, ==, GOOD);

    ocs = ufs_send_scsi_command(ufs, test_lun, sync_cache_16_immed_cdb, NULL,
                                0, NULL, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.scsi_status, ==, GOOD);

    ufs_exit(ufs, alloc);
}

static void ufstest_long_prdt(void *obj, void *data, QGuestAllocator *alloc)
{
    QUfs *ufs = obj;
    const uint8_t request_sense_cdb[UFS_CDB_SIZE] = {
        REQUEST_SENSE,
    };
    /* One block at LBA 0 and at LBA 1 */
    const uint8_t write_lba0_cdb[UFS_CDB_SIZE] = {
        WRITE_10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
    };
    const uint8_t read_lba0_cdb[UFS_CDB_SIZE] = {
        READ_10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
    };
    const uint8_t read_lba1_cdb[UFS_CDB_SIZE] = {
        READ_10, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x00,
    };
    /* Allocation length 36 */
    const uint8_t inquiry_cdb[UFS_CDB_SIZE] = {
        INQUIRY, 0x00, 0x00, 0x00, 0x24, 0x00,
    };
    uint8_t buf[2 * 4096];
    uint8_t zero_buf[4096] = { 0 };
    uint8_t write_buf[2 * 4096];
    UtpUpiuRsp rsp_upiu, write_rsp_upiu;
    enum UtpOcsCodes ocs;

    ufs_init(ufs, alloc);
    ocs = ufs_send_scsi_command(ufs, 1, request_sense_cdb, NULL, 0, buf, 4096,
                                &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);

    /* A one-block WRITE with a two-block PRDT writes one block */
    memset(write_buf, 0xa5, sizeof(write_buf));
    ocs = ufs_send_scsi_command(ufs, 1, write_lba0_cdb, write_buf,
                                sizeof(write_buf), NULL, 0, &write_rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(write_rsp_upiu.header.scsi_status, ==, GOOD);

    ocs = ufs_send_scsi_command(ufs, 1, read_lba1_cdb, NULL, 0, buf, 4096,
                                &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.scsi_status, ==, GOOD);
    g_assert_cmpmem(buf, 4096, zero_buf, sizeof(zero_buf));

    g_assert_cmpuint(write_rsp_upiu.header.flags, ==, UFS_UPIU_FLAG_UNDERFLOW);
    g_assert_cmpuint(be32_to_cpu(write_rsp_upiu.sr.residual_transfer_count),
                     ==, 4096);

    /* A one-block READ with a two-block PRDT reads one block */
    ocs = ufs_send_scsi_command(ufs, 1, read_lba0_cdb, NULL, 0, buf,
                                sizeof(buf), &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.scsi_status, ==, GOOD);
    g_assert_cmpuint(rsp_upiu.header.flags, ==, UFS_UPIU_FLAG_UNDERFLOW);
    g_assert_cmpuint(be32_to_cpu(rsp_upiu.sr.residual_transfer_count), ==,
                     4096);
    g_assert_cmpmem(buf, 4096, write_buf, 4096);

    /* A 36-byte INQUIRY with a 4 KiB PRDT reports the rest as a residual */
    ocs = ufs_send_scsi_command(ufs, 1, inquiry_cdb, NULL, 0, buf, 4096,
                                &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.scsi_status, ==, GOOD);
    g_assert_cmpuint(rsp_upiu.header.flags, ==, UFS_UPIU_FLAG_UNDERFLOW);
    g_assert_cmpuint(be32_to_cpu(rsp_upiu.sr.residual_transfer_count), ==,
                     4096 - 36);

    ufs_exit(ufs, alloc);
}

static void ufstest_short_prdt(void *obj, void *data, QGuestAllocator *alloc)
{
    QUfs *ufs = obj;
    const uint8_t request_sense_cdb[UFS_CDB_SIZE] = {
        REQUEST_SENSE,
    };
    /* Two blocks at LBA 2, and one */
    const uint8_t write_2_cdb[UFS_CDB_SIZE] = {
        WRITE_10, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x02, 0x00,
    };
    const uint8_t read_2_cdb[UFS_CDB_SIZE] = {
        READ_10, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x02, 0x00,
    };
    const uint8_t write_1_cdb[UFS_CDB_SIZE] = {
        WRITE_10, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, 0x00,
    };
    const uint8_t read_1_cdb[UFS_CDB_SIZE] = {
        READ_10, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, 0x00,
    };
    /* Allocation length 36 */
    const uint8_t inquiry_cdb[UFS_CDB_SIZE] = {
        INQUIRY, 0x00, 0x00, 0x00, 0x24, 0x00,
    };
    uint8_t buf[4096];
    uint8_t zero_buf[4096] = { 0 };
    uint8_t write_buf[4096];
    UtpUpiuRsp rsp_upiu;
    enum UtpOcsCodes ocs;

    ufs_init(ufs, alloc);
    ocs = ufs_send_scsi_command(ufs, 1, request_sense_cdb, NULL, 0, buf, 4096,
                                &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    memset(write_buf, 0x5a, sizeof(write_buf));

    /* A PRDT that describes less than the command moves fails */
    ocs = ufs_send_scsi_command(ufs, 1, write_2_cdb, write_buf,
                                sizeof(write_buf), NULL, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_MISMATCH_DATA_BUF_SIZE);
    ocs = ufs_send_scsi_command(ufs, 1, read_2_cdb, NULL, 0, buf, sizeof(buf),
                                &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_MISMATCH_DATA_BUF_SIZE);
    ocs = ufs_send_scsi_command(ufs, 1, inquiry_cdb, NULL, 0, buf, 16,
                                &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_MISMATCH_DATA_BUF_SIZE);

    /* The failed WRITE wrote nothing */
    ocs = ufs_send_scsi_command(ufs, 1, read_1_cdb, NULL, 0, buf, sizeof(buf),
                                &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.scsi_status, ==, GOOD);
    g_assert_cmpmem(buf, sizeof(buf), zero_buf, sizeof(zero_buf));

    /* A matching PRDT still works */
    ocs = ufs_send_scsi_command(ufs, 1, write_1_cdb, write_buf,
                                sizeof(write_buf), NULL, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.scsi_status, ==, GOOD);
    ocs = ufs_send_scsi_command(ufs, 1, read_1_cdb, NULL, 0, buf, sizeof(buf),
                                &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpmem(buf, sizeof(buf), write_buf, sizeof(write_buf));

    ufs_exit(ufs, alloc);
}

static void ufstest_mcq_cq_wraparound(void *obj, void *data,
                                      QGuestAllocator *alloc)
{
    QUfs *ufs = obj;
    UfsCqEntry cqe[QUEUE_SIZE];
    const int num_requests = QUEUE_SIZE;
    int i, completed = 0;

    ufs_init(ufs, alloc);

    /* Ensure MCQ is supported */
    g_assert_true(ufs->support_mcq);

    for (i = 0; i < num_requests; ++i) {
        ufs_mcq_send_nop_out_async(ufs);
    }

    while (completed != num_requests) {
        int n_cqe = ufs_mcq_poll_cq(ufs, cqe, ARRAY_SIZE(cqe));
        if (!n_cqe) {
            break;
        }

        for (i = 0; i < n_cqe; ++i) {
            uint64_t ucdba;
            uint64_t rsp_upiu_addr;
            UtpUpiuRsp rsp_upiu;
            uint8_t tag;

            g_assert_cmpuint(cqe[i].status, ==, UFS_OCS_SUCCESS);

            ucdba = le64_to_cpu(cqe[i].utp_addr) & MAKE_64BIT_MASK(7, 57);
            rsp_upiu_addr = ucdba + UTP_RESPONSE_UPIU_OFFSET;
            qtest_memread(ufs->dev.bus->qts, rsp_upiu_addr, &rsp_upiu,
                          sizeof(rsp_upiu));

            tag = rsp_upiu.header.task_tag;
            release_cmd_desc_slot(ufs, tag);
        }

        completed += n_cqe;
    }

    g_assert_cmpint(completed, ==, num_requests);
    ufs_exit(ufs, alloc);
}

static void ufstest_query_flag_request(void *obj, void *data,
                                       QGuestAllocator *alloc)
{
    QUfs *ufs = obj;

    enum UtpOcsCodes ocs;
    UtpUpiuRsp rsp_upiu;
    ufs_init(ufs, alloc);

    /* Read read-only flag */
    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_FLAG,
                         UFS_QUERY_FLAG_IDN_FDEVICEINIT, 0, 0, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.opcode, ==, UFS_UPIU_QUERY_OPCODE_READ_FLAG);
    g_assert_cmpuint(rsp_upiu.qr.idn, ==, UFS_QUERY_FLAG_IDN_FDEVICEINIT);
    g_assert_cmpuint(rsp_upiu.qr.value, ==, be32_to_cpu(0));

    /* Flag Set, Clear, Toggle Test with fDeviceLifeSpanModeEn */
    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_FLAG,
                         UFS_QUERY_FLAG_IDN_LIFE_SPAN_MODE_ENABLE, 0, 0, 0,
                         &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.value, ==, be32_to_cpu(0));

    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_WRITE_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_SET_FLAG,
                         UFS_QUERY_FLAG_IDN_LIFE_SPAN_MODE_ENABLE, 0, 0, 0,
                         &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.value, ==, be32_to_cpu(1));

    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_WRITE_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_CLEAR_FLAG,
                         UFS_QUERY_FLAG_IDN_LIFE_SPAN_MODE_ENABLE, 0, 0, 0,
                         &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.value, ==, be32_to_cpu(0));

    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_WRITE_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_TOGGLE_FLAG,
                         UFS_QUERY_FLAG_IDN_LIFE_SPAN_MODE_ENABLE, 0, 0, 0,
                         &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.value, ==, be32_to_cpu(1));

    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_WRITE_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_TOGGLE_FLAG,
                         UFS_QUERY_FLAG_IDN_LIFE_SPAN_MODE_ENABLE, 0, 0, 0,
                         &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.value, ==, be32_to_cpu(0));

    /* Read Write-only Flag (Intended Failure) */
    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_FLAG,
                         UFS_QUERY_FLAG_IDN_PURGE_ENABLE, 0, 0, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_INVALID_CMD_TABLE_ATTR);
    g_assert_cmpuint(rsp_upiu.header.response, ==,
                     UFS_QUERY_RESULT_NOT_READABLE);

    /* Write Read-Only Flag (Intended Failure) */
    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_WRITE_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_SET_FLAG,
                         UFS_QUERY_FLAG_IDN_BUSY_RTC, 0, 0, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_INVALID_CMD_TABLE_ATTR);
    g_assert_cmpuint(rsp_upiu.header.response, ==,
                     UFS_QUERY_RESULT_NOT_WRITEABLE);

    ufs_exit(ufs, alloc);
}

static void ufstest_query_attr_request(void *obj, void *data,
                                       QGuestAllocator *alloc)
{
    QUfs *ufs = obj;

    enum UtpOcsCodes ocs;
    UtpUpiuRsp rsp_upiu;
    ufs_init(ufs, alloc);

    /* Read Readable Attributes*/
    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_ATTR,
                         UFS_QUERY_ATTR_IDN_BOOT_LU_EN, 0, 0, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.opcode, ==, UFS_UPIU_QUERY_OPCODE_READ_ATTR);
    g_assert_cmpuint(rsp_upiu.qr.idn, ==, UFS_QUERY_ATTR_IDN_BOOT_LU_EN);
    g_assert_cmpuint(rsp_upiu.qr.value, ==, cpu_to_be32(0x00));

    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_ATTR,
                         UFS_QUERY_ATTR_IDN_BKOPS_STATUS, 0, 0, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.value, ==, cpu_to_be32(0x00));

    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_ATTR,
                         UFS_QUERY_ATTR_IDN_CASE_ROUGH_TEMP, 0, 0, 0,
                         &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.value, ==, cpu_to_be32(0x00));

    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_ATTR,
                         UFS_QUERY_ATTR_IDN_HIGH_TEMP_BOUND, 0, 0, 0,
                         &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.value, ==, cpu_to_be32(160));

    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_ATTR,
                         UFS_QUERY_ATTR_IDN_LOW_TEMP_BOUND, 0, 0, 0,
                         &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.value, ==, cpu_to_be32(60));

    /* Write Writable Attributes & Read Again */
    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_WRITE_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_WRITE_ATTR,
                         UFS_QUERY_ATTR_IDN_ACTIVE_ICC_LVL, 0, 0, 0x03,
                         &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.value, ==, cpu_to_be32(0x03));

    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_WRITE_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_WRITE_ATTR,
                         UFS_QUERY_ATTR_IDN_EE_CONTROL, 0, 0, 0x07, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.value, ==, cpu_to_be32(0x07));

    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_ATTR,
                         UFS_QUERY_ATTR_IDN_ACTIVE_ICC_LVL, 0, 0, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.value, ==, cpu_to_be32(0x03));

    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_ATTR,
                         UFS_QUERY_ATTR_IDN_EE_CONTROL, 0, 0, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.value, ==, cpu_to_be32(0x07));

    /* Write Invalid Value (Intended Error) */
    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_WRITE_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_WRITE_ATTR,
                         UFS_QUERY_ATTR_IDN_ACTIVE_ICC_LVL, 0, 0, 0x10,
                         &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_INVALID_CMD_TABLE_ATTR);
    g_assert_cmpuint(rsp_upiu.header.response, ==,
                     UFS_QUERY_RESULT_INVALID_VALUE);

    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_ATTR,
                         UFS_QUERY_ATTR_IDN_ACTIVE_ICC_LVL, 0, 0, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.value, ==, cpu_to_be32(0x03));

    /* Read Write-Only Attribute (Intended Error) */
    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_ATTR,
                         UFS_QUERY_ATTR_IDN_SECONDS_PASSED, 0, 0, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_INVALID_CMD_TABLE_ATTR);
    g_assert_cmpuint(rsp_upiu.header.response, ==,
                     UFS_QUERY_RESULT_NOT_READABLE);

    /* Write Read-Only Attribute (Intended Error) */
    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_WRITE_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_WRITE_ATTR,
                         UFS_QUERY_ATTR_IDN_POWER_MODE, 0, 0, 0x01, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_INVALID_CMD_TABLE_ATTR);
    g_assert_cmpuint(rsp_upiu.header.response, ==,
                     UFS_QUERY_RESULT_NOT_WRITEABLE);

    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_ATTR,
                         UFS_QUERY_ATTR_IDN_POWER_MODE, 0, 0, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.value, ==, cpu_to_be32(0x00));

    /* Reset Written Attributes */
    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_WRITE_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_WRITE_ATTR,
                         UFS_QUERY_ATTR_IDN_ACTIVE_ICC_LVL, 0, 0, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.value, ==, cpu_to_be32(0x00));

    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_WRITE_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_WRITE_ATTR,
                         UFS_QUERY_ATTR_IDN_EE_CONTROL, 0, 0, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.value, ==, cpu_to_be32(0x00));

    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_ATTR,
                         UFS_QUERY_ATTR_IDN_ACTIVE_ICC_LVL, 0, 0, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.value, ==, cpu_to_be32(0x00));

    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_ATTR,
                         UFS_QUERY_ATTR_IDN_EE_CONTROL, 0, 0, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.value, ==, cpu_to_be32(0x00));

    ufs_exit(ufs, alloc);
}

static void ufstest_query_desc_request(void *obj, void *data,
                                       QGuestAllocator *alloc)
{
    QUfs *ufs = obj;

    enum UtpOcsCodes ocs;
    UtpUpiuRsp rsp_upiu;
    ufs_init(ufs, alloc);

    /* Write Descriptor is not supported yet */

    /* Read Device Descriptor */
    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_DESC,
                         UFS_QUERY_DESC_IDN_DEVICE, 0, 0, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.opcode, ==, UFS_UPIU_QUERY_OPCODE_READ_DESC);
    g_assert_cmpuint(rsp_upiu.qr.idn, ==, UFS_QUERY_DESC_IDN_DEVICE);
    g_assert_cmpuint(rsp_upiu.qr.data[0], ==, sizeof(DeviceDescriptor));
    g_assert_cmpuint(rsp_upiu.qr.data[1], ==, UFS_QUERY_DESC_IDN_DEVICE);

    /* Read Configuration Descriptor is not supported yet*/

    /* Read Unit Descriptor */
    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_DESC,
                         UFS_QUERY_DESC_IDN_UNIT, 0, 0, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.data[0], ==, sizeof(UnitDescriptor));
    g_assert_cmpuint(rsp_upiu.qr.data[1], ==, UFS_QUERY_DESC_IDN_UNIT);
    g_assert_cmpuint(rsp_upiu.qr.data[2], ==, 0);

    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_DESC,
                         UFS_QUERY_DESC_IDN_UNIT, 1, 0, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.data[0], ==, sizeof(UnitDescriptor));
    g_assert_cmpuint(rsp_upiu.qr.data[1], ==, UFS_QUERY_DESC_IDN_UNIT);
    g_assert_cmpuint(rsp_upiu.qr.data[2], ==, 1);

    ocs =
        ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                       UFS_UPIU_QUERY_OPCODE_READ_DESC, UFS_QUERY_DESC_IDN_UNIT,
                       UFS_UPIU_RPMB_WLUN, 0, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.data[0], ==, sizeof(RpmbUnitDescriptor));
    g_assert_cmpuint(rsp_upiu.qr.data[1], ==, UFS_QUERY_DESC_IDN_UNIT);
    g_assert_cmpuint(rsp_upiu.qr.data[2], ==, UFS_UPIU_RPMB_WLUN);

    /* Read Interconnect Descriptor */
    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_DESC,
                         UFS_QUERY_DESC_IDN_INTERCONNECT, 0, 0, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.data[0], ==, sizeof(InterconnectDescriptor));
    g_assert_cmpuint(rsp_upiu.qr.data[1], ==, UFS_QUERY_DESC_IDN_INTERCONNECT);

    /* Read String Descriptor */
    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_DESC,
                         UFS_QUERY_DESC_IDN_STRING, 0, 0, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.data[0], ==, 0x12);
    g_assert_cmpuint(rsp_upiu.qr.data[1], ==, UFS_QUERY_DESC_IDN_STRING);

    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_DESC,
                         UFS_QUERY_DESC_IDN_STRING, 1, 0, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.data[0], ==, 0x22);
    g_assert_cmpuint(rsp_upiu.qr.data[1], ==, UFS_QUERY_DESC_IDN_STRING);

    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_DESC,
                         UFS_QUERY_DESC_IDN_STRING, 4, 0, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.data[0], ==, 0x0a);
    g_assert_cmpuint(rsp_upiu.qr.data[1], ==, UFS_QUERY_DESC_IDN_STRING);

    /* Read Geometry Descriptor */
    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_DESC,
                         UFS_QUERY_DESC_IDN_GEOMETRY, 0, 0, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.data[0], ==, sizeof(GeometryDescriptor));
    g_assert_cmpuint(rsp_upiu.qr.data[1], ==, UFS_QUERY_DESC_IDN_GEOMETRY);

    /* Read Power Descriptor */
    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_DESC,
                         UFS_QUERY_DESC_IDN_POWER, 0, 0, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.data[0], ==,
                     sizeof(PowerParametersDescriptor));
    g_assert_cmpuint(rsp_upiu.qr.data[1], ==, UFS_QUERY_DESC_IDN_POWER);

    /* Read Health Descriptor */
    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_DESC,
                         UFS_QUERY_DESC_IDN_HEALTH, 0, 0, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.data[0], ==, sizeof(DeviceHealthDescriptor));
    g_assert_cmpuint(rsp_upiu.qr.data[1], ==, UFS_QUERY_DESC_IDN_HEALTH);

    /* Invalid Index (Intended Failure) */
    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_DESC,
                         UFS_QUERY_DESC_IDN_UNIT, 4, 0, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_INVALID_CMD_TABLE_ATTR);
    g_assert_cmpuint(rsp_upiu.header.response, ==,
                     UFS_QUERY_RESULT_INVALID_INDEX);

    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_DESC,
                         UFS_QUERY_DESC_IDN_STRING, 5, 0, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_INVALID_CMD_TABLE_ATTR);
    g_assert_cmpuint(rsp_upiu.header.response, ==,
                     UFS_QUERY_RESULT_INVALID_INDEX);

    /* Invalid Selector (Intended Failure) */
    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_DESC,
                         UFS_QUERY_DESC_IDN_DEVICE, 0, 1, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_INVALID_CMD_TABLE_ATTR);
    g_assert_cmpuint(rsp_upiu.header.response, ==,
                     UFS_QUERY_RESULT_INVALID_SELECTOR);

    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_DESC,
                         UFS_QUERY_DESC_IDN_STRING, 0, 1, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_INVALID_CMD_TABLE_ATTR);
    g_assert_cmpuint(rsp_upiu.header.response, ==,
                     UFS_QUERY_RESULT_INVALID_SELECTOR);

    ufs_exit(ufs, alloc);
}

static void ufstest_wb_init(void *obj, void *data, QGuestAllocator *alloc)
{
    QUfs *ufs = obj;
    enum UtpOcsCodes ocs;
    UtpUpiuRsp rsp_upiu;
    uint8_t *desc;
    uint32_t value;

    ufs_init(ufs, alloc);

    /* Read Device Descriptor */
    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_DESC,
                         UFS_QUERY_DESC_IDN_DEVICE, 0, 0, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.opcode, ==, UFS_UPIU_QUERY_OPCODE_READ_DESC);
    g_assert_cmpuint(rsp_upiu.qr.idn, ==, UFS_QUERY_DESC_IDN_DEVICE);
    g_assert_cmpuint(rsp_upiu.qr.data[0], ==, sizeof(DeviceDescriptor));
    g_assert_cmpuint(rsp_upiu.qr.data[1], ==, UFS_QUERY_DESC_IDN_DEVICE);

    /* Check Write Booster Supportability */
    desc = rsp_upiu.qr.data;

    value = lduw_be_p(desc + UFS_DEVICE_DESC_PARAM_EXT_WB_SUP);
    g_assert_cmpuint(value, ==, WB_RESIZE | WB_FIFO | WB_PINNED);

    value = ldl_be_p(desc + UFS_DEVICE_DESC_PARAM_EXT_UFS_FEATURE_SUP);
    g_assert_cmpuint(value & UFS_DEV_WB_SUPPORT, ==, UFS_DEV_WB_SUPPORT);

    /* Read Geometry Descriptor */
    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_DESC,
                         UFS_QUERY_DESC_IDN_GEOMETRY, 0, 0, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.data[0], ==, sizeof(GeometryDescriptor));
    g_assert_cmpuint(rsp_upiu.qr.data[1], ==, UFS_QUERY_DESC_IDN_GEOMETRY);

    /* Check Write Booster Configuration */
    desc = rsp_upiu.qr.data;

    value = ldl_be_p(desc + UFS_GEOMETRY_DESC_PARAM_WB_MAX_ALLOC_UNITS);
    g_assert_cmpuint(value, ==, 1024);

    value = desc[UFS_GEOMETRY_DESC_PARAM_WB_MAX_WB_LUNS];
    g_assert_cmpuint(value, ==, 1);

    value = desc[UFS_GEOMETRY_DESC_PARAM_WB_BUFF_CAP_ADJ];
    g_assert_cmpuint(value, ==, 3);

    value = desc[UFS_GEOMETRY_DESC_PARAM_WB_SUP_RED_TYPE];
    g_assert_cmpuint(value, ==, 1);

    value = desc[UFS_GEOMETRY_DESC_PARAM_WB_SUP_WB_TYPE];
    g_assert_cmpuint(value, ==, 1);

    ufs_exit(ufs, alloc);
}

static void ufstest_wb_read_write(void *obj, void *data, QGuestAllocator *alloc)
{
    QUfs *ufs = obj;
    uint8_t read_buf[4096] = { 0 };
    uint8_t write_buf[4096] = { 0 };
    const uint8_t read_capacity_cdb[UFS_CDB_SIZE] = {
        /* allocation length 4096 */
        SERVICE_ACTION_IN_16,
        SAI_READ_CAPACITY_16,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x10,
        0x00,
        0x00,
        0x00
    };
    const uint8_t request_sense_cdb[UFS_CDB_SIZE] = {
        REQUEST_SENSE,
    };
    const uint8_t write_cdb[UFS_CDB_SIZE] = {
        /* WRITE(10) to LBA 0, transfer length 1 */
        WRITE_10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00
    };
    uint32_t block_size;
    enum UtpOcsCodes ocs;
    UtpUpiuRsp rsp_upiu;
    const int test_lun = 1;
    uint64_t end_time;

    ufs_init(ufs, alloc);

    /* Clear Unit Attention */
    ocs = ufs_send_scsi_command(ufs, test_lun, request_sense_cdb, NULL, 0,
                                read_buf, sizeof(read_buf), &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.scsi_status, ==, CHECK_CONDITION);

    /* Read capacity */
    ocs = ufs_send_scsi_command(ufs, test_lun, read_capacity_cdb, NULL, 0,
                                read_buf, sizeof(read_buf), &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.scsi_status, ==,
                     UFS_COMMAND_RESULT_SUCCESS);
    block_size = ldl_be_p(&read_buf[8]);
    g_assert_cmpuint(block_size, ==, 4096);

    /* Check available buffer size */
    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_ATTR,
                         UFS_QUERY_ATTR_IDN_AVAIL_WB_BUFF_SIZE, 0, 0, 0,
                         &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.opcode, ==, UFS_UPIU_QUERY_OPCODE_READ_ATTR);
    g_assert_cmpuint(rsp_upiu.qr.idn, ==,
                     UFS_QUERY_ATTR_IDN_AVAIL_WB_BUFF_SIZE);
    g_assert_cmpuint(rsp_upiu.qr.value, ==, cpu_to_be32(0xA));

    /* Enable WB */
    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_WRITE_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_SET_FLAG,
                         UFS_QUERY_FLAG_IDN_WB_EN, 0, 0, 0, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp_upiu.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(rsp_upiu.qr.value, ==, be32_to_cpu(1));

    /* Write data */
    for (int i = 0; i < 256; i++) {
        memset(write_buf, 0xab, block_size);
        ocs = ufs_send_scsi_command(ufs, test_lun, write_cdb, write_buf,
                                    block_size, NULL, 0, &rsp_upiu);
        g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
        g_assert_cmpuint(rsp_upiu.header.scsi_status, ==,
                         UFS_COMMAND_RESULT_SUCCESS);
    }

    end_time = g_get_monotonic_time() + TIMEOUT_SECONDS * G_TIME_SPAN_SECOND;
    do {
        qtest_clock_step(ufs->dev.bus->qts, 100);

        /* Check available buffer size */
        ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                             UFS_UPIU_QUERY_OPCODE_READ_ATTR,
                             UFS_QUERY_ATTR_IDN_AVAIL_WB_BUFF_SIZE, 0, 0, 0,
                             &rsp_upiu);
        g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
        g_assert_cmpuint(rsp_upiu.header.response, ==,
                         UFS_COMMAND_RESULT_SUCCESS);
        g_assert_cmpuint(rsp_upiu.qr.opcode, ==,
                         UFS_UPIU_QUERY_OPCODE_READ_ATTR);
        g_assert_cmpuint(rsp_upiu.qr.idn, ==,
                         UFS_QUERY_ATTR_IDN_AVAIL_WB_BUFF_SIZE);
    } while (rsp_upiu.qr.value == cpu_to_be32(0xA) &&
             g_get_monotonic_time() < end_time);

    /* Check available buffer size */
    g_assert_cmpuint(rsp_upiu.qr.value, ==, cpu_to_be32(0x9));

    ufs_exit(ufs, alloc);
}

static void drive_destroy(void *path)
{
    unlink(path);
    g_free(path);
    qos_invalidate_command_line();
}

static char *drive_create(void)
{
    int fd, ret;
    char *t_path;

    /* Create a temporary raw image */
    fd = g_file_open_tmp("qtest-ufs.XXXXXX", &t_path, NULL);
    g_assert_cmpint(fd, >=, 0);
    ret = ftruncate(fd, TEST_IMAGE_SIZE);
    g_assert_cmpint(ret, ==, 0);
    close(fd);

    g_test_queue_destroy(drive_destroy, t_path);
    return t_path;
}

static void *ufs_blk_test_setup(GString *cmd_line, void *arg)
{
    char *tmp_path = drive_create();

    g_string_append_printf(cmd_line,
                           " -blockdev file,filename=%s,node-name=drv1 "
                           "-device ufs-lu,bus=ufs0,drive=drv1,lun=1 ",
                           tmp_path);

    return arg;
}

/*
 * Helper to read a single HID attribute value.
 * Returns the host-endian attribute value; asserts OCS success.
 */
static uint32_t ufs_hid_read_attr(QUfs *ufs, uint8_t idn)
{
    enum UtpOcsCodes ocs;
    UtpUpiuRsp rsp;

    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_READ_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_READ_ATTR, idn, 0, 0, 0, &rsp);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
    return be32_to_cpu(rsp.qr.value);
}

/*
 * Helper to write a single HID attribute value.
 * Asserts OCS success.
 */
static void ufs_hid_write_attr(QUfs *ufs, uint8_t idn, uint32_t value)
{
    enum UtpOcsCodes ocs;
    UtpUpiuRsp rsp;

    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_WRITE_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_WRITE_ATTR, idn, 0, 0, value,
                         &rsp);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(rsp.header.response, ==, UFS_COMMAND_RESULT_SUCCESS);
}

static uint32_t ufs_hid_wait_leave_state(QUfs *ufs, uint32_t from)
{
    uint64_t end_time =
        g_get_monotonic_time() + TIMEOUT_SECONDS * G_TIME_SPAN_SECOND;
    uint32_t state;

    do {
        qtest_clock_step(ufs->dev.bus->qts, 100);
        state = ufs_hid_read_attr(ufs, UFS_QUERY_ATTR_IDN_HID_STATE);
    } while (state == from && g_get_monotonic_time() < end_time);

    return state;
}

static uint32_t ufs_hid_wait_state(QUfs *ufs, uint32_t expected)
{
    uint64_t end_time =
        g_get_monotonic_time() + TIMEOUT_SECONDS * G_TIME_SPAN_SECOND;
    uint32_t state;

    do {
        qtest_clock_step(ufs->dev.bus->qts, 100);
        state = ufs_hid_read_attr(ufs, UFS_QUERY_ATTR_IDN_HID_STATE);
    } while (state != expected && g_get_monotonic_time() < end_time);

    g_assert_cmpuint(state, ==, expected);
    return state;
}

static void ufs_hid_wait_progress_complete(QUfs *ufs)
{
    uint64_t end_time =
        g_get_monotonic_time() + TIMEOUT_SECONDS * G_TIME_SPAN_SECOND;
    uint32_t val;

    do {
        qtest_clock_step(ufs->dev.bus->qts, 100);
        val = ufs_hid_read_attr(ufs, UFS_QUERY_ATTR_IDN_HID_PROG_RATIO);
    } while (val != 100 && g_get_monotonic_time() < end_time);

    g_assert_cmpuint(val, ==, 100);
}

static void ufstest_hid(void *obj, void *data, QGuestAllocator *alloc)
{
    QUfs *ufs = obj;
    uint32_t val;
    enum UtpOcsCodes ocs;
    UtpUpiuRsp rsp;
    const int test_lun = 1;
    const uint8_t request_sense_cdb[UFS_CDB_SIZE] = {
        REQUEST_SENSE,
    };
    const uint8_t write_cdb[UFS_CDB_SIZE] = {
        /* WRITE(10) to LBA 0, transfer length 8 sectors = 1 fragment */
        WRITE_10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00
    };
    /* The PRDT must hold the eight blocks that the WRITE(10) moves */
    uint8_t write_buf[8 * 4096];
    uint8_t read_buf[4096];
    int i;

    ufs_init(ufs, alloc);

    /* Clear Unit Attention */
    ufs_send_scsi_command(ufs, test_lun, request_sense_cdb, NULL, 0, read_buf,
                          sizeof(read_buf), &rsp);

    /*
     * 1. Verify HID default attribute values
     */
    val = ufs_hid_read_attr(ufs, UFS_QUERY_ATTR_IDN_DEFRAG_OP);
    g_assert_cmpuint(val, ==, UFS_HID_OP_DISABLE);

    val = ufs_hid_read_attr(ufs, UFS_QUERY_ATTR_IDN_HID_AVAIL_SIZE);
    g_assert_cmpuint(val, ==, 0xFFFFFFFF);

    val = ufs_hid_read_attr(ufs, UFS_QUERY_ATTR_IDN_HID_SIZE);
    g_assert_cmpuint(val, ==, 0xFFFFFFFF);

    val = ufs_hid_read_attr(ufs, UFS_QUERY_ATTR_IDN_HID_PROG_RATIO);
    g_assert_cmpuint(val, ==, 0);

    val = ufs_hid_read_attr(ufs, UFS_QUERY_ATTR_IDN_HID_STATE);
    g_assert_cmpuint(val, ==, UFS_HID_STATE_IDLE);

    /*
     * 2. Verify read-only attributes reject writes
     */
    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_WRITE_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_WRITE_ATTR,
                         UFS_QUERY_ATTR_IDN_HID_AVAIL_SIZE, 0, 0, 0x100, &rsp);
    g_assert_cmpuint(ocs, ==, UFS_OCS_INVALID_CMD_TABLE_ATTR);
    g_assert_cmpuint(rsp.header.response, ==, UFS_QUERY_RESULT_NOT_WRITEABLE);

    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_WRITE_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_WRITE_ATTR,
                         UFS_QUERY_ATTR_IDN_HID_PROG_RATIO, 0, 0, 50, &rsp);
    g_assert_cmpuint(ocs, ==, UFS_OCS_INVALID_CMD_TABLE_ATTR);
    g_assert_cmpuint(rsp.header.response, ==, UFS_QUERY_RESULT_NOT_WRITEABLE);

    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_WRITE_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_WRITE_ATTR,
                         UFS_QUERY_ATTR_IDN_HID_STATE, 0, 0, 0x01, &rsp);
    g_assert_cmpuint(ocs, ==, UFS_OCS_INVALID_CMD_TABLE_ATTR);
    g_assert_cmpuint(rsp.header.response, ==, UFS_QUERY_RESULT_NOT_WRITEABLE);

    /*
     * 3. Verify invalid bDefragOperation value is rejected
     */
    ocs = ufs_send_query(ufs, UFS_UPIU_QUERY_FUNC_STANDARD_WRITE_REQUEST,
                         UFS_UPIU_QUERY_OPCODE_WRITE_ATTR,
                         UFS_QUERY_ATTR_IDN_DEFRAG_OP, 0, 0, 0x03, &rsp);
    g_assert_cmpuint(ocs, ==, UFS_OCS_INVALID_CMD_TABLE_ATTR);
    g_assert_cmpuint(rsp.header.response, ==, UFS_QUERY_RESULT_INVALID_VALUE);

    /*
     * 4. dHIDSize is writable and readable
     */
    ufs_hid_write_attr(ufs, UFS_QUERY_ATTR_IDN_HID_SIZE, 0x100);
    val = ufs_hid_read_attr(ufs, UFS_QUERY_ATTR_IDN_HID_SIZE);
    g_assert_cmpuint(val, ==, 0x100);

    /* Restore default */
    ufs_hid_write_attr(ufs, UFS_QUERY_ATTR_IDN_HID_SIZE, 0xFFFFFFFF);

    /*
     * 5. Analysis with no fragments -> Defrag Not Required, then reading
     *    bHIDState in a terminal state auto-resets HID to Idle.
     */
    ufs_hid_write_attr(ufs, UFS_QUERY_ATTR_IDN_DEFRAG_OP, UFS_HID_OP_ANALYSIS);

    val = ufs_hid_read_attr(ufs, UFS_QUERY_ATTR_IDN_HID_STATE);
    g_assert_cmpuint(val, ==, UFS_HID_STATE_ANALYSIS_IN_PROGRESS);

    /* Idle handler advances analysis; first non-analysis read is decisive */
    val = ufs_hid_wait_leave_state(ufs, UFS_HID_STATE_ANALYSIS_IN_PROGRESS);
    g_assert_cmpuint(val, ==, UFS_HID_STATE_DEFRAG_NOT_REQUIRED);

    /* Reading Not Required auto-reset to Idle */
    val = ufs_hid_read_attr(ufs, UFS_QUERY_ATTR_IDN_HID_STATE);
    g_assert_cmpuint(val, ==, UFS_HID_STATE_IDLE);

    val = ufs_hid_read_attr(ufs, UFS_QUERY_ATTR_IDN_HID_AVAIL_SIZE);
    g_assert_cmpuint(val, ==, 0xFFFFFFFF);

    /*
     * 6. Generate fragmentation via SCSI WRITE, then analyze (analysis
     *    only): the machine settles at Defrag Required.
     */
    memset(write_buf, 0xab, sizeof(write_buf));
    for (i = 0; i < 4; i++) {
        ocs = ufs_send_scsi_command(ufs, test_lun, write_cdb, write_buf,
                                    sizeof(write_buf), NULL, 0, &rsp);
        g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    }

    ufs_hid_write_attr(ufs, UFS_QUERY_ATTR_IDN_DEFRAG_OP, UFS_HID_OP_ANALYSIS);

    val = ufs_hid_read_attr(ufs, UFS_QUERY_ATTR_IDN_HID_STATE);
    g_assert_cmpuint(val, ==, UFS_HID_STATE_ANALYSIS_IN_PROGRESS);

    val = ufs_hid_wait_leave_state(ufs, UFS_HID_STATE_ANALYSIS_IN_PROGRESS);
    g_assert_cmpuint(val, ==, UFS_HID_STATE_DEFRAG_REQUIRED);

    val = ufs_hid_read_attr(ufs, UFS_QUERY_ATTR_IDN_DEFRAG_OP);
    g_assert_cmpuint(val, ==, UFS_HID_OP_DISABLE);

    val = ufs_hid_read_attr(ufs, UFS_QUERY_ATTR_IDN_HID_AVAIL_SIZE);
    g_assert_cmpuint(val, >, 0);

    /*
     * 7. Disable resets to Idle from any state
     */
    ufs_hid_write_attr(ufs, UFS_QUERY_ATTR_IDN_DEFRAG_OP, UFS_HID_OP_DISABLE);

    val = ufs_hid_read_attr(ufs, UFS_QUERY_ATTR_IDN_HID_STATE);
    g_assert_cmpuint(val, ==, UFS_HID_STATE_IDLE);

    /*
     * 8. Partial defrag cycle: dHIDSize limits the requested defrag size,
     *    and reading bHIDProgressRatio at 100% resets the HID attributes.
     */
    for (i = 0; i < 4; i++) {
        ocs = ufs_send_scsi_command(ufs, test_lun, write_cdb, write_buf,
                                    sizeof(write_buf), NULL, 0, &rsp);
        g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    }

    ufs_hid_write_attr(ufs, UFS_QUERY_ATTR_IDN_HID_SIZE, 2);
    ufs_hid_write_attr(ufs, UFS_QUERY_ATTR_IDN_DEFRAG_OP, UFS_HID_OP_DEFRAG);

    /* Should start in Analysis In Progress */
    val = ufs_hid_read_attr(ufs, UFS_QUERY_ATTR_IDN_HID_STATE);
    g_assert_cmpuint(val, ==, UFS_HID_STATE_ANALYSIS_IN_PROGRESS);

    ufs_hid_wait_progress_complete(ufs);

    val = ufs_hid_read_attr(ufs, UFS_QUERY_ATTR_IDN_HID_AVAIL_SIZE);
    g_assert_cmpuint(val, ==, 0xFFFFFFFF);

    val = ufs_hid_read_attr(ufs, UFS_QUERY_ATTR_IDN_DEFRAG_OP);
    g_assert_cmpuint(val, ==, UFS_HID_OP_DISABLE);

    val = ufs_hid_read_attr(ufs, UFS_QUERY_ATTR_IDN_HID_PROG_RATIO);
    g_assert_cmpuint(val, ==, 0);

    /*
     * Re-analyze after the partial defrag. There were 8 fragments before
     * defrag, and dHIDSize limited the completed operation to 2.
     */
    ufs_hid_write_attr(ufs, UFS_QUERY_ATTR_IDN_DEFRAG_OP, UFS_HID_OP_ANALYSIS);

    val = ufs_hid_wait_leave_state(ufs, UFS_HID_STATE_ANALYSIS_IN_PROGRESS);
    g_assert_cmpuint(val, ==, UFS_HID_STATE_DEFRAG_REQUIRED);

    val = ufs_hid_read_attr(ufs, UFS_QUERY_ATTR_IDN_HID_AVAIL_SIZE);
    g_assert_cmpuint(val, ==, 6);

    ufs_hid_write_attr(ufs, UFS_QUERY_ATTR_IDN_DEFRAG_OP, UFS_HID_OP_DISABLE);
    ufs_hid_write_attr(ufs, UFS_QUERY_ATTR_IDN_HID_SIZE, 0xFFFFFFFF);

    /*
     * 9. Full defrag cycle: reading bHIDState in Completed state returns
     *    Completed once and resets HID attributes to Idle/default values.
     */
    ufs_hid_write_attr(ufs, UFS_QUERY_ATTR_IDN_DEFRAG_OP, UFS_HID_OP_DEFRAG);

    val = ufs_hid_wait_state(ufs, UFS_HID_STATE_DEFRAG_COMPLETED);
    g_assert_cmpuint(val, ==, UFS_HID_STATE_DEFRAG_COMPLETED);

    val = ufs_hid_read_attr(ufs, UFS_QUERY_ATTR_IDN_HID_STATE);
    g_assert_cmpuint(val, ==, UFS_HID_STATE_IDLE);

    val = ufs_hid_read_attr(ufs, UFS_QUERY_ATTR_IDN_DEFRAG_OP);
    g_assert_cmpuint(val, ==, UFS_HID_OP_DISABLE);

    val = ufs_hid_read_attr(ufs, UFS_QUERY_ATTR_IDN_HID_AVAIL_SIZE);
    g_assert_cmpuint(val, ==, 0xFFFFFFFF);

    ufs_exit(ufs, alloc);
}

static void ufs_send_tmr(QUfs *ufs, uint64_t utmrlba, int slot,
                         uint8_t tm_func, uint8_t lun,
                         uint32_t task_tag,
                         UtpTaskReqDesc *utrd_out)
{
    uint64_t desc_addr = utmrlba + slot * sizeof(UtpTaskReqDesc);
    UtpTaskReqDesc desc = { 0 };

    desc.header.dword_0 = cpu_to_le32(UFS_UTP_REQ_DESC_INT_CMD);
    desc.header.dword_2 = cpu_to_le32(UFS_OCS_INVALID_COMMAND_STATUS);

    desc.upiu_req.req_header.trans_type = UFS_UPIU_TRANSACTION_TASK_REQ;
    desc.upiu_req.req_header.query_func = tm_func;
    desc.upiu_req.req_header.lun = lun;
    desc.upiu_req.req_header.task_tag = 0x10 + slot;
    desc.upiu_req.input_param1 = cpu_to_be32(lun);
    desc.upiu_req.input_param2 = cpu_to_be32(task_tag);

    qtest_memwrite(ufs->dev.bus->qts, desc_addr, &desc, sizeof(desc));

    ufs_wreg(ufs, A_UTMRLDBR, 1 << slot);
    ufs_wait_for_irq(ufs);
    g_assert_true(FIELD_EX32(ufs_rreg(ufs, A_IS), IS, UTMRCS));
    ufs_wreg(ufs, A_IS, FIELD_DP32(0, IS, UTMRCS, 1));
    g_assert_cmpuint(ufs_rreg(ufs, A_UTMRLDBR) & (1 << slot), ==, 0);

    qtest_memread(ufs->dev.bus->qts, desc_addr, utrd_out, sizeof(*utrd_out));
}

static int ufs_mcq_send_scsi_cmd_async(QUfs *ufs, uint8_t lun,
                                       const uint8_t *cdb)
{
    int cmd_desc_slot = alloc_cmd_desc_slot(ufs);
    uint64_t req_upiu_addr =
        ufs->cmd_desc_addr + cmd_desc_slot * UTP_COMMAND_DESCRIPTOR_SIZE;
    UtpUpiuReq req_upiu = { 0 };
    UtpTransferReqDesc utrd;

    req_upiu.header.trans_type = UFS_UPIU_TRANSACTION_COMMAND;
    req_upiu.header.flags = UFS_UPIU_CMD_FLAGS_NONE;
    req_upiu.header.lun = lun;
    req_upiu.header.task_tag = cmd_desc_slot;
    memcpy(req_upiu.sc.cdb, cdb, UFS_CDB_SIZE);
    qtest_memwrite(ufs->dev.bus->qts, req_upiu_addr, &req_upiu,
                   sizeof(req_upiu));

    utrd = ufs_build_req_utrd(req_upiu_addr, UFS_UTP_NO_DATA_TRANSFER, 0);
    __ufs_send_transfer_request_mcq_async(ufs, lun, &utrd);

    return cmd_desc_slot;
}

static int ufs_mcq_send_write_async(QUfs *ufs, uint8_t lun,
                                    const uint8_t *cdb,
                                    const uint8_t *data_in,
                                    size_t data_in_len)
{
    QTestState *qts = ufs->dev.bus->qts;
    int cmd_desc_slot = alloc_cmd_desc_slot(ufs);
    uint64_t req_upiu_addr =
        ufs->cmd_desc_addr + cmd_desc_slot * UTP_COMMAND_DESCRIPTOR_SIZE;
    UfshcdSgEntry entry = { 0 };
    UtpUpiuReq req_upiu = { 0 };
    UtpTransferReqDesc utrd;

    g_assert_true(ufs->support_mcq);
    g_assert_cmpuint(data_in_len, <=, PRD_ENTRY_DATA_SIZE);
    qtest_memwrite(qts, ufs->data_buffer_addr, data_in, data_in_len);
    entry.addr = cpu_to_le64(ufs->data_buffer_addr);
    entry.size = cpu_to_le32(data_in_len - 1);
    qtest_memwrite(qts, req_upiu_addr + UTP_PRDT_UPIU_OFFSET, &entry,
                   sizeof(entry));

    req_upiu.header.trans_type = UFS_UPIU_TRANSACTION_COMMAND;
    req_upiu.header.flags = UFS_UPIU_CMD_FLAGS_WRITE;
    req_upiu.header.lun = lun;
    req_upiu.header.task_tag = cmd_desc_slot;
    req_upiu.sc.exp_data_transfer_len = cpu_to_be32(data_in_len);
    memcpy(req_upiu.sc.cdb, cdb, UFS_CDB_SIZE);
    qtest_memwrite(qts, req_upiu_addr, &req_upiu, sizeof(req_upiu));

    utrd = ufs_build_req_utrd(req_upiu_addr, UFS_UTP_HOST_TO_DEVICE, 1);
    __ufs_send_transfer_request_mcq_async(ufs, lun, &utrd);
    return cmd_desc_slot;
}

static int ufs_send_scsi_cmd_doorbell_async(QUfs *ufs, uint8_t lun,
                                            const uint8_t *cdb)
{
    int cmd_desc_slot = alloc_cmd_desc_slot(ufs);
    uint64_t req_upiu_addr =
        ufs->cmd_desc_addr + cmd_desc_slot * UTP_COMMAND_DESCRIPTOR_SIZE;
    uint64_t utrd_addr =
        ufs->utrlba + cmd_desc_slot * sizeof(UtpTransferReqDesc);
    UtpUpiuReq req_upiu = { 0 };
    UtpTransferReqDesc utrd;

    req_upiu.header.trans_type = UFS_UPIU_TRANSACTION_COMMAND;
    req_upiu.header.flags = UFS_UPIU_CMD_FLAGS_NONE;
    req_upiu.header.lun = lun;
    req_upiu.header.task_tag = cmd_desc_slot;
    memcpy(req_upiu.sc.cdb, cdb, UFS_CDB_SIZE);
    qtest_memwrite(ufs->dev.bus->qts, req_upiu_addr, &req_upiu,
                   sizeof(req_upiu));

    utrd = ufs_build_req_utrd(req_upiu_addr, UFS_UTP_NO_DATA_TRANSFER, 0);
    qtest_memwrite(ufs->dev.bus->qts, utrd_addr, &utrd, sizeof(utrd));

    ufs_wreg(ufs, A_UTRLDBR, 1 << cmd_desc_slot);
    return cmd_desc_slot;
}


static void ufs_test_hce_reset(void *obj, void *data, QGuestAllocator *alloc)
{
    QUfs *ufs = obj;
    uint32_t mcqcap, qcfgptr, mcq_reg_offset;
    uint32_t orig_sqdao, orig_cqdao, orig_ie;
    enum UtpOcsCodes ocs;
    UtpUpiuRsp rsp_upiu;

    ufs_init(ufs, alloc);
    g_assert_true(ufs->support_mcq);

    mcqcap = ufs_rreg(ufs, A_MCQCAP);
    qcfgptr = FIELD_EX32(mcqcap, MCQCAP, QCFGPTR);
    mcq_reg_offset = qcfgptr * 0x200 + TEST_QID * 0x40;

    orig_sqdao = ufs_rreg(ufs, mcq_reg_offset + A_SQDAO);
    orig_cqdao = ufs_rreg(ufs, mcq_reg_offset + A_CQDAO);
    orig_ie = ufs_rreg(ufs, A_IE);
    g_assert_cmpuint(orig_sqdao, !=, 0);
    g_assert_cmpuint(orig_cqdao, !=, 0);
    g_assert_cmpuint(orig_ie, !=, 0);
    g_assert_cmpuint(ufs_rreg(ufs, mcq_reg_offset + A_SQATTR), !=, 0);
    g_assert_cmpuint(ufs_rreg(ufs, mcq_reg_offset + A_CQATTR), !=, 0);
    g_assert_cmpuint(ufs_rreg(ufs, orig_sqdao + 0x4), !=, 0);

    ufs_wreg(ufs, A_UTMRLBA, 0x1000);
    ufs_wreg(ufs, A_UTMRLBAU, 0x1);
    ufs_wreg(ufs, A_UTMRLRSR, 1);
    ufs_wreg(ufs, A_MCQCONFIG, 0x100);
    g_assert_cmpuint(ufs_rreg(ufs, A_UTMRLRSR), ==, 1);

    /* Write HCE=0 to trigger controller reset */
    ufs_wreg(ufs, A_HCE, 0);
    g_assert_cmpuint(FIELD_EX32(ufs_rreg(ufs, A_HCE), HCE, HCE), ==, 0);
    g_assert_cmpuint(ufs_rreg(ufs, A_HCS), ==, 0);
    g_assert_cmpuint(ufs_rreg(ufs, A_UTMRLRSR), ==, 0);

    /* Verify host-configured registers remain preserved across HCE=0 */
    g_assert_cmpuint(ufs_rreg(ufs, A_IE), ==, orig_ie);
    g_assert_cmpuint(ufs_rreg(ufs, A_UTMRLBA), ==, 0x1000);
    g_assert_cmpuint(ufs_rreg(ufs, A_UTMRLBAU), ==, 0x1);
    g_assert_cmpuint(ufs_rreg(ufs, A_MCQCONFIG), ==, 0x100);

    /* Verify dynamic queue configuration registers reset to 0 */
    g_assert_cmpuint(ufs_rreg(ufs, mcq_reg_offset + A_SQATTR), ==, 0);
    g_assert_cmpuint(ufs_rreg(ufs, mcq_reg_offset + A_SQLBA), ==, 0);
    g_assert_cmpuint(ufs_rreg(ufs, mcq_reg_offset + A_CQATTR), ==, 0);
    g_assert_cmpuint(ufs_rreg(ufs, mcq_reg_offset + A_CQLBA), ==, 0);

    /* Verify dynamic MCQ operational registers reset to 0 on HCE=0 */
    g_assert_cmpuint(ufs_rreg(ufs, orig_sqdao), ==, 0);
    g_assert_cmpuint(ufs_rreg(ufs, orig_sqdao + 0x4), ==, 0);
    g_assert_cmpuint(ufs_rreg(ufs, orig_cqdao), ==, 0);
    g_assert_cmpuint(ufs_rreg(ufs, orig_cqdao + 0x4), ==, 0);

    /* Verify read-only capability offsets (SQDAO/CQDAO) remain intact */
    g_assert_cmpuint(ufs_rreg(ufs, mcq_reg_offset + A_SQDAO), ==, orig_sqdao);
    g_assert_cmpuint(ufs_rreg(ufs, mcq_reg_offset + A_CQDAO), ==, orig_cqdao);

    /* Re-enable HCE=1 and verify dynamic queue status/pointers remain 0 */
    ufs_wreg(ufs, A_HCE, FIELD_DP32(0, HCE, HCE, 1));
    g_assert_cmpuint(FIELD_EX32(ufs_rreg(ufs, A_HCE), HCE, HCE), ==, 1);
    g_assert_cmpuint(ufs_rreg(ufs, orig_sqdao), ==, 0);
    g_assert_cmpuint(ufs_rreg(ufs, orig_sqdao + 0x4), ==, 0);
    g_assert_cmpuint(ufs_rreg(ufs, orig_cqdao), ==, 0);
    g_assert_cmpuint(ufs_rreg(ufs, orig_cqdao + 0x4), ==, 0);

    /* Free previous guest allocations before re-initializing */
    ufs_exit(ufs, alloc);

    /* Re-enable HCE=1, re-init controller, and verify normal I/O succeeds */
    ufs_init(ufs, alloc);
    ocs = ufs_send_nop_out(ufs, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);

    ufs_exit(ufs, alloc);
}

static void ufs_test_tmr_handling(void *obj, void *data, QGuestAllocator *alloc)
{
    QUfs *ufs = obj;
    uint64_t utmrlba, desc_addr, req_upiu_addr, rsp_upiu_addr;
    UtpTaskReqDesc desc = { 0 }, utrd_result;
    UtpTransferReqDesc null_utrd;
    UtpUpiuReq req_upiu = { 0 };
    UfsCqEntry cqe;
    uint32_t mcq_reg_offset, sqdao, cqisao, sqrts, sqrtc, orig_sqattr;
    int null_slot;
    enum UtpOcsCodes ocs;
    UtpUpiuRsp rsp_upiu;

    ufs_init(ufs, alloc);
    g_assert_true(ufs->support_mcq);

    /* Set up UTMRL */
    utmrlba = guest_alloc(alloc, 8 * sizeof(UtpTaskReqDesc));
    ufs_wreg(ufs, A_UTMRLBA, utmrlba & 0xffffffff);
    ufs_wreg(ufs, A_UTMRLBAU, utmrlba >> 32);
    ufs_wreg(ufs, A_UTMRLRSR, 1);

    /* Issue UFS_QUERY_TASK for inactive tag 15 (Interrupt bit I=1) */
    ufs_send_tmr(ufs, utmrlba, 0, UFS_QUERY_TASK, 0, 15, &utrd_result);
    g_assert_cmpuint(le32_to_cpu(utrd_result.header.dword_2) & UFS_MASK_OCS,
                     ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(utrd_result.upiu_rsp.rsp_header.response,
                     ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(be32_to_cpu(utrd_result.upiu_rsp.output_param1),
                     ==, UFS_UPIU_TASK_MANAGEMENT_FUNC_COMPL);

    /* Issue polling UTMRD with I=0 (slot 1): clears doorbell without UTMRCS */
    desc_addr = utmrlba + 1 * sizeof(UtpTaskReqDesc);
    desc.header.dword_0 = 0;
    desc.header.dword_2 = cpu_to_le32(UFS_OCS_INVALID_COMMAND_STATUS);
    desc.upiu_req.req_header.trans_type = UFS_UPIU_TRANSACTION_TASK_REQ;
    desc.upiu_req.req_header.query_func = UFS_QUERY_TASK;
    desc.upiu_req.req_header.lun = 0;
    desc.upiu_req.req_header.task_tag = 0x11;
    desc.upiu_req.input_param1 = cpu_to_be32(0);
    desc.upiu_req.input_param2 = cpu_to_be32(15);
    qtest_memwrite(ufs->dev.bus->qts, desc_addr, &desc, sizeof(desc));

    ufs_wreg(ufs, A_UTMRLDBR, 1 << 1);
    g_assert_cmpuint(ufs_rreg(ufs, A_UTMRLDBR) & (1 << 1), ==, 0);
    g_assert_cmpuint(FIELD_EX32(ufs_rreg(ufs, A_IS), IS, UTMRCS), ==, 0);
    qtest_memread(ufs->dev.bus->qts, desc_addr, &utrd_result,
                  sizeof(utrd_result));
    g_assert_cmpuint(le32_to_cpu(utrd_result.header.dword_2) & UFS_MASK_OCS,
                     ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(utrd_result.upiu_rsp.rsp_header.response,
                     ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(be32_to_cpu(utrd_result.upiu_rsp.output_param1),
                     ==, UFS_UPIU_TASK_MANAGEMENT_FUNC_COMPL);

    /* Verify SQCTI, CQIACR, SQRTS/SQRTC, and CQIS/IS.CQES W1C sync */
    sqdao = ufs->sqdao[TEST_QID];
    mcq_reg_offset =
        FIELD_EX32(ufs_rreg(ufs, A_MCQCAP), MCQCAP, QCFGPTR) * 0x200 +
        TEST_QID * 0x40;
    cqisao = ufs_rreg(ufs, mcq_reg_offset + A_CQISAO);

    /* SQCTI and CQIACR read/write */
    ufs_wreg(ufs, sqdao + A_SQCTI, (1 << 8) | 0x2b);
    g_assert_cmpuint(ufs_rreg(ufs, sqdao + A_SQCTI), ==, (1 << 8) | 0x2b);
    ufs_wreg(ufs, cqisao + offsetof(UfsMcqCqIntReg, iacr), 0x101);
    g_assert_cmpuint(ufs_rreg(ufs, cqisao + offsetof(UfsMcqCqIntReg, iacr)),
                     ==, 0x101);

    /* SQ_ICU while running (STS=0) -> sets SQRTS.CUS=1, RTC=2 */
    ufs_wreg(ufs, sqdao + A_SQRTC, FIELD_DP32(0, SQRTC, ICU, 1));
    sqrts = ufs_rreg(ufs, sqdao + A_SQRTS);
    sqrtc = ufs_rreg(ufs, sqdao + A_SQRTC);
    g_assert_cmpuint(FIELD_EX32(sqrts, SQRTS, STS), ==, 0);
    g_assert_cmpuint(FIELD_EX32(sqrts, SQRTS, CUS), ==, 1);
    g_assert_cmpuint(FIELD_EX32(sqrts, SQRTS, RTC), ==, 2);
    g_assert_cmpuint(FIELD_EX32(sqrtc, SQRTC, ICU), ==, 0);

    /* SQ_STOP -> sets SQRTS.STS = 1 */
    ufs_wreg(ufs, sqdao + A_SQRTC, FIELD_DP32(0, SQRTC, STOP, 1));
    sqrts = ufs_rreg(ufs, sqdao + A_SQRTS);
    g_assert_cmpuint(FIELD_EX32(sqrts, SQRTS, STS), ==, 1);

    /*
     * SQ_ICU while stopped (no matching req) -> sets SQRTS.CUS=1, RTC=1 and
     * self-clears ICU.
     */
    ufs_wreg(ufs, sqdao + A_SQRTC,
             FIELD_DP32(0, SQRTC, STOP, 1) | FIELD_DP32(0, SQRTC, ICU, 1));
    sqrts = ufs_rreg(ufs, sqdao + A_SQRTS);
    sqrtc = ufs_rreg(ufs, sqdao + A_SQRTC);
    g_assert_cmpuint(FIELD_EX32(sqrts, SQRTS, STS), ==, 1);
    g_assert_cmpuint(FIELD_EX32(sqrts, SQRTS, CUS), ==, 1);
    g_assert_cmpuint(FIELD_EX32(sqrts, SQRTS, RTC), ==, 1);
    g_assert_cmpuint(FIELD_EX32(sqrtc, SQRTC, ICU), ==, 0);

    /*
     * Queue a nullified SQE (CT = 0xF) while SQ is stopped, then resume SQ
     * (STOP 1->0): SQRTS.STS clears to 0 while SQRTS.CUS remains 1, and the
     * nullified SQE posts CQE with UFS_OCS_ABORTED without writing rsp_upiu.
     */
    null_slot = alloc_cmd_desc_slot(ufs);
    req_upiu_addr =
        ufs->cmd_desc_addr + null_slot * UTP_COMMAND_DESCRIPTOR_SIZE;
    rsp_upiu_addr = req_upiu_addr + UTP_RESPONSE_UPIU_OFFSET;
    req_upiu.header.trans_type = UFS_UPIU_TRANSACTION_NOP_OUT;
    req_upiu.header.task_tag = null_slot;
    qtest_memwrite(ufs->dev.bus->qts, req_upiu_addr, &req_upiu,
                   sizeof(req_upiu));
    memset(&rsp_upiu, 0xa5, sizeof(rsp_upiu));
    qtest_memwrite(ufs->dev.bus->qts, rsp_upiu_addr, &rsp_upiu,
                   sizeof(rsp_upiu));

    null_utrd = ufs_build_req_utrd(req_upiu_addr, UFS_UTP_NO_DATA_TRANSFER, 0);
    null_utrd.header.dword_0 = cpu_to_le32((UFS_UTP_CMD_TYPE_NULL << 28) |
                                           UFS_UTP_NO_DATA_TRANSFER |
                                           UFS_UTP_REQ_DESC_INT_CMD);
    __ufs_send_transfer_request_mcq_async(ufs, 0, &null_utrd);

    /* SQ_START (STOP 1->0) -> clears SQRTS.STS = 0 while preserving CUS = 1 */
    ufs_wreg(ufs, sqdao + A_SQRTC, 0);
    sqrts = ufs_rreg(ufs, sqdao + A_SQRTS);
    g_assert_cmpuint(FIELD_EX32(sqrts, SQRTS, STS), ==, 0);
    g_assert_cmpuint(FIELD_EX32(sqrts, SQRTS, CUS), ==, 1);

    ufs_wait_for_irq(ufs);
    g_assert_cmpuint(ufs_mcq_poll_cq(ufs, &cqe, 1), ==, 1);
    g_assert_cmpuint(cqe.status, ==, UFS_OCS_ABORTED);
    g_assert_cmpuint(cqe.task_tag, ==, null_slot);
    qtest_memread(ufs->dev.bus->qts, rsp_upiu_addr, &rsp_upiu,
                  sizeof(rsp_upiu));
    g_assert_cmpuint(rsp_upiu.header.trans_type, ==, 0xa5);
    ufs_wreg(ufs, cqisao + A_CQIS, FIELD_DP32(0, CQIS, TEPS, 1));
    release_cmd_desc_slot(ufs, null_slot);

    /* Verify MCQ command sets CQIS.TEPS=1 and W1C clears CQIS.TEPS & IS.CQES */
    ocs = ufs_send_nop_out(ufs, &rsp_upiu);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpuint(FIELD_EX32(ufs_rreg(ufs, cqisao + A_CQIS), CQIS, TEPS),
                     ==, 1);
    ufs_wreg(ufs, cqisao + A_CQIS, FIELD_DP32(0, CQIS, TEPS, 1));
    g_assert_cmpuint(FIELD_EX32(ufs_rreg(ufs, cqisao + A_CQIS), CQIS, TEPS),
                     ==, 0);
    g_assert_cmpuint(FIELD_EX32(ufs_rreg(ufs, A_IS), IS, CQES), ==, 0);

    /* SQ_ICU on disabled SQ (SQEN=0) -> sets SQRTS.CUS=1, RTC=3 */
    orig_sqattr = ufs_rreg(ufs, mcq_reg_offset + A_SQATTR);
    ufs_wreg(ufs, mcq_reg_offset + A_SQATTR, 0);
    ufs_wreg(ufs, sqdao + A_SQRTC, FIELD_DP32(0, SQRTC, ICU, 1));
    sqrts = ufs_rreg(ufs, sqdao + A_SQRTS);
    g_assert_cmpuint(FIELD_EX32(sqrts, SQRTS, CUS), ==, 1);
    g_assert_cmpuint(FIELD_EX32(sqrts, SQRTS, RTC), ==, 3);
    ufs_wreg(ufs, mcq_reg_offset + A_SQATTR, orig_sqattr);

    guest_free(alloc, utmrlba);
    ufs_exit(ufs, alloc);
}

#define UFS_FAULT_INJECT_TAG_NONE 0xffffffffU
#define UFS_FAULT_INJECT_TAG_ANY  0xfffffffeU

static void ufs_test_fault_injection_abort(void *obj, void *data,
                                           QGuestAllocator *alloc)
{
    QUfs *ufs = obj;
    QTestState *qts = ufs->dev.bus->qts;
    uint64_t utmrlba, desc_addr;
    UtpTaskReqDesc utrd, utrd_result;
    const uint8_t test_unit_ready_cdb[UFS_CDB_SIZE] = { TEST_UNIT_READY };
    const uint8_t read10_lba512_cdb[UFS_CDB_SIZE] = {
        READ_10, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    const uint8_t start_stop_cdb[UFS_CDB_SIZE] = { START_STOP };
    const uint8_t request_sense_cdb[UFS_CDB_SIZE] = { REQUEST_SENSE };
    const uint8_t write10_cdb[UFS_CDB_SIZE] = {
        WRITE_10, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00
    };
    const uint8_t read10_cdb[UFS_CDB_SIZE] = {
        READ_10, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00
    };
    const uint8_t read10_lba0_cdb[UFS_CDB_SIZE] = {
        READ_10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    const uint8_t read16_lba0_cdb[UFS_CDB_SIZE] = {
        READ_16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    const uint8_t read16_lba512_cdb[UFS_CDB_SIZE] = {
        READ_16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    uint8_t write_buf[4096];
    uint8_t read_buf[4096];
    UtpUpiuRsp read_rsp;
    enum UtpOcsCodes ocs;
    uint32_t sqdao, cqdao, mcq_reg_offset, cqisao, sqrts, cqhp, cqtp;
    UfsCqEntry cqe;
    int slot;
    QDict *rsp;

    ufs_init(ufs, alloc);
    g_assert_true(ufs->support_mcq);

    utmrlba = guest_alloc(alloc, 8 * sizeof(UtpTaskReqDesc));
    ufs_wreg(ufs, A_UTMRLBA, utmrlba & 0xffffffff);
    ufs_wreg(ufs, A_UTMRLBAU, utmrlba >> 32);
    ufs_wreg(ufs, A_UTMRLRSR, 1);

    sqdao = ufs->sqdao[TEST_QID];
    cqdao = ufs->cqdao[TEST_QID];
    mcq_reg_offset =
        FIELD_EX32(ufs_rreg(ufs, A_MCQCAP), MCQCAP, QCFGPTR) * 0x200 +
        TEST_QID * 0x40;
    cqisao = ufs_rreg(ufs, mcq_reg_offset + A_CQISAO);

    /*
     * Case 1: x-fault-inject-mode = abort-success (two-stage MCQ abort +
     * SQRTC.ICU).
     */
    slot = find_first_zero_bit(ufs->cmd_desc_bitmap, UFS_MAX_CMD_DESC);
    qtest_qmp_assert_success(
        qts,
        "{ 'execute': 'qom-set', 'arguments': { "
        "'path': '/machine/peripheral/ufs0', "
        "'property': 'x-fault-inject-mode', 'value': 'abort-success' } }");
    qtest_qmp_assert_success(
        qts,
        "{ 'execute': 'qom-set', 'arguments': { "
        "'path': '/machine/peripheral/ufs0', "
        "'property': 'x-fault-inject-tag', 'value': %d } }", slot);

    slot = ufs_mcq_send_scsi_cmd_async(ufs, 0, test_unit_ready_cdb);
    qtest_clock_step(qts, 100);

    /* Issue UFS_QUERY_TASK -> assert Target Success & SUCCEEDED (0x08) */
    ufs_send_tmr(ufs, utmrlba, 0, UFS_QUERY_TASK, 0, slot, &utrd_result);
    g_assert_cmpuint(utrd_result.upiu_rsp.rsp_header.response,
                     ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(be32_to_cpu(utrd_result.upiu_rsp.output_param1),
                     ==, UFS_UPIU_TASK_MANAGEMENT_FUNC_SUCCEEDED);

    /* Verify x-fault-inject-tag cannot be overwritten while a req is held */
    rsp = qtest_qmp(qts,
                    "{ 'execute': 'qom-set', 'arguments': { "
                    "'path': '/machine/peripheral/ufs0', "
                    "'property': 'x-fault-inject-tag', 'value': 7 } }");
    g_assert_true(qdict_haskey(rsp, "error"));
    qobject_unref(rsp);

    /* Issue UFS_ABORT_TASK -> assert COMPL (0x00) and tag cleared */
    ufs_send_tmr(ufs, utmrlba, 0, UFS_ABORT_TASK, 0, slot, &utrd_result);
    g_assert_cmpuint(utrd_result.upiu_rsp.rsp_header.response,
                     ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(be32_to_cpu(utrd_result.upiu_rsp.output_param1),
                     ==, UFS_UPIU_TASK_MANAGEMENT_FUNC_COMPL);

    rsp = qtest_qmp(
        qts,
        "{ 'execute': 'qom-get', 'arguments': { "
        "'path': '/machine/peripheral/ufs0', "
        "'property': 'x-fault-inject-tag' } }");
    g_assert_cmpuint(qdict_get_int(rsp, "return"), ==,
                     UFS_FAULT_INJECT_TAG_NONE);
    qobject_unref(rsp);

    /*
     * UFS_ABORT_TASK alone must NOT post a CQE
     * before SQRTC.ICU is issued.
     */
    cqhp = ufs_rreg(ufs, cqdao);
    cqtp = ufs_rreg(ufs, cqdao + 0x4);
    g_assert_cmpuint(cqtp, ==, cqhp);

    /* Stop SQ and test SQRTC.ICU with a mismatched LUN (lun=1 vs lun=0) */
    ufs_wreg(ufs, sqdao + A_SQRTC, FIELD_DP32(0, SQRTC, STOP, 1));
    sqrts = ufs_rreg(ufs, sqdao + A_SQRTS);
    g_assert_cmpuint(FIELD_EX32(sqrts, SQRTS, STS), ==, 1);

    ufs_wreg(ufs, sqdao + A_SQCTI, (1 << 8) | slot);
    ufs_wreg(ufs, sqdao + A_SQRTC,
             FIELD_DP32(0, SQRTC, STOP, 1) | FIELD_DP32(0, SQRTC, ICU, 1));
    sqrts = ufs_rreg(ufs, sqdao + A_SQRTS);
    g_assert_cmpuint(FIELD_EX32(sqrts, SQRTS, CUS), ==, 1);
    g_assert_cmpuint(FIELD_EX32(sqrts, SQRTS, RTC), ==, 1);
    g_assert_cmpuint(ufs_rreg(ufs, cqdao + 0x4), ==, cqhp);

    /* Issue SQRTC.ICU with matching SQCTI (lun=0) -> posts ABORTED CQE */
    ufs_wreg(ufs, sqdao + A_SQCTI, (0 << 8) | slot);
    ufs_wreg(ufs, sqdao + A_SQRTC,
             FIELD_DP32(0, SQRTC, STOP, 1) | FIELD_DP32(0, SQRTC, ICU, 1));
    sqrts = ufs_rreg(ufs, sqdao + A_SQRTS);
    g_assert_cmpuint(FIELD_EX32(sqrts, SQRTS, CUS), ==, 1);
    g_assert_cmpuint(FIELD_EX32(sqrts, SQRTS, RTC), ==, 0);

    cqtp = ufs_rreg(ufs, cqdao + 0x4);
    g_assert_cmpuint(cqtp, !=, cqhp);
    qtest_memread(qts, ufs->cqlba[TEST_QID] + cqhp, &cqe, sizeof(cqe));
    g_assert_cmpuint(cqe.status, ==, UFS_OCS_ABORTED);
    g_assert_cmpuint(cqe.task_tag, ==, slot);
    ufs_wreg(ufs, cqdao, cqtp);
    ufs_wreg(ufs, cqisao + A_CQIS, FIELD_DP32(0, CQIS, TEPS, 1));
    ufs_wreg(ufs, A_IS, FIELD_DP32(0, IS, CQES, 1));
    ufs_wreg(ufs, sqdao + A_SQRTC, 0);
    release_cmd_desc_slot(ufs, slot);

    /*
     * Direct SQRTC.ICU without prior UFS_ABORT_TASK:
     * ufs_fault_inject_sq_cleanup() must clear held_req and active tag.
     */
    slot = find_first_zero_bit(ufs->cmd_desc_bitmap, UFS_MAX_CMD_DESC);
    qtest_qmp_assert_success(
        qts,
        "{ 'execute': 'qom-set', 'arguments': { "
        "'path': '/machine/peripheral/ufs0', "
        "'property': 'x-fault-inject-mode', 'value': 'abort-success' } }");
    qtest_qmp_assert_success(
        qts,
        "{ 'execute': 'qom-set', 'arguments': { "
        "'path': '/machine/peripheral/ufs0', "
        "'property': 'x-fault-inject-tag', 'value': %d } }", slot);

    cqhp = ufs_rreg(ufs, cqdao);
    slot = ufs_mcq_send_scsi_cmd_async(ufs, 0, test_unit_ready_cdb);
    qtest_clock_step(qts, 100);

    /* Abort directly via SQRTC.ICU */
    ufs_wreg(ufs, sqdao + A_SQRTC, FIELD_DP32(0, SQRTC, STOP, 1));
    ufs_wreg(ufs, sqdao + A_SQCTI, (0 << 8) | slot);
    ufs_wreg(ufs, sqdao + A_SQRTC,
             FIELD_DP32(0, SQRTC, STOP, 1) | FIELD_DP32(0, SQRTC, ICU, 1));
    sqrts = ufs_rreg(ufs, sqdao + A_SQRTS);
    g_assert_cmpuint(FIELD_EX32(sqrts, SQRTS, CUS), ==, 1);
    g_assert_cmpuint(FIELD_EX32(sqrts, SQRTS, RTC), ==, 0);

    /* Verify ufs_fault_inject_sq_cleanup cleared the held request */
    rsp = qtest_qmp(
        qts,
        "{ 'execute': 'qom-get', 'arguments': { "
        "'path': '/machine/peripheral/ufs0', "
        "'property': 'x-fault-inject-tag' } }");
    g_assert_cmpuint(qdict_get_int(rsp, "return"), ==,
                     UFS_FAULT_INJECT_TAG_NONE);
    qobject_unref(rsp);

    /* Verify setting tag now succeeds (not blocked by held_req) */
    qtest_qmp_assert_success(
        qts,
        "{ 'execute': 'qom-set', 'arguments': { "
        "'path': '/machine/peripheral/ufs0', "
        "'property': 'x-fault-inject-tag', 'value': 7 } }");
    qtest_qmp_assert_success(
        qts,
        "{ 'execute': 'qom-set', 'arguments': { "
        "'path': '/machine/peripheral/ufs0', "
        "'property': 'x-fault-inject-tag', 'value': %u } }",
        UFS_FAULT_INJECT_TAG_NONE);

    cqtp = ufs_rreg(ufs, cqdao + 0x4);
    ufs_wreg(ufs, cqdao, cqtp);
    ufs_wreg(ufs, cqisao + A_CQIS, FIELD_DP32(0, CQIS, TEPS, 1));
    ufs_wreg(ufs, A_IS, FIELD_DP32(0, IS, CQES, 1));
    ufs_wreg(ufs, sqdao + A_SQRTC, 0);
    release_cmd_desc_slot(ufs, slot);

    /*
     * Case 2: x-fault-inject-mode = in-transition.
     * WRITE a pattern while held -> QUERY TASK (in-transition) releases
     * and really executes the request -> READ back shows the pattern.
     */
    memset(write_buf, 0x5a, sizeof(write_buf));
    memset(read_buf, 0, sizeof(read_buf));

    /* Clear Unit Attention on LUN 1 before I/O */
    ufs_send_scsi_command(ufs, 1, request_sense_cdb, NULL, 0, read_buf,
                          sizeof(read_buf), &read_rsp);

    slot = find_first_zero_bit(ufs->cmd_desc_bitmap, UFS_MAX_CMD_DESC);
    qtest_qmp_assert_success(
        qts,
        "{ 'execute': 'qom-set', 'arguments': { "
        "'path': '/machine/peripheral/ufs0', "
        "'property': 'x-fault-inject-mode', 'value': 'in-transition' } }");
    qtest_qmp_assert_success(
        qts,
        "{ 'execute': 'qom-set', 'arguments': { "
        "'path': '/machine/peripheral/ufs0', "
        "'property': 'x-fault-inject-tag', 'value': %d } }", slot);

    cqhp = ufs_rreg(ufs, cqdao);
    slot = ufs_mcq_send_write_async(ufs, 1, write10_cdb, write_buf,
                                    sizeof(write_buf));
    qtest_clock_step(qts, 100);

    /* Command is held; no CQE posted yet */
    g_assert_cmpuint(ufs_rreg(ufs, cqdao + 0x4), ==, cqhp);

    /* QUERY TASK releases the held request and lets it actually execute */
    ufs_send_tmr(ufs, utmrlba, 0, UFS_QUERY_TASK, 1, slot, &utrd_result);
    g_assert_cmpuint(utrd_result.upiu_rsp.rsp_header.response,
                     ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(be32_to_cpu(utrd_result.upiu_rsp.output_param1),
                     ==, UFS_UPIU_TASK_MANAGEMENT_FUNC_SUCCEEDED);

    /* Wait for the released request to finish and post its CQE */
    ufs_wait_for_irq(ufs);
    g_assert_true(FIELD_EX32(ufs_rreg(ufs, A_IS), IS, CQES));

    /* Verify completion CQE was posted with real status (SUCCESS) */
    cqtp = ufs_rreg(ufs, cqdao + 0x4);
    g_assert_cmpuint(cqtp, !=, cqhp);
    qtest_memread(qts, ufs->cqlba[TEST_QID] + cqhp, &cqe, sizeof(cqe));
    g_assert_cmpuint(cqe.status, ==, UFS_OCS_SUCCESS);
    ufs_wreg(ufs, cqdao, cqtp);
    ufs_wreg(ufs, cqisao + A_CQIS, FIELD_DP32(0, CQIS, TEPS, 1));
    ufs_wreg(ufs, A_IS, FIELD_DP32(0, IS, CQES, 1));
    release_cmd_desc_slot(ufs, slot);

    /* READ back LBA 512 and verify the pattern actually reached the LU */
    ocs = ufs_send_scsi_command(ufs, 1, read10_cdb, NULL, 0, read_buf,
                                sizeof(read_buf), &read_rsp);
    g_assert_cmpuint(ocs, ==, UFS_OCS_SUCCESS);
    g_assert_cmpmem(read_buf, sizeof(read_buf), write_buf, sizeof(write_buf));

    /*
     * Case 3: x-fault-inject-mode = timeout + A_UTMRLCLR clear-on-zero.
     * UFS_ABORT_TASK times out, leaving UTMRLDBR bit 2 set.
     *
     * UFSHCI defines UTMRLCLR as write-0-to-clear: writing 0 to a bit clears
     * that slot in UTMRLDBR, writing 1 leaves it unchanged.
     */
    slot = find_first_zero_bit(ufs->cmd_desc_bitmap, UFS_MAX_CMD_DESC);
    qtest_qmp_assert_success(
        qts,
        "{ 'execute': 'qom-set', 'arguments': { "
        "'path': '/machine/peripheral/ufs0', "
        "'property': 'x-fault-inject-mode', 'value': 'timeout' } }");
    qtest_qmp_assert_success(
        qts,
        "{ 'execute': 'qom-set', 'arguments': { "
        "'path': '/machine/peripheral/ufs0', "
        "'property': 'x-fault-inject-tag', 'value': %d } }", slot);

    slot = ufs_mcq_send_scsi_cmd_async(ufs, 0, test_unit_ready_cdb);
    qtest_clock_step(qts, 100);

    memset(&utrd, 0, sizeof(utrd));
    utrd.header.dword_0 = cpu_to_le32(UFS_UTP_REQ_DESC_INT_CMD);
    utrd.header.dword_2 = cpu_to_le32(UFS_OCS_INVALID_COMMAND_STATUS);
    utrd.upiu_req.req_header.trans_type = UFS_UPIU_TRANSACTION_TASK_REQ;
    utrd.upiu_req.req_header.query_func = UFS_ABORT_TASK;
    utrd.upiu_req.req_header.lun = 0;
    utrd.upiu_req.req_header.task_tag = 2;
    utrd.upiu_req.input_param1 = cpu_to_be32(0);
    utrd.upiu_req.input_param2 = cpu_to_be32(slot);
    desc_addr = utmrlba + 2 * sizeof(UtpTaskReqDesc);
    qtest_memwrite(qts, desc_addr, &utrd, sizeof(utrd));

    ufs_wreg(ufs, A_UTMRLDBR, 1 << 2);
    g_assert_cmpuint(ufs_rreg(ufs, A_UTMRLDBR) & (1 << 2), ==, 1 << 2);

    /*
     * Writing 1s to A_UTMRLCLR indicates no change per UFSHCI spec,
     * so it must NOT clear UTMRLDBR bit 2.
     */
    ufs_wreg(ufs, A_UTMRLCLR, 0xffffffffU);
    g_assert_cmpuint(ufs_rreg(ufs, A_UTMRLDBR) & (1 << 2), ==, 1 << 2);

    /*
     * Writing 0 at bit 2 (~(1 << 2)) to A_UTMRLCLR indicates slot cleared,
     * so it MUST clear bit 2.
     */
    ufs_wreg(ufs, A_UTMRLCLR, ~(1U << 2));
    g_assert_cmpuint(ufs_rreg(ufs, A_UTMRLDBR) & (1 << 2), ==, 0);

    ufs_wreg(ufs, A_HCE, 0);
    release_cmd_desc_slot(ufs, slot);
    ufs_exit(ufs, alloc);
    ufs_init(ufs, alloc);
    ufs_wreg(ufs, A_UTMRLBA, utmrlba & 0xffffffff);
    ufs_wreg(ufs, A_UTMRLBAU, utmrlba >> 32);
    ufs_wreg(ufs, A_UTMRLRSR, 1);

    /*
     * Case 4: x-fault-inject-mode = abort-failed.
     */
    slot = find_first_zero_bit(ufs->cmd_desc_bitmap, UFS_MAX_CMD_DESC);
    qtest_qmp_assert_success(
        qts,
        "{ 'execute': 'qom-set', 'arguments': { "
        "'path': '/machine/peripheral/ufs0', "
        "'property': 'x-fault-inject-mode', 'value': 'abort-failed' } }");
    qtest_qmp_assert_success(
        qts,
        "{ 'execute': 'qom-set', 'arguments': { "
        "'path': '/machine/peripheral/ufs0', "
        "'property': 'x-fault-inject-tag', 'value': %d } }", slot);

    slot = ufs_mcq_send_scsi_cmd_async(ufs, 0, test_unit_ready_cdb);
    qtest_clock_step(qts, 100);

    /* Issue UFS_QUERY_TASK -> assert Target Success & SUCCEEDED (0x08) */
    ufs_send_tmr(ufs, utmrlba, 0, UFS_QUERY_TASK, 0, slot, &utrd_result);
    g_assert_cmpuint(utrd_result.upiu_rsp.rsp_header.response,
                     ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(be32_to_cpu(utrd_result.upiu_rsp.output_param1),
                     ==, UFS_UPIU_TASK_MANAGEMENT_FUNC_SUCCEEDED);

    /* Issue UFS_ABORT_TASK -> assert Target Failure & FAILED (0x05) */
    ufs_send_tmr(ufs, utmrlba, 0, UFS_ABORT_TASK, 0, slot, &utrd_result);
    g_assert_cmpuint(utrd_result.upiu_rsp.rsp_header.response,
                     ==, UFS_COMMAND_RESULT_FAIL);
    g_assert_cmpuint(be32_to_cpu(utrd_result.upiu_rsp.output_param1),
                     ==, UFS_UPIU_TASK_MANAGEMENT_FUNC_FAILED);

    /* Reset controller to clear held request cleanly */
    ufs_wreg(ufs, A_HCE, 0);
    release_cmd_desc_slot(ufs, slot);
    ufs_exit(ufs, alloc);
    ufs_init(ufs, alloc);
    ufs_wreg(ufs, A_UTMRLBA, utmrlba & 0xffffffff);
    ufs_wreg(ufs, A_UTMRLBAU, utmrlba >> 32);
    ufs_wreg(ufs, A_UTMRLRSR, 1);

    /*
     * Case 5: x-fault-inject-mode = ssu-timeout.
     * Non-SSU commands on LU 0 complete normally even with a matching
     * specific tag or UFS_FAULT_INJECT_TAG_ANY, whereas START_STOP to
     * UFS_UPIU_UFS_DEVICE_WLUN is held.
     */
    slot = find_first_zero_bit(ufs->cmd_desc_bitmap, UFS_MAX_CMD_DESC);
    qtest_qmp_assert_success(
        qts,
        "{ 'execute': 'qom-set', 'arguments': { "
        "'path': '/machine/peripheral/ufs0', "
        "'property': 'x-fault-inject-mode', 'value': 'ssu-timeout' } }");
    qtest_qmp_assert_success(
        qts,
        "{ 'execute': 'qom-set', 'arguments': { "
        "'path': '/machine/peripheral/ufs0', "
        "'property': 'x-fault-inject-tag', 'value': %d } }", slot);

    cqhp = ufs_rreg(ufs, cqdao);
    slot = ufs_mcq_send_scsi_cmd_async(ufs, 0, test_unit_ready_cdb);
    qtest_clock_step(qts, 100);
    cqtp = ufs_rreg(ufs, cqdao + 0x4);
    g_assert_cmpuint(cqtp, !=, cqhp);
    qtest_memread(qts, ufs->cqlba[TEST_QID] + cqhp, &cqe, sizeof(cqe));
    g_assert_cmpuint(cqe.status, ==, UFS_OCS_SUCCESS);
    ufs_wreg(ufs, cqdao, cqtp);
    ufs_wreg(ufs, cqisao + A_CQIS, FIELD_DP32(0, CQIS, TEPS, 1));
    ufs_wreg(ufs, A_IS, FIELD_DP32(0, IS, CQES, 1));
    release_cmd_desc_slot(ufs, slot);

    qtest_qmp_assert_success(
        qts,
        "{ 'execute': 'qom-set', 'arguments': { "
        "'path': '/machine/peripheral/ufs0', "
        "'property': 'x-fault-inject-tag', 'value': %u } }",
        UFS_FAULT_INJECT_TAG_ANY);

    cqhp = ufs_rreg(ufs, cqdao);
    slot = ufs_mcq_send_scsi_cmd_async(ufs, 0, read10_lba512_cdb);
    qtest_clock_step(qts, 100);
    cqtp = ufs_rreg(ufs, cqdao + 0x4);
    g_assert_cmpuint(cqtp, !=, cqhp);
    qtest_memread(qts, ufs->cqlba[TEST_QID] + cqhp, &cqe, sizeof(cqe));
    g_assert_cmpuint(cqe.status, ==, UFS_OCS_SUCCESS);
    ufs_wreg(ufs, cqdao, cqtp);
    ufs_wreg(ufs, cqisao + A_CQIS, FIELD_DP32(0, CQIS, TEPS, 1));
    ufs_wreg(ufs, A_IS, FIELD_DP32(0, IS, CQES, 1));
    release_cmd_desc_slot(ufs, slot);

    cqhp = ufs_rreg(ufs, cqdao);
    slot = ufs_mcq_send_scsi_cmd_async(ufs, UFS_UPIU_UFS_DEVICE_WLUN,
                                       start_stop_cdb);
    qtest_clock_step(qts, 100);
    cqtp = ufs_rreg(ufs, cqdao + 0x4);
    g_assert_cmpuint(cqtp, ==, cqhp);

    ufs_send_tmr(ufs, utmrlba, 0, UFS_QUERY_TASK, UFS_UPIU_UFS_DEVICE_WLUN,
                 slot, &utrd_result);
    g_assert_cmpuint(utrd_result.upiu_rsp.rsp_header.response,
                     ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(be32_to_cpu(utrd_result.upiu_rsp.output_param1),
                     ==, UFS_UPIU_TASK_MANAGEMENT_FUNC_SUCCEEDED);

    ufs_wreg(ufs, A_HCE, 0);
    release_cmd_desc_slot(ufs, slot);
    ufs_exit(ufs, alloc);
    ufs_init(ufs, alloc);
    ufs_wreg(ufs, A_UTMRLBA, utmrlba & 0xffffffff);
    ufs_wreg(ufs, A_UTMRLBAU, utmrlba >> 32);
    ufs_wreg(ufs, A_UTMRLRSR, 1);

    /*
     * Case 6: x-fault-inject-tag = ANY with READ_10 and READ_16.
     * Commands with LBA < 512 must complete normally (not held).
     * Commands with LBA >= 512 must be held.
     */
    qtest_qmp_assert_success(
        qts,
        "{ 'execute': 'qom-set', 'arguments': { "
        "'path': '/machine/peripheral/ufs0', "
        "'property': 'x-fault-inject-mode', 'value': 'abort-success' } }");
    qtest_qmp_assert_success(
        qts,
        "{ 'execute': 'qom-set', 'arguments': { "
        "'path': '/machine/peripheral/ufs0', "
        "'property': 'x-fault-inject-tag', 'value': %u } }",
        UFS_FAULT_INJECT_TAG_ANY);

    /* (a) READ_10 LBA 0 (< 512) -> completes normally, NOT held */
    cqhp = ufs_rreg(ufs, cqdao);
    slot = ufs_mcq_send_scsi_cmd_async(ufs, 0, read10_lba0_cdb);
    qtest_clock_step(qts, 100);
    cqtp = ufs_rreg(ufs, cqdao + 0x4);
    g_assert_cmpuint(cqtp, !=, cqhp);
    qtest_memread(qts, ufs->cqlba[TEST_QID] + cqhp, &cqe, sizeof(cqe));
    g_assert_cmpuint(cqe.status, ==, UFS_OCS_SUCCESS);
    ufs_wreg(ufs, cqdao, cqtp);
    ufs_wreg(ufs, cqisao + A_CQIS, FIELD_DP32(0, CQIS, TEPS, 1));
    ufs_wreg(ufs, A_IS, FIELD_DP32(0, IS, CQES, 1));
    release_cmd_desc_slot(ufs, slot);

    rsp = qtest_qmp(
        qts,
        "{ 'execute': 'qom-get', 'arguments': { "
        "'path': '/machine/peripheral/ufs0', "
        "'property': 'x-fault-inject-tag' } }");
    g_assert_cmpuint(qdict_get_int(rsp, "return"), ==,
                     UFS_FAULT_INJECT_TAG_ANY);
    qobject_unref(rsp);

    /* (b) READ_10 LBA 512 (>= 512) -> HELD */
    cqhp = ufs_rreg(ufs, cqdao);
    slot = ufs_mcq_send_scsi_cmd_async(ufs, 0, read10_lba512_cdb);
    qtest_clock_step(qts, 100);
    g_assert_cmpuint(ufs_rreg(ufs, cqdao + 0x4), ==, cqhp);

    rsp = qtest_qmp(
        qts,
        "{ 'execute': 'qom-get', 'arguments': { "
        "'path': '/machine/peripheral/ufs0', "
        "'property': 'x-fault-inject-tag' } }");
    g_assert_cmpuint(qdict_get_int(rsp, "return"), ==, slot);
    qobject_unref(rsp);

    /* Abort the held READ_10 */
    ufs_send_tmr(ufs, utmrlba, 0, UFS_ABORT_TASK, 0, slot, &utrd_result);
    g_assert_cmpuint(utrd_result.upiu_rsp.rsp_header.response,
                     ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(be32_to_cpu(utrd_result.upiu_rsp.output_param1),
                     ==, UFS_UPIU_TASK_MANAGEMENT_FUNC_COMPL);
    release_cmd_desc_slot(ufs, slot);

    /* Re-arm ANY for 16-byte testing */
    qtest_qmp_assert_success(
        qts,
        "{ 'execute': 'qom-set', 'arguments': { "
        "'path': '/machine/peripheral/ufs0', "
        "'property': 'x-fault-inject-tag', 'value': %u } }",
        UFS_FAULT_INJECT_TAG_ANY);

    /* (c) READ_16 LBA 0 (< 512) -> completes normally, NOT held */
    cqhp = ufs_rreg(ufs, cqdao);
    slot = ufs_mcq_send_scsi_cmd_async(ufs, 0, read16_lba0_cdb);
    qtest_clock_step(qts, 100);
    cqtp = ufs_rreg(ufs, cqdao + 0x4);
    g_assert_cmpuint(cqtp, !=, cqhp);
    qtest_memread(qts, ufs->cqlba[TEST_QID] + cqhp, &cqe, sizeof(cqe));
    g_assert_cmpuint(cqe.status, ==, UFS_OCS_SUCCESS);
    ufs_wreg(ufs, cqdao, cqtp);
    ufs_wreg(ufs, cqisao + A_CQIS, FIELD_DP32(0, CQIS, TEPS, 1));
    ufs_wreg(ufs, A_IS, FIELD_DP32(0, IS, CQES, 1));
    release_cmd_desc_slot(ufs, slot);

    rsp = qtest_qmp(
        qts,
        "{ 'execute': 'qom-get', 'arguments': { "
        "'path': '/machine/peripheral/ufs0', "
        "'property': 'x-fault-inject-tag' } }");
    g_assert_cmpuint(qdict_get_int(rsp, "return"), ==,
                     UFS_FAULT_INJECT_TAG_ANY);
    qobject_unref(rsp);

    /* (d) READ_16 LBA 512 (>= 512) -> HELD */
    cqhp = ufs_rreg(ufs, cqdao);
    slot = ufs_mcq_send_scsi_cmd_async(ufs, 0, read16_lba512_cdb);
    qtest_clock_step(qts, 100);
    g_assert_cmpuint(ufs_rreg(ufs, cqdao + 0x4), ==, cqhp);

    rsp = qtest_qmp(
        qts,
        "{ 'execute': 'qom-get', 'arguments': { "
        "'path': '/machine/peripheral/ufs0', "
        "'property': 'x-fault-inject-tag' } }");
    g_assert_cmpuint(qdict_get_int(rsp, "return"), ==, slot);
    qobject_unref(rsp);

    /* Abort the held READ_16 */
    ufs_send_tmr(ufs, utmrlba, 0, UFS_ABORT_TASK, 0, slot, &utrd_result);
    g_assert_cmpuint(utrd_result.upiu_rsp.rsp_header.response,
                     ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(be32_to_cpu(utrd_result.upiu_rsp.output_param1),
                     ==, UFS_UPIU_TASK_MANAGEMENT_FUNC_COMPL);
    release_cmd_desc_slot(ufs, slot);

    ufs_wreg(ufs, A_HCE, 0);
    guest_free(alloc, utmrlba);
    ufs_exit(ufs, alloc);
}

static void ufs_test_fault_injection_legacy(void *obj, void *data,
                                            QGuestAllocator *alloc)
{
    QUfs *ufs = obj;
    QTestState *qts = ufs->dev.bus->qts;
    uint64_t utmrlba;
    UtpTaskReqDesc utrd_result;
    const uint8_t test_unit_ready_cdb[UFS_CDB_SIZE] = { TEST_UNIT_READY };
    int slot;
    QDict *rsp;

    ufs_init(ufs, alloc);
    g_assert_false(ufs->support_mcq);

    utmrlba = guest_alloc(alloc, 8 * sizeof(UtpTaskReqDesc));
    ufs_wreg(ufs, A_UTMRLBA, utmrlba & 0xffffffff);
    ufs_wreg(ufs, A_UTMRLBAU, utmrlba >> 32);
    ufs_wreg(ufs, A_UTMRLRSR, 1);

    slot = find_first_zero_bit(ufs->cmd_desc_bitmap, UFS_MAX_CMD_DESC);
    qtest_qmp_assert_success(
        qts,
        "{ 'execute': 'qom-set', 'arguments': { "
        "'path': '/machine/peripheral/ufs0', "
        "'property': 'x-fault-inject-mode', 'value': 'abort-success' } }");
    qtest_qmp_assert_success(
        qts,
        "{ 'execute': 'qom-set', 'arguments': { "
        "'path': '/machine/peripheral/ufs0', "
        "'property': 'x-fault-inject-tag', 'value': %d } }", slot);

    slot = ufs_send_scsi_cmd_doorbell_async(ufs, 0, test_unit_ready_cdb);
    qtest_clock_step(qts, 100);

    /* Assert UTRLDBR bit is set while command is held */
    g_assert_cmpuint(ufs_rreg(ufs, A_UTRLDBR) & (1 << slot), ==, 1 << slot);

    /* Issue UFS_ABORT_TASK -> clears UTRLDBR bit and returns COMPL */
    ufs_send_tmr(ufs, utmrlba, 0, UFS_ABORT_TASK, 0, slot, &utrd_result);
    g_assert_cmpuint(utrd_result.upiu_rsp.rsp_header.response,
                     ==, UFS_COMMAND_RESULT_SUCCESS);
    g_assert_cmpuint(be32_to_cpu(utrd_result.upiu_rsp.output_param1),
                     ==, UFS_UPIU_TASK_MANAGEMENT_FUNC_COMPL);

    /* Verify UTRLDBR bit was cleared by the abort */
    g_assert_cmpuint(ufs_rreg(ufs, A_UTRLDBR) & (1 << slot), ==, 0);

    rsp = qtest_qmp(
        qts,
        "{ 'execute': 'qom-get', 'arguments': { "
        "'path': '/machine/peripheral/ufs0', "
        "'property': 'x-fault-inject-tag' } }");
    g_assert_cmpuint(qdict_get_int(rsp, "return"), ==,
                     UFS_FAULT_INJECT_TAG_NONE);
    qobject_unref(rsp);

    release_cmd_desc_slot(ufs, slot);
    guest_free(alloc, utmrlba);
    ufs_exit(ufs, alloc);
}

static void ufs_register_nodes(void)
{
    const char *arch;
    QOSGraphEdgeOptions edge_opts = {
        .before_cmd_line = "-blockdev null-co,node-name=drv0,read-zeroes=on",
        .after_cmd_line = "-device ufs-lu,bus=ufs0,drive=drv0,lun=0",
        .extra_device_opts = "addr=04.0,id=ufs0"
    };

    QOSGraphTestOptions io_test_opts = { .before = ufs_blk_test_setup,
                                         .edge.extra_device_opts =
                                             "mcq=false,nutrs=32,nutmrs=8" };

    QOSGraphTestOptions mcq_test_opts = { .before = ufs_blk_test_setup,
                                          .edge.extra_device_opts =
                                              "mcq=true,mcq-maxq=1" };

    QOSGraphTestOptions wb_test_opts = { .before = ufs_blk_test_setup,
                                         .edge.extra_device_opts =
                                             "mcq=false,nutrs=32,nutmrs=8,"
                                             "wb-max-size=1024,"
                                             "wb-min-size=256" };
    QOSGraphTestOptions acpi_eject_test_opts = { .subprocess = true };

    add_qpci_address(&edge_opts, &(QPCIAddress){ .devfn = QPCI_DEVFN(4, 0) });

    qos_node_create_driver("ufs", ufs_create);
    qos_node_consumes("ufs", "pci-bus", &edge_opts);
    qos_node_produces("ufs", "pci-device");

    qos_add_test("reg-read", "ufs", ufstest_reg_read, NULL);

    /*
     * Check architecture
     * TODO: Enable ufs io tests for ppc64
     */
    arch = qtest_get_arch();
    if (!strcmp(arch, "ppc64")) {
        g_test_message("Skipping ufs io tests for ppc64");
        return;
    }
    if (!strcmp(arch, "i386") || !strcmp(arch, "x86_64")) {
        qos_add_test("acpi-eject", "ufs", ufstest_acpi_eject,
                     &acpi_eject_test_opts);
    }
    qos_add_test("init", "ufs", ufstest_init, NULL);
    qos_add_test("legacy-read-write", "ufs", ufstest_read_write, &io_test_opts);
    qos_add_test("mcq-read-write", "ufs", ufstest_read_write, &mcq_test_opts);
    qos_add_test("no-prdt", "ufs", ufstest_no_prdt, &io_test_opts);
    qos_add_test("sync-cache", "ufs", ufstest_sync_cache, &io_test_opts);
    qos_add_test("long-prdt", "ufs", ufstest_long_prdt, &io_test_opts);
    qos_add_test("short-prdt", "ufs", ufstest_short_prdt, &io_test_opts);
    qos_add_test("mcq-cq-wraparound", "ufs", ufstest_mcq_cq_wraparound,
                 &mcq_test_opts);
    qos_add_test("query-flag", "ufs", ufstest_query_flag_request,
                 &io_test_opts);
    qos_add_test("query-attribute", "ufs", ufstest_query_attr_request,
                 &io_test_opts);
    qos_add_test("query-desciptor", "ufs", ufstest_query_desc_request,
                 &io_test_opts);
    qos_add_test("hce-reset", "ufs", ufs_test_hce_reset, &mcq_test_opts);
    qos_add_test("tmr-handling", "ufs", ufs_test_tmr_handling, &mcq_test_opts);
    qos_add_test("fault-injection-abort", "ufs",
                 ufs_test_fault_injection_abort, &mcq_test_opts);
    qos_add_test("fault-injection-legacy", "ufs",
                 ufs_test_fault_injection_legacy, &io_test_opts);
    qos_add_test("wb-init", "ufs", ufstest_wb_init, &wb_test_opts);
    qos_add_test("wb-read-write", "ufs", ufstest_wb_read_write, &wb_test_opts);
    qos_add_test("hid", "ufs", ufstest_hid, &io_test_opts);
}

libqos_init(ufs_register_nodes);
