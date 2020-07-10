/*
 * QEMU VNC display driver
 *
 * Copyright (C) 2006 Anthony Liguori <anthony@codemonkey.ws>
 * Copyright (C) 2006 Fabrice Bellard
 * Copyright (C) 2009 Red Hat, Inc
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
#include "vnc.h"
#include "vnc-jobs.h"
#include "trace.h"
#include "hw/qdev-core.h"
#include "sysemu/sysemu.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/sockets.h"
#include "qemu/timer.h"
#include "authz/list.h"
#include "qemu/config-file.h"
#include "qapi/qapi-emit-events.h"
#include "qapi/qapi-events-ui.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-ui.h"
#include "ui/input.h"
#include "crypto/hash.h"
#include "crypto/tlscredsanon.h"
#include "crypto/tlscredsx509.h"
#include "crypto/random.h"
#include "qom/object_interfaces.h"
#include "qemu/cutils.h"
#include "io/dns-resolver.h"

#define VNC_REFRESH_INTERVAL_BASE GUI_REFRESH_INTERVAL_DEFAULT
#define VNC_REFRESH_INTERVAL_INC  50
#define VNC_REFRESH_INTERVAL_MAX  GUI_REFRESH_INTERVAL_IDLE
static const struct timeval VNC_REFRESH_STATS = { 0, 500000 };
static const struct timeval VNC_REFRESH_LOSSY = { 2, 0 };

#include "vnc_keysym.h"
#include "crypto/cipher.h"

static QTAILQ_HEAD(, VncDisplay) vnc_displays =
    QTAILQ_HEAD_INITIALIZER(vnc_displays);

static int vnc_cursor_define(VncState *vs);
static void vnc_update_throttle_offset(VncState *vs);

static void vnc_set_share_mode(VncState *vs, VncShareMode mode)
{
#ifdef _VNC_DEBUG
    static const char *mn[] = {
        [0]                           = "undefined",
        [VNC_SHARE_MODE_CONNECTING]   = "connecting",
        [VNC_SHARE_MODE_SHARED]       = "shared",
        [VNC_SHARE_MODE_EXCLUSIVE]    = "exclusive",
        [VNC_SHARE_MODE_DISCONNECTED] = "disconnected",
    };
    fprintf(stderr, "%s/%p: %s -> %s\n", __func__,
            vs->ioc, mn[vs->share_mode], mn[mode]);
#endif

    switch (vs->share_mode) {
    case VNC_SHARE_MODE_CONNECTING:
        vs->vd->num_connecting--;
        break;
    case VNC_SHARE_MODE_SHARED:
        vs->vd->num_shared--;
        break;
    case VNC_SHARE_MODE_EXCLUSIVE:
        vs->vd->num_exclusive--;
        break;
    default:
        break;
    }

    vs->share_mode = mode;

    switch (vs->share_mode) {
    case VNC_SHARE_MODE_CONNECTING:
        vs->vd->num_connecting++;
        break;
    case VNC_SHARE_MODE_SHARED:
        vs->vd->num_shared++;
        break;
    case VNC_SHARE_MODE_EXCLUSIVE:
        vs->vd->num_exclusive++;
        break;
    default:
        break;
    }
}


static void vnc_init_basic_info(SocketAddress *addr,
                                VncBasicInfo *info,
                                Error **errp)
{
    switch (addr->type) {
    case SOCKET_ADDRESS_TYPE_INET:
        info->host = g_strdup(addr->u.inet.host);
        info->service = g_strdup(addr->u.inet.port);
        if (addr->u.inet.ipv6) {
            info->family = NETWORK_ADDRESS_FAMILY_IPV6;
        } else {
            info->family = NETWORK_ADDRESS_FAMILY_IPV4;
        }
        break;

    case SOCKET_ADDRESS_TYPE_UNIX:
        info->host = g_strdup("");
        info->service = g_strdup(addr->u.q_unix.path);
        info->family = NETWORK_ADDRESS_FAMILY_UNIX;
        break;

    case SOCKET_ADDRESS_TYPE_VSOCK:
    case SOCKET_ADDRESS_TYPE_FD:
        error_setg(errp, "Unsupported socket address type %s",
                   SocketAddressType_str(addr->type));
        break;
    default:
        abort();
    }

    return;
}

static void vnc_init_basic_info_from_server_addr(QIOChannelSocket *ioc,
                                                 VncBasicInfo *info,
                                                 Error **errp)
{
    SocketAddress *addr = NULL;

    if (!ioc) {
        error_setg(errp, "No listener socket available");
        return;
    }

    addr = qio_channel_socket_get_local_address(ioc, errp);
    if (!addr) {
        return;
    }

    vnc_init_basic_info(addr, info, errp);
    qapi_free_SocketAddress(addr);
}

static void vnc_init_basic_info_from_remote_addr(QIOChannelSocket *ioc,
                                                 VncBasicInfo *info,
                                                 Error **errp)
{
    SocketAddress *addr = NULL;

    addr = qio_channel_socket_get_remote_address(ioc, errp);
    if (!addr) {
        return;
    }

    vnc_init_basic_info(addr, info, errp);
    qapi_free_SocketAddress(addr);
}

static const char *vnc_auth_name(VncDisplay *vd) {
    switch (vd->auth) {
    case VNC_AUTH_INVALID:
        return "invalid";
    case VNC_AUTH_NONE:
        return "none";
    case VNC_AUTH_VNC:
        return "vnc";
    case VNC_AUTH_RA2:
        return "ra2";
    case VNC_AUTH_RA2NE:
        return "ra2ne";
    case VNC_AUTH_TIGHT:
        return "tight";
    case VNC_AUTH_ULTRA:
        return "ultra";
    case VNC_AUTH_TLS:
        return "tls";
    case VNC_AUTH_VENCRYPT:
        switch (vd->subauth) {
        case VNC_AUTH_VENCRYPT_PLAIN:
            return "vencrypt+plain";
        case VNC_AUTH_VENCRYPT_TLSNONE:
            return "vencrypt+tls+none";
        case VNC_AUTH_VENCRYPT_TLSVNC:
            return "vencrypt+tls+vnc";
        case VNC_AUTH_VENCRYPT_TLSPLAIN:
            return "vencrypt+tls+plain";
        case VNC_AUTH_VENCRYPT_X509NONE:
            return "vencrypt+x509+none";
        case VNC_AUTH_VENCRYPT_X509VNC:
            return "vencrypt+x509+vnc";
        case VNC_AUTH_VENCRYPT_X509PLAIN:
            return "vencrypt+x509+plain";
        case VNC_AUTH_VENCRYPT_TLSSASL:
            return "vencrypt+tls+sasl";
        case VNC_AUTH_VENCRYPT_X509SASL:
            return "vencrypt+x509+sasl";
        default:
            return "vencrypt";
        }
    case VNC_AUTH_SASL:
        return "sasl";
    }
    return "unknown";
}

static VncServerInfo *vnc_server_info_get(VncDisplay *vd)
{
    VncServerInfo *info;
    Error *err = NULL;

    if (!vd->listener || !vd->listener->nsioc) {
        return NULL;
    }

    info = g_malloc0(sizeof(*info));
    vnc_init_basic_info_from_server_addr(vd->listener->sioc[0],
                                         qapi_VncServerInfo_base(info), &err);
    info->has_auth = true;
    info->auth = g_strdup(vnc_auth_name(vd));
    if (err) {
        qapi_free_VncServerInfo(info);
        info = NULL;
        error_free(err);
    }
    return info;
}

static void vnc_client_cache_auth(VncState *client)
{
    if (!client->info) {
        return;
    }

    if (client->tls) {
        client->info->x509_dname =
            qcrypto_tls_session_get_peer_name(client->tls);
        client->info->has_x509_dname =
            client->info->x509_dname != NULL;
    }
#ifdef CONFIG_VNC_SASL
    if (client->sasl.conn &&
        client->sasl.username) {
        client->info->has_sasl_username = true;
        client->info->sasl_username = g_strdup(client->sasl.username);
    }
#endif
}

static void vnc_client_cache_addr(VncState *client)
{
    Error *err = NULL;

    client->info = g_malloc0(sizeof(*client->info));
    vnc_init_basic_info_from_remote_addr(client->sioc,
                                         qapi_VncClientInfo_base(client->info),
                                         &err);
    client->info->websocket = client->websocket;
    if (err) {
        qapi_free_VncClientInfo(client->info);
        client->info = NULL;
        error_free(err);
    }
}

static void vnc_qmp_event(VncState *vs, QAPIEvent event)
{
    VncServerInfo *si;

    if (!vs->info) {
        return;
    }

    si = vnc_server_info_get(vs->vd);
    if (!si) {
        return;
    }

    switch (event) {
    case QAPI_EVENT_VNC_CONNECTED:
        qapi_event_send_vnc_connected(si, qapi_VncClientInfo_base(vs->info));
        break;
    case QAPI_EVENT_VNC_INITIALIZED:
        qapi_event_send_vnc_initialized(si, vs->info);
        break;
    case QAPI_EVENT_VNC_DISCONNECTED:
        qapi_event_send_vnc_disconnected(si, vs->info);
        break;
    default:
        break;
    }

    qapi_free_VncServerInfo(si);
}

static VncClientInfo *qmp_query_vnc_client(const VncState *client)
{
    VncClientInfo *info;
    Error *err = NULL;

    info = g_malloc0(sizeof(*info));

    vnc_init_basic_info_from_remote_addr(client->sioc,
                                         qapi_VncClientInfo_base(info),
                                         &err);
    if (err) {
        error_free(err);
        qapi_free_VncClientInfo(info);
        return NULL;
    }

    info->websocket = client->websocket;

    if (client->tls) {
        info->x509_dname = qcrypto_tls_session_get_peer_name(client->tls);
        info->has_x509_dname = info->x509_dname != NULL;
    }
#ifdef CONFIG_VNC_SASL
    if (client->sasl.conn && client->sasl.username) {
        info->has_sasl_username = true;
        info->sasl_username = g_strdup(client->sasl.username);
    }
#endif

    return info;
}

static VncDisplay *vnc_display_find(const char *id)
{
    VncDisplay *vd;

    if (id == NULL) {
        return QTAILQ_FIRST(&vnc_displays);
    }
    QTAILQ_FOREACH(vd, &vnc_displays, next) {
        if (strcmp(id, vd->id) == 0) {
            return vd;
        }
    }
    return NULL;
}

static VncClientInfoList *qmp_query_client_list(VncDisplay *vd)
{
    VncClientInfoList *cinfo, *prev = NULL;
    VncState *client;

    QTAILQ_FOREACH(client, &vd->clients, next) {
        cinfo = g_new0(VncClientInfoList, 1);
        cinfo->value = qmp_query_vnc_client(client);
        cinfo->next = prev;
        prev = cinfo;
    }
    return prev;
}

VncInfo *qmp_query_vnc(Error **errp)
{
    VncInfo *info = g_malloc0(sizeof(*info));
    VncDisplay *vd = vnc_display_find(NULL);
    SocketAddress *addr = NULL;

    if (vd == NULL || !vd->listener || !vd->listener->nsioc) {
        info->enabled = false;
    } else {
        info->enabled = true;

        /* for compatibility with the original command */
        info->has_clients = true;
        info->clients = qmp_query_client_list(vd);

        addr = qio_channel_socket_get_local_address(vd->listener->sioc[0],
                                                    errp);
        if (!addr) {
            goto out_error;
        }

        switch (addr->type) {
        case SOCKET_ADDRESS_TYPE_INET:
            info->host = g_strdup(addr->u.inet.host);
            info->service = g_strdup(addr->u.inet.port);
            if (addr->u.inet.ipv6) {
                info->family = NETWORK_ADDRESS_FAMILY_IPV6;
            } else {
                info->family = NETWORK_ADDRESS_FAMILY_IPV4;
            }
            break;

        case SOCKET_ADDRESS_TYPE_UNIX:
            info->host = g_strdup("");
            info->service = g_strdup(addr->u.q_unix.path);
            info->family = NETWORK_ADDRESS_FAMILY_UNIX;
            break;

        case SOCKET_ADDRESS_TYPE_VSOCK:
        case SOCKET_ADDRESS_TYPE_FD:
            error_setg(errp, "Unsupported socket address type %s",
                       SocketAddressType_str(addr->type));
            goto out_error;
        default:
            abort();
        }

        info->has_host = true;
        info->has_service = true;
        info->has_family = true;

        info->has_auth = true;
        info->auth = g_strdup(vnc_auth_name(vd));
    }

    qapi_free_SocketAddress(addr);
    return info;

out_error:
    qapi_free_SocketAddress(addr);
    qapi_free_VncInfo(info);
    return NULL;
}


static void qmp_query_auth(int auth, int subauth,
                           VncPrimaryAuth *qmp_auth,
                           VncVencryptSubAuth *qmp_vencrypt,
                           bool *qmp_has_vencrypt);

static VncServerInfo2List *qmp_query_server_entry(QIOChannelSocket *ioc,
                                                  bool websocket,
                                                  int auth,
                                                  int subauth,
                                                  VncServerInfo2List *prev)
{
    VncServerInfo2List *list;
    VncServerInfo2 *info;
    Error *err = NULL;
    SocketAddress *addr;

    addr = qio_channel_socket_get_local_address(ioc, NULL);
    if (!addr) {
        return prev;
    }

    info = g_new0(VncServerInfo2, 1);
    vnc_init_basic_info(addr, qapi_VncServerInfo2_base(info), &err);
    qapi_free_SocketAddress(addr);
    if (err) {
        qapi_free_VncServerInfo2(info);
        error_free(err);
        return prev;
    }
    info->websocket = websocket;

    qmp_query_auth(auth, subauth, &info->auth,
                   &info->vencrypt, &info->has_vencrypt);

    list = g_new0(VncServerInfo2List, 1);
    list->value = info;
    list->next = prev;
    return list;
}

static void qmp_query_auth(int auth, int subauth,
                           VncPrimaryAuth *qmp_auth,
                           VncVencryptSubAuth *qmp_vencrypt,
                           bool *qmp_has_vencrypt)
{
    switch (auth) {
    case VNC_AUTH_VNC:
        *qmp_auth = VNC_PRIMARY_AUTH_VNC;
        break;
    case VNC_AUTH_RA2:
        *qmp_auth = VNC_PRIMARY_AUTH_RA2;
        break;
    case VNC_AUTH_RA2NE:
        *qmp_auth = VNC_PRIMARY_AUTH_RA2NE;
        break;
    case VNC_AUTH_TIGHT:
        *qmp_auth = VNC_PRIMARY_AUTH_TIGHT;
        break;
    case VNC_AUTH_ULTRA:
        *qmp_auth = VNC_PRIMARY_AUTH_ULTRA;
        break;
    case VNC_AUTH_TLS:
        *qmp_auth = VNC_PRIMARY_AUTH_TLS;
        break;
    case VNC_AUTH_VENCRYPT:
        *qmp_auth = VNC_PRIMARY_AUTH_VENCRYPT;
        *qmp_has_vencrypt = true;
        switch (subauth) {
        case VNC_AUTH_VENCRYPT_PLAIN:
            *qmp_vencrypt = VNC_VENCRYPT_SUB_AUTH_PLAIN;
            break;
        case VNC_AUTH_VENCRYPT_TLSNONE:
            *qmp_vencrypt = VNC_VENCRYPT_SUB_AUTH_TLS_NONE;
            break;
        case VNC_AUTH_VENCRYPT_TLSVNC:
            *qmp_vencrypt = VNC_VENCRYPT_SUB_AUTH_TLS_VNC;
            break;
        case VNC_AUTH_VENCRYPT_TLSPLAIN:
            *qmp_vencrypt = VNC_VENCRYPT_SUB_AUTH_TLS_PLAIN;
            break;
        case VNC_AUTH_VENCRYPT_X509NONE:
            *qmp_vencrypt = VNC_VENCRYPT_SUB_AUTH_X509_NONE;
            break;
        case VNC_AUTH_VENCRYPT_X509VNC:
            *qmp_vencrypt = VNC_VENCRYPT_SUB_AUTH_X509_VNC;
            break;
        case VNC_AUTH_VENCRYPT_X509PLAIN:
            *qmp_vencrypt = VNC_VENCRYPT_SUB_AUTH_X509_PLAIN;
            break;
        case VNC_AUTH_VENCRYPT_TLSSASL:
            *qmp_vencrypt = VNC_VENCRYPT_SUB_AUTH_TLS_SASL;
            break;
        case VNC_AUTH_VENCRYPT_X509SASL:
            *qmp_vencrypt = VNC_VENCRYPT_SUB_AUTH_X509_SASL;
            break;
        default:
            *qmp_has_vencrypt = false;
            break;
        }
        break;
    case VNC_AUTH_SASL:
        *qmp_auth = VNC_PRIMARY_AUTH_SASL;
        break;
    case VNC_AUTH_NONE:
    default:
        *qmp_auth = VNC_PRIMARY_AUTH_NONE;
        break;
    }
}

