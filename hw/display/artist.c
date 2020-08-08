/*
 * QEMU HP Artist Emulation
 *
 * Copyright (c) 2019 Sven Schnelle <svens@stackframe.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/error-report.h"
#include "qemu/typedefs.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/loader.h"
#include "hw/qdev-core.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "ui/console.h"
#include "trace.h"
#include "hw/display/framebuffer.h"

#define TYPE_ARTIST "artist"
#define ARTIST(obj) OBJECT_CHECK(ARTISTState, (obj), TYPE_ARTIST)

#ifdef HOST_WORDS_BIGENDIAN
#define ROP8OFF(_i) (3 - (_i))
#else
#define ROP8OFF
#endif

struct vram_buffer {
    MemoryRegion mr;
    uint8_t *data;
    int size;
    int width;
    int height;
};

typedef struct ARTISTState {
    SysBusDevice parent_obj;

    QemuConsole *con;
    MemoryRegion vram_mem;
    MemoryRegion mem_as_root;
    MemoryRegion reg;
    MemoryRegionSection fbsection;

    void *vram_int_mr;
    AddressSpace as;

    struct vram_buffer vram_buffer[16];

    uint16_t width;
    uint16_t height;
    uint16_t depth;

    uint32_t fg_color;
    uint32_t bg_color;

    uint32_t vram_char_y;
    uint32_t vram_bitmask;

    uint32_t vram_start;
    uint32_t vram_pos;

    uint32_t vram_size;

    uint32_t blockmove_source;
    uint32_t blockmove_dest;
    uint32_t blockmove_size;

    uint32_t line_size;
    uint32_t line_end;
    uint32_t line_xy;
    uint32_t line_pattern_start;
    uint32_t line_pattern_skip;

    uint32_t cursor_pos;

    uint32_t cursor_height;
    uint32_t cursor_width;

    uint32_t plane_mask;

    uint32_t reg_100080;
    uint32_t reg_300200;
    uint32_t reg_300208;
    uint32_t reg_300218;

    uint32_t cmap_bm_access;
    uint32_t dst_bm_access;
    uint32_t src_bm_access;
    uint32_t control_plane;
    uint32_t transfer_data;
    uint32_t image_bitmap_op;

    uint32_t font_write1;
    uint32_t font_write2;
    uint32_t font_write_pos_y;

    int draw_line_pattern;
} ARTISTState;

typedef enum {
    ARTIST_BUFFER_AP = 1,
    ARTIST_BUFFER_OVERLAY = 2,
    ARTIST_BUFFER_CURSOR1 = 6,
    ARTIST_BUFFER_CURSOR2 = 7,
    ARTIST_BUFFER_ATTRIBUTE = 13,
    ARTIST_BUFFER_CMAP = 15,
} artist_buffer_t;

typedef enum {
    VRAM_IDX = 0x1004a0,
    VRAM_BITMASK = 0x1005a0,
    VRAM_WRITE_INCR_X = 0x100600,
    VRAM_WRITE_INCR_X2 = 0x100604,
    VRAM_WRITE_INCR_Y = 0x100620,
    VRAM_START = 0x100800,
    BLOCK_MOVE_SIZE = 0x100804,
    BLOCK_MOVE_SOURCE = 0x100808,
    TRANSFER_DATA = 0x100820,
    FONT_WRITE_INCR_Y = 0x1008a0,
    VRAM_START_TRIGGER = 0x100a00,
    VRAM_SIZE_TRIGGER = 0x100a04,
    FONT_WRITE_START = 0x100aa0,
    BLOCK_MOVE_DEST_TRIGGER = 0x100b00,
    BLOCK_MOVE_SIZE_TRIGGER = 0x100b04,
    LINE_XY = 0x100ccc,
    PATTERN_LINE_START = 0x100ecc,
    LINE_SIZE = 0x100e04,
    LINE_END = 0x100e44,
    CMAP_BM_ACCESS = 0x118000,
    DST_BM_ACCESS = 0x118004,
    SRC_BM_ACCESS = 0x118008,
    CONTROL_PLANE = 0x11800c,
    FG_COLOR = 0x118010,
    BG_COLOR = 0x118014,
    PLANE_MASK = 0x118018,
    IMAGE_BITMAP_OP = 0x11801c,
    CURSOR_POS = 0x300100,
    CURSOR_CTRL = 0x300104,
} artist_reg_t;

typedef enum {
    ARTIST_ROP_CLEAR = 0,
    ARTIST_ROP_COPY = 3,
    ARTIST_ROP_XOR = 6,
    ARTIST_ROP_NOT_DST = 10,
    ARTIST_ROP_SET = 15,
} artist_rop_t;

#define REG_NAME(_x) case _x: return " "#_x;
static const char *artist_reg_name(uint64_t addr)
{
    switch ((artist_reg_t)addr) {
    REG_NAME(VRAM_IDX);
    REG_NAME(VRAM_BITMASK);
    REG_NAME(VRAM_WRITE_INCR_X);
    REG_NAME(VRAM_WRITE_INCR_X2);
    REG_NAME(VRAM_WRITE_INCR_Y);
    REG_NAME(VRAM_START);
    REG_NAME(BLOCK_MOVE_SIZE);
    REG_NAME(BLOCK_MOVE_SOURCE);
    REG_NAME(FG_COLOR);
    REG_NAME(BG_COLOR);
    REG_NAME(PLANE_MASK);
    REG_NAME(VRAM_START_TRIGGER);
    REG_NAME(VRAM_SIZE_TRIGGER);
    REG_NAME(BLOCK_MOVE_DEST_TRIGGER);
    REG_NAME(BLOCK_MOVE_SIZE_TRIGGER);
    REG_NAME(TRANSFER_DATA);
    REG_NAME(CONTROL_PLANE);
    REG_NAME(IMAGE_BITMAP_OP);
    REG_NAME(CMAP_BM_ACCESS);
    REG_NAME(DST_BM_ACCESS);
    REG_NAME(SRC_BM_ACCESS);
    REG_NAME(CURSOR_POS);
    REG_NAME(CURSOR_CTRL);
    REG_NAME(LINE_XY);
    REG_NAME(PATTERN_LINE_START);
    REG_NAME(LINE_SIZE);
    REG_NAME(LINE_END);
    REG_NAME(FONT_WRITE_INCR_Y);
    REG_NAME(FONT_WRITE_START);
    }
    return "";
}
#undef REG_NAME

static int16_t artist_get_x(uint32_t reg)
{
    return reg >> 16;
}

static int16_t artist_get_y(uint32_t reg)
{
    return reg & 0xffff;
}

static void artist_invalidate_lines(struct vram_buffer *buf,
                                    int starty, int height)
{
    int start = starty * buf->width;
    int size = height * buf->width;

    if (start + size <= buf->size) {
        memory_region_set_dirty(&buf->mr, start, size);
    }
}

static int vram_write_pix_per_transfer(ARTISTState *s)
{
    if (s->cmap_bm_access) {
        return 1 << ((s->cmap_bm_access >> 27) & 0x0f);
    } else {
        return 1 << ((s->dst_bm_access >> 27) & 0x0f);
    }
}

static int vram_pixel_length(ARTISTState *s)
{
    if (s->cmap_bm_access) {
        return (s->cmap_bm_access >> 24) & 0x07;
    } else {
        return (s->dst_bm_access >> 24) & 0x07;
    }
}

static int vram_write_bufidx(ARTISTState *s)
{
    if (s->cmap_bm_access) {
        return (s->cmap_bm_access >> 12) & 0x0f;
    } else {
        return (s->dst_bm_access >> 12) & 0x0f;
    }
}

static int vram_read_bufidx(ARTISTState *s)
{
    if (s->cmap_bm_access) {
        return (s->cmap_bm_access >> 12) & 0x0f;
    } else {
        return (s->src_bm_access >> 12) & 0x0f;
    }
}

static struct vram_buffer *vram_read_buffer(ARTISTState *s)
{
    return &s->vram_buffer[vram_read_bufidx(s)];
}

static struct vram_buffer *vram_write_buffer(ARTISTState *s)
{
    return &s->vram_buffer[vram_write_bufidx(s)];
}

static uint8_t artist_get_color(ARTISTState *s)
{
    if (s->image_bitmap_op & 2) {
        return s->fg_color;
    } else {
        return s->bg_color;
    }
}

static artist_rop_t artist_get_op(ARTISTState *s)
{
    return (s->image_bitmap_op >> 8) & 0xf;
}

static void artist_rop8(ARTISTState *s, uint8_t *dst, uint8_t val)
{

    const artist_rop_t op = artist_get_op(s);
    uint8_t plane_mask = s->plane_mask & 0xff;

    switch (op) {
    case ARTIST_ROP_CLEAR:
        *dst &= ~plane_mask;
        break;

    case ARTIST_ROP_COPY:
        *dst &= ~plane_mask;
        *dst |= val & plane_mask;
        break;

    case ARTIST_ROP_XOR:
        *dst ^= val & plane_mask;
        break;

    case ARTIST_ROP_NOT_DST:
        *dst ^= plane_mask;
        break;

    case ARTIST_ROP_SET:
        *dst |= plane_mask;
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "%s: unsupported rop %d\n", __func__, op);
        break;
    }
}

static void artist_get_cursor_pos(ARTISTState *s, int *x, int *y)
{
    /*
     * Don't know whether these magic offset values are configurable via
     * some register. They are the same for all resolutions, so don't
     * bother about it.
     */

    *y = 0x47a - artist_get_y(s->cursor_pos);
    *x = ((artist_get_x(s->cursor_pos) - 338) / 2);

    if (*x > s->width) {
        *x = 0;
    }

    if (*y > s->height) {
        *y = 0;
    }
}

