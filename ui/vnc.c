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
#include "sysemu.h"
#include "qemu_socket.h"
#include "qemu-timer.h"
#include "acl.h"
#include "qemu-objects.h"
#include "qmp-commands.h"

#define VNC_REFRESH_INTERVAL_BASE 30
#define VNC_REFRESH_INTERVAL_INC  50
#define VNC_REFRESH_INTERVAL_MAX  2000
static const struct timeval VNC_REFRESH_STATS = { 0, 500000 };
static const struct timeval VNC_REFRESH_LOSSY = { 2, 0 };

#include "vnc_keysym.h"
#include "d3des.h"

static VncDisplay *vnc_display; /* needed for info vnc */
static DisplayChangeListener *dcl;

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
    info->host = g_strdup(host);
    info->service = g_strdup(serv);
    info->family = g_strdup(inet_strfamily(sa.ss_family));

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
        info->family = g_strdup(inet_strfamily(sa.ss_family));

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

static int vnc_update_client(VncState *vs, int has_dirty);
static int vnc_update_client_sync(VncState *vs, int has_dirty);
static void vnc_disconnect_start(VncState *vs);
static void vnc_disconnect_finish(VncState *vs);
static void vnc_init_timer(VncDisplay *vd);
static void vnc_remove_timer(VncDisplay *vd);

static void vnc_colordepth(VncState *vs);
static void framebuffer_update_request(VncState *vs, int incremental,
                                       int x_position, int y_position,
                                       int w, int h);
static void vnc_refresh(void *opaque);
static int vnc_refresh_server_surface(VncDisplay *vd);

