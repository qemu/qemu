/*
 * SMC FDC37C669 Super I/O controller
 *
 * Copyright (c) 2018 Philippe Mathieu-Daudé
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/isa/superio.h"
#include "qemu/module.h"

/* UARTs (compatible with NS16450 or PC16550) */

static uint16_t get_serial_iobase(ISASuperIODevice *sio, uint8_t index)
{
    return index ? 0x2f8 : 0x3f8;
}

static unsigned int get_serial_irq(ISASuperIODevice *sio, uint8_t index)
{
    return index ? 3 : 4;
}

/* Parallel port */

static uint16_t get_parallel_iobase(ISASuperIODevice *sio, uint8_t index)
{
    return 0x378;
}

static unsigned int get_parallel_irq(ISASuperIODevice *sio, uint8_t index)
{
    return 7;
}

static unsigned int get_parallel_dma(ISASuperIODevice *sio, uint8_t index)
{
    return 3;
}

/* Diskette controller (Software compatible with the Intel PC8477) */

static uint16_t get_fdc_iobase(ISASuperIODevice *sio, uint8_t index)
{
    return 0x3f0;
}

static unsigned int get_fdc_irq(ISASuperIODevice *sio, uint8_t index)
{
    return 6;
}

static unsigned int get_fdc_dma(ISASuperIODevice *sio, uint8_t index)
{
    return 2;
}

static void smc37c669_class_init(ObjectClass *klass, const void *data)
{
    ISASuperIOClass *sc = ISA_SUPERIO_CLASS(klass);

    sc->parallel = (ISASuperIOFuncs){
        .count = 1,
        .get_iobase = get_parallel_iobase,
        .get_irq    = get_parallel_irq,
        .get_dma    = get_parallel_dma,
    };
    sc->serial = (ISASuperIOFuncs){
        .count = 2,
        .get_iobase = get_serial_iobase,
        .get_irq    = get_serial_irq,
    };
    sc->floppy = (ISASuperIOFuncs){
        .count = 1,
        .get_iobase = get_fdc_iobase,
        .get_irq    = get_fdc_irq,
        .get_dma    = get_fdc_dma,
    };
    sc->ide.count = 0;
}

static const TypeInfo smc37c669_type_info = {
    .name          = TYPE_SMC37C669_SUPERIO,
    .parent        = TYPE_ISA_SUPERIO,
    .class_size    = sizeof(ISASuperIOClass),
    .class_init    = smc37c669_class_init,
};

static void smc37c669_register_types(void)
{
    type_register_static(&smc37c669_type_info);
}

type_init(smc37c669_register_types)
