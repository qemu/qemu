/*
 * QEMU model of the Xilinx Zynq SPI controller
 *
 * Copyright (c) 2012 Peter A. G. Crosthwaite
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
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/ptimer.h"
#include "hw/qdev-properties.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/bitops.h"
#include "hw/ssi/xilinx_spips.h"
#include "qapi/error.h"
#include "hw/register.h"
#include "sysemu/dma.h"
#include "migration/blocker.h"
#include "migration/vmstate.h"

#ifndef XILINX_SPIPS_ERR_DEBUG
#define XILINX_SPIPS_ERR_DEBUG 0
#endif

#define DB_PRINT_L(level, ...) do { \
    if (XILINX_SPIPS_ERR_DEBUG > (level)) { \
        fprintf(stderr,  ": %s: ", __func__); \
        fprintf(stderr, ## __VA_ARGS__); \
    } \
} while (0)

/* config register */
#define R_CONFIG            (0x00 / 4)
#define IFMODE              (1U << 31)
#define R_CONFIG_ENDIAN     (1 << 26)
#define MODEFAIL_GEN_EN     (1 << 17)
#define MAN_START_COM       (1 << 16)
#define MAN_START_EN        (1 << 15)
#define MANUAL_CS           (1 << 14)
#define CS                  (0xF << 10)
#define CS_SHIFT            (10)
#define PERI_SEL            (1 << 9)
#define REF_CLK             (1 << 8)
#define FIFO_WIDTH          (3 << 6)
#define BAUD_RATE_DIV       (7 << 3)
#define CLK_PH              (1 << 2)
#define CLK_POL             (1 << 1)
#define MODE_SEL            (1 << 0)
#define R_CONFIG_RSVD       (0x7bf40000)

/* interrupt mechanism */
#define R_INTR_STATUS       (0x04 / 4)
#define R_INTR_STATUS_RESET (0x104)
#define R_INTR_EN           (0x08 / 4)
#define R_INTR_DIS          (0x0C / 4)
#define R_INTR_MASK         (0x10 / 4)
#define IXR_TX_FIFO_UNDERFLOW   (1 << 6)
/* Poll timeout not implemented */
#define IXR_RX_FIFO_EMPTY       (1 << 11)
#define IXR_GENERIC_FIFO_FULL   (1 << 10)
#define IXR_GENERIC_FIFO_NOT_FULL (1 << 9)
#define IXR_TX_FIFO_EMPTY       (1 << 8)
#define IXR_GENERIC_FIFO_EMPTY  (1 << 7)
#define IXR_RX_FIFO_FULL        (1 << 5)
#define IXR_RX_FIFO_NOT_EMPTY   (1 << 4)
#define IXR_TX_FIFO_FULL        (1 << 3)
#define IXR_TX_FIFO_NOT_FULL    (1 << 2)
#define IXR_TX_FIFO_MODE_FAIL   (1 << 1)
#define IXR_RX_FIFO_OVERFLOW    (1 << 0)
#define IXR_ALL                 ((1 << 13) - 1)
#define GQSPI_IXR_MASK          0xFBE
#define IXR_SELF_CLEAR \
(IXR_GENERIC_FIFO_EMPTY \
| IXR_GENERIC_FIFO_FULL  \
| IXR_GENERIC_FIFO_NOT_FULL \
| IXR_TX_FIFO_EMPTY \
| IXR_TX_FIFO_FULL  \
| IXR_TX_FIFO_NOT_FULL \
| IXR_RX_FIFO_EMPTY \
| IXR_RX_FIFO_FULL  \
| IXR_RX_FIFO_NOT_EMPTY)

#define R_EN                (0x14 / 4)
#define R_DELAY             (0x18 / 4)
#define R_TX_DATA           (0x1C / 4)
#define R_RX_DATA           (0x20 / 4)
#define R_SLAVE_IDLE_COUNT  (0x24 / 4)
#define R_TX_THRES          (0x28 / 4)
#define R_RX_THRES          (0x2C / 4)
#define R_GPIO              (0x30 / 4)
#define R_LPBK_DLY_ADJ      (0x38 / 4)
#define R_LPBK_DLY_ADJ_RESET (0x33)
#define R_IOU_TAPDLY_BYPASS (0x3C / 4)
#define R_TXD1              (0x80 / 4)
#define R_TXD2              (0x84 / 4)
#define R_TXD3              (0x88 / 4)

#define R_LQSPI_CFG         (0xa0 / 4)
#define R_LQSPI_CFG_RESET       0x03A002EB
#define LQSPI_CFG_LQ_MODE       (1U << 31)
#define LQSPI_CFG_TWO_MEM       (1 << 30)
#define LQSPI_CFG_SEP_BUS       (1 << 29)
#define LQSPI_CFG_U_PAGE        (1 << 28)
#define LQSPI_CFG_ADDR4         (1 << 27)
#define LQSPI_CFG_MODE_EN       (1 << 25)
#define LQSPI_CFG_MODE_WIDTH    8
#define LQSPI_CFG_MODE_SHIFT    16
#define LQSPI_CFG_DUMMY_WIDTH   3
#define LQSPI_CFG_DUMMY_SHIFT   8
#define LQSPI_CFG_INST_CODE     0xFF

#define R_CMND        (0xc0 / 4)
    #define R_CMND_RXFIFO_DRAIN   (1 << 19)
    FIELD(CMND, PARTIAL_BYTE_LEN, 16, 3)
#define R_CMND_EXT_ADD        (1 << 15)
    FIELD(CMND, RX_DISCARD, 8, 7)
    FIELD(CMND, DUMMY_CYCLES, 2, 6)
#define R_CMND_DMA_EN         (1 << 1)
#define R_CMND_PUSH_WAIT      (1 << 0)
#define R_TRANSFER_SIZE     (0xc4 / 4)
#define R_LQSPI_STS         (0xA4 / 4)
#define LQSPI_STS_WR_RECVD      (1 << 1)

#define R_DUMMY_CYCLE_EN    (0xC8 / 4)
#define R_ECO               (0xF8 / 4)
#define R_MOD_ID            (0xFC / 4)

#define R_GQSPI_SELECT          (0x144 / 4)
    FIELD(GQSPI_SELECT, GENERIC_QSPI_EN, 0, 1)
#define R_GQSPI_ISR         (0x104 / 4)
#define R_GQSPI_IER         (0x108 / 4)
#define R_GQSPI_IDR         (0x10c / 4)
#define R_GQSPI_IMR         (0x110 / 4)
#define R_GQSPI_IMR_RESET   (0xfbe)
#define R_GQSPI_TX_THRESH   (0x128 / 4)
#define R_GQSPI_RX_THRESH   (0x12c / 4)
#define R_GQSPI_GPIO (0x130 / 4)
#define R_GQSPI_LPBK_DLY_ADJ (0x138 / 4)
#define R_GQSPI_LPBK_DLY_ADJ_RESET (0x33)
#define R_GQSPI_CNFG        (0x100 / 4)
    FIELD(GQSPI_CNFG, MODE_EN, 30, 2)
    FIELD(GQSPI_CNFG, GEN_FIFO_START_MODE, 29, 1)
    FIELD(GQSPI_CNFG, GEN_FIFO_START, 28, 1)
    FIELD(GQSPI_CNFG, ENDIAN, 26, 1)
    /* Poll timeout not implemented */
    FIELD(GQSPI_CNFG, EN_POLL_TIMEOUT, 20, 1)
    /* QEMU doesnt care about any of these last three */
    FIELD(GQSPI_CNFG, BR, 3, 3)
    FIELD(GQSPI_CNFG, CPH, 2, 1)
    FIELD(GQSPI_CNFG, CPL, 1, 1)
#define R_GQSPI_GEN_FIFO        (0x140 / 4)
#define R_GQSPI_TXD             (0x11c / 4)
#define R_GQSPI_RXD             (0x120 / 4)
#define R_GQSPI_FIFO_CTRL       (0x14c / 4)
    FIELD(GQSPI_FIFO_CTRL, RX_FIFO_RESET, 2, 1)
    FIELD(GQSPI_FIFO_CTRL, TX_FIFO_RESET, 1, 1)
    FIELD(GQSPI_FIFO_CTRL, GENERIC_FIFO_RESET, 0, 1)
