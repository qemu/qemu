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

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <vte/vte.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <pty.h>
#include <math.h>

#include "qemu-common.h"
#include "ui/console.h"
#include "sysemu/sysemu.h"
#include "qmp-commands.h"
#include "x_keymap.h"
#include "keymaps.h"

//#define DEBUG_GTK

#ifdef DEBUG_GTK
#define DPRINTF(fmt, ...) printf(fmt, ## __VA_ARGS__)
#else
#define DPRINTF(fmt, ...) do { } while (0)
#endif

typedef struct VirtualConsole
{
    GtkWidget *menu_item;
    GtkWidget *terminal;
    GtkWidget *scrolled_window;
    CharDriverState *chr;
    int fd;
} VirtualConsole;

typedef struct GtkDisplayState
{
    GtkWidget *window;

    GtkWidget *menu_bar;

    GtkWidget *file_menu_item;
    GtkWidget *file_menu;
    GtkWidget *quit_item;

    GtkWidget *view_menu_item;
    GtkWidget *view_menu;
    GtkWidget *vga_item;

    GtkWidget *show_tabs_item;

    GtkWidget *vbox;
    GtkWidget *notebook;
    GtkWidget *drawing_area;
    cairo_surface_t *surface;
    DisplayChangeListener dcl;
    DisplayState *ds;
    int button_mask;
    int last_x;
    int last_y;

    double scale_x;
    double scale_y;

    GdkCursor *null_cursor;
    Notifier mouse_mode_notifier;
} GtkDisplayState;

static GtkDisplayState *global_state;

/** Utility Functions **/

static void gd_update_cursor(GtkDisplayState *s, gboolean override)
{
    GdkWindow *window;
    bool on_vga;

    window = gtk_widget_get_window(GTK_WIDGET(s->drawing_area));

    on_vga = (gtk_notebook_get_current_page(GTK_NOTEBOOK(s->notebook)) == 0);

    if ((override || on_vga) && kbd_mouse_is_absolute()) {
        gdk_window_set_cursor(window, s->null_cursor);
    } else {
        gdk_window_set_cursor(window, NULL);
    }
}

static void gd_update_caption(GtkDisplayState *s)
{
    const char *status = "";
    gchar *title;

    if (!runstate_is_running()) {
        status = " [Stopped]";
    }

    if (qemu_name) {
        title = g_strdup_printf("QEMU (%s)%s", qemu_name, status);
    } else {
        title = g_strdup_printf("QEMU%s", status);
    }

    gtk_window_set_title(GTK_WINDOW(s->window), title);

    g_free(title);
}

/** DisplayState Callbacks **/

static void gd_update(DisplayState *ds, int x, int y, int w, int h)
{
    GtkDisplayState *s = ds->opaque;
    int x1, x2, y1, y2;

    DPRINTF("update(x=%d, y=%d, w=%d, h=%d)\n", x, y, w, h);

    x1 = floor(x * s->scale_x);
    y1 = floor(y * s->scale_y);

    x2 = ceil(x * s->scale_x + w * s->scale_x);
    y2 = ceil(y * s->scale_y + h * s->scale_y);

    gtk_widget_queue_draw_area(s->drawing_area, x1, y1, (x2 - x1), (y2 - y1));
}

static void gd_refresh(DisplayState *ds)
{
    vga_hw_update();
}

static void gd_resize(DisplayState *ds)
{
    GtkDisplayState *s = ds->opaque;
    cairo_format_t kind;
    int stride;

    DPRINTF("resize(width=%d, height=%d)\n",
            ds_get_width(ds), ds_get_height(ds));

    if (s->surface) {
        cairo_surface_destroy(s->surface);
    }

    switch (ds->surface->pf.bits_per_pixel) {
    case 8:
        kind = CAIRO_FORMAT_A8;
        break;
    case 16:
        kind = CAIRO_FORMAT_RGB16_565;
        break;
    case 32:
        kind = CAIRO_FORMAT_RGB24;
        break;
    default:
        g_assert_not_reached();
        break;
    }

    stride = cairo_format_stride_for_width(kind, ds_get_width(ds));
    g_assert(ds_get_linesize(ds) == stride);

    s->surface = cairo_image_surface_create_for_data(ds_get_data(ds),
                                                     kind,
                                                     ds_get_width(ds),
                                                     ds_get_height(ds),
                                                     ds_get_linesize(ds));

    gtk_widget_set_size_request(s->drawing_area,
                                ds_get_width(ds) * s->scale_x,
                                ds_get_height(ds) * s->scale_y);
}

