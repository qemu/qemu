/*
 * QEMU model of the ZynqMP generic DMA
 *
 * Copyright (c) 2014 Xilinx Inc.
 * Copyright (c) 2018 FEIMTECH AB
 *
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>,
 *            Francisco Iglesias <francisco.iglesias@feimtech.se>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/dma/xlnx-zdma.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"

#ifndef XLNX_ZDMA_ERR_DEBUG
#define XLNX_ZDMA_ERR_DEBUG 0
#endif

REG32(ZDMA_ERR_CTRL, 0x0)
    FIELD(ZDMA_ERR_CTRL, APB_ERR_RES, 0, 1)
REG32(ZDMA_CH_ISR, 0x100)
    FIELD(ZDMA_CH_ISR, DMA_PAUSE, 11, 1)
    FIELD(ZDMA_CH_ISR, DMA_DONE, 10, 1)
    FIELD(ZDMA_CH_ISR, AXI_WR_DATA, 9, 1)
    FIELD(ZDMA_CH_ISR, AXI_RD_DATA, 8, 1)
    FIELD(ZDMA_CH_ISR, AXI_RD_DST_DSCR, 7, 1)
    FIELD(ZDMA_CH_ISR, AXI_RD_SRC_DSCR, 6, 1)
    FIELD(ZDMA_CH_ISR, IRQ_DST_ACCT_ERR, 5, 1)
    FIELD(ZDMA_CH_ISR, IRQ_SRC_ACCT_ERR, 4, 1)
    FIELD(ZDMA_CH_ISR, BYTE_CNT_OVRFL, 3, 1)
    FIELD(ZDMA_CH_ISR, DST_DSCR_DONE, 2, 1)
    FIELD(ZDMA_CH_ISR, SRC_DSCR_DONE, 1, 1)
    FIELD(ZDMA_CH_ISR, INV_APB, 0, 1)
REG32(ZDMA_CH_IMR, 0x104)
    FIELD(ZDMA_CH_IMR, DMA_PAUSE, 11, 1)
    FIELD(ZDMA_CH_IMR, DMA_DONE, 10, 1)
    FIELD(ZDMA_CH_IMR, AXI_WR_DATA, 9, 1)
    FIELD(ZDMA_CH_IMR, AXI_RD_DATA, 8, 1)
    FIELD(ZDMA_CH_IMR, AXI_RD_DST_DSCR, 7, 1)
    FIELD(ZDMA_CH_IMR, AXI_RD_SRC_DSCR, 6, 1)
    FIELD(ZDMA_CH_IMR, IRQ_DST_ACCT_ERR, 5, 1)
    FIELD(ZDMA_CH_IMR, IRQ_SRC_ACCT_ERR, 4, 1)
    FIELD(ZDMA_CH_IMR, BYTE_CNT_OVRFL, 3, 1)
    FIELD(ZDMA_CH_IMR, DST_DSCR_DONE, 2, 1)
    FIELD(ZDMA_CH_IMR, SRC_DSCR_DONE, 1, 1)
    FIELD(ZDMA_CH_IMR, INV_APB, 0, 1)
REG32(ZDMA_CH_IEN, 0x108)
    FIELD(ZDMA_CH_IEN, DMA_PAUSE, 11, 1)
    FIELD(ZDMA_CH_IEN, DMA_DONE, 10, 1)
    FIELD(ZDMA_CH_IEN, AXI_WR_DATA, 9, 1)
    FIELD(ZDMA_CH_IEN, AXI_RD_DATA, 8, 1)
    FIELD(ZDMA_CH_IEN, AXI_RD_DST_DSCR, 7, 1)
    FIELD(ZDMA_CH_IEN, AXI_RD_SRC_DSCR, 6, 1)
    FIELD(ZDMA_CH_IEN, IRQ_DST_ACCT_ERR, 5, 1)
    FIELD(ZDMA_CH_IEN, IRQ_SRC_ACCT_ERR, 4, 1)
    FIELD(ZDMA_CH_IEN, BYTE_CNT_OVRFL, 3, 1)
    FIELD(ZDMA_CH_IEN, DST_DSCR_DONE, 2, 1)
    FIELD(ZDMA_CH_IEN, SRC_DSCR_DONE, 1, 1)
    FIELD(ZDMA_CH_IEN, INV_APB, 0, 1)
REG32(ZDMA_CH_IDS, 0x10c)
    FIELD(ZDMA_CH_IDS, DMA_PAUSE, 11, 1)
    FIELD(ZDMA_CH_IDS, DMA_DONE, 10, 1)
    FIELD(ZDMA_CH_IDS, AXI_WR_DATA, 9, 1)
    FIELD(ZDMA_CH_IDS, AXI_RD_DATA, 8, 1)
    FIELD(ZDMA_CH_IDS, AXI_RD_DST_DSCR, 7, 1)
    FIELD(ZDMA_CH_IDS, AXI_RD_SRC_DSCR, 6, 1)
    FIELD(ZDMA_CH_IDS, IRQ_DST_ACCT_ERR, 5, 1)
    FIELD(ZDMA_CH_IDS, IRQ_SRC_ACCT_ERR, 4, 1)
    FIELD(ZDMA_CH_IDS, BYTE_CNT_OVRFL, 3, 1)
    FIELD(ZDMA_CH_IDS, DST_DSCR_DONE, 2, 1)
    FIELD(ZDMA_CH_IDS, SRC_DSCR_DONE, 1, 1)
    FIELD(ZDMA_CH_IDS, INV_APB, 0, 1)
REG32(ZDMA_CH_CTRL0, 0x110)
    FIELD(ZDMA_CH_CTRL0, OVR_FETCH, 7, 1)
    FIELD(ZDMA_CH_CTRL0, POINT_TYPE, 6, 1)
    FIELD(ZDMA_CH_CTRL0, MODE, 4, 2)
    FIELD(ZDMA_CH_CTRL0, RATE_CTRL, 3, 1)
    FIELD(ZDMA_CH_CTRL0, CONT_ADDR, 2, 1)
    FIELD(ZDMA_CH_CTRL0, CONT, 1, 1)
REG32(ZDMA_CH_CTRL1, 0x114)
    FIELD(ZDMA_CH_CTRL1, DST_ISSUE, 5, 5)
    FIELD(ZDMA_CH_CTRL1, SRC_ISSUE, 0, 5)
REG32(ZDMA_CH_FCI, 0x118)
    FIELD(ZDMA_CH_FCI, PROG_CELL_CNT, 2, 2)
    FIELD(ZDMA_CH_FCI, SIDE, 1, 1)
    FIELD(ZDMA_CH_FCI, EN, 0, 1)
REG32(ZDMA_CH_STATUS, 0x11c)
    FIELD(ZDMA_CH_STATUS, STATE, 0, 2)
REG32(ZDMA_CH_DATA_ATTR, 0x120)
    FIELD(ZDMA_CH_DATA_ATTR, ARBURST, 26, 2)
    FIELD(ZDMA_CH_DATA_ATTR, ARCACHE, 22, 4)
    FIELD(ZDMA_CH_DATA_ATTR, ARQOS, 18, 4)
    FIELD(ZDMA_CH_DATA_ATTR, ARLEN, 14, 4)
    FIELD(ZDMA_CH_DATA_ATTR, AWBURST, 12, 2)
    FIELD(ZDMA_CH_DATA_ATTR, AWCACHE, 8, 4)
    FIELD(ZDMA_CH_DATA_ATTR, AWQOS, 4, 4)
    FIELD(ZDMA_CH_DATA_ATTR, AWLEN, 0, 4)
REG32(ZDMA_CH_DSCR_ATTR, 0x124)
    FIELD(ZDMA_CH_DSCR_ATTR, AXCOHRNT, 8, 1)
    FIELD(ZDMA_CH_DSCR_ATTR, AXCACHE, 4, 4)
    FIELD(ZDMA_CH_DSCR_ATTR, AXQOS, 0, 4)
REG32(ZDMA_CH_SRC_DSCR_WORD0, 0x128)
REG32(ZDMA_CH_SRC_DSCR_WORD1, 0x12c)
    FIELD(ZDMA_CH_SRC_DSCR_WORD1, MSB, 0, 17)
REG32(ZDMA_CH_SRC_DSCR_WORD2, 0x130)
    FIELD(ZDMA_CH_SRC_DSCR_WORD2, SIZE, 0, 30)
REG32(ZDMA_CH_SRC_DSCR_WORD3, 0x134)
    FIELD(ZDMA_CH_SRC_DSCR_WORD3, CMD, 3, 2)
    FIELD(ZDMA_CH_SRC_DSCR_WORD3, INTR, 2, 1)
    FIELD(ZDMA_CH_SRC_DSCR_WORD3, TYPE, 1, 1)
    FIELD(ZDMA_CH_SRC_DSCR_WORD3, COHRNT, 0, 1)
REG32(ZDMA_CH_DST_DSCR_WORD0, 0x138)
REG32(ZDMA_CH_DST_DSCR_WORD1, 0x13c)
    FIELD(ZDMA_CH_DST_DSCR_WORD1, MSB, 0, 17)
REG32(ZDMA_CH_DST_DSCR_WORD2, 0x140)
    FIELD(ZDMA_CH_DST_DSCR_WORD2, SIZE, 0, 30)
REG32(ZDMA_CH_DST_DSCR_WORD3, 0x144)
    FIELD(ZDMA_CH_DST_DSCR_WORD3, INTR, 2, 1)
    FIELD(ZDMA_CH_DST_DSCR_WORD3, TYPE, 1, 1)
    FIELD(ZDMA_CH_DST_DSCR_WORD3, COHRNT, 0, 1)
REG32(ZDMA_CH_WR_ONLY_WORD0, 0x148)
REG32(ZDMA_CH_WR_ONLY_WORD1, 0x14c)
REG32(ZDMA_CH_WR_ONLY_WORD2, 0x150)
REG32(ZDMA_CH_WR_ONLY_WORD3, 0x154)
REG32(ZDMA_CH_SRC_START_LSB, 0x158)
REG32(ZDMA_CH_SRC_START_MSB, 0x15c)
    FIELD(ZDMA_CH_SRC_START_MSB, ADDR, 0, 17)
REG32(ZDMA_CH_DST_START_LSB, 0x160)
REG32(ZDMA_CH_DST_START_MSB, 0x164)
    FIELD(ZDMA_CH_DST_START_MSB, ADDR, 0, 17)
REG32(ZDMA_CH_RATE_CTRL, 0x18c)
    FIELD(ZDMA_CH_RATE_CTRL, CNT, 0, 12)
REG32(ZDMA_CH_SRC_CUR_PYLD_LSB, 0x168)
REG32(ZDMA_CH_SRC_CUR_PYLD_MSB, 0x16c)
    FIELD(ZDMA_CH_SRC_CUR_PYLD_MSB, ADDR, 0, 17)
REG32(ZDMA_CH_DST_CUR_PYLD_LSB, 0x170)
REG32(ZDMA_CH_DST_CUR_PYLD_MSB, 0x174)
    FIELD(ZDMA_CH_DST_CUR_PYLD_MSB, ADDR, 0, 17)
REG32(ZDMA_CH_SRC_CUR_DSCR_LSB, 0x178)
REG32(ZDMA_CH_SRC_CUR_DSCR_MSB, 0x17c)
    FIELD(ZDMA_CH_SRC_CUR_DSCR_MSB, ADDR, 0, 17)
REG32(ZDMA_CH_DST_CUR_DSCR_LSB, 0x180)
REG32(ZDMA_CH_DST_CUR_DSCR_MSB, 0x184)
    FIELD(ZDMA_CH_DST_CUR_DSCR_MSB, ADDR, 0, 17)
REG32(ZDMA_CH_TOTAL_BYTE, 0x188)
REG32(ZDMA_CH_RATE_CNTL, 0x18c)
    FIELD(ZDMA_CH_RATE_CNTL, CNT, 0, 12)
REG32(ZDMA_CH_IRQ_SRC_ACCT, 0x190)
    FIELD(ZDMA_CH_IRQ_SRC_ACCT, CNT, 0, 8)
REG32(ZDMA_CH_IRQ_DST_ACCT, 0x194)
    FIELD(ZDMA_CH_IRQ_DST_ACCT, CNT, 0, 8)
REG32(ZDMA_CH_DBG0, 0x198)
    FIELD(ZDMA_CH_DBG0, CMN_BUF_FREE, 0, 9)
REG32(ZDMA_CH_DBG1, 0x19c)
    FIELD(ZDMA_CH_DBG1, CMN_BUF_OCC, 0, 9)
REG32(ZDMA_CH_CTRL2, 0x200)
    FIELD(ZDMA_CH_CTRL2, EN, 0, 1)

enum {
    PT_REG = 0,
    PT_MEM = 1,
};

enum {
    CMD_HALT = 1,
    CMD_STOP = 2,
};

enum {
    RW_MODE_RW = 0,
    RW_MODE_WO = 1,
    RW_MODE_RO = 2,
};

enum {
    DTYPE_LINEAR = 0,
    DTYPE_LINKED = 1,
};

enum {
    AXI_BURST_FIXED = 0,
    AXI_BURST_INCR  = 1,
};

static void zdma_ch_imr_update_irq(XlnxZDMA *s)
{
    bool pending;

    pending = s->regs[R_ZDMA_CH_ISR] & ~s->regs[R_ZDMA_CH_IMR];

    qemu_set_irq(s->irq_zdma_ch_imr, pending);
}

static void zdma_ch_isr_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxZDMA *s = XLNX_ZDMA(reg->opaque);
    zdma_ch_imr_update_irq(s);
}

static uint64_t zdma_ch_ien_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxZDMA *s = XLNX_ZDMA(reg->opaque);
    uint32_t val = val64;

    s->regs[R_ZDMA_CH_IMR] &= ~val;
    zdma_ch_imr_update_irq(s);
    return 0;
}

static uint64_t zdma_ch_ids_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxZDMA *s = XLNX_ZDMA(reg->opaque);
    uint32_t val = val64;

    s->regs[R_ZDMA_CH_IMR] |= val;
    zdma_ch_imr_update_irq(s);
    return 0;
}

static void zdma_set_state(XlnxZDMA *s, XlnxZDMAState state)
{
    s->state = state;
    ARRAY_FIELD_DP32(s->regs, ZDMA_CH_STATUS, STATE, state);

    /* Signal error if we have an error condition.  */
    if (s->error) {
        ARRAY_FIELD_DP32(s->regs, ZDMA_CH_STATUS, STATE, 3);
    }
}

