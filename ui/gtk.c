/*
 * GTK UI
 *
 * Copyright IBM, Corp. 2012
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * Portions from gtk-vnc:
 *
 * GTK VNC Widget
 *
 * Copyright (C) 2006  Anthony Liguori <anthony@codemonkey.ws>
 * Copyright (C) 2009-2010 Daniel P. Berrange <dan@berrange.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.0 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#define GETTEXT_PACKAGE "qemu"
#define LOCALEDIR "po"

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/cutils.h"

#include "ui/console.h"
#include "ui/gtk.h"

#include <glib/gi18n.h>
#include <locale.h>
#if defined(CONFIG_VTE)
#include <vte/vte.h>
#endif
#include <math.h>

#include "trace.h"
#include "ui/input.h"
#include "sysemu/sysemu.h"
#include "qmp-commands.h"
#include "x_keymap.h"
#include "keymaps.h"
#include "sysemu/char.h"
#include "qom/object.h"

#define MAX_VCS 10
#define VC_WINDOW_X_MIN  320
#define VC_WINDOW_Y_MIN  240
#define VC_TERM_X_MIN     80
#define VC_TERM_Y_MIN     25
#define VC_SCALE_MIN    0.25
#define VC_SCALE_STEP   0.25

#if !defined(CONFIG_VTE)
# define VTE_CHECK_VERSION(a, b, c) 0
#endif

#if defined(CONFIG_VTE) && !GTK_CHECK_VERSION(3, 0, 0)
/*
 * The gtk2 vte terminal widget seriously messes up the window resize
 * for some reason.  You basically can't make the qemu window smaller
 * any more because the toplevel window geoemtry hints are overridden.
 *
 * Workaround that by hiding all vte widgets, except the one in the
 * current tab.
 *
 * Luckily everything works smooth in gtk3.
 */
# define VTE_RESIZE_HACK 1
#endif

#if !GTK_CHECK_VERSION(2, 20, 0)
#define gtk_widget_get_realized(widget) GTK_WIDGET_REALIZED(widget)
#endif

#ifndef GDK_IS_X11_DISPLAY
#define GDK_IS_X11_DISPLAY(dpy) (dpy == dpy)
#endif
#ifndef GDK_IS_WIN32_DISPLAY
#define GDK_IS_WIN32_DISPLAY(dpy) (dpy == dpy)
#endif

#ifndef GDK_KEY_0
#define GDK_KEY_0 GDK_0
#define GDK_KEY_1 GDK_1
#define GDK_KEY_2 GDK_2
#define GDK_KEY_f GDK_f
#define GDK_KEY_g GDK_g
#define GDK_KEY_q GDK_q
#define GDK_KEY_plus GDK_plus
#define GDK_KEY_minus GDK_minus
#define GDK_KEY_Pause GDK_Pause
#endif

/* Some older mingw versions lack this constant or have
 * it conditionally defined */
#ifdef _WIN32
# ifndef MAPVK_VK_TO_VSC
#  define MAPVK_VK_TO_VSC 0
# endif
#endif


#define HOTKEY_MODIFIERS        (GDK_CONTROL_MASK | GDK_MOD1_MASK)

static const int modifier_keycode[] = {
    /* shift, control, alt keys, meta keys, both left & right */
    0x2a, 0x36, 0x1d, 0x9d, 0x38, 0xb8, 0xdb, 0xdd,
};

struct GtkDisplayState {
    GtkWidget *window;

    GtkWidget *menu_bar;

    GtkAccelGroup *accel_group;

    GtkWidget *machine_menu_item;
    GtkWidget *machine_menu;
    GtkWidget *pause_item;
    GtkWidget *reset_item;
    GtkWidget *powerdown_item;
    GtkWidget *quit_item;

    GtkWidget *view_menu_item;
    GtkWidget *view_menu;
    GtkWidget *full_screen_item;
    GtkWidget *zoom_in_item;
    GtkWidget *zoom_out_item;
    GtkWidget *zoom_fixed_item;
    GtkWidget *zoom_fit_item;
    GtkWidget *grab_item;
    GtkWidget *grab_on_hover_item;

    int nb_vcs;
    VirtualConsole vc[MAX_VCS];

    GtkWidget *show_tabs_item;
    GtkWidget *untabify_item;

    GtkWidget *vbox;
    GtkWidget *notebook;
    int button_mask;
    gboolean last_set;
    int last_x;
    int last_y;
    int grab_x_root;
    int grab_y_root;
    VirtualConsole *kbd_owner;
    VirtualConsole *ptr_owner;

    gboolean full_screen;

    GdkCursor *null_cursor;
    Notifier mouse_mode_notifier;
    gboolean free_scale;

    bool external_pause_update;

