/*
 *  Allwinner SPI Bus Serial Interface Emulation
 *
 *  Copyright (C) 2024 Strahinja Jankovic <strahinja.p.jankovic@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/ssi/allwinner-a10-spi.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"

/* Allwinner SPI memory map */
#define SPI_RXDATA_REG   0x00 /* receive data register */
#define SPI_TXDATA_REG   0x04 /* transmit data register */
#define SPI_CTL_REG      0x08 /* control register */
#define SPI_INTCTL_REG   0x0c /* interrupt control register */
#define SPI_INT_STA_REG  0x10 /* interrupt status register */
#define SPI_DMACTL_REG   0x14 /* DMA control register */
#define SPI_WAIT_REG     0x18 /* wait clock counter register */
#define SPI_CCTL_REG     0x1c /* clock rate control register */
#define SPI_BC_REG       0x20 /* burst control register */
#define SPI_TC_REG       0x24 /* transmit counter register */
#define SPI_FIFO_STA_REG 0x28 /* FIFO status register */

/* Data register */
#define SPI_DATA_RESET 0

/* Control register */
#define SPI_CTL_SDC      (1 << 19)
#define SPI_CTL_TP_EN    (1 << 18)
#define SPI_CTL_SS_LEVEL (1 << 17)
#define SPI_CTL_SS_CTRL  (1 << 16)
#define SPI_CTL_DHB      (1 << 15)
#define SPI_CTL_DDB      (1 << 14)
#define SPI_CTL_SS       (3 << 12)
#define SPI_CTL_SS_SHIFT 12
#define SPI_CTL_RPSM     (1 << 11)
#define SPI_CTL_XCH      (1 << 10)
#define SPI_CTL_RF_RST   (1 << 9)
#define SPI_CTL_TF_RST   (1 << 8)
#define SPI_CTL_SSCTL    (1 << 7)
#define SPI_CTL_LMTF     (1 << 6)
#define SPI_CTL_DMAMC    (1 << 5)
#define SPI_CTL_SSPOL    (1 << 4)
#define SPI_CTL_POL      (1 << 3)
#define SPI_CTL_PHA      (1 << 2)
#define SPI_CTL_MODE     (1 << 1)
#define SPI_CTL_EN       (1 << 0)
#define SPI_CTL_MASK     0xFFFFFu
#define SPI_CTL_RESET    0x0002001Cu

/* Interrupt control register */
#define SPI_INTCTL_SS_INT_EN          (1 << 17)
#define SPI_INTCTL_TX_INT_EN          (1 << 16)
#define SPI_INTCTL_TF_UR_INT_EN       (1 << 14)
#define SPI_INTCTL_TF_OF_INT_EN       (1 << 13)
#define SPI_INTCTL_TF_E34_INT_EN      (1 << 12)
#define SPI_INTCTL_TF_E14_INT_EN      (1 << 11)
#define SPI_INTCTL_TF_FL_INT_EN       (1 << 10)
#define SPI_INTCTL_TF_HALF_EMP_INT_EN (1 << 9)
#define SPI_INTCTL_TF_EMP_INT_EN      (1 << 8)
#define SPI_INTCTL_RF_UR_INT_EN       (1 << 6)
#define SPI_INTCTL_RF_OF_INT_EN       (1 << 5)
#define SPI_INTCTL_RF_E34_INT_EN      (1 << 4)
#define SPI_INTCTL_RF_E14_INT_EN      (1 << 3)
#define SPI_INTCTL_RF_FU_INT_EN       (1 << 2)
#define SPI_INTCTL_RF_HALF_FU_INT_EN  (1 << 1)
#define SPI_INTCTL_RF_RDY_INT_EN      (1 << 0)
#define SPI_INTCTL_MASK               0x37F7Fu
#define SPI_INTCTL_RESET              0