static void artist_invalidate_cursor(ARTISTState *s)
{
    int x, y;
    artist_get_cursor_pos(s, &x, &y);
    artist_invalidate_lines(&s->vram_buffer[ARTIST_BUFFER_AP],
                            y, s->cursor_height);
}

static void vram_bit_write(ARTISTState *s, int posx, int posy, bool incr_x,
                           int size, uint32_t data)
{
    struct vram_buffer *buf;
    uint32_t vram_bitmask = s->vram_bitmask;
    int mask, i, pix_count, pix_length, offset, height, width;
    uint8_t *data8, *p;

    pix_count = vram_write_pix_per_transfer(s);
    pix_length = vram_pixel_length(s);

    buf = vram_write_buffer(s);
    height = buf->height;
    width = buf->width;

    if (s->cmap_bm_access) {
        offset = s->vram_pos;
    } else {
        offset = posy * width + posx;
    }

    if (!buf->size) {
        qemu_log("write to non-existent buffer\n");
        return;
    }

    p = buf->data;

    if (pix_count > size * 8) {
        pix_count = size * 8;
    }

    if (posy * width + posx + pix_count > buf->size) {
        qemu_log("write outside bounds: wants %dx%d, max size %dx%d\n",
                 posx, posy, width, height);
        return;
    }


    switch (pix_length) {
    case 0:
        if (s->image_bitmap_op & 0x20000000) {
            data &= vram_bitmask;
        }

        for (i = 0; i < pix_count; i++) {
            artist_rop8(s, p + offset + pix_count - 1 - i,
                        (data & 1) ? (s->plane_mask >> 24) : 0);
            data >>= 1;
        }
        memory_region_set_dirty(&buf->mr, offset, pix_count);
        break;

    case 3:
        if (s->cmap_bm_access) {
            *(uint32_t *)(p + offset) = data;
            break;
        }
        data8 = (uint8_t *)&data;

        for (i = 3; i >= 0; i--) {
            if (!(s->image_bitmap_op & 0x20000000) ||
                s->vram_bitmask & (1 << (28 + i))) {
                artist_rop8(s, p + offset + 3 - i, data8[ROP8OFF(i)]);
            }
        }
        memory_region_set_dirty(&buf->mr, offset, 3);
        break;

    case 6:
        switch (size) {
        default:
        case 4:
            vram_bitmask = s->vram_bitmask;
            break;

        case 2:
            vram_bitmask = s->vram_bitmask >> 16;
            break;

        case 1:
            vram_bitmask = s->vram_bitmask >> 24;
            break;
        }

        for (i = 0; i < pix_count; i++) {
            mask = 1 << (pix_count - 1 - i);

            if (!(s->image_bitmap_op & 0x20000000) ||
                (vram_bitmask & mask)) {
                if (data & mask) {
                    artist_rop8(s, p + offset + i, s->fg_color);
                } else {
                    if (!(s->image_bitmap_op & 0x10000002)) {
                        artist_rop8(s, p + offset + i, s->bg_color);
                    }
                }
            }
        }
        memory_region_set_dirty(&buf->mr, offset, pix_count);
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "%s: unknown pixel length %d\n",
                      __func__, pix_length);
        break;
    }

    if (incr_x) {
        if (s->cmap_bm_access) {
            s->vram_pos += 4;
        } else {
            s->vram_pos += pix_count << 2;
        }
    }

    if (vram_write_bufidx(s) == ARTIST_BUFFER_CURSOR1 ||
        vram_write_bufidx(s) == ARTIST_BUFFER_CURSOR2) {
        artist_invalidate_cursor(s);
    }
}