    bool modifier_pressed[ARRAY_SIZE(modifier_keycode)];
    bool has_evdev;
    bool ignore_keys;
};

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
    if (s->full_screen || qemu_input_is_absolute() || s->ptr_owner == vc) {
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
        if (s->free_scale) {
            geo.min_width  = surface_width(vc->gfx.ds) * VC_SCALE_MIN;
            geo.min_height = surface_height(vc->gfx.ds) * VC_SCALE_MIN;
            mask |= GDK_HINT_MIN_SIZE;
        } else {
            geo.min_width  = surface_width(vc->gfx.ds) * vc->gfx.scale_x;
            geo.min_height = surface_height(vc->gfx.ds) * vc->gfx.scale_y;
            mask |= GDK_HINT_MIN_SIZE;
        }
        geo_widget = vc->gfx.drawing_area;
        gtk_widget_set_size_request(geo_widget, geo.min_width, geo.min_height);

#if defined(CONFIG_VTE)
    } else if (vc->type == GD_VC_VTE) {
        VteTerminal *term = VTE_TERMINAL(vc->vte.terminal);
        GtkBorder *ib;

        geo.width_inc  = vte_terminal_get_char_width(term);
        geo.height_inc = vte_terminal_get_char_height(term);
        mask |= GDK_HINT_RESIZE_INC;
        geo.base_width  = geo.width_inc;
        geo.base_height = geo.height_inc;
        mask |= GDK_HINT_BASE_SIZE;
        geo.min_width  = geo.width_inc * VC_TERM_X_MIN;
        geo.min_height = geo.height_inc * VC_TERM_Y_MIN;
        mask |= GDK_HINT_MIN_SIZE;
        gtk_widget_style_get(vc->vte.terminal, "inner-border", &ib, NULL);
        if (ib) {
            geo.base_width  += ib->left + ib->right;
            geo.base_height += ib->top + ib->bottom;
            geo.min_width   += ib->left + ib->right;
            geo.min_height  += ib->top + ib->bottom;
        }
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
    gdk_drawable_get_size(gtk_widget_get_window(area), &ww, &wh);
#if defined(CONFIG_GTK_GL)
    if (vc->gfx.gls) {
        gtk_gl_area_queue_render(GTK_GL_AREA(vc->gfx.drawing_area));
        return;
    }
#endif
    gtk_widget_queue_draw_area(area, 0, 0, ww, wh);
}

static void gtk_release_modifiers(GtkDisplayState *s)
{
    VirtualConsole *vc = gd_vc_find_current(s);
    int i, keycode;

    if (vc->type != GD_VC_GFX ||
        !qemu_console_is_graphic(vc->gfx.dcl.con)) {
        return;
    }
    for (i = 0; i < ARRAY_SIZE(modifier_keycode); i++) {
        keycode = modifier_keycode[i];
        if (!s->modifier_pressed[i]) {
            continue;
        }
        qemu_input_event_send_key_number(vc->gfx.dcl.con, keycode, false);
        s->modifier_pressed[i] = false;
    }
}

static void gd_widget_reparent(GtkWidget *from, GtkWidget *to,
                               GtkWidget *widget)
{
    g_object_ref(G_OBJECT(widget));
    gtk_container_remove(GTK_CONTAINER(from), widget);
    gtk_container_add(GTK_CONTAINER(to), widget);
    g_object_unref(G_OBJECT(widget));
}

/** DisplayState Callbacks **/

static void gd_update(DisplayChangeListener *dcl,
                      int x, int y, int w, int h)
{
    VirtualConsole *vc = container_of(dcl, VirtualConsole, gfx.dcl);
    GdkWindow *win;
    int x1, x2, y1, y2;
    int mx, my;
    int fbw, fbh;
    int ww, wh;

    trace_gd_update(vc->label, x, y, w, h);

    if (!gtk_widget_get_realized(vc->gfx.drawing_area)) {
        return;
    }

    if (vc->gfx.convert) {
        pixman_image_composite(PIXMAN_OP_SRC, vc->gfx.ds->image,
                               NULL, vc->gfx.convert,
                               x, y, 0, 0, x, y, w, h);
    }

    x1 = floor(x * vc->gfx.scale_x);
    y1 = floor(y * vc->gfx.scale_y);

    x2 = ceil(x * vc->gfx.scale_x + w * vc->gfx.scale_x);
    y2 = ceil(y * vc->gfx.scale_y + h * vc->gfx.scale_y);

    fbw = surface_width(vc->gfx.ds) * vc->gfx.scale_x;
    fbh = surface_height(vc->gfx.ds) * vc->gfx.scale_y;

    win = gtk_widget_get_window(vc->gfx.drawing_area);
    if (!win) {
        return;
    }
    gdk_drawable_get_size(win, &ww, &wh);

    mx = my = 0;
    if (ww > fbw) {
        mx = (ww - fbw) / 2;
    }
    if (wh > fbh) {
        my = (wh - fbh) / 2;
    }

    gtk_widget_queue_draw_area(vc->gfx.drawing_area,
                               mx + x1, my + y1, (x2 - x1), (y2 - y1));
}

static void gd_refresh(DisplayChangeListener *dcl)
{
    graphic_hw_update(dcl->con);
}

#if GTK_CHECK_VERSION(3, 0, 0)
static void gd_mouse_set(DisplayChangeListener *dcl,
                         int x, int y, int visible)
{
    VirtualConsole *vc = container_of(dcl, VirtualConsole, gfx.dcl);
    GdkDisplay *dpy;
    GdkDeviceManager *mgr;
    gint x_root, y_root;

    if (qemu_input_is_absolute()) {
        return;
    }

    dpy = gtk_widget_get_display(vc->gfx.drawing_area);
    mgr = gdk_display_get_device_manager(dpy);
    gdk_window_get_root_coords(gtk_widget_get_window(vc->gfx.drawing_area),
                               x, y, &x_root, &y_root);
    gdk_device_warp(gdk_device_manager_get_client_pointer(mgr),
                    gtk_widget_get_screen(vc->gfx.drawing_area),
                    x_root, y_root);
    vc->s->last_x = x;
    vc->s->last_y = y;
}
#else
static void gd_mouse_set(DisplayChangeListener *dcl,
                         int x, int y, int visible)
{
    VirtualConsole *vc = container_of(dcl, VirtualConsole, gfx.dcl);
    gint x_root, y_root;

    if (qemu_input_is_absolute()) {
        return;
    }

    gdk_window_get_root_coords(gtk_widget_get_window(vc->gfx.drawing_area),
                               x, y, &x_root, &y_root);
    gdk_display_warp_pointer(gtk_widget_get_display(vc->gfx.drawing_area),
                             gtk_widget_get_screen(vc->gfx.drawing_area),
                             x_root, y_root);
}
#endif

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
#if !GTK_CHECK_VERSION(3, 0, 0)
    gdk_cursor_unref(cursor);
