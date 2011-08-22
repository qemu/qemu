/*
 * QEMU VMware-SVGA "chipset".
 *
 * Copyright (c) 2007 Andrzej Zaborowski  <balrog@zabor.org>
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
#include "loader.h"
#include "console.h"
#include "pci.h"
#include "vmware_vga.h"

#undef VERBOSE
#define HW_RECT_ACCEL
#define HW_FILL_ACCEL
#define HW_MOUSE_ACCEL

# include "vga_int.h"

struct vmsvga_state_s {
    VGACommonState vga;

    int width;
    int height;
    int invalidated;
    int depth;
    int bypp;
    int enable;
    int config;
    struct {
        int id;
        int x;
        int y;
        int on;
    } cursor;

    int index;
    int scratch_size;
    uint32_t *scratch;
    int new_width;
    int new_height;
    uint32_t guest;
    uint32_t svgaid;
    uint32_t wred;
    uint32_t wgreen;
    uint32_t wblue;
    int syncing;
    int fb_size;

    MemoryRegion fifo_ram;
    uint8_t *fifo_ptr;
    unsigned int fifo_size;

    union {
        uint32_t *fifo;
        struct __attribute__((__packed__)) {
            uint32_t min;
            uint32_t max;
            uint32_t next_cmd;
            uint32_t stop;
            /* Add registers here when adding capabilities.  */
            uint32_t fifo[0];
        } *cmd;
    };

#define REDRAW_FIFO_LEN	512
    struct vmsvga_rect_s {
        int x, y, w, h;
    } redraw_fifo[REDRAW_FIFO_LEN];
    int redraw_fifo_first, redraw_fifo_last;
};

struct pci_vmsvga_state_s {
    PCIDevice card;
    struct vmsvga_state_s chip;
    MemoryRegion io_bar;
};

#define SVGA_MAGIC		0x900000UL
#define SVGA_MAKE_ID(ver)	(SVGA_MAGIC << 8 | (ver))
#define SVGA_ID_0		SVGA_MAKE_ID(0)
#define SVGA_ID_1		SVGA_MAKE_ID(1)
#define SVGA_ID_2		SVGA_MAKE_ID(2)

#define SVGA_LEGACY_BASE_PORT	0x4560
#define SVGA_INDEX_PORT		0x0
#define SVGA_VALUE_PORT		0x1
#define SVGA_BIOS_PORT		0x2

#define SVGA_VERSION_2

#ifdef SVGA_VERSION_2
# define SVGA_ID		SVGA_ID_2
# define SVGA_IO_BASE		SVGA_LEGACY_BASE_PORT
# define SVGA_IO_MUL		1
# define SVGA_FIFO_SIZE		0x10000
# define SVGA_PCI_DEVICE_ID	PCI_DEVICE_ID_VMWARE_SVGA2
#else
# define SVGA_ID		SVGA_ID_1
# define SVGA_IO_BASE		SVGA_LEGACY_BASE_PORT
# define SVGA_IO_MUL		4
# define SVGA_FIFO_SIZE		0x10000
# define SVGA_PCI_DEVICE_ID	PCI_DEVICE_ID_VMWARE_SVGA
#endif

enum {
    /* ID 0, 1 and 2 registers */
    SVGA_REG_ID = 0,
    SVGA_REG_ENABLE = 1,
    SVGA_REG_WIDTH = 2,
    SVGA_REG_HEIGHT = 3,
    SVGA_REG_MAX_WIDTH = 4,
    SVGA_REG_MAX_HEIGHT = 5,
    SVGA_REG_DEPTH = 6,
    SVGA_REG_BITS_PER_PIXEL = 7,	/* Current bpp in the guest */
    SVGA_REG_PSEUDOCOLOR = 8,
    SVGA_REG_RED_MASK = 9,
    SVGA_REG_GREEN_MASK = 10,
    SVGA_REG_BLUE_MASK = 11,
    SVGA_REG_BYTES_PER_LINE = 12,
    SVGA_REG_FB_START = 13,
    SVGA_REG_FB_OFFSET = 14,
    SVGA_REG_VRAM_SIZE = 15,
    SVGA_REG_FB_SIZE = 16,

    /* ID 1 and 2 registers */
    SVGA_REG_CAPABILITIES = 17,
    SVGA_REG_MEM_START = 18,		/* Memory for command FIFO */
    SVGA_REG_MEM_SIZE = 19,
    SVGA_REG_CONFIG_DONE = 20,		/* Set when memory area configured */
    SVGA_REG_SYNC = 21,			/* Write to force synchronization */
    SVGA_REG_BUSY = 22,			/* Read to check if sync is done */
    SVGA_REG_GUEST_ID = 23,		/* Set guest OS identifier */
    SVGA_REG_CURSOR_ID = 24,		/* ID of cursor */
    SVGA_REG_CURSOR_X = 25,		/* Set cursor X position */
    SVGA_REG_CURSOR_Y = 26,		/* Set cursor Y position */
    SVGA_REG_CURSOR_ON = 27,		/* Turn cursor on/off */
    SVGA_REG_HOST_BITS_PER_PIXEL = 28,	/* Current bpp in the host */
    SVGA_REG_SCRATCH_SIZE = 29,		/* Number of scratch registers */
    SVGA_REG_MEM_REGS = 30,		/* Number of FIFO registers */
    SVGA_REG_NUM_DISPLAYS = 31,		/* Number of guest displays */
    SVGA_REG_PITCHLOCK = 32,		/* Fixed pitch for all modes */