static void block_move(ARTISTState *s, int source_x, int source_y, int dest_x,
                       int dest_y, int width, int height)
{
    struct vram_buffer *buf;
    int line, endline, lineincr, startcolumn, endcolumn, columnincr, column;
    uint32_t dst, src;

    trace_artist_block_move(source_x, source_y, dest_x, dest_y, width, height);

    if (s->control_plane != 0) {
        /* We don't support CONTROL_PLANE accesses */
        qemu_log_mask(LOG_UNIMP, "%s: CONTROL_PLANE: %08x\n", __func__,
                      s->control_plane);
        return;
    }

    buf = &s->vram_buffer[ARTIST_BUFFER_AP];

    if (dest_y > source_y) {
        /* move down */
        line = height - 1;
        endline = -1;
        lineincr = -1;
    } else {
        /* move up */
        line = 0;
        endline = height;
        lineincr = 1;
    }

    if (dest_x > source_x) {
        /* move right */
        startcolumn = width - 1;
        endcolumn = -1;
        columnincr = -1;
    } else {
        /* move left */
        startcolumn = 0;
        endcolumn = width;
        columnincr = 1;
    }

    for ( ; line != endline; line += lineincr) {
        src = source_x + ((line + source_y) * buf->width);
        dst = dest_x + ((line + dest_y) * buf->width);

        for (column = startcolumn; column != endcolumn; column += columnincr) {
            if (dst + column > buf->size || src + column > buf->size) {
                continue;
            }
            artist_rop8(s, buf->data + dst + column, buf->data[src + column]);
        }
    }

    artist_invalidate_lines(buf, dest_y, height);
}

static void fill_window(ARTISTState *s, int startx, int starty,
                        int width, int height)
{
    uint32_t offset;
    uint8_t color = artist_get_color(s);
    struct vram_buffer *buf;
    int x, y;

    trace_artist_fill_window(startx, starty, width, height,
                             s->image_bitmap_op, s->control_plane);

    if (s->control_plane != 0) {
        /* We don't support CONTROL_PLANE accesses */
        qemu_log_mask(LOG_UNIMP, "%s: CONTROL_PLANE: %08x\n", __func__,
                      s->control_plane);
        return;
    }

    if (s->reg_100080 == 0x7d) {
        /*
         * Not sure what this register really does, but
         * 0x7d seems to enable autoincremt of the Y axis
         * by the current block move height.
         */
        height = artist_get_y(s->blockmove_size);
        s->vram_start += height;
    }

    buf = &s->vram_buffer[ARTIST_BUFFER_AP];

    for (y = starty; y < starty + height; y++) {
        offset = y * s->width;

        for (x = startx; x < startx + width; x++) {
            artist_rop8(s, buf->data + offset + x, color);
        }
    }
    artist_invalidate_lines(buf, starty, height);
}

