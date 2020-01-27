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

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/loader.h"
#include "trace.h"
#include "ui/vnc.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#undef VERBOSE
#define HW_RECT_ACCEL
#define HW_FILL_ACCEL
#define HW_MOUSE_ACCEL

#include "vga_int.h"

/* See http://vmware-svga.sf.net/ for some documentation on VMWare SVGA */

struct vmsvga_state_s {
    VGACommonState vga;

    int invalidated;
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
    int new_depth;
    uint32_t guest;
    uint32_t svgaid;
    int syncing;

    MemoryRegion fifo_ram;
    uint8_t *fifo_ptr;
    unsigned int fifo_size;

    uint32_t *fifo;
    uint32_t fifo_min;
    uint32_t fifo_max;
    uint32_t fifo_next;
    uint32_t fifo_stop;

#define REDRAW_FIFO_LEN  512
    struct vmsvga_rect_s {
        int x, y, w, h;
    } redraw_fifo[REDRAW_FIFO_LEN];
    int redraw_fifo_first, redraw_fifo_last;
};

#define TYPE_VMWARE_SVGA "vmware-svga"

#define VMWARE_SVGA(obj) \
    OBJECT_CHECK(struct pci_vmsvga_state_s, (obj), TYPE_VMWARE_SVGA)

struct pci_vmsvga_state_s {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/

    struct vmsvga_state_s chip;
    MemoryRegion io_bar;
};

#define SVGA_MAGIC              0x900000UL
#define SVGA_MAKE_ID(ver)       (SVGA_MAGIC << 8 | (ver))
#define SVGA_ID_0               SVGA_MAKE_ID(0)
#define SVGA_ID_1               SVGA_MAKE_ID(1)
#define SVGA_ID_2               SVGA_MAKE_ID(2)

#define SVGA_LEGACY_BASE_PORT   0x4560
#define SVGA_INDEX_PORT         0x0
#define SVGA_VALUE_PORT         0x1
#define SVGA_BIOS_PORT          0x2

#define SVGA_VERSION_2

#ifdef SVGA_VERSION_2
# define SVGA_ID                SVGA_ID_2
# define SVGA_IO_BASE           SVGA_LEGACY_BASE_PORT
# define SVGA_IO_MUL            1
# define SVGA_FIFO_SIZE         0x10000
# define SVGA_PCI_DEVICE_ID     PCI_DEVICE_ID_VMWARE_SVGA2
#else
# define SVGA_ID                SVGA_ID_1
# define SVGA_IO_BASE           SVGA_LEGACY_BASE_PORT
# define SVGA_IO_MUL            4
# define SVGA_FIFO_SIZE         0x10000
# define SVGA_PCI_DEVICE_ID     PCI_DEVICE_ID_VMWARE_SVGA
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
    SVGA_REG_BITS_PER_PIXEL = 7,        /* Current bpp in the guest */
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
    SVGA_REG_MEM_START = 18,            /* Memory for command FIFO */
    SVGA_REG_MEM_SIZE = 19,
    SVGA_REG_CONFIG_DONE = 20,          /* Set when memory area configured */
    SVGA_REG_SYNC = 21,                 /* Write to force synchronization */
    SVGA_REG_BUSY = 22,                 /* Read to check if sync is done */
    SVGA_REG_GUEST_ID = 23,             /* Set guest OS identifier */
    SVGA_REG_CURSOR_ID = 24,            /* ID of cursor */
    SVGA_REG_CURSOR_X = 25,             /* Set cursor X position */
    SVGA_REG_CURSOR_Y = 26,             /* Set cursor Y position */
    SVGA_REG_CURSOR_ON = 27,            /* Turn cursor on/off */
    SVGA_REG_HOST_BITS_PER_PIXEL = 28,  /* Current bpp in the host */
    SVGA_REG_SCRATCH_SIZE = 29,         /* Number of scratch registers */
    SVGA_REG_MEM_REGS = 30,             /* Number of FIFO registers */
    SVGA_REG_NUM_DISPLAYS = 31,         /* Number of guest displays */
    SVGA_REG_PITCHLOCK = 32,            /* Fixed pitch for all modes */

    SVGA_PALETTE_BASE = 1024,           /* Base of SVGA color map */
    SVGA_PALETTE_END  = SVGA_PALETTE_BASE + 767,
    SVGA_SCRATCH_BASE = SVGA_PALETTE_BASE + 768,
};

