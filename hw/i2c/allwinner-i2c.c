/*
 *  Allwinner I2C Bus Serial Interface Emulation
 *
 *  Copyright (C) 2022 Strahinja Jankovic <strahinja.p.jankovic@gmail.com>
 *
 *  This file is derived from IMX I2C controller,
 *  by Jean-Christophe DUBOIS .
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
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "hw/i2c/allwinner-i2c.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "hw/i2c/i2c.h"
#include "qemu/log.h"
#include "trace.h"
#include "qemu/module.h"

/* Allwinner I2C memory map */
#define TWI_ADDR_REG            0x00  /* slave address register */
#define TWI_XADDR_REG           0x04  /* extended slave address register */
#define TWI_DATA_REG            0x08  /* data register */
#define TWI_CNTR_REG            0x0c  /* control register */
#define TWI_STAT_REG            0x10  /* status register */
#define TWI_CCR_REG             0x14  /* clock control register */
#define TWI_SRST_REG            0x18  /* software reset register */
#define TWI_EFR_REG             0x1c  /* enhance feature register */
#define TWI_LCR_REG             0x20  /* line control register */

/* Used only in slave mode, do not set */
#define TWI_ADDR_RESET          0
#define TWI_XADDR_RESET         0

/* Data register */
#define TWI_DATA_MASK           0xFF
#define TWI_DATA_RESET          0

/* Control register */
#define TWI_CNTR_INT_EN         (1 << 7)
#define TWI_CNTR_BUS_EN         (1 << 6)
#define TWI_CNTR_M_STA          (1 << 5)
#define TWI_CNTR_M_STP          (1 << 4)
#define TWI_CNTR_INT_FLAG       (1 << 3)
#define TWI_CNTR_A_ACK          (1 << 2)
#define TWI_CNTR_MASK           0xFC
#define TWI_CNTR_RESET          0

/* Status register */
#define TWI_STAT_MASK           0xF8
#define TWI_STAT_RESET          0xF8

/* Clock register */
#define TWI_CCR_CLK_M_MASK      0x78
#define TWI_CCR_CLK_N_MASK      0x07
#define TWI_CCR_MASK            0x7F
#define TWI_CCR_RESET           0

/* Soft reset */
#define TWI_SRST_MASK           0x01
#define TWI_SRST_RESET          0

/* Enhance feature */
#define TWI_EFR_MASK            0x03
#define TWI_EFR_RESET           0

/* Line control */
#define TWI_LCR_SCL_STATE       (1 << 5)
#define TWI_LCR_SDA_STATE       (1 << 4)
#define TWI_LCR_SCL_CTL         (1 << 3)
#define TWI_LCR_SCL_CTL_EN      (1 << 2)
#define TWI_LCR_SDA_CTL         (1 << 1)
#define TWI_LCR_SDA_CTL_EN      (1 << 0)
#define TWI_LCR_MASK            0x3F
#define TWI_LCR_RESET           0x3A

/* Status value in STAT register is shifted by 3 bits */
#define TWI_STAT_SHIFT      3
#define STAT_FROM_STA(x)    ((x) << TWI_STAT_SHIFT)
#define STAT_TO_STA(x)      ((x) >> TWI_STAT_SHIFT)

enum {
    STAT_BUS_ERROR = 0,
    /* Master mode */
    STAT_M_STA_TX,
    STAT_M_RSTA_TX,
    STAT_M_ADDR_WR_ACK,
    STAT_M_ADDR_WR_NACK,
    STAT_M_DATA_TX_ACK,
    STAT_M_DATA_TX_NACK,
    STAT_M_ARB_LOST,
    STAT_M_ADDR_RD_ACK,
    STAT_M_ADDR_RD_NACK,
    STAT_M_DATA_RX_ACK,
    STAT_M_DATA_RX_NACK,
    /* Slave mode */
    STAT_S_ADDR_WR_ACK,
    STAT_S_ARB_LOST_AW_ACK,
    STAT_S_GCA_ACK,
    STAT_S_ARB_LOST_GCA_ACK,
    STAT_S_DATA_RX_SA_ACK,
    STAT_S_DATA_RX_SA_NACK,
    STAT_S_DATA_RX_GCA_ACK,
    STAT_S_DATA_RX_GCA_NACK,
    STAT_S_STP_RSTA,
    STAT_S_ADDR_RD_ACK,
    STAT_S_ARB_LOST_AR_ACK,
    STAT_S_DATA_TX_ACK,
    STAT_S_DATA_TX_NACK,
    STAT_S_LB_TX_ACK,
    /* Master mode, 10-bit */
    STAT_M_2ND_ADDR_WR_ACK,
    STAT_M_2ND_ADDR_WR_NACK,
    /* Idle */
    STAT_IDLE = 0x1f
} TWI_STAT_STA;

