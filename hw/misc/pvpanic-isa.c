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
#include "qemu/log.h"
#include "qemu/module.h"
#include "sysemu/runstate.h"

#include "hw/nvram/fw_cfg.h"
#include "hw/qdev-properties.h"
#include "hw/misc/pvpanic.h"
#include "qom/object.h"
#include "hw/isa/isa.h"

OBJECT_DECLARE_SIMPLE_TYPE(PVPanicISAState, PVPANIC_ISA_DEVICE)

/*
 * PVPanicISAState for ISA device and
 * use ioport.
 */
struct PVPanicISAState {
    ISADevice parent_obj;

    uint16_t ioport;
    PVPanicState pvpanic;
};

static void pvpanic_isa_initfn(Object *obj)
{
    PVPanicISAState *s = PVPANIC_ISA_DEVICE(obj);

    pvpanic_setup_io(&s->pvpanic, DEVICE(s), 1);
}

static void pvpanic_isa_realizefn(DeviceState *dev, Error **errp)
{
    ISADevice *d = ISA_DEVICE(dev);
    PVPanicISAState *s = PVPANIC_ISA_DEVICE(dev);
    PVPanicState *ps = &s->pvpanic;
    FWCfgState *fw_cfg = fw_cfg_find();
    uint16_t *pvpanic_port;

    if (!fw_cfg) {
        return;
    }

    pvpanic_port = g_malloc(sizeof(*pvpanic_port));
    *pvpanic_port = cpu_to_le16(s->ioport);
    fw_cfg_add_file(fw_cfg, "etc/pvpanic-port", pvpanic_port,
                    sizeof(*pvpanic_port));

    isa_register_ioport(d, &ps->mr, s->ioport);
}

static Property pvpanic_isa_properties[] = {
    DEFINE_PROP_UINT16(PVPANIC_IOPORT_PROP, PVPanicISAState, ioport, 0x505),
    DEFINE_PROP_UINT8("events", PVPanicISAState, pvpanic.events, PVPANIC_PANICKED | PVPANIC_CRASHLOADED),
    DEFINE_PROP_END_OF_LIST(),
};

static void pvpanic_isa_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pvpanic_isa_realizefn;
    device_class_set_props(dc, pvpanic_isa_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static TypeInfo pvpanic_isa_info = {
    .name          = TYPE_PVPANIC_ISA_DEVICE,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(PVPanicISAState),
    .instance_init = pvpanic_isa_initfn,
    .class_init    = pvpanic_isa_class_init,
};

static void pvpanic_register_types(void)
{
    type_register_static(&pvpanic_isa_info);
}

type_init(pvpanic_register_types)