#define SVGA_CAP_NONE                   0
#define SVGA_CAP_RECT_FILL              (1 << 0)
#define SVGA_CAP_RECT_COPY              (1 << 1)
#define SVGA_CAP_RECT_PAT_FILL          (1 << 2)
#define SVGA_CAP_LEGACY_OFFSCREEN       (1 << 3)
#define SVGA_CAP_RASTER_OP              (1 << 4)
#define SVGA_CAP_CURSOR                 (1 << 5)
#define SVGA_CAP_CURSOR_BYPASS          (1 << 6)
#define SVGA_CAP_CURSOR_BYPASS_2        (1 << 7)
#define SVGA_CAP_8BIT_EMULATION         (1 << 8)
#define SVGA_CAP_ALPHA_CURSOR           (1 << 9)
#define SVGA_CAP_GLYPH                  (1 << 10)
#define SVGA_CAP_GLYPH_CLIPPING         (1 << 11)
#define SVGA_CAP_OFFSCREEN_1            (1 << 12)
#define SVGA_CAP_ALPHA_BLEND            (1 << 13)
#define SVGA_CAP_3D                     (1 << 14)
#define SVGA_CAP_EXTENDED_FIFO          (1 << 15)
#define SVGA_CAP_MULTIMON               (1 << 16)
#define SVGA_CAP_PITCHLOCK              (1 << 17)

/*
 * FIFO offsets (seen as an array of 32-bit words)
 */
enum {
    /*
     * The original defined FIFO offsets
     */
    SVGA_FIFO_MIN = 0,
    SVGA_FIFO_MAX,      /* The distance from MIN to MAX must be at least 10K */
    SVGA_FIFO_NEXT,
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

#define SVGA_FIFO_CAP_NONE              0
#define SVGA_FIFO_CAP_FENCE             (1 << 0)
#define SVGA_FIFO_CAP_ACCELFRONT        (1 << 1)
#define SVGA_FIFO_CAP_PITCHLOCK         (1 << 2)

#define SVGA_FIFO_FLAG_NONE             0
#define SVGA_FIFO_FLAG_ACCELFRONT       (1 << 0)

/* These values can probably be changed arbitrarily.  */
#define SVGA_SCRATCH_SIZE               0x8000
#define SVGA_MAX_WIDTH                  ROUND_UP(2360, VNC_DIRTY_PIXELS_PER_BIT)
#define SVGA_MAX_HEIGHT                 1770

#ifdef VERBOSE
# define GUEST_OS_BASE          0x5001
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

static inline bool vmsvga_verify_rect(DisplaySurface *surface,
                                      const char *name,
                                      int x, int y, int w, int h)
{
    if (x < 0) {
        fprintf(stderr, "%s: x was < 0 (%d)\n", name, x);
        return false;
    }
    if (x > SVGA_MAX_WIDTH) {
        fprintf(stderr, "%s: x was > %d (%d)\n", name, SVGA_MAX_WIDTH, x);
        return false;
    }
    if (w < 0) {
        fprintf(stderr, "%s: w was < 0 (%d)\n", name, w);
        return false;
    }
    if (w > SVGA_MAX_WIDTH) {
        fprintf(stderr, "%s: w was > %d (%d)\n", name, SVGA_MAX_WIDTH, w);
        return false;
    }
    if (x + w > surface_width(surface)) {
        fprintf(stderr, "%s: width was > %d (x: %d, w: %d)\n",
                name, surface_width(surface), x, w);
        return false;
    }

    if (y < 0) {
        fprintf(stderr, "%s: y was < 0 (%d)\n", name, y);
        return false;
    }
    if (y > SVGA_MAX_HEIGHT) {
        fprintf(stderr, "%s: y was > %d (%d)\n", name, SVGA_MAX_HEIGHT, y);
        return false;
    }
    if (h < 0) {
        fprintf(stderr, "%s: h was < 0 (%d)\n", name, h);
        return false;
    }
    if (h > SVGA_MAX_HEIGHT) {
        fprintf(stderr, "%s: h was > %d (%d)\n", name, SVGA_MAX_HEIGHT, h);
        return false;
    }
    if (y + h > surface_height(surface)) {
        fprintf(stderr, "%s: update height > %d (y: %d, h: %d)\n",
                name, surface_height(surface), y, h);
        return false;
    }

    return true;
}

static inline void vmsvga_update_rect(struct vmsvga_state_s *s,
                                      int x, int y, int w, int h)
{
    DisplaySurface *surface = qemu_console_surface(s->vga.con);
    int line;
    int bypl;
    int width;
    int start;
    uint8_t *src;
    uint8_t *dst;

    if (!vmsvga_verify_rect(surface, __func__, x, y, w, h)) {
        /* go for a fullscreen update as fallback */
        x = 0;
        y = 0;
        w = surface_width(surface);
        h = surface_height(surface);
    }

    bypl = surface_stride(surface);
    width = surface_bytes_per_pixel(surface) * w;
    start = surface_bytes_per_pixel(surface) * x + bypl * y;
    src = s->vga.vram_ptr + start;
    dst = surface_data(surface) + start;

    for (line = h; line > 0; line--, src += bypl, dst += bypl) {
        memcpy(dst, src, width);
    }
    dpy_gfx_update(s->vga.con, x, y, w, h);
}

static inline void vmsvga_update_rect_delayed(struct vmsvga_state_s *s,
                int x, int y, int w, int h)
{
    struct vmsvga_rect_s *rect = &s->redraw_fifo[s->redraw_fifo_last++];

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
        rect = &s->redraw_fifo[s->redraw_fifo_first++];
        s->redraw_fifo_first &= REDRAW_FIFO_LEN - 1;
        vmsvga_update_rect(s, rect->x, rect->y, rect->w, rect->h);
    }
}

