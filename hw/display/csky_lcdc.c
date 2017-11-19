/*
 * CSKY LCD controller
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "ui/console.h"
#include "framebuffer.h"
#include "ui/pixel_ops.h"
#include "qemu/log.h"
#include "target/csky/cpu.h"
#include "exec/cpu-all.h"

#define TYPE_CSKY_LCDC  "csky_lcdc"
#define CSKY_LCDC(obj)  OBJECT_CHECK(csky_lcdc_state, (obj), TYPE_CSKY_LCDC)

typedef struct csky_lcdc_state {
    SysBusDevice parent_obj;

    MemoryRegion *sysmem;
    MemoryRegion iomem;
    MemoryRegionSection fbsection;
    QemuConsole *con;

    qemu_irq irq;

    int out_pixel_select;
    int dma_watermark_level;
    int video_mem_burst_len;
    int endian_select;
    int pixel_bit_size;
    int tft;
    int color;
    int lcd_enable;

    uint32_t timing[3];
    uint32_t base_addr;
    int line_fifo_run;
    int bus_error;
    int base_addr_update;
    int lcd_invalidate;
    uint32_t int_mask;

    uint32_t dither_duty_12;
    uint32_t dither_duty_47;
    uint32_t dither_duty_35;
    uint32_t dither_duty_23;
    uint32_t dither_duty_57;
    uint32_t dither_duty_34;
    uint32_t dither_duty_45;
    uint32_t dither_duty_67;

    uint16_t palette[256];
    int width;
    int height;
} csky_lcdc_state;

static const VMStateDescription vmstate_csky_lcdc = {
    .name = TYPE_CSKY_LCDC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(out_pixel_select, csky_lcdc_state),
        VMSTATE_INT32(dma_watermark_level, csky_lcdc_state),
        VMSTATE_INT32(video_mem_burst_len, csky_lcdc_state),
        VMSTATE_INT32(endian_select, csky_lcdc_state),
        VMSTATE_INT32(pixel_bit_size, csky_lcdc_state),
        VMSTATE_INT32(tft, csky_lcdc_state),
        VMSTATE_INT32(color, csky_lcdc_state),
        VMSTATE_INT32(lcd_enable, csky_lcdc_state),

        VMSTATE_UINT32_ARRAY(timing, csky_lcdc_state, 3),
        VMSTATE_UINT32(base_addr, csky_lcdc_state),
        VMSTATE_INT32(line_fifo_run, csky_lcdc_state),
        VMSTATE_INT32(bus_error, csky_lcdc_state),
        VMSTATE_INT32(base_addr_update, csky_lcdc_state),
        VMSTATE_INT32(lcd_invalidate, csky_lcdc_state),
        VMSTATE_UINT32(int_mask, csky_lcdc_state),

        VMSTATE_UINT32(dither_duty_12, csky_lcdc_state),
        VMSTATE_UINT32(dither_duty_47, csky_lcdc_state),
        VMSTATE_UINT32(dither_duty_35, csky_lcdc_state),
        VMSTATE_UINT32(dither_duty_23, csky_lcdc_state),
        VMSTATE_UINT32(dither_duty_57, csky_lcdc_state),
        VMSTATE_UINT32(dither_duty_34, csky_lcdc_state),
        VMSTATE_UINT32(dither_duty_45, csky_lcdc_state),
        VMSTATE_UINT32(dither_duty_67, csky_lcdc_state),

        VMSTATE_UINT16_ARRAY(palette, csky_lcdc_state, 256),
        VMSTATE_INT32(width, csky_lcdc_state),
        VMSTATE_INT32(height, csky_lcdc_state),

        VMSTATE_END_OF_LIST()
    }
};


#define LCD_CONTROL     0x000
#define LCD_TIMING0     0x004
#define LCD_TIMING1     0x008
#define LCD_TIMING2     0x00C
#define LCD_PBASE       0x010

#define LCD_PCURR       0x018

#define LCD_INT_STAT    0x020
#define LCD_INT_MASK    0x024
#define LCD_DP1_2       0x028
#define LCD_DP4_7       0x02C
#define LCD_DP3_5       0x030
#define LCD_DP2_3       0x034
#define LCD_DP5_7       0x038
#define LCD_DP3_4       0x03C
#define LCD_DP4_5       0x040
#define LCD_DP6_7       0x044

#define LCD_PALETTE     0x800 ... 0x9FC


static void csky_lcd_interrupts(csky_lcdc_state *s)
{
    if (s->line_fifo_run && (s->int_mask & 8)) {
        qemu_irq_raise(s->irq);
        return;
    }

    if (s->bus_error && (s->int_mask & 4)) {
        qemu_irq_raise(s->irq);
        return;
    }

    if (s->base_addr_update && (s->int_mask & 2)) {
        qemu_irq_raise(s->irq);
        return;
    }

    if (s->lcd_invalidate && (s->int_mask & 1)) {
        qemu_irq_raise(s->irq);
        return;
    }

    qemu_irq_lower(s->irq);
}

#define DEPTH 16
#include "csky_lcd_template.h"
#define DEPTH 24
#include "csky_lcd_template.h"
#define DEPTH 32
#include "csky_lcd_template.h"

static drawfn draw_line_table16[33] = {
    [0 ... 32]    = NULL,
    [16]    = draw_line16_16,
    [24]    = draw_line16_24,
    [32]    = draw_line16_32,
}, draw_line_table24[33] = {
    [0 ... 32]    = NULL,
    [16]    = draw_line24_16,
    [24]    = draw_line24_24,
    [32]    = draw_line24_32,
};

static void csky_update_display(void *opaque)
{
    csky_lcdc_state *csky_lcd = (csky_lcdc_state *) opaque;
    SysBusDevice *sbd = SYS_BUS_DEVICE(csky_lcd);
    DisplaySurface *surface = qemu_console_surface(csky_lcd->con);
    drawfn draw_line;
    int height, first, last;
    int width, linesize, step;
    hwaddr frame_base;

    if (!csky_lcd || !csky_lcd->lcd_enable ||
        !surface_bits_per_pixel(surface)) {
        return;
    }

    /* Colour depth */
    switch (csky_lcd->out_pixel_select) {
    case 0:
        draw_line = draw_line_table16[surface_bits_per_pixel(surface)];
        break;

    case 1:
        draw_line = draw_line_table24[surface_bits_per_pixel(surface)];
        break;

    /*
    case 2 :
        draw_line = draw_line_table16[surface_bits_per_pixel(surface)];
        break;
    */
    default:
        /* Unsupported at the moment.  */
        return;
    }

    /* Resolution */
    width = csky_lcd->width;
    if (width != surface_width(surface) ||
        csky_lcd->height != surface_height(surface)) {
        qemu_console_resize(csky_lcd->con,
                            csky_lcd->width, csky_lcd->height);
        surface = qemu_console_surface(csky_lcd->con);
    }
    /* FIXME: delete csky_lcd->base_addr_update by luoy */
    if (csky_lcd->bus_error | csky_lcd->line_fifo_run |
        csky_lcd->lcd_invalidate) {
        csky_lcd_interrupts(csky_lcd);
       // return;
    }
    /* Content */
    frame_base = csky_lcd->base_addr;

    if (!surface_bits_per_pixel(surface)) {
        return;
    }

    first = 0;
    height = csky_lcd->height;

    if (csky_lcd->out_pixel_select) {
        step = width * 32 >> 3;
    } else {
        step = width * 16 >> 3;
    }

    linesize = surface_stride(surface);

    if (csky_lcd->lcd_invalidate) {
        framebuffer_update_memory_section(&csky_lcd->fbsection,
                                          sysbus_address_space(sbd),
                                          frame_base,
                                          height, step);
    }

    framebuffer_update_display(surface, &csky_lcd->fbsection,
                               width, height,
                               step, linesize, 0,
                               csky_lcd->lcd_invalidate,
                               draw_line, csky_lcd->palette,
                               &first, &last);
    if (first >= 0) {
        dpy_gfx_update(csky_lcd->con, 0, first, width, last - first + 1);
    }
    csky_lcd->lcd_invalidate = 0;
}

