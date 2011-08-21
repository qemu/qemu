/*
 * OMAP LCD controller.
 *
 * Copyright (C) 2006-2007 Andrzej Zaborowski  <balrog@zabor.org>
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
#include "omap.h"
#include "framebuffer.h"

struct omap_lcd_panel_s {
    qemu_irq irq;
    DisplayState *state;
    ram_addr_t imif_base;
    ram_addr_t emiff_base;

    int plm;
    int tft;
    int mono;
    int enable;
    int width;
    int height;
    int interrupts;
    uint32_t timing[3];
    uint32_t subpanel;
    uint32_t ctrl;

    struct omap_dma_lcd_channel_s *dma;
    uint16_t palette[256];
    int palette_done;
    int frame_done;
    int invalidate;
    int sync_error;
};

static void omap_lcd_interrupts(struct omap_lcd_panel_s *s)
{
    if (s->frame_done && (s->interrupts & 1)) {
        qemu_irq_raise(s->irq);
        return;
    }

    if (s->palette_done && (s->interrupts & 2)) {
        qemu_irq_raise(s->irq);
        return;
    }

    if (s->sync_error) {
        qemu_irq_raise(s->irq);
        return;
    }

    qemu_irq_lower(s->irq);
}

#include "pixel_ops.h"

#define draw_line_func drawfn

#define DEPTH 8
#include "omap_lcd_template.h"
#define DEPTH 15
#include "omap_lcd_template.h"
#define DEPTH 16
#include "omap_lcd_template.h"
#define DEPTH 32
#include "omap_lcd_template.h"

static draw_line_func draw_line_table2[33] = {
    [0 ... 32]	= NULL,
    [8]		= draw_line2_8,
    [15]	= draw_line2_15,
    [16]	= draw_line2_16,
    [32]	= draw_line2_32,
}, draw_line_table4[33] = {
    [0 ... 32]	= NULL,
    [8]		= draw_line4_8,
    [15]	= draw_line4_15,
    [16]	= draw_line4_16,
    [32]	= draw_line4_32,
}, draw_line_table8[33] = {
    [0 ... 32]	= NULL,
    [8]		= draw_line8_8,
    [15]	= draw_line8_15,
    [16]	= draw_line8_16,
    [32]	= draw_line8_32,
}, draw_line_table12[33] = {
    [0 ... 32]	= NULL,
    [8]		= draw_line12_8,
    [15]	= draw_line12_15,
    [16]	= draw_line12_16,
    [32]	= draw_line12_32,
}, draw_line_table16[33] = {
    [0 ... 32]	= NULL,
    [8]		= draw_line16_8,
    [15]	= draw_line16_15,
    [16]	= draw_line16_16,
    [32]	= draw_line16_32,
};

static void omap_update_display(void *opaque)
{
    struct omap_lcd_panel_s *omap_lcd = (struct omap_lcd_panel_s *) opaque;
    draw_line_func draw_line;
    int size, height, first, last;
    int width, linesize, step, bpp, frame_offset;
    target_phys_addr_t frame_base;

    if (!omap_lcd || omap_lcd->plm == 1 ||
                    !omap_lcd->enable || !ds_get_bits_per_pixel(omap_lcd->state))
        return;

    frame_offset = 0;
    if (omap_lcd->plm != 2) {
        cpu_physical_memory_read(omap_lcd->dma->phys_framebuffer[
                                  omap_lcd->dma->current_frame],
                                 (void *)omap_lcd->palette, 0x200);
        switch (omap_lcd->palette[0] >> 12 & 7) {
        case 3 ... 7:
            frame_offset += 0x200;
            break;
        default:
            frame_offset += 0x20;
        }
    }

    /* Colour depth */
    switch ((omap_lcd->palette[0] >> 12) & 7) {
    case 1:
        draw_line = draw_line_table2[ds_get_bits_per_pixel(omap_lcd->state)];
        bpp = 2;
        break;

    case 2:
        draw_line = draw_line_table4[ds_get_bits_per_pixel(omap_lcd->state)];
        bpp = 4;
        break;

    case 3:
        draw_line = draw_line_table8[ds_get_bits_per_pixel(omap_lcd->state)];
        bpp = 8;
        break;

    case 4 ... 7:
        if (!omap_lcd->tft)
            draw_line = draw_line_table12[ds_get_bits_per_pixel(omap_lcd->state)];
        else
            draw_line = draw_line_table16[ds_get_bits_per_pixel(omap_lcd->state)];
        bpp = 16;
        break;

    default:
        /* Unsupported at the moment.  */
        return;
    }

    /* Resolution */
    width = omap_lcd->width;
    if (width != ds_get_width(omap_lcd->state) ||
            omap_lcd->height != ds_get_height(omap_lcd->state)) {
        qemu_console_resize(omap_lcd->state,
                            omap_lcd->width, omap_lcd->height);
        omap_lcd->invalidate = 1;
    }

    if (omap_lcd->dma->current_frame == 0)
        size = omap_lcd->dma->src_f1_bottom - omap_lcd->dma->src_f1_top;
    else
        size = omap_lcd->dma->src_f2_bottom - omap_lcd->dma->src_f2_top;

    if (frame_offset + ((width * omap_lcd->height * bpp) >> 3) > size + 2) {
        omap_lcd->sync_error = 1;
        omap_lcd_interrupts(omap_lcd);
        omap_lcd->enable = 0;
        return;
    }

    /* Content */
    frame_base = omap_lcd->dma->phys_framebuffer[
            omap_lcd->dma->current_frame] + frame_offset;
    omap_lcd->dma->condition |= 1 << omap_lcd->dma->current_frame;
    if (omap_lcd->dma->interrupts & 1)
        qemu_irq_raise(omap_lcd->dma->irq);
    if (omap_lcd->dma->dual)
        omap_lcd->dma->current_frame ^= 1;

    if (!ds_get_bits_per_pixel(omap_lcd->state))
        return;

    first = 0;
    height = omap_lcd->height;
    if (omap_lcd->subpanel & (1 << 31)) {
        if (omap_lcd->subpanel & (1 << 29))
            first = (omap_lcd->subpanel >> 16) & 0x3ff;
        else
            height = (omap_lcd->subpanel >> 16) & 0x3ff;
        /* TODO: fill the rest of the panel with DPD */
    }

    step = width * bpp >> 3;
    linesize = ds_get_linesize(omap_lcd->state);
    framebuffer_update_display(omap_lcd->state,
                               frame_base, width, height,
                               step, linesize, 0,
                               omap_lcd->invalidate,
                               draw_line, omap_lcd->palette,
                               &first, &last);
    if (first >= 0) {
        dpy_update(omap_lcd->state, 0, first, width, last - first + 1);
    }
    omap_lcd->invalidate = 0;
}

