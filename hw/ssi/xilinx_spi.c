/*
 * QEMU model of the Xilinx SPI Controller
 *
 * Copyright (C) 2010 Edgar E. Iglesias.
 * Copyright (C) 2012 Peter A. G. Crosthwaite <peter.crosthwaite@petalogix.com>
 * Copyright (C) 2012 PetaLogix
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
#include "sysemu/sysemu.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/fifo8.h"

#include "hw/ssi/ssi.h"

#ifdef XILINX_SPI_ERR_DEBUG
#define DB_PRINT(...) do { \
    fprintf(stderr,  ": %s: ", __func__); \
    fprintf(stderr, ## __VA_ARGS__); \
    } while (0)
#else
    #define DB_PRINT(...)
#endif

#define R_DGIER     (0x1c / 4)
#define R_DGIER_IE  (1 << 31)

#define R_IPISR     (0x20 / 4)
#define IRQ_DRR_NOT_EMPTY    (1 << (31 - 23))
#define IRQ_DRR_OVERRUN      (1 << (31 - 26))
#define IRQ_DRR_FULL         (1 << (31 - 27))
#define IRQ_TX_FF_HALF_EMPTY (1 << 6)
#define IRQ_DTR_UNDERRUN     (1 << 3)
#define IRQ_DTR_EMPTY        (1 << (31 - 29))

#define R_IPIER     (0x28 / 4)
#define R_SRR       (0x40 / 4)
#define R_SPICR     (0x60 / 4)
#define R_SPICR_TXFF_RST     (1 << 5)
#define R_SPICR_RXFF_RST     (1 << 6)
#define R_SPICR_MTI          (1 << 8)

#define R_SPISR     (0x64 / 4)
#define SR_TX_FULL    (1 << 3)
#define SR_TX_EMPTY   (1 << 2)
#define SR_RX_FULL    (1 << 1)
#define SR_RX_EMPTY   (1 << 0)

#define R_SPIDTR    (0x68 / 4)
#define R_SPIDRR    (0x6C / 4)
#define R_SPISSR    (0x70 / 4)
#define R_TX_FF_OCY (0x74 / 4)
#define R_RX_FF_OCY (0x78 / 4)
#define R_MAX       (0x7C / 4)

#define FIFO_CAPACITY 256

#define TYPE_XILINX_SPI "xlnx.xps-spi"
#define XILINX_SPI(obj) OBJECT_CHECK(XilinxSPI, (obj), TYPE_XILINX_SPI)

typedef struct XilinxSPI {
    SysBusDevice parent_obj;

    MemoryRegion mmio;

    qemu_irq irq;
    int irqline;

    uint8_t num_cs;
    qemu_irq *cs_lines;

    SSIBus *spi;

    Fifo8 rx_fifo;
    Fifo8 tx_fifo;

    uint32_t regs[R_MAX];
} XilinxSPI;

static void txfifo_reset(XilinxSPI *s)
{
    fifo8_reset(&s->tx_fifo);

    s->regs[R_SPISR] &= ~SR_TX_FULL;
    s->regs[R_SPISR] |= SR_TX_EMPTY;
}

static void rxfifo_reset(XilinxSPI *s)
{
    fifo8_reset(&s->rx_fifo);

    s->regs[R_SPISR] |= SR_RX_EMPTY;
    s->regs[R_SPISR] &= ~SR_RX_FULL;
}

static void xlx_spi_update_cs(XilinxSPI *s)
{
    int i;

    for (i = 0; i < s->num_cs; ++i) {
        qemu_set_irq(s->cs_lines[i], !(~s->regs[R_SPISSR] & 1 << i));
    }
}

static void xlx_spi_update_irq(XilinxSPI *s)
{
    uint32_t pending;

    s->regs[R_IPISR] |=
            (!fifo8_is_empty(&s->rx_fifo) ? IRQ_DRR_NOT_EMPTY : 0) |
            (fifo8_is_full(&s->rx_fifo) ? IRQ_DRR_FULL : 0);

    pending = s->regs[R_IPISR] & s->regs[R_IPIER];

    pending = pending && (s->regs[R_DGIER] & R_DGIER_IE);
    pending = !!pending;

    /* This call lies right in the data paths so don't call the
       irq chain unless things really changed.  */
    if (pending != s->irqline) {
        s->irqline = pending;
        DB_PRINT("irq_change of state %d ISR:%x IER:%X\n",
                    pending, s->regs[R_IPISR], s->regs[R_IPIER]);
        qemu_set_irq(s->irq, pending);
    }

}

static void xlx_spi_do_reset(XilinxSPI *s)
{
    memset(s->regs, 0, sizeof s->regs);

    rxfifo_reset(s);
    txfifo_reset(s);

    s->regs[R_SPISSR] = ~0;
    xlx_spi_update_irq(s);
    xlx_spi_update_cs(s);
}

static void xlx_spi_reset(DeviceState *d)
{
    xlx_spi_do_reset(XILINX_SPI(d));
}

static inline int spi_master_enabled(XilinxSPI *s)
{
    return !(s->regs[R_SPICR] & R_SPICR_MTI);
}

static void spi_flush_txfifo(XilinxSPI *s)
{
    uint32_t tx;
    uint32_t rx;

    while (!fifo8_is_empty(&s->tx_fifo)) {
        tx = (uint32_t)fifo8_pop(&s->tx_fifo);
        DB_PRINT("data tx:%x\n", tx);
        rx = ssi_transfer(s->spi, tx);
        DB_PRINT("data rx:%x\n", rx);
        if (fifo8_is_full(&s->rx_fifo)) {
            s->regs[R_IPISR] |= IRQ_DRR_OVERRUN;
        } else {
            fifo8_push(&s->rx_fifo, (uint8_t)rx);
            if (fifo8_is_full(&s->rx_fifo)) {
                s->regs[R_SPISR] |= SR_RX_FULL;
                s->regs[R_IPISR] |= IRQ_DRR_FULL;
            }
        }

        s->regs[R_SPISR] &= ~SR_RX_EMPTY;
        s->regs[R_SPISR] &= ~SR_TX_FULL;
        s->regs[R_SPISR] |= SR_TX_EMPTY;

        s->regs[R_IPISR] |= IRQ_DTR_EMPTY;
        s->regs[R_IPISR] |= IRQ_DRR_NOT_EMPTY;
    }

}

