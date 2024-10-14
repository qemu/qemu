/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * QEMU UI Console
 */
#ifndef SURFACE_H
#define SURFACE_H

#include "ui/qemu-pixman.h"

#ifdef CONFIG_OPENGL
# include <epoxy/gl.h>
# include "ui/shader.h"
#endif

#define QEMU_ALLOCATED_FLAG     0x01
#define QEMU_PLACEHOLDER_FLAG   0x02

typedef struct DisplaySurface {
    pixman_image_t *image;
    uint8_t flags;
#ifdef CONFIG_OPENGL
    GLenum glformat;
    GLenum gltype;
    GLuint texture;
#endif
    qemu_pixman_shareable share_handle;
    uint32_t share_handle_offset;
} DisplaySurface;

PixelFormat qemu_default_pixelformat(int bpp);

DisplaySurface *qemu_create_displaysurface_from(int width, int height,
                                                pixman_format_code_t format,
                                                int linesize, uint8_t *data);
DisplaySurface *qemu_create_displaysurface_pixman(pixman_image_t *image);
DisplaySurface *qemu_create_placeholder_surface(int w, int h,
                                                const char *msg);

void qemu_displaysurface_set_share_handle(DisplaySurface *surface,
                                          qemu_pixman_shareable handle,
                                          uint32_t offset);

DisplaySurface *qemu_create_displaysurface(int width, int height);
void qemu_free_displaysurface(DisplaySurface *surface);

static inline int surface_is_allocated(DisplaySurface *surface)
{
    return surface->flags & QEMU_ALLOCATED_FLAG;
}

static inline int surface_is_placeholder(DisplaySurface *surface)
{
    return surface->flags & QEMU_PLACEHOLDER_FLAG;
}

static inline int surface_stride(DisplaySurface *s)
{
    return pixman_image_get_stride(s->image);
}

static inline void *surface_data(DisplaySurface *s)
{
    return pixman_image_get_data(s->image);
}

static inline int surface_width(DisplaySurface *s)
{
    return pixman_image_get_width(s->image);
}

static inline int surface_height(DisplaySurface *s)
{
    return pixman_image_get_height(s->image);
}

static inline pixman_format_code_t surface_format(DisplaySurface *s)
{
    return pixman_image_get_format(s->image);
}

static inline int surface_bits_per_pixel(DisplaySurface *s)
{
    int bits = PIXMAN_FORMAT_BPP(surface_format(s));
    return bits;
}

static inline int surface_bytes_per_pixel(DisplaySurface *s)
{
    int bits = PIXMAN_FORMAT_BPP(surface_format(s));
    return DIV_ROUND_UP(bits, 8);
}

#endif