#else
    g_object_unref(cursor);
#endif
}

static void gd_switch(DisplayChangeListener *dcl,
                      DisplaySurface *surface)
{
    VirtualConsole *vc = container_of(dcl, VirtualConsole, gfx.dcl);
    bool resized = true;

    trace_gd_switch(vc->label,
                    surface ? surface_width(surface)  : 0,
                    surface ? surface_height(surface) : 0);

    if (vc->gfx.surface) {
        cairo_surface_destroy(vc->gfx.surface);
        vc->gfx.surface = NULL;
    }
    if (vc->gfx.convert) {
        pixman_image_unref(vc->gfx.convert);
        vc->gfx.convert = NULL;
    }

    if (vc->gfx.ds && surface &&
        surface_width(vc->gfx.ds) == surface_width(surface) &&
        surface_height(vc->gfx.ds) == surface_height(surface)) {
        resized = false;
    }
    vc->gfx.ds = surface;

    if (!surface) {
        return;
    }

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

/** DisplayState Callbacks (opengl version) **/

#if defined(CONFIG_GTK_GL)

static const DisplayChangeListenerOps dcl_gl_area_ops = {
    .dpy_name             = "gtk-egl",
    .dpy_gfx_update       = gd_gl_area_update,
    .dpy_gfx_switch       = gd_gl_area_switch,
    .dpy_gfx_check_format = console_gl_check_format,
    .dpy_refresh          = gd_gl_area_refresh,
    .dpy_mouse_set        = gd_mouse_set,
    .dpy_cursor_define    = gd_cursor_define,

    .dpy_gl_ctx_create       = gd_gl_area_create_context,
    .dpy_gl_ctx_destroy      = gd_gl_area_destroy_context,
    .dpy_gl_ctx_make_current = gd_gl_area_make_current,
    .dpy_gl_ctx_get_current  = gd_gl_area_get_current_context,
    .dpy_gl_scanout          = gd_gl_area_scanout,
    .dpy_gl_update           = gd_gl_area_scanout_flush,
};

#else

static const DisplayChangeListenerOps dcl_egl_ops = {
    .dpy_name             = "gtk-egl",
    .dpy_gfx_update       = gd_egl_update,
    .dpy_gfx_switch       = gd_egl_switch,
    .dpy_gfx_check_format = console_gl_check_format,
    .dpy_refresh          = gd_egl_refresh,
    .dpy_mouse_set        = gd_mouse_set,
    .dpy_cursor_define    = gd_cursor_define,

    .dpy_gl_ctx_create       = gd_egl_create_context,
    .dpy_gl_ctx_destroy      = qemu_egl_destroy_context,
    .dpy_gl_ctx_make_current = gd_egl_make_current,
    .dpy_gl_ctx_get_current  = qemu_egl_get_current_context,
    .dpy_gl_scanout          = gd_egl_scanout,
    .dpy_gl_update           = gd_egl_scanout_flush,
};

#endif /* CONFIG_GTK_GL */
#endif /* CONFIG_OPENGL */

/** QEMU Events **/

static void gd_change_runstate(void *opaque, int running, RunState state)
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
    if (qemu_input_is_absolute() && gd_is_grab_active(s)) {
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(s->grab_item),
                                       FALSE);
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
    int i;

    if (!no_quit) {
        for (i = 0; i < s->nb_vcs; i++) {
            if (s->vc[i].type != GD_VC_GFX) {
                continue;
            }
            unregister_displaychangelistener(&s->vc[i].gfx.dcl);
        }
        qmp_quit(NULL);
        return FALSE;
    }

    return TRUE;
}

static void gd_set_ui_info(VirtualConsole *vc, gint width, gint height)
{
    QemuUIInfo info;

    memset(&info, 0, sizeof(info));
    info.width = width;
    info.height = height;
    dpy_set_ui_info(vc->gfx.dcl.con, &info);
}

#if defined(CONFIG_GTK_GL)

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

    gd_set_ui_info(vc, width, height);
}

#endif

static gboolean gd_draw_event(GtkWidget *widget, cairo_t *cr, void *opaque)
{
    VirtualConsole *vc = opaque;
    GtkDisplayState *s = vc->s;
    int mx, my;
    int ww, wh;
    int fbw, fbh;

#if defined(CONFIG_OPENGL)
    if (vc->gfx.gls) {
#if defined(CONFIG_GTK_GL)
        /* invoke render callback please */
        return FALSE;
#else
        gd_egl_draw(vc);
        return TRUE;
#endif
    }
#endif

    if (!gtk_widget_get_realized(widget)) {
        return FALSE;
    }
    if (!vc->gfx.ds) {
        return FALSE;
    }

    fbw = surface_width(vc->gfx.ds);
    fbh = surface_height(vc->gfx.ds);

    gdk_drawable_get_size(gtk_widget_get_window(widget), &ww, &wh);

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

    return TRUE;
}

#if !GTK_CHECK_VERSION(3, 0, 0)
static gboolean gd_expose_event(GtkWidget *widget, GdkEventExpose *expose,
                                void *opaque)
{
    cairo_t *cr;
    gboolean ret;

    cr = gdk_cairo_create(gtk_widget_get_window(widget));
    cairo_rectangle(cr,
                    expose->area.x,
                    expose->area.y,
                    expose->area.width,
                    expose->area.height);
    cairo_clip(cr);

    ret = gd_draw_event(widget, cr, opaque);

    cairo_destroy(cr);

    return ret;
}
#endif