VncInfo2List *qmp_query_vnc_servers(Error **errp)
{
    VncInfo2List *item, *prev = NULL;
    VncInfo2 *info;
    VncDisplay *vd;
    DeviceState *dev;
    size_t i;

    QTAILQ_FOREACH(vd, &vnc_displays, next) {
        info = g_new0(VncInfo2, 1);
        info->id = g_strdup(vd->id);
        info->clients = qmp_query_client_list(vd);
        qmp_query_auth(vd->auth, vd->subauth, &info->auth,
                       &info->vencrypt, &info->has_vencrypt);
        if (vd->dcl.con) {
            dev = DEVICE(object_property_get_link(OBJECT(vd->dcl.con),
                                                  "device", &error_abort));
            info->has_display = true;
            info->display = g_strdup(dev->id);
        }
        for (i = 0; vd->listener != NULL && i < vd->listener->nsioc; i++) {
            info->server = qmp_query_server_entry(
                vd->listener->sioc[i], false, vd->auth, vd->subauth,
                info->server);
        }
        for (i = 0; vd->wslistener != NULL && i < vd->wslistener->nsioc; i++) {
            info->server = qmp_query_server_entry(
                vd->wslistener->sioc[i], true, vd->ws_auth,
                vd->ws_subauth, info->server);
        }

        item = g_new0(VncInfo2List, 1);
        item->value = info;
        item->next = prev;
        prev = item;
    }
    return prev;
}

/* TODO
   1) Get the queue working for IO.
   2) there is some weirdness when using the -S option (the screen is grey
      and not totally invalidated
   3) resolutions > 1024
*/

static int vnc_update_client(VncState *vs, int has_dirty);
static void vnc_disconnect_start(VncState *vs);

static void vnc_colordepth(VncState *vs);
static void framebuffer_update_request(VncState *vs, int incremental,
                                       int x_position, int y_position,
                                       int w, int h);
static void vnc_refresh(DisplayChangeListener *dcl);
static int vnc_refresh_server_surface(VncDisplay *vd);

static int vnc_width(VncDisplay *vd)
{
    return MIN(VNC_MAX_WIDTH, ROUND_UP(surface_width(vd->ds),
                                       VNC_DIRTY_PIXELS_PER_BIT));
}

static int vnc_height(VncDisplay *vd)
{
    return MIN(VNC_MAX_HEIGHT, surface_height(vd->ds));
}

static void vnc_set_area_dirty(DECLARE_BITMAP(dirty[VNC_MAX_HEIGHT],
                               VNC_MAX_WIDTH / VNC_DIRTY_PIXELS_PER_BIT),
                               VncDisplay *vd,
                               int x, int y, int w, int h)
{
    int width = vnc_width(vd);
    int height = vnc_height(vd);

    /* this is needed this to ensure we updated all affected
     * blocks if x % VNC_DIRTY_PIXELS_PER_BIT != 0 */
    w += (x % VNC_DIRTY_PIXELS_PER_BIT);
    x -= (x % VNC_DIRTY_PIXELS_PER_BIT);

    x = MIN(x, width);
    y = MIN(y, height);
    w = MIN(x + w, width) - x;
    h = MIN(y + h, height);

    for (; y < h; y++) {
        bitmap_set(dirty[y], x / VNC_DIRTY_PIXELS_PER_BIT,
                   DIV_ROUND_UP(w, VNC_DIRTY_PIXELS_PER_BIT));
    }
}

static void vnc_dpy_update(DisplayChangeListener *dcl,
                           int x, int y, int w, int h)
{
    VncDisplay *vd = container_of(dcl, VncDisplay, dcl);
    struct VncSurface *s = &vd->guest;

    vnc_set_area_dirty(s->dirty, vd, x, y, w, h);
}

void vnc_framebuffer_update(VncState *vs, int x, int y, int w, int h,
                            int32_t encoding)
{
    vnc_write_u16(vs, x);
    vnc_write_u16(vs, y);
    vnc_write_u16(vs, w);
    vnc_write_u16(vs, h);

    vnc_write_s32(vs, encoding);
}


static void vnc_desktop_resize(VncState *vs)
{
    if (vs->ioc == NULL || !vnc_has_feature(vs, VNC_FEATURE_RESIZE)) {
        return;
    }
    if (vs->client_width == pixman_image_get_width(vs->vd->server) &&
        vs->client_height == pixman_image_get_height(vs->vd->server)) {
        return;
    }

    assert(pixman_image_get_width(vs->vd->server) < 65536 &&
           pixman_image_get_width(vs->vd->server) >= 0);
    assert(pixman_image_get_height(vs->vd->server) < 65536 &&
           pixman_image_get_height(vs->vd->server) >= 0);
    vs->client_width = pixman_image_get_width(vs->vd->server);
    vs->client_height = pixman_image_get_height(vs->vd->server);
    vnc_lock_output(vs);
    vnc_write_u8(vs, VNC_MSG_SERVER_FRAMEBUFFER_UPDATE);
    vnc_write_u8(vs, 0);
    vnc_write_u16(vs, 1); /* number of rects */
    vnc_framebuffer_update(vs, 0, 0, vs->client_width, vs->client_height,
                           VNC_ENCODING_DESKTOPRESIZE);
    vnc_unlock_output(vs);
    vnc_flush(vs);
}

static void vnc_abort_display_jobs(VncDisplay *vd)
{
    VncState *vs;

    QTAILQ_FOREACH(vs, &vd->clients, next) {
        vnc_lock_output(vs);
        vs->abort = true;
        vnc_unlock_output(vs);
    }
    QTAILQ_FOREACH(vs, &vd->clients, next) {
        vnc_jobs_join(vs);
    }
    QTAILQ_FOREACH(vs, &vd->clients, next) {
        vnc_lock_output(vs);
        if (vs->update == VNC_STATE_UPDATE_NONE &&
            vs->job_update != VNC_STATE_UPDATE_NONE) {
            /* job aborted before completion */
            vs->update = vs->job_update;
            vs->job_update = VNC_STATE_UPDATE_NONE;
        }
        vs->abort = false;
        vnc_unlock_output(vs);
    }
}

int vnc_server_fb_stride(VncDisplay *vd)
{
    return pixman_image_get_stride(vd->server);
}

void *vnc_server_fb_ptr(VncDisplay *vd, int x, int y)
{
    uint8_t *ptr;

    ptr  = (uint8_t *)pixman_image_get_data(vd->server);
    ptr += y * vnc_server_fb_stride(vd);
    ptr += x * VNC_SERVER_FB_BYTES;
    return ptr;
}

static void vnc_update_server_surface(VncDisplay *vd)
{
    int width, height;

    qemu_pixman_image_unref(vd->server);
    vd->server = NULL;

    if (QTAILQ_EMPTY(&vd->clients)) {
        return;
    }

    width = vnc_width(vd);
    height = vnc_height(vd);
    vd->server = pixman_image_create_bits(VNC_SERVER_FB_FORMAT,
                                          width, height,
                                          NULL, 0);

    memset(vd->guest.dirty, 0x00, sizeof(vd->guest.dirty));
    vnc_set_area_dirty(vd->guest.dirty, vd, 0, 0,
                       width, height);
}

static bool vnc_check_pageflip(DisplaySurface *s1,
                               DisplaySurface *s2)
{
    return (s1 != NULL &&
            s2 != NULL &&
            surface_width(s1) == surface_width(s2) &&
            surface_height(s1) == surface_height(s2) &&
            surface_format(s1) == surface_format(s2));

}

static void vnc_dpy_switch(DisplayChangeListener *dcl,
                           DisplaySurface *surface)
{
    static const char placeholder_msg[] =
        "Display output is not active.";
    static DisplaySurface *placeholder;
    VncDisplay *vd = container_of(dcl, VncDisplay, dcl);
    bool pageflip = vnc_check_pageflip(vd->ds, surface);
    VncState *vs;

    if (surface == NULL) {
        if (placeholder == NULL) {
            placeholder = qemu_create_message_surface(640, 480, placeholder_msg);
        }
        surface = placeholder;
    }

    vnc_abort_display_jobs(vd);
    vd->ds = surface;

    /* guest surface */
    qemu_pixman_image_unref(vd->guest.fb);
    vd->guest.fb = pixman_image_ref(surface->image);
    vd->guest.format = surface->format;

    if (pageflip) {
        vnc_set_area_dirty(vd->guest.dirty, vd, 0, 0,
                           surface_width(surface),
                           surface_height(surface));
        return;
    }

    /* server surface */
    vnc_update_server_surface(vd);

    QTAILQ_FOREACH(vs, &vd->clients, next) {
        vnc_colordepth(vs);
        vnc_desktop_resize(vs);
        if (vs->vd->cursor) {
            vnc_cursor_define(vs);
        }
        memset(vs->dirty, 0x00, sizeof(vs->dirty));
        vnc_set_area_dirty(vs->dirty, vd, 0, 0,
                           vnc_width(vd),
                           vnc_height(vd));
        vnc_update_throttle_offset(vs);
    }
}

/* fastest code */
static void vnc_write_pixels_copy(VncState *vs,
                                  void *pixels, int size)
{
    vnc_write(vs, pixels, size);
}

/* slowest but generic code. */
void vnc_convert_pixel(VncState *vs, uint8_t *buf, uint32_t v)
{
    uint8_t r, g, b;

#if VNC_SERVER_FB_FORMAT == PIXMAN_FORMAT(32, PIXMAN_TYPE_ARGB, 0, 8, 8, 8)
    r = (((v & 0x00ff0000) >> 16) << vs->client_pf.rbits) >> 8;
    g = (((v & 0x0000ff00) >>  8) << vs->client_pf.gbits) >> 8;
    b = (((v & 0x000000ff) >>  0) << vs->client_pf.bbits) >> 8;
#else
# error need some bits here if you change VNC_SERVER_FB_FORMAT
#endif
    v = (r << vs->client_pf.rshift) |
        (g << vs->client_pf.gshift) |
        (b << vs->client_pf.bshift);
    switch (vs->client_pf.bytes_per_pixel) {
    case 1:
        buf[0] = v;
        break;
    case 2:
        if (vs->client_be) {
            buf[0] = v >> 8;
            buf[1] = v;
        } else {
            buf[1] = v >> 8;
            buf[0] = v;
        }
        break;
    default:
    case 4:
        if (vs->client_be) {
            buf[0] = v >> 24;
            buf[1] = v >> 16;
            buf[2] = v >> 8;
            buf[3] = v;
        } else {
            buf[3] = v >> 24;
            buf[2] = v >> 16;
            buf[1] = v >> 8;
            buf[0] = v;
        }
        break;
    }
}

static void vnc_write_pixels_generic(VncState *vs,
                                     void *pixels1, int size)
{
    uint8_t buf[4];

    if (VNC_SERVER_FB_BYTES == 4) {
        uint32_t *pixels = pixels1;
        int n, i;
        n = size >> 2;
        for (i = 0; i < n; i++) {
            vnc_convert_pixel(vs, buf, pixels[i]);
            vnc_write(vs, buf, vs->client_pf.bytes_per_pixel);
        }
    }
}

int vnc_raw_send_framebuffer_update(VncState *vs, int x, int y, int w, int h)
{
    int i;
    uint8_t *row;
    VncDisplay *vd = vs->vd;

    row = vnc_server_fb_ptr(vd, x, y);
    for (i = 0; i < h; i++) {
        vs->write_pixels(vs, row, w * VNC_SERVER_FB_BYTES);
        row += vnc_server_fb_stride(vd);
    }
    return 1;
}

int vnc_send_framebuffer_update(VncState *vs, int x, int y, int w, int h)
{
    int n = 0;

    switch(vs->vnc_encoding) {
        case VNC_ENCODING_ZLIB:
            n = vnc_zlib_send_framebuffer_update(vs, x, y, w, h);
            break;
        case VNC_ENCODING_HEXTILE:
            vnc_framebuffer_update(vs, x, y, w, h, VNC_ENCODING_HEXTILE);
            n = vnc_hextile_send_framebuffer_update(vs, x, y, w, h);
            break;
        case VNC_ENCODING_TIGHT:
            n = vnc_tight_send_framebuffer_update(vs, x, y, w, h);
            break;
        case VNC_ENCODING_TIGHT_PNG:
            n = vnc_tight_png_send_framebuffer_update(vs, x, y, w, h);
            break;
        case VNC_ENCODING_ZRLE:
            n = vnc_zrle_send_framebuffer_update(vs, x, y, w, h);
            break;
        case VNC_ENCODING_ZYWRLE:
            n = vnc_zywrle_send_framebuffer_update(vs, x, y, w, h);
            break;
        default:
            vnc_framebuffer_update(vs, x, y, w, h, VNC_ENCODING_RAW);
            n = vnc_raw_send_framebuffer_update(vs, x, y, w, h);
            break;
    }
    return n;
}

static void vnc_mouse_set(DisplayChangeListener *dcl,
                          int x, int y, int visible)
{
    /* can we ask the client(s) to move the pointer ??? */
}

static int vnc_cursor_define(VncState *vs)
{
    QEMUCursor *c = vs->vd->cursor;
    int isize;

    if (vnc_has_feature(vs, VNC_FEATURE_RICH_CURSOR)) {
        vnc_lock_output(vs);
        vnc_write_u8(vs,  VNC_MSG_SERVER_FRAMEBUFFER_UPDATE);
        vnc_write_u8(vs,  0);  /*  padding     */
        vnc_write_u16(vs, 1);  /*  # of rects  */
        vnc_framebuffer_update(vs, c->hot_x, c->hot_y, c->width, c->height,
                               VNC_ENCODING_RICH_CURSOR);
        isize = c->width * c->height * vs->client_pf.bytes_per_pixel;
        vnc_write_pixels_generic(vs, c->data, isize);
        vnc_write(vs, vs->vd->cursor_mask, vs->vd->cursor_msize);
        vnc_unlock_output(vs);
        return 0;
    }
    return -1;
}

static void vnc_dpy_cursor_define(DisplayChangeListener *dcl,
                                  QEMUCursor *c)
{
    VncDisplay *vd = container_of(dcl, VncDisplay, dcl);
    VncState *vs;

    cursor_put(vd->cursor);
    g_free(vd->cursor_mask);

    vd->cursor = c;
    cursor_get(vd->cursor);
    vd->cursor_msize = cursor_get_mono_bpl(c) * c->height;
    vd->cursor_mask = g_malloc0(vd->cursor_msize);
    cursor_get_mono_mask(c, 0, vd->cursor_mask);

    QTAILQ_FOREACH(vs, &vd->clients, next) {
        vnc_cursor_define(vs);
    }
}

static int find_and_clear_dirty_height(VncState *vs,
                                       int y, int last_x, int x, int height)
{
    int h;

    for (h = 1; h < (height - y); h++) {
        if (!test_bit(last_x, vs->dirty[y + h])) {
            break;
        }
        bitmap_clear(vs->dirty[y + h], last_x, x - last_x);
    }

    return h;
}

/*
 * Figure out how much pending data we should allow in the output
 * buffer before we throttle incremental display updates, and/or
 * drop audio samples.
 *
 * We allow for equiv of 1 full display's worth of FB updates,
 * and 1 second of audio samples. If audio backlog was larger
 * than that the client would already suffering awful audio
 * glitches, so dropping samples is no worse really).
 */
static void vnc_update_throttle_offset(VncState *vs)
{
    size_t offset =
        vs->client_width * vs->client_height * vs->client_pf.bytes_per_pixel;

    if (vs->audio_cap) {
        int bps;
        switch (vs->as.fmt) {
        default:
        case  AUDIO_FORMAT_U8:
        case  AUDIO_FORMAT_S8:
            bps = 1;
            break;
        case  AUDIO_FORMAT_U16:
        case  AUDIO_FORMAT_S16:
            bps = 2;
            break;
        case  AUDIO_FORMAT_U32:
        case  AUDIO_FORMAT_S32:
            bps = 4;
            break;
        }
        offset += vs->as.freq * bps * vs->as.nchannels;
    }

    /* Put a floor of 1MB on offset, so that if we have a large pending
     * buffer and the display is resized to a small size & back again
     * we don't suddenly apply a tiny send limit
     */
    offset = MAX(offset, 1024 * 1024);

    if (vs->throttle_output_offset != offset) {
        trace_vnc_client_throttle_threshold(
            vs, vs->ioc, vs->throttle_output_offset, offset, vs->client_width,
            vs->client_height, vs->client_pf.bytes_per_pixel, vs->audio_cap);
    }

    vs->throttle_output_offset = offset;
}

static bool vnc_should_update(VncState *vs)
{
    switch (vs->update) {
    case VNC_STATE_UPDATE_NONE:
        break;
    case VNC_STATE_UPDATE_INCREMENTAL:
        /* Only allow incremental updates if the pending send queue
         * is less than the permitted threshold, and the job worker
         * is completely idle.
         */
        if (vs->output.offset < vs->throttle_output_offset &&
            vs->job_update == VNC_STATE_UPDATE_NONE) {
            return true;
        }
        trace_vnc_client_throttle_incremental(
            vs, vs->ioc, vs->job_update, vs->output.offset);
        break;
    case VNC_STATE_UPDATE_FORCE:
        /* Only allow forced updates if the pending send queue
         * does not contain a previous forced update, and the
         * job worker is completely idle.
         *
         * Note this means we'll queue a forced update, even if
         * the output buffer size is otherwise over the throttle
         * output limit.
         */
        if (vs->force_update_offset == 0 &&
            vs->job_update == VNC_STATE_UPDATE_NONE) {
            return true;
        }
        trace_vnc_client_throttle_forced(
            vs, vs->ioc, vs->job_update, vs->force_update_offset);
        break;
    }
    return false;
}

