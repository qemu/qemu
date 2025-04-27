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
#include "qemu/module.h"
#include "system/runstate.h"

#include "hw/nvram/fw_cfg.h"
#include "hw/qdev-properties.h"
#include "hw/misc/pvpanic.h"
#include "qom/object.h"
#include "hw/isa/isa.h"
#include "hw/acpi/acpi_aml_interface.h"

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

static void build_pvpanic_isa_aml(AcpiDevAmlIf *adev, Aml *scope)
{
    Aml *crs, *field, *method;
    PVPanicISAState *s = PVPANIC_ISA_DEVICE(adev);
    Aml *dev = aml_device("PEVT");

    aml_append(dev, aml_name_decl("_HID", aml_string("QEMU0001")));

    crs = aml_resource_template();
    aml_append(crs,
        aml_io(AML_DECODE16, s->ioport, s->ioport, 1, 1)
    );
    aml_append(dev, aml_name_decl("_CRS", crs));

    aml_append(dev, aml_operation_region("PEOR", AML_SYSTEM_IO,
                                          aml_int(s->ioport), 1));
    field = aml_field("PEOR", AML_BYTE_ACC, AML_NOLOCK, AML_PRESERVE);
    aml_append(field, aml_named_field("PEPT", 8));
    aml_append(dev, field);

    /* device present, functioning, decoding, shown in UI */
    aml_append(dev, aml_name_decl("_STA", aml_int(0xF)));

    method = aml_method("RDPT", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_store(aml_name("PEPT"), aml_local(0)));
    aml_append(method, aml_return(aml_local(0)));
    aml_append(dev, method);

    method = aml_method("WRPT", 1, AML_NOTSERIALIZED);
    aml_append(method, aml_store(aml_arg(0), aml_name("PEPT")));
    aml_append(dev, method);

    aml_append(scope, dev);
}

static const Property pvpanic_isa_properties[] = {
    DEFINE_PROP_UINT16(PVPANIC_IOPORT_PROP, PVPanicISAState, ioport, 0x505),
    DEFINE_PROP_UINT8("events", PVPanicISAState, pvpanic.events,
                      PVPANIC_EVENTS),
};

static void pvpanic_isa_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AcpiDevAmlIfClass *adevc = ACPI_DEV_AML_IF_CLASS(klass);

    dc->realize = pvpanic_isa_realizefn;
    device_class_set_props(dc, pvpanic_isa_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    adevc->build_dev_aml = build_pvpanic_isa_aml;
}

static const TypeInfo pvpanic_isa_info = {
    .name          = TYPE_PVPANIC_ISA_DEVICE,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(PVPanicISAState),
    .instance_init = pvpanic_isa_initfn,
    .class_init    = pvpanic_isa_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { TYPE_ACPI_DEV_AML_IF },
        { },
    },
};

static void pvpanic_register_types(void)
{
    type_register_static(&pvpanic_isa_info);
}

type_init(pvpanic_register_types)