/** QEMU Events **/

static void gd_change_runstate(void *opaque, int running, RunState state)
{
    GtkDisplayState *s = opaque;

    gd_update_caption(s);
}

static void gd_mouse_mode_change(Notifier *notify, void *data)
{
    gd_update_cursor(container_of(notify, GtkDisplayState, mouse_mode_notifier),
                     FALSE);
}

/** GTK Events **/

static gboolean gd_window_close(GtkWidget *widget, GdkEvent *event,
                                void *opaque)
{
    GtkDisplayState *s = opaque;

    if (!no_quit) {
        unregister_displaychangelistener(s->ds, &s->dcl);
        qmp_quit(NULL);
        return FALSE;
    }

    return TRUE;
}

static gboolean gd_draw_event(GtkWidget *widget, cairo_t *cr, void *opaque)
{
    GtkDisplayState *s = opaque;
    int ww, wh;
    int fbw, fbh;

    fbw = ds_get_width(s->ds);
    fbh = ds_get_height(s->ds);

    gdk_drawable_get_size(gtk_widget_get_window(widget), &ww, &wh);

    cairo_rectangle(cr, 0, 0, ww, wh);

    if (ww != fbw || wh != fbh) {
        s->scale_x = (double)ww / fbw;
        s->scale_y = (double)wh / fbh;
        cairo_scale(cr, s->scale_x, s->scale_y);
    } else {
        s->scale_x = 1.0;
        s->scale_y = 1.0;
    }

    cairo_set_source_surface(cr, s->surface, 0, 0);
    cairo_paint(cr);

    return TRUE;
}

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

static gboolean gd_motion_event(GtkWidget *widget, GdkEventMotion *motion,
                                void *opaque)
{
    GtkDisplayState *s = opaque;
    int dx, dy;
    int x, y;

    x = motion->x / s->scale_x;
    y = motion->y / s->scale_y;

    if (kbd_mouse_is_absolute()) {
        dx = x * 0x7FFF / (ds_get_width(s->ds) - 1);
        dy = y * 0x7FFF / (ds_get_height(s->ds) - 1);
    } else if (s->last_x == -1 || s->last_y == -1) {
        dx = 0;
        dy = 0;
    } else {
        dx = x - s->last_x;
        dy = y - s->last_y;
    }

    s->last_x = x;
    s->last_y = y;

    if (kbd_mouse_is_absolute()) {
        kbd_mouse_event(dx, dy, 0, s->button_mask);
    }

    return TRUE;
}

static gboolean gd_button_event(GtkWidget *widget, GdkEventButton *button,
                                void *opaque)
{
    GtkDisplayState *s = opaque;
    int dx, dy;
    int n;

    if (button->button == 1) {
        n = 0x01;
    } else if (button->button == 2) {
        n = 0x04;
    } else if (button->button == 3) {
        n = 0x02;
    } else {
        n = 0x00;
    }

    if (button->type == GDK_BUTTON_PRESS) {
        s->button_mask |= n;
    } else if (button->type == GDK_BUTTON_RELEASE) {
        s->button_mask &= ~n;
    }

    if (kbd_mouse_is_absolute()) {
        dx = s->last_x * 0x7FFF / (ds_get_width(s->ds) - 1);
        dy = s->last_y * 0x7FFF / (ds_get_height(s->ds) - 1);
    } else {
        dx = 0;
        dy = 0;
    }

    kbd_mouse_event(dx, dy, 0, s->button_mask);
        
    return TRUE;
}