static uint64_t
spi_read(void *opaque, hwaddr addr, unsigned int size)
{
    XilinxSPI *s = opaque;
    uint32_t r = 0;

    addr >>= 2;
    switch (addr) {
    case R_SPIDRR:
        if (fifo8_is_empty(&s->rx_fifo)) {
            DB_PRINT("Read from empty FIFO!\n");
            return 0xdeadbeef;
        }

        s->regs[R_SPISR] &= ~SR_RX_FULL;
        r = fifo8_pop(&s->rx_fifo);
        if (fifo8_is_empty(&s->rx_fifo)) {
            s->regs[R_SPISR] |= SR_RX_EMPTY;
        }
        break;

    case R_SPISR:
        r = s->regs[addr];
        break;

    default:
        if (addr < ARRAY_SIZE(s->regs)) {
            r = s->regs[addr];
        }
        break;

    }
    DB_PRINT("addr=" TARGET_FMT_plx " = %x\n", addr * 4, r);
    xlx_spi_update_irq(s);
    return r;
}

static void
spi_write(void *opaque, hwaddr addr,
            uint64_t val64, unsigned int size)
{
    XilinxSPI *s = opaque;
    uint32_t value = val64;

    DB_PRINT("addr=" TARGET_FMT_plx " = %x\n", addr, value);
    addr >>= 2;
    switch (addr) {
    case R_SRR:
        if (value != 0xa) {
            DB_PRINT("Invalid write to SRR %x\n", value);
        } else {
            xlx_spi_do_reset(s);
        }
        break;

    case R_SPIDTR:
        s->regs[R_SPISR] &= ~SR_TX_EMPTY;
        fifo8_push(&s->tx_fifo, (uint8_t)value);
        if (fifo8_is_full(&s->tx_fifo)) {
            s->regs[R_SPISR] |= SR_TX_FULL;
        }
        if (!spi_master_enabled(s)) {
            goto done;
        } else {
            DB_PRINT("DTR and master enabled\n");
        }
        spi_flush_txfifo(s);
        break;

    case R_SPISR:
        DB_PRINT("Invalid write to SPISR %x\n", value);
        break;

    case R_IPISR:
        /* Toggle the bits.  */
        s->regs[addr] ^= value;
        break;

    /* Slave Select Register.  */
    case R_SPISSR:
        s->regs[addr] = value;
        xlx_spi_update_cs(s);
        break;

    case R_SPICR:
        /* FIXME: reset irq and sr state to empty queues.  */
        if (value & R_SPICR_RXFF_RST) {
            rxfifo_reset(s);
        }

        if (value & R_SPICR_TXFF_RST) {
            txfifo_reset(s);
        }
        value &= ~(R_SPICR_RXFF_RST | R_SPICR_TXFF_RST);
        s->regs[addr] = value;

        if (!(value & R_SPICR_MTI)) {
            spi_flush_txfifo(s);
        }
        break;

    default:
        if (addr < ARRAY_SIZE(s->regs)) {
            s->regs[addr] = value;
        }
        break;
    }

done:
    xlx_spi_update_irq(s);
}

static const MemoryRegionOps spi_ops = {
    .read = spi_read,
    .write = spi_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static void xilinx_spi_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    XilinxSPI *s = XILINX_SPI(dev);
    int i;

    DB_PRINT("\n");

    s->spi = ssi_create_bus(dev, "spi");

    sysbus_init_irq(sbd, &s->irq);
    s->cs_lines = g_new0(qemu_irq, s->num_cs);
    ssi_auto_connect_slaves(dev, s->cs_lines, s->spi);
    for (i = 0; i < s->num_cs; ++i) {
        sysbus_init_irq(sbd, &s->cs_lines[i]);
    }

    memory_region_init_io(&s->mmio, OBJECT(s), &spi_ops, s,
                          "xilinx-spi", R_MAX * 4);
    sysbus_init_mmio(sbd, &s->mmio);

    s->irqline = -1;

    fifo8_create(&s->tx_fifo, FIFO_CAPACITY);
    fifo8_create(&s->rx_fifo, FIFO_CAPACITY);
}

static const VMStateDescription vmstate_xilinx_spi = {
    .name = "xilinx_spi",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_FIFO8(tx_fifo, XilinxSPI),
        VMSTATE_FIFO8(rx_fifo, XilinxSPI),
        VMSTATE_UINT32_ARRAY(regs, XilinxSPI, R_MAX),
        VMSTATE_END_OF_LIST()
    }
};

static Property xilinx_spi_properties[] = {
    DEFINE_PROP_UINT8("num-ss-bits", XilinxSPI, num_cs, 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void xilinx_spi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = xilinx_spi_realize;
    dc->reset = xlx_spi_reset;
    dc->props = xilinx_spi_properties;
    dc->vmsd = &vmstate_xilinx_spi;
}

static const TypeInfo xilinx_spi_info = {
    .name           = TYPE_XILINX_SPI,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(XilinxSPI),
    .class_init     = xilinx_spi_class_init,
};

static void xilinx_spi_register_types(void)
{
    type_register_static(&xilinx_spi_info);
}

type_init(xilinx_spi_register_types)