#define R_GQSPI_GFIFO_THRESH    (0x150 / 4)
#define R_GQSPI_DATA_STS (0x15c / 4)
/* We use the snapshot register to hold the core state for the currently
 * or most recently executed command. So the generic fifo format is defined
 * for the snapshot register
 */
#define R_GQSPI_GF_SNAPSHOT (0x160 / 4)
    FIELD(GQSPI_GF_SNAPSHOT, POLL, 19, 1)
    FIELD(GQSPI_GF_SNAPSHOT, STRIPE, 18, 1)
    FIELD(GQSPI_GF_SNAPSHOT, RECIEVE, 17, 1)
    FIELD(GQSPI_GF_SNAPSHOT, TRANSMIT, 16, 1)
    FIELD(GQSPI_GF_SNAPSHOT, DATA_BUS_SELECT, 14, 2)
    FIELD(GQSPI_GF_SNAPSHOT, CHIP_SELECT, 12, 2)
    FIELD(GQSPI_GF_SNAPSHOT, SPI_MODE, 10, 2)
    FIELD(GQSPI_GF_SNAPSHOT, EXPONENT, 9, 1)
    FIELD(GQSPI_GF_SNAPSHOT, DATA_XFER, 8, 1)
    FIELD(GQSPI_GF_SNAPSHOT, IMMEDIATE_DATA, 0, 8)
#define R_GQSPI_MOD_ID        (0x1fc / 4)
#define R_GQSPI_MOD_ID_RESET  (0x10a0000)

#define R_QSPIDMA_DST_CTRL         (0x80c / 4)
#define R_QSPIDMA_DST_CTRL_RESET   (0x803ffa00)
#define R_QSPIDMA_DST_I_MASK       (0x820 / 4)
#define R_QSPIDMA_DST_I_MASK_RESET (0xfe)
#define R_QSPIDMA_DST_CTRL2        (0x824 / 4)
#define R_QSPIDMA_DST_CTRL2_RESET  (0x081bfff8)

/* size of TXRX FIFOs */
#define RXFF_A          (128)
#define TXFF_A          (128)

#define RXFF_A_Q          (64 * 4)
#define TXFF_A_Q          (64 * 4)

/* 16MB per linear region */
#define LQSPI_ADDRESS_BITS 24

#define SNOOP_CHECKING 0xFF
#define SNOOP_ADDR 0xF0
#define SNOOP_NONE 0xEE
#define SNOOP_STRIPING 0

#define MIN_NUM_BUSSES 1
#define MAX_NUM_BUSSES 2

static inline int num_effective_busses(XilinxSPIPS *s)
{
    return (s->regs[R_LQSPI_CFG] & LQSPI_CFG_SEP_BUS &&
            s->regs[R_LQSPI_CFG] & LQSPI_CFG_TWO_MEM) ? s->num_busses : 1;
}

static void xilinx_spips_update_cs(XilinxSPIPS *s, int field)
{
    int i;

    for (i = 0; i < s->num_cs * s->num_busses; i++) {
        bool old_state = s->cs_lines_state[i];
        bool new_state = field & (1 << i);

        if (old_state != new_state) {
            s->cs_lines_state[i] = new_state;
            s->rx_discard = ARRAY_FIELD_EX32(s->regs, CMND, RX_DISCARD);
            DB_PRINT_L(1, "%sselecting slave %d\n", new_state ? "" : "de", i);
        }
        qemu_set_irq(s->cs_lines[i], !new_state);
    }
    if (!(field & ((1 << (s->num_cs * s->num_busses)) - 1))) {
        s->snoop_state = SNOOP_CHECKING;
        s->cmd_dummies = 0;
        s->link_state = 1;
        s->link_state_next = 1;
        s->link_state_next_when = 0;
        DB_PRINT_L(1, "moving to snoop check state\n");
    }
}

static void xlnx_zynqmp_qspips_update_cs_lines(XlnxZynqMPQSPIPS *s)
{
    if (s->regs[R_GQSPI_GF_SNAPSHOT]) {
        int field = ARRAY_FIELD_EX32(s->regs, GQSPI_GF_SNAPSHOT, CHIP_SELECT);
        bool upper_cs_sel = field & (1 << 1);
        bool lower_cs_sel = field & 1;
        bool bus0_enabled;
        bool bus1_enabled;
        uint8_t buses;
        int cs = 0;

        buses = ARRAY_FIELD_EX32(s->regs, GQSPI_GF_SNAPSHOT, DATA_BUS_SELECT);
        bus0_enabled = buses & 1;
        bus1_enabled = buses & (1 << 1);

        if (bus0_enabled && bus1_enabled) {
            if (lower_cs_sel) {
                cs |= 1;
            }
            if (upper_cs_sel) {
                cs |= 1 << 3;
            }
        } else if (bus0_enabled) {
            if (lower_cs_sel) {
                cs |= 1;
            }
            if (upper_cs_sel) {
                cs |= 1 << 1;
            }
        } else if (bus1_enabled) {
            if (lower_cs_sel) {
                cs |= 1 << 2;
            }
            if (upper_cs_sel) {
                cs |= 1 << 3;
            }
        }
        xilinx_spips_update_cs(XILINX_SPIPS(s), cs);
    }
}

static void xilinx_spips_update_cs_lines(XilinxSPIPS *s)
{
    int field = ~((s->regs[R_CONFIG] & CS) >> CS_SHIFT);

    /* In dual parallel, mirror low CS to both */
    if (num_effective_busses(s) == 2) {
        /* Single bit chip-select for qspi */
        field &= 0x1;
        field |= field << 3;
    /* Dual stack U-Page */
    } else if (s->regs[R_LQSPI_CFG] & LQSPI_CFG_TWO_MEM &&
               s->regs[R_LQSPI_STS] & LQSPI_CFG_U_PAGE) {
        /* Single bit chip-select for qspi */
        field &= 0x1;
        /* change from CS0 to CS1 */
        field <<= 1;
    }
    /* Auto CS */
    if (!(s->regs[R_CONFIG] & MANUAL_CS) &&
        fifo8_is_empty(&s->tx_fifo)) {
        field = 0;
    }
    xilinx_spips_update_cs(s, field);
}

static void xilinx_spips_update_ixr(XilinxSPIPS *s)
{
    if (!(s->regs[R_LQSPI_CFG] & LQSPI_CFG_LQ_MODE)) {
        s->regs[R_INTR_STATUS] &= ~IXR_SELF_CLEAR;
        s->regs[R_INTR_STATUS] |=
            (fifo8_is_full(&s->rx_fifo) ? IXR_RX_FIFO_FULL : 0) |
            (s->rx_fifo.num >= s->regs[R_RX_THRES] ?
                                    IXR_RX_FIFO_NOT_EMPTY : 0) |
            (fifo8_is_full(&s->tx_fifo) ? IXR_TX_FIFO_FULL : 0) |
            (fifo8_is_empty(&s->tx_fifo) ? IXR_TX_FIFO_EMPTY : 0) |
            (s->tx_fifo.num < s->regs[R_TX_THRES] ? IXR_TX_FIFO_NOT_FULL : 0);
    }
    int new_irqline = !!(s->regs[R_INTR_MASK] & s->regs[R_INTR_STATUS] &
                                                                IXR_ALL);
    if (new_irqline != s->irqline) {
        s->irqline = new_irqline;
        qemu_set_irq(s->irq, s->irqline);
    }
}

