/*
 * QEMU G364 framebuffer Emulator.
 *
 * Copyright (c) 2007-2009 Herve Poussineau
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw.h"
#include "mips.h"
#include "console.h"
#include "pixel_ops.h"

//#define DEBUG_G364

#ifdef DEBUG_G364
#define DPRINTF(fmt, ...) \
do { printf("g364: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do {} while (0)
#endif
#define BADF(fmt, ...) \
do { fprintf(stderr, "g364 ERROR: " fmt , ## __VA_ARGS__);} while (0)

typedef struct G364State {
    /* hardware */
    uint8_t *vram;
    ram_addr_t vram_offset;
    int vram_size;
    qemu_irq irq;
    /* registers */
    uint8_t color_palette[256][3];
    uint8_t cursor_palette[3][3];
    uint16_t cursor[512];
    uint32_t cursor_position;
    uint32_t ctla;
    uint32_t top_of_screen;
    uint32_t width, height; /* in pixels */
    /* display refresh support */
    DisplayState *ds;
    int depth;
    int blanked;
} G364State;

#define REG_ID       0x000000
#define REG_BOOT     0x080000
#define REG_DISPLAY  0x080118
#define REG_VDISPLAY 0x080150
#define REG_CTLA     0x080300
#define REG_TOP      0x080400
#define REG_CURS_PAL 0x080508
#define REG_CURS_POS 0x080638
#define REG_CLR_PAL  0x080800
#define REG_CURS_PAT 0x081000
#define REG_RESET    0x180000

#define CTLA_FORCE_BLANK 0x00000400
#define CTLA_NO_CURSOR   0x00800000

static inline int check_dirty(ram_addr_t page)
{
    return cpu_physical_memory_get_dirty(page, VGA_DIRTY_FLAG);
}

static inline void reset_dirty(G364State *s,
                               ram_addr_t page_min, ram_addr_t page_max)
{
    cpu_physical_memory_reset_dirty(page_min, page_max + TARGET_PAGE_SIZE - 1,
                                    VGA_DIRTY_FLAG);
}