static void csky_invalidate_display(void *opaque)
{
    csky_lcdc_state *csky_lcd = opaque;
    csky_lcd->lcd_invalidate = 1;
    csky_lcd_interrupts(csky_lcd);
}


static uint64_t csky_lcdc_read(void *opaque, hwaddr addr, unsigned size)
{
    csky_lcdc_state *s = (csky_lcdc_state *) opaque;
    uint64_t ret = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_lcdc_read: 0x%x must word align read\n",
                      (int)addr);
    }

    switch (addr) {
    case LCD_CONTROL:
        ret = (s->out_pixel_select << 12) | (s->dma_watermark_level << 11) |
            (s->video_mem_burst_len << 9) | (s->endian_select << 8) |
            (s->pixel_bit_size << 5) | (s->tft << 3) |
            (s->color << 1) | (s->lcd_enable);
        break;
    case LCD_TIMING0:
        ret = (s->timing[0] << 10) | ((s->width / 16 - 1) << 4);
        break;
    case LCD_TIMING1:
        ret = (s->timing[1] << 10) | (s->height - 1);
        break;
    case LCD_TIMING2:
        ret = s->timing[2];
        break;
    case LCD_PBASE:
        ret = s->base_addr;
        break;
    case LCD_PCURR:
        ret = s->base_addr;
        break;

    case LCD_INT_STAT:
        ret = (s->line_fifo_run << 3) | (s->bus_error << 2) |
            (s->base_addr_update << 1) | (s->lcd_invalidate);
        break;
    case LCD_INT_MASK:
        ret = s->int_mask;
        break;
    case LCD_DP1_2:
        ret = s->dither_duty_12;
        break;
    case LCD_DP4_7:
        ret = s->dither_duty_47;
        break;
    case LCD_DP3_5:
        ret = s->dither_duty_35;
        break;
    case LCD_DP2_3:
        ret = s->dither_duty_23;
        break;
    case LCD_DP5_7:
        ret = s->dither_duty_57;
        break;
    case LCD_DP3_4:
        ret = s->dither_duty_34;
        break;
    case LCD_DP4_5:
        ret = s->dither_duty_45;
        break;
    case LCD_DP6_7:
        ret = s->dither_duty_67;
        break;

    case LCD_PALETTE:
        if (!s->endian_select) {
            if (addr % 4 == 0) {
                ret = ((s->palette[(addr - 0x800) / 2 + 1]) << 16) |
                    ((s->palette[(addr - 0x800) / 2]) & 0x0000ffff);
            }
        } else {
            if (addr % 4 == 0) {
                ret = ((s->palette[(addr - 0x800) / 2]) << 16) |
                    ((s->palette[(addr - 0x800) / 2 + 1]) & 0x0000ffff);
            }
        }
        break;
    default:
        break;
    }

    return ret;
}