    SVGA_PALETTE_BASE = 1024,		/* Base of SVGA color map */
    SVGA_PALETTE_END  = SVGA_PALETTE_BASE + 767,
    SVGA_SCRATCH_BASE = SVGA_PALETTE_BASE + 768,
};

#define SVGA_CAP_NONE			0
#define SVGA_CAP_RECT_FILL		(1 << 0)
#define SVGA_CAP_RECT_COPY		(1 << 1)
#define SVGA_CAP_RECT_PAT_FILL		(1 << 2)
#define SVGA_CAP_LEGACY_OFFSCREEN	(1 << 3)
#define SVGA_CAP_RASTER_OP		(1 << 4)
#define SVGA_CAP_CURSOR			(1 << 5)
#define SVGA_CAP_CURSOR_BYPASS		(1 << 6)
#define SVGA_CAP_CURSOR_BYPASS_2	(1 << 7)
#define SVGA_CAP_8BIT_EMULATION		(1 << 8)
#define SVGA_CAP_ALPHA_CURSOR		(1 << 9)
#define SVGA_CAP_GLYPH			(1 << 10)
#define SVGA_CAP_GLYPH_CLIPPING		(1 << 11)
#define SVGA_CAP_OFFSCREEN_1		(1 << 12)
#define SVGA_CAP_ALPHA_BLEND		(1 << 13)
#define SVGA_CAP_3D			(1 << 14)
#define SVGA_CAP_EXTENDED_FIFO		(1 << 15)
#define SVGA_CAP_MULTIMON		(1 << 16)
#define SVGA_CAP_PITCHLOCK		(1 << 17)

/*
 * FIFO offsets (seen as an array of 32-bit words)
 */
enum {
    /*
     * The original defined FIFO offsets
     */
    SVGA_FIFO_MIN = 0,
    SVGA_FIFO_MAX,	/* The distance from MIN to MAX must be at least 10K */
    SVGA_FIFO_NEXT_CMD,
    SVGA_FIFO_STOP,

    /*
     * Additional offsets added as of SVGA_CAP_EXTENDED_FIFO
     */
    SVGA_FIFO_CAPABILITIES = 4,
    SVGA_FIFO_FLAGS,
    SVGA_FIFO_FENCE,
    SVGA_FIFO_3D_HWVERSION,
    SVGA_FIFO_PITCHLOCK,
};

#define SVGA_FIFO_CAP_NONE		0
#define SVGA_FIFO_CAP_FENCE		(1 << 0)
#define SVGA_FIFO_CAP_ACCELFRONT	(1 << 1)
#define SVGA_FIFO_CAP_PITCHLOCK		(1 << 2)

#define SVGA_FIFO_FLAG_NONE		0
#define SVGA_FIFO_FLAG_ACCELFRONT	(1 << 0)

/* These values can probably be changed arbitrarily.  */
#define SVGA_SCRATCH_SIZE		0x8000
#define SVGA_MAX_WIDTH			2360
#define SVGA_MAX_HEIGHT			1770

#ifdef VERBOSE
# define GUEST_OS_BASE		0x5001
static const char *vmsvga_guest_id[] = {
    [0x00] = "Dos",
    [0x01] = "Windows 3.1",
    [0x02] = "Windows 95",
    [0x03] = "Windows 98",
    [0x04] = "Windows ME",
    [0x05] = "Windows NT",
    [0x06] = "Windows 2000",
    [0x07] = "Linux",
    [0x08] = "OS/2",
    [0x09] = "an unknown OS",
    [0x0a] = "BSD",
    [0x0b] = "Whistler",
    [0x0c] = "an unknown OS",
    [0x0d] = "an unknown OS",
    [0x0e] = "an unknown OS",
    [0x0f] = "an unknown OS",
    [0x10] = "an unknown OS",
    [0x11] = "an unknown OS",
    [0x12] = "an unknown OS",
    [0x13] = "an unknown OS",
    [0x14] = "an unknown OS",
    [0x15] = "Windows 2003",
};
#endif

enum {
    SVGA_CMD_INVALID_CMD = 0,
    SVGA_CMD_UPDATE = 1,
    SVGA_CMD_RECT_FILL = 2,
    SVGA_CMD_RECT_COPY = 3,
    SVGA_CMD_DEFINE_BITMAP = 4,
    SVGA_CMD_DEFINE_BITMAP_SCANLINE = 5,
    SVGA_CMD_DEFINE_PIXMAP = 6,
    SVGA_CMD_DEFINE_PIXMAP_SCANLINE = 7,
    SVGA_CMD_RECT_BITMAP_FILL = 8,
    SVGA_CMD_RECT_PIXMAP_FILL = 9,
    SVGA_CMD_RECT_BITMAP_COPY = 10,
    SVGA_CMD_RECT_PIXMAP_COPY = 11,
    SVGA_CMD_FREE_OBJECT = 12,
    SVGA_CMD_RECT_ROP_FILL = 13,
    SVGA_CMD_RECT_ROP_COPY = 14,
    SVGA_CMD_RECT_ROP_BITMAP_FILL = 15,
    SVGA_CMD_RECT_ROP_PIXMAP_FILL = 16,
    SVGA_CMD_RECT_ROP_BITMAP_COPY = 17,
    SVGA_CMD_RECT_ROP_PIXMAP_COPY = 18,
    SVGA_CMD_DEFINE_CURSOR = 19,
    SVGA_CMD_DISPLAY_CURSOR = 20,
    SVGA_CMD_MOVE_CURSOR = 21,
    SVGA_CMD_DEFINE_ALPHA_CURSOR = 22,
    SVGA_CMD_DRAW_GLYPH = 23,
    SVGA_CMD_DRAW_GLYPH_CLIPPED = 24,
    SVGA_CMD_UPDATE_VERBOSE = 25,
    SVGA_CMD_SURFACE_FILL = 26,
    SVGA_CMD_SURFACE_COPY = 27,
    SVGA_CMD_SURFACE_ALPHA_BLEND = 28,
    SVGA_CMD_FRONT_ROP_FILL = 29,
    SVGA_CMD_FENCE = 30,
};

