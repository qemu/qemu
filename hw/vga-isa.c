/*
 * QEMU ISA VGA Emulator.
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

int isa_vga_init(void)
{
    VGACommonState *s;

    s = qemu_mallocz(sizeof(*s));

    vga_common_init(s, VGA_RAM_SIZE);
    vga_init(s);
    register_savevm("vga", 0, 2, vga_common_save, vga_common_load, s);

    s->ds = graphic_console_init(s->update, s->invalidate,
                                 s->screen_dump, s->text_update, s);

#ifdef CONFIG_BOCHS_VBE
    /* XXX: use optimized standard vga accesses */
    cpu_register_physical_memory(VBE_DISPI_LFB_PHYSICAL_ADDRESS,
                                 VGA_RAM_SIZE, s->vram_offset);
#endif
    return 0;
}
