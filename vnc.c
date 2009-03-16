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
#include "sysemu.h"
#include "qemu_socket.h"
#include "qemu-timer.h"
#include "acl.h"

#define VNC_REFRESH_INTERVAL (1000 / 30)

#include "vnc_keysym.h"
#include "d3des.h"

#define count_bits(c, v) { \
    for (c = 0; v; v >>= 1) \
    { \
        c += v & 1; \
    } \
}


static VncDisplay *vnc_display; /* needed for info vnc */
static DisplayChangeListener *dcl;

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
     * subsituting in. */
    addrlen = strlen(format) + strlen(host) + strlen(serv);
    addr = qemu_malloc(addrlen + 1);
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

static void do_info_vnc_client(Monitor *mon, VncState *client)
{
    char *clientAddr =
        vnc_socket_remote_addr("     address: %s:%s\n",
                               client->csock);
    if (!clientAddr)
        return;

    monitor_printf(mon, "Client:\n");
    monitor_printf(mon, "%s", clientAddr);
    free(clientAddr);

#ifdef CONFIG_VNC_TLS
    if (client->tls.session &&
        client->tls.dname)
        monitor_printf(mon, "  x509 dname: %s\n", client->tls.dname);
    else
        monitor_printf(mon, "  x509 dname: none\n");
#endif
#ifdef CONFIG_VNC_SASL
    if (client->sasl.conn &&
        client->sasl.username)
        monitor_printf(mon, "    username: %s\n", client->sasl.username);
    else
        monitor_printf(mon, "    username: none\n");
#endif
}

void do_info_vnc(Monitor *mon)
{
    if (vnc_display == NULL || vnc_display->display == NULL) {
        monitor_printf(mon, "Server: disabled\n");
    } else {
        char *serverAddr = vnc_socket_local_addr("     address: %s:%s\n",
                                                 vnc_display->lsock);

        if (!serverAddr)
            return;

        monitor_printf(mon, "Server:\n");
        monitor_printf(mon, "%s", serverAddr);
        free(serverAddr);
        monitor_printf(mon, "        auth: %s\n", vnc_auth_name(vnc_display));

        if (vnc_display->clients) {
            VncState *client = vnc_display->clients;
            while (client) {
                do_info_vnc_client(mon, client);
                client = client->next;
            }
        } else {
            monitor_printf(mon, "Client: none\n");
        }
    }
}

static inline uint32_t vnc_has_feature(VncState *vs, int feature) {
    return (vs->features & (1 << feature));
}

/* TODO
   1) Get the queue working for IO.
   2) there is some weirdness when using the -S option (the screen is grey
      and not totally invalidated
   3) resolutions > 1024
*/

static void vnc_update_client(void *opaque);

static void vnc_colordepth(VncState *vs);

static inline void vnc_set_bit(uint32_t *d, int k)
{
    d[k >> 5] |= 1 << (k & 0x1f);
}

static inline void vnc_clear_bit(uint32_t *d, int k)
{
    d[k >> 5] &= ~(1 << (k & 0x1f));
}

static inline void vnc_set_bits(uint32_t *d, int n, int nb_words)
{
    int j;

    j = 0;
    while (n >= 32) {
        d[j++] = -1;
        n -= 32;
    }
    if (n > 0)
        d[j++] = (1 << n) - 1;
    while (j < nb_words)
        d[j++] = 0;
}

static inline int vnc_get_bit(const uint32_t *d, int k)
{
    return (d[k >> 5] >> (k & 0x1f)) & 1;
}

static inline int vnc_and_bits(const uint32_t *d1, const uint32_t *d2,
                               int nb_words)
{
    int i;
    for(i = 0; i < nb_words; i++) {
        if ((d1[i] & d2[i]) != 0)
            return 1;
    }
    return 0;
}

static void vnc_update(VncState *vs, int x, int y, int w, int h)
{
    int i;

    h += y;

    /* round x down to ensure the loop only spans one 16-pixel block per,
       iteration.  otherwise, if (x % 16) != 0, the last iteration may span
       two 16-pixel blocks but we only mark the first as dirty
    */
    w += (x % 16);
    x -= (x % 16);

    x = MIN(x, vs->serverds.width);
    y = MIN(y, vs->serverds.height);
    w = MIN(x + w, vs->serverds.width) - x;
    h = MIN(h, vs->serverds.height);

    for (; y < h; y++)
        for (i = 0; i < w; i += 16)
            vnc_set_bit(vs->dirty_row[y], (x + i) / 16);
}

static void vnc_dpy_update(DisplayState *ds, int x, int y, int w, int h)
{
    VncDisplay *vd = ds->opaque;
    VncState *vs = vd->clients;
    while (vs != NULL) {
        vnc_update(vs, x, y, w, h);
        vs = vs->next;
    }
}