static void g364fb_draw_graphic8(G364State *s)
{
    int i, w;
    uint8_t *vram;
    uint8_t *data_display, *dd;
    ram_addr_t page, page_min, page_max;
    int x, y;
    int xmin, xmax;
    int ymin, ymax;
    int xcursor, ycursor;
    unsigned int (*rgb_to_pixel)(unsigned int r, unsigned int g, unsigned int b);

    switch (ds_get_bits_per_pixel(s->ds)) {
        case 8:
            rgb_to_pixel = rgb_to_pixel8;
            w = 1;
            break;
        case 15:
            rgb_to_pixel = rgb_to_pixel15;
            w = 2;
            break;
        case 16:
            rgb_to_pixel = rgb_to_pixel16;
            w = 2;
            break;
        case 32:
            rgb_to_pixel = rgb_to_pixel32;
            w = 4;
            break;
        default:
            BADF("unknown host depth %d\n", ds_get_bits_per_pixel(s->ds));
            return;
    }

    page = s->vram_offset;
    page_min = (ram_addr_t)-1;
    page_max = 0;

    x = y = 0;
    xmin = s->width;
    xmax = 0;
    ymin = s->height;
    ymax = 0;

    if (!(s->ctla & CTLA_NO_CURSOR)) {
        xcursor = s->cursor_position >> 12;
        ycursor = s->cursor_position & 0xfff;
    } else {
        xcursor = ycursor = -65;
    }

    vram = s->vram + s->top_of_screen;
    /* XXX: out of range in vram? */
    data_display = dd = ds_get_data(s->ds);
    while (y < s->height) {
        if (check_dirty(page)) {
            if (y < ymin)
                ymin = ymax = y;
            if (page_min == (ram_addr_t)-1)
                page_min = page;
            page_max = page;
            if (x < xmin)
                xmin = x;
            for (i = 0; i < TARGET_PAGE_SIZE; i++) {
                uint8_t index;
                unsigned int color;
                if (unlikely((y >= ycursor && y < ycursor + 64) &&
                    (x >= xcursor && x < xcursor + 64))) {
                    /* pointer area */
                    int xdiff = x - xcursor;
                    uint16_t curs = s->cursor[(y - ycursor) * 8 + xdiff / 8];
                    int op = (curs >> ((xdiff & 7) * 2)) & 3;
                    if (likely(op == 0)) {
                        /* transparent */
                        index = *vram;
                        color = (*rgb_to_pixel)(
                            s->color_palette[index][0],
                            s->color_palette[index][1],
                            s->color_palette[index][2]);
                    } else {
                        /* get cursor color */
                        index = op - 1;
                        color = (*rgb_to_pixel)(
                            s->cursor_palette[index][0],
                            s->cursor_palette[index][1],
                            s->cursor_palette[index][2]);
                    }
                } else {
                    /* normal area */
                    index = *vram;
                    color = (*rgb_to_pixel)(
                        s->color_palette[index][0],
                        s->color_palette[index][1],
                        s->color_palette[index][2]);
                }
                memcpy(dd, &color, w);
                dd += w;
                x++;
                vram++;
                if (x == s->width) {
                    xmax = s->width - 1;
                    y++;
                    if (y == s->height) {
                        ymax = s->height - 1;
                        goto done;
                    }
                    data_display = dd = data_display + ds_get_linesize(s->ds);
                    xmin = 0;
                    x = 0;
                }
            }
            if (x > xmax)
                xmax = x;
            if (y > ymax)
                ymax = y;
        } else {
            int dy;
            if (page_min != (ram_addr_t)-1) {
                reset_dirty(s, page_min, page_max);
                page_min = (ram_addr_t)-1;
                page_max = 0;
                dpy_update(s->ds, xmin, ymin, xmax - xmin + 1, ymax - ymin + 1);
                xmin = s->width;
                xmax = 0;
                ymin = s->height;
                ymax = 0;
            }
            x += TARGET_PAGE_SIZE;
            dy = x / s->width;
            x = x % s->width;
            y += dy;
            vram += TARGET_PAGE_SIZE;
            data_display += dy * ds_get_linesize(s->ds);
            dd = data_display + x * w;
        }
        page += TARGET_PAGE_SIZE;
    }

done:
    if (page_min != (ram_addr_t)-1) {
        dpy_update(s->ds, xmin, ymin, xmax - xmin + 1, ymax - ymin + 1);
        reset_dirty(s, page_min, page_max);
    }
}

static void g364fb_draw_blank(G364State *s)
{
    int i, w;
    uint8_t *d;

    if (s->blanked) {
        /* Screen is already blank. No need to redraw it */
        return;
    }

    w = s->width * ((ds_get_bits_per_pixel(s->ds) + 7) >> 3);
    d = ds_get_data(s->ds);
    for (i = 0; i < s->height; i++) {
        memset(d, 0, w);
        d += ds_get_linesize(s->ds);
    }

    dpy_update(s->ds, 0, 0, s->width, s->height);
    s->blanked = 1;
}

static void g364fb_update_display(void *opaque)
{
    G364State *s = opaque;

    if (s->width == 0 || s->height == 0)
        return;

    if (s->width != ds_get_width(s->ds) || s->height != ds_get_height(s->ds)) {
        qemu_console_resize(s->ds, s->width, s->height);
    }

    if (s->ctla & CTLA_FORCE_BLANK) {
        g364fb_draw_blank(s);
    } else if (s->depth == 8) {
        g364fb_draw_graphic8(s);
    } else {
        BADF("unknown guest depth %d\n", s->depth);
    }

    qemu_irq_raise(s->irq);
}

static inline void g364fb_invalidate_display(void *opaque)
{
    G364State *s = opaque;
    int i;

    s->blanked = 0;
    for (i = 0; i < s->vram_size; i += TARGET_PAGE_SIZE) {
        cpu_physical_memory_set_dirty(s->vram_offset + i);
    }
}

