/*
 * GTK UI
 *
 * Copyright IBM, Corp. 2012
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Portions from gtk-vnc (originally licensed under the LGPL v2+):
 *
 * GTK VNC Widget
 *
 * Copyright (C) 2006  Anthony Liguori <anthony@codemonkey.ws>
 * Copyright (C) 2009-2010 Daniel P. Berrange <dan@berrange.com>
 */

#define GETTEXT_PACKAGE "qemu"
#define LOCALEDIR "po"

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-control.h"
#include "qapi/qapi-commands-machine.h"
#include "qapi/qapi-commands-misc.h"
#include "qemu/cutils.h"
#include "qemu/main-loop.h"

#include "ui/console.h"
#include "ui/gtk.h"
#ifdef G_OS_WIN32
#include <gdk/gdkwin32.h>
#endif
#include "ui/win32-kbd-hook.h"

#include <glib/gi18n.h>
#include <locale.h>
#if defined(CONFIG_VTE)
#include <vte/vte.h>
#endif
#include <math.h>

#include "trace.h"
#include "qemu/cutils.h"
#include "ui/input.h"
#include "sysemu/runstate.h"
#include "sysemu/sysemu.h"
#include "keymaps.h"
#include "chardev/char.h"
#include "qom/object.h"

#define VC_WINDOW_X_MIN  320
#define VC_WINDOW_Y_MIN  240
#define VC_TERM_X_MIN     80
#define VC_TERM_Y_MIN     25
#define VC_SCALE_MIN    0.25
#define VC_SCALE_STEP   0.25

#ifdef GDK_WINDOWING_X11
#include "x_keymap.h"
#endif

#ifdef GDK_WINDOWING_BROADWAY
#include <gdk/broadway/gdkbroadway.h>
#endif

#if !defined(CONFIG_VTE)
# define VTE_CHECK_VERSION(a, b, c) 0
#endif

#define HOTKEY_MODIFIERS        (GDK_CONTROL_MASK | GDK_ALT_MASK)

static const guint16 *keycode_map;
static size_t keycode_maplen;

struct VCChardev {
    Chardev parent;
    VirtualConsole *console;
    bool echo;
};
typedef struct VCChardev VCChardev;

#define TYPE_CHARDEV_VC "chardev-vc"
DECLARE_INSTANCE_CHECKER(VCChardev, VC_CHARDEV,
                         TYPE_CHARDEV_VC)

bool gtk_use_gl_area;

static void gd_grab_pointer(VirtualConsole *vc, const char *reason);
static void gd_ungrab_pointer(GtkDisplayState *s);
static void gd_grab_keyboard(VirtualConsole *vc, const char *reason);
static void gd_ungrab_keyboard(GtkDisplayState *s);
static void setup_actions(GtkDisplayState *ds, GtkApplicationWindow *window);

/** Utility Functions **/

static VirtualConsole *gd_vc_find_by_menu(GtkDisplayState *s, const char* idx)
{
    VirtualConsole *vc;
    gint i;

    for (i = 0; i < s->nb_vcs; i++) {
        vc = &s->vc[i];
        if (g_str_equal(vc->label, idx)) {
            return vc;
        }
    }
    return NULL;
}

static VirtualConsole *gd_vc_find_by_page(GtkDisplayState *s, gint page)
{
    VirtualConsole *vc;
    gint i, p;

    for (i = 0; i < s->nb_vcs; i++) {
        vc = &s->vc[i];
        p = gtk_notebook_page_num(GTK_NOTEBOOK(s->notebook), vc->tab_item);
        if (p == page) {
            return vc;
        }
    }
    return NULL;
}

static VirtualConsole *gd_vc_find_current(GtkDisplayState *s)
{
    gint page;

    page = gtk_notebook_get_current_page(GTK_NOTEBOOK(s->notebook));
    return gd_vc_find_by_page(s, page);
}

static bool gd_is_grab_active(GtkDisplayState *s)
{
    return g_variant_get_boolean(g_action_get_state(G_ACTION(s->grab_action)));
}

static bool gd_grab_on_hover(GtkDisplayState *s)
{
    return g_variant_get_boolean(g_action_get_state(G_ACTION(s->grab_on_hover_action)));
}

static void gd_update_cursor(VirtualConsole *vc)
{
    GtkDisplayState *s = vc->s;

    if (vc->type != GD_VC_GFX ||
        !qemu_console_is_graphic(vc->gfx.dcl.con)) {
        return;
    }

    if (!gtk_widget_get_realized(vc->gfx.drawing_area)) {
        return;
    }

    if (s->full_screen || qemu_input_is_absolute() || s->ptr_owner == vc) {
        gtk_widget_set_cursor(GTK_WIDGET(vc->gfx.drawing_area), s->null_cursor);
    } else {
        gtk_widget_set_cursor(GTK_WIDGET(vc->gfx.drawing_area), NULL);
    }
}

static void gd_update_caption(GtkDisplayState *s)
{
    const char *status = "";
    gchar *prefix;
    gchar *title;
    const char *grab = "";
    bool is_paused = !runstate_is_running();
    int i;

    if (qemu_name) {
        prefix = g_strdup_printf("QEMU (%s)", qemu_name);
    } else {
        prefix = g_strdup_printf("QEMU");
    }

    if (s->ptr_owner != NULL &&
        s->ptr_owner->window == NULL) {
        grab = _(" - Press Ctrl+Alt+G to release grab");
    }

    if (is_paused) {
        status = _(" [Paused]");
    }
    s->external_pause_update = true;
    g_simple_action_set_state(s->pause_action, g_variant_new_boolean(is_paused));
    s->external_pause_update = false;

    title = g_strdup_printf("%s%s%s", prefix, status, grab);
    gtk_window_set_title(GTK_WINDOW(s->window), title);
    g_free(title);

    for (i = 0; i < s->nb_vcs; i++) {
        VirtualConsole *vc = &s->vc[i];

        if (!vc->window) {
            continue;
        }
        title = g_strdup_printf("%s: %s%s%s", prefix, vc->label,
                                vc == s->kbd_owner ? " +kbd" : "",
                                vc == s->ptr_owner ? " +ptr" : "");
        gtk_window_set_title(GTK_WINDOW(vc->window), title);
        g_free(title);
    }

    g_free(prefix);
}

static void gd_update_geometry_hints(VirtualConsole *vc)
{
    GtkDisplayState *s = vc->s;
    int width = 0;
    int height = 0;
    GtkWindow *geo_window;

    if (vc->type == GD_VC_GFX) {
        if (!vc->gfx.ds) {
            return;
        }
        if (s->free_scale) {
            width  = surface_width(vc->gfx.ds) * VC_SCALE_MIN;
            height = surface_height(vc->gfx.ds) * VC_SCALE_MIN;
        } else {
            width  = surface_width(vc->gfx.ds) * vc->gfx.scale_x;
            height = surface_height(vc->gfx.ds) * vc->gfx.scale_y;
        }
        gtk_widget_set_size_request(vc->gfx.drawing_area, width, height);

#if defined(CONFIG_VTE)
    } else if (vc->type == GD_VC_VTE) {
        VteTerminal *term = VTE_TERMINAL(vc->vte.terminal);

        width = vte_terminal_get_char_width(term) * VC_TERM_X_MIN;
        height = vte_terminal_get_char_height(term) * VC_TERM_Y_MIN;
#endif
    }

    geo_window = GTK_WINDOW(vc->window ? vc->window : s->window);
    gtk_window_set_default_size(geo_window, width, height);
}

void gd_update_windowsize(VirtualConsole *vc)
{
    GtkDisplayState *s = vc->s;

    gd_update_geometry_hints(vc);

    if (vc->type == GD_VC_GFX && !s->full_screen && !s->free_scale) {
        /*
        gtk_window_resize(GTK_WINDOW(vc->window ? vc->window : s->window),
                          VC_WINDOW_X_MIN, VC_WINDOW_Y_MIN);

        */
    }
}

static void gd_update_full_redraw(VirtualConsole *vc)
{
#if defined(CONFIG_OPENGL)
    if (vc->gfx.gls && gtk_use_gl_area) {
        gtk_gl_area_queue_render(GTK_GL_AREA(vc->gfx.drawing_area));
        return;
    }
#endif
    gtk_widget_queue_draw(vc->gfx.drawing_area);
}

static void gtk_release_modifiers(GtkDisplayState *s)
{
    VirtualConsole *vc = gd_vc_find_current(s);

    if (vc->type != GD_VC_GFX ||
        !qemu_console_is_graphic(vc->gfx.dcl.con)) {
        return;
    }
    qkbd_state_lift_all_keys(vc->gfx.kbd);
}

static void gd_widget_reparent(GtkWidget *from, GtkWidget *to,
                               GtkWidget *widget)
{
    g_object_ref(G_OBJECT(widget));
    gtk_widget_unparent(widget);
    gtk_widget_set_parent(widget, to);
    g_object_unref(G_OBJECT(widget));
}

static void *gd_win32_get_hwnd(VirtualConsole *vc)
{
#ifdef G_OS_WIN32
    return gdk_win32_window_get_impl_hwnd(
        gtk_widget_get_window(vc->window ? vc->window : vc->s->window));
#else
    return NULL;
#endif
}

/** DisplayState Callbacks **/

static void gd_update(DisplayChangeListener *dcl,
                      int x, int y, int w, int h)
{
    VirtualConsole *vc = container_of(dcl, VirtualConsole, gfx.dcl);

    trace_gd_update(vc->label, x, y, w, h);

    if (!gtk_widget_get_realized(vc->gfx.drawing_area)) {
        return;
    }

    if (vc->gfx.convert) {
        pixman_image_composite(PIXMAN_OP_SRC, vc->gfx.ds->image,
                               NULL, vc->gfx.convert,
                               x, y, 0, 0, x, y, w, h);
    }

    gtk_widget_queue_draw(vc->gfx.drawing_area);
}

static void gd_refresh(DisplayChangeListener *dcl)
{
    graphic_hw_update(dcl->con);
}

/*
static GdkDevice *gd_get_pointer(GdkDisplay *dpy)
{
    return gdk_seat_get_pointer(gdk_display_get_default_seat(dpy));
}
*/

static void gd_mouse_set(DisplayChangeListener *dcl,
                         int x, int y, int visible)
{
    VirtualConsole *vc = container_of(dcl, VirtualConsole, gfx.dcl);
/*
    GdkDisplay *dpy;
    gint x_root, y_root;

    if (qemu_input_is_absolute()) {
        return;
    }
    dpy = gtk_widget_get_display(vc->gfx.drawing_area);
    FIXME: replace gdk_device_warp with XWarpPointer
    gdk_window_get_root_coords(gtk_widget_get_window(vc->gfx.drawing_area),
                               x, y, &x_root, &y_root);
    gdk_device_warp(gd_get_pointer(dpy),
                    gtk_widget_get_screen(vc->gfx.drawing_area),
                    x_root, y_root);
*/
    vc->s->last_x = x;
    vc->s->last_y = y;
}