#ifdef HW_RECT_ACCEL
static inline int vmsvga_copy_rect(struct vmsvga_state_s *s,
                int x0, int y0, int x1, int y1, int w, int h)
{
    DisplaySurface *surface = qemu_console_surface(s->vga.con);
    uint8_t *vram = s->vga.vram_ptr;
    int bypl = surface_stride(surface);
    int bypp = surface_bytes_per_pixel(surface);
    int width = bypp * w;
    int line = h;
    uint8_t *ptr[2];

    if (!vmsvga_verify_rect(surface, "vmsvga_copy_rect/src", x0, y0, w, h)) {
        return -1;
    }
    if (!vmsvga_verify_rect(surface, "vmsvga_copy_rect/dst", x1, y1, w, h)) {
        return -1;
    }

    if (y1 > y0) {
        ptr[0] = vram + bypp * x0 + bypl * (y0 + h - 1);
        ptr[1] = vram + bypp * x1 + bypl * (y1 + h - 1);
        for (; line > 0; line --, ptr[0] -= bypl, ptr[1] -= bypl) {
            memmove(ptr[1], ptr[0], width);
        }
    } else {
        ptr[0] = vram + bypp * x0 + bypl * y0;
        ptr[1] = vram + bypp * x1 + bypl * y1;
        for (; line > 0; line --, ptr[0] += bypl, ptr[1] += bypl) {
            memmove(ptr[1], ptr[0], width);
        }
    }

    vmsvga_update_rect_delayed(s, x1, y1, w, h);
    return 0;
}
#endif

#ifdef HW_FILL_ACCEL
static inline int vmsvga_fill_rect(struct vmsvga_state_s *s,
                uint32_t c, int x, int y, int w, int h)
{
    DisplaySurface *surface = qemu_console_surface(s->vga.con);
    int bypl = surface_stride(surface);
    int width = surface_bytes_per_pixel(surface) * w;
    int line = h;
    int column;
    uint8_t *fst;
    uint8_t *dst;
    uint8_t *src;
    uint8_t col[4];

    if (!vmsvga_verify_rect(surface, __func__, x, y, w, h)) {
        return -1;
    }

    col[0] = c;
    col[1] = c >> 8;
    col[2] = c >> 16;
    col[3] = c >> 24;

    fst = s->vga.vram_ptr + surface_bytes_per_pixel(surface) * x + bypl * y;

    if (line--) {
        dst = fst;
        src = col;
        for (column = width; column > 0; column--) {
            *(dst++) = *(src++);
            if (src - col == surface_bytes_per_pixel(surface)) {
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
    return 0;
}
#endif

struct vmsvga_cursor_definition_s {
    uint32_t width;
    uint32_t height;
    int id;
    uint32_t bpp;
    int hot_x;
    int hot_y;
    uint32_t mask[1024];
    uint32_t image[4096];
};

#define SVGA_BITMAP_SIZE(w, h)          ((((w) + 31) >> 5) * (h))
#define SVGA_PIXMAP_SIZE(w, h, bpp)     (((((w) * (bpp)) + 31) >> 5) * (h))

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
        cursor_set_mono(qc, 0xffffff, 0x000000, (void *)c->image,
                        1, (void *)c->mask);
#ifdef DEBUG
        cursor_print_ascii_art(qc, "vmware/mono");
#endif
        break;
    case 32:
        /* fill alpha channel from mask, set color to zero */
        cursor_set_mono(qc, 0x000000, 0x000000, (void *)c->mask,
                        1, (void *)c->mask);
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
                __func__, c->bpp);
        cursor_put(qc);
        qc = cursor_builtin_left_ptr();
    }

    dpy_cursor_define(s->vga.con, qc);
    cursor_put(qc);
}
#endif