static int vnc_update_client(VncState *vs, int has_dirty)
{
    VncDisplay *vd = vs->vd;
    VncJob *job;
    int y;
    int height, width;
    int n = 0;

    if (vs->disconnecting) {
        vnc_disconnect_finish(vs);
        return 0;
    }

    vs->has_dirty += has_dirty;
    if (!vnc_should_update(vs)) {
        return 0;
    }

    if (!vs->has_dirty && vs->update != VNC_STATE_UPDATE_FORCE) {
        return 0;
    }

    /*
     * Send screen updates to the vnc client using the server
     * surface and server dirty map.  guest surface updates
     * happening in parallel don't disturb us, the next pass will
     * send them to the client.
     */
    job = vnc_job_new(vs);

    height = pixman_image_get_height(vd->server);
    width = pixman_image_get_width(vd->server);

    y = 0;
    for (;;) {
        int x, h;
        unsigned long x2;
        unsigned long offset = find_next_bit((unsigned long *) &vs->dirty,
                                             height * VNC_DIRTY_BPL(vs),
                                             y * VNC_DIRTY_BPL(vs));
        if (offset == height * VNC_DIRTY_BPL(vs)) {
            /* no more dirty bits */
            break;
        }
        y = offset / VNC_DIRTY_BPL(vs);
        x = offset % VNC_DIRTY_BPL(vs);
        x2 = find_next_zero_bit((unsigned long *) &vs->dirty[y],
                                VNC_DIRTY_BPL(vs), x);
        bitmap_clear(vs->dirty[y], x, x2 - x);
        h = find_and_clear_dirty_height(vs, y, x, x2, height);
        x2 = MIN(x2, width / VNC_DIRTY_PIXELS_PER_BIT);
        if (x2 > x) {
            n += vnc_job_add_rect(job, x * VNC_DIRTY_PIXELS_PER_BIT, y,
                                  (x2 - x) * VNC_DIRTY_PIXELS_PER_BIT, h);
        }
        if (!x && x2 == width / VNC_DIRTY_PIXELS_PER_BIT) {
            y += h;
            if (y == height) {
                break;
            }
        }
    }

    vs->job_update = vs->update;
    vs->update = VNC_STATE_UPDATE_NONE;
    vnc_job_push(job);
    vs->has_dirty = 0;
    return n;
}

/* audio */
static void audio_capture_notify(void *opaque, audcnotification_e cmd)
{
    VncState *vs = opaque;

    assert(vs->magic == VNC_MAGIC);
    switch (cmd) {
    case AUD_CNOTIFY_DISABLE:
        vnc_lock_output(vs);
        vnc_write_u8(vs, VNC_MSG_SERVER_QEMU);
        vnc_write_u8(vs, VNC_MSG_SERVER_QEMU_AUDIO);
        vnc_write_u16(vs, VNC_MSG_SERVER_QEMU_AUDIO_END);
        vnc_unlock_output(vs);
        vnc_flush(vs);
        break;

    case AUD_CNOTIFY_ENABLE:
        vnc_lock_output(vs);
        vnc_write_u8(vs, VNC_MSG_SERVER_QEMU);
        vnc_write_u8(vs, VNC_MSG_SERVER_QEMU_AUDIO);
        vnc_write_u16(vs, VNC_MSG_SERVER_QEMU_AUDIO_BEGIN);
        vnc_unlock_output(vs);
        vnc_flush(vs);
        break;
    }
}

static void audio_capture_destroy(void *opaque)
{
}

static void audio_capture(void *opaque, const void *buf, int size)
{
    VncState *vs = opaque;

    assert(vs->magic == VNC_MAGIC);
    vnc_lock_output(vs);
    if (vs->output.offset < vs->throttle_output_offset) {
        vnc_write_u8(vs, VNC_MSG_SERVER_QEMU);
        vnc_write_u8(vs, VNC_MSG_SERVER_QEMU_AUDIO);
        vnc_write_u16(vs, VNC_MSG_SERVER_QEMU_AUDIO_DATA);
        vnc_write_u32(vs, size);
        vnc_write(vs, buf, size);
    } else {
        trace_vnc_client_throttle_audio(vs, vs->ioc, vs->output.offset);
    }
    vnc_unlock_output(vs);
    vnc_flush(vs);
}

static void audio_add(VncState *vs)
{
    struct audio_capture_ops ops;

    if (vs->audio_cap) {
        error_report("audio already running");
        return;
    }

    ops.notify = audio_capture_notify;
    ops.destroy = audio_capture_destroy;
    ops.capture = audio_capture;

    vs->audio_cap = AUD_add_capture(vs->vd->audio_state, &vs->as, &ops, vs);
    if (!vs->audio_cap) {
        error_report("Failed to add audio capture");
    }
}

static void audio_del(VncState *vs)
{
    if (vs->audio_cap) {
        AUD_del_capture(vs->audio_cap, vs);
        vs->audio_cap = NULL;
    }
}

static void vnc_disconnect_start(VncState *vs)
{
    if (vs->disconnecting) {
        return;
    }
    trace_vnc_client_disconnect_start(vs, vs->ioc);
    vnc_set_share_mode(vs, VNC_SHARE_MODE_DISCONNECTED);
    if (vs->ioc_tag) {
        g_source_remove(vs->ioc_tag);
        vs->ioc_tag = 0;
    }
    qio_channel_close(vs->ioc, NULL);
    vs->disconnecting = TRUE;
}

void vnc_disconnect_finish(VncState *vs)
{
    int i;

    trace_vnc_client_disconnect_finish(vs, vs->ioc);

    vnc_jobs_join(vs); /* Wait encoding jobs */

    vnc_lock_output(vs);
    vnc_qmp_event(vs, QAPI_EVENT_VNC_DISCONNECTED);

    buffer_free(&vs->input);
    buffer_free(&vs->output);

    qapi_free_VncClientInfo(vs->info);

    vnc_zlib_clear(vs);
    vnc_tight_clear(vs);
    vnc_zrle_clear(vs);

#ifdef CONFIG_VNC_SASL
    vnc_sasl_client_cleanup(vs);
#endif /* CONFIG_VNC_SASL */
    audio_del(vs);
    qkbd_state_lift_all_keys(vs->vd->kbd);

    if (vs->mouse_mode_notifier.notify != NULL) {
        qemu_remove_mouse_mode_change_notifier(&vs->mouse_mode_notifier);
    }
    QTAILQ_REMOVE(&vs->vd->clients, vs, next);
    if (QTAILQ_EMPTY(&vs->vd->clients)) {
        /* last client gone */
        vnc_update_server_surface(vs->vd);
    }

    vnc_unlock_output(vs);

    qemu_mutex_destroy(&vs->output_mutex);
    if (vs->bh != NULL) {
        qemu_bh_delete(vs->bh);
    }
    buffer_free(&vs->jobs_buffer);

    for (i = 0; i < VNC_STAT_ROWS; ++i) {
        g_free(vs->lossy_rect[i]);
    }
    g_free(vs->lossy_rect);

    object_unref(OBJECT(vs->ioc));
    vs->ioc = NULL;
    object_unref(OBJECT(vs->sioc));
    vs->sioc = NULL;
    vs->magic = 0;
    g_free(vs->zrle);
    g_free(vs->tight);
    g_free(vs);
}

size_t vnc_client_io_error(VncState *vs, ssize_t ret, Error *err)
{
    if (ret <= 0) {
        if (ret == 0) {
            trace_vnc_client_eof(vs, vs->ioc);
            vnc_disconnect_start(vs);
        } else if (ret != QIO_CHANNEL_ERR_BLOCK) {
            trace_vnc_client_io_error(vs, vs->ioc,
                                      err ? error_get_pretty(err) : "Unknown");
            vnc_disconnect_start(vs);
        }

        error_free(err);
        return 0;
    }
    return ret;
}


void vnc_client_error(VncState *vs)
{
    VNC_DEBUG("Closing down client sock: protocol error\n");
    vnc_disconnect_start(vs);
}


/*
 * Called to write a chunk of data to the client socket. The data may
 * be the raw data, or may have already been encoded by SASL.
 * The data will be written either straight onto the socket, or
 * written via the GNUTLS wrappers, if TLS/SSL encryption is enabled
 *
 * NB, it is theoretically possible to have 2 layers of encryption,
 * both SASL, and this TLS layer. It is highly unlikely in practice
 * though, since SASL encryption will typically be a no-op if TLS
 * is active
 *
 * Returns the number of bytes written, which may be less than
 * the requested 'datalen' if the socket would block. Returns
 * 0 on I/O error, and disconnects the client socket.
 */
size_t vnc_client_write_buf(VncState *vs, const uint8_t *data, size_t datalen)
{
    Error *err = NULL;
    ssize_t ret;
    ret = qio_channel_write(vs->ioc, (const char *)data, datalen, &err);
    VNC_DEBUG("Wrote wire %p %zd -> %ld\n", data, datalen, ret);
    return vnc_client_io_error(vs, ret, err);
}


/*
 * Called to write buffered data to the client socket, when not
 * using any SASL SSF encryption layers. Will write as much data
 * as possible without blocking. If all buffered data is written,
 * will switch the FD poll() handler back to read monitoring.
 *
 * Returns the number of bytes written, which may be less than
 * the buffered output data if the socket would block.  Returns
 * 0 on I/O error, and disconnects the client socket.
 */
static size_t vnc_client_write_plain(VncState *vs)
{
    size_t offset;
    size_t ret;

#ifdef CONFIG_VNC_SASL
    VNC_DEBUG("Write Plain: Pending output %p size %zd offset %zd. Wait SSF %d\n",
              vs->output.buffer, vs->output.capacity, vs->output.offset,
              vs->sasl.waitWriteSSF);

    if (vs->sasl.conn &&
        vs->sasl.runSSF &&
        vs->sasl.waitWriteSSF) {
        ret = vnc_client_write_buf(vs, vs->output.buffer, vs->sasl.waitWriteSSF);
        if (ret)
            vs->sasl.waitWriteSSF -= ret;
    } else
#endif /* CONFIG_VNC_SASL */
        ret = vnc_client_write_buf(vs, vs->output.buffer, vs->output.offset);
    if (!ret)
        return 0;

    if (ret >= vs->force_update_offset) {
        if (vs->force_update_offset != 0) {
            trace_vnc_client_unthrottle_forced(vs, vs->ioc);
        }
        vs->force_update_offset = 0;
    } else {
        vs->force_update_offset -= ret;
    }
    offset = vs->output.offset;
    buffer_advance(&vs->output, ret);
    if (offset >= vs->throttle_output_offset &&
        vs->output.offset < vs->throttle_output_offset) {
        trace_vnc_client_unthrottle_incremental(vs, vs->ioc, vs->output.offset);
    }

    if (vs->output.offset == 0) {
        if (vs->ioc_tag) {
            g_source_remove(vs->ioc_tag);
        }
        vs->ioc_tag = qio_channel_add_watch(
            vs->ioc, G_IO_IN, vnc_client_io, vs, NULL);
    }

    return ret;
}


/*
 * First function called whenever there is data to be written to
 * the client socket. Will delegate actual work according to whether
 * SASL SSF layers are enabled (thus requiring encryption calls)
 */
static void vnc_client_write_locked(VncState *vs)
{
#ifdef CONFIG_VNC_SASL
    if (vs->sasl.conn &&
        vs->sasl.runSSF &&
        !vs->sasl.waitWriteSSF) {
        vnc_client_write_sasl(vs);
    } else
#endif /* CONFIG_VNC_SASL */
    {
        vnc_client_write_plain(vs);
    }
}

static void vnc_client_write(VncState *vs)
{
    assert(vs->magic == VNC_MAGIC);
    vnc_lock_output(vs);
    if (vs->output.offset) {
        vnc_client_write_locked(vs);
    } else if (vs->ioc != NULL) {
        if (vs->ioc_tag) {
            g_source_remove(vs->ioc_tag);
        }
        vs->ioc_tag = qio_channel_add_watch(
            vs->ioc, G_IO_IN, vnc_client_io, vs, NULL);
    }
    vnc_unlock_output(vs);
}

void vnc_read_when(VncState *vs, VncReadEvent *func, size_t expecting)
{
    vs->read_handler = func;
    vs->read_handler_expect = expecting;
}


/*
 * Called to read a chunk of data from the client socket. The data may
 * be the raw data, or may need to be further decoded by SASL.
 * The data will be read either straight from to the socket, or
 * read via the GNUTLS wrappers, if TLS/SSL encryption is enabled
 *
 * NB, it is theoretically possible to have 2 layers of encryption,
 * both SASL, and this TLS layer. It is highly unlikely in practice
 * though, since SASL encryption will typically be a no-op if TLS
 * is active
 *
 * Returns the number of bytes read, which may be less than
 * the requested 'datalen' if the socket would block. Returns
 * 0 on I/O error or EOF, and disconnects the client socket.
 */
size_t vnc_client_read_buf(VncState *vs, uint8_t *data, size_t datalen)
{
    ssize_t ret;
    Error *err = NULL;
    ret = qio_channel_read(vs->ioc, (char *)data, datalen, &err);
    VNC_DEBUG("Read wire %p %zd -> %ld\n", data, datalen, ret);
    return vnc_client_io_error(vs, ret, err);
}


/*
 * Called to read data from the client socket to the input buffer,
 * when not using any SASL SSF encryption layers. Will read as much
 * data as possible without blocking.
 *
 * Returns the number of bytes read, which may be less than
 * the requested 'datalen' if the socket would block. Returns
 * 0 on I/O error or EOF, and disconnects the client socket.
 */
static size_t vnc_client_read_plain(VncState *vs)
{
    size_t ret;
    VNC_DEBUG("Read plain %p size %zd offset %zd\n",
              vs->input.buffer, vs->input.capacity, vs->input.offset);
    buffer_reserve(&vs->input, 4096);
    ret = vnc_client_read_buf(vs, buffer_end(&vs->input), 4096);
    if (!ret)
        return 0;
    vs->input.offset += ret;
    return ret;
}

static void vnc_jobs_bh(void *opaque)
{
    VncState *vs = opaque;

    assert(vs->magic == VNC_MAGIC);
    vnc_jobs_consume_buffer(vs);
}

/*
 * First function called whenever there is more data to be read from
 * the client socket. Will delegate actual work according to whether
 * SASL SSF layers are enabled (thus requiring decryption calls)
 * Returns 0 on success, -1 if client disconnected
 */
static int vnc_client_read(VncState *vs)
{
    size_t ret;

#ifdef CONFIG_VNC_SASL
    if (vs->sasl.conn && vs->sasl.runSSF)
        ret = vnc_client_read_sasl(vs);
    else
#endif /* CONFIG_VNC_SASL */
        ret = vnc_client_read_plain(vs);
    if (!ret) {
        if (vs->disconnecting) {
            vnc_disconnect_finish(vs);
            return -1;
        }
        return 0;
    }

    while (vs->read_handler && vs->input.offset >= vs->read_handler_expect) {
        size_t len = vs->read_handler_expect;
        int ret;

        ret = vs->read_handler(vs, vs->input.buffer, len);
        if (vs->disconnecting) {
            vnc_disconnect_finish(vs);
            return -1;
        }

        if (!ret) {
            buffer_advance(&vs->input, len);
        } else {
            vs->read_handler_expect = ret;
        }
    }
    return 0;
}

gboolean vnc_client_io(QIOChannel *ioc G_GNUC_UNUSED,
                       GIOCondition condition, void *opaque)
{
    VncState *vs = opaque;

    assert(vs->magic == VNC_MAGIC);
    if (condition & G_IO_IN) {
        if (vnc_client_read(vs) < 0) {
            /* vs is free()ed here */
            return TRUE;
        }
    }
    if (condition & G_IO_OUT) {
        vnc_client_write(vs);
    }

    if (vs->disconnecting) {
        if (vs->ioc_tag != 0) {
            g_source_remove(vs->ioc_tag);
        }
        vs->ioc_tag = 0;
    }
    return TRUE;
}


/*
 * Scale factor to apply to vs->throttle_output_offset when checking for
 * hard limit. Worst case normal usage could be x2, if we have a complete
 * incremental update and complete forced update in the output buffer.
 * So x3 should be good enough, but we pick x5 to be conservative and thus
 * (hopefully) never trigger incorrectly.
 */
#define VNC_THROTTLE_OUTPUT_LIMIT_SCALE 5

