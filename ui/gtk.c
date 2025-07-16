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
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu-main.h"

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
#include "ui/input.h"
#include "system/runstate.h"
#include "system/system.h"
#include "keymaps.h"
#include "chardev/char.h"
#include "qom/object.h"

#define VC_WINDOW_X_MIN  320
#define VC_WINDOW_Y_MIN  240
#define VC_TERM_X_MIN     80
#define VC_TERM_Y_MIN     25
#define VC_SCALE_MIN    0.25
#define VC_SCALE_MAX       4
#define VC_SCALE_STEP   0.25

#ifdef GDK_WINDOWING_X11
#include "x_keymap.h"

/* Gtk2 compat */
#ifndef GDK_IS_X11_DISPLAY
#define GDK_IS_X11_DISPLAY(dpy) (dpy != NULL)
#endif
#endif


#ifdef GDK_WINDOWING_WAYLAND
/* Gtk2 compat */
#ifndef GDK_IS_WAYLAND_DISPLAY
#define GDK_IS_WAYLAND_DISPLAY(dpy) (dpy != NULL)
#endif
#endif


#ifdef GDK_WINDOWING_WIN32
/* Gtk2 compat */
#ifndef GDK_IS_WIN32_DISPLAY
#define GDK_IS_WIN32_DISPLAY(dpy) (dpy != NULL)
#endif
#endif


#ifdef GDK_WINDOWING_BROADWAY
/* Gtk2 compat */
#ifndef GDK_IS_BROADWAY_DISPLAY
#define GDK_IS_BROADWAY_DISPLAY(dpy) (dpy != NULL)
#endif
#endif


#ifdef GDK_WINDOWING_QUARTZ
/* Gtk2 compat */
#ifndef GDK_IS_QUARTZ_DISPLAY
#define GDK_IS_QUARTZ_DISPLAY(dpy) (dpy != NULL)
#endif
#endif


#if !defined(CONFIG_VTE)
# define VTE_CHECK_VERSION(a, b, c) 0
#endif

#define HOTKEY_MODIFIERS        (GDK_CONTROL_MASK | GDK_MOD1_MASK)

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

static struct touch_slot touch_slots[INPUT_EVENT_SLOTS_MAX];

bool gtk_use_gl_area;

static void gd_grab_pointer(VirtualConsole *vc, const char *reason);
static void gd_ungrab_pointer(GtkDisplayState *s);
static void gd_grab_keyboard(VirtualConsole *vc, const char *reason);
static void gd_ungrab_keyboard(GtkDisplayState *s);

/** Utility Functions **/

static VirtualConsole *gd_vc_find_by_menu(GtkDisplayState *s)
{
    VirtualConsole *vc;
    gint i;

    for (i = 0; i < s->nb_vcs; i++) {
        vc = &s->vc[i];
        if (gtk_check_menu_item_get_active
            (GTK_CHECK_MENU_ITEM(vc->menu_item))) {
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
    return gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(s->grab_item));
}

static bool gd_grab_on_hover(GtkDisplayState *s)
{
    return gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(s->grab_on_hover_item));
}

static void gd_update_cursor(VirtualConsole *vc)
{
    GtkDisplayState *s = vc->s;
    GdkWindow *window;

    if (vc->type != GD_VC_GFX ||
        !qemu_console_is_graphic(vc->gfx.dcl.con)) {
        return;
    }

    if (!gtk_widget_get_realized(vc->gfx.drawing_area)) {
        return;
    }

    window = gtk_widget_get_window(GTK_WIDGET(vc->gfx.drawing_area));
    if (s->full_screen || qemu_input_is_absolute(vc->gfx.dcl.con) || s->ptr_owner == vc) {
        gdk_window_set_cursor(window, s->null_cursor);
    } else {
        gdk_window_set_cursor(window, NULL);
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
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(s->pause_item),
                                   is_paused);
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
    GdkWindowHints mask = 0;
    GdkGeometry geo = {};
    GtkWidget *geo_widget = NULL;
    GtkWindow *geo_window;

    if (vc->type == GD_VC_GFX) {
        if (!vc->gfx.ds) {
            return;
        }
        double scale_x = s->free_scale ? VC_SCALE_MIN : vc->gfx.scale_x;
        double scale_y = s->free_scale ? VC_SCALE_MIN : vc->gfx.scale_y;
        geo.min_width  = surface_width(vc->gfx.ds) * scale_x;
        geo.min_height = surface_height(vc->gfx.ds) * scale_y;
        mask |= GDK_HINT_MIN_SIZE;
        geo_widget = vc->gfx.drawing_area;
        gtk_widget_set_size_request(geo_widget, geo.min_width, geo.min_height);

#if defined(CONFIG_VTE)
    } else if (vc->type == GD_VC_VTE) {
        VteTerminal *term = VTE_TERMINAL(vc->vte.terminal);
        GtkBorder padding = { 0 };

#if VTE_CHECK_VERSION(0, 37, 0)
        gtk_style_context_get_padding(
                gtk_widget_get_style_context(vc->vte.terminal),
                gtk_widget_get_state_flags(vc->vte.terminal),
                &padding);
#else
        {
            GtkBorder *ib = NULL;
            gtk_widget_style_get(vc->vte.terminal, "inner-border", &ib, NULL);
            if (ib) {
                padding = *ib;
                gtk_border_free(ib);
            }
        }
#endif

        geo.width_inc  = vte_terminal_get_char_width(term);
        geo.height_inc = vte_terminal_get_char_height(term);
        mask |= GDK_HINT_RESIZE_INC;
        geo.base_width  = geo.width_inc;
        geo.base_height = geo.height_inc;
        mask |= GDK_HINT_BASE_SIZE;
        geo.min_width  = geo.width_inc * VC_TERM_X_MIN;
        geo.min_height = geo.height_inc * VC_TERM_Y_MIN;
        mask |= GDK_HINT_MIN_SIZE;

        geo.base_width  += padding.left + padding.right;
        geo.base_height += padding.top + padding.bottom;
        geo.min_width   += padding.left + padding.right;
        geo.min_height  += padding.top + padding.bottom;
        geo_widget = vc->vte.terminal;
#endif
    }

    geo_window = GTK_WINDOW(vc->window ? vc->window : s->window);
    gtk_window_set_geometry_hints(geo_window, geo_widget, &geo, mask);
}

void gd_update_windowsize(VirtualConsole *vc)
{
    GtkDisplayState *s = vc->s;

    gd_update_geometry_hints(vc);

    if (vc->type == GD_VC_GFX && !s->full_screen && !s->free_scale) {
        gtk_window_resize(GTK_WINDOW(vc->window ? vc->window : s->window),
                          VC_WINDOW_X_MIN, VC_WINDOW_Y_MIN);
    }
}

static void gd_update_full_redraw(VirtualConsole *vc)
{
    GtkWidget *area = vc->gfx.drawing_area;
    int ww, wh;
    ww = gdk_window_get_width(gtk_widget_get_window(area));
    wh = gdk_window_get_height(gtk_widget_get_window(area));
#if defined(CONFIG_OPENGL)
    if (vc->gfx.gls && gtk_use_gl_area) {
        gtk_gl_area_queue_render(GTK_GL_AREA(vc->gfx.drawing_area));
        return;
    }
#endif
    gtk_widget_queue_draw_area(area, 0, 0, ww, wh);
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
    gtk_container_remove(GTK_CONTAINER(from), widget);
    gtk_container_add(GTK_CONTAINER(to), widget);
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
                      int fbx, int fby, int fbw, int fbh)
{
    VirtualConsole *vc = container_of(dcl, VirtualConsole, gfx.dcl);
    GdkWindow *win;
    int wx1, wx2, wy1, wy2;
    int wx_offset, wy_offset;
    int ww_surface, wh_surface;
    int ww_widget, wh_widget;

    trace_gd_update(vc->label, fbx, fby, fbw, fbh);

    if (!gtk_widget_get_realized(vc->gfx.drawing_area)) {
        return;
    }

    if (vc->gfx.convert) {
        pixman_image_composite(PIXMAN_OP_SRC, vc->gfx.ds->image,
                               NULL, vc->gfx.convert,
                               fbx, fby, 0, 0, fbx, fby, fbw, fbh);
    }

    wx1 = floor(fbx * vc->gfx.scale_x);
    wy1 = floor(fby * vc->gfx.scale_y);

    wx2 = ceil(fbx * vc->gfx.scale_x + fbw * vc->gfx.scale_x);
    wy2 = ceil(fby * vc->gfx.scale_y + fbh * vc->gfx.scale_y);

    ww_surface = surface_width(vc->gfx.ds) * vc->gfx.scale_x;
    wh_surface = surface_height(vc->gfx.ds) * vc->gfx.scale_y;

    win = gtk_widget_get_window(vc->gfx.drawing_area);
    if (!win) {
        return;
    }
    ww_widget = gdk_window_get_width(win);
    wh_widget = gdk_window_get_height(win);

    wx_offset = wy_offset = 0;
    if (ww_widget > ww_surface) {
        wx_offset = (ww_widget - ww_surface) / 2;
    }
    if (wh_widget > wh_surface) {
        wy_offset = (wh_widget - wh_surface) / 2;
    }

    gtk_widget_queue_draw_area(vc->gfx.drawing_area,
                               wx_offset + wx1, wy_offset + wy1,
                               (wx2 - wx1), (wy2 - wy1));
}