static gboolean gd_motion_event(GtkWidget *widget, GdkEventMotion *motion,
                                void *opaque)
{
    VirtualConsole *vc = opaque;
    GtkDisplayState *s = vc->s;
    int x, y;
    int mx, my;
    int fbh, fbw;
    int ww, wh;

    if (!vc->gfx.ds) {
        return TRUE;
    }

    fbw = surface_width(vc->gfx.ds) * vc->gfx.scale_x;
    fbh = surface_height(vc->gfx.ds) * vc->gfx.scale_y;

    gdk_drawable_get_size(gtk_widget_get_window(vc->gfx.drawing_area),
                          &ww, &wh);

    mx = my = 0;
    if (ww > fbw) {
        mx = (ww - fbw) / 2;
    }
    if (wh > fbh) {
        my = (wh - fbh) / 2;
    }

    x = (motion->x - mx) / vc->gfx.scale_x;
    y = (motion->y - my) / vc->gfx.scale_y;

    if (qemu_input_is_absolute()) {
        if (x < 0 || y < 0 ||
            x >= surface_width(vc->gfx.ds) ||
            y >= surface_height(vc->gfx.ds)) {
            return TRUE;
        }
        qemu_input_queue_abs(vc->gfx.dcl.con, INPUT_AXIS_X, x,
                             surface_width(vc->gfx.ds));
        qemu_input_queue_abs(vc->gfx.dcl.con, INPUT_AXIS_Y, y,
                             surface_height(vc->gfx.ds));
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
        GdkScreen *screen = gtk_widget_get_screen(vc->gfx.drawing_area);
        int x = (int)motion->x_root;
        int y = (int)motion->y_root;

        /* In relative mode check to see if client pointer hit
         * one of the screen edges, and if so move it back by
         * 200 pixels. This is important because the pointer
         * in the server doesn't correspond 1-for-1, and so
         * may still be only half way across the screen. Without
         * this warp, the server pointer would thus appear to hit
         * an invisible wall */
        if (x == 0) {
            x += 200;
        }
        if (y == 0) {
            y += 200;
        }
        if (x == (gdk_screen_get_width(screen) - 1)) {
            x -= 200;
        }
        if (y == (gdk_screen_get_height(screen) - 1)) {
            y -= 200;
        }

        if (x != (int)motion->x_root || y != (int)motion->y_root) {
#if GTK_CHECK_VERSION(3, 0, 0)
            GdkDevice *dev = gdk_event_get_device((GdkEvent *)motion);
            gdk_device_warp(dev, screen, x, y);
#else
            GdkDisplay *display = gtk_widget_get_display(widget);
            gdk_display_warp_pointer(display, screen, x, y);
#endif
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
        !qemu_input_is_absolute() && s->ptr_owner != vc) {
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
    } else {
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
    InputButton btn;

    if (scroll->direction == GDK_SCROLL_UP) {
        btn = INPUT_BUTTON_WHEEL_UP;
    } else if (scroll->direction == GDK_SCROLL_DOWN) {
        btn = INPUT_BUTTON_WHEEL_DOWN;
    } else {
        return TRUE;
    }

    qemu_input_queue_btn(vc->gfx.dcl.con, btn, true);
    qemu_input_event_sync();
    qemu_input_queue_btn(vc->gfx.dcl.con, btn, false);
    qemu_input_event_sync();
    return TRUE;
}

static int gd_map_keycode(GtkDisplayState *s, GdkDisplay *dpy, int gdk_keycode)
{
    int qemu_keycode;

#ifdef GDK_WINDOWING_WIN32
    if (GDK_IS_WIN32_DISPLAY(dpy)) {
        qemu_keycode = MapVirtualKey(gdk_keycode, MAPVK_VK_TO_VSC);
        switch (qemu_keycode) {
        case 103:   /* alt gr */
            qemu_keycode = 56 | SCANCODE_GREY;
            break;
        }
        return qemu_keycode;
    }
#endif

    if (gdk_keycode < 9) {
        qemu_keycode = 0;
    } else if (gdk_keycode < 97) {
        qemu_keycode = gdk_keycode - 8;
#ifdef GDK_WINDOWING_X11
    } else if (GDK_IS_X11_DISPLAY(dpy) && gdk_keycode < 158) {
        if (s->has_evdev) {
            qemu_keycode = translate_evdev_keycode(gdk_keycode - 97);
        } else {
            qemu_keycode = translate_xfree86_keycode(gdk_keycode - 97);
        }
#endif
    } else if (gdk_keycode == 208) { /* Hiragana_Katakana */
        qemu_keycode = 0x70;
    } else if (gdk_keycode == 211) { /* backslash */
        qemu_keycode = 0x73;
    } else {
        qemu_keycode = 0;
    }

    return qemu_keycode;
}

static gboolean gd_text_key_down(GtkWidget *widget,
                                 GdkEventKey *key, void *opaque)
{
    VirtualConsole *vc = opaque;
    QemuConsole *con = vc->gfx.dcl.con;

    if (key->length) {
        kbd_put_string_console(con, key->string, key->length);
    } else {
        int num = gd_map_keycode(vc->s, gtk_widget_get_display(widget),
                                 key->hardware_keycode);
        int qcode = qemu_input_key_number_to_qcode(num);
        kbd_put_qcode_console(con, qcode);
    }
    return TRUE;
}

static gboolean gd_key_event(GtkWidget *widget, GdkEventKey *key, void *opaque)
{
    VirtualConsole *vc = opaque;
    GtkDisplayState *s = vc->s;
    int gdk_keycode = key->hardware_keycode;
    int qemu_keycode;
    int i;

    if (s->ignore_keys) {
        s->ignore_keys = (key->type == GDK_KEY_PRESS);
        return TRUE;
    }

    if (key->keyval == GDK_KEY_Pause) {
        qemu_input_event_send_key_qcode(vc->gfx.dcl.con, Q_KEY_CODE_PAUSE,
                                        key->type == GDK_KEY_PRESS);
        return TRUE;
    }

    qemu_keycode = gd_map_keycode(s, gtk_widget_get_display(widget),
                                  gdk_keycode);

    trace_gd_key_event(vc->label, gdk_keycode, qemu_keycode,
                       (key->type == GDK_KEY_PRESS) ? "down" : "up");

    for (i = 0; i < ARRAY_SIZE(modifier_keycode); i++) {
        if (qemu_keycode == modifier_keycode[i]) {
            s->modifier_pressed[i] = (key->type == GDK_KEY_PRESS);
        }
    }

    qemu_input_event_send_key_number(vc->gfx.dcl.con, qemu_keycode,
                                     key->type == GDK_KEY_PRESS);

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
    s->ignore_keys = false;
}

static void gd_accel_switch_vc(void *opaque)
{
    VirtualConsole *vc = opaque;

    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(vc->menu_item), TRUE);
#if !GTK_CHECK_VERSION(3, 0, 0)
    /* GTK2 sends the accel key to the target console - ignore this until */
    vc->s->ignore_keys = true;
#endif
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
        gtk_widget_show(s->menu_bar);
        s->full_screen = FALSE;
        if (vc->type == GD_VC_GFX) {
            vc->gfx.scale_x = 1.0;
            vc->gfx.scale_y = 1.0;
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

    vc->gfx.scale_x = 1.0;
    vc->gfx.scale_y = 1.0;

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
        vc->gfx.scale_x = 1.0;
        vc->gfx.scale_y = 1.0;
    }

    gd_update_windowsize(vc);
    gd_update_full_redraw(vc);
}

#if GTK_CHECK_VERSION(3, 0, 0)
static void gd_grab_devices(VirtualConsole *vc, bool grab,
                            GdkInputSource source, GdkEventMask mask,
                            GdkCursor *cursor)
{
    GdkDisplay *display = gtk_widget_get_display(vc->gfx.drawing_area);
    GdkDeviceManager *mgr = gdk_display_get_device_manager(display);
    GList *devs = gdk_device_manager_list_devices(mgr, GDK_DEVICE_TYPE_MASTER);
    GList *tmp = devs;

    for (tmp = devs; tmp; tmp = tmp->next) {
        GdkDevice *dev = tmp->data;
        if (gdk_device_get_source(dev) != source) {
            continue;
        }
        if (grab) {
            GdkWindow *win = gtk_widget_get_window(vc->gfx.drawing_area);
            gdk_device_grab(dev, win, GDK_OWNERSHIP_NONE, FALSE,
                            mask, cursor, GDK_CURRENT_TIME);
        } else {
            gdk_device_ungrab(dev, GDK_CURRENT_TIME);
        }
    }
    g_list_free(devs);
}
#endif

static void gd_grab_keyboard(VirtualConsole *vc, const char *reason)
{
    if (vc->s->kbd_owner) {
        if (vc->s->kbd_owner == vc) {
            return;
        } else {
            gd_ungrab_keyboard(vc->s);
        }
    }

#if GTK_CHECK_VERSION(3, 0, 0)
    gd_grab_devices(vc, true, GDK_SOURCE_KEYBOARD,
                   GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK,
                   NULL);
#else
    gdk_keyboard_grab(gtk_widget_get_window(vc->gfx.drawing_area),
                      FALSE,
                      GDK_CURRENT_TIME);
#endif
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

#if GTK_CHECK_VERSION(3, 0, 0)
    gd_grab_devices(vc, false, GDK_SOURCE_KEYBOARD, 0, NULL);
#else
    gdk_keyboard_ungrab(GDK_CURRENT_TIME);
#endif
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

#if GTK_CHECK_VERSION(3, 0, 0)
    GdkDeviceManager *mgr = gdk_display_get_device_manager(display);
    gd_grab_devices(vc, true, GDK_SOURCE_MOUSE,
                    GDK_POINTER_MOTION_MASK |
                    GDK_BUTTON_PRESS_MASK |
                    GDK_BUTTON_RELEASE_MASK |
                    GDK_BUTTON_MOTION_MASK |
                    GDK_SCROLL_MASK,
                    vc->s->null_cursor);
    gdk_device_get_position(gdk_device_manager_get_client_pointer(mgr),
                            NULL, &vc->s->grab_x_root, &vc->s->grab_y_root);
#else
    gdk_pointer_grab(gtk_widget_get_window(vc->gfx.drawing_area),
                     FALSE, /* All events to come to our window directly */
                     GDK_POINTER_MOTION_MASK |
                     GDK_BUTTON_PRESS_MASK |
                     GDK_BUTTON_RELEASE_MASK |
                     GDK_BUTTON_MOTION_MASK |
                     GDK_SCROLL_MASK,
                     NULL, /* Allow cursor to move over entire desktop */
                     vc->s->null_cursor,
                     GDK_CURRENT_TIME);
    gdk_display_get_pointer(display, NULL,
                            &vc->s->grab_x_root, &vc->s->grab_y_root, NULL);
#endif
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

    GdkDisplay *display = gtk_widget_get_display(vc->gfx.drawing_area);
#if GTK_CHECK_VERSION(3, 0, 0)
    GdkDeviceManager *mgr = gdk_display_get_device_manager(display);
    gd_grab_devices(vc, false, GDK_SOURCE_MOUSE, 0, NULL);
    gdk_device_warp(gdk_device_manager_get_client_pointer(mgr),
                    gtk_widget_get_screen(vc->gfx.drawing_area),
                    vc->s->grab_x_root, vc->s->grab_y_root);
#else
    gdk_pointer_ungrab(GDK_CURRENT_TIME);
    gdk_display_warp_pointer(display,
                             gtk_widget_get_screen(vc->gfx.drawing_area),
                             vc->s->grab_x_root, vc->s->grab_y_root);
#endif
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

#ifdef VTE_RESIZE_HACK
    vc = gd_vc_find_current(s);
    if (vc && vc->type == GD_VC_VTE) {
        gtk_widget_hide(vc->vte.terminal);
    }
#endif
    vc = gd_vc_find_by_page(s, arg2);
    if (!vc) {
        return;
    }
#ifdef VTE_RESIZE_HACK
    if (vc->type == GD_VC_VTE) {
        gtk_widget_show(vc->vte.terminal);
    }
#endif
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

static gboolean gd_focus_out_event(GtkWidget *widget,
                                   GdkEventCrossing *crossing, gpointer opaque)
{
    VirtualConsole *vc = opaque;
    GtkDisplayState *s = vc->s;

    gtk_release_modifiers(s);
    return TRUE;
}

static gboolean gd_configure(GtkWidget *widget,
                             GdkEventConfigure *cfg, gpointer opaque)
{
    VirtualConsole *vc = opaque;

    gd_set_ui_info(vc, cfg->width, cfg->height);
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
#if GTK_CHECK_VERSION(3, 8, 0)
    gtk_accel_label_set_accel(
            GTK_ACCEL_LABEL(gtk_bin_get_child(GTK_BIN(vc->menu_item))),
            GDK_KEY_1 + idx, HOTKEY_MODIFIERS);
#endif

    g_signal_connect(vc->menu_item, "activate",
                     G_CALLBACK(gd_menu_switch_vc), s);
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), vc->menu_item);

    group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(vc->menu_item));
    return group;
}

