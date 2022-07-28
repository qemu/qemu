/*
 * Allwinner F1 Display Engine Frontend Unit emulation
 *
 * Copyright (C) 2022 froloff
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "ui/pixel_ops.h"
#include "hw/sysbus.h"
#include "hw/display/allwinner-f1-display.h"
#include "framebuffer.h"

//Display engine back end register low addresses
enum {
    DEBE_MODE_CTRL           = 0x00000800,
    DEBE_COLOR_CTRL          = 0x00000804,
    DEBE_LAY_SIZE            = 0x00000808,
    DEBE_LAY0_SIZE           = 0x00000810,
    DEBE_LAY1_SIZE           = 0x00000814,
    DEBE_LAY2_SIZE           = 0x00000818,
    DEBE_LAY3_SIZE           = 0x0000081C,
    DEBE_LAY0_CODNT          = 0x00000820,
    DEBE_LAY1_CODNT          = 0x00000824,
    DEBE_LAY2_CODNT          = 0x00000828,
    DEBE_LAY3_CODNT          = 0x0000082C,
    DEBE_LAY0_LINEWIDTH      = 0x00000840,
    DEBE_LAY1_LINEWIDTH      = 0x00000844,
    DEBE_LAY2_LINEWIDTH      = 0x00000848,
    DEBE_LAY3_LINEWIDTH      = 0x0000084C,
    DEBE_LAY0_FB_ADDR_LO     = 0x00000850,
    DEBE_LAY1_FB_ADDR_LO     = 0x00000854,
    DEBE_LAY2_FB_ADDR_LO     = 0x00000858,
    DEBE_LAY3_FB_ADDR_LO     = 0x0000085C,
    DEBE_LAY0_FB_ADDR_HI     = 0x00000860,
    DEBE_LAY1_FB_ADDR_HI     = 0x00000864,
    DEBE_LAY2_FB_ADDR_HI     = 0x00000868,
    DEBE_LAY3_FB_ADDR_HI     = 0x0000086C,
    DEBE_REGBUFF_CTRL        = 0x00000870,
    DEBE_CK_MAX              = 0x00000880,
    DEBE_CK_MIN              = 0x00000884,
    DEBE_CK_CFG              = 0x00000888,
    DEBE_LAY0_ATT_CTRL0      = 0x00000890,
    DEBE_LAY1_ATT_CTRL0      = 0x00000894,
    DEBE_LAY2_ATT_CTRL0      = 0x00000898,
    DEBE_LAY3_ATT_CTRL0      = 0x0000089C,
    DEBE_LAY0_ATT_CTRL1      = 0x000008A0,
    DEBE_LAY1_ATT_CTRL1      = 0x000008A4,
    DEBE_LAY2_ATT_CTRL1      = 0x000008A8,
    DEBE_LAY3_ATT_CTRL1      = 0x000008AC,
    DEBE_HWC_CTRL            = 0x000008D8,
    DEBE_HWCFB_CTRL          = 0x000008E0,
    DEBE_WB_CTRL             = 0x000008F0,
    DEBE_WB_ADDR             = 0x000008F4,
    DEBE_WB_LW               = 0x000008F8,
    DEBE_IYUV_CH_CTRL        = 0x00000920,
    DEBE_CH0_YUV_FB_ADDR     = 0x00000930,
    DEBE_CH1_YUV_FB_ADDR     = 0x00000934,
    DEBE_CH2_YUV_FB_ADDR     = 0x00000938,
    DEBE_CH0_YUV_BLW         = 0x00000940,
    DEBE_CH1_YUV_BLW         = 0x00000944,
    DEBE_CH2_YUV_BLW         = 0x00000948,
    DEBE_COEF00              = 0x00000950,
    DEBE_COEF01              = 0x00000954,
    DEBE_COEF02              = 0x00000958,
    DEBE_COEF03              = 0x0000095C,
    DEBE_COEF10              = 0x00000960,
    DEBE_COEF11              = 0x00000964,
    DEBE_COEF12              = 0x00000968,
    DEBE_COEF13              = 0x0000096C,
    DEBE_COEF20              = 0x00000970,
    DEBE_COEF21              = 0x00000974,
    DEBE_COEF22              = 0x00000978,
    DEBE_COEF23              = 0x0000097C,
};

#define DEBE_MODE_CTRL_EN        0x00000001  // Enable the Display Engine Back End
#define DEBE_MODE_CTRL_START     0x00000002  // Start the Display Engine Output
#define DEBE_MODE_CTRL_LAYER0_EN 0x00000100  // Enable Layer0
#define DEBE_MODE_CTRL_LAYER1_EN 0x00000200  // Enable Layer1
#define DEBE_MODE_CTRL_LAYER2_EN 0x00000400  // Enable Layer2
#define DEBE_MODE_CTRL_LAYER3_EN 0x00000800  // Enable Layer3

#define DEBE_MODE_CTRL_STARTED   (DEBE_MODE_CTRL_EN | DEBE_MODE_CTRL_START)

#define REG_INDEX(offset)    ((offset) / sizeof(uint32_t))

#define AW_DEBE_IOSTART      (DEBE_MODE_CTRL)
#define AW_DEBE_IOEND        (DEBE_COEF23 + 4)
/** Size of register I/O address space used by DEBE device */
#define AW_DEBE_IOSIZE       AW_DEBE_IOEND 

