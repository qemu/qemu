/*
 * ARM Versatile I2C controller
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Copyright (c) 2012 Oskar Andero <oskar.andero@gmail.com>
 *
 * This file is derived from hw/realview.c by Paul Brook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "bitbang_i2c.h"

#define TYPE_VERSATILE_I2C "versatile_i2c"
#define VERSATILE_I2C(obj) \
    OBJECT_CHECK(VersatileI2CState, (obj), TYPE_VERSATILE_I2C)

typedef struct VersatileI2CState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    bitbang_i2c_interface *bitbang;
    int out;
    int in;
} VersatileI2CState;

static uint64_t versatile_i2c_read(void *opaque, hwaddr offset,
                                   unsigned size)
{
    VersatileI2CState *s = (VersatileI2CState *)opaque;

    if (offset == 0) {
        return (s->out & 1) | (s->in << 1);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%x\n", __func__, (int)offset);
        return -1;
    }
}

static void versatile_i2c_write(void *opaque, hwaddr offset,
                                uint64_t value, unsigned size)
{
    VersatileI2CState *s = (VersatileI2CState *)opaque;

    switch (offset) {
    case 0:
        s->out |= value & 3;
        break;
    case 4:
        s->out &= ~value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%x\n", __func__, (int)offset);
    }
    bitbang_i2c_set(s->bitbang, BITBANG_I2C_SCL, (s->out & 1) != 0);
    s->in = bitbang_i2c_set(s->bitbang, BITBANG_I2C_SDA, (s->out & 2) != 0);
}

static const MemoryRegionOps versatile_i2c_ops = {
    .read = versatile_i2c_read,
    .write = versatile_i2c_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int versatile_i2c_init(SysBusDevice *sbd)
{
    DeviceState *dev = DEVICE(sbd);
    VersatileI2CState *s = VERSATILE_I2C(dev);
    I2CBus *bus;

    bus = i2c_init_bus(dev, "i2c");
    s->bitbang = bitbang_i2c_init(bus);
    memory_region_init_io(&s->iomem, OBJECT(s), &versatile_i2c_ops, s,
                          "versatile_i2c", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    return 0;
}

static void versatile_i2c_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = versatile_i2c_init;
}

static const TypeInfo versatile_i2c_info = {
    .name          = TYPE_VERSATILE_I2C,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(VersatileI2CState),
    .class_init    = versatile_i2c_class_init,
};

static void versatile_i2c_register_types(void)
{
    type_register_static(&versatile_i2c_info);
}

type_init(versatile_i2c_register_types)
