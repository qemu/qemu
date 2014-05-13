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

#include "hw/sysbus.h"
#include "sysemu/sysemu.h"
#include "hw/ptimer.h"
#include "qemu/log.h"
#include "qemu/fifo8.h"
#include "hw/ssi.h"
#include "qemu/bitops.h"

#ifndef XILINX_SPIPS_ERR_DEBUG
#define XILINX_SPIPS_ERR_DEBUG 0
#endif

#define DB_PRINT_L(level, ...) do { \
    if (XILINX_SPIPS_ERR_DEBUG > (level)) { \
        fprintf(stderr,  ": %s: ", __func__); \
        fprintf(stderr, ## __VA_ARGS__); \
    } \
} while (0);

/* config register */
#define R_CONFIG            (0x00 / 4)
#define IFMODE              (1U << 31)
#define ENDIAN              (1 << 26)
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
#define R_INTR_EN           (0x08 / 4)
#define R_INTR_DIS          (0x0C / 4)
#define R_INTR_MASK         (0x10 / 4)
#define IXR_TX_FIFO_UNDERFLOW   (1 << 6)
#define IXR_RX_FIFO_FULL        (1 << 5)
#define IXR_RX_FIFO_NOT_EMPTY   (1 << 4)
#define IXR_TX_FIFO_FULL        (1 << 3)
#define IXR_TX_FIFO_NOT_FULL    (1 << 2)
#define IXR_TX_FIFO_MODE_FAIL   (1 << 1)
#define IXR_RX_FIFO_OVERFLOW    (1 << 0)
#define IXR_ALL                 ((IXR_TX_FIFO_UNDERFLOW<<1)-1)

#define R_EN                (0x14 / 4)
#define R_DELAY             (0x18 / 4)
#define R_TX_DATA           (0x1C / 4)
#define R_RX_DATA           (0x20 / 4)
#define R_SLAVE_IDLE_COUNT  (0x24 / 4)
#define R_TX_THRES          (0x28 / 4)
#define R_RX_THRES          (0x2C / 4)
#define R_TXD1              (0x80 / 4)
#define R_TXD2              (0x84 / 4)
#define R_TXD3              (0x88 / 4)

#define R_LQSPI_CFG         (0xa0 / 4)
#define R_LQSPI_CFG_RESET       0x03A002EB
#define LQSPI_CFG_LQ_MODE       (1U << 31)
#define LQSPI_CFG_TWO_MEM       (1 << 30)
#define LQSPI_CFG_SEP_BUS       (1 << 30)
#define LQSPI_CFG_U_PAGE        (1 << 28)
#define LQSPI_CFG_MODE_EN       (1 << 25)
#define LQSPI_CFG_MODE_WIDTH    8
#define LQSPI_CFG_MODE_SHIFT    16
#define LQSPI_CFG_DUMMY_WIDTH   3
#define LQSPI_CFG_DUMMY_SHIFT   8
#define LQSPI_CFG_INST_CODE     0xFF

#define R_LQSPI_STS         (0xA4 / 4)
#define LQSPI_STS_WR_RECVD      (1 << 1)

#define R_MOD_ID            (0xFC / 4)

#define R_MAX (R_MOD_ID+1)

/* size of TXRX FIFOs */
#define RXFF_A          32
#define TXFF_A          32

#define RXFF_A_Q          (64 * 4)
#define TXFF_A_Q          (64 * 4)

/* 16MB per linear region */
#define LQSPI_ADDRESS_BITS 24
/* Bite off 4k chunks at a time */
#define LQSPI_CACHE_SIZE 1024

#define SNOOP_CHECKING 0xFF
#define SNOOP_NONE 0xFE
#define SNOOP_STRIPING 0

typedef enum {
    READ = 0x3,
    FAST_READ = 0xb,
    DOR = 0x3b,
    QOR = 0x6b,
    DIOR = 0xbb,
    QIOR = 0xeb,

    PP = 0x2,
    DPP = 0xa2,
    QPP = 0x32,
} FlashCMD;

typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    MemoryRegion mmlqspi;

    qemu_irq irq;
    int irqline;

    uint8_t num_cs;
    uint8_t num_busses;

    uint8_t snoop_state;
    qemu_irq *cs_lines;
    SSIBus **spi;

    Fifo8 rx_fifo;
    Fifo8 tx_fifo;

    uint8_t num_txrx_bytes;

    uint32_t regs[R_MAX];
} XilinxSPIPS;

typedef struct {
    XilinxSPIPS parent_obj;

    uint8_t lqspi_buf[LQSPI_CACHE_SIZE];
    hwaddr lqspi_cached_addr;
} XilinxQSPIPS;

typedef struct XilinxSPIPSClass {
    SysBusDeviceClass parent_class;

    const MemoryRegionOps *reg_ops;

    uint32_t rx_fifo_size;
    uint32_t tx_fifo_size;
} XilinxSPIPSClass;

#define TYPE_XILINX_SPIPS "xlnx.ps7-spi"
#define TYPE_XILINX_QSPIPS "xlnx.ps7-qspi"

#define XILINX_SPIPS(obj) \
     OBJECT_CHECK(XilinxSPIPS, (obj), TYPE_XILINX_SPIPS)
#define XILINX_SPIPS_CLASS(klass) \
     OBJECT_CLASS_CHECK(XilinxSPIPSClass, (klass), TYPE_XILINX_SPIPS)
#define XILINX_SPIPS_GET_CLASS(obj) \
     OBJECT_GET_CLASS(XilinxSPIPSClass, (obj), TYPE_XILINX_SPIPS)

#define XILINX_QSPIPS(obj) \
     OBJECT_CHECK(XilinxQSPIPS, (obj), TYPE_XILINX_QSPIPS)

static inline int num_effective_busses(XilinxSPIPS *s)
{
    return (s->regs[R_LQSPI_CFG] & LQSPI_CFG_SEP_BUS &&
            s->regs[R_LQSPI_CFG] & LQSPI_CFG_TWO_MEM) ? s->num_busses : 1;
}

static inline bool xilinx_spips_cs_is_set(XilinxSPIPS *s, int i, int field)
{
    return ~field & (1 << i) && (s->regs[R_CONFIG] & MANUAL_CS
                    || !fifo8_is_empty(&s->tx_fifo));
}

static void xilinx_spips_update_cs_lines(XilinxSPIPS *s)
{
    int i, j;
    bool found = false;
    int field = s->regs[R_CONFIG] >> CS_SHIFT;

    for (i = 0; i < s->num_cs; i++) {
        for (j = 0; j < num_effective_busses(s); j++) {
            int upage = !!(s->regs[R_LQSPI_STS] & LQSPI_CFG_U_PAGE);
            int cs_to_set = (j * s->num_cs + i + upage) %
                                (s->num_cs * s->num_busses);

            if (xilinx_spips_cs_is_set(s, i, field) && !found) {
                DB_PRINT_L(0, "selecting slave %d\n", i);
                qemu_set_irq(s->cs_lines[cs_to_set], 0);
            } else {
                DB_PRINT_L(0, "deselecting slave %d\n", i);
                qemu_set_irq(s->cs_lines[cs_to_set], 1);
            }
        }
        if (xilinx_spips_cs_is_set(s, i, field)) {
            found = true;
        }
    }
    if (!found) {
        s->snoop_state = SNOOP_CHECKING;
        DB_PRINT_L(1, "moving to snoop check state\n");
    }
}

static void xilinx_spips_update_ixr(XilinxSPIPS *s)
{
    if (s->regs[R_LQSPI_CFG] & LQSPI_CFG_LQ_MODE) {
        return;
    }
    /* These are set/cleared as they occur */
    s->regs[R_INTR_STATUS] &= (IXR_TX_FIFO_UNDERFLOW | IXR_RX_FIFO_OVERFLOW |
                                IXR_TX_FIFO_MODE_FAIL);
    /* these are pure functions of fifo state, set them here */
    s->regs[R_INTR_STATUS] |=
        (fifo8_is_full(&s->rx_fifo) ? IXR_RX_FIFO_FULL : 0) |
        (s->rx_fifo.num >= s->regs[R_RX_THRES] ? IXR_RX_FIFO_NOT_EMPTY : 0) |
        (fifo8_is_full(&s->tx_fifo) ? IXR_TX_FIFO_FULL : 0) |
        (s->tx_fifo.num < s->regs[R_TX_THRES] ? IXR_TX_FIFO_NOT_FULL : 0);
    /* drive external interrupt pin */
    int new_irqline = !!(s->regs[R_INTR_MASK] & s->regs[R_INTR_STATUS] &
                                                                IXR_ALL);
    if (new_irqline != s->irqline) {
        s->irqline = new_irqline;
        qemu_set_irq(s->irq, s->irqline);
    }
}