static void zdma_src_done(XlnxZDMA *s)
{
    unsigned int cnt;
    cnt = ARRAY_FIELD_EX32(s->regs, ZDMA_CH_IRQ_SRC_ACCT, CNT);
    cnt++;
    ARRAY_FIELD_DP32(s->regs, ZDMA_CH_IRQ_SRC_ACCT, CNT, cnt);
    ARRAY_FIELD_DP32(s->regs, ZDMA_CH_ISR, SRC_DSCR_DONE, true);

    /* Did we overflow?  */
    if (cnt != ARRAY_FIELD_EX32(s->regs, ZDMA_CH_IRQ_SRC_ACCT, CNT)) {
        ARRAY_FIELD_DP32(s->regs, ZDMA_CH_ISR, IRQ_SRC_ACCT_ERR, true);
    }
    zdma_ch_imr_update_irq(s);
}

static void zdma_dst_done(XlnxZDMA *s)
{
    unsigned int cnt;
    cnt = ARRAY_FIELD_EX32(s->regs, ZDMA_CH_IRQ_DST_ACCT, CNT);
    cnt++;
    ARRAY_FIELD_DP32(s->regs, ZDMA_CH_IRQ_DST_ACCT, CNT, cnt);
    ARRAY_FIELD_DP32(s->regs, ZDMA_CH_ISR, DST_DSCR_DONE, true);

    /* Did we overflow?  */
    if (cnt != ARRAY_FIELD_EX32(s->regs, ZDMA_CH_IRQ_DST_ACCT, CNT)) {
        ARRAY_FIELD_DP32(s->regs, ZDMA_CH_ISR, IRQ_DST_ACCT_ERR, true);
    }
    zdma_ch_imr_update_irq(s);
}