static void xlnx_zynqmp_qspips_update_ixr(XlnxZynqMPQSPIPS *s)
{
    uint32_t gqspi_int;
    int new_irqline;

    s->regs[R_GQSPI_ISR] &= ~IXR_SELF_CLEAR;
    s->regs[R_GQSPI_ISR] |=
        (fifo32_is_empty(&s->fifo_g) ? IXR_GENERIC_FIFO_EMPTY : 0) |
        (fifo32_is_full(&s->fifo_g) ? IXR_GENERIC_FIFO_FULL : 0) |
        (s->fifo_g.fifo.num < s->regs[R_GQSPI_GFIFO_THRESH] ?
                                    IXR_GENERIC_FIFO_NOT_FULL : 0) |
        (fifo8_is_empty(&s->rx_fifo_g) ? IXR_RX_FIFO_EMPTY : 0) |
        (fifo8_is_full(&s->rx_fifo_g) ? IXR_RX_FIFO_FULL : 0) |
        (s->rx_fifo_g.num >= s->regs[R_GQSPI_RX_THRESH] ?
                                    IXR_RX_FIFO_NOT_EMPTY : 0) |
        (fifo8_is_empty(&s->tx_fifo_g) ? IXR_TX_FIFO_EMPTY : 0) |
        (fifo8_is_full(&s->tx_fifo_g) ? IXR_TX_FIFO_FULL : 0) |
        (s->tx_fifo_g.num < s->regs[R_GQSPI_TX_THRESH] ?
                                    IXR_TX_FIFO_NOT_FULL : 0);

    /* GQSPI Interrupt Trigger Status */
    gqspi_int = (~s->regs[R_GQSPI_IMR]) & s->regs[R_GQSPI_ISR] & GQSPI_IXR_MASK;
    new_irqline = !!(gqspi_int & IXR_ALL);

    /* drive external interrupt pin */
    if (new_irqline != s->gqspi_irqline) {
        s->gqspi_irqline = new_irqline;
        qemu_set_irq(XILINX_SPIPS(s)->irq, s->gqspi_irqline);
    }
}

static void xilinx_spips_reset(DeviceState *d)
{
    XilinxSPIPS *s = XILINX_SPIPS(d);

    memset(s->regs, 0, sizeof(s->regs));

    fifo8_reset(&s->rx_fifo);
    fifo8_reset(&s->rx_fifo);
    /* non zero resets */
    s->regs[R_CONFIG] |= MODEFAIL_GEN_EN;
    s->regs[R_SLAVE_IDLE_COUNT] = 0xFF;
    s->regs[R_TX_THRES] = 1;
    s->regs[R_RX_THRES] = 1;
    /* FIXME: move magic number definition somewhere sensible */
    s->regs[R_MOD_ID] = 0x01090106;
    s->regs[R_LQSPI_CFG] = R_LQSPI_CFG_RESET;
    s->link_state = 1;
    s->link_state_next = 1;
    s->link_state_next_when = 0;
    s->snoop_state = SNOOP_CHECKING;
    s->cmd_dummies = 0;
    s->man_start_com = false;
    xilinx_spips_update_ixr(s);
    xilinx_spips_update_cs_lines(s);
}

static void xlnx_zynqmp_qspips_reset(DeviceState *d)
{
    XlnxZynqMPQSPIPS *s = XLNX_ZYNQMP_QSPIPS(d);

    xilinx_spips_reset(d);

    memset(s->regs, 0, sizeof(s->regs));

    fifo8_reset(&s->rx_fifo_g);
    fifo8_reset(&s->rx_fifo_g);
    fifo32_reset(&s->fifo_g);
    s->regs[R_INTR_STATUS] = R_INTR_STATUS_RESET;
    s->regs[R_GPIO] = 1;
    s->regs[R_LPBK_DLY_ADJ] = R_LPBK_DLY_ADJ_RESET;
    s->regs[R_GQSPI_GFIFO_THRESH] = 0x10;
    s->regs[R_MOD_ID] = 0x01090101;
    s->regs[R_GQSPI_IMR] = R_GQSPI_IMR_RESET;
    s->regs[R_GQSPI_TX_THRESH] = 1;
    s->regs[R_GQSPI_RX_THRESH] = 1;
    s->regs[R_GQSPI_GPIO] = 1;
    s->regs[R_GQSPI_LPBK_DLY_ADJ] = R_GQSPI_LPBK_DLY_ADJ_RESET;
    s->regs[R_GQSPI_MOD_ID] = R_GQSPI_MOD_ID_RESET;
    s->regs[R_QSPIDMA_DST_CTRL] = R_QSPIDMA_DST_CTRL_RESET;
    s->regs[R_QSPIDMA_DST_I_MASK] = R_QSPIDMA_DST_I_MASK_RESET;
    s->regs[R_QSPIDMA_DST_CTRL2] = R_QSPIDMA_DST_CTRL2_RESET;
    s->man_start_com_g = false;
    s->gqspi_irqline = 0;
    xlnx_zynqmp_qspips_update_ixr(s);
}

/* N way (num) in place bit striper. Lay out row wise bits (MSB to LSB)
 * column wise (from element 0 to N-1). num is the length of x, and dir
 * reverses the direction of the transform. Best illustrated by example:
 * Each digit in the below array is a single bit (num == 3):
 *
 * {{ 76543210, }  ----- stripe (dir == false) -----> {{ 741gdaFC, }
 *  { hgfedcba, }                                      { 630fcHEB, }
 *  { HGFEDCBA, }} <---- upstripe (dir == true) -----  { 52hebGDA, }}
 */

static inline void stripe8(uint8_t *x, int num, bool dir)
{
    uint8_t r[MAX_NUM_BUSSES];
    int idx[2] = {0, 0};
    int bit[2] = {0, 7};
    int d = dir;

    assert(num <= MAX_NUM_BUSSES);
    memset(r, 0, sizeof(uint8_t) * num);

    for (idx[0] = 0; idx[0] < num; ++idx[0]) {
        for (bit[0] = 7; bit[0] >= 0; bit[0]--) {
            r[idx[!d]] |= x[idx[d]] & 1 << bit[d] ? 1 << bit[!d] : 0;
            idx[1] = (idx[1] + 1) % num;
            if (!idx[1]) {
                bit[1]--;
            }
        }
    }
    memcpy(x, r, sizeof(uint8_t) * num);
}

