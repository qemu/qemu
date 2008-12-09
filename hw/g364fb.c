/*
 * QEMU G364 framebuffer Emulator.
 *
 * Copyright (c) 2007-2008 Hervé Poussineau
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include "hw.h"
#include "mips.h"
#include "console.h"
#include "pixel_ops.h"

//#define DEBUG_G364

typedef struct G364State {
    unsigned int vram_size;
    uint8_t *vram_buffer;
    uint32_t ctla;
    uint8_t palette[256][3];
    /* display refresh support */
    DisplayState *ds;
    QEMUConsole *console;
    int graphic_mode;
    uint32_t scr_width, scr_height; /* in pixels */
} G364State;

/*
 * graphic modes
 */
#define BPP 8
#define PIXEL_WIDTH 8
#include "g364fb_template.h"
#undef BPP
#undef PIXEL_WIDTH

#define BPP 15
#define PIXEL_WIDTH 16
#include "g364fb_template.h"
#undef BPP
#undef PIXEL_WIDTH

#define BPP 16
#define PIXEL_WIDTH 16
#include "g364fb_template.h"
#undef BPP
#undef PIXEL_WIDTH

#define BPP 32
#define PIXEL_WIDTH 32
#include "g364fb_template.h"
#undef BPP
#undef PIXEL_WIDTH

#define REG_DISPLAYX 0x0918
#define REG_DISPLAYY 0x0940

#define CTLA_FORCE_BLANK 0x400

static void g364fb_draw_graphic(G364State *s, int full_update)
{
    switch (ds_get_bits_per_pixel(s->ds)) {
        case 8:
            g364fb_draw_graphic8(s, full_update);
            break;
        case 15:
            g364fb_draw_graphic15(s, full_update);
            break;
        case 16:
            g364fb_draw_graphic16(s, full_update);
            break;
        case 32:
            g364fb_draw_graphic32(s, full_update);
            break;
        default:
            printf("g364fb: unknown depth %d\n", ds_get_bits_per_pixel(s->ds));
            return;
    }

    dpy_update(s->ds, 0, 0, s->scr_width, s->scr_height);
}

static void g364fb_draw_blank(G364State *s, int full_update)
{
    int i, w;
    uint8_t *d;

    if (!full_update)
        return;

    w = s->scr_width * ((ds_get_bits_per_pixel(s->ds) + 7) >> 3);
    d = ds_get_data(s->ds);
    for(i = 0; i < s->scr_height; i++) {
        memset(d, 0, w);
        d += ds_get_linesize(s->ds);
    }

    dpy_update(s->ds, 0, 0, s->scr_width, s->scr_height);
}

#define GMODE_GRAPH 0
#define GMODE_BLANK 1

static void g364fb_update_display(void *opaque)
{
    G364State *s = opaque;
    int full_update, graphic_mode;

    if (s->scr_width == 0 || s->scr_height == 0)
        return;

    if (s->ctla & CTLA_FORCE_BLANK)
        graphic_mode = GMODE_BLANK;
    else
        graphic_mode = GMODE_GRAPH;
    full_update = 0;
    if (graphic_mode != s->graphic_mode) {
        s->graphic_mode = graphic_mode;
        full_update = 1;
    }
    if (s->scr_width != ds_get_width(s->ds) || s->scr_height != ds_get_height(s->ds)) {
        qemu_console_resize(s->console, s->scr_width, s->scr_height);
        full_update = 1;
    }
    switch(graphic_mode) {
        case GMODE_GRAPH:
            g364fb_draw_graphic(s, full_update);
            break;
        case GMODE_BLANK:
        default:
            g364fb_draw_blank(s, full_update);
            break;
    }
}

/* force a full display refresh */
static void g364fb_invalidate_display(void *opaque)
{
    G364State *s = opaque;
    s->graphic_mode = -1; /* force full update */
}

static void g364fb_reset(void *opaque)
{
    G364State *s = opaque;

    memset(s->palette, 0, sizeof(s->palette));
    s->scr_width = s->scr_height = 0;
    memset(s->vram_buffer, 0, s->vram_size);
    s->graphic_mode = -1; /* force full update */
}

static void g364fb_screen_dump(void *opaque, const char *filename)
{
    G364State *s = opaque;
    int y, x;
    uint8_t index;
    uint8_t *data_buffer;
    FILE *f;

    f = fopen(filename, "wb");
    if (!f)
        return;

    data_buffer = s->vram_buffer;
    fprintf(f, "P6\n%d %d\n%d\n",
        s->scr_width, s->scr_height, 255);
    for(y = 0; y < s->scr_height; y++)
        for(x = 0; x < s->scr_width; x++, data_buffer++) {
            index = *data_buffer;
            fputc(s->palette[index][0], f);
            fputc(s->palette[index][1], f);
            fputc(s->palette[index][2], f);
        }
    fclose(f);
}

/* called for accesses to io ports */
static uint32_t g364fb_ctrl_readb(void *opaque, target_phys_addr_t addr)
{
    //G364State *s = opaque;
    uint32_t val;

    addr &= 0xffff;

    switch (addr) {
        default:
#ifdef DEBUG_G364
            printf("g364fb/ctrl: invalid read at [" TARGET_FMT_lx "]\n", addr);
#endif
            val = 0;
            break;
    }

#ifdef DEBUG_G364
    printf("g364fb/ctrl: read 0x%02x at [" TARGET_FMT_lx "]\n", val, addr);
#endif

    return val;
}

static uint32_t g364fb_ctrl_readw(void *opaque, target_phys_addr_t addr)
{
    uint32_t v;
    v = g364fb_ctrl_readb(opaque, addr);
    v |= g364fb_ctrl_readb(opaque, addr + 1) << 8;
    return v;
}