static int ppm_save(const char *filename, uint8_t *data,
                int w, int h, int linesize)
{
    FILE *f;
    uint8_t *d, *d1;
    unsigned int v;
    int y, x, bpp;

    f = fopen(filename, "wb");
    if (!f)
        return -1;
    fprintf(f, "P6\n%d %d\n%d\n", w, h, 255);
    d1 = data;
    bpp = linesize / w;
    for (y = 0; y < h; y ++) {
        d = d1;
        for (x = 0; x < w; x ++) {
            v = *(uint32_t *) d;
            switch (bpp) {
            case 2:
                fputc((v >> 8) & 0xf8, f);
                fputc((v >> 3) & 0xfc, f);
                fputc((v << 3) & 0xf8, f);
                break;
            case 3:
            case 4:
            default:
                fputc((v >> 16) & 0xff, f);
                fputc((v >> 8) & 0xff, f);
                fputc((v) & 0xff, f);
                break;
            }
            d += bpp;
        }
        d1 += linesize;
    }
    fclose(f);
    return 0;
}

static void omap_screen_dump(void *opaque, const char *filename) {
    struct omap_lcd_panel_s *omap_lcd = opaque;
    omap_update_display(opaque);
    if (omap_lcd && ds_get_data(omap_lcd->state))
        ppm_save(filename, ds_get_data(omap_lcd->state),
                omap_lcd->width, omap_lcd->height,
                ds_get_linesize(omap_lcd->state));
}

static void omap_invalidate_display(void *opaque) {
    struct omap_lcd_panel_s *omap_lcd = opaque;
    omap_lcd->invalidate = 1;
}

static void omap_lcd_update(struct omap_lcd_panel_s *s) {
    if (!s->enable) {
        s->dma->current_frame = -1;
        s->sync_error = 0;
        if (s->plm != 1)
            s->frame_done = 1;
        omap_lcd_interrupts(s);
        return;
    }

    if (s->dma->current_frame == -1) {
        s->frame_done = 0;
        s->palette_done = 0;
        s->dma->current_frame = 0;
    }

    if (!s->dma->mpu->port[s->dma->src].addr_valid(s->dma->mpu,
                            s->dma->src_f1_top) ||
                    !s->dma->mpu->port[
                    s->dma->src].addr_valid(s->dma->mpu,
                            s->dma->src_f1_bottom) ||
                    (s->dma->dual &&
                     (!s->dma->mpu->port[
                      s->dma->src].addr_valid(s->dma->mpu,
                              s->dma->src_f2_top) ||
                      !s->dma->mpu->port[
                      s->dma->src].addr_valid(s->dma->mpu,
                              s->dma->src_f2_bottom)))) {
        s->dma->condition |= 1 << 2;
        if (s->dma->interrupts & (1 << 1))
            qemu_irq_raise(s->dma->irq);
        s->enable = 0;
        return;
    }

    s->dma->phys_framebuffer[0] = s->dma->src_f1_top;
    s->dma->phys_framebuffer[1] = s->dma->src_f2_top;

    if (s->plm != 2 && !s->palette_done) {
        cpu_physical_memory_read(
            s->dma->phys_framebuffer[s->dma->current_frame],
            (void *)s->palette, 0x200);
        s->palette_done = 1;
        omap_lcd_interrupts(s);
    }
}