static void xlnx_zynqmp_qspips_flush_fifo_g(XlnxZynqMPQSPIPS *s)
{
    while (s->regs[R_GQSPI_DATA_STS] || !fifo32_is_empty(&s->fifo_g)) {
        uint8_t tx_rx[2] = { 0 };
        int num_stripes = 1;
        uint8_t busses;
        int i;

        if (!s->regs[R_GQSPI_DATA_STS]) {
            uint8_t imm;

            s->regs[R_GQSPI_GF_SNAPSHOT] = fifo32_pop(&s->fifo_g);
            DB_PRINT_L(0, "GQSPI command: %x\n", s->regs[R_GQSPI_GF_SNAPSHOT]);
            if (!s->regs[R_GQSPI_GF_SNAPSHOT]) {
                DB_PRINT_L(0, "Dummy GQSPI Delay Command Entry, Do nothing");
                continue;
            }
            xlnx_zynqmp_qspips_update_cs_lines(s);

            imm = ARRAY_FIELD_EX32(s->regs, GQSPI_GF_SNAPSHOT, IMMEDIATE_DATA);
            if (!ARRAY_FIELD_EX32(s->regs, GQSPI_GF_SNAPSHOT, DATA_XFER)) {
                /* immedate transfer */
                if (ARRAY_FIELD_EX32(s->regs, GQSPI_GF_SNAPSHOT, TRANSMIT) ||
                    ARRAY_FIELD_EX32(s->regs, GQSPI_GF_SNAPSHOT, RECIEVE)) {
                    s->regs[R_GQSPI_DATA_STS] = 1;
                /* CS setup/hold - do nothing */
                } else {
                    s->regs[R_GQSPI_DATA_STS] = 0;
                }
            } else if (ARRAY_FIELD_EX32(s->regs, GQSPI_GF_SNAPSHOT, EXPONENT)) {
                if (imm > 31) {
                    qemu_log_mask(LOG_UNIMP, "QSPI exponential transfer too"
                                  " long - 2 ^ %" PRId8 " requested\n", imm);
                }
                s->regs[R_GQSPI_DATA_STS] = 1ul << imm;
            } else {
                s->regs[R_GQSPI_DATA_STS] = imm;
            }
        }
        /* Zero length transfer check */
        if (!s->regs[R_GQSPI_DATA_STS]) {
            continue;
        }
        if (ARRAY_FIELD_EX32(s->regs, GQSPI_GF_SNAPSHOT, RECIEVE) &&
            fifo8_is_full(&s->rx_fifo_g)) {
            /* No space in RX fifo for transfer - try again later */
            return;
        }
        if (ARRAY_FIELD_EX32(s->regs, GQSPI_GF_SNAPSHOT, STRIPE) &&
            (ARRAY_FIELD_EX32(s->regs, GQSPI_GF_SNAPSHOT, TRANSMIT) ||
             ARRAY_FIELD_EX32(s->regs, GQSPI_GF_SNAPSHOT, RECIEVE))) {
            num_stripes = 2;
        }
        if (!ARRAY_FIELD_EX32(s->regs, GQSPI_GF_SNAPSHOT, DATA_XFER)) {
            tx_rx[0] = ARRAY_FIELD_EX32(s->regs,
                                        GQSPI_GF_SNAPSHOT, IMMEDIATE_DATA);
        } else if (ARRAY_FIELD_EX32(s->regs, GQSPI_GF_SNAPSHOT, TRANSMIT)) {
            for (i = 0; i < num_stripes; ++i) {
                if (!fifo8_is_empty(&s->tx_fifo_g)) {
                    tx_rx[i] = fifo8_pop(&s->tx_fifo_g);
                    s->tx_fifo_g_align++;
                } else {
                    return;
                }
            }
        }
        if (num_stripes == 1) {
            /* mirror */
            tx_rx[1] = tx_rx[0];
        }
        busses = ARRAY_FIELD_EX32(s->regs, GQSPI_GF_SNAPSHOT, DATA_BUS_SELECT);
        for (i = 0; i < 2; ++i) {
            DB_PRINT_L(1, "bus %d tx = %02x\n", i, tx_rx[i]);
            tx_rx[i] = ssi_transfer(XILINX_SPIPS(s)->spi[i], tx_rx[i]);
            DB_PRINT_L(1, "bus %d rx = %02x\n", i, tx_rx[i]);
        }
        if (s->regs[R_GQSPI_DATA_STS] > 1 &&
            busses == 0x3 && num_stripes == 2) {
            s->regs[R_GQSPI_DATA_STS] -= 2;
        } else if (s->regs[R_GQSPI_DATA_STS] > 0) {
            s->regs[R_GQSPI_DATA_STS]--;
        }
        if (ARRAY_FIELD_EX32(s->regs, GQSPI_GF_SNAPSHOT, RECIEVE)) {
            for (i = 0; i < 2; ++i) {
                if (busses & (1 << i)) {
                    DB_PRINT_L(1, "bus %d push_byte = %02x\n", i, tx_rx[i]);
                    fifo8_push(&s->rx_fifo_g, tx_rx[i]);
                    s->rx_fifo_g_align++;
                }
            }
        }
        if (!s->regs[R_GQSPI_DATA_STS]) {
            for (; s->tx_fifo_g_align % 4; s->tx_fifo_g_align++) {
                fifo8_pop(&s->tx_fifo_g);
            }
            for (; s->rx_fifo_g_align % 4; s->rx_fifo_g_align++) {
                fifo8_push(&s->rx_fifo_g, 0);
            }
        }
    }
}

static int xilinx_spips_num_dummies(XilinxQSPIPS *qs, uint8_t command)
{
    if (!qs) {
        /* The SPI device is not a QSPI device */
        return -1;
    }

    switch (command) { /* check for dummies */
    case READ: /* no dummy bytes/cycles */
    case PP:
    case DPP:
    case QPP:
    case READ_4:
    case PP_4:
    case QPP_4:
        return 0;
    case FAST_READ:
    case DOR:
    case QOR:
    case DOR_4:
    case QOR_4:
        return 1;
    case DIOR:
    case FAST_READ_4:
    case DIOR_4:
        return 2;
    case QIOR:
    case QIOR_4:
        return 4;
    default:
        return -1;
    }
}

static inline uint8_t get_addr_length(XilinxSPIPS *s, uint8_t cmd)
{
   switch (cmd) {
   case PP_4:
   case QPP_4:
   case READ_4:
   case QIOR_4:
   case FAST_READ_4:
   case DOR_4:
   case QOR_4:
   case DIOR_4:
       return 4;
   default:
       return (s->regs[R_CMND] & R_CMND_EXT_ADD) ? 4 : 3;
   }
}

static void xilinx_spips_flush_txfifo(XilinxSPIPS *s)
{
    int debug_level = 0;
    XilinxQSPIPS *q = (XilinxQSPIPS *) object_dynamic_cast(OBJECT(s),
                                                           TYPE_XILINX_QSPIPS);

    for (;;) {
        int i;
        uint8_t tx = 0;
        uint8_t tx_rx[MAX_NUM_BUSSES] = { 0 };
        uint8_t dummy_cycles = 0;
        uint8_t addr_length;

        if (fifo8_is_empty(&s->tx_fifo)) {
            xilinx_spips_update_ixr(s);
            return;
        } else if (s->snoop_state == SNOOP_STRIPING ||
                   s->snoop_state == SNOOP_NONE) {
            for (i = 0; i < num_effective_busses(s); ++i) {
                tx_rx[i] = fifo8_pop(&s->tx_fifo);
            }
            stripe8(tx_rx, num_effective_busses(s), false);
        } else if (s->snoop_state >= SNOOP_ADDR) {
            tx = fifo8_pop(&s->tx_fifo);
            for (i = 0; i < num_effective_busses(s); ++i) {
                tx_rx[i] = tx;
            }
        } else {
            /* Extract a dummy byte and generate dummy cycles according to the
             * link state */
            tx = fifo8_pop(&s->tx_fifo);
            dummy_cycles = 8 / s->link_state;
        }

        for (i = 0; i < num_effective_busses(s); ++i) {
            int bus = num_effective_busses(s) - 1 - i;
            if (dummy_cycles) {
                int d;
                for (d = 0; d < dummy_cycles; ++d) {
                    tx_rx[0] = ssi_transfer(s->spi[bus], (uint32_t)tx_rx[0]);
                }
            } else {
                DB_PRINT_L(debug_level, "tx = %02x\n", tx_rx[i]);
                tx_rx[i] = ssi_transfer(s->spi[bus], (uint32_t)tx_rx[i]);
                DB_PRINT_L(debug_level, "rx = %02x\n", tx_rx[i]);
            }
        }

        if (s->regs[R_CMND] & R_CMND_RXFIFO_DRAIN) {
            DB_PRINT_L(debug_level, "dircarding drained rx byte\n");
            /* Do nothing */
        } else if (s->rx_discard) {
            DB_PRINT_L(debug_level, "dircarding discarded rx byte\n");
            s->rx_discard -= 8 / s->link_state;
        } else if (fifo8_is_full(&s->rx_fifo)) {
            s->regs[R_INTR_STATUS] |= IXR_RX_FIFO_OVERFLOW;
            DB_PRINT_L(0, "rx FIFO overflow");
        } else if (s->snoop_state == SNOOP_STRIPING) {
            stripe8(tx_rx, num_effective_busses(s), true);
            for (i = 0; i < num_effective_busses(s); ++i) {
                fifo8_push(&s->rx_fifo, (uint8_t)tx_rx[i]);
                DB_PRINT_L(debug_level, "pushing striped rx byte\n");
            }
        } else {
           DB_PRINT_L(debug_level, "pushing unstriped rx byte\n");
           fifo8_push(&s->rx_fifo, (uint8_t)tx_rx[0]);
        }

        if (s->link_state_next_when) {
            s->link_state_next_when--;
            if (!s->link_state_next_when) {
                s->link_state = s->link_state_next;
            }
        }

        DB_PRINT_L(debug_level, "initial snoop state: %x\n",
                   (unsigned)s->snoop_state);
        switch (s->snoop_state) {
        case (SNOOP_CHECKING):
            /* Store the count of dummy bytes in the txfifo */
            s->cmd_dummies = xilinx_spips_num_dummies(q, tx);
            addr_length = get_addr_length(s, tx);
            if (s->cmd_dummies < 0) {
                s->snoop_state = SNOOP_NONE;
            } else {
                s->snoop_state = SNOOP_ADDR + addr_length - 1;
            }
            switch (tx) {
            case DPP:
            case DOR:
            case DOR_4:
                s->link_state_next = 2;
                s->link_state_next_when = addr_length + s->cmd_dummies;
                break;
            case QPP:
            case QPP_4:
            case QOR:
            case QOR_4:
                s->link_state_next = 4;
                s->link_state_next_when = addr_length + s->cmd_dummies;
                break;
            case DIOR:
            case DIOR_4:
                s->link_state = 2;
                break;
            case QIOR:
            case QIOR_4:
                s->link_state = 4;
                break;
            }
            break;
        case (SNOOP_ADDR):
            /* Address has been transmitted, transmit dummy cycles now if
             * needed */
            if (s->cmd_dummies < 0) {
                s->snoop_state = SNOOP_NONE;
            } else {
                s->snoop_state = s->cmd_dummies;
            }
            break;
        case (SNOOP_STRIPING):
        case (SNOOP_NONE):
            /* Once we hit the boring stuff - squelch debug noise */
            if (!debug_level) {
                DB_PRINT_L(0, "squelching debug info ....\n");
                debug_level = 1;
            }
            break;
        default:
            s->snoop_state--;
        }
        DB_PRINT_L(debug_level, "final snoop state: %x\n",
                   (unsigned)s->snoop_state);
    }
}

