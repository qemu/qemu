/*
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "ui/console.h"
#include "qemu/memfd.h"
#include "standard-headers/drm/drm_fourcc.h"
#include "trace.h"

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
            return PIXMAN_b8g8r8x8;
        break;
        }
    }
    return 0;
}

/* Note: drm is little endian, pixman is native endian */
static const struct {
    uint32_t drm_format;
    pixman_format_code_t pixman_format;
} drm_format_pixman_map[] = {
    { DRM_FORMAT_RGB888,   PIXMAN_LE_r8g8b8   },
    { DRM_FORMAT_ARGB8888, PIXMAN_LE_a8r8g8b8 },
    { DRM_FORMAT_XRGB8888, PIXMAN_LE_x8r8g8b8 },
    { DRM_FORMAT_XBGR8888, PIXMAN_LE_x8b8g8r8 },
    { DRM_FORMAT_ABGR8888, PIXMAN_LE_a8b8g8r8 },
};

pixman_format_code_t qemu_drm_format_to_pixman(uint32_t drm_format)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(drm_format_pixman_map); i++) {
        if (drm_format == drm_format_pixman_map[i].drm_format) {
            return drm_format_pixman_map[i].pixman_format;
        }
    }
    return 0;
}

uint32_t qemu_pixman_to_drm_format(pixman_format_code_t pixman_format)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(drm_format_pixman_map); i++) {
        if (pixman_format == drm_format_pixman_map[i].pixman_format) {
            return drm_format_pixman_map[i].drm_format;
        }
    }
    return 0;
}

int qemu_pixman_get_type(int rshift, int gshift, int bshift, int endian)
{
    int type = PIXMAN_TYPE_OTHER;
    bool native_endian = (endian == G_BYTE_ORDER);

    if (rshift > gshift && gshift > bshift) {
        if (bshift == 0) {
            type = native_endian ? PIXMAN_TYPE_ARGB : PIXMAN_TYPE_BGRA;
        } else {
            type = native_endian ? PIXMAN_TYPE_RGBA : PIXMAN_TYPE_ABGR;
        }
    } else if (rshift < gshift && gshift < bshift) {
        if (rshift == 0) {
            type = native_endian ? PIXMAN_TYPE_ABGR : PIXMAN_TYPE_RGBA;
        } else {
            type = native_endian ? PIXMAN_TYPE_BGRA : PIXMAN_TYPE_ARGB;
        }
    }
    return type;
}

#ifdef CONFIG_PIXMAN
pixman_format_code_t qemu_pixman_get_format(PixelFormat *pf, int endian)
{
    pixman_format_code_t format;
    int type;

    type = qemu_pixman_get_type(pf->rshift, pf->gshift, pf->bshift, endian);
    format = PIXMAN_FORMAT(pf->bits_per_pixel, type,
                           pf->abits, pf->rbits, pf->gbits, pf->bbits);
    if (!pixman_format_supported_source(format)) {
        return 0;
    }
    return format;
}
#endif

/*
 * Return true for known-good pixman conversions.
 *
 * UIs using pixman for format conversion can hook this into
 * DisplayChangeListenerOps->dpy_gfx_check_format
 */
bool qemu_pixman_check_format(DisplayChangeListener *dcl,
                              pixman_format_code_t format)
{
    switch (format) {
    /* 32 bpp */
    case PIXMAN_x8r8g8b8:
    case PIXMAN_a8r8g8b8:
    case PIXMAN_b8g8r8x8:
    case PIXMAN_b8g8r8a8:
    /* 24 bpp */
    case PIXMAN_r8g8b8:
    case PIXMAN_b8g8r8:
    /* 16 bpp */
    case PIXMAN_x1r5g5b5:
    case PIXMAN_r5g6b5:
        return true;
    default:
        return false;
    }
}

#ifdef CONFIG_PIXMAN
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

pixman_image_t *qemu_pixman_mirror_create(pixman_format_code_t format,
                                          pixman_image_t *image)
{
    return pixman_image_create_bits(format,
                                    pixman_image_get_width(image),
                                    pixman_image_get_height(image),
                                    NULL,
                                    pixman_image_get_stride(image));
}
#endif

void qemu_pixman_image_unref(pixman_image_t *image)
{
    if (image == NULL) {
        return;
    }
    pixman_image_unref(image);
}

#ifdef CONFIG_PIXMAN
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
#endif /* CONFIG_PIXMAN */

static void *
qemu_pixman_shareable_alloc(const char *name, size_t size,
                            qemu_pixman_shareable *handle,
                            Error **errp)
{
#ifdef WIN32
    return qemu_win32_map_alloc(size, handle, errp);
#else
    return qemu_memfd_alloc(name, size, 0, handle, errp);
#endif
}

static void
qemu_pixman_shareable_free(qemu_pixman_shareable handle,
                           void *ptr, size_t size)
{
#ifdef WIN32
    Error *err = NULL;

    qemu_win32_map_free(ptr, handle, &err);
    if (err) {
        error_report_err(err);
    }
#else
    qemu_memfd_free(ptr, size, handle);
#endif
}

static void
qemu_pixman_shared_image_destroy(pixman_image_t *image, void *data)
{
    qemu_pixman_shareable handle = PTR_TO_SHAREABLE(data);
    void *ptr = pixman_image_get_data(image);
    size_t size = pixman_image_get_height(image) * pixman_image_get_stride(image);

    qemu_pixman_shareable_free(handle, ptr, size);
}

bool
qemu_pixman_image_new_shareable(pixman_image_t **image,
                                qemu_pixman_shareable *handle,
                                const char *name,
                                pixman_format_code_t format,
                                int width,
                                int height,
                                int rowstride_bytes,
                                Error **errp)
{
    ERRP_GUARD();
    size_t size = height * rowstride_bytes;
    void *bits = NULL;

    g_return_val_if_fail(image != NULL, false);
    g_return_val_if_fail(handle != NULL, false);

    bits = qemu_pixman_shareable_alloc(name, size, handle, errp);
    if (!bits) {
        return false;
    }

    *image = pixman_image_create_bits(format, width, height, bits, rowstride_bytes);
    if (!*image) {
        error_setg(errp, "Failed to allocate image");
        qemu_pixman_shareable_free(*handle, bits, size);
        return false;
    }

    pixman_image_set_destroy_function(*image,
                                      qemu_pixman_shared_image_destroy,
                                      SHAREABLE_TO_PTR(*handle));

    return true;
}
