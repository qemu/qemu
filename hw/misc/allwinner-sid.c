/*
 * Allwinner Security ID emulation
 *
 * Copyright (C) 2019 Niek Linnenbank <nieklinnenbank@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/guest-random.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/misc/allwinner-sid.h"
#include "trace.h"

/* SID register offsets */
enum {
    REG_PRCTL = 0x40,   /* Control */
    REG_RDKEY = 0x60,   /* Read Key */
};

/* SID register flags */
enum {
    REG_PRCTL_WRITE   = 0x0002, /* Unknown write flag */
    REG_PRCTL_OP_LOCK = 0xAC00, /* Lock operation */
};

static uint64_t allwinner_sid_read(void *opaque, hwaddr offset,
                                   unsigned size)
{
    const AwSidState *s = AW_SID(opaque);
    uint64_t val = 0;

    switch (offset) {
    case REG_PRCTL:    /* Control */
        val = s->control;
        break;
    case REG_RDKEY:    /* Read Key */
        val = s->rdkey;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return 0;
    }

    trace_allwinner_sid_read(offset, val, size);

    return val;
}

static void allwinner_sid_write(void *opaque, hwaddr offset,
                                uint64_t val, unsigned size)
{
    AwSidState *s = AW_SID(opaque);

    trace_allwinner_sid_write(offset, val, size);

    switch (offset) {
    case REG_PRCTL:    /* Control */
        s->control = val;

        if ((s->control & REG_PRCTL_OP_LOCK) &&
            (s->control & REG_PRCTL_WRITE)) {
            uint32_t id = s->control >> 16;

            if (id <= sizeof(QemuUUID) - sizeof(s->rdkey)) {
                s->rdkey = ldl_be_p(&s->identifier.data[id]);
            }
        }
        s->control &= ~REG_PRCTL_WRITE;
        break;
    case REG_RDKEY:    /* Read Key */
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        break;
    }
}

static const MemoryRegionOps allwinner_sid_ops = {
    .read = allwinner_sid_read,
    .write = allwinner_sid_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

static void allwinner_sid_reset(DeviceState *dev)
{
    AwSidState *s = AW_SID(dev);

    /* Set default values for registers */
    s->control = 0;
    s->rdkey = 0;
}

static void allwinner_sid_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    AwSidState *s = AW_SID(obj);

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &allwinner_sid_ops, s,
                           TYPE_AW_SID, 1 * KiB);
    sysbus_init_mmio(sbd, &s->iomem);
}

static Property allwinner_sid_properties[] = {
    DEFINE_PROP_UUID_NODEFAULT("identifier", AwSidState, identifier),
    DEFINE_PROP_END_OF_LIST()
};

static const VMStateDescription allwinner_sid_vmstate = {
    .name = "allwinner-sid",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(control, AwSidState),
        VMSTATE_UINT32(rdkey, AwSidState),
        VMSTATE_UINT8_ARRAY_V(identifier.data, AwSidState, sizeof(QemuUUID), 1),
        VMSTATE_END_OF_LIST()
    }
};

static void allwinner_sid_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = allwinner_sid_reset;
    dc->vmsd = &allwinner_sid_vmstate;
    device_class_set_props(dc, allwinner_sid_properties);
}

static const TypeInfo allwinner_sid_info = {
    .name          = TYPE_AW_SID,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = allwinner_sid_init,
    .instance_size = sizeof(AwSidState),
    .class_init    = allwinner_sid_class_init,
};

static void allwinner_sid_register(void)
{
    type_register_static(&allwinner_sid_info);
}

type_init(allwinner_sid_register)