/* Interrupt status register */
#define SPI_INT_STA_INT_CBF (1 << 31)
#define SPI_INT_STA_SSI     (1 << 17)
#define SPI_INT_STA_TC      (1 << 16)
#define SPI_INT_STA_TU      (1 << 14)
#define SPI_INT_STA_TO      (1 << 13)
#define SPI_INT_STA_TE34    (1 << 12)
#define SPI_INT_STA_TE14    (1 << 11)
#define SPI_INT_STA_TF      (1 << 10)
#define SPI_INT_STA_THE     (1 << 9)
#define SPI_INT_STA_TE      (1 << 8)
#define SPI_INT_STA_RU      (1 << 6)
#define SPI_INT_STA_RO      (1 << 5)
#define SPI_INT_STA_RF34    (1 << 4)
#define SPI_INT_STA_RF14    (1 << 3)
#define SPI_INT_STA_RF      (1 << 2)
#define SPI_INT_STA_RHF     (1 << 1)
#define SPI_INT_STA_RR      (1 << 0)
#define SPI_INT_STA_MASK    0x80037F7Fu
#define SPI_INT_STA_RESET   0x00001B00u

/* DMA control register - not implemented */
#define SPI_DMACTL_RESET 0

/* Wait clock register */
#define SPI_WAIT_REG_WCC_MASK 0xFFFFu
#define SPI_WAIT_RESET        0

/* Clock control register - not implemented */
#define SPI_CCTL_RESET 2

/* Burst count register */
#define SPI_BC_BC_MASK 0xFFFFFFu
#define SPI_BC_RESET   0

/* Transmi counter register */
#define SPI_TC_WTC_MASK 0xFFFFFFu
#define SPI_TC_RESET    0

/* FIFO status register */
#define SPI_FIFO_STA_CNT_MASK     0x7F
#define SPI_FIFO_STA_TF_CNT_SHIFT 16
#define SPI_FIFO_STA_RF_CNT_SHIFT 0
#define SPI_FIFO_STA_RESET        0

#define REG_INDEX(offset)         (offset / sizeof(uint32_t))


static const char *allwinner_a10_spi_get_regname(unsigned offset)
{
    switch (offset) {
    case SPI_RXDATA_REG:
        return "RXDATA";
    case SPI_TXDATA_REG:
        return "TXDATA";
    case SPI_CTL_REG:
        return "CTL";
    case SPI_INTCTL_REG:
        return "INTCTL";
    case SPI_INT_STA_REG:
        return "INT_STA";
    case SPI_DMACTL_REG:
        return "DMACTL";
    case SPI_WAIT_REG:
        return "WAIT";
    case SPI_CCTL_REG:
        return "CCTL";
    case SPI_BC_REG:
        return "BC";
    case SPI_TC_REG:
        return "TC";
    case SPI_FIFO_STA_REG:
        return "FIFO_STA";
    default:
        return "[?]";
    }
}

static bool allwinner_a10_spi_is_enabled(AWA10SPIState *s)
{
    return s->regs[REG_INDEX(SPI_CTL_REG)] & SPI_CTL_EN;
}

static void allwinner_a10_spi_txfifo_reset(AWA10SPIState *s)
{
    fifo8_reset(&s->tx_fifo);
    s->regs[REG_INDEX(SPI_INT_STA_REG)] |= (SPI_INT_STA_TE | SPI_INT_STA_TE14 |
                                            SPI_INT_STA_THE | SPI_INT_STA_TE34);
    s->regs[REG_INDEX(SPI_INT_STA_REG)] &= ~(SPI_INT_STA_TU | SPI_INT_STA_TO);
}

static void allwinner_a10_spi_rxfifo_reset(AWA10SPIState *s)
{
    fifo8_reset(&s->rx_fifo);
    s->regs[REG_INDEX(SPI_INT_STA_REG)] &=
        ~(SPI_INT_STA_RU | SPI_INT_STA_RO | SPI_INT_STA_RF | SPI_INT_STA_RR |
          SPI_INT_STA_RHF | SPI_INT_STA_RF14 | SPI_INT_STA_RF34);
}