void vnc_write(VncState *vs, const void *data, size_t len)
{
    assert(vs->magic == VNC_MAGIC);
    if (vs->disconnecting) {
        return;
    }
    /* Protection against malicious client/guest to prevent our output
     * buffer growing without bound if client stops reading data. This
     * should rarely trigger, because we have earlier throttling code
     * which stops issuing framebuffer updates and drops audio data
     * if the throttle_output_offset value is exceeded. So we only reach
     * this higher level if a huge number of pseudo-encodings get
     * triggered while data can't be sent on the socket.
     *
     * NB throttle_output_offset can be zero during early protocol
     * handshake, or from the job thread's VncState clone
     */
    if (vs->throttle_output_offset != 0 &&
        (vs->output.offset / VNC_THROTTLE_OUTPUT_LIMIT_SCALE) >
        vs->throttle_output_offset) {
        trace_vnc_client_output_limit(vs, vs->ioc, vs->output.offset,
                                      vs->throttle_output_offset);
        vnc_disconnect_start(vs);
        return;
    }
    buffer_reserve(&vs->output, len);

    if (vs->ioc != NULL && buffer_empty(&vs->output)) {
        if (vs->ioc_tag) {
            g_source_remove(vs->ioc_tag);
        }
        vs->ioc_tag = qio_channel_add_watch(
            vs->ioc, G_IO_IN | G_IO_OUT, vnc_client_io, vs, NULL);
    }

    buffer_append(&vs->output, data, len);
}

void vnc_write_s32(VncState *vs, int32_t value)
{
    vnc_write_u32(vs, *(uint32_t *)&value);
}

void vnc_write_u32(VncState *vs, uint32_t value)
{
    uint8_t buf[4];

    buf[0] = (value >> 24) & 0xFF;
    buf[1] = (value >> 16) & 0xFF;
    buf[2] = (value >>  8) & 0xFF;
    buf[3] = value & 0xFF;

    vnc_write(vs, buf, 4);
}

void vnc_write_u16(VncState *vs, uint16_t value)
{
    uint8_t buf[2];

    buf[0] = (value >> 8) & 0xFF;
    buf[1] = value & 0xFF;

    vnc_write(vs, buf, 2);
}

void vnc_write_u8(VncState *vs, uint8_t value)
{
    vnc_write(vs, (char *)&value, 1);
}

void vnc_flush(VncState *vs)
{
    vnc_lock_output(vs);
    if (vs->ioc != NULL && vs->output.offset) {
        vnc_client_write_locked(vs);
    }
    if (vs->disconnecting) {
        if (vs->ioc_tag != 0) {
            g_source_remove(vs->ioc_tag);
        }
        vs->ioc_tag = 0;
    }
    vnc_unlock_output(vs);
}

static uint8_t read_u8(uint8_t *data, size_t offset)
{
    return data[offset];
}

static uint16_t read_u16(uint8_t *data, size_t offset)
{
    return ((data[offset] & 0xFF) << 8) | (data[offset + 1] & 0xFF);
}

static int32_t read_s32(uint8_t *data, size_t offset)
{
    return (int32_t)((data[offset] << 24) | (data[offset + 1] << 16) |
                     (data[offset + 2] << 8) | data[offset + 3]);
}

uint32_t read_u32(uint8_t *data, size_t offset)
{
    return ((data[offset] << 24) | (data[offset + 1] << 16) |
            (data[offset + 2] << 8) | data[offset + 3]);
}

static void client_cut_text(VncState *vs, size_t len, uint8_t *text)
{
}

static void check_pointer_type_change(Notifier *notifier, void *data)
{
    VncState *vs = container_of(notifier, VncState, mouse_mode_notifier);
    int absolute = qemu_input_is_absolute();

    if (vnc_has_feature(vs, VNC_FEATURE_POINTER_TYPE_CHANGE) && vs->absolute != absolute) {
        vnc_lock_output(vs);
        vnc_write_u8(vs, VNC_MSG_SERVER_FRAMEBUFFER_UPDATE);
        vnc_write_u8(vs, 0);
        vnc_write_u16(vs, 1);
        vnc_framebuffer_update(vs, absolute, 0,
                               pixman_image_get_width(vs->vd->server),
                               pixman_image_get_height(vs->vd->server),
                               VNC_ENCODING_POINTER_TYPE_CHANGE);
        vnc_unlock_output(vs);
        vnc_flush(vs);
    }
    vs->absolute = absolute;
}

static void pointer_event(VncState *vs, int button_mask, int x, int y)
{
    static uint32_t bmap[INPUT_BUTTON__MAX] = {
        [INPUT_BUTTON_LEFT]       = 0x01,
        [INPUT_BUTTON_MIDDLE]     = 0x02,
        [INPUT_BUTTON_RIGHT]      = 0x04,
        [INPUT_BUTTON_WHEEL_UP]   = 0x08,
        [INPUT_BUTTON_WHEEL_DOWN] = 0x10,
    };
    QemuConsole *con = vs->vd->dcl.con;
    int width = pixman_image_get_width(vs->vd->server);
    int height = pixman_image_get_height(vs->vd->server);

    if (vs->last_bmask != button_mask) {
        qemu_input_update_buttons(con, bmap, vs->last_bmask, button_mask);
        vs->last_bmask = button_mask;
    }

    if (vs->absolute) {
        qemu_input_queue_abs(con, INPUT_AXIS_X, x, 0, width);
        qemu_input_queue_abs(con, INPUT_AXIS_Y, y, 0, height);
    } else if (vnc_has_feature(vs, VNC_FEATURE_POINTER_TYPE_CHANGE)) {
        qemu_input_queue_rel(con, INPUT_AXIS_X, x - 0x7FFF);
        qemu_input_queue_rel(con, INPUT_AXIS_Y, y - 0x7FFF);
    } else {
        if (vs->last_x != -1) {
            qemu_input_queue_rel(con, INPUT_AXIS_X, x - vs->last_x);
            qemu_input_queue_rel(con, INPUT_AXIS_Y, y - vs->last_y);
        }
        vs->last_x = x;
        vs->last_y = y;
    }
    qemu_input_event_sync();
}

static void press_key(VncState *vs, QKeyCode qcode)
{
    qkbd_state_key_event(vs->vd->kbd, qcode, true);
    qkbd_state_key_event(vs->vd->kbd, qcode, false);
}

static void vnc_led_state_change(VncState *vs)
{
    if (!vnc_has_feature(vs, VNC_FEATURE_LED_STATE)) {
        return;
    }

    vnc_lock_output(vs);
    vnc_write_u8(vs, VNC_MSG_SERVER_FRAMEBUFFER_UPDATE);
    vnc_write_u8(vs, 0);
    vnc_write_u16(vs, 1);
    vnc_framebuffer_update(vs, 0, 0, 1, 1, VNC_ENCODING_LED_STATE);
    vnc_write_u8(vs, vs->vd->ledstate);
    vnc_unlock_output(vs);
    vnc_flush(vs);
}

static void kbd_leds(void *opaque, int ledstate)
{
    VncDisplay *vd = opaque;
    VncState *client;

    trace_vnc_key_guest_leds((ledstate & QEMU_CAPS_LOCK_LED),
                             (ledstate & QEMU_NUM_LOCK_LED),
                             (ledstate & QEMU_SCROLL_LOCK_LED));

    if (ledstate == vd->ledstate) {
        return;
    }

    vd->ledstate = ledstate;

    QTAILQ_FOREACH(client, &vd->clients, next) {
        vnc_led_state_change(client);
    }
}

static void do_key_event(VncState *vs, int down, int keycode, int sym)
{
    QKeyCode qcode = qemu_input_key_number_to_qcode(keycode);

    /* QEMU console switch */
    switch (qcode) {
    case Q_KEY_CODE_1 ... Q_KEY_CODE_9: /* '1' to '9' keys */
        if (vs->vd->dcl.con == NULL && down &&
            qkbd_state_modifier_get(vs->vd->kbd, QKBD_MOD_CTRL) &&
            qkbd_state_modifier_get(vs->vd->kbd, QKBD_MOD_ALT)) {
            /* Reset the modifiers sent to the current console */
            qkbd_state_lift_all_keys(vs->vd->kbd);
            console_select(qcode - Q_KEY_CODE_1);
            return;
        }
    default:
        break;
    }

    /* Turn off the lock state sync logic if the client support the led
       state extension.
    */
    if (down && vs->vd->lock_key_sync &&
        !vnc_has_feature(vs, VNC_FEATURE_LED_STATE) &&
        keycode_is_keypad(vs->vd->kbd_layout, keycode)) {
        /* If the numlock state needs to change then simulate an additional
           keypress before sending this one.  This will happen if the user
           toggles numlock away from the VNC window.
        */
        if (keysym_is_numlock(vs->vd->kbd_layout, sym & 0xFFFF)) {
            if (!qkbd_state_modifier_get(vs->vd->kbd, QKBD_MOD_NUMLOCK)) {
                trace_vnc_key_sync_numlock(true);
                press_key(vs, Q_KEY_CODE_NUM_LOCK);
            }
        } else {
            if (qkbd_state_modifier_get(vs->vd->kbd, QKBD_MOD_NUMLOCK)) {
                trace_vnc_key_sync_numlock(false);
                press_key(vs, Q_KEY_CODE_NUM_LOCK);
            }
        }
    }

    if (down && vs->vd->lock_key_sync &&
        !vnc_has_feature(vs, VNC_FEATURE_LED_STATE) &&
        ((sym >= 'A' && sym <= 'Z') || (sym >= 'a' && sym <= 'z'))) {
        /* If the capslock state needs to change then simulate an additional
           keypress before sending this one.  This will happen if the user
           toggles capslock away from the VNC window.
        */
        int uppercase = !!(sym >= 'A' && sym <= 'Z');
        bool shift = qkbd_state_modifier_get(vs->vd->kbd, QKBD_MOD_SHIFT);
        bool capslock = qkbd_state_modifier_get(vs->vd->kbd, QKBD_MOD_CAPSLOCK);
        if (capslock) {
            if (uppercase == shift) {
                trace_vnc_key_sync_capslock(false);
                press_key(vs, Q_KEY_CODE_CAPS_LOCK);
            }
        } else {
            if (uppercase != shift) {
                trace_vnc_key_sync_capslock(true);
                press_key(vs, Q_KEY_CODE_CAPS_LOCK);
            }
        }
    }

    qkbd_state_key_event(vs->vd->kbd, qcode, down);
    if (!qemu_console_is_graphic(NULL)) {
        bool numlock = qkbd_state_modifier_get(vs->vd->kbd, QKBD_MOD_NUMLOCK);
        bool control = qkbd_state_modifier_get(vs->vd->kbd, QKBD_MOD_CTRL);
        /* QEMU console emulation */
        if (down) {
            switch (keycode) {
            case 0x2a:                          /* Left Shift */
            case 0x36:                          /* Right Shift */
            case 0x1d:                          /* Left CTRL */
            case 0x9d:                          /* Right CTRL */
            case 0x38:                          /* Left ALT */
            case 0xb8:                          /* Right ALT */
                break;
            case 0xc8:
                kbd_put_keysym(QEMU_KEY_UP);
                break;
            case 0xd0:
                kbd_put_keysym(QEMU_KEY_DOWN);
                break;
            case 0xcb:
                kbd_put_keysym(QEMU_KEY_LEFT);
                break;
            case 0xcd:
                kbd_put_keysym(QEMU_KEY_RIGHT);
                break;
            case 0xd3:
                kbd_put_keysym(QEMU_KEY_DELETE);
                break;
            case 0xc7:
                kbd_put_keysym(QEMU_KEY_HOME);
                break;
            case 0xcf:
                kbd_put_keysym(QEMU_KEY_END);
                break;
            case 0xc9:
                kbd_put_keysym(QEMU_KEY_PAGEUP);
                break;
            case 0xd1:
                kbd_put_keysym(QEMU_KEY_PAGEDOWN);
                break;

            case 0x47:
                kbd_put_keysym(numlock ? '7' : QEMU_KEY_HOME);
                break;
            case 0x48:
                kbd_put_keysym(numlock ? '8' : QEMU_KEY_UP);
                break;
            case 0x49:
                kbd_put_keysym(numlock ? '9' : QEMU_KEY_PAGEUP);
                break;
            case 0x4b:
                kbd_put_keysym(numlock ? '4' : QEMU_KEY_LEFT);
                break;
            case 0x4c:
                kbd_put_keysym('5');
                break;
            case 0x4d:
                kbd_put_keysym(numlock ? '6' : QEMU_KEY_RIGHT);
                break;
            case 0x4f:
                kbd_put_keysym(numlock ? '1' : QEMU_KEY_END);
                break;
            case 0x50:
                kbd_put_keysym(numlock ? '2' : QEMU_KEY_DOWN);
                break;
            case 0x51:
                kbd_put_keysym(numlock ? '3' : QEMU_KEY_PAGEDOWN);
                break;
            case 0x52:
                kbd_put_keysym('0');
                break;
            case 0x53:
                kbd_put_keysym(numlock ? '.' : QEMU_KEY_DELETE);
                break;

            case 0xb5:
                kbd_put_keysym('/');
                break;
            case 0x37:
                kbd_put_keysym('*');
                break;
            case 0x4a:
                kbd_put_keysym('-');
                break;
            case 0x4e:
                kbd_put_keysym('+');
                break;
            case 0x9c:
                kbd_put_keysym('\n');
                break;

            default:
                if (control) {
                    kbd_put_keysym(sym & 0x1f);
                } else {
                    kbd_put_keysym(sym);
                }
                break;
            }
        }
    }
}

static const char *code2name(int keycode)
{
    return QKeyCode_str(qemu_input_key_number_to_qcode(keycode));
}

static void key_event(VncState *vs, int down, uint32_t sym)
{
    int keycode;
    int lsym = sym;

    if (lsym >= 'A' && lsym <= 'Z' && qemu_console_is_graphic(NULL)) {
        lsym = lsym - 'A' + 'a';
    }

    keycode = keysym2scancode(vs->vd->kbd_layout, lsym & 0xFFFF,
                              vs->vd->kbd, down) & SCANCODE_KEYMASK;
    trace_vnc_key_event_map(down, sym, keycode, code2name(keycode));
    do_key_event(vs, down, keycode, sym);
}

static void ext_key_event(VncState *vs, int down,
                          uint32_t sym, uint16_t keycode)
{
    /* if the user specifies a keyboard layout, always use it */
    if (keyboard_layout) {
        key_event(vs, down, sym);
    } else {
        trace_vnc_key_event_ext(down, sym, keycode, code2name(keycode));
        do_key_event(vs, down, keycode, sym);
    }
}

static void framebuffer_update_request(VncState *vs, int incremental,
                                       int x, int y, int w, int h)
{
    if (incremental) {
        if (vs->update != VNC_STATE_UPDATE_FORCE) {
            vs->update = VNC_STATE_UPDATE_INCREMENTAL;
        }
    } else {
        vs->update = VNC_STATE_UPDATE_FORCE;
        vnc_set_area_dirty(vs->dirty, vs->vd, x, y, w, h);
    }
}

static void send_ext_key_event_ack(VncState *vs)
{
    vnc_lock_output(vs);
    vnc_write_u8(vs, VNC_MSG_SERVER_FRAMEBUFFER_UPDATE);
    vnc_write_u8(vs, 0);
    vnc_write_u16(vs, 1);
    vnc_framebuffer_update(vs, 0, 0,
                           pixman_image_get_width(vs->vd->server),
                           pixman_image_get_height(vs->vd->server),
                           VNC_ENCODING_EXT_KEY_EVENT);
    vnc_unlock_output(vs);
    vnc_flush(vs);
}

static void send_ext_audio_ack(VncState *vs)
{
    vnc_lock_output(vs);
    vnc_write_u8(vs, VNC_MSG_SERVER_FRAMEBUFFER_UPDATE);
    vnc_write_u8(vs, 0);
    vnc_write_u16(vs, 1);
    vnc_framebuffer_update(vs, 0, 0,
                           pixman_image_get_width(vs->vd->server),
                           pixman_image_get_height(vs->vd->server),
                           VNC_ENCODING_AUDIO);
    vnc_unlock_output(vs);
    vnc_flush(vs);
}