static inline void allwinner_f1_invalidate_display(void *opaque);

static uint64_t allwinner_f1_debe_read(void *opaque, hwaddr offset,
                                       unsigned size)
{
    const AwF1DEBEState *s = AW_F1_DEBE(opaque);
    const uint32_t idx = REG_INDEX(offset - AW_DEBE_IOSTART);

    switch (offset) {
    case 0x000 ... AW_DEBE_IOSTART - 4:
    case AW_DEBE_IOEND ... AW_DEBE_IOSIZE:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return 0;
    }

    return s->regs[idx];
}

static bool allwinner_f1_debe_enabled(AwF1DEBEState *s)
{
    return (s->ctl & DEBE_MODE_CTRL_EN) != 0;    
}

static bool allwinner_f1_debe_started(AwF1DEBEState *s)
{
    return (s->ctl & DEBE_MODE_CTRL_STARTED) == DEBE_MODE_CTRL_STARTED;    
}

static void allwinner_f1_resize(AwF1DEBEState *s, int width, int height)
{
    if (width != s->xres || height != s->yres) {
        s->xres = width;
        s->yres = height;
        allwinner_f1_invalidate_display(s);
    }
}

static void allwinner_f1_debe_write(void *opaque, hwaddr offset,
                                    uint64_t val, unsigned size)
{
    AwF1DEBEState *s = AW_F1_DEBE(opaque);
    const uint32_t idx = REG_INDEX(offset - AW_DEBE_IOSTART);
    bool flag;
    
    if (allwinner_f1_debe_enabled(s)) {

        switch (offset) {
        case DEBE_MODE_CTRL:
            flag = (!(s->ctl & DEBE_MODE_CTRL_START)) &&
                   (val & DEBE_MODE_CTRL_START);
            s->ctl = (uint32_t) val;
            if (flag) allwinner_f1_invalidate_display(s);
            break;
        case DEBE_LAY_SIZE:
            allwinner_f1_resize(s, (val & 0x07FF) + 1, ((val >> 16) & 0x07FF) + 1);
            break;
        case DEBE_LAY0_FB_ADDR_LO:
            //The dram array is 4 bytes per word and the information set in this register is a bit address so divide by 32 to get the actual index
            s->fb0_base = (s->fb0_base & 0xE0000000) | (val >> 3);
            break;
        case DEBE_LAY0_FB_ADDR_HI:
            //The dram array is 4 bytes per word and the information set in this register is a bit address so divide by 32 to get the actual index
            s->fb0_base = (s->fb0_base & 0x1FFFFFFF) | (val << 29);
            break;
        case DEBE_LAY0_ATT_CTRL1:
            s->pix0_fmt = (val >> 8) & 0xF;
            s->pix0_opts = (s->pix0_opts & 0x7) | (val & 0x7);
            break;
        case 0x000 ... AW_DEBE_IOSTART - 4:
        case AW_DEBE_IOEND ... AW_DEBE_IOSIZE:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                          __func__, (uint32_t)offset);
            return;
    
        default:
            qemu_log_mask(LOG_UNIMP, "%s: unimplemented write offset 0x%04x\n",
                          __func__, (uint32_t)offset);
            break;
        }
        s->regs[idx] = (uint32_t) val;
    } else {
        // Allow only DE Control Mode register access
        if (offset == DEBE_MODE_CTRL) {
            // Enable Display Engine Back End?
            if (val & DEBE_MODE_CTRL_EN) {
                s->ctl = (uint32_t) val;
            } else {
                s->ctl = 0;
            }     
            s->regs[idx] = (uint32_t) val;
        }
    }
}

