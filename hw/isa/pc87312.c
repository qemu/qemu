/*
 * QEMU National Semiconductor PC87312 (Super I/O)
 *
 * Copyright (c) 2010-2012 Herve Poussineau
 * Copyright (c) 2011-2012 Andreas Färber
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
#include "hw/isa/pc87312.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "trace.h"


#define REG_FER 0
#define REG_FAR 1
#define REG_PTR 2

#define FER_PARALLEL_EN   0x01
#define FER_UART1_EN      0x02
#define FER_UART2_EN      0x04
#define FER_FDC_EN        0x08
#define FER_FDC_4         0x10
#define FER_FDC_ADDR      0x20
#define FER_IDE_EN        0x40
#define FER_IDE_ADDR      0x80

#define FAR_PARALLEL_ADDR 0x03
#define FAR_UART1_ADDR    0x0C
#define FAR_UART2_ADDR    0x30
#define FAR_UART_3_4      0xC0

#define PTR_POWER_DOWN    0x01
#define PTR_CLOCK_DOWN    0x02
#define PTR_PWDN          0x04
#define PTR_IRQ_5_7       0x08
#define PTR_UART1_TEST    0x10
#define PTR_UART2_TEST    0x20
#define PTR_LOCK_CONF     0x40
#define PTR_EPP_MODE      0x80


/* Parallel port */

static bool is_parallel_enabled(ISASuperIODevice *sio, uint8_t index)
{
    PC87312State *s = PC87312(sio);
    return index ? false : s->regs[REG_FER] & FER_PARALLEL_EN;
}

static const uint16_t parallel_base[] = { 0x378, 0x3bc, 0x278, 0x00 };

static uint16_t get_parallel_iobase(ISASuperIODevice *sio, uint8_t index)
{
    PC87312State *s = PC87312(sio);
    return parallel_base[s->regs[REG_FAR] & FAR_PARALLEL_ADDR];
}

static const unsigned int parallel_irq[] = { 5, 7, 5, 0 };

static unsigned int get_parallel_irq(ISASuperIODevice *sio, uint8_t index)
{
    PC87312State *s = PC87312(sio);
    int idx;
    idx = (s->regs[REG_FAR] & FAR_PARALLEL_ADDR);
    if (idx == 0) {
        return (s->regs[REG_PTR] & PTR_IRQ_5_7) ? 7 : 5;
    } else {
        return parallel_irq[idx];
    }
}


/* UARTs */

static const uint16_t uart_base[2][4] = {
    { 0x3e8, 0x338, 0x2e8, 0x220 },
    { 0x2e8, 0x238, 0x2e0, 0x228 }
};

static uint16_t get_uart_iobase(ISASuperIODevice *sio, uint8_t i)
{
    PC87312State *s = PC87312(sio);
    int idx;
    idx = (s->regs[REG_FAR] >> (2 * i + 2)) & 0x3;
    if (idx == 0) {
        return 0x3f8;
    } else if (idx == 1) {
        return 0x2f8;
    } else {
        return uart_base[idx & 1][(s->regs[REG_FAR] & FAR_UART_3_4) >> 6];
    }
}

static unsigned int get_uart_irq(ISASuperIODevice *sio, uint8_t i)
{
    PC87312State *s = PC87312(sio);
    int idx;
    idx = (s->regs[REG_FAR] >> (2 * i + 2)) & 0x3;
    return (idx & 1) ? 3 : 4;
}

static bool is_uart_enabled(ISASuperIODevice *sio, uint8_t i)
{
    PC87312State *s = PC87312(sio);
    return s->regs[REG_FER] & (FER_UART1_EN << i);
}


/* Floppy controller */

static bool is_fdc_enabled(ISASuperIODevice *sio, uint8_t index)
{
    PC87312State *s = PC87312(sio);
    assert(!index);
    return s->regs[REG_FER] & FER_FDC_EN;
}

static uint16_t get_fdc_iobase(ISASuperIODevice *sio, uint8_t index)
{
    PC87312State *s = PC87312(sio);
    assert(!index);
    return (s->regs[REG_FER] & FER_FDC_ADDR) ? 0x370 : 0x3f0;
}

static unsigned int get_fdc_irq(ISASuperIODevice *sio, uint8_t index)
{
    assert(!index);
    return 6;
}


/* IDE controller */

static bool is_ide_enabled(ISASuperIODevice *sio, uint8_t index)
{
    PC87312State *s = PC87312(sio);

    return s->regs[REG_FER] & FER_IDE_EN;
}

static uint16_t get_ide_iobase(ISASuperIODevice *sio, uint8_t index)
{
    PC87312State *s = PC87312(sio);

    if (index == 1) {
        return get_ide_iobase(sio, 0) + 0x206;
    }
    return (s->regs[REG_FER] & FER_IDE_ADDR) ? 0x170 : 0x1f0;
}

static unsigned int get_ide_irq(ISASuperIODevice *sio, uint8_t index)
{
    assert(index == 0);
    return 14;
}

static void reconfigure_devices(PC87312State *s)
{
    error_report("pc87312: unsupported device reconfiguration (%02x %02x %02x)",
                 s->regs[REG_FER], s->regs[REG_FAR], s->regs[REG_PTR]);
}

