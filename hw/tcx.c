/*
 * QEMU TCX Frame buffer
 * 
 * Copyright (c) 2003-2005 Fabrice Bellard
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
#include "vl.h"

#define MAXX 1024
#define MAXY 768
#define TCX_DAC_NREGS 16

typedef struct TCXState {
    uint32_t addr;
    DisplayState *ds;
    uint8_t *vram;
    ram_addr_t vram_offset;
    uint16_t width, height;
    uint8_t r[256], g[256], b[256];
    uint32_t palette[256];
    uint8_t dac_index, dac_state;
} TCXState;

static void tcx_screen_dump(void *opaque, const char *filename);

/* XXX: unify with vga draw line functions */
static inline unsigned int rgb_to_pixel8(unsigned int r, unsigned int g, unsigned b)
{
    return ((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6);
}

static inline unsigned int rgb_to_pixel15(unsigned int r, unsigned int g, unsigned b)
{
    return ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
}

static inline unsigned int rgb_to_pixel16(unsigned int r, unsigned int g, unsigned b)
{
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

static inline unsigned int rgb_to_pixel32(unsigned int r, unsigned int g, unsigned b)
{
    return (r << 16) | (g << 8) | b;
}

static void update_palette_entries(TCXState *s, int start, int end)
{
    int i;
    for(i = start; i < end; i++) {
        switch(s->ds->depth) {
        default:
        case 8:
            s->palette[i] = rgb_to_pixel8(s->r[i], s->g[i], s->b[i]);
            break;
        case 15:
            s->palette[i] = rgb_to_pixel15(s->r[i], s->g[i], s->b[i]);
            break;
        case 16:
            s->palette[i] = rgb_to_pixel16(s->r[i], s->g[i], s->b[i]);
            break;
        case 32:
            s->palette[i] = rgb_to_pixel32(s->r[i], s->g[i], s->b[i]);
            break;
        }
    }
}

static void tcx_draw_line32(TCXState *s1, uint8_t *d, 
			    const uint8_t *s, int width)
{
    int x;
    uint8_t val;
    uint32_t *p = (uint32_t *)d;

    for(x = 0; x < width; x++) {
	val = *s++;
        *p++ = s1->palette[val];
    }
}

static void tcx_draw_line16(TCXState *s1, uint8_t *d, 
			    const uint8_t *s, int width)
{
    int x;
    uint8_t val;
    uint16_t *p = (uint16_t *)d;

    for(x = 0; x < width; x++) {
	val = *s++;
        *p++ = s1->palette[val];
    }
}

static void tcx_draw_line8(TCXState *s1, uint8_t *d, 
			   const uint8_t *s, int width)
{
    int x;
    uint8_t val;

    for(x = 0; x < width; x++) {
	val = *s++;
        *d++ = s1->palette[val];
    }
}

/* Fixed line length 1024 allows us to do nice tricks not possible on
   VGA... */
static void tcx_update_display(void *opaque)
{
    TCXState *ts = opaque;
    ram_addr_t page, page_min, page_max;
    int y, y_start, dd, ds;
    uint8_t *d, *s;
    void (*f)(TCXState *s1, uint8_t *d, const uint8_t *s, int width);

    if (ts->ds->depth == 0)
	return;
    page = ts->vram_offset;
    y_start = -1;
    page_min = 0xffffffff;
    page_max = 0;
    d = ts->ds->data;
    s = ts->vram;
    dd = ts->ds->linesize;
    ds = 1024;

    switch (ts->ds->depth) {
    case 32:
	f = tcx_draw_line32;
	break;
    case 15:
    case 16:
	f = tcx_draw_line16;
	break;
    default:
    case 8:
	f = tcx_draw_line8;
	break;
    case 0:
	return;
    }
    
    for(y = 0; y < ts->height; y += 4, page += TARGET_PAGE_SIZE) {
	if (cpu_physical_memory_get_dirty(page, VGA_DIRTY_FLAG)) {
	    if (y_start < 0)
                y_start = y;
            if (page < page_min)
                page_min = page;
            if (page > page_max)
                page_max = page;
	    f(ts, d, s, ts->width);
	    d += dd;
	    s += ds;
	    f(ts, d, s, ts->width);
	    d += dd;
	    s += ds;
	    f(ts, d, s, ts->width);
	    d += dd;
	    s += ds;
	    f(ts, d, s, ts->width);
	    d += dd;
	    s += ds;
	} else {
            if (y_start >= 0) {
                /* flush to display */
                dpy_update(ts->ds, 0, y_start, 
                           ts->width, y - y_start);
                y_start = -1;
            }
	    d += dd * 4;
	    s += ds * 4;
	}
    }
    if (y_start >= 0) {
	/* flush to display */
	dpy_update(ts->ds, 0, y_start, 
		   ts->width, y - y_start);
    }
    /* reset modified pages */
    if (page_min <= page_max) {
        cpu_physical_memory_reset_dirty(page_min, page_max + TARGET_PAGE_SIZE,
                                        VGA_DIRTY_FLAG);
    }
}

static void tcx_invalidate_display(void *opaque)
{
    TCXState *s = opaque;
    int i;

    for (i = 0; i < MAXX*MAXY; i += TARGET_PAGE_SIZE) {
	cpu_physical_memory_set_dirty(s->vram_offset + i);
    }
}

static void tcx_save(QEMUFile *f, void *opaque)
{
    TCXState *s = opaque;
    
    qemu_put_be32s(f, (uint32_t *)&s->addr);
    qemu_put_be32s(f, (uint32_t *)&s->vram);
    qemu_put_be16s(f, (uint16_t *)&s->height);
    qemu_put_be16s(f, (uint16_t *)&s->width);
    qemu_put_buffer(f, s->r, 256);
    qemu_put_buffer(f, s->g, 256);
    qemu_put_buffer(f, s->b, 256);
    qemu_put_8s(f, &s->dac_index);
    qemu_put_8s(f, &s->dac_state);
}

static int tcx_load(QEMUFile *f, void *opaque, int version_id)
{
    TCXState *s = opaque;
    
    if (version_id != 1)
        return -EINVAL;

    qemu_get_be32s(f, (uint32_t *)&s->addr);
    qemu_get_be32s(f, (uint32_t *)&s->vram);
    qemu_get_be16s(f, (uint16_t *)&s->height);
    qemu_get_be16s(f, (uint16_t *)&s->width);
    qemu_get_buffer(f, s->r, 256);
    qemu_get_buffer(f, s->g, 256);
    qemu_get_buffer(f, s->b, 256);
    qemu_get_8s(f, &s->dac_index);
    qemu_get_8s(f, &s->dac_state);
    update_palette_entries(s, 0, 256);
    return 0;
}

static void tcx_reset(void *opaque)
{
    TCXState *s = opaque;

    /* Initialize palette */
    memset(s->r, 0, 256);
    memset(s->g, 0, 256);
    memset(s->b, 0, 256);
    s->r[255] = s->g[255] = s->b[255] = 255;
    update_palette_entries(s, 0, 256);
    memset(s->vram, 0, MAXX*MAXY);
    cpu_physical_memory_reset_dirty(s->vram_offset, s->vram_offset + MAXX*MAXY,
                                    VGA_DIRTY_FLAG);
    s->dac_index = 0;
    s->dac_state = 0;
}

static uint32_t tcx_dac_readl(void *opaque, target_phys_addr_t addr)
{
    return 0;
}

static void tcx_dac_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    TCXState *s = opaque;
    uint32_t saddr;

    saddr = (addr & (TCX_DAC_NREGS - 1)) >> 2;
    switch (saddr) {
    case 0:
	s->dac_index = val >> 24;
	s->dac_state = 0;
	break;
    case 1:
	switch (s->dac_state) {
	case 0:
	    s->r[s->dac_index] = val >> 24;
            update_palette_entries(s, s->dac_index, s->dac_index + 1);
	    s->dac_state++;
	    break;
	case 1:
	    s->g[s->dac_index] = val >> 24;
            update_palette_entries(s, s->dac_index, s->dac_index + 1);
	    s->dac_state++;
	    break;
	case 2:
	    s->b[s->dac_index] = val >> 24;
            update_palette_entries(s, s->dac_index, s->dac_index + 1);
	default:
	    s->dac_state = 0;
	    break;
	}
	break;
    default:
	break;
    }
    return;
}

static CPUReadMemoryFunc *tcx_dac_read[3] = {
    tcx_dac_readl,
    tcx_dac_readl,
    tcx_dac_readl,
};

static CPUWriteMemoryFunc *tcx_dac_write[3] = {
    tcx_dac_writel,
    tcx_dac_writel,
    tcx_dac_writel,
};

void tcx_init(DisplayState *ds, uint32_t addr, uint8_t *vram_base,
	      unsigned long vram_offset, int vram_size, int width, int height)
{
    TCXState *s;
    int io_memory;

    s = qemu_mallocz(sizeof(TCXState));
    if (!s)
        return;
    s->ds = ds;
    s->addr = addr;
    s->vram = vram_base;
    s->vram_offset = vram_offset;
    s->width = width;
    s->height = height;

    cpu_register_physical_memory(addr + 0x800000, vram_size, vram_offset);
    io_memory = cpu_register_io_memory(0, tcx_dac_read, tcx_dac_write, s);
    cpu_register_physical_memory(addr + 0x200000, TCX_DAC_NREGS, io_memory);

    graphic_console_init(s->ds, tcx_update_display, tcx_invalidate_display,
                         tcx_screen_dump, s);
    register_savevm("tcx", addr, 1, tcx_save, tcx_load, s);
    qemu_register_reset(tcx_reset, s);
    tcx_reset(s);
    dpy_resize(s->ds, width, height);
}

static void tcx_screen_dump(void *opaque, const char *filename)
{
    TCXState *s = opaque;
    FILE *f;
    uint8_t *d, *d1, v;
    int y, x;

    f = fopen(filename, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n%d %d\n%d\n", s->width, s->height, 255);
    d1 = s->vram;
    for(y = 0; y < s->height; y++) {
        d = d1;
        for(x = 0; x < s->width; x++) {
            v = *d;
            fputc(s->r[v], f);
            fputc(s->g[v], f);
            fputc(s->b[v], f);
            d++;
        }
        d1 += MAXX;
    }
    fclose(f);
    return;
}



