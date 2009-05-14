/*
 * Syborg Framebuffer
 *
 * Copyright (c) 2009 CodeSourcery
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

#include "sysbus.h"
#include "console.h"
#include "syborg.h"
#include "framebuffer.h"

//#define DEBUG_SYBORG_FB

#ifdef DEBUG_SYBORG_FB
#define DPRINTF(fmt, ...) \
do { printf("syborg_fb: " fmt , ## __VA_ARGS__); } while (0)
#define BADF(fmt, ...) \
do { fprintf(stderr, "syborg_fb: error: " fmt , ## __VA_ARGS__); \
    exit(1);} while (0)
#else
#define DPRINTF(fmt, ...) do {} while(0)
#define BADF(fmt, ...) \
do { fprintf(stderr, "syborg_fb: error: " fmt , ## __VA_ARGS__);} while (0)
#endif

enum {
    FB_ID               = 0,
    FB_BASE             = 1,
    FB_HEIGHT           = 2,
    FB_WIDTH            = 3,
    FB_ORIENTATION      = 4,
    FB_BLANK            = 5,
    FB_INT_MASK         = 6,
    FB_INTERRUPT_CAUSE  = 7,
    FB_BPP              = 8,
    FB_COLOR_ORDER      = 9,
    FB_BYTE_ORDER       = 10,
    FB_PIXEL_ORDER      = 11,
    FB_ROW_PITCH        = 12,
    FB_ENABLED          = 13,
    FB_PALETTE_START    = 0x400 >> 2,
    FB_PALETTE_END   = FB_PALETTE_START+256-1,
};

#define FB_INT_VSYNC            (1U << 0)
#define FB_INT_BASE_UPDATE_DONE (1U << 1)

typedef struct {
    SysBusDevice busdev;
    DisplayState *ds;
    /*QEMUConsole *console;*/
    uint32_t need_update : 1;
    uint32_t need_int : 1;
    uint32_t enabled : 1;
    uint32_t int_status;
    uint32_t int_enable;
    qemu_irq irq;

    uint32_t base;
    uint32_t pitch;
    int rows;
    int cols;
    int blank;
    int bpp;
    int rgb; /* 0 = BGR, 1 = RGB */
    int endian; /* 0 = Little, 1 = Big */
    uint32_t raw_palette[256];
    uint32_t palette[256];
} SyborgFBState;

enum {
    BPP_SRC_1,
    BPP_SRC_2,
    BPP_SRC_4,
    BPP_SRC_8,
    BPP_SRC_16,
    BPP_SRC_32,
    /* TODO: Implement these.  */
    BPP_SRC_15 = -1,
    BPP_SRC_24 = -2
};

#include "pixel_ops.h"

#define BITS 8
#include "pl110_template.h"
#define BITS 15
#include "pl110_template.h"
#define BITS 16
#include "pl110_template.h"
#define BITS 24
#include "pl110_template.h"
#define BITS 32
#include "pl110_template.h"

/* Update interrupts.  */
static void syborg_fb_update(SyborgFBState *s)
{
    if ((s->int_status & s->int_enable) != 0) {
        DPRINTF("Raise IRQ\n");
        qemu_irq_raise(s->irq);
    } else {
        DPRINTF("Lower IRQ\n");
        qemu_irq_lower(s->irq);
    }
}

static int syborg_fb_enabled(const SyborgFBState *s)
{
    return s->enabled;
}

static void syborg_fb_update_palette(SyborgFBState *s)
{
    int n, i;
    uint32_t raw;
    unsigned int r, g, b;

    switch (s->bpp) {
    case BPP_SRC_1: n = 2; break;
    case BPP_SRC_2: n = 4; break;
    case BPP_SRC_4: n = 16; break;
    case BPP_SRC_8: n = 256; break;
    default: return;
    }

    for (i = 0; i < n; i++) {
        raw = s->raw_palette[i];
        r = (raw >> 16) & 0xff;
        g = (raw >> 8) & 0xff;
        b = raw & 0xff;
        switch (ds_get_bits_per_pixel(s->ds)) {
        case 8:
            s->palette[i] = rgb_to_pixel8(r, g, b);
            break;
        case 15:
            s->palette[i] = rgb_to_pixel15(r, g, b);
            break;
        case 16:
            s->palette[i] = rgb_to_pixel16(r, g, b);
            break;
        case 24:
        case 32:
            s->palette[i] = rgb_to_pixel32(r, g, b);
            break;
        default:
            abort();
        }
    }

}