#if defined(CONFIG_VTE)
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

static int gd_vc_chr_write(CharDriverState *chr, const uint8_t *buf, int len)
{
    VirtualConsole *vc = chr->opaque;

    vte_terminal_feed(VTE_TERMINAL(vc->vte.terminal), (const char *)buf, len);
    return len;
}

static void gd_vc_chr_set_echo(CharDriverState *chr, bool echo)
{
    VirtualConsole *vc = chr->opaque;

    vc->vte.echo = echo;
}

static int nb_vcs;
static CharDriverState *vcs[MAX_VCS];

static CharDriverState *gd_vc_handler(ChardevVC *vc, Error **errp)
{
    ChardevCommon *common = qapi_ChardevVC_base(vc);
    CharDriverState *chr;

    chr = qemu_chr_alloc(common, errp);
    if (!chr) {
        return NULL;
    }

    chr->chr_write = gd_vc_chr_write;
    chr->chr_set_echo = gd_vc_chr_set_echo;

    /* Temporary, until gd_vc_vte_init runs.  */
    chr->opaque = g_new0(VirtualConsole, 1);

    /* defer OPENED events until our vc is fully initialized */
    chr->explicit_be_open = true;

    vcs[nb_vcs++] = chr;

    return chr;
}

static gboolean gd_vc_in(VteTerminal *terminal, gchar *text, guint size,
                         gpointer user_data)
{
    VirtualConsole *vc = user_data;

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

    qemu_chr_be_write(vc->vte.chr, (uint8_t  *)text, (unsigned int)size);
    return TRUE;
}