/* Legal values for the SVGA_REG_CURSOR_ON register in cursor bypass mode */
enum {
    SVGA_CURSOR_ON_HIDE = 0,
    SVGA_CURSOR_ON_SHOW = 1,
    SVGA_CURSOR_ON_REMOVE_FROM_FB = 2,
    SVGA_CURSOR_ON_RESTORE_TO_FB = 3,
};

static inline void vmsvga_update_rect(struct vmsvga_state_s *s,
                int x, int y, int w, int h)
{
    int line;
    int bypl;
    int width;
    int start;
    uint8_t *src;
    uint8_t *dst;

    if (x + w > s->width) {
        fprintf(stderr, "%s: update width too large x: %d, w: %d\n",
                        __FUNCTION__, x, w);
        x = MIN(x, s->width);
        w = s->width - x;
    }

    if (y + h > s->height) {
        fprintf(stderr, "%s: update height too large y: %d, h: %d\n",
                        __FUNCTION__, y, h);
        y = MIN(y, s->height);
        h = s->height - y;
    }

    line = h;
    bypl = s->bypp * s->width;
    width = s->bypp * w;
    start = s->bypp * x + bypl * y;
    src = s->vga.vram_ptr + start;
    dst = ds_get_data(s->vga.ds) + start;

    for (; line > 0; line --, src += bypl, dst += bypl)
        memcpy(dst, src, width);

    dpy_update(s->vga.ds, x, y, w, h);
}

static inline void vmsvga_update_screen(struct vmsvga_state_s *s)
{
    memcpy(ds_get_data(s->vga.ds), s->vga.vram_ptr,
           s->bypp * s->width * s->height);
    dpy_update(s->vga.ds, 0, 0, s->width, s->height);
}

static inline void vmsvga_update_rect_delayed(struct vmsvga_state_s *s,
                int x, int y, int w, int h)
{
    struct vmsvga_rect_s *rect = &s->redraw_fifo[s->redraw_fifo_last ++];
    s->redraw_fifo_last &= REDRAW_FIFO_LEN - 1;
    rect->x = x;
    rect->y = y;
    rect->w = w;
    rect->h = h;
}

static inline void vmsvga_update_rect_flush(struct vmsvga_state_s *s)
{
    struct vmsvga_rect_s *rect;
    if (s->invalidated) {
        s->redraw_fifo_first = s->redraw_fifo_last;
        return;
    }
    /* Overlapping region updates can be optimised out here - if someone
     * knows a smart algorithm to do that, please share.  */
    while (s->redraw_fifo_first != s->redraw_fifo_last) {
        rect = &s->redraw_fifo[s->redraw_fifo_first ++];
        s->redraw_fifo_first &= REDRAW_FIFO_LEN - 1;
        vmsvga_update_rect(s, rect->x, rect->y, rect->w, rect->h);
    }
}

#ifdef HW_RECT_ACCEL
static inline void vmsvga_copy_rect(struct vmsvga_state_s *s,
                int x0, int y0, int x1, int y1, int w, int h)
{
    uint8_t *vram = s->vga.vram_ptr;
    int bypl = s->bypp * s->width;
    int width = s->bypp * w;
    int line = h;
    uint8_t *ptr[2];

    if (y1 > y0) {
        ptr[0] = vram + s->bypp * x0 + bypl * (y0 + h - 1);
        ptr[1] = vram + s->bypp * x1 + bypl * (y1 + h - 1);
        for (; line > 0; line --, ptr[0] -= bypl, ptr[1] -= bypl) {
            memmove(ptr[1], ptr[0], width);
        }
    } else {
        ptr[0] = vram + s->bypp * x0 + bypl * y0;
        ptr[1] = vram + s->bypp * x1 + bypl * y1;
        for (; line > 0; line --, ptr[0] += bypl, ptr[1] += bypl) {
            memmove(ptr[1], ptr[0], width);
        }
    }

    vmsvga_update_rect_delayed(s, x1, y1, w, h);
}
#endif

#ifdef HW_FILL_ACCEL
static inline void vmsvga_fill_rect(struct vmsvga_state_s *s,
                uint32_t c, int x, int y, int w, int h)
{
    uint8_t *vram = s->vga.vram_ptr;
    int bypp = s->bypp;
    int bypl = bypp * s->width;
    int width = bypp * w;
    int line = h;
    int column;
    uint8_t *fst = vram + bypp * x + bypl * y;
    uint8_t *dst;
    uint8_t *src;
    uint8_t col[4];

    col[0] = c;
    col[1] = c >> 8;
    col[2] = c >> 16;
    col[3] = c >> 24;

    if (line--) {
        dst = fst;
        src = col;
        for (column = width; column > 0; column--) {
            *(dst++) = *(src++);
            if (src - col == bypp) {
                src = col;
            }
        }
        dst = fst;
        for (; line > 0; line--) {
            dst += bypl;
            memcpy(dst, fst, width);
        }
    }

    vmsvga_update_rect_delayed(s, x, y, w, h);
}
#endif