static uint64_t zdma_get_regaddr64(XlnxZDMA *s, unsigned int basereg)
{
    uint64_t addr;

    addr = s->regs[basereg + 1];
    addr <<= 32;
    addr |= s->regs[basereg];

    return addr;
}

static void zdma_put_regaddr64(XlnxZDMA *s, unsigned int basereg, uint64_t addr)
{
    s->regs[basereg] = addr;
    s->regs[basereg + 1] = addr >> 32;
}

static void zdma_load_descriptor_reg(XlnxZDMA *s, unsigned int reg,
                                     XlnxZDMADescr *descr)
{
    descr->addr = zdma_get_regaddr64(s, reg);
    descr->size = s->regs[reg + 2];
    descr->attr = s->regs[reg + 3];
}

static bool zdma_load_descriptor(XlnxZDMA *s, uint64_t addr,
                                 XlnxZDMADescr *descr)
{
    /* ZDMA descriptors must be aligned to their own size.  */
    if (addr % sizeof(XlnxZDMADescr)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "zdma: unaligned descriptor at %" PRIx64,
                      addr);
        memset(descr, 0x0, sizeof(XlnxZDMADescr));
        s->error = true;
        return false;
    }

    descr->addr = address_space_ldq_le(&s->dma_as, addr, s->attr, NULL);
    descr->size = address_space_ldl_le(&s->dma_as, addr + 8, s->attr, NULL);
    descr->attr = address_space_ldl_le(&s->dma_as, addr + 12, s->attr, NULL);
    return true;
}

