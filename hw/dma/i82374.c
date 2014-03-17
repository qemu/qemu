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

#include "hw/isa/isa.h"

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
    uint8_t commands[8];
    qemu_irq out;
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

static void i82374_realize(I82374State *s, Error **errp)
{
    DMA_init(1, &s->out);
    memset(s->commands, 0, sizeof(s->commands));
}

#define TYPE_I82374 "i82374"
#define I82374(obj) OBJECT_CHECK(ISAi82374State, (obj), TYPE_I82374)

typedef struct ISAi82374State {
    ISADevice parent_obj;

    uint32_t iobase;
    I82374State state;
} ISAi82374State;

static const VMStateDescription vmstate_isa_i82374 = {
    .name = "isa-i82374",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(state, ISAi82374State, 0, vmstate_i82374, I82374State),
        VMSTATE_END_OF_LIST()
    },
};

static const MemoryRegionPortio i82374_portio_list[] = {
    { 0x0A, 1, 1, .read = i82374_read_isr, },
    { 0x10, 8, 1, .write = i82374_write_command, },
    { 0x18, 8, 1, .read = i82374_read_status, },
    { 0x20, 0x20, 1,
      .write = i82374_write_descriptor, .read = i82374_read_descriptor, },
    PORTIO_END_OF_LIST(),
};

static void i82374_isa_realize(DeviceState *dev, Error **errp)
{
    ISAi82374State *isa = I82374(dev);
    I82374State *s = &isa->state;
    PortioList *port_list = g_new(PortioList, 1);

    portio_list_init(port_list, OBJECT(isa), i82374_portio_list, s, "i82374");
    portio_list_add(port_list, isa_address_space_io(&isa->parent_obj),
                    isa->iobase);

    i82374_realize(s, errp);

    qdev_init_gpio_out(dev, &s->out, 1);
}

static Property i82374_properties[] = {
    DEFINE_PROP_UINT32("iobase", ISAi82374State, iobase, 0x400),
    DEFINE_PROP_END_OF_LIST()
};

static void i82374_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    
    dc->realize = i82374_isa_realize;
    dc->vmsd = &vmstate_isa_i82374;
    dc->props = i82374_properties;
}

static const TypeInfo i82374_isa_info = {
    .name  = TYPE_I82374,
    .parent = TYPE_ISA_DEVICE,
    .instance_size  = sizeof(ISAi82374State),
    .class_init = i82374_class_init,
};

static void i82374_register_types(void)
{
    type_register_static(&i82374_isa_info);
}

type_init(i82374_register_types)
