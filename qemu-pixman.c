/*
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu-pixman.h"

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
            type = PIXMAN_TYPE_BGRA;
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

void qemu_pixman_linebuf_fill(pixman_image_t *linebuf, pixman_image_t *fb,
                              int width, int y)
{
    pixman_image_composite(PIXMAN_OP_SRC, fb, NULL, linebuf,
                           0, y, 0, 0, 0, 0, width, 1);
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