static uint8_t allwinner_a10_spi_selected_channel(AWA10SPIState *s)
{
    return (s->regs[REG_INDEX(SPI_CTL_REG)] & SPI_CTL_SS) >> SPI_CTL_SS_SHIFT;
}

static void allwinner_a10_spi_reset_hold(Object *obj, ResetType type)
{
    AWA10SPIState *s = AW_A10_SPI(obj);

    s->regs[REG_INDEX(SPI_RXDATA_REG)] = SPI_DATA_RESET;
    s->regs[REG_INDEX(SPI_TXDATA_REG)] = SPI_DATA_RESET;
    s->regs[REG_INDEX(SPI_CTL_REG)] = SPI_CTL_RESET;
    s->regs[REG_INDEX(SPI_INTCTL_REG)] = SPI_INTCTL_RESET;
    s->regs[REG_INDEX(SPI_INT_STA_REG)] = SPI_INT_STA_RESET;
    s->regs[REG_INDEX(SPI_DMACTL_REG)] = SPI_DMACTL_RESET;
    s->regs[REG_INDEX(SPI_WAIT_REG)] = SPI_WAIT_RESET;
    s->regs[REG_INDEX(SPI_CCTL_REG)] = SPI_CCTL_RESET;
    s->regs[REG_INDEX(SPI_BC_REG)] = SPI_BC_RESET;
    s->regs[REG_INDEX(SPI_TC_REG)] = SPI_TC_RESET;
    s->regs[REG_INDEX(SPI_FIFO_STA_REG)] = SPI_FIFO_STA_RESET;

    allwinner_a10_spi_txfifo_reset(s);
    allwinner_a10_spi_rxfifo_reset(s);
}

static void allwinner_a10_spi_update_irq(AWA10SPIState *s)
{
    bool level;

    if (fifo8_is_empty(&s->rx_fifo)) {
        s->regs[REG_INDEX(SPI_INT_STA_REG)] &= ~SPI_INT_STA_RR;
    } else {
        s->regs[REG_INDEX(SPI_INT_STA_REG)] |= SPI_INT_STA_RR;
    }

    if (fifo8_num_used(&s->rx_fifo) >= (AW_A10_SPI_FIFO_SIZE >> 2)) {
        s->regs[REG_INDEX(SPI_INT_STA_REG)] |= SPI_INT_STA_RF14;
    } else {
        s->regs[REG_INDEX(SPI_INT_STA_REG)] &= ~SPI_INT_STA_RF14;
    }

    if (fifo8_num_used(&s->rx_fifo) >= (AW_A10_SPI_FIFO_SIZE >> 1)) {
        s->regs[REG_INDEX(SPI_INT_STA_REG)] |= SPI_INT_STA_RHF;
    } else {
        s->regs[REG_INDEX(SPI_INT_STA_REG)] &= ~SPI_INT_STA_RHF;
    }

    if (fifo8_num_free(&s->rx_fifo) <= (AW_A10_SPI_FIFO_SIZE >> 2)) {
        s->regs[REG_INDEX(SPI_INT_STA_REG)] |= SPI_INT_STA_RF34;
    } else {
        s->regs[REG_INDEX(SPI_INT_STA_REG)] &= ~SPI_INT_STA_RF34;
    }

    if (fifo8_is_full(&s->rx_fifo)) {
        s->regs[REG_INDEX(SPI_INT_STA_REG)] |= SPI_INT_STA_RF;
    } else {
        s->regs[REG_INDEX(SPI_INT_STA_REG)] &= ~SPI_INT_STA_RF;
    }

    if (fifo8_is_empty(&s->tx_fifo)) {
        s->regs[REG_INDEX(SPI_INT_STA_REG)] |= SPI_INT_STA_TE;
    } else {
        s->regs[REG_INDEX(SPI_INT_STA_REG)] &= ~SPI_INT_STA_TE;
    }

    if (fifo8_num_free(&s->tx_fifo) >= (AW_A10_SPI_FIFO_SIZE >> 2)) {
        s->regs[REG_INDEX(SPI_INT_STA_REG)] |= SPI_INT_STA_TE14;
    } else {
        s->regs[REG_INDEX(SPI_INT_STA_REG)] &= ~SPI_INT_STA_TE14;
    }

    if (fifo8_num_free(&s->tx_fifo) >= (AW_A10_SPI_FIFO_SIZE >> 1)) {
        s->regs[REG_INDEX(SPI_INT_STA_REG)] |= SPI_INT_STA_THE;
    } else {
        s->regs[REG_INDEX(SPI_INT_STA_REG)] &= ~SPI_INT_STA_THE;
    }

    if (fifo8_num_used(&s->tx_fifo) <= (AW_A10_SPI_FIFO_SIZE >> 2)) {
        s->regs[REG_INDEX(SPI_INT_STA_REG)] |= SPI_INT_STA_TE34;
    } else {
        s->regs[REG_INDEX(SPI_INT_STA_REG)] &= ~SPI_INT_STA_TE34;
    }

    if (fifo8_is_full(&s->rx_fifo)) {
        s->regs[REG_INDEX(SPI_INT_STA_REG)] |= SPI_INT_STA_TF;
    } else {
        s->regs[REG_INDEX(SPI_INT_STA_REG)] &= ~SPI_INT_STA_TF;
    }

    level = (s->regs[REG_INDEX(SPI_INT_STA_REG)] &
             s->regs[REG_INDEX(SPI_INTCTL_REG)]) != 0;

    qemu_set_irq(s->irq, level);

    trace_allwinner_a10_spi_update_irq(level);
}

