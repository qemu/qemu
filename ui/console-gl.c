/*
 * QEMU graphical console -- opengl helper bits
 *
 * Copyright (c) 2014 Red Hat
 *
 * Authors:
 *    Gerd Hoffmann <kraxel@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "ui/console.h"
#include "ui/shader.h"

/* ---------------------------------------------------------------------- */

bool console_gl_check_format(DisplayChangeListener *dcl,
                             pixman_format_code_t format)
{
    switch (format) {
    case PIXMAN_BE_b8g8r8x8:
    case PIXMAN_BE_b8g8r8a8:
    case PIXMAN_r5g6b5:
        return true;
    default:
        return false;
    }
}

void surface_gl_create_texture(QemuGLShader *gls,
                               DisplaySurface *surface)
{
    assert(gls);
    assert(QEMU_IS_ALIGNED(surface_stride(surface), surface_bytes_per_pixel(surface)));

    switch (surface->format) {
    case PIXMAN_BE_b8g8r8x8:
    case PIXMAN_BE_b8g8r8a8:
        surface->glformat = GL_BGRA_EXT;
        surface->gltype = GL_UNSIGNED_BYTE;
        break;
    case PIXMAN_BE_x8r8g8b8:
    case PIXMAN_BE_a8r8g8b8:
        surface->glformat = GL_RGBA;
        surface->gltype = GL_UNSIGNED_BYTE;
        break;
    case PIXMAN_r5g6b5:
        surface->glformat = GL_RGB;
        surface->gltype = GL_UNSIGNED_SHORT_5_6_5;
        break;
    default:
        g_assert_not_reached();
    }

    glGenTextures(1, &surface->texture);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, surface->texture);
    glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT,
                  surface_stride(surface) / surface_bytes_per_pixel(surface));
    if (epoxy_is_desktop_gl()) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
                     surface_width(surface),
                     surface_height(surface),
                     0, surface->glformat, surface->gltype,
                     surface_data(surface));
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, surface->glformat,
                     surface_width(surface),
                     surface_height(surface),
                     0, surface->glformat, surface->gltype,
                     surface_data(surface));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
}

void surface_gl_update_texture(QemuGLShader *gls,
                               DisplaySurface *surface,
                               int x, int y, int w, int h)
{
    uint8_t *data = (void *)surface_data(surface);

    assert(gls);

    if (surface->texture) {
        glBindTexture(GL_TEXTURE_2D, surface->texture);
        glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT,
                      surface_stride(surface)
                      / surface_bytes_per_pixel(surface));
        glTexSubImage2D(GL_TEXTURE_2D, 0,
                        x, y, w, h,
                        surface->glformat, surface->gltype,
                        data + surface_stride(surface) * y
                        + surface_bytes_per_pixel(surface) * x);
    }
}

void surface_gl_render_texture(QemuGLShader *gls,
                               DisplaySurface *surface)
{
    assert(gls);

    glClearColor(0.1f, 0.1f, 0.1f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    qemu_gl_run_texture_blit(gls, false);
}

void surface_gl_destroy_texture(QemuGLShader *gls,
                                DisplaySurface *surface)
{
    if (!surface || !surface->texture) {
        return;
    }
    glDeleteTextures(1, &surface->texture);
    surface->texture = 0;
}

void surface_gl_setup_viewport(QemuGLShader *gls,
                               DisplaySurface *surface,
                               int ww, int wh)
{
    int gw, gh, stripe;
    float sw, sh;

    assert(gls);

    gw = surface_width(surface);
    gh = surface_height(surface);

    sw = (float)ww/gw;
    sh = (float)wh/gh;
    if (sw < sh) {
        stripe = wh - wh*sw/sh;
        glViewport(0, stripe / 2, ww, wh - stripe);
    } else {
        stripe = ww - ww*sh/sw;
        glViewport(stripe / 2, 0, ww - stripe, wh);
    }
}