static void zdma_load_src_descriptor(XlnxZDMA *s)
{
    uint64_t src_addr;
    unsigned int ptype = ARRAY_FIELD_EX32(s->regs, ZDMA_CH_CTRL0, POINT_TYPE);

    if (ptype == PT_REG) {
        zdma_load_descriptor_reg(s, R_ZDMA_CH_SRC_DSCR_WORD0, &s->dsc_src);
        return;
    }

    src_addr = zdma_get_regaddr64(s, R_ZDMA_CH_SRC_CUR_DSCR_LSB);

    if (!zdma_load_descriptor(s, src_addr, &s->dsc_src)) {
        ARRAY_FIELD_DP32(s->regs, ZDMA_CH_ISR, AXI_RD_SRC_DSCR, true);
    }
}

static void zdma_update_descr_addr(XlnxZDMA *s, bool type,
                                   unsigned int basereg)
{
    uint64_t addr, next;

    if (type == DTYPE_LINEAR) {
        addr = zdma_get_regaddr64(s, basereg);
        next = addr + sizeof(s->dsc_dst);
    } else {
        addr = zdma_get_regaddr64(s, basereg);
        addr += sizeof(s->dsc_dst);
        next = address_space_ldq_le(&s->dma_as, addr, s->attr, NULL);
    }

    zdma_put_regaddr64(s, basereg, next);
}

static void zdma_load_dst_descriptor(XlnxZDMA *s)
{
    uint64_t dst_addr;
    unsigned int ptype = ARRAY_FIELD_EX32(s->regs, ZDMA_CH_CTRL0, POINT_TYPE);
    bool dst_type;

    if (ptype == PT_REG) {
        zdma_load_descriptor_reg(s, R_ZDMA_CH_DST_DSCR_WORD0, &s->dsc_dst);
        return;
    }

    dst_addr = zdma_get_regaddr64(s, R_ZDMA_CH_DST_CUR_DSCR_LSB);

    if (!zdma_load_descriptor(s, dst_addr, &s->dsc_dst)) {
        ARRAY_FIELD_DP32(s->regs, ZDMA_CH_ISR, AXI_RD_DST_DSCR, true);
    }

    /* Advance the descriptor pointer.  */
    dst_type = FIELD_EX32(s->dsc_dst.words[3], ZDMA_CH_DST_DSCR_WORD3, TYPE);
    zdma_update_descr_addr(s, dst_type, R_ZDMA_CH_DST_CUR_DSCR_LSB);
}

