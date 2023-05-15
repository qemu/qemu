/*
 * QEMU DBus display console
 *
 * Copyright (c) 2021 Marc-André Lureau <marcandre.lureau@redhat.com>
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
#include "qemu/error-report.h"
#include "sysemu/sysemu.h"
#include "dbus.h"
#include <gio/gunixfdlist.h>

#ifdef CONFIG_OPENGL
#include "ui/shader.h"
#include "ui/egl-helpers.h"
#include "ui/egl-context.h"
#endif
#include "trace.h"

struct _DBusDisplayListener {
    GObject parent;

    char *bus_name;
    DBusDisplayConsole *console;
    GDBusConnection *conn;

    QemuDBusDisplay1Listener *proxy;

    DisplayChangeListener dcl;
    DisplaySurface *ds;
    int gl_updates;
};

G_DEFINE_TYPE(DBusDisplayListener, dbus_display_listener, G_TYPE_OBJECT)

#if defined(CONFIG_OPENGL) && defined(CONFIG_GBM)
static void dbus_update_gl_cb(GObject *source_object,
                           GAsyncResult *res,
                           gpointer user_data)
{
    g_autoptr(GError) err = NULL;
    DBusDisplayListener *ddl = user_data;

    if (!qemu_dbus_display1_listener_call_update_dmabuf_finish(ddl->proxy,
                                                               res, &err)) {
        error_report("Failed to call update: %s", err->message);
    }

    graphic_hw_gl_block(ddl->dcl.con, false);
    g_object_unref(ddl);
}

static void dbus_call_update_gl(DBusDisplayListener *ddl,
                                int x, int y, int w, int h)
{
    graphic_hw_gl_block(ddl->dcl.con, true);
    glFlush();
    qemu_dbus_display1_listener_call_update_dmabuf(ddl->proxy,
        x, y, w, h,
        G_DBUS_CALL_FLAGS_NONE,
        DBUS_DEFAULT_TIMEOUT, NULL,
        dbus_update_gl_cb,
        g_object_ref(ddl));
}

static void dbus_scanout_disable(DisplayChangeListener *dcl)
{
    DBusDisplayListener *ddl = container_of(dcl, DBusDisplayListener, dcl);

    ddl->ds = NULL;
    qemu_dbus_display1_listener_call_disable(
        ddl->proxy, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}

static void dbus_scanout_dmabuf(DisplayChangeListener *dcl,
                                QemuDmaBuf *dmabuf)
{
    DBusDisplayListener *ddl = container_of(dcl, DBusDisplayListener, dcl);
    g_autoptr(GError) err = NULL;
    g_autoptr(GUnixFDList) fd_list = NULL;

    fd_list = g_unix_fd_list_new();
    if (g_unix_fd_list_append(fd_list, dmabuf->fd, &err) != 0) {
        error_report("Failed to setup dmabuf fdlist: %s", err->message);
        return;
    }

    /* FIXME: add missing x/y/w/h support */
    qemu_dbus_display1_listener_call_scanout_dmabuf(
        ddl->proxy,
        g_variant_new_handle(0),
        dmabuf->width,
        dmabuf->height,
        dmabuf->stride,
        dmabuf->fourcc,
        dmabuf->modifier,
        dmabuf->y0_top,
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        fd_list,
        NULL, NULL, NULL);
}

static void dbus_scanout_texture(DisplayChangeListener *dcl,
                                 uint32_t tex_id,
                                 bool backing_y_0_top,
                                 uint32_t backing_width,
                                 uint32_t backing_height,
                                 uint32_t x, uint32_t y,
                                 uint32_t w, uint32_t h)
{
    QemuDmaBuf dmabuf = {
        .width = backing_width,
        .height = backing_height,
        .y0_top = backing_y_0_top,
        .x = x,
        .y = y,
        .scanout_width = w,
        .scanout_height = h,
    };

    assert(tex_id);
    dmabuf.fd = egl_get_fd_for_texture(
        tex_id, (EGLint *)&dmabuf.stride,
        (EGLint *)&dmabuf.fourcc,
        &dmabuf.modifier);
    if (dmabuf.fd < 0) {
        error_report("%s: failed to get fd for texture", __func__);
        return;
    }

    dbus_scanout_dmabuf(dcl, &dmabuf);
    close(dmabuf.fd);
}

