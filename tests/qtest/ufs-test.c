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
    qos_add_test("init", "ufs", ufstest_init, NULL);
    qos_add_test("legacy-read-write", "ufs", ufstest_read_write, &io_test_opts);
    qos_add_test("mcq-read-write", "ufs", ufstest_read_write, &mcq_test_opts);
    qos_add_test("query-flag", "ufs", ufstest_query_flag_request,
                 &io_test_opts);
    qos_add_test("query-attribute", "ufs", ufstest_query_attr_request,
                 &io_test_opts);
    qos_add_test("query-desciptor", "ufs", ufstest_query_desc_request,
                 &io_test_opts);
}

libqos_init(ufs_register_nodes);