static void gd_refresh(DisplayChangeListener *dcl)
{
    graphic_hw_update(dcl->con);
}

static GdkDevice *gd_get_pointer(GdkDisplay *dpy)
{
    return gdk_seat_get_pointer(gdk_display_get_default_seat(dpy));
}

static void gd_mouse_set(DisplayChangeListener *dcl,
                         int x, int y, bool visible)
{
    VirtualConsole *vc = container_of(dcl, VirtualConsole, gfx.dcl);
    GdkDisplay *dpy;
    gint x_root, y_root;

    if (!gtk_widget_get_realized(vc->gfx.drawing_area) ||
        qemu_input_is_absolute(dcl->con)) {
        return;
    }

    dpy = gtk_widget_get_display(vc->gfx.drawing_area);
    gdk_window_get_root_coords(gtk_widget_get_window(vc->gfx.drawing_area),
                               x, y, &x_root, &y_root);
    gdk_device_warp(gd_get_pointer(dpy),
                    gtk_widget_get_screen(vc->gfx.drawing_area),
                    x_root, y_root);
    vc->s->last_x = x;
    vc->s->last_y = y;
}

static void gd_cursor_define(DisplayChangeListener *dcl,
                             QEMUCursor *c)
{
    VirtualConsole *vc = container_of(dcl, VirtualConsole, gfx.dcl);
    GdkPixbuf *pixbuf;
    GdkCursor *cursor;

    if (!gtk_widget_get_realized(vc->gfx.drawing_area)) {
        return;
    }

    pixbuf = gdk_pixbuf_new_from_data((guchar *)(c->data),
                                      GDK_COLORSPACE_RGB, true, 8,
                                      c->width, c->height, c->width * 4,
                                      NULL, NULL);
    cursor = gdk_cursor_new_from_pixbuf
        (gtk_widget_get_display(vc->gfx.drawing_area),
         pixbuf, c->hot_x, c->hot_y);
    gdk_window_set_cursor(gtk_widget_get_window(vc->gfx.drawing_area), cursor);
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

    if (surface_format(surface) == PIXMAN_x8r8g8b8) {
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
    VirtualConsole *vc = container_of(dcl, VirtualConsole, gfx.dcl);

    egl_dmabuf_release_texture(dmabuf);
    if (vc->gfx.guest_fb.dmabuf == dmabuf) {
        vc->gfx.guest_fb.dmabuf = NULL;
    }
#endif
}

void gd_hw_gl_flushed(void *vcon)
{
    VirtualConsole *vc = vcon;
    QemuDmaBuf *dmabuf = vc->gfx.guest_fb.dmabuf;
    int fence_fd;

    fence_fd = qemu_dmabuf_get_fence_fd(dmabuf);
    if (fence_fd >= 0) {
        qemu_set_fd_handler(fence_fd, NULL, NULL, NULL);
        close(fence_fd);
        qemu_dmabuf_set_fence_fd(dmabuf, -1);
        graphic_hw_gl_block(vc->gfx.dcl.con, false);
    }
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
    if (s->ptr_owner && qemu_input_is_absolute(s->ptr_owner->gfx.dcl.con)) {
        if (!s->ptr_owner->window) {
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(s->grab_item),
                                           FALSE);
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

static gboolean gd_window_close(GtkWidget *widget, GdkEvent *event,
                                void *opaque)
{
    GtkDisplayState *s = opaque;
    bool allow_close = true;

    if (s->opts->has_window_close && !s->opts->window_close) {
        allow_close = false;
    }

    if (allow_close) {
        qmp_quit(NULL);
    }

    return TRUE;
}

static void gd_set_ui_refresh_rate(VirtualConsole *vc, int refresh_rate)
{
    QemuUIInfo info;

    if (!dpy_ui_info_supported(vc->gfx.dcl.con)) {
        return;
    }

    info = *dpy_get_ui_info(vc->gfx.dcl.con);
    info.refresh_rate = refresh_rate;
    dpy_set_ui_info(vc->gfx.dcl.con, &info, true);
}

static void gd_set_ui_size(VirtualConsole *vc, gint width, gint height)
{
    QemuUIInfo info;

    if (!dpy_ui_info_supported(vc->gfx.dcl.con)) {
        return;
    }

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
    double pw = width, ph = height;
    double sx = vc->gfx.scale_x, sy = vc->gfx.scale_y;
    GdkWindow *window = gtk_widget_get_window(GTK_WIDGET(area));
    const int gs = gdk_window_get_scale_factor(window);

    if (!vc->s->free_scale && !vc->s->full_screen) {
        pw /= sx;
        ph /= sy;
    }

    /**
     * width and height here are in pixel coordinate, so we must divide it
     * by global window scale (gs)
     */
    gd_set_ui_size(vc, pw / gs, ph / gs);
}

#endif

void gd_update_monitor_refresh_rate(VirtualConsole *vc, GtkWidget *widget)
{
#ifdef GDK_VERSION_3_22
    GdkWindow *win = gtk_widget_get_window(widget);
    int refresh_rate;

    if (win) {
        GdkDisplay *dpy = gtk_widget_get_display(widget);
        GdkMonitor *monitor = gdk_display_get_monitor_at_window(dpy, win);
        refresh_rate = gdk_monitor_get_refresh_rate(monitor); /* [mHz] */
    } else {
        refresh_rate = 0;
    }

    gd_set_ui_refresh_rate(vc, refresh_rate);

    /* T = 1 / f = 1 [s*Hz] / f = 1000*1000 [ms*mHz] / f */
    vc->gfx.dcl.update_interval = refresh_rate ?
        MIN(1000 * 1000 / refresh_rate, GUI_REFRESH_INTERVAL_DEFAULT) :
        GUI_REFRESH_INTERVAL_DEFAULT;
#endif
}

void gd_update_scale(VirtualConsole *vc, int ww, int wh, int fbw, int fbh)
{
    if (!vc) {
        return;
    }

    if (vc->s->full_screen) {
        vc->gfx.scale_x = (double)ww / fbw;
        vc->gfx.scale_y = (double)wh / fbh;
    } else if (vc->s->free_scale) {
        double sx, sy;

        sx = (double)ww / fbw;
        sy = (double)wh / fbh;
        if (vc->s->keep_aspect_ratio) {
            vc->gfx.scale_x = vc->gfx.scale_y = MIN(sx, sy);
        } else {
            vc->gfx.scale_x = sx;
            vc->gfx.scale_y = sy;
        }
    }
}
/**
 * DOC: Coordinate handling.
 *
 * We are coping with sizes and positions in various coordinates and the
 * handling of these coordinates is somewhat confusing. It would benefit us
 * all if we define these coordinates explicitly and clearly. Besides, it's
 * also helpful to follow the same naming convention for variables
 * representing values in different coordinates.
 *
 * I. Definitions
 *
 * - (guest) buffer coordinate: this is the coordinates that the guest will
 *   see. The x/y offsets and width/height specified in commands sent by
 *   guest is basically in buffer coordinate.
 *
 * - (host) pixel coordinate: this is the coordinate in pixel level on the
 *   host destop. A window/widget of width 300 in pixel coordinate means it
 *   occupies 300 pixels horizontally.
 *
 * - (host) logical window coordinate: the existence of global scaling
 *   factor in desktop level makes this kind of coordinate play a role. It
 *   always holds that (logical window size) * (global scale factor) =
 *   (pixel size).
 *
 * - global scale factor: this is specified in desktop level and is
 *   typically invariant during the life cycle of the process. Users with
 *   high-DPI monitors might set this scale, for example, to 2, in order to
 *   make the UI look larger.
 *
 * - zooming scale: this can be freely controlled by the QEMU user to zoom
 *   in/out the guest content.
 *
 * II. Representation
 *
 * We'd like to use consistent representation for variables in different
 * coordinates:
 * - buffer coordinate: prefix fb
 * - pixel coordinate: prefix p
 * - logical window coordinate: prefix w
 *
 * For scales:
 * - global scale factor: prefix gs
 * - zooming scale: prefix scale/s
 *
 * Example: fbw, pw, ww for width in different coordinates
 *
 * III. Equation
 *
 * - fbw * gs * scale_x = pw
 * - pw = gs * ww
 *
 * Consequently we have
 *
 * - fbw * scale_x = ww
 *
 * Example: assuming we are running QEMU on a 3840x2160 screen and have set
 * global scaling factor to 2, if the guest buffer size is 1920x1080 and the
 * zooming scale is 0.5, then we have:
 * - fbw = 1920, fbh = 1080
 * - pw  = 1920, ph  = 1080
 * - ww  = 960,  wh  = 540
 * A bonus of this configuration is that we can achieve pixel to pixel
 * presentation of the guest content.
 */

static gboolean gd_draw_event(GtkWidget *widget, cairo_t *cr, void *opaque)
{
    VirtualConsole *vc = opaque;
    GtkDisplayState *s = vc->s;
    int wx_offset, wy_offset;
    int ww_widget, wh_widget, ww_surface, wh_surface;
    int fbw, fbh;

#if defined(CONFIG_OPENGL)
    if (vc->gfx.gls) {
        if (gtk_use_gl_area) {
            /* invoke render callback please */
            return FALSE;
        } else {
#ifdef CONFIG_X11
            gd_egl_draw(vc);
            return TRUE;
#else
            abort();
#endif
        }
    }
#endif

    if (!gtk_widget_get_realized(widget)) {
        return FALSE;
    }
    if (!vc->gfx.ds) {
        return FALSE;
    }
    if (!vc->gfx.surface) {
        return FALSE;
    }

    gd_update_monitor_refresh_rate(vc, vc->window ? vc->window : s->window);

    fbw = surface_width(vc->gfx.ds);
    fbh = surface_height(vc->gfx.ds);

    ww_widget = gdk_window_get_width(gtk_widget_get_window(widget));
    wh_widget = gdk_window_get_height(gtk_widget_get_window(widget));

    gd_update_scale(vc, ww_widget, wh_widget, fbw, fbh);

    ww_surface = fbw * vc->gfx.scale_x;
    wh_surface = fbh * vc->gfx.scale_y;

    wx_offset = wy_offset = 0;
    if (ww_widget > ww_surface) {
        wx_offset = (ww_widget - ww_surface) / 2;
    }
    if (wh_widget > wh_surface) {
        wy_offset = (wh_widget - wh_surface) / 2;
    }

    cairo_rectangle(cr, 0, 0, ww_widget, wh_widget);

    /* Optionally cut out the inner area where the pixmap
       will be drawn. This avoids 'flashing' since we're
       not double-buffering. Note we're using the undocumented
       behaviour of drawing the rectangle from right to left
       to cut out the whole */
    cairo_rectangle(cr, wx_offset + ww_surface, wy_offset,
                    -1 * ww_surface, wh_surface);
    cairo_fill(cr);

    cairo_scale(cr, vc->gfx.scale_x, vc->gfx.scale_y);
    cairo_set_source_surface(cr, vc->gfx.surface,
                             wx_offset / vc->gfx.scale_x,
                             wy_offset / vc->gfx.scale_y);
    cairo_paint(cr);

    return TRUE;
}

static gboolean gd_motion_event(GtkWidget *widget, GdkEventMotion *motion,
                                void *opaque)
{
    VirtualConsole *vc = opaque;
    GtkDisplayState *s = vc->s;
    int fbx, fby;
    int wx_offset, wy_offset;
    int wh_surface, ww_surface;
    int ww_widget, wh_widget;

    if (!vc->gfx.ds) {
        return TRUE;
    }

    ww_surface = surface_width(vc->gfx.ds) * vc->gfx.scale_x;
    wh_surface = surface_height(vc->gfx.ds) * vc->gfx.scale_y;
    ww_widget = gtk_widget_get_allocated_width(widget);
    wh_widget = gtk_widget_get_allocated_height(widget);

    /*
     * `widget` may not have the same size with the frame buffer.
     * In such cases, some paddings are needed around the `vc`.
     * To achieve that, `vc` will be displayed at (mx, my)
     * so that it is displayed at the center of the widget.
     */
    wx_offset = wy_offset = 0;
    if (ww_widget > ww_surface) {
        wx_offset = (ww_widget - ww_surface) / 2;
    }
    if (wh_widget > wh_surface) {
        wy_offset = (wh_widget - wh_surface) / 2;
    }

    /*
     * `motion` is reported in `widget` coordinates
     * so translating it to the coordinates in `vc`.
     */
    fbx = (motion->x - wx_offset) / vc->gfx.scale_x;
    fby = (motion->y - wy_offset) / vc->gfx.scale_y;

    trace_gd_motion_event(ww_widget, wh_widget,
                          gtk_widget_get_scale_factor(widget), fbx, fby);

    if (qemu_input_is_absolute(vc->gfx.dcl.con)) {
        if (fbx < 0 || fby < 0 ||
            fbx >= surface_width(vc->gfx.ds) ||
            fby >= surface_height(vc->gfx.ds)) {
            return TRUE;
        }
        qemu_input_queue_abs(vc->gfx.dcl.con, INPUT_AXIS_X, fbx,
                             0, surface_width(vc->gfx.ds));
        qemu_input_queue_abs(vc->gfx.dcl.con, INPUT_AXIS_Y, fby,
                             0, surface_height(vc->gfx.ds));
        qemu_input_event_sync();
    } else if (s->last_set && s->ptr_owner == vc) {
        qemu_input_queue_rel(vc->gfx.dcl.con, INPUT_AXIS_X, fbx - s->last_x);
        qemu_input_queue_rel(vc->gfx.dcl.con, INPUT_AXIS_Y, fby - s->last_y);
        qemu_input_event_sync();
    }
    s->last_x = fbx;
    s->last_y = fby;
    s->last_set = TRUE;

    if (!qemu_input_is_absolute(vc->gfx.dcl.con) && s->ptr_owner == vc) {
        GdkScreen *screen = gtk_widget_get_screen(vc->gfx.drawing_area);
        GdkDisplay *dpy = gtk_widget_get_display(widget);
        GdkWindow *win = gtk_widget_get_window(widget);
        GdkMonitor *monitor = gdk_display_get_monitor_at_window(dpy, win);
        GdkRectangle geometry;

        int xr = (int)motion->x_root;
        int yr = (int)motion->y_root;

        gdk_monitor_get_geometry(monitor, &geometry);

        /* In relative mode check to see if client pointer hit
         * one of the monitor edges, and if so move it back to the
         * center of the monitor. This is important because the pointer
         * in the server doesn't correspond 1-for-1, and so
         * may still be only half way across the screen. Without
         * this warp, the server pointer would thus appear to hit
         * an invisible wall */
        if (xr <= geometry.x || xr - geometry.x >= geometry.width - 1 ||
            yr <= geometry.y || yr - geometry.y >= geometry.height - 1) {
            GdkDevice *dev = gdk_event_get_device((GdkEvent *)motion);
            xr = geometry.x + geometry.width / 2;
            yr = geometry.y + geometry.height / 2;

            gdk_device_warp(dev, screen, xr, yr);
            s->last_set = FALSE;
            return FALSE;
        }
    }
    return TRUE;
}

static gboolean gd_button_event(GtkWidget *widget, GdkEventButton *button,
                                void *opaque)
{
    VirtualConsole *vc = opaque;
    GtkDisplayState *s = vc->s;
    InputButton btn;

    /* implicitly grab the input at the first click in the relative mode */
    if (button->button == 1 && button->type == GDK_BUTTON_PRESS &&
        !qemu_input_is_absolute(vc->gfx.dcl.con) && s->ptr_owner != vc) {
        if (!vc->window) {
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(s->grab_item),
                                           TRUE);
        } else {
            gd_grab_pointer(vc, "relative-mode-click");
        }
        return TRUE;
    }

    if (button->button == 1) {
        btn = INPUT_BUTTON_LEFT;
    } else if (button->button == 2) {
        btn = INPUT_BUTTON_MIDDLE;
    } else if (button->button == 3) {
        btn = INPUT_BUTTON_RIGHT;
    } else if (button->button == 8) {
        btn = INPUT_BUTTON_SIDE;
    } else if (button->button == 9) {
        btn = INPUT_BUTTON_EXTRA;
    } else {
        return TRUE;
    }

    if (button->type == GDK_2BUTTON_PRESS || button->type == GDK_3BUTTON_PRESS) {
        return TRUE;
    }

    qemu_input_queue_btn(vc->gfx.dcl.con, btn,
                         button->type == GDK_BUTTON_PRESS);
    qemu_input_event_sync();
    return TRUE;
}

static gboolean gd_scroll_event(GtkWidget *widget, GdkEventScroll *scroll,
                                void *opaque)
{
    VirtualConsole *vc = opaque;
    InputButton btn_vertical;
    InputButton btn_horizontal;
    bool has_vertical = false;
    bool has_horizontal = false;

    if (scroll->direction == GDK_SCROLL_UP) {
        btn_vertical = INPUT_BUTTON_WHEEL_UP;
        has_vertical = true;
    } else if (scroll->direction == GDK_SCROLL_DOWN) {
        btn_vertical = INPUT_BUTTON_WHEEL_DOWN;
        has_vertical = true;
    } else if (scroll->direction == GDK_SCROLL_LEFT) {
        btn_horizontal = INPUT_BUTTON_WHEEL_LEFT;
        has_horizontal = true;
    } else if (scroll->direction == GDK_SCROLL_RIGHT) {
        btn_horizontal = INPUT_BUTTON_WHEEL_RIGHT;
        has_horizontal = true;
    } else if (scroll->direction == GDK_SCROLL_SMOOTH) {
        gdouble delta_x, delta_y;
        if (!gdk_event_get_scroll_deltas((GdkEvent *)scroll,
                                         &delta_x, &delta_y)) {
            return TRUE;
        }

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


static gboolean gd_touch_event(GtkWidget *widget, GdkEventTouch *touch,
                               void *opaque)
{
    VirtualConsole *vc = opaque;
    uint64_t num_slot = GPOINTER_TO_UINT(touch->sequence);
    int type = -1;

    switch (touch->type) {
    case GDK_TOUCH_BEGIN:
        type = INPUT_MULTI_TOUCH_TYPE_BEGIN;
        break;
    case GDK_TOUCH_UPDATE:
        type = INPUT_MULTI_TOUCH_TYPE_UPDATE;
        break;
    case GDK_TOUCH_END:
    case GDK_TOUCH_CANCEL:
        type = INPUT_MULTI_TOUCH_TYPE_END;
        break;
    default:
        warn_report("gtk: unexpected touch event type\n");
        return FALSE;
    }

    console_handle_touch_event(vc->gfx.dcl.con, touch_slots,
                               num_slot, surface_width(vc->gfx.ds),
                               surface_height(vc->gfx.ds), touch->x,
                               touch->y, type, &error_warn);
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

static int gd_get_keycode(GdkEventKey *key)
{
#ifdef G_OS_WIN32
    int scancode = gdk_event_get_scancode((GdkEvent *)key);

    /* translate Windows native scancodes to atset1 keycodes */
    switch (scancode & (KF_EXTENDED | 0xff)) {
    case 0x145:     /* NUMLOCK */
        return scancode & 0xff;
    }

    return scancode & KF_EXTENDED ?
        0xe000 | (scancode & 0xff) : scancode & 0xff;

#else
    return key->hardware_keycode;
#endif
}

static gboolean gd_text_key_down(GtkWidget *widget,
                                 GdkEventKey *key, void *opaque)
{
    VirtualConsole *vc = opaque;
    QemuTextConsole *con = QEMU_TEXT_CONSOLE(vc->gfx.dcl.con);

    if (key->keyval == GDK_KEY_Delete) {
        qemu_text_console_put_qcode(con, Q_KEY_CODE_DELETE, false);
    } else if (key->length) {
        qemu_text_console_put_string(con, key->string, key->length);
    } else {
        int qcode = gd_map_keycode(gd_get_keycode(key));
        qemu_text_console_put_qcode(con, qcode, false);
    }
    return TRUE;
}

static gboolean gd_key_event(GtkWidget *widget, GdkEventKey *key, void *opaque)
{
    VirtualConsole *vc = opaque;
    int keycode, qcode;

#ifdef G_OS_WIN32
    /* on windows, we ought to ignore the reserved key event? */
    if (key->hardware_keycode == 0xff)
        return false;

    if (!vc->s->kbd_owner) {
        if (key->hardware_keycode == VK_LWIN ||
            key->hardware_keycode == VK_RWIN) {
            return FALSE;
        }
    }
#endif

    if (key->keyval == GDK_KEY_Pause
#ifdef G_OS_WIN32
        /* for some reason GDK does not fill keyval for VK_PAUSE
         * See https://bugzilla.gnome.org/show_bug.cgi?id=769214
         */
        || key->hardware_keycode == VK_PAUSE
#endif
        ) {
        qkbd_state_key_event(vc->gfx.kbd, Q_KEY_CODE_PAUSE,
                             key->type == GDK_KEY_PRESS);
        return TRUE;
    }

    keycode = gd_get_keycode(key);
    qcode = gd_map_keycode(keycode);

    trace_gd_key_event(vc->label, keycode, qcode,
                       (key->type == GDK_KEY_PRESS) ? "down" : "up");

    qkbd_state_key_event(vc->gfx.kbd, qcode,
                         key->type == GDK_KEY_PRESS);

    return TRUE;
}

static gboolean gd_grab_broken_event(GtkWidget *widget,
                                     GdkEventGrabBroken *event, void *opaque)
{
#ifdef CONFIG_WIN32
    /*
     * On Windows the Ctrl-Alt-Del key combination can't be grabbed. This
     * key combination leaves all three keys in a stuck condition. We use
     * the grab-broken-event to release all keys.
     */
    if (event->keyboard) {
        VirtualConsole *vc = opaque;
        GtkDisplayState *s = vc->s;

        gtk_release_modifiers(s);
    }
#endif
    return TRUE;
}

static gboolean gd_event(GtkWidget *widget, GdkEvent *event, void *opaque)
{
    if (event->type == GDK_MOTION_NOTIFY) {
        return gd_motion_event(widget, &event->motion, opaque);
    }
    return FALSE;
}

/** Window Menu Actions **/

static void gd_menu_pause(GtkMenuItem *item, void *opaque)
{
    GtkDisplayState *s = opaque;

    if (s->external_pause_update) {
        return;
    }
    if (runstate_is_running()) {
        qmp_stop(NULL);
    } else {
        qmp_cont(NULL);
    }
}

static void gd_menu_reset(GtkMenuItem *item, void *opaque)
{
    qmp_system_reset(NULL);
}

static void gd_menu_powerdown(GtkMenuItem *item, void *opaque)
{
    qmp_system_powerdown(NULL);
}

static void gd_menu_quit(GtkMenuItem *item, void *opaque)
{
    qmp_quit(NULL);
}

static void gd_menu_switch_vc(GtkMenuItem *item, void *opaque)
{
    GtkDisplayState *s = opaque;
    VirtualConsole *vc = gd_vc_find_by_menu(s);
    GtkNotebook *nb = GTK_NOTEBOOK(s->notebook);
    gint page;

    gtk_release_modifiers(s);
    if (vc) {
        page = gtk_notebook_page_num(nb, vc->tab_item);
        gtk_notebook_set_current_page(nb, page);
        gtk_widget_grab_focus(vc->focus);
    }
}

static void gd_accel_switch_vc(void *opaque)
{
    VirtualConsole *vc = opaque;

    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(vc->menu_item), TRUE);
}

static void gd_menu_show_tabs(GtkMenuItem *item, void *opaque)
{
    GtkDisplayState *s = opaque;
    VirtualConsole *vc = gd_vc_find_current(s);

    if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(s->show_tabs_item))) {
        gtk_notebook_set_show_tabs(GTK_NOTEBOOK(s->notebook), TRUE);
    } else {
        gtk_notebook_set_show_tabs(GTK_NOTEBOOK(s->notebook), FALSE);
    }
    gd_update_windowsize(vc);
}

static gboolean gd_tab_window_close(GtkWidget *widget, GdkEvent *event,
                                    void *opaque)
{
    VirtualConsole *vc = opaque;
    GtkDisplayState *s = vc->s;

    gtk_widget_set_sensitive(vc->menu_item, true);
    gd_widget_reparent(vc->window, s->notebook, vc->tab_item);
    gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(s->notebook),
                                    vc->tab_item, vc->label);
    gtk_widget_destroy(vc->window);
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

static gboolean gd_win_grab(void *opaque)
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

static void gd_menu_untabify(GtkMenuItem *item, void *opaque)
{
    GtkDisplayState *s = opaque;
    VirtualConsole *vc = gd_vc_find_current(s);

    if (vc->type == GD_VC_GFX &&
        qemu_console_is_graphic(vc->gfx.dcl.con)) {
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(s->grab_item),
                                       FALSE);
    }
    if (!vc->window) {
        gtk_widget_set_sensitive(vc->menu_item, false);
        vc->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
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
        gd_widget_reparent(s->notebook, vc->window, vc->tab_item);

        g_signal_connect(vc->window, "delete-event",
                         G_CALLBACK(gd_tab_window_close), vc);
        gtk_widget_show_all(vc->window);

        if (qemu_console_is_graphic(vc->gfx.dcl.con)) {
            GtkAccelGroup *ag = gtk_accel_group_new();
            gtk_window_add_accel_group(GTK_WINDOW(vc->window), ag);

            GClosure *cb = g_cclosure_new_swap(G_CALLBACK(gd_win_grab),
                                               vc, NULL);
            gtk_accel_group_connect(ag, GDK_KEY_g, HOTKEY_MODIFIERS, 0, cb);
        }

        gd_update_geometry_hints(vc);
        gd_update_caption(s);
    }
}

static void gd_menu_show_menubar(GtkMenuItem *item, void *opaque)
{
    GtkDisplayState *s = opaque;
    VirtualConsole *vc = gd_vc_find_current(s);

    if (s->full_screen) {
        return;
    }

    if (gtk_check_menu_item_get_active(
                GTK_CHECK_MENU_ITEM(s->show_menubar_item))) {
        gtk_widget_show(s->menu_bar);
    } else {
        gtk_widget_hide(s->menu_bar);
    }
    gd_update_windowsize(vc);
}

static void gd_accel_show_menubar(void *opaque)
{
    GtkDisplayState *s = opaque;
    gtk_menu_item_activate(GTK_MENU_ITEM(s->show_menubar_item));
}

static void gd_menu_full_screen(GtkMenuItem *item, void *opaque)
{
    GtkDisplayState *s = opaque;
    VirtualConsole *vc = gd_vc_find_current(s);

    if (!s->full_screen) {
        gtk_notebook_set_show_tabs(GTK_NOTEBOOK(s->notebook), FALSE);
        gtk_widget_hide(s->menu_bar);
        if (vc->type == GD_VC_GFX) {
            gtk_widget_set_size_request(vc->gfx.drawing_area, -1, -1);
        }
        gtk_window_fullscreen(GTK_WINDOW(s->window));
        s->full_screen = TRUE;
    } else {
        gtk_window_unfullscreen(GTK_WINDOW(s->window));
        gd_menu_show_tabs(GTK_MENU_ITEM(s->show_tabs_item), s);
        if (gtk_check_menu_item_get_active(
                    GTK_CHECK_MENU_ITEM(s->show_menubar_item))) {
            gtk_widget_show(s->menu_bar);
        }
        s->full_screen = FALSE;
        if (vc->type == GD_VC_GFX) {
            vc->gfx.scale_x = vc->gfx.preferred_scale;
            vc->gfx.scale_y = vc->gfx.preferred_scale;
            gd_update_windowsize(vc);
        }
    }

    gd_update_cursor(vc);
}

static void gd_accel_full_screen(void *opaque)
{
    GtkDisplayState *s = opaque;
    gtk_menu_item_activate(GTK_MENU_ITEM(s->full_screen_item));
}

static void gd_menu_zoom_in(GtkMenuItem *item, void *opaque)
{
    GtkDisplayState *s = opaque;
    VirtualConsole *vc = gd_vc_find_current(s);

    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(s->zoom_fit_item),
                                   FALSE);

    vc->gfx.scale_x += VC_SCALE_STEP;
    vc->gfx.scale_y += VC_SCALE_STEP;

    gd_update_windowsize(vc);
}

