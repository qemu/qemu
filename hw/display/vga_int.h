/*
 * QEMU internal VGA defines.
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
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

#ifndef HW_VGA_INT_H
#define HW_VGA_INT_H

#include "exec/ioport.h"
#include "exec/memory.h"

#include "hw/display/bochs-vbe.h"
#include "hw/acpi/acpi_aml_interface.h"

#define ST01_V_RETRACE      0x08
#define ST01_DISP_ENABLE    0x01

#define CH_ATTR_SIZE (160 * 100)
#define VGA_MAX_HEIGHT 2048

struct vga_precise_retrace {
    int64_t ticks_per_char;
    int64_t total_chars;
    int htotal;
    int hstart;
    int hend;
    int vstart;
    int vend;
    int freq;
};

union vga_retrace {
    struct vga_precise_retrace precise;
};

struct VGACommonState;
typedef uint8_t (* vga_retrace_fn)(struct VGACommonState *s);
typedef void (* vga_update_retrace_info_fn)(struct VGACommonState *s);

typedef struct VGADisplayParams {
    uint32_t line_offset;
    uint32_t start_addr;
    uint32_t line_compare;
    uint8_t  hpel;
    bool     hpel_split;
} VGADisplayParams;

typedef struct VGACommonState {
    MemoryRegion *legacy_address_space;
    uint8_t *vram_ptr;
    MemoryRegion vram;
    uint32_t vram_size;
    uint32_t vram_size_mb; /* property */
    uint32_t vbe_size;
    uint32_t vbe_size_mask;
    uint32_t latch;
    bool has_chain4_alias;
    MemoryRegion chain4_alias;
    uint8_t sr_index;
    uint8_t sr[256];
    uint8_t sr_vbe[256];
    uint8_t gr_index;
    uint8_t gr[256];
    uint8_t ar_index;
    uint8_t ar[21];
    int ar_flip_flop;
    uint8_t cr_index;
    uint8_t cr[256]; /* CRT registers */
    uint8_t msr; /* Misc Output Register */
    uint8_t fcr; /* Feature Control Register */
    uint8_t st00; /* status 0 */
    uint8_t st01; /* status 1 */
    uint8_t dac_state;
    uint8_t dac_sub_index;
    uint8_t dac_read_index;
    uint8_t dac_write_index;
    uint8_t dac_cache[3]; /* used when writing */
    int dac_8bit;
    uint8_t palette[768];
    int32_t bank_offset;
    int (*get_bpp)(struct VGACommonState *s);
    void (*get_params)(struct VGACommonState *s, VGADisplayParams *params);
    void (*get_resolution)(struct VGACommonState *s,
                        int *pwidth,
                        int *pheight);
    PortioList vga_port_list;
    PortioList vbe_port_list;
    /* bochs vbe state */
    uint16_t vbe_index;
    uint16_t vbe_regs[VBE_DISPI_INDEX_NB];
    uint32_t vbe_start_addr;
    uint32_t vbe_line_offset;
    uint32_t vbe_bank_mask;
    /* display refresh support */
    QemuConsole *con;
    uint32_t font_offsets[2];
    uint8_t *panning_buf;
    int graphic_mode;
    uint8_t shift_control;
    uint8_t double_scan;
    VGADisplayParams params;
    uint32_t plane_updated;
    uint32_t last_line_offset;
    uint8_t last_cw, last_ch;
    uint32_t last_width, last_height; /* in chars or pixels */
    uint32_t last_scr_width, last_scr_height; /* in pixels */
    uint32_t last_depth; /* in bits */
    bool last_byteswap;
    bool force_shadow;
    uint8_t cursor_start, cursor_end;
    bool cursor_visible_phase;
    int64_t cursor_blink_time;
    uint32_t cursor_offset;
    const GraphicHwOps *hw_ops;
    bool full_update_text;
    bool full_update_gfx;
    bool big_endian_fb;
    bool default_endian_fb;
    bool global_vmstate;
    /* hardware mouse cursor support */
    uint32_t invalidated_y_table[VGA_MAX_HEIGHT / 32];
    uint32_t hw_cursor_x;
    uint32_t hw_cursor_y;
    void (*cursor_invalidate)(struct VGACommonState *s);
    void (*cursor_draw_line)(struct VGACommonState *s, uint8_t *d, int y);
    /* tell for each page if it has been updated since the last time */
    uint32_t last_palette[256];
    uint32_t last_ch_attr[CH_ATTR_SIZE]; /* XXX: make it dynamic */
    /* retrace */
    vga_retrace_fn retrace;
    vga_update_retrace_info_fn update_retrace_info;
    union vga_retrace retrace_info;
    uint8_t is_vbe_vmstate;
} VGACommonState;

static inline int c6_to_8(int v)
{
    int b;
    v &= 0x3f;
    b = v & 1;
    return (v << 2) | (b << 1) | b;
}

bool vga_common_init(VGACommonState *s, Object *obj, Error **errp);
void vga_init(VGACommonState *s, Object *obj, MemoryRegion *address_space,
              MemoryRegion *address_space_io, bool init_vga_ports);
MemoryRegion *vga_init_io(VGACommonState *s, Object *obj,
                          const MemoryRegionPortio **vga_ports,
                          const MemoryRegionPortio **vbe_ports);
void vga_common_reset(VGACommonState *s);

void vga_dirty_log_start(VGACommonState *s);
void vga_dirty_log_stop(VGACommonState *s);

extern const VMStateDescription vmstate_vga_common;
uint32_t vga_ioport_read(void *opaque, uint32_t addr);
void vga_ioport_write(void *opaque, uint32_t addr, uint32_t val);
uint32_t vga_mem_readb(VGACommonState *s, hwaddr addr);
void vga_mem_writeb(VGACommonState *s, hwaddr addr, uint32_t val);
void vga_invalidate_scanlines(VGACommonState *s, int y1, int y2);

int vga_ioport_invalid(VGACommonState *s, uint32_t addr);

uint32_t vbe_ioport_read_data(void *opaque, uint32_t addr);
void vbe_ioport_write_index(void *opaque, uint32_t addr, uint32_t val);
void vbe_ioport_write_data(void *opaque, uint32_t addr, uint32_t val);

extern const uint8_t sr_mask[8];
extern const uint8_t gr_mask[16];

#define VGABIOS_FILENAME "vgabios.bin"
#define VGABIOS_CIRRUS_FILENAME "vgabios-cirrus.bin"

extern const MemoryRegionOps vga_mem_ops;

/* vga-pci.c */
void pci_std_vga_mmio_region_init(VGACommonState *s,
                                  Object *owner,
                                  MemoryRegion *parent,
                                  MemoryRegion *subs,
                                  bool qext, bool edid);

void build_vga_aml(AcpiDevAmlIf *adev, Aml *scope);
#endif