struct vmsvga_cursor_definition_s {
    int width;
    int height;
    int id;
    int bpp;
    int hot_x;
    int hot_y;
    uint32_t mask[1024];
    uint32_t image[4096];
};

#define SVGA_BITMAP_SIZE(w, h)		((((w) + 31) >> 5) * (h))
#define SVGA_PIXMAP_SIZE(w, h, bpp)	(((((w) * (bpp)) + 31) >> 5) * (h))

#ifdef HW_MOUSE_ACCEL
static inline void vmsvga_cursor_define(struct vmsvga_state_s *s,
                struct vmsvga_cursor_definition_s *c)
{
    QEMUCursor *qc;
    int i, pixels;

    qc = cursor_alloc(c->width, c->height);
    qc->hot_x = c->hot_x;
    qc->hot_y = c->hot_y;
    switch (c->bpp) {
    case 1:
        cursor_set_mono(qc, 0xffffff, 0x000000, (void*)c->image,
                        1, (void*)c->mask);
#ifdef DEBUG
        cursor_print_ascii_art(qc, "vmware/mono");
#endif
        break;
    case 32:
        /* fill alpha channel from mask, set color to zero */
        cursor_set_mono(qc, 0x000000, 0x000000, (void*)c->mask,
                        1, (void*)c->mask);
        /* add in rgb values */
        pixels = c->width * c->height;
        for (i = 0; i < pixels; i++) {
            qc->data[i] |= c->image[i] & 0xffffff;
        }
#ifdef DEBUG
        cursor_print_ascii_art(qc, "vmware/32bit");
#endif
        break;
    default:
        fprintf(stderr, "%s: unhandled bpp %d, using fallback cursor\n",
                __FUNCTION__, c->bpp);
        cursor_put(qc);
        qc = cursor_builtin_left_ptr();
    }

    if (s->vga.ds->cursor_define)
        s->vga.ds->cursor_define(qc);
    cursor_put(qc);
}
#endif

#define CMD(f)	le32_to_cpu(s->cmd->f)

static inline int vmsvga_fifo_length(struct vmsvga_state_s *s)
{
    int num;
    if (!s->config || !s->enable)
        return 0;
    num = CMD(next_cmd) - CMD(stop);
    if (num < 0)
        num += CMD(max) - CMD(min);
    return num >> 2;
}

static inline uint32_t vmsvga_fifo_read_raw(struct vmsvga_state_s *s)
{
    uint32_t cmd = s->fifo[CMD(stop) >> 2];
    s->cmd->stop = cpu_to_le32(CMD(stop) + 4);
    if (CMD(stop) >= CMD(max))
        s->cmd->stop = s->cmd->min;
    return cmd;
}

static inline uint32_t vmsvga_fifo_read(struct vmsvga_state_s *s)
{
    return le32_to_cpu(vmsvga_fifo_read_raw(s));
}