static void draw_line(ARTISTState *s, int x1, int y1, int x2, int y2,
                      bool update_start, int skip_pix, int max_pix)
{
    struct vram_buffer *buf;
    uint8_t color;
    int dx, dy, t, e, x, y, incy, diago, horiz;
    bool c1;
    uint8_t *p;

    trace_artist_draw_line(x1, y1, x2, y2);

    if (update_start) {
        s->vram_start = (x2 << 16) | y2;
    }

    if (x2 > x1) {
        dx = x2 - x1;
    } else {
        dx = x1 - x2;
    }
    if (y2 > y1) {
        dy = y2 - y1;
    } else {
        dy = y1 - y2;
    }
    if (!dx || !dy) {
        return;
    }

    c1 = false;
    if (dy > dx) {
        t = y2;
        y2 = x2;
        x2 = t;

        t = y1;
        y1 = x1;
        x1 = t;

        t = dx;
        dx = dy;
        dy = t;

        c1 = true;
    }

    if (x1 > x2) {
        t = y2;
        y2 = y1;
        y1 = t;

        t = x1;
        x1 = x2;
        x2 = t;
    }

    horiz = dy << 1;
    diago = (dy - dx) << 1;
    e = (dy << 1) - dx;

    if (y1 <= y2) {
        incy = 1;
    } else {
        incy = -1;
    }
    x = x1;
    y = y1;
    color = artist_get_color(s);
    buf = &s->vram_buffer[ARTIST_BUFFER_AP];

    do {
        if (c1) {
            p = buf->data + x * s->width + y;
        } else {
            p = buf->data + y * s->width + x;
        }

        if (skip_pix > 0) {
            skip_pix--;
        } else {
            artist_rop8(s, p, color);
        }

        if (e > 0) {
            artist_invalidate_lines(buf, y, 1);
            y  += incy;
            e  += diago;
        } else {
            e += horiz;
        }
        x++;
    } while (x <= x2 && (max_pix == -1 || --max_pix > 0));
}

static void draw_line_pattern_start(ARTISTState *s)
{

    int startx = artist_get_x(s->vram_start);
    int starty = artist_get_y(s->vram_start);
    int endx = artist_get_x(s->blockmove_size);
    int endy = artist_get_y(s->blockmove_size);
    int pstart = s->line_pattern_start >> 16;

    draw_line(s, startx, starty, endx, endy, false, -1, pstart);
    s->line_pattern_skip = pstart;
}

static void draw_line_pattern_next(ARTISTState *s)
{

    int startx = artist_get_x(s->vram_start);
    int starty = artist_get_y(s->vram_start);
    int endx = artist_get_x(s->blockmove_size);
    int endy = artist_get_y(s->blockmove_size);
    int line_xy = s->line_xy >> 16;

    draw_line(s, startx, starty, endx, endy, false, s->line_pattern_skip,
              s->line_pattern_skip + line_xy);
    s->line_pattern_skip += line_xy;
    s->image_bitmap_op ^= 2;
}

static void draw_line_size(ARTISTState *s, bool update_start)
{

    int startx = artist_get_x(s->vram_start);
    int starty = artist_get_y(s->vram_start);
    int endx = artist_get_x(s->line_size);
    int endy = artist_get_y(s->line_size);

    draw_line(s, startx, starty, endx, endy, update_start, -1, -1);
}

static void draw_line_xy(ARTISTState *s, bool update_start)
{

    int startx = artist_get_x(s->vram_start);
    int starty = artist_get_y(s->vram_start);
    int sizex = artist_get_x(s->blockmove_size);
    int sizey = artist_get_y(s->blockmove_size);
    int linexy = s->line_xy >> 16;
    int endx, endy;

    endx = startx;
    endy = starty;

    if (sizex > 0) {
        endx = startx + linexy;
    }

    if (sizex < 0) {
        endx = startx;
        startx -= linexy;
    }

    if (sizey > 0) {
        endy = starty + linexy;
    }

    if (sizey < 0) {
        endy = starty;
        starty -= linexy;
    }

    if (startx < 0) {
        startx = 0;
    }

    if (endx < 0) {
        endx = 0;
    }

    if (starty < 0) {
        starty = 0;
    }

    if (endy < 0) {
        endy = 0;
    }

    draw_line(s, startx, starty, endx, endy, false, -1, -1);
}

static void draw_line_end(ARTISTState *s, bool update_start)
{

    int startx = artist_get_x(s->vram_start);
    int starty = artist_get_y(s->vram_start);
    int endx = artist_get_x(s->line_end);
    int endy = artist_get_y(s->line_end);

    draw_line(s, startx, starty, endx, endy, update_start, -1, -1);
}

