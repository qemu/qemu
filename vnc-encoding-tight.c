/*
 * QEMU VNC display driver: tight encoding
 *
 * From libvncserver/libvncserver/tight.c
 * Copyright (C) 2000, 2001 Const Kaplinsky.  All Rights Reserved.
 * Copyright (C) 1999 AT&T Laboratories Cambridge.  All Rights Reserved.
 *
 * Copyright (C) 2010 Corentin Chary <corentin.chary@gmail.com>
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

#include <stdbool.h>

#include "vnc.h"
#include "vnc-encoding-tight.h"

/* Compression level stuff. The following array contains various
   encoder parameters for each of 10 compression levels (0..9).
   Last three parameters correspond to JPEG quality levels (0..9). */

static const struct {
    int max_rect_size, max_rect_width;
    int mono_min_rect_size, gradient_min_rect_size;
    int idx_zlib_level, mono_zlib_level, raw_zlib_level, gradient_zlib_level;
    int gradient_threshold, gradient_threshold24;
    int idx_max_colors_divisor;
    int jpeg_quality, jpeg_threshold, jpeg_threshold24;
} tight_conf[] = {
    {   512,   32,   6, 65536, 0, 0, 0, 0,   0,   0,   4,  5, 10000, 23000 },
    {  2048,  128,   6, 65536, 1, 1, 1, 0,   0,   0,   8, 10,  8000, 18000 },
    {  6144,  256,   8, 65536, 3, 3, 2, 0,   0,   0,  24, 15,  6500, 15000 },
    { 10240, 1024,  12, 65536, 5, 5, 3, 0,   0,   0,  32, 25,  5000, 12000 },
    { 16384, 2048,  12, 65536, 6, 6, 4, 0,   0,   0,  32, 37,  4000, 10000 },
    { 32768, 2048,  12,  4096, 7, 7, 5, 4, 150, 380,  32, 50,  3000,  8000 },
    { 65536, 2048,  16,  4096, 7, 7, 6, 4, 170, 420,  48, 60,  2000,  5000 },
    { 65536, 2048,  16,  4096, 8, 8, 7, 5, 180, 450,  64, 70,  1000,  2500 },
    { 65536, 2048,  32,  8192, 9, 9, 8, 6, 190, 475,  64, 75,   500,  1200 },
    { 65536, 2048,  32,  8192, 9, 9, 9, 6, 200, 500,  96, 80,   200,   500 }
};

/*
 * Check if a rectangle is all of the same color. If needSameColor is
 * set to non-zero, then also check that its color equals to the
 * *colorPtr value. The result is 1 if the test is successfull, and in
 * that case new color will be stored in *colorPtr.
 */