static void gd_accel_zoom_in(void *opaque)
{
    GtkDisplayState *s = opaque;
    gtk_menu_item_activate(GTK_MENU_ITEM(s->zoom_in_item));
}

static void gd_menu_zoom_out(GtkMenuItem *item, void *opaque)
{
    GtkDisplayState *s = opaque;
    VirtualConsole *vc = gd_vc_find_current(s);

    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(s->zoom_fit_item),
                                   FALSE);

    vc->gfx.scale_x -= VC_SCALE_STEP;
    vc->gfx.scale_y -= VC_SCALE_STEP;

    vc->gfx.scale_x = MAX(vc->gfx.scale_x, VC_SCALE_MIN);
    vc->gfx.scale_y = MAX(vc->gfx.scale_y, VC_SCALE_MIN);

    gd_update_windowsize(vc);
}

static void gd_menu_zoom_fixed(GtkMenuItem *item, void *opaque)
{
    GtkDisplayState *s = opaque;
    VirtualConsole *vc = gd_vc_find_current(s);

    vc->gfx.scale_x = vc->gfx.preferred_scale;
    vc->gfx.scale_y = vc->gfx.preferred_scale;

    gd_update_windowsize(vc);
}

static void gd_menu_zoom_fit(GtkMenuItem *item, void *opaque)
{
    GtkDisplayState *s = opaque;
    VirtualConsole *vc = gd_vc_find_current(s);

    if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(s->zoom_fit_item))) {
        s->free_scale = TRUE;
    } else {
        s->free_scale = FALSE;
        vc->gfx.scale_x = vc->gfx.preferred_scale;
        vc->gfx.scale_y = vc->gfx.preferred_scale;
    }

    gd_update_windowsize(vc);
    gd_update_full_redraw(vc);
}