static void g364fb_reset(void *opaque)
{
    G364State *s = opaque;
    qemu_irq_lower(s->irq);

    memset(s->color_palette, 0, sizeof(s->color_palette));
    memset(s->cursor_palette, 0, sizeof(s->cursor_palette));
    memset(s->cursor, 0, sizeof(s->cursor));
    s->cursor_position = 0;
    s->ctla = 0;
    s->top_of_screen = 0;
    s->width = s->height = 0;
    memset(s->vram, 0, s->vram_size);
    g364fb_invalidate_display(opaque);
}

static void g364fb_screen_dump(void *opaque, const char *filename)
{
    G364State *s = opaque;
    int y, x;
    uint8_t index;
    uint8_t *data_buffer;
    FILE *f;

    if (s->depth != 8) {
        BADF("unknown guest depth %d\n", s->depth);
        return;
    }

    f = fopen(filename, "wb");
    if (!f)
        return;

    if (s->ctla & CTLA_FORCE_BLANK) {
        /* blank screen */
        fprintf(f, "P4\n%d %d\n",
            s->width, s->height);
        for (y = 0; y < s->height; y++)
            for (x = 0; x < s->width; x++)
                fputc(0, f);
    } else {
        data_buffer = s->vram + s->top_of_screen;
        fprintf(f, "P6\n%d %d\n%d\n",
            s->width, s->height, 255);
        for (y = 0; y < s->height; y++)
            for (x = 0; x < s->width; x++, data_buffer++) {
                index = *data_buffer;
                fputc(s->color_palette[index][0], f);
                fputc(s->color_palette[index][1], f);
                fputc(s->color_palette[index][2], f);
        }
    }

    fclose(f);
}

/* called for accesses to io ports */
static uint32_t g364fb_ctrl_readl(void *opaque, target_phys_addr_t addr)
{
    G364State *s = opaque;
    uint32_t val;

    if (addr >= REG_CURS_PAT && addr < REG_CURS_PAT + 0x1000) {
        /* cursor pattern */
        int idx = (addr - REG_CURS_PAT) >> 3;
        val = s->cursor[idx];
    } else if (addr >= REG_CURS_PAL && addr < REG_CURS_PAL + 0x18) {
        /* cursor palette */
        int idx = (addr - REG_CURS_PAL) >> 3;
        val = ((uint32_t)s->cursor_palette[idx][0] << 16);
        val |= ((uint32_t)s->cursor_palette[idx][1] << 8);
        val |= ((uint32_t)s->cursor_palette[idx][2] << 0);
    } else {
        switch (addr) {
            case REG_ID:
                val = 0x10; /* Mips G364 */
                break;
            case REG_DISPLAY:
                val = s->width / 4;
                break;
            case REG_VDISPLAY:
                val = s->height * 2;
                break;
            case REG_CTLA:
                val = s->ctla;
                break;
            default:
            {
                BADF("invalid read at [" TARGET_FMT_plx "]\n", addr);
                val = 0;
                break;
            }
        }
    }

    DPRINTF("read 0x%08x at [" TARGET_FMT_plx "]\n", val, addr);

    return val;
}

static uint32_t g364fb_ctrl_readw(void *opaque, target_phys_addr_t addr)
{
    uint32_t v = g364fb_ctrl_readl(opaque, addr & ~0x3);
    if (addr & 0x2)
        return v >> 16;
    else
        return v & 0xffff;
}

static uint32_t g364fb_ctrl_readb(void *opaque, target_phys_addr_t addr)
{
    uint32_t v = g364fb_ctrl_readl(opaque, addr & ~0x3);
    return (v >> (8 * (addr & 0x3))) & 0xff;
}

static void g364fb_update_depth(G364State *s)
{
    static const int depths[8] = { 1, 2, 4, 8, 15, 16, 0 };
    s->depth = depths[(s->ctla & 0x00700000) >> 20];
}

