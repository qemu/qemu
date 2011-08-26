/*
 * QEMU G364 framebuffer Emulator.
 *
 * Copyright (c) 2007-2011 Herve Poussineau
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
#include "console.h"
#include "pixel_ops.h"
#include "trace.h"
#include "sysbus.h"

typedef struct G364State {
    /* hardware */
    uint8_t *vram;
    uint32_t vram_size;
    qemu_irq irq;
    MemoryRegion mem_vram;
    MemoryRegion mem_ctrl;
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

#define REG_BOOT     0x000000
#define REG_DISPLAY  0x000118
#define REG_VDISPLAY 0x000150
#define REG_CTLA     0x000300
#define REG_TOP      0x000400
#define REG_CURS_PAL 0x000508
#define REG_CURS_POS 0x000638
#define REG_CLR_PAL  0x000800
#define REG_CURS_PAT 0x001000
#define REG_RESET    0x100000

#define CTLA_FORCE_BLANK 0x00000400
#define CTLA_NO_CURSOR   0x00800000

static inline int check_dirty(G364State *s, ram_addr_t page)
{
    return memory_region_get_dirty(&s->mem_vram, page, DIRTY_MEMORY_VGA);
}

static inline void reset_dirty(G364State *s,
                               ram_addr_t page_min, ram_addr_t page_max)
{
    memory_region_reset_dirty(&s->mem_vram,
                              page_min,
                              page_max + TARGET_PAGE_SIZE - page_min - 1,
                              DIRTY_MEMORY_VGA);
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
            hw_error("g364: unknown host depth %d",
                     ds_get_bits_per_pixel(s->ds));
            return;
    }

    page = 0;
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
        if (check_dirty(s, page)) {
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
        error_report("g364: unknown guest depth %d", s->depth);
    }

    qemu_irq_raise(s->irq);
}

static inline void g364fb_invalidate_display(void *opaque)
{
    G364State *s = opaque;
    int i;

    s->blanked = 0;
    for (i = 0; i < s->vram_size; i += TARGET_PAGE_SIZE) {
        memory_region_set_dirty(&s->mem_vram, i);
    }
}

static void g364fb_reset(G364State *s)
{
    qemu_irq_lower(s->irq);

    memset(s->color_palette, 0, sizeof(s->color_palette));
    memset(s->cursor_palette, 0, sizeof(s->cursor_palette));
    memset(s->cursor, 0, sizeof(s->cursor));
    s->cursor_position = 0;
    s->ctla = 0;
    s->top_of_screen = 0;
    s->width = s->height = 0;
    memset(s->vram, 0, s->vram_size);
    g364fb_invalidate_display(s);
}

static void g364fb_screen_dump(void *opaque, const char *filename)
{
    G364State *s = opaque;
    int y, x;
    uint8_t index;
    uint8_t *data_buffer;
    FILE *f;

    if (s->depth != 8) {
        error_report("g364: unknown guest depth %d", s->depth);
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
static uint64_t g364fb_ctrl_read(void *opaque,
                                 target_phys_addr_t addr,
                                 unsigned int size)
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
                error_report("g364: invalid read at [" TARGET_FMT_plx "]",
                             addr);
                val = 0;
                break;
            }
        }
    }

    trace_g364fb_read(addr, val);

    return val;
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
        memory_region_set_dirty(&s->mem_vram, i);
    }
}

static void g364fb_ctrl_write(void *opaque,
                              target_phys_addr_t addr,
                              uint64_t val,
                              unsigned int size)
{
    G364State *s = opaque;

    trace_g364fb_write(addr, val);

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
        case REG_BOOT: /* Boot timing */
        case 0x00108: /* Line timing: half sync */
        case 0x00110: /* Line timing: back porch */
        case 0x00120: /* Line timing: short display */
        case 0x00128: /* Frame timing: broad pulse */
        case 0x00130: /* Frame timing: v sync */
        case 0x00138: /* Frame timing: v preequalise */
        case 0x00140: /* Frame timing: v postequalise */
        case 0x00148: /* Frame timing: v blank */
        case 0x00158: /* Line timing: line time */
        case 0x00160: /* Frame store: line start */
        case 0x00168: /* vram cycle: mem init */
        case 0x00170: /* vram cycle: transfer delay */
        case 0x00200: /* vram cycle: mask register */
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
            error_report("g364: invalid write of 0x%" PRIx64
                         " at [" TARGET_FMT_plx "]", val, addr);
            break;
        }
    }
    qemu_irq_lower(s->irq);
}

static const MemoryRegionOps g364fb_ctrl_ops = {
    .read = g364fb_ctrl_read,
    .write = g364fb_ctrl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static int g364fb_post_load(void *opaque, int version_id)
{
    G364State *s = opaque;

    /* force refresh */
    g364fb_update_depth(s);
    g364fb_invalidate_display(s);

    return 0;
}

static const VMStateDescription vmstate_g364fb = {
    .name = "g364fb",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .post_load = g364fb_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_VBUFFER_UINT32(vram, G364State, 1, NULL, 0, vram_size),
        VMSTATE_BUFFER_UNSAFE(color_palette, G364State, 0, 256 * 3),
        VMSTATE_BUFFER_UNSAFE(cursor_palette, G364State, 0, 9),
        VMSTATE_UINT16_ARRAY(cursor, G364State, 512),
        VMSTATE_UINT32(cursor_position, G364State),
        VMSTATE_UINT32(ctla, G364State),
        VMSTATE_UINT32(top_of_screen, G364State),
        VMSTATE_UINT32(width, G364State),
        VMSTATE_UINT32(height, G364State),
        VMSTATE_END_OF_LIST()
    }
};

static void g364fb_init(DeviceState *dev, G364State *s)
{
    s->vram = g_malloc0(s->vram_size);

    s->ds = graphic_console_init(g364fb_update_display,
                                 g364fb_invalidate_display,
                                 g364fb_screen_dump, NULL, s);

    memory_region_init_io(&s->mem_ctrl, &g364fb_ctrl_ops, s, "ctrl", 0x180000);
    memory_region_init_ram_ptr(&s->mem_vram, dev, "vram",
                               s->vram_size, s->vram);
    memory_region_set_coalescing(&s->mem_vram);
}

typedef struct {
    SysBusDevice busdev;
    G364State g364;
} G364SysBusState;

static int g364fb_sysbus_init(SysBusDevice *dev)
{
    G364State *s = &FROM_SYSBUS(G364SysBusState, dev)->g364;

    g364fb_init(&dev->qdev, s);
    sysbus_init_irq(dev, &s->irq);
    sysbus_init_mmio_region(dev, &s->mem_ctrl);
    sysbus_init_mmio_region(dev, &s->mem_vram);

    return 0;
}

static void g364fb_sysbus_reset(DeviceState *d)
{
    G364SysBusState *s = DO_UPCAST(G364SysBusState, busdev.qdev, d);
    g364fb_reset(&s->g364);
}

static SysBusDeviceInfo g364fb_sysbus_info = {
    .init = g364fb_sysbus_init,
    .qdev.name = "sysbus-g364",
    .qdev.desc = "G364 framebuffer",
    .qdev.size = sizeof(G364SysBusState),
    .qdev.vmsd = &vmstate_g364fb,
    .qdev.reset = g364fb_sysbus_reset,
    .qdev.props = (Property[]) {
        DEFINE_PROP_HEX32("vram_size", G364SysBusState, g364.vram_size,
                          8 * 1024 * 1024),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void g364fb_register(void)
{
    sysbus_register_withprop(&g364fb_sysbus_info);
}

device_init(g364fb_register);
