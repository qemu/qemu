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
#define XSZ (8*80)
#define YSZ (24*11)
#define XOFF (MAXX-XSZ)
#define YOFF (MAXY-YSZ)

typedef struct TCXState {
    uint32_t addr;
    DisplayState *ds;
    uint8_t *vram;
} TCXState;

static TCXState *ts;

void vga_update_display()
{
    dpy_update(ts->ds, 0, 0, XSZ, YSZ);
}

void vga_invalidate_display() {}

static uint32_t tcx_mem_readb(void *opaque, target_phys_addr_t addr)
{
    TCXState *s = opaque;
    uint32_t saddr;
    unsigned int x, y;

    saddr = addr - s->addr - YOFF*MAXX - XOFF;
    y = saddr / MAXX;
    x = saddr - y * MAXX;
    if (x < XSZ && y < YSZ) {
	return s->vram[y * XSZ + x];
    }
    return 0;
}

static uint32_t tcx_mem_readw(void *opaque, target_phys_addr_t addr)
{
    uint32_t v;
#ifdef TARGET_WORDS_BIGENDIAN
    v = tcx_mem_readb(opaque, addr) << 8;
    v |= tcx_mem_readb(opaque, addr + 1);
#else
    v = tcx_mem_readb(opaque, addr);
    v |= tcx_mem_readb(opaque, addr + 1) << 8;
#endif
    return v;
}

static uint32_t tcx_mem_readl(void *opaque, target_phys_addr_t addr)
{
    uint32_t v;
#ifdef TARGET_WORDS_BIGENDIAN
    v = tcx_mem_readb(opaque, addr) << 24;
    v |= tcx_mem_readb(opaque, addr + 1) << 16;
    v |= tcx_mem_readb(opaque, addr + 2) << 8;
    v |= tcx_mem_readb(opaque, addr + 3);
#else
    v = tcx_mem_readb(opaque, addr);
    v |= tcx_mem_readb(opaque, addr + 1) << 8;
    v |= tcx_mem_readb(opaque, addr + 2) << 16;
    v |= tcx_mem_readb(opaque, addr + 3) << 24;
#endif
    return v;
}

static void tcx_mem_writeb(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    TCXState *s = opaque;
    uint32_t saddr;
    unsigned int x, y;
    char *sptr;

    saddr = addr - s->addr - YOFF*MAXX - XOFF;
    y = saddr / MAXX;
    x = saddr - y * MAXX;
    if (x < XSZ && y < YSZ) {
	sptr = 	s->ds->data;
	if (sptr) {
	    if (s->ds->depth == 24 || s->ds->depth == 32) {
		/* XXX need to do CLUT translation */
		sptr[y * s->ds->linesize + x*4] = val & 0xff;
		sptr[y * s->ds->linesize + x*4+1] = val & 0xff;
		sptr[y * s->ds->linesize + x*4+2] = val & 0xff;
	    }
	    else if (s->ds->depth == 8) {
		sptr[y * s->ds->linesize + x] = val & 0xff;
	    }
	}
	cpu_physical_memory_set_dirty(addr);
	s->vram[y * XSZ + x] = val & 0xff;
    }
}

static void tcx_mem_writew(void *opaque, target_phys_addr_t addr, uint32_t val)
{
#ifdef TARGET_WORDS_BIGENDIAN
    tcx_mem_writeb(opaque, addr, (val >> 8) & 0xff);
    tcx_mem_writeb(opaque, addr + 1, val & 0xff);
#else
    tcx_mem_writeb(opaque, addr, val & 0xff);
    tcx_mem_writeb(opaque, addr + 1, (val >> 8) & 0xff);
#endif
}

static void tcx_mem_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
#ifdef TARGET_WORDS_BIGENDIAN
    tcx_mem_writeb(opaque, addr, (val >> 24) & 0xff);
    tcx_mem_writeb(opaque, addr + 1, (val >> 16) & 0xff);
    tcx_mem_writeb(opaque, addr + 2, (val >> 8) & 0xff);
    tcx_mem_writeb(opaque, addr + 3, val & 0xff);
#else
    tcx_mem_writeb(opaque, addr, val & 0xff);
    tcx_mem_writeb(opaque, addr + 1, (val >> 8) & 0xff);
    tcx_mem_writeb(opaque, addr + 2, (val >> 16) & 0xff);
    tcx_mem_writeb(opaque, addr + 3, (val >> 24) & 0xff);
#endif
}

static CPUReadMemoryFunc *tcx_mem_read[3] = {
    tcx_mem_readb,
    tcx_mem_readw,
    tcx_mem_readl,
};

static CPUWriteMemoryFunc *tcx_mem_write[3] = {
    tcx_mem_writeb,
    tcx_mem_writew,
    tcx_mem_writel,
};

void tcx_init(DisplayState *ds, uint32_t addr)
{
    TCXState *s;
    int tcx_io_memory;

    s = qemu_mallocz(sizeof(TCXState));
    if (!s)
        return;
    s->ds = ds;
    s->addr = addr;
    ts = s;
    tcx_io_memory = cpu_register_io_memory(0, tcx_mem_read, tcx_mem_write, s);
    cpu_register_physical_memory(addr, 0x100000, 
                                 tcx_io_memory);
    s->vram = qemu_mallocz(XSZ*YSZ);
    dpy_resize(s->ds, XSZ, YSZ);
}

void vga_screen_dump(const char *filename)
{
    TCXState *s = ts;
    FILE *f;
    uint8_t *d, *d1;
    unsigned int v;
    int y, x;

    f = fopen(filename, "wb");
    if (!f)
        return -1;
    fprintf(f, "P6\n%d %d\n%d\n",
            XSZ, YSZ, 255);
    d1 = s->vram;
    for(y = 0; y < YSZ; y++) {
        d = d1;
        for(x = 0; x < XSZ; x++) {
            v = *d;
            fputc((v) & 0xff, f);
            fputc((v) & 0xff, f);
            fputc((v) & 0xff, f);
            d++;
        }
        d1 += XSZ;
    }
    fclose(f);
    return;
}



