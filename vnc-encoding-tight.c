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
    for(lpc = 0; lpc < bytes; lpc++) {
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

int vnc_tight_send_framebuffer_update(VncState *vs, int x, int y,
                                      int w, int h)
{
    if (vs->clientds.pf.bytes_per_pixel == 4 && vs->clientds.pf.rmax == 0xFF &&
        vs->clientds.pf.bmax == 0xFF && vs->clientds.pf.gmax == 0xFF) {
        vs->tight_pixel24 = true;
    } else {
        vs->tight_pixel24 = false;
    }

    return send_rect_simple(vs, x, y, w, h);
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