static void set_encodings(VncState *vs, int32_t *encodings, size_t n_encodings)
{
    int i;
    unsigned int enc = 0;

    vs->features = 0;
    vs->vnc_encoding = 0;
    vs->tight->compression = 9;
    vs->tight->quality = -1; /* Lossless by default */
    vs->absolute = -1;

    /*
     * Start from the end because the encodings are sent in order of preference.
     * This way the preferred encoding (first encoding defined in the array)
     * will be set at the end of the loop.
     */
    for (i = n_encodings - 1; i >= 0; i--) {
        enc = encodings[i];
        switch (enc) {
        case VNC_ENCODING_RAW:
            vs->vnc_encoding = enc;
            break;
        case VNC_ENCODING_COPYRECT:
            vs->features |= VNC_FEATURE_COPYRECT_MASK;
            break;
        case VNC_ENCODING_HEXTILE:
            vs->features |= VNC_FEATURE_HEXTILE_MASK;
            vs->vnc_encoding = enc;
            break;
        case VNC_ENCODING_TIGHT:
            vs->features |= VNC_FEATURE_TIGHT_MASK;
            vs->vnc_encoding = enc;
            break;
#ifdef CONFIG_VNC_PNG
        case VNC_ENCODING_TIGHT_PNG:
            vs->features |= VNC_FEATURE_TIGHT_PNG_MASK;
            vs->vnc_encoding = enc;
            break;
#endif
        case VNC_ENCODING_ZLIB:
            /*
             * VNC_ENCODING_ZRLE compresses better than VNC_ENCODING_ZLIB.
             * So prioritize ZRLE, even if the client hints that it prefers
             * ZLIB.
             */
            if ((vs->features & VNC_FEATURE_ZRLE_MASK) == 0) {
                vs->features |= VNC_FEATURE_ZLIB_MASK;
                vs->vnc_encoding = enc;
            }
            break;
        case VNC_ENCODING_ZRLE:
            vs->features |= VNC_FEATURE_ZRLE_MASK;
            vs->vnc_encoding = enc;
            break;
        case VNC_ENCODING_ZYWRLE:
            vs->features |= VNC_FEATURE_ZYWRLE_MASK;
            vs->vnc_encoding = enc;
            break;
        case VNC_ENCODING_DESKTOPRESIZE:
            vs->features |= VNC_FEATURE_RESIZE_MASK;
            break;
        case VNC_ENCODING_POINTER_TYPE_CHANGE:
            vs->features |= VNC_FEATURE_POINTER_TYPE_CHANGE_MASK;
            break;
        case VNC_ENCODING_RICH_CURSOR:
            vs->features |= VNC_FEATURE_RICH_CURSOR_MASK;
            if (vs->vd->cursor) {
                vnc_cursor_define(vs);
            }
            break;
        case VNC_ENCODING_EXT_KEY_EVENT:
            send_ext_key_event_ack(vs);
            break;
        case VNC_ENCODING_AUDIO:
            send_ext_audio_ack(vs);
            break;
        case VNC_ENCODING_WMVi:
            vs->features |= VNC_FEATURE_WMVI_MASK;
            break;
        case VNC_ENCODING_LED_STATE:
            vs->features |= VNC_FEATURE_LED_STATE_MASK;
            break;
        case VNC_ENCODING_COMPRESSLEVEL0 ... VNC_ENCODING_COMPRESSLEVEL0 + 9:
            vs->tight->compression = (enc & 0x0F);
            break;
        case VNC_ENCODING_QUALITYLEVEL0 ... VNC_ENCODING_QUALITYLEVEL0 + 9:
            if (vs->vd->lossy) {
                vs->tight->quality = (enc & 0x0F);
            }
            break;
        default:
            VNC_DEBUG("Unknown encoding: %d (0x%.8x): %d\n", i, enc, enc);
            break;
        }
    }
    vnc_desktop_resize(vs);
    check_pointer_type_change(&vs->mouse_mode_notifier, NULL);
    vnc_led_state_change(vs);
}

static void set_pixel_conversion(VncState *vs)
{
    pixman_format_code_t fmt = qemu_pixman_get_format(&vs->client_pf);

    if (fmt == VNC_SERVER_FB_FORMAT) {
        vs->write_pixels = vnc_write_pixels_copy;
        vnc_hextile_set_pixel_conversion(vs, 0);
    } else {
        vs->write_pixels = vnc_write_pixels_generic;
        vnc_hextile_set_pixel_conversion(vs, 1);
    }
}

static void send_color_map(VncState *vs)
{
    int i;

    vnc_write_u8(vs, VNC_MSG_SERVER_SET_COLOUR_MAP_ENTRIES);
    vnc_write_u8(vs,  0);    /* padding     */
    vnc_write_u16(vs, 0);    /* first color */
    vnc_write_u16(vs, 256);  /* # of colors */

    for (i = 0; i < 256; i++) {
        PixelFormat *pf = &vs->client_pf;

        vnc_write_u16(vs, (((i >> pf->rshift) & pf->rmax) << (16 - pf->rbits)));
        vnc_write_u16(vs, (((i >> pf->gshift) & pf->gmax) << (16 - pf->gbits)));
        vnc_write_u16(vs, (((i >> pf->bshift) & pf->bmax) << (16 - pf->bbits)));
    }
}

static void set_pixel_format(VncState *vs, int bits_per_pixel,
                             int big_endian_flag, int true_color_flag,
                             int red_max, int green_max, int blue_max,
                             int red_shift, int green_shift, int blue_shift)
{
    if (!true_color_flag) {
        /* Expose a reasonable default 256 color map */
        bits_per_pixel = 8;
        red_max = 7;
        green_max = 7;
        blue_max = 3;
        red_shift = 0;
        green_shift = 3;
        blue_shift = 6;
    }

    switch (bits_per_pixel) {
    case 8:
    case 16:
    case 32:
        break;
    default:
        vnc_client_error(vs);
        return;
    }

    vs->client_pf.rmax = red_max ? red_max : 0xFF;
    vs->client_pf.rbits = ctpopl(red_max);
    vs->client_pf.rshift = red_shift;
    vs->client_pf.rmask = red_max << red_shift;
    vs->client_pf.gmax = green_max ? green_max : 0xFF;
    vs->client_pf.gbits = ctpopl(green_max);
    vs->client_pf.gshift = green_shift;
    vs->client_pf.gmask = green_max << green_shift;
    vs->client_pf.bmax = blue_max ? blue_max : 0xFF;
    vs->client_pf.bbits = ctpopl(blue_max);
    vs->client_pf.bshift = blue_shift;
    vs->client_pf.bmask = blue_max << blue_shift;
    vs->client_pf.bits_per_pixel = bits_per_pixel;
    vs->client_pf.bytes_per_pixel = bits_per_pixel / 8;
    vs->client_pf.depth = bits_per_pixel == 32 ? 24 : bits_per_pixel;
    vs->client_be = big_endian_flag;

    if (!true_color_flag) {
        send_color_map(vs);
    }

    set_pixel_conversion(vs);

    graphic_hw_invalidate(vs->vd->dcl.con);
    graphic_hw_update(vs->vd->dcl.con);
}

static void pixel_format_message (VncState *vs) {
    char pad[3] = { 0, 0, 0 };

    vs->client_pf = qemu_default_pixelformat(32);

    vnc_write_u8(vs, vs->client_pf.bits_per_pixel); /* bits-per-pixel */
    vnc_write_u8(vs, vs->client_pf.depth); /* depth */

#ifdef HOST_WORDS_BIGENDIAN
    vnc_write_u8(vs, 1);             /* big-endian-flag */
#else
    vnc_write_u8(vs, 0);             /* big-endian-flag */
#endif
    vnc_write_u8(vs, 1);             /* true-color-flag */
    vnc_write_u16(vs, vs->client_pf.rmax);     /* red-max */
    vnc_write_u16(vs, vs->client_pf.gmax);     /* green-max */
    vnc_write_u16(vs, vs->client_pf.bmax);     /* blue-max */
    vnc_write_u8(vs, vs->client_pf.rshift);    /* red-shift */
    vnc_write_u8(vs, vs->client_pf.gshift);    /* green-shift */
    vnc_write_u8(vs, vs->client_pf.bshift);    /* blue-shift */
    vnc_write(vs, pad, 3);           /* padding */

    vnc_hextile_set_pixel_conversion(vs, 0);
    vs->write_pixels = vnc_write_pixels_copy;
}

static void vnc_colordepth(VncState *vs)
{
    if (vnc_has_feature(vs, VNC_FEATURE_WMVI)) {
        /* Sending a WMVi message to notify the client*/
        vnc_lock_output(vs);
        vnc_write_u8(vs, VNC_MSG_SERVER_FRAMEBUFFER_UPDATE);
        vnc_write_u8(vs, 0);
        vnc_write_u16(vs, 1); /* number of rects */
        vnc_framebuffer_update(vs, 0, 0,
                               pixman_image_get_width(vs->vd->server),
                               pixman_image_get_height(vs->vd->server),
                               VNC_ENCODING_WMVi);
        pixel_format_message(vs);
        vnc_unlock_output(vs);
        vnc_flush(vs);
    } else {
        set_pixel_conversion(vs);
    }
}

static int protocol_client_msg(VncState *vs, uint8_t *data, size_t len)
{
    int i;
    uint16_t limit;
    uint32_t freq;
    VncDisplay *vd = vs->vd;

    if (data[0] > 3) {
        update_displaychangelistener(&vd->dcl, VNC_REFRESH_INTERVAL_BASE);
    }

    switch (data[0]) {
    case VNC_MSG_CLIENT_SET_PIXEL_FORMAT:
        if (len == 1)
            return 20;

        set_pixel_format(vs, read_u8(data, 4),
                         read_u8(data, 6), read_u8(data, 7),
                         read_u16(data, 8), read_u16(data, 10),
                         read_u16(data, 12), read_u8(data, 14),
                         read_u8(data, 15), read_u8(data, 16));
        break;
    case VNC_MSG_CLIENT_SET_ENCODINGS:
        if (len == 1)
            return 4;

        if (len == 4) {
            limit = read_u16(data, 2);
            if (limit > 0)
                return 4 + (limit * 4);
        } else
            limit = read_u16(data, 2);

        for (i = 0; i < limit; i++) {
            int32_t val = read_s32(data, 4 + (i * 4));
            memcpy(data + 4 + (i * 4), &val, sizeof(val));
        }

        set_encodings(vs, (int32_t *)(data + 4), limit);
        break;
    case VNC_MSG_CLIENT_FRAMEBUFFER_UPDATE_REQUEST:
        if (len == 1)
            return 10;

        framebuffer_update_request(vs,
                                   read_u8(data, 1), read_u16(data, 2), read_u16(data, 4),
                                   read_u16(data, 6), read_u16(data, 8));
        break;
    case VNC_MSG_CLIENT_KEY_EVENT:
        if (len == 1)
            return 8;

        key_event(vs, read_u8(data, 1), read_u32(data, 4));
        break;
    case VNC_MSG_CLIENT_POINTER_EVENT:
        if (len == 1)
            return 6;

        pointer_event(vs, read_u8(data, 1), read_u16(data, 2), read_u16(data, 4));
        break;
    case VNC_MSG_CLIENT_CUT_TEXT:
        if (len == 1) {
            return 8;
        }
        if (len == 8) {
            uint32_t dlen = read_u32(data, 4);
            if (dlen > (1 << 20)) {
                error_report("vnc: client_cut_text msg payload has %u bytes"
                             " which exceeds our limit of 1MB.", dlen);
                vnc_client_error(vs);
                break;
            }
            if (dlen > 0) {
                return 8 + dlen;
            }
        }

        client_cut_text(vs, read_u32(data, 4), data + 8);
        break;
    case VNC_MSG_CLIENT_QEMU:
        if (len == 1)
            return 2;

        switch (read_u8(data, 1)) {
        case VNC_MSG_CLIENT_QEMU_EXT_KEY_EVENT:
            if (len == 2)
                return 12;

            ext_key_event(vs, read_u16(data, 2),
                          read_u32(data, 4), read_u32(data, 8));
            break;
        case VNC_MSG_CLIENT_QEMU_AUDIO:
            if (len == 2)
                return 4;

            switch (read_u16 (data, 2)) {
            case VNC_MSG_CLIENT_QEMU_AUDIO_ENABLE:
                audio_add(vs);
                break;
            case VNC_MSG_CLIENT_QEMU_AUDIO_DISABLE:
                audio_del(vs);
                break;
            case VNC_MSG_CLIENT_QEMU_AUDIO_SET_FORMAT:
                if (len == 4)
                    return 10;
                switch (read_u8(data, 4)) {
                case 0: vs->as.fmt = AUDIO_FORMAT_U8; break;
                case 1: vs->as.fmt = AUDIO_FORMAT_S8; break;
                case 2: vs->as.fmt = AUDIO_FORMAT_U16; break;
                case 3: vs->as.fmt = AUDIO_FORMAT_S16; break;
                case 4: vs->as.fmt = AUDIO_FORMAT_U32; break;
                case 5: vs->as.fmt = AUDIO_FORMAT_S32; break;
                default:
                    VNC_DEBUG("Invalid audio format %d\n", read_u8(data, 4));
                    vnc_client_error(vs);
                    break;
                }
                vs->as.nchannels = read_u8(data, 5);
                if (vs->as.nchannels != 1 && vs->as.nchannels != 2) {
                    VNC_DEBUG("Invalid audio channel count %d\n",
                              read_u8(data, 5));
                    vnc_client_error(vs);
                    break;
                }
                freq = read_u32(data, 6);
                /* No official limit for protocol, but 48khz is a sensible
                 * upper bound for trustworthy clients, and this limit
                 * protects calculations involving 'vs->as.freq' later.
                 */
                if (freq > 48000) {
                    VNC_DEBUG("Invalid audio frequency %u > 48000", freq);
                    vnc_client_error(vs);
                    break;
                }
                vs->as.freq = freq;
                break;
            default:
                VNC_DEBUG("Invalid audio message %d\n", read_u8(data, 4));
                vnc_client_error(vs);
                break;
            }
            break;

        default:
            VNC_DEBUG("Msg: %d\n", read_u16(data, 0));
            vnc_client_error(vs);
            break;
        }
        break;
    default:
        VNC_DEBUG("Msg: %d\n", data[0]);
        vnc_client_error(vs);
        break;
    }

    vnc_update_throttle_offset(vs);
    vnc_read_when(vs, protocol_client_msg, 1);
    return 0;
}

static int protocol_client_init(VncState *vs, uint8_t *data, size_t len)
{
    char buf[1024];
    VncShareMode mode;
    int size;

    mode = data[0] ? VNC_SHARE_MODE_SHARED : VNC_SHARE_MODE_EXCLUSIVE;
    switch (vs->vd->share_policy) {
    case VNC_SHARE_POLICY_IGNORE:
        /*
         * Ignore the shared flag.  Nothing to do here.
         *
         * Doesn't conform to the rfb spec but is traditional qemu
         * behavior, thus left here as option for compatibility
         * reasons.
         */
        break;
    case VNC_SHARE_POLICY_ALLOW_EXCLUSIVE:
        /*
         * Policy: Allow clients ask for exclusive access.
         *
         * Implementation: When a client asks for exclusive access,
         * disconnect all others. Shared connects are allowed as long
         * as no exclusive connection exists.
         *
         * This is how the rfb spec suggests to handle the shared flag.
         */
        if (mode == VNC_SHARE_MODE_EXCLUSIVE) {
            VncState *client;
            QTAILQ_FOREACH(client, &vs->vd->clients, next) {
                if (vs == client) {
                    continue;
                }
                if (client->share_mode != VNC_SHARE_MODE_EXCLUSIVE &&
                    client->share_mode != VNC_SHARE_MODE_SHARED) {
                    continue;
                }
                vnc_disconnect_start(client);
            }
        }
        if (mode == VNC_SHARE_MODE_SHARED) {
            if (vs->vd->num_exclusive > 0) {
                vnc_disconnect_start(vs);
                return 0;
            }
        }
        break;
    case VNC_SHARE_POLICY_FORCE_SHARED:
        /*
         * Policy: Shared connects only.
         * Implementation: Disallow clients asking for exclusive access.
         *
         * Useful for shared desktop sessions where you don't want
         * someone forgetting to say -shared when running the vnc
         * client disconnect everybody else.
         */
        if (mode == VNC_SHARE_MODE_EXCLUSIVE) {
            vnc_disconnect_start(vs);
            return 0;
        }
        break;
    }
    vnc_set_share_mode(vs, mode);

    if (vs->vd->num_shared > vs->vd->connections_limit) {
        vnc_disconnect_start(vs);
        return 0;
    }

    assert(pixman_image_get_width(vs->vd->server) < 65536 &&
           pixman_image_get_width(vs->vd->server) >= 0);
    assert(pixman_image_get_height(vs->vd->server) < 65536 &&
           pixman_image_get_height(vs->vd->server) >= 0);
    vs->client_width = pixman_image_get_width(vs->vd->server);
    vs->client_height = pixman_image_get_height(vs->vd->server);
    vnc_write_u16(vs, vs->client_width);
    vnc_write_u16(vs, vs->client_height);

    pixel_format_message(vs);

    if (qemu_name) {
        size = snprintf(buf, sizeof(buf), "QEMU (%s)", qemu_name);
        if (size > sizeof(buf)) {
            size = sizeof(buf);
        }
    } else {
        size = snprintf(buf, sizeof(buf), "QEMU");
    }

    vnc_write_u32(vs, size);
    vnc_write(vs, buf, size);
    vnc_flush(vs);

    vnc_client_cache_auth(vs);
    vnc_qmp_event(vs, QAPI_EVENT_VNC_INITIALIZED);

    vnc_read_when(vs, protocol_client_msg, 1);

    return 0;
}

void start_client_init(VncState *vs)
{
    vnc_read_when(vs, protocol_client_init, 1);
}

static void authentication_failed(VncState *vs)
{
    vnc_write_u32(vs, 1); /* Reject auth */
    if (vs->minor >= 8) {
        static const char err[] = "Authentication failed";
        vnc_write_u32(vs, sizeof(err));
        vnc_write(vs, err, sizeof(err));
    }
    vnc_flush(vs);
    vnc_client_error(vs);
}

