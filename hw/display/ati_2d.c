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
#include "ui/rect.h"

/*
 * NOTE:
 * This is 2D _acceleration_ and supposed to be fast. Therefore, don't try to
 * reinvent the wheel (unlikely to get better with a naive implementation than
 * existing libraries) and avoid (poorly) reimplementing gfx primitives.
 * That is unnecessary and would become a performance problem. Instead, try to
 * map to and reuse existing optimised facilities (e.g. pixman) wherever
 * possible.
 */

static int ati_bpp_from_datatype(const ATIVGAState *s)
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

typedef struct {
    int bpp;
    uint32_t rop3;
    bool left_to_right;
    bool top_to_bottom;
    uint32_t frgd_clr;
    const uint8_t *palette;
    const uint8_t *vram_end;
    QemuRect scissor;

    QemuRect dst;
    int dst_stride;
    uint8_t *dst_bits;
    uint32_t dst_offset;

    QemuRect src;
    int src_stride;
    const uint8_t *src_bits;
} ATI2DCtx;

static void ati_set_dirty(VGACommonState *vga, const ATI2DCtx *ctx)
{
    DisplaySurface *ds = qemu_console_surface(vga->con);

    DPRINTF("%p %u ds: %p %d %d rop: %x\n", vga->vram_ptr, vga->vbe_start_addr,
            surface_data(ds), surface_stride(ds), surface_bits_per_pixel(ds),
            ctx->rop3 >> 16);
    if (ctx->dst_bits >= vga->vram_ptr + vga->vbe_start_addr &&
        ctx->dst_bits < vga->vram_ptr + vga->vbe_start_addr +
        vga->vbe_regs[VBE_DISPI_INDEX_YRES] * vga->vbe_line_offset) {
        memory_region_set_dirty(&vga->vram,
                                vga->vbe_start_addr + ctx->dst_offset +
                                ctx->dst.y * surface_stride(ds),
                                ctx->dst.height * surface_stride(ds));
    }
}

static void setup_2d_blt_ctx(const ATIVGAState *s, ATI2DCtx *ctx)
{
    ctx->bpp = ati_bpp_from_datatype(s);
    ctx->rop3 = s->regs.dp_mix & GMC_ROP3_MASK;
    ctx->left_to_right = s->regs.dp_cntl & DST_X_LEFT_TO_RIGHT;
    ctx->top_to_bottom = s->regs.dp_cntl & DST_Y_TOP_TO_BOTTOM;
    ctx->frgd_clr = s->regs.dp_brush_frgd_clr;
    ctx->palette = s->vga.palette;
    ctx->dst_offset = s->regs.dst_offset;
    ctx->vram_end = s->vga.vram_ptr + s->vga.vram_size;

    ctx->scissor.width = s->regs.sc_right - s->regs.sc_left + 1;
    ctx->scissor.height = s->regs.sc_bottom - s->regs.sc_top + 1;
    ctx->scissor.x = s->regs.sc_left;
    ctx->scissor.y = s->regs.sc_top;

    ctx->dst.width = s->regs.dst_width;
    ctx->dst.height = s->regs.dst_height;
    ctx->dst.x = (ctx->left_to_right ?
                 s->regs.dst_x : s->regs.dst_x + 1 - ctx->dst.width);
    ctx->dst.y = (ctx->top_to_bottom ?
                 s->regs.dst_y : s->regs.dst_y + 1 - ctx->dst.height);
    ctx->dst_stride = s->regs.dst_pitch;
    ctx->dst_bits = s->vga.vram_ptr + s->regs.dst_offset;
    if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
        ctx->dst_bits += s->regs.crtc_offset & 0x07ffffff;
        ctx->dst_stride *= ctx->bpp;
    }

    ctx->src.x = (ctx->left_to_right ?
                 s->regs.src_x : s->regs.src_x + 1 - ctx->dst.width);
    ctx->src.y = (ctx->top_to_bottom ?
                 s->regs.src_y : s->regs.src_y + 1 - ctx->dst.height);
    ctx->src_stride = s->regs.src_pitch;
    ctx->src_bits = s->vga.vram_ptr + s->regs.src_offset;
    if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
        ctx->src_bits += s->regs.crtc_offset & 0x07ffffff;
        ctx->src_stride *= ctx->bpp;
    }
    DPRINTF("%d %d %d, %d %d %d, (%d,%d) -> (%d,%d) %dx%d %c %c\n",
            s->regs.src_offset, s->regs.dst_offset, s->regs.default_offset,
            ctx->src_stride, ctx->dst_stride, s->regs.default_pitch,
            ctx->src.x, ctx->src.y, ctx->dst.x, ctx->dst.y,
            ctx->dst.width, ctx->dst.height,
            (ctx->left_to_right ? '>' : '<'),
            (ctx->top_to_bottom ? 'v' : '^'));
}

