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

#include "sysbus.h"
#include "sysemu.h"
#include "ptimer.h"
#include "qemu-log.h"
#include "fifo.h"
#include "ssi.h"
#include "bitops.h"

#ifdef XILINX_SPIPS_ERR_DEBUG
#define DB_PRINT(...) do { \
    fprintf(stderr,  ": %s: ", __func__); \
    fprintf(stderr, ## __VA_ARGS__); \
    } while (0);
#else
    #define DB_PRINT(...)
#endif

/* config register */
#define R_CONFIG            (0x00 / 4)
#define IFMODE              (1 << 31)
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
#define LQSPI_CFG_LQ_MODE       (1 << 31)
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

/* 16MB per linear region */
#define LQSPI_ADDRESS_BITS 24
/* Bite off 4k chunks at a time */
#define LQSPI_CACHE_SIZE 1024

#define SNOOP_CHECKING 0xFF
#define SNOOP_NONE 0xFE
#define SNOOP_STRIPING 0

typedef struct {
    SysBusDevice busdev;
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

    uint32_t lqspi_buf[LQSPI_CACHE_SIZE];
    hwaddr lqspi_cached_addr;
} XilinxSPIPS;

static inline int num_effective_busses(XilinxSPIPS *s)
{
    return (s->regs[R_LQSPI_STS] & LQSPI_CFG_SEP_BUS &&
            s->regs[R_LQSPI_STS] & LQSPI_CFG_TWO_MEM) ? s->num_busses : 1;
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

            if (~field & (1 << i) && !found) {
                DB_PRINT("selecting slave %d\n", i);
                qemu_set_irq(s->cs_lines[cs_to_set], 0);
            } else {
                qemu_set_irq(s->cs_lines[cs_to_set], 1);
            }
        }
        if (~field & (1 << i)) {
            found = true;
        }
    }
    if (!found) {
        s->snoop_state = SNOOP_CHECKING;
    }
}