static void gd_cursor_define(DisplayChangeListener *dcl,
                             QEMUCursor *c)
{
    VirtualConsole *vc = container_of(dcl, VirtualConsole, gfx.dcl);
    GdkPixbuf *pixbuf;
    GdkTexture *texture;
    GdkCursor *cursor;

    if (!gtk_widget_get_realized(vc->gfx.drawing_area)) {
        return;
    }
    // TODO: make use of a MemoryTexture directly here
    pixbuf = gdk_pixbuf_new_from_data((guchar *)(c->data),
                                      GDK_COLORSPACE_RGB, true, 8,
                                      c->width, c->height, c->width * 4,
                                      NULL, NULL);
    texture = gdk_texture_new_for_pixbuf(pixbuf);
    cursor = gdk_cursor_new_from_texture(texture, c->hot_x, c->hot_y, NULL);
    gtk_widget_set_cursor(vc->gfx.drawing_area, cursor);
    g_object_unref(pixbuf);
    g_object_unref(cursor);
}

static void gd_switch(DisplayChangeListener *dcl,
                      DisplaySurface *surface)
{
    VirtualConsole *vc = container_of(dcl, VirtualConsole, gfx.dcl);
    bool resized = true;

    trace_gd_switch(vc->label, surface_width(surface), surface_height(surface));

    if (vc->gfx.surface) {
        cairo_surface_destroy(vc->gfx.surface);
        vc->gfx.surface = NULL;
    }
    if (vc->gfx.convert) {
        pixman_image_unref(vc->gfx.convert);
        vc->gfx.convert = NULL;
    }

    if (vc->gfx.ds &&
        surface_width(vc->gfx.ds) == surface_width(surface) &&
        surface_height(vc->gfx.ds) == surface_height(surface)) {
        resized = false;
    }
    vc->gfx.ds = surface;

    if (surface->format == PIXMAN_x8r8g8b8) {
        /*
         * PIXMAN_x8r8g8b8 == CAIRO_FORMAT_RGB24
         *
         * No need to convert, use surface directly.  Should be the
         * common case as this is qemu_default_pixelformat(32) too.
         */
        vc->gfx.surface = cairo_image_surface_create_for_data
            (surface_data(surface),
             CAIRO_FORMAT_RGB24,
             surface_width(surface),
             surface_height(surface),
             surface_stride(surface));
    } else {
        /* Must convert surface, use pixman to do it. */
        vc->gfx.convert = pixman_image_create_bits(PIXMAN_x8r8g8b8,
                                                   surface_width(surface),
                                                   surface_height(surface),
                                                   NULL, 0);
        vc->gfx.surface = cairo_image_surface_create_for_data
            ((void *)pixman_image_get_data(vc->gfx.convert),
             CAIRO_FORMAT_RGB24,
             pixman_image_get_width(vc->gfx.convert),
             pixman_image_get_height(vc->gfx.convert),
             pixman_image_get_stride(vc->gfx.convert));
        pixman_image_composite(PIXMAN_OP_SRC, vc->gfx.ds->image,
                               NULL, vc->gfx.convert,
                               0, 0, 0, 0, 0, 0,
                               pixman_image_get_width(vc->gfx.convert),
                               pixman_image_get_height(vc->gfx.convert));
    }

    if (resized) {
        gd_update_windowsize(vc);
    } else {
        gd_update_full_redraw(vc);
    }
}

static const DisplayChangeListenerOps dcl_ops = {
    .dpy_name             = "gtk",
    .dpy_gfx_update       = gd_update,
    .dpy_gfx_switch       = gd_switch,
    .dpy_gfx_check_format = qemu_pixman_check_format,
    .dpy_refresh          = gd_refresh,
    .dpy_mouse_set        = gd_mouse_set,
    .dpy_cursor_define    = gd_cursor_define,
};


#if defined(CONFIG_OPENGL)

static bool gd_has_dmabuf(DisplayChangeListener *dcl)
{
    VirtualConsole *vc = container_of(dcl, VirtualConsole, gfx.dcl);

    if (gtk_use_gl_area && !gtk_widget_get_realized(vc->gfx.drawing_area)) {
        /* FIXME: Assume it will work, actual check done after realize */
        /* fixing this would require delaying listener registration */
        return true;
    }

    return vc->gfx.has_dmabuf;
}

static void gd_gl_release_dmabuf(DisplayChangeListener *dcl,
                                 QemuDmaBuf *dmabuf)
{
#ifdef CONFIG_GBM
    egl_dmabuf_release_texture(dmabuf);
#endif
}

void gd_hw_gl_flushed(void *vcon)
{
    VirtualConsole *vc = vcon;
    QemuDmaBuf *dmabuf = vc->gfx.guest_fb.dmabuf;

    qemu_set_fd_handler(dmabuf->fence_fd, NULL, NULL, NULL);
    close(dmabuf->fence_fd);
    dmabuf->fence_fd = -1;
    graphic_hw_gl_block(vc->gfx.dcl.con, false);
}

/** DisplayState Callbacks (opengl version) **/

static const DisplayChangeListenerOps dcl_gl_area_ops = {
    .dpy_name             = "gtk-egl",
    .dpy_gfx_update       = gd_gl_area_update,
    .dpy_gfx_switch       = gd_gl_area_switch,
    .dpy_gfx_check_format = console_gl_check_format,
    .dpy_refresh          = gd_gl_area_refresh,
    .dpy_mouse_set        = gd_mouse_set,
    .dpy_cursor_define    = gd_cursor_define,

    .dpy_gl_scanout_texture  = gd_gl_area_scanout_texture,
    .dpy_gl_scanout_disable  = gd_gl_area_scanout_disable,
    .dpy_gl_update           = gd_gl_area_scanout_flush,
    .dpy_gl_scanout_dmabuf   = gd_gl_area_scanout_dmabuf,
    .dpy_gl_release_dmabuf   = gd_gl_release_dmabuf,
    .dpy_has_dmabuf          = gd_has_dmabuf,
};

static bool
gd_gl_area_is_compatible_dcl(DisplayGLCtx *dgc,
                             DisplayChangeListener *dcl)
{
    return dcl->ops == &dcl_gl_area_ops;
}

static const DisplayGLCtxOps gl_area_ctx_ops = {
    .dpy_gl_ctx_is_compatible_dcl = gd_gl_area_is_compatible_dcl,
    .dpy_gl_ctx_create       = gd_gl_area_create_context,
    .dpy_gl_ctx_destroy      = gd_gl_area_destroy_context,
    .dpy_gl_ctx_make_current = gd_gl_area_make_current,
};

#ifdef CONFIG_X11
static const DisplayChangeListenerOps dcl_egl_ops = {
    .dpy_name             = "gtk-egl",
    .dpy_gfx_update       = gd_egl_update,
    .dpy_gfx_switch       = gd_egl_switch,
    .dpy_gfx_check_format = console_gl_check_format,
    .dpy_refresh          = gd_egl_refresh,
    .dpy_mouse_set        = gd_mouse_set,
    .dpy_cursor_define    = gd_cursor_define,

    .dpy_gl_scanout_disable  = gd_egl_scanout_disable,
    .dpy_gl_scanout_texture  = gd_egl_scanout_texture,
    .dpy_gl_scanout_dmabuf   = gd_egl_scanout_dmabuf,
    .dpy_gl_cursor_dmabuf    = gd_egl_cursor_dmabuf,
    .dpy_gl_cursor_position  = gd_egl_cursor_position,
    .dpy_gl_update           = gd_egl_flush,
    .dpy_gl_release_dmabuf   = gd_gl_release_dmabuf,
    .dpy_has_dmabuf          = gd_has_dmabuf,
};

static bool
gd_egl_is_compatible_dcl(DisplayGLCtx *dgc,
                         DisplayChangeListener *dcl)
{
    return dcl->ops == &dcl_egl_ops;
}

static const DisplayGLCtxOps egl_ctx_ops = {
    .dpy_gl_ctx_is_compatible_dcl = gd_egl_is_compatible_dcl,
    .dpy_gl_ctx_create       = gd_egl_create_context,
    .dpy_gl_ctx_destroy      = qemu_egl_destroy_context,
    .dpy_gl_ctx_make_current = gd_egl_make_current,
};
#endif

#endif /* CONFIG_OPENGL */

/** QEMU Events **/

static void gd_change_runstate(void *opaque, bool running, RunState state)
{
    GtkDisplayState *s = opaque;

    gd_update_caption(s);
}

static void gd_mouse_mode_change(Notifier *notify, void *data)
{
    GtkDisplayState *s;
    int i;

    s = container_of(notify, GtkDisplayState, mouse_mode_notifier);
    /* release the grab at switching to absolute mode */
    if (qemu_input_is_absolute() && s->ptr_owner) {
        if (!s->ptr_owner->window) {
            g_simple_action_set_state(s->grab_action, g_variant_new_boolean(FALSE));
        } else {
            gd_ungrab_pointer(s);
        }
    }
    for (i = 0; i < s->nb_vcs; i++) {
        VirtualConsole *vc = &s->vc[i];
        gd_update_cursor(vc);
    }
}

/** GTK Events **/

static gboolean gd_window_close(GtkWidget *widget, void *opaque)
{
    GtkDisplayState *s = opaque;
    bool allow_close = true;

    if (s->opts->has_window_close && !s->opts->window_close) {
        allow_close = false;
    }

    if (allow_close) {
        qmp_quit(NULL);
        g_application_quit(G_APPLICATION(s->app));
    }

    return TRUE;
}

static void gd_set_ui_refresh_rate(VirtualConsole *vc, int refresh_rate)
{
    QemuUIInfo info;

    info = *dpy_get_ui_info(vc->gfx.dcl.con);
    info.refresh_rate = refresh_rate;
    dpy_set_ui_info(vc->gfx.dcl.con, &info, true);
}

static void gd_set_ui_size(VirtualConsole *vc, gint width, gint height)
{
    QemuUIInfo info;

    info = *dpy_get_ui_info(vc->gfx.dcl.con);
    info.width = width;
    info.height = height;
    dpy_set_ui_info(vc->gfx.dcl.con, &info, true);
}

#if defined(CONFIG_OPENGL)

static gboolean gd_render_event(GtkGLArea *area, GdkGLContext *context,
                                void *opaque)
{
    VirtualConsole *vc = opaque;

    if (vc->gfx.gls) {
        gd_gl_area_draw(vc);
    }
    return TRUE;
}