static bool ati_2d_do_blt(ATI2DCtx *ctx, uint8_t use_pixman)
{
    QemuRect vis_src, vis_dst;

    if (!ctx->bpp) {
        qemu_log_mask(LOG_GUEST_ERROR, "Invalid bpp\n");
        return false;
    }
    if (!ctx->dst_stride) {
        qemu_log_mask(LOG_GUEST_ERROR, "Zero dest pitch\n");
        return false;
    }
    if (ctx->dst.x > 0x3fff || ctx->dst.y > 0x3fff ||
        ctx->dst_bits >= ctx->vram_end || ctx->dst_bits + ctx->dst.x +
        (ctx->dst.y + ctx->dst.height) * ctx->dst_stride >= ctx->vram_end) {
        qemu_log_mask(LOG_UNIMP, "blt outside vram not implemented\n");
        return false;
    }
    qemu_rect_intersect(&ctx->dst, &ctx->scissor, &vis_dst);
    if (!vis_dst.height || !vis_dst.width) {
        /* Nothing is visible, completely clipped */
        return false;
    }
    /*
     * The src must be offset if clipping is applied to the dst.
     * This is so that when the source is blit to a dst clipped
     * on the top or left the src image is not shifted into the
     * clipped region but actually clipped.
     */
    vis_src.x = ctx->src.x + (vis_dst.x - ctx->dst.x);
    vis_src.y = ctx->src.y + (vis_dst.y - ctx->dst.y);
    vis_src.width = vis_dst.width;
    vis_src.height = vis_dst.height;

    DPRINTF("dst: (%d,%d) %dx%d -> vis_dst: (%d,%d) %dx%d\n",
            ctx->dst.x, ctx->dst.y, ctx->dst.width, ctx->dst.height,
            vis_dst.x, vis_dst.y, vis_dst.width, vis_dst.height);
    DPRINTF("src: (%d,%d) %dx%d -> vis_src: (%d,%d) %dx%d\n",
            ctx->src.x, ctx->src.y, ctx->dst.width, ctx->dst.height,
            vis_src.x, vis_src.y, vis_src.width, vis_src.height);

    switch (ctx->rop3) {
    case ROP3_SRCCOPY:
    {
        bool fallback = false;
        if (!ctx->src_stride) {
            qemu_log_mask(LOG_GUEST_ERROR, "Zero source pitch\n");
            return false;
        }
        if (vis_src.x > 0x3fff || vis_src.y > 0x3fff ||
            ctx->src_bits >= ctx->vram_end || ctx->src_bits + vis_src.x +
            (vis_src.y + vis_dst.height) * ctx->src_stride >= ctx->vram_end) {
            qemu_log_mask(LOG_UNIMP, "blt outside vram not implemented\n");
            return false;
        }

        DPRINTF("pixman_blt(%p, %p, %ld, %ld, %d, %d, %d, %d, %d, %d, %d, %d)\n",
                ctx->src_bits, ctx->dst_bits,
                ctx->src_stride / sizeof(uint32_t),
                ctx->dst_stride / sizeof(uint32_t),
                ctx->bpp, ctx->bpp, vis_src.x, vis_src.y, vis_dst.x, vis_dst.y,
                vis_dst.width, vis_dst.height);
#ifdef CONFIG_PIXMAN
        int src_stride_words = ctx->src_stride / sizeof(uint32_t);
        int dst_stride_words = ctx->dst_stride / sizeof(uint32_t);
        if ((use_pixman & BIT(1)) &&
            ctx->left_to_right && ctx->top_to_bottom) {
            fallback = !pixman_blt((uint32_t *)ctx->src_bits,
                                   (uint32_t *)ctx->dst_bits, src_stride_words,
                                   dst_stride_words, ctx->bpp, ctx->bpp,
                                   vis_src.x, vis_src.y, vis_dst.x, vis_dst.y,
                                   vis_dst.width, vis_dst.height);
        } else if (use_pixman & BIT(1)) {
            /* FIXME: We only really need a temporary if src and dst overlap */
            int llb = vis_dst.width * (ctx->bpp / 8);
            int tmp_stride_words = DIV_ROUND_UP(llb, sizeof(uint32_t));
            uint32_t *tmp = g_malloc(tmp_stride_words * sizeof(uint32_t) *
                                     vis_dst.height);
            fallback = !pixman_blt((uint32_t *)ctx->src_bits, tmp,
                                   src_stride_words, tmp_stride_words, ctx->bpp,
                                   ctx->bpp, vis_src.x, vis_src.y, 0, 0,
                                   vis_dst.width, vis_dst.height);
            if (!fallback) {
                fallback = !pixman_blt(tmp, (uint32_t *)ctx->dst_bits,
                                       tmp_stride_words, dst_stride_words,
                                       ctx->bpp, ctx->bpp, 0, 0,
                                       vis_dst.x, vis_dst.y,
                                       vis_dst.width, vis_dst.height);
            }
            g_free(tmp);
        } else
#endif
        {
            fallback = true;
        }
        if (fallback) {
            unsigned int y, i, j, bypp = ctx->bpp / 8;
            for (y = 0; y < vis_dst.height; y++) {
                i = vis_dst.x * bypp;
                j = vis_src.x * bypp;
                if (ctx->top_to_bottom) {
                    i += (vis_dst.y + y) * ctx->dst_stride;
                    j += (vis_src.y + y) * ctx->src_stride;
                } else {
                    i += (vis_dst.y + vis_dst.height - 1 - y)
                         * ctx->dst_stride;
                    j += (vis_src.y + vis_dst.height - 1 - y)
                         * ctx->src_stride;
                }
                memmove(&ctx->dst_bits[i], &ctx->src_bits[j],
                        vis_dst.width * bypp);
            }
        }
        break;
    }
    case ROP3_PATCOPY:
    case ROP3_BLACKNESS:
    case ROP3_WHITENESS:
    {
        uint32_t filler = 0;

        switch (ctx->rop3) {
        case ROP3_PATCOPY:
            filler = ctx->frgd_clr;
            break;
        case ROP3_BLACKNESS:
            filler = 0xffUL << 24 | rgb_to_pixel32(ctx->palette[0],
                                                   ctx->palette[1],
                                                   ctx->palette[2]);
            break;
        case ROP3_WHITENESS:
            filler = 0xffUL << 24 | rgb_to_pixel32(ctx->palette[3],
                                                   ctx->palette[4],
                                                   ctx->palette[5]);
            break;
        }

        DPRINTF("pixman_fill(%p, %ld, %d, %d, %d, %d, %d, %x)\n",
                ctx->dst_bits, ctx->dst_stride / sizeof(uint32_t), ctx->bpp,
                vis_dst.x, vis_dst.y, vis_dst.width, vis_dst.height, filler);
#ifdef CONFIG_PIXMAN
        if (!(use_pixman & BIT(0)) ||
            !pixman_fill((uint32_t *)ctx->dst_bits,
                         ctx->dst_stride / sizeof(uint32_t), ctx->bpp,
                         vis_dst.x, vis_dst.y, vis_dst.width, vis_dst.height,
                         filler))
#endif
        {
            /* fallback when pixman failed or we don't want to call it */
            unsigned int x, y, i, bypp = ctx->bpp / 8;
            for (y = 0; y < vis_dst.height; y++) {
                i = vis_dst.x * bypp + (vis_dst.y + y) * ctx->dst_stride;
                for (x = 0; x < vis_dst.width; x++, i += bypp) {
                    stn_he_p(&ctx->dst_bits[i], bypp, filler);
                }
            }
        }
        break;
    }
    default:
        qemu_log_mask(LOG_UNIMP, "Unimplemented ati_2d blt op %x\n",
                      ctx->rop3 >> 16);
        return false;
    }

    return true;
}

void ati_2d_blt(ATIVGAState *s)
{
    ATI2DCtx ctx;
    setup_2d_blt_ctx(s, &ctx);
    if (ati_2d_do_blt(&ctx, s->use_pixman)) {
        ati_set_dirty(&s->vga, &ctx);
    }
}
