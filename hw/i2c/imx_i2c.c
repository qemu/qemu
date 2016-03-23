/*
 *  i.MX I2C Bus Serial Interface Emulation
 *
 *  Copyright (C) 2013 Jean-Christophe Dubois. <jcd@tribudubois.net>
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
 */

#include "qemu/osdep.h"
#include "hw/i2c/imx_i2c.h"
#include "hw/i2c/i2c.h"

#ifndef DEBUG_IMX_I2C
#define DEBUG_IMX_I2C 0
#endif

#define DPRINTF(fmt, args...) \
    do { \
        if (DEBUG_IMX_I2C) { \
            fprintf(stderr, "[%s]%s: " fmt , TYPE_IMX_I2C, \
                                             __func__, ##args); \
        } \
    } while (0)

static const char *imx_i2c_get_regname(unsigned offset)
{
    switch (offset) {
    case IADR_ADDR:
        return "IADR";
    case IFDR_ADDR:
        return "IFDR";
    case I2CR_ADDR:
        return "I2CR";
    case I2SR_ADDR:
        return "I2SR";
    case I2DR_ADDR:
        return "I2DR";
    default:
        return "[?]";
    }
}

static inline bool imx_i2c_is_enabled(IMXI2CState *s)
{
    return s->i2cr & I2CR_IEN;
}

static inline bool imx_i2c_interrupt_is_enabled(IMXI2CState *s)
{
    return s->i2cr & I2CR_IIEN;
}

static inline bool imx_i2c_is_master(IMXI2CState *s)
{
    return s->i2cr & I2CR_MSTA;
}

static void imx_i2c_reset(DeviceState *dev)
{
    IMXI2CState *s = IMX_I2C(dev);

    if (s->address != ADDR_RESET) {
        i2c_end_transfer(s->bus);
    }

    s->address    = ADDR_RESET;
    s->iadr       = IADR_RESET;
    s->ifdr       = IFDR_RESET;
    s->i2cr       = I2CR_RESET;
    s->i2sr       = I2SR_RESET;
    s->i2dr_read  = I2DR_RESET;
    s->i2dr_write = I2DR_RESET;
}

static inline void imx_i2c_raise_interrupt(IMXI2CState *s)
{
    /*
     * raise an interrupt if the device is enabled and it is configured
     * to generate some interrupts.
     */
    if (imx_i2c_is_enabled(s) && imx_i2c_interrupt_is_enabled(s)) {
        s->i2sr |= I2SR_IIF;
        qemu_irq_raise(s->irq);
    }
}

static uint64_t imx_i2c_read(void *opaque, hwaddr offset,
                             unsigned size)
{
    uint16_t value;
    IMXI2CState *s = IMX_I2C(opaque);

    switch (offset) {
    case IADR_ADDR:
        value = s->iadr;
        break;
    case IFDR_ADDR:
        value = s->ifdr;
        break;
    case I2CR_ADDR:
        value = s->i2cr;
        break;
    case I2SR_ADDR:
        value = s->i2sr;
        break;
    case I2DR_ADDR:
        value = s->i2dr_read;

        if (imx_i2c_is_master(s)) {
            int ret = 0xff;

            if (s->address == ADDR_RESET) {
                /* something is wrong as the address is not set */
                qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Trying to read "
                              "without specifying the slave address\n",
                              TYPE_IMX_I2C, __func__);
            } else if (s->i2cr & I2CR_MTX) {
                qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Trying to read "
                              "but MTX is set\n", TYPE_IMX_I2C, __func__);
            } else {
                /* get the next byte */
                ret = i2c_recv(s->bus);

                if (ret >= 0) {
                    imx_i2c_raise_interrupt(s);
                } else {
                    qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: read failed "
                                  "for device 0x%02x\n", TYPE_IMX_I2C,
                                  __func__, s->address);
                    ret = 0xff;
                }
            }

            s->i2dr_read = ret;
        } else {
            qemu_log_mask(LOG_UNIMP, "[%s]%s: slave mode not implemented\n",
                          TYPE_IMX_I2C, __func__);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad address at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX_I2C, __func__, offset);
        value = 0;
        break;
    }

    DPRINTF("read %s [0x%" HWADDR_PRIx "] -> 0x%02x\n",
            imx_i2c_get_regname(offset), offset, value);

    return (uint64_t)value;
}