static void gd_resize_event(GtkGLArea *area,
                            gint width, gint height, gpointer *opaque)
{
    VirtualConsole *vc = (void *)opaque;

    gd_set_ui_size(vc, width, height);
}

#endif

void gd_update_monitor_refresh_rate(VirtualConsole *vc, GtkWidget *widget)
{   
    GtkNative *native = gtk_widget_get_native(widget);
    GdkSurface *surface = gtk_native_get_surface(native);
    int refresh_rate;

    if (surface) {
        GdkDisplay *dpy = gtk_widget_get_display(widget);
        GdkMonitor *monitor = gdk_display_get_monitor_at_surface(dpy, surface);
        refresh_rate = gdk_monitor_get_refresh_rate(monitor); /* [mHz] */
    } else {
        refresh_rate = 0;
    }

    gd_set_ui_refresh_rate(vc, refresh_rate);

    /* T = 1 / f = 1 [s*Hz] / f = 1000*1000 [ms*mHz] / f */
    vc->gfx.dcl.update_interval = refresh_rate ?
        MIN(1000 * 1000 / refresh_rate, GUI_REFRESH_INTERVAL_DEFAULT) :
        GUI_REFRESH_INTERVAL_DEFAULT;
}

static void gd_draw_event(GtkDrawingArea *widget, cairo_t *cr, int ww, int wh, void *opaque)
{
    VirtualConsole *vc = opaque;
    GtkDisplayState *s = vc->s;
    int mx, my;
    int fbw, fbh;

#if defined(CONFIG_OPENGL)
    if (vc->gfx.gls) {
        if (gtk_use_gl_area) {
            /* invoke render callback please */
            return;
        } else {
#ifdef CONFIG_X11
            gd_egl_draw(vc);
            return;
#else
            abort();
#endif
        }
    }
#endif

    if (!gtk_widget_get_realized(GTK_WIDGET(widget))) {
        return;
    }
    if (!vc->gfx.ds) {
        return;
    }
    if (!vc->gfx.surface) {
        return;
    }

    gd_update_monitor_refresh_rate(vc, vc->window ? vc->window : s->window);

    fbw = surface_width(vc->gfx.ds);
    fbh = surface_height(vc->gfx.ds);


    if (s->full_screen) {
        vc->gfx.scale_x = (double)ww / fbw;
        vc->gfx.scale_y = (double)wh / fbh;
    } else if (s->free_scale) {
        double sx, sy;

        sx = (double)ww / fbw;
        sy = (double)wh / fbh;

        vc->gfx.scale_x = vc->gfx.scale_y = MIN(sx, sy);
    }

    fbw *= vc->gfx.scale_x;
    fbh *= vc->gfx.scale_y;

    mx = my = 0;
    if (ww > fbw) {
        mx = (ww - fbw) / 2;
    }
    if (wh > fbh) {
        my = (wh - fbh) / 2;
    }

    cairo_rectangle(cr, 0, 0, ww, wh);

    /* Optionally cut out the inner area where the pixmap
       will be drawn. This avoids 'flashing' since we're
       not double-buffering. Note we're using the undocumented
       behaviour of drawing the rectangle from right to left
       to cut out the whole */
    cairo_rectangle(cr, mx + fbw, my,
                    -1 * fbw, fbh);
    cairo_fill(cr);

    cairo_scale(cr, vc->gfx.scale_x, vc->gfx.scale_y);
    cairo_set_source_surface(cr, vc->gfx.surface,
                             mx / vc->gfx.scale_x, my / vc->gfx.scale_y);
    cairo_paint(cr);
}

static void gd_motion_event(GtkEventControllerMotion *controller, 
                            double pointer_x, double pointer_y, void *opaque)
{
    VirtualConsole *vc = opaque;
    GtkDisplayState *s = vc->s;
    GtkNative *native;
    GdkSurface *surface;
    int x, y;
    int mx = 0;
    int my = 0;
    int fbh, fbw;
    int ww, wh, ws;

    if (!vc->gfx.ds) {
        return ;
    }

    fbw = surface_width(vc->gfx.ds) * vc->gfx.scale_x;
    fbh = surface_height(vc->gfx.ds) * vc->gfx.scale_y;

    native = gtk_widget_get_native(vc->gfx.drawing_area);
    surface = gtk_native_get_surface(native);
    ww = gdk_surface_get_width(surface);
    wh = gdk_surface_get_height(surface);
    ws = gdk_surface_get_scale_factor(surface);

    if (ww > fbw) {
        mx = (ww - fbw) / 2;
    }
    if (wh > fbh) {
        my = (wh - fbh) / 2;
    }

    x = ((int)pointer_x - mx) / vc->gfx.scale_x * ws;
    y = ((int)pointer_y - my) / vc->gfx.scale_y * ws;

    if (qemu_input_is_absolute()) {
        if (x < 0 || y < 0 ||
            x >= surface_width(vc->gfx.ds) ||
            y >= surface_height(vc->gfx.ds)) {
            return ;
        }
        qemu_input_queue_abs(vc->gfx.dcl.con, INPUT_AXIS_X, x,
                             0, surface_width(vc->gfx.ds));
        qemu_input_queue_abs(vc->gfx.dcl.con, INPUT_AXIS_Y, y,
                             0, surface_height(vc->gfx.ds));
        qemu_input_event_sync();
    } else if (s->last_set && s->ptr_owner == vc) {
        qemu_input_queue_rel(vc->gfx.dcl.con, INPUT_AXIS_X, x - s->last_x);
        qemu_input_queue_rel(vc->gfx.dcl.con, INPUT_AXIS_Y, y - s->last_y);
        qemu_input_event_sync();
    }
    s->last_x = x;
    s->last_y = y;
    s->last_set = TRUE;

    if (!qemu_input_is_absolute() && s->ptr_owner == vc) {
        //GdkScreen *screen = gtk_widget_get_screen(vc->gfx.drawing_area);
        GdkDisplay *dpy = gtk_widget_get_display(vc->gfx.drawing_area);
        GtkNative *native = gtk_widget_get_native(vc->gfx.drawing_area);
        GdkSurface *surface = gtk_native_get_surface(native);
        GdkMonitor *monitor = gdk_display_get_monitor_at_surface(dpy, surface);
        GdkRectangle geometry;

        int x = (int)pointer_x;
        int y = (int)pointer_y;

        gdk_monitor_get_geometry(monitor, &geometry);

        /* In relative mode check to see if client pointer hit
         * one of the monitor edges, and if so move it back to the
         * center of the monitor. This is important because the pointer
         * in the server doesn't correspond 1-for-1, and so
         * may still be only half way across the screen. Without
         * this warp, the server pointer would thus appear to hit
         * an invisible wall */
        if (x <= geometry.x || x - geometry.x >= geometry.width - 1 ||
            y <= geometry.y || y - geometry.y >= geometry.height - 1) {
            //GdkDevice *dev = gdk_event_get_device((GdkEvent *)motion);
            x = geometry.x + geometry.width / 2;
            y = geometry.y + geometry.height / 2;

            //TODO: replace with X11 spexific API
            //gdk_device_warp(dev, screen, x, y);
            s->last_set = FALSE;
            return ;
        }
    }
    return ;
}

static void gd_button_event(GtkEventController *controller, gint n_presses, 
                                    double x, double y, void *opaque)
{
    VirtualConsole *vc = opaque;
    GdkEvent *button = gtk_event_controller_get_current_event(controller);
    GtkDisplayState *s = vc->s;
    guint button_nbr = gdk_button_event_get_button(GDK_EVENT(button));
    InputButton btn;

    /* implicitly grab the input at the first click in the relative mode */
    if (button_nbr == 1 && n_presses == 1 &&
        !qemu_input_is_absolute() && s->ptr_owner != vc) {
        if (!vc->window) {
            g_simple_action_set_state(s->grab_action, 
                                      g_variant_new_boolean(TRUE));
        } else {
            gd_grab_pointer(vc, "relative-mode-click");
        }
    }

    if (button_nbr == 1) {
        btn = INPUT_BUTTON_LEFT;
    } else if (button_nbr == 2) {
        btn = INPUT_BUTTON_MIDDLE;
    } else if (button_nbr == 3) {
        btn = INPUT_BUTTON_RIGHT;
    } else if (button_nbr == 8) {
        btn = INPUT_BUTTON_SIDE;
    } else if (button_nbr == 9) {
        btn = INPUT_BUTTON_EXTRA;
    } else {
        return;
    }

    if (n_presses == 2 || n_presses == 3) {
        return;
    }

    qemu_input_queue_btn(vc->gfx.dcl.con, btn, n_presses == 1);
    qemu_input_event_sync();
}

static gboolean gd_scroll_event(GtkEventControllerScroll *controller,
                                double delta_x, double delta_y,
                                void *opaque)
{
    VirtualConsole *vc = opaque;
    GdkEvent *event = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(controller));
    GdkScrollDirection direction = gdk_scroll_event_get_direction(GDK_EVENT(event));
    InputButton btn_vertical;
    InputButton btn_horizontal;
    bool has_vertical = false;
    bool has_horizontal = false;

    if (direction == GDK_SCROLL_UP) {
        btn_vertical = INPUT_BUTTON_WHEEL_UP;
        has_vertical = true;
    } else if (direction == GDK_SCROLL_DOWN) {
        btn_vertical = INPUT_BUTTON_WHEEL_DOWN;
        has_vertical = true;
    } else if (direction == GDK_SCROLL_LEFT) {
        btn_horizontal = INPUT_BUTTON_WHEEL_LEFT;
        has_horizontal = true;
    } else if (direction == GDK_SCROLL_RIGHT) {
        btn_horizontal = INPUT_BUTTON_WHEEL_RIGHT;
        has_horizontal = true;
    } else if (direction == GDK_SCROLL_SMOOTH) {
        if (delta_y > 0) {
            btn_vertical = INPUT_BUTTON_WHEEL_DOWN;
            has_vertical = true;
        } else if (delta_y < 0) {
            btn_vertical = INPUT_BUTTON_WHEEL_UP;
            has_vertical = true;
        } else if (delta_x > 0) {
            btn_horizontal = INPUT_BUTTON_WHEEL_RIGHT;
            has_horizontal = true;
        } else if (delta_x < 0) {
            btn_horizontal = INPUT_BUTTON_WHEEL_LEFT;
            has_horizontal = true;
        } else {
            return TRUE;
        }
    } else {
        return TRUE;
    }

    if (has_vertical) {
        qemu_input_queue_btn(vc->gfx.dcl.con, btn_vertical, true);
        qemu_input_event_sync();
        qemu_input_queue_btn(vc->gfx.dcl.con, btn_vertical, false);
        qemu_input_event_sync();
    }

    if (has_horizontal) {
        qemu_input_queue_btn(vc->gfx.dcl.con, btn_horizontal, true);
        qemu_input_event_sync();
        qemu_input_queue_btn(vc->gfx.dcl.con, btn_horizontal, false);
        qemu_input_event_sync();
    }

    return TRUE;
}