static inline void tx_data_bytes(Fifo8 *fifo, uint32_t value, int num, bool be)
{
    int i;
    for (i = 0; i < num && !fifo8_is_full(fifo); ++i) {
        if (be) {
            fifo8_push(fifo, (uint8_t)(value >> 24));
            value <<= 8;
        } else {
            fifo8_push(fifo, (uint8_t)value);
            value >>= 8;
        }
    }
}

static void xilinx_spips_check_zero_pump(XilinxSPIPS *s)
{
    if (!s->regs[R_TRANSFER_SIZE]) {
        return;
    }
    if (!fifo8_is_empty(&s->tx_fifo) && s->regs[R_CMND] & R_CMND_PUSH_WAIT) {
        return;
    }
    /*
     * The zero pump must never fill tx fifo such that rx overflow is
     * possible
     */
    while (s->regs[R_TRANSFER_SIZE] &&
           s->rx_fifo.num + s->tx_fifo.num < RXFF_A_Q - 3) {
        /* endianess just doesn't matter when zero pumping */
        tx_data_bytes(&s->tx_fifo, 0, 4, false);
        s->regs[R_TRANSFER_SIZE] &= ~0x03ull;
        s->regs[R_TRANSFER_SIZE] -= 4;
    }
}

static void xilinx_spips_check_flush(XilinxSPIPS *s)
{
    if (s->man_start_com ||
        (!fifo8_is_empty(&s->tx_fifo) &&
         !(s->regs[R_CONFIG] & MAN_START_EN))) {
        xilinx_spips_check_zero_pump(s);
        xilinx_spips_flush_txfifo(s);
    }
    if (fifo8_is_empty(&s->tx_fifo) && !s->regs[R_TRANSFER_SIZE]) {
        s->man_start_com = false;
    }
    xilinx_spips_update_ixr(s);
}

static void xlnx_zynqmp_qspips_check_flush(XlnxZynqMPQSPIPS *s)
{
    bool gqspi_has_work = s->regs[R_GQSPI_DATA_STS] ||
                          !fifo32_is_empty(&s->fifo_g);

    if (ARRAY_FIELD_EX32(s->regs, GQSPI_SELECT, GENERIC_QSPI_EN)) {
        if (s->man_start_com_g || (gqspi_has_work &&
             !ARRAY_FIELD_EX32(s->regs, GQSPI_CNFG, GEN_FIFO_START_MODE))) {
            xlnx_zynqmp_qspips_flush_fifo_g(s);
        }
    } else {
        xilinx_spips_check_flush(XILINX_SPIPS(s));
    }
    if (!gqspi_has_work) {
        s->man_start_com_g = false;
    }
    xlnx_zynqmp_qspips_update_ixr(s);
}

static inline int rx_data_bytes(Fifo8 *fifo, uint8_t *value, int max)
{
    int i;

    for (i = 0; i < max && !fifo8_is_empty(fifo); ++i) {
        value[i] = fifo8_pop(fifo);
    }
    return max - i;
}

static const void *pop_buf(Fifo8 *fifo, uint32_t max, uint32_t *num)
{
    void *ret;

    if (max == 0 || max > fifo->num) {
        abort();
    }
    *num = MIN(fifo->capacity - fifo->head, max);
    ret = &fifo->data[fifo->head];
    fifo->head += *num;
    fifo->head %= fifo->capacity;
    fifo->num -= *num;
    return ret;
}

static void xlnx_zynqmp_qspips_notify(void *opaque)
{
    XlnxZynqMPQSPIPS *rq = XLNX_ZYNQMP_QSPIPS(opaque);
    XilinxSPIPS *s = XILINX_SPIPS(rq);
    Fifo8 *recv_fifo;

    if (ARRAY_FIELD_EX32(rq->regs, GQSPI_SELECT, GENERIC_QSPI_EN)) {
        if (!(ARRAY_FIELD_EX32(rq->regs, GQSPI_CNFG, MODE_EN) == 2)) {
            return;
        }
        recv_fifo = &rq->rx_fifo_g;
    } else {
        if (!(s->regs[R_CMND] & R_CMND_DMA_EN)) {
            return;
        }
        recv_fifo = &s->rx_fifo;
    }
    while (recv_fifo->num >= 4
           && stream_can_push(rq->dma, xlnx_zynqmp_qspips_notify, rq))
    {
        size_t ret;
        uint32_t num;
        const void *rxd;
        int len;

        len = recv_fifo->num >= rq->dma_burst_size ? rq->dma_burst_size :
                                                   recv_fifo->num;
        rxd = pop_buf(recv_fifo, len, &num);

        memcpy(rq->dma_buf, rxd, num);

        ret = stream_push(rq->dma, rq->dma_buf, num);
        assert(ret == num);
        xlnx_zynqmp_qspips_check_flush(rq);
    }
}

static uint64_t xilinx_spips_read(void *opaque, hwaddr addr,
                                                        unsigned size)
{
    XilinxSPIPS *s = opaque;
    uint32_t mask = ~0;
    uint32_t ret;
    uint8_t rx_buf[4];
    int shortfall;

    addr >>= 2;
    switch (addr) {
    case R_CONFIG:
        mask = ~(R_CONFIG_RSVD | MAN_START_COM);
        break;
    case R_INTR_STATUS:
        ret = s->regs[addr] & IXR_ALL;
        s->regs[addr] = 0;
        DB_PRINT_L(0, "addr=" TARGET_FMT_plx " = %x\n", addr * 4, ret);
        xilinx_spips_update_ixr(s);
        return ret;
    case R_INTR_MASK:
        mask = IXR_ALL;
        break;
    case  R_EN:
        mask = 0x1;
        break;
    case R_SLAVE_IDLE_COUNT:
        mask = 0xFF;
        break;
    case R_MOD_ID:
        mask = 0x01FFFFFF;
        break;
    case R_INTR_EN:
    case R_INTR_DIS:
    case R_TX_DATA:
        mask = 0;
        break;
    case R_RX_DATA:
        memset(rx_buf, 0, sizeof(rx_buf));
        shortfall = rx_data_bytes(&s->rx_fifo, rx_buf, s->num_txrx_bytes);
        ret = s->regs[R_CONFIG] & R_CONFIG_ENDIAN ?
                        cpu_to_be32(*(uint32_t *)rx_buf) :
                        cpu_to_le32(*(uint32_t *)rx_buf);
        if (!(s->regs[R_CONFIG] & R_CONFIG_ENDIAN)) {
            ret <<= 8 * shortfall;
        }
        DB_PRINT_L(0, "addr=" TARGET_FMT_plx " = %x\n", addr * 4, ret);
        xilinx_spips_check_flush(s);
        xilinx_spips_update_ixr(s);
        return ret;
    }
    DB_PRINT_L(0, "addr=" TARGET_FMT_plx " = %x\n", addr * 4,
               s->regs[addr] & mask);
    return s->regs[addr] & mask;

}

