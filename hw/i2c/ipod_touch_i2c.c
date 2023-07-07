/*
 *  IPod Touch I2C Bus Serial Interface Emulation
 *
 *  Copyright (C) 2012 Samsung Electronics Co Ltd.
 *    Maksim Kozlov, <m.kozlov@samsung.com>
 *    Igor Mitsyanko, <i.mitsyanko@samsung.com>
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

#include "hw/i2c/ipod_touch_i2c.h"

static void s5l8900_i2c_update(IPodTouchI2CState *s)
{
    uint16_t level;
    level = (s->status & S5L8900_IICSTAT_START) &&
            (s->control & S5L8900_IICCON_IRQEN);

    if (s->control & S5L8900_IICCON_IRQPEND)
        level = 0;

    qemu_irq_raise(s->irq);
}

static int s5l8900_i2c_receive(IPodTouchI2CState *s)
{
    int r;
    r = i2c_recv(s->bus);
    s5l8900_i2c_update(s);
    return r;
}

static int s5l8900_i2c_send(IPodTouchI2CState *s, uint8_t data)
{
    if (!(s->status & S5L8900_IICSTAT_LASTBIT)) {
        s->status |= S5L8900_IICCON_ACKEN;
        s->data = data;
        s->iicreg20 |= 0x100;
        i2c_send(s->bus, s->data);
    }
    s5l8900_i2c_update(s);
    return 1;
}

/* I2C read function */
static uint64_t ipod_touch_i2c_read(void *opaque, hwaddr offset, unsigned size)
{
    IPodTouchI2CState *s = (IPodTouchI2CState *)opaque;

    //fprintf(stderr, "s5l8900_i2c_read(): offset = 0x%08x\n", offset);

    switch (offset) {
    case I2CCON:
        return s->control;
    case I2CSTAT:
        return s->status;
    case I2CADD:
        return s->address;
    case I2CDS:
        s->iicreg20 |= 0x100;
        s->data = s5l8900_i2c_receive(s);
        return s->data;
    case I2CLC:
        return s->line_ctrl;
    case IICREG20:
        {
            // clear the flags
            uint32_t tmp_reg20 = s->iicreg20;
            s->iicreg20 &= ~0x100;
            s->iicreg20 &= ~0x2000;
            return tmp_reg20; 
        }
    default:
        break;
        //hw_error("s5l8900.i2c: bad read offset 0x" TARGET_FMT_plx "\n", offset);
        //fprintf(stderr, "%s: bad read offset 0x%08x\n", __func__, offset);
    }
    return 0;
}

/* I2C write function */
static void ipod_touch_i2c_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    IPodTouchI2CState *s = (IPodTouchI2CState *)opaque;
    int mode;

    //printf("s5l8900_i2c_write (base %d): offset = 0x%08x, val = 0x%08x\n", s->base, offset, value);

    qemu_irq_lower(s->irq);

    switch (offset) {
    case I2CCON:
        if(value & ~(S5L8900_IICCON_ACKEN)) {
            s->iicreg20 |= 0x100;
        }
        if((value & 0x10) && (s->status == 0x90))  {
            s->iicreg20 |= 0x2000;
        }
        s->control = value & 0xff;

        qemu_irq_raise(s->irq);
        break;

    case I2CSTAT:
        /* We have to make sure we don't miss an end transfer */
        if((!s->active) && ((s->status >> 6) != ((value >> 6)))) {
            s->status = value & 0xff;
        /* If they toggle the tx bit then we have to force an end transfer before mode update */
        } else if((s->active) && ((s->status >> 6) != ((value >> 6)))) {
                    i2c_end_transfer(s->bus);
                    s->active=0;
                    s->status = value & 0xff;
                    s->status |= S5L8900_IICSTAT_TXRXEN;
                    break;
        }
        mode = (s->status >> 6) & 0x3;
        if (value & S5L8900_IICSTAT_TXRXEN) {
            /* IIC-bus data output enable/disable bit */
            switch(mode) {
            case SR_MODE:
                s->data = s5l8900_i2c_receive(s);
                break;
            case ST_MODE:
                s->data = s5l8900_i2c_receive(s);
                break;
            case MR_MODE:
                if (value & S5L8900_IICSTAT_START) {
                    /* START condition */
                    s->status &= ~S5L8900_IICSTAT_LASTBIT;

                    s->iicreg20 |= 0x100;
                    s->active = 1;
                    i2c_start_transfer(s->bus, s->data >> 1, 1);
                } else {
                    i2c_end_transfer(s->bus);
                    s->active = 0;
                    s->status |= S5L8900_IICSTAT_TXRXEN;
                }
                break;
            case MT_MODE:
                if (value & S5L8900_IICSTAT_START) {
                    /* START condition */
                    s->status &= ~S5L8900_IICSTAT_LASTBIT;
                        
                    s->iicreg20 |= 0x100;
                    s->active = 1;
                    i2c_start_transfer(s->bus, s->data >> 1, 0);
                } else {
                    i2c_end_transfer(s->bus);
                    s->active = 0;
                    s->status |= S5L8900_IICSTAT_TXRXEN;
                }
                break;
            default:
                break;
            }
        }
        s5l8900_i2c_update(s);
        break;

    case I2CADD:
        s->address = value & 0xff;
        break;

    case I2CDS:
        s5l8900_i2c_send(s, value & 0xff);
        break;

    case I2CLC:
        s->line_ctrl = value & 0xff;
        break;

    case IICREG20:
        //s->iicreg20 &= ~value;
        break;
    default:
        break;
        //hw_error("s5l8900.i2c: bad write offset 0x" TARGET_FMT_plx "\n", offset);
        //fprintf(stderr, "%s: bad write offset 0x%08x\n", __func__, offset);
    }
}

static const MemoryRegionOps ipod_touch_i2c_ops = {
    .read = ipod_touch_i2c_read,
    .write = ipod_touch_i2c_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int i2c_index = 0;

static void ipod_touch_i2c_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    IPodTouchI2CState *s = IPOD_TOUCH_I2C(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &ipod_touch_i2c_ops, s, TYPE_IPOD_TOUCH_I2C, 0x100);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    char bus_name[5];
    sprintf(bus_name, "i2c%d", i2c_index);

    s->bus = i2c_init_bus(dev, bus_name);
    i2c_index += 1;
}

static void ipod_touch_i2c_reset(DeviceState *d)
{
    
}

static void ipod_touch_i2c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->reset = ipod_touch_i2c_reset;
}

static const TypeInfo ipod_touch_i2c_type_info = {
    .name = TYPE_IPOD_TOUCH_I2C,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IPodTouchI2CState),
    .instance_init = ipod_touch_i2c_init,
    .class_init = ipod_touch_i2c_class_init,
};

static void ipod_touch_i2c_register_types(void)
{
    type_register_static(&ipod_touch_i2c_type_info);
}

type_init(ipod_touch_i2c_register_types)