static const guint16 *gd_get_keymap(size_t *maplen)
{
    GdkDisplay *dpy = gdk_display_get_default();

#ifdef GDK_WINDOWING_X11
    if (GDK_IS_X11_DISPLAY(dpy)) {
        trace_gd_keymap_windowing("x11");
        return qemu_xkeymap_mapping_table(
            gdk_x11_display_get_xdisplay(dpy), maplen);
    }
#endif

#ifdef GDK_WINDOWING_WAYLAND
    if (GDK_IS_WAYLAND_DISPLAY(dpy)) {
        trace_gd_keymap_windowing("wayland");
        *maplen = qemu_input_map_xorgevdev_to_qcode_len;
        return qemu_input_map_xorgevdev_to_qcode;
    }
#endif

#ifdef GDK_WINDOWING_WIN32
    if (GDK_IS_WIN32_DISPLAY(dpy)) {
        trace_gd_keymap_windowing("win32");
        *maplen = qemu_input_map_atset1_to_qcode_len;
        return qemu_input_map_atset1_to_qcode;
    }
#endif

#ifdef GDK_WINDOWING_QUARTZ
    if (GDK_IS_QUARTZ_DISPLAY(dpy)) {
        trace_gd_keymap_windowing("quartz");
        *maplen = qemu_input_map_osx_to_qcode_len;
        return qemu_input_map_osx_to_qcode;
    }
#endif
/*
#ifdef GDK_WINDOWING_BROADWAY
    if (GDK_IS_BROADWAY_DISPLAY(dpy)) {
        trace_gd_keymap_windowing("broadway");
        g_warning("experimental: using broadway, x11 virtual keysym\n"
                  "mapping - with very limited support. See also\n"
                  "https://bugzilla.gnome.org/show_bug.cgi?id=700105");
        *maplen = qemu_input_map_x11_to_qcode_len;
        return qemu_input_map_x11_to_qcode;
    }
#endif
*/
    g_warning("Unsupported GDK Windowing platform.\n"
              "Disabling extended keycode tables.\n"
              "Please report to qemu-devel@nongnu.org\n"
              "including the following information:\n"
              "\n"
              "  - Operating system\n"
              "  - GDK Windowing system build\n");
    return NULL;
}


static int gd_map_keycode(int scancode)
{
    if (!keycode_map) {
        return 0;
    }
    if (scancode > keycode_maplen) {
        return 0;
    }

    return keycode_map[scancode];
}

static gboolean gd_text_key_down(GtkEventControllerKey *controller,
                                 guint keyval, guint keycode, 
                                 GdkModifierType state, void *opaque)
{
    VirtualConsole *vc = opaque;
    QemuConsole *con = vc->gfx.dcl.con;

    if (keyval == GDK_KEY_Delete) {
        kbd_put_qcode_console(con, Q_KEY_CODE_DELETE, false);
    } else {
        int qcode = gd_map_keycode(keycode);
        kbd_put_qcode_console(con, qcode, false);
    }
    return TRUE;
}

static void gd_key_event(guint keyval, guint keycode, bool is_press,
                        VirtualConsole *vc)
{
    int qcode;

    if (keyval == GDK_KEY_Pause) {
        qkbd_state_key_event(vc->gfx.kbd, Q_KEY_CODE_PAUSE,
                             is_press);
        return;
    }

    qcode = gd_map_keycode(keycode);

    trace_gd_key_event(vc->label, keycode, qcode,
                       (is_press) ? "down" : "up");

    qkbd_state_key_event(vc->gfx.kbd, qcode,
                         is_press);
}

static void gd_key_event_pressed(GtkEventControllerKey *controller,
                                guint keyval, guint keycode, GdkModifierType state,
                                void *opaque)                            
{
    VirtualConsole *vc = opaque;
    gd_key_event(keyval, keycode, true, vc);

}

static void gd_key_event_released(GtkEventControllerKey *controller,
                                guint keyval, guint keycode, GdkModifierType state,
                                void *opaque) 
{
    VirtualConsole *vc = opaque;
    gd_key_event(keyval, keycode, false, vc);
}
/*
static gboolean gd_grab_broken_event(GtkWidget *widget,
                                     GdkGrabBrokenEvent *event, void *opaque)
{
#ifdef CONFIG_WIN32*/
    /*
     * On Windows the Ctrl-Alt-Del key combination can't be grabbed. This
     * key combination leaves all three keys in a stuck condition. We use
     * the grab-broken-event to release all keys.
     */
/*    if (event->keyboard) {
        VirtualConsole *vc = opaque;
        GtkDisplayState *s = vc->s;

        gtk_release_modifiers(s);
    }
#endif
    return TRUE;
}
*/
/** Window Menu Actions **/

static void gd_menu_pause(GSimpleAction *action, GVariant *param, GtkDisplayState *s)
{
    if (s->external_pause_update) {
        return;
    }
    if (runstate_is_running()) {
        qmp_stop(NULL);
    } else {
        qmp_cont(NULL);
    }
}

static void gd_menu_reset(void *opaque)
{
    qmp_system_reset(NULL);
}

static void gd_menu_powerdown(void *opaque)
{
    qmp_system_powerdown(NULL);
}

static void gd_menu_quit(GSimpleAction *action, GVariant *param, GtkDisplayState *s)
{
    qmp_quit(NULL);
    g_application_quit(G_APPLICATION(s->app));
}

static void gd_menu_switch_vc(GSimpleAction *action, GVariant *param, GtkDisplayState *s)
{
    VirtualConsole *vc = gd_vc_find_by_menu(s, g_variant_get_string(param, NULL));
    GtkNotebook *nb = GTK_NOTEBOOK(s->notebook);
    gint page;

    gtk_release_modifiers(s);
    if (vc) {
        page = gtk_notebook_page_num(nb, vc->tab_item);
        gtk_notebook_set_current_page(nb, page);
        g_simple_action_set_state(action, param);
        gtk_widget_grab_focus(vc->focus);
    }
}
/*
static void gd_accel_switch_vc(void *opaque)
{
    VirtualConsole *vc = opaque;

    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(vc->menu_item), TRUE);
}
*/

static void gd_menu_show_tabs(GSimpleAction *action, GVariant *param, GtkDisplayState *s)
{
    VirtualConsole *vc = gd_vc_find_current(s);

    if (!g_variant_get_boolean(g_action_get_state(G_ACTION(s->show_tabs_action)))) {
        gtk_notebook_set_show_tabs(GTK_NOTEBOOK(s->notebook), TRUE);
        g_simple_action_set_state(s->show_tabs_action, g_variant_new_boolean(TRUE));
    } else {
        gtk_notebook_set_show_tabs(GTK_NOTEBOOK(s->notebook), FALSE);
        g_simple_action_set_state(s->show_tabs_action, g_variant_new_boolean(FALSE));
    }
    gd_update_windowsize(vc);
}

static gboolean gd_tab_window_close(GtkWidget *widget, GdkEvent *event,
                                    void *opaque)
{
    VirtualConsole *vc = opaque;
    GtkDisplayState *s = vc->s;

    
    //gtk_widget_set_sensitive(vc->menu_item, true);
    gd_widget_reparent(vc->window, s->notebook, vc->tab_item);
    gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(s->notebook),
                                    vc->tab_item, vc->label);
    gtk_window_close(GTK_WINDOW(vc->window));
    vc->window = NULL;
#if defined(CONFIG_OPENGL)
    if (vc->gfx.esurface) {
        eglDestroySurface(qemu_egl_display, vc->gfx.esurface);
        vc->gfx.esurface = NULL;
    }
    if (vc->gfx.ectx) {
        eglDestroyContext(qemu_egl_display, vc->gfx.ectx);
        vc->gfx.ectx = NULL;
    }
#endif
    return TRUE;
}

static gboolean gd_win_grab(GtkWidget *widget, GVariant *args, void *opaque)
{
    VirtualConsole *vc = opaque;

    fprintf(stderr, "%s: %s\n", __func__, vc->label);
    if (vc->s->ptr_owner) {
        gd_ungrab_pointer(vc->s);
    } else {
        gd_grab_pointer(vc, "user-request-detached-tab");
    }
    return TRUE;
}

static void gd_menu_show_menubar(GSimpleAction *action, GVariant *param, GtkDisplayState *s)
{
    VirtualConsole *vc = gd_vc_find_current(s);

    if (s->full_screen) {
        return;
    }

    if (!g_variant_get_boolean(g_action_get_state(G_ACTION(s->show_menubar_action)))) {
        g_simple_action_set_state(s->show_menubar_action, g_variant_new_boolean(TRUE));
        gtk_application_window_set_show_menubar(GTK_APPLICATION_WINDOW(s->window), 
                                                TRUE);
    } else {
        g_simple_action_set_state(s->show_menubar_action, g_variant_new_boolean(FALSE));
        gtk_application_window_set_show_menubar(GTK_APPLICATION_WINDOW(s->window), 
                                                FALSE);        
    }
    gd_update_windowsize(vc);
}

static void gd_menu_full_screen(GSimpleAction *action, GVariant* param, GtkDisplayState *s)
{
    VirtualConsole *vc = gd_vc_find_current(s);

    if (!s->full_screen) {
        gtk_notebook_set_show_tabs(GTK_NOTEBOOK(s->notebook), FALSE);
        gtk_application_window_set_show_menubar(GTK_APPLICATION_WINDOW(s->window), 
                                                FALSE);
        if (vc->type == GD_VC_GFX) {
            gtk_widget_set_size_request(vc->gfx.drawing_area, -1, -1);
        }
        gtk_window_fullscreen(GTK_WINDOW(s->window));
        s->full_screen = TRUE;
    } else {
        gtk_window_unfullscreen(GTK_WINDOW(s->window));
        if (g_variant_get_boolean(g_action_get_state(G_ACTION(s->show_tabs_action)))) {
            gtk_notebook_set_show_tabs(GTK_NOTEBOOK(s->notebook), TRUE);
        }
        if (g_variant_get_boolean(g_action_get_state(G_ACTION(s->show_menubar_action)))) {
            gtk_application_window_set_show_menubar(GTK_APPLICATION_WINDOW(s->window), 
                                                    TRUE);
        }
        s->full_screen = FALSE;
        if (vc->type == GD_VC_GFX) {
            vc->gfx.scale_x = 1.0;
            vc->gfx.scale_y = 1.0;
            gd_update_windowsize(vc);
        }
    }

    gd_update_cursor(vc);
}

