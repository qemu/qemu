/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 * Refactoring for Pi2 Copyright (c) 2015, Microsoft. Written by Andrew Baumann.
 * This code is licensed under the GNU GPLv2 and later.
 *
 * Heavily based on milkymist-vgafb.c, copyright terms below:
 *  QEMU model of the Milkymist VGA framebuffer.
 *
 *  Copyright (c) 2010-2012 Michael Walle <michael@walle.cc>
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
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/display/bcm2835_fb.h"
#include "hw/display/framebuffer.h"
#include "ui/pixel_ops.h"
#include "hw/misc/bcm2835_mbox_defs.h"
#include "qemu/log.h"

#define DEFAULT_VCRAM_SIZE 0x4000000
#define BCM2835_FB_OFFSET  0x00100000

static void fb_invalidate_display(void *opaque)
{
    BCM2835FBState *s = BCM2835_FB(opaque);

    s->invalidate = true;
}

static void draw_line_src16(void *opaque, uint8_t *dst, const uint8_t *src,
                            int width, int deststep)
{
    BCM2835FBState *s = opaque;
    uint16_t rgb565;
    uint32_t rgb888;
    uint8_t r, g, b;
    DisplaySurface *surface = qemu_console_surface(s->con);
    int bpp = surface_bits_per_pixel(surface);

    while (width--) {
        switch (s->bpp) {
        case 8:
            /* lookup palette starting at video ram base
             * TODO: cache translation, rather than doing this each time!
             */
            rgb888 = ldl_le_phys(&s->dma_as, s->vcram_base + (*src << 2));
            r = (rgb888 >> 0) & 0xff;
            g = (rgb888 >> 8) & 0xff;
            b = (rgb888 >> 16) & 0xff;
            src++;
            break;
        case 16:
            rgb565 = lduw_le_p(src);
            r = ((rgb565 >> 11) & 0x1f) << 3;
            g = ((rgb565 >>  5) & 0x3f) << 2;
            b = ((rgb565 >>  0) & 0x1f) << 3;
            src += 2;
            break;
        case 24:
            rgb888 = ldl_le_p(src);
            r = (rgb888 >> 0) & 0xff;
            g = (rgb888 >> 8) & 0xff;
            b = (rgb888 >> 16) & 0xff;
            src += 3;
            break;
        case 32:
            rgb888 = ldl_le_p(src);
            r = (rgb888 >> 0) & 0xff;
            g = (rgb888 >> 8) & 0xff;
            b = (rgb888 >> 16) & 0xff;
            src += 4;
            break;
        default:
            r = 0;
            g = 0;
            b = 0;
            break;
        }

        if (s->pixo == 0) {
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
            *(uint32_t *)dst = rgb_to_pixel32(r, g, b);
            dst += 4;
            break;
        default:
            return;
        }
    }
}

static void fb_update_display(void *opaque)
{
    BCM2835FBState *s = opaque;
    DisplaySurface *surface = qemu_console_surface(s->con);
    int first = 0;
    int last = 0;
    int src_width = 0;
    int dest_width = 0;

    if (s->lock || !s->xres) {
        return;
    }

    src_width = s->xres * (s->bpp >> 3);
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
        hw_error("bcm2835_fb: bad color depth\n");
        break;
    }

    if (s->invalidate) {
        framebuffer_update_memory_section(&s->fbsection, s->dma_mr, s->base,
                                          s->yres, src_width);
    }

    framebuffer_update_display(surface, &s->fbsection, s->xres, s->yres,
                               src_width, dest_width, 0, s->invalidate,
                               draw_line_src16, s, &first, &last);

    if (first >= 0) {
        dpy_gfx_update(s->con, 0, first, s->xres, last - first + 1);
    }

    s->invalidate = false;
}

