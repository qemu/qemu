/*
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_PIXMAN_H
#define QEMU_PIXMAN_H

#ifdef CONFIG_PIXMAN
#include <pixman.h>
#else
#include "pixman-minimal.h"
#endif

#include "qapi/error.h"

/*
 * pixman image formats are defined to be native endian,
 * that means host byte order on qemu.  So we go define
 * fixed formats here for cases where it is needed, like
 * feeding libjpeg / libpng and writing screenshots.
 */

#if HOST_BIG_ENDIAN
# define PIXMAN_BE_r8g8b8     PIXMAN_r8g8b8
# define PIXMAN_BE_x8r8g8b8   PIXMAN_x8r8g8b8
# define PIXMAN_BE_a8r8g8b8   PIXMAN_a8r8g8b8
# define PIXMAN_BE_b8g8r8x8   PIXMAN_b8g8r8x8
# define PIXMAN_BE_b8g8r8a8   PIXMAN_b8g8r8a8
# define PIXMAN_BE_r8g8b8x8   PIXMAN_r8g8b8x8
# define PIXMAN_BE_r8g8b8a8   PIXMAN_r8g8b8a8
# define PIXMAN_BE_x8b8g8r8   PIXMAN_x8b8g8r8
# define PIXMAN_BE_a8b8g8r8   PIXMAN_a8b8g8r8
# define PIXMAN_LE_r8g8b8     PIXMAN_b8g8r8
# define PIXMAN_LE_a8r8g8b8   PIXMAN_b8g8r8a8
# define PIXMAN_LE_x8r8g8b8   PIXMAN_b8g8r8x8
# define PIXMAN_LE_a8b8g8r8   PIXMAN_r8g8b8a8
# define PIXMAN_LE_x8b8g8r8   PIXMAN_r8g8b8x8
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
# define PIXMAN_LE_r8g8b8     PIXMAN_r8g8b8
# define PIXMAN_LE_a8r8g8b8   PIXMAN_a8r8g8b8
# define PIXMAN_LE_x8r8g8b8   PIXMAN_x8r8g8b8
# define PIXMAN_LE_a8b8g8r8   PIXMAN_a8b8g8r8
# define PIXMAN_LE_x8b8g8r8   PIXMAN_x8b8g8r8
#endif

#define QEMU_PIXMAN_COLOR(r, g, b)                                               \
    { .red = r << 8, .green = g << 8, .blue = b << 8, .alpha = 0xffff }

#define QEMU_PIXMAN_COLOR_BLACK QEMU_PIXMAN_COLOR(0x00, 0x00, 0x00)
#define QEMU_PIXMAN_COLOR_GRAY QEMU_PIXMAN_COLOR(0xaa, 0xaa, 0xaa)

/* -------------------------------------------------------------------- */

typedef struct PixelFormat {
    uint8_t bits_per_pixel;
    uint8_t bytes_per_pixel;
    uint8_t depth; /* color depth in bits */
    uint32_t rmask, gmask, bmask, amask;
    uint8_t rshift, gshift, bshift, ashift;
    uint8_t rmax, gmax, bmax, amax;
    uint8_t rbits, gbits, bbits, abits;
} PixelFormat;

PixelFormat qemu_pixelformat_from_pixman(pixman_format_code_t format);
pixman_format_code_t qemu_default_pixman_format(int bpp, bool native_endian);
pixman_format_code_t qemu_drm_format_to_pixman(uint32_t drm_format);
uint32_t qemu_pixman_to_drm_format(pixman_format_code_t pixman);
int qemu_pixman_get_type(int rshift, int gshift, int bshift);
bool qemu_pixman_check_format(DisplayChangeListener *dcl,
                              pixman_format_code_t format);

#ifdef CONFIG_PIXMAN
pixman_format_code_t qemu_pixman_get_format(PixelFormat *pf);
pixman_image_t *qemu_pixman_linebuf_create(pixman_format_code_t format,
                                           int width);
void qemu_pixman_linebuf_fill(pixman_image_t *linebuf, pixman_image_t *fb,
                              int width, int x, int y);
pixman_image_t *qemu_pixman_mirror_create(pixman_format_code_t format,
                                          pixman_image_t *image);

pixman_image_t *qemu_pixman_glyph_from_vgafont(int height, const uint8_t *font,
                                               unsigned int ch);
void qemu_pixman_glyph_render(pixman_image_t *glyph,
                              pixman_image_t *surface,
                              pixman_color_t *fgcol,
                              pixman_color_t *bgcol,
                              int x, int y, int cw, int ch);
#endif

void qemu_pixman_image_unref(pixman_image_t *image);

#ifdef WIN32
typedef HANDLE qemu_pixman_shareable;
#define SHAREABLE_NONE (NULL)
#define SHAREABLE_TO_PTR(handle) (handle)
#define PTR_TO_SHAREABLE(ptr) (ptr)
#else
typedef int qemu_pixman_shareable;
#define SHAREABLE_NONE (-1)
#define SHAREABLE_TO_PTR(handle) GINT_TO_POINTER(handle)
#define PTR_TO_SHAREABLE(ptr) GPOINTER_TO_INT(ptr)
#endif

bool qemu_pixman_image_new_shareable(
    pixman_image_t **image,
    qemu_pixman_shareable *handle,
    const char *name,
    pixman_format_code_t format,
    int width,
    int height,
    int rowstride_bytes,
    Error **errp);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(pixman_image_t, qemu_pixman_image_unref)

#endif /* QEMU_PIXMAN_H */
