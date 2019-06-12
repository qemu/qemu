/*
 * QEMU simulated pvpanic device.
 *
 * Copyright Fujitsu, Corp. 2013
 *
 * Authors:
 *     Wen Congyang <wency@cn.fujitsu.com>
 *     Hu Tao <hutao@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "sysemu/sysemu.h"
#include "qemu/log.h"
#include "qemu/module.h"

#include "hw/nvram/fw_cfg.h"
#include "hw/misc/pvpanic.h"

/* The bit of supported pv event */
#define PVPANIC_F_PANICKED      0

/* The pv event value */
#define PVPANIC_PANICKED        (1 << PVPANIC_F_PANICKED)

#define ISA_PVPANIC_DEVICE(obj)    \
    OBJECT_CHECK(PVPanicState, (obj), TYPE_PVPANIC)

static void handle_event(int event)
{
    static bool logged;

    if (event & ~PVPANIC_PANICKED && !logged) {
        qemu_log_mask(LOG_GUEST_ERROR, "pvpanic: unknown event %#x.\n", event);
        logged = true;
    }

    if (event & PVPANIC_PANICKED) {
        qemu_system_guest_panicked(NULL);
        return;
    }
}

#include "hw/isa/isa.h"

typedef struct PVPanicState {
    ISADevice parent_obj;

    MemoryRegion io;
    uint16_t ioport;
} PVPanicState;

/* return supported events on read */
static uint64_t pvpanic_ioport_read(void *opaque, hwaddr addr, unsigned size)
{
    return PVPANIC_PANICKED;
}

static void pvpanic_ioport_write(void *opaque, hwaddr addr, uint64_t val,
                                 unsigned size)
{
    handle_event(val);
}

static const MemoryRegionOps pvpanic_ops = {
    .read = pvpanic_ioport_read,
    .write = pvpanic_ioport_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void pvpanic_isa_initfn(Object *obj)
{
    PVPanicState *s = ISA_PVPANIC_DEVICE(obj);

    memory_region_init_io(&s->io, OBJECT(s), &pvpanic_ops, s, "pvpanic", 1);
}

static void pvpanic_isa_realizefn(DeviceState *dev, Error **errp)
{
    ISADevice *d = ISA_DEVICE(dev);
    PVPanicState *s = ISA_PVPANIC_DEVICE(dev);
    FWCfgState *fw_cfg = fw_cfg_find();
    uint16_t *pvpanic_port;

    if (!fw_cfg) {
        return;
    }

    pvpanic_port = g_malloc(sizeof(*pvpanic_port));
    *pvpanic_port = cpu_to_le16(s->ioport);
    fw_cfg_add_file(fw_cfg, "etc/pvpanic-port", pvpanic_port,
                    sizeof(*pvpanic_port));

    isa_register_ioport(d, &s->io, s->ioport);
}

static Property pvpanic_isa_properties[] = {
    DEFINE_PROP_UINT16(PVPANIC_IOPORT_PROP, PVPanicState, ioport, 0x505),
    DEFINE_PROP_END_OF_LIST(),
};

static void pvpanic_isa_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pvpanic_isa_realizefn;
    dc->props = pvpanic_isa_properties;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static TypeInfo pvpanic_isa_info = {
    .name          = TYPE_PVPANIC,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(PVPanicState),
    .instance_init = pvpanic_isa_initfn,
    .class_init    = pvpanic_isa_class_init,
};

static void pvpanic_register_types(void)
{
    type_register_static(&pvpanic_isa_info);
}

type_init(pvpanic_register_types)