static void vnc_dpy_update(DisplayState *ds, int x, int y, int w, int h)
{
    int i;
    VncDisplay *vd = ds->opaque;
    struct VncSurface *s = &vd->guest;

    h += y;

    /* round x down to ensure the loop only spans one 16-pixel block per,
       iteration.  otherwise, if (x % 16) != 0, the last iteration may span
       two 16-pixel blocks but we only mark the first as dirty
    */
    w += (x % 16);
    x -= (x % 16);

    x = MIN(x, s->ds->width);
    y = MIN(y, s->ds->height);
    w = MIN(x + w, s->ds->width) - x;
    h = MIN(h, s->ds->height);

    for (; y < h; y++)
        for (i = 0; i < w; i += 16)
            set_bit((x + i) / 16, s->dirty[y]);
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

int buffer_empty(Buffer *buffer)
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

static void vnc_desktop_resize(VncState *vs)
{
    DisplayState *ds = vs->ds;

    if (vs->csock == -1 || !vnc_has_feature(vs, VNC_FEATURE_RESIZE)) {
        return;
    }
    if (vs->client_width == ds_get_width(ds) &&
        vs->client_height == ds_get_height(ds)) {
        return;
    }
    vs->client_width = ds_get_width(ds);
    vs->client_height = ds_get_height(ds);
    vnc_lock_output(vs);
    vnc_write_u8(vs, VNC_MSG_SERVER_FRAMEBUFFER_UPDATE);
    vnc_write_u8(vs, 0);
    vnc_write_u16(vs, 1); /* number of rects */
    vnc_framebuffer_update(vs, 0, 0, vs->client_width, vs->client_height,
                           VNC_ENCODING_DESKTOPRESIZE);
    vnc_unlock_output(vs);
    vnc_flush(vs);
}

#ifdef CONFIG_VNC_THREAD
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
#else
static void vnc_abort_display_jobs(VncDisplay *vd)
{
}
#endif

static void vnc_dpy_resize(DisplayState *ds)
{
    VncDisplay *vd = ds->opaque;
    VncState *vs;

    vnc_abort_display_jobs(vd);

    /* server surface */
    if (!vd->server)
        vd->server = g_malloc0(sizeof(*vd->server));
    if (vd->server->data)
        g_free(vd->server->data);
    *(vd->server) = *(ds->surface);
    vd->server->data = g_malloc0(vd->server->linesize *
                                    vd->server->height);

    /* guest surface */
    if (!vd->guest.ds)
        vd->guest.ds = g_malloc0(sizeof(*vd->guest.ds));
    if (ds_get_bytes_per_pixel(ds) != vd->guest.ds->pf.bytes_per_pixel)
        console_color_init(ds);
    *(vd->guest.ds) = *(ds->surface);
    memset(vd->guest.dirty, 0xFF, sizeof(vd->guest.dirty));

    QTAILQ_FOREACH(vs, &vd->clients, next) {
        vnc_colordepth(vs);
        vnc_desktop_resize(vs);
        if (vs->vd->cursor) {
            vnc_cursor_define(vs);
        }
        memset(vs->dirty, 0xFF, sizeof(vs->dirty));
    }
}

/* fastest code */
static void vnc_write_pixels_copy(VncState *vs, struct PixelFormat *pf,
                                  void *pixels, int size)
{
    vnc_write(vs, pixels, size);
}

/* slowest but generic code. */
void vnc_convert_pixel(VncState *vs, uint8_t *buf, uint32_t v)
{
    uint8_t r, g, b;
    VncDisplay *vd = vs->vd;

    r = ((((v & vd->server->pf.rmask) >> vd->server->pf.rshift) << vs->clientds.pf.rbits) >>
        vd->server->pf.rbits);
    g = ((((v & vd->server->pf.gmask) >> vd->server->pf.gshift) << vs->clientds.pf.gbits) >>
        vd->server->pf.gbits);
    b = ((((v & vd->server->pf.bmask) >> vd->server->pf.bshift) << vs->clientds.pf.bbits) >>
        vd->server->pf.bbits);
    v = (r << vs->clientds.pf.rshift) |
        (g << vs->clientds.pf.gshift) |
        (b << vs->clientds.pf.bshift);
    switch(vs->clientds.pf.bytes_per_pixel) {
    case 1:
        buf[0] = v;
        break;
    case 2:
        if (vs->clientds.flags & QEMU_BIG_ENDIAN_FLAG) {
            buf[0] = v >> 8;
            buf[1] = v;
        } else {
            buf[1] = v >> 8;
            buf[0] = v;
        }
        break;
    default:
    case 4:
        if (vs->clientds.flags & QEMU_BIG_ENDIAN_FLAG) {
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

static void vnc_write_pixels_generic(VncState *vs, struct PixelFormat *pf,
                                     void *pixels1, int size)
{
    uint8_t buf[4];

    if (pf->bytes_per_pixel == 4) {
        uint32_t *pixels = pixels1;
        int n, i;
        n = size >> 2;
        for(i = 0; i < n; i++) {
            vnc_convert_pixel(vs, buf, pixels[i]);
            vnc_write(vs, buf, vs->clientds.pf.bytes_per_pixel);
        }
    } else if (pf->bytes_per_pixel == 2) {
        uint16_t *pixels = pixels1;
        int n, i;
        n = size >> 1;
        for(i = 0; i < n; i++) {
            vnc_convert_pixel(vs, buf, pixels[i]);
            vnc_write(vs, buf, vs->clientds.pf.bytes_per_pixel);
        }
    } else if (pf->bytes_per_pixel == 1) {
        uint8_t *pixels = pixels1;
        int n, i;
        n = size;
        for(i = 0; i < n; i++) {
            vnc_convert_pixel(vs, buf, pixels[i]);
            vnc_write(vs, buf, vs->clientds.pf.bytes_per_pixel);
        }
    } else {
        fprintf(stderr, "vnc_write_pixels_generic: VncState color depth not supported\n");
    }
}

int vnc_raw_send_framebuffer_update(VncState *vs, int x, int y, int w, int h)
{
    int i;
    uint8_t *row;
    VncDisplay *vd = vs->vd;

    row = vd->server->data + y * ds_get_linesize(vs->ds) + x * ds_get_bytes_per_pixel(vs->ds);
    for (i = 0; i < h; i++) {
        vs->write_pixels(vs, &vd->server->pf, row, w * ds_get_bytes_per_pixel(vs->ds));
        row += ds_get_linesize(vs->ds);
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

static void vnc_dpy_copy(DisplayState *ds, int src_x, int src_y, int dst_x, int dst_y, int w, int h)
{
    VncDisplay *vd = ds->opaque;
    VncState *vs, *vn;
    uint8_t *src_row;
    uint8_t *dst_row;
    int i,x,y,pitch,depth,inc,w_lim,s;
    int cmp_bytes;

    vnc_refresh_server_surface(vd);
    QTAILQ_FOREACH_SAFE(vs, &vd->clients, next, vn) {
        if (vnc_has_feature(vs, VNC_FEATURE_COPYRECT)) {
            vs->force_update = 1;
            vnc_update_client_sync(vs, 1);
            /* vs might be free()ed here */
        }
    }

    /* do bitblit op on the local surface too */
    pitch = ds_get_linesize(vd->ds);
    depth = ds_get_bytes_per_pixel(vd->ds);
    src_row = vd->server->data + pitch * src_y + depth * src_x;
    dst_row = vd->server->data + pitch * dst_y + depth * dst_x;
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
    w_lim = w - (16 - (dst_x % 16));
    if (w_lim < 0)
        w_lim = w;
    else
        w_lim = w - (w_lim % 16);
    for (i = 0; i < h; i++) {
        for (x = 0; x <= w_lim;
                x += s, src_row += cmp_bytes, dst_row += cmp_bytes) {
            if (x == w_lim) {
                if ((s = w - w_lim) == 0)
                    break;
            } else if (!x) {
                s = (16 - (dst_x % 16));
                s = MIN(s, w_lim);
            } else {
                s = 16;
            }
            cmp_bytes = s * depth;
            if (memcmp(src_row, dst_row, cmp_bytes) == 0)
                continue;
            memmove(dst_row, src_row, cmp_bytes);
            QTAILQ_FOREACH(vs, &vd->clients, next) {
                if (!vnc_has_feature(vs, VNC_FEATURE_COPYRECT)) {
                    set_bit(((x + dst_x) / 16), vs->dirty[y]);
                }
            }
        }
        src_row += pitch - w * depth;
        dst_row += pitch - w * depth;
        y += inc;
    }

    QTAILQ_FOREACH(vs, &vd->clients, next) {
        if (vnc_has_feature(vs, VNC_FEATURE_COPYRECT)) {
            vnc_copy(vs, src_x, src_y, dst_x, dst_y, w, h);
        }
    }
}

static void vnc_mouse_set(int x, int y, int visible)
{
    /* can we ask the client(s) to move the pointer ??? */
}

static int vnc_cursor_define(VncState *vs)
{
    QEMUCursor *c = vs->vd->cursor;
    PixelFormat pf = qemu_default_pixelformat(32);
    int isize;

    if (vnc_has_feature(vs, VNC_FEATURE_RICH_CURSOR)) {
        vnc_lock_output(vs);
        vnc_write_u8(vs,  VNC_MSG_SERVER_FRAMEBUFFER_UPDATE);
        vnc_write_u8(vs,  0);  /*  padding     */
        vnc_write_u16(vs, 1);  /*  # of rects  */
        vnc_framebuffer_update(vs, c->hot_x, c->hot_y, c->width, c->height,
                               VNC_ENCODING_RICH_CURSOR);
        isize = c->width * c->height * vs->clientds.pf.bytes_per_pixel;
        vnc_write_pixels_generic(vs, &pf, c->data, isize);
        vnc_write(vs, vs->vd->cursor_mask, vs->vd->cursor_msize);
        vnc_unlock_output(vs);
        return 0;
    }
    return -1;
}

static void vnc_dpy_cursor_define(QEMUCursor *c)
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
        int tmp_x;
        if (!test_bit(last_x, vs->dirty[y + h])) {
            break;
        }
        for (tmp_x = last_x; tmp_x < x; tmp_x++) {
            clear_bit(tmp_x, vs->dirty[y + h]);
        }
    }

    return h;
}

#ifdef CONFIG_VNC_THREAD
static int vnc_update_client_sync(VncState *vs, int has_dirty)
{
    int ret = vnc_update_client(vs, has_dirty);
    vnc_jobs_join(vs);
    return ret;
}
#else
static int vnc_update_client_sync(VncState *vs, int has_dirty)
{
    return vnc_update_client(vs, has_dirty);
}
#endif

static int vnc_update_client(VncState *vs, int has_dirty)
{
    if (vs->need_update && vs->csock != -1) {
        VncDisplay *vd = vs->vd;
        VncJob *job;
        int y;
        int width, height;
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

        width = MIN(vd->server->width, vs->client_width);
        height = MIN(vd->server->height, vs->client_height);

        for (y = 0; y < height; y++) {
            int x;
            int last_x = -1;
            for (x = 0; x < width / 16; x++) {
                if (test_and_clear_bit(x, vs->dirty[y])) {
                    if (last_x == -1) {
                        last_x = x;
                    }
                } else {
                    if (last_x != -1) {
                        int h = find_and_clear_dirty_height(vs, y, last_x, x,
                                                            height);

                        n += vnc_job_add_rect(job, last_x * 16, y,
                                              (x - last_x) * 16, h);
                    }
                    last_x = -1;
                }
            }
            if (last_x != -1) {
                int h = find_and_clear_dirty_height(vs, y, last_x, x, height);
                n += vnc_job_add_rect(job, last_x * 16, y,
                                      (x - last_x) * 16, h);
            }
        }

        vnc_job_push(job);
        vs->force_update = 0;
        return n;
    }

    if (vs->csock == -1)
        vnc_disconnect_finish(vs);

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
        monitor_printf(default_mon, "audio already running\n");
        return;
    }

    ops.notify = audio_capture_notify;
    ops.destroy = audio_capture_destroy;
    ops.capture = audio_capture;

    vs->audio_cap = AUD_add_capture(&vs->as, &ops, vs);
    if (!vs->audio_cap) {
        monitor_printf(default_mon, "Failed to add audio capture\n");
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

static void vnc_disconnect_finish(VncState *vs)
{
    int i;

    vnc_jobs_join(vs); /* Wait encoding jobs */

    vnc_lock_output(vs);
    vnc_qmp_event(vs, QEVENT_VNC_DISCONNECTED);

    buffer_free(&vs->input);
    buffer_free(&vs->output);

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

    QTAILQ_REMOVE(&vs->vd->clients, vs, next);

    if (QTAILQ_EMPTY(&vs->vd->clients)) {
        dcl->idle = 1;
    }

    qemu_remove_mouse_mode_change_notifier(&vs->mouse_mode_notifier);
    vnc_remove_timer(vs->vd);
    if (vs->vd->lock_key_sync)
        qemu_remove_led_event_handler(vs->led);
    vnc_unlock_output(vs);

#ifdef CONFIG_VNC_THREAD
    qemu_mutex_destroy(&vs->output_mutex);
    qemu_bh_delete(vs->bh);
    buffer_free(&vs->jobs_buffer);
#endif

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
        ret = gnutls_write(vs->tls.session, data, datalen);
        if (ret < 0) {
            if (ret == GNUTLS_E_AGAIN)
                errno = EAGAIN;
            else
                errno = EIO;
            ret = -1;
        }
    } else
#endif /* CONFIG_VNC_TLS */
        ret = send(vs->csock, (const void *)data, datalen, 0);
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

    memmove(vs->output.buffer, vs->output.buffer + ret, (vs->output.offset - ret));
    vs->output.offset -= ret;

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
        vnc_client_write_plain(vs);
}

void vnc_client_write(void *opaque)
{
    VncState *vs = opaque;

    vnc_lock_output(vs);
    if (vs->output.offset) {
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
        ret = gnutls_read(vs->tls.session, data, datalen);
        if (ret < 0) {
            if (ret == GNUTLS_E_AGAIN)
                errno = EAGAIN;
            else
                errno = EIO;
            ret = -1;
        }
    } else
#endif /* CONFIG_VNC_TLS */
        ret = qemu_recv(vs->csock, data, datalen, 0);
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

#ifdef CONFIG_VNC_THREAD
static void vnc_jobs_bh(void *opaque)
{
    VncState *vs = opaque;

    vnc_jobs_consume_buffer(vs);
}
#endif

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
        ret = vnc_client_read_plain(vs);
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
            memmove(vs->input.buffer, vs->input.buffer + len, (vs->input.offset - len));
            vs->input.offset -= len;
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
    if (vs->csock != -1 && vs->output.offset) {
        vnc_client_write_locked(vs);
    }
    vnc_unlock_output(vs);
}

uint8_t read_u8(uint8_t *data, size_t offset)
{
    return data[offset];
}

uint16_t read_u16(uint8_t *data, size_t offset)
{
    return ((data[offset] & 0xFF) << 8) | (data[offset + 1] & 0xFF);
}

int32_t read_s32(uint8_t *data, size_t offset)
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
    int absolute = kbd_mouse_is_absolute();

    if (vnc_has_feature(vs, VNC_FEATURE_POINTER_TYPE_CHANGE) && vs->absolute != absolute) {
        vnc_lock_output(vs);
        vnc_write_u8(vs, VNC_MSG_SERVER_FRAMEBUFFER_UPDATE);
        vnc_write_u8(vs, 0);
        vnc_write_u16(vs, 1);
        vnc_framebuffer_update(vs, absolute, 0,
                               ds_get_width(vs->ds), ds_get_height(vs->ds),
                               VNC_ENCODING_POINTER_TYPE_CHANGE);
        vnc_unlock_output(vs);
        vnc_flush(vs);
    }
    vs->absolute = absolute;
}

static void pointer_event(VncState *vs, int button_mask, int x, int y)
{
    int buttons = 0;
    int dz = 0;

    if (button_mask & 0x01)
        buttons |= MOUSE_EVENT_LBUTTON;
    if (button_mask & 0x02)
        buttons |= MOUSE_EVENT_MBUTTON;
    if (button_mask & 0x04)
        buttons |= MOUSE_EVENT_RBUTTON;
    if (button_mask & 0x08)
        dz = -1;
    if (button_mask & 0x10)
        dz = 1;

    if (vs->absolute) {
        kbd_mouse_event(ds_get_width(vs->ds) > 1 ?
                          x * 0x7FFF / (ds_get_width(vs->ds) - 1) : 0x4000,
                        ds_get_height(vs->ds) > 1 ?
                          y * 0x7FFF / (ds_get_height(vs->ds) - 1) : 0x4000,
                        dz, buttons);
    } else if (vnc_has_feature(vs, VNC_FEATURE_POINTER_TYPE_CHANGE)) {
        x -= 0x7FFF;
        y -= 0x7FFF;

        kbd_mouse_event(x, y, dz, buttons);
    } else {
        if (vs->last_x != -1)
            kbd_mouse_event(x - vs->last_x,
                            y - vs->last_y,
                            dz, buttons);
        vs->last_x = x;
        vs->last_y = y;
    }
}

static void reset_keys(VncState *vs)
{
    int i;
    for(i = 0; i < 256; i++) {
        if (vs->modifiers_state[i]) {
            if (i & SCANCODE_GREY)
                kbd_put_keycode(SCANCODE_EMUL0);
            kbd_put_keycode(i | SCANCODE_UP);
            vs->modifiers_state[i] = 0;
        }
    }
}

static void press_key(VncState *vs, int keysym)
{
    int keycode = keysym2scancode(vs->vd->kbd_layout, keysym) & SCANCODE_KEYMASK;
    if (keycode & SCANCODE_GREY)
        kbd_put_keycode(SCANCODE_EMUL0);
    kbd_put_keycode(keycode & SCANCODE_KEYCODEMASK);
    if (keycode & SCANCODE_GREY)
        kbd_put_keycode(SCANCODE_EMUL0);
    kbd_put_keycode(keycode | SCANCODE_UP);
}

static void kbd_leds(void *opaque, int ledstate)
{
    VncState *vs = opaque;
    int caps, num;

    caps = ledstate & QEMU_CAPS_LOCK_LED ? 1 : 0;
    num  = ledstate & QEMU_NUM_LOCK_LED  ? 1 : 0;

    if (vs->modifiers_state[0x3a] != caps) {
        vs->modifiers_state[0x3a] = caps;
    }
    if (vs->modifiers_state[0x45] != num) {
        vs->modifiers_state[0x45] = num;
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

    if (down && vs->vd->lock_key_sync &&
        keycode_is_keypad(vs->vd->kbd_layout, keycode)) {
        /* If the numlock state needs to change then simulate an additional
           keypress before sending this one.  This will happen if the user
           toggles numlock away from the VNC window.
        */
        if (keysym_is_numlock(vs->vd->kbd_layout, sym & 0xFFFF)) {
            if (!vs->modifiers_state[0x45]) {
                vs->modifiers_state[0x45] = 1;
                press_key(vs, 0xff7f);
            }
        } else {
            if (vs->modifiers_state[0x45]) {
                vs->modifiers_state[0x45] = 0;
                press_key(vs, 0xff7f);
            }
        }
    }

    if (down && vs->vd->lock_key_sync &&
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
                vs->modifiers_state[0x3a] = 0;
                press_key(vs, 0xffe5);
            }
        } else {
            if (uppercase != shift) {
                vs->modifiers_state[0x3a] = 1;
                press_key(vs, 0xffe5);
            }
        }
    }

    if (is_graphic_console()) {
        if (keycode & SCANCODE_GREY)
            kbd_put_keycode(SCANCODE_EMUL0);
        if (down)
            kbd_put_keycode(keycode & SCANCODE_KEYCODEMASK);
        else
            kbd_put_keycode(keycode | SCANCODE_UP);
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

    if (!is_graphic_console()) {
        return;
    }
    for (i = 0; i < ARRAY_SIZE(keycodes); i++) {
        keycode = keycodes[i];
        if (!vs->modifiers_state[keycode]) {
            continue;
        }
        if (keycode & SCANCODE_GREY) {
            kbd_put_keycode(SCANCODE_EMUL0);
        }
        kbd_put_keycode(keycode | SCANCODE_UP);
    }
}

static void key_event(VncState *vs, int down, uint32_t sym)
{
    int keycode;
    int lsym = sym;

    if (lsym >= 'A' && lsym <= 'Z' && is_graphic_console()) {
        lsym = lsym - 'A' + 'a';
    }

    keycode = keysym2scancode(vs->vd->kbd_layout, lsym & 0xFFFF) & SCANCODE_KEYMASK;
    do_key_event(vs, down, keycode, sym);
}

static void ext_key_event(VncState *vs, int down,
                          uint32_t sym, uint16_t keycode)
{
    /* if the user specifies a keyboard layout, always use it */
    if (keyboard_layout)
        key_event(vs, down, sym);
    else
        do_key_event(vs, down, keycode, sym);
}

static void framebuffer_update_request(VncState *vs, int incremental,
                                       int x_position, int y_position,
                                       int w, int h)
{
    int i;
    const size_t width = ds_get_width(vs->ds) / 16;

    if (y_position > ds_get_height(vs->ds))
        y_position = ds_get_height(vs->ds);
    if (y_position + h >= ds_get_height(vs->ds))
        h = ds_get_height(vs->ds) - y_position;

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
    vnc_framebuffer_update(vs, 0, 0, ds_get_width(vs->ds), ds_get_height(vs->ds),
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
    vnc_framebuffer_update(vs, 0, 0, ds_get_width(vs->ds), ds_get_height(vs->ds),
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
        case VNC_ENCODING_TIGHT_PNG:
            vs->features |= VNC_FEATURE_TIGHT_PNG_MASK;
            vs->vnc_encoding = enc;
            break;
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
}

static void set_pixel_conversion(VncState *vs)
{
    if ((vs->clientds.flags & QEMU_BIG_ENDIAN_FLAG) ==
        (vs->ds->surface->flags & QEMU_BIG_ENDIAN_FLAG) && 
        !memcmp(&(vs->clientds.pf), &(vs->ds->surface->pf), sizeof(PixelFormat))) {
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

    vs->clientds = *(vs->vd->guest.ds);
    vs->clientds.pf.rmax = red_max;
    vs->clientds.pf.rbits = hweight_long(red_max);
    vs->clientds.pf.rshift = red_shift;
    vs->clientds.pf.rmask = red_max << red_shift;
    vs->clientds.pf.gmax = green_max;
    vs->clientds.pf.gbits = hweight_long(green_max);
    vs->clientds.pf.gshift = green_shift;
    vs->clientds.pf.gmask = green_max << green_shift;
    vs->clientds.pf.bmax = blue_max;
    vs->clientds.pf.bbits = hweight_long(blue_max);
    vs->clientds.pf.bshift = blue_shift;
    vs->clientds.pf.bmask = blue_max << blue_shift;
    vs->clientds.pf.bits_per_pixel = bits_per_pixel;
    vs->clientds.pf.bytes_per_pixel = bits_per_pixel / 8;
    vs->clientds.pf.depth = bits_per_pixel == 32 ? 24 : bits_per_pixel;
    vs->clientds.flags = big_endian_flag ? QEMU_BIG_ENDIAN_FLAG : 0x00;

    set_pixel_conversion(vs);

    vga_hw_invalidate();
    vga_hw_update();
}

static void pixel_format_message (VncState *vs) {
    char pad[3] = { 0, 0, 0 };

    vnc_write_u8(vs, vs->ds->surface->pf.bits_per_pixel); /* bits-per-pixel */
    vnc_write_u8(vs, vs->ds->surface->pf.depth); /* depth */

#ifdef HOST_WORDS_BIGENDIAN
    vnc_write_u8(vs, 1);             /* big-endian-flag */
#else
    vnc_write_u8(vs, 0);             /* big-endian-flag */
#endif
    vnc_write_u8(vs, 1);             /* true-color-flag */
    vnc_write_u16(vs, vs->ds->surface->pf.rmax);     /* red-max */
    vnc_write_u16(vs, vs->ds->surface->pf.gmax);     /* green-max */
    vnc_write_u16(vs, vs->ds->surface->pf.bmax);     /* blue-max */
    vnc_write_u8(vs, vs->ds->surface->pf.rshift);    /* red-shift */
    vnc_write_u8(vs, vs->ds->surface->pf.gshift);    /* green-shift */
    vnc_write_u8(vs, vs->ds->surface->pf.bshift);    /* blue-shift */

    vnc_hextile_set_pixel_conversion(vs, 0);

    vs->clientds = *(vs->ds->surface);
    vs->clientds.flags &= ~QEMU_ALLOCATED_FLAG;
    vs->write_pixels = vnc_write_pixels_copy;

    vnc_write(vs, pad, 3);           /* padding */
}

static void vnc_dpy_setdata(DisplayState *ds)
{
    VncDisplay *vd = ds->opaque;

    *(vd->guest.ds) = *(ds->surface);
    vnc_dpy_update(ds, 0, 0, ds_get_width(ds), ds_get_height(ds));
}

static void vnc_colordepth(VncState *vs)
{
    if (vnc_has_feature(vs, VNC_FEATURE_WMVI)) {
        /* Sending a WMVi message to notify the client*/
        vnc_lock_output(vs);
        vnc_write_u8(vs, VNC_MSG_SERVER_FRAMEBUFFER_UPDATE);
        vnc_write_u8(vs, 0);
        vnc_write_u16(vs, 1); /* number of rects */
        vnc_framebuffer_update(vs, 0, 0, ds_get_width(vs->ds), 
                               ds_get_height(vs->ds), VNC_ENCODING_WMVi);
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
        vd->timer_interval = VNC_REFRESH_INTERVAL_BASE;
        if (!qemu_timer_expired(vd->timer, qemu_get_clock_ms(rt_clock) + vd->timer_interval))
            qemu_mod_timer(vd->timer, qemu_get_clock_ms(rt_clock) + vd->timer_interval);
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

    vs->client_width = ds_get_width(vs->ds);
    vs->client_height = ds_get_height(vs->ds);
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
            bitmap_set(vs->dirty[y + j], x / 16, VNC_STAT_RECT / 16);
        }
        has_dirty++;
    }

    return has_dirty;
}

static int vnc_update_stats(VncDisplay *vd,  struct timeval * tv)
{
    int x, y;
    struct timeval res;
    int has_dirty = 0;

    for (y = 0; y < vd->guest.ds->height; y += VNC_STAT_RECT) {
        for (x = 0; x < vd->guest.ds->width; x += VNC_STAT_RECT) {
            VncRectStat *rect = vnc_stat_rect(vd, x, y);

            rect->updated = false;
        }
    }

    qemu_timersub(tv, &VNC_REFRESH_STATS, &res);

    if (timercmp(&vd->guest.last_freq_check, &res, >)) {
        return has_dirty;
    }
    vd->guest.last_freq_check = *tv;

    for (y = 0; y < vd->guest.ds->height; y += VNC_STAT_RECT) {
        for (x = 0; x < vd->guest.ds->width; x += VNC_STAT_RECT) {
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
    int y;
    uint8_t *guest_row;
    uint8_t *server_row;
    int cmp_bytes;
    VncState *vs;
    int has_dirty = 0;

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
    cmp_bytes = 16 * ds_get_bytes_per_pixel(vd->ds);
    if (cmp_bytes > vd->ds->surface->linesize) {
        cmp_bytes = vd->ds->surface->linesize;
    }
    guest_row  = vd->guest.ds->data;
    server_row = vd->server->data;
    for (y = 0; y < vd->guest.ds->height; y++) {
        if (!bitmap_empty(vd->guest.dirty[y], VNC_DIRTY_BITS)) {
            int x;
            uint8_t *guest_ptr;
            uint8_t *server_ptr;

            guest_ptr  = guest_row;
            server_ptr = server_row;

            for (x = 0; x + 15 < vd->guest.ds->width;
                    x += 16, guest_ptr += cmp_bytes, server_ptr += cmp_bytes) {
                if (!test_and_clear_bit((x / 16), vd->guest.dirty[y]))
                    continue;
                if (memcmp(server_ptr, guest_ptr, cmp_bytes) == 0)
                    continue;
                memcpy(server_ptr, guest_ptr, cmp_bytes);
                if (!vd->non_adaptive)
                    vnc_rect_updated(vd, x, y, &tv);
                QTAILQ_FOREACH(vs, &vd->clients, next) {
                    set_bit((x / 16), vs->dirty[y]);
                }
                has_dirty++;
            }
        }
        guest_row  += ds_get_linesize(vd->ds);
        server_row += ds_get_linesize(vd->ds);
    }
    return has_dirty;
}

static void vnc_refresh(void *opaque)
{
    VncDisplay *vd = opaque;
    VncState *vs, *vn;
    int has_dirty, rects = 0;

    vga_hw_update();

    if (vnc_trylock_display(vd)) {
        vd->timer_interval = VNC_REFRESH_INTERVAL_BASE;
        qemu_mod_timer(vd->timer, qemu_get_clock_ms(rt_clock) +
                       vd->timer_interval);
        return;
    }

    has_dirty = vnc_refresh_server_surface(vd);
    vnc_unlock_display(vd);

    QTAILQ_FOREACH_SAFE(vs, &vd->clients, next, vn) {
        rects += vnc_update_client(vs, has_dirty);
        /* vs might be free()ed here */
    }

    /* vd->timer could be NULL now if the last client disconnected,
     * in this case don't update the timer */
    if (vd->timer == NULL)
        return;

    if (has_dirty && rects) {
        vd->timer_interval /= 2;
        if (vd->timer_interval < VNC_REFRESH_INTERVAL_BASE)
            vd->timer_interval = VNC_REFRESH_INTERVAL_BASE;
    } else {
        vd->timer_interval += VNC_REFRESH_INTERVAL_INC;
        if (vd->timer_interval > VNC_REFRESH_INTERVAL_MAX)
            vd->timer_interval = VNC_REFRESH_INTERVAL_MAX;
    }
    qemu_mod_timer(vd->timer, qemu_get_clock_ms(rt_clock) + vd->timer_interval);
}

static void vnc_init_timer(VncDisplay *vd)
{
    vd->timer_interval = VNC_REFRESH_INTERVAL_BASE;
    if (vd->timer == NULL && !QTAILQ_EMPTY(&vd->clients)) {
        vd->timer = qemu_new_timer_ms(rt_clock, vnc_refresh, vd);
        vnc_dpy_resize(vd->ds);
        vnc_refresh(vd);
    }
}

static void vnc_remove_timer(VncDisplay *vd)
{
    if (vd->timer != NULL && QTAILQ_EMPTY(&vd->clients)) {
        qemu_del_timer(vd->timer);
        qemu_free_timer(vd->timer);
        vd->timer = NULL;
    }
}

static void vnc_connect(VncDisplay *vd, int csock, int skipauth)
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
    dcl->idle = 0;
    socket_set_nonblock(vs->csock);
    qemu_set_fd_handler2(vs->csock, NULL, vnc_client_read, NULL, vs);

    vnc_client_cache_addr(vs);
    vnc_qmp_event(vs, QEVENT_VNC_CONNECTED);
    vnc_set_share_mode(vs, VNC_SHARE_MODE_CONNECTING);

    vs->vd = vd;
    vs->ds = vd->ds;
    vs->last_x = -1;
    vs->last_y = -1;

    vs->as.freq = 44100;
    vs->as.nchannels = 2;
    vs->as.fmt = AUD_FMT_S16;
    vs->as.endianness = 0;

#ifdef CONFIG_VNC_THREAD
    qemu_mutex_init(&vs->output_mutex);
    vs->bh = qemu_bh_new(vnc_jobs_bh, vs);
#endif

    QTAILQ_INSERT_HEAD(&vd->clients, vs, next);

    vga_hw_update();

    vnc_write(vs, "RFB 003.008\n", 12);
    vnc_flush(vs);
    vnc_read_when(vs, protocol_version, 12);
    reset_keys(vs);
    if (vs->vd->lock_key_sync)
        vs->led = qemu_add_led_event_handler(kbd_leds, vs);

    vs->mouse_mode_notifier.notify = check_pointer_type_change;
    qemu_add_mouse_mode_change_notifier(&vs->mouse_mode_notifier);

    vnc_init_timer(vd);

    /* vs might be free()ed here */
}

static void vnc_listen_read(void *opaque)
{
    VncDisplay *vs = opaque;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);

    /* Catch-up */
    vga_hw_update();

    int csock = qemu_accept(vs->lsock, (struct sockaddr *)&addr, &addrlen);
    if (csock != -1) {
        vnc_connect(vs, csock, 0);
    }
}

void vnc_display_init(DisplayState *ds)
{
    VncDisplay *vs = g_malloc0(sizeof(*vs));

    dcl = g_malloc0(sizeof(DisplayChangeListener));

    ds->opaque = vs;
    dcl->idle = 1;
    vnc_display = vs;

    vs->lsock = -1;

    vs->ds = ds;
    QTAILQ_INIT(&vs->clients);
    vs->expires = TIME_MAX;

    if (keyboard_layout)
        vs->kbd_layout = init_keyboard_layout(name2keysym, keyboard_layout);
    else
        vs->kbd_layout = init_keyboard_layout(name2keysym, "en-us");

    if (!vs->kbd_layout)
        exit(1);

#ifdef CONFIG_VNC_THREAD
    qemu_mutex_init(&vs->mutex);
    vnc_start_worker_thread();
#endif

    dcl->dpy_copy = vnc_dpy_copy;
    dcl->dpy_update = vnc_dpy_update;
    dcl->dpy_resize = vnc_dpy_resize;
    dcl->dpy_setdata = vnc_dpy_setdata;
    register_displaychangelistener(ds, dcl);
    ds->mouse_set = vnc_mouse_set;
    ds->cursor_define = vnc_dpy_cursor_define;
}


void vnc_display_close(DisplayState *ds)
{
    VncDisplay *vs = ds ? (VncDisplay *)ds->opaque : vnc_display;

    if (!vs)
        return;
    if (vs->display) {
        g_free(vs->display);
        vs->display = NULL;
    }
    if (vs->lsock != -1) {
        qemu_set_fd_handler2(vs->lsock, NULL, NULL, NULL, NULL);
        close(vs->lsock);
        vs->lsock = -1;
    }
    vs->auth = VNC_AUTH_INVALID;
#ifdef CONFIG_VNC_TLS
    vs->subauth = VNC_AUTH_INVALID;
    vs->tls.x509verify = 0;
#endif
}

int vnc_display_disable_login(DisplayState *ds)
{
    VncDisplay *vs = ds ? (VncDisplay *)ds->opaque : vnc_display;

    if (!vs) {
        return -1;
    }

    if (vs->password) {
        g_free(vs->password);
    }

    vs->password = NULL;
    if (vs->auth == VNC_AUTH_NONE) {
        vs->auth = VNC_AUTH_VNC;
    }

    return 0;
}

int vnc_display_password(DisplayState *ds, const char *password)
{
    VncDisplay *vs = ds ? (VncDisplay *)ds->opaque : vnc_display;

    if (!vs) {
        return -EINVAL;
    }

    if (!password) {
        /* This is not the intention of this interface but err on the side
           of being safe */
        return vnc_display_disable_login(ds);
    }

    if (vs->password) {
        g_free(vs->password);
        vs->password = NULL;
    }
    vs->password = g_strdup(password);
    if (vs->auth == VNC_AUTH_NONE) {
        vs->auth = VNC_AUTH_VNC;
    }

    return 0;
}

int vnc_display_pw_expire(DisplayState *ds, time_t expires)
{
    VncDisplay *vs = ds ? (VncDisplay *)ds->opaque : vnc_display;

    vs->expires = expires;
    return 0;
}

char *vnc_display_local_addr(DisplayState *ds)
{
    VncDisplay *vs = ds ? (VncDisplay *)ds->opaque : vnc_display;
    
    return vnc_socket_local_addr("%s:%s", vs->lsock);
}

int vnc_display_open(DisplayState *ds, const char *display)
{
    VncDisplay *vs = ds ? (VncDisplay *)ds->opaque : vnc_display;
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

    if (!vnc_display)
        return -1;
    vnc_display_close(ds);
    if (strcmp(display, "none") == 0)
        return 0;

    if (!(vs->display = strdup(display)))
        return -1;
    vs->share_policy = VNC_SHARE_POLICY_ALLOW_EXCLUSIVE;

    options = display;
    while ((options = strchr(options, ','))) {
        options++;
        if (strncmp(options, "password", 8) == 0) {
            password = 1; /* Require password auth */
        } else if (strncmp(options, "reverse", 7) == 0) {
            reverse = 1;
        } else if (strncmp(options, "no-lock-key-sync", 16) == 0) {
            lock_key_sync = 0;
#ifdef CONFIG_VNC_SASL
        } else if (strncmp(options, "sasl", 4) == 0) {
            sasl = 1; /* Require SASL auth */
#endif
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
                    fprintf(stderr, "Failed to find x509 certificates/keys in %s\n", path);
                    g_free(path);
                    g_free(vs->display);
                    vs->display = NULL;
                    return -1;
                }
                g_free(path);
            } else {
                fprintf(stderr, "No certificate path provided\n");
                g_free(vs->display);
                vs->display = NULL;
                return -1;
            }
#endif
#if defined(CONFIG_VNC_TLS) || defined(CONFIG_VNC_SASL)
        } else if (strncmp(options, "acl", 3) == 0) {
            acl = 1;
#endif
        } else if (strncmp(options, "lossy", 5) == 0) {
            vs->lossy = true;
        } else if (strncmp(options, "non-adapative", 13) == 0) {
            vs->non_adaptive = true;
        } else if (strncmp(options, "share=", 6) == 0) {
            if (strncmp(options+6, "ignore", 6) == 0) {
                vs->share_policy = VNC_SHARE_POLICY_IGNORE;
            } else if (strncmp(options+6, "allow-exclusive", 15) == 0) {
                vs->share_policy = VNC_SHARE_POLICY_ALLOW_EXCLUSIVE;
            } else if (strncmp(options+6, "force-shared", 12) == 0) {
                vs->share_policy = VNC_SHARE_POLICY_FORCE_SHARED;
            } else {
                fprintf(stderr, "unknown vnc share= option\n");
                g_free(vs->display);
                vs->display = NULL;
                return -1;
            }
        }
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
        fprintf(stderr, "Failed to initialize SASL auth %s",
                sasl_errstring(saslErr, NULL, NULL));
        g_free(vs->display);
        vs->display = NULL;
        return -1;
    }
#endif
    vs->lock_key_sync = lock_key_sync;

    if (reverse) {
        /* connect to viewer */
        if (strncmp(display, "unix:", 5) == 0)
            vs->lsock = unix_connect(display+5);
        else
            vs->lsock = inet_connect(display, SOCK_STREAM);
        if (-1 == vs->lsock) {
            g_free(vs->display);
            vs->display = NULL;
            return -1;
        } else {
            int csock = vs->lsock;
            vs->lsock = -1;
            vnc_connect(vs, csock, 0);
        }
        return 0;

    } else {
        /* listen for connects */
        char *dpy;
        dpy = g_malloc(256);
        if (strncmp(display, "unix:", 5) == 0) {
            pstrcpy(dpy, 256, "unix:");
            vs->lsock = unix_listen(display+5, dpy+5, 256-5);
        } else {
            vs->lsock = inet_listen(display, dpy, 256, SOCK_STREAM, 5900);
        }
        if (-1 == vs->lsock) {
            g_free(dpy);
            return -1;
        } else {
            g_free(vs->display);
            vs->display = dpy;
        }
    }
    return qemu_set_fd_handler2(vs->lsock, NULL, vnc_listen_read, NULL, vs);
}

void vnc_display_add_client(DisplayState *ds, int csock, int skipauth)
{
    VncDisplay *vs = ds ? (VncDisplay *)ds->opaque : vnc_display;

    return vnc_connect(vs, csock, skipauth);
}