static void csky_lcdc_write(void *opaque, hwaddr addr, uint64_t value,
                            unsigned size)
{
    csky_lcdc_state *s = (csky_lcdc_state *) opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_lcdc_read: 0x%x must word align read\n",
                      (int)addr);
    }

    s->lcd_invalidate = 1;

    switch (addr) {
    case LCD_CONTROL:
        {
            s->out_pixel_select = (value >> 12) & 1;
            s->dma_watermark_level = (value >> 11) & 1;
            s->video_mem_burst_len = (value >> 9) & 3;
            s->endian_select = (value >> 8) & 1;
            s->pixel_bit_size = (value >> 5) & 3;
            s->tft = (value >> 3) & 1;
            s->color = (value >> 1) & 1;
            s->lcd_enable = (value & 1) ;
        }
        break;
    case LCD_TIMING0:
        {
            s->timing[0] = value >> 10 ;
            s->width = (((value & 0x000003f0) >> 4) + 1) * 16;
        }
        break;
    case LCD_TIMING1:
        {
            s->timing[1] = value >> 10 ;
            s->height = (value & 0x000003ff) ; /* remove +1 by luoy */
        }
        break;
    case LCD_TIMING2:
        s->timing[2] = value;
        break;
    case LCD_PBASE:
        s->base_addr = value;
        break;

    case LCD_PCURR:
        break;

    case LCD_INT_STAT:
        {
            s->line_fifo_run = (value & 0x00000008) >> 3;
            s->bus_error = (value & 0x00000004) >> 2;
            s->base_addr_update = (value & 0x00000002) >> 1;
            s->lcd_invalidate = value & 0x00000001;
            if ((value & 0x00000008) >> 3 == 1) {
                s->line_fifo_run = 0;
            }
            if ((value & 0x00000004) >> 2 == 1) {
                s->bus_error = 0;
            }
            if (value & 0x00000001) {
            //    s->lcd_invalidate = 0;
            }
            csky_lcd_interrupts(s);
        }
        break;
    case LCD_INT_MASK:
        s->int_mask = value & 0x0000000f;
        break;
    case LCD_DP1_2:
        s->dither_duty_12 = value;
        break;
    case LCD_DP4_7:
        s->dither_duty_47 = value;
        break;
    case LCD_DP3_5:
        s->dither_duty_35 = value;
        break;
    case LCD_DP2_3:
        s->dither_duty_23 = value;
        break;
    case LCD_DP5_7:
        s->dither_duty_57 = value;
        break;
    case LCD_DP3_4:
        s->dither_duty_34 = value;
        break;
    case LCD_DP4_5:
        s->dither_duty_45 = value;
        break;
    case LCD_DP6_7:
        s->dither_duty_67 = value;
        break;

    case LCD_PALETTE:
        {
            if (!s->endian_select) {
                if (addr % 4 == 0) {
                    s->palette[(addr - 0x800) / 2 + 1] = value >> 16;
                    s->palette[(addr - 0x800) / 2] = value & 0x0000ffff;
                } else {
                    break;
                }
            } else {
                if (addr % 4 == 0) {
                    s->palette[(addr - 0x800) / 2] = value >> 16;
                    s->palette[(addr - 0x800) / 2 + 1] = value & 0x0000ffff;
                } else {
                    return;
                }
            }
        }
        break;
    default:
        break;
    }
}

