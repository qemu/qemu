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
#define R_MOD_ID            (0xFC / 4)

#define R_MAX (R_MOD_ID+1)

/* size of TXRX FIFOs */
#define NUM_CS_LINES    4
#define RXFF_A          32
#define TXFF_A          32

typedef struct {
    SysBusDevice busdev;
    MemoryRegion iomem;
    qemu_irq irq;
    int irqline;

    qemu_irq cs_lines[NUM_CS_LINES];
    SSIBus *spi;

    Fifo8 rx_fifo;
    Fifo8 tx_fifo;

    uint32_t regs[R_MAX];
} XilinxSPIPS;

static void xilinx_spips_update_cs_lines(XilinxSPIPS *s)
{
    int i;
    bool found = false;
    int field = s->regs[R_CONFIG] >> CS_SHIFT;

    for (i = 0; i < NUM_CS_LINES; i++) {
        if (~field & (1 << i) && !found) {
            found = true;
            DB_PRINT("selecting slave %d\n", i);
            qemu_set_irq(s->cs_lines[i], 0);
        } else {
            qemu_set_irq(s->cs_lines[i], 1);
        }
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
    xilinx_spips_update_ixr(s);
    xilinx_spips_update_cs_lines(s);
}

static void xilinx_spips_flush_txfifo(XilinxSPIPS *s)
{
    for (;;) {
        uint32_t r;
        uint8_t value;

        if (fifo8_is_empty(&s->tx_fifo)) {
            s->regs[R_INTR_STATUS] |= IXR_TX_FIFO_UNDERFLOW;
            break;
        } else {
            value = fifo8_pop(&s->tx_fifo);
        }

        r = ssi_transfer(s->spi, (uint32_t)value);
        DB_PRINT("tx = %02x rx = %02x\n", value, r);
        if (fifo8_is_full(&s->rx_fifo)) {
            s->regs[R_INTR_STATUS] |= IXR_RX_FIFO_OVERFLOW;
            DB_PRINT("rx FIFO overflow");
        } else {
            fifo8_push(&s->rx_fifo, (uint8_t)r);
        }
    }
    xilinx_spips_update_ixr(s);
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
        ret = (uint32_t)fifo8_pop(&s->rx_fifo);
        DB_PRINT("addr=" TARGET_FMT_plx " = %x\n", addr * 4, ret);
        xilinx_spips_update_ixr(s);
        return ret;
    }
    DB_PRINT("addr=" TARGET_FMT_plx " = %x\n", addr * 4, s->regs[addr] & mask);
    return s->regs[addr] & mask;

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
        fifo8_push(&s->tx_fifo, (uint8_t)value);
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

static int xilinx_spips_init(SysBusDevice *dev)
{
    XilinxSPIPS *s = FROM_SYSBUS(typeof(*s), dev);
    int i;

    DB_PRINT("inited device model\n");

    s->spi = ssi_create_bus(&dev->qdev, "spi");

    ssi_auto_connect_slaves(DEVICE(s), s->cs_lines, s->spi);
    sysbus_init_irq(dev, &s->irq);
    for (i = 0; i < NUM_CS_LINES; ++i) {
        sysbus_init_irq(dev, &s->cs_lines[i]);
    }

    memory_region_init_io(&s->iomem, &spips_ops, s, "spi", R_MAX*4);
    sysbus_init_mmio(dev, &s->iomem);

    s->irqline = -1;

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
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .post_load = xilinx_spips_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_FIFO8(tx_fifo, XilinxSPIPS),
        VMSTATE_FIFO8(rx_fifo, XilinxSPIPS),
        VMSTATE_UINT32_ARRAY(regs, XilinxSPIPS, R_MAX),
        VMSTATE_END_OF_LIST()
    }
};

static void xilinx_spips_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);

    sdc->init = xilinx_spips_init;
    dc->reset = xilinx_spips_reset;
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
