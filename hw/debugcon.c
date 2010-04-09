/*
 * QEMU Bochs-style debug console ("port E9") emulation
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 * Copyright (c) 2008 Citrix Systems, Inc.
 * Copyright (c) Intel Corporation; author: H. Peter Anvin
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

#include "hw.h"
#include "qemu-char.h"
#include "isa.h"
#include "pc.h"

//#define DEBUG_DEBUGCON

typedef struct DebugconState {
    CharDriverState *chr;
    uint32_t readback;
} DebugconState;

typedef struct ISADebugconState {
    ISADevice dev;
    uint32_t iobase;
    DebugconState state;
} ISADebugconState;

static void debugcon_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    DebugconState *s = opaque;
    unsigned char ch = val;

#ifdef DEBUG_DEBUGCON
    printf("debugcon: write addr=0x%04x val=0x%02x\n", addr, val);
#endif

    qemu_chr_write(s->chr, &ch, 1);
}


static uint32_t debugcon_ioport_read(void *opaque, uint32_t addr)
{
    DebugconState *s = opaque;

#ifdef DEBUG_DEBUGCON
    printf("debugcon: read addr=0x%04x\n", addr);
#endif

    return s->readback;
}

static void debugcon_init_core(DebugconState *s)
{
    if (!s->chr) {
        fprintf(stderr, "Can't create debugcon device, empty char device\n");
        exit(1);
    }

    qemu_chr_add_handlers(s->chr, NULL, NULL, NULL, s);
}

static int debugcon_isa_initfn(ISADevice *dev)
{
    ISADebugconState *isa = DO_UPCAST(ISADebugconState, dev, dev);
    DebugconState *s = &isa->state;

    debugcon_init_core(s);
    register_ioport_write(isa->iobase, 1, 1, debugcon_ioport_write, s);
    register_ioport_read(isa->iobase, 1, 1, debugcon_ioport_read, s);
    return 0;
}

static ISADeviceInfo debugcon_isa_info = {
    .qdev.name  = "isa-debugcon",
    .qdev.size  = sizeof(ISADebugconState),
    .init       = debugcon_isa_initfn,
    .qdev.props = (Property[]) {
        DEFINE_PROP_HEX32("iobase", ISADebugconState, iobase, 0xe9),
        DEFINE_PROP_CHR("chardev",  ISADebugconState, state.chr),
        DEFINE_PROP_HEX32("readback", ISADebugconState, state.readback, 0xe9),
        DEFINE_PROP_END_OF_LIST(),
    },
};

static void debugcon_register_devices(void)
{
    isa_qdev_register(&debugcon_isa_info);
}

device_init(debugcon_register_devices)
