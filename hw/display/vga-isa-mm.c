/*
 * QEMU ISA MM VGA Emulator.
 *
 * Copyright (c) 2003 Fabrice Bellard
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
#include "hw/hw.h"
#include "ui/console.h"
#include "hw/i386/pc.h"
#include "vga_int.h"
#include "ui/pixel_ops.h"
#include "qemu/timer.h"

#define VGA_RAM_SIZE (8192 * 1024)

typedef struct ISAVGAMMState {
    VGACommonState vga;
    int it_shift;
} ISAVGAMMState;

/* Memory mapped interface */
static uint32_t vga_mm_readb (void *opaque, hwaddr addr)
{
    ISAVGAMMState *s = opaque;

    return vga_ioport_read(&s->vga, addr >> s->it_shift) & 0xff;
}

static void vga_mm_writeb (void *opaque,
                           hwaddr addr, uint32_t value)
{
    ISAVGAMMState *s = opaque;

    vga_ioport_write(&s->vga, addr >> s->it_shift, value & 0xff);
}

static uint32_t vga_mm_readw (void *opaque, hwaddr addr)
{
    ISAVGAMMState *s = opaque;

    return vga_ioport_read(&s->vga, addr >> s->it_shift) & 0xffff;
}

static void vga_mm_writew (void *opaque,
                           hwaddr addr, uint32_t value)
{
    ISAVGAMMState *s = opaque;

    vga_ioport_write(&s->vga, addr >> s->it_shift, value & 0xffff);
}

static uint32_t vga_mm_readl (void *opaque, hwaddr addr)
{
    ISAVGAMMState *s = opaque;

    return vga_ioport_read(&s->vga, addr >> s->it_shift);
}

static void vga_mm_writel (void *opaque,
                           hwaddr addr, uint32_t value)
{
    ISAVGAMMState *s = opaque;

    vga_ioport_write(&s->vga, addr >> s->it_shift, value);
}

static const MemoryRegionOps vga_mm_ctrl_ops = {
    .old_mmio = {
        .read = {
            vga_mm_readb,
            vga_mm_readw,
            vga_mm_readl,
        },
        .write = {
            vga_mm_writeb,
            vga_mm_writew,
            vga_mm_writel,
        },
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void vga_mm_init(ISAVGAMMState *s, hwaddr vram_base,
                        hwaddr ctrl_base, int it_shift,
                        MemoryRegion *address_space)
{
    MemoryRegion *s_ioport_ctrl, *vga_io_memory;

    s->it_shift = it_shift;
    s_ioport_ctrl = g_malloc(sizeof(*s_ioport_ctrl));
    memory_region_init_io(s_ioport_ctrl, NULL, &vga_mm_ctrl_ops, s,
                          "vga-mm-ctrl", 0x100000);
    memory_region_set_flush_coalesced(s_ioport_ctrl);

    vga_io_memory = g_malloc(sizeof(*vga_io_memory));
    /* XXX: endianness? */
    memory_region_init_io(vga_io_memory, NULL, &vga_mem_ops, &s->vga,
                          "vga-mem", 0x20000);

    vmstate_register(NULL, 0, &vmstate_vga_common, s);

    memory_region_add_subregion(address_space, ctrl_base, s_ioport_ctrl);
    s->vga.bank_offset = 0;
    memory_region_add_subregion(address_space,
                                vram_base + 0x000a0000, vga_io_memory);
    memory_region_set_coalescing(vga_io_memory);
}

int isa_vga_mm_init(hwaddr vram_base,
                    hwaddr ctrl_base, int it_shift,
                    MemoryRegion *address_space)
{
    ISAVGAMMState *s;

    s = g_malloc0(sizeof(*s));

    s->vga.vram_size_mb = VGA_RAM_SIZE >> 20;
    vga_common_init(&s->vga, NULL);
    vga_mm_init(s, vram_base, ctrl_base, it_shift, address_space);

    s->vga.con = graphic_console_init(NULL, 0, s->vga.hw_ops, s);

    vga_init_vbe(&s->vga, NULL, address_space);
    return 0;
}
