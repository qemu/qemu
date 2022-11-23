/*
 * GTK UI -- egl opengl code.
 *
 * Note that gtk 3.16+ (released 2015-03-23) has a GtkGLArea widget,
 * which is GtkDrawingArea like widget with opengl rendering support.
 *
 * This code handles opengl support on older gtk versions, using egl
 * to get a opengl context for the X11 window.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"

#include "trace.h"

#include "ui/console.h"
#include "ui/gtk.h"
#include "ui/egl-helpers.h"
#include "ui/shader.h"

#include "sysemu/sysemu.h"

static void gtk_egl_set_scanout_mode(VirtualConsole *vc, bool scanout)
{
    if (vc->gfx.scanout_mode == scanout) {
        return;
    }

    vc->gfx.scanout_mode = scanout;
    if (!vc->gfx.scanout_mode) {
        egl_fb_destroy(&vc->gfx.guest_fb);
        if (vc->gfx.surface) {
            surface_gl_destroy_texture(vc->gfx.gls, vc->gfx.ds);
            surface_gl_create_texture(vc->gfx.gls, vc->gfx.ds);
        }
    }
}

/** DisplayState Callbacks (opengl version) **/

void gd_egl_init(VirtualConsole *vc)
{
    GdkWindow *gdk_window = gtk_widget_get_window(vc->gfx.drawing_area);
    if (!gdk_window) {
        return;
    }

    Window x11_window = gdk_x11_window_get_xid(gdk_window);
    if (!x11_window) {
        return;
    }

    vc->gfx.ectx = qemu_egl_init_ctx();
    vc->gfx.esurface = qemu_egl_init_surface_x11
        (vc->gfx.ectx, (EGLNativeWindowType)x11_window);

    assert(vc->gfx.esurface);
}

void gd_egl_draw(VirtualConsole *vc)
{
    GdkWindow *window;
#ifdef CONFIG_GBM
    QemuDmaBuf *dmabuf = vc->gfx.guest_fb.dmabuf;
#endif
    int ww, wh;

    if (!vc->gfx.gls) {
        return;
    }

    window = gtk_widget_get_window(vc->gfx.drawing_area);
    ww = gdk_window_get_width(window);
    wh = gdk_window_get_height(window);

    if (vc->gfx.scanout_mode) {
#ifdef CONFIG_GBM
        if (dmabuf) {
            if (!dmabuf->draw_submitted) {
                return;
            } else {
                dmabuf->draw_submitted = false;
            }
        }
#endif
        gd_egl_scanout_flush(&vc->gfx.dcl, 0, 0, vc->gfx.w, vc->gfx.h);

        vc->gfx.scale_x = (double)ww / vc->gfx.w;
        vc->gfx.scale_y = (double)wh / vc->gfx.h;

        glFlush();
#ifdef CONFIG_GBM
        if (dmabuf) {
            egl_dmabuf_create_fence(dmabuf);
            if (dmabuf->fence_fd > 0) {
                qemu_set_fd_handler(dmabuf->fence_fd, gd_hw_gl_flushed, NULL, vc);
                return;
            }
            graphic_hw_gl_block(vc->gfx.dcl.con, false);
        }
#endif
    } else {
        if (!vc->gfx.ds) {
            return;
        }
        eglMakeCurrent(qemu_egl_display, vc->gfx.esurface,
                       vc->gfx.esurface, vc->gfx.ectx);

        surface_gl_setup_viewport(vc->gfx.gls, vc->gfx.ds, ww, wh);
        surface_gl_render_texture(vc->gfx.gls, vc->gfx.ds);

        eglSwapBuffers(qemu_egl_display, vc->gfx.esurface);

        vc->gfx.scale_x = (double)ww / surface_width(vc->gfx.ds);
        vc->gfx.scale_y = (double)wh / surface_height(vc->gfx.ds);

        glFlush();
    }
}

void gd_egl_update(DisplayChangeListener *dcl,
                   int x, int y, int w, int h)
{
    VirtualConsole *vc = container_of(dcl, VirtualConsole, gfx.dcl);

    if (!vc->gfx.gls || !vc->gfx.ds) {
        return;
    }

    eglMakeCurrent(qemu_egl_display, vc->gfx.esurface,
                   vc->gfx.esurface, vc->gfx.ectx);
    surface_gl_update_texture(vc->gfx.gls, vc->gfx.ds, x, y, w, h);
    vc->gfx.glupdates++;
}