static int protocol_client_auth_vnc(VncState *vs, uint8_t *data, size_t len)
{
    unsigned char response[VNC_AUTH_CHALLENGE_SIZE];
    size_t i, pwlen;
    unsigned char key[8];
    time_t now = time(NULL);
    QCryptoCipher *cipher = NULL;
    Error *err = NULL;

    if (!vs->vd->password) {
        trace_vnc_auth_fail(vs, vs->auth, "password is not set", "");
        goto reject;
    }
    if (vs->vd->expires < now) {
        trace_vnc_auth_fail(vs, vs->auth, "password is expired", "");
        goto reject;
    }

    memcpy(response, vs->challenge, VNC_AUTH_CHALLENGE_SIZE);

    /* Calculate the expected challenge response */
    pwlen = strlen(vs->vd->password);
    for (i=0; i<sizeof(key); i++)
        key[i] = i<pwlen ? vs->vd->password[i] : 0;

    cipher = qcrypto_cipher_new(
        QCRYPTO_CIPHER_ALG_DES_RFB,
        QCRYPTO_CIPHER_MODE_ECB,
        key, G_N_ELEMENTS(key),
        &err);
    if (!cipher) {
        trace_vnc_auth_fail(vs, vs->auth, "cannot create cipher",
                            error_get_pretty(err));
        error_free(err);
        goto reject;
    }

    if (qcrypto_cipher_encrypt(cipher,
                               vs->challenge,
                               response,
                               VNC_AUTH_CHALLENGE_SIZE,
                               &err) < 0) {
        trace_vnc_auth_fail(vs, vs->auth, "cannot encrypt challenge response",
                            error_get_pretty(err));
        error_free(err);
        goto reject;
    }

    /* Compare expected vs actual challenge response */
    if (memcmp(response, data, VNC_AUTH_CHALLENGE_SIZE) != 0) {
        trace_vnc_auth_fail(vs, vs->auth, "mis-matched challenge response", "");
        goto reject;
    } else {
        trace_vnc_auth_pass(vs, vs->auth);
        vnc_write_u32(vs, 0); /* Accept auth */
        vnc_flush(vs);

        start_client_init(vs);
    }

    qcrypto_cipher_free(cipher);
    return 0;

reject:
    authentication_failed(vs);
    qcrypto_cipher_free(cipher);
    return 0;
}

void start_auth_vnc(VncState *vs)
{
    Error *err = NULL;

    if (qcrypto_random_bytes(vs->challenge, sizeof(vs->challenge), &err)) {
        trace_vnc_auth_fail(vs, vs->auth, "cannot get random bytes",
                            error_get_pretty(err));
        error_free(err);
        authentication_failed(vs);
        return;
    }

    /* Send client a 'random' challenge */
    vnc_write(vs, vs->challenge, sizeof(vs->challenge));
    vnc_flush(vs);

    vnc_read_when(vs, protocol_client_auth_vnc, sizeof(vs->challenge));
}


static int protocol_client_auth(VncState *vs, uint8_t *data, size_t len)
{
    /* We only advertise 1 auth scheme at a time, so client
     * must pick the one we sent. Verify this */
    if (data[0] != vs->auth) { /* Reject auth */
       trace_vnc_auth_reject(vs, vs->auth, (int)data[0]);
       authentication_failed(vs);
    } else { /* Accept requested auth */
       trace_vnc_auth_start(vs, vs->auth);
       switch (vs->auth) {
       case VNC_AUTH_NONE:
           if (vs->minor >= 8) {
               vnc_write_u32(vs, 0); /* Accept auth completion */
               vnc_flush(vs);
           }
           trace_vnc_auth_pass(vs, vs->auth);
           start_client_init(vs);
           break;

       case VNC_AUTH_VNC:
           start_auth_vnc(vs);
           break;

       case VNC_AUTH_VENCRYPT:
           start_auth_vencrypt(vs);
           break;

#ifdef CONFIG_VNC_SASL
       case VNC_AUTH_SASL:
           start_auth_sasl(vs);
           break;
#endif /* CONFIG_VNC_SASL */

       default: /* Should not be possible, but just in case */
           trace_vnc_auth_fail(vs, vs->auth, "Unhandled auth method", "");
           authentication_failed(vs);
       }
    }
    return 0;
}

static int protocol_version(VncState *vs, uint8_t *version, size_t len)
{
    char local[13];

    memcpy(local, version, 12);
    local[12] = 0;

    if (sscanf(local, "RFB %03d.%03d\n", &vs->major, &vs->minor) != 2) {
        VNC_DEBUG("Malformed protocol version %s\n", local);
        vnc_client_error(vs);
        return 0;
    }
    VNC_DEBUG("Client request protocol version %d.%d\n", vs->major, vs->minor);
    if (vs->major != 3 ||
        (vs->minor != 3 &&
         vs->minor != 4 &&
         vs->minor != 5 &&
         vs->minor != 7 &&
         vs->minor != 8)) {
        VNC_DEBUG("Unsupported client version\n");
        vnc_write_u32(vs, VNC_AUTH_INVALID);
        vnc_flush(vs);
        vnc_client_error(vs);
        return 0;
    }
    /* Some broken clients report v3.4 or v3.5, which spec requires to be treated
     * as equivalent to v3.3 by servers
     */
    if (vs->minor == 4 || vs->minor == 5)
        vs->minor = 3;

    if (vs->minor == 3) {
        trace_vnc_auth_start(vs, vs->auth);
        if (vs->auth == VNC_AUTH_NONE) {
            vnc_write_u32(vs, vs->auth);
            vnc_flush(vs);
            trace_vnc_auth_pass(vs, vs->auth);
            start_client_init(vs);
       } else if (vs->auth == VNC_AUTH_VNC) {
            VNC_DEBUG("Tell client VNC auth\n");
            vnc_write_u32(vs, vs->auth);
            vnc_flush(vs);
            start_auth_vnc(vs);
       } else {
            trace_vnc_auth_fail(vs, vs->auth,
                                "Unsupported auth method for v3.3", "");
            vnc_write_u32(vs, VNC_AUTH_INVALID);
            vnc_flush(vs);
            vnc_client_error(vs);
       }
    } else {
        vnc_write_u8(vs, 1); /* num auth */
        vnc_write_u8(vs, vs->auth);
        vnc_read_when(vs, protocol_client_auth, 1);
        vnc_flush(vs);
    }

    return 0;
}

static VncRectStat *vnc_stat_rect(VncDisplay *vd, int x, int y)
{
    struct VncSurface *vs = &vd->guest;

    return &vs->stats[y / VNC_STAT_RECT][x / VNC_STAT_RECT];
}

void vnc_sent_lossy_rect(VncState *vs, int x, int y, int w, int h)
{
    int i, j;

    w = (x + w) / VNC_STAT_RECT;
    h = (y + h) / VNC_STAT_RECT;
    x /= VNC_STAT_RECT;
    y /= VNC_STAT_RECT;

    for (j = y; j <= h; j++) {
        for (i = x; i <= w; i++) {
            vs->lossy_rect[j][i] = 1;
        }
    }
}

static int vnc_refresh_lossy_rect(VncDisplay *vd, int x, int y)
{
    VncState *vs;
    int sty = y / VNC_STAT_RECT;
    int stx = x / VNC_STAT_RECT;
    int has_dirty = 0;

    y = QEMU_ALIGN_DOWN(y, VNC_STAT_RECT);
    x = QEMU_ALIGN_DOWN(x, VNC_STAT_RECT);

    QTAILQ_FOREACH(vs, &vd->clients, next) {
        int j;

        /* kernel send buffers are full -> refresh later */
        if (vs->output.offset) {
            continue;
        }

        if (!vs->lossy_rect[sty][stx]) {
            continue;
        }

        vs->lossy_rect[sty][stx] = 0;
        for (j = 0; j < VNC_STAT_RECT; ++j) {
            bitmap_set(vs->dirty[y + j],
                       x / VNC_DIRTY_PIXELS_PER_BIT,
                       VNC_STAT_RECT / VNC_DIRTY_PIXELS_PER_BIT);
        }
        has_dirty++;
    }

    return has_dirty;
}

static int vnc_update_stats(VncDisplay *vd,  struct timeval * tv)
{
    int width = MIN(pixman_image_get_width(vd->guest.fb),
                    pixman_image_get_width(vd->server));
    int height = MIN(pixman_image_get_height(vd->guest.fb),
                     pixman_image_get_height(vd->server));
    int x, y;
    struct timeval res;
    int has_dirty = 0;

    for (y = 0; y < height; y += VNC_STAT_RECT) {
        for (x = 0; x < width; x += VNC_STAT_RECT) {
            VncRectStat *rect = vnc_stat_rect(vd, x, y);

            rect->updated = false;
        }
    }

    qemu_timersub(tv, &VNC_REFRESH_STATS, &res);

    if (timercmp(&vd->guest.last_freq_check, &res, >)) {
        return has_dirty;
    }
    vd->guest.last_freq_check = *tv;

    for (y = 0; y < height; y += VNC_STAT_RECT) {
        for (x = 0; x < width; x += VNC_STAT_RECT) {
            VncRectStat *rect= vnc_stat_rect(vd, x, y);
            int count = ARRAY_SIZE(rect->times);
            struct timeval min, max;

            if (!timerisset(&rect->times[count - 1])) {
                continue ;
            }

            max = rect->times[(rect->idx + count - 1) % count];
            qemu_timersub(tv, &max, &res);

            if (timercmp(&res, &VNC_REFRESH_LOSSY, >)) {
                rect->freq = 0;
                has_dirty += vnc_refresh_lossy_rect(vd, x, y);
                memset(rect->times, 0, sizeof (rect->times));
                continue ;
            }

            min = rect->times[rect->idx];
            max = rect->times[(rect->idx + count - 1) % count];
            qemu_timersub(&max, &min, &res);

            rect->freq = res.tv_sec + res.tv_usec / 1000000.;
            rect->freq /= count;
            rect->freq = 1. / rect->freq;
        }
    }
    return has_dirty;
}

double vnc_update_freq(VncState *vs, int x, int y, int w, int h)
{
    int i, j;
    double total = 0;
    int num = 0;

    x =  QEMU_ALIGN_DOWN(x, VNC_STAT_RECT);
    y =  QEMU_ALIGN_DOWN(y, VNC_STAT_RECT);

    for (j = y; j <= y + h; j += VNC_STAT_RECT) {
        for (i = x; i <= x + w; i += VNC_STAT_RECT) {
            total += vnc_stat_rect(vs->vd, i, j)->freq;
            num++;
        }
    }

    if (num) {
        return total / num;
    } else {
        return 0;
    }
}

static void vnc_rect_updated(VncDisplay *vd, int x, int y, struct timeval * tv)
{
    VncRectStat *rect;

    rect = vnc_stat_rect(vd, x, y);
    if (rect->updated) {
        return ;
    }
    rect->times[rect->idx] = *tv;
    rect->idx = (rect->idx + 1) % ARRAY_SIZE(rect->times);
    rect->updated = true;
}

static int vnc_refresh_server_surface(VncDisplay *vd)
{
    int width = MIN(pixman_image_get_width(vd->guest.fb),
                    pixman_image_get_width(vd->server));
    int height = MIN(pixman_image_get_height(vd->guest.fb),
                     pixman_image_get_height(vd->server));
    int cmp_bytes, server_stride, line_bytes, guest_ll, guest_stride, y = 0;
    uint8_t *guest_row0 = NULL, *server_row0;
    VncState *vs;
    int has_dirty = 0;
    pixman_image_t *tmpbuf = NULL;

    struct timeval tv = { 0, 0 };

    if (!vd->non_adaptive) {
        gettimeofday(&tv, NULL);
        has_dirty = vnc_update_stats(vd, &tv);
    }

    /*
     * Walk through the guest dirty map.
     * Check and copy modified bits from guest to server surface.
     * Update server dirty map.
     */
    server_row0 = (uint8_t *)pixman_image_get_data(vd->server);
    server_stride = guest_stride = guest_ll =
        pixman_image_get_stride(vd->server);
    cmp_bytes = MIN(VNC_DIRTY_PIXELS_PER_BIT * VNC_SERVER_FB_BYTES,
                    server_stride);
    if (vd->guest.format != VNC_SERVER_FB_FORMAT) {
        int width = pixman_image_get_width(vd->server);
        tmpbuf = qemu_pixman_linebuf_create(VNC_SERVER_FB_FORMAT, width);
    } else {
        int guest_bpp =
            PIXMAN_FORMAT_BPP(pixman_image_get_format(vd->guest.fb));
        guest_row0 = (uint8_t *)pixman_image_get_data(vd->guest.fb);
        guest_stride = pixman_image_get_stride(vd->guest.fb);
        guest_ll = pixman_image_get_width(vd->guest.fb)
                   * DIV_ROUND_UP(guest_bpp, 8);
    }
    line_bytes = MIN(server_stride, guest_ll);

    for (;;) {
        int x;
        uint8_t *guest_ptr, *server_ptr;
        unsigned long offset = find_next_bit((unsigned long *) &vd->guest.dirty,
                                             height * VNC_DIRTY_BPL(&vd->guest),
                                             y * VNC_DIRTY_BPL(&vd->guest));
        if (offset == height * VNC_DIRTY_BPL(&vd->guest)) {
            /* no more dirty bits */
            break;
        }
        y = offset / VNC_DIRTY_BPL(&vd->guest);
        x = offset % VNC_DIRTY_BPL(&vd->guest);

        server_ptr = server_row0 + y * server_stride + x * cmp_bytes;

        if (vd->guest.format != VNC_SERVER_FB_FORMAT) {
            qemu_pixman_linebuf_fill(tmpbuf, vd->guest.fb, width, 0, y);
            guest_ptr = (uint8_t *)pixman_image_get_data(tmpbuf);
        } else {
            guest_ptr = guest_row0 + y * guest_stride;
        }
        guest_ptr += x * cmp_bytes;

        for (; x < DIV_ROUND_UP(width, VNC_DIRTY_PIXELS_PER_BIT);
             x++, guest_ptr += cmp_bytes, server_ptr += cmp_bytes) {
            int _cmp_bytes = cmp_bytes;
            if (!test_and_clear_bit(x, vd->guest.dirty[y])) {
                continue;
            }
            if ((x + 1) * cmp_bytes > line_bytes) {
                _cmp_bytes = line_bytes - x * cmp_bytes;
            }
            assert(_cmp_bytes >= 0);
            if (memcmp(server_ptr, guest_ptr, _cmp_bytes) == 0) {
                continue;
            }
            memcpy(server_ptr, guest_ptr, _cmp_bytes);
            if (!vd->non_adaptive) {
                vnc_rect_updated(vd, x * VNC_DIRTY_PIXELS_PER_BIT,
                                 y, &tv);
            }
            QTAILQ_FOREACH(vs, &vd->clients, next) {
                set_bit(x, vs->dirty[y]);
            }
            has_dirty++;
        }

        y++;
    }
    qemu_pixman_image_unref(tmpbuf);
    return has_dirty;
}

static void vnc_refresh(DisplayChangeListener *dcl)
{
    VncDisplay *vd = container_of(dcl, VncDisplay, dcl);
    VncState *vs, *vn;
    int has_dirty, rects = 0;

    if (QTAILQ_EMPTY(&vd->clients)) {
        update_displaychangelistener(&vd->dcl, VNC_REFRESH_INTERVAL_MAX);
        return;
    }

    graphic_hw_update(vd->dcl.con);

    if (vnc_trylock_display(vd)) {
        update_displaychangelistener(&vd->dcl, VNC_REFRESH_INTERVAL_BASE);
        return;
    }

    has_dirty = vnc_refresh_server_surface(vd);
    vnc_unlock_display(vd);

    QTAILQ_FOREACH_SAFE(vs, &vd->clients, next, vn) {
        rects += vnc_update_client(vs, has_dirty);
        /* vs might be free()ed here */
    }

    if (has_dirty && rects) {
        vd->dcl.update_interval /= 2;
        if (vd->dcl.update_interval < VNC_REFRESH_INTERVAL_BASE) {
            vd->dcl.update_interval = VNC_REFRESH_INTERVAL_BASE;
        }
    } else {
        vd->dcl.update_interval += VNC_REFRESH_INTERVAL_INC;
        if (vd->dcl.update_interval > VNC_REFRESH_INTERVAL_MAX) {
            vd->dcl.update_interval = VNC_REFRESH_INTERVAL_MAX;
        }
    }
}