static void g364_invalidate_cursor_position(G364State *s)
{
    int ymin, ymax, start, end, i;

    /* invalidate only near the cursor */
    ymin = s->cursor_position & 0xfff;
    ymax = MIN(s->height, ymin + 64);
    start = ymin * ds_get_linesize(s->ds);
    end = (ymax + 1) * ds_get_linesize(s->ds);

    for (i = start; i < end; i += TARGET_PAGE_SIZE) {
        cpu_physical_memory_set_dirty(s->vram_offset + i);
    }
}

static void g364fb_ctrl_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    G364State *s = opaque;

    DPRINTF("write 0x%08x at [" TARGET_FMT_plx "]\n", val, addr);

    if (addr >= REG_CLR_PAL && addr < REG_CLR_PAL + 0x800) {
        /* color palette */
        int idx = (addr - REG_CLR_PAL) >> 3;
        s->color_palette[idx][0] = (val >> 16) & 0xff;
        s->color_palette[idx][1] = (val >> 8) & 0xff;
        s->color_palette[idx][2] = val & 0xff;
        g364fb_invalidate_display(s);
    } else if (addr >= REG_CURS_PAT && addr < REG_CURS_PAT + 0x1000) {
        /* cursor pattern */
        int idx = (addr - REG_CURS_PAT) >> 3;
        s->cursor[idx] = val;
        g364fb_invalidate_display(s);
    } else if (addr >= REG_CURS_PAL && addr < REG_CURS_PAL + 0x18) {
        /* cursor palette */
        int idx = (addr - REG_CURS_PAL) >> 3;
        s->cursor_palette[idx][0] = (val >> 16) & 0xff;
        s->cursor_palette[idx][1] = (val >> 8) & 0xff;
        s->cursor_palette[idx][2] = val & 0xff;
        g364fb_invalidate_display(s);
    } else {
        switch (addr) {
            case REG_ID: /* Card identifier; read-only */
            case REG_BOOT: /* Boot timing */
            case 0x80108: /* Line timing: half sync */
            case 0x80110: /* Line timing: back porch */
            case 0x80120: /* Line timing: short display */
            case 0x80128: /* Frame timing: broad pulse */
            case 0x80130: /* Frame timing: v sync */
            case 0x80138: /* Frame timing: v preequalise */
            case 0x80140: /* Frame timing: v postequalise */
            case 0x80148: /* Frame timing: v blank */
            case 0x80158: /* Line timing: line time */
            case 0x80160: /* Frame store: line start */
            case 0x80168: /* vram cycle: mem init */
            case 0x80170: /* vram cycle: transfer delay */
            case 0x80200: /* vram cycle: mask register */
                /* ignore */
                break;
            case REG_TOP:
                s->top_of_screen = val;
                g364fb_invalidate_display(s);
                break;
            case REG_DISPLAY:
                s->width = val * 4;
                break;
            case REG_VDISPLAY:
                s->height = val / 2;
                break;
            case REG_CTLA:
                s->ctla = val;
                g364fb_update_depth(s);
                g364fb_invalidate_display(s);
                break;
            case REG_CURS_POS:
                g364_invalidate_cursor_position(s);
                s->cursor_position = val;
                g364_invalidate_cursor_position(s);
                break;
            case REG_RESET:
                g364fb_reset(s);
                break;
            default:
                BADF("invalid write of 0x%08x at [" TARGET_FMT_plx "]\n", val, addr);
                break;
        }
    }
    qemu_irq_lower(s->irq);
}

static void g364fb_ctrl_writew(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    uint32_t old_val = g364fb_ctrl_readl(opaque, addr & ~0x3);

    if (addr & 0x2)
        val = (val << 16) | (old_val & 0x0000ffff);
    else
        val = val | (old_val & 0xffff0000);
    g364fb_ctrl_writel(opaque, addr & ~0x3, val);
}

