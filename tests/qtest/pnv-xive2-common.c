/*
 * QTest testcase for PowerNV 10 interrupt controller (xive2)
 *  - Common functions for XIVE2 tests
 *
 * Copyright (c) 2024, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "libqtest.h"

#include "pnv-xive2-common.h"


static uint64_t pnv_xscom_addr(uint32_t pcba)
{
    return P10_XSCOM_BASE | ((uint64_t) pcba << 3);
}

static uint64_t pnv_xive_xscom_addr(uint32_t reg)
{
    return pnv_xscom_addr(XIVE_XSCOM + reg);
}

uint64_t pnv_xive_xscom_read(QTestState *qts, uint32_t reg)
{
    return qtest_readq(qts, pnv_xive_xscom_addr(reg));
}

void pnv_xive_xscom_write(QTestState *qts, uint32_t reg, uint64_t val)
{
    qtest_writeq(qts, pnv_xive_xscom_addr(reg), val);
}

static void xive_get_struct(QTestState *qts, uint64_t src, void *dest,
                            size_t size)
{
    uint8_t *destination = (uint8_t *)dest;
    size_t i;

    for (i = 0; i < size; i++) {
        *(destination + i) = qtest_readb(qts, src + i);
    }
}

static void xive_copy_struct(QTestState *qts, void *src, uint64_t dest,
                             size_t size)
{
    uint8_t *source = (uint8_t *)src;
    size_t i;

    for (i = 0; i < size; i++) {
        qtest_writeb(qts, dest + i, *(source + i));
    }
}

uint64_t xive_get_queue_addr(uint32_t end_index)
{
    return XIVE_QUEUE_MEM + (uint64_t)end_index * XIVE_QUEUE_SIZE;
}

uint8_t get_esb(QTestState *qts, uint32_t index, uint8_t page,
                uint32_t offset)
{
    uint64_t addr;

    addr = XIVE_ESB_ADDR + ((uint64_t)index << (XIVE_PAGE_SHIFT + 1));
    if (page == 1) {
        addr += 1 << XIVE_PAGE_SHIFT;
    }
    return qtest_readb(qts, addr + offset);
}

void set_esb(QTestState *qts, uint32_t index, uint8_t page,
             uint32_t offset, uint32_t val)
{
    uint64_t addr;

    addr = XIVE_ESB_ADDR + ((uint64_t)index << (XIVE_PAGE_SHIFT + 1));
    if (page == 1) {
        addr += 1 << XIVE_PAGE_SHIFT;
    }
    return qtest_writel(qts, addr + offset, cpu_to_be32(val));
}

void get_nvp(QTestState *qts, uint32_t index, Xive2Nvp* nvp)
{
    uint64_t addr = XIVE_NVP_MEM + (uint64_t)index * sizeof(Xive2Nvp);
    xive_get_struct(qts, addr, nvp, sizeof(Xive2Nvp));
}

void set_nvp(QTestState *qts, uint32_t index, uint8_t first)
{
    uint64_t nvp_addr;
    Xive2Nvp nvp;
    uint64_t report_addr;

    nvp_addr = XIVE_NVP_MEM + (uint64_t)index * sizeof(Xive2Nvp);
    report_addr = (XIVE_REPORT_MEM + (uint64_t)index * XIVE_REPORT_SIZE) >> 8;

    memset(&nvp, 0, sizeof(nvp));
    nvp.w0 = xive_set_field32(NVP2_W0_VALID, 0, 1);
    nvp.w0 = xive_set_field32(NVP2_W0_PGOFIRST, nvp.w0, first);
    nvp.w6 = xive_set_field32(NVP2_W6_REPORTING_LINE, nvp.w6,
                              (report_addr >> 24) & 0xfffffff);
    nvp.w7 = xive_set_field32(NVP2_W7_REPORTING_LINE, nvp.w7,
                              report_addr & 0xffffff);
    xive_copy_struct(qts, &nvp, nvp_addr, sizeof(nvp));
}

static uint64_t get_cl_pair_addr(Xive2Nvp *nvp)
{
    uint64_t upper = xive_get_field32(0x0fffffff, nvp->w6);
    uint64_t lower = xive_get_field32(0xffffff00, nvp->w7);
    return (upper << 32) | (lower << 8);
}

void get_cl_pair(QTestState *qts, Xive2Nvp *nvp, uint8_t *cl_pair)
{
    uint64_t addr = get_cl_pair_addr(nvp);
    xive_get_struct(qts, addr, cl_pair, XIVE_REPORT_SIZE);
}

void set_cl_pair(QTestState *qts, Xive2Nvp *nvp, uint8_t *cl_pair)
{
    uint64_t addr = get_cl_pair_addr(nvp);
    xive_copy_struct(qts, cl_pair, addr, XIVE_REPORT_SIZE);
}

void set_nvg(QTestState *qts, uint32_t index, uint8_t next)
{
    uint64_t nvg_addr;
    Xive2Nvgc nvg;

    nvg_addr = XIVE_NVG_MEM + (uint64_t)index * sizeof(Xive2Nvgc);

    memset(&nvg, 0, sizeof(nvg));
    nvg.w0 = xive_set_field32(NVGC2_W0_VALID, 0, 1);
    nvg.w0 = xive_set_field32(NVGC2_W0_PGONEXT, nvg.w0, next);
    xive_copy_struct(qts, &nvg, nvg_addr, sizeof(nvg));
}

void set_eas(QTestState *qts, uint32_t index, uint32_t end_index,
             uint32_t data)
{
    uint64_t eas_addr;
    Xive2Eas eas;

    eas_addr = XIVE_EAS_MEM + (uint64_t)index * sizeof(Xive2Eas);

    memset(&eas, 0, sizeof(eas));
    eas.w = xive_set_field64(EAS2_VALID, 0, 1);
    eas.w = xive_set_field64(EAS2_END_INDEX, eas.w, end_index);
    eas.w = xive_set_field64(EAS2_END_DATA, eas.w, data);
    xive_copy_struct(qts, &eas, eas_addr, sizeof(eas));
}

void set_end(QTestState *qts, uint32_t index, uint32_t nvp_index,
             uint8_t priority, bool i)
{
    uint64_t end_addr, queue_addr, queue_hi, queue_lo;
    uint8_t queue_size;
    Xive2End end;

    end_addr = XIVE_END_MEM + (uint64_t)index * sizeof(Xive2End);
    queue_addr = xive_get_queue_addr(index);
    queue_hi = (queue_addr >> 32) & END2_W2_EQ_ADDR_HI;
    queue_lo = queue_addr & END2_W3_EQ_ADDR_LO;
    queue_size = ctz16(XIVE_QUEUE_SIZE) - 12;

    memset(&end, 0, sizeof(end));
    end.w0 = xive_set_field32(END2_W0_VALID, 0, 1);
    end.w0 = xive_set_field32(END2_W0_ENQUEUE, end.w0, 1);
    end.w0 = xive_set_field32(END2_W0_UCOND_NOTIFY, end.w0, 1);
    end.w0 = xive_set_field32(END2_W0_BACKLOG, end.w0, 1);

    end.w1 = xive_set_field32(END2_W1_GENERATION, 0, 1);

    end.w2 = cpu_to_be32(queue_hi);

    end.w3 = cpu_to_be32(queue_lo);
    end.w3 = xive_set_field32(END2_W3_QSIZE, end.w3, queue_size);

    end.w6 = xive_set_field32(END2_W6_IGNORE, 0, i);
    end.w6 = xive_set_field32(END2_W6_VP_OFFSET, end.w6, nvp_index);

    end.w7 = xive_set_field32(END2_W7_F0_PRIORITY, 0, priority);
    xive_copy_struct(qts, &end, end_addr, sizeof(end));
}