static void font_write16(ARTISTState *s, uint16_t val)
{
    struct vram_buffer *buf;
    uint32_t color = (s->image_bitmap_op & 2) ? s->fg_color : s->bg_color;
    uint16_t mask;
    int i;

    int startx = artist_get_x(s->vram_start);
    int starty = artist_get_y(s->vram_start) + s->font_write_pos_y;
    int offset = starty * s->width + startx;

    buf = &s->vram_buffer[ARTIST_BUFFER_AP];

    if (offset + 16 > buf->size) {
        return;
    }

    for (i = 0; i < 16; i++) {
        mask = 1 << (15 - i);
        if (val & mask) {
            artist_rop8(s, buf->data + offset + i, color);
        } else {
            if (!(s->image_bitmap_op & 0x20000000)) {
                artist_rop8(s, buf->data + offset + i, s->bg_color);
            }
        }
    }
    artist_invalidate_lines(buf, starty, 1);
}

static void font_write(ARTISTState *s, uint32_t val)
{
    font_write16(s, val >> 16);
    if (++s->font_write_pos_y == artist_get_y(s->blockmove_size)) {
        s->vram_start += (s->blockmove_size & 0xffff0000);
        return;
    }

    font_write16(s, val & 0xffff);
    if (++s->font_write_pos_y == artist_get_y(s->blockmove_size)) {
        s->vram_start += (s->blockmove_size & 0xffff0000);
        return;
    }
}

static void combine_write_reg(hwaddr addr, uint64_t val, int size, void *out)
{
    /*
     * FIXME: is there a qemu helper for this?
     */

#ifndef HOST_WORDS_BIGENDIAN
    addr ^= 3;
#endif

    switch (size) {
    case 1:
        *(uint8_t *)(out + (addr & 3)) = val;
        break;

    case 2:
        *(uint16_t *)(out + (addr & 2)) = val;
        break;

    case 4:
        *(uint32_t *)out = val;
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "unsupported write size: %d\n", size);
    }
}

static void artist_reg_write(void *opaque, hwaddr addr, uint64_t val,
                             unsigned size)
{
    ARTISTState *s = opaque;
    int posx, posy;
    int width, height;

    trace_artist_reg_write(size, addr, artist_reg_name(addr & ~3ULL), val);

    switch (addr & ~3ULL) {
    case 0x100080:
        combine_write_reg(addr, val, size, &s->reg_100080);
        break;

    case FG_COLOR:
        combine_write_reg(addr, val, size, &s->fg_color);
        break;

    case BG_COLOR:
        combine_write_reg(addr, val, size, &s->bg_color);
        break;

    case VRAM_BITMASK:
        combine_write_reg(addr, val, size, &s->vram_bitmask);
        break;

    case VRAM_WRITE_INCR_Y:
        posx = (s->vram_pos >> 2) & 0x7ff;
        posy = (s->vram_pos >> 13) & 0x3ff;
        vram_bit_write(s, posx, posy + s->vram_char_y++, false, size, val);
        break;

    case VRAM_WRITE_INCR_X:
    case VRAM_WRITE_INCR_X2:
        posx = (s->vram_pos >> 2) & 0x7ff;
        posy = (s->vram_pos >> 13) & 0x3ff;
        vram_bit_write(s, posx, posy + s->vram_char_y, true, size, val);
        break;

    case VRAM_IDX:
        combine_write_reg(addr, val, size, &s->vram_pos);
        s->vram_char_y = 0;
        s->draw_line_pattern = 0;
        break;

    case VRAM_START:
        combine_write_reg(addr, val, size, &s->vram_start);
        s->draw_line_pattern = 0;
        break;

    case VRAM_START_TRIGGER:
        combine_write_reg(addr, val, size, &s->vram_start);
        fill_window(s, artist_get_x(s->vram_start),
                    artist_get_y(s->vram_start),
                    artist_get_x(s->blockmove_size),
                    artist_get_y(s->blockmove_size));
        break;

    case VRAM_SIZE_TRIGGER:
        combine_write_reg(addr, val, size, &s->vram_size);

        if (size == 2 && !(addr & 2)) {
            height = artist_get_y(s->blockmove_size);
        } else {
            height = artist_get_y(s->vram_size);
        }

        if (size == 2 && (addr & 2)) {
            width = artist_get_x(s->blockmove_size);
        } else {
            width = artist_get_x(s->vram_size);
        }

        fill_window(s, artist_get_x(s->vram_start),
                    artist_get_y(s->vram_start),
                    width, height);
        break;

    case LINE_XY:
        combine_write_reg(addr, val, size, &s->line_xy);
        if (s->draw_line_pattern) {
            draw_line_pattern_next(s);
        } else {
            draw_line_xy(s, true);
        }
        break;

    case PATTERN_LINE_START:
        combine_write_reg(addr, val, size, &s->line_pattern_start);
        s->draw_line_pattern = 1;
        draw_line_pattern_start(s);
        break;

    case LINE_SIZE:
        combine_write_reg(addr, val, size, &s->line_size);
        draw_line_size(s, true);
        break;

    case LINE_END:
        combine_write_reg(addr, val, size, &s->line_end);
        draw_line_end(s, true);
        break;

    case BLOCK_MOVE_SIZE:
        combine_write_reg(addr, val, size, &s->blockmove_size);
        break;

    case BLOCK_MOVE_SOURCE:
        combine_write_reg(addr, val, size, &s->blockmove_source);
        break;

    case BLOCK_MOVE_DEST_TRIGGER:
        combine_write_reg(addr, val, size, &s->blockmove_dest);

        block_move(s, artist_get_x(s->blockmove_source),
                   artist_get_y(s->blockmove_source),
                   artist_get_x(s->blockmove_dest),
                   artist_get_y(s->blockmove_dest),
                   artist_get_x(s->blockmove_size),
                   artist_get_y(s->blockmove_size));
        break;

    case BLOCK_MOVE_SIZE_TRIGGER:
        combine_write_reg(addr, val, size, &s->blockmove_size);

        block_move(s,
                   artist_get_x(s->blockmove_source),
                   artist_get_y(s->blockmove_source),
                   artist_get_x(s->vram_start),
                   artist_get_y(s->vram_start),
                   artist_get_x(s->blockmove_size),
                   artist_get_y(s->blockmove_size));
        break;

    case PLANE_MASK:
        combine_write_reg(addr, val, size, &s->plane_mask);
        break;

    case CMAP_BM_ACCESS:
        combine_write_reg(addr, val, size, &s->cmap_bm_access);
        break;

    case DST_BM_ACCESS:
        combine_write_reg(addr, val, size, &s->dst_bm_access);
        s->cmap_bm_access = 0;
        break;

    case SRC_BM_ACCESS:
        combine_write_reg(addr, val, size, &s->src_bm_access);
        s->cmap_bm_access = 0;
        break;

    case CONTROL_PLANE:
        combine_write_reg(addr, val, size, &s->control_plane);
        break;

    case TRANSFER_DATA:
        combine_write_reg(addr, val, size, &s->transfer_data);
        break;

    case 0x300200:
        combine_write_reg(addr, val, size, &s->reg_300200);
        break;

    case 0x300208:
        combine_write_reg(addr, val, size, &s->reg_300208);
        break;

    case 0x300218:
        combine_write_reg(addr, val, size, &s->reg_300218);
        break;

    case CURSOR_POS:
        artist_invalidate_cursor(s);
        combine_write_reg(addr, val, size, &s->cursor_pos);
        artist_invalidate_cursor(s);
        break;

    case CURSOR_CTRL:
        break;

    case IMAGE_BITMAP_OP:
        combine_write_reg(addr, val, size, &s->image_bitmap_op);
        break;

    case FONT_WRITE_INCR_Y:
        combine_write_reg(addr, val, size, &s->font_write1);
        font_write(s, s->font_write1);
        break;

    case FONT_WRITE_START:
        combine_write_reg(addr, val, size, &s->font_write2);
        s->font_write_pos_y = 0;
        font_write(s, s->font_write2);
        break;

    case 300104:
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "%s: unknown register: reg=%08" HWADDR_PRIx
                      " val=%08" PRIx64 " size=%d\n",
                      __func__, addr, val, size);
        break;
    }
}

