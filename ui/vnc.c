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

#include "vnc.h"
#include "vnc-jobs.h"
#include "trace.h"
#include "sysemu/sysemu.h"
#include "qemu/sockets.h"
#include "qemu/timer.h"
#include "qemu/acl.h"
#include "qapi/qmp/types.h"
#include "qmp-commands.h"
#include "qemu/osdep.h"
#include "ui/input.h"

#define VNC_REFRESH_INTERVAL_BASE GUI_REFRESH_INTERVAL_DEFAULT
#define VNC_REFRESH_INTERVAL_INC  50
#define VNC_REFRESH_INTERVAL_MAX  GUI_REFRESH_INTERVAL_IDLE
static const struct timeval VNC_REFRESH_STATS = { 0, 500000 };
static const struct timeval VNC_REFRESH_LOSSY = { 2, 0 };

#include "vnc_keysym.h"
#include "d3des.h"

static VncDisplay *vnc_display; /* needed for info vnc */

static int vnc_cursor_define(VncState *vs);
static void vnc_release_modifiers(VncState *vs);

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
    fprintf(stderr, "%s/%d: %s -> %s\n", __func__,
            vs->csock, mn[vs->share_mode], mn[mode]);
#endif

    if (vs->share_mode == VNC_SHARE_MODE_EXCLUSIVE) {
        vs->vd->num_exclusive--;
    }
    vs->share_mode = mode;
    if (vs->share_mode == VNC_SHARE_MODE_EXCLUSIVE) {
        vs->vd->num_exclusive++;
    }
}

static char *addr_to_string(const char *format,
                            struct sockaddr_storage *sa,
                            socklen_t salen) {
    char *addr;
    char host[NI_MAXHOST];
    char serv[NI_MAXSERV];
    int err;
    size_t addrlen;

    if ((err = getnameinfo((struct sockaddr *)sa, salen,
                           host, sizeof(host),
                           serv, sizeof(serv),
                           NI_NUMERICHOST | NI_NUMERICSERV)) != 0) {
        VNC_DEBUG("Cannot resolve address %d: %s\n",
                  err, gai_strerror(err));
        return NULL;
    }

    /* Enough for the existing format + the 2 vars we're
     * substituting in. */
    addrlen = strlen(format) + strlen(host) + strlen(serv);
    addr = g_malloc(addrlen + 1);
    snprintf(addr, addrlen, format, host, serv);
    addr[addrlen] = '\0';

    return addr;
}


char *vnc_socket_local_addr(const char *format, int fd) {
    struct sockaddr_storage sa;
    socklen_t salen;

    salen = sizeof(sa);
    if (getsockname(fd, (struct sockaddr*)&sa, &salen) < 0)
        return NULL;

    return addr_to_string(format, &sa, salen);
}

char *vnc_socket_remote_addr(const char *format, int fd) {
    struct sockaddr_storage sa;
    socklen_t salen;

    salen = sizeof(sa);
    if (getpeername(fd, (struct sockaddr*)&sa, &salen) < 0)
        return NULL;

    return addr_to_string(format, &sa, salen);
}

static int put_addr_qdict(QDict *qdict, struct sockaddr_storage *sa,
                          socklen_t salen)
{
    char host[NI_MAXHOST];
    char serv[NI_MAXSERV];
    int err;

    if ((err = getnameinfo((struct sockaddr *)sa, salen,
                           host, sizeof(host),
                           serv, sizeof(serv),
                           NI_NUMERICHOST | NI_NUMERICSERV)) != 0) {
        VNC_DEBUG("Cannot resolve address %d: %s\n",
                  err, gai_strerror(err));
        return -1;
    }

    qdict_put(qdict, "host", qstring_from_str(host));
    qdict_put(qdict, "service", qstring_from_str(serv));
    qdict_put(qdict, "family",qstring_from_str(inet_strfamily(sa->ss_family)));

    return 0;
}

static int vnc_server_addr_put(QDict *qdict, int fd)
{
    struct sockaddr_storage sa;
    socklen_t salen;

    salen = sizeof(sa);
    if (getsockname(fd, (struct sockaddr*)&sa, &salen) < 0) {
        return -1;
    }

    return put_addr_qdict(qdict, &sa, salen);
}

static int vnc_qdict_remote_addr(QDict *qdict, int fd)
{
    struct sockaddr_storage sa;
    socklen_t salen;

    salen = sizeof(sa);
    if (getpeername(fd, (struct sockaddr*)&sa, &salen) < 0) {
        return -1;
    }

    return put_addr_qdict(qdict, &sa, salen);
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
#ifdef CONFIG_VNC_TLS
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
#else
        return "vencrypt";
#endif
    case VNC_AUTH_SASL:
        return "sasl";
    }
    return "unknown";
}

static int vnc_server_info_put(QDict *qdict)
{
    if (vnc_server_addr_put(qdict, vnc_display->lsock) < 0) {
        return -1;
    }

    qdict_put(qdict, "auth", qstring_from_str(vnc_auth_name(vnc_display)));
    return 0;
}

static void vnc_client_cache_auth(VncState *client)
{
#if defined(CONFIG_VNC_TLS) || defined(CONFIG_VNC_SASL)
    QDict *qdict;
#endif

    if (!client->info) {
        return;
    }

#if defined(CONFIG_VNC_TLS) || defined(CONFIG_VNC_SASL)
    qdict = qobject_to_qdict(client->info);
#endif

#ifdef CONFIG_VNC_TLS
    if (client->tls.session &&
        client->tls.dname) {
        qdict_put(qdict, "x509_dname", qstring_from_str(client->tls.dname));
    }
#endif
#ifdef CONFIG_VNC_SASL
    if (client->sasl.conn &&
        client->sasl.username) {
        qdict_put(qdict, "sasl_username",
                  qstring_from_str(client->sasl.username));
    }
#endif
}

static void vnc_client_cache_addr(VncState *client)
{
    QDict *qdict;

    qdict = qdict_new();
    if (vnc_qdict_remote_addr(qdict, client->csock) < 0) {
        QDECREF(qdict);
        /* XXX: how to report the error? */
        return;
    }

    client->info = QOBJECT(qdict);
}

static void vnc_qmp_event(VncState *vs, MonitorEvent event)
{
    QDict *server;
    QObject *data;

    if (!vs->info) {
        return;
    }

    server = qdict_new();
    if (vnc_server_info_put(server) < 0) {
        QDECREF(server);
        return;
    }

    data = qobject_from_jsonf("{ 'client': %p, 'server': %p }",
                              vs->info, QOBJECT(server));

    monitor_protocol_event(event, data);

    qobject_incref(vs->info);
    qobject_decref(data);
}

static VncClientInfo *qmp_query_vnc_client(const VncState *client)
{
    struct sockaddr_storage sa;
    socklen_t salen = sizeof(sa);
    char host[NI_MAXHOST];
    char serv[NI_MAXSERV];
    VncClientInfo *info;

    if (getpeername(client->csock, (struct sockaddr *)&sa, &salen) < 0) {
        return NULL;
    }

    if (getnameinfo((struct sockaddr *)&sa, salen,
                    host, sizeof(host),
                    serv, sizeof(serv),
                    NI_NUMERICHOST | NI_NUMERICSERV) < 0) {
        return NULL;
    }

    info = g_malloc0(sizeof(*info));
    info->base = g_malloc0(sizeof(*info->base));
    info->base->host = g_strdup(host);
    info->base->service = g_strdup(serv);
    info->base->family = inet_netfamily(sa.ss_family);

#ifdef CONFIG_VNC_TLS
    if (client->tls.session && client->tls.dname) {
        info->has_x509_dname = true;
        info->x509_dname = g_strdup(client->tls.dname);
    }
#endif
#ifdef CONFIG_VNC_SASL
    if (client->sasl.conn && client->sasl.username) {
        info->has_sasl_username = true;
        info->sasl_username = g_strdup(client->sasl.username);
    }
#endif

    return info;
}

