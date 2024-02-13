/*
 * QEMU I/O port 0x92 (System Control Port A, to handle Fast Gate A20)
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "sysemu/runstate.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "hw/isa/isa.h"
#include "hw/i386/pc.h"
#include "trace.h"
#include "qom/object.h"

OBJECT_DECLARE_SIMPLE_TYPE(Port92State, PORT92)

struct Port92State {
    ISADevice parent_obj;

    MemoryRegion io;
    uint8_t outport;
    qemu_irq a20_out;
};

static void port92_write(void *opaque, hwaddr addr, uint64_t val,
                         unsigned size)
{
    Port92State *s = opaque;
    int oldval = s->outport;

    trace_port92_write(val);
    s->outport = val;
    qemu_set_irq(s->a20_out, (val >> 1) & 1);
    if ((val & 1) && !(oldval & 1)) {
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    }
}

static uint64_t port92_read(void *opaque, hwaddr addr,
                            unsigned size)
{
    Port92State *s = opaque;
    uint32_t ret;

    ret = s->outport;
    trace_port92_read(ret);

    return ret;
}

static const VMStateDescription vmstate_port92_isa = {
    .name = "port92",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(outport, Port92State),
        VMSTATE_END_OF_LIST()
    }
};

static void port92_reset(DeviceState *d)
{
    Port92State *s = PORT92(d);

    s->outport &= ~1;
}

static const MemoryRegionOps port92_ops = {
    .read = port92_read,
    .write = port92_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void port92_initfn(Object *obj)
{
    Port92State *s = PORT92(obj);

    memory_region_init_io(&s->io, OBJECT(s), &port92_ops, s, "port92", 1);

    s->outport = 0;

    qdev_init_gpio_out_named(DEVICE(obj), &s->a20_out, PORT92_A20_LINE, 1);
}

static void port92_realizefn(DeviceState *dev, Error **errp)
{
    ISADevice *isadev = ISA_DEVICE(dev);
    Port92State *s = PORT92(dev);

    isa_register_ioport(isadev, &s->io, 0x92);
}

static void port92_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = port92_realizefn;
    dc->reset = port92_reset;
    dc->vmsd = &vmstate_port92_isa;
    /*
     * Reason: unlike ordinary ISA devices, this one needs additional
     * wiring: its A20 output line needs to be wired up with
     * qdev_connect_gpio_out_named().
     */
    dc->user_creatable = false;
}

static const TypeInfo port92_info = {
    .name          = TYPE_PORT92,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(Port92State),
    .instance_init = port92_initfn,
    .class_init    = port92_class_initfn,
};

static void port92_register_types(void)
{
    type_register_static(&port92_info);
}

type_init(port92_register_types)