static void vnc_connect(VncDisplay *vd, QIOChannelSocket *sioc,
                        bool skipauth, bool websocket)
{
    VncState *vs = g_new0(VncState, 1);
    bool first_client = QTAILQ_EMPTY(&vd->clients);
    int i;

    trace_vnc_client_connect(vs, sioc);
    vs->zrle = g_new0(VncZrle, 1);
    vs->tight = g_new0(VncTight, 1);
    vs->magic = VNC_MAGIC;
    vs->sioc = sioc;
    object_ref(OBJECT(vs->sioc));
    vs->ioc = QIO_CHANNEL(sioc);
    object_ref(OBJECT(vs->ioc));
    vs->vd = vd;

    buffer_init(&vs->input,          "vnc-input/%p", sioc);
    buffer_init(&vs->output,         "vnc-output/%p", sioc);
    buffer_init(&vs->jobs_buffer,    "vnc-jobs_buffer/%p", sioc);

    buffer_init(&vs->tight->tight,    "vnc-tight/%p", sioc);
    buffer_init(&vs->tight->zlib,     "vnc-tight-zlib/%p", sioc);
    buffer_init(&vs->tight->gradient, "vnc-tight-gradient/%p", sioc);
#ifdef CONFIG_VNC_JPEG
    buffer_init(&vs->tight->jpeg,     "vnc-tight-jpeg/%p", sioc);
#endif
#ifdef CONFIG_VNC_PNG
    buffer_init(&vs->tight->png,      "vnc-tight-png/%p", sioc);
#endif
    buffer_init(&vs->zlib.zlib,      "vnc-zlib/%p", sioc);
    buffer_init(&vs->zrle->zrle,      "vnc-zrle/%p", sioc);
    buffer_init(&vs->zrle->fb,        "vnc-zrle-fb/%p", sioc);
    buffer_init(&vs->zrle->zlib,      "vnc-zrle-zlib/%p", sioc);

    if (skipauth) {
        vs->auth = VNC_AUTH_NONE;
        vs->subauth = VNC_AUTH_INVALID;
    } else {
        if (websocket) {
            vs->auth = vd->ws_auth;
            vs->subauth = VNC_AUTH_INVALID;
        } else {
            vs->auth = vd->auth;
            vs->subauth = vd->subauth;
        }
    }
    VNC_DEBUG("Client sioc=%p ws=%d auth=%d subauth=%d\n",
              sioc, websocket, vs->auth, vs->subauth);

    vs->lossy_rect = g_malloc0(VNC_STAT_ROWS * sizeof (*vs->lossy_rect));
    for (i = 0; i < VNC_STAT_ROWS; ++i) {
        vs->lossy_rect[i] = g_new0(uint8_t, VNC_STAT_COLS);
    }

    VNC_DEBUG("New client on socket %p\n", vs->sioc);
    update_displaychangelistener(&vd->dcl, VNC_REFRESH_INTERVAL_BASE);
    qio_channel_set_blocking(vs->ioc, false, NULL);
    if (vs->ioc_tag) {
        g_source_remove(vs->ioc_tag);
    }
    if (websocket) {
        vs->websocket = 1;
        if (vd->tlscreds) {
            vs->ioc_tag = qio_channel_add_watch(
                vs->ioc, G_IO_IN, vncws_tls_handshake_io, vs, NULL);
        } else {
            vs->ioc_tag = qio_channel_add_watch(
                vs->ioc, G_IO_IN, vncws_handshake_io, vs, NULL);
        }
    } else {
        vs->ioc_tag = qio_channel_add_watch(
            vs->ioc, G_IO_IN, vnc_client_io, vs, NULL);
    }

    vnc_client_cache_addr(vs);
    vnc_qmp_event(vs, QAPI_EVENT_VNC_CONNECTED);
    vnc_set_share_mode(vs, VNC_SHARE_MODE_CONNECTING);

    vs->last_x = -1;
    vs->last_y = -1;

    vs->as.freq = 44100;
    vs->as.nchannels = 2;
    vs->as.fmt = AUDIO_FORMAT_S16;
    vs->as.endianness = 0;

    qemu_mutex_init(&vs->output_mutex);
    vs->bh = qemu_bh_new(vnc_jobs_bh, vs);

    QTAILQ_INSERT_TAIL(&vd->clients, vs, next);
    if (first_client) {
        vnc_update_server_surface(vd);
    }

    graphic_hw_update(vd->dcl.con);

    if (!vs->websocket) {
        vnc_start_protocol(vs);
    }

    if (vd->num_connecting > vd->connections_limit) {
        QTAILQ_FOREACH(vs, &vd->clients, next) {
            if (vs->share_mode == VNC_SHARE_MODE_CONNECTING) {
                vnc_disconnect_start(vs);
                return;
            }
        }
    }
}

void vnc_start_protocol(VncState *vs)
{
    vnc_write(vs, "RFB 003.008\n", 12);
    vnc_flush(vs);
    vnc_read_when(vs, protocol_version, 12);

    vs->mouse_mode_notifier.notify = check_pointer_type_change;
    qemu_add_mouse_mode_change_notifier(&vs->mouse_mode_notifier);
}

static void vnc_listen_io(QIONetListener *listener,
                          QIOChannelSocket *cioc,
                          void *opaque)
{
    VncDisplay *vd = opaque;
    bool isWebsock = listener == vd->wslistener;

    qio_channel_set_name(QIO_CHANNEL(cioc),
                         isWebsock ? "vnc-ws-server" : "vnc-server");
    qio_channel_set_delay(QIO_CHANNEL(cioc), false);
    vnc_connect(vd, cioc, false, isWebsock);
}

static const DisplayChangeListenerOps dcl_ops = {
    .dpy_name             = "vnc",
    .dpy_refresh          = vnc_refresh,
    .dpy_gfx_update       = vnc_dpy_update,
    .dpy_gfx_switch       = vnc_dpy_switch,
    .dpy_gfx_check_format = qemu_pixman_check_format,
    .dpy_mouse_set        = vnc_mouse_set,
    .dpy_cursor_define    = vnc_dpy_cursor_define,
};

void vnc_display_init(const char *id, Error **errp)
{
    VncDisplay *vd;

    if (vnc_display_find(id) != NULL) {
        return;
    }
    vd = g_malloc0(sizeof(*vd));

    vd->id = strdup(id);
    QTAILQ_INSERT_TAIL(&vnc_displays, vd, next);

    QTAILQ_INIT(&vd->clients);
    vd->expires = TIME_MAX;

    if (keyboard_layout) {
        trace_vnc_key_map_init(keyboard_layout);
        vd->kbd_layout = init_keyboard_layout(name2keysym,
                                              keyboard_layout, errp);
    } else {
        vd->kbd_layout = init_keyboard_layout(name2keysym, "en-us", errp);
    }

    if (!vd->kbd_layout) {
        return;
    }

    vd->share_policy = VNC_SHARE_POLICY_ALLOW_EXCLUSIVE;
    vd->connections_limit = 32;

    qemu_mutex_init(&vd->mutex);
    vnc_start_worker_thread();

    vd->dcl.ops = &dcl_ops;
    register_displaychangelistener(&vd->dcl);
    vd->kbd = qkbd_state_init(vd->dcl.con);
}


static void vnc_display_close(VncDisplay *vd)
{
    if (!vd) {
        return;
    }
    vd->is_unix = false;

    if (vd->listener) {
        qio_net_listener_disconnect(vd->listener);
        object_unref(OBJECT(vd->listener));
    }
    vd->listener = NULL;

    if (vd->wslistener) {
        qio_net_listener_disconnect(vd->wslistener);
        object_unref(OBJECT(vd->wslistener));
    }
    vd->wslistener = NULL;

    vd->auth = VNC_AUTH_INVALID;
    vd->subauth = VNC_AUTH_INVALID;
    if (vd->tlscreds) {
        object_unparent(OBJECT(vd->tlscreds));
        vd->tlscreds = NULL;
    }
    if (vd->tlsauthz) {
        object_unparent(OBJECT(vd->tlsauthz));
        vd->tlsauthz = NULL;
    }
    g_free(vd->tlsauthzid);
    vd->tlsauthzid = NULL;
    if (vd->lock_key_sync) {
        qemu_remove_led_event_handler(vd->led);
        vd->led = NULL;
    }
#ifdef CONFIG_VNC_SASL
    if (vd->sasl.authz) {
        object_unparent(OBJECT(vd->sasl.authz));
        vd->sasl.authz = NULL;
    }
    g_free(vd->sasl.authzid);
    vd->sasl.authzid = NULL;
#endif
}

int vnc_display_password(const char *id, const char *password)
{
    VncDisplay *vd = vnc_display_find(id);

    if (!vd) {
        return -EINVAL;
    }
    if (vd->auth == VNC_AUTH_NONE) {
        error_printf_unless_qmp("If you want use passwords please enable "
                                "password auth using '-vnc ${dpy},password'.\n");
        return -EINVAL;
    }

    g_free(vd->password);
    vd->password = g_strdup(password);

    return 0;
}

int vnc_display_pw_expire(const char *id, time_t expires)
{
    VncDisplay *vd = vnc_display_find(id);

    if (!vd) {
        return -EINVAL;
    }

    vd->expires = expires;
    return 0;
}

static void vnc_display_print_local_addr(VncDisplay *vd)
{
    SocketAddress *addr;

    if (!vd->listener || !vd->listener->nsioc) {
        return;
    }

    addr = qio_channel_socket_get_local_address(vd->listener->sioc[0], NULL);
    if (!addr) {
        return;
    }

    if (addr->type != SOCKET_ADDRESS_TYPE_INET) {
        qapi_free_SocketAddress(addr);
        return;
    }
    error_printf_unless_qmp("VNC server running on %s:%s\n",
                            addr->u.inet.host,
                            addr->u.inet.port);
    qapi_free_SocketAddress(addr);
}

static QemuOptsList qemu_vnc_opts = {
    .name = "vnc",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_vnc_opts.head),
    .implied_opt_name = "vnc",
    .desc = {
        {
            .name = "vnc",
            .type = QEMU_OPT_STRING,
        },{
            .name = "websocket",
            .type = QEMU_OPT_STRING,
        },{
            .name = "tls-creds",
            .type = QEMU_OPT_STRING,
        },{
            .name = "share",
            .type = QEMU_OPT_STRING,
        },{
            .name = "display",
            .type = QEMU_OPT_STRING,
        },{
            .name = "head",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "connections",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "to",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "ipv4",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "ipv6",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "password",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "reverse",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "lock-key-sync",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "key-delay-ms",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "sasl",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "acl",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "tls-authz",
            .type = QEMU_OPT_STRING,
        },{
            .name = "sasl-authz",
            .type = QEMU_OPT_STRING,
        },{
            .name = "lossy",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "non-adaptive",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "audiodev",
            .type = QEMU_OPT_STRING,
        },
        { /* end of list */ }
    },
};


static int
vnc_display_setup_auth(int *auth,
                       int *subauth,
                       QCryptoTLSCreds *tlscreds,
                       bool password,
                       bool sasl,
                       bool websocket,
                       Error **errp)
{
    /*
     * We have a choice of 3 authentication options
     *
     *   1. none
     *   2. vnc
     *   3. sasl
     *
     * The channel can be run in 2 modes
     *
     *   1. clear
     *   2. tls
     *
     * And TLS can use 2 types of credentials
     *
     *   1. anon
     *   2. x509
     *
     * We thus have 9 possible logical combinations
     *
     *   1. clear + none
     *   2. clear + vnc
     *   3. clear + sasl
     *   4. tls + anon + none
     *   5. tls + anon + vnc
     *   6. tls + anon + sasl
     *   7. tls + x509 + none
     *   8. tls + x509 + vnc
     *   9. tls + x509 + sasl
     *
     * These need to be mapped into the VNC auth schemes
     * in an appropriate manner. In regular VNC, all the
     * TLS options get mapped into VNC_AUTH_VENCRYPT
     * sub-auth types.
     *
     * In websockets, the https:// protocol already provides
     * TLS support, so there is no need to make use of the
     * VeNCrypt extension. Furthermore, websockets browser
     * clients could not use VeNCrypt even if they wanted to,
     * as they cannot control when the TLS handshake takes
     * place. Thus there is no option but to rely on https://,
     * meaning combinations 4->6 and 7->9 will be mapped to
     * VNC auth schemes in the same way as combos 1->3.
     *
     * Regardless of fact that we have a different mapping to
     * VNC auth mechs for plain VNC vs websockets VNC, the end
     * result has the same security characteristics.
     */
    if (websocket || !tlscreds) {
        if (password) {
            VNC_DEBUG("Initializing VNC server with password auth\n");
            *auth = VNC_AUTH_VNC;
        } else if (sasl) {
            VNC_DEBUG("Initializing VNC server with SASL auth\n");
            *auth = VNC_AUTH_SASL;
        } else {
            VNC_DEBUG("Initializing VNC server with no auth\n");
            *auth = VNC_AUTH_NONE;
        }
        *subauth = VNC_AUTH_INVALID;
    } else {
        bool is_x509 = object_dynamic_cast(OBJECT(tlscreds),
                                           TYPE_QCRYPTO_TLS_CREDS_X509) != NULL;
        bool is_anon = object_dynamic_cast(OBJECT(tlscreds),
                                           TYPE_QCRYPTO_TLS_CREDS_ANON) != NULL;

        if (!is_x509 && !is_anon) {
            error_setg(errp,
                       "Unsupported TLS cred type %s",
                       object_get_typename(OBJECT(tlscreds)));
            return -1;
        }
        *auth = VNC_AUTH_VENCRYPT;
        if (password) {
            if (is_x509) {
                VNC_DEBUG("Initializing VNC server with x509 password auth\n");
                *subauth = VNC_AUTH_VENCRYPT_X509VNC;
            } else {
                VNC_DEBUG("Initializing VNC server with TLS password auth\n");
                *subauth = VNC_AUTH_VENCRYPT_TLSVNC;
            }

        } else if (sasl) {
            if (is_x509) {
                VNC_DEBUG("Initializing VNC server with x509 SASL auth\n");
                *subauth = VNC_AUTH_VENCRYPT_X509SASL;
            } else {
                VNC_DEBUG("Initializing VNC server with TLS SASL auth\n");
                *subauth = VNC_AUTH_VENCRYPT_TLSSASL;
            }
        } else {
            if (is_x509) {
                VNC_DEBUG("Initializing VNC server with x509 no auth\n");
                *subauth = VNC_AUTH_VENCRYPT_X509NONE;
            } else {
                VNC_DEBUG("Initializing VNC server with TLS no auth\n");
                *subauth = VNC_AUTH_VENCRYPT_TLSNONE;
            }
        }
    }
    return 0;
}


static int vnc_display_get_address(const char *addrstr,
                                   bool websocket,
                                   bool reverse,
                                   int displaynum,
                                   int to,
                                   bool has_ipv4,
                                   bool has_ipv6,
                                   bool ipv4,
                                   bool ipv6,
                                   SocketAddress **retaddr,
                                   Error **errp)
{
    int ret = -1;
    SocketAddress *addr = NULL;

    addr = g_new0(SocketAddress, 1);

    if (strncmp(addrstr, "unix:", 5) == 0) {
        addr->type = SOCKET_ADDRESS_TYPE_UNIX;
        addr->u.q_unix.path = g_strdup(addrstr + 5);

        if (websocket) {
            error_setg(errp, "UNIX sockets not supported with websock");
            goto cleanup;
        }

        if (to) {
            error_setg(errp, "Port range not support with UNIX socket");
            goto cleanup;
        }
        ret = 0;
    } else {
        const char *port;
        size_t hostlen;
        unsigned long long baseport = 0;
        InetSocketAddress *inet;

        port = strrchr(addrstr, ':');
        if (!port) {
            if (websocket) {
                hostlen = 0;
                port = addrstr;
            } else {
                error_setg(errp, "no vnc port specified");
                goto cleanup;
            }
        } else {
            hostlen = port - addrstr;
            port++;
            if (*port == '\0') {
                error_setg(errp, "vnc port cannot be empty");
                goto cleanup;
            }
        }

        addr->type = SOCKET_ADDRESS_TYPE_INET;
        inet = &addr->u.inet;
        if (addrstr[0] == '[' && addrstr[hostlen - 1] == ']') {
            inet->host = g_strndup(addrstr + 1, hostlen - 2);
        } else {
            inet->host = g_strndup(addrstr, hostlen);
        }
        /* plain VNC port is just an offset, for websocket
         * port is absolute */
        if (websocket) {
            if (g_str_equal(addrstr, "") ||
                g_str_equal(addrstr, "on")) {
                if (displaynum == -1) {
                    error_setg(errp, "explicit websocket port is required");
                    goto cleanup;
                }
                inet->port = g_strdup_printf(
                    "%d", displaynum + 5700);
                if (to) {
                    inet->has_to = true;
                    inet->to = to + 5700;
                }
            } else {
                inet->port = g_strdup(port);
            }
        } else {
            int offset = reverse ? 0 : 5900;
            if (parse_uint_full(port, &baseport, 10) < 0) {
                error_setg(errp, "can't convert to a number: %s", port);
                goto cleanup;
            }
            if (baseport > 65535 ||
                baseport + offset > 65535) {
                error_setg(errp, "port %s out of range", port);
                goto cleanup;
            }
            inet->port = g_strdup_printf(
                "%d", (int)baseport + offset);

            if (to) {
                inet->has_to = true;
                inet->to = to + offset;
            }
        }

        inet->ipv4 = ipv4;
        inet->has_ipv4 = has_ipv4;
        inet->ipv6 = ipv6;
        inet->has_ipv6 = has_ipv6;

        ret = baseport;
    }

    *retaddr = addr;

 cleanup:
    if (ret < 0) {
        qapi_free_SocketAddress(addr);
    }
    return ret;
}