static void gd_grab_update(VirtualConsole *vc, bool kbd, bool ptr)
{
    GdkDisplay *display = gtk_widget_get_display(vc->gfx.drawing_area);
    GdkSeat *seat = gdk_display_get_default_seat(display);
    GdkWindow *window = gtk_widget_get_window(vc->gfx.drawing_area);
    GdkSeatCapabilities caps = 0;
    GdkCursor *cursor = NULL;

    if (kbd) {
        caps |= GDK_SEAT_CAPABILITY_KEYBOARD;
    }
    if (ptr) {
        caps |= GDK_SEAT_CAPABILITY_ALL_POINTING;
        cursor = vc->s->null_cursor;
    }

    if (caps) {
        gdk_seat_grab(seat, window, caps, false, cursor,
                      NULL, NULL, NULL);
    } else {
        gdk_seat_ungrab(seat);
    }
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
    GdkDisplay *display = gtk_widget_get_display(vc->gfx.drawing_area);

    if (vc->s->ptr_owner) {
        if (vc->s->ptr_owner == vc) {
            return;
        } else {
            gd_ungrab_pointer(vc->s);
        }
    }

    gd_grab_update(vc, vc->s->kbd_owner == vc, true);
    gdk_device_get_position(gd_get_pointer(display),
                            NULL, &vc->s->grab_x_root, &vc->s->grab_y_root);
    vc->s->ptr_owner = vc;
    gd_update_caption(vc->s);
    trace_gd_grab(vc->label, "ptr", reason);
}