static void zdma_write_dst(XlnxZDMA *s, uint8_t *buf, uint32_t len)
{
    uint32_t dst_size, dlen;
    bool dst_intr;
    unsigned int ptype = ARRAY_FIELD_EX32(s->regs, ZDMA_CH_CTRL0, POINT_TYPE);
    unsigned int rw_mode = ARRAY_FIELD_EX32(s->regs, ZDMA_CH_CTRL0, MODE);
    unsigned int burst_type = ARRAY_FIELD_EX32(s->regs, ZDMA_CH_DATA_ATTR,
                                               AWBURST);

    /* FIXED burst types are only supported in simple dma mode.  */
    if (ptype != PT_REG) {
        burst_type = AXI_BURST_INCR;
    }

    while (len) {
        dst_size = FIELD_EX32(s->dsc_dst.words[2], ZDMA_CH_DST_DSCR_WORD2,
                              SIZE);
        if (dst_size == 0 && ptype == PT_MEM) {
            zdma_load_dst_descriptor(s);
            dst_size = FIELD_EX32(s->dsc_dst.words[2], ZDMA_CH_DST_DSCR_WORD2,
                                  SIZE);
        }

        /* Match what hardware does by ignoring the dst_size and only using
         * the src size for Simple register mode.  */
        if (ptype == PT_REG && rw_mode != RW_MODE_WO) {
            dst_size = len;
        }

        dst_intr = FIELD_EX32(s->dsc_dst.words[3], ZDMA_CH_DST_DSCR_WORD3,
                              INTR);

        dlen = len > dst_size ? dst_size : len;
        if (burst_type == AXI_BURST_FIXED) {
            if (dlen > (s->cfg.bus_width / 8)) {
                dlen = s->cfg.bus_width / 8;
            }
        }

        address_space_write(&s->dma_as, s->dsc_dst.addr, s->attr, buf, dlen);
        if (burst_type == AXI_BURST_INCR) {
            s->dsc_dst.addr += dlen;
        }
        dst_size -= dlen;
        buf += dlen;
        len -= dlen;

        if (dst_size == 0 && dst_intr) {
            zdma_dst_done(s);
        }

        /* Write back to buffered descriptor.  */
        s->dsc_dst.words[2] = FIELD_DP32(s->dsc_dst.words[2],
                                         ZDMA_CH_DST_DSCR_WORD2,
                                         SIZE,
                                         dst_size);
    }
}

static void zdma_process_descr(XlnxZDMA *s)
{
    uint64_t src_addr;
    uint32_t src_size, len;
    unsigned int src_cmd;
    bool src_intr, src_type;
    unsigned int ptype = ARRAY_FIELD_EX32(s->regs, ZDMA_CH_CTRL0, POINT_TYPE);
    unsigned int rw_mode = ARRAY_FIELD_EX32(s->regs, ZDMA_CH_CTRL0, MODE);
    unsigned int burst_type = ARRAY_FIELD_EX32(s->regs, ZDMA_CH_DATA_ATTR,
                                               ARBURST);

    src_addr = s->dsc_src.addr;
    src_size = FIELD_EX32(s->dsc_src.words[2], ZDMA_CH_SRC_DSCR_WORD2, SIZE);
    src_cmd = FIELD_EX32(s->dsc_src.words[3], ZDMA_CH_SRC_DSCR_WORD3, CMD);
    src_type = FIELD_EX32(s->dsc_src.words[3], ZDMA_CH_SRC_DSCR_WORD3, TYPE);
    src_intr = FIELD_EX32(s->dsc_src.words[3], ZDMA_CH_SRC_DSCR_WORD3, INTR);

    /* FIXED burst types and non-rw modes are only supported in
     * simple dma mode.
     */
    if (ptype != PT_REG) {
        if (rw_mode != RW_MODE_RW) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "zDMA: rw-mode=%d but not simple DMA mode.\n",
                          rw_mode);
        }
        if (burst_type != AXI_BURST_INCR) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "zDMA: burst_type=%d but not simple DMA mode.\n",
                          burst_type);
        }
        burst_type = AXI_BURST_INCR;
        rw_mode = RW_MODE_RW;
    }

    if (rw_mode == RW_MODE_WO) {
        /* In Simple DMA Write-Only, we need to push DST size bytes
         * regardless of what SRC size is set to.  */
        src_size = FIELD_EX32(s->dsc_dst.words[2], ZDMA_CH_DST_DSCR_WORD2,
                              SIZE);
        memcpy(s->buf, &s->regs[R_ZDMA_CH_WR_ONLY_WORD0], s->cfg.bus_width / 8);
    }

    while (src_size) {
        len = src_size > ARRAY_SIZE(s->buf) ? ARRAY_SIZE(s->buf) : src_size;
        if (burst_type == AXI_BURST_FIXED) {
            if (len > (s->cfg.bus_width / 8)) {
                len = s->cfg.bus_width / 8;
            }
        }

        if (rw_mode == RW_MODE_WO) {
            if (len > s->cfg.bus_width / 8) {
                len = s->cfg.bus_width / 8;
            }
        } else {
            address_space_read(&s->dma_as, src_addr, s->attr, s->buf, len);
            if (burst_type == AXI_BURST_INCR) {
                src_addr += len;
            }
        }

        if (rw_mode != RW_MODE_RO) {
            zdma_write_dst(s, s->buf, len);
        }

        s->regs[R_ZDMA_CH_TOTAL_BYTE] += len;
        src_size -= len;
    }

    ARRAY_FIELD_DP32(s->regs, ZDMA_CH_ISR, DMA_DONE, true);

    if (src_intr) {
        zdma_src_done(s);
    }

    if (ptype == PT_REG || src_cmd == CMD_STOP) {
        ARRAY_FIELD_DP32(s->regs, ZDMA_CH_CTRL2, EN, 0);
        zdma_set_state(s, DISABLED);
    }

    if (src_cmd == CMD_HALT) {
        zdma_set_state(s, PAUSED);
        ARRAY_FIELD_DP32(s->regs, ZDMA_CH_ISR, DMA_PAUSE, 1);
        ARRAY_FIELD_DP32(s->regs, ZDMA_CH_ISR, DMA_DONE, false);
        zdma_ch_imr_update_irq(s);
        return;
    }

    zdma_update_descr_addr(s, src_type, R_ZDMA_CH_SRC_CUR_DSCR_LSB);
}

