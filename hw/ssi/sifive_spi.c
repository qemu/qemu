/*
 * QEMU model of the SiFive SPI Controller
 *
 * Copyright (c) 2021 Wind River Systems, Inc.
 *
 * Author:
 *   Bin Meng <bin.meng@windriver.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qemu/fifo8.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/ssi/sifive_spi.h"

#define R_SCKDIV        (0x00 / 4)
#define R_SCKMODE       (0x04 / 4)
#define R_CSID          (0x10 / 4)
#define R_CSDEF         (0x14 / 4)
#define R_CSMODE        (0x18 / 4)
#define R_DELAY0        (0x28 / 4)
#define R_DELAY1        (0x2C / 4)
#define R_FMT           (0x40 / 4)
#define R_TXDATA        (0x48 / 4)
#define R_RXDATA        (0x4C / 4)
#define R_TXMARK        (0x50 / 4)
#define R_RXMARK        (0x54 / 4)
#define R_FCTRL         (0x60 / 4)
#define R_FFMT          (0x64 / 4)
#define R_IE            (0x70 / 4)
#define R_IP            (0x74 / 4)

#define FMT_DIR         (1 << 3)

#define TXDATA_FULL     (1 << 31)
#define RXDATA_EMPTY    (1 << 31)

#define IE_TXWM         (1 << 0)
#define IE_RXWM         (1 << 1)

#define IP_TXWM         (1 << 0)
#define IP_RXWM         (1 << 1)

#define FIFO_CAPACITY   8

static void sifive_spi_txfifo_reset(SiFiveSPIState *s)
{
    fifo8_reset(&s->tx_fifo);

    s->regs[R_TXDATA] &= ~TXDATA_FULL;
    s->regs[R_IP] &= ~IP_TXWM;
}

static void sifive_spi_rxfifo_reset(SiFiveSPIState *s)
{
    fifo8_reset(&s->rx_fifo);

    s->regs[R_RXDATA] |= RXDATA_EMPTY;
    s->regs[R_IP] &= ~IP_RXWM;
}

static void sifive_spi_update_cs(SiFiveSPIState *s)
{
    int i;

    for (i = 0; i < s->num_cs; i++) {
        if (s->regs[R_CSDEF] & (1 << i)) {
            qemu_set_irq(s->cs_lines[i], !(s->regs[R_CSMODE]));
        }
    }
}

static void sifive_spi_update_irq(SiFiveSPIState *s)
{
    int level;

    if (fifo8_num_used(&s->tx_fifo) < s->regs[R_TXMARK]) {
        s->regs[R_IP] |= IP_TXWM;
    } else {
        s->regs[R_IP] &= ~IP_TXWM;
    }

    if (fifo8_num_used(&s->rx_fifo) > s->regs[R_RXMARK]) {
        s->regs[R_IP] |= IP_RXWM;
    } else {
        s->regs[R_IP] &= ~IP_RXWM;
    }

    level = s->regs[R_IP] & s->regs[R_IE] ? 1 : 0;
    qemu_set_irq(s->irq, level);
}

static void sifive_spi_reset(DeviceState *d)
{
    SiFiveSPIState *s = SIFIVE_SPI(d);

    memset(s->regs, 0, sizeof(s->regs));

    /* The reset value is high for all implemented CS pins */
    s->regs[R_CSDEF] = (1 << s->num_cs) - 1;

    /* Populate register with their default value */
    s->regs[R_SCKDIV] = 0x03;
    s->regs[R_DELAY0] = 0x1001;
    s->regs[R_DELAY1] = 0x01;

    sifive_spi_txfifo_reset(s);
    sifive_spi_rxfifo_reset(s);

    sifive_spi_update_cs(s);
    sifive_spi_update_irq(s);
}

static void sifive_spi_flush_txfifo(SiFiveSPIState *s)
{
    uint8_t tx;
    uint8_t rx;

    while (!fifo8_is_empty(&s->tx_fifo)) {
        tx = fifo8_pop(&s->tx_fifo);
        rx = ssi_transfer(s->spi, tx);

        if (!fifo8_is_full(&s->rx_fifo)) {
            if (!(s->regs[R_FMT] & FMT_DIR)) {
                fifo8_push(&s->rx_fifo, rx);
            }
        }
    }
}

static bool sifive_spi_is_bad_reg(hwaddr addr, bool allow_reserved)
{
    bool bad;

    switch (addr) {
    /* reserved offsets */
    case 0x08:
    case 0x0C:
    case 0x1C:
    case 0x20:
    case 0x24:
    case 0x30:
    case 0x34:
    case 0x38:
    case 0x3C:
    case 0x44:
    case 0x58:
    case 0x5C:
    case 0x68:
    case 0x6C:
        bad = allow_reserved ? false : true;
        break;
    default:
        bad = false;
    }

    if (addr >= (SIFIVE_SPI_REG_NUM << 2)) {
        bad = true;
    }

    return bad;
}

