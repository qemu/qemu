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
#include "qemu-common.h"

#include "trace.h"

#include "ui/console.h"
#include "ui/gtk.h"
#include "ui/egl-helpers.h"

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

#if GTK_CHECK_VERSION(3, 0, 0)
    Window x11_window = gdk_x11_window_get_xid(gdk_window);
#else
    Window x11_window = gdk_x11_drawable_get_xid(gdk_window);
#endif
    if (!x11_window) {
        return;
    }

    vc->gfx.ectx = qemu_egl_init_ctx();
    vc->gfx.esurface = qemu_egl_init_surface_x11(vc->gfx.ectx, x11_window);

    assert(vc->gfx.esurface);
}

void gd_egl_draw(VirtualConsole *vc)
{
    GdkWindow *window;
    int ww, wh;

    if (!vc->gfx.gls) {
        return;
    }

    if (vc->gfx.scanout_mode) {
        gd_egl_scanout_flush(&vc->gfx.dcl, 0, 0, vc->gfx.w, vc->gfx.h);
    } else {
        if (!vc->gfx.ds) {
            return;
        }
        eglMakeCurrent(qemu_egl_display, vc->gfx.esurface,
                       vc->gfx.esurface, vc->gfx.ectx);

        window = gtk_widget_get_window(vc->gfx.drawing_area);
        gdk_drawable_get_size(window, &ww, &wh);
        surface_gl_setup_viewport(vc->gfx.gls, vc->gfx.ds, ww, wh);
        surface_gl_render_texture(vc->gfx.gls, vc->gfx.ds);

        eglSwapBuffers(qemu_egl_display, vc->gfx.esurface);
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

    if (!vc->gfx.esurface) {
        gd_egl_init(vc);
        if (!vc->gfx.esurface) {
            return;
        }
        vc->gfx.gls = console_gl_init_context();
        if (vc->gfx.ds) {
            surface_gl_create_texture(vc->gfx.gls, vc->gfx.ds);
        }
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

    surface_gl_destroy_texture(vc->gfx.gls, vc->gfx.ds);
    vc->gfx.ds = surface;
    if (vc->gfx.gls) {
        surface_gl_create_texture(vc->gfx.gls, vc->gfx.ds);
    }

    if (resized) {
        gd_update_windowsize(vc);
    }
}

QEMUGLContext gd_egl_create_context(DisplayChangeListener *dcl,
                                    QEMUGLParams *params)
{
    VirtualConsole *vc = container_of(dcl, VirtualConsole, gfx.dcl);

    eglMakeCurrent(qemu_egl_display, vc->gfx.esurface,
                   vc->gfx.esurface, vc->gfx.ectx);
    return qemu_egl_create_context(dcl, params);
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
    egl_fb_create_for_tex(&vc->gfx.guest_fb, backing_width, backing_height,
                          backing_id);
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
    gdk_drawable_get_size(window, &ww, &wh);
    egl_fb_setup_default(&vc->gfx.win_fb, ww, wh);
    egl_fb_blit(&vc->gfx.win_fb, &vc->gfx.guest_fb, !vc->gfx.y0_top);

    eglSwapBuffers(qemu_egl_display, vc->gfx.esurface);
}

void gtk_egl_init(void)
{
    GdkDisplay *gdk_display = gdk_display_get_default();
    Display *x11_display = gdk_x11_display_get_xdisplay(gdk_display);

    if (qemu_egl_init_dpy_x11(x11_display) < 0) {
        return;
    }

    display_opengl = 1;
}

int gd_egl_make_current(DisplayChangeListener *dcl,
                        QEMUGLContext ctx)
{
    VirtualConsole *vc = container_of(dcl, VirtualConsole, gfx.dcl);

    return eglMakeCurrent(qemu_egl_display, vc->gfx.esurface,
                          vc->gfx.esurface, ctx);
}