static uint32_t omap_lcdc_read(void *opaque, target_phys_addr_t addr)
{
    struct omap_lcd_panel_s *s = (struct omap_lcd_panel_s *) opaque;

    switch (addr) {
    case 0x00:	/* LCD_CONTROL */
        return (s->tft << 23) | (s->plm << 20) |
                (s->tft << 7) | (s->interrupts << 3) |
                (s->mono << 1) | s->enable | s->ctrl | 0xfe000c34;

    case 0x04:	/* LCD_TIMING0 */
        return (s->timing[0] << 10) | (s->width - 1) | 0x0000000f;

    case 0x08:	/* LCD_TIMING1 */
        return (s->timing[1] << 10) | (s->height - 1);

    case 0x0c:	/* LCD_TIMING2 */
        return s->timing[2] | 0xfc000000;

    case 0x10:	/* LCD_STATUS */
        return (s->palette_done << 6) | (s->sync_error << 2) | s->frame_done;

    case 0x14:	/* LCD_SUBPANEL */
        return s->subpanel;

    default:
        break;
    }
    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_lcdc_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct omap_lcd_panel_s *s = (struct omap_lcd_panel_s *) opaque;

    switch (addr) {
    case 0x00:	/* LCD_CONTROL */
        s->plm = (value >> 20) & 3;
        s->tft = (value >> 7) & 1;
        s->interrupts = (value >> 3) & 3;
        s->mono = (value >> 1) & 1;
        s->ctrl = value & 0x01cff300;
        if (s->enable != (value & 1)) {
            s->enable = value & 1;
            omap_lcd_update(s);
        }
        break;

    case 0x04:	/* LCD_TIMING0 */
        s->timing[0] = value >> 10;
        s->width = (value & 0x3ff) + 1;
        break;

    case 0x08:	/* LCD_TIMING1 */
        s->timing[1] = value >> 10;
        s->height = (value & 0x3ff) + 1;
        break;

    case 0x0c:	/* LCD_TIMING2 */
        s->timing[2] = value;
        break;

    case 0x10:	/* LCD_STATUS */
        break;

    case 0x14:	/* LCD_SUBPANEL */
        s->subpanel = value & 0xa1ffffff;
        break;

    default:
        OMAP_BAD_REG(addr);
    }
}

static CPUReadMemoryFunc * const omap_lcdc_readfn[] = {
    omap_lcdc_read,
    omap_lcdc_read,
    omap_lcdc_read,
};

static CPUWriteMemoryFunc * const omap_lcdc_writefn[] = {
    omap_lcdc_write,
    omap_lcdc_write,
    omap_lcdc_write,
};

void omap_lcdc_reset(struct omap_lcd_panel_s *s)
{
    s->dma->current_frame = -1;
    s->plm = 0;
    s->tft = 0;
    s->mono = 0;
    s->enable = 0;
    s->width = 0;
    s->height = 0;
    s->interrupts = 0;
    s->timing[0] = 0;
    s->timing[1] = 0;
    s->timing[2] = 0;
    s->subpanel = 0;
    s->palette_done = 0;
    s->frame_done = 0;
    s->sync_error = 0;
    s->invalidate = 1;
    s->subpanel = 0;
    s->ctrl = 0;
}

struct omap_lcd_panel_s *omap_lcdc_init(target_phys_addr_t base, qemu_irq irq,
                struct omap_dma_lcd_channel_s *dma,
                ram_addr_t imif_base, ram_addr_t emiff_base, omap_clk clk)
{
    int iomemtype;
    struct omap_lcd_panel_s *s = (struct omap_lcd_panel_s *)
            g_malloc0(sizeof(struct omap_lcd_panel_s));

    s->irq = irq;
    s->dma = dma;
    s->imif_base = imif_base;
    s->emiff_base = emiff_base;
    omap_lcdc_reset(s);

    iomemtype = cpu_register_io_memory(omap_lcdc_readfn,
                    omap_lcdc_writefn, s, DEVICE_NATIVE_ENDIAN);
    cpu_register_physical_memory(base, 0x100, iomemtype);

    s->state = graphic_console_init(omap_update_display,
                                    omap_invalidate_display,
                                    omap_screen_dump, NULL, s);

    return s;
}