static void gd_menu_zoom_in(GSimpleAction *action, GVariant* param, GtkDisplayState *s)
{
    VirtualConsole *vc = gd_vc_find_current(s);

    g_simple_action_set_state(s->zoom_fit_action, g_variant_new_boolean(FALSE));

    vc->gfx.scale_x += VC_SCALE_STEP;
    vc->gfx.scale_y += VC_SCALE_STEP;

    gd_update_windowsize(vc);
}

static void gd_menu_zoom_out(GSimpleAction *action, GVariant* param, GtkDisplayState *s)
{
    VirtualConsole *vc = gd_vc_find_current(s);

    g_simple_action_set_state(s->zoom_fit_action, g_variant_new_boolean(FALSE));

    vc->gfx.scale_x -= VC_SCALE_STEP;
    vc->gfx.scale_y -= VC_SCALE_STEP;

    vc->gfx.scale_x = MAX(vc->gfx.scale_x, VC_SCALE_MIN);
    vc->gfx.scale_y = MAX(vc->gfx.scale_y, VC_SCALE_MIN);

    gd_update_windowsize(vc);
}

static void gd_menu_zoom_fixed(GSimpleAction *action, GVariant* param, GtkDisplayState *s)
{
    VirtualConsole *vc = gd_vc_find_current(s);

    vc->gfx.scale_x = 1.0;
    vc->gfx.scale_y = 1.0;

    gd_update_windowsize(vc);
}

static void gd_menu_zoom_fit(GSimpleAction *action, GVariant* param, GtkDisplayState *s)
{
    VirtualConsole *vc = gd_vc_find_current(s);

    if (g_variant_get_boolean(g_action_get_state(G_ACTION(s->zoom_fit_action)))) {
        g_simple_action_set_state(s->zoom_fit_action, g_variant_new_boolean(FALSE));
        s->free_scale = TRUE;
    } else {
        g_simple_action_set_state(s->zoom_fit_action, g_variant_new_boolean(TRUE));
        s->free_scale = FALSE;
        vc->gfx.scale_x = 1.0;
        vc->gfx.scale_y = 1.0;
    }

    gd_update_windowsize(vc);
    gd_update_full_redraw(vc);
}

static void gd_grab_update(VirtualConsole *vc, bool kbd, bool ptr)
{
    /*
    GdkDisplay *display = gtk_widget_get_display(vc->gfx.drawing_area);
    GdkSeatCapabilities caps = 0;
    GdkCursor *cursor = NULL;

    if (kbd) {
        caps |= GDK_SEAT_CAPABILITY_KEYBOARD;
    }
    if (ptr) {
        caps |= GDK_SEAT_CAPABILITY_ALL_POINTING;
        cursor = vc->s->null_cursor;
    }

    TODO: figure out how to replace this with XIGrabDevice
    if (caps) {
        gdk_seat_grab(seat, window, caps, false, cursor,
                      NULL, NULL, NULL);
    } else {
        gdk_seat_ungrab(seat);
    }
    */
}

static void gd_grab_keyboard(VirtualConsole *vc, const char *reason)
{
    if (vc->s->kbd_owner) {
        if (vc->s->kbd_owner == vc) {
            return;
        } else {
            gd_ungrab_keyboard(vc->s);
        }
    }

    win32_kbd_set_grab(true);
    gd_grab_update(vc, true, vc->s->ptr_owner == vc);
    vc->s->kbd_owner = vc;
    gd_update_caption(vc->s);
    trace_gd_grab(vc->label, "kbd", reason);
}

static void gd_ungrab_keyboard(GtkDisplayState *s)
{
    VirtualConsole *vc = s->kbd_owner;

    if (vc == NULL) {
        return;
    }
    s->kbd_owner = NULL;

    win32_kbd_set_grab(false);
    gd_grab_update(vc, false, vc->s->ptr_owner == vc);
    gd_update_caption(s);
    trace_gd_ungrab(vc->label, "kbd");
}

static void gd_grab_pointer(VirtualConsole *vc, const char *reason)
{
    //GdkDisplay *display = gtk_widget_get_display(vc->gfx.drawing_area);

    if (vc->s->ptr_owner) {
        if (vc->s->ptr_owner == vc) {
            return;
        } else {
            gd_ungrab_pointer(vc->s);
        }
    }

    gd_grab_update(vc, vc->s->kbd_owner == vc, true);
    /*
    TODO: FIXME
    gdk_device_get_position(gd_get_pointer(display),
                            NULL, &vc->s->grab_x_root, &vc->s->grab_y_root);
    */
    vc->s->ptr_owner = vc;
    gd_update_caption(vc->s);
    trace_gd_grab(vc->label, "ptr", reason);
}

static void gd_ungrab_pointer(GtkDisplayState *s)
{
    VirtualConsole *vc = s->ptr_owner;

    if (vc == NULL) {
        return;
    }
    s->ptr_owner = NULL;

    gd_grab_update(vc, vc->s->kbd_owner == vc, false);
    /*
    GdkDisplay *display;
    display = gtk_widget_get_display(vc->gfx.drawing_area);
    gdk_device_warp(gd_get_pointer(display),
                    gtk_widget_get_screen(vc->gfx.drawing_area),
                    vc->s->grab_x_root, vc->s->grab_y_root);
    */
    gd_update_caption(s);
    trace_gd_ungrab(vc->label, "ptr");
}

static void gd_menu_grab_input(GSimpleAction *action, GVariant *param, GtkDisplayState *s)
{
    VirtualConsole *vc = gd_vc_find_current(s);

    if (!gd_is_grab_active(s)) {
        g_simple_action_set_state(s->grab_action, g_variant_new_boolean(TRUE));
        gd_grab_keyboard(vc, "user-request-main-window");
        gd_grab_pointer(vc, "user-request-main-window");
    } else {
        g_simple_action_set_state(s->grab_action, g_variant_new_boolean(FALSE));
        gd_ungrab_keyboard(s);
        gd_ungrab_pointer(s);
    }

    gd_update_cursor(vc);
}

static void gd_change_page(GtkNotebook *nb, gpointer arg1, guint arg2,
                           gpointer data)
{
    GtkDisplayState *s = data;
    VirtualConsole *vc;
    gboolean on_vga;

    if (!gtk_widget_get_realized(s->notebook)) {
        return;
    }

    vc = gd_vc_find_by_page(s, arg2);
    if (!vc) {
        return;
    }
    //gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(vc->menu_item),
    //                               TRUE);
    on_vga = (vc->type == GD_VC_GFX &&
              qemu_console_is_graphic(vc->gfx.dcl.con));
    if (!on_vga) {
        g_simple_action_set_state(s->grab_action, g_variant_new_boolean(FALSE));
    } else if (s->full_screen) {
        g_simple_action_set_state(s->grab_action, g_variant_new_boolean(TRUE));
    }
    g_simple_action_set_enabled(s->grab_action, on_vga);
#ifdef CONFIG_VTE
    g_simple_action_set_enabled(s->copy_action, vc->type == GD_VC_VTE);
#endif

    gd_update_windowsize(vc);
    gd_update_cursor(vc);
}

static gboolean gd_enter_event(GtkEventControllerMotion *controller, double x, double y,
                               gpointer opaque)
{
    VirtualConsole *vc = opaque;
    GtkDisplayState *s = vc->s;

    if (gd_grab_on_hover(s)) {
        gd_grab_keyboard(vc, "grab-on-hover");
    }
    return TRUE;
}

static gboolean gd_leave_event(GtkEventControllerMotion *controller, double x, double y,
                               gpointer opaque)
{
    VirtualConsole *vc = opaque;
    GtkDisplayState *s = vc->s;

    if (gd_grab_on_hover(s)) {
        gd_ungrab_keyboard(s);
    }
    return TRUE;
}

static void gd_focus_enter_event(GtkEventControllerFocus *controller, gpointer opaque)
{
    VirtualConsole *vc = opaque;

    win32_kbd_set_window(gd_win32_get_hwnd(vc));
}

static void gd_focus_leave_event(GtkEventControllerFocus *controller, gpointer opaque)
{
    VirtualConsole *vc = opaque;
    GtkDisplayState *s = vc->s;

    win32_kbd_set_window(NULL);
    gtk_release_modifiers(s);
}

/** Virtual Console Callbacks **/

static void gd_vc_menu_init(GtkDisplayState *s, VirtualConsole *vc,
                               int idx)
{
    g_menu_append(s->vc_menu, vc->label, g_strdup_printf("win.vc('%s')", vc->label));
    //FIXME gtk_accel_group_connect(s->accel_group, GDK_KEY_1 + idx,
}

#if defined(CONFIG_VTE)
static void gd_menu_copy(GSimpleAction *action, GVariant *param, GtkDisplayState *s)
{
    VirtualConsole *vc = gd_vc_find_current(s);

#if VTE_CHECK_VERSION(0, 50, 0)
    vte_terminal_copy_clipboard_format(VTE_TERMINAL(vc->vte.terminal),
                                       VTE_FORMAT_TEXT);
#else
    vte_terminal_copy_clipboard(VTE_TERMINAL(vc->vte.terminal));
#endif
}

static void gd_vc_adjustment_changed(GtkAdjustment *adjustment, void *opaque)
{
    VirtualConsole *vc = opaque;

    if (gtk_adjustment_get_upper(adjustment) >
        gtk_adjustment_get_page_size(adjustment)) {
        gtk_widget_set_visible(vc->vte.scrollbar, TRUE);
    } else {
        gtk_widget_set_visible(vc->vte.scrollbar, FALSE);
    }
}

static void gd_vc_send_chars(VirtualConsole *vc)
{
    uint32_t len, avail;

    len = qemu_chr_be_can_write(vc->vte.chr);
    avail = fifo8_num_used(&vc->vte.out_fifo);
    while (len > 0 && avail > 0) {
        const uint8_t *buf;
        uint32_t size;

        buf = fifo8_pop_buf(&vc->vte.out_fifo, MIN(len, avail), &size);
        qemu_chr_be_write(vc->vte.chr, buf, size);
        len = qemu_chr_be_can_write(vc->vte.chr);
        avail -= size;
    }
}

static int gd_vc_chr_write(Chardev *chr, const uint8_t *buf, int len)
{
    VCChardev *vcd = VC_CHARDEV(chr);
    VirtualConsole *vc = vcd->console;

    vte_terminal_feed(VTE_TERMINAL(vc->vte.terminal), (const char *)buf, len);
    return len;
}