VncInfo *qmp_query_vnc(Error **errp)
{
    VncInfo *info = g_malloc0(sizeof(*info));

    if (vnc_display == NULL || vnc_display->display == NULL) {
        info->enabled = false;
    } else {
        VncClientInfoList *cur_item = NULL;
        struct sockaddr_storage sa;
        socklen_t salen = sizeof(sa);
        char host[NI_MAXHOST];
        char serv[NI_MAXSERV];
        VncState *client;

        info->enabled = true;

        /* for compatibility with the original command */
        info->has_clients = true;

        QTAILQ_FOREACH(client, &vnc_display->clients, next) {
            VncClientInfoList *cinfo = g_malloc0(sizeof(*info));
            cinfo->value = qmp_query_vnc_client(client);

            /* XXX: waiting for the qapi to support GSList */
            if (!cur_item) {
                info->clients = cur_item = cinfo;
            } else {
                cur_item->next = cinfo;
                cur_item = cinfo;
            }
        }

        if (vnc_display->lsock == -1) {
            return info;
        }

        if (getsockname(vnc_display->lsock, (struct sockaddr *)&sa,
                        &salen) == -1) {
            error_set(errp, QERR_UNDEFINED_ERROR);
            goto out_error;
        }

        if (getnameinfo((struct sockaddr *)&sa, salen,
                        host, sizeof(host),
                        serv, sizeof(serv),
                        NI_NUMERICHOST | NI_NUMERICSERV) < 0) {
            error_set(errp, QERR_UNDEFINED_ERROR);
            goto out_error;
        }

        info->has_host = true;
        info->host = g_strdup(host);

        info->has_service = true;
        info->service = g_strdup(serv);

        info->has_family = true;
        info->family = inet_netfamily(sa.ss_family);

        info->has_auth = true;
        info->auth = g_strdup(vnc_auth_name(vnc_display));
    }

    return info;

out_error:
    qapi_free_VncInfo(info);
    return NULL;
}

/* TODO
   1) Get the queue working for IO.
   2) there is some weirdness when using the -S option (the screen is grey
      and not totally invalidated
   3) resolutions > 1024
*/

static int vnc_update_client(VncState *vs, int has_dirty, bool sync);
static void vnc_disconnect_start(VncState *vs);

static void vnc_colordepth(VncState *vs);
static void framebuffer_update_request(VncState *vs, int incremental,
                                       int x_position, int y_position,
                                       int w, int h);
static void vnc_refresh(DisplayChangeListener *dcl);
static int vnc_refresh_server_surface(VncDisplay *vd);