static uint64_t combine_read_reg(hwaddr addr, int size, void *in)
{
    /*
     * FIXME: is there a qemu helper for this?
     */

#ifndef HOST_WORDS_BIGENDIAN
    addr ^= 3;
#endif

    switch (size) {
    case 1:
        return *(uint8_t *)(in + (addr & 3));

    case 2:
        return *(uint16_t *)(in + (addr & 2));

    case 4:
        return *(uint32_t *)in;

    default:
        qemu_log_mask(LOG_UNIMP, "unsupported read size: %d\n", size);
        return 0;
    }
}

static uint64_t artist_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    ARTISTState *s = opaque;
    uint32_t val = 0;

    switch (addr & ~3ULL) {
        /* Unknown status registers */
    case 0:
        break;

    case 0x211110:
        val = (s->width << 16) | s->height;
        if (s->depth == 1) {
            val |= 1 << 31;
        }
        break;

    case 0x100000:
    case 0x300000:
    case 0x300004:
    case 0x300308:
    case 0x380000:
        break;

    case 0x300008:
    case 0x380008:
        /*
         * FIFO ready flag. we're not emulating the FIFOs
         * so we're always ready
         */
        val = 0x10;
        break;

    case 0x300200:
        val = s->reg_300200;
        break;

    case 0x300208:
        val = s->reg_300208;
        break;

    case 0x300218:
        val = s->reg_300218;
        break;

    case 0x30023c:
        val = 0xac4ffdac;
        break;

    case 0x380004:
        /* 0x02000000 Buserror */
        val = 0x6dc20006;
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "%s: unknown register: %08" HWADDR_PRIx
                      " size %d\n", __func__, addr, size);
        break;
    }
    val = combine_read_reg(addr, size, &val);
    trace_artist_reg_read(size, addr, artist_reg_name(addr & ~3ULL), val);
    return val;
}