static void gd_vc_chr_accept_input(Chardev *chr)
{
    VCChardev *vcd = VC_CHARDEV(chr);
    VirtualConsole *vc = vcd->console;

    gd_vc_send_chars(vc);
}

static void gd_vc_chr_set_echo(Chardev *chr, bool echo)
{
    VCChardev *vcd = VC_CHARDEV(chr);
    VirtualConsole *vc = vcd->console;

    if (vc) {
        vc->vte.echo = echo;
    } else {
        vcd->echo = echo;
    }
}

static int nb_vcs;
static Chardev *vcs[MAX_VCS];
static void gd_vc_open(Chardev *chr,
                       ChardevBackend *backend,
                       bool *be_opened,
                       Error **errp)
{
    if (nb_vcs == MAX_VCS) {
        error_setg(errp, "Maximum number of consoles reached");
        return;
    }

    vcs[nb_vcs++] = chr;

    /* console/chardev init sometimes completes elsewhere in a 2nd
     * stage, so defer OPENED events until they are fully initialized
     */
    *be_opened = false;
}

static void char_gd_vc_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->parse = qemu_chr_parse_vc;
    cc->open = gd_vc_open;
    cc->chr_write = gd_vc_chr_write;
    cc->chr_accept_input = gd_vc_chr_accept_input;
    cc->chr_set_echo = gd_vc_chr_set_echo;
}

static const TypeInfo char_gd_vc_type_info = {
    .name = TYPE_CHARDEV_VC,
    .parent = TYPE_CHARDEV,
    .instance_size = sizeof(VCChardev),
    .class_init = char_gd_vc_class_init,
};

static gboolean gd_vc_in(VteTerminal *terminal, gchar *text, guint size,
                         gpointer user_data)
{
    VirtualConsole *vc = user_data;
    uint32_t free;

    if (vc->vte.echo) {
        VteTerminal *term = VTE_TERMINAL(vc->vte.terminal);
        int i;
        for (i = 0; i < size; i++) {
            uint8_t c = text[i];
            if (c >= 128 || isprint(c)) {
                /* 8-bit characters are considered printable.  */
                vte_terminal_feed(term, &text[i], 1);
            } else if (c == '\r' || c == '\n') {
                vte_terminal_feed(term, "\r\n", 2);
            } else {
                char ctrl[2] = { '^', 0};
                ctrl[1] = text[i] ^ 64;
                vte_terminal_feed(term, ctrl, 2);
            }
        }
    }

    free = fifo8_num_free(&vc->vte.out_fifo);
    fifo8_push_all(&vc->vte.out_fifo, (uint8_t *)text, MIN(free, size));
    gd_vc_send_chars(vc);

    return TRUE;
}

static void gd_vc_vte_init(GtkDisplayState *s, VirtualConsole *vc,
                              Chardev *chr, int idx)
{
    char buffer[32];
    GtkWidget *box;
    GtkWidget *scrollbar;
    GtkAdjustment *vadjustment;
    VCChardev *vcd = VC_CHARDEV(chr);

    vc->s = s;
    vc->vte.echo = vcd->echo;
    vc->vte.chr = chr;
    fifo8_create(&vc->vte.out_fifo, 4096);
    vcd->console = vc;

    snprintf(buffer, sizeof(buffer), "vc%d", idx);
    vc->label = g_strdup_printf("%s", vc->vte.chr->label
                                ? vc->vte.chr->label : buffer);
    gd_vc_menu_init(s, vc, idx);

    vc->vte.terminal = vte_terminal_new();
    g_signal_connect(vc->vte.terminal, "commit", G_CALLBACK(gd_vc_in), vc);

    /* The documentation says that the default is UTF-8, but actually it is
     * 7-bit ASCII at least in VTE 0.38. The function is deprecated since
     * VTE 0.54 (only UTF-8 is supported now). */
#if !VTE_CHECK_VERSION(0, 54, 0)
#if VTE_CHECK_VERSION(0, 38, 0)
    vte_terminal_set_encoding(VTE_TERMINAL(vc->vte.terminal), "UTF-8", NULL);
#else
    vte_terminal_set_encoding(VTE_TERMINAL(vc->vte.terminal), "UTF-8");
#endif
#endif

    vte_terminal_set_scrollback_lines(VTE_TERMINAL(vc->vte.terminal), -1);
    vte_terminal_set_size(VTE_TERMINAL(vc->vte.terminal),
                          VC_TERM_X_MIN, VC_TERM_Y_MIN);

#if VTE_CHECK_VERSION(0, 28, 0)
    vadjustment = gtk_scrollable_get_vadjustment
        (GTK_SCROLLABLE(vc->vte.terminal));
#else
    vadjustment = vte_terminal_get_adjustment(VTE_TERMINAL(vc->vte.terminal));
#endif

    box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    scrollbar = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, vadjustment);

    gtk_box_append(GTK_BOX(box), scrollbar);
    gtk_widget_set_hexpand(vc->vte.terminal, TRUE);
    gtk_widget_set_vexpand(vc->vte.terminal, TRUE);
    gtk_box_append(GTK_BOX(box), vc->vte.terminal);

    vc->vte.box = box;
    vc->vte.scrollbar = scrollbar;

    g_signal_connect(vadjustment, "changed",
                     G_CALLBACK(gd_vc_adjustment_changed), vc);

    vc->type = GD_VC_VTE;
    vc->tab_item = box;
    vc->focus = vc->vte.terminal;
    gtk_notebook_append_page(GTK_NOTEBOOK(s->notebook), vc->tab_item,
                             gtk_label_new(vc->label));

    qemu_chr_be_event(vc->vte.chr, CHR_EVENT_OPENED);
}


static void gd_vcs_init(GtkDisplayState *s)
{
    int i;

    for (i = 0; i < nb_vcs; i++) {
        VirtualConsole *vc = &s->vc[s->nb_vcs];
        gd_vc_vte_init(s, vc, vcs[i], s->nb_vcs);
        s->nb_vcs++;
    }
}
#endif /* CONFIG_VTE */

/** Window Creation **/

static void gd_connect_vc_gfx_signals(VirtualConsole *vc)
{   
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(vc->gfx.drawing_area),
                                    gd_draw_event, vc, NULL);
#if defined(CONFIG_OPENGL)
    if (gtk_use_gl_area) {
        /* wire up GtkGlArea events */
        g_signal_connect(vc->gfx.drawing_area, "render",
                         G_CALLBACK(gd_render_event), vc);
        g_signal_connect(vc->gfx.drawing_area, "resize",
                         G_CALLBACK(gd_resize_event), vc);
    }
#endif

    GtkEventController *key_controller = gtk_event_controller_key_new();
    gtk_widget_add_controller(vc->gfx.drawing_area, key_controller);

    if (qemu_console_is_graphic(vc->gfx.dcl.con)) {
        GtkEventController *motion_controller = gtk_event_controller_motion_new();
        g_signal_connect(motion_controller, "motion",
                         G_CALLBACK(gd_motion_event), vc);
        g_signal_connect(motion_controller, "enter",
                         G_CALLBACK(gd_enter_event), vc);
        g_signal_connect(motion_controller, "leave",
                         G_CALLBACK(gd_leave_event), vc);
        gtk_widget_add_controller(vc->gfx.drawing_area, motion_controller);

        GtkGesture *controller = gtk_gesture_click_new();
        g_signal_connect(controller, "pressed",
                         G_CALLBACK(gd_button_event), vc);
        g_signal_connect(controller, "released",
                         G_CALLBACK(gd_button_event), vc);
        gtk_widget_add_controller(vc->gfx.drawing_area, 
                                  GTK_EVENT_CONTROLLER(controller));

        GtkEventController *scroll_controller = gtk_event_controller_scroll_new(
            GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES
        );
        g_signal_connect(scroll_controller, "scroll",
                         G_CALLBACK(gd_scroll_event), vc);
        gtk_widget_add_controller(vc->gfx.drawing_area, scroll_controller);

        g_signal_connect(key_controller, "key-pressed",
                         G_CALLBACK(gd_key_event_pressed), vc);
        g_signal_connect(key_controller, "key-released",
                         G_CALLBACK(gd_key_event_released), vc);

        GtkEventController *focus_controller = gtk_event_controller_focus_new();
        g_signal_connect(focus_controller, "enter",
                         G_CALLBACK(gd_focus_enter_event), vc);
        g_signal_connect(focus_controller, "leave",
                         G_CALLBACK(gd_focus_leave_event), vc);
        gtk_widget_add_controller(vc->gfx.drawing_area, focus_controller);

        /*
        TODO: Figure out whether we can drop this event handler
        g_signal_connect(vc->gfx.drawing_area, "configure-event",
                         G_CALLBACK(gd_configure), vc); 
        g_signal_connect(vc->gfx.drawing_area, "grab-broken-event",
                         G_CALLBACK(gd_grab_broken_event), vc);
        */
    } else {
        g_signal_connect(key_controller, "key-pressed",
                         G_CALLBACK(gd_text_key_down), vc);
    }
}

static void gd_connect_signals(GtkDisplayState *s)
{
    g_signal_connect(s->window, "close-request",
                     G_CALLBACK(gd_window_close), s);
    g_signal_connect(s->notebook, "switch-page",
                     G_CALLBACK(gd_change_page), s);
}

static GMenu *gd_create_menu_machine(GtkDisplayState *s)
{
    GMenu *machine_menu;
    machine_menu = g_menu_new();
    g_menu_append(machine_menu, _("_Pause"), "win.pause");

    GMenu *first_section_model = g_menu_new();
    GMenuItem *first_section = g_menu_item_new_section(NULL, G_MENU_MODEL(first_section_model));
    g_menu_append_item(machine_menu, first_section);

    g_menu_append(first_section_model, _("_Reset"), "win.reset");

    g_menu_append(first_section_model, _("Power _Down"), "win.power_down");

    GMenu *second_section_model = g_menu_new();
    GMenuItem *second_section = g_menu_item_new_section(NULL, G_MENU_MODEL(second_section_model));
    g_menu_append_item(machine_menu, second_section);

    g_menu_append(second_section_model, _("_Quit"), "win.quit");

    return machine_menu;
}

