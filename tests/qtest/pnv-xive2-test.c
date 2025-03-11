/*
 * QTest testcase for PowerNV 10 interrupt controller (xive2)
 *  - Test irq to hardware thread
 *  - Test 'Pull Thread Context to Odd Thread Reporting Line'
 *  - Test irq to hardware group
 *  - Test irq to hardware group going through backlog
 *  - Test irq to pool thread
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

#define SMT                     4 /* some tests will break if less than 4 */


static void set_table(QTestState *qts, uint64_t type, uint64_t addr)
{
    uint64_t vsd, size, log_size;

    /*
     * First, let's make sure that all the resources used fit in the
     * given table.
     */
    switch (type) {
    case VST_ESB:
        size = MAX_IRQS / 4;
        break;
    case VST_EAS:
        size = MAX_IRQS * 8;
        break;
    case VST_END:
        size = MAX_ENDS * 32;
        break;
    case VST_NVP:
    case VST_NVG:
    case VST_NVC:
        size = MAX_VPS * 32;
        break;
    case VST_SYNC:
        size = 64 * 1024;
        break;
    default:
        g_assert_not_reached();
    }

    g_assert_cmpuint(size, <=, XIVE_VST_SIZE);
    log_size = ctzl(XIVE_VST_SIZE) - 12;

    vsd = ((uint64_t) VSD_MODE_EXCLUSIVE) << 62 | addr | log_size;
    pnv_xive_xscom_write(qts, X_VC_VSD_TABLE_ADDR, type << 48);
    pnv_xive_xscom_write(qts, X_VC_VSD_TABLE_DATA, vsd);

    if (type != VST_EAS && type != VST_IC && type != VST_ERQ) {
        pnv_xive_xscom_write(qts, X_PC_VSD_TABLE_ADDR, type << 48);
        pnv_xive_xscom_write(qts, X_PC_VSD_TABLE_DATA, vsd);
    }
}

static void set_tima8(QTestState *qts, uint32_t pir, uint32_t offset,
                      uint8_t b)
{
    uint64_t ic_addr;

    ic_addr = XIVE_IC_TM_INDIRECT + (pir << XIVE_PAGE_SHIFT);
    qtest_writeb(qts, ic_addr + offset, b);
}

static void set_tima32(QTestState *qts, uint32_t pir, uint32_t offset,
                       uint32_t l)
{
    uint64_t ic_addr;

    ic_addr = XIVE_IC_TM_INDIRECT + (pir << XIVE_PAGE_SHIFT);
    qtest_writel(qts, ic_addr + offset, l);
}

static uint8_t get_tima8(QTestState *qts, uint32_t pir, uint32_t offset)
{
    uint64_t ic_addr;

    ic_addr = XIVE_IC_TM_INDIRECT + (pir << XIVE_PAGE_SHIFT);
    return qtest_readb(qts, ic_addr + offset);
}

static uint16_t get_tima16(QTestState *qts, uint32_t pir, uint32_t offset)
{
    uint64_t ic_addr;

    ic_addr = XIVE_IC_TM_INDIRECT + (pir << XIVE_PAGE_SHIFT);
    return qtest_readw(qts, ic_addr + offset);
}

static uint32_t get_tima32(QTestState *qts, uint32_t pir, uint32_t offset)
{
    uint64_t ic_addr;

    ic_addr = XIVE_IC_TM_INDIRECT + (pir << XIVE_PAGE_SHIFT);
    return qtest_readl(qts, ic_addr + offset);
}

static void reset_pool_threads(QTestState *qts)
{
    uint8_t first_group = 0;
    int i;

    for (i = 0; i < SMT; i++) {
        uint32_t nvp_idx = 0x100 + i;
        set_nvp(qts, nvp_idx, first_group);
        set_tima32(qts, i, TM_QW2_HV_POOL + TM_WORD0, 0x000000ff);
        set_tima32(qts, i, TM_QW2_HV_POOL + TM_WORD1, 0);
        set_tima32(qts, i, TM_QW2_HV_POOL + TM_WORD2, TM_QW2W2_VP | nvp_idx);
    }
}