static void vnc_framebuffer_update(VncState *vs, int x, int y, int w, int h,
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
        buffer->buffer = qemu_realloc(buffer->buffer, buffer->capacity);
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

void buffer_append(Buffer *buffer, const void *data, size_t len)
{
    memcpy(buffer->buffer + buffer->offset, data, len);
    buffer->offset += len;
}

static void vnc_resize(VncState *vs)
{
    DisplayState *ds = vs->ds;

    int size_changed;

    vs->old_data = qemu_realloc(vs->old_data, ds_get_linesize(ds) * ds_get_height(ds));

    if (vs->old_data == NULL) {
        fprintf(stderr, "vnc: memory allocation failed\n");
        exit(1);
    }

    if (ds_get_bytes_per_pixel(ds) != vs->serverds.pf.bytes_per_pixel)
        console_color_init(ds);
    vnc_colordepth(vs);
    size_changed = ds_get_width(ds) != vs->serverds.width ||
                   ds_get_height(ds) != vs->serverds.height;
    vs->serverds = *(ds->surface);
    if (size_changed) {
        if (vs->csock != -1 && vnc_has_feature(vs, VNC_FEATURE_RESIZE)) {
            vnc_write_u8(vs, 0);  /* msg id */
            vnc_write_u8(vs, 0);
            vnc_write_u16(vs, 1); /* number of rects */
            vnc_framebuffer_update(vs, 0, 0, ds_get_width(ds), ds_get_height(ds),
                                   VNC_ENCODING_DESKTOPRESIZE);
            vnc_flush(vs);
        }
    }

    memset(vs->dirty_row, 0xFF, sizeof(vs->dirty_row));
    memset(vs->old_data, 42, ds_get_linesize(vs->ds) * ds_get_height(vs->ds));
}

static void vnc_dpy_resize(DisplayState *ds)
{
    VncDisplay *vd = ds->opaque;
    VncState *vs = vd->clients;
    while (vs != NULL) {
        vnc_resize(vs);
        vs = vs->next;
    }
}

/* fastest code */
static void vnc_write_pixels_copy(VncState *vs, void *pixels, int size)
{
    vnc_write(vs, pixels, size);
}

/* slowest but generic code. */
static void vnc_convert_pixel(VncState *vs, uint8_t *buf, uint32_t v)
{
    uint8_t r, g, b;

    r = ((((v & vs->serverds.pf.rmask) >> vs->serverds.pf.rshift) << vs->clientds.pf.rbits) >>
        vs->serverds.pf.rbits);
    g = ((((v & vs->serverds.pf.gmask) >> vs->serverds.pf.gshift) << vs->clientds.pf.gbits) >>
        vs->serverds.pf.gbits);
    b = ((((v & vs->serverds.pf.bmask) >> vs->serverds.pf.bshift) << vs->clientds.pf.bbits) >>
        vs->serverds.pf.bbits);
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

static void vnc_write_pixels_generic(VncState *vs, void *pixels1, int size)
{
    uint8_t buf[4];

    if (vs->serverds.pf.bytes_per_pixel == 4) {
        uint32_t *pixels = pixels1;
        int n, i;
        n = size >> 2;
        for(i = 0; i < n; i++) {
            vnc_convert_pixel(vs, buf, pixels[i]);
            vnc_write(vs, buf, vs->clientds.pf.bytes_per_pixel);
        }
    } else if (vs->serverds.pf.bytes_per_pixel == 2) {
        uint16_t *pixels = pixels1;
        int n, i;
        n = size >> 1;
        for(i = 0; i < n; i++) {
            vnc_convert_pixel(vs, buf, pixels[i]);
            vnc_write(vs, buf, vs->clientds.pf.bytes_per_pixel);
        }
    } else if (vs->serverds.pf.bytes_per_pixel == 1) {
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

static void send_framebuffer_update_raw(VncState *vs, int x, int y, int w, int h)
{
    int i;
    uint8_t *row;

    row = ds_get_data(vs->ds) + y * ds_get_linesize(vs->ds) + x * ds_get_bytes_per_pixel(vs->ds);
    for (i = 0; i < h; i++) {
        vs->write_pixels(vs, row, w * ds_get_bytes_per_pixel(vs->ds));
        row += ds_get_linesize(vs->ds);
    }
}

static void hextile_enc_cord(uint8_t *ptr, int x, int y, int w, int h)
{
    ptr[0] = ((x & 0x0F) << 4) | (y & 0x0F);
    ptr[1] = (((w - 1) & 0x0F) << 4) | ((h - 1) & 0x0F);
}

#define BPP 8
#include "vnchextile.h"
#undef BPP

#define BPP 16
#include "vnchextile.h"
#undef BPP

#define BPP 32
#include "vnchextile.h"
#undef BPP

#define GENERIC
#define BPP 8
#include "vnchextile.h"
#undef BPP
#undef GENERIC

#define GENERIC
#define BPP 16
#include "vnchextile.h"
#undef BPP
#undef GENERIC

#define GENERIC
#define BPP 32
#include "vnchextile.h"
#undef BPP
#undef GENERIC

static void send_framebuffer_update_hextile(VncState *vs, int x, int y, int w, int h)
{
    int i, j;
    int has_fg, has_bg;
    uint8_t *last_fg, *last_bg;

    last_fg = (uint8_t *) qemu_malloc(vs->serverds.pf.bytes_per_pixel);
    last_bg = (uint8_t *) qemu_malloc(vs->serverds.pf.bytes_per_pixel);
    has_fg = has_bg = 0;
    for (j = y; j < (y + h); j += 16) {
        for (i = x; i < (x + w); i += 16) {
            vs->send_hextile_tile(vs, i, j,
                                  MIN(16, x + w - i), MIN(16, y + h - j),
                                  last_bg, last_fg, &has_bg, &has_fg);
        }
    }
    free(last_fg);
    free(last_bg);

}

static void vnc_zlib_init(VncState *vs)
{
    int i;
    for (i=0; i<(sizeof(vs->zlib_stream) / sizeof(z_stream)); i++)
        vs->zlib_stream[i].opaque = NULL;
}

static void vnc_zlib_start(VncState *vs)
{
    buffer_reset(&vs->zlib);

    // make the output buffer be the zlib buffer, so we can compress it later
    vs->zlib_tmp = vs->output;
    vs->output = vs->zlib;
}

static int vnc_zlib_stop(VncState *vs, int stream_id)
{
    z_streamp zstream = &vs->zlib_stream[stream_id];
    int previous_out;

    // switch back to normal output/zlib buffers
    vs->zlib = vs->output;
    vs->output = vs->zlib_tmp;

    // compress the zlib buffer

    // initialize the stream
    // XXX need one stream per session
    if (zstream->opaque != vs) {
        int err;

        VNC_DEBUG("VNC: initializing zlib stream %d\n", stream_id);
        VNC_DEBUG("VNC: opaque = %p | vs = %p\n", zstream->opaque, vs);
        zstream->zalloc = Z_NULL;
        zstream->zfree = Z_NULL;

        err = deflateInit2(zstream, vs->tight_compression, Z_DEFLATED, MAX_WBITS,
                           MAX_MEM_LEVEL, Z_DEFAULT_STRATEGY);

        if (err != Z_OK) {
            fprintf(stderr, "VNC: error initializing zlib\n");
            return -1;
        }

        zstream->opaque = vs;
    }

    // XXX what to do if tight_compression changed in between?

    // reserve memory in output buffer
    buffer_reserve(&vs->output, vs->zlib.offset + 64);

    // set pointers
    zstream->next_in = vs->zlib.buffer;
    zstream->avail_in = vs->zlib.offset;
    zstream->next_out = vs->output.buffer + vs->output.offset;
    zstream->avail_out = vs->output.capacity - vs->output.offset;
    zstream->data_type = Z_BINARY;
    previous_out = zstream->total_out;

    // start encoding
    if (deflate(zstream, Z_SYNC_FLUSH) != Z_OK) {
        fprintf(stderr, "VNC: error during zlib compression\n");
        return -1;
    }

    vs->output.offset = vs->output.capacity - zstream->avail_out;
    return zstream->total_out - previous_out;
}

static void send_framebuffer_update_zlib(VncState *vs, int x, int y, int w, int h)
{
    int old_offset, new_offset, bytes_written;

    vnc_framebuffer_update(vs, x, y, w, h, VNC_ENCODING_ZLIB);

    // remember where we put in the follow-up size
    old_offset = vs->output.offset;
    vnc_write_s32(vs, 0);

    // compress the stream
    vnc_zlib_start(vs);
    send_framebuffer_update_raw(vs, x, y, w, h);
    bytes_written = vnc_zlib_stop(vs, 0);

    if (bytes_written == -1)
        return;

    // hack in the size
    new_offset = vs->output.offset;
    vs->output.offset = old_offset;
    vnc_write_u32(vs, bytes_written);
    vs->output.offset = new_offset;
}

static void send_framebuffer_update(VncState *vs, int x, int y, int w, int h)
{
    switch(vs->vnc_encoding) {
        case VNC_ENCODING_ZLIB:
            send_framebuffer_update_zlib(vs, x, y, w, h);
            break;
        case VNC_ENCODING_HEXTILE:
            vnc_framebuffer_update(vs, x, y, w, h, VNC_ENCODING_HEXTILE);
            send_framebuffer_update_hextile(vs, x, y, w, h);
            break;
        default:
            vnc_framebuffer_update(vs, x, y, w, h, VNC_ENCODING_RAW);
            send_framebuffer_update_raw(vs, x, y, w, h);
            break;
    }
}

static void vnc_copy(VncState *vs, int src_x, int src_y, int dst_x, int dst_y, int w, int h)
{
    vnc_update_client(vs);

    vnc_write_u8(vs, 0);  /* msg id */
    vnc_write_u8(vs, 0);
    vnc_write_u16(vs, 1); /* number of rects */
    vnc_framebuffer_update(vs, dst_x, dst_y, w, h, VNC_ENCODING_COPYRECT);
    vnc_write_u16(vs, src_x);
    vnc_write_u16(vs, src_y);
    vnc_flush(vs);
}

static void vnc_dpy_copy(DisplayState *ds, int src_x, int src_y, int dst_x, int dst_y, int w, int h)
{
    VncDisplay *vd = ds->opaque;
    VncState *vs = vd->clients;
    while (vs != NULL) {
        if (vnc_has_feature(vs, VNC_FEATURE_COPYRECT))
            vnc_copy(vs, src_x, src_y, dst_x, dst_y, w, h);
        else /* TODO */
            vnc_update(vs, dst_x, dst_y, w, h);
        vs = vs->next;
    }
}

static int find_dirty_height(VncState *vs, int y, int last_x, int x)
{
    int h;

    for (h = 1; h < (vs->serverds.height - y); h++) {
        int tmp_x;
        if (!vnc_get_bit(vs->dirty_row[y + h], last_x))
            break;
        for (tmp_x = last_x; tmp_x < x; tmp_x++)
            vnc_clear_bit(vs->dirty_row[y + h], tmp_x);
    }

    return h;
}

static void vnc_update_client(void *opaque)
{
    VncState *vs = opaque;
    if (vs->need_update && vs->csock != -1) {
        int y;
        uint8_t *row;
        char *old_row;
        uint32_t width_mask[VNC_DIRTY_WORDS];
        int n_rectangles;
        int saved_offset;
        int has_dirty = 0;

        vga_hw_update();

        vnc_set_bits(width_mask, (ds_get_width(vs->ds) / 16), VNC_DIRTY_WORDS);

        /* Walk through the dirty map and eliminate tiles that
           really aren't dirty */
        row = ds_get_data(vs->ds);
        old_row = vs->old_data;

        for (y = 0; y < ds_get_height(vs->ds); y++) {
            if (vnc_and_bits(vs->dirty_row[y], width_mask, VNC_DIRTY_WORDS)) {
                int x;
                uint8_t *ptr;
                char *old_ptr;

                ptr = row;
                old_ptr = (char*)old_row;

                for (x = 0; x < ds_get_width(vs->ds); x += 16) {
                    if (memcmp(old_ptr, ptr, 16 * ds_get_bytes_per_pixel(vs->ds)) == 0) {
                        vnc_clear_bit(vs->dirty_row[y], (x / 16));
                    } else {
                        has_dirty = 1;
                        memcpy(old_ptr, ptr, 16 * ds_get_bytes_per_pixel(vs->ds));
                    }

                    ptr += 16 * ds_get_bytes_per_pixel(vs->ds);
                    old_ptr += 16 * ds_get_bytes_per_pixel(vs->ds);
                }
            }

            row += ds_get_linesize(vs->ds);
            old_row += ds_get_linesize(vs->ds);
        }

        if (!has_dirty && !vs->audio_cap) {
            qemu_mod_timer(vs->timer, qemu_get_clock(rt_clock) + VNC_REFRESH_INTERVAL);
            return;
        }

        /* Count rectangles */
        n_rectangles = 0;
        vnc_write_u8(vs, 0);  /* msg id */
        vnc_write_u8(vs, 0);
        saved_offset = vs->output.offset;
        vnc_write_u16(vs, 0);

        for (y = 0; y < vs->serverds.height; y++) {
            int x;
            int last_x = -1;
            for (x = 0; x < vs->serverds.width / 16; x++) {
                if (vnc_get_bit(vs->dirty_row[y], x)) {
                    if (last_x == -1) {
                        last_x = x;
                    }
                    vnc_clear_bit(vs->dirty_row[y], x);
                } else {
                    if (last_x != -1) {
                        int h = find_dirty_height(vs, y, last_x, x);
                        send_framebuffer_update(vs, last_x * 16, y, (x - last_x) * 16, h);
                        n_rectangles++;
                    }
                    last_x = -1;
                }
            }
            if (last_x != -1) {
                int h = find_dirty_height(vs, y, last_x, x);
                send_framebuffer_update(vs, last_x * 16, y, (x - last_x) * 16, h);
                n_rectangles++;
            }
        }
        vs->output.buffer[saved_offset] = (n_rectangles >> 8) & 0xFF;
        vs->output.buffer[saved_offset + 1] = n_rectangles & 0xFF;
        vnc_flush(vs);

    }

    if (vs->csock != -1) {
        qemu_mod_timer(vs->timer, qemu_get_clock(rt_clock) + VNC_REFRESH_INTERVAL);
    }

}

/* audio */
static void audio_capture_notify(void *opaque, audcnotification_e cmd)
{
    VncState *vs = opaque;

    switch (cmd) {
    case AUD_CNOTIFY_DISABLE:
        vnc_write_u8(vs, 255);
        vnc_write_u8(vs, 1);
        vnc_write_u16(vs, 0);
        vnc_flush(vs);
        break;

    case AUD_CNOTIFY_ENABLE:
        vnc_write_u8(vs, 255);
        vnc_write_u8(vs, 1);
        vnc_write_u16(vs, 1);
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

    vnc_write_u8(vs, 255);
    vnc_write_u8(vs, 1);
    vnc_write_u16(vs, 2);
    vnc_write_u32(vs, size);
    vnc_write(vs, buf, size);
    vnc_flush(vs);
}

static void audio_add(VncState *vs)
{
    Monitor *mon = cur_mon;
    struct audio_capture_ops ops;

    if (vs->audio_cap) {
        monitor_printf(mon, "audio already running\n");
        return;
    }

    ops.notify = audio_capture_notify;
    ops.destroy = audio_capture_destroy;
    ops.capture = audio_capture;

    vs->audio_cap = AUD_add_capture(NULL, &vs->as, &ops, vs);
    if (!vs->audio_cap) {
        monitor_printf(mon, "Failed to add audio capture\n");
    }
}

static void audio_del(VncState *vs)
{
    if (vs->audio_cap) {
        AUD_del_capture(vs->audio_cap, vs);
        vs->audio_cap = NULL;
    }
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

        VNC_DEBUG("Closing down client sock %d %d\n", ret, ret < 0 ? last_errno : 0);
        qemu_set_fd_handler2(vs->csock, NULL, NULL, NULL, NULL);
        closesocket(vs->csock);
        qemu_del_timer(vs->timer);
        qemu_free_timer(vs->timer);
        if (vs->input.buffer) qemu_free(vs->input.buffer);
        if (vs->output.buffer) qemu_free(vs->output.buffer);
#ifdef CONFIG_VNC_TLS
        vnc_tls_client_cleanup(vs);
#endif /* CONFIG_VNC_TLS */
#ifdef CONFIG_VNC_SASL
        vnc_sasl_client_cleanup(vs);
#endif /* CONFIG_VNC_SASL */
        audio_del(vs);

        VncState *p, *parent = NULL;
        for (p = vs->vd->clients; p != NULL; p = p->next) {
            if (p == vs) {
                if (parent)
                    parent->next = p->next;
                else
                    vs->vd->clients = p->next;
                break;
            }
            parent = p;
        }
        if (!vs->vd->clients)
            dcl->idle = 1;

        qemu_free(vs->old_data);
        qemu_free(vs);
  
        return 0;
    }
    return ret;
}


void vnc_client_error(VncState *vs)
{
    vnc_client_io_error(vs, -1, EINVAL);
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
        ret = send(vs->csock, data, datalen, 0);
    VNC_DEBUG("Wrote wire %p %d -> %ld\n", data, datalen, ret);
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
    VNC_DEBUG("Write Plain: Pending output %p size %d offset %d. Wait SSF %d\n",
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
void vnc_client_write(void *opaque)
{
    long ret;
    VncState *vs = opaque;

#ifdef CONFIG_VNC_SASL
    if (vs->sasl.conn &&
        vs->sasl.runSSF &&
        !vs->sasl.waitWriteSSF)
        ret = vnc_client_write_sasl(vs);
    else
#endif /* CONFIG_VNC_SASL */
        ret = vnc_client_write_plain(vs);
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
        ret = recv(vs->csock, data, datalen, 0);
    VNC_DEBUG("Read wire %p %d -> %ld\n", data, datalen, ret);
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
    VNC_DEBUG("Read plain %p size %d offset %d\n",
              vs->input.buffer, vs->input.capacity, vs->input.offset);
    buffer_reserve(&vs->input, 4096);
    ret = vnc_client_read_buf(vs, buffer_end(&vs->input), 4096);
    if (!ret)
        return 0;
    vs->input.offset += ret;
    return ret;
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
        ret = vnc_client_read_plain(vs);
    if (!ret)
        return;

    while (vs->read_handler && vs->input.offset >= vs->read_handler_expect) {
        size_t len = vs->read_handler_expect;
        int ret;

        ret = vs->read_handler(vs, vs->input.buffer, len);
        if (vs->csock == -1)
            return;

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

    if (buffer_empty(&vs->output)) {
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
    if (vs->output.offset)
        vnc_client_write(vs);
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

static void check_pointer_type_change(VncState *vs, int absolute)
{
    if (vnc_has_feature(vs, VNC_FEATURE_POINTER_TYPE_CHANGE) && vs->absolute != absolute) {
        vnc_write_u8(vs, 0);
        vnc_write_u8(vs, 0);
        vnc_write_u16(vs, 1);
        vnc_framebuffer_update(vs, absolute, 0,
                               ds_get_width(vs->ds), ds_get_height(vs->ds),
                               VNC_ENCODING_POINTER_TYPE_CHANGE);
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
        kbd_mouse_event(x * 0x7FFF / (ds_get_width(vs->ds) - 1),
                        y * 0x7FFF / (ds_get_height(vs->ds) - 1),
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

    check_pointer_type_change(vs, kbd_mouse_is_absolute());
}

static void reset_keys(VncState *vs)
{
    int i;
    for(i = 0; i < 256; i++) {
        if (vs->modifiers_state[i]) {
            if (i & 0x80)
                kbd_put_keycode(0xe0);
            kbd_put_keycode(i | 0x80);
            vs->modifiers_state[i] = 0;
        }
    }
}

static void press_key(VncState *vs, int keysym)
{
    kbd_put_keycode(keysym2scancode(vs->vd->kbd_layout, keysym) & 0x7f);
    kbd_put_keycode(keysym2scancode(vs->vd->kbd_layout, keysym) | 0x80);
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
        if (!down)
            vs->modifiers_state[keycode] ^= 1;
        break;
    }

    if (keycode_is_keypad(vs->vd->kbd_layout, keycode)) {
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

    if (is_graphic_console()) {
        if (keycode & 0x80)
            kbd_put_keycode(0xe0);
        if (down)
            kbd_put_keycode(keycode & 0x7f);
        else
            kbd_put_keycode(keycode | 0x80);
    } else {
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
            default:
                kbd_put_keysym(sym);
                break;
            }
        }
    }
}

static void key_event(VncState *vs, int down, uint32_t sym)
{
    int keycode;

    if (sym >= 'A' && sym <= 'Z' && is_graphic_console())
        sym = sym - 'A' + 'a';

    keycode = keysym2scancode(vs->vd->kbd_layout, sym & 0xFFFF);
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
    if (x_position > ds_get_width(vs->ds))
        x_position = ds_get_width(vs->ds);
    if (y_position > ds_get_height(vs->ds))
        y_position = ds_get_height(vs->ds);
    if (x_position + w >= ds_get_width(vs->ds))
        w = ds_get_width(vs->ds)  - x_position;
    if (y_position + h >= ds_get_height(vs->ds))
        h = ds_get_height(vs->ds) - y_position;

    int i;
    vs->need_update = 1;
    if (!incremental) {
        char *old_row = vs->old_data + y_position * ds_get_linesize(vs->ds);

        for (i = 0; i < h; i++) {
            vnc_set_bits(vs->dirty_row[y_position + i],
                         (ds_get_width(vs->ds) / 16), VNC_DIRTY_WORDS);
            memset(old_row, 42, ds_get_width(vs->ds) * ds_get_bytes_per_pixel(vs->ds));
            old_row += ds_get_linesize(vs->ds);
        }
    }
}

static void send_ext_key_event_ack(VncState *vs)
{
    vnc_write_u8(vs, 0);
    vnc_write_u8(vs, 0);
    vnc_write_u16(vs, 1);
    vnc_framebuffer_update(vs, 0, 0, ds_get_width(vs->ds), ds_get_height(vs->ds),
                           VNC_ENCODING_EXT_KEY_EVENT);
    vnc_flush(vs);
}

static void send_ext_audio_ack(VncState *vs)
{
    vnc_write_u8(vs, 0);
    vnc_write_u8(vs, 0);
    vnc_write_u16(vs, 1);
    vnc_framebuffer_update(vs, 0, 0, ds_get_width(vs->ds), ds_get_height(vs->ds),
                           VNC_ENCODING_AUDIO);
    vnc_flush(vs);
}

static void set_encodings(VncState *vs, int32_t *encodings, size_t n_encodings)
{
    int i;
    unsigned int enc = 0;

    vnc_zlib_init(vs);
    vs->features = 0;
    vs->vnc_encoding = 0;
    vs->tight_compression = 9;
    vs->tight_quality = 9;
    vs->absolute = -1;

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
        case VNC_ENCODING_ZLIB:
            vs->features |= VNC_FEATURE_ZLIB_MASK;
            vs->vnc_encoding = enc;
            break;
        case VNC_ENCODING_DESKTOPRESIZE:
            vs->features |= VNC_FEATURE_RESIZE_MASK;
            break;
        case VNC_ENCODING_POINTER_TYPE_CHANGE:
            vs->features |= VNC_FEATURE_POINTER_TYPE_CHANGE_MASK;
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
            vs->tight_compression = (enc & 0x0F);
            break;
        case VNC_ENCODING_QUALITYLEVEL0 ... VNC_ENCODING_QUALITYLEVEL0 + 9:
            vs->tight_quality = (enc & 0x0F);
            break;
        default:
            VNC_DEBUG("Unknown encoding: %d (0x%.8x): %d\n", i, enc, enc);
            break;
        }
    }

    check_pointer_type_change(vs, kbd_mouse_is_absolute());
}

static void set_pixel_conversion(VncState *vs)
{
    if ((vs->clientds.flags & QEMU_BIG_ENDIAN_FLAG) ==
        (vs->ds->surface->flags & QEMU_BIG_ENDIAN_FLAG) && 
        !memcmp(&(vs->clientds.pf), &(vs->ds->surface->pf), sizeof(PixelFormat))) {
        vs->write_pixels = vnc_write_pixels_copy;
        switch (vs->ds->surface->pf.bits_per_pixel) {
            case 8:
                vs->send_hextile_tile = send_hextile_tile_8;
                break;
            case 16:
                vs->send_hextile_tile = send_hextile_tile_16;
                break;
            case 32:
                vs->send_hextile_tile = send_hextile_tile_32;
                break;
        }
    } else {
        vs->write_pixels = vnc_write_pixels_generic;
        switch (vs->ds->surface->pf.bits_per_pixel) {
            case 8:
                vs->send_hextile_tile = send_hextile_tile_generic_8;
                break;
            case 16:
                vs->send_hextile_tile = send_hextile_tile_generic_16;
                break;
            case 32:
                vs->send_hextile_tile = send_hextile_tile_generic_32;
                break;
        }
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

    vs->clientds = vs->serverds;
    vs->clientds.pf.rmax = red_max;
    count_bits(vs->clientds.pf.rbits, red_max);
    vs->clientds.pf.rshift = red_shift;
    vs->clientds.pf.rmask = red_max << red_shift;
    vs->clientds.pf.gmax = green_max;
    count_bits(vs->clientds.pf.gbits, green_max);
    vs->clientds.pf.gshift = green_shift;
    vs->clientds.pf.gmask = green_max << green_shift;
    vs->clientds.pf.bmax = blue_max;
    count_bits(vs->clientds.pf.bbits, blue_max);
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

#ifdef WORDS_BIGENDIAN
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
    if (vs->ds->surface->pf.bits_per_pixel == 32)
        vs->send_hextile_tile = send_hextile_tile_32;
    else if (vs->ds->surface->pf.bits_per_pixel == 16)
        vs->send_hextile_tile = send_hextile_tile_16;
    else if (vs->ds->surface->pf.bits_per_pixel == 8)
        vs->send_hextile_tile = send_hextile_tile_8;
    vs->clientds = *(vs->ds->surface);
    vs->clientds.flags |= ~QEMU_ALLOCATED_FLAG;
    vs->write_pixels = vnc_write_pixels_copy;

    vnc_write(vs, pad, 3);           /* padding */
}

static void vnc_dpy_setdata(DisplayState *ds)
{
    /* We don't have to do anything */
}

static void vnc_colordepth(VncState *vs)
{
    if (vnc_has_feature(vs, VNC_FEATURE_WMVI)) {
        /* Sending a WMVi message to notify the client*/
        vnc_write_u8(vs, 0);  /* msg id */
        vnc_write_u8(vs, 0);
        vnc_write_u16(vs, 1); /* number of rects */
        vnc_framebuffer_update(vs, 0, 0, ds_get_width(vs->ds), 
                               ds_get_height(vs->ds), VNC_ENCODING_WMVi);
        pixel_format_message(vs);
        vnc_flush(vs);
    } else {
        set_pixel_conversion(vs);
    }
}

static int protocol_client_msg(VncState *vs, uint8_t *data, size_t len)
{
    int i;
    uint16_t limit;

    switch (data[0]) {
    case 0:
        if (len == 1)
            return 20;

        set_pixel_format(vs, read_u8(data, 4), read_u8(data, 5),
                         read_u8(data, 6), read_u8(data, 7),
                         read_u16(data, 8), read_u16(data, 10),
                         read_u16(data, 12), read_u8(data, 14),
                         read_u8(data, 15), read_u8(data, 16));
        break;
    case 2:
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
    case 3:
        if (len == 1)
            return 10;

        framebuffer_update_request(vs,
                                   read_u8(data, 1), read_u16(data, 2), read_u16(data, 4),
                                   read_u16(data, 6), read_u16(data, 8));
        break;
    case 4:
        if (len == 1)
            return 8;

        key_event(vs, read_u8(data, 1), read_u32(data, 4));
        break;
    case 5:
        if (len == 1)
            return 6;

        pointer_event(vs, read_u8(data, 1), read_u16(data, 2), read_u16(data, 4));
        break;
    case 6:
        if (len == 1)
            return 8;

        if (len == 8) {
            uint32_t dlen = read_u32(data, 4);
            if (dlen > 0)
                return 8 + dlen;
        }

        client_cut_text(vs, read_u32(data, 4), data + 8);
        break;
    case 255:
        if (len == 1)
            return 2;

        switch (read_u8(data, 1)) {
        case 0:
            if (len == 2)
                return 12;

            ext_key_event(vs, read_u16(data, 2),
                          read_u32(data, 4), read_u32(data, 8));
            break;
        case 1:
            if (len == 2)
                return 4;

            switch (read_u16 (data, 2)) {
            case 0:
                audio_add(vs);
                break;
            case 1:
                audio_del(vs);
                break;
            case 2:
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
    int size;

    vnc_write_u16(vs, ds_get_width(vs->ds));
    vnc_write_u16(vs, ds_get_height(vs->ds));

    pixel_format_message(vs);

    if (qemu_name)
        size = snprintf(buf, sizeof(buf), "QEMU (%s)", qemu_name);
    else
        size = snprintf(buf, sizeof(buf), "QEMU");

    vnc_write_u32(vs, size);
    vnc_write(vs, buf, size);
    vnc_flush(vs);

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

    if (!vs->vd->password || !vs->vd->password[0]) {
        VNC_DEBUG("No password configured on server");
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
        VNC_DEBUG("Client challenge reponse did not match\n");
        vnc_write_u32(vs, 1); /* Reject auth */
        if (vs->minor >= 8) {
            static const char err[] = "Authentication failed";
            vnc_write_u32(vs, sizeof(err));
            vnc_write(vs, err, sizeof(err));
        }
        vnc_flush(vs);
        vnc_client_error(vs);
    } else {
        VNC_DEBUG("Accepting VNC challenge response\n");
        vnc_write_u32(vs, 0); /* Accept auth */
        vnc_flush(vs);

        start_client_init(vs);
    }
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
    if (data[0] != vs->vd->auth) { /* Reject auth */
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
       switch (vs->vd->auth) {
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
           VNC_DEBUG("Accept VeNCrypt auth\n");;
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
           VNC_DEBUG("Reject auth %d server code bug\n", vs->vd->auth);
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
        if (vs->vd->auth == VNC_AUTH_NONE) {
            VNC_DEBUG("Tell client auth none\n");
            vnc_write_u32(vs, vs->vd->auth);
            vnc_flush(vs);
            start_client_init(vs);
       } else if (vs->vd->auth == VNC_AUTH_VNC) {
            VNC_DEBUG("Tell client VNC auth\n");
            vnc_write_u32(vs, vs->vd->auth);
            vnc_flush(vs);
            start_auth_vnc(vs);
       } else {
            VNC_DEBUG("Unsupported auth %d for protocol 3.3\n", vs->vd->auth);
            vnc_write_u32(vs, VNC_AUTH_INVALID);
            vnc_flush(vs);
            vnc_client_error(vs);
       }
    } else {
        VNC_DEBUG("Telling client we support auth %d\n", vs->vd->auth);
        vnc_write_u8(vs, 1); /* num auth */
        vnc_write_u8(vs, vs->vd->auth);
        vnc_read_when(vs, protocol_client_auth, 1);
        vnc_flush(vs);
    }

    return 0;
}

static void vnc_connect(VncDisplay *vd, int csock)
{
    VncState *vs = qemu_mallocz(sizeof(VncState));
    vs->csock = csock;

    VNC_DEBUG("New client on socket %d\n", csock);
    dcl->idle = 0;
    socket_set_nonblock(vs->csock);
    qemu_set_fd_handler2(vs->csock, NULL, vnc_client_read, NULL, vs);

    vs->vd = vd;
    vs->ds = vd->ds;
    vs->timer = qemu_new_timer(rt_clock, vnc_update_client, vs);
    vs->last_x = -1;
    vs->last_y = -1;

    vs->as.freq = 44100;
    vs->as.nchannels = 2;
    vs->as.fmt = AUD_FMT_S16;
    vs->as.endianness = 0;

    vnc_resize(vs);
    vnc_write(vs, "RFB 003.008\n", 12);
    vnc_flush(vs);
    vnc_read_when(vs, protocol_version, 12);
    memset(vs->old_data, 0, ds_get_linesize(vs->ds) * ds_get_height(vs->ds));
    memset(vs->dirty_row, 0xFF, sizeof(vs->dirty_row));
    vnc_update_client(vs);
    reset_keys(vs);

    vs->next = vd->clients;
    vd->clients = vs;
}

static void vnc_listen_read(void *opaque)
{
    VncDisplay *vs = opaque;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);

    /* Catch-up */
    vga_hw_update();

    int csock = accept(vs->lsock, (struct sockaddr *)&addr, &addrlen);
    if (csock != -1) {
        vnc_connect(vs, csock);
    }
}

void vnc_display_init(DisplayState *ds)
{
    VncDisplay *vs;

    vs = qemu_mallocz(sizeof(VncState));
    dcl = qemu_mallocz(sizeof(DisplayChangeListener));

    ds->opaque = vs;
    dcl->idle = 1;
    vnc_display = vs;

    vs->lsock = -1;

    vs->ds = ds;

    if (keyboard_layout)
        vs->kbd_layout = init_keyboard_layout(name2keysym, keyboard_layout);
    else
        vs->kbd_layout = init_keyboard_layout(name2keysym, "en-us");

    if (!vs->kbd_layout)
        exit(1);

    dcl->dpy_copy = vnc_dpy_copy;
    dcl->dpy_update = vnc_dpy_update;
    dcl->dpy_resize = vnc_dpy_resize;
    dcl->dpy_setdata = vnc_dpy_setdata;
    register_displaychangelistener(ds, dcl);
}


void vnc_display_close(DisplayState *ds)
{
    VncDisplay *vs = ds ? (VncDisplay *)ds->opaque : vnc_display;

    if (!vs)
        return;
    if (vs->display) {
        qemu_free(vs->display);
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

int vnc_display_password(DisplayState *ds, const char *password)
{
    VncDisplay *vs = ds ? (VncDisplay *)ds->opaque : vnc_display;

    if (vs->password) {
        qemu_free(vs->password);
        vs->password = NULL;
    }
    if (password && password[0]) {
        if (!(vs->password = qemu_strdup(password)))
            return -1;
    }

    return 0;
}

int vnc_display_open(DisplayState *ds, const char *display)
{
    VncDisplay *vs = ds ? (VncDisplay *)ds->opaque : vnc_display;
    const char *options;
    int password = 0;
    int reverse = 0;
    int to_port = 0;
#ifdef CONFIG_VNC_TLS
    int tls = 0, x509 = 0;
#endif
#ifdef CONFIG_VNC_SASL
    int sasl = 0;
    int saslErr;
#endif
    int acl = 0;

    if (!vnc_display)
        return -1;
    vnc_display_close(ds);
    if (strcmp(display, "none") == 0)
        return 0;

    if (!(vs->display = strdup(display)))
        return -1;

    options = display;
    while ((options = strchr(options, ','))) {
        options++;
        if (strncmp(options, "password", 8) == 0) {
            password = 1; /* Require password auth */
        } else if (strncmp(options, "reverse", 7) == 0) {
            reverse = 1;
        } else if (strncmp(options, "to=", 3) == 0) {
            to_port = atoi(options+3) + 5900;
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
                char *path = qemu_strndup(start + 1, len);

                VNC_DEBUG("Trying certificate path '%s'\n", path);
                if (vnc_tls_set_x509_creds_dir(vs, path) < 0) {
                    fprintf(stderr, "Failed to find x509 certificates/keys in %s\n", path);
                    qemu_free(path);
                    qemu_free(vs->display);
                    vs->display = NULL;
                    return -1;
                }
                qemu_free(path);
            } else {
                fprintf(stderr, "No certificate path provided\n");
                qemu_free(vs->display);
                vs->display = NULL;
                return -1;
            }
#endif
        } else if (strncmp(options, "acl", 3) == 0) {
            acl = 1;
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
        free(vs->display);
        vs->display = NULL;
        return -1;
    }
#endif

    if (reverse) {
        /* connect to viewer */
        if (strncmp(display, "unix:", 5) == 0)
            vs->lsock = unix_connect(display+5);
        else
            vs->lsock = inet_connect(display, SOCK_STREAM);
        if (-1 == vs->lsock) {
            free(vs->display);
            vs->display = NULL;
            return -1;
        } else {
            int csock = vs->lsock;
            vs->lsock = -1;
            vnc_connect(vs, csock);
        }
        return 0;

    } else {
        /* listen for connects */
        char *dpy;
        dpy = qemu_malloc(256);
        if (strncmp(display, "unix:", 5) == 0) {
            pstrcpy(dpy, 256, "unix:");
            vs->lsock = unix_listen(display+5, dpy+5, 256-5);
        } else {
            vs->lsock = inet_listen(display, dpy, 256, SOCK_STREAM, 5900);
        }
        if (-1 == vs->lsock) {
            free(dpy);
            return -1;
        } else {
            free(vs->display);
            vs->display = dpy;
        }
    }
    return qemu_set_fd_handler2(vs->lsock, NULL, vnc_listen_read, NULL, vs);
}
