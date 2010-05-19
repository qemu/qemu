/*
 * QEMU VNC display driver: zlib encoding
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

#define ZALLOC_ALIGNMENT 16

static void *zalloc(void *x, unsigned items, unsigned size)
{
    void *p;

    size *= items;
    size = (size + ZALLOC_ALIGNMENT - 1) & ~(ZALLOC_ALIGNMENT - 1);

    p = qemu_mallocz(size);

    return (p);
}

static void zfree(void *x, void *addr)
{
    qemu_free(addr);
}

static void vnc_zlib_start(VncState *vs)
{
    buffer_reset(&vs->zlib);

    // make the output buffer be the zlib buffer, so we can compress it later
    vs->zlib_tmp = vs->output;
    vs->output = vs->zlib;
}

static int vnc_zlib_stop(VncState *vs)
{
    z_streamp zstream = &vs->zlib_stream;
    int previous_out;

    // switch back to normal output/zlib buffers
    vs->zlib = vs->output;
    vs->output = vs->zlib_tmp;

    // compress the zlib buffer

    // initialize the stream
    // XXX need one stream per session
    if (zstream->opaque != vs) {
        int err;

        VNC_DEBUG("VNC: initializing zlib stream\n");
        VNC_DEBUG("VNC: opaque = %p | vs = %p\n", zstream->opaque, vs);
        zstream->zalloc = zalloc;
        zstream->zfree = zfree;

        err = deflateInit2(zstream, vs->tight_compression, Z_DEFLATED, MAX_WBITS,
                           MAX_MEM_LEVEL, Z_DEFAULT_STRATEGY);

        if (err != Z_OK) {
            fprintf(stderr, "VNC: error initializing zlib\n");
            return -1;
        }

        vs->zlib_level = vs->tight_compression;
        zstream->opaque = vs;
    }

    if (vs->tight_compression != vs->zlib_level) {
        if (deflateParams(zstream, vs->tight_compression,
                          Z_DEFAULT_STRATEGY) != Z_OK) {
            return -1;
        }
        vs->zlib_level = vs->tight_compression;
    }

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

int vnc_zlib_send_framebuffer_update(VncState *vs, int x, int y, int w, int h)
{
    int old_offset, new_offset, bytes_written;

    vnc_framebuffer_update(vs, x, y, w, h, VNC_ENCODING_ZLIB);

    // remember where we put in the follow-up size
    old_offset = vs->output.offset;
    vnc_write_s32(vs, 0);

    // compress the stream
    vnc_zlib_start(vs);
    vnc_raw_send_framebuffer_update(vs, x, y, w, h);
    bytes_written = vnc_zlib_stop(vs);

    if (bytes_written == -1)
        return 0;

    // hack in the size
    new_offset = vs->output.offset;
    vs->output.offset = old_offset;
    vnc_write_u32(vs, bytes_written);
    vs->output.offset = new_offset;

    return 1;
}

void vnc_zlib_clear(VncState *vs)
{
    if (vs->zlib_stream.opaque) {
        deflateEnd(&vs->zlib_stream);
    }
    buffer_free(&vs->zlib);
}