static uint64_t sifive_spi_read(void *opaque, hwaddr addr, unsigned int size)
{
    SiFiveSPIState *s = opaque;
    uint32_t r;

    if (sifive_spi_is_bad_reg(addr, true)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read at address 0x%"
                      HWADDR_PRIx "\n", __func__, addr);
        return 0;
    }

    addr >>= 2;
    switch (addr) {
    case R_TXDATA:
        if (fifo8_is_full(&s->tx_fifo)) {
            return TXDATA_FULL;
        }
        r = 0;
        break;

    case R_RXDATA:
        if (fifo8_is_empty(&s->rx_fifo)) {
            return RXDATA_EMPTY;
        }
        r = fifo8_pop(&s->rx_fifo);
        break;

    default:
        r = s->regs[addr];
        break;
    }

    sifive_spi_update_irq(s);

    return r;
}

static void sifive_spi_write(void *opaque, hwaddr addr,
                             uint64_t val64, unsigned int size)
{
    SiFiveSPIState *s = opaque;
    uint32_t value = val64;

    if (sifive_spi_is_bad_reg(addr, false)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write at addr=0x%"
                      HWADDR_PRIx " value=0x%x\n", __func__, addr, value);
        return;
    }

    addr >>= 2;
    switch (addr) {
    case R_CSID:
        if (value >= s->num_cs) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid csid %d\n",
                          __func__, value);
        } else {
            s->regs[R_CSID] = value;
            sifive_spi_update_cs(s);
        }
        break;

    case R_CSDEF:
        if (value >= (1 << s->num_cs)) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid csdef %x\n",
                          __func__, value);
        } else {
            s->regs[R_CSDEF] = value;
        }
        break;

    case R_CSMODE:
        if (value > 3) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid csmode %x\n",
                          __func__, value);
        } else {
            s->regs[R_CSMODE] = value;
            sifive_spi_update_cs(s);
        }
        break;

    case R_TXDATA:
        if (!fifo8_is_full(&s->tx_fifo)) {
            fifo8_push(&s->tx_fifo, (uint8_t)value);
            sifive_spi_flush_txfifo(s);
        }
        break;

    case R_RXDATA:
    case R_IP:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid write to read-only reigster 0x%"
                      HWADDR_PRIx " with 0x%x\n", __func__, addr << 2, value);
        break;

    case R_TXMARK:
    case R_RXMARK:
        if (value >= FIFO_CAPACITY) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid watermark %d\n",
                          __func__, value);
        } else {
            s->regs[addr] = value;
        }
        break;

    case R_FCTRL:
    case R_FFMT:
        qemu_log_mask(LOG_UNIMP,
                      "%s: direct-map flash interface unimplemented\n",
                      __func__);
        break;

    default:
        s->regs[addr] = value;
        break;
    }

    sifive_spi_update_irq(s);
}

static const MemoryRegionOps sifive_spi_ops = {
    .read = sifive_spi_read,
    .write = sifive_spi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static void sifive_spi_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    SiFiveSPIState *s = SIFIVE_SPI(dev);
    int i;

    s->spi = ssi_create_bus(dev, "spi");
    sysbus_init_irq(sbd, &s->irq);

    s->cs_lines = g_new0(qemu_irq, s->num_cs);
    for (i = 0; i < s->num_cs; i++) {
        sysbus_init_irq(sbd, &s->cs_lines[i]);
    }

    memory_region_init_io(&s->mmio, OBJECT(s), &sifive_spi_ops, s,
                          TYPE_SIFIVE_SPI, 0x1000);
    sysbus_init_mmio(sbd, &s->mmio);

    fifo8_create(&s->tx_fifo, FIFO_CAPACITY);
    fifo8_create(&s->rx_fifo, FIFO_CAPACITY);
}

static Property sifive_spi_properties[] = {
    DEFINE_PROP_UINT32("num-cs", SiFiveSPIState, num_cs, 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void sifive_spi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, sifive_spi_properties);
    dc->reset = sifive_spi_reset;
    dc->realize = sifive_spi_realize;
}

static const TypeInfo sifive_spi_info = {
    .name           = TYPE_SIFIVE_SPI,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(SiFiveSPIState),
    .class_init     = sifive_spi_class_init,
};

static void sifive_spi_register_types(void)
{
    type_register_static(&sifive_spi_info);
}

type_init(sifive_spi_register_types)