static void g364fb_ctrl_writeb(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    uint32_t old_val = g364fb_ctrl_readl(opaque, addr & ~0x3);

    switch (addr & 3) {
    case 0:
        val = val | (old_val & 0xffffff00);
        break;
    case 1:
        val = (val << 8) | (old_val & 0xffff00ff);
        break;
    case 2:
        val = (val << 16) | (old_val & 0xff00ffff);
        break;
    case 3:
        val = (val << 24) | (old_val & 0x00ffffff);
        break;
    }
    g364fb_ctrl_writel(opaque, addr & ~0x3, val);
}

static CPUReadMemoryFunc * const g364fb_ctrl_read[3] = {
    g364fb_ctrl_readb,
    g364fb_ctrl_readw,
    g364fb_ctrl_readl,
};

static CPUWriteMemoryFunc * const g364fb_ctrl_write[3] = {
    g364fb_ctrl_writeb,
    g364fb_ctrl_writew,
    g364fb_ctrl_writel,
};

static int g364fb_load(QEMUFile *f, void *opaque, int version_id)
{
    G364State *s = opaque;
    unsigned int i, vram_size;

    if (version_id != 1)
        return -EINVAL;

    vram_size = qemu_get_be32(f);
    if (vram_size < s->vram_size)
        return -EINVAL;
    qemu_get_buffer(f, s->vram, s->vram_size);
    for (i = 0; i < 256; i++)
        qemu_get_buffer(f, s->color_palette[i], 3);
    for (i = 0; i < 3; i++)
        qemu_get_buffer(f, s->cursor_palette[i], 3);
    qemu_get_buffer(f, (uint8_t *)s->cursor, sizeof(s->cursor));
    s->cursor_position = qemu_get_be32(f);
    s->ctla = qemu_get_be32(f);
    s->top_of_screen = qemu_get_be32(f);
    s->width = qemu_get_be32(f);
    s->height = qemu_get_be32(f);

    /* force refresh */
    g364fb_update_depth(s);
    g364fb_invalidate_display(s);

    return 0;
}

static void g364fb_save(QEMUFile *f, void *opaque)
{
    G364State *s = opaque;
    int i;

    qemu_put_be32(f, s->vram_size);
    qemu_put_buffer(f, s->vram, s->vram_size);
    for (i = 0; i < 256; i++)
        qemu_put_buffer(f, s->color_palette[i], 3);
    for (i = 0; i < 3; i++)
        qemu_put_buffer(f, s->cursor_palette[i], 3);
    qemu_put_buffer(f, (uint8_t *)s->cursor, sizeof(s->cursor));
    qemu_put_be32(f, s->cursor_position);
    qemu_put_be32(f, s->ctla);
    qemu_put_be32(f, s->top_of_screen);
    qemu_put_be32(f, s->width);
    qemu_put_be32(f, s->height);
}

int g364fb_mm_init(target_phys_addr_t vram_base,
                   target_phys_addr_t ctrl_base, int it_shift,
                   qemu_irq irq)
{
    G364State *s;
    int io_ctrl;

    s = g_malloc0(sizeof(G364State));

    s->vram_size = 8 * 1024 * 1024;
    s->vram_offset = qemu_ram_alloc(NULL, "g364fb.vram", s->vram_size);
    s->vram = qemu_get_ram_ptr(s->vram_offset);
    s->irq = irq;

    qemu_register_reset(g364fb_reset, s);
    register_savevm(NULL, "g364fb", 0, 1, g364fb_save, g364fb_load, s);
    g364fb_reset(s);

    s->ds = graphic_console_init(g364fb_update_display,
                                 g364fb_invalidate_display,
                                 g364fb_screen_dump, NULL, s);

    cpu_register_physical_memory(vram_base, s->vram_size, s->vram_offset);

    io_ctrl = cpu_register_io_memory(g364fb_ctrl_read, g364fb_ctrl_write, s,
                                     DEVICE_NATIVE_ENDIAN);
    cpu_register_physical_memory(ctrl_base, 0x200000, io_ctrl);

    return 0;
}