static inline int vmsvga_fifo_length(struct vmsvga_state_s *s)
{
    int num;

    if (!s->config || !s->enable) {
        return 0;
    }

    s->fifo_min  = le32_to_cpu(s->fifo[SVGA_FIFO_MIN]);
    s->fifo_max  = le32_to_cpu(s->fifo[SVGA_FIFO_MAX]);
    s->fifo_next = le32_to_cpu(s->fifo[SVGA_FIFO_NEXT]);
    s->fifo_stop = le32_to_cpu(s->fifo[SVGA_FIFO_STOP]);

    /* Check range and alignment.  */
    if ((s->fifo_min | s->fifo_max | s->fifo_next | s->fifo_stop) & 3) {
        return 0;
    }
    if (s->fifo_min < sizeof(uint32_t) * 4) {
        return 0;
    }
    if (s->fifo_max > SVGA_FIFO_SIZE ||
        s->fifo_min >= SVGA_FIFO_SIZE ||
        s->fifo_stop >= SVGA_FIFO_SIZE ||
        s->fifo_next >= SVGA_FIFO_SIZE) {
        return 0;
    }
    if (s->fifo_max < s->fifo_min + 10 * KiB) {
        return 0;
    }

    num = s->fifo_next - s->fifo_stop;
    if (num < 0) {
        num += s->fifo_max - s->fifo_min;
    }
    return num >> 2;
}

static inline uint32_t vmsvga_fifo_read_raw(struct vmsvga_state_s *s)
{
    uint32_t cmd = s->fifo[s->fifo_stop >> 2];

    s->fifo_stop += 4;
    if (s->fifo_stop >= s->fifo_max) {
        s->fifo_stop = s->fifo_min;
    }
    s->fifo[SVGA_FIFO_STOP] = cpu_to_le32(s->fifo_stop);
    return cmd;
}

static inline uint32_t vmsvga_fifo_read(struct vmsvga_state_s *s)
{
    return le32_to_cpu(vmsvga_fifo_read_raw(s));
}

