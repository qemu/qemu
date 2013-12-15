/*
 * Samsung S3C24xx series LCD controller.
 *
 * Copyright (c) 2007 OpenMoko, Inc.
 * Author: Andrzej Zaborowski <andrew@openedhand.com>
 *
 * Copyright 2010, 2013 Stefan Weil
 *
 * This code is licenced under the GNU GPL v2.
 */

#include "hw/hw.h"
#include "ui/console.h"
#include "framebuffer.h"
#include "hw/sysbus.h"

#include "s3c24xx.h"

typedef struct {
    SysBusDevice busdev;
    MemoryRegion mmio;
    qemu_irq irq;
    //~ hwaddr base;
    drawfn *line_fn;
    QemuConsole *con;
    uint32_t caddr[5];
    uint32_t saddr[3];
    uint32_t r;
    uint32_t g;
    uint16_t b;
    uint32_t dithmode;
    uint32_t tpal;
    uint8_t intpnd;
    uint8_t srcpnd;
    uint8_t intmsk;
    uint8_t lpcsel;

    uint16_t raw_pal[0x100];

    int width;
    int height;
    int bpp;
    int enable;
    int msb;
    int frm565;
    hwaddr fb;
    uint32_t palette[0x100];
    int invalidate;
    int invalidatep;
    int src_width;
    int dest_width;
    drawfn fn;
} S3C24xxLCD_State;

static void s3c24xx_lcd_update(S3C24xxLCD_State *s)
{
    s->intpnd |= s->srcpnd & ~s->intmsk;
    qemu_set_irq(s->irq, !!s->intpnd);
}

static void s3c24xx_lcd_reset(S3C24xxLCD_State *s)
{
    s->enable = 0;
    s->invalidate = 1;
    s->invalidatep = 1;
    s->width = -1;
    s->height = -1;

    s->caddr[0] = 0x00000000;
    s->caddr[1] = 0x00000000;
    s->caddr[2] = 0x00000000;
    s->caddr[3] = 0x00000000;
    s->caddr[4] = 0x00000000;
    s->saddr[0] = 0x00000000;
    s->saddr[1] = 0x00000000;
    s->saddr[2] = 0x00000000;
    s->r = 0x00000000;
    s->g = 0x00000000;
    s->b = 0x0000;
    s->dithmode = 0x00000;
    s->tpal = 0x00000000;
    s->intpnd = 0;
    s->srcpnd = 0;
    s->intmsk = 3;
    s->lpcsel = 4;
    s3c24xx_lcd_update(s);
}

#define S3C24XX_LCDCON1	0x00	/* LCD Control register 1 */
#define S3C24XX_LCDCON2	0x04	/* LCD Control register 2 */
#define S3C24XX_LCDCON3	0x08	/* LCD Control register 3 */
#define S3C24XX_LCDCON4	0x0c	/* LCD Control register 4 */
#define S3C24XX_LCDCON5	0x10	/* LCD Control register 5 */
#define S3C24XX_LCDSADDR1 0x14	/* Framebuffer Start Address 1 register */
#define S3C24XX_LCDSADDR2 0x18	/* Framebuffer Start Address 2 register */
#define S3C24XX_LCDSADDR3 0x1c	/* Framebuffer Start Address 3 register */
#define S3C24XX_REDLUT 0x20	/* Red Lookup Table register */
#define S3C24XX_GREENLUT 0x24	/* Green Lookup Table register */
#define S3C24XX_BLUELUT	0x28	/* Blue Lookup Table register */
#define S3C24XX_DITHMODE 0x4c	/* Dithering Mode register */
#define S3C24XX_TPAL 0x50	/* Temporary Palette register */
#define S3C24XX_LCDINTPND 0x54	/* LCD Interrupt Pending register */
#define S3C24XX_LCDSRCPND 0x58	/* LCD Interrupt Source Pending register */
#define S3C24XX_LCDINTMSK 0x5c	/* LCD Interrupt Mask register */
#define S3C24XX_LPCSEL 0x60	/* LPC3600 Control register */

