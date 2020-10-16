/*
 * QEMU M48T59 and M48T08 NVRAM emulation (ISA bus interface)
 *
 * Copyright (c) 2003-2005, 2007 Jocelyn Mayer
 * Copyright (c) 2013 Hervé Poussineau
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
#include "hw/rtc/m48t59.h"
#include "m48t59-internal.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qom/object.h"

#define TYPE_M48TXX_ISA "isa-m48txx"
typedef struct M48txxISADeviceClass M48txxISADeviceClass;
typedef struct M48txxISAState M48txxISAState;
DECLARE_OBJ_CHECKERS(M48txxISAState, M48txxISADeviceClass,
                     M48TXX_ISA, TYPE_M48TXX_ISA)

struct M48txxISAState {
    ISADevice parent_obj;
    M48t59State state;
    uint32_t io_base;
    MemoryRegion io;
};

struct M48txxISADeviceClass {
    ISADeviceClass parent_class;
    M48txxInfo info;
};

static M48txxInfo m48txx_isa_info[] = {
    {
        .bus_name = "isa-m48t59",
        .model = 59,
        .size = 0x2000,
    }
};

static uint32_t m48txx_isa_read(Nvram *obj, uint32_t addr)
{
    M48txxISAState *d = M48TXX_ISA(obj);
    return m48t59_read(&d->state, addr);
}

static void m48txx_isa_write(Nvram *obj, uint32_t addr, uint32_t val)
{
    M48txxISAState *d = M48TXX_ISA(obj);
    m48t59_write(&d->state, addr, val);
}

static void m48txx_isa_toggle_lock(Nvram *obj, int lock)
{
    M48txxISAState *d = M48TXX_ISA(obj);
    m48t59_toggle_lock(&d->state, lock);
}

static Property m48t59_isa_properties[] = {
    DEFINE_PROP_INT32("base-year", M48txxISAState, state.base_year, 0),
    DEFINE_PROP_UINT32("iobase", M48txxISAState, io_base, 0x74),
    DEFINE_PROP_END_OF_LIST(),
};

static void m48t59_reset_isa(DeviceState *d)
{
    M48txxISAState *isa = M48TXX_ISA(d);
    M48t59State *NVRAM = &isa->state;

    m48t59_reset_common(NVRAM);
}

static void m48t59_isa_realize(DeviceState *dev, Error **errp)
{
    M48txxISADeviceClass *u = M48TXX_ISA_GET_CLASS(dev);
    ISADevice *isadev = ISA_DEVICE(dev);
    M48txxISAState *d = M48TXX_ISA(dev);
    M48t59State *s = &d->state;

    s->model = u->info.model;
    s->size = u->info.size;
    isa_init_irq(isadev, &s->IRQ, 8);
    m48t59_realize_common(s, errp);
    memory_region_init_io(&d->io, OBJECT(dev), &m48t59_io_ops, s, "m48t59", 4);
    if (d->io_base != 0) {
        isa_register_ioport(isadev, &d->io, d->io_base);
    }
}

static void m48txx_isa_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    NvramClass *nc = NVRAM_CLASS(klass);

    dc->realize = m48t59_isa_realize;
    dc->reset = m48t59_reset_isa;
    device_class_set_props(dc, m48t59_isa_properties);
    nc->read = m48txx_isa_read;
    nc->write = m48txx_isa_write;
    nc->toggle_lock = m48txx_isa_toggle_lock;
}

static void m48txx_isa_concrete_class_init(ObjectClass *klass, void *data)
{
    M48txxISADeviceClass *u = M48TXX_ISA_CLASS(klass);
    M48txxInfo *info = data;

    u->info = *info;
}

static const TypeInfo m48txx_isa_type_info = {
    .name = TYPE_M48TXX_ISA,
    .parent = TYPE_ISA_DEVICE,
    .instance_size = sizeof(M48txxISAState),
    .abstract = true,
    .class_init = m48txx_isa_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_NVRAM },
        { }
    }
};

static void m48t59_isa_register_types(void)
{
    TypeInfo isa_type_info = {
        .parent = TYPE_M48TXX_ISA,
        .class_size = sizeof(M48txxISADeviceClass),
        .class_init = m48txx_isa_concrete_class_init,
    };
    int i;

    type_register_static(&m48txx_isa_type_info);

    for (i = 0; i < ARRAY_SIZE(m48txx_isa_info); i++) {
        isa_type_info.name = m48txx_isa_info[i].bus_name;
        isa_type_info.class_data = &m48txx_isa_info[i];
        type_register(&isa_type_info);
    }
}

type_init(m48t59_isa_register_types)
