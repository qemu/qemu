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
#include "qapi/error.h"
#include "sysemu/sysemu.h"
#include "dbus.h"
#ifdef G_OS_UNIX
#include <gio/gunixfdlist.h>
#endif
#ifdef WIN32
#include <d3d11.h>
#include <dxgi1_2.h>
#endif

#ifdef CONFIG_OPENGL
#include "ui/shader.h"
#include "ui/egl-helpers.h"
#include "ui/egl-context.h"
#endif
#include "trace.h"

static void dbus_gfx_switch(DisplayChangeListener *dcl,
                            struct DisplaySurface *new_surface);

enum share_kind {
    SHARE_KIND_NONE,
    SHARE_KIND_MAPPED,
    SHARE_KIND_D3DTEX,
};

struct _DBusDisplayListener {
    GObject parent;

    char *bus_name;
    DBusDisplayConsole *console;
    GDBusConnection *conn;

    QemuDBusDisplay1Listener *proxy;

    DisplayChangeListener dcl;
    DisplaySurface *ds;
    enum share_kind ds_share;

    int gl_updates;

    bool ds_mapped;
    bool can_share_map;

#ifdef WIN32
    QemuDBusDisplay1ListenerWin32Map *map_proxy;
    QemuDBusDisplay1ListenerWin32D3d11 *d3d11_proxy;
    HANDLE peer_process;
    ID3D11Texture2D *d3d_texture;
#ifdef CONFIG_OPENGL
    egl_fb fb;
#endif
#endif
};

G_DEFINE_TYPE(DBusDisplayListener, dbus_display_listener, G_TYPE_OBJECT)

static void dbus_gfx_update(DisplayChangeListener *dcl,
                            int x, int y, int w, int h);

