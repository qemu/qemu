/*
 * QEMU 16550A UART emulation
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 * Copyright (c) 2008 Citrix Systems, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "system/system.h"
#include "hw/acpi/acpi_aml_interface.h"
#include "hw/char/serial.h"
#include "hw/char/serial-isa.h"
#include "hw/isa/isa.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qom/object.h"

OBJECT_DECLARE_SIMPLE_TYPE(ISASerialState, ISA_SERIAL)

struct ISASerialState {
    ISADevice parent_obj;

    uint32_t index;
    uint32_t iobase;
    uint32_t isairq;
    SerialState state;
};

static const int isa_serial_io[MAX_ISA_SERIAL_PORTS] = {
    0x3f8, 0x2f8, 0x3e8, 0x2e8
};
static const int isa_serial_irq[MAX_ISA_SERIAL_PORTS] = {
    4, 3, 4, 3
};

static void serial_isa_realizefn(DeviceState *dev, Error **errp)
{
    static int index;
    ISADevice *isadev = ISA_DEVICE(dev);
    ISASerialState *isa = ISA_SERIAL(dev);
    SerialState *s = &isa->state;

    if (isa->index == -1) {
        isa->index = index;
    }
    if (isa->index >= MAX_ISA_SERIAL_PORTS) {
        error_setg(errp, "Max. supported number of ISA serial ports is %d.",
                   MAX_ISA_SERIAL_PORTS);
        return;
    }
    if (isa->iobase == -1) {
        isa->iobase = isa_serial_io[isa->index];
    }
    if (isa->isairq == -1) {
        isa->isairq = isa_serial_irq[isa->index];
    }
    index++;

    s->irq = isa_get_irq(isadev, isa->isairq);
    qdev_realize(DEVICE(s), NULL, errp);
    qdev_set_legacy_instance_id(dev, isa->iobase, 3);

    memory_region_init_io(&s->io, OBJECT(isa), &serial_io_ops, s, "serial", 8);
    isa_register_ioport(isadev, &s->io, isa->iobase);
}

static void serial_isa_build_aml(AcpiDevAmlIf *adev, Aml *scope)
{
    ISASerialState *isa = ISA_SERIAL(adev);
    Aml *dev;
    Aml *crs;

    crs = aml_resource_template();
    aml_append(crs, aml_io(AML_DECODE16, isa->iobase, isa->iobase, 0x00, 0x08));
    aml_append(crs, aml_irq_no_flags(isa->isairq));

    dev = aml_device("COM%d", isa->index + 1);
    aml_append(dev, aml_name_decl("_HID", aml_eisaid("PNP0501")));
    aml_append(dev, aml_name_decl("_UID", aml_int(isa->index + 1)));
    aml_append(dev, aml_name_decl("_STA", aml_int(0xf)));
    aml_append(dev, aml_name_decl("_CRS", crs));

    aml_append(scope, dev);
}

static const VMStateDescription vmstate_isa_serial = {
    .name = "serial",
    .version_id = 3,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(state, ISASerialState, 0, vmstate_serial, SerialState),
        VMSTATE_END_OF_LIST()
    }
};

static const Property serial_isa_properties[] = {
    DEFINE_PROP_UINT32("index",  ISASerialState, index,   -1),
    DEFINE_PROP_UINT32("iobase",  ISASerialState, iobase,  -1),
    DEFINE_PROP_UINT32("irq",    ISASerialState, isairq,  -1),
};

static void serial_isa_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AcpiDevAmlIfClass *adevc = ACPI_DEV_AML_IF_CLASS(klass);

    dc->realize = serial_isa_realizefn;
    dc->vmsd = &vmstate_isa_serial;
    adevc->build_dev_aml = serial_isa_build_aml;
    device_class_set_props(dc, serial_isa_properties);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static void serial_isa_initfn(Object *o)
{
    ISASerialState *self = ISA_SERIAL(o);

    object_initialize_child(o, "serial", &self->state, TYPE_SERIAL);

    qdev_alias_all_properties(DEVICE(&self->state), o);
}

static const TypeInfo serial_isa_info = {
    .name          = TYPE_ISA_SERIAL,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(ISASerialState),
    .instance_init = serial_isa_initfn,
    .class_init    = serial_isa_class_initfn,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_ACPI_DEV_AML_IF },
        { },
    },
};

static void serial_register_types(void)
{
    type_register_static(&serial_isa_info);
}

type_init(serial_register_types)

static void serial_isa_init(ISABus *bus, int index, Chardev *chr)
{
    DeviceState *dev;
    ISADevice *isadev;

    isadev = isa_new(TYPE_ISA_SERIAL);
    dev = DEVICE(isadev);
    qdev_prop_set_uint32(dev, "index", index);
    qdev_prop_set_chr(dev, "chardev", chr);
    isa_realize_and_unref(isadev, bus, &error_fatal);
}

void serial_hds_isa_init(ISABus *bus, int from, int to)
{
    int i;

    assert(from >= 0);
    assert(to <= MAX_ISA_SERIAL_PORTS);

    for (i = from; i < to; ++i) {
        if (serial_hd(i)) {
            serial_isa_init(bus, i, serial_hd(i));
        }
    }
}

void isa_serial_set_iobase(ISADevice *serial, hwaddr iobase)
{
    ISASerialState *s = ISA_SERIAL(serial);

    serial->ioport_id = iobase;
    s->iobase = iobase;
    memory_region_set_address(&s->state.io, s->iobase);
}

void isa_serial_set_enabled(ISADevice *serial, bool enabled)
{
    memory_region_set_enabled(&ISA_SERIAL(serial)->state.io, enabled);
}