static GSList *gd_vc_vte_init(GtkDisplayState *s, VirtualConsole *vc,
                              CharDriverState *chr, int idx,
                              GSList *group, GtkWidget *view_menu)
{
    char buffer[32];
    GtkWidget *box;
    GtkWidget *scrollbar;
    GtkAdjustment *vadjustment;
    VirtualConsole *tmp_vc = chr->opaque;

    vc->s = s;
    vc->vte.echo = tmp_vc->vte.echo;

    vc->vte.chr = chr;
    chr->opaque = vc;
    g_free(tmp_vc);

    snprintf(buffer, sizeof(buffer), "vc%d", idx);
    vc->label = g_strdup_printf("%s", vc->vte.chr->label
                                ? vc->vte.chr->label : buffer);
    group = gd_vc_menu_init(s, vc, idx, group, view_menu);

    vc->vte.terminal = vte_terminal_new();
    g_signal_connect(vc->vte.terminal, "commit", G_CALLBACK(gd_vc_in), vc);

    /* The documentation says that the default is UTF-8, but actually it is
     * 7-bit ASCII at least in VTE 0.38.
     */
#if VTE_CHECK_VERSION(0, 40, 0)
    vte_terminal_set_encoding(VTE_TERMINAL(vc->vte.terminal), "UTF-8", NULL);
#else
    vte_terminal_set_encoding(VTE_TERMINAL(vc->vte.terminal), "UTF-8");
#endif

    vte_terminal_set_scrollback_lines(VTE_TERMINAL(vc->vte.terminal), -1);
    vte_terminal_set_size(VTE_TERMINAL(vc->vte.terminal),
                          VC_TERM_X_MIN, VC_TERM_Y_MIN);

#if VTE_CHECK_VERSION(0, 28, 0) && GTK_CHECK_VERSION(3, 0, 0)
    vadjustment = gtk_scrollable_get_vadjustment
        (GTK_SCROLLABLE(vc->vte.terminal));
#else
    vadjustment = vte_terminal_get_adjustment(VTE_TERMINAL(vc->vte.terminal));
#endif

#if GTK_CHECK_VERSION(3, 0, 0)
    box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    scrollbar = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, vadjustment);
