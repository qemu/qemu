/*
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_PIXMAN_H
#define QEMU_PIXMAN_H

#include <pixman.h>

#include "console.h"

/*
 * pixman image formats are defined to be native endian,
 * that means host byte order on qemu.  So we go define
 * fixed formats here for cases where it is needed, like
 * feeding libjpeg / libpng and writing screenshots.
 */

#ifdef HOST_WORDS_BIGENDIAN
# define PIXMAN_BE_r8g8b8     PIXMAN_r8g8b8
#else
# define PIXMAN_BE_r8g8b8     PIXMAN_b8g8r8
#endif

/* -------------------------------------------------------------------- */

int qemu_pixman_get_type(int rshift, int gshift, int bshift);
pixman_format_code_t qemu_pixman_get_format(PixelFormat *pf);

pixman_image_t *qemu_pixman_linebuf_create(pixman_format_code_t format,
                                           int width);
void qemu_pixman_linebuf_fill(pixman_image_t *linebuf, pixman_image_t *fb,
                              int width, int y);
pixman_image_t *qemu_pixman_mirror_create(pixman_format_code_t format,
                                          pixman_image_t *image);
void qemu_pixman_image_unref(pixman_image_t *image);

#endif /* QEMU_PIXMAN_H */