static const char *allwinner_i2c_get_regname(unsigned offset)
{
    switch (offset) {
    case TWI_ADDR_REG:
        return "ADDR";
    case TWI_XADDR_REG:
        return "XADDR";
    case TWI_DATA_REG:
        return "DATA";
    case TWI_CNTR_REG:
        return "CNTR";
    case TWI_STAT_REG:
        return "STAT";
    case TWI_CCR_REG:
        return "CCR";
    case TWI_SRST_REG:
        return "SRST";
    case TWI_EFR_REG:
        return "EFR";
    case TWI_LCR_REG:
        return "LCR";
    default:
        return "[?]";
    }
}

static inline bool allwinner_i2c_is_reset(AWI2CState *s)
{
    return s->srst & TWI_SRST_MASK;
}

static inline bool allwinner_i2c_bus_is_enabled(AWI2CState *s)
{
    return s->cntr & TWI_CNTR_BUS_EN;
}

static inline bool allwinner_i2c_interrupt_is_enabled(AWI2CState *s)
{
    return s->cntr & TWI_CNTR_INT_EN;
}

static void allwinner_i2c_reset_hold(Object *obj)
{
    AWI2CState *s = AW_I2C(obj);

    if (STAT_TO_STA(s->stat) != STAT_IDLE) {
        i2c_end_transfer(s->bus);
    }

    s->addr  = TWI_ADDR_RESET;
    s->xaddr = TWI_XADDR_RESET;
    s->data  = TWI_DATA_RESET;
    s->cntr  = TWI_CNTR_RESET;
    s->stat  = TWI_STAT_RESET;
    s->ccr   = TWI_CCR_RESET;
    s->srst  = TWI_SRST_RESET;
    s->efr   = TWI_EFR_RESET;
    s->lcr   = TWI_LCR_RESET;
}

static inline void allwinner_i2c_raise_interrupt(AWI2CState *s)
{
    /*
     * Raise an interrupt if the device is not reset and it is configured
     * to generate some interrupts.
     */
    if (!allwinner_i2c_is_reset(s) && allwinner_i2c_bus_is_enabled(s)) {
        if (STAT_TO_STA(s->stat) != STAT_IDLE) {
            s->cntr |= TWI_CNTR_INT_FLAG;
            if (allwinner_i2c_interrupt_is_enabled(s)) {
                qemu_irq_raise(s->irq);
            }
        }
    }
}