#else
    box = gtk_hbox_new(false, 2);
    scrollbar = gtk_vscrollbar_new(vadjustment);
#endif

    gtk_box_pack_start(GTK_BOX(box), vc->vte.terminal, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), scrollbar, FALSE, FALSE, 0);

    vc->vte.box = box;
    vc->vte.scrollbar = scrollbar;

    g_signal_connect(vadjustment, "changed",
                     G_CALLBACK(gd_vc_adjustment_changed), vc);

    vc->type = GD_VC_VTE;
    vc->tab_item = box;
    vc->focus = vc->vte.terminal;
    gtk_notebook_append_page(GTK_NOTEBOOK(s->notebook), vc->tab_item,
                             gtk_label_new(vc->label));

    qemu_chr_be_generic_open(vc->vte.chr);
    if (vc->vte.chr->init) {
        vc->vte.chr->init(vc->vte.chr);
    }

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
#if GTK_CHECK_VERSION(3, 0, 0)
    g_signal_connect(vc->gfx.drawing_area, "draw",
                     G_CALLBACK(gd_draw_event), vc);
#if defined(CONFIG_GTK_GL)
    if (display_opengl) {
        /* wire up GtkGlArea events */
        g_signal_connect(vc->gfx.drawing_area, "render",
                         G_CALLBACK(gd_render_event), vc);
        g_signal_connect(vc->gfx.drawing_area, "resize",
                         G_CALLBACK(gd_resize_event), vc);
    }
#endif
#else
    g_signal_connect(vc->gfx.drawing_area, "expose-event",
                     G_CALLBACK(gd_expose_event), vc);
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

        g_signal_connect(vc->gfx.drawing_area, "enter-notify-event",
                         G_CALLBACK(gd_enter_event), vc);
        g_signal_connect(vc->gfx.drawing_area, "leave-notify-event",
                         G_CALLBACK(gd_leave_event), vc);
        g_signal_connect(vc->gfx.drawing_area, "focus-out-event",
                         G_CALLBACK(gd_focus_out_event), vc);
        g_signal_connect(vc->gfx.drawing_area, "configure-event",
                         G_CALLBACK(gd_configure), vc);
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

static GSList *gd_vc_gfx_init(GtkDisplayState *s, VirtualConsole *vc,
                              QemuConsole *con, int idx,
                              GSList *group, GtkWidget *view_menu)
{
    vc->label = qemu_console_get_label(con);
    vc->s = s;
    vc->gfx.scale_x = 1.0;
    vc->gfx.scale_y = 1.0;

#if defined(CONFIG_OPENGL)
    if (display_opengl) {
#if defined(CONFIG_GTK_GL)
        vc->gfx.drawing_area = gtk_gl_area_new();
        vc->gfx.dcl.ops = &dcl_gl_area_ops;
#else
        vc->gfx.drawing_area = gtk_drawing_area_new();
        /*
         * gtk_widget_set_double_buffered() was deprecated in 3.14.
         * It is required for opengl rendering on X11 though.  A
         * proper replacement (native opengl support) is only
         * available in 3.16+.  Silence the warning if possible.
         */
#ifdef CONFIG_PRAGMA_DIAGNOSTIC_AVAILABLE
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
        gtk_widget_set_double_buffered(vc->gfx.drawing_area, FALSE);
#ifdef CONFIG_PRAGMA_DIAGNOSTIC_AVAILABLE
#pragma GCC diagnostic pop
#endif
        vc->gfx.dcl.ops = &dcl_egl_ops;
#endif /* CONFIG_GTK_GL */
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
                          GDK_ENTER_NOTIFY_MASK |
                          GDK_LEAVE_NOTIFY_MASK |
                          GDK_SCROLL_MASK |
                          GDK_KEY_PRESS_MASK);
    gtk_widget_set_can_focus(vc->gfx.drawing_area, TRUE);

    vc->type = GD_VC_GFX;
    vc->tab_item = vc->gfx.drawing_area;
    vc->focus = vc->gfx.drawing_area;
    gtk_notebook_append_page(GTK_NOTEBOOK(s->notebook),
                             vc->tab_item, gtk_label_new(vc->label));

    vc->gfx.dcl.con = con;
    register_displaychangelistener(&vc->gfx.dcl);

    gd_connect_vc_gfx_signals(vc);
    group = gd_vc_menu_init(s, vc, idx, group, view_menu);

    if (dpy_ui_info_supported(vc->gfx.dcl.con)) {
        gtk_menu_item_activate(GTK_MENU_ITEM(s->zoom_fit_item));
        s->free_scale = true;
    }

    return group;
}

static GtkWidget *gd_create_menu_view(GtkDisplayState *s)
{
    GSList *group = NULL;
    GtkWidget *view_menu;
    GtkWidget *separator;
    QemuConsole *con;
    int vc;

    view_menu = gtk_menu_new();
    gtk_menu_set_accel_group(GTK_MENU(view_menu), s->accel_group);

    s->full_screen_item = gtk_menu_item_new_with_mnemonic(_("_Fullscreen"));

    gtk_accel_group_connect(s->accel_group, GDK_KEY_f, HOTKEY_MODIFIERS, 0,
            g_cclosure_new_swap(G_CALLBACK(gd_accel_full_screen), s, NULL));
#if GTK_CHECK_VERSION(3, 8, 0)
    gtk_accel_label_set_accel(
            GTK_ACCEL_LABEL(gtk_bin_get_child(GTK_BIN(s->full_screen_item))),
            GDK_KEY_f, HOTKEY_MODIFIERS);
#endif
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), s->full_screen_item);

    separator = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), separator);

    s->zoom_in_item = gtk_menu_item_new_with_mnemonic(_("Zoom _In"));
    gtk_menu_item_set_accel_path(GTK_MENU_ITEM(s->zoom_in_item),
                                 "<QEMU>/View/Zoom In");
    gtk_accel_map_add_entry("<QEMU>/View/Zoom In", GDK_KEY_plus,
                            HOTKEY_MODIFIERS);
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

    return view_menu;
}