static void xilinx_spips_reset(DeviceState *d)
{
    XilinxSPIPS *s = XILINX_SPIPS(d);

    int i;
    for (i = 0; i < R_MAX; i++) {
        s->regs[i] = 0;
    }

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
    s->snoop_state = SNOOP_CHECKING;
    xilinx_spips_update_ixr(s);
    xilinx_spips_update_cs_lines(s);
}

/* N way (num) in place bit striper. Lay out row wise bits (LSB to MSB)
 * column wise (from element 0 to N-1). num is the length of x, and dir
 * reverses the direction of the transform. Best illustrated by example:
 * Each digit in the below array is a single bit (num == 3):
 *
 * {{ 76543210, }  ----- stripe (dir == false) -----> {{ FCheb630, }
 *  { hgfedcba, }                                      { GDAfc741, }
 *  { HGFEDCBA, }} <---- upstripe (dir == true) -----  { HEBgda52, }}
 */

static inline void stripe8(uint8_t *x, int num, bool dir)
{
    uint8_t r[num];
    memset(r, 0, sizeof(uint8_t) * num);
    int idx[2] = {0, 0};
    int bit[2] = {0, 0};
    int d = dir;

    for (idx[0] = 0; idx[0] < num; ++idx[0]) {
        for (bit[0] = 0; bit[0] < 8; ++bit[0]) {
            r[idx[d]] |= x[idx[!d]] & 1 << bit[!d] ? 1 << bit[d] : 0;
            idx[1] = (idx[1] + 1) % num;
            if (!idx[1]) {
                bit[1]++;
            }
        }
    }
    memcpy(x, r, sizeof(uint8_t) * num);
}