static uint64_t xlnx_zynqmp_qspips_read(void *opaque,
                                        hwaddr addr, unsigned size)
{
    XlnxZynqMPQSPIPS *s = XLNX_ZYNQMP_QSPIPS(opaque);
    uint32_t reg = addr / 4;
    uint32_t ret;
    uint8_t rx_buf[4];
    int shortfall;

    if (reg <= R_MOD_ID) {
        return xilinx_spips_read(opaque, addr, size);
    } else {
        switch (reg) {
        case R_GQSPI_RXD:
            if (fifo8_is_empty(&s->rx_fifo_g)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "Read from empty GQSPI RX FIFO\n");
                return 0;
            }
            memset(rx_buf, 0, sizeof(rx_buf));
            shortfall = rx_data_bytes(&s->rx_fifo_g, rx_buf,
                                      XILINX_SPIPS(s)->num_txrx_bytes);
            ret = ARRAY_FIELD_EX32(s->regs, GQSPI_CNFG, ENDIAN) ?
                  cpu_to_be32(*(uint32_t *)rx_buf) :
                  cpu_to_le32(*(uint32_t *)rx_buf);
            if (!ARRAY_FIELD_EX32(s->regs, GQSPI_CNFG, ENDIAN)) {
                ret <<= 8 * shortfall;
            }
            xlnx_zynqmp_qspips_check_flush(s);
            xlnx_zynqmp_qspips_update_ixr(s);
            return ret;
        default:
            return s->regs[reg];
        }
    }
}

static void xilinx_spips_write(void *opaque, hwaddr addr,
                                        uint64_t value, unsigned size)
{
    int mask = ~0;
    XilinxSPIPS *s = opaque;
    bool try_flush = true;

    DB_PRINT_L(0, "addr=" TARGET_FMT_plx " = %x\n", addr, (unsigned)value);
    addr >>= 2;
    switch (addr) {
    case R_CONFIG:
        mask = ~(R_CONFIG_RSVD | MAN_START_COM);
        if ((value & MAN_START_COM) && (s->regs[R_CONFIG] & MAN_START_EN)) {
            s->man_start_com = true;
        }
        break;
    case R_INTR_STATUS:
        mask = IXR_ALL;
        s->regs[R_INTR_STATUS] &= ~(mask & value);
        goto no_reg_update;
    case R_INTR_DIS:
        mask = IXR_ALL;
        s->regs[R_INTR_MASK] &= ~(mask & value);
        goto no_reg_update;
    case R_INTR_EN:
        mask = IXR_ALL;
        s->regs[R_INTR_MASK] |= mask & value;
        goto no_reg_update;
    case R_EN:
        mask = 0x1;
        break;
    case R_SLAVE_IDLE_COUNT:
        mask = 0xFF;
        break;
    case R_RX_DATA:
    case R_INTR_MASK:
    case R_MOD_ID:
        mask = 0;
        break;
    case R_TX_DATA:
        tx_data_bytes(&s->tx_fifo, (uint32_t)value, s->num_txrx_bytes,
                      s->regs[R_CONFIG] & R_CONFIG_ENDIAN);
        goto no_reg_update;
    case R_TXD1:
        tx_data_bytes(&s->tx_fifo, (uint32_t)value, 1,
                      s->regs[R_CONFIG] & R_CONFIG_ENDIAN);
        goto no_reg_update;
    case R_TXD2:
        tx_data_bytes(&s->tx_fifo, (uint32_t)value, 2,
                      s->regs[R_CONFIG] & R_CONFIG_ENDIAN);
        goto no_reg_update;
    case R_TXD3:
        tx_data_bytes(&s->tx_fifo, (uint32_t)value, 3,
                      s->regs[R_CONFIG] & R_CONFIG_ENDIAN);
        goto no_reg_update;
    /* Skip SPI bus update for below registers writes */
    case R_GPIO:
    case R_LPBK_DLY_ADJ:
    case R_IOU_TAPDLY_BYPASS:
    case R_DUMMY_CYCLE_EN:
    case R_ECO:
        try_flush = false;
        break;
    }
    s->regs[addr] = (s->regs[addr] & ~mask) | (value & mask);
no_reg_update:
    if (try_flush) {
        xilinx_spips_update_cs_lines(s);
        xilinx_spips_check_flush(s);
        xilinx_spips_update_cs_lines(s);
        xilinx_spips_update_ixr(s);
    }
}