static void vmsvga_fifo_run(struct vmsvga_state_s *s)
{
    uint32_t cmd, colour;
    int args, len;
    int x, y, dx, dy, width, height;
    struct vmsvga_cursor_definition_s cursor;
    uint32_t cmd_start;

    len = vmsvga_fifo_length(s);
    while (len > 0) {
        /* May need to go back to the start of the command if incomplete */
        cmd_start = s->cmd->stop;

        switch (cmd = vmsvga_fifo_read(s)) {
        case SVGA_CMD_UPDATE:
        case SVGA_CMD_UPDATE_VERBOSE:
            len -= 5;
            if (len < 0)
                goto rewind;

            x = vmsvga_fifo_read(s);
            y = vmsvga_fifo_read(s);
            width = vmsvga_fifo_read(s);
            height = vmsvga_fifo_read(s);
            vmsvga_update_rect_delayed(s, x, y, width, height);
            break;

        case SVGA_CMD_RECT_FILL:
            len -= 6;
            if (len < 0)
                goto rewind;

            colour = vmsvga_fifo_read(s);
            x = vmsvga_fifo_read(s);
            y = vmsvga_fifo_read(s);
            width = vmsvga_fifo_read(s);
            height = vmsvga_fifo_read(s);
#ifdef HW_FILL_ACCEL
            vmsvga_fill_rect(s, colour, x, y, width, height);
            break;
#else
            args = 0;
            goto badcmd;
#endif

        case SVGA_CMD_RECT_COPY:
            len -= 7;
            if (len < 0)
                goto rewind;

            x = vmsvga_fifo_read(s);
            y = vmsvga_fifo_read(s);
            dx = vmsvga_fifo_read(s);
            dy = vmsvga_fifo_read(s);
            width = vmsvga_fifo_read(s);
            height = vmsvga_fifo_read(s);
#ifdef HW_RECT_ACCEL
            vmsvga_copy_rect(s, x, y, dx, dy, width, height);
            break;
#else
            args = 0;
            goto badcmd;
#endif

        case SVGA_CMD_DEFINE_CURSOR:
            len -= 8;
            if (len < 0)
                goto rewind;

            cursor.id = vmsvga_fifo_read(s);
            cursor.hot_x = vmsvga_fifo_read(s);
            cursor.hot_y = vmsvga_fifo_read(s);
            cursor.width = x = vmsvga_fifo_read(s);
            cursor.height = y = vmsvga_fifo_read(s);
            vmsvga_fifo_read(s);
            cursor.bpp = vmsvga_fifo_read(s);

            args = SVGA_BITMAP_SIZE(x, y) + SVGA_PIXMAP_SIZE(x, y, cursor.bpp);
            if (SVGA_BITMAP_SIZE(x, y) > sizeof cursor.mask ||
                SVGA_PIXMAP_SIZE(x, y, cursor.bpp) > sizeof cursor.image)
                    goto badcmd;

            len -= args;
            if (len < 0)
                goto rewind;

            for (args = 0; args < SVGA_BITMAP_SIZE(x, y); args ++)
                cursor.mask[args] = vmsvga_fifo_read_raw(s);
            for (args = 0; args < SVGA_PIXMAP_SIZE(x, y, cursor.bpp); args ++)
                cursor.image[args] = vmsvga_fifo_read_raw(s);
#ifdef HW_MOUSE_ACCEL
            vmsvga_cursor_define(s, &cursor);
            break;
#else
            args = 0;
            goto badcmd;
#endif

        /*
         * Other commands that we at least know the number of arguments
         * for so we can avoid FIFO desync if driver uses them illegally.
         */
        case SVGA_CMD_DEFINE_ALPHA_CURSOR:
            len -= 6;
            if (len < 0)
                goto rewind;

            vmsvga_fifo_read(s);
            vmsvga_fifo_read(s);
            vmsvga_fifo_read(s);
            x = vmsvga_fifo_read(s);
            y = vmsvga_fifo_read(s);
            args = x * y;
            goto badcmd;
        case SVGA_CMD_RECT_ROP_FILL:
            args = 6;
            goto badcmd;
        case SVGA_CMD_RECT_ROP_COPY:
            args = 7;
            goto badcmd;
        case SVGA_CMD_DRAW_GLYPH_CLIPPED:
            len -= 4;
            if (len < 0)
                goto rewind;

            vmsvga_fifo_read(s);
            vmsvga_fifo_read(s);
            args = 7 + (vmsvga_fifo_read(s) >> 2);
            goto badcmd;
        case SVGA_CMD_SURFACE_ALPHA_BLEND:
            args = 12;
            goto badcmd;

        /*
         * Other commands that are not listed as depending on any
         * CAPABILITIES bits, but are not described in the README either.
         */
        case SVGA_CMD_SURFACE_FILL:
        case SVGA_CMD_SURFACE_COPY:
        case SVGA_CMD_FRONT_ROP_FILL:
        case SVGA_CMD_FENCE:
        case SVGA_CMD_INVALID_CMD:
            break; /* Nop */

        default:
            args = 0;
        badcmd:
            len -= args;
            if (len < 0)
                goto rewind;
            while (args --)
                vmsvga_fifo_read(s);
            printf("%s: Unknown command 0x%02x in SVGA command FIFO\n",
                            __FUNCTION__, cmd);
            break;

        rewind:
            s->cmd->stop = cmd_start;
            break;
        }
    }

    s->syncing = 0;
}

static uint32_t vmsvga_index_read(void *opaque, uint32_t address)
{
    struct vmsvga_state_s *s = opaque;
    return s->index;
}

static void vmsvga_index_write(void *opaque, uint32_t address, uint32_t index)
{
    struct vmsvga_state_s *s = opaque;
    s->index = index;
}

