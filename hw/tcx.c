/*
 * QEMU Sun4m System Emulator
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
#include "vl.h"

#define MAXX 1024
#define MAXY 768
/*
 * Proll uses only small part of display, we need to switch to full
 * display when we get linux framebuffer console or X11 running. For
 * now it's just slower and awkward.
*/
#if 1
#define XSZ (8*80)
#define YSZ (24*11)
#define XOFF (MAXX-XSZ)
#define YOFF (MAXY-YSZ)
#else
#define XSZ MAXX
#define YSZ MAXY
#define XOFF 0
#define YOFF 0
#endif

typedef struct TCXState {
    uint32_t addr;
    DisplayState *ds;
    uint8_t *vram;
    unsigned long vram_offset;
    uint8_t r[256], g[256], b[256];
} TCXState;

static void tcx_draw_line32(TCXState *s1, uint8_t *d, 
			    const uint8_t *s, int width)
{
    int x;
    uint8_t val;

    for(x = 0; x < width; x++) {
	val = *s++;
	*d++ = s1->r[val];
	*d++ = s1->g[val];
	*d++ = s1->b[val];
	d++;
    }
}

static void tcx_draw_line24(TCXState *s1, uint8_t *d, 
			    const uint8_t *s, int width)
{
    int x;
    uint8_t val;

    for(x = 0; x < width; x++) {
	val = *s++;
	*d++ = s1->r[val];
	*d++ = s1->g[val];
	*d++ = s1->b[val];
    }
}

static void tcx_draw_line8(TCXState *s1, uint8_t *d, 
			   const uint8_t *s, int width)
{
    int x;
    uint8_t val;

    for(x = 0; x < width; x++) {
	val = *s++;
	/* XXX translate between palettes? */
	*d++ = val;
    }
}

/* Fixed line length 1024 allows us to do nice tricks not possible on
   VGA... */
void tcx_update_display(void *opaque)
{
    TCXState *ts = opaque;
    uint32_t page;
    int y, page_min, page_max, y_start, dd, ds;
    uint8_t *d, *s;
    void (*f)(TCXState *s1, uint8_t *d, const uint8_t *s, int width);

    if (ts->ds->depth == 0)
	return;
    page = ts->vram_offset + YOFF*MAXX;
    y_start = -1;
    page_min = 0x7fffffff;
    page_max = -1;
    d = ts->ds->data;
    s = ts->vram + YOFF*MAXX + XOFF;
    dd = ts->ds->linesize;
    ds = 1024;

    switch (ts->ds->depth) {
    case 32:
	f = tcx_draw_line32;
	break;
    case 24:
	f = tcx_draw_line24;
	break;
    default:
    case 8:
	f = tcx_draw_line8;
	break;
    case 0:
	return;
    }
    
    for(y = 0; y < YSZ; y += 4, page += TARGET_PAGE_SIZE) {
	if (cpu_physical_memory_is_dirty(page)) {
	    if (y_start < 0)
                y_start = y;
            if (page < page_min)
                page_min = page;
            if (page > page_max)
                page_max = page;
	    f(ts, d, s, XSZ);
	    d += dd;
	    s += ds;
	    f(ts, d, s, XSZ);
	    d += dd;
	    s += ds;
	    f(ts, d, s, XSZ);
	    d += dd;
	    s += ds;
	    f(ts, d, s, XSZ);
	    d += dd;
	    s += ds;
	} else {
            if (y_start >= 0) {
                /* flush to display */
                dpy_update(ts->ds, 0, y_start, 
                           XSZ, y - y_start);
                y_start = -1;
            }
	    d += dd * 4;
	    s += ds * 4;
	}
    }
    if (y_start >= 0) {
	/* flush to display */
	dpy_update(ts->ds, 0, y_start, 
		   XSZ, y - y_start);
    }
    /* reset modified pages */
    if (page_max != -1) {
        cpu_physical_memory_reset_dirty(page_min, page_max + TARGET_PAGE_SIZE);
    }
}

void tcx_invalidate_display(void *opaque)
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
    qemu_put_buffer(f, s->r, 256);
    qemu_put_buffer(f, s->g, 256);
    qemu_put_buffer(f, s->b, 256);
}

static int tcx_load(QEMUFile *f, void *opaque, int version_id)
{
    TCXState *s = opaque;
    
    if (version_id != 1)
        return -EINVAL;

    qemu_get_be32s(f, (uint32_t *)&s->addr);
    qemu_get_be32s(f, (uint32_t *)&s->vram);
    qemu_get_buffer(f, s->r, 256);
    qemu_get_buffer(f, s->g, 256);
    qemu_get_buffer(f, s->b, 256);
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
    memset(s->vram, 0, MAXX*MAXY);
    cpu_physical_memory_reset_dirty(s->vram_offset, s->vram_offset + MAXX*MAXY);
}

void *tcx_init(DisplayState *ds, uint32_t addr, uint8_t *vram_base,
	      unsigned long vram_offset, int vram_size)
{
    TCXState *s;

    s = qemu_mallocz(sizeof(TCXState));
    if (!s)
        return NULL;
    s->ds = ds;
    s->addr = addr;
    s->vram = vram_base;
    s->vram_offset = vram_offset;

    cpu_register_physical_memory(addr, vram_size, vram_offset);

    register_savevm("tcx", addr, 1, tcx_save, tcx_load, s);
    qemu_register_reset(tcx_reset, s);
    tcx_reset(s);
    dpy_resize(s->ds, XSZ, YSZ);
    return s;
}

void tcx_screen_dump(void *opaque, const char *filename)
{
    TCXState *s = opaque;
    FILE *f;
    uint8_t *d, *d1, v;
    int y, x;

    f = fopen(filename, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n%d %d\n%d\n", XSZ, YSZ, 255);
    d1 = s->vram + YOFF*MAXX + XOFF;
    for(y = 0; y < YSZ; y++) {
        d = d1;
        for(x = 0; x < XSZ; x++) {
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



