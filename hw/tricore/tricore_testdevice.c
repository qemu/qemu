/*
 *  Copyright (c) 2018-2021 Bastian Koppelmann Paderborn University
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "hw/tricore/tricore_testdevice.h"

static void tricore_testdevice_write(void *opaque, hwaddr offset,
                                      uint64_t value, unsigned size)
{
    exit(value);
}

static uint64_t tricore_testdevice_read(void *opaque, hwaddr offset,
                                         unsigned size)
{
    return 0xdeadbeef;
}

static void tricore_testdevice_reset(DeviceState *dev)
{
}

static const MemoryRegionOps tricore_testdevice_ops = {
    .read = tricore_testdevice_read,
    .write = tricore_testdevice_write,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void tricore_testdevice_init(Object *obj)
{
    TriCoreTestDeviceState *s = TRICORE_TESTDEVICE(obj);
   /* map memory */
    memory_region_init_io(&s->iomem, OBJECT(s), &tricore_testdevice_ops, s,
                          "tricore_testdevice", 0x4);
}

static Property tricore_testdevice_properties[] = {
    DEFINE_PROP_END_OF_LIST()
};

static void tricore_testdevice_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, tricore_testdevice_properties);
    dc->reset = tricore_testdevice_reset;
}

static const TypeInfo tricore_testdevice_info = {
    .name          = TYPE_TRICORE_TESTDEVICE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TriCoreTestDeviceState),
    .instance_init = tricore_testdevice_init,
    .class_init    = tricore_testdevice_class_init,
};

static void tricore_testdevice_register_types(void)
{
    type_register_static(&tricore_testdevice_info);
}

type_init(tricore_testdevice_register_types)
