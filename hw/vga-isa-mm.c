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
#include "hw.h"
#include "console.h"
#include "pc.h"
#include "vga_int.h"
#include "pixel_ops.h"
#include "qemu-timer.h"

typedef struct ISAVGAMMState {
    VGACommonState vga;
    int it_shift;
} ISAVGAMMState;

/* Memory mapped interface */
static uint32_t vga_mm_readb (void *opaque, target_phys_addr_t addr)
{
    ISAVGAMMState *s = opaque;

    return vga_ioport_read(&s->vga, addr >> s->it_shift) & 0xff;
}

static void vga_mm_writeb (void *opaque,
                           target_phys_addr_t addr, uint32_t value)
{
    ISAVGAMMState *s = opaque;

    vga_ioport_write(&s->vga, addr >> s->it_shift, value & 0xff);
}

static uint32_t vga_mm_readw (void *opaque, target_phys_addr_t addr)
{
    ISAVGAMMState *s = opaque;

    return vga_ioport_read(&s->vga, addr >> s->it_shift) & 0xffff;
}

static void vga_mm_writew (void *opaque,
                           target_phys_addr_t addr, uint32_t value)
{
    ISAVGAMMState *s = opaque;

    vga_ioport_write(&s->vga, addr >> s->it_shift, value & 0xffff);
}

static uint32_t vga_mm_readl (void *opaque, target_phys_addr_t addr)
{
    ISAVGAMMState *s = opaque;

    return vga_ioport_read(&s->vga, addr >> s->it_shift);
}

static void vga_mm_writel (void *opaque,
                           target_phys_addr_t addr, uint32_t value)
{
    ISAVGAMMState *s = opaque;

    vga_ioport_write(&s->vga, addr >> s->it_shift, value);
}

static CPUReadMemoryFunc * const vga_mm_read_ctrl[] = {
    &vga_mm_readb,
    &vga_mm_readw,
    &vga_mm_readl,
};

static CPUWriteMemoryFunc * const vga_mm_write_ctrl[] = {
    &vga_mm_writeb,
    &vga_mm_writew,
    &vga_mm_writel,
};

static void vga_mm_init(ISAVGAMMState *s, target_phys_addr_t vram_base,
                        target_phys_addr_t ctrl_base, int it_shift)
{
    int s_ioport_ctrl, vga_io_memory;

    s->it_shift = it_shift;
    s_ioport_ctrl = cpu_register_io_memory(vga_mm_read_ctrl, vga_mm_write_ctrl, s,
                                           DEVICE_NATIVE_ENDIAN);
    vga_io_memory = cpu_register_io_memory(vga_mem_read, vga_mem_write, s,
                                           DEVICE_NATIVE_ENDIAN);

    vmstate_register(NULL, 0, &vmstate_vga_common, s);

    cpu_register_physical_memory(ctrl_base, 0x100000, s_ioport_ctrl);
    s->vga.bank_offset = 0;
    cpu_register_physical_memory(vram_base + 0x000a0000, 0x20000, vga_io_memory);
    qemu_register_coalesced_mmio(vram_base + 0x000a0000, 0x20000);
}

int isa_vga_mm_init(target_phys_addr_t vram_base,
                    target_phys_addr_t ctrl_base, int it_shift)
{
    ISAVGAMMState *s;

    s = qemu_mallocz(sizeof(*s));

    vga_common_init(&s->vga, VGA_RAM_SIZE);
    vga_mm_init(s, vram_base, ctrl_base, it_shift);

    s->vga.ds = graphic_console_init(s->vga.update, s->vga.invalidate,
                                     s->vga.screen_dump, s->vga.text_update, s);

    vga_init_vbe(&s->vga);
    return 0;
}