static const MemoryRegionOps csky_lcdc_ops = {
    .read = csky_lcdc_read,
    .write = csky_lcdc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void csky_lcdc_reset(csky_lcdc_state *s)
{
    s->out_pixel_select = 0;
    s->dma_watermark_level = 0;
    s->video_mem_burst_len = 10;

    s->pixel_bit_size = 0;
    s->tft = 0;
    s->color = 0;
    s->lcd_enable = 0;

    s->timing[0] = 0;
    s->timing[1] = 0;
    s->timing[2] = 0;
    s->base_addr = 0;

    s->line_fifo_run = 0;
    s->bus_error = 0;
    s->base_addr_update = 0;
    s->lcd_invalidate = 0;
    s->int_mask = 15;

    s->dither_duty_12 = 0x01010000;
    s->dither_duty_47 = 0x11110421;
    s->dither_duty_35 = 0x92491249;
    s->dither_duty_23 = 0x555592c9;
    s->dither_duty_57 = 0xd5d5d555;
    s->dither_duty_34 = 0xddddd5dd;
    s->dither_duty_45 = 0xdfdfdfdd;
    s->dither_duty_67 = 0xffffdfff;

    s->width = 0;
    s->height = 0;

}

static const GraphicHwOps csky_lcdc_gfx_ops = {
    .invalidate  = csky_invalidate_display,
    .gfx_update  = csky_update_display,
};

static void csky_lcdc_realize(DeviceState *dev, Error **errp)
{
    csky_lcdc_state *s = CSKY_LCDC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &csky_lcdc_ops, s,
                          TYPE_CSKY_LCDC, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    s->con = graphic_console_init(dev, 0, &csky_lcdc_gfx_ops, s);
}

static void csky_lcdc_device_reset(DeviceState *d)
{
    csky_lcdc_state *s = CSKY_LCDC(d);

    csky_lcdc_reset(s);
}

static void csky_lcdc_init(Object *dev)
{

}

static void csky_lcdc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
    dc->reset = csky_lcdc_device_reset;
    dc->vmsd = &vmstate_csky_lcdc;
    dc->realize = csky_lcdc_realize;
}

static const TypeInfo csky_lcdc_info = {
    .name          = TYPE_CSKY_LCDC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(csky_lcdc_state),
    .instance_init = csky_lcdc_init,
    .class_init    = csky_lcdc_class_init,
};

static void csky_lcdc_register_types(void)
{
    type_register_static(&csky_lcdc_info);
}

type_init(csky_lcdc_register_types)
