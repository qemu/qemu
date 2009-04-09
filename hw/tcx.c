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
#include "hw.h"
#include "sun4m.h"
#include "console.h"
#include "pixel_ops.h"

#define MAXX 1024
#define MAXY 768
#define TCX_DAC_NREGS 16
#define TCX_THC_NREGS_8  0x081c
#define TCX_THC_NREGS_24 0x1000
#define TCX_TEC_NREGS    0x1000

typedef struct TCXState {
    target_phys_addr_t addr;
    DisplayState *ds;
    uint8_t *vram;
    uint32_t *vram24, *cplane;
    ram_addr_t vram_offset, vram24_offset, cplane_offset;
    uint16_t width, height, depth;
    uint8_t r[256], g[256], b[256];
    uint32_t palette[256];
    uint8_t dac_index, dac_state;
} TCXState;

static void tcx_screen_dump(void *opaque, const char *filename);
static void tcx24_screen_dump(void *opaque, const char *filename);
static void tcx_invalidate_display(void *opaque);
static void tcx24_invalidate_display(void *opaque);

static void update_palette_entries(TCXState *s, int start, int end)
{
    int i;
    for(i = start; i < end; i++) {
        switch(ds_get_bits_per_pixel(s->ds)) {
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
            if (is_surface_bgr(s->ds->surface))
                s->palette[i] = rgb_to_pixel32bgr(s->r[i], s->g[i], s->b[i]);
            else
                s->palette[i] = rgb_to_pixel32(s->r[i], s->g[i], s->b[i]);
            break;
        }
    }
    if (s->depth == 24)
        tcx24_invalidate_display(s);
    else
        tcx_invalidate_display(s);
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

/*
  XXX Could be much more optimal:
  * detect if line/page/whole screen is in 24 bit mode
  * if destination is also BGR, use memcpy
  */
static inline void tcx24_draw_line32(TCXState *s1, uint8_t *d,
                                     const uint8_t *s, int width,
                                     const uint32_t *cplane,
                                     const uint32_t *s24)
{
    int x, bgr, r, g, b;
    uint8_t val, *p8;
    uint32_t *p = (uint32_t *)d;
    uint32_t dval;

    bgr = is_surface_bgr(s1->ds->surface);
    for(x = 0; x < width; x++, s++, s24++) {
        if ((be32_to_cpu(*cplane++) & 0xff000000) == 0x03000000) {
            // 24-bit direct, BGR order
            p8 = (uint8_t *)s24;
            p8++;
            b = *p8++;
            g = *p8++;
            r = *p8++;
            if (bgr)
                dval = rgb_to_pixel32bgr(r, g, b);
            else
                dval = rgb_to_pixel32(r, g, b);
        } else {
            val = *s;
            dval = s1->palette[val];
        }
        *p++ = dval;
    }
}

static inline int check_dirty(ram_addr_t page, ram_addr_t page24,
                              ram_addr_t cpage)
{
    int ret;
    unsigned int off;

    ret = cpu_physical_memory_get_dirty(page, VGA_DIRTY_FLAG);
    for (off = 0; off < TARGET_PAGE_SIZE * 4; off += TARGET_PAGE_SIZE) {
        ret |= cpu_physical_memory_get_dirty(page24 + off, VGA_DIRTY_FLAG);
        ret |= cpu_physical_memory_get_dirty(cpage + off, VGA_DIRTY_FLAG);
    }
    return ret;
}

static inline void reset_dirty(TCXState *ts, ram_addr_t page_min,
                               ram_addr_t page_max, ram_addr_t page24,
                              ram_addr_t cpage)
{
    cpu_physical_memory_reset_dirty(page_min, page_max + TARGET_PAGE_SIZE,
                                    VGA_DIRTY_FLAG);
    page_min -= ts->vram_offset;
    page_max -= ts->vram_offset;
    cpu_physical_memory_reset_dirty(page24 + page_min * 4,
                                    page24 + page_max * 4 + TARGET_PAGE_SIZE,
                                    VGA_DIRTY_FLAG);
    cpu_physical_memory_reset_dirty(cpage + page_min * 4,
                                    cpage + page_max * 4 + TARGET_PAGE_SIZE,
                                    VGA_DIRTY_FLAG);
}

/* Fixed line length 1024 allows us to do nice tricks not possible on
   VGA... */
static void tcx_update_display(void *opaque)
{
    TCXState *ts = opaque;
    ram_addr_t page, page_min, page_max;
    int y, y_start, dd, ds;
    uint8_t *d, *s;
    void (*f)(TCXState *s1, uint8_t *dst, const uint8_t *src, int width);

    if (ds_get_bits_per_pixel(ts->ds) == 0)
        return;
    page = ts->vram_offset;
    y_start = -1;
    page_min = 0xffffffff;
    page_max = 0;
    d = ds_get_data(ts->ds);
    s = ts->vram;
    dd = ds_get_linesize(ts->ds);
    ds = 1024;

    switch (ds_get_bits_per_pixel(ts->ds)) {
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

static void tcx24_update_display(void *opaque)
{
    TCXState *ts = opaque;
    ram_addr_t page, page_min, page_max, cpage, page24;
    int y, y_start, dd, ds;
    uint8_t *d, *s;
    uint32_t *cptr, *s24;

    if (ds_get_bits_per_pixel(ts->ds) != 32)
            return;
    page = ts->vram_offset;
    page24 = ts->vram24_offset;
    cpage = ts->cplane_offset;
    y_start = -1;
    page_min = 0xffffffff;
    page_max = 0;
    d = ds_get_data(ts->ds);
    s = ts->vram;
    s24 = ts->vram24;
    cptr = ts->cplane;
    dd = ds_get_linesize(ts->ds);
    ds = 1024;

    for(y = 0; y < ts->height; y += 4, page += TARGET_PAGE_SIZE,
            page24 += TARGET_PAGE_SIZE, cpage += TARGET_PAGE_SIZE) {
        if (check_dirty(page, page24, cpage)) {
            if (y_start < 0)
                y_start = y;
            if (page < page_min)
                page_min = page;
            if (page > page_max)
                page_max = page;
            tcx24_draw_line32(ts, d, s, ts->width, cptr, s24);
            d += dd;
            s += ds;
            cptr += ds;
            s24 += ds;
            tcx24_draw_line32(ts, d, s, ts->width, cptr, s24);
            d += dd;
            s += ds;
            cptr += ds;
            s24 += ds;
            tcx24_draw_line32(ts, d, s, ts->width, cptr, s24);
            d += dd;
            s += ds;
            cptr += ds;
            s24 += ds;
            tcx24_draw_line32(ts, d, s, ts->width, cptr, s24);
            d += dd;
            s += ds;
            cptr += ds;
            s24 += ds;
        } else {
            if (y_start >= 0) {
                /* flush to display */
                dpy_update(ts->ds, 0, y_start,
                           ts->width, y - y_start);
                y_start = -1;
            }
            d += dd * 4;
            s += ds * 4;
            cptr += ds * 4;
            s24 += ds * 4;
        }
    }
    if (y_start >= 0) {
        /* flush to display */
        dpy_update(ts->ds, 0, y_start,
                   ts->width, y - y_start);
    }
    /* reset modified pages */
    if (page_min <= page_max) {
        reset_dirty(ts, page_min, page_max, page24, cpage);
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

static void tcx24_invalidate_display(void *opaque)
{
    TCXState *s = opaque;
    int i;

    tcx_invalidate_display(s);
    for (i = 0; i < MAXX*MAXY * 4; i += TARGET_PAGE_SIZE) {
        cpu_physical_memory_set_dirty(s->vram24_offset + i);
        cpu_physical_memory_set_dirty(s->cplane_offset + i);
    }
}

static void tcx_save(QEMUFile *f, void *opaque)
{
    TCXState *s = opaque;

    qemu_put_be16s(f, &s->height);
    qemu_put_be16s(f, &s->width);
    qemu_put_be16s(f, &s->depth);
    qemu_put_buffer(f, s->r, 256);
    qemu_put_buffer(f, s->g, 256);
    qemu_put_buffer(f, s->b, 256);
    qemu_put_8s(f, &s->dac_index);
    qemu_put_8s(f, &s->dac_state);
}

static int tcx_load(QEMUFile *f, void *opaque, int version_id)
{
    TCXState *s = opaque;
    uint32_t dummy;

    if (version_id != 3 && version_id != 4)
        return -EINVAL;

    if (version_id == 3) {
        qemu_get_be32s(f, &dummy);
        qemu_get_be32s(f, &dummy);
        qemu_get_be32s(f, &dummy);
    }
    qemu_get_be16s(f, &s->height);
    qemu_get_be16s(f, &s->width);
    qemu_get_be16s(f, &s->depth);
    qemu_get_buffer(f, s->r, 256);
    qemu_get_buffer(f, s->g, 256);
    qemu_get_buffer(f, s->b, 256);
    qemu_get_8s(f, &s->dac_index);
    qemu_get_8s(f, &s->dac_state);
    update_palette_entries(s, 0, 256);
    if (s->depth == 24)
        tcx24_invalidate_display(s);
    else
        tcx_invalidate_display(s);

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
    cpu_physical_memory_reset_dirty(s->vram_offset, s->vram_offset +
                                    MAXX * MAXY * (1 + 4 + 4), VGA_DIRTY_FLAG);
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

    switch (addr) {
    case 0:
        s->dac_index = val >> 24;
        s->dac_state = 0;
        break;
    case 4:
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
            s->dac_index = (s->dac_index + 1) & 255; // Index autoincrement
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
    NULL,
    NULL,
    tcx_dac_readl,
};

static CPUWriteMemoryFunc *tcx_dac_write[3] = {
    NULL,
    NULL,
    tcx_dac_writel,
};

static uint32_t tcx_dummy_readl(void *opaque, target_phys_addr_t addr)
{
    return 0;
}

static void tcx_dummy_writel(void *opaque, target_phys_addr_t addr,
                             uint32_t val)
{
}

static CPUReadMemoryFunc *tcx_dummy_read[3] = {
    NULL,
    NULL,
    tcx_dummy_readl,
};

static CPUWriteMemoryFunc *tcx_dummy_write[3] = {
    NULL,
    NULL,
    tcx_dummy_writel,
};

void tcx_init(target_phys_addr_t addr, int vram_size, int width, int height,
              int depth)
{
    TCXState *s;
    int io_memory, dummy_memory;
    ram_addr_t vram_offset;
    int size;
    uint8_t *vram_base;

    vram_offset = qemu_ram_alloc(vram_size);
    vram_base = qemu_get_ram_ptr(vram_offset);

    s = qemu_mallocz(sizeof(TCXState));
    s->addr = addr;
    s->vram_offset = vram_offset;
    s->width = width;
    s->height = height;
    s->depth = depth;

    // 8-bit plane
    s->vram = vram_base;
    size = vram_size;
    cpu_register_physical_memory(addr + 0x00800000ULL, size, vram_offset);
    vram_offset += size;
    vram_base += size;

    io_memory = cpu_register_io_memory(0, tcx_dac_read, tcx_dac_write, s);
    cpu_register_physical_memory(addr + 0x00200000ULL, TCX_DAC_NREGS,
                                 io_memory);

    dummy_memory = cpu_register_io_memory(0, tcx_dummy_read, tcx_dummy_write,
                                          s);
    cpu_register_physical_memory(addr + 0x00700000ULL, TCX_TEC_NREGS,
                                 dummy_memory);
    if (depth == 24) {
        // 24-bit plane
        size = vram_size * 4;
        s->vram24 = (uint32_t *)vram_base;
        s->vram24_offset = vram_offset;
        cpu_register_physical_memory(addr + 0x02000000ULL, size, vram_offset);
        vram_offset += size;
        vram_base += size;

        // Control plane
        size = vram_size * 4;
        s->cplane = (uint32_t *)vram_base;
        s->cplane_offset = vram_offset;
        cpu_register_physical_memory(addr + 0x0a000000ULL, size, vram_offset);
        s->ds = graphic_console_init(tcx24_update_display,
                                     tcx24_invalidate_display,
                                     tcx24_screen_dump, NULL, s);
    } else {
        cpu_register_physical_memory(addr + 0x00300000ULL, TCX_THC_NREGS_8,
                                     dummy_memory);
        s->ds = graphic_console_init(tcx_update_display,
                                     tcx_invalidate_display,
                                     tcx_screen_dump, NULL, s);
    }
    // NetBSD writes here even with 8-bit display
    cpu_register_physical_memory(addr + 0x00301000ULL, TCX_THC_NREGS_24,
                                 dummy_memory);

    register_savevm("tcx", addr, 4, tcx_save, tcx_load, s);
    qemu_register_reset(tcx_reset, s);
    tcx_reset(s);
    qemu_console_resize(s->ds, width, height);
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

static void tcx24_screen_dump(void *opaque, const char *filename)
{
    TCXState *s = opaque;
    FILE *f;
    uint8_t *d, *d1, v;
    uint32_t *s24, *cptr, dval;
    int y, x;

    f = fopen(filename, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n%d %d\n%d\n", s->width, s->height, 255);
    d1 = s->vram;
    s24 = s->vram24;
    cptr = s->cplane;
    for(y = 0; y < s->height; y++) {
        d = d1;
        for(x = 0; x < s->width; x++, d++, s24++) {
            if ((*cptr++ & 0xff000000) == 0x03000000) { // 24-bit direct
                dval = *s24 & 0x00ffffff;
                fputc((dval >> 16) & 0xff, f);
                fputc((dval >> 8) & 0xff, f);
                fputc(dval & 0xff, f);
            } else {
                v = *d;
                fputc(s->r[v], f);
                fputc(s->g[v], f);
                fputc(s->b[v], f);
            }
        }
        d1 += MAXX;
    }
    fclose(f);
    return;
}