#ifdef CONFIG_OPENGL
static void dbus_scanout_disable(DisplayChangeListener *dcl)
{
    DBusDisplayListener *ddl = container_of(dcl, DBusDisplayListener, dcl);

    qemu_dbus_display1_listener_call_disable(
        ddl->proxy, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}

#ifdef WIN32
static bool d3d_texture2d_share(ID3D11Texture2D *d3d_texture,
                                HANDLE *handle, Error **errp)
{
    IDXGIResource1 *dxgiResource = NULL;
    HRESULT hr;

    hr = d3d_texture->lpVtbl->QueryInterface(d3d_texture,
                                             &IID_IDXGIResource1,
                                             (void **)&dxgiResource);
    if (FAILED(hr)) {
        goto fail;
    }

    hr = dxgiResource->lpVtbl->CreateSharedHandle(
        dxgiResource,
        NULL,
        DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
        NULL,
        handle
        );

    dxgiResource->lpVtbl->Release(dxgiResource);

    if (SUCCEEDED(hr)) {
        return true;
    }

fail:
    error_setg_win32(errp, GetLastError(), "failed to create shared handle");
    return false;
}

static bool d3d_texture2d_acquire0(ID3D11Texture2D *d3d_texture, Error **errp)
{
    IDXGIKeyedMutex *dxgiMutex = NULL;
    HRESULT hr;

    hr = d3d_texture->lpVtbl->QueryInterface(d3d_texture,
                                             &IID_IDXGIKeyedMutex,
                                             (void **)&dxgiMutex);
    if (FAILED(hr)) {
        goto fail;
    }

    hr = dxgiMutex->lpVtbl->AcquireSync(dxgiMutex, 0, INFINITE);

    dxgiMutex->lpVtbl->Release(dxgiMutex);

    if (SUCCEEDED(hr)) {
        return true;
    }

fail:
    error_setg_win32(errp, GetLastError(), "failed to acquire texture mutex");
    return false;
}

static bool d3d_texture2d_release0(ID3D11Texture2D *d3d_texture, Error **errp)
{
    IDXGIKeyedMutex *dxgiMutex = NULL;
    HRESULT hr;

    hr = d3d_texture->lpVtbl->QueryInterface(d3d_texture,
                                             &IID_IDXGIKeyedMutex,
                                             (void **)&dxgiMutex);
    if (FAILED(hr)) {
        goto fail;
    }

    hr = dxgiMutex->lpVtbl->ReleaseSync(dxgiMutex, 0);

    dxgiMutex->lpVtbl->Release(dxgiMutex);

    if (SUCCEEDED(hr)) {
        return true;
    }

fail:
    error_setg_win32(errp, GetLastError(), "failed to release texture mutex");
    return false;
}
#endif /* WIN32 */

#if defined(CONFIG_GBM) || defined(WIN32)
static void dbus_update_gl_cb(GObject *source_object,
                              GAsyncResult *res,
                              gpointer user_data)
{
    g_autoptr(GError) err = NULL;
    DBusDisplayListener *ddl = user_data;
    bool success;

#ifdef CONFIG_GBM
    success = qemu_dbus_display1_listener_call_update_dmabuf_finish(
        ddl->proxy, res, &err);
#endif

#ifdef WIN32
    success = qemu_dbus_display1_listener_win32_d3d11_call_update_texture2d_finish(
        ddl->d3d11_proxy, res, &err);
    d3d_texture2d_acquire0(ddl->d3d_texture, &error_warn);
#endif

    if (!success) {
        error_report("Failed to call update: %s", err->message);
    }

    graphic_hw_gl_block(ddl->dcl.con, false);
    g_object_unref(ddl);
}
#endif

static void dbus_call_update_gl(DisplayChangeListener *dcl,
                                int x, int y, int w, int h)
{
#if defined(CONFIG_GBM) || defined(WIN32)
    DBusDisplayListener *ddl = container_of(dcl, DBusDisplayListener, dcl);
#endif

    trace_dbus_update_gl(x, y, w, h);

    glFlush();
#ifdef CONFIG_GBM
    graphic_hw_gl_block(ddl->dcl.con, true);
    qemu_dbus_display1_listener_call_update_dmabuf(ddl->proxy,
        x, y, w, h,
        G_DBUS_CALL_FLAGS_NONE,
        DBUS_DEFAULT_TIMEOUT, NULL,
        dbus_update_gl_cb,
        g_object_ref(ddl));
#endif

#ifdef WIN32
    switch (ddl->ds_share) {
    case SHARE_KIND_MAPPED:
        egl_fb_read_rect(ddl->ds, &ddl->fb, x, y, w, h);
        dbus_gfx_update(dcl, x, y, w, h);
        break;
    case SHARE_KIND_D3DTEX: {
        Error *err = NULL;
        assert(ddl->d3d_texture);

        graphic_hw_gl_block(ddl->dcl.con, true);
        if (!d3d_texture2d_release0(ddl->d3d_texture, &err)) {
            error_report_err(err);
            return;
        }
        qemu_dbus_display1_listener_win32_d3d11_call_update_texture2d(
            ddl->d3d11_proxy,
            x, y, w, h,
            G_DBUS_CALL_FLAGS_NONE,
            DBUS_DEFAULT_TIMEOUT, NULL,
            dbus_update_gl_cb,
            g_object_ref(ddl));
        break;
    }
    default:
        g_warn_if_reached();
    }
#endif
}

#ifdef CONFIG_GBM
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
#endif /* GBM */
#endif /* OPENGL */

#ifdef WIN32
static bool dbus_scanout_map(DBusDisplayListener *ddl)
{
    g_autoptr(GError) err = NULL;
    BOOL success;
    HANDLE target_handle;

    if (ddl->ds_share == SHARE_KIND_MAPPED) {
        return true;
    }

    if (!ddl->can_share_map || !ddl->ds->handle) {
        return false;
    }

    success = DuplicateHandle(
        GetCurrentProcess(),
        ddl->ds->handle,
        ddl->peer_process,
        &target_handle,
        FILE_MAP_READ | SECTION_QUERY,
        FALSE, 0);
    if (!success) {
        g_autofree char *msg = g_win32_error_message(GetLastError());
        g_debug("Failed to DuplicateHandle: %s", msg);
        ddl->can_share_map = false;
        return false;
    }

    if (!qemu_dbus_display1_listener_win32_map_call_scanout_map_sync(
            ddl->map_proxy,
            GPOINTER_TO_UINT(target_handle),
            ddl->ds->handle_offset,
            surface_width(ddl->ds),
            surface_height(ddl->ds),
            surface_stride(ddl->ds),
            surface_format(ddl->ds),
            G_DBUS_CALL_FLAGS_NONE,
            DBUS_DEFAULT_TIMEOUT,
            NULL,
            &err)) {
        g_debug("Failed to call ScanoutMap: %s", err->message);
        ddl->can_share_map = false;
        return false;
    }

    ddl->ds_share = SHARE_KIND_MAPPED;

    return true;
}

#ifdef CONFIG_OPENGL
static bool
dbus_scanout_share_d3d_texture(
    DBusDisplayListener *ddl,
    ID3D11Texture2D *tex,
    bool backing_y_0_top,
    uint32_t backing_width,
    uint32_t backing_height,
    uint32_t x, uint32_t y,
    uint32_t w, uint32_t h)
{
    Error *err = NULL;
    BOOL success;
    HANDLE share_handle, target_handle;

    if (!d3d_texture2d_release0(tex, &err)) {
        error_report_err(err);
        return false;
    }

    if (!d3d_texture2d_share(tex, &share_handle, &err)) {
        error_report_err(err);
        return false;
    }

    success = DuplicateHandle(
        GetCurrentProcess(),
        share_handle,
        ddl->peer_process,
        &target_handle,
        0,
        FALSE, DUPLICATE_SAME_ACCESS);
    if (!success) {
        g_autofree char *msg = g_win32_error_message(GetLastError());
        g_debug("Failed to DuplicateHandle: %s", msg);
        CloseHandle(share_handle);
        return false;
    }

    qemu_dbus_display1_listener_win32_d3d11_call_scanout_texture2d(
        ddl->d3d11_proxy,
        GPOINTER_TO_INT(target_handle),
        backing_width,
        backing_height,
        backing_y_0_top,
        x, y, w, h,
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL, NULL, NULL);

    CloseHandle(share_handle);

    if (!d3d_texture2d_acquire0(tex, &err)) {
        error_report_err(err);
        return false;
    }

    ddl->d3d_texture = tex;
    ddl->ds_share = SHARE_KIND_D3DTEX;

    return true;
}
#endif /* CONFIG_OPENGL */
#endif /* WIN32 */

#ifdef CONFIG_OPENGL
static void dbus_scanout_texture(DisplayChangeListener *dcl,
                                 uint32_t tex_id,
                                 bool backing_y_0_top,
                                 uint32_t backing_width,
                                 uint32_t backing_height,
                                 uint32_t x, uint32_t y,
                                 uint32_t w, uint32_t h,
                                 void *d3d_tex2d)
{
    trace_dbus_scanout_texture(tex_id, backing_y_0_top,
                               backing_width, backing_height, x, y, w, h);
#ifdef CONFIG_GBM
    QemuDmaBuf dmabuf = {
        .width = w,
        .height = h,
        .y0_top = backing_y_0_top,
        .x = x,
        .y = y,
        .backing_width = backing_width,
        .backing_height = backing_height,
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
#endif

#ifdef WIN32
    DBusDisplayListener *ddl = container_of(dcl, DBusDisplayListener, dcl);

    /* there must be a matching gfx_switch before */
    assert(surface_width(ddl->ds) == w);
    assert(surface_height(ddl->ds) == h);

    if (d3d_tex2d) {
        dbus_scanout_share_d3d_texture(ddl, d3d_tex2d, backing_y_0_top,
                                       backing_width, backing_height, x, y, w, h);
    } else {
        dbus_scanout_map(ddl);
        egl_fb_setup_for_tex(&ddl->fb, backing_width, backing_height, tex_id, false);
    }
#endif
}

#ifdef CONFIG_GBM
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

static void dbus_release_dmabuf(DisplayChangeListener *dcl,
                                QemuDmaBuf *dmabuf)
{
    dbus_scanout_disable(dcl);
}
#endif /* GBM */

static void dbus_gl_cursor_position(DisplayChangeListener *dcl,
                                 uint32_t pos_x, uint32_t pos_y)
{
    DBusDisplayListener *ddl = container_of(dcl, DBusDisplayListener, dcl);

    qemu_dbus_display1_listener_call_mouse_set(
        ddl->proxy, pos_x, pos_y, true,
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}

static void dbus_scanout_update(DisplayChangeListener *dcl,
                                uint32_t x, uint32_t y,
                                uint32_t w, uint32_t h)
{
    dbus_call_update_gl(dcl, x, y, w, h);
}

static void dbus_gl_refresh(DisplayChangeListener *dcl)
{
    DBusDisplayListener *ddl = container_of(dcl, DBusDisplayListener, dcl);

    graphic_hw_update(dcl->con);

    if (!ddl->ds || qemu_console_is_gl_blocked(ddl->dcl.con)) {
        return;
    }

    if (ddl->gl_updates) {
        dbus_call_update_gl(dcl, 0, 0,
                            surface_width(ddl->ds), surface_height(ddl->ds));
        ddl->gl_updates = 0;
    }
}
#endif /* OPENGL */

static void dbus_refresh(DisplayChangeListener *dcl)
{
    graphic_hw_update(dcl->con);
}

#ifdef CONFIG_OPENGL
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

    trace_dbus_update(x, y, w, h);

#ifdef WIN32
    if (dbus_scanout_map(ddl)) {
        qemu_dbus_display1_listener_win32_map_call_update_map(
            ddl->map_proxy,
            x, y, w, h,
            G_DBUS_CALL_FLAGS_NONE,
            DBUS_DEFAULT_TIMEOUT, NULL, NULL, NULL);
        return;
    }
#endif

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
    stride = w * DIV_ROUND_UP(PIXMAN_FORMAT_BPP(surface_format(ddl->ds)), 8);
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

#ifdef CONFIG_OPENGL
static void dbus_gl_gfx_switch(DisplayChangeListener *dcl,
                               struct DisplaySurface *new_surface)
{
    DBusDisplayListener *ddl = container_of(dcl, DBusDisplayListener, dcl);

    trace_dbus_gl_gfx_switch(new_surface);

    ddl->ds = new_surface;
    ddl->ds_share = SHARE_KIND_NONE;
    if (ddl->ds) {
        int width = surface_width(ddl->ds);
        int height = surface_height(ddl->ds);

        /* TODO: lazy send dmabuf (there are unnecessary sent otherwise) */
        dbus_scanout_texture(&ddl->dcl, ddl->ds->texture, false,
                             width, height, 0, 0, width, height, NULL);
    }
}
#endif

static void dbus_gfx_switch(DisplayChangeListener *dcl,
                            struct DisplaySurface *new_surface)
{
    DBusDisplayListener *ddl = container_of(dcl, DBusDisplayListener, dcl);

    ddl->ds = new_surface;
    ddl->ds_share = SHARE_KIND_NONE;
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

#ifdef CONFIG_OPENGL
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
#ifdef CONFIG_GBM
    .dpy_gl_scanout_dmabuf   = dbus_scanout_dmabuf,
    .dpy_gl_cursor_dmabuf    = dbus_cursor_dmabuf,
    .dpy_gl_release_dmabuf   = dbus_release_dmabuf,
#endif
    .dpy_gl_cursor_position  = dbus_gl_cursor_position,
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
#ifdef WIN32
    g_clear_object(&ddl->map_proxy);
    g_clear_object(&ddl->d3d11_proxy);
    g_clear_pointer(&ddl->peer_process, CloseHandle);
#ifdef CONFIG_OPENGL
    egl_fb_destroy(&ddl->fb);
#endif
#endif

    G_OBJECT_CLASS(dbus_display_listener_parent_class)->dispose(object);
}

static void
dbus_display_listener_constructed(GObject *object)
{
    DBusDisplayListener *ddl = DBUS_DISPLAY_LISTENER(object);

    ddl->dcl.ops = &dbus_dcl_ops;
#ifdef CONFIG_OPENGL
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

#ifdef WIN32
static bool
dbus_display_listener_implements(DBusDisplayListener *ddl, const char *iface)
{
    QemuDBusDisplay1Listener *l = QEMU_DBUS_DISPLAY1_LISTENER(ddl->proxy);
    bool implements;

    implements = g_strv_contains(qemu_dbus_display1_listener_get_interfaces(l), iface);
    if (!implements) {
        g_debug("Display listener does not implement: `%s`", iface);
    }

    return implements;
}

static bool
dbus_display_listener_setup_peer_process(DBusDisplayListener *ddl)
{
    g_autoptr(GError) err = NULL;
    GDBusConnection *conn;
    GIOStream *stream;
    GSocket *sock;
    g_autoptr(GCredentials) creds = NULL;
    DWORD *pid;

    if (ddl->peer_process) {
        return true;
    }

    conn = g_dbus_proxy_get_connection(G_DBUS_PROXY(ddl->proxy));
    stream = g_dbus_connection_get_stream(conn);

    if (!G_IS_UNIX_CONNECTION(stream)) {
        return false;
    }

    sock = g_socket_connection_get_socket(G_SOCKET_CONNECTION(stream));
    creds = g_socket_get_credentials(sock, &err);

    if (!creds) {
        g_debug("Failed to get peer credentials: %s", err->message);
        return false;
    }

    pid = g_credentials_get_native(creds, G_CREDENTIALS_TYPE_WIN32_PID);

    if (pid == NULL) {
        g_debug("Failed to get peer PID");
        return false;
    }

    ddl->peer_process = OpenProcess(
        PROCESS_DUP_HANDLE | PROCESS_QUERY_INFORMATION,
        false, *pid);

    if (!ddl->peer_process) {
        g_autofree char *msg = g_win32_error_message(GetLastError());
        g_debug("Failed to OpenProcess: %s", msg);
        return false;
    }

    return true;
}
#endif

static void
dbus_display_listener_setup_d3d11(DBusDisplayListener *ddl)
{
#ifdef WIN32
    g_autoptr(GError) err = NULL;

    if (!dbus_display_listener_implements(ddl,
            "org.qemu.Display1.Listener.Win32.D3d11")) {
        return;
    }

    if (!dbus_display_listener_setup_peer_process(ddl)) {
        return;
    }

    ddl->d3d11_proxy =
        qemu_dbus_display1_listener_win32_d3d11_proxy_new_sync(ddl->conn,
            G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
            NULL,
            "/org/qemu/Display1/Listener",
            NULL,
            &err);
    if (!ddl->d3d11_proxy) {
        g_debug("Failed to setup win32 d3d11 proxy: %s", err->message);
        return;
    }
#endif
}

static void
dbus_display_listener_setup_shared_map(DBusDisplayListener *ddl)
{
#ifdef WIN32
    g_autoptr(GError) err = NULL;

    if (!dbus_display_listener_implements(ddl, "org.qemu.Display1.Listener.Win32.Map")) {
        return;
    }

    if (!dbus_display_listener_setup_peer_process(ddl)) {
        return;
    }

    ddl->map_proxy =
        qemu_dbus_display1_listener_win32_map_proxy_new_sync(ddl->conn,
            G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
            NULL,
            "/org/qemu/Display1/Listener",
            NULL,
            &err);
    if (!ddl->map_proxy) {
        g_debug("Failed to setup win32 map proxy: %s", err->message);
        return;
    }

    ddl->can_share_map = true;
#endif
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

    dbus_display_listener_setup_shared_map(ddl);
    dbus_display_listener_setup_d3d11(ddl);

    con = qemu_console_lookup_by_index(dbus_display_console_get_index(console));
    assert(con);
    ddl->dcl.con = con;
    register_displaychangelistener(&ddl->dcl);

    return ddl;
}
