/*
 * QEMU ATI SVGA emulation
 * 2D engine functions
 *
 * Copyright (c) 2019 BALATON Zoltan
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 */

#include "qemu/osdep.h"
#include "ati_int.h"
#include "ati_regs.h"
#include "qemu/log.h"
#include "ui/pixel_ops.h"
#include "ui/console.h"

/*
 * NOTE:
 * This is 2D _acceleration_ and supposed to be fast. Therefore, don't try to
 * reinvent the wheel (unlikely to get better with a naive implementation than
 * existing libraries) and avoid (poorly) reimplementing gfx primitives.
 * That is unnecessary and would become a performance problem. Instead, try to
 * map to and reuse existing optimised facilities (e.g. pixman) wherever
 * possible.
 */

static int ati_bpp_from_datatype(ATIVGAState *s)
{
    switch (s->regs.dp_datatype & 0xf) {
    case 2:
        return 8;
    case 3:
    case 4:
        return 16;
    case 5:
        return 24;
    case 6:
        return 32;
    default:
        qemu_log_mask(LOG_UNIMP, "Unknown dst datatype %d\n",
                      s->regs.dp_datatype & 0xf);
        return 0;
    }
}

static void ati_set_dirty(ATIVGAState *s,
                          const uint8_t *dst_bits, unsigned dst_y)
{
    VGACommonState *vga = &s->vga;
    DisplaySurface *ds = qemu_console_surface(vga->con);

    DPRINTF("%p %u ds: %p %d %d rop: %x\n", vga->vram_ptr, vga->vbe_start_addr,
            surface_data(ds), surface_stride(ds), surface_bits_per_pixel(ds),
            (s->regs.dp_mix & GMC_ROP3_MASK) >> 16);
    if (dst_bits >= vga->vram_ptr + vga->vbe_start_addr &&
        dst_bits < vga->vram_ptr + vga->vbe_start_addr +
                   vga->vbe_regs[VBE_DISPI_INDEX_YRES] * vga->vbe_line_offset) {
        memory_region_set_dirty(&vga->vram,
                                vga->vbe_start_addr + s->regs.dst_offset
                                                    + dst_y * surface_stride(ds),
                                s->regs.dst_height * surface_stride(ds));
    }
}