static void gd_ungrab_pointer(GtkDisplayState *s)
{
    VirtualConsole *vc = s->ptr_owner;
    GdkDisplay *display;

    if (vc == NULL) {
        return;
    }
    s->ptr_owner = NULL;

    display = gtk_widget_get_display(vc->gfx.drawing_area);
    gd_grab_update(vc, vc->s->kbd_owner == vc, false);
    gdk_device_warp(gd_get_pointer(display),
                    gtk_widget_get_screen(vc->gfx.drawing_area),
                    vc->s->grab_x_root, vc->s->grab_y_root);
    gd_update_caption(s);
    trace_gd_ungrab(vc->label, "ptr");
}

static void gd_menu_grab_input(GtkMenuItem *item, void *opaque)
{
    GtkDisplayState *s = opaque;
    VirtualConsole *vc = gd_vc_find_current(s);

    if (gd_is_grab_active(s)) {
        gd_grab_keyboard(vc, "user-request-main-window");
        gd_grab_pointer(vc, "user-request-main-window");
    } else {
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
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(vc->menu_item),
                                   TRUE);
    on_vga = (vc->type == GD_VC_GFX &&
              qemu_console_is_graphic(vc->gfx.dcl.con));
    if (!on_vga) {
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(s->grab_item),
                                       FALSE);
    } else if (s->full_screen) {
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(s->grab_item),
                                       TRUE);
    }
    gtk_widget_set_sensitive(s->grab_item, on_vga);
