/*
 * QEMU IDE Emulation: ISA Bus support.
 *
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2006 Openedhand Ltd.
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
#include "hw/isa/isa.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "sysemu/dma.h"

#include "hw/ide/internal.h"
#include "qom/object.h"

/***********************************************************/
/* ISA IDE definitions */

#define TYPE_ISA_IDE "isa-ide"
OBJECT_DECLARE_SIMPLE_TYPE(ISAIDEState, ISA_IDE)

struct ISAIDEState {
    ISADevice parent_obj;

    IDEBus    bus;
    uint32_t  iobase;
    uint32_t  iobase2;
    uint32_t  isairq;
    qemu_irq  irq;
};

static void isa_ide_reset(DeviceState *d)
{
    ISAIDEState *s = ISA_IDE(d);

    ide_bus_reset(&s->bus);
}

static const VMStateDescription vmstate_ide_isa = {
    .name = "isa-ide",
    .version_id = 3,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_IDE_BUS(bus, ISAIDEState),
        VMSTATE_IDE_DRIVES(bus.ifs, ISAIDEState),
        VMSTATE_END_OF_LIST()
    }
};

static void isa_ide_realizefn(DeviceState *dev, Error **errp)
{
    ISADevice *isadev = ISA_DEVICE(dev);
    ISAIDEState *s = ISA_IDE(dev);

    ide_bus_new(&s->bus, sizeof(s->bus), dev, 0, 2);
    ide_init_ioport(&s->bus, isadev, s->iobase, s->iobase2);
    isa_init_irq(isadev, &s->irq, s->isairq);
    ide_init2(&s->bus, s->irq);
    vmstate_register(VMSTATE_IF(dev), 0, &vmstate_ide_isa, s);
    ide_register_restart_cb(&s->bus);
}

ISADevice *isa_ide_init(ISABus *bus, int iobase, int iobase2, int isairq,
                        DriveInfo *hd0, DriveInfo *hd1)
{
    DeviceState *dev;
    ISADevice *isadev;
    ISAIDEState *s;

    isadev = isa_new(TYPE_ISA_IDE);
    dev = DEVICE(isadev);
    qdev_prop_set_uint32(dev, "iobase",  iobase);
    qdev_prop_set_uint32(dev, "iobase2", iobase2);
    qdev_prop_set_uint32(dev, "irq",     isairq);
    isa_realize_and_unref(isadev, bus, &error_fatal);

    s = ISA_IDE(dev);
    if (hd0) {
        ide_create_drive(&s->bus, 0, hd0);
    }
    if (hd1) {
        ide_create_drive(&s->bus, 1, hd1);
    }
    return isadev;
}

static Property isa_ide_properties[] = {
    DEFINE_PROP_UINT32("iobase",  ISAIDEState, iobase,  0x1f0),
    DEFINE_PROP_UINT32("iobase2", ISAIDEState, iobase2, 0x3f6),
    DEFINE_PROP_UINT32("irq",    ISAIDEState, isairq,  14),
    DEFINE_PROP_END_OF_LIST(),
};

static void isa_ide_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = isa_ide_realizefn;
    dc->fw_name = "ide";
    dc->reset = isa_ide_reset;
    device_class_set_props(dc, isa_ide_properties);
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo isa_ide_info = {
    .name          = TYPE_ISA_IDE,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(ISAIDEState),
    .class_init    = isa_ide_class_initfn,
};

static void isa_ide_register_types(void)
{
    type_register_static(&isa_ide_info);
}

type_init(isa_ide_register_types)