static void vnc_free_addresses(SocketAddress ***retsaddr,
                               size_t *retnsaddr)
{
    size_t i;

    for (i = 0; i < *retnsaddr; i++) {
        qapi_free_SocketAddress((*retsaddr)[i]);
    }
    g_free(*retsaddr);

    *retsaddr = NULL;
    *retnsaddr = 0;
}

static int vnc_display_get_addresses(QemuOpts *opts,
                                     bool reverse,
                                     SocketAddress ***retsaddr,
                                     size_t *retnsaddr,
                                     SocketAddress ***retwsaddr,
                                     size_t *retnwsaddr,
                                     Error **errp)
{
    SocketAddress *saddr = NULL;
    SocketAddress *wsaddr = NULL;
    QemuOptsIter addriter;
    const char *addr;
    int to = qemu_opt_get_number(opts, "to", 0);
    bool has_ipv4 = qemu_opt_get(opts, "ipv4");
    bool has_ipv6 = qemu_opt_get(opts, "ipv6");
    bool ipv4 = qemu_opt_get_bool(opts, "ipv4", false);
    bool ipv6 = qemu_opt_get_bool(opts, "ipv6", false);
    int displaynum = -1;
    int ret = -1;

    *retsaddr = NULL;
    *retnsaddr = 0;
    *retwsaddr = NULL;
    *retnwsaddr = 0;

    addr = qemu_opt_get(opts, "vnc");
    if (addr == NULL || g_str_equal(addr, "none")) {
        ret = 0;
        goto cleanup;
    }
    if (qemu_opt_get(opts, "websocket") &&
        !qcrypto_hash_supports(QCRYPTO_HASH_ALG_SHA1)) {
        error_setg(errp,
                   "SHA1 hash support is required for websockets");
        goto cleanup;
    }

    qemu_opt_iter_init(&addriter, opts, "vnc");
    while ((addr = qemu_opt_iter_next(&addriter)) != NULL) {
        int rv;
        rv = vnc_display_get_address(addr, false, reverse, 0, to,
                                     has_ipv4, has_ipv6,
                                     ipv4, ipv6,
                                     &saddr, errp);
        if (rv < 0) {
            goto cleanup;
        }
        /* Historical compat - first listen address can be used
         * to set the default websocket port
         */
        if (displaynum == -1) {
            displaynum = rv;
        }
        *retsaddr = g_renew(SocketAddress *, *retsaddr, *retnsaddr + 1);
        (*retsaddr)[(*retnsaddr)++] = saddr;
    }

    /* If we had multiple primary displays, we don't do defaults
     * for websocket, and require explicit config instead. */
    if (*retnsaddr > 1) {
        displaynum = -1;
    }

    qemu_opt_iter_init(&addriter, opts, "websocket");
    while ((addr = qemu_opt_iter_next(&addriter)) != NULL) {
        if (vnc_display_get_address(addr, true, reverse, displaynum, to,
                                    has_ipv4, has_ipv6,
                                    ipv4, ipv6,
                                    &wsaddr, errp) < 0) {
            goto cleanup;
        }

        /* Historical compat - if only a single listen address was
         * provided, then this is used to set the default listen
         * address for websocket too
         */
        if (*retnsaddr == 1 &&
            (*retsaddr)[0]->type == SOCKET_ADDRESS_TYPE_INET &&
            wsaddr->type == SOCKET_ADDRESS_TYPE_INET &&
            g_str_equal(wsaddr->u.inet.host, "") &&
            !g_str_equal((*retsaddr)[0]->u.inet.host, "")) {
            g_free(wsaddr->u.inet.host);
            wsaddr->u.inet.host = g_strdup((*retsaddr)[0]->u.inet.host);
        }

        *retwsaddr = g_renew(SocketAddress *, *retwsaddr, *retnwsaddr + 1);
        (*retwsaddr)[(*retnwsaddr)++] = wsaddr;
    }

    ret = 0;
 cleanup:
    if (ret < 0) {
        vnc_free_addresses(retsaddr, retnsaddr);
        vnc_free_addresses(retwsaddr, retnwsaddr);
    }
    return ret;
}

static int vnc_display_connect(VncDisplay *vd,
                               SocketAddress **saddr,
                               size_t nsaddr,
                               SocketAddress **wsaddr,
                               size_t nwsaddr,
                               Error **errp)
{
    /* connect to viewer */
    QIOChannelSocket *sioc = NULL;
    if (nwsaddr != 0) {
        error_setg(errp, "Cannot use websockets in reverse mode");
        return -1;
    }
    if (nsaddr != 1) {
        error_setg(errp, "Expected a single address in reverse mode");
        return -1;
    }
    /* TODO SOCKET_ADDRESS_TYPE_FD when fd has AF_UNIX */
    vd->is_unix = saddr[0]->type == SOCKET_ADDRESS_TYPE_UNIX;
    sioc = qio_channel_socket_new();
    qio_channel_set_name(QIO_CHANNEL(sioc), "vnc-reverse");
    if (qio_channel_socket_connect_sync(sioc, saddr[0], errp) < 0) {
        return -1;
    }
    vnc_connect(vd, sioc, false, false);
    object_unref(OBJECT(sioc));
    return 0;
}


static int vnc_display_listen(VncDisplay *vd,
                              SocketAddress **saddr,
                              size_t nsaddr,
                              SocketAddress **wsaddr,
                              size_t nwsaddr,
                              Error **errp)
{
    size_t i;

    if (nsaddr) {
        vd->listener = qio_net_listener_new();
        qio_net_listener_set_name(vd->listener, "vnc-listen");
        for (i = 0; i < nsaddr; i++) {
            if (qio_net_listener_open_sync(vd->listener,
                                           saddr[i], 1,
                                           errp) < 0)  {
                return -1;
            }
        }

        qio_net_listener_set_client_func(vd->listener,
                                         vnc_listen_io, vd, NULL);
    }

    if (nwsaddr) {
        vd->wslistener = qio_net_listener_new();
        qio_net_listener_set_name(vd->wslistener, "vnc-ws-listen");
        for (i = 0; i < nwsaddr; i++) {
            if (qio_net_listener_open_sync(vd->wslistener,
                                           wsaddr[i], 1,
                                           errp) < 0)  {
                return -1;
            }
        }

        qio_net_listener_set_client_func(vd->wslistener,
                                         vnc_listen_io, vd, NULL);
    }

    return 0;
}


void vnc_display_open(const char *id, Error **errp)
{
    VncDisplay *vd = vnc_display_find(id);
    QemuOpts *opts = qemu_opts_find(&qemu_vnc_opts, id);
    SocketAddress **saddr = NULL, **wsaddr = NULL;
    size_t nsaddr, nwsaddr;
    const char *share, *device_id;
    QemuConsole *con;
    bool password = false;
    bool reverse = false;
    const char *credid;
    bool sasl = false;
    int acl = 0;
    const char *tlsauthz;
    const char *saslauthz;
    int lock_key_sync = 1;
    int key_delay_ms;
    const char *audiodev;

    if (!vd) {
        error_setg(errp, "VNC display not active");
        return;
    }
    vnc_display_close(vd);

    if (!opts) {
        return;
    }

    reverse = qemu_opt_get_bool(opts, "reverse", false);
    if (vnc_display_get_addresses(opts, reverse, &saddr, &nsaddr,
                                  &wsaddr, &nwsaddr, errp) < 0) {
        goto fail;
    }

    password = qemu_opt_get_bool(opts, "password", false);
    if (password) {
        if (fips_get_state()) {
            error_setg(errp,
                       "VNC password auth disabled due to FIPS mode, "
                       "consider using the VeNCrypt or SASL authentication "
                       "methods as an alternative");
            goto fail;
        }
        if (!qcrypto_cipher_supports(
                QCRYPTO_CIPHER_ALG_DES_RFB, QCRYPTO_CIPHER_MODE_ECB)) {
            error_setg(errp,
                       "Cipher backend does not support DES RFB algorithm");
            goto fail;
        }
    }

    lock_key_sync = qemu_opt_get_bool(opts, "lock-key-sync", true);
    key_delay_ms = qemu_opt_get_number(opts, "key-delay-ms", 10);
    sasl = qemu_opt_get_bool(opts, "sasl", false);
#ifndef CONFIG_VNC_SASL
    if (sasl) {
        error_setg(errp, "VNC SASL auth requires cyrus-sasl support");
        goto fail;
    }
#endif /* CONFIG_VNC_SASL */
    credid = qemu_opt_get(opts, "tls-creds");
    if (credid) {
        Object *creds;
        creds = object_resolve_path_component(
            object_get_objects_root(), credid);
        if (!creds) {
            error_setg(errp, "No TLS credentials with id '%s'",
                       credid);
            goto fail;
        }
        vd->tlscreds = (QCryptoTLSCreds *)
            object_dynamic_cast(creds,
                                TYPE_QCRYPTO_TLS_CREDS);
        if (!vd->tlscreds) {
            error_setg(errp, "Object with id '%s' is not TLS credentials",
                       credid);
            goto fail;
        }
        object_ref(OBJECT(vd->tlscreds));

        if (vd->tlscreds->endpoint != QCRYPTO_TLS_CREDS_ENDPOINT_SERVER) {
            error_setg(errp,
                       "Expecting TLS credentials with a server endpoint");
            goto fail;
        }
    }
    if (qemu_opt_get(opts, "acl")) {
        error_report("The 'acl' option to -vnc is deprecated. "
                     "Please use the 'tls-authz' and 'sasl-authz' "
                     "options instead");
    }
    acl = qemu_opt_get_bool(opts, "acl", false);
    tlsauthz = qemu_opt_get(opts, "tls-authz");
    if (acl && tlsauthz) {
        error_setg(errp, "'acl' option is mutually exclusive with the "
                   "'tls-authz' option");
        goto fail;
    }
    if (tlsauthz && !vd->tlscreds) {
        error_setg(errp, "'tls-authz' provided but TLS is not enabled");
        goto fail;
    }

    saslauthz = qemu_opt_get(opts, "sasl-authz");
    if (acl && saslauthz) {
        error_setg(errp, "'acl' option is mutually exclusive with the "
                   "'sasl-authz' option");
        goto fail;
    }
    if (saslauthz && !sasl) {
        error_setg(errp, "'sasl-authz' provided but SASL auth is not enabled");
        goto fail;
    }

    share = qemu_opt_get(opts, "share");
    if (share) {
        if (strcmp(share, "ignore") == 0) {
            vd->share_policy = VNC_SHARE_POLICY_IGNORE;
        } else if (strcmp(share, "allow-exclusive") == 0) {
            vd->share_policy = VNC_SHARE_POLICY_ALLOW_EXCLUSIVE;
        } else if (strcmp(share, "force-shared") == 0) {
            vd->share_policy = VNC_SHARE_POLICY_FORCE_SHARED;
        } else {
            error_setg(errp, "unknown vnc share= option");
            goto fail;
        }
    } else {
        vd->share_policy = VNC_SHARE_POLICY_ALLOW_EXCLUSIVE;
    }
    vd->connections_limit = qemu_opt_get_number(opts, "connections", 32);

#ifdef CONFIG_VNC_JPEG
    vd->lossy = qemu_opt_get_bool(opts, "lossy", false);
#endif
    vd->non_adaptive = qemu_opt_get_bool(opts, "non-adaptive", false);
    /* adaptive updates are only used with tight encoding and
     * if lossy updates are enabled so we can disable all the
     * calculations otherwise */
    if (!vd->lossy) {
        vd->non_adaptive = true;
    }

    if (tlsauthz) {
        vd->tlsauthzid = g_strdup(tlsauthz);
    } else if (acl) {
        if (strcmp(vd->id, "default") == 0) {
            vd->tlsauthzid = g_strdup("vnc.x509dname");
        } else {
            vd->tlsauthzid = g_strdup_printf("vnc.%s.x509dname", vd->id);
        }
        vd->tlsauthz = QAUTHZ(qauthz_list_new(vd->tlsauthzid,
                                              QAUTHZ_LIST_POLICY_DENY,
                                              &error_abort));
    }
#ifdef CONFIG_VNC_SASL
    if (sasl) {
        if (saslauthz) {
            vd->sasl.authzid = g_strdup(saslauthz);
        } else if (acl) {
            if (strcmp(vd->id, "default") == 0) {
                vd->sasl.authzid = g_strdup("vnc.username");
            } else {
                vd->sasl.authzid = g_strdup_printf("vnc.%s.username", vd->id);
            }
            vd->sasl.authz = QAUTHZ(qauthz_list_new(vd->sasl.authzid,
                                                    QAUTHZ_LIST_POLICY_DENY,
                                                    &error_abort));
        }
    }
#endif

    if (vnc_display_setup_auth(&vd->auth, &vd->subauth,
                               vd->tlscreds, password,
                               sasl, false, errp) < 0) {
        goto fail;
    }
    trace_vnc_auth_init(vd, 0, vd->auth, vd->subauth);

    if (vnc_display_setup_auth(&vd->ws_auth, &vd->ws_subauth,
                               vd->tlscreds, password,
                               sasl, true, errp) < 0) {
        goto fail;
    }
    trace_vnc_auth_init(vd, 1, vd->ws_auth, vd->ws_subauth);

#ifdef CONFIG_VNC_SASL
    if (sasl) {
        int saslErr = sasl_server_init(NULL, "qemu");

        if (saslErr != SASL_OK) {
            error_setg(errp, "Failed to initialize SASL auth: %s",
                       sasl_errstring(saslErr, NULL, NULL));
            goto fail;
        }
    }
#endif
    vd->lock_key_sync = lock_key_sync;
    if (lock_key_sync) {
        vd->led = qemu_add_led_event_handler(kbd_leds, vd);
    }
    vd->ledstate = 0;

    audiodev = qemu_opt_get(opts, "audiodev");
    if (audiodev) {
        vd->audio_state = audio_state_by_name(audiodev);
        if (!vd->audio_state) {
            error_setg(errp, "Audiodev '%s' not found", audiodev);
            goto fail;
        }
    }

    device_id = qemu_opt_get(opts, "display");
    if (device_id) {
        int head = qemu_opt_get_number(opts, "head", 0);
        Error *err = NULL;

        con = qemu_console_lookup_by_device_name(device_id, head, &err);
        if (err) {
            error_propagate(errp, err);
            goto fail;
        }
    } else {
        con = NULL;
    }

    if (con != vd->dcl.con) {
        qkbd_state_free(vd->kbd);
        unregister_displaychangelistener(&vd->dcl);
        vd->dcl.con = con;
        register_displaychangelistener(&vd->dcl);
        vd->kbd = qkbd_state_init(vd->dcl.con);
    }
    qkbd_state_set_delay(vd->kbd, key_delay_ms);

    if (saddr == NULL) {
        goto cleanup;
    }

    if (reverse) {
        if (vnc_display_connect(vd, saddr, nsaddr, wsaddr, nwsaddr, errp) < 0) {
            goto fail;
        }
    } else {
        if (vnc_display_listen(vd, saddr, nsaddr, wsaddr, nwsaddr, errp) < 0) {
            goto fail;
        }
    }

    if (qemu_opt_get(opts, "to")) {
        vnc_display_print_local_addr(vd);
    }

 cleanup:
    vnc_free_addresses(&saddr, &nsaddr);
    vnc_free_addresses(&wsaddr, &nwsaddr);
    return;

fail:
    vnc_display_close(vd);
    goto cleanup;
}

void vnc_display_add_client(const char *id, int csock, bool skipauth)
{
    VncDisplay *vd = vnc_display_find(id);
    QIOChannelSocket *sioc;

    if (!vd) {
        return;
    }

    sioc = qio_channel_socket_new_fd(csock, NULL);
    if (sioc) {
        qio_channel_set_name(QIO_CHANNEL(sioc), "vnc-server");
        vnc_connect(vd, sioc, skipauth, false);
        object_unref(OBJECT(sioc));
    }
}

static void vnc_auto_assign_id(QemuOptsList *olist, QemuOpts *opts)
{
    int i = 2;
    char *id;

    id = g_strdup("default");
    while (qemu_opts_find(olist, id)) {
        g_free(id);
        id = g_strdup_printf("vnc%d", i++);
    }
    qemu_opts_set_id(opts, id);
}

QemuOpts *vnc_parse(const char *str, Error **errp)
{
    QemuOptsList *olist = qemu_find_opts("vnc");
    QemuOpts *opts = qemu_opts_parse(olist, str, true, errp);
    const char *id;

    if (!opts) {
        return NULL;
    }

    id = qemu_opts_id(opts);
    if (!id) {
        /* auto-assign id if not present */
        vnc_auto_assign_id(olist, opts);
    }
    return opts;
}

int vnc_init_func(void *opaque, QemuOpts *opts, Error **errp)
{
    Error *local_err = NULL;
    char *id = (char *)qemu_opts_id(opts);

    assert(id);
    vnc_display_init(id, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return -1;
    }
    vnc_display_open(id, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return -1;
    }
    return 0;
}

static void vnc_register_config(void)
{
    qemu_add_opts(&qemu_vnc_opts);
}
opts_init(vnc_register_config);