#ifdef CONFIG_VTE
    gtk_widget_set_sensitive(s->copy_item, vc->type == GD_VC_VTE);
#endif

    gd_update_windowsize(vc);
    gd_update_cursor(vc);
}

static gboolean gd_enter_event(GtkWidget *widget, GdkEventCrossing *crossing,
                               gpointer opaque)
{
    VirtualConsole *vc = opaque;
    GtkDisplayState *s = vc->s;

    if (gd_grab_on_hover(s)) {
        gd_grab_keyboard(vc, "grab-on-hover");
    }
    return TRUE;
}

static gboolean gd_leave_event(GtkWidget *widget, GdkEventCrossing *crossing,
                               gpointer opaque)
{
    VirtualConsole *vc = opaque;
    GtkDisplayState *s = vc->s;

    if (gd_grab_on_hover(s)) {
        gd_ungrab_keyboard(s);
    }
    return TRUE;
}

static gboolean gd_focus_in_event(GtkWidget *widget,
                                  GdkEventFocus *event, gpointer opaque)
{
    VirtualConsole *vc = opaque;

    win32_kbd_set_window(gd_win32_get_hwnd(vc));
    return TRUE;
}

static gboolean gd_focus_out_event(GtkWidget *widget,
                                   GdkEventFocus *event, gpointer opaque)
{
    VirtualConsole *vc = opaque;
    GtkDisplayState *s = vc->s;

    win32_kbd_set_window(NULL);
    gtk_release_modifiers(s);
    return TRUE;
}

static gboolean gd_configure(GtkWidget *widget,
                             GdkEventConfigure *cfg, gpointer opaque)
{
    VirtualConsole *vc = opaque;
    const double sx = vc->gfx.scale_x, sy = vc->gfx.scale_y;
    double width = cfg->width, height = cfg->height;

    if (!vc->s->free_scale && !vc->s->full_screen) {
        width /= sx;
        height /= sy;
    }

    gd_set_ui_size(vc, width, height);

    return FALSE;
}

/** Virtual Console Callbacks **/

static GSList *gd_vc_menu_init(GtkDisplayState *s, VirtualConsole *vc,
                               int idx, GSList *group, GtkWidget *view_menu)
{
    vc->menu_item = gtk_radio_menu_item_new_with_mnemonic(group, vc->label);
    gtk_accel_group_connect(s->accel_group, GDK_KEY_1 + idx,
            HOTKEY_MODIFIERS, 0,
            g_cclosure_new_swap(G_CALLBACK(gd_accel_switch_vc), vc, NULL));
    gtk_accel_label_set_accel(
            GTK_ACCEL_LABEL(gtk_bin_get_child(GTK_BIN(vc->menu_item))),
            GDK_KEY_1 + idx, HOTKEY_MODIFIERS);

    g_signal_connect(vc->menu_item, "activate",
                     G_CALLBACK(gd_menu_switch_vc), s);
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), vc->menu_item);

    return gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(vc->menu_item));
}

#if defined(CONFIG_VTE)
static void gd_menu_copy(GtkMenuItem *item, void *opaque)
{
    GtkDisplayState *s = opaque;
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
        gtk_widget_show(vc->vte.scrollbar);
    } else {
        gtk_widget_hide(vc->vte.scrollbar);
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

        buf = fifo8_pop_bufptr(&vc->vte.out_fifo, MIN(len, avail), &size);
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

    if (vc) {
        gd_vc_send_chars(vc);
    }
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

static void char_gd_vc_class_init(ObjectClass *oc, const void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

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

static GSList *gd_vc_vte_init(GtkDisplayState *s, VirtualConsole *vc,
                              Chardev *chr, int idx,
                              GSList *group, GtkWidget *view_menu)
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
    vc->label = g_strdup(vc->vte.chr->label ? : buffer);
    group = gd_vc_menu_init(s, vc, idx, group, view_menu);

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

    gtk_box_pack_end(GTK_BOX(box), scrollbar, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(box), vc->vte.terminal, TRUE, TRUE, 0);

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

    return group;
}

static void gd_vcs_init(GtkDisplayState *s, GSList *group,
                        GtkWidget *view_menu)
{
    int i;

    for (i = 0; i < nb_vcs; i++) {
        VirtualConsole *vc = &s->vc[s->nb_vcs];
        group = gd_vc_vte_init(s, vc, vcs[i], s->nb_vcs, group, view_menu);
        s->nb_vcs++;
    }
}
#endif /* CONFIG_VTE */

/** Window Creation **/

static void gd_connect_vc_gfx_signals(VirtualConsole *vc)
{
    g_signal_connect(vc->gfx.drawing_area, "draw",
                     G_CALLBACK(gd_draw_event), vc);
#if defined(CONFIG_OPENGL)
    if (gtk_use_gl_area) {
        /* wire up GtkGlArea events */
        g_signal_connect(vc->gfx.drawing_area, "render",
                         G_CALLBACK(gd_render_event), vc);
        g_signal_connect(vc->gfx.drawing_area, "resize",
                         G_CALLBACK(gd_resize_event), vc);
    }
#endif
    if (qemu_console_is_graphic(vc->gfx.dcl.con)) {
        g_signal_connect(vc->gfx.drawing_area, "event",
                         G_CALLBACK(gd_event), vc);
        g_signal_connect(vc->gfx.drawing_area, "button-press-event",
                         G_CALLBACK(gd_button_event), vc);
        g_signal_connect(vc->gfx.drawing_area, "button-release-event",
                         G_CALLBACK(gd_button_event), vc);
        g_signal_connect(vc->gfx.drawing_area, "scroll-event",
                         G_CALLBACK(gd_scroll_event), vc);
        g_signal_connect(vc->gfx.drawing_area, "key-press-event",
                         G_CALLBACK(gd_key_event), vc);
        g_signal_connect(vc->gfx.drawing_area, "key-release-event",
                         G_CALLBACK(gd_key_event), vc);
        g_signal_connect(vc->gfx.drawing_area, "touch-event",
                         G_CALLBACK(gd_touch_event), vc);

        g_signal_connect(vc->gfx.drawing_area, "enter-notify-event",
                         G_CALLBACK(gd_enter_event), vc);
        g_signal_connect(vc->gfx.drawing_area, "leave-notify-event",
                         G_CALLBACK(gd_leave_event), vc);
        g_signal_connect(vc->gfx.drawing_area, "focus-in-event",
                         G_CALLBACK(gd_focus_in_event), vc);
        g_signal_connect(vc->gfx.drawing_area, "focus-out-event",
                         G_CALLBACK(gd_focus_out_event), vc);
        g_signal_connect(vc->gfx.drawing_area, "configure-event",
                         G_CALLBACK(gd_configure), vc);
        g_signal_connect(vc->gfx.drawing_area, "grab-broken-event",
                         G_CALLBACK(gd_grab_broken_event), vc);
    } else {
        g_signal_connect(vc->gfx.drawing_area, "key-press-event",
                         G_CALLBACK(gd_text_key_down), vc);
    }
}

static void gd_connect_signals(GtkDisplayState *s)
{
    g_signal_connect(s->show_tabs_item, "activate",
                     G_CALLBACK(gd_menu_show_tabs), s);
    g_signal_connect(s->untabify_item, "activate",
                     G_CALLBACK(gd_menu_untabify), s);
    g_signal_connect(s->show_menubar_item, "activate",
                     G_CALLBACK(gd_menu_show_menubar), s);

    g_signal_connect(s->window, "delete-event",
                     G_CALLBACK(gd_window_close), s);

    g_signal_connect(s->pause_item, "activate",
                     G_CALLBACK(gd_menu_pause), s);
    g_signal_connect(s->reset_item, "activate",
                     G_CALLBACK(gd_menu_reset), s);
    g_signal_connect(s->powerdown_item, "activate",
                     G_CALLBACK(gd_menu_powerdown), s);
    g_signal_connect(s->quit_item, "activate",
                     G_CALLBACK(gd_menu_quit), s);
#if defined(CONFIG_VTE)
    g_signal_connect(s->copy_item, "activate",
                     G_CALLBACK(gd_menu_copy), s);
#endif
    g_signal_connect(s->full_screen_item, "activate",
                     G_CALLBACK(gd_menu_full_screen), s);
    g_signal_connect(s->zoom_in_item, "activate",
                     G_CALLBACK(gd_menu_zoom_in), s);
    g_signal_connect(s->zoom_out_item, "activate",
                     G_CALLBACK(gd_menu_zoom_out), s);
    g_signal_connect(s->zoom_fixed_item, "activate",
                     G_CALLBACK(gd_menu_zoom_fixed), s);
    g_signal_connect(s->zoom_fit_item, "activate",
                     G_CALLBACK(gd_menu_zoom_fit), s);
    g_signal_connect(s->grab_item, "activate",
                     G_CALLBACK(gd_menu_grab_input), s);
    g_signal_connect(s->notebook, "switch-page",
                     G_CALLBACK(gd_change_page), s);
}

static GtkWidget *gd_create_menu_machine(GtkDisplayState *s)
{
    GtkWidget *machine_menu;
    GtkWidget *separator;

    machine_menu = gtk_menu_new();
    gtk_menu_set_accel_group(GTK_MENU(machine_menu), s->accel_group);

    s->pause_item = gtk_check_menu_item_new_with_mnemonic(_("_Pause"));
    gtk_menu_shell_append(GTK_MENU_SHELL(machine_menu), s->pause_item);

    separator = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(machine_menu), separator);

    s->reset_item = gtk_menu_item_new_with_mnemonic(_("_Reset"));
    gtk_menu_shell_append(GTK_MENU_SHELL(machine_menu), s->reset_item);

    s->powerdown_item = gtk_menu_item_new_with_mnemonic(_("Power _Down"));
    gtk_menu_shell_append(GTK_MENU_SHELL(machine_menu), s->powerdown_item);

    separator = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(machine_menu), separator);

    s->quit_item = gtk_menu_item_new_with_mnemonic(_("_Quit"));
    gtk_menu_item_set_accel_path(GTK_MENU_ITEM(s->quit_item),
                                 "<QEMU>/Machine/Quit");
    gtk_accel_map_add_entry("<QEMU>/Machine/Quit",
                            GDK_KEY_q, HOTKEY_MODIFIERS);
    gtk_menu_shell_append(GTK_MENU_SHELL(machine_menu), s->quit_item);

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

