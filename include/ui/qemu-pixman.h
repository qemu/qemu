/*
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_PIXMAN_H
#define QEMU_PIXMAN_H

/* pixman-0.16.0 headers have a redundant declaration */
#ifdef CONFIG_PRAGMA_DIAGNOSTIC_AVAILABLE
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wredundant-decls"
#endif
#include <pixman.h>
#ifdef CONFIG_PRAGMA_DIAGNOSTIC_AVAILABLE
#pragma GCC diagnostic pop
#endif

/*
 * pixman image formats are defined to be native endian,
 * that means host byte order on qemu.  So we go define
 * fixed formats here for cases where it is needed, like
 * feeding libjpeg / libpng and writing screenshots.
 */

#ifdef HOST_WORDS_BIGENDIAN
# define PIXMAN_BE_r8g8b8     PIXMAN_r8g8b8
# define PIXMAN_BE_x8r8g8b8   PIXMAN_x8r8g8b8
# define PIXMAN_BE_a8r8g8b8   PIXMAN_a8r8g8b8
# define PIXMAN_BE_b8g8r8x8   PIXMAN_b8g8r8x8
# define PIXMAN_BE_b8g8r8a8   PIXMAN_b8g8r8a8
# define PIXMAN_BE_r8g8b8x8   PIXMAN_r8g8b8x8
# define PIXMAN_BE_r8g8b8a8   PIXMAN_r8g8b8a8
# define PIXMAN_BE_x8b8g8r8   PIXMAN_x8b8g8r8
# define PIXMAN_BE_a8b8g8r8   PIXMAN_a8b8g8r8
# define PIXMAN_LE_x8r8g8b8   PIXMAN_b8g8r8x8
#else
# define PIXMAN_BE_r8g8b8     PIXMAN_b8g8r8
# define PIXMAN_BE_x8r8g8b8   PIXMAN_b8g8r8x8
# define PIXMAN_BE_a8r8g8b8   PIXMAN_b8g8r8a8
# define PIXMAN_BE_b8g8r8x8   PIXMAN_x8r8g8b8
# define PIXMAN_BE_b8g8r8a8   PIXMAN_a8r8g8b8
# define PIXMAN_BE_r8g8b8x8   PIXMAN_x8b8g8r8
# define PIXMAN_BE_r8g8b8a8   PIXMAN_a8b8g8r8
# define PIXMAN_BE_x8b8g8r8   PIXMAN_r8g8b8x8
# define PIXMAN_BE_a8b8g8r8   PIXMAN_r8g8b8a8
# define PIXMAN_LE_x8r8g8b8   PIXMAN_x8r8g8b8
#endif

/* -------------------------------------------------------------------- */

PixelFormat qemu_pixelformat_from_pixman(pixman_format_code_t format);
pixman_format_code_t qemu_default_pixman_format(int bpp, bool native_endian);
int qemu_pixman_get_type(int rshift, int gshift, int bshift);
pixman_format_code_t qemu_pixman_get_format(PixelFormat *pf);
bool qemu_pixman_check_format(DisplayChangeListener *dcl,
                              pixman_format_code_t format);

pixman_image_t *qemu_pixman_linebuf_create(pixman_format_code_t format,
                                           int width);
void qemu_pixman_linebuf_fill(pixman_image_t *linebuf, pixman_image_t *fb,
                              int width, int x, int y);
void qemu_pixman_linebuf_copy(pixman_image_t *fb, int width, int x, int y,
                              pixman_image_t *linebuf);
pixman_image_t *qemu_pixman_mirror_create(pixman_format_code_t format,
                                          pixman_image_t *image);
void qemu_pixman_image_unref(pixman_image_t *image);

pixman_color_t qemu_pixman_color(PixelFormat *pf, uint32_t color);
pixman_image_t *qemu_pixman_glyph_from_vgafont(int height, const uint8_t *font,
                                               unsigned int ch);
void qemu_pixman_glyph_render(pixman_image_t *glyph,
                              pixman_image_t *surface,
                              pixman_color_t *fgcol,
                              pixman_color_t *bgcol,
                              int x, int y, int cw, int ch);

#endif /* QEMU_PIXMAN_H */