static uint64_t allwinner_i2c_read(void *opaque, hwaddr offset,
                                   unsigned size)
{
    uint16_t value;
    AWI2CState *s = AW_I2C(opaque);

    switch (offset) {
    case TWI_ADDR_REG:
        value = s->addr;
        break;
    case TWI_XADDR_REG:
        value = s->xaddr;
        break;
    case TWI_DATA_REG:
        if ((STAT_TO_STA(s->stat) == STAT_M_ADDR_RD_ACK) ||
            (STAT_TO_STA(s->stat) == STAT_M_DATA_RX_ACK) ||
            (STAT_TO_STA(s->stat) == STAT_M_DATA_RX_NACK)) {
            /* Get the next byte */
            s->data = i2c_recv(s->bus);

            if (s->cntr & TWI_CNTR_A_ACK) {
                s->stat = STAT_FROM_STA(STAT_M_DATA_RX_ACK);
            } else {
                s->stat = STAT_FROM_STA(STAT_M_DATA_RX_NACK);
            }
            allwinner_i2c_raise_interrupt(s);
        }
        value = s->data;
        break;
    case TWI_CNTR_REG:
        value = s->cntr;
        break;
    case TWI_STAT_REG:
        value = s->stat;
        /*
         * If polling when reading then change state to indicate data
         * is available
         */
        if (STAT_TO_STA(s->stat) == STAT_M_ADDR_RD_ACK) {
            if (s->cntr & TWI_CNTR_A_ACK) {
                s->stat = STAT_FROM_STA(STAT_M_DATA_RX_ACK);
            } else {
                s->stat = STAT_FROM_STA(STAT_M_DATA_RX_NACK);
            }
            allwinner_i2c_raise_interrupt(s);
        }
        break;
    case TWI_CCR_REG:
        value = s->ccr;
        break;
    case TWI_SRST_REG:
        value = s->srst;
        break;
    case TWI_EFR_REG:
        value = s->efr;
        break;
    case TWI_LCR_REG:
        value = s->lcr;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad address at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_AW_I2C, __func__, offset);
        value = 0;
        break;
    }

    trace_allwinner_i2c_read(allwinner_i2c_get_regname(offset), offset, value);

    return (uint64_t)value;
}

static void allwinner_i2c_write(void *opaque, hwaddr offset,
                                uint64_t value, unsigned size)
{
    AWI2CState *s = AW_I2C(opaque);

    value &= 0xff;

    trace_allwinner_i2c_write(allwinner_i2c_get_regname(offset), offset, value);

    switch (offset) {
    case TWI_ADDR_REG:
        s->addr = (uint8_t)value;
        break;
    case TWI_XADDR_REG:
        s->xaddr = (uint8_t)value;
        break;
    case TWI_DATA_REG:
        /* If the device is in reset or not enabled, nothing to do */
        if (allwinner_i2c_is_reset(s) || (!allwinner_i2c_bus_is_enabled(s))) {
            break;
        }

        s->data = value & TWI_DATA_MASK;

        switch (STAT_TO_STA(s->stat)) {
        case STAT_M_STA_TX:
        case STAT_M_RSTA_TX:
            /* Send address */
            if (i2c_start_transfer(s->bus, extract32(s->data, 1, 7),
                                extract32(s->data, 0, 1))) {
                /* If non zero is returned, the address is not valid */
                s->stat = STAT_FROM_STA(STAT_M_ADDR_WR_NACK);
            } else {
                /* Determine if read of write */
                if (extract32(s->data, 0, 1)) {
                    s->stat = STAT_FROM_STA(STAT_M_ADDR_RD_ACK);
                } else {
                    s->stat = STAT_FROM_STA(STAT_M_ADDR_WR_ACK);
                }
                allwinner_i2c_raise_interrupt(s);
            }
            break;
        case STAT_M_ADDR_WR_ACK:
        case STAT_M_DATA_TX_ACK:
            if (i2c_send(s->bus, s->data)) {
                /* If the target return non zero then end the transfer */
                s->stat = STAT_FROM_STA(STAT_M_DATA_TX_NACK);
                i2c_end_transfer(s->bus);
            } else {
                s->stat = STAT_FROM_STA(STAT_M_DATA_TX_ACK);
                allwinner_i2c_raise_interrupt(s);
            }
            break;
        default:
            break;
        }
        break;
    case TWI_CNTR_REG:
        if (!allwinner_i2c_is_reset(s)) {
            /* Do something only if not in software reset */
            s->cntr = value & TWI_CNTR_MASK;

            /* Check if start condition should be sent */
            if (s->cntr & TWI_CNTR_M_STA) {
                /* Update status */
                if (STAT_TO_STA(s->stat) == STAT_IDLE) {
                    /* Send start condition */
                    s->stat = STAT_FROM_STA(STAT_M_STA_TX);
                } else {
                    /* Send repeated start condition */
                    s->stat = STAT_FROM_STA(STAT_M_RSTA_TX);
                }
                /* Clear start condition */
                s->cntr &= ~TWI_CNTR_M_STA;
            }
            if (s->cntr & TWI_CNTR_M_STP) {
                /* Update status */
                i2c_end_transfer(s->bus);
                s->stat = STAT_FROM_STA(STAT_IDLE);
                s->cntr &= ~TWI_CNTR_M_STP;
            }

            if (!s->irq_clear_inverted && !(s->cntr & TWI_CNTR_INT_FLAG)) {
                /* Write 0 to clear this flag */
                qemu_irq_lower(s->irq);
            } else if (s->irq_clear_inverted && (s->cntr & TWI_CNTR_INT_FLAG)) {
                /* Write 1 to clear this flag */
                s->cntr &= ~TWI_CNTR_INT_FLAG;
                qemu_irq_lower(s->irq);
            }

            if ((s->cntr & TWI_CNTR_A_ACK) == 0) {
                if (STAT_TO_STA(s->stat) == STAT_M_DATA_RX_ACK) {
                    s->stat = STAT_FROM_STA(STAT_M_DATA_RX_NACK);
                }
            } else {
                if (STAT_TO_STA(s->stat) == STAT_M_DATA_RX_NACK) {
                    s->stat = STAT_FROM_STA(STAT_M_DATA_RX_ACK);
                }
            }
            allwinner_i2c_raise_interrupt(s);

        }
        break;
    case TWI_CCR_REG:
        s->ccr = value & TWI_CCR_MASK;
        break;
    case TWI_SRST_REG:
        if (((value & TWI_SRST_MASK) == 0) && (s->srst & TWI_SRST_MASK)) {
            /* Perform reset */
            allwinner_i2c_reset_hold(OBJECT(s));
        }
        s->srst = value & TWI_SRST_MASK;
        break;
    case TWI_EFR_REG:
        s->efr = value & TWI_EFR_MASK;
        break;
    case TWI_LCR_REG:
        s->lcr = value & TWI_LCR_MASK;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad address at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_AW_I2C, __func__, offset);
        break;
    }
}