static void reset_hw_threads(QTestState *qts)
{
    uint8_t first_group = 0;
    uint32_t w1 = 0x000000ff;
    int i;

    if (SMT >= 4) {
        /* define 2 groups of 2, part of a bigger group of size 4 */
        set_nvg(qts, 0x80, 0x02);
        set_nvg(qts, 0x82, 0x02);
        set_nvg(qts, 0x81, 0);
        first_group = 0x01;
        w1 = 0x000300ff;
    }

    for (i = 0; i < SMT; i++) {
        set_nvp(qts, 0x80 + i, first_group);
        set_tima32(qts, i, TM_QW3_HV_PHYS + TM_WORD0, 0x00ff00ff);
        set_tima32(qts, i, TM_QW3_HV_PHYS + TM_WORD1, w1);
        set_tima32(qts, i, TM_QW3_HV_PHYS + TM_WORD2, 0x80000000);
    }
}

static void reset_state(QTestState *qts)
{
    size_t mem_used = XIVE_MEM_END - XIVE_MEM_START;

    qtest_memset(qts, XIVE_MEM_START, 0, mem_used);
    reset_hw_threads(qts);
    reset_pool_threads(qts);
}

static void init_xive(QTestState *qts)
{
    uint64_t val1, val2, range;

    /*
     * We can take a few shortcuts here, as we know the default values
     * used for xive initialization
     */

    /*
     * Set the BARs.
     * We reuse the same values used by firmware to ease debug.
     */
    pnv_xive_xscom_write(qts, X_CQ_IC_BAR, XIVE_IC_BAR);
    pnv_xive_xscom_write(qts, X_CQ_TM_BAR, XIVE_TM_BAR);

    /* ESB and NVPG use 2 pages per resource. The others only one page */
    range = (MAX_IRQS << 17) >> 25;
    val1 = XIVE_ESB_BAR | range;
    pnv_xive_xscom_write(qts, X_CQ_ESB_BAR, val1);

    range = (MAX_ENDS << 16) >> 25;
    val1 = XIVE_END_BAR | range;
    pnv_xive_xscom_write(qts, X_CQ_END_BAR, val1);

    range = (MAX_VPS << 17) >> 25;
    val1 = XIVE_NVPG_BAR | range;
    pnv_xive_xscom_write(qts, X_CQ_NVPG_BAR, val1);

    range = (MAX_VPS << 16) >> 25;
    val1 = XIVE_NVC_BAR | range;
    pnv_xive_xscom_write(qts, X_CQ_NVC_BAR, val1);

    /*
     * Enable hw threads.
     * We check the value written. Useless with current
     * implementation, but it validates the xscom read path and it's
     * what the hardware procedure says
     */
    val1 = 0xF000000000000000ull; /* core 0, 4 threads */
    pnv_xive_xscom_write(qts, X_TCTXT_EN0, val1);
    val2 = pnv_xive_xscom_read(qts, X_TCTXT_EN0);
    g_assert_cmphex(val1, ==, val2);

    /* Memory tables */
    set_table(qts, VST_ESB, XIVE_ESB_MEM);
    set_table(qts, VST_EAS, XIVE_EAS_MEM);
    set_table(qts, VST_END, XIVE_END_MEM);
    set_table(qts, VST_NVP, XIVE_NVP_MEM);
    set_table(qts, VST_NVG, XIVE_NVG_MEM);
    set_table(qts, VST_NVC, XIVE_NVC_MEM);
    set_table(qts, VST_SYNC, XIVE_SYNC_MEM);

    reset_hw_threads(qts);
    reset_pool_threads(qts);
}