#define S3C24XX_PALETTE	0x400	/* Palette IO start offset */
#define S3C24XX_PALETTEEND 0x7fc	/* Palette IO end offset */

static uint64_t s3c24xx_lcd_read(void *opaque,
                                 hwaddr addr, unsigned size)
{
    S3C24xxLCD_State *s = opaque;

    switch (addr) {
    case S3C24XX_LCDCON1:
        return s->caddr[0];		/* XXX Return random LINECNT? */
    case S3C24XX_LCDCON2:
        return s->caddr[1];
    case S3C24XX_LCDCON3:
        return s->caddr[2];
    case S3C24XX_LCDCON4:
        return s->caddr[3];
    case S3C24XX_LCDCON5:
        return s->caddr[4];		/* XXX Return random STATUS? */
    case S3C24XX_LCDSADDR1:
        return s->saddr[0];
    case S3C24XX_LCDSADDR2:
        return s->saddr[1];
    case S3C24XX_LCDSADDR3:
        return s->saddr[2];
    case S3C24XX_REDLUT:
        return s->r;
    case S3C24XX_GREENLUT:
        return s->g;
    case S3C24XX_BLUELUT:
        return s->b;
    case S3C24XX_DITHMODE:
        return s->dithmode;
    case S3C24XX_TPAL:
        return s->tpal;
    case S3C24XX_LCDINTPND:
        return s->intpnd;
    case S3C24XX_LCDSRCPND:
        return s->srcpnd;
    case S3C24XX_LCDINTMSK:
        return s->intmsk;
    case S3C24XX_LPCSEL:
        return s->lpcsel;
    case S3C24XX_PALETTE ... S3C24XX_PALETTEEND:
        /* XXX assuming 16bit access */
        return s->raw_pal[(addr - S3C24XX_PALETTE) >> 2];
    default:
        printf("%s: Bad register 0x" TARGET_FMT_plx "\n", __FUNCTION__, addr);
        break;
    }
    return 0;
}

static void s3c24xx_lcd_write(void *opaque, hwaddr addr,
                              uint64_t value, unsigned size)
{
    S3C24xxLCD_State *s = opaque;

    switch (addr) {
    case S3C24XX_LCDCON1:
        s->caddr[0] = value & 0x0003ffff;
        s->enable = value & 1;
        s->bpp = (value >> 1) & 0xf;
        s->invalidate = 1;
        s->invalidatep = 1;
        break;
    case S3C24XX_LCDCON2:
        s->caddr[1] = value;
        s->invalidate = 1;
        break;
    case S3C24XX_LCDCON3:
        s->caddr[2] = value;
        s->invalidate = 1;
        break;
    case S3C24XX_LCDCON4:
        s->caddr[3] = value & 0xffff;
        break;
    case S3C24XX_LCDCON5:
        s->caddr[4] = value & 0x1fff;
        s->frm565 = (value >> 11) & 1;
        s->msb = (value >> 12) & 1;
        s->invalidatep = 1;
        s->invalidate = 1;
        break;
    case S3C24XX_LCDSADDR1:
        s->saddr[0] = value;
        s->fb = ((s->saddr[0] << 1) & 0x7ffffffe);
        s->invalidate = 1;
        break;
    case S3C24XX_LCDSADDR2:
        s->saddr[1] = value;
        s->invalidate = 1;
        break;
    case S3C24XX_LCDSADDR3:
        s->saddr[2] = value;
        s->invalidate = 1;
        break;
    case S3C24XX_REDLUT:
        s->r = value;
        s->invalidatep = 1;
        s->invalidate = 1;
        break;
    case S3C24XX_GREENLUT:
        s->g = value;
        s->invalidatep = 1;
        s->invalidate = 1;
        break;
    case S3C24XX_BLUELUT:
        s->b = value;
        s->invalidatep = 1;
        s->invalidate = 1;
        break;
    case S3C24XX_DITHMODE:
        s->dithmode = value;
        break;
    case S3C24XX_TPAL:
        s->tpal = value;
        s->invalidatep = 1;
        s->invalidate = 1;
        break;
    case S3C24XX_LCDINTPND:
        s->intpnd = value & 3;
        break;
    case S3C24XX_LCDSRCPND:
        s->srcpnd = value & 3;
        break;
    case S3C24XX_LCDINTMSK:
        s->intmsk = value & 7;
        s3c24xx_lcd_update(s);
        break;
    case S3C24XX_LPCSEL:
        s->lpcsel = (value & 3) | 4;
        if (value & 1)
            printf("%s: attempt to enable LPC3600\n", __FUNCTION__);
        break;
    case S3C24XX_PALETTE ... S3C24XX_PALETTEEND:
        /* XXX assuming 16bit access */
        s->raw_pal[(addr - S3C24XX_PALETTE) >> 2] = value;
        break;
    default:
        printf("%s: Bad register 0x" TARGET_FMT_plx "\n", __FUNCTION__, addr);
    }
}