static void vmsvga_fifo_run(struct vmsvga_state_s *s)
{
    uint32_t cmd, colour;
    int args, len, maxloop = 1024;
    int x, y, dx, dy, width, height;
    struct vmsvga_cursor_definition_s cursor;
    uint32_t cmd_start;

    len = vmsvga_fifo_length(s);
    while (len > 0 && --maxloop > 0) {
        /* May need to go back to the start of the command if incomplete */
        cmd_start = s->fifo_stop;

        switch (cmd = vmsvga_fifo_read(s)) {
        case SVGA_CMD_UPDATE:
        case SVGA_CMD_UPDATE_VERBOSE:
            len -= 5;
            if (len < 0) {
                goto rewind;
            }

            x = vmsvga_fifo_read(s);
            y = vmsvga_fifo_read(s);
            width = vmsvga_fifo_read(s);
            height = vmsvga_fifo_read(s);
            vmsvga_update_rect_delayed(s, x, y, width, height);
            break;

        case SVGA_CMD_RECT_FILL:
            len -= 6;
            if (len < 0) {
                goto rewind;
            }

            colour = vmsvga_fifo_read(s);
            x = vmsvga_fifo_read(s);
            y = vmsvga_fifo_read(s);
            width = vmsvga_fifo_read(s);
            height = vmsvga_fifo_read(s);
#ifdef HW_FILL_ACCEL
            if (vmsvga_fill_rect(s, colour, x, y, width, height) == 0) {
                break;
            }
#endif
            args = 0;
            goto badcmd;

        case SVGA_CMD_RECT_COPY:
            len -= 7;
            if (len < 0) {
                goto rewind;
            }

            x = vmsvga_fifo_read(s);
            y = vmsvga_fifo_read(s);
            dx = vmsvga_fifo_read(s);
            dy = vmsvga_fifo_read(s);
            width = vmsvga_fifo_read(s);
            height = vmsvga_fifo_read(s);
#ifdef HW_RECT_ACCEL
            if (vmsvga_copy_rect(s, x, y, dx, dy, width, height) == 0) {
                break;
            }
#endif
            args = 0;
            goto badcmd;

        case SVGA_CMD_DEFINE_CURSOR:
            len -= 8;
            if (len < 0) {
                goto rewind;
            }

            cursor.id = vmsvga_fifo_read(s);
            cursor.hot_x = vmsvga_fifo_read(s);
            cursor.hot_y = vmsvga_fifo_read(s);
            cursor.width = x = vmsvga_fifo_read(s);
            cursor.height = y = vmsvga_fifo_read(s);
            vmsvga_fifo_read(s);
            cursor.bpp = vmsvga_fifo_read(s);

            args = SVGA_BITMAP_SIZE(x, y) + SVGA_PIXMAP_SIZE(x, y, cursor.bpp);
            if (cursor.width > 256
                || cursor.height > 256
                || cursor.bpp > 32
                || SVGA_BITMAP_SIZE(x, y) > ARRAY_SIZE(cursor.mask)
                || SVGA_PIXMAP_SIZE(x, y, cursor.bpp)
                    > ARRAY_SIZE(cursor.image)) {
                    goto badcmd;
            }

            len -= args;
            if (len < 0) {
                goto rewind;
            }

            for (args = 0; args < SVGA_BITMAP_SIZE(x, y); args++) {
                cursor.mask[args] = vmsvga_fifo_read_raw(s);
            }
            for (args = 0; args < SVGA_PIXMAP_SIZE(x, y, cursor.bpp); args++) {
                cursor.image[args] = vmsvga_fifo_read_raw(s);
            }
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
            if (len < 0) {
                goto rewind;
            }
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
            if (len < 0) {
                goto rewind;
            }
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
            if (len < 0) {
                goto rewind;
            }
            while (args--) {
                vmsvga_fifo_read(s);
            }
            printf("%s: Unknown command 0x%02x in SVGA command FIFO\n",
                   __func__, cmd);
            break;

        rewind:
            s->fifo_stop = cmd_start;
            s->fifo[SVGA_FIFO_STOP] = cpu_to_le32(s->fifo_stop);
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
    DisplaySurface *surface = qemu_console_surface(s->vga.con);
    PixelFormat pf;
    uint32_t ret;

    switch (s->index) {
    case SVGA_REG_ID:
        ret = s->svgaid;
        break;

    case SVGA_REG_ENABLE:
        ret = s->enable;
        break;

    case SVGA_REG_WIDTH:
        ret = s->new_width ? s->new_width : surface_width(surface);
        break;

    case SVGA_REG_HEIGHT:
        ret = s->new_height ? s->new_height : surface_height(surface);
        break;

    case SVGA_REG_MAX_WIDTH:
        ret = SVGA_MAX_WIDTH;
        break;

    case SVGA_REG_MAX_HEIGHT:
        ret = SVGA_MAX_HEIGHT;
        break;

    case SVGA_REG_DEPTH:
        ret = (s->new_depth == 32) ? 24 : s->new_depth;
        break;

    case SVGA_REG_BITS_PER_PIXEL:
    case SVGA_REG_HOST_BITS_PER_PIXEL:
        ret = s->new_depth;
        break;

    case SVGA_REG_PSEUDOCOLOR:
        ret = 0x0;
        break;

    case SVGA_REG_RED_MASK:
        pf = qemu_default_pixelformat(s->new_depth);
        ret = pf.rmask;
        break;

    case SVGA_REG_GREEN_MASK:
        pf = qemu_default_pixelformat(s->new_depth);
        ret = pf.gmask;
        break;

    case SVGA_REG_BLUE_MASK:
        pf = qemu_default_pixelformat(s->new_depth);
        ret = pf.bmask;
        break;

    case SVGA_REG_BYTES_PER_LINE:
        if (s->new_width) {
            ret = (s->new_depth * s->new_width) / 8;
        } else {
            ret = surface_stride(surface);
        }
        break;

    case SVGA_REG_FB_START: {
        struct pci_vmsvga_state_s *pci_vmsvga
            = container_of(s, struct pci_vmsvga_state_s, chip);
        ret = pci_get_bar_addr(PCI_DEVICE(pci_vmsvga), 1);
        break;
    }

    case SVGA_REG_FB_OFFSET:
        ret = 0x0;
        break;

    case SVGA_REG_VRAM_SIZE:
        ret = s->vga.vram_size; /* No physical VRAM besides the framebuffer */
        break;

    case SVGA_REG_FB_SIZE:
        ret = s->vga.vram_size;
        break;

    case SVGA_REG_CAPABILITIES:
        caps = SVGA_CAP_NONE;
#ifdef HW_RECT_ACCEL
        caps |= SVGA_CAP_RECT_COPY;
#endif
#ifdef HW_FILL_ACCEL
        caps |= SVGA_CAP_RECT_FILL;
#endif
#ifdef HW_MOUSE_ACCEL
        if (dpy_cursor_define_supported(s->vga.con)) {
            caps |= SVGA_CAP_CURSOR | SVGA_CAP_CURSOR_BYPASS_2 |
                    SVGA_CAP_CURSOR_BYPASS;
        }
#endif
        ret = caps;
        break;

    case SVGA_REG_MEM_START: {
        struct pci_vmsvga_state_s *pci_vmsvga
            = container_of(s, struct pci_vmsvga_state_s, chip);
        ret = pci_get_bar_addr(PCI_DEVICE(pci_vmsvga), 2);
        break;
    }

    case SVGA_REG_MEM_SIZE:
        ret = s->fifo_size;
        break;

    case SVGA_REG_CONFIG_DONE:
        ret = s->config;
        break;

    case SVGA_REG_SYNC:
    case SVGA_REG_BUSY:
        ret = s->syncing;
        break;

    case SVGA_REG_GUEST_ID:
        ret = s->guest;
        break;

    case SVGA_REG_CURSOR_ID:
        ret = s->cursor.id;
        break;

    case SVGA_REG_CURSOR_X:
        ret = s->cursor.x;
        break;

    case SVGA_REG_CURSOR_Y:
        ret = s->cursor.y;
        break;

    case SVGA_REG_CURSOR_ON:
        ret = s->cursor.on;
        break;

    case SVGA_REG_SCRATCH_SIZE:
        ret = s->scratch_size;
        break;

    case SVGA_REG_MEM_REGS:
    case SVGA_REG_NUM_DISPLAYS:
    case SVGA_REG_PITCHLOCK:
    case SVGA_PALETTE_BASE ... SVGA_PALETTE_END:
        ret = 0;
        break;

    default:
        if (s->index >= SVGA_SCRATCH_BASE &&
            s->index < SVGA_SCRATCH_BASE + s->scratch_size) {
            ret = s->scratch[s->index - SVGA_SCRATCH_BASE];
            break;
        }
        printf("%s: Bad register %02x\n", __func__, s->index);
        ret = 0;
        break;
    }

    if (s->index >= SVGA_SCRATCH_BASE) {
        trace_vmware_scratch_read(s->index, ret);
    } else if (s->index >= SVGA_PALETTE_BASE) {
        trace_vmware_palette_read(s->index, ret);
    } else {
        trace_vmware_value_read(s->index, ret);
    }
    return ret;
}

static void vmsvga_value_write(void *opaque, uint32_t address, uint32_t value)
{
    struct vmsvga_state_s *s = opaque;

    if (s->index >= SVGA_SCRATCH_BASE) {
        trace_vmware_scratch_write(s->index, value);
    } else if (s->index >= SVGA_PALETTE_BASE) {
        trace_vmware_palette_write(s->index, value);
    } else {
        trace_vmware_value_write(s->index, value);
    }
    switch (s->index) {
    case SVGA_REG_ID:
        if (value == SVGA_ID_2 || value == SVGA_ID_1 || value == SVGA_ID_0) {
            s->svgaid = value;
        }
        break;

    case SVGA_REG_ENABLE:
        s->enable = !!value;
        s->invalidated = 1;
        s->vga.hw_ops->invalidate(&s->vga);
        if (s->enable && s->config) {
            vga_dirty_log_stop(&s->vga);
        } else {
            vga_dirty_log_start(&s->vga);
        }
        break;

    case SVGA_REG_WIDTH:
        if (value <= SVGA_MAX_WIDTH) {
            s->new_width = value;
            s->invalidated = 1;
        } else {
            printf("%s: Bad width: %i\n", __func__, value);
        }
        break;

    case SVGA_REG_HEIGHT:
        if (value <= SVGA_MAX_HEIGHT) {
            s->new_height = value;
            s->invalidated = 1;
        } else {
            printf("%s: Bad height: %i\n", __func__, value);
        }
        break;

    case SVGA_REG_BITS_PER_PIXEL:
        if (value != 32) {
            printf("%s: Bad bits per pixel: %i bits\n", __func__, value);
            s->config = 0;
            s->invalidated = 1;
        }
        break;

    case SVGA_REG_CONFIG_DONE:
        if (value) {
            s->fifo = (uint32_t *) s->fifo_ptr;
            vga_dirty_log_stop(&s->vga);
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
            ARRAY_SIZE(vmsvga_guest_id)) {
            printf("%s: guest runs %s.\n", __func__,
                   vmsvga_guest_id[value - GUEST_OS_BASE]);
        }
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
        if (value <= SVGA_CURSOR_ON_SHOW) {
            dpy_mouse_set(s->vga.con, s->cursor.x, s->cursor.y, s->cursor.on);
        }
#endif
        break;

    case SVGA_REG_DEPTH:
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
        printf("%s: Bad register %02x\n", __func__, s->index);
    }
}

static uint32_t vmsvga_bios_read(void *opaque, uint32_t address)
{
    printf("%s: what are we supposed to return?\n", __func__);
    return 0xcafe;
}

static void vmsvga_bios_write(void *opaque, uint32_t address, uint32_t data)
{
    printf("%s: what are we supposed to do with (%08x)?\n", __func__, data);
}

static inline void vmsvga_check_size(struct vmsvga_state_s *s)
{
    DisplaySurface *surface = qemu_console_surface(s->vga.con);

    if (s->new_width != surface_width(surface) ||
        s->new_height != surface_height(surface) ||
        s->new_depth != surface_bits_per_pixel(surface)) {
        int stride = (s->new_depth * s->new_width) / 8;
        pixman_format_code_t format =
            qemu_default_pixman_format(s->new_depth, true);
        trace_vmware_setmode(s->new_width, s->new_height, s->new_depth);
        surface = qemu_create_displaysurface_from(s->new_width, s->new_height,
                                                  format, stride,
                                                  s->vga.vram_ptr);
        dpy_gfx_replace_surface(s->vga.con, surface);
        s->invalidated = 1;
    }
}

static void vmsvga_update_display(void *opaque)
{
    struct vmsvga_state_s *s = opaque;

    if (!s->enable || !s->config) {
        /* in standard vga mode */
        s->vga.hw_ops->gfx_update(&s->vga);
        return;
    }

    vmsvga_check_size(s);

    vmsvga_fifo_run(s);
    vmsvga_update_rect_flush(s);

    if (s->invalidated) {
        s->invalidated = 0;
        dpy_gfx_update_full(s->vga.con);
    }
}

static void vmsvga_reset(DeviceState *dev)
{
    struct pci_vmsvga_state_s *pci = VMWARE_SVGA(dev);
    struct vmsvga_state_s *s = &pci->chip;

    s->index = 0;
    s->enable = 0;
    s->config = 0;
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
        s->vga.hw_ops->invalidate(&s->vga);
        return;
    }

    s->invalidated = 1;
}

static void vmsvga_text_update(void *opaque, console_ch_t *chardata)
{
    struct vmsvga_state_s *s = opaque;

    if (s->vga.hw_ops->text_update) {
        s->vga.hw_ops->text_update(&s->vga, chardata);
    }
}

static int vmsvga_post_load(void *opaque, int version_id)
{
    struct vmsvga_state_s *s = opaque;

    s->invalidated = 1;
    if (s->config) {
        s->fifo = (uint32_t *) s->fifo_ptr;
    }
    return 0;
}

static const VMStateDescription vmstate_vmware_vga_internal = {
    .name = "vmware_vga_internal",
    .version_id = 0,
    .minimum_version_id = 0,
    .post_load = vmsvga_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_INT32_EQUAL(new_depth, struct vmsvga_state_s, NULL),
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
        VMSTATE_UNUSED(4), /* was fb_size */
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_vmware_vga = {
    .name = "vmware_vga",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, struct pci_vmsvga_state_s),
        VMSTATE_STRUCT(chip, struct pci_vmsvga_state_s, 0,
                       vmstate_vmware_vga_internal, struct vmsvga_state_s),
        VMSTATE_END_OF_LIST()
    }
};