static void test_hw_irq(QTestState *qts)
{
    uint32_t irq = 2;
    uint32_t irq_data = 0x600df00d;
    uint32_t end_index = 5;
    uint32_t target_pir = 1;
    uint32_t target_nvp = 0x80 + target_pir;
    uint8_t priority = 5;
    uint32_t reg32;
    uint16_t reg16;
    uint8_t pq, nsr, cppr;

    g_test_message("=========================================================");
    g_test_message("Testing irq %d to hardware thread %d", irq, target_pir);

    /* irq config */
    set_eas(qts, irq, end_index, irq_data);
    set_end(qts, end_index, target_nvp, priority, false /* group */);

    /* enable and trigger irq */
    get_esb(qts, irq, XIVE_EOI_PAGE, XIVE_ESB_SET_PQ_00);
    set_esb(qts, irq, XIVE_TRIGGER_PAGE, 0, 0);

    /* check irq is raised on cpu */
    pq = get_esb(qts, irq, XIVE_EOI_PAGE, XIVE_ESB_GET);
    g_assert_cmpuint(pq, ==, XIVE_ESB_PENDING);

    reg32 = get_tima32(qts, target_pir, TM_QW3_HV_PHYS + TM_WORD0);
    nsr = reg32 >> 24;
    cppr = (reg32 >> 16) & 0xFF;
    g_assert_cmphex(nsr, ==, 0x80);
    g_assert_cmphex(cppr, ==, 0xFF);

    /* ack the irq */
    reg16 = get_tima16(qts, target_pir, TM_SPC_ACK_HV_REG);
    nsr = reg16 >> 8;
    cppr = reg16 & 0xFF;
    g_assert_cmphex(nsr, ==, 0x80);
    g_assert_cmphex(cppr, ==, priority);

    /* check irq data is what was configured */
    reg32 = qtest_readl(qts, xive_get_queue_addr(end_index));
    g_assert_cmphex((reg32 & 0x7fffffff), ==, (irq_data & 0x7fffffff));

    /* End Of Interrupt */
    set_esb(qts, irq, XIVE_EOI_PAGE, XIVE_ESB_STORE_EOI, 0);
    pq = get_esb(qts, irq, XIVE_EOI_PAGE, XIVE_ESB_GET);
    g_assert_cmpuint(pq, ==, XIVE_ESB_RESET);

    /* reset CPPR */
    set_tima8(qts, target_pir, TM_QW3_HV_PHYS + TM_CPPR, 0xFF);
    reg32 = get_tima32(qts, target_pir, TM_QW3_HV_PHYS + TM_WORD0);
    nsr = reg32 >> 24;
    cppr = (reg32 >> 16) & 0xFF;
    g_assert_cmphex(nsr, ==, 0x00);
    g_assert_cmphex(cppr, ==, 0xFF);
}

static void test_pool_irq(QTestState *qts)
{
    uint32_t irq = 2;
    uint32_t irq_data = 0x600d0d06;
    uint32_t end_index = 5;
    uint32_t target_pir = 1;
    uint32_t target_nvp = 0x100 + target_pir;
    uint8_t priority = 5;
    uint32_t reg32;
    uint16_t reg16;
    uint8_t pq, nsr, cppr, ipb;

    g_test_message("=========================================================");
    g_test_message("Testing irq %d to pool thread %d", irq, target_pir);

    /* irq config */
    set_eas(qts, irq, end_index, irq_data);
    set_end(qts, end_index, target_nvp, priority, false /* group */);

    /* enable and trigger irq */
    get_esb(qts, irq, XIVE_EOI_PAGE, XIVE_ESB_SET_PQ_00);
    set_esb(qts, irq, XIVE_TRIGGER_PAGE, 0, 0);

    /* check irq is raised on cpu */
    pq = get_esb(qts, irq, XIVE_EOI_PAGE, XIVE_ESB_GET);
    g_assert_cmpuint(pq, ==, XIVE_ESB_PENDING);

    /* check TIMA values in the PHYS ring (shared by POOL ring) */
    reg32 = get_tima32(qts, target_pir, TM_QW3_HV_PHYS + TM_WORD0);
    nsr = reg32 >> 24;
    cppr = (reg32 >> 16) & 0xFF;
    g_assert_cmphex(nsr, ==, 0x40);
    g_assert_cmphex(cppr, ==, 0xFF);

    /* check TIMA values in the POOL ring */
    reg32 = get_tima32(qts, target_pir, TM_QW2_HV_POOL + TM_WORD0);
    nsr = reg32 >> 24;
    cppr = (reg32 >> 16) & 0xFF;
    ipb = (reg32 >> 8) & 0xFF;
    g_assert_cmphex(nsr, ==, 0);
    g_assert_cmphex(cppr, ==, 0);
    g_assert_cmphex(ipb, ==, 0x80 >> priority);

    /* ack the irq */
    reg16 = get_tima16(qts, target_pir, TM_SPC_ACK_HV_REG);
    nsr = reg16 >> 8;
    cppr = reg16 & 0xFF;
    g_assert_cmphex(nsr, ==, 0x40);
    g_assert_cmphex(cppr, ==, priority);

    /* check irq data is what was configured */
    reg32 = qtest_readl(qts, xive_get_queue_addr(end_index));
    g_assert_cmphex((reg32 & 0x7fffffff), ==, (irq_data & 0x7fffffff));

    /* check IPB is cleared in the POOL ring */
    reg32 = get_tima32(qts, target_pir, TM_QW2_HV_POOL + TM_WORD0);
    ipb = (reg32 >> 8) & 0xFF;
    g_assert_cmphex(ipb, ==, 0);

    /* End Of Interrupt */
    set_esb(qts, irq, XIVE_EOI_PAGE, XIVE_ESB_STORE_EOI, 0);
    pq = get_esb(qts, irq, XIVE_EOI_PAGE, XIVE_ESB_GET);
    g_assert_cmpuint(pq, ==, XIVE_ESB_RESET);

    /* reset CPPR */
    set_tima8(qts, target_pir, TM_QW3_HV_PHYS + TM_CPPR, 0xFF);
    reg32 = get_tima32(qts, target_pir, TM_QW3_HV_PHYS + TM_WORD0);
    nsr = reg32 >> 24;
    cppr = (reg32 >> 16) & 0xFF;
    g_assert_cmphex(nsr, ==, 0x00);
    g_assert_cmphex(cppr, ==, 0xFF);
}