static const MemoryRegionOps s3c24xx_lcd_ops = {
    .read = s3c24xx_lcd_read,
    .write = s3c24xx_lcd_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4
    }
};

static inline void s3c24xx_lcd_resize(S3C24xxLCD_State *s)
{
    int new_width, new_height;
    new_height = ((s->caddr[1] >> 14) & 0x3ff) + 1;
    new_width = ((s->caddr[2] >> 8) & 0x7ff) + 1;
    if (s->width != new_width || s->height != new_height) {
        s->width = new_width;
        s->height = new_height;
        qemu_console_resize(s->con, s->width, s->height);
        s->invalidate = 1;
    }
}

static inline
uint32_t s3c24xx_rgb_to_pixel8(unsigned int r, unsigned int g, unsigned b)
{
    return ((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6);
}

static inline
uint32_t s3c24xx_rgb_to_pixel15(unsigned int r, unsigned int g, unsigned b)
{
    return ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
}

static inline
uint32_t s3c24xx_rgb_to_pixel16(unsigned int r, unsigned int g, unsigned b)
{
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

static inline
uint32_t s3c24xx_rgb_to_pixel24(unsigned int r, unsigned int g, unsigned b)
{
    return (r << 16) | (g << 8) | b;
}

static inline
uint32_t s3c24xx_rgb_to_pixel32(unsigned int r, unsigned int g, unsigned b)
{
    return (r << 16) | (g << 8) | b;
}

static inline uint32_t s3c24xx_rgb(DisplaySurface *surface,
                                   unsigned int r, unsigned int g, unsigned b)
{
    switch (surface_bits_per_pixel(surface)) {
    case 8:
        return s3c24xx_rgb_to_pixel32(r << 2, g << 2, b << 2);
    case 15:
        return s3c24xx_rgb_to_pixel15(r << 2, g << 2, b << 2);
    case 16:
        return s3c24xx_rgb_to_pixel16(r << 2, g << 2, b << 2);
    case 24:
        return s3c24xx_rgb_to_pixel24(r << 2, g << 2, b << 2);
    case 32:
        return s3c24xx_rgb_to_pixel32(r << 2, g << 2, b << 2);
    default:
        fprintf(stderr, "%s: Bad color depth\n", __FUNCTION__);
        exit(1);
    }
}

static void s3c24xx_lcd_palette_load(S3C24xxLCD_State *s)
{
    int i, n;
    switch (s->bpp) {
    case 0:
    case 8:
        n = 2;
        s->src_width = s->width >> 3;
        s->fn = s->line_fn[0];
        break;
    case 1:
    case 9:
        n = 4;
        s->src_width = s->width >> 2;
        s->fn = s->line_fn[1];
        break;
    case 2:
    case 10:
        n = 16;
        s->src_width = s->width >> 1;
        s->fn = s->line_fn[2];
        break;
    case 3:
    case 11:
        n = 256;
        s->src_width = s->width >> 0;
        s->fn = s->line_fn[3];
        break;
    case 6:
        s->src_width = (s->width * 3) >> 1;
        s->fn = s->line_fn[4];
        return;
    case 12:
        s->src_width = s->width << 1;
        if (s->frm565)
            s->fn = s->line_fn[5];
        else
            s->fn = s->line_fn[6];
        return;
    case 13:
        s->src_width = s->width << 2;
        s->fn = s->line_fn[7];
        return;
    default:
        return;
    }
    if (s->bpp & 8) {
        for (i = 0; i < n; i ++)
            if (s->frm565)
                s->palette[i] = s3c24xx_rgb(qemu_console_surface(s->con),
                                            (s->raw_pal[i] >> 10) & 0x3e,
                                            (s->raw_pal[i] >> 5) & 0x3f,
                                            (s->raw_pal[i] << 1) & 0x3e);
            else
                s->palette[i] = s3c24xx_rgb(qemu_console_surface(s->con),
                                            ((s->raw_pal[i] >> 10) & 0x3e) | (s->raw_pal[i] & 1),
                                            ((s->raw_pal[i] >> 6) & 0x3e) | (s->raw_pal[i] & 1),
                                            s->raw_pal[i] & 0x3f);
    } else {
        for (i = 0; i < n; i ++)
            if (n < 256)
                s->palette[i] = s3c24xx_rgb(qemu_console_surface(s->con),
                                            ((s->r >> (i * 4)) & 0xf) << 2,
                                            ((s->g >> (i * 4)) & 0xf) << 2,
                                            ((s->b >> (i * 4)) & 0xf) << 2);
            else
                s->palette[i] = s3c24xx_rgb(qemu_console_surface(s->con),
                                            ((s->r >> (((i >> 5) & 7) * 4)) & 0xf) << 2,
                                            ((s->g >> (((i >> 2) & 7) * 4)) & 0xf) << 2,
                                            ((s->b >> ((i & 3) * 4)) & 0xf) << 2);
    }
}

static void s3c24xx_update_display(void *opaque)
{
    S3C24xxLCD_State *s = opaque;
    int src_width, dest_width, miny = 0, maxy = 0;

    if (!s->enable || !s->dest_width)
        return;

    s3c24xx_lcd_resize(s);

    if (s->invalidatep) {
        s3c24xx_lcd_palette_load(s);
        s->invalidatep = 0;
    }

    src_width = s->src_width;
    dest_width = s->width * s->dest_width;

    framebuffer_update_display(qemu_console_surface(s->con),
                               &s->mmio, s->width, s->height,
                               src_width, dest_width, s->dest_width,
                               0, s->invalidate,
                               s->fn, s->palette, &miny, &maxy);


    s->srcpnd |= (1 << 1);			/* INT_FrSyn */
    s3c24xx_lcd_update(s);
    dpy_gfx_update(s->con, 0, miny, s->width, maxy);
}

static void s3c24xx_invalidate_display(void *opaque)
{
    S3C24xxLCD_State *s = opaque;
    s->invalidate = 1;
}

static void s3c24xx_screen_dump(void *opaque, const char *filename,
                                bool cswitch, Error **errp)
{
    /* TODO */
}

#define BITS 8
#include "s3c24xx_template.h"
#define BITS 15
#include "s3c24xx_template.h"
#define BITS 16
#include "s3c24xx_template.h"
#define BITS 24
#include "s3c24xx_template.h"
#define BITS 32
#include "s3c24xx_template.h"

#define S3C24XX_LCD_SIZE 0x1000000

static int s3c24xx_lcd_init(SysBusDevice *sbd)
{
    DeviceState *dev = DEVICE(sbd);
    S3C24xxLCD_State *s = S3C24XX_LCD(dev);

    //~ s->brightness = 7;

    memory_region_init_io(&s->mmio, &s3c24xx_lcd_ops, s,
                          "s3c24xx-lcd", S3C24XX_LCD_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);

    sysbus_init_irq(dev, &s->irq);

    s3c24xx_lcd_reset(s);

    s->con = graphic_console_init(DEVICE(dev), &s3c24xx_gfx_ops, s);
//~ s3c24xx_update_display,
//~ s3c24xx_invalidate_display,
//~ s3c24xx_screen_dump, NULL, s);

    //~ qdev_init_gpio_in(&dev->qdev, s3c24xx_lcd_gpio_brigthness_in, 3);

    switch (surface_bits_per_pixel(qemu_console_surface(s->con))) {
    case 0:
        s->dest_width = 0;
        break;

    case 8:
        s->line_fn = s3c24xx_draw_fn_8;
        s->dest_width = 1;
        break;

    case 15:
        s->line_fn = s3c24xx_draw_fn_15;
        s->dest_width = 2;
        break;

    case 16:
        s->line_fn = s3c24xx_draw_fn_16;
        s->dest_width = 2;
        break;

    case 24:
        s->line_fn = s3c24xx_draw_fn_24;
        s->dest_width = 3;
        break;

    case 32:
        s->line_fn = s3c24xx_draw_fn_32;
        s->dest_width = 4;
        break;

    default:
        fprintf(stderr, "%s: Bad color depth\n", __FUNCTION__);
        exit(1);
    }

    return 0;
}

static const VMStateDescription s3c24xx_lcd_vmsd = {
    .name = "s3c24xx_lcd",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        //~ VMSTATE_UINT32(brightness, S3C24xxLCD_State),
        //~ VMSTATE_UINT32(mode, S3C24xxLCD_State),
        //~ VMSTATE_UINT32(irqctrl, S3C24xxLCD_State),
        //~ VMSTATE_UINT32(page, S3C24xxLCD_State),
        //~ VMSTATE_UINT32(page_off, S3C24xxLCD_State),
        //~ VMSTATE_BUFFER(video_ram, S3C24xxLCD_State),
#if 0
    for (i = 0; i < 5; i ++)
        qemu_put_be32s(f, &s->con[i]);
    for (i = 0; i < 3; i ++)
        qemu_put_be32s(f, &s->saddr[i]);
    qemu_put_be32s(f, &s->r);
    qemu_put_be32s(f, &s->g);
    qemu_put_be16s(f, &s->b);
    qemu_put_be32s(f, &s->dithmode);
    qemu_put_be32s(f, &s->tpal);
    qemu_put_8s(f, &s->intpnd);
    qemu_put_8s(f, &s->srcpnd);
    qemu_put_8s(f, &s->intmsk);
    qemu_put_8s(f, &s->lpcsel);
    for (i = 0; i < 0x100; i ++)
        qemu_put_be16s(f, &s->raw_pal[i]);
#elif 0
    for (i = 0; i < 5; i ++)
        qemu_get_be32s(f, &s->con[i]);
    for (i = 0; i < 3; i ++)
        qemu_get_be32s(f, &s->saddr[i]);
    qemu_get_be32s(f, &s->r);
    qemu_get_be32s(f, &s->g);
    qemu_get_be16s(f, &s->b);
    qemu_get_be32s(f, &s->dithmode);
    qemu_get_be32s(f, &s->tpal);
    qemu_get_8s(f, &s->intpnd);
    qemu_get_8s(f, &s->srcpnd);
    qemu_get_8s(f, &s->intmsk);
    qemu_get_8s(f, &s->lpcsel);

    s->invalidate = 1;
    s->invalidatep = 1;
    s->width = -1;
    s->height = -1;
    s->bpp = (s->con[0] >> 1) & 0xf;
    s->enable = s->con[0] & 1;
    s->msb = (s->con[4] >> 12) & 1;
    s->frm565 = (s->con[4] >> 11) & 1;
    s->fb = ((s->saddr[0] << 1) & 0x7ffffffe);

    for (i = 0; i < 0x100; i ++)
        qemu_get_be16s(f, &s->raw_pal[i]);
#endif
        VMSTATE_END_OF_LIST()
    }
};

static void s3c24xx_lcd_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);
    dc->vmsd = &s3c24xx_lcd_vmsd;
    k->init = s3c24xx_lcd_init;
}

static const TypeInfo s3c24xx_lcd_info = {
    .name = "s3c24xx_lcd",
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(S3C24xxLCD_State),
    .class_init = s3c24xx_lcd_class_init
};

static void s3c24xx_lcd_register_types(void)
{
    type_register_static(&s3c24xx_lcd_info);
}

type_init(s3c24xx_lcd_register_types)