static void imx_i2c_write(void *opaque, hwaddr offset,
                          uint64_t value, unsigned size)
{
    IMXI2CState *s = IMX_I2C(opaque);

    DPRINTF("write %s [0x%" HWADDR_PRIx "] <- 0x%02x\n",
            imx_i2c_get_regname(offset), offset, (int)value);

    value &= 0xff;

    switch (offset) {
    case IADR_ADDR:
        s->iadr = value & IADR_MASK;
        /* i2c_set_slave_address(s->bus, (uint8_t)s->iadr); */
        break;
    case IFDR_ADDR:
        s->ifdr = value & IFDR_MASK;
        break;
    case I2CR_ADDR:
        if (imx_i2c_is_enabled(s) && ((value & I2CR_IEN) == 0)) {
            /* This is a soft reset. IADR is preserved during soft resets */
            uint16_t iadr = s->iadr;
            imx_i2c_reset(DEVICE(s));
            s->iadr = iadr;
        } else { /* normal write */
            s->i2cr = value & I2CR_MASK;

            if (imx_i2c_is_master(s)) {
                /* set the bus to busy */
                s->i2sr |= I2SR_IBB;
            } else { /* slave mode */
                /* bus is not busy anymore */
                s->i2sr &= ~I2SR_IBB;

                /*
                 * if we unset the master mode then it ends the ongoing
                 * transfer if any
                 */
                if (s->address != ADDR_RESET) {
                    i2c_end_transfer(s->bus);
                    s->address = ADDR_RESET;
                }
            }

            if (s->i2cr & I2CR_RSTA) { /* Restart */
                /* if this is a restart then it ends the ongoing transfer */
                if (s->address != ADDR_RESET) {
                    i2c_end_transfer(s->bus);
                    s->address = ADDR_RESET;
                    s->i2cr &= ~I2CR_RSTA;
                }
            }
        }
        break;
    case I2SR_ADDR:
        /*
         * if the user writes 0 to IIF then lower the interrupt and
         * reset the bit
         */
        if ((s->i2sr & I2SR_IIF) && !(value & I2SR_IIF)) {
            s->i2sr &= ~I2SR_IIF;
            qemu_irq_lower(s->irq);
        }

        /*
         * if the user writes 0 to IAL, reset the bit
         */
        if ((s->i2sr & I2SR_IAL) && !(value & I2SR_IAL)) {
            s->i2sr &= ~I2SR_IAL;
        }

        break;
    case I2DR_ADDR:
        /* if the device is not enabled, nothing to do */
        if (!imx_i2c_is_enabled(s)) {
            break;
        }

        s->i2dr_write = value & I2DR_MASK;

        if (imx_i2c_is_master(s)) {
            /* If this is the first write cycle then it is the slave addr */
            if (s->address == ADDR_RESET) {
                if (i2c_start_transfer(s->bus, extract32(s->i2dr_write, 1, 7),
                                       extract32(s->i2dr_write, 0, 1))) {
                    /* if non zero is returned, the address is not valid */
                    s->i2sr |= I2SR_RXAK;
                } else {
                    s->address = s->i2dr_write;
                    s->i2sr &= ~I2SR_RXAK;
                    imx_i2c_raise_interrupt(s);
                }
            } else { /* This is a normal data write */
                if (i2c_send(s->bus, s->i2dr_write)) {
                    /* if the target return non zero then end the transfer */
                    s->i2sr |= I2SR_RXAK;
                    s->address = ADDR_RESET;
                    i2c_end_transfer(s->bus);
                } else {
                    s->i2sr &= ~I2SR_RXAK;
                    imx_i2c_raise_interrupt(s);
                }
            }
        } else {
            qemu_log_mask(LOG_UNIMP, "[%s]%s: slave mode not implemented\n",
                          TYPE_IMX_I2C, __func__);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad address at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX_I2C, __func__, offset);
        break;
    }
}

static const MemoryRegionOps imx_i2c_ops = {
    .read = imx_i2c_read,
    .write = imx_i2c_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 2,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription imx_i2c_vmstate = {
    .name = TYPE_IMX_I2C,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(address, IMXI2CState),
        VMSTATE_UINT16(iadr, IMXI2CState),
        VMSTATE_UINT16(ifdr, IMXI2CState),
        VMSTATE_UINT16(i2cr, IMXI2CState),
        VMSTATE_UINT16(i2sr, IMXI2CState),
        VMSTATE_UINT16(i2dr_read, IMXI2CState),
        VMSTATE_UINT16(i2dr_write, IMXI2CState),
        VMSTATE_END_OF_LIST()
    }
};

static void imx_i2c_realize(DeviceState *dev, Error **errp)
{
    IMXI2CState *s = IMX_I2C(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &imx_i2c_ops, s, TYPE_IMX_I2C,
                          IMX_I2C_MEM_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);
    s->bus = i2c_init_bus(DEVICE(dev), "i2c");
}

static void imx_i2c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &imx_i2c_vmstate;
    dc->reset = imx_i2c_reset;
    dc->realize = imx_i2c_realize;
    dc->desc = "i.MX I2C Controller";
}

static const TypeInfo imx_i2c_type_info = {
    .name = TYPE_IMX_I2C,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IMXI2CState),
    .class_init = imx_i2c_class_init,
};

static void imx_i2c_register_types(void)
{
    type_register_static(&imx_i2c_type_info);
}

type_init(imx_i2c_register_types)