static void xilinx_spips_flush_txfifo(XilinxSPIPS *s)
{
    int debug_level = 0;

    for (;;) {
        int i;
        uint8_t tx = 0;
        uint8_t tx_rx[num_effective_busses(s)];

        if (fifo8_is_empty(&s->tx_fifo)) {
            if (!(s->regs[R_LQSPI_CFG] & LQSPI_CFG_LQ_MODE)) {
                s->regs[R_INTR_STATUS] |= IXR_TX_FIFO_UNDERFLOW;
            }
            xilinx_spips_update_ixr(s);
            return;
        } else if (s->snoop_state == SNOOP_STRIPING) {
            for (i = 0; i < num_effective_busses(s); ++i) {
                tx_rx[i] = fifo8_pop(&s->tx_fifo);
            }
            stripe8(tx_rx, num_effective_busses(s), false);
        } else {
            tx = fifo8_pop(&s->tx_fifo);
            for (i = 0; i < num_effective_busses(s); ++i) {
                tx_rx[i] = tx;
            }
        }

        for (i = 0; i < num_effective_busses(s); ++i) {
            DB_PRINT_L(debug_level, "tx = %02x\n", tx_rx[i]);
            tx_rx[i] = ssi_transfer(s->spi[i], (uint32_t)tx_rx[i]);
            DB_PRINT_L(debug_level, "rx = %02x\n", tx_rx[i]);
        }

        if (fifo8_is_full(&s->rx_fifo)) {
            s->regs[R_INTR_STATUS] |= IXR_RX_FIFO_OVERFLOW;
            DB_PRINT_L(0, "rx FIFO overflow");
        } else if (s->snoop_state == SNOOP_STRIPING) {
            stripe8(tx_rx, num_effective_busses(s), true);
            for (i = 0; i < num_effective_busses(s); ++i) {
                fifo8_push(&s->rx_fifo, (uint8_t)tx_rx[i]);
            }
        } else {
           fifo8_push(&s->rx_fifo, (uint8_t)tx_rx[0]);
        }

        DB_PRINT_L(debug_level, "initial snoop state: %x\n",
                   (unsigned)s->snoop_state);
        switch (s->snoop_state) {
        case (SNOOP_CHECKING):
            switch (tx) { /* new instruction code */
            case READ: /* 3 address bytes, no dummy bytes/cycles */
            case PP:
            case DPP:
            case QPP:
                s->snoop_state = 3;
                break;
            case FAST_READ: /* 3 address bytes, 1 dummy byte */
            case DOR:
            case QOR:
            case DIOR: /* FIXME: these vary between vendor - set to spansion */
                s->snoop_state = 4;
                break;
            case QIOR: /* 3 address bytes, 2 dummy bytes */
                s->snoop_state = 6;
                break;
            default:
                s->snoop_state = SNOOP_NONE;
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

static inline void rx_data_bytes(XilinxSPIPS *s, uint8_t *value, int max)
{
    int i;

    for (i = 0; i < max && !fifo8_is_empty(&s->rx_fifo); ++i) {
        value[i] = fifo8_pop(&s->rx_fifo);
    }
}

static uint64_t xilinx_spips_read(void *opaque, hwaddr addr,
                                                        unsigned size)
{
    XilinxSPIPS *s = opaque;
    uint32_t mask = ~0;
    uint32_t ret;
    uint8_t rx_buf[4];

    addr >>= 2;
    switch (addr) {
    case R_CONFIG:
        mask = ~(R_CONFIG_RSVD | MAN_START_COM);
        break;
    case R_INTR_STATUS:
        ret = s->regs[addr] & IXR_ALL;
        s->regs[addr] = 0;
        DB_PRINT_L(0, "addr=" TARGET_FMT_plx " = %x\n", addr * 4, ret);
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
        rx_data_bytes(s, rx_buf, s->num_txrx_bytes);
        ret = s->regs[R_CONFIG] & ENDIAN ? cpu_to_be32(*(uint32_t *)rx_buf)
                        : cpu_to_le32(*(uint32_t *)rx_buf);
        DB_PRINT_L(0, "addr=" TARGET_FMT_plx " = %x\n", addr * 4, ret);
        xilinx_spips_update_ixr(s);
        return ret;
    }
    DB_PRINT_L(0, "addr=" TARGET_FMT_plx " = %x\n", addr * 4,
               s->regs[addr] & mask);
    return s->regs[addr] & mask;

}

static inline void tx_data_bytes(XilinxSPIPS *s, uint32_t value, int num)
{
    int i;
    for (i = 0; i < num && !fifo8_is_full(&s->tx_fifo); ++i) {
        if (s->regs[R_CONFIG] & ENDIAN) {
            fifo8_push(&s->tx_fifo, (uint8_t)(value >> 24));
            value <<= 8;
        } else {
            fifo8_push(&s->tx_fifo, (uint8_t)value);
            value >>= 8;
        }
    }
}

static void xilinx_spips_write(void *opaque, hwaddr addr,
                                        uint64_t value, unsigned size)
{
    int mask = ~0;
    int man_start_com = 0;
    XilinxSPIPS *s = opaque;

    DB_PRINT_L(0, "addr=" TARGET_FMT_plx " = %x\n", addr, (unsigned)value);
    addr >>= 2;
    switch (addr) {
    case R_CONFIG:
        mask = ~(R_CONFIG_RSVD | MAN_START_COM);
        if (value & MAN_START_COM) {
            man_start_com = 1;
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
        tx_data_bytes(s, (uint32_t)value, s->num_txrx_bytes);
        goto no_reg_update;
    case R_TXD1:
        tx_data_bytes(s, (uint32_t)value, 1);
        goto no_reg_update;
    case R_TXD2:
        tx_data_bytes(s, (uint32_t)value, 2);
        goto no_reg_update;
    case R_TXD3:
        tx_data_bytes(s, (uint32_t)value, 3);
        goto no_reg_update;
    }
    s->regs[addr] = (s->regs[addr] & ~mask) | (value & mask);
no_reg_update:
    xilinx_spips_update_cs_lines(s);
    if ((man_start_com && s->regs[R_CONFIG] & MAN_START_EN) ||
            (fifo8_is_empty(&s->tx_fifo) && s->regs[R_CONFIG] & MAN_START_EN)) {
        xilinx_spips_flush_txfifo(s);
    }
    xilinx_spips_update_cs_lines(s);
    xilinx_spips_update_ixr(s);
}

static const MemoryRegionOps spips_ops = {
    .read = xilinx_spips_read,
    .write = xilinx_spips_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void xilinx_qspips_write(void *opaque, hwaddr addr,
                                uint64_t value, unsigned size)
{
    XilinxQSPIPS *q = XILINX_QSPIPS(opaque);

    xilinx_spips_write(opaque, addr, value, size);
    addr >>= 2;

    if (addr == R_LQSPI_CFG) {
        q->lqspi_cached_addr = ~0ULL;
    }
}

static const MemoryRegionOps qspips_ops = {
    .read = xilinx_spips_read,
    .write = xilinx_qspips_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

#define LQSPI_CACHE_SIZE 1024

static uint64_t
lqspi_read(void *opaque, hwaddr addr, unsigned int size)
{
    int i;
    XilinxQSPIPS *q = opaque;
    XilinxSPIPS *s = opaque;
    uint32_t ret;

    if (addr >= q->lqspi_cached_addr &&
            addr <= q->lqspi_cached_addr + LQSPI_CACHE_SIZE - 4) {
        uint8_t *retp = &q->lqspi_buf[addr - q->lqspi_cached_addr];
        ret = cpu_to_le32(*(uint32_t *)retp);
        DB_PRINT_L(1, "addr: %08x, data: %08x\n", (unsigned)addr,
                   (unsigned)ret);
        return ret;
    } else {
        int flash_addr = (addr / num_effective_busses(s));
        int slave = flash_addr >> LQSPI_ADDRESS_BITS;
        int cache_entry = 0;
        uint32_t u_page_save = s->regs[R_LQSPI_STS] & ~LQSPI_CFG_U_PAGE;

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
                tx_data_bytes(s, 0, 1);
            }
            xilinx_spips_flush_txfifo(s);
            for (i = 0; i < 64; ++i) {
                rx_data_bytes(s, &q->lqspi_buf[cache_entry++], 1);
            }
        }

        s->regs[R_LQSPI_STS] &= ~LQSPI_CFG_U_PAGE;
        s->regs[R_LQSPI_STS] |= u_page_save;
        xilinx_spips_update_cs_lines(s);

        q->lqspi_cached_addr = flash_addr * num_effective_busses(s);
        return lqspi_read(opaque, addr, size);
    }
}

static const MemoryRegionOps lqspi_ops = {
    .read = lqspi_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
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
    int i;

    DB_PRINT_L(0, "realized spips\n");

    s->spi = g_new(SSIBus *, s->num_busses);
    for (i = 0; i < s->num_busses; ++i) {
        char bus_name[16];
        snprintf(bus_name, 16, "spi%d", i);
        s->spi[i] = ssi_create_bus(dev, bus_name);
    }

    s->cs_lines = g_new0(qemu_irq, s->num_cs * s->num_busses);
    ssi_auto_connect_slaves(DEVICE(s), s->cs_lines, s->spi[0]);
    ssi_auto_connect_slaves(DEVICE(s), s->cs_lines, s->spi[1]);
    sysbus_init_irq(sbd, &s->irq);
    for (i = 0; i < s->num_cs * s->num_busses; ++i) {
        sysbus_init_irq(sbd, &s->cs_lines[i]);
    }

    memory_region_init_io(&s->iomem, OBJECT(s), xsc->reg_ops, s,
                          "spi", R_MAX*4);
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
        VMSTATE_UINT32_ARRAY(regs, XilinxSPIPS, R_MAX),
        VMSTATE_UINT8(snoop_state, XilinxSPIPS),
        VMSTATE_END_OF_LIST()
    }
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

static void xilinx_spips_register_types(void)
{
    type_register_static(&xilinx_spips_info);
    type_register_static(&xilinx_qspips_info);
}

type_init(xilinx_spips_register_types)
