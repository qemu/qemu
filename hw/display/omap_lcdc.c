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

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "ui/console.h"
#include "hw/arm/omap.h"
#include "framebuffer.h"
#include "ui/pixel_ops.h"

struct omap_lcd_panel_s {
    MemoryRegion *sysmem;
    MemoryRegion iomem;
    MemoryRegionSection fbsection;
    qemu_irq irq;
    QemuConsole *con;

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

/*
 * 2-bit colour
 */
static void draw_line2_32(void *opaque, uint8_t *d, const uint8_t *s,
                          int width, int deststep)
{
    uint16_t *pal = opaque;
    uint8_t v, r, g, b;

    do {
        v = ldub_p((void *) s);
        r = (pal[v & 3] >> 4) & 0xf0;
        g = pal[v & 3] & 0xf0;
        b = (pal[v & 3] << 4) & 0xf0;
        ((uint32_t *) d)[0] = rgb_to_pixel32(r, g, b);
        d += 4;
        v >>= 2;
        r = (pal[v & 3] >> 4) & 0xf0;
        g = pal[v & 3] & 0xf0;
        b = (pal[v & 3] << 4) & 0xf0;
        ((uint32_t *) d)[0] = rgb_to_pixel32(r, g, b);
        d += 4;
        v >>= 2;
        r = (pal[v & 3] >> 4) & 0xf0;
        g = pal[v & 3] & 0xf0;
        b = (pal[v & 3] << 4) & 0xf0;
        ((uint32_t *) d)[0] = rgb_to_pixel32(r, g, b);
        d += 4;
        v >>= 2;
        r = (pal[v & 3] >> 4) & 0xf0;
        g = pal[v & 3] & 0xf0;
        b = (pal[v & 3] << 4) & 0xf0;
        ((uint32_t *) d)[0] = rgb_to_pixel32(r, g, b);
        d += 4;
        s++;
        width -= 4;
    } while (width > 0);
}

/*
 * 4-bit colour
 */
static void draw_line4_32(void *opaque, uint8_t *d, const uint8_t *s,
                          int width, int deststep)
{
    uint16_t *pal = opaque;
    uint8_t v, r, g, b;

    do {
        v = ldub_p((void *) s);
        r = (pal[v & 0xf] >> 4) & 0xf0;
        g = pal[v & 0xf] & 0xf0;
        b = (pal[v & 0xf] << 4) & 0xf0;
        ((uint32_t *) d)[0] = rgb_to_pixel32(r, g, b);
        d += 4;
        v >>= 4;
        r = (pal[v & 0xf] >> 4) & 0xf0;
        g = pal[v & 0xf] & 0xf0;
        b = (pal[v & 0xf] << 4) & 0xf0;
        ((uint32_t *) d)[0] = rgb_to_pixel32(r, g, b);
        d += 4;
        s++;
        width -= 2;
    } while (width > 0);
}

/*
 * 8-bit colour
 */
static void draw_line8_32(void *opaque, uint8_t *d, const uint8_t *s,
                          int width, int deststep)
{
    uint16_t *pal = opaque;
    uint8_t v, r, g, b;

    do {
        v = ldub_p((void *) s);
        r = (pal[v] >> 4) & 0xf0;
        g = pal[v] & 0xf0;
        b = (pal[v] << 4) & 0xf0;
        ((uint32_t *) d)[0] = rgb_to_pixel32(r, g, b);
        s++;
        d += 4;
    } while (-- width != 0);
}

/*
 * 12-bit colour
 */
static void draw_line12_32(void *opaque, uint8_t *d, const uint8_t *s,
                           int width, int deststep)
{
    uint16_t v;
    uint8_t r, g, b;

    do {
        v = lduw_le_p((void *) s);
        r = (v >> 4) & 0xf0;
        g = v & 0xf0;
        b = (v << 4) & 0xf0;
        ((uint32_t *) d)[0] = rgb_to_pixel32(r, g, b);
        s += 2;
        d += 4;
    } while (-- width != 0);
}

/*
 * 16-bit colour
 */
static void draw_line16_32(void *opaque, uint8_t *d, const uint8_t *s,
                           int width, int deststep)
{
    uint16_t v;
    uint8_t r, g, b;

    do {
        v = lduw_le_p((void *) s);
        r = (v >> 8) & 0xf8;
        g = (v >> 3) & 0xfc;
        b = (v << 3) & 0xf8;
        ((uint32_t *) d)[0] = rgb_to_pixel32(r, g, b);
        s += 2;
        d += 4;
    } while (-- width != 0);
}

static void omap_update_display(void *opaque)
{
    struct omap_lcd_panel_s *omap_lcd = (struct omap_lcd_panel_s *) opaque;
    DisplaySurface *surface;
    drawfn draw_line;
    int size, height, first, last;
    int width, linesize, step, bpp, frame_offset;
    hwaddr frame_base;

    if (!omap_lcd || omap_lcd->plm == 1 || !omap_lcd->enable) {
        return;
    }

    surface = qemu_console_surface(omap_lcd->con);
    if (!surface_bits_per_pixel(surface)) {
        return;
    }

    frame_offset = 0;
    if (omap_lcd->plm != 2) {
        cpu_physical_memory_read(
                omap_lcd->dma->phys_framebuffer[omap_lcd->dma->current_frame],
                omap_lcd->palette, 0x200);
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
        draw_line = draw_line2_32;
        bpp = 2;
        break;

    case 2:
        draw_line = draw_line4_32;
        bpp = 4;
        break;

    case 3:
        draw_line = draw_line8_32;
        bpp = 8;
        break;

    case 4 ... 7:
        if (!omap_lcd->tft)
            draw_line = draw_line12_32;
        else
            draw_line = draw_line16_32;
        bpp = 16;
        break;

    default:
        /* Unsupported at the moment.  */
        return;
    }

    /* Resolution */
    width = omap_lcd->width;
    if (width != surface_width(surface) ||
        omap_lcd->height != surface_height(surface)) {
        qemu_console_resize(omap_lcd->con,
                            omap_lcd->width, omap_lcd->height);
        surface = qemu_console_surface(omap_lcd->con);
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

    if (!surface_bits_per_pixel(surface)) {
        return;
    }

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
    linesize = surface_stride(surface);
    if (omap_lcd->invalidate) {
        framebuffer_update_memory_section(&omap_lcd->fbsection,
                                          omap_lcd->sysmem, frame_base,
                                          height, step);
    }

    framebuffer_update_display(surface, &omap_lcd->fbsection,
                               width, height,
                               step, linesize, 0,
                               omap_lcd->invalidate,
                               draw_line, omap_lcd->palette,
                               &first, &last);

    if (first >= 0) {
        dpy_gfx_update(omap_lcd->con, 0, first, width, last - first + 1);
    }
    omap_lcd->invalidate = 0;
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
                            s->palette, 0x200);
        s->palette_done = 1;
        omap_lcd_interrupts(s);
    }
}

static uint64_t omap_lcdc_read(void *opaque, hwaddr addr,
                               unsigned size)
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

static void omap_lcdc_write(void *opaque, hwaddr addr,
                            uint64_t value, unsigned size)
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

static const MemoryRegionOps omap_lcdc_ops = {
    .read = omap_lcdc_read,
    .write = omap_lcdc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
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

static const GraphicHwOps omap_ops = {
    .invalidate  = omap_invalidate_display,
    .gfx_update  = omap_update_display,
};

struct omap_lcd_panel_s *omap_lcdc_init(MemoryRegion *sysmem,
                                        hwaddr base,
                                        qemu_irq irq,
                                        struct omap_dma_lcd_channel_s *dma,
                                        omap_clk clk)
{
    struct omap_lcd_panel_s *s = g_new0(struct omap_lcd_panel_s, 1);

    s->irq = irq;
    s->dma = dma;
    s->sysmem = sysmem;
    omap_lcdc_reset(s);

    memory_region_init_io(&s->iomem, NULL, &omap_lcdc_ops, s, "omap.lcdc", 0x100);
    memory_region_add_subregion(sysmem, base, &s->iomem);

    s->con = graphic_console_init(NULL, 0, &omap_ops, s);

    return s;
}