static void zdma_run(XlnxZDMA *s)
{
    while (s->state == ENABLED && !s->error) {
        zdma_load_src_descriptor(s);

        if (s->error) {
            zdma_set_state(s, DISABLED);
        } else {
            zdma_process_descr(s);
        }
    }

    zdma_ch_imr_update_irq(s);
}

static void zdma_update_descr_addr_from_start(XlnxZDMA *s)
{
    uint64_t src_addr, dst_addr;

    src_addr = zdma_get_regaddr64(s, R_ZDMA_CH_SRC_START_LSB);
    zdma_put_regaddr64(s, R_ZDMA_CH_SRC_CUR_DSCR_LSB, src_addr);
    dst_addr = zdma_get_regaddr64(s, R_ZDMA_CH_DST_START_LSB);
    zdma_put_regaddr64(s, R_ZDMA_CH_DST_CUR_DSCR_LSB, dst_addr);
    zdma_load_dst_descriptor(s);
}

static void zdma_ch_ctrlx_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxZDMA *s = XLNX_ZDMA(reg->opaque);

    if (ARRAY_FIELD_EX32(s->regs, ZDMA_CH_CTRL2, EN)) {
        s->error = false;

        if (s->state == PAUSED &&
            ARRAY_FIELD_EX32(s->regs, ZDMA_CH_CTRL0, CONT)) {
            if (ARRAY_FIELD_EX32(s->regs, ZDMA_CH_CTRL0, CONT_ADDR) == 1) {
                zdma_update_descr_addr_from_start(s);
            } else {
                bool src_type = FIELD_EX32(s->dsc_src.words[3],
                                       ZDMA_CH_SRC_DSCR_WORD3, TYPE);
                zdma_update_descr_addr(s, src_type,
                                          R_ZDMA_CH_SRC_CUR_DSCR_LSB);
            }
            ARRAY_FIELD_DP32(s->regs, ZDMA_CH_CTRL0, CONT, false);
            zdma_set_state(s, ENABLED);
        } else if (s->state == DISABLED) {
            zdma_update_descr_addr_from_start(s);
            zdma_set_state(s, ENABLED);
        }
    } else {
        /* Leave Paused state?  */
        if (s->state == PAUSED &&
            ARRAY_FIELD_EX32(s->regs, ZDMA_CH_CTRL0, CONT)) {
            zdma_set_state(s, DISABLED);
        }
    }

    zdma_run(s);
}