#define DEFINE_CHECK_SOLID_FUNCTION(bpp)                                \
                                                                        \
    static bool                                                         \
    check_solid_tile##bpp(VncState *vs, int x, int y, int w, int h,     \
                          uint32_t* color, bool samecolor)              \
    {                                                                   \
        VncDisplay *vd = vs->vd;                                        \
        uint##bpp##_t *fbptr;                                           \
        uint##bpp##_t c;                                                \
        int dx, dy;                                                     \
                                                                        \
        fbptr = (uint##bpp##_t *)                                       \
            (vd->server->data + y * ds_get_linesize(vs->ds) +           \
             x * ds_get_bytes_per_pixel(vs->ds));                       \
                                                                        \
        c = *fbptr;                                                     \
        if (samecolor && (uint32_t)c != *color) {                       \
            return false;                                               \
        }                                                               \
                                                                        \
        for (dy = 0; dy < h; dy++) {                                    \
            for (dx = 0; dx < w; dx++) {                                \
                if (c != fbptr[dx]) {                                   \
                    return false;                                       \
                }                                                       \
            }                                                           \
            fbptr = (uint##bpp##_t *)                                   \
                ((uint8_t *)fbptr + ds_get_linesize(vs->ds));           \
        }                                                               \
                                                                        \
        *color = (uint32_t)c;                                           \
        return true;                                                    \
    }

DEFINE_CHECK_SOLID_FUNCTION(32)
DEFINE_CHECK_SOLID_FUNCTION(16)
DEFINE_CHECK_SOLID_FUNCTION(8)

static bool check_solid_tile(VncState *vs, int x, int y, int w, int h,
                             uint32_t* color, bool samecolor)
{
    VncDisplay *vd = vs->vd;

    switch(vd->server->pf.bytes_per_pixel) {
    case 4:
        return check_solid_tile32(vs, x, y, w, h, color, samecolor);
    case 2:
        return check_solid_tile16(vs, x, y, w, h, color, samecolor);
    default:
        return check_solid_tile8(vs, x, y, w, h, color, samecolor);
    }
}

static void find_best_solid_area(VncState *vs, int x, int y, int w, int h,
                                 uint32_t color, int *w_ptr, int *h_ptr)
{
    int dx, dy, dw, dh;
    int w_prev;
    int w_best = 0, h_best = 0;

    w_prev = w;

    for (dy = y; dy < y + h; dy += VNC_TIGHT_MAX_SPLIT_TILE_SIZE) {

        dh = MIN(VNC_TIGHT_MAX_SPLIT_TILE_SIZE, y + h - dy);
        dw = MIN(VNC_TIGHT_MAX_SPLIT_TILE_SIZE, w_prev);

        if (!check_solid_tile(vs, x, dy, dw, dh, &color, true)) {
            break;
        }

        for (dx = x + dw; dx < x + w_prev;) {
            dw = MIN(VNC_TIGHT_MAX_SPLIT_TILE_SIZE, x + w_prev - dx);

            if (!check_solid_tile(vs, dx, dy, dw, dh, &color, true)) {
                break;
            }
            dx += dw;
        }

        w_prev = dx - x;
        if (w_prev * (dy + dh - y) > w_best * h_best) {
            w_best = w_prev;
            h_best = dy + dh - y;
        }
    }

    *w_ptr = w_best;
    *h_ptr = h_best;
}

static void extend_solid_area(VncState *vs, int x, int y, int w, int h,
                              uint32_t color, int *x_ptr, int *y_ptr,
                              int *w_ptr, int *h_ptr)
{
    int cx, cy;

    /* Try to extend the area upwards. */
    for ( cy = *y_ptr - 1;
          cy >= y && check_solid_tile(vs, *x_ptr, cy, *w_ptr, 1, &color, true);
          cy-- );
    *h_ptr += *y_ptr - (cy + 1);
    *y_ptr = cy + 1;

    /* ... downwards. */
    for ( cy = *y_ptr + *h_ptr;
          cy < y + h &&
              check_solid_tile(vs, *x_ptr, cy, *w_ptr, 1, &color, true);
          cy++ );
    *h_ptr += cy - (*y_ptr + *h_ptr);

    /* ... to the left. */
    for ( cx = *x_ptr - 1;
          cx >= x && check_solid_tile(vs, cx, *y_ptr, 1, *h_ptr, &color, true);
          cx-- );
    *w_ptr += *x_ptr - (cx + 1);
    *x_ptr = cx + 1;

    /* ... to the right. */
    for ( cx = *x_ptr + *w_ptr;
          cx < x + w &&
              check_solid_tile(vs, cx, *y_ptr, 1, *h_ptr, &color, true);
          cx++ );
    *w_ptr += cx - (*x_ptr + *w_ptr);
}

static int tight_init_stream(VncState *vs, int stream_id,
                             int level, int strategy)
{
    z_streamp zstream = &vs->tight_stream[stream_id];

    if (zstream->opaque == NULL) {
        int err;

        VNC_DEBUG("VNC: TIGHT: initializing zlib stream %d\n", stream_id);
        VNC_DEBUG("VNC: TIGHT: opaque = %p | vs = %p\n", zstream->opaque, vs);
        zstream->zalloc = vnc_zlib_zalloc;
        zstream->zfree = vnc_zlib_zfree;

        err = deflateInit2(zstream, level, Z_DEFLATED, MAX_WBITS,
                           MAX_MEM_LEVEL, strategy);

        if (err != Z_OK) {
            fprintf(stderr, "VNC: error initializing zlib\n");
            return -1;
        }

        vs->tight_levels[stream_id] = level;
        zstream->opaque = vs;
    }

    if (vs->tight_levels[stream_id] != level) {
        if (deflateParams(zstream, level, strategy) != Z_OK) {
            return -1;
        }
        vs->tight_levels[stream_id] = level;
    }
    return 0;
}

static void tight_send_compact_size(VncState *vs, size_t len)
{
    int lpc = 0;
    int bytes = 0;
    char buf[3] = {0, 0, 0};

    buf[bytes++] = len & 0x7F;
    if (len > 0x7F) {
        buf[bytes-1] |= 0x80;
        buf[bytes++] = (len >> 7) & 0x7F;
        if (len > 0x3FFF) {
            buf[bytes-1] |= 0x80;
            buf[bytes++] = (len >> 14) & 0xFF;
        }
    }
    for (lpc = 0; lpc < bytes; lpc++) {
        vnc_write_u8(vs, buf[lpc]);
    }
}

static int tight_compress_data(VncState *vs, int stream_id, size_t bytes,
                               int level, int strategy)
{
    z_streamp zstream = &vs->tight_stream[stream_id];
    int previous_out;

    if (bytes < VNC_TIGHT_MIN_TO_COMPRESS) {
        vnc_write(vs, vs->tight.buffer, vs->tight.offset);
        return bytes;
    }

    if (tight_init_stream(vs, stream_id, level, strategy)) {
        return -1;
    }

    /* reserve memory in output buffer */
    buffer_reserve(&vs->tight_zlib, bytes + 64);

    /* set pointers */
    zstream->next_in = vs->tight.buffer;
    zstream->avail_in = vs->tight.offset;
    zstream->next_out = vs->tight_zlib.buffer + vs->tight_zlib.offset;
    zstream->avail_out = vs->tight_zlib.capacity - vs->tight_zlib.offset;
    zstream->data_type = Z_BINARY;
    previous_out = zstream->total_out;

    /* start encoding */
    if (deflate(zstream, Z_SYNC_FLUSH) != Z_OK) {
        fprintf(stderr, "VNC: error during tight compression\n");
        return -1;
    }

    vs->tight_zlib.offset = vs->tight_zlib.capacity - zstream->avail_out;
    bytes = zstream->total_out - previous_out;

    tight_send_compact_size(vs, bytes);
    vnc_write(vs, vs->tight_zlib.buffer, bytes);

    buffer_reset(&vs->tight_zlib);

    return bytes;
}

/*
 * Subencoding implementations.
 */
static void tight_pack24(VncState *vs, size_t count)
{
    unsigned char *buf;
    uint32_t *buf32;
    uint32_t pix;
    int rshift, gshift, bshift;

    buf = vs->tight.buffer;
    buf32 = (uint32_t *)buf;

    if ((vs->clientds.flags & QEMU_BIG_ENDIAN_FLAG) ==
        (vs->ds->surface->flags & QEMU_BIG_ENDIAN_FLAG)) {
        rshift = vs->clientds.pf.rshift;
        gshift = vs->clientds.pf.gshift;
        bshift = vs->clientds.pf.bshift;
    } else {
        rshift = 24 - vs->clientds.pf.rshift;
        gshift = 24 - vs->clientds.pf.gshift;
        bshift = 24 - vs->clientds.pf.bshift;
    }

    vs->tight.offset = count * 3;

    while (count--) {
        pix = *buf32++;
        *buf++ = (char)(pix >> rshift);
        *buf++ = (char)(pix >> gshift);
        *buf++ = (char)(pix >> bshift);
    }
}

static int send_full_color_rect(VncState *vs, int w, int h)
{
    int stream = 0;
    size_t bytes;

    vnc_write_u8(vs, stream << 4); /* no flushing, no filter */

    if (vs->tight_pixel24) {
        tight_pack24(vs, w * h);
        bytes = 3;
    } else {
        bytes = vs->clientds.pf.bytes_per_pixel;
    }

    bytes = tight_compress_data(vs, stream, w * h * bytes,
                                tight_conf[vs->tight_compression].raw_zlib_level,
                                Z_DEFAULT_STRATEGY);

    return (bytes >= 0);
}

static int send_solid_rect(VncState *vs)
{
    size_t bytes;

    vnc_write_u8(vs, VNC_TIGHT_FILL << 4); /* no flushing, no filter */

    if (vs->tight_pixel24) {
        tight_pack24(vs, 1);
        bytes = 3;
    } else {
        bytes = vs->clientds.pf.bytes_per_pixel;
    }

    vnc_write(vs, vs->tight.buffer, bytes);
    return 1;
}

static void vnc_tight_start(VncState *vs)
{
    buffer_reset(&vs->tight);

    // make the output buffer be the zlib buffer, so we can compress it later
    vs->tight_tmp = vs->output;
    vs->output = vs->tight;
}

static void vnc_tight_stop(VncState *vs)
{
    // switch back to normal output/zlib buffers
    vs->tight = vs->output;
    vs->output = vs->tight_tmp;
}

static int send_sub_rect(VncState *vs, int x, int y, int w, int h)
{
    vnc_framebuffer_update(vs, x, y, w, h, VNC_ENCODING_TIGHT);

    /*
     * Convert pixels and store them in vs->tight
     * We will probably rework that later, probably
     * when adding other sub-encodings
     */
    vnc_tight_start(vs);
    vnc_raw_send_framebuffer_update(vs, x, y, w, h);
    vnc_tight_stop(vs);

    return send_full_color_rect(vs, w, h);
}

static int send_sub_rect_solid(VncState *vs, int x, int y, int w, int h)
{
    vnc_framebuffer_update(vs, x, y, w, h, VNC_ENCODING_TIGHT);

    vnc_tight_start(vs);
    vnc_raw_send_framebuffer_update(vs, x, y, w, h);
    vnc_tight_stop(vs);

    return send_solid_rect(vs);
}

static int send_rect_simple(VncState *vs, int x, int y, int w, int h)
{
    int max_size, max_width;
    int max_sub_width, max_sub_height;
    int dx, dy;
    int rw, rh;
    int n = 0;

    max_size = tight_conf[vs->tight_compression].max_rect_size;
    max_width = tight_conf[vs->tight_compression].max_rect_width;

    if (w > max_width || w * h > max_size) {
        max_sub_width = (w > max_width) ? max_width : w;
        max_sub_height = max_size / max_sub_width;

        for (dy = 0; dy < h; dy += max_sub_height) {
            for (dx = 0; dx < w; dx += max_width) {
                rw = MIN(max_sub_width, w - dx);
                rh = MIN(max_sub_height, h - dy);
                n += send_sub_rect(vs, x+dx, y+dy, rw, rh);
            }
        }
    } else {
        n += send_sub_rect(vs, x, y, w, h);
    }

    return n;
}

static int find_large_solid_color_rect(VncState *vs, int x, int y,
                                       int w, int h, int max_rows)
{
    int dx, dy, dw, dh;
    int n = 0;

    /* Try to find large solid-color areas and send them separately. */

    for (dy = y; dy < y + h; dy += VNC_TIGHT_MAX_SPLIT_TILE_SIZE) {

        /* If a rectangle becomes too large, send its upper part now. */

        if (dy - y >= max_rows) {
            n += send_rect_simple(vs, x, y, w, max_rows);
            y += max_rows;
            h -= max_rows;
        }

        dh = MIN(VNC_TIGHT_MAX_SPLIT_TILE_SIZE, (y + h - dy));

        for (dx = x; dx < x + w; dx += VNC_TIGHT_MAX_SPLIT_TILE_SIZE) {
            uint32_t color_value;
            int x_best, y_best, w_best, h_best;

            dw = MIN(VNC_TIGHT_MAX_SPLIT_TILE_SIZE, (x + w - dx));

            if (!check_solid_tile(vs, dx, dy, dw, dh, &color_value, false)) {
                continue ;
            }

            /* Get dimensions of solid-color area. */

            find_best_solid_area(vs, dx, dy, w - (dx - x), h - (dy - y),
                                 color_value, &w_best, &h_best);

            /* Make sure a solid rectangle is large enough
               (or the whole rectangle is of the same color). */

            if (w_best * h_best != w * h &&
                w_best * h_best < VNC_TIGHT_MIN_SOLID_SUBRECT_SIZE) {
                continue;
            }

            /* Try to extend solid rectangle to maximum size. */

            x_best = dx; y_best = dy;
            extend_solid_area(vs, x, y, w, h, color_value,
                              &x_best, &y_best, &w_best, &h_best);

            /* Send rectangles at top and left to solid-color area. */

            if (y_best != y) {
                n += send_rect_simple(vs, x, y, w, y_best-y);
            }
            if (x_best != x) {
                n += vnc_tight_send_framebuffer_update(vs, x, y_best,
                                                       x_best-x, h_best);
            }

            /* Send solid-color rectangle. */
            n += send_sub_rect_solid(vs, x_best, y_best, w_best, h_best);

            /* Send remaining rectangles (at right and bottom). */

            if (x_best + w_best != x + w) {
                n += vnc_tight_send_framebuffer_update(vs, x_best+w_best,
                                                       y_best,
                                                       w-(x_best-x)-w_best,
                                                       h_best);
            }
            if (y_best + h_best != y + h) {
                n += vnc_tight_send_framebuffer_update(vs, x, y_best+h_best,
                                                       w, h-(y_best-y)-h_best);
            }

            /* Return after all recursive calls are done. */
            return n;
        }
    }
    return n + send_rect_simple(vs, x, y, w, h);
}

int vnc_tight_send_framebuffer_update(VncState *vs, int x, int y,
                                      int w, int h)
{
    int max_rows;

    if (vs->clientds.pf.bytes_per_pixel == 4 && vs->clientds.pf.rmax == 0xFF &&
        vs->clientds.pf.bmax == 0xFF && vs->clientds.pf.gmax == 0xFF) {
        vs->tight_pixel24 = true;
    } else {
        vs->tight_pixel24 = false;
    }

    if (w * h < VNC_TIGHT_MIN_SPLIT_RECT_SIZE)
        return send_rect_simple(vs, x, y, w, h);

    /* Calculate maximum number of rows in one non-solid rectangle. */

    max_rows = tight_conf[vs->tight_compression].max_rect_size;
    max_rows /= MIN(tight_conf[vs->tight_compression].max_rect_width, w);

    return find_large_solid_color_rect(vs, x, y, w, h, max_rows);
}

void vnc_tight_clear(VncState *vs)
{
    int i;
    for (i=0; i<ARRAY_SIZE(vs->tight_stream); i++) {
        if (vs->tight_stream[i].opaque) {
            deflateEnd(&vs->tight_stream[i]);
        }
    }

    buffer_free(&vs->tight);
    buffer_free(&vs->tight_zlib);
}