static void bcm2835_fb_mbox_push(BCM2835FBState *s, uint32_t value)
{
    value &= ~0xf;

    s->lock = true;

    s->xres = ldl_le_phys(&s->dma_as, value);
    s->yres = ldl_le_phys(&s->dma_as, value + 4);
    s->xres_virtual = ldl_le_phys(&s->dma_as, value + 8);
    s->yres_virtual = ldl_le_phys(&s->dma_as, value + 12);
    s->bpp = ldl_le_phys(&s->dma_as, value + 20);
    s->xoffset = ldl_le_phys(&s->dma_as, value + 24);
    s->yoffset = ldl_le_phys(&s->dma_as, value + 28);

    s->base = s->vcram_base | (value & 0xc0000000);
    s->base += BCM2835_FB_OFFSET;

    /* TODO - Manage properly virtual resolution */

    s->pitch = s->xres * (s->bpp >> 3);
    s->size = s->yres * s->pitch;

    stl_le_phys(&s->dma_as, value + 16, s->pitch);
    stl_le_phys(&s->dma_as, value + 32, s->base);
    stl_le_phys(&s->dma_as, value + 36, s->size);

    s->invalidate = true;
    qemu_console_resize(s->con, s->xres, s->yres);
    s->lock = false;
}

void bcm2835_fb_reconfigure(BCM2835FBState *s, uint32_t *xres, uint32_t *yres,
                            uint32_t *xoffset, uint32_t *yoffset, uint32_t *bpp,
                            uint32_t *pixo, uint32_t *alpha)
{
    s->lock = true;

    /* TODO: input validation! */
    if (xres) {
        s->xres = *xres;
    }
    if (yres) {
        s->yres = *yres;
    }
    if (xoffset) {
        s->xoffset = *xoffset;
    }
    if (yoffset) {
        s->yoffset = *yoffset;
    }
    if (bpp) {
        s->bpp = *bpp;
    }
    if (pixo) {
        s->pixo = *pixo;
    }
    if (alpha) {
        s->alpha = *alpha;
    }

    /* TODO - Manage properly virtual resolution */

    s->pitch = s->xres * (s->bpp >> 3);
    s->size = s->yres * s->pitch;

    s->invalidate = true;
    qemu_console_resize(s->con, s->xres, s->yres);
    s->lock = false;
}

static uint64_t bcm2835_fb_read(void *opaque, hwaddr offset, unsigned size)
{
    BCM2835FBState *s = opaque;
    uint32_t res = 0;

    switch (offset) {
    case MBOX_AS_DATA:
        res = MBOX_CHAN_FB;
        s->pending = false;
        qemu_set_irq(s->mbox_irq, 0);
        break;

    case MBOX_AS_PENDING:
        res = s->pending;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset %"HWADDR_PRIx"\n",
                      __func__, offset);
        return 0;
    }

    return res;
}

static void bcm2835_fb_write(void *opaque, hwaddr offset, uint64_t value,
                             unsigned size)
{
    BCM2835FBState *s = opaque;

    switch (offset) {
    case MBOX_AS_DATA:
        /* bcm2835_mbox should check our pending status before pushing */
        assert(!s->pending);
        s->pending = true;
        bcm2835_fb_mbox_push(s, value);
        qemu_set_irq(s->mbox_irq, 1);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset %"HWADDR_PRIx"\n",
                      __func__, offset);
        return;
    }
}

static const MemoryRegionOps bcm2835_fb_ops = {
    .read = bcm2835_fb_read,
    .write = bcm2835_fb_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static const VMStateDescription vmstate_bcm2835_fb = {
    .name = TYPE_BCM2835_FB,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(lock, BCM2835FBState),
        VMSTATE_BOOL(invalidate, BCM2835FBState),
        VMSTATE_BOOL(pending, BCM2835FBState),
        VMSTATE_UINT32(xres, BCM2835FBState),
        VMSTATE_UINT32(yres, BCM2835FBState),
        VMSTATE_UINT32(xres_virtual, BCM2835FBState),
        VMSTATE_UINT32(yres_virtual, BCM2835FBState),
        VMSTATE_UINT32(xoffset, BCM2835FBState),
        VMSTATE_UINT32(yoffset, BCM2835FBState),
        VMSTATE_UINT32(bpp, BCM2835FBState),
        VMSTATE_UINT32(base, BCM2835FBState),
        VMSTATE_UINT32(pitch, BCM2835FBState),
        VMSTATE_UINT32(size, BCM2835FBState),
        VMSTATE_UINT32(pixo, BCM2835FBState),
        VMSTATE_UINT32(alpha, BCM2835FBState),
        VMSTATE_END_OF_LIST()
    }
};