static RegisterAccessInfo zdma_regs_info[] = {
    {   .name = "ZDMA_ERR_CTRL",  .addr = A_ZDMA_ERR_CTRL,
        .rsvd = 0xfffffffe,
    },{ .name = "ZDMA_CH_ISR",  .addr = A_ZDMA_CH_ISR,
        .rsvd = 0xfffff000,
        .w1c = 0xfff,
        .post_write = zdma_ch_isr_postw,
    },{ .name = "ZDMA_CH_IMR",  .addr = A_ZDMA_CH_IMR,
        .reset = 0xfff,
        .rsvd = 0xfffff000,
        .ro = 0xfff,
    },{ .name = "ZDMA_CH_IEN",  .addr = A_ZDMA_CH_IEN,
        .rsvd = 0xfffff000,
        .pre_write = zdma_ch_ien_prew,
    },{ .name = "ZDMA_CH_IDS",  .addr = A_ZDMA_CH_IDS,
        .rsvd = 0xfffff000,
        .pre_write = zdma_ch_ids_prew,
    },{ .name = "ZDMA_CH_CTRL0",  .addr = A_ZDMA_CH_CTRL0,
        .reset = 0x80,
        .rsvd = 0xffffff01,
        .post_write = zdma_ch_ctrlx_postw,
    },{ .name = "ZDMA_CH_CTRL1",  .addr = A_ZDMA_CH_CTRL1,
        .reset = 0x3ff,
        .rsvd = 0xfffffc00,
    },{ .name = "ZDMA_CH_FCI",  .addr = A_ZDMA_CH_FCI,
        .rsvd = 0xffffffc0,
    },{ .name = "ZDMA_CH_STATUS",  .addr = A_ZDMA_CH_STATUS,
        .rsvd = 0xfffffffc,
        .ro = 0x3,
    },{ .name = "ZDMA_CH_DATA_ATTR",  .addr = A_ZDMA_CH_DATA_ATTR,
        .reset = 0x483d20f,
        .rsvd = 0xf0000000,
    },{ .name = "ZDMA_CH_DSCR_ATTR",  .addr = A_ZDMA_CH_DSCR_ATTR,
        .rsvd = 0xfffffe00,
    },{ .name = "ZDMA_CH_SRC_DSCR_WORD0",  .addr = A_ZDMA_CH_SRC_DSCR_WORD0,
    },{ .name = "ZDMA_CH_SRC_DSCR_WORD1",  .addr = A_ZDMA_CH_SRC_DSCR_WORD1,
        .rsvd = 0xfffe0000,
    },{ .name = "ZDMA_CH_SRC_DSCR_WORD2",  .addr = A_ZDMA_CH_SRC_DSCR_WORD2,
        .rsvd = 0xc0000000,
    },{ .name = "ZDMA_CH_SRC_DSCR_WORD3",  .addr = A_ZDMA_CH_SRC_DSCR_WORD3,
        .rsvd = 0xffffffe0,
    },{ .name = "ZDMA_CH_DST_DSCR_WORD0",  .addr = A_ZDMA_CH_DST_DSCR_WORD0,
    },{ .name = "ZDMA_CH_DST_DSCR_WORD1",  .addr = A_ZDMA_CH_DST_DSCR_WORD1,
        .rsvd = 0xfffe0000,
    },{ .name = "ZDMA_CH_DST_DSCR_WORD2",  .addr = A_ZDMA_CH_DST_DSCR_WORD2,
        .rsvd = 0xc0000000,
    },{ .name = "ZDMA_CH_DST_DSCR_WORD3",  .addr = A_ZDMA_CH_DST_DSCR_WORD3,
        .rsvd = 0xfffffffa,
    },{ .name = "ZDMA_CH_WR_ONLY_WORD0",  .addr = A_ZDMA_CH_WR_ONLY_WORD0,
    },{ .name = "ZDMA_CH_WR_ONLY_WORD1",  .addr = A_ZDMA_CH_WR_ONLY_WORD1,
    },{ .name = "ZDMA_CH_WR_ONLY_WORD2",  .addr = A_ZDMA_CH_WR_ONLY_WORD2,
    },{ .name = "ZDMA_CH_WR_ONLY_WORD3",  .addr = A_ZDMA_CH_WR_ONLY_WORD3,
    },{ .name = "ZDMA_CH_SRC_START_LSB",  .addr = A_ZDMA_CH_SRC_START_LSB,
    },{ .name = "ZDMA_CH_SRC_START_MSB",  .addr = A_ZDMA_CH_SRC_START_MSB,
        .rsvd = 0xfffe0000,
    },{ .name = "ZDMA_CH_DST_START_LSB",  .addr = A_ZDMA_CH_DST_START_LSB,
    },{ .name = "ZDMA_CH_DST_START_MSB",  .addr = A_ZDMA_CH_DST_START_MSB,
        .rsvd = 0xfffe0000,
    },{ .name = "ZDMA_CH_SRC_CUR_PYLD_LSB",  .addr = A_ZDMA_CH_SRC_CUR_PYLD_LSB,
        .ro = 0xffffffff,
    },{ .name = "ZDMA_CH_SRC_CUR_PYLD_MSB",  .addr = A_ZDMA_CH_SRC_CUR_PYLD_MSB,
        .rsvd = 0xfffe0000,
        .ro = 0x1ffff,
    },{ .name = "ZDMA_CH_DST_CUR_PYLD_LSB",  .addr = A_ZDMA_CH_DST_CUR_PYLD_LSB,
        .ro = 0xffffffff,
    },{ .name = "ZDMA_CH_DST_CUR_PYLD_MSB",  .addr = A_ZDMA_CH_DST_CUR_PYLD_MSB,
        .rsvd = 0xfffe0000,
        .ro = 0x1ffff,
    },{ .name = "ZDMA_CH_SRC_CUR_DSCR_LSB",  .addr = A_ZDMA_CH_SRC_CUR_DSCR_LSB,
        .ro = 0xffffffff,
    },{ .name = "ZDMA_CH_SRC_CUR_DSCR_MSB",  .addr = A_ZDMA_CH_SRC_CUR_DSCR_MSB,
        .rsvd = 0xfffe0000,
        .ro = 0x1ffff,
    },{ .name = "ZDMA_CH_DST_CUR_DSCR_LSB",  .addr = A_ZDMA_CH_DST_CUR_DSCR_LSB,
        .ro = 0xffffffff,
    },{ .name = "ZDMA_CH_DST_CUR_DSCR_MSB",  .addr = A_ZDMA_CH_DST_CUR_DSCR_MSB,
        .rsvd = 0xfffe0000,
        .ro = 0x1ffff,
    },{ .name = "ZDMA_CH_TOTAL_BYTE",  .addr = A_ZDMA_CH_TOTAL_BYTE,
        .w1c = 0xffffffff,
    },{ .name = "ZDMA_CH_RATE_CNTL",  .addr = A_ZDMA_CH_RATE_CNTL,
        .rsvd = 0xfffff000,
    },{ .name = "ZDMA_CH_IRQ_SRC_ACCT",  .addr = A_ZDMA_CH_IRQ_SRC_ACCT,
        .rsvd = 0xffffff00,
        .ro = 0xff,
        .cor = 0xff,
    },{ .name = "ZDMA_CH_IRQ_DST_ACCT",  .addr = A_ZDMA_CH_IRQ_DST_ACCT,
        .rsvd = 0xffffff00,
        .ro = 0xff,
        .cor = 0xff,
    },{ .name = "ZDMA_CH_DBG0",  .addr = A_ZDMA_CH_DBG0,
        .rsvd = 0xfffffe00,
        .ro = 0x1ff,

        /*
         * There's SW out there that will check the debug regs for free space.
         * Claim that we always have 0x100 free.
         */
        .reset = 0x100
    },{ .name = "ZDMA_CH_DBG1",  .addr = A_ZDMA_CH_DBG1,
        .rsvd = 0xfffffe00,
        .ro = 0x1ff,
    },{ .name = "ZDMA_CH_CTRL2",  .addr = A_ZDMA_CH_CTRL2,
        .rsvd = 0xfffffffe,
        .post_write = zdma_ch_ctrlx_postw,
    }
};