static void allwinner_a10_spi_flush_txfifo(AWA10SPIState *s)
{
    uint32_t burst_count = s->regs[REG_INDEX(SPI_BC_REG)];
    uint32_t tx_burst = s->regs[REG_INDEX(SPI_TC_REG)];
    trace_allwinner_a10_spi_burst_length(tx_burst);

    trace_allwinner_a10_spi_flush_txfifo_begin(fifo8_num_used(&s->tx_fifo),
                                               fifo8_num_used(&s->rx_fifo));

    while (!fifo8_is_empty(&s->tx_fifo)) {
        uint8_t tx = fifo8_pop(&s->tx_fifo);
        uint8_t rx = 0;
        bool fill_rx = true;

        trace_allwinner_a10_spi_tx(tx);

        /* Write one byte at a time */
        rx = ssi_transfer(s->bus, tx);

        trace_allwinner_a10_spi_rx(rx);

        /* Check DHB here to determine if RX bytes should be stored */
        if (s->regs[REG_INDEX(SPI_CTL_REG)] & SPI_CTL_DHB) {
            /* Store rx bytes only after WTC transfers */
            if (tx_burst > 0u) {
                fill_rx = false;
                tx_burst--;
            }
        }

        if (fill_rx) {
            if (fifo8_is_full(&s->rx_fifo)) {
                s->regs[REG_INDEX(SPI_INT_STA_REG)] |= SPI_INT_STA_RF;
            } else {
                fifo8_push(&s->rx_fifo, rx);
            }
        }

        allwinner_a10_spi_update_irq(s);

        burst_count--;

        if (burst_count == 0) {
            s->regs[REG_INDEX(SPI_INT_STA_REG)] |= SPI_INT_STA_TC;
            s->regs[REG_INDEX(SPI_CTL_REG)] &= ~SPI_CTL_XCH;
            break;
        }
    }

    if (fifo8_is_empty(&s->tx_fifo)) {
        s->regs[REG_INDEX(SPI_INT_STA_REG)] |= SPI_INT_STA_TC;
        s->regs[REG_INDEX(SPI_CTL_REG)] &= ~SPI_CTL_XCH;
    }

    trace_allwinner_a10_spi_flush_txfifo_end(fifo8_num_used(&s->tx_fifo),
                                             fifo8_num_used(&s->rx_fifo));
}

