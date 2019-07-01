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

void ati_2d_blt(ATIVGAState *s)
{
    /* FIXME it is probably more complex than this and may need to be */
    /* rewritten but for now as a start just to get some output: */
    DisplaySurface *ds = qemu_console_surface(s->vga.con);
    DPRINTF("%p %u ds: %p %d %d rop: %x\n", s->vga.vram_ptr,
            s->vga.vbe_start_addr, surface_data(ds), surface_stride(ds),
            surface_bits_per_pixel(ds),
            (s->regs.dp_mix & GMC_ROP3_MASK) >> 16);
    DPRINTF("%d %d %d, %d %d %d, (%d,%d) -> (%d,%d) %dx%d\n",
            s->regs.src_offset, s->regs.dst_offset, s->regs.default_offset,
            s->regs.src_pitch, s->regs.dst_pitch, s->regs.default_pitch,
            s->regs.src_x, s->regs.src_y, s->regs.dst_x, s->regs.dst_y,
            s->regs.dst_width, s->regs.dst_height);
    switch (s->regs.dp_mix & GMC_ROP3_MASK) {
    case ROP3_SRCCOPY:
    {
        uint8_t *src_bits, *dst_bits, *end;
        int src_stride, dst_stride, bpp = ati_bpp_from_datatype(s);
        src_bits = s->vga.vram_ptr +
                   (s->regs.dp_gui_master_cntl & GMC_SRC_PITCH_OFFSET_CNTL ?
                    s->regs.src_offset : s->regs.default_offset);
        dst_bits = s->vga.vram_ptr +
                   (s->regs.dp_gui_master_cntl & GMC_DST_PITCH_OFFSET_CNTL ?
                    s->regs.dst_offset : s->regs.default_offset);
        src_stride = (s->regs.dp_gui_master_cntl & GMC_SRC_PITCH_OFFSET_CNTL ?
                      s->regs.src_pitch : s->regs.default_pitch);
        dst_stride = (s->regs.dp_gui_master_cntl & GMC_DST_PITCH_OFFSET_CNTL ?
                      s->regs.dst_pitch : s->regs.default_pitch);

        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
            src_bits += s->regs.crtc_offset & 0x07ffffff;
            dst_bits += s->regs.crtc_offset & 0x07ffffff;
            src_stride *= bpp;
            dst_stride *= bpp;
        }
        src_stride /= sizeof(uint32_t);
        dst_stride /= sizeof(uint32_t);

        DPRINTF("pixman_blt(%p, %p, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d)\n",
                src_bits, dst_bits, src_stride, dst_stride, bpp, bpp,
                s->regs.src_x, s->regs.src_y, s->regs.dst_x, s->regs.dst_y,
                s->regs.dst_width, s->regs.dst_height);
        end = s->vga.vram_ptr + s->vga.vram_size;
        if (src_bits >= end || dst_bits >= end ||
            src_bits + s->regs.src_x + (s->regs.src_y + s->regs.dst_height) *
            src_stride * sizeof(uint32_t) >= end ||
            dst_bits + s->regs.dst_x + (s->regs.dst_y + s->regs.dst_height) *
            dst_stride * sizeof(uint32_t) >= end) {
            qemu_log_mask(LOG_UNIMP, "blt outside vram not implemented\n");
            return;
        }
        pixman_blt((uint32_t *)src_bits, (uint32_t *)dst_bits,
                   src_stride, dst_stride, bpp, bpp,
                   s->regs.src_x, s->regs.src_y,
                   s->regs.dst_x, s->regs.dst_y,
                   s->regs.dst_width, s->regs.dst_height);
        if (dst_bits >= s->vga.vram_ptr + s->vga.vbe_start_addr &&
            dst_bits < s->vga.vram_ptr + s->vga.vbe_start_addr +
            s->vga.vbe_regs[VBE_DISPI_INDEX_YRES] * s->vga.vbe_line_offset) {
            memory_region_set_dirty(&s->vga.vram, s->vga.vbe_start_addr +
                                    s->regs.dst_offset +
                                    s->regs.dst_y * surface_stride(ds),
                                    s->regs.dst_height * surface_stride(ds));
        }
        s->regs.dst_x += s->regs.dst_width;
        s->regs.dst_y += s->regs.dst_height;
        break;
    }
    case ROP3_PATCOPY:
    case ROP3_BLACKNESS:
    case ROP3_WHITENESS:
    {
        uint8_t *dst_bits, *end;
        int dst_stride, bpp = ati_bpp_from_datatype(s);
        uint32_t filler = 0;
        dst_bits = s->vga.vram_ptr +
                   (s->regs.dp_gui_master_cntl & GMC_DST_PITCH_OFFSET_CNTL ?
                    s->regs.dst_offset : s->regs.default_offset);
        dst_stride = (s->regs.dp_gui_master_cntl & GMC_DST_PITCH_OFFSET_CNTL ?
                      s->regs.dst_pitch : s->regs.default_pitch);

        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
            dst_bits += s->regs.crtc_offset & 0x07ffffff;
            dst_stride *= bpp;
        }
        dst_stride /= sizeof(uint32_t);

        switch (s->regs.dp_mix & GMC_ROP3_MASK) {
        case ROP3_PATCOPY:
            filler = bswap32(s->regs.dp_brush_frgd_clr);
            break;
        case ROP3_BLACKNESS:
            filler = rgb_to_pixel32(s->vga.palette[0], s->vga.palette[1],
                                    s->vga.palette[2]) << 8 | 0xff;
            break;
        case ROP3_WHITENESS:
            filler = rgb_to_pixel32(s->vga.palette[3], s->vga.palette[4],
                                    s->vga.palette[5]) << 8 | 0xff;
            break;
        }

        DPRINTF("pixman_fill(%p, %d, %d, %d, %d, %d, %d, %x)\n",
                dst_bits, dst_stride, bpp,
                s->regs.dst_x, s->regs.dst_y,
                s->regs.dst_width, s->regs.dst_height,
                filler);
        end = s->vga.vram_ptr + s->vga.vram_size;
        if (dst_bits >= end ||
            dst_bits + s->regs.dst_x + (s->regs.dst_y + s->regs.dst_height) *
            dst_stride * sizeof(uint32_t) >= end) {
            qemu_log_mask(LOG_UNIMP, "blt outside vram not implemented\n");
            return;
        }
        pixman_fill((uint32_t *)dst_bits, dst_stride, bpp,
                   s->regs.dst_x, s->regs.dst_y,
                   s->regs.dst_width, s->regs.dst_height,
                   filler);
        if (dst_bits >= s->vga.vram_ptr + s->vga.vbe_start_addr &&
            dst_bits < s->vga.vram_ptr + s->vga.vbe_start_addr +
            s->vga.vbe_regs[VBE_DISPI_INDEX_YRES] * s->vga.vbe_line_offset) {
            memory_region_set_dirty(&s->vga.vram, s->vga.vbe_start_addr +
                                    s->regs.dst_offset +
                                    s->regs.dst_y * surface_stride(ds),
                                    s->regs.dst_height * surface_stride(ds));
        }
        s->regs.dst_y += s->regs.dst_height;
        break;
    }
    default:
        qemu_log_mask(LOG_UNIMP, "Unimplemented ati_2d blt op %x\n",
                      (s->regs.dp_mix & GMC_ROP3_MASK) >> 16);
    }
}