#define XIVE_ODD_CL 0x80
static void test_pull_thread_ctx_to_odd_thread_cl(QTestState *qts)
{
    uint32_t target_pir = 1;
    uint32_t target_nvp = 0x80 + target_pir;
    Xive2Nvp nvp;
    uint8_t cl_pair[XIVE_REPORT_SIZE];
    uint32_t qw1w0, qw3w0, qw1w2, qw2w2;
    uint8_t qw3b8;
    uint32_t cl_word;
    uint32_t word2;

    g_test_message("=========================================================");
    g_test_message("Testing 'Pull Thread Context to Odd Thread Reporting " \
                   "Line'");

    /* clear odd cache line prior to pull operation */
    memset(cl_pair, 0, sizeof(cl_pair));
    get_nvp(qts, target_nvp, &nvp);
    set_cl_pair(qts, &nvp, cl_pair);

    /* Read some values from TIMA that we expect to see in cacheline */
    qw1w0 = get_tima32(qts, target_pir, TM_QW1_OS + TM_WORD0);
    qw3w0 = get_tima32(qts, target_pir, TM_QW3_HV_PHYS + TM_WORD0);
    qw1w2 = get_tima32(qts, target_pir, TM_QW1_OS + TM_WORD2);
    qw2w2 = get_tima32(qts, target_pir, TM_QW2_HV_POOL + TM_WORD2);
    qw3b8 = get_tima8(qts, target_pir, TM_QW3_HV_PHYS + TM_WORD2);

    /* Execute the pull operation */
    set_tima8(qts, target_pir, TM_SPC_PULL_PHYS_CTX_OL, 0);

    /* Verify odd cache line values match TIMA after pull operation */
    get_cl_pair(qts, &nvp, cl_pair);
    memcpy(&cl_word, &cl_pair[XIVE_ODD_CL + TM_QW1_OS + TM_WORD0], 4);
    g_assert_cmphex(qw1w0, ==, be32_to_cpu(cl_word));
    memcpy(&cl_word, &cl_pair[XIVE_ODD_CL + TM_QW3_HV_PHYS + TM_WORD0], 4);
    g_assert_cmphex(qw3w0, ==, be32_to_cpu(cl_word));
    memcpy(&cl_word, &cl_pair[XIVE_ODD_CL + TM_QW1_OS + TM_WORD2], 4);
    g_assert_cmphex(qw1w2, ==, be32_to_cpu(cl_word));
    memcpy(&cl_word, &cl_pair[XIVE_ODD_CL + TM_QW2_HV_POOL + TM_WORD2], 4);
    g_assert_cmphex(qw2w2, ==, be32_to_cpu(cl_word));
    g_assert_cmphex(qw3b8, ==,
                    cl_pair[XIVE_ODD_CL + TM_QW3_HV_PHYS + TM_WORD2]);

    /* Verify that all TIMA valid bits for target thread are cleared */
    word2 = get_tima32(qts, target_pir, TM_QW1_OS + TM_WORD2);
    g_assert_cmphex(xive_get_field32(TM_QW1W2_VO, word2), ==, 0);
    word2 = get_tima32(qts, target_pir, TM_QW2_HV_POOL + TM_WORD2);
    g_assert_cmphex(xive_get_field32(TM_QW2W2_VP, word2), ==, 0);
    word2 = get_tima32(qts, target_pir, TM_QW3_HV_PHYS + TM_WORD2);
    g_assert_cmphex(xive_get_field32(TM_QW3W2_VT, word2), ==, 0);
}