static void xilinx_spips_update_ixr(XilinxSPIPS *s)
{
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
    XilinxSPIPS *s = DO_UPCAST(XilinxSPIPS, busdev.qdev, d);

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

static void xilinx_spips_flush_txfifo(XilinxSPIPS *s)
{
    for (;;) {
        int i;
        uint8_t rx;
        uint8_t tx = 0;

        for (i = 0; i < num_effective_busses(s); ++i) {
            if (!i || s->snoop_state == SNOOP_STRIPING) {
                if (fifo8_is_empty(&s->tx_fifo)) {
                    s->regs[R_INTR_STATUS] |= IXR_TX_FIFO_UNDERFLOW;
                    xilinx_spips_update_ixr(s);
                    return;
                } else {
                    tx = fifo8_pop(&s->tx_fifo);
                }
            }
            rx = ssi_transfer(s->spi[i], (uint32_t)tx);
            DB_PRINT("tx = %02x rx = %02x\n", tx, rx);
            if (!i || s->snoop_state == SNOOP_STRIPING) {
                if (fifo8_is_full(&s->rx_fifo)) {
                    s->regs[R_INTR_STATUS] |= IXR_RX_FIFO_OVERFLOW;
                    DB_PRINT("rx FIFO overflow");
                } else {
                    fifo8_push(&s->rx_fifo, (uint8_t)rx);
                }
            }
        }

        switch (s->snoop_state) {
        case (SNOOP_CHECKING):
            switch (tx) { /* new instruction code */
            case 0x0b: /* dual/quad output read DOR/QOR */
            case 0x6b:
                s->snoop_state = 4;
                break;
            /* FIXME: these vary between vendor - set to spansion */
            case 0xbb: /* high performance dual read DIOR */
                s->snoop_state = 4;
                break;
            case 0xeb: /* high performance quad read QIOR */
                s->snoop_state = 6;
                break;
            default:
                s->snoop_state = SNOOP_NONE;
            }
            break;
        case (SNOOP_STRIPING):
        case (SNOOP_NONE):
            break;
        default:
            s->snoop_state--;
        }
    }
}

static inline void rx_data_bytes(XilinxSPIPS *s, uint32_t *value, int max)
{
    int i;

    *value = 0;
    for (i = 0; i < max && !fifo8_is_empty(&s->rx_fifo); ++i) {
        uint32_t next = fifo8_pop(&s->rx_fifo) & 0xFF;
        *value |= next << 8 * (s->regs[R_CONFIG] & ENDIAN ? 3-i : i);
    }
}

static uint64_t xilinx_spips_read(void *opaque, hwaddr addr,
                                                        unsigned size)
{
    XilinxSPIPS *s = opaque;
    uint32_t mask = ~0;
    uint32_t ret;

    addr >>= 2;
    switch (addr) {
    case R_CONFIG:
        mask = 0x0002FFFF;
        break;
    case R_INTR_STATUS:
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
        rx_data_bytes(s, &ret, s->num_txrx_bytes);
        DB_PRINT("addr=" TARGET_FMT_plx " = %x\n", addr * 4, ret);
        xilinx_spips_update_ixr(s);
        return ret;
    }
    DB_PRINT("addr=" TARGET_FMT_plx " = %x\n", addr * 4, s->regs[addr] & mask);
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

    DB_PRINT("addr=" TARGET_FMT_plx " = %x\n", addr, (unsigned)value);
    addr >>= 2;
    switch (addr) {
    case R_CONFIG:
        mask = 0x0002FFFF;
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
    if (man_start_com) {
        xilinx_spips_flush_txfifo(s);
    }
    xilinx_spips_update_ixr(s);
    xilinx_spips_update_cs_lines(s);
}

static const MemoryRegionOps spips_ops = {
    .read = xilinx_spips_read,
    .write = xilinx_spips_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

#define LQSPI_CACHE_SIZE 1024

static uint64_t
lqspi_read(void *opaque, hwaddr addr, unsigned int size)
{
    int i;
    XilinxSPIPS *s = opaque;

    if (addr >= s->lqspi_cached_addr &&
            addr <= s->lqspi_cached_addr + LQSPI_CACHE_SIZE - 4) {
        return s->lqspi_buf[(addr - s->lqspi_cached_addr) >> 2];
    } else {
        int flash_addr = (addr / num_effective_busses(s));
        int slave = flash_addr >> LQSPI_ADDRESS_BITS;
        int cache_entry = 0;

        DB_PRINT("config reg status: %08x\n", s->regs[R_LQSPI_CFG]);

        fifo8_reset(&s->tx_fifo);
        fifo8_reset(&s->rx_fifo);

        s->regs[R_CONFIG] &= ~CS;
        s->regs[R_CONFIG] |= (~(1 << slave) << CS_SHIFT) & CS;
        xilinx_spips_update_cs_lines(s);

        /* instruction */
        DB_PRINT("pushing read instruction: %02x\n",
                 (uint8_t)(s->regs[R_LQSPI_CFG] & LQSPI_CFG_INST_CODE));
        fifo8_push(&s->tx_fifo, s->regs[R_LQSPI_CFG] & LQSPI_CFG_INST_CODE);
        /* read address */
        DB_PRINT("pushing read address %06x\n", flash_addr);
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
            DB_PRINT("pushing dummy byte\n");
            fifo8_push(&s->tx_fifo, 0);
        }
        xilinx_spips_flush_txfifo(s);
        fifo8_reset(&s->rx_fifo);

        DB_PRINT("starting QSPI data read\n");

        for (i = 0; i < LQSPI_CACHE_SIZE / 4; ++i) {
            tx_data_bytes(s, 0, 4);
            xilinx_spips_flush_txfifo(s);
            rx_data_bytes(s, &s->lqspi_buf[cache_entry], 4);
            cache_entry++;
        }

        s->regs[R_CONFIG] |= CS;
        xilinx_spips_update_cs_lines(s);

        s->lqspi_cached_addr = addr;
        return lqspi_read(opaque, addr, size);
    }
}

static const MemoryRegionOps lqspi_ops = {
    .read = lqspi_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static int xilinx_spips_init(SysBusDevice *dev)
{
    XilinxSPIPS *s = FROM_SYSBUS(typeof(*s), dev);
    int i;

    DB_PRINT("inited device model\n");

    s->spi = g_new(SSIBus *, s->num_busses);
    for (i = 0; i < s->num_busses; ++i) {
        char bus_name[16];
        snprintf(bus_name, 16, "spi%d", i);
        s->spi[i] = ssi_create_bus(&dev->qdev, bus_name);
    }

    s->cs_lines = g_new(qemu_irq, s->num_cs * s->num_busses);
    ssi_auto_connect_slaves(DEVICE(s), s->cs_lines, s->spi[0]);
    ssi_auto_connect_slaves(DEVICE(s), s->cs_lines, s->spi[1]);
    sysbus_init_irq(dev, &s->irq);
    for (i = 0; i < s->num_cs * s->num_busses; ++i) {
        sysbus_init_irq(dev, &s->cs_lines[i]);
    }

    memory_region_init_io(&s->iomem, &spips_ops, s, "spi", R_MAX*4);
    sysbus_init_mmio(dev, &s->iomem);

    memory_region_init_io(&s->mmlqspi, &lqspi_ops, s, "lqspi",
                          (1 << LQSPI_ADDRESS_BITS) * 2);
    sysbus_init_mmio(dev, &s->mmlqspi);

    s->irqline = -1;
    s->lqspi_cached_addr = ~0ULL;

    fifo8_create(&s->rx_fifo, RXFF_A);
    fifo8_create(&s->tx_fifo, TXFF_A);

    return 0;
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
    .minimum_version_id_old = 2,
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
static void xilinx_spips_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);

    sdc->init = xilinx_spips_init;
    dc->reset = xilinx_spips_reset;
    dc->props = xilinx_spips_properties;
    dc->vmsd = &vmstate_xilinx_spips;
}

static const TypeInfo xilinx_spips_info = {
    .name  = "xilinx,spips",
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(XilinxSPIPS),
    .class_init = xilinx_spips_class_init,
};

static void xilinx_spips_register_types(void)
{
    type_register_static(&xilinx_spips_info);
}

type_init(xilinx_spips_register_types)