void gd_egl_refresh(DisplayChangeListener *dcl)
{
    VirtualConsole *vc = container_of(dcl, VirtualConsole, gfx.dcl);

    gd_update_monitor_refresh_rate(
            vc, vc->window ? vc->window : vc->gfx.drawing_area);

    if (!vc->gfx.esurface) {
        gd_egl_init(vc);
        if (!vc->gfx.esurface) {
            return;
        }
        vc->gfx.gls = qemu_gl_init_shader();
        if (vc->gfx.ds) {
            surface_gl_destroy_texture(vc->gfx.gls, vc->gfx.ds);
            surface_gl_create_texture(vc->gfx.gls, vc->gfx.ds);
        }
#ifdef CONFIG_GBM
        if (vc->gfx.guest_fb.dmabuf) {
            egl_dmabuf_release_texture(vc->gfx.guest_fb.dmabuf);
            gd_egl_scanout_dmabuf(dcl, vc->gfx.guest_fb.dmabuf);
        }
#endif
    }

    graphic_hw_update(dcl->con);

    if (vc->gfx.glupdates) {
        vc->gfx.glupdates = 0;
        gtk_egl_set_scanout_mode(vc, false);
        gd_egl_draw(vc);
    }
}

void gd_egl_switch(DisplayChangeListener *dcl,
                   DisplaySurface *surface)
{
    VirtualConsole *vc = container_of(dcl, VirtualConsole, gfx.dcl);
    bool resized = true;

    trace_gd_switch(vc->label, surface_width(surface), surface_height(surface));

    if (vc->gfx.ds &&
        surface_width(vc->gfx.ds) == surface_width(surface) &&
        surface_height(vc->gfx.ds) == surface_height(surface)) {
        resized = false;
    }
    eglMakeCurrent(qemu_egl_display, vc->gfx.esurface,
                   vc->gfx.esurface, vc->gfx.ectx);

    surface_gl_destroy_texture(vc->gfx.gls, vc->gfx.ds);
    vc->gfx.ds = surface;
    if (vc->gfx.gls) {
        surface_gl_create_texture(vc->gfx.gls, vc->gfx.ds);
    }

    if (resized) {
        gd_update_windowsize(vc);
    }

    eglMakeCurrent(qemu_egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                   EGL_NO_CONTEXT);
}

QEMUGLContext gd_egl_create_context(DisplayGLCtx *dgc,
                                    QEMUGLParams *params)
{
    VirtualConsole *vc = container_of(dgc, VirtualConsole, gfx.dgc);

    eglMakeCurrent(qemu_egl_display, vc->gfx.esurface,
                   vc->gfx.esurface, vc->gfx.ectx);
    return qemu_egl_create_context(dgc, params);
}

void gd_egl_scanout_disable(DisplayChangeListener *dcl)
{
    VirtualConsole *vc = container_of(dcl, VirtualConsole, gfx.dcl);

    vc->gfx.w = 0;
    vc->gfx.h = 0;
    gtk_egl_set_scanout_mode(vc, false);
}

void gd_egl_scanout_texture(DisplayChangeListener *dcl,
                            uint32_t backing_id, bool backing_y_0_top,
                            uint32_t backing_width, uint32_t backing_height,
                            uint32_t x, uint32_t y,
                            uint32_t w, uint32_t h)
{
    VirtualConsole *vc = container_of(dcl, VirtualConsole, gfx.dcl);

    vc->gfx.x = x;
    vc->gfx.y = y;
    vc->gfx.w = w;
    vc->gfx.h = h;
    vc->gfx.y0_top = backing_y_0_top;

    eglMakeCurrent(qemu_egl_display, vc->gfx.esurface,
                   vc->gfx.esurface, vc->gfx.ectx);

    gtk_egl_set_scanout_mode(vc, true);
    egl_fb_setup_for_tex(&vc->gfx.guest_fb, backing_width, backing_height,
                         backing_id, false);
}

void gd_egl_scanout_dmabuf(DisplayChangeListener *dcl,
                           QemuDmaBuf *dmabuf)
{
#ifdef CONFIG_GBM
    VirtualConsole *vc = container_of(dcl, VirtualConsole, gfx.dcl);

    eglMakeCurrent(qemu_egl_display, vc->gfx.esurface,
                   vc->gfx.esurface, vc->gfx.ectx);

    egl_dmabuf_import_texture(dmabuf);
    if (!dmabuf->texture) {
        return;
    }

    gd_egl_scanout_texture(dcl, dmabuf->texture,
                           false, dmabuf->width, dmabuf->height,
                           0, 0, dmabuf->width, dmabuf->height);

    if (dmabuf->allow_fences) {
        vc->gfx.guest_fb.dmabuf = dmabuf;
    }
#endif
}