static uint64_t allwinner_a10_spi_read(void *opaque, hwaddr offset,
                                       unsigned size)
{
    uint32_t value = 0;
    AWA10SPIState *s = opaque;
    uint32_t index = offset >> 2;

    if (offset > SPI_FIFO_STA_REG) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "[%s]%s: Bad register at offset 0x%" HWADDR_PRIx "\n",
                      TYPE_AW_A10_SPI, __func__, offset);
        return 0;
    }

    value = s->regs[index];

    if (allwinner_a10_spi_is_enabled(s)) {
        switch (offset) {
        case SPI_RXDATA_REG:
            if (fifo8_is_empty(&s->rx_fifo)) {
                /* value is undefined */
                value = 0xdeadbeef;
            } else {
                /* read from the RX FIFO */
                value = fifo8_pop(&s->rx_fifo);
            }
            break;
        case SPI_TXDATA_REG:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "[%s]%s: Trying to read from TX FIFO\n",
                          TYPE_AW_A10_SPI, __func__);

            /* Reading from TXDATA gives 0 */
            break;
        case SPI_FIFO_STA_REG:
            /* Read current tx/rx fifo data count */
            value = fifo8_num_used(&s->tx_fifo) << SPI_FIFO_STA_TF_CNT_SHIFT |
                    fifo8_num_used(&s->rx_fifo) << SPI_FIFO_STA_RF_CNT_SHIFT;
            break;
        case SPI_CTL_REG:
        case SPI_INTCTL_REG:
        case SPI_INT_STA_REG:
        case SPI_DMACTL_REG:
        case SPI_WAIT_REG:
        case SPI_CCTL_REG:
        case SPI_BC_REG:
        case SPI_TC_REG:
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                    "%s: bad offset 0x%x\n", __func__,
                    (uint32_t)offset);
            break;
        }

        allwinner_a10_spi_update_irq(s);
    }
    trace_allwinner_a10_spi_read(allwinner_a10_spi_get_regname(offset), value);

    return value;
}

static bool allwinner_a10_spi_update_cs_level(AWA10SPIState *s, int cs_line_nr)
{
    if (cs_line_nr == allwinner_a10_spi_selected_channel(s)) {
        return (s->regs[REG_INDEX(SPI_CTL_REG)] & SPI_CTL_SS_LEVEL) != 0;
    } else {
        return (s->regs[REG_INDEX(SPI_CTL_REG)] & SPI_CTL_SSPOL) != 0;
    }
}

