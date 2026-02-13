/*
 * calypso_spi.c — Calypso SPI + TWL3025 ABB
 *
 * SPI controller with integrated TWL3025 Analog Baseband emulation.
 *
 * BUG FIX vs previous implementation:
 *   - twl3025_spi_xfer() is now actually called on TX writes
 *     (was marked __unused__, calypso_spi_read returned hardcoded 0x2)
 *   - Firmware can now read VRPCSTS, ITSTATREG, etc. via SPI protocol
 *
 * Calypso SPI wire protocol:
 *   TX word: bit[15]=R/W, bits[14:6]=register addr, bits[5:0]=write data
 *   RX word: for reads, returns the register value
 *
 * Register map (16-bit, offsets from base):
 *   0x00  STATUS  (bit0=TX_READY, bit1=RX_READY)
 *   0x02  CTRL
 *   0x04  TX      (write triggers SPI transaction)
 *   0x06  RX      (result of last transaction)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "qemu/log.h"
#include "calypso_spi.h"

/* ---- TWL3025 ABB SPI transaction ---- */

static uint16_t twl3025_spi_xfer(CalypsoSPIState *s, uint16_t tx)
{
    int read  = (tx >> 15) & 1;
    int addr  = (tx >> 6) & 0x1FF;
    int wdata = tx & 0x3F;

    if (addr >= 256) {
        addr = 0;
    }

    if (read) {
        return s->abb_regs[addr];
    } else {
        s->abb_regs[addr] = wdata;
        /* Side effects for specific registers */
        if (addr == ABB_VRPCDEV) {
            /* Writing power control → update power status */
            s->abb_regs[ABB_VRPCSTS] = 0x1F; /* All regulators on */
        }
        return 0;
    }
}

/* ---- MMIO ---- */

static uint64_t calypso_spi_read(void *opaque, hwaddr offset, unsigned size)
{
    CalypsoSPIState *s = CALYPSO_SPI(opaque);

    switch (offset) {
    case 0x00: /* STATUS */
        return s->status;
    case 0x02: /* CTRL */
        return s->ctrl;
    case 0x04: /* TX (read-back) */
        return s->tx_data;
    case 0x06: /* RX */
        return s->rx_data;
    default:
        qemu_log_mask(LOG_UNIMP, "calypso-spi: unimplemented read 0x%02x\n",
                       (unsigned)offset);
        return 0;
    }
}

static void calypso_spi_write(void *opaque, hwaddr offset, uint64_t value,
                               unsigned size)
{
    CalypsoSPIState *s = CALYPSO_SPI(opaque);

    switch (offset) {
    case 0x00: /* STATUS (write to clear bits) */
        s->status &= ~(value & 0xFFFF);
        break;
    case 0x02: /* CTRL */
        s->ctrl = value & 0xFFFF;
        break;
    case 0x04: /* TX — triggers SPI transaction */
        s->tx_data = value & 0xFFFF;
        s->rx_data = twl3025_spi_xfer(s, s->tx_data);
        s->status = SPI_STATUS_TX_READY | SPI_STATUS_RX_READY;
        /* Raise IRQ to signal completion */
        qemu_irq_pulse(s->irq);
        break;
    case 0x06: /* RX (write ignored) */
        break;
    default:
        break;
    }
}

static const MemoryRegionOps calypso_spi_ops = {
    .read = calypso_spi_read,
    .write = calypso_spi_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = { .min_access_size = 2, .max_access_size = 2 },
};

/* ---- QOM lifecycle ---- */

static void calypso_spi_realize(DeviceState *dev, Error **errp)
{
    CalypsoSPIState *s = CALYPSO_SPI(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &calypso_spi_ops, s,
                          "calypso-spi", 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);
}

static void calypso_spi_reset(DeviceState *dev)
{
    CalypsoSPIState *s = CALYPSO_SPI(dev);

    s->ctrl = 0;
    s->status = SPI_STATUS_TX_READY;  /* TX ready at reset */
    s->tx_data = 0;
    s->rx_data = 0;
    memset(s->abb_regs, 0, sizeof(s->abb_regs));

    /* Power-on defaults: all regulators on */
    s->abb_regs[ABB_VRPCSTS] = 0x1F;
    s->abb_regs[ABB_ITSTATREG] = 0x00;
}

static void calypso_spi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = calypso_spi_realize;
    device_class_set_legacy_reset(dc, calypso_spi_reset);
    dc->desc = "Calypso SPI controller + TWL3025 ABB";
}

static const TypeInfo calypso_spi_info = {
    .name          = TYPE_CALYPSO_SPI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(CalypsoSPIState),
    .class_init    = calypso_spi_class_init,
};

static void calypso_spi_register_types(void)
{
    type_register_static(&calypso_spi_info);
}

type_init(calypso_spi_register_types)