static gboolean gd_key_event(GtkWidget *widget, GdkEventKey *key, void *opaque)
{
    int gdk_keycode;
    int qemu_keycode;

    gdk_keycode = key->hardware_keycode;

    if (gdk_keycode < 9) {
        qemu_keycode = 0;
    } else if (gdk_keycode < 97) {
        qemu_keycode = gdk_keycode - 8;
    } else if (gdk_keycode < 158) {
        qemu_keycode = translate_evdev_keycode(gdk_keycode - 97);
    } else if (gdk_keycode == 208) { /* Hiragana_Katakana */
        qemu_keycode = 0x70;
    } else if (gdk_keycode == 211) { /* backslash */
        qemu_keycode = 0x73;
    } else {
        qemu_keycode = 0;
    }

    DPRINTF("translated GDK keycode %d to QEMU keycode %d (%s)\n",
            gdk_keycode, qemu_keycode,
            (key->type == GDK_KEY_PRESS) ? "down" : "up");

    if (qemu_keycode & SCANCODE_GREY) {
        kbd_put_keycode(SCANCODE_EMUL0);
    }

    if (key->type == GDK_KEY_PRESS) {
        kbd_put_keycode(qemu_keycode & SCANCODE_KEYCODEMASK);
    } else if (key->type == GDK_KEY_RELEASE) {
        kbd_put_keycode(qemu_keycode | SCANCODE_UP);
    } else {
        g_assert_not_reached();
    }

    return TRUE;
}

/** Window Menu Actions **/

static void gd_menu_quit(GtkMenuItem *item, void *opaque)
{
    qmp_quit(NULL);
}

static void gd_menu_switch_vc(GtkMenuItem *item, void *opaque)
{
    GtkDisplayState *s = opaque;

    if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(s->vga_item))) {
        gtk_notebook_set_current_page(GTK_NOTEBOOK(s->notebook), 0);
    }
}

static void gd_menu_show_tabs(GtkMenuItem *item, void *opaque)
{
    GtkDisplayState *s = opaque;

    if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(s->show_tabs_item))) {
        gtk_notebook_set_show_tabs(GTK_NOTEBOOK(s->notebook), TRUE);
    } else {
        gtk_notebook_set_show_tabs(GTK_NOTEBOOK(s->notebook), FALSE);
    }
}

static void gd_change_page(GtkNotebook *nb, gpointer arg1, guint arg2,
                           gpointer data)
{
    GtkDisplayState *s = data;

    if (!gtk_widget_get_realized(s->notebook)) {
        return;
    }

    gd_update_cursor(s, TRUE);
}

void early_gtk_display_init(void)
{
}

/** Window Creation **/

static void gd_connect_signals(GtkDisplayState *s)
{
    g_signal_connect(s->show_tabs_item, "activate",
                     G_CALLBACK(gd_menu_show_tabs), s);

    g_signal_connect(s->window, "delete-event",
                     G_CALLBACK(gd_window_close), s);

    g_signal_connect(s->drawing_area, "expose-event",
                     G_CALLBACK(gd_expose_event), s);
    g_signal_connect(s->drawing_area, "motion-notify-event",
                     G_CALLBACK(gd_motion_event), s);
    g_signal_connect(s->drawing_area, "button-press-event",
                     G_CALLBACK(gd_button_event), s);
    g_signal_connect(s->drawing_area, "button-release-event",
                     G_CALLBACK(gd_button_event), s);
    g_signal_connect(s->drawing_area, "key-press-event",
                     G_CALLBACK(gd_key_event), s);
    g_signal_connect(s->drawing_area, "key-release-event",
                     G_CALLBACK(gd_key_event), s);

    g_signal_connect(s->quit_item, "activate",
                     G_CALLBACK(gd_menu_quit), s);
    g_signal_connect(s->vga_item, "activate",
                     G_CALLBACK(gd_menu_switch_vc), s);
    g_signal_connect(s->notebook, "switch-page",
                     G_CALLBACK(gd_change_page), s);
}