static bool gd_scale_valid(double scale)
{
    return scale >= VC_SCALE_MIN && scale <= VC_SCALE_MAX;
}

static GSList *gd_vc_gfx_init(GtkDisplayState *s, VirtualConsole *vc,
                              QemuConsole *con, int idx,
                              GSList *group, GtkWidget *view_menu)
{
    bool zoom_to_fit = false;
    int i;

    vc->label = qemu_console_get_label(con);
    vc->s = s;
    vc->gfx.preferred_scale = 1.0;
    if (s->opts->u.gtk.has_scale) {
        if (gd_scale_valid(s->opts->u.gtk.scale)) {
            vc->gfx.preferred_scale = s->opts->u.gtk.scale;
        } else {
            error_report("Invalid scale value %lf given, being ignored",
                         s->opts->u.gtk.scale);
            s->opts->u.gtk.has_scale = false;
        }
    }
    vc->gfx.scale_x = vc->gfx.preferred_scale;
    vc->gfx.scale_y = vc->gfx.preferred_scale;

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
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
            gtk_widget_set_double_buffered(vc->gfx.drawing_area, FALSE);
#pragma GCC diagnostic pop
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


    gtk_widget_add_events(vc->gfx.drawing_area,
                          GDK_POINTER_MOTION_MASK |
                          GDK_BUTTON_PRESS_MASK |
                          GDK_BUTTON_RELEASE_MASK |
                          GDK_BUTTON_MOTION_MASK |
                          GDK_TOUCH_MASK |
                          GDK_ENTER_NOTIFY_MASK |
                          GDK_LEAVE_NOTIFY_MASK |
                          GDK_SCROLL_MASK |
                          GDK_SMOOTH_SCROLL_MASK |
                          GDK_KEY_PRESS_MASK);
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
    group = gd_vc_menu_init(s, vc, idx, group, view_menu);

    if (dpy_ui_info_supported(vc->gfx.dcl.con)) {
        zoom_to_fit = true;
    }
    if (s->opts->u.gtk.has_zoom_to_fit) {
        zoom_to_fit = s->opts->u.gtk.zoom_to_fit;
    }
    if (zoom_to_fit) {
        gtk_menu_item_activate(GTK_MENU_ITEM(s->zoom_fit_item));
        s->free_scale = true;
    }

    s->keep_aspect_ratio = true;
    if (s->opts->u.gtk.has_keep_aspect_ratio)
        s->keep_aspect_ratio = s->opts->u.gtk.keep_aspect_ratio;

    for (i = 0; i < INPUT_EVENT_SLOTS_MAX; i++) {
        struct touch_slot *slot = &touch_slots[i];
        slot->tracking_id = -1;
    }

    return group;
}

static GtkWidget *gd_create_menu_view(GtkDisplayState *s, DisplayOptions *opts)
{
    GSList *group = NULL;
    GtkWidget *view_menu;
    GtkWidget *separator;
    QemuConsole *con;
    int vc;

    view_menu = gtk_menu_new();
    gtk_menu_set_accel_group(GTK_MENU(view_menu), s->accel_group);

    s->full_screen_item = gtk_menu_item_new_with_mnemonic(_("_Fullscreen"));

#if defined(CONFIG_VTE)
    s->copy_item = gtk_menu_item_new_with_mnemonic(_("_Copy"));
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), s->copy_item);
#endif

    gtk_accel_group_connect(s->accel_group, GDK_KEY_f, HOTKEY_MODIFIERS, 0,
            g_cclosure_new_swap(G_CALLBACK(gd_accel_full_screen), s, NULL));
    gtk_accel_label_set_accel(
            GTK_ACCEL_LABEL(gtk_bin_get_child(GTK_BIN(s->full_screen_item))),
            GDK_KEY_f, HOTKEY_MODIFIERS);
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), s->full_screen_item);

    separator = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), separator);

    s->zoom_in_item = gtk_menu_item_new_with_mnemonic(_("Zoom _In"));
    gtk_menu_item_set_accel_path(GTK_MENU_ITEM(s->zoom_in_item),
                                 "<QEMU>/View/Zoom In");
    gtk_accel_map_add_entry("<QEMU>/View/Zoom In", GDK_KEY_plus,
                            HOTKEY_MODIFIERS);
    gtk_accel_group_connect(s->accel_group, GDK_KEY_equal, HOTKEY_MODIFIERS, 0,
            g_cclosure_new_swap(G_CALLBACK(gd_accel_zoom_in), s, NULL));
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), s->zoom_in_item);

    s->zoom_out_item = gtk_menu_item_new_with_mnemonic(_("Zoom _Out"));
    gtk_menu_item_set_accel_path(GTK_MENU_ITEM(s->zoom_out_item),
                                 "<QEMU>/View/Zoom Out");
    gtk_accel_map_add_entry("<QEMU>/View/Zoom Out", GDK_KEY_minus,
                            HOTKEY_MODIFIERS);
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), s->zoom_out_item);

    s->zoom_fixed_item = gtk_menu_item_new_with_mnemonic(_("Best _Fit"));
    gtk_menu_item_set_accel_path(GTK_MENU_ITEM(s->zoom_fixed_item),
                                 "<QEMU>/View/Zoom Fixed");
    gtk_accel_map_add_entry("<QEMU>/View/Zoom Fixed", GDK_KEY_0,
                            HOTKEY_MODIFIERS);
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), s->zoom_fixed_item);

    s->zoom_fit_item = gtk_check_menu_item_new_with_mnemonic(_("Zoom To _Fit"));
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), s->zoom_fit_item);

    separator = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), separator);

    s->grab_on_hover_item = gtk_check_menu_item_new_with_mnemonic(_("Grab On _Hover"));
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), s->grab_on_hover_item);

    s->grab_item = gtk_check_menu_item_new_with_mnemonic(_("_Grab Input"));
    gtk_menu_item_set_accel_path(GTK_MENU_ITEM(s->grab_item),
                                 "<QEMU>/View/Grab Input");
    gtk_accel_map_add_entry("<QEMU>/View/Grab Input", GDK_KEY_g,
                            HOTKEY_MODIFIERS);
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), s->grab_item);

    separator = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), separator);

    /* gfx */
    for (vc = 0;; vc++) {
        con = qemu_console_lookup_by_index(vc);
        if (!con) {
            break;
        }
        group = gd_vc_gfx_init(s, &s->vc[vc], con,
                               vc, group, view_menu);
        s->nb_vcs++;
    }

