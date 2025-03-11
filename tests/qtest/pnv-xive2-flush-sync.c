/*
 * QTest testcase for PowerNV 10 interrupt controller (xive2)
 *  - Test cache flush/queue sync injection
 *
 * Copyright (c) 2024, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "libqtest.h"

#include "pnv-xive2-common.h"
#include "hw/intc/pnv_xive2_regs.h"
#include "hw/ppc/xive_regs.h"
#include "hw/ppc/xive2_regs.h"

#define PNV_XIVE2_QUEUE_IPI              0x00
#define PNV_XIVE2_QUEUE_HW               0x01
#define PNV_XIVE2_QUEUE_NXC              0x02
#define PNV_XIVE2_QUEUE_INT              0x03
#define PNV_XIVE2_QUEUE_OS               0x04
#define PNV_XIVE2_QUEUE_POOL             0x05
#define PNV_XIVE2_QUEUE_HARD             0x06
#define PNV_XIVE2_CACHE_ENDC             0x08
#define PNV_XIVE2_CACHE_ESBC             0x09
#define PNV_XIVE2_CACHE_EASC             0x0a
#define PNV_XIVE2_QUEUE_NXC_LD_LCL_NCO   0x10
#define PNV_XIVE2_QUEUE_NXC_LD_LCL_CO    0x11
#define PNV_XIVE2_QUEUE_NXC_ST_LCL_NCI   0x12
#define PNV_XIVE2_QUEUE_NXC_ST_LCL_CI    0x13
#define PNV_XIVE2_QUEUE_NXC_ST_RMT_NCI   0x14
#define PNV_XIVE2_QUEUE_NXC_ST_RMT_CI    0x15
#define PNV_XIVE2_CACHE_NXC              0x18

#define PNV_XIVE2_SYNC_IPI              0x000
#define PNV_XIVE2_SYNC_HW               0x080
#define PNV_XIVE2_SYNC_NxC              0x100
#define PNV_XIVE2_SYNC_INT              0x180
#define PNV_XIVE2_SYNC_OS_ESC           0x200
#define PNV_XIVE2_SYNC_POOL_ESC         0x280
#define PNV_XIVE2_SYNC_HARD_ESC         0x300
#define PNV_XIVE2_SYNC_NXC_LD_LCL_NCO   0x800
#define PNV_XIVE2_SYNC_NXC_LD_LCL_CO    0x880
#define PNV_XIVE2_SYNC_NXC_ST_LCL_NCI   0x900
#define PNV_XIVE2_SYNC_NXC_ST_LCL_CI    0x980
#define PNV_XIVE2_SYNC_NXC_ST_RMT_NCI   0xA00
#define PNV_XIVE2_SYNC_NXC_ST_RMT_CI    0xA80


static uint64_t get_sync_addr(uint32_t src_pir, int ic_topo_id, int type)
{
    int thread_nr = src_pir & 0x7f;
    uint64_t addr = XIVE_SYNC_MEM +  thread_nr * 512 + ic_topo_id * 32 + type;
    return addr;
}

static uint8_t get_sync(QTestState *qts, uint32_t src_pir, int ic_topo_id,
                        int type)
{
    uint64_t addr = get_sync_addr(src_pir, ic_topo_id, type);
    return qtest_readb(qts, addr);
}

static void clr_sync(QTestState *qts, uint32_t src_pir, int ic_topo_id,
                        int type)
{
    uint64_t addr = get_sync_addr(src_pir, ic_topo_id, type);
    qtest_writeb(qts, addr, 0x0);
}

static void inject_cache_flush(QTestState *qts, int ic_topo_id,
                               uint64_t scom_addr)
{
    (void)ic_topo_id;
    pnv_xive_xscom_write(qts, scom_addr, 0);
}

static void inject_queue_sync(QTestState *qts, int ic_topo_id, uint64_t offset)
{
    (void)ic_topo_id;
    uint64_t addr = XIVE_IC_ADDR + (VST_SYNC << XIVE_PAGE_SHIFT) + offset;
    qtest_writeq(qts, addr, 0);
}

static void inject_op(QTestState *qts, int ic_topo_id, int type)
{
    switch (type) {
    case PNV_XIVE2_QUEUE_IPI:
        inject_queue_sync(qts, ic_topo_id, PNV_XIVE2_SYNC_IPI);
        break;
    case PNV_XIVE2_QUEUE_HW:
        inject_queue_sync(qts, ic_topo_id, PNV_XIVE2_SYNC_HW);
        break;
    case PNV_XIVE2_QUEUE_NXC:
        inject_queue_sync(qts, ic_topo_id, PNV_XIVE2_SYNC_NxC);
        break;
    case PNV_XIVE2_QUEUE_INT:
        inject_queue_sync(qts, ic_topo_id, PNV_XIVE2_SYNC_INT);
        break;
    case PNV_XIVE2_QUEUE_OS:
        inject_queue_sync(qts, ic_topo_id, PNV_XIVE2_SYNC_OS_ESC);
        break;
    case PNV_XIVE2_QUEUE_POOL:
        inject_queue_sync(qts, ic_topo_id, PNV_XIVE2_SYNC_POOL_ESC);
        break;
    case PNV_XIVE2_QUEUE_HARD:
        inject_queue_sync(qts, ic_topo_id, PNV_XIVE2_SYNC_HARD_ESC);
        break;
    case PNV_XIVE2_CACHE_ENDC:
        inject_cache_flush(qts, ic_topo_id, X_VC_ENDC_FLUSH_INJECT);
        break;
    case PNV_XIVE2_CACHE_ESBC:
        inject_cache_flush(qts, ic_topo_id, X_VC_ESBC_FLUSH_INJECT);
        break;
    case PNV_XIVE2_CACHE_EASC:
        inject_cache_flush(qts, ic_topo_id, X_VC_EASC_FLUSH_INJECT);
        break;
    case PNV_XIVE2_QUEUE_NXC_LD_LCL_NCO:
        inject_queue_sync(qts, ic_topo_id, PNV_XIVE2_SYNC_NXC_LD_LCL_NCO);
        break;
    case PNV_XIVE2_QUEUE_NXC_LD_LCL_CO:
        inject_queue_sync(qts, ic_topo_id, PNV_XIVE2_SYNC_NXC_LD_LCL_CO);
        break;
    case PNV_XIVE2_QUEUE_NXC_ST_LCL_NCI:
        inject_queue_sync(qts, ic_topo_id, PNV_XIVE2_SYNC_NXC_ST_LCL_NCI);
        break;
    case PNV_XIVE2_QUEUE_NXC_ST_LCL_CI:
        inject_queue_sync(qts, ic_topo_id, PNV_XIVE2_SYNC_NXC_ST_LCL_CI);
        break;
    case PNV_XIVE2_QUEUE_NXC_ST_RMT_NCI:
        inject_queue_sync(qts, ic_topo_id, PNV_XIVE2_SYNC_NXC_ST_RMT_NCI);
        break;
    case PNV_XIVE2_QUEUE_NXC_ST_RMT_CI:
        inject_queue_sync(qts, ic_topo_id, PNV_XIVE2_SYNC_NXC_ST_RMT_CI);
        break;
    case PNV_XIVE2_CACHE_NXC:
        inject_cache_flush(qts, ic_topo_id, X_PC_NXC_FLUSH_INJECT);
        break;
    default:
        g_assert_not_reached();
        break;
    }
}

const uint8_t xive_inject_tests[] = {
    PNV_XIVE2_QUEUE_IPI,
    PNV_XIVE2_QUEUE_HW,
    PNV_XIVE2_QUEUE_NXC,
    PNV_XIVE2_QUEUE_INT,
    PNV_XIVE2_QUEUE_OS,
    PNV_XIVE2_QUEUE_POOL,
    PNV_XIVE2_QUEUE_HARD,
    PNV_XIVE2_CACHE_ENDC,
    PNV_XIVE2_CACHE_ESBC,
    PNV_XIVE2_CACHE_EASC,
    PNV_XIVE2_QUEUE_NXC_LD_LCL_NCO,
    PNV_XIVE2_QUEUE_NXC_LD_LCL_CO,
    PNV_XIVE2_QUEUE_NXC_ST_LCL_NCI,
    PNV_XIVE2_QUEUE_NXC_ST_LCL_CI,
    PNV_XIVE2_QUEUE_NXC_ST_RMT_NCI,
    PNV_XIVE2_QUEUE_NXC_ST_RMT_CI,
    PNV_XIVE2_CACHE_NXC,
};

void test_flush_sync_inject(QTestState *qts)
{
    int ic_topo_id = 0;

    /*
     * Writes performed by qtest are not done in the context of a thread.
     * This means that QEMU XIVE code doesn't have a way to determine what
     * thread is originating the write.  In order to allow for some testing,
     * QEMU XIVE code will assume a PIR of 0 when unable to determine the
     * source thread for cache flush and queue sync inject operations.
     * See hw/intc/pnv_xive2.c: pnv_xive2_inject_notify() for details.
     */
    int src_pir = 0;
    int test_nr;
    uint8_t byte;

    g_test_message("=========================================================");
    g_test_message("Starting cache flush/queue sync injection tests...");

    for (test_nr = 0; test_nr < sizeof(xive_inject_tests);
         test_nr++) {
        int op_type = xive_inject_tests[test_nr];

        g_test_message("Running test %d", test_nr);

        /* start with status byte set to 0 */
        clr_sync(qts, src_pir, ic_topo_id, op_type);
        byte = get_sync(qts, src_pir, ic_topo_id, op_type);
        g_assert_cmphex(byte, ==, 0);

        /* request cache flush or queue sync operation */
        inject_op(qts, ic_topo_id, op_type);

        /* verify that status byte was written to 0xff */
        byte = get_sync(qts, src_pir, ic_topo_id, op_type);
        g_assert_cmphex(byte, ==, 0xff);

        clr_sync(qts, src_pir, ic_topo_id, op_type);
    }
}