static uint32_t vmsvga_value_read(void *opaque, uint32_t address)
{
    uint32_t caps;
    struct vmsvga_state_s *s = opaque;
    switch (s->index) {
    case SVGA_REG_ID:
        return s->svgaid;

    case SVGA_REG_ENABLE:
        return s->enable;

    case SVGA_REG_WIDTH:
        return s->width;

    case SVGA_REG_HEIGHT:
        return s->height;

    case SVGA_REG_MAX_WIDTH:
        return SVGA_MAX_WIDTH;

    case SVGA_REG_MAX_HEIGHT:
        return SVGA_MAX_HEIGHT;

    case SVGA_REG_DEPTH:
        return s->depth;

    case SVGA_REG_BITS_PER_PIXEL:
        return (s->depth + 7) & ~7;

    case SVGA_REG_PSEUDOCOLOR:
        return 0x0;

    case SVGA_REG_RED_MASK:
        return s->wred;
    case SVGA_REG_GREEN_MASK:
        return s->wgreen;
    case SVGA_REG_BLUE_MASK:
        return s->wblue;

    case SVGA_REG_BYTES_PER_LINE:
        return ((s->depth + 7) >> 3) * s->new_width;

    case SVGA_REG_FB_START: {
        struct pci_vmsvga_state_s *pci_vmsvga
            = container_of(s, struct pci_vmsvga_state_s, chip);
        return pci_get_bar_addr(&pci_vmsvga->card, 1);
    }

    case SVGA_REG_FB_OFFSET:
        return 0x0;

    case SVGA_REG_VRAM_SIZE:
        return s->vga.vram_size;

    case SVGA_REG_FB_SIZE:
        return s->fb_size;

    case SVGA_REG_CAPABILITIES:
        caps = SVGA_CAP_NONE;
#ifdef HW_RECT_ACCEL
        caps |= SVGA_CAP_RECT_COPY;
#endif
#ifdef HW_FILL_ACCEL
        caps |= SVGA_CAP_RECT_FILL;
#endif
#ifdef HW_MOUSE_ACCEL
        if (s->vga.ds->mouse_set)
            caps |= SVGA_CAP_CURSOR | SVGA_CAP_CURSOR_BYPASS_2 |
                    SVGA_CAP_CURSOR_BYPASS;
#endif
        return caps;

    case SVGA_REG_MEM_START: {
        struct pci_vmsvga_state_s *pci_vmsvga
            = container_of(s, struct pci_vmsvga_state_s, chip);
        return pci_get_bar_addr(&pci_vmsvga->card, 2);
    }

    case SVGA_REG_MEM_SIZE:
        return s->fifo_size;

    case SVGA_REG_CONFIG_DONE:
        return s->config;

    case SVGA_REG_SYNC:
    case SVGA_REG_BUSY:
        return s->syncing;

    case SVGA_REG_GUEST_ID:
        return s->guest;

    case SVGA_REG_CURSOR_ID:
        return s->cursor.id;

    case SVGA_REG_CURSOR_X:
        return s->cursor.x;

    case SVGA_REG_CURSOR_Y:
        return s->cursor.x;

    case SVGA_REG_CURSOR_ON:
        return s->cursor.on;

    case SVGA_REG_HOST_BITS_PER_PIXEL:
        return (s->depth + 7) & ~7;

    case SVGA_REG_SCRATCH_SIZE:
        return s->scratch_size;

    case SVGA_REG_MEM_REGS:
    case SVGA_REG_NUM_DISPLAYS:
    case SVGA_REG_PITCHLOCK:
    case SVGA_PALETTE_BASE ... SVGA_PALETTE_END:
        return 0;

    default:
        if (s->index >= SVGA_SCRATCH_BASE &&
                s->index < SVGA_SCRATCH_BASE + s->scratch_size)
            return s->scratch[s->index - SVGA_SCRATCH_BASE];
        printf("%s: Bad register %02x\n", __FUNCTION__, s->index);
    }

    return 0;
}

static void vmsvga_value_write(void *opaque, uint32_t address, uint32_t value)
{
    struct vmsvga_state_s *s = opaque;
    switch (s->index) {
    case SVGA_REG_ID:
        if (value == SVGA_ID_2 || value == SVGA_ID_1 || value == SVGA_ID_0)
            s->svgaid = value;
        break;

    case SVGA_REG_ENABLE:
        s->enable = value;
        s->config &= !!value;
        s->width = -1;
        s->height = -1;
        s->invalidated = 1;
        s->vga.invalidate(&s->vga);
        if (s->enable) {
            s->fb_size = ((s->depth + 7) >> 3) * s->new_width * s->new_height;
            vga_dirty_log_stop(&s->vga);
        } else {
            vga_dirty_log_start(&s->vga);
        }
        break;

    case SVGA_REG_WIDTH:
        s->new_width = value;
        s->invalidated = 1;
        break;

    case SVGA_REG_HEIGHT:
        s->new_height = value;
        s->invalidated = 1;
        break;

    case SVGA_REG_DEPTH:
    case SVGA_REG_BITS_PER_PIXEL:
        if (value != s->depth) {
            printf("%s: Bad colour depth: %i bits\n", __FUNCTION__, value);
            s->config = 0;
        }
        break;

    case SVGA_REG_CONFIG_DONE:
        if (value) {
            s->fifo = (uint32_t *) s->fifo_ptr;
            /* Check range and alignment.  */
            if ((CMD(min) | CMD(max) |
                        CMD(next_cmd) | CMD(stop)) & 3)
                break;
            if (CMD(min) < (uint8_t *) s->cmd->fifo - (uint8_t *) s->fifo)
                break;
            if (CMD(max) > SVGA_FIFO_SIZE)
                break;
            if (CMD(max) < CMD(min) + 10 * 1024)
                break;
        }
        s->config = !!value;
        break;

    case SVGA_REG_SYNC:
        s->syncing = 1;
        vmsvga_fifo_run(s); /* Or should we just wait for update_display? */
        break;

    case SVGA_REG_GUEST_ID:
        s->guest = value;
#ifdef VERBOSE
        if (value >= GUEST_OS_BASE && value < GUEST_OS_BASE +
                ARRAY_SIZE(vmsvga_guest_id))
            printf("%s: guest runs %s.\n", __FUNCTION__,
                            vmsvga_guest_id[value - GUEST_OS_BASE]);
#endif
        break;

    case SVGA_REG_CURSOR_ID:
        s->cursor.id = value;
        break;

    case SVGA_REG_CURSOR_X:
        s->cursor.x = value;
        break;

    case SVGA_REG_CURSOR_Y:
        s->cursor.y = value;
        break;

    case SVGA_REG_CURSOR_ON:
        s->cursor.on |= (value == SVGA_CURSOR_ON_SHOW);
        s->cursor.on &= (value != SVGA_CURSOR_ON_HIDE);
#ifdef HW_MOUSE_ACCEL
        if (s->vga.ds->mouse_set && value <= SVGA_CURSOR_ON_SHOW)
            s->vga.ds->mouse_set(s->cursor.x, s->cursor.y, s->cursor.on);
#endif
        break;

    case SVGA_REG_MEM_REGS:
    case SVGA_REG_NUM_DISPLAYS:
    case SVGA_REG_PITCHLOCK:
    case SVGA_PALETTE_BASE ... SVGA_PALETTE_END:
        break;

    default:
        if (s->index >= SVGA_SCRATCH_BASE &&
                s->index < SVGA_SCRATCH_BASE + s->scratch_size) {
            s->scratch[s->index - SVGA_SCRATCH_BASE] = value;
            break;
        }
        printf("%s: Bad register %02x\n", __FUNCTION__, s->index);
    }
}