#if defined(CONFIG_VTE)
    /* vte */
    gd_vcs_init(s, group, view_menu);
#endif

    separator = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), separator);

    s->show_tabs_item = gtk_check_menu_item_new_with_mnemonic(_("Show _Tabs"));
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), s->show_tabs_item);

    s->untabify_item = gtk_menu_item_new_with_mnemonic(_("Detach Tab"));
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), s->untabify_item);

    s->show_menubar_item = gtk_check_menu_item_new_with_mnemonic(
            _("Show Menubar"));
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(s->show_menubar_item),
                                   !opts->u.gtk.has_show_menubar ||
                                   opts->u.gtk.show_menubar);
    gtk_accel_group_connect(s->accel_group, GDK_KEY_m, HOTKEY_MODIFIERS, 0,
            g_cclosure_new_swap(G_CALLBACK(gd_accel_show_menubar), s, NULL));
    gtk_accel_label_set_accel(
            GTK_ACCEL_LABEL(gtk_bin_get_child(GTK_BIN(s->show_menubar_item))),
            GDK_KEY_m, HOTKEY_MODIFIERS);
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), s->show_menubar_item);

    return view_menu;
}

static void gd_create_menus(GtkDisplayState *s, DisplayOptions *opts)
{
    GtkSettings *settings;

    s->accel_group = gtk_accel_group_new();
    s->machine_menu = gd_create_menu_machine(s);
    s->view_menu = gd_create_menu_view(s, opts);

    s->machine_menu_item = gtk_menu_item_new_with_mnemonic(_("_Machine"));
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(s->machine_menu_item),
                              s->machine_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(s->menu_bar), s->machine_menu_item);

    s->view_menu_item = gtk_menu_item_new_with_mnemonic(_("_View"));
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(s->view_menu_item), s->view_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(s->menu_bar), s->view_menu_item);

    g_object_set_data(G_OBJECT(s->window), "accel_group", s->accel_group);
    gtk_window_add_accel_group(GTK_WINDOW(s->window), s->accel_group);

    /* Disable the default "F10" menu shortcut. */
    settings = gtk_widget_get_settings(s->window);
    g_object_set(G_OBJECT(settings), "gtk-menu-bar-accel", "", NULL);
}


static gboolean gtkinit;

static void gtk_display_init(DisplayState *ds, DisplayOptions *opts)
{
    VirtualConsole *vc;

    GtkDisplayState *s;
    GdkDisplay *window_display;
    GtkIconTheme *theme;
    char *dir;
    int idx;

    if (!gtkinit) {
        fprintf(stderr, "gtk initialization failed\n");
        exit(1);
    }
    assert(opts->type == DISPLAY_TYPE_GTK);
    s = g_malloc0(sizeof(*s));
    s->opts = opts;

    theme = gtk_icon_theme_get_default();
    dir = get_relocated_path(CONFIG_QEMU_ICONDIR);
    gtk_icon_theme_prepend_search_path(theme, dir);
    g_free(dir);
    g_set_prgname("qemu");

    s->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    s->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    s->notebook = gtk_notebook_new();
    s->menu_bar = gtk_menu_bar_new();

    s->free_scale = FALSE;

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

    window_display = gtk_widget_get_display(s->window);
    if (s->opts->has_show_cursor && s->opts->show_cursor) {
        s->null_cursor = NULL; /* default pointer */
    } else {
        s->null_cursor = gdk_cursor_new_for_display(window_display,
                                                    GDK_BLANK_CURSOR);
    }

    s->mouse_mode_notifier.notify = gd_mouse_mode_change;
    qemu_add_mouse_mode_change_notifier(&s->mouse_mode_notifier);
    qemu_add_vm_change_state_handler(gd_change_runstate, s);

    gtk_window_set_icon_name(GTK_WINDOW(s->window), "qemu");

    gd_create_menus(s, opts);

    gd_connect_signals(s);

    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(s->notebook), FALSE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(s->notebook), FALSE);

    gd_update_caption(s);

    gtk_box_pack_start(GTK_BOX(s->vbox), s->menu_bar, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(s->vbox), s->notebook, TRUE, TRUE, 0);

    gtk_container_add(GTK_CONTAINER(s->window), s->vbox);

    gtk_widget_show_all(s->window);

    for (idx = 0;; idx++) {
        QemuConsole *con = qemu_console_lookup_by_index(idx);
        if (!con) {
            break;
        }
        gtk_widget_realize(s->vc[idx].gfx.drawing_area);
    }

    if (opts->u.gtk.has_show_menubar &&
        !opts->u.gtk.show_menubar) {
        gtk_widget_hide(s->menu_bar);
    }

    vc = gd_vc_find_current(s);
    gtk_widget_set_sensitive(s->view_menu, vc != NULL);
#ifdef CONFIG_VTE
    gtk_widget_set_sensitive(s->copy_item,
                             vc && vc->type == GD_VC_VTE);
#endif

    if (opts->has_full_screen &&
        opts->full_screen) {
        gtk_menu_item_activate(GTK_MENU_ITEM(s->full_screen_item));
    }
    if (opts->u.gtk.has_grab_on_hover &&
        opts->u.gtk.grab_on_hover) {
        gtk_menu_item_activate(GTK_MENU_ITEM(s->grab_on_hover_item));
    }
    if (opts->u.gtk.has_show_tabs &&
        opts->u.gtk.show_tabs) {
        gtk_menu_item_activate(GTK_MENU_ITEM(s->show_tabs_item));
    }
#ifdef CONFIG_GTK_CLIPBOARD
    gd_clipboard_init(s);
#endif /* CONFIG_GTK_CLIPBOARD */

    /* GTK's event polling must happen on the main thread. */
    qemu_main = NULL;
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
    gtkinit = gtk_init_check(NULL, NULL);
    if (!gtkinit) {
        /* don't exit yet, that'll break -help */
        return;
    }

    assert(opts->type == DISPLAY_TYPE_GTK);
    if (opts->has_gl && opts->gl != DISPLAY_GL_MODE_OFF) {
#if defined(CONFIG_OPENGL)
#if defined(GDK_WINDOWING_WAYLAND)
        if (GDK_IS_WAYLAND_DISPLAY(gdk_display_get_default())) {
            gtk_use_gl_area = true;
            gtk_gl_area_init();
        } else
#endif
#if defined(GDK_WINDOWING_WIN32)
        if (GDK_IS_WIN32_DISPLAY(gdk_display_get_default())) {
            gtk_use_gl_area = true;
            gtk_gl_area_init();
        } else
#endif
        {
#ifdef CONFIG_X11
            DisplayGLMode mode = opts->has_gl ? opts->gl : DISPLAY_GL_MODE_ON;
            gtk_egl_init(mode);
#endif
        }
#endif
    }

    keycode_map = gd_get_keymap(&keycode_maplen);

#if defined(CONFIG_VTE)
    type_register_static(&char_gd_vc_type_info);
#endif
}

static QemuDisplay qemu_display_gtk = {
    .type       = DISPLAY_TYPE_GTK,
    .early_init = early_gtk_display_init,
    .init       = gtk_display_init,
    .vc         = "vc",
};

static void register_gtk(void)
{
    qemu_display_register(&qemu_display_gtk);
}

type_init(register_gtk);

#ifdef CONFIG_OPENGL
module_dep("ui-opengl");
#endif