static const GraphicHwOps vmsvga_ops = {
    .invalidate  = vmsvga_invalidate_display,
    .gfx_update  = vmsvga_update_display,
    .text_update = vmsvga_text_update,
};

static void vmsvga_init(DeviceState *dev, struct vmsvga_state_s *s,
                        MemoryRegion *address_space, MemoryRegion *io)
{
    s->scratch_size = SVGA_SCRATCH_SIZE;
    s->scratch = g_malloc(s->scratch_size * 4);

    s->vga.con = graphic_console_init(dev, 0, &vmsvga_ops, s);

    s->fifo_size = SVGA_FIFO_SIZE;
    memory_region_init_ram(&s->fifo_ram, NULL, "vmsvga.fifo", s->fifo_size,
                           &error_fatal);
    s->fifo_ptr = memory_region_get_ram_ptr(&s->fifo_ram);

    vga_common_init(&s->vga, OBJECT(dev));
    vga_init(&s->vga, OBJECT(dev), address_space, io, true);
    vmstate_register(NULL, 0, &vmstate_vga_common, &s->vga);
    s->new_depth = 32;
}

static uint64_t vmsvga_io_read(void *opaque, hwaddr addr, unsigned size)
{
    struct vmsvga_state_s *s = opaque;

    switch (addr) {
    case SVGA_IO_MUL * SVGA_INDEX_PORT: return vmsvga_index_read(s, addr);
    case SVGA_IO_MUL * SVGA_VALUE_PORT: return vmsvga_value_read(s, addr);
    case SVGA_IO_MUL * SVGA_BIOS_PORT: return vmsvga_bios_read(s, addr);
    default: return -1u;
    }
}

