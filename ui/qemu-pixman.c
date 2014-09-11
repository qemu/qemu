/*
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu-common.h"
#include "ui/console.h"

PixelFormat qemu_pixelformat_from_pixman(pixman_format_code_t format)
{
    PixelFormat pf;
    uint8_t bpp;

    bpp = pf.bits_per_pixel = PIXMAN_FORMAT_BPP(format);
    pf.bytes_per_pixel = PIXMAN_FORMAT_BPP(format) / 8;
    pf.depth = PIXMAN_FORMAT_DEPTH(format);

    pf.abits = PIXMAN_FORMAT_A(format);
    pf.rbits = PIXMAN_FORMAT_R(format);
    pf.gbits = PIXMAN_FORMAT_G(format);
    pf.bbits = PIXMAN_FORMAT_B(format);

    switch (PIXMAN_FORMAT_TYPE(format)) {
    case PIXMAN_TYPE_ARGB:
        pf.ashift = pf.bbits + pf.gbits + pf.rbits;
        pf.rshift = pf.bbits + pf.gbits;
        pf.gshift = pf.bbits;
        pf.bshift = 0;
        break;
    case PIXMAN_TYPE_ABGR:
        pf.ashift = pf.rbits + pf.gbits + pf.bbits;
        pf.bshift = pf.rbits + pf.gbits;
        pf.gshift = pf.rbits;
        pf.rshift = 0;
        break;
    case PIXMAN_TYPE_BGRA:
	pf.bshift = bpp - pf.bbits;
        pf.gshift = bpp - (pf.bbits + pf.gbits);
        pf.rshift = bpp - (pf.bbits + pf.gbits + pf.rbits);
        pf.ashift = 0;
        break;
    case PIXMAN_TYPE_RGBA:
        pf.rshift = bpp - pf.rbits;
        pf.gshift = bpp - (pf.rbits + pf.gbits);
        pf.bshift = bpp - (pf.rbits + pf.gbits + pf.bbits);
        pf.ashift = 0;
        break;
    default:
        g_assert_not_reached();
        break;
    }

    pf.amax = (1 << pf.abits) - 1;
    pf.rmax = (1 << pf.rbits) - 1;
    pf.gmax = (1 << pf.gbits) - 1;
    pf.bmax = (1 << pf.bbits) - 1;
    pf.amask = pf.amax << pf.ashift;
    pf.rmask = pf.rmax << pf.rshift;
    pf.gmask = pf.gmax << pf.gshift;
    pf.bmask = pf.bmax << pf.bshift;

    return pf;
}

pixman_format_code_t qemu_default_pixman_format(int bpp, bool native_endian)
{
    if (native_endian) {
        switch (bpp) {
        case 15:
            return PIXMAN_x1r5g5b5;
        case 16:
            return PIXMAN_r5g6b5;
        case 24:
            return PIXMAN_r8g8b8;
        case 32:
            return PIXMAN_x8r8g8b8;
        }
    } else {
        switch (bpp) {
        case 24:
            return PIXMAN_b8g8r8;
        case 32:
            return PIXMAN_b8g8r8a8;
        break;
        }
    }
    g_assert_not_reached();
}

int qemu_pixman_get_type(int rshift, int gshift, int bshift)
{
    int type = PIXMAN_TYPE_OTHER;

    if (rshift > gshift && gshift > bshift) {
        if (bshift == 0) {
            type = PIXMAN_TYPE_ARGB;
        } else {
#if PIXMAN_VERSION >= PIXMAN_VERSION_ENCODE(0, 21, 8)
            type = PIXMAN_TYPE_RGBA;
#endif
        }
    } else if (rshift < gshift && gshift < bshift) {
        if (rshift == 0) {
            type = PIXMAN_TYPE_ABGR;
        } else {
#if PIXMAN_VERSION >= PIXMAN_VERSION_ENCODE(0, 16, 0)
            type = PIXMAN_TYPE_BGRA;
#endif
        }
    }
    return type;
}

pixman_format_code_t qemu_pixman_get_format(PixelFormat *pf)
{
    pixman_format_code_t format;
    int type;

    type = qemu_pixman_get_type(pf->rshift, pf->gshift, pf->bshift);
    format = PIXMAN_FORMAT(pf->bits_per_pixel, type,
                           pf->abits, pf->rbits, pf->gbits, pf->bbits);
    if (!pixman_format_supported_source(format)) {
        return 0;
    }
    return format;
}

pixman_image_t *qemu_pixman_linebuf_create(pixman_format_code_t format,
                                           int width)
{
    pixman_image_t *image = pixman_image_create_bits(format, width, 1, NULL, 0);
    assert(image != NULL);
    return image;
}

/* fill linebuf from framebuffer */
void qemu_pixman_linebuf_fill(pixman_image_t *linebuf, pixman_image_t *fb,
                              int width, int x, int y)
{
    pixman_image_composite(PIXMAN_OP_SRC, fb, NULL, linebuf,
                           x, y, 0, 0, 0, 0, width, 1);
}