static void syborg_fb_update_display(void *opaque)
{
    SyborgFBState *s = (SyborgFBState *)opaque;
    drawfn* fntable;
    drawfn fn;
    int dest_width;
    int src_width;
    int bpp_offset;
    int first;
    int last;

    if (!syborg_fb_enabled(s))
        return;

    switch (ds_get_bits_per_pixel(s->ds)) {
    case 0:
        return;
    case 8:
        fntable = pl110_draw_fn_8;
        dest_width = 1;
        break;
    case 15:
        fntable = pl110_draw_fn_15;
        dest_width = 2;
        break;
    case 16:
        fntable = pl110_draw_fn_16;
        dest_width = 2;
        break;
    case 24:
        fntable = pl110_draw_fn_24;
        dest_width = 3;
        break;
    case 32:
        fntable = pl110_draw_fn_32;
        dest_width = 4;
        break;
    default:
        fprintf(stderr, "syborg_fb: Bad color depth\n");
        exit(1);
    }

    if (s->need_int) {
        s->int_status |= FB_INT_BASE_UPDATE_DONE;
        syborg_fb_update(s);
        s->need_int = 0;
    }

    if (s->rgb) {
        bpp_offset = 18;
    } else {
        bpp_offset = 0;
    }
    if (s->endian) {
        bpp_offset += 6;
    }

    fn = fntable[s->bpp + bpp_offset];

    if (s->pitch) {
        src_width = s->pitch;
    } else {
        src_width = s->cols;
        switch (s->bpp) {
        case BPP_SRC_1:
            src_width >>= 3;
            break;
        case BPP_SRC_2:
            src_width >>= 2;
            break;
        case BPP_SRC_4:
            src_width >>= 1;
            break;
        case BPP_SRC_8:
            break;
        case BPP_SRC_15:
        case BPP_SRC_16:
            src_width <<= 1;
            break;
        case BPP_SRC_24:
            src_width *= 3;
            break;
        case BPP_SRC_32:
            src_width <<= 2;
            break;
        }
    }
    dest_width *= s->cols;
    first = 0;
    /* TODO: Implement blanking.  */
    if (!s->blank) {
        if (s->need_update && s->bpp <= BPP_SRC_8) {
            syborg_fb_update_palette(s);
        }
        framebuffer_update_display(s->ds,
                                   s->base, s->cols, s->rows,
                                   src_width, dest_width, 0,
                                   s->need_update,
                                   fn, s->palette,
                                   &first, &last);
        if (first >= 0) {
            dpy_update(s->ds, 0, first, s->cols, last - first + 1);
        }

        s->int_status |= FB_INT_VSYNC;
        syborg_fb_update(s);
    }

    s->need_update = 0;
}

static void syborg_fb_invalidate_display(void * opaque)
{
    SyborgFBState *s = (SyborgFBState *)opaque;
    s->need_update = 1;
}

static uint32_t syborg_fb_read(void *opaque, target_phys_addr_t offset)
{
    SyborgFBState *s = opaque;

    DPRINTF("read reg %d\n", (int)offset);
    offset &= 0xfff;
    switch (offset >> 2) {
    case FB_ID:
        return SYBORG_ID_FRAMEBUFFER;

    case FB_BASE:
        return s->base;

    case FB_HEIGHT:
        return s->rows;

    case FB_WIDTH:
        return s->cols;

    case FB_ORIENTATION:
        return 0;

    case FB_BLANK:
        return s->blank;

    case FB_INT_MASK:
        return s->int_enable;

    case FB_INTERRUPT_CAUSE:
        return s->int_status;

    case FB_BPP:
        switch (s->bpp) {
        case BPP_SRC_1: return 1;
        case BPP_SRC_2: return 2;
        case BPP_SRC_4: return 4;
        case BPP_SRC_8: return 8;
        case BPP_SRC_15: return 15;
        case BPP_SRC_16: return 16;
        case BPP_SRC_24: return 24;
        case BPP_SRC_32: return 32;
        default: return 0;
        }

    case FB_COLOR_ORDER:
        return s->rgb;

    case FB_BYTE_ORDER:
        return s->endian;

    case FB_PIXEL_ORDER:
        return 0;

    case FB_ROW_PITCH:
        return s->pitch;

    case FB_ENABLED:
        return s->enabled;

    default:
        if ((offset >> 2) >= FB_PALETTE_START
            && (offset >> 2) <= FB_PALETTE_END) {
            return s->raw_palette[(offset >> 2) - FB_PALETTE_START];
        } else {
            cpu_abort (cpu_single_env, "syborg_fb_read: Bad offset %x\n",
                         (int)offset);
        }
        return 0;
    }
}