static void gd_create_menus(GtkDisplayState *s)
{
    s->accel_group = gtk_accel_group_new();
    s->machine_menu = gd_create_menu_machine(s);
    s->view_menu = gd_create_menu_view(s);

    s->machine_menu_item = gtk_menu_item_new_with_mnemonic(_("_Machine"));
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(s->machine_menu_item),
                              s->machine_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(s->menu_bar), s->machine_menu_item);

    s->view_menu_item = gtk_menu_item_new_with_mnemonic(_("_View"));
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(s->view_menu_item), s->view_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(s->menu_bar), s->view_menu_item);

    g_object_set_data(G_OBJECT(s->window), "accel_group", s->accel_group);
    gtk_window_add_accel_group(GTK_WINDOW(s->window), s->accel_group);
}

static void gd_set_keycode_type(GtkDisplayState *s)
{
#ifdef GDK_WINDOWING_X11
    GdkDisplay *display = gtk_widget_get_display(s->window);
    if (GDK_IS_X11_DISPLAY(display)) {
        Display *x11_display = gdk_x11_display_get_xdisplay(display);
        XkbDescPtr desc = XkbGetKeyboard(x11_display, XkbGBN_AllComponentsMask,
                                         XkbUseCoreKbd);
        char *keycodes = NULL;

        if (desc && desc->names) {
            keycodes = XGetAtomName(x11_display, desc->names->keycodes);
        }
        if (keycodes == NULL) {
            fprintf(stderr, "could not lookup keycode name\n");
        } else if (strstart(keycodes, "evdev", NULL)) {
            s->has_evdev = true;
        } else if (!strstart(keycodes, "xfree86", NULL)) {
            fprintf(stderr, "unknown keycodes `%s', please report to "
                    "qemu-devel@nongnu.org\n", keycodes);
        }

        if (desc) {
            XkbFreeKeyboard(desc, XkbGBN_AllComponentsMask, True);
        }
        if (keycodes) {
            XFree(keycodes);
        }
    }
#endif
}

static gboolean gtkinit;

void gtk_display_init(DisplayState *ds, bool full_screen, bool grab_on_hover)
{
    GtkDisplayState *s = g_malloc0(sizeof(*s));
    char *filename;
    GdkDisplay *window_display;

    if (!gtkinit) {
        fprintf(stderr, "gtk initialization failed\n");
        exit(1);
    }

    s->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
#if GTK_CHECK_VERSION(3, 2, 0)
    s->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
#else
    s->vbox = gtk_vbox_new(FALSE, 0);
#endif
    s->notebook = gtk_notebook_new();
    s->menu_bar = gtk_menu_bar_new();

    s->free_scale = FALSE;

    /* LC_MESSAGES only. See early_gtk_display_init() for details */
    setlocale(LC_MESSAGES, "");
    bindtextdomain("qemu", CONFIG_QEMU_LOCALEDIR);
    textdomain("qemu");

    window_display = gtk_widget_get_display(s->window);
    s->null_cursor = gdk_cursor_new_for_display(window_display,
                                                GDK_BLANK_CURSOR);

    s->mouse_mode_notifier.notify = gd_mouse_mode_change;
    qemu_add_mouse_mode_change_notifier(&s->mouse_mode_notifier);
    qemu_add_vm_change_state_handler(gd_change_runstate, s);

    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, "qemu_logo_no_text.svg");
    if (filename) {
        GError *error = NULL;
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(filename, &error);
        if (pixbuf) {
            gtk_window_set_icon(GTK_WINDOW(s->window), pixbuf);
        } else {
            g_error_free(error);
        }
        g_free(filename);
    }

    gd_create_menus(s);

    gd_connect_signals(s);

    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(s->notebook), FALSE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(s->notebook), FALSE);

    gd_update_caption(s);

    gtk_box_pack_start(GTK_BOX(s->vbox), s->menu_bar, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(s->vbox), s->notebook, TRUE, TRUE, 0);

    gtk_container_add(GTK_CONTAINER(s->window), s->vbox);

    gtk_widget_show_all(s->window);

#ifdef VTE_RESIZE_HACK
    {
        VirtualConsole *cur = gd_vc_find_current(s);
        if (cur) {
            int i;

            for (i = 0; i < s->nb_vcs; i++) {
                VirtualConsole *vc = &s->vc[i];
                if (vc && vc->type == GD_VC_VTE && vc != cur) {
                    gtk_widget_hide(vc->vte.terminal);
                }
            }
            gd_update_windowsize(cur);
        }
    }
#endif

    if (full_screen) {
        gtk_menu_item_activate(GTK_MENU_ITEM(s->full_screen_item));
    }
    if (grab_on_hover) {
        gtk_menu_item_activate(GTK_MENU_ITEM(s->grab_on_hover_item));
    }

    gd_set_keycode_type(s);
}

void early_gtk_display_init(int opengl)
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

    switch (opengl) {
    case -1: /* default */
    case 0:  /* off */
        break;
    case 1: /* on */
#if defined(CONFIG_OPENGL)
#if defined(CONFIG_GTK_GL)
        gtk_gl_area_init();
#else
        gtk_egl_init();
#endif
#endif
        break;
    default:
        g_assert_not_reached();
        break;
    }

#if defined(CONFIG_VTE)
    register_vc_handler(gd_vc_handler);
#endif
}