static void allwinner_a10_spi_write(void *opaque, hwaddr offset, uint64_t value,
                                    unsigned size)
{
    AWA10SPIState *s = opaque;
    uint32_t index = offset >> 2;
    int i = 0;

    if (offset > SPI_FIFO_STA_REG) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "[%s]%s: Bad register at offset 0x%" HWADDR_PRIx "\n",
                      TYPE_AW_A10_SPI, __func__, offset);
        return;
    }

    trace_allwinner_a10_spi_write(allwinner_a10_spi_get_regname(offset),
                                  (uint32_t)value);

    if (!allwinner_a10_spi_is_enabled(s)) {
        /* Block is disabled */
        if (offset != SPI_CTL_REG) {
            /* Ignore access */
            return;
        }
    }

    switch (offset) {
    case SPI_RXDATA_REG:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Trying to write to RX FIFO\n",
                      TYPE_AW_A10_SPI, __func__);
        break;
    case SPI_TXDATA_REG:
        if (fifo8_is_full(&s->tx_fifo)) {
            /* Ignore writes if queue is full */
            break;
        }

        fifo8_push(&s->tx_fifo, (uint8_t)value);

        break;
    case SPI_INT_STA_REG:
        /* Handle W1C bits - everything except SPI_INT_STA_INT_CBF. */
        value &= ~SPI_INT_STA_INT_CBF;
        s->regs[REG_INDEX(SPI_INT_STA_REG)] &= ~(value & SPI_INT_STA_MASK);
        break;
    case SPI_CTL_REG:
        s->regs[REG_INDEX(SPI_CTL_REG)] = value;

        for (i = 0; i < AW_A10_SPI_CS_LINES_NR; i++) {
            qemu_set_irq(
                s->cs_lines[i],
                allwinner_a10_spi_update_cs_level(s, i));
        }

        if (s->regs[REG_INDEX(SPI_CTL_REG)] & SPI_CTL_XCH) {
            /* Request to start emitting */
            allwinner_a10_spi_flush_txfifo(s);
        }
        if (s->regs[REG_INDEX(SPI_CTL_REG)] & SPI_CTL_TF_RST) {
            allwinner_a10_spi_txfifo_reset(s);
            s->regs[REG_INDEX(SPI_CTL_REG)] &= ~SPI_CTL_TF_RST;
        }
        if (s->regs[REG_INDEX(SPI_CTL_REG)] & SPI_CTL_RF_RST) {
            allwinner_a10_spi_rxfifo_reset(s);
            s->regs[REG_INDEX(SPI_CTL_REG)] &= ~SPI_CTL_RF_RST;
        }
        break;
    case SPI_INTCTL_REG:
    case SPI_DMACTL_REG:
    case SPI_WAIT_REG:
    case SPI_CCTL_REG:
    case SPI_BC_REG:
    case SPI_TC_REG:
    case SPI_FIFO_STA_REG:
        s->regs[index] = value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
            "%s: bad offset 0x%x\n", __func__,
            (uint32_t)offset);
        break;
    }

    allwinner_a10_spi_update_irq(s);
}

static const MemoryRegionOps allwinner_a10_spi_ops = {
    .read = allwinner_a10_spi_read,
    .write = allwinner_a10_spi_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const VMStateDescription allwinner_a10_spi_vmstate = {
    .name = TYPE_AW_A10_SPI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_FIFO8(tx_fifo, AWA10SPIState),
        VMSTATE_FIFO8(rx_fifo, AWA10SPIState),
        VMSTATE_UINT32_ARRAY(regs, AWA10SPIState, AW_A10_SPI_REGS_NUM),
        VMSTATE_END_OF_LIST()
    }
};

static void allwinner_a10_spi_realize(DeviceState *dev, Error **errp)
{
    AWA10SPIState *s = AW_A10_SPI(dev);
    int i = 0;

    memory_region_init_io(&s->iomem, OBJECT(s), &allwinner_a10_spi_ops, s,
                          TYPE_AW_A10_SPI, AW_A10_SPI_IOSIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);

    s->bus = ssi_create_bus(dev, "spi");
    for (i = 0; i < AW_A10_SPI_CS_LINES_NR; i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->cs_lines[i]);
    }
    fifo8_create(&s->tx_fifo, AW_A10_SPI_FIFO_SIZE);
    fifo8_create(&s->rx_fifo, AW_A10_SPI_FIFO_SIZE);
}

static void allwinner_a10_spi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = allwinner_a10_spi_reset_hold;
    dc->vmsd = &allwinner_a10_spi_vmstate;
    dc->realize = allwinner_a10_spi_realize;
    dc->desc = "Allwinner A10 SPI Controller";
}

static const TypeInfo allwinner_a10_spi_type_info = {
    .name = TYPE_AW_A10_SPI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AWA10SPIState),
    .class_init = allwinner_a10_spi_class_init,
};

static void allwinner_a10_spi_register_types(void)
{
    type_register_static(&allwinner_a10_spi_type_info);
}

type_init(allwinner_a10_spi_register_types)