static void gd_create_menus(GtkDisplayState *s)
{
    GtkStockItem item;
    GtkAccelGroup *accel_group;
    GSList *group = NULL;
    GtkWidget *separator;

    accel_group = gtk_accel_group_new();
    s->file_menu = gtk_menu_new();
    gtk_menu_set_accel_group(GTK_MENU(s->file_menu), accel_group);
    s->file_menu_item = gtk_menu_item_new_with_mnemonic("_File");

    s->quit_item = gtk_image_menu_item_new_from_stock(GTK_STOCK_QUIT, NULL);
    gtk_stock_lookup(GTK_STOCK_QUIT, &item);
    gtk_menu_item_set_accel_path(GTK_MENU_ITEM(s->quit_item),
                                 "<QEMU>/File/Quit");
    gtk_accel_map_add_entry("<QEMU>/File/Quit", item.keyval, item.modifier);

    s->view_menu = gtk_menu_new();
    gtk_menu_set_accel_group(GTK_MENU(s->view_menu), accel_group);
    s->view_menu_item = gtk_menu_item_new_with_mnemonic("_View");

    separator = gtk_separator_menu_item_new();
    gtk_menu_append(GTK_MENU(s->view_menu), separator);

    s->vga_item = gtk_radio_menu_item_new_with_mnemonic(group, "_VGA");
    group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(s->vga_item));
    gtk_menu_item_set_accel_path(GTK_MENU_ITEM(s->vga_item),
                                 "<QEMU>/View/VGA");
    gtk_accel_map_add_entry("<QEMU>/View/VGA", GDK_KEY_1, GDK_CONTROL_MASK | GDK_MOD1_MASK);
    gtk_menu_append(GTK_MENU(s->view_menu), s->vga_item);

    separator = gtk_separator_menu_item_new();
    gtk_menu_append(GTK_MENU(s->view_menu), separator);

    s->show_tabs_item = gtk_check_menu_item_new_with_mnemonic("Show _Tabs");
    gtk_menu_append(GTK_MENU(s->view_menu), s->show_tabs_item);

    g_object_set_data(G_OBJECT(s->window), "accel_group", accel_group);
    gtk_window_add_accel_group(GTK_WINDOW(s->window), accel_group);

    gtk_menu_append(GTK_MENU(s->file_menu), s->quit_item);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(s->file_menu_item), s->file_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(s->menu_bar), s->file_menu_item);

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(s->view_menu_item), s->view_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(s->menu_bar), s->view_menu_item);
}

void gtk_display_init(DisplayState *ds)
{
    GtkDisplayState *s = g_malloc0(sizeof(*s));

    gtk_init(NULL, NULL);

    ds->opaque = s;
    s->ds = ds;
    s->dcl.dpy_gfx_update = gd_update;
    s->dcl.dpy_gfx_resize = gd_resize;
    s->dcl.dpy_refresh = gd_refresh;

    s->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    s->vbox = gtk_vbox_new(FALSE, 0);
    s->notebook = gtk_notebook_new();
    s->drawing_area = gtk_drawing_area_new();
    s->menu_bar = gtk_menu_bar_new();

    s->scale_x = 1.0;
    s->scale_y = 1.0;

    s->null_cursor = gdk_cursor_new(GDK_BLANK_CURSOR);

    s->mouse_mode_notifier.notify = gd_mouse_mode_change;
    qemu_add_mouse_mode_change_notifier(&s->mouse_mode_notifier);
    qemu_add_vm_change_state_handler(gd_change_runstate, s);

    gtk_notebook_append_page(GTK_NOTEBOOK(s->notebook), s->drawing_area, gtk_label_new("VGA"));

    gd_create_menus(s);

    gd_connect_signals(s);

    gtk_widget_add_events(s->drawing_area,
                          GDK_POINTER_MOTION_MASK |
                          GDK_BUTTON_PRESS_MASK |
                          GDK_BUTTON_RELEASE_MASK |
                          GDK_BUTTON_MOTION_MASK |
                          GDK_SCROLL_MASK |
                          GDK_KEY_PRESS_MASK);
    gtk_widget_set_double_buffered(s->drawing_area, FALSE);
    gtk_widget_set_can_focus(s->drawing_area, TRUE);

    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(s->notebook), FALSE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(s->notebook), FALSE);

    gtk_window_set_resizable(GTK_WINDOW(s->window), FALSE);

    gd_update_caption(s);

    gtk_box_pack_start(GTK_BOX(s->vbox), s->menu_bar, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(s->vbox), s->notebook, TRUE, TRUE, 0);

    gtk_container_add(GTK_CONTAINER(s->window), s->vbox);

    gtk_widget_show_all(s->window);

    register_displaychangelistener(ds, &s->dcl);

    global_state = s;
}