static void pc87312_soft_reset(PC87312State *s)
{
    static const uint8_t fer_init[] = {
        0x4f, 0x4f, 0x4f, 0x4f, 0x4f, 0x4f, 0x4b, 0x4b,
        0x4b, 0x4b, 0x4b, 0x4b, 0x0f, 0x0f, 0x0f, 0x0f,
        0x49, 0x49, 0x49, 0x49, 0x07, 0x07, 0x07, 0x07,
        0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x08, 0x00,
    };
    static const uint8_t far_init[] = {
        0x10, 0x11, 0x11, 0x39, 0x24, 0x38, 0x00, 0x01,
        0x01, 0x09, 0x08, 0x08, 0x10, 0x11, 0x39, 0x24,
        0x00, 0x01, 0x01, 0x00, 0x10, 0x11, 0x39, 0x24,
        0x10, 0x11, 0x11, 0x39, 0x24, 0x38, 0x10, 0x10,
    };
    static const uint8_t ptr_init[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    };

    s->read_id_step = 0;
    s->selected_index = REG_FER;

    s->regs[REG_FER] = fer_init[s->config & 0x1f];
    s->regs[REG_FAR] = far_init[s->config & 0x1f];
    s->regs[REG_PTR] = ptr_init[s->config & 0x1f];
}

static void pc87312_hard_reset(PC87312State *s)
{
    pc87312_soft_reset(s);
}

static void pc87312_io_write(void *opaque, hwaddr addr, uint64_t val,
                             unsigned int size)
{
    PC87312State *s = opaque;

    trace_pc87312_io_write(addr, val);

    if ((addr & 1) == 0) {
        /* Index register */
        s->read_id_step = 2;
        s->selected_index = val;
    } else {
        /* Data register */
        if (s->selected_index < 3) {
            s->regs[s->selected_index] = val;
            reconfigure_devices(s);
        }
    }
}

static uint64_t pc87312_io_read(void *opaque, hwaddr addr, unsigned int size)
{
    PC87312State *s = opaque;
    uint32_t val;

    if ((addr & 1) == 0) {
        /* Index register */
        if (s->read_id_step++ == 0) {
            val = 0x88;
        } else if (s->read_id_step++ == 1) {
            val = 0;
        } else {
            val = s->selected_index;
        }
    } else {
        /* Data register */
        if (s->selected_index < 3) {
            val = s->regs[s->selected_index];
        } else {
            /* Invalid selected index */
            val = 0;
        }
    }

    trace_pc87312_io_read(addr, val);
    return val;
}

static const MemoryRegionOps pc87312_io_ops = {
    .read  = pc87312_io_read,
    .write = pc87312_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static int pc87312_post_load(void *opaque, int version_id)
{
    PC87312State *s = opaque;

    reconfigure_devices(s);
    return 0;
}

static void pc87312_reset(DeviceState *d)
{
    PC87312State *s = PC87312(d);

    pc87312_soft_reset(s);
}

static void pc87312_realize(DeviceState *dev, Error **errp)
{
    PC87312State *s;
    ISADevice *isa;
    Error *local_err = NULL;

    s = PC87312(dev);
    isa = ISA_DEVICE(dev);
    isa_register_ioport(isa, &s->io, s->iobase);
    pc87312_hard_reset(s);

    ISA_SUPERIO_GET_CLASS(dev)->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
}

static void pc87312_initfn(Object *obj)
{
    PC87312State *s = PC87312(obj);

    memory_region_init_io(&s->io, obj, &pc87312_io_ops, s, "pc87312", 2);
}

static const VMStateDescription vmstate_pc87312 = {
    .name = "pc87312",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = pc87312_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(read_id_step, PC87312State),
        VMSTATE_UINT8(selected_index, PC87312State),
        VMSTATE_UINT8_ARRAY(regs, PC87312State, 3),
        VMSTATE_END_OF_LIST()
    }
};

static const Property pc87312_properties[] = {
    DEFINE_PROP_UINT16("iobase", PC87312State, iobase, 0x398),
    DEFINE_PROP_UINT8("config", PC87312State, config, 1),
};

static void pc87312_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ISASuperIOClass *sc = ISA_SUPERIO_CLASS(klass);

    device_class_set_legacy_reset(dc, pc87312_reset);
    dc->vmsd = &vmstate_pc87312;
    device_class_set_parent_realize(dc, pc87312_realize,
                                    &sc->parent_realize);
    device_class_set_props(dc, pc87312_properties);

    sc->parallel = (ISASuperIOFuncs){
        .count = 1,
        .is_enabled = is_parallel_enabled,
        .get_iobase = get_parallel_iobase,
        .get_irq    = get_parallel_irq,
    };
    sc->serial = (ISASuperIOFuncs){
        .count = 2,
        .is_enabled = is_uart_enabled,
        .get_iobase = get_uart_iobase,
        .get_irq    = get_uart_irq,
    };
    sc->floppy = (ISASuperIOFuncs){
        .count = 1,
        .is_enabled = is_fdc_enabled,
        .get_iobase = get_fdc_iobase,
        .get_irq    = get_fdc_irq,
    };
    sc->ide = (ISASuperIOFuncs){
        .count = 1,
        .is_enabled = is_ide_enabled,
        .get_iobase = get_ide_iobase,
        .get_irq    = get_ide_irq,
    };
}

static const TypeInfo pc87312_type_info = {
    .name          = TYPE_PC87312,
    .parent        = TYPE_ISA_SUPERIO,
    .instance_size = sizeof(PC87312State),
    .instance_init = pc87312_initfn,
    .class_init    = pc87312_class_init,
    /* FIXME use a qdev drive property instead of drive_get() */
};

static void pc87312_register_types(void)
{
    type_register_static(&pc87312_type_info);
}

type_init(pc87312_register_types)
