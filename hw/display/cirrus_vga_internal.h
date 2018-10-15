/*
 * QEMU Cirrus CLGD 54xx VGA Emulator, ISA bus support
 *
 * Copyright (c) 2004 Fabrice Bellard
 * Copyright (c) 2004 Makoto Suzuki (suzu)
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

#ifndef CIRRUS_VGA_INTERNAL_H
#define CIRRUS_VGA_INTERNAL_H

#include "vga_int.h"

/* IDs */
#define CIRRUS_ID_CLGD5422  (0x23 << 2)
#define CIRRUS_ID_CLGD5426  (0x24 << 2)
#define CIRRUS_ID_CLGD5424  (0x25 << 2)
#define CIRRUS_ID_CLGD5428  (0x26 << 2)
#define CIRRUS_ID_CLGD5430  (0x28 << 2)
#define CIRRUS_ID_CLGD5434  (0x2A << 2)
#define CIRRUS_ID_CLGD5436  (0x2B << 2)
#define CIRRUS_ID_CLGD5446  (0x2E << 2)

extern const VMStateDescription vmstate_cirrus_vga;

struct CirrusVGAState;
typedef void (*cirrus_bitblt_rop_t)(struct CirrusVGAState *s,
                                    uint32_t dstaddr, uint32_t srcaddr,
                                    int dstpitch, int srcpitch,
                                    int bltwidth, int bltheight);

typedef struct CirrusVGAState {
    VGACommonState vga;

    MemoryRegion cirrus_vga_io;
    MemoryRegion cirrus_linear_io;
    MemoryRegion cirrus_linear_bitblt_io;
    MemoryRegion cirrus_mmio_io;
    MemoryRegion pci_bar;
    bool linear_vram;  /* vga.vram mapped over cirrus_linear_io */
    MemoryRegion low_mem_container; /* container for 0xa0000-0xc0000 */
    MemoryRegion low_mem;           /* always mapped, overridden by: */
    MemoryRegion cirrus_bank[2];    /*   aliases at 0xa0000-0xb0000  */
    uint32_t cirrus_addr_mask;
    uint32_t linear_mmio_mask;
    uint8_t cirrus_shadow_gr0;
    uint8_t cirrus_shadow_gr1;
    uint8_t cirrus_hidden_dac_lockindex;
    uint8_t cirrus_hidden_dac_data;
    uint32_t cirrus_bank_base[2];
    uint32_t cirrus_bank_limit[2];
    uint8_t cirrus_hidden_palette[48];
    bool enable_blitter;
    int cirrus_blt_pixelwidth;
    int cirrus_blt_width;
    int cirrus_blt_height;
    int cirrus_blt_dstpitch;
    int cirrus_blt_srcpitch;
    uint32_t cirrus_blt_fgcol;
    uint32_t cirrus_blt_bgcol;
    uint32_t cirrus_blt_dstaddr;
    uint32_t cirrus_blt_srcaddr;
    uint8_t cirrus_blt_mode;
    uint8_t cirrus_blt_modeext;
    cirrus_bitblt_rop_t cirrus_rop;
#define CIRRUS_BLTBUFSIZE (2048 * 4) /* one line width */
    uint8_t cirrus_bltbuf[CIRRUS_BLTBUFSIZE];
    uint8_t *cirrus_srcptr;
    uint8_t *cirrus_srcptr_end;
    uint32_t cirrus_srccounter;
    /* hwcursor display state */
    int last_hw_cursor_size;
    int last_hw_cursor_x;
    int last_hw_cursor_y;
    int last_hw_cursor_y_start;
    int last_hw_cursor_y_end;
    int real_vram_size; /* XXX: suppress that */
    int device_id;
    int bustype;
} CirrusVGAState;

void cirrus_init_common(CirrusVGAState *s, Object *owner,
                        int device_id, int is_pci,
                        MemoryRegion *system_memory, MemoryRegion *system_io);

#endif