#if defined(CONFIG_OPENGL)
static void gl_area_realize(GtkGLArea *area, VirtualConsole *vc)
{
    gtk_gl_area_make_current(area);
    qemu_egl_display = eglGetCurrentDisplay();
    vc->gfx.has_dmabuf = qemu_egl_has_dmabuf();
    if (!vc->gfx.has_dmabuf) {
        error_report("GtkGLArea console lacks DMABUF support.");
    }
}
#endif
static void gd_vc_gfx_init(GtkDisplayState *s, VirtualConsole *vc,
                              QemuConsole *con, int idx)
{
    bool zoom_to_fit = false;

    vc->label = qemu_console_get_label(con);
    vc->s = s;
    vc->gfx.scale_x = 1.0;
    vc->gfx.scale_y = 1.0;

#if defined(CONFIG_OPENGL)
    if (display_opengl) {
        if (gtk_use_gl_area) {
            vc->gfx.drawing_area = gtk_gl_area_new();
            g_signal_connect(vc->gfx.drawing_area, "realize",
                             G_CALLBACK(gl_area_realize), vc);
            vc->gfx.dcl.ops = &dcl_gl_area_ops;
            vc->gfx.dgc.ops = &gl_area_ctx_ops;
        } else {
#ifdef CONFIG_X11
            vc->gfx.drawing_area = gtk_drawing_area_new();
            /*
             * gtk_widget_set_double_buffered() was deprecated in 3.14.
             * It is required for opengl rendering on X11 though.  A
             * proper replacement (native opengl support) is only
             * available in 3.16+.  Silence the warning if possible.
             */
/* TODO: drop me
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
            gtk_widget_set_double_buffered(vc->gfx.drawing_area, FALSE);
#pragma GCC diagnostic pop
*/
            vc->gfx.dcl.ops = &dcl_egl_ops;
            vc->gfx.dgc.ops = &egl_ctx_ops;
            vc->gfx.has_dmabuf = qemu_egl_has_dmabuf();
#else
            abort();
#endif
        }
    } else
#endif
    {
        vc->gfx.drawing_area = gtk_drawing_area_new();
        vc->gfx.dcl.ops = &dcl_ops;
    }

    gtk_widget_set_can_focus(vc->gfx.drawing_area, TRUE);

    vc->type = GD_VC_GFX;
    vc->tab_item = vc->gfx.drawing_area;
    vc->focus = vc->gfx.drawing_area;
    gtk_notebook_append_page(GTK_NOTEBOOK(s->notebook),
                             vc->tab_item, gtk_label_new(vc->label));

    vc->gfx.kbd = qkbd_state_init(con);
    vc->gfx.dcl.con = con;

    if (display_opengl) {
        qemu_console_set_display_gl_ctx(con, &vc->gfx.dgc);
    }
    register_displaychangelistener(&vc->gfx.dcl);

    gd_connect_vc_gfx_signals(vc);
    gd_vc_menu_init(s, vc, idx);

    if (dpy_ui_info_supported(vc->gfx.dcl.con)) {
        zoom_to_fit = true;
    }
    if (s->opts->u.gtk.has_zoom_to_fit) {
        zoom_to_fit = s->opts->u.gtk.zoom_to_fit;
    }
    if (zoom_to_fit) {
        g_action_activate(G_ACTION(s->zoom_fit_action), NULL);
        s->free_scale = true;
    }
}

static GMenu *gd_create_menu_view(GtkDisplayState *s, DisplayOptions *opts)
{
    GMenu *view_menu;
    QemuConsole *con;
    int vc;

    view_menu = g_menu_new();

    GMenu *first_section_model = g_menu_new();
    GMenuItem *first_section = g_menu_item_new_section(NULL, G_MENU_MODEL(first_section_model));
    g_menu_append_item(view_menu, first_section);
#if defined(CONFIG_VTE)
    g_menu_append(first_section_model, _("_Copy"), "win.copy");
#endif
    g_menu_append(first_section_model, _("_Fullscreen"), "win.fullscreen");

    GMenu *second_section_model = g_menu_new();
    GMenuItem *second_section = g_menu_item_new_section(NULL, G_MENU_MODEL(second_section_model));
    g_menu_append_item(view_menu, second_section);

    g_menu_append(second_section_model, _("Zoom _In"), "win.zoom-in");
    g_menu_append(second_section_model, _("Zoom _Out"), "win.zoom-out");
    g_menu_append(second_section_model, _("Best _Fit"), "win.zoom-best-fit");
    g_menu_append(second_section_model, _("Zoom To _Fit"), "win.zoom-to-fit");

    GMenu *third_section_model = g_menu_new();
    GMenuItem *third_section = g_menu_item_new_section(NULL, G_MENU_MODEL(third_section_model));
    g_menu_append_item(view_menu, third_section);

    g_menu_append(third_section_model, _("Grab On _Hover"), "win.grab-on-hover");
    g_menu_append(third_section_model, _("_Grab Input"), "win.grab-input");

    s->vc_menu = g_menu_new();
    GMenuItem *vc_section = g_menu_item_new_section(NULL, G_MENU_MODEL(s->vc_menu));
    g_menu_append_item(view_menu, vc_section);

    GMenu *fourth_section_model = g_menu_new();
    GMenuItem *fourth_section = g_menu_item_new_section(NULL, G_MENU_MODEL(fourth_section_model));
    g_menu_append_item(view_menu, fourth_section);

    g_menu_append(fourth_section_model, _("Show _Tabs"), "win.show-tabs");
    g_menu_append(fourth_section_model, _("Detach Tab"), "win.untabify");
    g_menu_append(fourth_section_model, _("Show Menubar"), "win.show-menubar");

    for (vc = 0;; vc++) {
        con = qemu_console_lookup_by_index(vc);
        if (!con) {
            break;
        }
        gd_vc_gfx_init(s, &s->vc[vc], con, vc);
        s->nb_vcs++;
    }
#if defined(CONFIG_VTE)
    gd_vcs_init(s);
#endif

    return view_menu;
}

static GMenu* gd_create_menus_models(GtkDisplayState *s, DisplayOptions *opts)
{
    GMenu *model = g_menu_new();
    GMenu *machine_menu = gd_create_menu_machine(s);
    GMenu *view_menu = gd_create_menu_view(s, opts);

    GMenuItem *machine_item = g_menu_item_new_submenu(_("_Machine"), G_MENU_MODEL(machine_menu));
    g_menu_insert_item(model, 0, machine_item);

    GMenuItem *view_menu_item = g_menu_item_new_submenu(_("_View"), G_MENU_MODEL(view_menu));
    s->view_menu_item = view_menu_item;
    g_menu_insert_item(model, 1, view_menu_item);

    return model;
}

static void gd_menu_untabify(GSimpleAction *action, GVariant *param, GtkDisplayState *s)
{
    VirtualConsole *vc = gd_vc_find_current(s);

    if (vc->type == GD_VC_GFX &&
        qemu_console_is_graphic(vc->gfx.dcl.con)) {
        g_simple_action_set_state(s->grab_action, g_variant_new_boolean(FALSE));
    }
    if (!vc->window) {
        //gtk_widget_set_sensitive(vc->menu_item, false);
        vc->window = gtk_application_window_new(s->app);
        gtk_application_window_set_show_menubar(GTK_APPLICATION_WINDOW(vc->window),
                                                TRUE);
        setup_actions(s, GTK_APPLICATION_WINDOW(vc->window));
#if defined(CONFIG_OPENGL)
        if (vc->gfx.esurface) {
            eglDestroySurface(qemu_egl_display, vc->gfx.esurface);
            vc->gfx.esurface = NULL;
        }
        if (vc->gfx.esurface) {
            eglDestroyContext(qemu_egl_display, vc->gfx.ectx);
            vc->gfx.ectx = NULL;
        }
#endif
        gd_widget_reparent(s->notebook, vc->window, vc->tab_item);

        g_signal_connect(vc->window, "close-request",
                         G_CALLBACK(gd_tab_window_close), vc);
        gtk_window_present(GTK_WINDOW(vc->window));

        if (qemu_console_is_graphic(vc->gfx.dcl.con)) {
            GtkEventController *controller = gtk_shortcut_controller_new();
            GtkShortcutTrigger *trigger = gtk_keyval_trigger_new(GDK_KEY_g, HOTKEY_MODIFIERS);
            GtkShortcutAction *action = gtk_callback_action_new(gd_win_grab, vc, NULL);
            GtkShortcut *shortcut = gtk_shortcut_new(trigger, action);
            gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(controller), shortcut);
            gtk_widget_add_controller(vc->window, controller);
        }
        gd_update_caption(s);
    }
}

static gboolean gtkinit;
static void setup_actions(GtkDisplayState *ds, GtkApplicationWindow *window)
{
    GSimpleAction* action;

    action = g_simple_action_new ("quit", NULL);
    g_signal_connect(action, "activate",
                     G_CALLBACK(gd_menu_quit), ds);
    g_action_map_add_action (G_ACTION_MAP(window), G_ACTION (action));

    action = g_simple_action_new ("power_down", NULL);
    g_signal_connect(action, "activate",
                    G_CALLBACK (gd_menu_powerdown), NULL);
    g_action_map_add_action (G_ACTION_MAP(window), G_ACTION (action));

    ds->pause_action = g_simple_action_new_stateful ("pause", NULL, g_variant_new_boolean (FALSE));
    g_signal_connect(ds->pause_action, "activate",
                    G_CALLBACK (gd_menu_pause), ds);
    g_action_map_add_action (G_ACTION_MAP(window), G_ACTION (ds->pause_action));

    action = g_simple_action_new ("reset", NULL);
    g_signal_connect(action, "activate",
                    G_CALLBACK (gd_menu_reset), NULL);
    g_action_map_add_action (G_ACTION_MAP(window), G_ACTION (action));


    ds->grab_on_hover_action = g_simple_action_new_stateful ("grab-on-hover", NULL, g_variant_new_boolean (FALSE));
    g_action_map_add_action (G_ACTION_MAP(window), G_ACTION (ds->grab_on_hover_action));

    ds->grab_action = g_simple_action_new_stateful ("grab-input", NULL, g_variant_new_boolean (FALSE));
    g_signal_connect(ds->grab_action, "activate",
                    G_CALLBACK (gd_menu_grab_input), ds);
    g_action_map_add_action (G_ACTION_MAP(window), G_ACTION (ds->grab_action));

    ds->show_tabs_action = g_simple_action_new_stateful ("show-tabs", NULL, g_variant_new_boolean (FALSE));
    g_signal_connect(ds->show_tabs_action, "activate",
                    G_CALLBACK (gd_menu_show_tabs), ds);
    g_action_map_add_action (G_ACTION_MAP(window), G_ACTION (ds->show_tabs_action));

    action = g_simple_action_new ("untabify", NULL);
    g_signal_connect(action, "activate",
                    G_CALLBACK (gd_menu_untabify), ds);
    g_action_map_add_action (G_ACTION_MAP(window), G_ACTION (action));

    ds->show_menubar_action = g_simple_action_new_stateful ("show-menubar", NULL, g_variant_new_boolean (FALSE));
    g_signal_connect(ds->show_menubar_action, "activate",
                    G_CALLBACK (gd_menu_show_menubar), ds);
    
    g_simple_action_set_state(ds->show_menubar_action,
                              g_variant_new_boolean(
                                !ds->opts->u.gtk.has_show_menubar || ds->opts->u.gtk.show_menubar
                             ));
    g_action_map_add_action (G_ACTION_MAP(window), G_ACTION (ds->show_menubar_action));

#if defined(CONFIG_VTE)
    ds->copy_action = g_simple_action_new ("copy", NULL);
    g_signal_connect(ds->copy_action, "activate",
                    G_CALLBACK (gd_menu_copy), ds);
    g_action_map_add_action (G_ACTION_MAP(window), G_ACTION (ds->copy_action));
#endif

    ds->full_screen_action = g_simple_action_new ("fullscreen", NULL);
    g_signal_connect(ds->full_screen_action, "activate",
                    G_CALLBACK (gd_menu_full_screen), ds);
    g_action_map_add_action (G_ACTION_MAP(window), G_ACTION (ds->full_screen_action));
 
    action = g_simple_action_new ("zoom-in", NULL);
    g_signal_connect(action, "activate",
                    G_CALLBACK (gd_menu_zoom_in), ds);
    g_action_map_add_action (G_ACTION_MAP(window), G_ACTION (action));

    action = g_simple_action_new ("zoom-out", NULL);
    g_signal_connect(action, "activate",
                    G_CALLBACK (gd_menu_zoom_out), ds);
    g_action_map_add_action (G_ACTION_MAP(window), G_ACTION (action));

    action = g_simple_action_new ("zoom-best-fit", NULL);
    g_signal_connect(action, "activate",
                    G_CALLBACK (gd_menu_zoom_fixed), ds);
    g_action_map_add_action (G_ACTION_MAP(window), G_ACTION (action));

    ds->zoom_fit_action = g_simple_action_new_stateful ("zoom-to-fit", NULL, g_variant_new_boolean (FALSE));
    g_signal_connect(ds->zoom_fit_action, "activate",
                    G_CALLBACK (gd_menu_zoom_fit), ds);
    g_action_map_add_action (G_ACTION_MAP(window), G_ACTION (ds->zoom_fit_action));

    action = g_simple_action_new_stateful("vc", G_VARIANT_TYPE_STRING, g_variant_new_string("VGA"));
    g_signal_connect(action, "activate",
                    G_CALLBACK (gd_menu_switch_vc), ds);
    g_action_map_add_action (G_ACTION_MAP(window), G_ACTION (action));    
}