static const GraphicHwOps vgafb_ops = {
    .invalidate  = fb_invalidate_display,
    .gfx_update  = fb_update_display,
};

static void bcm2835_fb_init(Object *obj)
{
    BCM2835FBState *s = BCM2835_FB(obj);

    memory_region_init_io(&s->iomem, obj, &bcm2835_fb_ops, s, TYPE_BCM2835_FB,
                          0x10);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(s), &s->mbox_irq);
}

static void bcm2835_fb_reset(DeviceState *dev)
{
    BCM2835FBState *s = BCM2835_FB(dev);

    s->pending = false;

    s->xres_virtual = s->xres;
    s->yres_virtual = s->yres;
    s->xoffset = 0;
    s->yoffset = 0;
    s->base = s->vcram_base + BCM2835_FB_OFFSET;
    s->pitch = s->xres * (s->bpp >> 3);
    s->size = s->yres * s->pitch;

    s->invalidate = true;
    s->lock = false;
}

static void bcm2835_fb_realize(DeviceState *dev, Error **errp)
{
    BCM2835FBState *s = BCM2835_FB(dev);
    Error *err = NULL;
    Object *obj;

    if (s->vcram_base == 0) {
        error_setg(errp, "%s: required vcram-base property not set", __func__);
        return;
    }

    obj = object_property_get_link(OBJECT(dev), "dma-mr", &err);
    if (obj == NULL) {
        error_setg(errp, "%s: required dma-mr link not found: %s",
                   __func__, error_get_pretty(err));
        return;
    }

    s->dma_mr = MEMORY_REGION(obj);
    address_space_init(&s->dma_as, s->dma_mr, NULL);

    bcm2835_fb_reset(dev);

    s->con = graphic_console_init(dev, 0, &vgafb_ops, s);
    qemu_console_resize(s->con, s->xres, s->yres);
}

static Property bcm2835_fb_props[] = {
    DEFINE_PROP_UINT32("vcram-base", BCM2835FBState, vcram_base, 0),/*required*/
    DEFINE_PROP_UINT32("vcram-size", BCM2835FBState, vcram_size,
                       DEFAULT_VCRAM_SIZE),
    DEFINE_PROP_UINT32("xres", BCM2835FBState, xres, 640),
    DEFINE_PROP_UINT32("yres", BCM2835FBState, yres, 480),
    DEFINE_PROP_UINT32("bpp", BCM2835FBState, bpp, 16),
    DEFINE_PROP_UINT32("pixo", BCM2835FBState, pixo, 1), /* 1=RGB, 0=BGR */
    DEFINE_PROP_UINT32("alpha", BCM2835FBState, alpha, 2), /* alpha ignored */
    DEFINE_PROP_END_OF_LIST()
};

static void bcm2835_fb_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->props = bcm2835_fb_props;
    dc->realize = bcm2835_fb_realize;
    dc->reset = bcm2835_fb_reset;
    dc->vmsd = &vmstate_bcm2835_fb;
}

static TypeInfo bcm2835_fb_info = {
    .name          = TYPE_BCM2835_FB,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835FBState),
    .class_init    = bcm2835_fb_class_init,
    .instance_init = bcm2835_fb_init,
};

static void bcm2835_fb_register_types(void)
{
    type_register_static(&bcm2835_fb_info);
}

type_init(bcm2835_fb_register_types)