static void test_hw_group_irq(QTestState *qts)
{
    uint32_t irq = 100;
    uint32_t irq_data = 0xdeadbeef;
    uint32_t end_index = 23;
    uint32_t chosen_one;
    uint32_t target_nvp = 0x81; /* group size = 4 */
    uint8_t priority = 6;
    uint32_t reg32;
    uint16_t reg16;
    uint8_t pq, nsr, cppr;

    g_test_message("=========================================================");
    g_test_message("Testing irq %d to hardware group of size 4", irq);

    /* irq config */
    set_eas(qts, irq, end_index, irq_data);
    set_end(qts, end_index, target_nvp, priority, true /* group */);

    /* enable and trigger irq */
    get_esb(qts, irq, XIVE_EOI_PAGE, XIVE_ESB_SET_PQ_00);
    set_esb(qts, irq, XIVE_TRIGGER_PAGE, 0, 0);

    /* check irq is raised on cpu */
    pq = get_esb(qts, irq, XIVE_EOI_PAGE, XIVE_ESB_GET);
    g_assert_cmpuint(pq, ==, XIVE_ESB_PENDING);

    /* find the targeted vCPU */
    for (chosen_one = 0; chosen_one < SMT; chosen_one++) {
        reg32 = get_tima32(qts, chosen_one, TM_QW3_HV_PHYS + TM_WORD0);
        nsr = reg32 >> 24;
        if (nsr == 0x82) {
            break;
        }
    }
    g_assert_cmphex(chosen_one, <, SMT);
    cppr = (reg32 >> 16) & 0xFF;
    g_assert_cmphex(nsr, ==, 0x82);
    g_assert_cmphex(cppr, ==, 0xFF);

    /* ack the irq */
    reg16 = get_tima16(qts, chosen_one, TM_SPC_ACK_HV_REG);
    nsr = reg16 >> 8;
    cppr = reg16 & 0xFF;
    g_assert_cmphex(nsr, ==, 0x82);
    g_assert_cmphex(cppr, ==, priority);

    /* check irq data is what was configured */
    reg32 = qtest_readl(qts, xive_get_queue_addr(end_index));
    g_assert_cmphex((reg32 & 0x7fffffff), ==, (irq_data & 0x7fffffff));

    /* End Of Interrupt */
    set_esb(qts, irq, XIVE_EOI_PAGE, XIVE_ESB_STORE_EOI, 0);
    pq = get_esb(qts, irq, XIVE_EOI_PAGE, XIVE_ESB_GET);
    g_assert_cmpuint(pq, ==, XIVE_ESB_RESET);

    /* reset CPPR */
    set_tima8(qts, chosen_one, TM_QW3_HV_PHYS + TM_CPPR, 0xFF);
    reg32 = get_tima32(qts, chosen_one, TM_QW3_HV_PHYS + TM_WORD0);
    nsr = reg32 >> 24;
    cppr = (reg32 >> 16) & 0xFF;
    g_assert_cmphex(nsr, ==, 0x00);
    g_assert_cmphex(cppr, ==, 0xFF);
}