void gd_egl_cursor_dmabuf(DisplayChangeListener *dcl,
                          QemuDmaBuf *dmabuf, bool have_hot,
                          uint32_t hot_x, uint32_t hot_y)
{
#ifdef CONFIG_GBM
    VirtualConsole *vc = container_of(dcl, VirtualConsole, gfx.dcl);

    if (dmabuf) {
        egl_dmabuf_import_texture(dmabuf);
        if (!dmabuf->texture) {
            return;
        }
        egl_fb_setup_for_tex(&vc->gfx.cursor_fb, dmabuf->width, dmabuf->height,
                             dmabuf->texture, false);
    } else {
        egl_fb_destroy(&vc->gfx.cursor_fb);
    }
#endif
}

void gd_egl_cursor_position(DisplayChangeListener *dcl,
                            uint32_t pos_x, uint32_t pos_y)
{
    VirtualConsole *vc = container_of(dcl, VirtualConsole, gfx.dcl);

    vc->gfx.cursor_x = pos_x * vc->gfx.scale_x;
    vc->gfx.cursor_y = pos_y * vc->gfx.scale_y;
}

void gd_egl_scanout_flush(DisplayChangeListener *dcl,
                          uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    VirtualConsole *vc = container_of(dcl, VirtualConsole, gfx.dcl);
    GdkWindow *window;
    int ww, wh;

    if (!vc->gfx.scanout_mode) {
        return;
    }
    if (!vc->gfx.guest_fb.framebuffer) {
        return;
    }

    eglMakeCurrent(qemu_egl_display, vc->gfx.esurface,
                   vc->gfx.esurface, vc->gfx.ectx);

    window = gtk_widget_get_window(vc->gfx.drawing_area);
    ww = gdk_window_get_width(window);
    wh = gdk_window_get_height(window);
    egl_fb_setup_default(&vc->gfx.win_fb, ww, wh);
    if (vc->gfx.cursor_fb.texture) {
        egl_texture_blit(vc->gfx.gls, &vc->gfx.win_fb, &vc->gfx.guest_fb,
                         vc->gfx.y0_top);
        egl_texture_blend(vc->gfx.gls, &vc->gfx.win_fb, &vc->gfx.cursor_fb,
                          vc->gfx.y0_top,
                          vc->gfx.cursor_x, vc->gfx.cursor_y,
                          vc->gfx.scale_x, vc->gfx.scale_y);
    } else {
        egl_fb_blit(&vc->gfx.win_fb, &vc->gfx.guest_fb, !vc->gfx.y0_top);
    }

#ifdef CONFIG_GBM
    if (vc->gfx.guest_fb.dmabuf) {
        egl_dmabuf_create_sync(vc->gfx.guest_fb.dmabuf);
    }
#endif

    eglSwapBuffers(qemu_egl_display, vc->gfx.esurface);
}

void gd_egl_flush(DisplayChangeListener *dcl,
                  uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    VirtualConsole *vc = container_of(dcl, VirtualConsole, gfx.dcl);
    GtkWidget *area = vc->gfx.drawing_area;

    if (vc->gfx.guest_fb.dmabuf && !vc->gfx.guest_fb.dmabuf->draw_submitted) {
        graphic_hw_gl_block(vc->gfx.dcl.con, true);
        vc->gfx.guest_fb.dmabuf->draw_submitted = true;
        gtk_widget_queue_draw_area(area, x, y, w, h);
        return;
    }

    gd_egl_scanout_flush(&vc->gfx.dcl, x, y, w, h);
}

void gtk_egl_init(DisplayGLMode mode)
{
    GdkDisplay *gdk_display = gdk_display_get_default();
    Display *x11_display = gdk_x11_display_get_xdisplay(gdk_display);

    if (qemu_egl_init_dpy_x11(x11_display, mode) < 0) {
        return;
    }

    display_opengl = 1;
}

int gd_egl_make_current(DisplayGLCtx *dgc,
                        QEMUGLContext ctx)
{
    VirtualConsole *vc = container_of(dgc, VirtualConsole, gfx.dgc);

    return eglMakeCurrent(qemu_egl_display, vc->gfx.esurface,
                          vc->gfx.esurface, ctx);
}
