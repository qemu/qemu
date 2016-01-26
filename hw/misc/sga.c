/*
 * QEMU dummy ISA device for loading sgabios option rom.
 *
 * Copyright (c) 2011 Glauber Costa, Red Hat Inc.
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
 *
 * sgabios code originally available at code.google.com/p/sgabios
 *
 */
#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/i386/pc.h"
#include "hw/loader.h"
#include "sysemu/sysemu.h"

#define SGABIOS_FILENAME "sgabios.bin"

#define TYPE_SGA "sga"
#define SGA(obj) OBJECT_CHECK(ISASGAState, (obj), TYPE_SGA)

typedef struct ISASGAState {
    ISADevice parent_obj;
} ISASGAState;

static void sga_realizefn(DeviceState *dev, Error **errp)
{
    rom_add_vga(SGABIOS_FILENAME);
}

static void sga_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
    dc->realize = sga_realizefn;
    dc->desc = "Serial Graphics Adapter";
}

static const TypeInfo sga_info = {
    .name          = TYPE_SGA,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(ISASGAState),
    .class_init    = sga_class_initfn,
};

static void sga_register_types(void)
{
    type_register_static(&sga_info);
}

type_init(sga_register_types)