static uint32_t vmsvga_bios_read(void *opaque, uint32_t address)
{
    printf("%s: what are we supposed to return?\n", __FUNCTION__);
    return 0xcafe;
}

static void vmsvga_bios_write(void *opaque, uint32_t address, uint32_t data)
{
    printf("%s: what are we supposed to do with (%08x)?\n",
                    __FUNCTION__, data);
}

static inline void vmsvga_size(struct vmsvga_state_s *s)
{
    if (s->new_width != s->width || s->new_height != s->height) {
        s->width = s->new_width;
        s->height = s->new_height;
        qemu_console_resize(s->vga.ds, s->width, s->height);
        s->invalidated = 1;
    }
}

static void vmsvga_update_display(void *opaque)
{
    struct vmsvga_state_s *s = opaque;
    if (!s->enable) {
        s->vga.update(&s->vga);
        return;
    }

    vmsvga_size(s);

    vmsvga_fifo_run(s);
    vmsvga_update_rect_flush(s);

    /*
     * Is it more efficient to look at vram VGA-dirty bits or wait
     * for the driver to issue SVGA_CMD_UPDATE?
     */
    if (s->invalidated) {
        s->invalidated = 0;
        vmsvga_update_screen(s);
    }
}

static void vmsvga_reset(DeviceState *dev)
{
    struct pci_vmsvga_state_s *pci =
        DO_UPCAST(struct pci_vmsvga_state_s, card.qdev, dev);
    struct vmsvga_state_s *s = &pci->chip;

    s->index = 0;
    s->enable = 0;
    s->config = 0;
    s->width = -1;
    s->height = -1;
    s->svgaid = SVGA_ID;
    s->cursor.on = 0;
    s->redraw_fifo_first = 0;
    s->redraw_fifo_last = 0;
    s->syncing = 0;

    vga_dirty_log_start(&s->vga);
}

static void vmsvga_invalidate_display(void *opaque)
{
    struct vmsvga_state_s *s = opaque;
    if (!s->enable) {
        s->vga.invalidate(&s->vga);
        return;
    }

    s->invalidated = 1;
}

/* save the vga display in a PPM image even if no display is
   available */
static void vmsvga_screen_dump(void *opaque, const char *filename)
{
    struct vmsvga_state_s *s = opaque;
    if (!s->enable) {
        s->vga.screen_dump(&s->vga, filename);
        return;
    }

    if (s->depth == 32) {
        DisplaySurface *ds = qemu_create_displaysurface_from(s->width,
                s->height, 32, ds_get_linesize(s->vga.ds), s->vga.vram_ptr);
        ppm_save(filename, ds);
        g_free(ds);
    }
}

static void vmsvga_text_update(void *opaque, console_ch_t *chardata)
{
    struct vmsvga_state_s *s = opaque;

    if (s->vga.text_update)
        s->vga.text_update(&s->vga, chardata);
}

static int vmsvga_post_load(void *opaque, int version_id)
{
    struct vmsvga_state_s *s = opaque;

    s->invalidated = 1;
    if (s->config)
        s->fifo = (uint32_t *) s->fifo_ptr;

    return 0;
}