static void test_hw_group_irq_backlog(QTestState *qts)
{
    uint32_t irq = 31;
    uint32_t irq_data = 0x01234567;
    uint32_t end_index = 129;
    uint32_t target_nvp = 0x81; /* group size = 4 */
    uint32_t chosen_one = 3;
    uint8_t blocking_priority, priority = 3;
    uint32_t reg32;
    uint16_t reg16;
    uint8_t pq, nsr, cppr, lsmfb, i;

    g_test_message("=========================================================");
    g_test_message("Testing irq %d to hardware group of size 4 going " \
                   "through backlog",
                   irq);

    /*
     * set current priority of all threads in the group to something
     * higher than what we're about to trigger
     */
    blocking_priority = priority - 1;
    for (i = 0; i < SMT; i++) {
        set_tima8(qts, i, TM_QW3_HV_PHYS + TM_CPPR, blocking_priority);
    }

    /* irq config */
    set_eas(qts, irq, end_index, irq_data);
    set_end(qts, end_index, target_nvp, priority, true /* group */);

    /* enable and trigger irq */
    get_esb(qts, irq, XIVE_EOI_PAGE, XIVE_ESB_SET_PQ_00);
    set_esb(qts, irq, XIVE_TRIGGER_PAGE, 0, 0);

    /* check irq is raised on cpu */
    pq = get_esb(qts, irq, XIVE_EOI_PAGE, XIVE_ESB_GET);
    g_assert_cmpuint(pq, ==, XIVE_ESB_PENDING);

    /* check no interrupt is pending on the 2 possible targets */
    for (i = 0; i < SMT; i++) {
        reg32 = get_tima32(qts, i, TM_QW3_HV_PHYS + TM_WORD0);
        nsr = reg32 >> 24;
        cppr = (reg32 >> 16) & 0xFF;
        lsmfb = reg32 & 0xFF;
        g_assert_cmphex(nsr, ==, 0x0);
        g_assert_cmphex(cppr, ==, blocking_priority);
        g_assert_cmphex(lsmfb, ==, priority);
    }

    /* lower priority of one thread */
    set_tima8(qts, chosen_one, TM_QW3_HV_PHYS + TM_CPPR, priority + 1);

    /* check backlogged interrupt is presented */
    reg32 = get_tima32(qts, chosen_one, TM_QW3_HV_PHYS + TM_WORD0);
    nsr = reg32 >> 24;
    cppr = (reg32 >> 16) & 0xFF;
    g_assert_cmphex(nsr, ==, 0x82);
    g_assert_cmphex(cppr, ==, priority + 1);

    /* ack the irq */
    reg16 = get_tima16(qts, chosen_one, TM_SPC_ACK_HV_REG);
    nsr = reg16 >> 8;
    cppr = reg16 & 0xFF;
    g_assert_cmphex(nsr, ==, 0x82);
    g_assert_cmphex(cppr, ==, priority);

    /* check irq data is what was configured */
    reg32 = qtest_readl(qts, xive_get_queue_addr(end_index));
    g_assert_cmphex((reg32 & 0x7fffffff), ==, (irq_data & 0x7fffffff));

    /* End Of Interrupt */
    set_esb(qts, irq, XIVE_EOI_PAGE, XIVE_ESB_STORE_EOI, 0);
    pq = get_esb(qts, irq, XIVE_EOI_PAGE, XIVE_ESB_GET);
    g_assert_cmpuint(pq, ==, XIVE_ESB_RESET);

    /* reset CPPR */
    set_tima8(qts, chosen_one, TM_QW3_HV_PHYS + TM_CPPR, 0xFF);
    reg32 = get_tima32(qts, chosen_one, TM_QW3_HV_PHYS + TM_WORD0);
    nsr = reg32 >> 24;
    cppr = (reg32 >> 16) & 0xFF;
    lsmfb = reg32 & 0xFF;
    g_assert_cmphex(nsr, ==, 0x00);
    g_assert_cmphex(cppr, ==, 0xFF);
    g_assert_cmphex(lsmfb, ==, 0xFF);
}

static void test_xive(void)
{
    QTestState *qts;

    qts = qtest_initf("-M powernv10 -smp %d,cores=1,threads=%d -nographic "
                      "-nodefaults -serial mon:stdio -S "
                      "-d guest_errors -trace '*xive*'",
                      SMT, SMT);
    init_xive(qts);

    test_hw_irq(qts);

    /* omit reset_state here and use settings from test_hw_irq */
    test_pull_thread_ctx_to_odd_thread_cl(qts);

    reset_state(qts);
    test_pool_irq(qts);

    reset_state(qts);
    test_hw_group_irq(qts);

    reset_state(qts);
    test_hw_group_irq_backlog(qts);

    reset_state(qts);
    test_flush_sync_inject(qts);

    reset_state(qts);
    test_nvpg_bar(qts);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("xive2", test_xive);
    return g_test_run();
}