static uint32_t g364fb_ctrl_readl(void *opaque, target_phys_addr_t addr)
{
    uint32_t v;
    v = g364fb_ctrl_readb(opaque, addr);
    v |= g364fb_ctrl_readb(opaque, addr + 1) << 8;
    v |= g364fb_ctrl_readb(opaque, addr + 2) << 16;
    v |= g364fb_ctrl_readb(opaque, addr + 3) << 24;
    return v;
}

static void g364fb_ctrl_writeb(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    G364State *s = opaque;

    addr &= 0xffff;

#ifdef DEBUG_G364
    printf("g364fb/ctrl: write 0x%02x at [" TARGET_FMT_lx "]\n", val, addr);
#endif

    if (addr < 0x0800) {
        /* color palette */
        int idx = addr >> 3;
        int c = addr & 7;
        if (c < 3)
            s->palette[idx][c] = (uint8_t)val;
    } else {
        switch (addr) {
            case REG_DISPLAYX:
                s->scr_width = (s->scr_width & 0xfffffc03) | (val << 2);
                break;
            case REG_DISPLAYX + 1:
                s->scr_width = (s->scr_width & 0xfffc03ff) | (val << 10);
                break;
            case REG_DISPLAYY:
                s->scr_height = (s->scr_height & 0xffffff80) | (val >> 1);
                break;
            case REG_DISPLAYY + 1:
                s->scr_height = (s->scr_height & 0xffff801f) | (val << 7);
                break;
            default:
#ifdef DEBUG_G364
                printf("g364fb/ctrl: invalid write of 0x%02x at [" TARGET_FMT_lx "]\n", val, addr);
#endif
                break;
        }
    }
    s->graphic_mode = -1; /* force full update */
}

static void g364fb_ctrl_writew(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    g364fb_ctrl_writeb(opaque, addr, val & 0xff);
    g364fb_ctrl_writeb(opaque, addr + 1, (val >> 8) & 0xff);
}

static void g364fb_ctrl_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    g364fb_ctrl_writeb(opaque, addr, val & 0xff);
    g364fb_ctrl_writeb(opaque, addr + 1, (val >> 8) & 0xff);
    g364fb_ctrl_writeb(opaque, addr + 2, (val >> 16) & 0xff);
    g364fb_ctrl_writeb(opaque, addr + 3, (val >> 24) & 0xff);
}

static CPUReadMemoryFunc *g364fb_ctrl_read[3] = {
    g364fb_ctrl_readb,
    g364fb_ctrl_readw,
    g364fb_ctrl_readl,
};

static CPUWriteMemoryFunc *g364fb_ctrl_write[3] = {
    g364fb_ctrl_writeb,
    g364fb_ctrl_writew,
    g364fb_ctrl_writel,
};

/* called for accesses to video ram */
static uint32_t g364fb_mem_readb(void *opaque, target_phys_addr_t addr)
{
    G364State *s = opaque;

    return s->vram_buffer[addr];
}

static uint32_t g364fb_mem_readw(void *opaque, target_phys_addr_t addr)
{
    uint32_t v;
    v = g364fb_mem_readb(opaque, addr);
    v |= g364fb_mem_readb(opaque, addr + 1) << 8;
    return v;
}

static uint32_t g364fb_mem_readl(void *opaque, target_phys_addr_t addr)
{
    uint32_t v;
    v = g364fb_mem_readb(opaque, addr);
    v |= g364fb_mem_readb(opaque, addr + 1) << 8;
    v |= g364fb_mem_readb(opaque, addr + 2) << 16;
    v |= g364fb_mem_readb(opaque, addr + 3) << 24;
    return v;
}

static void g364fb_mem_writeb(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    G364State *s = opaque;

    s->vram_buffer[addr] = val;
}

static void g364fb_mem_writew(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    g364fb_mem_writeb(opaque, addr, val & 0xff);
    g364fb_mem_writeb(opaque, addr + 1, (val >> 8) & 0xff);
}

static void g364fb_mem_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    g364fb_mem_writeb(opaque, addr, val & 0xff);
    g364fb_mem_writeb(opaque, addr + 1, (val >> 8) & 0xff);
    g364fb_mem_writeb(opaque, addr + 2, (val >> 16) & 0xff);
    g364fb_mem_writeb(opaque, addr + 3, (val >> 24) & 0xff);
}

static CPUReadMemoryFunc *g364fb_mem_read[3] = {
    g364fb_mem_readb,
    g364fb_mem_readw,
    g364fb_mem_readl,
};

static CPUWriteMemoryFunc *g364fb_mem_write[3] = {
    g364fb_mem_writeb,
    g364fb_mem_writew,
    g364fb_mem_writel,
};

int g364fb_mm_init(DisplayState *ds,
                   int vram_size, int it_shift,
                   target_phys_addr_t vram_base, target_phys_addr_t ctrl_base)
{
    G364State *s;
    int io_vram, io_ctrl;

    s = qemu_mallocz(sizeof(G364State));
    if (!s)
        return -1;

    s->vram_size = vram_size;
    s->vram_buffer = qemu_mallocz(s->vram_size);

    qemu_register_reset(g364fb_reset, s);
    g364fb_reset(s);

    s->ds = ds;

    s->console = graphic_console_init(ds, g364fb_update_display,
                                      g364fb_invalidate_display,
                                      g364fb_screen_dump, NULL, s);

    io_vram = cpu_register_io_memory(0, g364fb_mem_read, g364fb_mem_write, s);
    cpu_register_physical_memory(vram_base, vram_size, io_vram);

    io_ctrl = cpu_register_io_memory(0, g364fb_ctrl_read, g364fb_ctrl_write, s);
    cpu_register_physical_memory(ctrl_base, 0x10000, io_ctrl);

    return 0;
}
