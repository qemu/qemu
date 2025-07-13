/*
 * MAX78000 Instruction Cache
 *
 * Copyright (c) 2025 Jackson Donaldson <jcksn@duck.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "trace.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "hw/misc/max78000_icc.h"


static uint64_t max78000_icc_read(void *opaque, hwaddr addr,
                                    unsigned int size)
{
    Max78000IccState *s = opaque;
    switch (addr) {
    case ICC_INFO:
        return s->info;

    case ICC_SZ:
        return s->sz;

    case ICC_CTRL:
        return s->ctrl;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return 0;

    }
}

static void max78000_icc_write(void *opaque, hwaddr addr,
                    uint64_t val64, unsigned int size)
{
    Max78000IccState *s = opaque;

    switch (addr) {
    case ICC_CTRL:
        s->ctrl = 0x10000 | (val64 & 1);
        break;

    case ICC_INVALIDATE:
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
    }
}

static const MemoryRegionOps max78000_icc_ops = {
    .read = max78000_icc_read,
    .write = max78000_icc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static const VMStateDescription max78000_icc_vmstate = {
    .name = TYPE_MAX78000_ICC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(info, Max78000IccState),
        VMSTATE_UINT32(sz, Max78000IccState),
        VMSTATE_UINT32(ctrl, Max78000IccState),
        VMSTATE_END_OF_LIST()
    }
};

static void max78000_icc_reset_hold(Object *obj, ResetType type)
{
    Max78000IccState *s = MAX78000_ICC(obj);
    s->info = 0;
    s->sz = 0x10000010;
    s->ctrl = 0x10000;
}

static void max78000_icc_init(Object *obj)
{
    Max78000IccState *s = MAX78000_ICC(obj);

    memory_region_init_io(&s->mmio, obj, &max78000_icc_ops, s,
                        TYPE_MAX78000_ICC, 0x800);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void max78000_icc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = max78000_icc_reset_hold;
    dc->vmsd = &max78000_icc_vmstate;
}

static const TypeInfo max78000_icc_info = {
    .name          = TYPE_MAX78000_ICC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Max78000IccState),
    .instance_init = max78000_icc_init,
    .class_init    = max78000_icc_class_init,
};

static void max78000_icc_register_types(void)
{
    type_register_static(&max78000_icc_info);
}

type_init(max78000_icc_register_types)