static void artist_vram_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
    ARTISTState *s = opaque;
    struct vram_buffer *buf;
    int posy = (addr >> 11) & 0x3ff;
    int posx = addr & 0x7ff;
    uint32_t offset;
    trace_artist_vram_write(size, addr, val);

    if (s->cmap_bm_access) {
        buf = &s->vram_buffer[ARTIST_BUFFER_CMAP];
        if (addr + 3 < buf->size) {
            *(uint32_t *)(buf->data + addr) = val;
        }
        return;
    }

    buf = vram_write_buffer(s);
    if (!buf->size) {
        return;
    }

    if (posy > buf->height || posx > buf->width) {
        return;
    }

    offset = posy * buf->width + posx;
    switch (size) {
    case 4:
        *(uint32_t *)(buf->data + offset) = be32_to_cpu(val);
        memory_region_set_dirty(&buf->mr, offset, 4);
        break;
    case 2:
        *(uint16_t *)(buf->data + offset) = be16_to_cpu(val);
        memory_region_set_dirty(&buf->mr, offset, 2);
        break;
    case 1:
        *(uint8_t *)(buf->data + offset) = val;
        memory_region_set_dirty(&buf->mr, offset, 1);
        break;
    default:
        break;
    }
}

static uint64_t artist_vram_read(void *opaque, hwaddr addr, unsigned size)
{
    ARTISTState *s = opaque;
    struct vram_buffer *buf;
    uint64_t val;
    int posy, posx;

    if (s->cmap_bm_access) {
        buf = &s->vram_buffer[ARTIST_BUFFER_CMAP];
        val = *(uint32_t *)(buf->data + addr);
        trace_artist_vram_read(size, addr, 0, 0, val);
        return 0;
    }

    buf = vram_read_buffer(s);
    if (!buf->size) {
        return 0;
    }

    posy = (addr >> 13) & 0x3ff;
    posx = (addr >> 2) & 0x7ff;

    if (posy > buf->height || posx > buf->width) {
        return 0;
    }

    val = cpu_to_be32(*(uint32_t *)(buf->data + posy * buf->width + posx));
    trace_artist_vram_read(size, addr, posx, posy, val);
    return val;
}

static const MemoryRegionOps artist_reg_ops = {
    .read = artist_reg_read,
    .write = artist_reg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 1,
    .impl.max_access_size = 4,
};

static const MemoryRegionOps artist_vram_ops = {
    .read = artist_vram_read,
    .write = artist_vram_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 1,
    .impl.max_access_size = 4,
};

static void artist_draw_cursor(ARTISTState *s)
{
    DisplaySurface *surface = qemu_console_surface(s->con);
    uint32_t *data = (uint32_t *)surface_data(surface);
    struct vram_buffer *cursor0, *cursor1 , *buf;
    int cx, cy, cursor_pos_x, cursor_pos_y;

    cursor0 = &s->vram_buffer[ARTIST_BUFFER_CURSOR1];
    cursor1 = &s->vram_buffer[ARTIST_BUFFER_CURSOR2];
    buf = &s->vram_buffer[ARTIST_BUFFER_AP];

    artist_get_cursor_pos(s, &cursor_pos_x, &cursor_pos_y);

    for (cy = 0; cy < s->cursor_height; cy++) {

        for (cx = 0; cx < s->cursor_width; cx++) {

            if (cursor_pos_y + cy < 0 ||
                cursor_pos_x + cx < 0 ||
                cursor_pos_y + cy > buf->height - 1 ||
                cursor_pos_x + cx > buf->width) {
                continue;
            }

            int dstoffset = (cursor_pos_y + cy) * s->width +
                (cursor_pos_x + cx);

            if (cursor0->data[cy * cursor0->width + cx]) {
                data[dstoffset] = 0;
            } else {
                if (cursor1->data[cy * cursor1->width + cx]) {
                    data[dstoffset] = 0xffffff;
                }
            }
        }
    }
}

static void artist_draw_line(void *opaque, uint8_t *d, const uint8_t *src,
                             int width, int pitch)
{
    ARTISTState *s = ARTIST(opaque);
    uint32_t *cmap, *data = (uint32_t *)d;
    int x;

    cmap = (uint32_t *)(s->vram_buffer[ARTIST_BUFFER_CMAP].data + 0x400);

    for (x = 0; x < s->width; x++) {
        *data++ = cmap[*src++];
    }
}

static void artist_update_display(void *opaque)
{
    ARTISTState *s = opaque;
    DisplaySurface *surface = qemu_console_surface(s->con);
    int first = 0, last;


    framebuffer_update_display(surface, &s->fbsection, s->width, s->height,
                               s->width, s->width * 4, 0, 0, artist_draw_line,
                               s, &first, &last);

    artist_draw_cursor(s);

    dpy_gfx_update(s->con, 0, 0, s->width, s->height);
}

static void artist_invalidate(void *opaque)
{
    ARTISTState *s = ARTIST(opaque);
    struct vram_buffer *buf = &s->vram_buffer[ARTIST_BUFFER_AP];
    memory_region_set_dirty(&buf->mr, 0, buf->size);
}

static const GraphicHwOps artist_ops = {
    .invalidate  = artist_invalidate,
    .gfx_update = artist_update_display,
};

static void artist_initfn(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    ARTISTState *s = ARTIST(obj);

    memory_region_init_io(&s->reg, obj, &artist_reg_ops, s, "artist.reg",
                          4 * MiB);
    memory_region_init_io(&s->vram_mem, obj, &artist_vram_ops, s, "artist.vram",
                          8 * MiB);
    sysbus_init_mmio(sbd, &s->reg);
    sysbus_init_mmio(sbd, &s->vram_mem);
}

