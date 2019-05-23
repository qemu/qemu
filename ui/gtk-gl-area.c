/*
 * GTK UI -- glarea opengl code.
 *
 * Requires 3.16+ (GtkGLArea widget).
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "trace.h"

#include "ui/console.h"
#include "ui/gtk.h"
#include "ui/egl-helpers.h"

#include "sysemu/sysemu.h"

static void gtk_gl_area_set_scanout_mode(VirtualConsole *vc, bool scanout)
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

void gd_gl_area_draw(VirtualConsole *vc)
{
    int ww, wh, y1, y2;

    if (!vc->gfx.gls) {
        return;
    }

    gtk_gl_area_make_current(GTK_GL_AREA(vc->gfx.drawing_area));
    ww = gtk_widget_get_allocated_width(vc->gfx.drawing_area);
    wh = gtk_widget_get_allocated_height(vc->gfx.drawing_area);

    if (vc->gfx.scanout_mode) {
        if (!vc->gfx.guest_fb.framebuffer) {
            return;
        }

        glBindFramebuffer(GL_READ_FRAMEBUFFER, vc->gfx.guest_fb.framebuffer);
        /* GtkGLArea sets GL_DRAW_FRAMEBUFFER for us */

        glViewport(0, 0, ww, wh);
        y1 = vc->gfx.y0_top ? 0 : vc->gfx.h;
        y2 = vc->gfx.y0_top ? vc->gfx.h : 0;
        glBlitFramebuffer(0, y1, vc->gfx.w, y2,
                          0, 0, ww, wh,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
    } else {
        if (!vc->gfx.ds) {
            return;
        }
        gtk_gl_area_make_current(GTK_GL_AREA(vc->gfx.drawing_area));

        surface_gl_setup_viewport(vc->gfx.gls, vc->gfx.ds, ww, wh);
        surface_gl_render_texture(vc->gfx.gls, vc->gfx.ds);
    }
}

void gd_gl_area_update(DisplayChangeListener *dcl,
                   int x, int y, int w, int h)
{
    VirtualConsole *vc = container_of(dcl, VirtualConsole, gfx.dcl);

    if (!vc->gfx.gls || !vc->gfx.ds) {
        return;
    }

    gtk_gl_area_make_current(GTK_GL_AREA(vc->gfx.drawing_area));
    surface_gl_update_texture(vc->gfx.gls, vc->gfx.ds, x, y, w, h);
    vc->gfx.glupdates++;
}

void gd_gl_area_refresh(DisplayChangeListener *dcl)
{
    VirtualConsole *vc = container_of(dcl, VirtualConsole, gfx.dcl);

    if (!vc->gfx.gls) {
        if (!gtk_widget_get_realized(vc->gfx.drawing_area)) {
            return;
        }
        gtk_gl_area_make_current(GTK_GL_AREA(vc->gfx.drawing_area));
        vc->gfx.gls = qemu_gl_init_shader();
        if (vc->gfx.ds) {
            surface_gl_create_texture(vc->gfx.gls, vc->gfx.ds);
        }
    }

    graphic_hw_update(dcl->con);

    if (vc->gfx.glupdates) {
        vc->gfx.glupdates = 0;
        gtk_gl_area_set_scanout_mode(vc, false);
        gtk_gl_area_queue_render(GTK_GL_AREA(vc->gfx.drawing_area));
    }
}

void gd_gl_area_switch(DisplayChangeListener *dcl,
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

    if (vc->gfx.gls) {
        gtk_gl_area_make_current(GTK_GL_AREA(vc->gfx.drawing_area));
        surface_gl_destroy_texture(vc->gfx.gls, vc->gfx.ds);
        surface_gl_create_texture(vc->gfx.gls, surface);
    }
    vc->gfx.ds = surface;

    if (resized) {
        gd_update_windowsize(vc);
    }
}

QEMUGLContext gd_gl_area_create_context(DisplayChangeListener *dcl,
                                        QEMUGLParams *params)
{
    VirtualConsole *vc = container_of(dcl, VirtualConsole, gfx.dcl);
    GdkWindow *window;
    GdkGLContext *ctx;
    GError *err = NULL;

    gtk_gl_area_make_current(GTK_GL_AREA(vc->gfx.drawing_area));
    window = gtk_widget_get_window(vc->gfx.drawing_area);
    ctx = gdk_window_create_gl_context(window, &err);
    gdk_gl_context_set_required_version(ctx,
                                        params->major_ver,
                                        params->minor_ver);
    gdk_gl_context_realize(ctx, &err);
    return ctx;
}

void gd_gl_area_destroy_context(DisplayChangeListener *dcl, QEMUGLContext ctx)
{
    /* FIXME */
}

void gd_gl_area_scanout_texture(DisplayChangeListener *dcl,
                                uint32_t backing_id,
                                bool backing_y_0_top,
                                uint32_t backing_width,
                                uint32_t backing_height,
                                uint32_t x, uint32_t y,
                                uint32_t w, uint32_t h)
{
    VirtualConsole *vc = container_of(dcl, VirtualConsole, gfx.dcl);

    vc->gfx.x = x;
    vc->gfx.y = y;
    vc->gfx.w = w;
    vc->gfx.h = h;
    vc->gfx.y0_top = backing_y_0_top;

    gtk_gl_area_make_current(GTK_GL_AREA(vc->gfx.drawing_area));

    if (backing_id == 0 || vc->gfx.w == 0 || vc->gfx.h == 0) {
        gtk_gl_area_set_scanout_mode(vc, false);
        return;
    }

    gtk_gl_area_set_scanout_mode(vc, true);
    egl_fb_setup_for_tex(&vc->gfx.guest_fb, backing_width, backing_height,
                         backing_id, false);
}

void gd_gl_area_scanout_flush(DisplayChangeListener *dcl,
                          uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    VirtualConsole *vc = container_of(dcl, VirtualConsole, gfx.dcl);

    gtk_gl_area_queue_render(GTK_GL_AREA(vc->gfx.drawing_area));
}

void gtk_gl_area_init(void)
{
    display_opengl = 1;
}

QEMUGLContext gd_gl_area_get_current_context(DisplayChangeListener *dcl)
{
    return gdk_gl_context_get_current();
}

int gd_gl_area_make_current(DisplayChangeListener *dcl,
                            QEMUGLContext ctx)
{
    gdk_gl_context_make_current(ctx);
    return 0;
}