static inline unsigned int argb_to_pixel32(unsigned int a, unsigned int r,
                                           unsigned int g, unsigned int b)
{
    return (a << 24) | (r << 16) | (g << 8) | b;
}


static void draw_line_src(void *opaque, uint8_t *dst, const uint8_t *src,
                          int width, int deststep)
{
    AwF1DEBEState *s = opaque;
    uint16_t rgb565;
    uint32_t rgb888;
    uint8_t a = 0;
    uint8_t r, g, b;
    DisplaySurface *surface = qemu_console_surface(s->con);
    int bpp = surface_bits_per_pixel(surface);


    while (width--) {
        switch (s->pix0_fmt) {
        case 5: // RGB565
            rgb565 = lduw_le_p(src);
            r = ((rgb565 >> 11) & 0x1f) << 3;
            g = ((rgb565 >>  5) & 0x3f) << 2;
            b = ((rgb565 >>  0) & 0x1f) << 3;
            src += 2;
            break;
        case 9: // RGB0888
        case 10:// ARGB8888
        case 11:// RGB888
            rgb888 = ldl_le_p(src);
            r = (rgb888 >> 0) & 0xff;
            g = (rgb888 >> 8) & 0xff;
            b = (rgb888 >> 16) & 0xff;
            if (s->pix0_fmt == 10)
                a = (rgb888 >> 24) & 0xff;
            if (s->pix0_fmt == 11)
                src += 3;
            else
                src += 4;
            break;
        default:
            r = g = b = 0;
            qemu_log_mask(LOG_UNIMP, "%s: pixel format: %d\n",
                      __func__, s->pix0_fmt);
        }
        if (s->pix0_opts & 0x4) {
            /* swap to BGR pixel format */
            uint8_t tmp = r;
            r = b;
            b = tmp;
        }
        switch (bpp) {
        case 8:
            *dst++ = rgb_to_pixel8(r, g, b);
            break;
        case 15:
            *(uint16_t *)dst = rgb_to_pixel15(r, g, b);
            dst += 2;
            break;
        case 16:
            *(uint16_t *)dst = rgb_to_pixel16(r, g, b);
            dst += 2;
            break;
        case 24:
            rgb888 = rgb_to_pixel24(r, g, b);
            *dst++ = rgb888 & 0xff;
            *dst++ = (rgb888 >> 8) & 0xff;
            *dst++ = (rgb888 >> 16) & 0xff;
            break;
        case 32:
            *(uint32_t *)dst = argb_to_pixel32(a, r, g, b);
            dst += 4;
            break;
        default:
            return;
        }
    }  
}

static void allwinner_f1_update_display(void *opaque)
{
    AwF1DEBEState *s = opaque;
    SysBusDevice *sbd = SYS_BUS_DEVICE(s);

    DisplaySurface *surface = qemu_console_surface(s->con);
    uint32_t src_width  = 0;
    uint32_t dest_width = 0;
    int first = 0;
    int last = 0;

    if (s->xres == 0 || s->yres == 0) return;

    // qemu_flush_coalesced_mmio_buffer();

    if (s->xres != surface_width(surface) ||
        s->yres != surface_height(surface)) {
        s->invalidate = true;
        qemu_console_resize(s->con, s->xres, s->yres);
    }
    // Check that DEBE enabled and output started
    if (!allwinner_f1_debe_started(s)) return;
    // Check that some layer enabled
    if ((s->ctl & 0x00000f00) == 0) return;
    
    src_width = s->xres;
    switch (s->pix0_fmt) {
        case 5: // RGB565
            src_width *= 2;
            break;
        case 9: // RGB0888
        case 10:// ARGB8888
            src_width *= 4;
            break;
        case 11:// RGB888
            src_width *= 3;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "%s: pixel format: %d\n",
                      __func__, s->pix0_fmt);
    }
    
    dest_width = s->xres;

    switch (surface_bits_per_pixel(surface)) {
    case 0:
        return;
    case 8:
        break;
    case 15:
        dest_width *= 2;
        break;
    case 16:
        dest_width *= 2;
        break;
    case 24:
        dest_width *= 3;
        break;
    case 32:
        dest_width *= 4;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad color depth: %d\n",
                      __func__, surface_bits_per_pixel(surface));
        break;
    }

    first = 0;

    if (s->invalidate) {
        framebuffer_update_memory_section(&s->fbsection, sysbus_address_space(sbd),
                                          s->fb0_base, s->yres, src_width);
    }

    framebuffer_update_display(surface, &s->fbsection,
                               s->xres, s->yres,
                               src_width, dest_width, 0, s->invalidate,
                               draw_line_src, s, &first, &last);

    if (first >= 0) {
        dpy_gfx_update(s->con, 0, first, s->xres,
                       last - first + 1);
    }

    s->invalidate = false;
}