/* copy linebuf to framebuffer */
void qemu_pixman_linebuf_copy(pixman_image_t *fb, int width, int x, int y,
                              pixman_image_t *linebuf)
{
    pixman_image_composite(PIXMAN_OP_SRC, linebuf, NULL, fb,
                           0, 0, 0, 0, x, y, width, 1);
}

pixman_image_t *qemu_pixman_mirror_create(pixman_format_code_t format,
                                          pixman_image_t *image)
{
    pixman_image_t *mirror;

    mirror = pixman_image_create_bits(format,
                                      pixman_image_get_width(image),
                                      pixman_image_get_height(image),
                                      NULL,
                                      pixman_image_get_stride(image));
    return mirror;
}

void qemu_pixman_image_unref(pixman_image_t *image)
{
    if (image == NULL) {
        return;
    }
    pixman_image_unref(image);
}

pixman_color_t qemu_pixman_color(PixelFormat *pf, uint32_t color)
{
    pixman_color_t c;

    c.red   = ((color & pf->rmask) >> pf->rshift) << (16 - pf->rbits);
    c.green = ((color & pf->gmask) >> pf->gshift) << (16 - pf->gbits);
    c.blue  = ((color & pf->bmask) >> pf->bshift) << (16 - pf->bbits);
    c.alpha = ((color & pf->amask) >> pf->ashift) << (16 - pf->abits);
    return c;
}

pixman_image_t *qemu_pixman_glyph_from_vgafont(int height, const uint8_t *font,
                                               unsigned int ch)
{
    pixman_image_t *glyph;
    uint8_t *data;
    bool bit;
    int x, y;

    glyph = pixman_image_create_bits(PIXMAN_a8, 8, height,
                                     NULL, 0);
    data = (uint8_t *)pixman_image_get_data(glyph);

    font += height * ch;
    for (y = 0; y < height; y++, font++) {
        for (x = 0; x < 8; x++, data++) {
            bit = (*font) & (1 << (7-x));
            *data = bit ? 0xff : 0x00;
        }
    }
    return glyph;
}

void qemu_pixman_glyph_render(pixman_image_t *glyph,
                              pixman_image_t *surface,
                              pixman_color_t *fgcol,
                              pixman_color_t *bgcol,
                              int x, int y, int cw, int ch)
{
    pixman_image_t *ifg = pixman_image_create_solid_fill(fgcol);
    pixman_image_t *ibg = pixman_image_create_solid_fill(bgcol);

    pixman_image_composite(PIXMAN_OP_SRC, ibg, NULL, surface,
                           0, 0, 0, 0,
                           cw * x, ch * y,
                           cw, ch);
    pixman_image_composite(PIXMAN_OP_OVER, ifg, glyph, surface,
                           0, 0, 0, 0,
                           cw * x, ch * y,
                           cw, ch);
    pixman_image_unref(ifg);
    pixman_image_unref(ibg);
}