static void on_app_startup(GtkApplication *app, GtkDisplayState *ds)
{
    const char * quit_accels[2] = {  "<Ctrl><Alt>Q", NULL };
    const char * fullscreen_accels[2] = {  "<Ctrl><Alt>F", NULL };
    const char * show_menubar_accels[2] = {  "<Ctrl><Alt>M", NULL };
    const char * grab_input_accels[2] = {  "<Ctrl><Alt>G", NULL };
    const char * best_fit_accels[2] = {  "<Ctrl><Alt>0", NULL };
    const char * zoom_out_accels[2] = {  "<Ctrl><Alt>minus", NULL };
    const char * zoom_in_accels[2] = {  "<Ctrl><Alt>plus", NULL };

    gtk_application_set_accels_for_action(app, "win.zoom-in", zoom_in_accels);
    gtk_application_set_accels_for_action(app, "win.zoom-out", zoom_out_accels);
    gtk_application_set_accels_for_action(app, "win.zoom-best-fit", best_fit_accels);
    gtk_application_set_accels_for_action(app, "win.grab-input", grab_input_accels);
    gtk_application_set_accels_for_action(app, "win.show-menubar", show_menubar_accels);
    gtk_application_set_accels_for_action(app, "win.fullscreen", fullscreen_accels);
    gtk_application_set_accels_for_action(app, "win.quit", quit_accels);
}

static void on_app_activate(GtkApplication *app, GtkDisplayState *ds)
{
    VirtualConsole *vc;
    GtkIconTheme *theme;
    GdkDisplay *display;
    char *dir;

    g_set_prgname("qemu");


    ds->window = gtk_application_window_new(app);
    gtk_application_window_set_show_menubar(GTK_APPLICATION_WINDOW(ds->window),
                                            TRUE);
    ds->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    ds->notebook = gtk_notebook_new();
    GMenu *menu = gd_create_menus_models(ds, ds->opts);
    gtk_application_set_menubar(ds->app, G_MENU_MODEL(menu));
    setup_actions(ds, GTK_APPLICATION_WINDOW(ds->window));

    display = gtk_widget_get_display(ds->window);
    theme = gtk_icon_theme_get_for_display(display);
    dir = get_relocated_path(CONFIG_QEMU_ICONDIR);
    gtk_icon_theme_add_search_path(theme, dir);
    g_free(dir);

    ds->free_scale = FALSE;

    /* Mostly LC_MESSAGES only. See early_gtk_display_init() for details. For
     * LC_CTYPE, we need to make sure that non-ASCII characters are considered
     * printable, but without changing any of the character classes to make
     * sure that we don't accidentally break implicit assumptions.  */
    setlocale(LC_MESSAGES, "");
    setlocale(LC_CTYPE, "C.UTF-8");
    dir = get_relocated_path(CONFIG_QEMU_LOCALEDIR);
    bindtextdomain("qemu", dir);
    g_free(dir);
    bind_textdomain_codeset("qemu", "UTF-8");
    textdomain("qemu");

    if (ds->opts->has_show_cursor && ds->opts->show_cursor) {
        ds->null_cursor = NULL; /* default pointer */
    } else {
        ds->null_cursor = gdk_cursor_new_from_name("none", NULL);
    }

    ds->mouse_mode_notifier.notify = gd_mouse_mode_change;
    qemu_add_mouse_mode_change_notifier(&ds->mouse_mode_notifier);
    qemu_add_vm_change_state_handler(gd_change_runstate, ds);

    gtk_window_set_icon_name(GTK_WINDOW(ds->window), "qemu");

    gd_connect_signals(ds);

    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(ds->notebook), FALSE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(ds->notebook), FALSE);

    gd_update_caption(ds);

    gtk_widget_set_vexpand(ds->vbox, TRUE);
    gtk_box_append(GTK_BOX(ds->vbox), ds->notebook);

    gtk_window_set_child(GTK_WINDOW(ds->window), ds->vbox);

    gtk_window_present(GTK_WINDOW(ds->window));
    if (ds->opts->u.gtk.has_show_menubar &&
        !ds->opts->u.gtk.show_menubar) {
        gtk_application_window_set_show_menubar(GTK_APPLICATION_WINDOW(ds->window), 
                                                FALSE);
    }

    vc = gd_vc_find_current(ds);
    g_menu_item_set_attribute_value(ds->view_menu_item, "sensitive", g_variant_new_boolean(vc != NULL));
#ifdef CONFIG_VTE
    g_simple_action_set_enabled(ds->copy_action, vc && vc->type == GD_VC_VTE);
#endif

    if (ds->opts->has_full_screen &&
        ds->opts->full_screen) {
        g_action_activate(G_ACTION(ds->full_screen_action), NULL);
    }
    if (ds->opts->u.gtk.has_grab_on_hover &&
        ds->opts->u.gtk.grab_on_hover) {
        g_action_activate(G_ACTION(ds->grab_on_hover_action), g_variant_new_boolean(TRUE));
    }
    if (ds->opts->u.gtk.has_show_tabs &&
        ds->opts->u.gtk.show_tabs) {
        g_action_activate(G_ACTION(ds->show_tabs_action), g_variant_new_boolean(TRUE));
    }
#ifdef CONFIG_GTK_CLIPBOARD
    gd_clipboard_init(s);
#endif /* CONFIG_GTK_CLIPBOARD */
}

static void gtk_display_init(DisplayState *ds, DisplayOptions *opts)
{
    GtkDisplayState *s = g_malloc0(sizeof(*s));

    if (!gtkinit) {
        fprintf(stderr, "gtk initialization failed\n");
        exit(1);
    }
    assert(opts->type == DISPLAY_TYPE_GTK);
    s->opts = opts;
    s->app = gtk_application_new("org.qemu.qemu", G_APPLICATION_FLAGS_NONE);
    g_signal_connect(s->app, "startup",
                    G_CALLBACK(on_app_startup), s);
    g_signal_connect(s->app, "activate",
                    G_CALLBACK(on_app_activate), s);
    g_application_run (G_APPLICATION(s->app), 0, NULL);
}

static void early_gtk_display_init(DisplayOptions *opts)
{
    /* The QEMU code relies on the assumption that it's always run in
     * the C locale. Therefore it is not prepared to deal with
     * operations that produce different results depending on the
     * locale, such as printf's formatting of decimal numbers, and
     * possibly others.
     *
     * Since GTK+ calls setlocale() by default -importing the locale
     * settings from the environment- we must prevent it from doing so
     * using gtk_disable_setlocale().
     *
     * QEMU's GTK+ UI, however, _does_ have translations for some of
     * the menu items. As a trade-off between a functionally correct
     * QEMU and a fully internationalized UI we support importing
     * LC_MESSAGES from the environment (see the setlocale() call
     * earlier in this file). This allows us to display translated
     * messages leaving everything else untouched.
     */
    gtk_disable_setlocale();
    gtkinit = gtk_init_check();
    if (!gtkinit) {
        /* don't exit yet, that'll break -help */
        return;
    }

    assert(opts->type == DISPLAY_TYPE_GTK);
    if (opts->has_gl && opts->gl != DISPLAYGL_MODE_OFF) {
#if defined(CONFIG_OPENGL)
#if defined(GDK_WINDOWING_WAYLAND)
        if (GDK_IS_WAYLAND_DISPLAY(gdk_display_get_default())) {
            gtk_use_gl_area = true;
            gtk_gl_area_init();
        } else
#endif
        {
#ifdef CONFIG_X11
            DisplayGLMode mode = opts->has_gl ? opts->gl : DISPLAYGL_MODE_ON;
            gtk_egl_init(mode);
#endif
        }
#endif
    }

    keycode_map = gd_get_keymap(&keycode_maplen);

#if defined(CONFIG_VTE)
    type_register(&char_gd_vc_type_info);
#endif
}

static QemuDisplay qemu_display_gtk = {
    .type       = DISPLAY_TYPE_GTK,
    .early_init = early_gtk_display_init,
    .init       = gtk_display_init,
};

static void register_gtk(void)
{
    qemu_display_register(&qemu_display_gtk);
}

type_init(register_gtk);

#ifdef CONFIG_OPENGL
module_dep("ui-opengl");
#endif