static const MemoryRegionOps spips_ops = {
    .read = xilinx_spips_read,
    .write = xilinx_spips_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void xilinx_qspips_invalidate_mmio_ptr(XilinxQSPIPS *q)
{
    q->lqspi_cached_addr = ~0ULL;
}

static void xilinx_qspips_write(void *opaque, hwaddr addr,
                                uint64_t value, unsigned size)
{
    XilinxQSPIPS *q = XILINX_QSPIPS(opaque);
    XilinxSPIPS *s = XILINX_SPIPS(opaque);

    xilinx_spips_write(opaque, addr, value, size);
    addr >>= 2;

    if (addr == R_LQSPI_CFG) {
        xilinx_qspips_invalidate_mmio_ptr(q);
    }
    if (s->regs[R_CMND] & R_CMND_RXFIFO_DRAIN) {
        fifo8_reset(&s->rx_fifo);
    }
}

static void xlnx_zynqmp_qspips_write(void *opaque, hwaddr addr,
                                        uint64_t value, unsigned size)
{
    XlnxZynqMPQSPIPS *s = XLNX_ZYNQMP_QSPIPS(opaque);
    uint32_t reg = addr / 4;

    if (reg <= R_MOD_ID) {
        xilinx_qspips_write(opaque, addr, value, size);
    } else {
        switch (reg) {
        case R_GQSPI_CNFG:
            if (FIELD_EX32(value, GQSPI_CNFG, GEN_FIFO_START) &&
                ARRAY_FIELD_EX32(s->regs, GQSPI_CNFG, GEN_FIFO_START_MODE)) {
                s->man_start_com_g = true;
            }
            s->regs[reg] = value & ~(R_GQSPI_CNFG_GEN_FIFO_START_MASK);
            break;
        case R_GQSPI_GEN_FIFO:
            if (!fifo32_is_full(&s->fifo_g)) {
                fifo32_push(&s->fifo_g, value);
            }
            break;
        case R_GQSPI_TXD:
            tx_data_bytes(&s->tx_fifo_g, (uint32_t)value, 4,
                          ARRAY_FIELD_EX32(s->regs, GQSPI_CNFG, ENDIAN));
            break;
        case R_GQSPI_FIFO_CTRL:
            if (FIELD_EX32(value, GQSPI_FIFO_CTRL, GENERIC_FIFO_RESET)) {
                fifo32_reset(&s->fifo_g);
            }
            if (FIELD_EX32(value, GQSPI_FIFO_CTRL, TX_FIFO_RESET)) {
                fifo8_reset(&s->tx_fifo_g);
            }
            if (FIELD_EX32(value, GQSPI_FIFO_CTRL, RX_FIFO_RESET)) {
                fifo8_reset(&s->rx_fifo_g);
            }
            break;
        case R_GQSPI_IDR:
            s->regs[R_GQSPI_IMR] |= value;
            break;
        case R_GQSPI_IER:
            s->regs[R_GQSPI_IMR] &= ~value;
            break;
        case R_GQSPI_ISR:
            s->regs[R_GQSPI_ISR] &= ~value;
            break;
        case R_GQSPI_IMR:
        case R_GQSPI_RXD:
        case R_GQSPI_GF_SNAPSHOT:
        case R_GQSPI_MOD_ID:
            break;
        default:
            s->regs[reg] = value;
            break;
        }
        xlnx_zynqmp_qspips_update_cs_lines(s);
        xlnx_zynqmp_qspips_check_flush(s);
        xlnx_zynqmp_qspips_update_cs_lines(s);
        xlnx_zynqmp_qspips_update_ixr(s);
    }
    xlnx_zynqmp_qspips_notify(s);
}

static const MemoryRegionOps qspips_ops = {
    .read = xilinx_spips_read,
    .write = xilinx_qspips_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const MemoryRegionOps xlnx_zynqmp_qspips_ops = {
    .read = xlnx_zynqmp_qspips_read,
    .write = xlnx_zynqmp_qspips_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

#define LQSPI_CACHE_SIZE 1024

static void lqspi_load_cache(void *opaque, hwaddr addr)
{
    XilinxQSPIPS *q = opaque;
    XilinxSPIPS *s = opaque;
    int i;
    int flash_addr = ((addr & ~(LQSPI_CACHE_SIZE - 1))
                   / num_effective_busses(s));
    int slave = flash_addr >> LQSPI_ADDRESS_BITS;
    int cache_entry = 0;
    uint32_t u_page_save = s->regs[R_LQSPI_STS] & ~LQSPI_CFG_U_PAGE;

    if (addr < q->lqspi_cached_addr ||
            addr > q->lqspi_cached_addr + LQSPI_CACHE_SIZE - 4) {
        xilinx_qspips_invalidate_mmio_ptr(q);
        s->regs[R_LQSPI_STS] &= ~LQSPI_CFG_U_PAGE;
        s->regs[R_LQSPI_STS] |= slave ? LQSPI_CFG_U_PAGE : 0;

        DB_PRINT_L(0, "config reg status: %08x\n", s->regs[R_LQSPI_CFG]);

        fifo8_reset(&s->tx_fifo);
        fifo8_reset(&s->rx_fifo);

        /* instruction */
        DB_PRINT_L(0, "pushing read instruction: %02x\n",
                   (unsigned)(uint8_t)(s->regs[R_LQSPI_CFG] &
                                       LQSPI_CFG_INST_CODE));
        fifo8_push(&s->tx_fifo, s->regs[R_LQSPI_CFG] & LQSPI_CFG_INST_CODE);
        /* read address */
        DB_PRINT_L(0, "pushing read address %06x\n", flash_addr);
        if (s->regs[R_LQSPI_CFG] & LQSPI_CFG_ADDR4) {
            fifo8_push(&s->tx_fifo, (uint8_t)(flash_addr >> 24));
        }
        fifo8_push(&s->tx_fifo, (uint8_t)(flash_addr >> 16));
        fifo8_push(&s->tx_fifo, (uint8_t)(flash_addr >> 8));
        fifo8_push(&s->tx_fifo, (uint8_t)flash_addr);
        /* mode bits */
        if (s->regs[R_LQSPI_CFG] & LQSPI_CFG_MODE_EN) {
            fifo8_push(&s->tx_fifo, extract32(s->regs[R_LQSPI_CFG],
                                              LQSPI_CFG_MODE_SHIFT,
                                              LQSPI_CFG_MODE_WIDTH));
        }
        /* dummy bytes */
        for (i = 0; i < (extract32(s->regs[R_LQSPI_CFG], LQSPI_CFG_DUMMY_SHIFT,
                                   LQSPI_CFG_DUMMY_WIDTH)); ++i) {
            DB_PRINT_L(0, "pushing dummy byte\n");
            fifo8_push(&s->tx_fifo, 0);
        }
        xilinx_spips_update_cs_lines(s);
        xilinx_spips_flush_txfifo(s);
        fifo8_reset(&s->rx_fifo);

        DB_PRINT_L(0, "starting QSPI data read\n");

        while (cache_entry < LQSPI_CACHE_SIZE) {
            for (i = 0; i < 64; ++i) {
                tx_data_bytes(&s->tx_fifo, 0, 1, false);
            }
            xilinx_spips_flush_txfifo(s);
            for (i = 0; i < 64; ++i) {
                rx_data_bytes(&s->rx_fifo, &q->lqspi_buf[cache_entry++], 1);
            }
        }

        s->regs[R_LQSPI_STS] &= ~LQSPI_CFG_U_PAGE;
        s->regs[R_LQSPI_STS] |= u_page_save;
        xilinx_spips_update_cs_lines(s);

        q->lqspi_cached_addr = flash_addr * num_effective_busses(s);
    }
}

static MemTxResult lqspi_read(void *opaque, hwaddr addr, uint64_t *value,
                              unsigned size, MemTxAttrs attrs)
{
    XilinxQSPIPS *q = XILINX_QSPIPS(opaque);

    if (addr >= q->lqspi_cached_addr &&
            addr <= q->lqspi_cached_addr + LQSPI_CACHE_SIZE - 4) {
        uint8_t *retp = &q->lqspi_buf[addr - q->lqspi_cached_addr];
        *value = cpu_to_le32(*(uint32_t *)retp);
        DB_PRINT_L(1, "addr: %08" HWADDR_PRIx ", data: %08" PRIx64 "\n",
                   addr, *value);
        return MEMTX_OK;
    }

    lqspi_load_cache(opaque, addr);
    return lqspi_read(opaque, addr, value, size, attrs);
}

static MemTxResult lqspi_write(void *opaque, hwaddr offset, uint64_t value,
                               unsigned size, MemTxAttrs attrs)
{
    /*
     * From UG1085, Chapter 24 (Quad-SPI controllers):
     * - Writes are ignored
     * - AXI writes generate an external AXI slave error (SLVERR)
     */
    qemu_log_mask(LOG_GUEST_ERROR, "%s Unexpected %u-bit access to 0x%" PRIx64
                                   " (value: 0x%" PRIx64 "\n",
                  __func__, size << 3, offset, value);

    return MEMTX_ERROR;
}

static const MemoryRegionOps lqspi_ops = {
    .read_with_attrs = lqspi_read,
    .write_with_attrs = lqspi_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4
    }
};

static void xilinx_spips_realize(DeviceState *dev, Error **errp)
{
    XilinxSPIPS *s = XILINX_SPIPS(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    XilinxSPIPSClass *xsc = XILINX_SPIPS_GET_CLASS(s);
    qemu_irq *cs;
    int i;

    DB_PRINT_L(0, "realized spips\n");

    if (s->num_busses > MAX_NUM_BUSSES) {
        error_setg(errp,
                   "requested number of SPI busses %u exceeds maximum %d",
                   s->num_busses, MAX_NUM_BUSSES);
        return;
    }
    if (s->num_busses < MIN_NUM_BUSSES) {
        error_setg(errp,
                   "requested number of SPI busses %u is below minimum %d",
                   s->num_busses, MIN_NUM_BUSSES);
        return;
    }

    s->spi = g_new(SSIBus *, s->num_busses);
    for (i = 0; i < s->num_busses; ++i) {
        char bus_name[16];
        snprintf(bus_name, 16, "spi%d", i);
        s->spi[i] = ssi_create_bus(dev, bus_name);
    }

    s->cs_lines = g_new0(qemu_irq, s->num_cs * s->num_busses);
    s->cs_lines_state = g_new0(bool, s->num_cs * s->num_busses);
    for (i = 0, cs = s->cs_lines; i < s->num_busses; ++i, cs += s->num_cs) {
        ssi_auto_connect_slaves(DEVICE(s), cs, s->spi[i]);
    }

    sysbus_init_irq(sbd, &s->irq);
    for (i = 0; i < s->num_cs * s->num_busses; ++i) {
        sysbus_init_irq(sbd, &s->cs_lines[i]);
    }

    memory_region_init_io(&s->iomem, OBJECT(s), xsc->reg_ops, s,
                          "spi", XLNX_ZYNQMP_SPIPS_R_MAX * 4);
    sysbus_init_mmio(sbd, &s->iomem);

    s->irqline = -1;

    fifo8_create(&s->rx_fifo, xsc->rx_fifo_size);
    fifo8_create(&s->tx_fifo, xsc->tx_fifo_size);
}

static void xilinx_qspips_realize(DeviceState *dev, Error **errp)
{
    XilinxSPIPS *s = XILINX_SPIPS(dev);
    XilinxQSPIPS *q = XILINX_QSPIPS(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    DB_PRINT_L(0, "realized qspips\n");

    s->num_busses = 2;
    s->num_cs = 2;
    s->num_txrx_bytes = 4;

    xilinx_spips_realize(dev, errp);
    memory_region_init_io(&s->mmlqspi, OBJECT(s), &lqspi_ops, s, "lqspi",
                          (1 << LQSPI_ADDRESS_BITS) * 2);
    sysbus_init_mmio(sbd, &s->mmlqspi);

    q->lqspi_cached_addr = ~0ULL;
}

static void xlnx_zynqmp_qspips_realize(DeviceState *dev, Error **errp)
{
    XlnxZynqMPQSPIPS *s = XLNX_ZYNQMP_QSPIPS(dev);
    XilinxSPIPSClass *xsc = XILINX_SPIPS_GET_CLASS(s);

    if (s->dma_burst_size > QSPI_DMA_MAX_BURST_SIZE) {
        error_setg(errp,
                   "qspi dma burst size %u exceeds maximum limit %d",
                   s->dma_burst_size, QSPI_DMA_MAX_BURST_SIZE);
        return;
    }
    xilinx_qspips_realize(dev, errp);
    fifo8_create(&s->rx_fifo_g, xsc->rx_fifo_size);
    fifo8_create(&s->tx_fifo_g, xsc->tx_fifo_size);
    fifo32_create(&s->fifo_g, 32);
}

static void xlnx_zynqmp_qspips_init(Object *obj)
{
    XlnxZynqMPQSPIPS *rq = XLNX_ZYNQMP_QSPIPS(obj);

    object_property_add_link(obj, "stream-connected-dma", TYPE_STREAM_SLAVE,
                             (Object **)&rq->dma,
                             object_property_allow_set_link,
                             OBJ_PROP_LINK_STRONG,
                             NULL);
}

static int xilinx_spips_post_load(void *opaque, int version_id)
{
    xilinx_spips_update_ixr((XilinxSPIPS *)opaque);
    xilinx_spips_update_cs_lines((XilinxSPIPS *)opaque);
    return 0;
}

static const VMStateDescription vmstate_xilinx_spips = {
    .name = "xilinx_spips",
    .version_id = 2,
    .minimum_version_id = 2,
    .post_load = xilinx_spips_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_FIFO8(tx_fifo, XilinxSPIPS),
        VMSTATE_FIFO8(rx_fifo, XilinxSPIPS),
        VMSTATE_UINT32_ARRAY(regs, XilinxSPIPS, XLNX_SPIPS_R_MAX),
        VMSTATE_UINT8(snoop_state, XilinxSPIPS),
        VMSTATE_END_OF_LIST()
    }
};

static int xlnx_zynqmp_qspips_post_load(void *opaque, int version_id)
{
    XlnxZynqMPQSPIPS *s = (XlnxZynqMPQSPIPS *)opaque;
    XilinxSPIPS *qs = XILINX_SPIPS(s);

    if (ARRAY_FIELD_EX32(s->regs, GQSPI_SELECT, GENERIC_QSPI_EN) &&
        fifo8_is_empty(&qs->rx_fifo) && fifo8_is_empty(&qs->tx_fifo)) {
        xlnx_zynqmp_qspips_update_ixr(s);
        xlnx_zynqmp_qspips_update_cs_lines(s);
    }
    return 0;
}

static const VMStateDescription vmstate_xilinx_qspips = {
    .name = "xilinx_qspips",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(parent_obj, XilinxQSPIPS, 0,
                       vmstate_xilinx_spips, XilinxSPIPS),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_xlnx_zynqmp_qspips = {
    .name = "xlnx_zynqmp_qspips",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = xlnx_zynqmp_qspips_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(parent_obj, XlnxZynqMPQSPIPS, 0,
                       vmstate_xilinx_qspips, XilinxQSPIPS),
        VMSTATE_FIFO8(tx_fifo_g, XlnxZynqMPQSPIPS),
        VMSTATE_FIFO8(rx_fifo_g, XlnxZynqMPQSPIPS),
        VMSTATE_FIFO32(fifo_g, XlnxZynqMPQSPIPS),
        VMSTATE_UINT32_ARRAY(regs, XlnxZynqMPQSPIPS, XLNX_ZYNQMP_SPIPS_R_MAX),
        VMSTATE_END_OF_LIST()
    }
};

static Property xilinx_zynqmp_qspips_properties[] = {
    DEFINE_PROP_UINT32("dma-burst-size", XlnxZynqMPQSPIPS, dma_burst_size, 64),
    DEFINE_PROP_END_OF_LIST(),
};

static Property xilinx_spips_properties[] = {
    DEFINE_PROP_UINT8("num-busses", XilinxSPIPS, num_busses, 1),
    DEFINE_PROP_UINT8("num-ss-bits", XilinxSPIPS, num_cs, 4),
    DEFINE_PROP_UINT8("num-txrx-bytes", XilinxSPIPS, num_txrx_bytes, 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void xilinx_qspips_class_init(ObjectClass *klass, void * data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    XilinxSPIPSClass *xsc = XILINX_SPIPS_CLASS(klass);

    dc->realize = xilinx_qspips_realize;
    xsc->reg_ops = &qspips_ops;
    xsc->rx_fifo_size = RXFF_A_Q;
    xsc->tx_fifo_size = TXFF_A_Q;
}

static void xilinx_spips_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    XilinxSPIPSClass *xsc = XILINX_SPIPS_CLASS(klass);

    dc->realize = xilinx_spips_realize;
    dc->reset = xilinx_spips_reset;
    dc->props = xilinx_spips_properties;
    dc->vmsd = &vmstate_xilinx_spips;

    xsc->reg_ops = &spips_ops;
    xsc->rx_fifo_size = RXFF_A;
    xsc->tx_fifo_size = TXFF_A;
}

static void xlnx_zynqmp_qspips_class_init(ObjectClass *klass, void * data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    XilinxSPIPSClass *xsc = XILINX_SPIPS_CLASS(klass);

    dc->realize = xlnx_zynqmp_qspips_realize;
    dc->reset = xlnx_zynqmp_qspips_reset;
    dc->vmsd = &vmstate_xlnx_zynqmp_qspips;
    dc->props = xilinx_zynqmp_qspips_properties;
    xsc->reg_ops = &xlnx_zynqmp_qspips_ops;
    xsc->rx_fifo_size = RXFF_A_Q;
    xsc->tx_fifo_size = TXFF_A_Q;
}

static const TypeInfo xilinx_spips_info = {
    .name  = TYPE_XILINX_SPIPS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(XilinxSPIPS),
    .class_init = xilinx_spips_class_init,
    .class_size = sizeof(XilinxSPIPSClass),
};

static const TypeInfo xilinx_qspips_info = {
    .name  = TYPE_XILINX_QSPIPS,
    .parent = TYPE_XILINX_SPIPS,
    .instance_size  = sizeof(XilinxQSPIPS),
    .class_init = xilinx_qspips_class_init,
};

static const TypeInfo xlnx_zynqmp_qspips_info = {
    .name  = TYPE_XLNX_ZYNQMP_QSPIPS,
    .parent = TYPE_XILINX_QSPIPS,
    .instance_size  = sizeof(XlnxZynqMPQSPIPS),
    .instance_init  = xlnx_zynqmp_qspips_init,
    .class_init = xlnx_zynqmp_qspips_class_init,
};

static void xilinx_spips_register_types(void)
{
    type_register_static(&xilinx_spips_info);
    type_register_static(&xilinx_qspips_info);
    type_register_static(&xlnx_zynqmp_qspips_info);
}

type_init(xilinx_spips_register_types)