static void zdma_reset(DeviceState *dev)
{
    XlnxZDMA *s = XLNX_ZDMA(dev);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }

    zdma_ch_imr_update_irq(s);
}

static uint64_t zdma_read(void *opaque, hwaddr addr, unsigned size)
{
    XlnxZDMA *s = XLNX_ZDMA(opaque);
    RegisterInfo *r = &s->regs_info[addr / 4];

    if (!r->data) {
        char *path = object_get_canonical_path(OBJECT(s));
        qemu_log("%s: Decode error: read from %" HWADDR_PRIx "\n",
                 path,
                 addr);
        g_free(path);
        ARRAY_FIELD_DP32(s->regs, ZDMA_CH_ISR, INV_APB, true);
        zdma_ch_imr_update_irq(s);
        return 0;
    }
    return register_read(r, ~0, NULL, false);
}

static void zdma_write(void *opaque, hwaddr addr, uint64_t value,
                      unsigned size)
{
    XlnxZDMA *s = XLNX_ZDMA(opaque);
    RegisterInfo *r = &s->regs_info[addr / 4];

    if (!r->data) {
        char *path = object_get_canonical_path(OBJECT(s));
        qemu_log("%s: Decode error: write to %" HWADDR_PRIx "=%" PRIx64 "\n",
                 path,
                 addr, value);
        g_free(path);
        ARRAY_FIELD_DP32(s->regs, ZDMA_CH_ISR, INV_APB, true);
        zdma_ch_imr_update_irq(s);
        return;
    }
    register_write(r, value, ~0, NULL, false);
}

static const MemoryRegionOps zdma_ops = {
    .read = zdma_read,
    .write = zdma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void zdma_realize(DeviceState *dev, Error **errp)
{
    XlnxZDMA *s = XLNX_ZDMA(dev);
    unsigned int i;

    if (!s->dma_mr) {
        error_setg(errp, TYPE_XLNX_ZDMA " 'dma' link not set");
        return;
    }
    address_space_init(&s->dma_as, s->dma_mr, "zdma-dma");

    for (i = 0; i < ARRAY_SIZE(zdma_regs_info); ++i) {
        RegisterInfo *r = &s->regs_info[zdma_regs_info[i].addr / 4];

        *r = (RegisterInfo) {
            .data = (uint8_t *)&s->regs[
                    zdma_regs_info[i].addr / 4],
            .data_size = sizeof(uint32_t),
            .access = &zdma_regs_info[i],
            .opaque = s,
        };
    }

    s->attr = MEMTXATTRS_UNSPECIFIED;
}

static void zdma_init(Object *obj)
{
    XlnxZDMA *s = XLNX_ZDMA(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &zdma_ops, s,
                          TYPE_XLNX_ZDMA, ZDMA_R_MAX * 4);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq_zdma_ch_imr);
}

static const VMStateDescription vmstate_zdma = {
    .name = TYPE_XLNX_ZDMA,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, XlnxZDMA, ZDMA_R_MAX),
        VMSTATE_UINT32(state, XlnxZDMA),
        VMSTATE_UINT32_ARRAY(dsc_src.words, XlnxZDMA, 4),
        VMSTATE_UINT32_ARRAY(dsc_dst.words, XlnxZDMA, 4),
        VMSTATE_END_OF_LIST(),
    }
};

static const Property zdma_props[] = {
    DEFINE_PROP_UINT32("bus-width", XlnxZDMA, cfg.bus_width, 64),
    DEFINE_PROP_LINK("dma", XlnxZDMA, dma_mr,
                     TYPE_MEMORY_REGION, MemoryRegion *),
};

static void zdma_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, zdma_reset);
    dc->realize = zdma_realize;
    device_class_set_props(dc, zdma_props);
    dc->vmsd = &vmstate_zdma;
}

static const TypeInfo zdma_info = {
    .name          = TYPE_XLNX_ZDMA,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XlnxZDMA),
    .class_init    = zdma_class_init,
    .instance_init = zdma_init,
};

static void zdma_register_types(void)
{
    type_register_static(&zdma_info);
}

type_init(zdma_register_types)