static void vnc_dpy_update(DisplayChangeListener *dcl,
                           int x, int y, int w, int h)
{
    VncDisplay *vd = container_of(dcl, VncDisplay, dcl);
    struct VncSurface *s = &vd->guest;
    int width = surface_width(vd->ds);
    int height = surface_height(vd->ds);

    /* this is needed this to ensure we updated all affected
     * blocks if x % VNC_DIRTY_PIXELS_PER_BIT != 0 */
    w += (x % VNC_DIRTY_PIXELS_PER_BIT);
    x -= (x % VNC_DIRTY_PIXELS_PER_BIT);

    x = MIN(x, width);
    y = MIN(y, height);
    w = MIN(x + w, width) - x;
    h = MIN(y + h, height);

    for (; y < h; y++) {
        bitmap_set(s->dirty[y], x / VNC_DIRTY_PIXELS_PER_BIT,
                   DIV_ROUND_UP(w, VNC_DIRTY_PIXELS_PER_BIT));
    }
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

void buffer_reserve(Buffer *buffer, size_t len)
{
    if ((buffer->capacity - buffer->offset) < len) {
        buffer->capacity += (len + 1024);
        buffer->buffer = g_realloc(buffer->buffer, buffer->capacity);
        if (buffer->buffer == NULL) {
            fprintf(stderr, "vnc: out of memory\n");
            exit(1);
        }
    }
}

static int buffer_empty(Buffer *buffer)
{
    return buffer->offset == 0;
}

uint8_t *buffer_end(Buffer *buffer)
{
    return buffer->buffer + buffer->offset;
}

void buffer_reset(Buffer *buffer)
{
        buffer->offset = 0;
}

void buffer_free(Buffer *buffer)
{
    g_free(buffer->buffer);
    buffer->offset = 0;
    buffer->capacity = 0;
    buffer->buffer = NULL;
}

void buffer_append(Buffer *buffer, const void *data, size_t len)
{
    memcpy(buffer->buffer + buffer->offset, data, len);
    buffer->offset += len;
}

void buffer_advance(Buffer *buf, size_t len)
{
    memmove(buf->buffer, buf->buffer + len,
            (buf->offset - len));
    buf->offset -= len;
}

static void vnc_desktop_resize(VncState *vs)
{
    DisplaySurface *ds = vs->vd->ds;

    if (vs->csock == -1 || !vnc_has_feature(vs, VNC_FEATURE_RESIZE)) {
        return;
    }
    if (vs->client_width == surface_width(ds) &&
        vs->client_height == surface_height(ds)) {
        return;
    }
    vs->client_width = surface_width(ds);
    vs->client_height = surface_height(ds);
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
/* this sets only the visible pixels of a dirty bitmap */
#define VNC_SET_VISIBLE_PIXELS_DIRTY(bitmap, w, h) {\
        int y;\
        memset(bitmap, 0x00, sizeof(bitmap));\
        for (y = 0; y < h; y++) {\
            bitmap_set(bitmap[y], 0,\
                       DIV_ROUND_UP(w, VNC_DIRTY_PIXELS_PER_BIT));\
        } \
    }

static void vnc_dpy_switch(DisplayChangeListener *dcl,
                           DisplaySurface *surface)
{
    VncDisplay *vd = container_of(dcl, VncDisplay, dcl);
    VncState *vs;

    vnc_abort_display_jobs(vd);

    /* server surface */
    qemu_pixman_image_unref(vd->server);
    vd->ds = surface;
    vd->server = pixman_image_create_bits(VNC_SERVER_FB_FORMAT,
                                          surface_width(vd->ds),
                                          surface_height(vd->ds),
                                          NULL, 0);

    /* guest surface */
#if 0 /* FIXME */
    if (ds_get_bytes_per_pixel(ds) != vd->guest.ds->pf.bytes_per_pixel)
        console_color_init(ds);
#endif
    qemu_pixman_image_unref(vd->guest.fb);
    vd->guest.fb = pixman_image_ref(surface->image);
    vd->guest.format = surface->format;
    VNC_SET_VISIBLE_PIXELS_DIRTY(vd->guest.dirty,
                                 surface_width(vd->ds),
                                 surface_height(vd->ds));

    QTAILQ_FOREACH(vs, &vd->clients, next) {
        vnc_colordepth(vs);
        vnc_desktop_resize(vs);
        if (vs->vd->cursor) {
            vnc_cursor_define(vs);
        }
        VNC_SET_VISIBLE_PIXELS_DIRTY(vs->dirty,
                                     surface_width(vd->ds),
                                     surface_height(vd->ds));
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

static void vnc_copy(VncState *vs, int src_x, int src_y, int dst_x, int dst_y, int w, int h)
{
    /* send bitblit op to the vnc client */
    vnc_lock_output(vs);
    vnc_write_u8(vs, VNC_MSG_SERVER_FRAMEBUFFER_UPDATE);
    vnc_write_u8(vs, 0);
    vnc_write_u16(vs, 1); /* number of rects */
    vnc_framebuffer_update(vs, dst_x, dst_y, w, h, VNC_ENCODING_COPYRECT);
    vnc_write_u16(vs, src_x);
    vnc_write_u16(vs, src_y);
    vnc_unlock_output(vs);
    vnc_flush(vs);
}

static void vnc_dpy_copy(DisplayChangeListener *dcl,
                         int src_x, int src_y,
                         int dst_x, int dst_y, int w, int h)
{
    VncDisplay *vd = container_of(dcl, VncDisplay, dcl);
    VncState *vs, *vn;
    uint8_t *src_row;
    uint8_t *dst_row;
    int i, x, y, pitch, inc, w_lim, s;
    int cmp_bytes;

    vnc_refresh_server_surface(vd);
    QTAILQ_FOREACH_SAFE(vs, &vd->clients, next, vn) {
        if (vnc_has_feature(vs, VNC_FEATURE_COPYRECT)) {
            vs->force_update = 1;
            vnc_update_client(vs, 1, true);
            /* vs might be free()ed here */
        }
    }

    /* do bitblit op on the local surface too */
    pitch = vnc_server_fb_stride(vd);
    src_row = vnc_server_fb_ptr(vd, src_x, src_y);
    dst_row = vnc_server_fb_ptr(vd, dst_x, dst_y);
    y = dst_y;
    inc = 1;
    if (dst_y > src_y) {
        /* copy backwards */
        src_row += pitch * (h-1);
        dst_row += pitch * (h-1);
        pitch = -pitch;
        y = dst_y + h - 1;
        inc = -1;
    }
    w_lim = w - (VNC_DIRTY_PIXELS_PER_BIT - (dst_x % VNC_DIRTY_PIXELS_PER_BIT));
    if (w_lim < 0) {
        w_lim = w;
    } else {
        w_lim = w - (w_lim % VNC_DIRTY_PIXELS_PER_BIT);
    }
    for (i = 0; i < h; i++) {
        for (x = 0; x <= w_lim;
                x += s, src_row += cmp_bytes, dst_row += cmp_bytes) {
            if (x == w_lim) {
                if ((s = w - w_lim) == 0)
                    break;
            } else if (!x) {
                s = (VNC_DIRTY_PIXELS_PER_BIT -
                    (dst_x % VNC_DIRTY_PIXELS_PER_BIT));
                s = MIN(s, w_lim);
            } else {
                s = VNC_DIRTY_PIXELS_PER_BIT;
            }
            cmp_bytes = s * VNC_SERVER_FB_BYTES;
            if (memcmp(src_row, dst_row, cmp_bytes) == 0)
                continue;
            memmove(dst_row, src_row, cmp_bytes);
            QTAILQ_FOREACH(vs, &vd->clients, next) {
                if (!vnc_has_feature(vs, VNC_FEATURE_COPYRECT)) {
                    set_bit(((x + dst_x) / VNC_DIRTY_PIXELS_PER_BIT),
                            vs->dirty[y]);
                }
            }
        }
        src_row += pitch - w * VNC_SERVER_FB_BYTES;
        dst_row += pitch - w * VNC_SERVER_FB_BYTES;
        y += inc;
    }

    QTAILQ_FOREACH(vs, &vd->clients, next) {
        if (vnc_has_feature(vs, VNC_FEATURE_COPYRECT)) {
            vnc_copy(vs, src_x, src_y, dst_x, dst_y, w, h);
        }
    }
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
    VncDisplay *vd = vnc_display;
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

static int find_and_clear_dirty_height(struct VncState *vs,
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

static int vnc_update_client(VncState *vs, int has_dirty, bool sync)
{
    if (vs->need_update && vs->csock != -1) {
        VncDisplay *vd = vs->vd;
        VncJob *job;
        int y;
        int height, width;
        int n = 0;

        if (vs->output.offset && !vs->audio_cap && !vs->force_update)
            /* kernel send buffers are full -> drop frames to throttle */
            return 0;

        if (!has_dirty && !vs->audio_cap && !vs->force_update)
            return 0;

        /*
         * Send screen updates to the vnc client using the server
         * surface and server dirty map.  guest surface updates
         * happening in parallel don't disturb us, the next pass will
         * send them to the client.
         */
        job = vnc_job_new(vs);

        height = MIN(pixman_image_get_height(vd->server), vs->client_height);
        width = MIN(pixman_image_get_width(vd->server), vs->client_width);

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
        }

        vnc_job_push(job);
        if (sync) {
            vnc_jobs_join(vs);
        }
        vs->force_update = 0;
        return n;
    }

    if (vs->csock == -1) {
        vnc_disconnect_finish(vs);
    } else if (sync) {
        vnc_jobs_join(vs);
    }

    return 0;
}

/* audio */
static void audio_capture_notify(void *opaque, audcnotification_e cmd)
{
    VncState *vs = opaque;

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

static void audio_capture(void *opaque, void *buf, int size)
{
    VncState *vs = opaque;

    vnc_lock_output(vs);
    vnc_write_u8(vs, VNC_MSG_SERVER_QEMU);
    vnc_write_u8(vs, VNC_MSG_SERVER_QEMU_AUDIO);
    vnc_write_u16(vs, VNC_MSG_SERVER_QEMU_AUDIO_DATA);
    vnc_write_u32(vs, size);
    vnc_write(vs, buf, size);
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

    vs->audio_cap = AUD_add_capture(&vs->as, &ops, vs);
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
    if (vs->csock == -1)
        return;
    vnc_set_share_mode(vs, VNC_SHARE_MODE_DISCONNECTED);
    qemu_set_fd_handler2(vs->csock, NULL, NULL, NULL, NULL);
    closesocket(vs->csock);
    vs->csock = -1;
}

void vnc_disconnect_finish(VncState *vs)
{
    int i;

    vnc_jobs_join(vs); /* Wait encoding jobs */

    vnc_lock_output(vs);
    vnc_qmp_event(vs, QEVENT_VNC_DISCONNECTED);

    buffer_free(&vs->input);
    buffer_free(&vs->output);
#ifdef CONFIG_VNC_WS
    buffer_free(&vs->ws_input);
    buffer_free(&vs->ws_output);
#endif /* CONFIG_VNC_WS */

    qobject_decref(vs->info);

    vnc_zlib_clear(vs);
    vnc_tight_clear(vs);
    vnc_zrle_clear(vs);

#ifdef CONFIG_VNC_TLS
    vnc_tls_client_cleanup(vs);
#endif /* CONFIG_VNC_TLS */
#ifdef CONFIG_VNC_SASL
    vnc_sasl_client_cleanup(vs);
#endif /* CONFIG_VNC_SASL */
    audio_del(vs);
    vnc_release_modifiers(vs);

    if (vs->initialized) {
        QTAILQ_REMOVE(&vs->vd->clients, vs, next);
        qemu_remove_mouse_mode_change_notifier(&vs->mouse_mode_notifier);
    }

    if (vs->vd->lock_key_sync)
        qemu_remove_led_event_handler(vs->led);
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
    g_free(vs);
}

int vnc_client_io_error(VncState *vs, int ret, int last_errno)
{
    if (ret == 0 || ret == -1) {
        if (ret == -1) {
            switch (last_errno) {
                case EINTR:
                case EAGAIN:
#ifdef _WIN32
                case WSAEWOULDBLOCK:
#endif
                    return 0;
                default:
                    break;
            }
        }

        VNC_DEBUG("Closing down client sock: ret %d, errno %d\n",
                  ret, ret < 0 ? last_errno : 0);
        vnc_disconnect_start(vs);

        return 0;
    }
    return ret;
}


void vnc_client_error(VncState *vs)
{
    VNC_DEBUG("Closing down client sock: protocol error\n");
    vnc_disconnect_start(vs);
}

#ifdef CONFIG_VNC_TLS
static long vnc_client_write_tls(gnutls_session_t *session,
                                 const uint8_t *data,
                                 size_t datalen)
{
    long ret = gnutls_write(*session, data, datalen);
    if (ret < 0) {
        if (ret == GNUTLS_E_AGAIN) {
            errno = EAGAIN;
        } else {
            errno = EIO;
        }
        ret = -1;
    }
    return ret;
}
#endif /* CONFIG_VNC_TLS */

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
 * -1 on error, and disconnects the client socket.
 */
long vnc_client_write_buf(VncState *vs, const uint8_t *data, size_t datalen)
{
    long ret;
#ifdef CONFIG_VNC_TLS
    if (vs->tls.session) {
        ret = vnc_client_write_tls(&vs->tls.session, data, datalen);
    } else {
#ifdef CONFIG_VNC_WS
        if (vs->ws_tls.session) {
            ret = vnc_client_write_tls(&vs->ws_tls.session, data, datalen);
        } else
#endif /* CONFIG_VNC_WS */
#endif /* CONFIG_VNC_TLS */
        {
            ret = send(vs->csock, (const void *)data, datalen, 0);
        }
#ifdef CONFIG_VNC_TLS
    }
#endif /* CONFIG_VNC_TLS */
    VNC_DEBUG("Wrote wire %p %zd -> %ld\n", data, datalen, ret);
    return vnc_client_io_error(vs, ret, socket_error());
}


/*
 * Called to write buffered data to the client socket, when not
 * using any SASL SSF encryption layers. Will write as much data
 * as possible without blocking. If all buffered data is written,
 * will switch the FD poll() handler back to read monitoring.
 *
 * Returns the number of bytes written, which may be less than
 * the buffered output data if the socket would block. Returns
 * -1 on error, and disconnects the client socket.
 */
static long vnc_client_write_plain(VncState *vs)
{
    long ret;

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

    buffer_advance(&vs->output, ret);

    if (vs->output.offset == 0) {
        qemu_set_fd_handler2(vs->csock, NULL, vnc_client_read, NULL, vs);
    }

    return ret;
}


/*
 * First function called whenever there is data to be written to
 * the client socket. Will delegate actual work according to whether
 * SASL SSF layers are enabled (thus requiring encryption calls)
 */
static void vnc_client_write_locked(void *opaque)
{
    VncState *vs = opaque;

#ifdef CONFIG_VNC_SASL
    if (vs->sasl.conn &&
        vs->sasl.runSSF &&
        !vs->sasl.waitWriteSSF) {
        vnc_client_write_sasl(vs);
    } else
#endif /* CONFIG_VNC_SASL */
    {
#ifdef CONFIG_VNC_WS
        if (vs->encode_ws) {
            vnc_client_write_ws(vs);
        } else
#endif /* CONFIG_VNC_WS */
        {
            vnc_client_write_plain(vs);
        }
    }
}

void vnc_client_write(void *opaque)
{
    VncState *vs = opaque;

    vnc_lock_output(vs);
    if (vs->output.offset
#ifdef CONFIG_VNC_WS
            || vs->ws_output.offset
#endif
            ) {
        vnc_client_write_locked(opaque);
    } else if (vs->csock != -1) {
        qemu_set_fd_handler2(vs->csock, NULL, vnc_client_read, NULL, vs);
    }
    vnc_unlock_output(vs);
}

void vnc_read_when(VncState *vs, VncReadEvent *func, size_t expecting)
{
    vs->read_handler = func;
    vs->read_handler_expect = expecting;
}

#ifdef CONFIG_VNC_TLS
static long vnc_client_read_tls(gnutls_session_t *session, uint8_t *data,
                                size_t datalen)
{
    long ret = gnutls_read(*session, data, datalen);
    if (ret < 0) {
        if (ret == GNUTLS_E_AGAIN) {
            errno = EAGAIN;
        } else {
            errno = EIO;
        }
        ret = -1;
    }
    return ret;
}
#endif /* CONFIG_VNC_TLS */

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
 * -1 on error, and disconnects the client socket.
 */
long vnc_client_read_buf(VncState *vs, uint8_t *data, size_t datalen)
{
    long ret;
#ifdef CONFIG_VNC_TLS
    if (vs->tls.session) {
        ret = vnc_client_read_tls(&vs->tls.session, data, datalen);
    } else {
#ifdef CONFIG_VNC_WS
        if (vs->ws_tls.session) {
            ret = vnc_client_read_tls(&vs->ws_tls.session, data, datalen);
        } else
#endif /* CONFIG_VNC_WS */
#endif /* CONFIG_VNC_TLS */
        {
            ret = qemu_recv(vs->csock, data, datalen, 0);
        }
#ifdef CONFIG_VNC_TLS
    }
#endif /* CONFIG_VNC_TLS */
    VNC_DEBUG("Read wire %p %zd -> %ld\n", data, datalen, ret);
    return vnc_client_io_error(vs, ret, socket_error());
}


/*
 * Called to read data from the client socket to the input buffer,
 * when not using any SASL SSF encryption layers. Will read as much
 * data as possible without blocking.
 *
 * Returns the number of bytes read. Returns -1 on error, and
 * disconnects the client socket.
 */
static long vnc_client_read_plain(VncState *vs)
{
    int ret;
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

    vnc_jobs_consume_buffer(vs);
}

/*
 * First function called whenever there is more data to be read from
 * the client socket. Will delegate actual work according to whether
 * SASL SSF layers are enabled (thus requiring decryption calls)
 */
void vnc_client_read(void *opaque)
{
    VncState *vs = opaque;
    long ret;

#ifdef CONFIG_VNC_SASL
    if (vs->sasl.conn && vs->sasl.runSSF)
        ret = vnc_client_read_sasl(vs);
    else
#endif /* CONFIG_VNC_SASL */
#ifdef CONFIG_VNC_WS
        if (vs->encode_ws) {
            ret = vnc_client_read_ws(vs);
            if (ret == -1) {
                vnc_disconnect_start(vs);
                return;
            } else if (ret == -2) {
                vnc_client_error(vs);
                return;
            }
        } else
#endif /* CONFIG_VNC_WS */
        {
        ret = vnc_client_read_plain(vs);
        }
    if (!ret) {
        if (vs->csock == -1)
            vnc_disconnect_finish(vs);
        return;
    }

    while (vs->read_handler && vs->input.offset >= vs->read_handler_expect) {
        size_t len = vs->read_handler_expect;
        int ret;

        ret = vs->read_handler(vs, vs->input.buffer, len);
        if (vs->csock == -1) {
            vnc_disconnect_finish(vs);
            return;
        }

        if (!ret) {
            buffer_advance(&vs->input, len);
        } else {
            vs->read_handler_expect = ret;
        }
    }
}

void vnc_write(VncState *vs, const void *data, size_t len)
{
    buffer_reserve(&vs->output, len);

    if (vs->csock != -1 && buffer_empty(&vs->output)) {
        qemu_set_fd_handler2(vs->csock, NULL, vnc_client_read, vnc_client_write, vs);
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
    if (vs->csock != -1 && (vs->output.offset
#ifdef CONFIG_VNC_WS
                || vs->ws_output.offset
#endif
                )) {
        vnc_client_write_locked(vs);
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
                               surface_width(vs->vd->ds),
                               surface_height(vs->vd->ds),
                               VNC_ENCODING_POINTER_TYPE_CHANGE);
        vnc_unlock_output(vs);
        vnc_flush(vs);
    }
    vs->absolute = absolute;
}

static void pointer_event(VncState *vs, int button_mask, int x, int y)
{
    static uint32_t bmap[INPUT_BUTTON_MAX] = {
        [INPUT_BUTTON_LEFT]       = 0x01,
        [INPUT_BUTTON_MIDDLE]     = 0x02,
        [INPUT_BUTTON_RIGHT]      = 0x04,
        [INPUT_BUTTON_WHEEL_UP]   = 0x08,
        [INPUT_BUTTON_WHEEL_DOWN] = 0x10,
    };
    QemuConsole *con = vs->vd->dcl.con;
    int width = surface_width(vs->vd->ds);
    int height = surface_height(vs->vd->ds);

    if (vs->last_bmask != button_mask) {
        qemu_input_update_buttons(con, bmap, vs->last_bmask, button_mask);
        vs->last_bmask = button_mask;
    }

    if (vs->absolute) {
        qemu_input_queue_abs(con, INPUT_AXIS_X, x, width);
        qemu_input_queue_abs(con, INPUT_AXIS_Y, y, height);
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

static void reset_keys(VncState *vs)
{
    int i;
    for(i = 0; i < 256; i++) {
        if (vs->modifiers_state[i]) {
            qemu_input_event_send_key_number(vs->vd->dcl.con, i, false);
            vs->modifiers_state[i] = 0;
        }
    }
}

static void press_key(VncState *vs, int keysym)
{
    int keycode = keysym2scancode(vs->vd->kbd_layout, keysym) & SCANCODE_KEYMASK;
    qemu_input_event_send_key_number(vs->vd->dcl.con, keycode, true);
    qemu_input_event_send_key_delay(0);
    qemu_input_event_send_key_number(vs->vd->dcl.con, keycode, false);
    qemu_input_event_send_key_delay(0);
}

static int current_led_state(VncState *vs)
{
    int ledstate = 0;

    if (vs->modifiers_state[0x46]) {
        ledstate |= QEMU_SCROLL_LOCK_LED;
    }
    if (vs->modifiers_state[0x45]) {
        ledstate |= QEMU_NUM_LOCK_LED;
    }
    if (vs->modifiers_state[0x3a]) {
        ledstate |= QEMU_CAPS_LOCK_LED;
    }

    return ledstate;
}

static void vnc_led_state_change(VncState *vs)
{
    int ledstate = 0;

    if (!vnc_has_feature(vs, VNC_FEATURE_LED_STATE)) {
        return;
    }

    ledstate = current_led_state(vs);
    vnc_lock_output(vs);
    vnc_write_u8(vs, VNC_MSG_SERVER_FRAMEBUFFER_UPDATE);
    vnc_write_u8(vs, 0);
    vnc_write_u16(vs, 1);
    vnc_framebuffer_update(vs, 0, 0, 1, 1, VNC_ENCODING_LED_STATE);
    vnc_write_u8(vs, ledstate);
    vnc_unlock_output(vs);
    vnc_flush(vs);
}

static void kbd_leds(void *opaque, int ledstate)
{
    VncState *vs = opaque;
    int caps, num, scr;
    bool has_changed = (ledstate != current_led_state(vs));

    trace_vnc_key_guest_leds((ledstate & QEMU_CAPS_LOCK_LED),
                             (ledstate & QEMU_NUM_LOCK_LED),
                             (ledstate & QEMU_SCROLL_LOCK_LED));

    caps = ledstate & QEMU_CAPS_LOCK_LED ? 1 : 0;
    num  = ledstate & QEMU_NUM_LOCK_LED  ? 1 : 0;
    scr  = ledstate & QEMU_SCROLL_LOCK_LED ? 1 : 0;

    if (vs->modifiers_state[0x3a] != caps) {
        vs->modifiers_state[0x3a] = caps;
    }
    if (vs->modifiers_state[0x45] != num) {
        vs->modifiers_state[0x45] = num;
    }
    if (vs->modifiers_state[0x46] != scr) {
        vs->modifiers_state[0x46] = scr;
    }

    /* Sending the current led state message to the client */
    if (has_changed) {
        vnc_led_state_change(vs);
    }
}

static void do_key_event(VncState *vs, int down, int keycode, int sym)
{
    /* QEMU console switch */
    switch(keycode) {
    case 0x2a:                          /* Left Shift */
    case 0x36:                          /* Right Shift */
    case 0x1d:                          /* Left CTRL */
    case 0x9d:                          /* Right CTRL */
    case 0x38:                          /* Left ALT */
    case 0xb8:                          /* Right ALT */
        if (down)
            vs->modifiers_state[keycode] = 1;
        else
            vs->modifiers_state[keycode] = 0;
        break;
    case 0x02 ... 0x0a: /* '1' to '9' keys */
        if (down && vs->modifiers_state[0x1d] && vs->modifiers_state[0x38]) {
            /* Reset the modifiers sent to the current console */
            reset_keys(vs);
            console_select(keycode - 0x02);
            return;
        }
        break;
    case 0x3a:                        /* CapsLock */
    case 0x45:                        /* NumLock */
        if (down)
            vs->modifiers_state[keycode] ^= 1;
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
            if (!vs->modifiers_state[0x45]) {
                trace_vnc_key_sync_numlock(true);
                vs->modifiers_state[0x45] = 1;
                press_key(vs, 0xff7f);
            }
        } else {
            if (vs->modifiers_state[0x45]) {
                trace_vnc_key_sync_numlock(false);
                vs->modifiers_state[0x45] = 0;
                press_key(vs, 0xff7f);
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
        int shift = !!(vs->modifiers_state[0x2a] | vs->modifiers_state[0x36]);
        int capslock = !!(vs->modifiers_state[0x3a]);
        if (capslock) {
            if (uppercase == shift) {
                trace_vnc_key_sync_capslock(false);
                vs->modifiers_state[0x3a] = 0;
                press_key(vs, 0xffe5);
            }
        } else {
            if (uppercase != shift) {
                trace_vnc_key_sync_capslock(true);
                vs->modifiers_state[0x3a] = 1;
                press_key(vs, 0xffe5);
            }
        }
    }

    if (qemu_console_is_graphic(NULL)) {
        qemu_input_event_send_key_number(vs->vd->dcl.con, keycode, down);
    } else {
        bool numlock = vs->modifiers_state[0x45];
        bool control = (vs->modifiers_state[0x1d] ||
                        vs->modifiers_state[0x9d]);
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

static void vnc_release_modifiers(VncState *vs)
{
    static const int keycodes[] = {
        /* shift, control, alt keys, both left & right */
        0x2a, 0x36, 0x1d, 0x9d, 0x38, 0xb8,
    };
    int i, keycode;

    if (!qemu_console_is_graphic(NULL)) {
        return;
    }
    for (i = 0; i < ARRAY_SIZE(keycodes); i++) {
        keycode = keycodes[i];
        if (!vs->modifiers_state[keycode]) {
            continue;
        }
        qemu_input_event_send_key_number(vs->vd->dcl.con, keycode, false);
    }
}

static const char *code2name(int keycode)
{
    return QKeyCode_lookup[qemu_input_key_number_to_qcode(keycode)];
}

static void key_event(VncState *vs, int down, uint32_t sym)
{
    int keycode;
    int lsym = sym;

    if (lsym >= 'A' && lsym <= 'Z' && qemu_console_is_graphic(NULL)) {
        lsym = lsym - 'A' + 'a';
    }

    keycode = keysym2scancode(vs->vd->kbd_layout, lsym & 0xFFFF) & SCANCODE_KEYMASK;
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
                                       int x_position, int y_position,
                                       int w, int h)
{
    int i;
    const size_t width = surface_width(vs->vd->ds) / VNC_DIRTY_PIXELS_PER_BIT;
    const size_t height = surface_height(vs->vd->ds);

    if (y_position > height) {
        y_position = height;
    }
    if (y_position + h >= height) {
        h = height - y_position;
    }

    vs->need_update = 1;
    if (!incremental) {
        vs->force_update = 1;
        for (i = 0; i < h; i++) {
            bitmap_set(vs->dirty[y_position + i], 0, width);
            bitmap_clear(vs->dirty[y_position + i], width,
                         VNC_DIRTY_BITS - width);
        }
    }
}

static void send_ext_key_event_ack(VncState *vs)
{
    vnc_lock_output(vs);
    vnc_write_u8(vs, VNC_MSG_SERVER_FRAMEBUFFER_UPDATE);
    vnc_write_u8(vs, 0);
    vnc_write_u16(vs, 1);
    vnc_framebuffer_update(vs, 0, 0,
                           surface_width(vs->vd->ds),
                           surface_height(vs->vd->ds),
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
                           surface_width(vs->vd->ds),
                           surface_height(vs->vd->ds),
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
    vs->tight.compression = 9;
    vs->tight.quality = -1; /* Lossless by default */
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
            vs->features |= VNC_FEATURE_ZLIB_MASK;
            vs->vnc_encoding = enc;
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
            vs->tight.compression = (enc & 0x0F);
            break;
        case VNC_ENCODING_QUALITYLEVEL0 ... VNC_ENCODING_QUALITYLEVEL0 + 9:
            if (vs->vd->lossy) {
                vs->tight.quality = (enc & 0x0F);
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

static void set_pixel_format(VncState *vs,
                             int bits_per_pixel, int depth,
                             int big_endian_flag, int true_color_flag,
                             int red_max, int green_max, int blue_max,
                             int red_shift, int green_shift, int blue_shift)
{
    if (!true_color_flag) {
        vnc_client_error(vs);
        return;
    }

    vs->client_pf.rmax = red_max;
    vs->client_pf.rbits = hweight_long(red_max);
    vs->client_pf.rshift = red_shift;
    vs->client_pf.rmask = red_max << red_shift;
    vs->client_pf.gmax = green_max;
    vs->client_pf.gbits = hweight_long(green_max);
    vs->client_pf.gshift = green_shift;
    vs->client_pf.gmask = green_max << green_shift;
    vs->client_pf.bmax = blue_max;
    vs->client_pf.bbits = hweight_long(blue_max);
    vs->client_pf.bshift = blue_shift;
    vs->client_pf.bmask = blue_max << blue_shift;
    vs->client_pf.bits_per_pixel = bits_per_pixel;
    vs->client_pf.bytes_per_pixel = bits_per_pixel / 8;
    vs->client_pf.depth = bits_per_pixel == 32 ? 24 : bits_per_pixel;
    vs->client_be = big_endian_flag;

    set_pixel_conversion(vs);

    graphic_hw_invalidate(NULL);
    graphic_hw_update(NULL);
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
                               surface_width(vs->vd->ds),
                               surface_height(vs->vd->ds),
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
    VncDisplay *vd = vs->vd;

    if (data[0] > 3) {
        update_displaychangelistener(&vd->dcl, VNC_REFRESH_INTERVAL_BASE);
    }

    switch (data[0]) {
    case VNC_MSG_CLIENT_SET_PIXEL_FORMAT:
        if (len == 1)
            return 20;

        set_pixel_format(vs, read_u8(data, 4), read_u8(data, 5),
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
        if (len == 1)
            return 8;

        if (len == 8) {
            uint32_t dlen = read_u32(data, 4);
            if (dlen > 0)
                return 8 + dlen;
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
                case 0: vs->as.fmt = AUD_FMT_U8; break;
                case 1: vs->as.fmt = AUD_FMT_S8; break;
                case 2: vs->as.fmt = AUD_FMT_U16; break;
                case 3: vs->as.fmt = AUD_FMT_S16; break;
                case 4: vs->as.fmt = AUD_FMT_U32; break;
                case 5: vs->as.fmt = AUD_FMT_S32; break;
                default:
                    printf("Invalid audio format %d\n", read_u8(data, 4));
                    vnc_client_error(vs);
                    break;
                }
                vs->as.nchannels = read_u8(data, 5);
                if (vs->as.nchannels != 1 && vs->as.nchannels != 2) {
                    printf("Invalid audio channel coount %d\n",
                           read_u8(data, 5));
                    vnc_client_error(vs);
                    break;
                }
                vs->as.freq = read_u32(data, 6);
                break;
            default:
                printf ("Invalid audio message %d\n", read_u8(data, 4));
                vnc_client_error(vs);
                break;
            }
            break;

        default:
            printf("Msg: %d\n", read_u16(data, 0));
            vnc_client_error(vs);
            break;
        }
        break;
    default:
        printf("Msg: %d\n", data[0]);
        vnc_client_error(vs);
        break;
    }

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

    vs->client_width = surface_width(vs->vd->ds);
    vs->client_height = surface_height(vs->vd->ds);
    vnc_write_u16(vs, vs->client_width);
    vnc_write_u16(vs, vs->client_height);

    pixel_format_message(vs);

    if (qemu_name)
        size = snprintf(buf, sizeof(buf), "QEMU (%s)", qemu_name);
    else
        size = snprintf(buf, sizeof(buf), "QEMU");

    vnc_write_u32(vs, size);
    vnc_write(vs, buf, size);
    vnc_flush(vs);

    vnc_client_cache_auth(vs);
    vnc_qmp_event(vs, QEVENT_VNC_INITIALIZED);

    vnc_read_when(vs, protocol_client_msg, 1);

    return 0;
}

void start_client_init(VncState *vs)
{
    vnc_read_when(vs, protocol_client_init, 1);
}

static void make_challenge(VncState *vs)
{
    int i;

    srand(time(NULL)+getpid()+getpid()*987654+rand());

    for (i = 0 ; i < sizeof(vs->challenge) ; i++)
        vs->challenge[i] = (int) (256.0*rand()/(RAND_MAX+1.0));
}

static int protocol_client_auth_vnc(VncState *vs, uint8_t *data, size_t len)
{
    unsigned char response[VNC_AUTH_CHALLENGE_SIZE];
    int i, j, pwlen;
    unsigned char key[8];
    time_t now = time(NULL);

    if (!vs->vd->password) {
        VNC_DEBUG("No password configured on server");
        goto reject;
    }
    if (vs->vd->expires < now) {
        VNC_DEBUG("Password is expired");
        goto reject;
    }

    memcpy(response, vs->challenge, VNC_AUTH_CHALLENGE_SIZE);

    /* Calculate the expected challenge response */
    pwlen = strlen(vs->vd->password);
    for (i=0; i<sizeof(key); i++)
        key[i] = i<pwlen ? vs->vd->password[i] : 0;
    deskey(key, EN0);
    for (j = 0; j < VNC_AUTH_CHALLENGE_SIZE; j += 8)
        des(response+j, response+j);

    /* Compare expected vs actual challenge response */
    if (memcmp(response, data, VNC_AUTH_CHALLENGE_SIZE) != 0) {
        VNC_DEBUG("Client challenge response did not match\n");
        goto reject;
    } else {
        VNC_DEBUG("Accepting VNC challenge response\n");
        vnc_write_u32(vs, 0); /* Accept auth */
        vnc_flush(vs);

        start_client_init(vs);
    }
    return 0;

reject:
    vnc_write_u32(vs, 1); /* Reject auth */
    if (vs->minor >= 8) {
        static const char err[] = "Authentication failed";
        vnc_write_u32(vs, sizeof(err));
        vnc_write(vs, err, sizeof(err));
    }
    vnc_flush(vs);
    vnc_client_error(vs);
    return 0;
}

void start_auth_vnc(VncState *vs)
{
    make_challenge(vs);
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
       VNC_DEBUG("Reject auth %d because it didn't match advertized\n", (int)data[0]);
       vnc_write_u32(vs, 1);
       if (vs->minor >= 8) {
           static const char err[] = "Authentication failed";
           vnc_write_u32(vs, sizeof(err));
           vnc_write(vs, err, sizeof(err));
       }
       vnc_client_error(vs);
    } else { /* Accept requested auth */
       VNC_DEBUG("Client requested auth %d\n", (int)data[0]);
       switch (vs->auth) {
       case VNC_AUTH_NONE:
           VNC_DEBUG("Accept auth none\n");
           if (vs->minor >= 8) {
               vnc_write_u32(vs, 0); /* Accept auth completion */
               vnc_flush(vs);
           }
           start_client_init(vs);
           break;

       case VNC_AUTH_VNC:
           VNC_DEBUG("Start VNC auth\n");
           start_auth_vnc(vs);
           break;

#ifdef CONFIG_VNC_TLS
       case VNC_AUTH_VENCRYPT:
           VNC_DEBUG("Accept VeNCrypt auth\n");
           start_auth_vencrypt(vs);
           break;
#endif /* CONFIG_VNC_TLS */

#ifdef CONFIG_VNC_SASL
       case VNC_AUTH_SASL:
           VNC_DEBUG("Accept SASL auth\n");
           start_auth_sasl(vs);
           break;
#endif /* CONFIG_VNC_SASL */

       default: /* Should not be possible, but just in case */
           VNC_DEBUG("Reject auth %d server code bug\n", vs->auth);
           vnc_write_u8(vs, 1);
           if (vs->minor >= 8) {
               static const char err[] = "Authentication failed";
               vnc_write_u32(vs, sizeof(err));
               vnc_write(vs, err, sizeof(err));
           }
           vnc_client_error(vs);
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
        if (vs->auth == VNC_AUTH_NONE) {
            VNC_DEBUG("Tell client auth none\n");
            vnc_write_u32(vs, vs->auth);
            vnc_flush(vs);
            start_client_init(vs);
       } else if (vs->auth == VNC_AUTH_VNC) {
            VNC_DEBUG("Tell client VNC auth\n");
            vnc_write_u32(vs, vs->auth);
            vnc_flush(vs);
            start_auth_vnc(vs);
       } else {
            VNC_DEBUG("Unsupported auth %d for protocol 3.3\n", vs->auth);
            vnc_write_u32(vs, VNC_AUTH_INVALID);
            vnc_flush(vs);
            vnc_client_error(vs);
       }
    } else {
        VNC_DEBUG("Telling client we support auth %d\n", vs->auth);
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

    y = y / VNC_STAT_RECT * VNC_STAT_RECT;
    x = x / VNC_STAT_RECT * VNC_STAT_RECT;

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
    int width = pixman_image_get_width(vd->guest.fb);
    int height = pixman_image_get_height(vd->guest.fb);
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

    x =  (x / VNC_STAT_RECT) * VNC_STAT_RECT;
    y =  (y / VNC_STAT_RECT) * VNC_STAT_RECT;

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
    int width = pixman_image_get_width(vd->guest.fb);
    int height = pixman_image_get_height(vd->guest.fb);
    int y;
    uint8_t *guest_row0 = NULL, *server_row0;
    int guest_stride = 0, server_stride;
    int cmp_bytes;
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
    cmp_bytes = VNC_DIRTY_PIXELS_PER_BIT * VNC_SERVER_FB_BYTES;
    if (cmp_bytes > vnc_server_fb_stride(vd)) {
        cmp_bytes = vnc_server_fb_stride(vd);
    }
    if (vd->guest.format != VNC_SERVER_FB_FORMAT) {
        int width = pixman_image_get_width(vd->server);
        tmpbuf = qemu_pixman_linebuf_create(VNC_SERVER_FB_FORMAT, width);
    } else {
        guest_row0 = (uint8_t *)pixman_image_get_data(vd->guest.fb);
        guest_stride = pixman_image_get_stride(vd->guest.fb);
    }
    server_row0 = (uint8_t *)pixman_image_get_data(vd->server);
    server_stride = pixman_image_get_stride(vd->server);

    y = 0;
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
            if (!test_and_clear_bit(x, vd->guest.dirty[y])) {
                continue;
            }
            if (memcmp(server_ptr, guest_ptr, cmp_bytes) == 0) {
                continue;
            }
            memcpy(server_ptr, guest_ptr, cmp_bytes);
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

    graphic_hw_update(NULL);

    if (vnc_trylock_display(vd)) {
        update_displaychangelistener(&vd->dcl, VNC_REFRESH_INTERVAL_BASE);
        return;
    }

    has_dirty = vnc_refresh_server_surface(vd);
    vnc_unlock_display(vd);

    QTAILQ_FOREACH_SAFE(vs, &vd->clients, next, vn) {
        rects += vnc_update_client(vs, has_dirty, false);
        /* vs might be free()ed here */
    }

    if (QTAILQ_EMPTY(&vd->clients)) {
        update_displaychangelistener(&vd->dcl, VNC_REFRESH_INTERVAL_MAX);
        return;
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

static void vnc_connect(VncDisplay *vd, int csock,
                        bool skipauth, bool websocket)
{
    VncState *vs = g_malloc0(sizeof(VncState));
    int i;

    vs->csock = csock;

    if (skipauth) {
	vs->auth = VNC_AUTH_NONE;
#ifdef CONFIG_VNC_TLS
	vs->subauth = VNC_AUTH_INVALID;
#endif
    } else {
	vs->auth = vd->auth;
#ifdef CONFIG_VNC_TLS
	vs->subauth = vd->subauth;
#endif
    }

    vs->lossy_rect = g_malloc0(VNC_STAT_ROWS * sizeof (*vs->lossy_rect));
    for (i = 0; i < VNC_STAT_ROWS; ++i) {
        vs->lossy_rect[i] = g_malloc0(VNC_STAT_COLS * sizeof (uint8_t));
    }

    VNC_DEBUG("New client on socket %d\n", csock);
    update_displaychangelistener(&vd->dcl, VNC_REFRESH_INTERVAL_BASE);
    qemu_set_nonblock(vs->csock);
#ifdef CONFIG_VNC_WS
    if (websocket) {
        vs->websocket = 1;
#ifdef CONFIG_VNC_TLS
        if (vd->tls.x509cert) {
            qemu_set_fd_handler2(vs->csock, NULL, vncws_tls_handshake_peek,
                                 NULL, vs);
        } else
#endif /* CONFIG_VNC_TLS */
        {
            qemu_set_fd_handler2(vs->csock, NULL, vncws_handshake_read,
                                 NULL, vs);
        }
    } else
#endif /* CONFIG_VNC_WS */
    {
        qemu_set_fd_handler2(vs->csock, NULL, vnc_client_read, NULL, vs);
    }

    vnc_client_cache_addr(vs);
    vnc_qmp_event(vs, QEVENT_VNC_CONNECTED);
    vnc_set_share_mode(vs, VNC_SHARE_MODE_CONNECTING);

    vs->vd = vd;

#ifdef CONFIG_VNC_WS
    if (!vs->websocket)
#endif
    {
        vnc_init_state(vs);
    }
}

void vnc_init_state(VncState *vs)
{
    vs->initialized = true;
    VncDisplay *vd = vs->vd;

    vs->last_x = -1;
    vs->last_y = -1;

    vs->as.freq = 44100;
    vs->as.nchannels = 2;
    vs->as.fmt = AUD_FMT_S16;
    vs->as.endianness = 0;

    qemu_mutex_init(&vs->output_mutex);
    vs->bh = qemu_bh_new(vnc_jobs_bh, vs);

    QTAILQ_INSERT_HEAD(&vd->clients, vs, next);

    graphic_hw_update(NULL);

    vnc_write(vs, "RFB 003.008\n", 12);
    vnc_flush(vs);
    vnc_read_when(vs, protocol_version, 12);
    reset_keys(vs);
    if (vs->vd->lock_key_sync)
        vs->led = qemu_add_led_event_handler(kbd_leds, vs);

    vs->mouse_mode_notifier.notify = check_pointer_type_change;
    qemu_add_mouse_mode_change_notifier(&vs->mouse_mode_notifier);

    /* vs might be free()ed here */
}

static void vnc_listen_read(void *opaque, bool websocket)
{
    VncDisplay *vs = opaque;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    int csock;

    /* Catch-up */
    graphic_hw_update(NULL);
#ifdef CONFIG_VNC_WS
    if (websocket) {
        csock = qemu_accept(vs->lwebsock, (struct sockaddr *)&addr, &addrlen);
    } else
#endif /* CONFIG_VNC_WS */
    {
        csock = qemu_accept(vs->lsock, (struct sockaddr *)&addr, &addrlen);
    }

    if (csock != -1) {
        vnc_connect(vs, csock, false, websocket);
    }
}

static void vnc_listen_regular_read(void *opaque)
{
    vnc_listen_read(opaque, false);
}

#ifdef CONFIG_VNC_WS
static void vnc_listen_websocket_read(void *opaque)
{
    vnc_listen_read(opaque, true);
}
#endif /* CONFIG_VNC_WS */

static const DisplayChangeListenerOps dcl_ops = {
    .dpy_name          = "vnc",
    .dpy_refresh       = vnc_refresh,
    .dpy_gfx_copy      = vnc_dpy_copy,
    .dpy_gfx_update    = vnc_dpy_update,
    .dpy_gfx_switch    = vnc_dpy_switch,
    .dpy_mouse_set     = vnc_mouse_set,
    .dpy_cursor_define = vnc_dpy_cursor_define,
};

void vnc_display_init(DisplayState *ds)
{
    VncDisplay *vs = g_malloc0(sizeof(*vs));

    vnc_display = vs;

    vs->lsock = -1;
#ifdef CONFIG_VNC_WS
    vs->lwebsock = -1;
#endif

    QTAILQ_INIT(&vs->clients);
    vs->expires = TIME_MAX;

    if (keyboard_layout) {
        trace_vnc_key_map_init(keyboard_layout);
        vs->kbd_layout = init_keyboard_layout(name2keysym, keyboard_layout);
    } else {
        vs->kbd_layout = init_keyboard_layout(name2keysym, "en-us");
    }

    if (!vs->kbd_layout)
        exit(1);

    qemu_mutex_init(&vs->mutex);
    vnc_start_worker_thread();

    vs->dcl.ops = &dcl_ops;
    register_displaychangelistener(&vs->dcl);
}


static void vnc_display_close(DisplayState *ds)
{
    VncDisplay *vs = vnc_display;

    if (!vs)
        return;
    g_free(vs->display);
    vs->display = NULL;
    if (vs->lsock != -1) {
        qemu_set_fd_handler2(vs->lsock, NULL, NULL, NULL, NULL);
        close(vs->lsock);
        vs->lsock = -1;
    }
#ifdef CONFIG_VNC_WS
    g_free(vs->ws_display);
    vs->ws_display = NULL;
    if (vs->lwebsock != -1) {
        qemu_set_fd_handler2(vs->lwebsock, NULL, NULL, NULL, NULL);
        close(vs->lwebsock);
        vs->lwebsock = -1;
    }
#endif /* CONFIG_VNC_WS */
    vs->auth = VNC_AUTH_INVALID;
#ifdef CONFIG_VNC_TLS
    vs->subauth = VNC_AUTH_INVALID;
    vs->tls.x509verify = 0;
#endif
}

int vnc_display_password(DisplayState *ds, const char *password)
{
    VncDisplay *vs = vnc_display;

    if (!vs) {
        return -EINVAL;
    }
    if (vs->auth == VNC_AUTH_NONE) {
        error_printf_unless_qmp("If you want use passwords please enable "
                                "password auth using '-vnc ${dpy},password'.");
        return -EINVAL;
    }

    g_free(vs->password);
    vs->password = g_strdup(password);

    return 0;
}

int vnc_display_pw_expire(DisplayState *ds, time_t expires)
{
    VncDisplay *vs = vnc_display;

    if (!vs) {
        return -EINVAL;
    }

    vs->expires = expires;
    return 0;
}

char *vnc_display_local_addr(DisplayState *ds)
{
    VncDisplay *vs = vnc_display;
    
    return vnc_socket_local_addr("%s:%s", vs->lsock);
}

void vnc_display_open(DisplayState *ds, const char *display, Error **errp)
{
    VncDisplay *vs = vnc_display;
    const char *options;
    int password = 0;
    int reverse = 0;
#ifdef CONFIG_VNC_TLS
    int tls = 0, x509 = 0;
#endif
#ifdef CONFIG_VNC_SASL
    int sasl = 0;
    int saslErr;
#endif
#if defined(CONFIG_VNC_TLS) || defined(CONFIG_VNC_SASL)
    int acl = 0;
#endif
    int lock_key_sync = 1;

    if (!vnc_display) {
        error_setg(errp, "VNC display not active");
        return;
    }
    vnc_display_close(ds);
    if (strcmp(display, "none") == 0)
        return;

    vs->display = g_strdup(display);
    vs->share_policy = VNC_SHARE_POLICY_ALLOW_EXCLUSIVE;

    options = display;
    while ((options = strchr(options, ','))) {
        options++;
        if (strncmp(options, "password", 8) == 0) {
            if (fips_get_state()) {
                error_setg(errp,
                           "VNC password auth disabled due to FIPS mode, "
                           "consider using the VeNCrypt or SASL authentication "
                           "methods as an alternative");
                goto fail;
            }
            password = 1; /* Require password auth */
        } else if (strncmp(options, "reverse", 7) == 0) {
            reverse = 1;
        } else if (strncmp(options, "no-lock-key-sync", 16) == 0) {
            lock_key_sync = 0;
#ifdef CONFIG_VNC_SASL
        } else if (strncmp(options, "sasl", 4) == 0) {
            sasl = 1; /* Require SASL auth */
#endif
#ifdef CONFIG_VNC_WS
        } else if (strncmp(options, "websocket", 9) == 0) {
            char *start, *end;
            vs->websocket = 1;

            /* Check for 'websocket=<port>' */
            start = strchr(options, '=');
            end = strchr(options, ',');
            if (start && (!end || (start < end))) {
                int len = end ? end-(start+1) : strlen(start+1);
                if (len < 6) {
                    /* extract the host specification from display */
                    char  *host = NULL, *port = NULL, *host_end = NULL;
                    port = g_strndup(start + 1, len);

                    /* ipv6 hosts have colons */
                    end = strchr(display, ',');
                    host_end = g_strrstr_len(display, end - display, ":");

                    if (host_end) {
                        host = g_strndup(display, host_end - display + 1);
                    } else {
                        host = g_strndup(":", 1);
                    }
                    vs->ws_display = g_strconcat(host, port, NULL);
                    g_free(host);
                    g_free(port);
                }
            }
#endif /* CONFIG_VNC_WS */
#ifdef CONFIG_VNC_TLS
        } else if (strncmp(options, "tls", 3) == 0) {
            tls = 1; /* Require TLS */
        } else if (strncmp(options, "x509", 4) == 0) {
            char *start, *end;
            x509 = 1; /* Require x509 certificates */
            if (strncmp(options, "x509verify", 10) == 0)
                vs->tls.x509verify = 1; /* ...and verify client certs */

            /* Now check for 'x509=/some/path' postfix
             * and use that to setup x509 certificate/key paths */
            start = strchr(options, '=');
            end = strchr(options, ',');
            if (start && (!end || (start < end))) {
                int len = end ? end-(start+1) : strlen(start+1);
                char *path = g_strndup(start + 1, len);

                VNC_DEBUG("Trying certificate path '%s'\n", path);
                if (vnc_tls_set_x509_creds_dir(vs, path) < 0) {
                    error_setg(errp, "Failed to find x509 certificates/keys in %s", path);
                    g_free(path);
                    goto fail;
                }
                g_free(path);
            } else {
                error_setg(errp, "No certificate path provided");
                goto fail;
            }
#endif
#if defined(CONFIG_VNC_TLS) || defined(CONFIG_VNC_SASL)
        } else if (strncmp(options, "acl", 3) == 0) {
            acl = 1;
#endif
        } else if (strncmp(options, "lossy", 5) == 0) {
#ifdef CONFIG_VNC_JPEG
            vs->lossy = true;
#endif
        } else if (strncmp(options, "non-adaptive", 12) == 0) {
            vs->non_adaptive = true;
        } else if (strncmp(options, "share=", 6) == 0) {
            if (strncmp(options+6, "ignore", 6) == 0) {
                vs->share_policy = VNC_SHARE_POLICY_IGNORE;
            } else if (strncmp(options+6, "allow-exclusive", 15) == 0) {
                vs->share_policy = VNC_SHARE_POLICY_ALLOW_EXCLUSIVE;
            } else if (strncmp(options+6, "force-shared", 12) == 0) {
                vs->share_policy = VNC_SHARE_POLICY_FORCE_SHARED;
            } else {
                error_setg(errp, "unknown vnc share= option");
                goto fail;
            }
        }
    }

    /* adaptive updates are only used with tight encoding and
     * if lossy updates are enabled so we can disable all the
     * calculations otherwise */
    if (!vs->lossy) {
        vs->non_adaptive = true;
    }

#ifdef CONFIG_VNC_TLS
    if (acl && x509 && vs->tls.x509verify) {
        if (!(vs->tls.acl = qemu_acl_init("vnc.x509dname"))) {
            fprintf(stderr, "Failed to create x509 dname ACL\n");
            exit(1);
        }
    }
#endif
#ifdef CONFIG_VNC_SASL
    if (acl && sasl) {
        if (!(vs->sasl.acl = qemu_acl_init("vnc.username"))) {
            fprintf(stderr, "Failed to create username ACL\n");
            exit(1);
        }
    }
#endif

    /*
     * Combinations we support here:
     *
     *  - no-auth                (clear text, no auth)
     *  - password               (clear text, weak auth)
     *  - sasl                   (encrypt, good auth *IF* using Kerberos via GSSAPI)
     *  - tls                    (encrypt, weak anonymous creds, no auth)
     *  - tls + password         (encrypt, weak anonymous creds, weak auth)
     *  - tls + sasl             (encrypt, weak anonymous creds, good auth)
     *  - tls + x509             (encrypt, good x509 creds, no auth)
     *  - tls + x509 + password  (encrypt, good x509 creds, weak auth)
     *  - tls + x509 + sasl      (encrypt, good x509 creds, good auth)
     *
     * NB1. TLS is a stackable auth scheme.
     * NB2. the x509 schemes have option to validate a client cert dname
     */
    if (password) {
#ifdef CONFIG_VNC_TLS
        if (tls) {
            vs->auth = VNC_AUTH_VENCRYPT;
            if (x509) {
                VNC_DEBUG("Initializing VNC server with x509 password auth\n");
                vs->subauth = VNC_AUTH_VENCRYPT_X509VNC;
            } else {
                VNC_DEBUG("Initializing VNC server with TLS password auth\n");
                vs->subauth = VNC_AUTH_VENCRYPT_TLSVNC;
            }
        } else {
#endif /* CONFIG_VNC_TLS */
            VNC_DEBUG("Initializing VNC server with password auth\n");
            vs->auth = VNC_AUTH_VNC;
#ifdef CONFIG_VNC_TLS
            vs->subauth = VNC_AUTH_INVALID;
        }
#endif /* CONFIG_VNC_TLS */
#ifdef CONFIG_VNC_SASL
    } else if (sasl) {
#ifdef CONFIG_VNC_TLS
        if (tls) {
            vs->auth = VNC_AUTH_VENCRYPT;
            if (x509) {
                VNC_DEBUG("Initializing VNC server with x509 SASL auth\n");
                vs->subauth = VNC_AUTH_VENCRYPT_X509SASL;
            } else {
                VNC_DEBUG("Initializing VNC server with TLS SASL auth\n");
                vs->subauth = VNC_AUTH_VENCRYPT_TLSSASL;
            }
        } else {
#endif /* CONFIG_VNC_TLS */
            VNC_DEBUG("Initializing VNC server with SASL auth\n");
            vs->auth = VNC_AUTH_SASL;
#ifdef CONFIG_VNC_TLS
            vs->subauth = VNC_AUTH_INVALID;
        }
#endif /* CONFIG_VNC_TLS */
#endif /* CONFIG_VNC_SASL */
    } else {
#ifdef CONFIG_VNC_TLS
        if (tls) {
            vs->auth = VNC_AUTH_VENCRYPT;
            if (x509) {
                VNC_DEBUG("Initializing VNC server with x509 no auth\n");
                vs->subauth = VNC_AUTH_VENCRYPT_X509NONE;
            } else {
                VNC_DEBUG("Initializing VNC server with TLS no auth\n");
                vs->subauth = VNC_AUTH_VENCRYPT_TLSNONE;
            }
        } else {
#endif
            VNC_DEBUG("Initializing VNC server with no auth\n");
            vs->auth = VNC_AUTH_NONE;
#ifdef CONFIG_VNC_TLS
            vs->subauth = VNC_AUTH_INVALID;
        }
#endif
    }

#ifdef CONFIG_VNC_SASL
    if ((saslErr = sasl_server_init(NULL, "qemu")) != SASL_OK) {
        error_setg(errp, "Failed to initialize SASL auth: %s",
                   sasl_errstring(saslErr, NULL, NULL));
        goto fail;
    }
#endif
    vs->lock_key_sync = lock_key_sync;

    if (reverse) {
        /* connect to viewer */
        int csock;
        vs->lsock = -1;
#ifdef CONFIG_VNC_WS
        vs->lwebsock = -1;
#endif
        if (strncmp(display, "unix:", 5) == 0) {
            csock = unix_connect(display+5, errp);
        } else {
            csock = inet_connect(display, errp);
        }
        if (csock < 0) {
            goto fail;
        }
        vnc_connect(vs, csock, false, false);
    } else {
        /* listen for connects */
        char *dpy;
        dpy = g_malloc(256);
        if (strncmp(display, "unix:", 5) == 0) {
            pstrcpy(dpy, 256, "unix:");
            vs->lsock = unix_listen(display+5, dpy+5, 256-5, errp);
        } else {
            vs->lsock = inet_listen(display, dpy, 256,
                                    SOCK_STREAM, 5900, errp);
            if (vs->lsock < 0) {
                g_free(dpy);
                goto fail;
            }
#ifdef CONFIG_VNC_WS
            if (vs->websocket) {
                if (vs->ws_display) {
                    vs->lwebsock = inet_listen(vs->ws_display, NULL, 256,
                        SOCK_STREAM, 0, errp);
                } else {
                    vs->lwebsock = inet_listen(vs->display, NULL, 256,
                        SOCK_STREAM, 5700, errp);
                }

                if (vs->lwebsock < 0) {
                    if (vs->lsock) {
                        close(vs->lsock);
                        vs->lsock = -1;
                    }
                    g_free(dpy);
                    goto fail;
                }
            }
#endif /* CONFIG_VNC_WS */
        }
        g_free(vs->display);
        vs->display = dpy;
        qemu_set_fd_handler2(vs->lsock, NULL,
                vnc_listen_regular_read, NULL, vs);
#ifdef CONFIG_VNC_WS
        if (vs->websocket) {
            qemu_set_fd_handler2(vs->lwebsock, NULL,
                    vnc_listen_websocket_read, NULL, vs);
        }
#endif /* CONFIG_VNC_WS */
    }
    return;

fail:
    g_free(vs->display);
    vs->display = NULL;
#ifdef CONFIG_VNC_WS
    g_free(vs->ws_display);
    vs->ws_display = NULL;
#endif /* CONFIG_VNC_WS */
}

void vnc_display_add_client(DisplayState *ds, int csock, bool skipauth)
{
    VncDisplay *vs = vnc_display;

    vnc_connect(vs, csock, skipauth, false);
}
