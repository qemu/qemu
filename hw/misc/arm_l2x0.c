/*
 * ARM dummy L210, L220, PL310 cache controller.
 *
 * Copyright (c) 2010-2012 Calxeda
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or any later version, as published by the Free Software
 * Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"

/* L2C-310 r3p2 */
#define CACHE_ID 0x410000c8

#define TYPE_ARM_L2X0 "l2x0"
#define ARM_L2X0(obj) OBJECT_CHECK(L2x0State, (obj), TYPE_ARM_L2X0)

typedef struct L2x0State {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t cache_type;
    uint32_t ctrl;
    uint32_t aux_ctrl;
    uint32_t data_ctrl;
    uint32_t tag_ctrl;
    uint32_t filter_start;
    uint32_t filter_end;
} L2x0State;

static const VMStateDescription vmstate_l2x0 = {
    .name = "l2x0",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(ctrl, L2x0State),
        VMSTATE_UINT32(aux_ctrl, L2x0State),
        VMSTATE_UINT32(data_ctrl, L2x0State),
        VMSTATE_UINT32(tag_ctrl, L2x0State),
        VMSTATE_UINT32(filter_start, L2x0State),
        VMSTATE_UINT32(filter_end, L2x0State),
        VMSTATE_END_OF_LIST()
    }
};


static uint64_t l2x0_priv_read(void *opaque, hwaddr offset,
                               unsigned size)
{
    uint32_t cache_data;
    L2x0State *s = (L2x0State *)opaque;
    offset &= 0xfff;
    if (offset >= 0x730 && offset < 0x800) {
        return 0; /* cache ops complete */
    }
    switch (offset) {
    case 0:
        return CACHE_ID;
    case 0x4:
        /* aux_ctrl values affect cache_type values */
        cache_data = (s->aux_ctrl & (7 << 17)) >> 15;
        cache_data |= (s->aux_ctrl & (1 << 16)) >> 16;
        return s->cache_type |= (cache_data << 18) | (cache_data << 6);
    case 0x100:
        return s->ctrl;
    case 0x104:
        return s->aux_ctrl;
    case 0x108:
        return s->tag_ctrl;
    case 0x10C:
        return s->data_ctrl;
    case 0xC00:
        return s->filter_start;
    case 0xC04:
        return s->filter_end;
    case 0xF40:
        return 0;
    case 0xF60:
        return 0;
    case 0xF80:
        return 0;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "l2x0_priv_read: Bad offset %x\n", (int)offset);
        break;
    }
    return 0;
}

static void l2x0_priv_write(void *opaque, hwaddr offset,
                            uint64_t value, unsigned size)
{
    L2x0State *s = (L2x0State *)opaque;
    offset &= 0xfff;
    if (offset >= 0x730 && offset < 0x800) {
        /* ignore */
        return;
    }
    switch (offset) {
    case 0x100:
        s->ctrl = value & 1;
        break;
    case 0x104:
        s->aux_ctrl = value;
        break;
    case 0x108:
        s->tag_ctrl = value;
        break;
    case 0x10C:
        s->data_ctrl = value;
        break;
    case 0xC00:
        s->filter_start = value;
        break;
    case 0xC04:
        s->filter_end = value;
        break;
    case 0xF40:
        return;
    case 0xF60:
        return;
    case 0xF80:
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "l2x0_priv_write: Bad offset %x\n", (int)offset);
        break;
    }
}

static void l2x0_priv_reset(DeviceState *dev)
{
    L2x0State *s = ARM_L2X0(dev);

    s->ctrl = 0;
    s->aux_ctrl = 0x02020000;
    s->tag_ctrl = 0;
    s->data_ctrl = 0;
    s->filter_start = 0;
    s->filter_end = 0;
}

static const MemoryRegionOps l2x0_mem_ops = {
    .read = l2x0_priv_read,
    .write = l2x0_priv_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
 };

static void l2x0_priv_init(Object *obj)
{
    L2x0State *s = ARM_L2X0(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &l2x0_mem_ops, s,
                          "l2x0_cc", 0x1000);
    sysbus_init_mmio(dev, &s->iomem);
}

static Property l2x0_properties[] = {
    DEFINE_PROP_UINT32("cache-type", L2x0State, cache_type, 0x1c100100),
    DEFINE_PROP_END_OF_LIST(),
};

static void l2x0_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_l2x0;
    dc->props = l2x0_properties;
    dc->reset = l2x0_priv_reset;
}

static const TypeInfo l2x0_info = {
    .name = TYPE_ARM_L2X0,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(L2x0State),
    .instance_init = l2x0_priv_init,
    .class_init = l2x0_class_init,
};

static void l2x0_register_types(void)
{
    type_register_static(&l2x0_info);
}

type_init(l2x0_register_types)