static const VMStateDescription vmstate_vmware_vga_internal = {
    .name = "vmware_vga_internal",
    .version_id = 0,
    .minimum_version_id = 0,
    .minimum_version_id_old = 0,
    .post_load = vmsvga_post_load,
    .fields      = (VMStateField []) {
        VMSTATE_INT32_EQUAL(depth, struct vmsvga_state_s),
        VMSTATE_INT32(enable, struct vmsvga_state_s),
        VMSTATE_INT32(config, struct vmsvga_state_s),
        VMSTATE_INT32(cursor.id, struct vmsvga_state_s),
        VMSTATE_INT32(cursor.x, struct vmsvga_state_s),
        VMSTATE_INT32(cursor.y, struct vmsvga_state_s),
        VMSTATE_INT32(cursor.on, struct vmsvga_state_s),
        VMSTATE_INT32(index, struct vmsvga_state_s),
        VMSTATE_VARRAY_INT32(scratch, struct vmsvga_state_s,
                             scratch_size, 0, vmstate_info_uint32, uint32_t),
        VMSTATE_INT32(new_width, struct vmsvga_state_s),
        VMSTATE_INT32(new_height, struct vmsvga_state_s),
        VMSTATE_UINT32(guest, struct vmsvga_state_s),
        VMSTATE_UINT32(svgaid, struct vmsvga_state_s),
        VMSTATE_INT32(syncing, struct vmsvga_state_s),
        VMSTATE_INT32(fb_size, struct vmsvga_state_s),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_vmware_vga = {
    .name = "vmware_vga",
    .version_id = 0,
    .minimum_version_id = 0,
    .minimum_version_id_old = 0,
    .fields      = (VMStateField []) {
        VMSTATE_PCI_DEVICE(card, struct pci_vmsvga_state_s),
        VMSTATE_STRUCT(chip, struct pci_vmsvga_state_s, 0,
                       vmstate_vmware_vga_internal, struct vmsvga_state_s),
        VMSTATE_END_OF_LIST()
    }
};

static void vmsvga_init(struct vmsvga_state_s *s, int vga_ram_size,
                        MemoryRegion *address_space)
{
    s->scratch_size = SVGA_SCRATCH_SIZE;
    s->scratch = g_malloc(s->scratch_size * 4);

    s->vga.ds = graphic_console_init(vmsvga_update_display,
                                     vmsvga_invalidate_display,
                                     vmsvga_screen_dump,
                                     vmsvga_text_update, s);


    s->fifo_size = SVGA_FIFO_SIZE;
    memory_region_init_ram(&s->fifo_ram, NULL, "vmsvga.fifo", s->fifo_size);
    s->fifo_ptr = memory_region_get_ram_ptr(&s->fifo_ram);

    vga_common_init(&s->vga, vga_ram_size);
    vga_init(&s->vga, address_space);
    vmstate_register(NULL, 0, &vmstate_vga_common, &s->vga);

    s->depth = ds_get_bits_per_pixel(s->vga.ds);
    s->bypp = ds_get_bytes_per_pixel(s->vga.ds);
    switch (s->depth) {
    case 8:
        s->wred   = 0x00000007;
        s->wgreen = 0x00000038;
        s->wblue  = 0x000000c0;
        break;
    case 15:
        s->wred   = 0x0000001f;
        s->wgreen = 0x000003e0;
        s->wblue  = 0x00007c00;
        break;
    case 16:
        s->wred   = 0x0000001f;
        s->wgreen = 0x000007e0;
        s->wblue  = 0x0000f800;
        break;
    case 24:
        s->wred   = 0x00ff0000;
        s->wgreen = 0x0000ff00;
        s->wblue  = 0x000000ff;
        break;
    case 32:
        s->wred   = 0x00ff0000;
        s->wgreen = 0x0000ff00;
        s->wblue  = 0x000000ff;
        break;
    }
}

static uint64_t vmsvga_io_read(void *opaque, target_phys_addr_t addr,
                               unsigned size)
{
    struct vmsvga_state_s *s = opaque;

    switch (addr) {
    case SVGA_IO_MUL * SVGA_INDEX_PORT: return vmsvga_index_read(s, addr);
    case SVGA_IO_MUL * SVGA_VALUE_PORT: return vmsvga_value_read(s, addr);
    case SVGA_IO_MUL * SVGA_BIOS_PORT: return vmsvga_bios_read(s, addr);
    default: return -1u;
    }
}

static void vmsvga_io_write(void *opaque, target_phys_addr_t addr,
                            uint64_t data, unsigned size)
{
    struct vmsvga_state_s *s = opaque;

    switch (addr) {
    case SVGA_IO_MUL * SVGA_INDEX_PORT:
        return vmsvga_index_write(s, addr, data);
    case SVGA_IO_MUL * SVGA_VALUE_PORT:
        return vmsvga_value_write(s, addr, data);
    case SVGA_IO_MUL * SVGA_BIOS_PORT:
        return vmsvga_bios_write(s, addr, data);
    }
}

static const MemoryRegionOps vmsvga_io_ops = {
    .read = vmsvga_io_read,
    .write = vmsvga_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static int pci_vmsvga_initfn(PCIDevice *dev)
{
    struct pci_vmsvga_state_s *s =
        DO_UPCAST(struct pci_vmsvga_state_s, card, dev);
    MemoryRegion *iomem;

    iomem = &s->chip.vga.vram;

    vga_dirty_log_restart(&s->chip.vga);

    s->card.config[PCI_CACHE_LINE_SIZE]	= 0x08;		/* Cache line size */
    s->card.config[PCI_LATENCY_TIMER] = 0x40;		/* Latency timer */
    s->card.config[PCI_INTERRUPT_LINE] = 0xff;		/* End */

    memory_region_init_io(&s->io_bar, &vmsvga_io_ops, &s->chip,
                          "vmsvga-io", 0x10);
    pci_register_bar(&s->card, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->io_bar);

    vmsvga_init(&s->chip, VGA_RAM_SIZE, pci_address_space(dev));

    pci_register_bar(&s->card, 1, PCI_BASE_ADDRESS_MEM_PREFETCH, iomem);
    pci_register_bar(&s->card, 2, PCI_BASE_ADDRESS_MEM_PREFETCH,
                     &s->chip.fifo_ram);

    if (!dev->rom_bar) {
        /* compatibility with pc-0.13 and older */
        vga_init_vbe(&s->chip.vga, pci_address_space(dev));
    }

    return 0;
}

static PCIDeviceInfo vmsvga_info = {
    .qdev.name    = "vmware-svga",
    .qdev.size    = sizeof(struct pci_vmsvga_state_s),
    .qdev.vmsd    = &vmstate_vmware_vga,
    .qdev.reset   = vmsvga_reset,
    .no_hotplug   = 1,
    .init         = pci_vmsvga_initfn,
    .romfile      = "vgabios-vmware.bin",

    .vendor_id    =  PCI_VENDOR_ID_VMWARE,
    .device_id    = SVGA_PCI_DEVICE_ID,
    .class_id     = PCI_CLASS_DISPLAY_VGA,
    .subsystem_vendor_id = PCI_VENDOR_ID_VMWARE,
    .subsystem_id = SVGA_PCI_DEVICE_ID,
};

static void vmsvga_register(void)
{
    pci_qdev_register(&vmsvga_info);
}
device_init(vmsvga_register);