static const MemoryRegionOps allwinner_i2c_ops = {
    .read = allwinner_i2c_read,
    .write = allwinner_i2c_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription allwinner_i2c_vmstate = {
    .name = TYPE_AW_I2C,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(addr, AWI2CState),
        VMSTATE_UINT8(xaddr, AWI2CState),
        VMSTATE_UINT8(data, AWI2CState),
        VMSTATE_UINT8(cntr, AWI2CState),
        VMSTATE_UINT8(ccr, AWI2CState),
        VMSTATE_UINT8(srst, AWI2CState),
        VMSTATE_UINT8(efr, AWI2CState),
        VMSTATE_UINT8(lcr, AWI2CState),
        VMSTATE_END_OF_LIST()
    }
};

static void allwinner_i2c_realize(DeviceState *dev, Error **errp)
{
    AWI2CState *s = AW_I2C(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &allwinner_i2c_ops, s,
                          TYPE_AW_I2C, AW_I2C_MEM_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);
    s->bus = i2c_init_bus(dev, "i2c");
}

static void allwinner_i2c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = allwinner_i2c_reset_hold;
    dc->vmsd = &allwinner_i2c_vmstate;
    dc->realize = allwinner_i2c_realize;
    dc->desc = "Allwinner I2C Controller";
}

static const TypeInfo allwinner_i2c_type_info = {
    .name = TYPE_AW_I2C,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AWI2CState),
    .class_init = allwinner_i2c_class_init,
};

static void allwinner_i2c_sun6i_init(Object *obj)
{
    AWI2CState *s = AW_I2C(obj);

    s->irq_clear_inverted = true;
}

static const TypeInfo allwinner_i2c_sun6i_type_info = {
    .name = TYPE_AW_I2C_SUN6I,
    .parent = TYPE_AW_I2C,
    .instance_init = allwinner_i2c_sun6i_init,
};

static void allwinner_i2c_register_types(void)
{
    type_register_static(&allwinner_i2c_type_info);
    type_register_static(&allwinner_i2c_sun6i_type_info);
}

type_init(allwinner_i2c_register_types)