void ati_2d_blt(ATIVGAState *s)
{
    /* FIXME it is probably more complex than this and may need to be */
    /* rewritten but for now as a start just to get some output: */
    uint32_t rop3 = s->regs.dp_mix & GMC_ROP3_MASK;
    bool left_to_right = s->regs.dp_cntl & DST_X_LEFT_TO_RIGHT;
    bool top_to_bottom = s->regs.dp_cntl & DST_Y_TOP_TO_BOTTOM;
    uint32_t frgd_clr = s->regs.dp_brush_frgd_clr;
    uint8_t *palette = s->vga.palette;
    unsigned dst_offset = s->regs.dst_offset;
    unsigned dst_width = s->regs.dst_width;
    unsigned dst_height = s->regs.dst_height;
    unsigned dst_x = (left_to_right ?
                     s->regs.dst_x : s->regs.dst_x + 1 - dst_width);
    unsigned dst_y = (top_to_bottom ?
                     s->regs.dst_y : s->regs.dst_y + 1 - dst_height);
    int bpp = ati_bpp_from_datatype(s);
    if (!bpp) {
        qemu_log_mask(LOG_GUEST_ERROR, "Invalid bpp\n");
        return;
    }
    int dst_stride = s->regs.dst_pitch;
    if (!dst_stride) {
        qemu_log_mask(LOG_GUEST_ERROR, "Zero dest pitch\n");
        return;
    }
    uint8_t *dst_bits = s->vga.vram_ptr + dst_offset;

    if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
        dst_bits += s->regs.crtc_offset & 0x07ffffff;
        dst_stride *= bpp;
    }
    uint8_t *end = s->vga.vram_ptr + s->vga.vram_size;
    if (dst_x > 0x3fff || dst_y > 0x3fff || dst_bits >= end
        || dst_bits + dst_x + (dst_y + dst_height) * dst_stride >= end) {
        qemu_log_mask(LOG_UNIMP, "blt outside vram not implemented\n");
        return;
    }
    DPRINTF("%d %d %d, %d %d %d, (%d,%d) -> (%d,%d) %dx%d %c %c\n",
            s->regs.src_offset, dst_offset, s->regs.default_offset,
            s->regs.src_pitch, dst_stride, s->regs.default_pitch,
            s->regs.src_x, s->regs.src_y, dst_x, dst_y,
            dst_width, dst_height,
            (left_to_right ? '>' : '<'),
            (top_to_bottom ? 'v' : '^'));
    switch (rop3) {
    case ROP3_SRCCOPY:
    {
        bool fallback = false;
        unsigned src_x = (left_to_right ?
                         s->regs.src_x : s->regs.src_x + 1 - dst_width);
        unsigned src_y = (top_to_bottom ?
                         s->regs.src_y : s->regs.src_y + 1 - dst_height);
        int src_stride = s->regs.src_pitch;
        if (!src_stride) {
            qemu_log_mask(LOG_GUEST_ERROR, "Zero source pitch\n");
            return;
        }
        uint8_t *src_bits = s->vga.vram_ptr + s->regs.src_offset;

        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
            src_bits += s->regs.crtc_offset & 0x07ffffff;
            src_stride *= bpp;
        }
        if (src_x > 0x3fff || src_y > 0x3fff || src_bits >= end
            || src_bits + src_x
             + (src_y + dst_height) * src_stride >= end) {
            qemu_log_mask(LOG_UNIMP, "blt outside vram not implemented\n");
            return;
        }

        DPRINTF("pixman_blt(%p, %p, %ld, %ld, %d, %d, %d, %d, %d, %d, %d, %d)\n",
                src_bits, dst_bits, src_stride / sizeof(uint32_t),
                dst_stride / sizeof(uint32_t), bpp, bpp, src_x, src_y, dst_x,
                dst_y, dst_width, dst_height);
#ifdef CONFIG_PIXMAN
        int src_stride_words = src_stride / sizeof(uint32_t);
        int dst_stride_words = dst_stride / sizeof(uint32_t);
        if ((s->use_pixman & BIT(1)) && left_to_right && top_to_bottom) {
            fallback = !pixman_blt((uint32_t *)src_bits, (uint32_t *)dst_bits,
                                   src_stride_words, dst_stride_words, bpp, bpp,
                                   src_x, src_y, dst_x, dst_y,
                                   dst_width, dst_height);
        } else if (s->use_pixman & BIT(1)) {
            /* FIXME: We only really need a temporary if src and dst overlap */
            int llb = dst_width * (bpp / 8);
            int tmp_stride_words = DIV_ROUND_UP(llb, sizeof(uint32_t));
            uint32_t *tmp = g_malloc(tmp_stride_words * sizeof(uint32_t) *
                                     dst_height);
            fallback = !pixman_blt((uint32_t *)src_bits, tmp,
                                   src_stride_words, tmp_stride_words, bpp, bpp,
                                   src_x, src_y, 0, 0,
                                   dst_width, dst_height);
            if (!fallback) {
                fallback = !pixman_blt(tmp, (uint32_t *)dst_bits,
                                       tmp_stride_words, dst_stride_words,
                                       bpp, bpp, 0, 0, dst_x, dst_y,
                                       dst_width, dst_height);
            }
            g_free(tmp);
        } else
#endif
        {
            fallback = true;
        }
        if (fallback) {
            unsigned int y, i, j, bypp = bpp / 8;
            for (y = 0; y < dst_height; y++) {
                i = dst_x * bypp;
                j = src_x * bypp;
                if (top_to_bottom) {
                    i += (dst_y + y) * dst_stride;
                    j += (src_y + y) * src_stride;
                } else {
                    i += (dst_y + dst_height - 1 - y) * dst_stride;
                    j += (src_y + dst_height - 1 - y) * src_stride;
                }
                memmove(&dst_bits[i], &src_bits[j], dst_width * bypp);
            }
        }
        break;
    }
    case ROP3_PATCOPY:
    case ROP3_BLACKNESS:
    case ROP3_WHITENESS:
    {
        uint32_t filler = 0;

        switch (rop3) {
        case ROP3_PATCOPY:
            filler = frgd_clr;
            break;
        case ROP3_BLACKNESS:
            filler = 0xffUL << 24 | rgb_to_pixel32(palette[0], palette[1],
                                                   palette[2]);
            break;
        case ROP3_WHITENESS:
            filler = 0xffUL << 24 | rgb_to_pixel32(palette[3], palette[4],
                                                   palette[5]);
            break;
        }

        DPRINTF("pixman_fill(%p, %ld, %d, %d, %d, %d, %d, %x)\n",
                dst_bits, dst_stride / sizeof(uint32_t), bpp, dst_x, dst_y,
                dst_width, dst_height, filler);
#ifdef CONFIG_PIXMAN
        if (!(s->use_pixman & BIT(0)) ||
            !pixman_fill((uint32_t *)dst_bits, dst_stride / sizeof(uint32_t),
                         bpp, dst_x, dst_y, dst_width, dst_height, filler))
#endif
        {
            /* fallback when pixman failed or we don't want to call it */
            unsigned int x, y, i, bypp = bpp / 8;
            for (y = 0; y < dst_height; y++) {
                i = dst_x * bypp + (dst_y + y) * dst_stride;
                for (x = 0; x < dst_width; x++, i += bypp) {
                    stn_he_p(&dst_bits[i], bypp, filler);
                }
            }
        }
        break;
    }
    default:
        qemu_log_mask(LOG_UNIMP, "Unimplemented ati_2d blt op %x\n",
                      rop3 >> 16);
        return;
    }

    ati_set_dirty(s, dst_bits, dst_y);
}