static void dbus_cursor_dmabuf(DisplayChangeListener *dcl,
                               QemuDmaBuf *dmabuf, bool have_hot,
                               uint32_t hot_x, uint32_t hot_y)
{
    DBusDisplayListener *ddl = container_of(dcl, DBusDisplayListener, dcl);
    DisplaySurface *ds;
    GVariant *v_data = NULL;
    egl_fb cursor_fb = EGL_FB_INIT;

    if (!dmabuf) {
        qemu_dbus_display1_listener_call_mouse_set(
            ddl->proxy, 0, 0, false,
            G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
        return;
    }

    egl_dmabuf_import_texture(dmabuf);
    if (!dmabuf->texture) {
        return;
    }
    egl_fb_setup_for_tex(&cursor_fb, dmabuf->width, dmabuf->height,
                         dmabuf->texture, false);
    ds = qemu_create_displaysurface(dmabuf->width, dmabuf->height);
    egl_fb_read(ds, &cursor_fb);

    v_data = g_variant_new_from_data(
        G_VARIANT_TYPE("ay"),
        surface_data(ds),
        surface_width(ds) * surface_height(ds) * 4,
        TRUE,
        (GDestroyNotify)qemu_free_displaysurface,
        ds);
    qemu_dbus_display1_listener_call_cursor_define(
        ddl->proxy,
        surface_width(ds),
        surface_height(ds),
        hot_x,
        hot_y,
        v_data,
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        NULL,
        NULL);
}

static void dbus_cursor_position(DisplayChangeListener *dcl,
                                 uint32_t pos_x, uint32_t pos_y)
{
    DBusDisplayListener *ddl = container_of(dcl, DBusDisplayListener, dcl);

    qemu_dbus_display1_listener_call_mouse_set(
        ddl->proxy, pos_x, pos_y, true,
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}

static void dbus_release_dmabuf(DisplayChangeListener *dcl,
                                QemuDmaBuf *dmabuf)
{
    dbus_scanout_disable(dcl);
}

static void dbus_scanout_update(DisplayChangeListener *dcl,
                                uint32_t x, uint32_t y,
                                uint32_t w, uint32_t h)
{
    DBusDisplayListener *ddl = container_of(dcl, DBusDisplayListener, dcl);

    dbus_call_update_gl(ddl, x, y, w, h);
}

static void dbus_gl_refresh(DisplayChangeListener *dcl)
{
    DBusDisplayListener *ddl = container_of(dcl, DBusDisplayListener, dcl);

    graphic_hw_update(dcl->con);

    if (!ddl->ds || qemu_console_is_gl_blocked(ddl->dcl.con)) {
        return;
    }

    if (ddl->gl_updates) {
        dbus_call_update_gl(ddl, 0, 0,
                            surface_width(ddl->ds), surface_height(ddl->ds));
        ddl->gl_updates = 0;
    }
}
#endif

static void dbus_refresh(DisplayChangeListener *dcl)
{
    graphic_hw_update(dcl->con);
}

#if defined(CONFIG_OPENGL) && defined(CONFIG_GBM)
static void dbus_gl_gfx_update(DisplayChangeListener *dcl,
                               int x, int y, int w, int h)
{
    DBusDisplayListener *ddl = container_of(dcl, DBusDisplayListener, dcl);

    ddl->gl_updates++;
}
#endif

static void dbus_gfx_update(DisplayChangeListener *dcl,
                            int x, int y, int w, int h)
{
    DBusDisplayListener *ddl = container_of(dcl, DBusDisplayListener, dcl);
    pixman_image_t *img;
    GVariant *v_data;
    size_t stride;

    assert(ddl->ds);
    stride = w * DIV_ROUND_UP(PIXMAN_FORMAT_BPP(surface_format(ddl->ds)), 8);

    trace_dbus_update(x, y, w, h);

    if (x == 0 && y == 0 && w == surface_width(ddl->ds) && h == surface_height(ddl->ds)) {
        v_data = g_variant_new_from_data(
            G_VARIANT_TYPE("ay"),
            surface_data(ddl->ds),
            surface_stride(ddl->ds) * surface_height(ddl->ds),
            TRUE,
            (GDestroyNotify)pixman_image_unref,
            pixman_image_ref(ddl->ds->image));
        qemu_dbus_display1_listener_call_scanout(
            ddl->proxy,
            surface_width(ddl->ds),
            surface_height(ddl->ds),
            surface_stride(ddl->ds),
            surface_format(ddl->ds),
            v_data,
            G_DBUS_CALL_FLAGS_NONE,
            DBUS_DEFAULT_TIMEOUT, NULL, NULL, NULL);
        return;
    }

    /* make a copy, since gvariant only handles linear data */
    img = pixman_image_create_bits(surface_format(ddl->ds),
                                   w, h, NULL, stride);
    pixman_image_composite(PIXMAN_OP_SRC, ddl->ds->image, NULL, img,
                           x, y, 0, 0, 0, 0, w, h);

    v_data = g_variant_new_from_data(
        G_VARIANT_TYPE("ay"),
        pixman_image_get_data(img),
        pixman_image_get_stride(img) * h,
        TRUE,
        (GDestroyNotify)pixman_image_unref,
        img);
    qemu_dbus_display1_listener_call_update(ddl->proxy,
        x, y, w, h, pixman_image_get_stride(img), pixman_image_get_format(img),
        v_data,
        G_DBUS_CALL_FLAGS_NONE,
        DBUS_DEFAULT_TIMEOUT, NULL, NULL, NULL);
}

#if defined(CONFIG_OPENGL) && defined(CONFIG_GBM)
static void dbus_gl_gfx_switch(DisplayChangeListener *dcl,
                               struct DisplaySurface *new_surface)
{
    DBusDisplayListener *ddl = container_of(dcl, DBusDisplayListener, dcl);

    ddl->ds = new_surface;
    if (ddl->ds) {
        int width = surface_width(ddl->ds);
        int height = surface_height(ddl->ds);

        /* TODO: lazy send dmabuf (there are unnecessary sent otherwise) */
        dbus_scanout_texture(&ddl->dcl, ddl->ds->texture, false,
                             width, height, 0, 0, width, height);
    }
}
#endif

static void dbus_gfx_switch(DisplayChangeListener *dcl,
                            struct DisplaySurface *new_surface)
{
    DBusDisplayListener *ddl = container_of(dcl, DBusDisplayListener, dcl);

    ddl->ds = new_surface;
    if (!ddl->ds) {
        /* why not call disable instead? */
        return;
    }
}

static void dbus_mouse_set(DisplayChangeListener *dcl,
                           int x, int y, int on)
{
    DBusDisplayListener *ddl = container_of(dcl, DBusDisplayListener, dcl);

    qemu_dbus_display1_listener_call_mouse_set(
        ddl->proxy, x, y, on, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}

static void dbus_cursor_define(DisplayChangeListener *dcl,
                               QEMUCursor *c)
{
    DBusDisplayListener *ddl = container_of(dcl, DBusDisplayListener, dcl);
    GVariant *v_data = NULL;

    v_data = g_variant_new_from_data(
        G_VARIANT_TYPE("ay"),
        c->data,
        c->width * c->height * 4,
        TRUE,
        (GDestroyNotify)cursor_unref,
        cursor_ref(c));

    qemu_dbus_display1_listener_call_cursor_define(
        ddl->proxy,
        c->width,
        c->height,
        c->hot_x,
        c->hot_y,
        v_data,
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        NULL,
        NULL);
}

#if defined(CONFIG_OPENGL) && defined(CONFIG_GBM)
const DisplayChangeListenerOps dbus_gl_dcl_ops = {
    .dpy_name                = "dbus-gl",
    .dpy_gfx_update          = dbus_gl_gfx_update,
    .dpy_gfx_switch          = dbus_gl_gfx_switch,
    .dpy_gfx_check_format    = console_gl_check_format,
    .dpy_refresh             = dbus_gl_refresh,
    .dpy_mouse_set           = dbus_mouse_set,
    .dpy_cursor_define       = dbus_cursor_define,

    .dpy_gl_scanout_disable  = dbus_scanout_disable,
    .dpy_gl_scanout_texture  = dbus_scanout_texture,
    .dpy_gl_scanout_dmabuf   = dbus_scanout_dmabuf,
    .dpy_gl_cursor_dmabuf    = dbus_cursor_dmabuf,
    .dpy_gl_cursor_position  = dbus_cursor_position,
    .dpy_gl_release_dmabuf   = dbus_release_dmabuf,
    .dpy_gl_update           = dbus_scanout_update,
};
#endif

const DisplayChangeListenerOps dbus_dcl_ops = {
    .dpy_name                = "dbus",
    .dpy_gfx_update          = dbus_gfx_update,
    .dpy_gfx_switch          = dbus_gfx_switch,
    .dpy_refresh             = dbus_refresh,
    .dpy_mouse_set           = dbus_mouse_set,
    .dpy_cursor_define       = dbus_cursor_define,
};

static void
dbus_display_listener_dispose(GObject *object)
{
    DBusDisplayListener *ddl = DBUS_DISPLAY_LISTENER(object);

    unregister_displaychangelistener(&ddl->dcl);
    g_clear_object(&ddl->conn);
    g_clear_pointer(&ddl->bus_name, g_free);
    g_clear_object(&ddl->proxy);

    G_OBJECT_CLASS(dbus_display_listener_parent_class)->dispose(object);
}

static void
dbus_display_listener_constructed(GObject *object)
{
    DBusDisplayListener *ddl = DBUS_DISPLAY_LISTENER(object);

    ddl->dcl.ops = &dbus_dcl_ops;
#if defined(CONFIG_OPENGL) && defined(CONFIG_GBM)
    if (display_opengl) {
        ddl->dcl.ops = &dbus_gl_dcl_ops;
    }
#endif

    G_OBJECT_CLASS(dbus_display_listener_parent_class)->constructed(object);
}

static void
dbus_display_listener_class_init(DBusDisplayListenerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->dispose = dbus_display_listener_dispose;
    object_class->constructed = dbus_display_listener_constructed;
}

static void
dbus_display_listener_init(DBusDisplayListener *ddl)
{
}

const char *
dbus_display_listener_get_bus_name(DBusDisplayListener *ddl)
{
    return ddl->bus_name ?: "p2p";
}

DBusDisplayConsole *
dbus_display_listener_get_console(DBusDisplayListener *ddl)
{
    return ddl->console;
}

DBusDisplayListener *
dbus_display_listener_new(const char *bus_name,
                          GDBusConnection *conn,
                          DBusDisplayConsole *console)
{
    DBusDisplayListener *ddl;
    QemuConsole *con;
    g_autoptr(GError) err = NULL;

    ddl = g_object_new(DBUS_DISPLAY_TYPE_LISTENER, NULL);
    ddl->proxy =
        qemu_dbus_display1_listener_proxy_new_sync(conn,
            G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
            NULL,
            "/org/qemu/Display1/Listener",
            NULL,
            &err);
    if (!ddl->proxy) {
        error_report("Failed to setup proxy: %s", err->message);
        g_object_unref(conn);
        g_object_unref(ddl);
        return NULL;
    }

    ddl->bus_name = g_strdup(bus_name);
    ddl->conn = conn;
    ddl->console = console;

    con = qemu_console_lookup_by_index(dbus_display_console_get_index(console));
    assert(con);
    ddl->dcl.con = con;
    register_displaychangelistener(&ddl->dcl);

    return ddl;
}