static void syborg_fb_write(void *opaque, target_phys_addr_t offset,
                            uint32_t val)
{
    SyborgFBState *s = opaque;

    DPRINTF("write reg %d = %d\n", (int)offset, val);
    s->need_update = 1;
    offset &= 0xfff;
    switch (offset >> 2) {
    case FB_BASE:
        s->base = val;
        s->need_int = 1;
        s->need_update = 1;
        syborg_fb_update(s);
        break;

    case FB_HEIGHT:
        s->rows = val;
        break;

    case FB_WIDTH:
        s->cols = val;
        break;

    case FB_ORIENTATION:
        /* TODO: Implement rotation.  */
        break;

    case FB_BLANK:
        s->blank = val & 1;
        break;

    case FB_INT_MASK:
        s->int_enable = val;
        syborg_fb_update(s);
        break;

    case FB_INTERRUPT_CAUSE:
        s->int_status &= ~val;
        syborg_fb_update(s);
        break;

    case FB_BPP:
        switch (val) {
        case 1: val = BPP_SRC_1; break;
        case 2: val = BPP_SRC_2; break;
        case 4: val = BPP_SRC_4; break;
        case 8: val = BPP_SRC_8; break;
        /* case 15: val = BPP_SRC_15; break; */
        case 16: val = BPP_SRC_16; break;
        /* case 24: val = BPP_SRC_24; break; */
        case 32: val = BPP_SRC_32; break;
        default: val = s->bpp; break;
        }
        s->bpp = val;
        break;

    case FB_COLOR_ORDER:
        s->rgb = (val != 0);
        break;

    case FB_BYTE_ORDER:
        s->endian = (val != 0);
        break;

    case FB_PIXEL_ORDER:
        /* TODO: Implement this.  */
        break;

    case FB_ROW_PITCH:
        s->pitch = val;
        break;

    case FB_ENABLED:
        s->enabled = val;
        break;

    default:
        if ((offset >> 2) >= FB_PALETTE_START
            && (offset >> 2) <= FB_PALETTE_END) {
            s->raw_palette[(offset >> 2) - FB_PALETTE_START] = val;
        } else {
            cpu_abort (cpu_single_env, "syborg_fb_write: Bad offset %x\n",
                      (int)offset);
        }
        break;
    }
}

static CPUReadMemoryFunc *syborg_fb_readfn[] = {
    syborg_fb_read,
    syborg_fb_read,
    syborg_fb_read
};

static CPUWriteMemoryFunc *syborg_fb_writefn[] = {
    syborg_fb_write,
    syborg_fb_write,
    syborg_fb_write
};

static void syborg_fb_save(QEMUFile *f, void *opaque)
{
    SyborgFBState *s = opaque;
    int i;

    qemu_put_be32(f, s->need_int);
    qemu_put_be32(f, s->int_status);
    qemu_put_be32(f, s->int_enable);
    qemu_put_be32(f, s->enabled);
    qemu_put_be32(f, s->base);
    qemu_put_be32(f, s->pitch);
    qemu_put_be32(f, s->rows);
    qemu_put_be32(f, s->cols);
    qemu_put_be32(f, s->bpp);
    qemu_put_be32(f, s->rgb);
    for (i = 0; i < 256; i++) {
        qemu_put_be32(f, s->raw_palette[i]);
    }
}

static int syborg_fb_load(QEMUFile *f, void *opaque, int version_id)
{
    SyborgFBState *s = opaque;
    int i;

    if (version_id != 1)
        return -EINVAL;

    s->need_int = qemu_get_be32(f);
    s->int_status = qemu_get_be32(f);
    s->int_enable = qemu_get_be32(f);
    s->enabled = qemu_get_be32(f);
    s->base = qemu_get_be32(f);
    s->pitch = qemu_get_be32(f);
    s->rows = qemu_get_be32(f);
    s->cols = qemu_get_be32(f);
    s->bpp = qemu_get_be32(f);
    s->rgb = qemu_get_be32(f);
    for (i = 0; i < 256; i++) {
        s->raw_palette[i] = qemu_get_be32(f);
    }
    s->need_update = 1;

    return 0;
}

static void syborg_fb_init(SysBusDevice *dev)
{
    SyborgFBState *s = FROM_SYSBUS(SyborgFBState, dev);
    int iomemtype;
    int width;
    int height;

    sysbus_init_irq(dev, &s->irq);
    iomemtype = cpu_register_io_memory(0, syborg_fb_readfn,
                                       syborg_fb_writefn, s);
    sysbus_init_mmio(dev, 0x1000, iomemtype);

    width = qdev_get_prop_int(&dev->qdev, "width", 0);
    height = qdev_get_prop_int(&dev->qdev, "height", 0);

    s->ds = graphic_console_init(syborg_fb_update_display,
                                 syborg_fb_invalidate_display,
                                 NULL, NULL, s);

    if (width != 0 && height != 0) {
        qemu_console_resize(s->ds, width, height);
    }

    if (!width)
        width = ds_get_width(s->ds);
    if (!height)
        height = ds_get_height(s->ds);

    s->cols = width;
    s->rows = height;

    register_savevm("syborg_framebuffer", -1, 1,
                    syborg_fb_save, syborg_fb_load, s);
}

static void syborg_fb_register_devices(void)
{
    sysbus_register_dev("syborg,framebuffer", sizeof(SyborgFBState),
                        syborg_fb_init);
}

device_init(syborg_fb_register_devices)
