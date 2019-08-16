/*
 * QEMU Intel 82374 emulation (Enhanced DMA controller)
 *
 * Copyright (c) 2010 Hervé Poussineau
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
#include "hw/isa/isa.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "hw/dma/i8257.h"

#define TYPE_I82374 "i82374"
#define I82374(obj) OBJECT_CHECK(I82374State, (obj), TYPE_I82374)

//#define DEBUG_I82374

#ifdef DEBUG_I82374
#define DPRINTF(fmt, ...) \
do { fprintf(stderr, "i82374: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
do {} while (0)
#endif
#define BADF(fmt, ...) \
do { fprintf(stderr, "i82374 ERROR: " fmt , ## __VA_ARGS__); } while (0)

typedef struct I82374State {
    ISADevice parent_obj;

    uint32_t iobase;
    uint8_t commands[8];
    PortioList port_list;
} I82374State;

static const VMStateDescription vmstate_i82374 = {
    .name = "i82374",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(commands, I82374State, 8),
        VMSTATE_END_OF_LIST()
    },
};

static uint32_t i82374_read_isr(void *opaque, uint32_t nport)
{
    uint32_t val = 0;

    BADF("%s: %08x\n", __func__, nport);

    DPRINTF("%s: %08x=%08x\n", __func__, nport, val);
    return val;
}

static void i82374_write_command(void *opaque, uint32_t nport, uint32_t data)
{
    DPRINTF("%s: %08x=%08x\n", __func__, nport, data);

    if (data != 0x42) {
        /* Not Stop S/G command */
        BADF("%s: %08x=%08x\n", __func__, nport, data);
    }
}

static uint32_t i82374_read_status(void *opaque, uint32_t nport)
{
    uint32_t val = 0;

    BADF("%s: %08x\n", __func__, nport);

    DPRINTF("%s: %08x=%08x\n", __func__, nport, val);
    return val;
}

static void i82374_write_descriptor(void *opaque, uint32_t nport, uint32_t data)
{
    DPRINTF("%s: %08x=%08x\n", __func__, nport, data);

    BADF("%s: %08x=%08x\n", __func__, nport, data);
}

static uint32_t i82374_read_descriptor(void *opaque, uint32_t nport)
{
    uint32_t val = 0;

    BADF("%s: %08x\n", __func__, nport);

    DPRINTF("%s: %08x=%08x\n", __func__, nport, val);
    return val;
}

static const MemoryRegionPortio i82374_portio_list[] = {
    { 0x0A, 1, 1, .read = i82374_read_isr, },
    { 0x10, 8, 1, .write = i82374_write_command, },
    { 0x18, 8, 1, .read = i82374_read_status, },
    { 0x20, 0x20, 1,
      .write = i82374_write_descriptor, .read = i82374_read_descriptor, },
    PORTIO_END_OF_LIST(),
};

static void i82374_realize(DeviceState *dev, Error **errp)
{
    I82374State *s = I82374(dev);
    ISABus *isa_bus = isa_bus_from_device(ISA_DEVICE(dev));

    if (isa_get_dma(isa_bus, 0)) {
        error_setg(errp, "DMA already initialized on ISA bus");
        return;
    }
    i8257_dma_init(isa_bus, true);

    portio_list_init(&s->port_list, OBJECT(s), i82374_portio_list, s,
                     "i82374");
    portio_list_add(&s->port_list, isa_address_space_io(&s->parent_obj),
                    s->iobase);

    memset(s->commands, 0, sizeof(s->commands));
}

static Property i82374_properties[] = {
    DEFINE_PROP_UINT32("iobase", I82374State, iobase, 0x400),
    DEFINE_PROP_END_OF_LIST()
};

static void i82374_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    
    dc->realize = i82374_realize;
    dc->vmsd = &vmstate_i82374;
    dc->props = i82374_properties;
}

static const TypeInfo i82374_info = {
    .name  = TYPE_I82374,
    .parent = TYPE_ISA_DEVICE,
    .instance_size  = sizeof(I82374State),
    .class_init = i82374_class_init,
};

static void i82374_register_types(void)
{
    type_register_static(&i82374_info);
}

type_init(i82374_register_types)