static void vmsvga_io_write(void *opaque, hwaddr addr,
                            uint64_t data, unsigned size)
{
    struct vmsvga_state_s *s = opaque;

    switch (addr) {
    case SVGA_IO_MUL * SVGA_INDEX_PORT:
        vmsvga_index_write(s, addr, data);
        break;
    case SVGA_IO_MUL * SVGA_VALUE_PORT:
        vmsvga_value_write(s, addr, data);
        break;
    case SVGA_IO_MUL * SVGA_BIOS_PORT:
        vmsvga_bios_write(s, addr, data);
        break;
    }
}

static const MemoryRegionOps vmsvga_io_ops = {
    .read = vmsvga_io_read,
    .write = vmsvga_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = true,
    },
    .impl = {
        .unaligned = true,
    },
};

static void pci_vmsvga_realize(PCIDevice *dev, Error **errp)
{
    struct pci_vmsvga_state_s *s = VMWARE_SVGA(dev);

    dev->config[PCI_CACHE_LINE_SIZE] = 0x08;
    dev->config[PCI_LATENCY_TIMER] = 0x40;
    dev->config[PCI_INTERRUPT_LINE] = 0xff;          /* End */

    memory_region_init_io(&s->io_bar, NULL, &vmsvga_io_ops, &s->chip,
                          "vmsvga-io", 0x10);
    memory_region_set_flush_coalesced(&s->io_bar);
    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->io_bar);

    vmsvga_init(DEVICE(dev), &s->chip,
                pci_address_space(dev), pci_address_space_io(dev));

    pci_register_bar(dev, 1, PCI_BASE_ADDRESS_MEM_PREFETCH,
                     &s->chip.vga.vram);
    pci_register_bar(dev, 2, PCI_BASE_ADDRESS_MEM_PREFETCH,
                     &s->chip.fifo_ram);
}