static void artist_create_buffer(ARTISTState *s, const char *name,
                                 hwaddr *offset, unsigned int idx,
                                 int width, int height)
{
    struct vram_buffer *buf = s->vram_buffer + idx;

    memory_region_init_ram(&buf->mr, NULL, name, width * height,
                           &error_fatal);
    memory_region_add_subregion_overlap(&s->mem_as_root, *offset, &buf->mr, 0);

    buf->data = memory_region_get_ram_ptr(&buf->mr);
    buf->size = height * width;
    buf->width = width;
    buf->height = height;

    *offset += buf->size;
}

static void artist_realizefn(DeviceState *dev, Error **errp)
{
    ARTISTState *s = ARTIST(dev);
    struct vram_buffer *buf;
    hwaddr offset = 0;

    memory_region_init(&s->mem_as_root, OBJECT(dev), "artist", ~0ull);
    address_space_init(&s->as, &s->mem_as_root, "artist");

    artist_create_buffer(s, "cmap", &offset, ARTIST_BUFFER_CMAP, 2048, 4);
    artist_create_buffer(s, "ap", &offset, ARTIST_BUFFER_AP,
                         s->width, s->height);
    artist_create_buffer(s, "cursor1", &offset, ARTIST_BUFFER_CURSOR1, 64, 64);
    artist_create_buffer(s, "cursor2", &offset, ARTIST_BUFFER_CURSOR2, 64, 64);
    artist_create_buffer(s, "attribute", &offset, ARTIST_BUFFER_ATTRIBUTE,
                         64, 64);

    buf = &s->vram_buffer[ARTIST_BUFFER_AP];
    framebuffer_update_memory_section(&s->fbsection, &buf->mr, 0,
                                      buf->width, buf->height);
    /*
     * no idea whether the cursor is fixed size or not, so assume 32x32 which
     * seems sufficient for HP-UX X11.
     */
    s->cursor_height = 32;
    s->cursor_width = 32;

    s->con = graphic_console_init(DEVICE(dev), 0, &artist_ops, s);
    qemu_console_resize(s->con, s->width, s->height);
}

static int vmstate_artist_post_load(void *opaque, int version_id)
{
    artist_invalidate(opaque);
    return 0;
}

static const VMStateDescription vmstate_artist = {
    .name = "artist",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = vmstate_artist_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(height, ARTISTState),
        VMSTATE_UINT16(width, ARTISTState),
        VMSTATE_UINT16(depth, ARTISTState),
        VMSTATE_UINT32(fg_color, ARTISTState),
        VMSTATE_UINT32(bg_color, ARTISTState),
        VMSTATE_UINT32(vram_char_y, ARTISTState),
        VMSTATE_UINT32(vram_bitmask, ARTISTState),
        VMSTATE_UINT32(vram_start, ARTISTState),
        VMSTATE_UINT32(vram_pos, ARTISTState),
        VMSTATE_UINT32(vram_size, ARTISTState),
        VMSTATE_UINT32(blockmove_source, ARTISTState),
        VMSTATE_UINT32(blockmove_dest, ARTISTState),
        VMSTATE_UINT32(blockmove_size, ARTISTState),
        VMSTATE_UINT32(line_size, ARTISTState),
        VMSTATE_UINT32(line_end, ARTISTState),
        VMSTATE_UINT32(line_xy, ARTISTState),
        VMSTATE_UINT32(cursor_pos, ARTISTState),
        VMSTATE_UINT32(cursor_height, ARTISTState),
        VMSTATE_UINT32(cursor_width, ARTISTState),
        VMSTATE_UINT32(plane_mask, ARTISTState),
        VMSTATE_UINT32(reg_100080, ARTISTState),
        VMSTATE_UINT32(reg_300200, ARTISTState),
        VMSTATE_UINT32(reg_300208, ARTISTState),
        VMSTATE_UINT32(reg_300218, ARTISTState),
        VMSTATE_UINT32(cmap_bm_access, ARTISTState),
        VMSTATE_UINT32(dst_bm_access, ARTISTState),
        VMSTATE_UINT32(src_bm_access, ARTISTState),
        VMSTATE_UINT32(control_plane, ARTISTState),
        VMSTATE_UINT32(transfer_data, ARTISTState),
        VMSTATE_UINT32(image_bitmap_op, ARTISTState),
        VMSTATE_UINT32(font_write1, ARTISTState),
        VMSTATE_UINT32(font_write2, ARTISTState),
        VMSTATE_UINT32(font_write_pos_y, ARTISTState),
        VMSTATE_END_OF_LIST()
    }
};

static Property artist_properties[] = {
    DEFINE_PROP_UINT16("width",        ARTISTState, width, 1280),
    DEFINE_PROP_UINT16("height",       ARTISTState, height, 1024),
    DEFINE_PROP_UINT16("depth",        ARTISTState, depth, 8),
    DEFINE_PROP_END_OF_LIST(),
};

static void artist_reset(DeviceState *qdev)
{
}

static void artist_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = artist_realizefn;
    dc->vmsd = &vmstate_artist;
    dc->reset = artist_reset;
    device_class_set_props(dc, artist_properties);
}

static const TypeInfo artist_info = {
    .name          = TYPE_ARTIST,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ARTISTState),
    .instance_init = artist_initfn,
    .class_init    = artist_class_init,
};

static void artist_register_types(void)
{
    type_register_static(&artist_info);
}

type_init(artist_register_types)