static inline void allwinner_f1_invalidate_display(void *opaque)
{
    AwF1DEBEState *s = (AwF1DEBEState *)opaque;
    
    s->invalidate = true;
    
    if (allwinner_f1_debe_started(s)) {
        qemu_console_resize(s->con, s->xres, s->yres);
    }
}

static void allwinner_f1_debe_reset(DeviceState *dev)
{
    AwF1DEBEState *s = AW_F1_DEBE(dev);
    
    s->ctl = 0;
    s->fb0_base = 0;
    s->xres = 0;
    s->yres = 0;  
    s->pix0_fmt = 0;
    s->pix0_opts = 0;
    s->invalidate = true;    
}

static const MemoryRegionOps allwinner_f1_debe_mem_ops = {
    .read  = allwinner_f1_debe_read,
    .write = allwinner_f1_debe_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

static const GraphicHwOps allwinner_f1_debe_gfx_ops = {
    .invalidate  = allwinner_f1_invalidate_display,
    .gfx_update  = allwinner_f1_update_display,
};

static void allwinner_f1_debe_realize(DeviceState *dev, Error **errp)
{
    AwF1DEBEState *s = AW_F1_DEBE(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &allwinner_f1_debe_mem_ops, s,
                          TYPE_AW_F1_DEBE, AW_DEBE_IOSIZE);
    sysbus_init_mmio(sbd, &s->iomem);    

//    allwinner_f1_debe_reset(dev);
    s->con = graphic_console_init(dev, 0, &allwinner_f1_debe_gfx_ops, s);    
}

static void allwinner_f1_debe_init(Object *obj)
{
    AwF1DEBEState *s = AW_F1_DEBE(obj);
}

static const VMStateDescription allwinner_f1_debe_vmstate = {
    .name = "allwinner-f1-debe",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(ctl,        AwF1DEBEState),
        VMSTATE_UINT32(fb0_base,   AwF1DEBEState),
        VMSTATE_UINT16(xres,       AwF1DEBEState),
        VMSTATE_UINT16(yres,       AwF1DEBEState),
        VMSTATE_UINT16(pix0_fmt,   AwF1DEBEState),
        VMSTATE_UINT16(pix0_opts,  AwF1DEBEState),
        VMSTATE_BOOL  (invalidate, AwF1DEBEState),
        VMSTATE_UINT32_ARRAY(regs, AwF1DEBEState, AW_DEBE_REGS_NUM),
        VMSTATE_END_OF_LIST()
    }
};

static void allwinner_f1_debe_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset   = allwinner_f1_debe_reset;
    dc->realize = allwinner_f1_debe_realize;    
    dc->vmsd    = &allwinner_f1_debe_vmstate;
}

static const TypeInfo allwinner_f1_debe_info = {
    .name          = TYPE_AW_F1_DEBE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = allwinner_f1_debe_init,
    .instance_size = sizeof(AwF1DEBEState),
    .class_init    = allwinner_f1_debe_class_init,
};

static void allwinner_f1_debe_register(void)
{
    type_register_static(&allwinner_f1_debe_info);
}

type_init(allwinner_f1_debe_register)