static Property vga_vmware_properties[] = {
    DEFINE_PROP_UINT32("vgamem_mb", struct pci_vmsvga_state_s,
                       chip.vga.vram_size_mb, 16),
    DEFINE_PROP_BOOL("global-vmstate", struct pci_vmsvga_state_s,
                     chip.vga.global_vmstate, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void vmsvga_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pci_vmsvga_realize;
    k->romfile = "vgabios-vmware.bin";
    k->vendor_id = PCI_VENDOR_ID_VMWARE;
    k->device_id = SVGA_PCI_DEVICE_ID;
    k->class_id = PCI_CLASS_DISPLAY_VGA;
    k->subsystem_vendor_id = PCI_VENDOR_ID_VMWARE;
    k->subsystem_id = SVGA_PCI_DEVICE_ID;
    dc->reset = vmsvga_reset;
    dc->vmsd = &vmstate_vmware_vga;
    device_class_set_props(dc, vga_vmware_properties);
    dc->hotpluggable = false;
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
}

static const TypeInfo vmsvga_info = {
    .name          = TYPE_VMWARE_SVGA,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(struct pci_vmsvga_state_s),
    .class_init    = vmsvga_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void vmsvga_register_types(void)
{
    type_register_static(&vmsvga_info);
}

type_init(vmsvga_register_types)
