/*
 * QEMU VNC display driver: Zlib Run-length Encoding (ZRLE)
 *
 * From libvncserver/libvncserver/zrle.c
 * Copyright (C) 2002 RealVNC Ltd.  All Rights Reserved.
 * Copyright (C) 2003 Sun Microsystems, Inc.
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

#include "qemu/osdep.h"
#include "vnc.h"
#include "vnc-enc-zrle.h"

static const int bits_per_packed_pixel[] = {
  0, 1, 2, 2, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4
};


static void vnc_zrle_start(VncState *vs)
{
    buffer_reset(&vs->zrle->zrle);

    /* make the output buffer be the zlib buffer, so we can compress it later */
    vs->zrle->tmp = vs->output;
    vs->output = vs->zrle->zrle;
}

static void vnc_zrle_stop(VncState *vs)
{
    /* switch back to normal output/zlib buffers */
    vs->zrle->zrle = vs->output;
    vs->output = vs->zrle->tmp;
}

static void *zrle_convert_fb(VncState *vs, int x, int y, int w, int h,
                             int bpp)
{
    Buffer tmp;

    buffer_reset(&vs->zrle->fb);
    buffer_reserve(&vs->zrle->fb, w * h * bpp + bpp);

    tmp = vs->output;
    vs->output = vs->zrle->fb;

    vnc_raw_send_framebuffer_update(vs, x, y, w, h);

    vs->zrle->fb = vs->output;
    vs->output = tmp;
    return vs->zrle->fb.buffer;
}

static int zrle_compress_data(VncState *vs, int level)
{
    z_streamp zstream = &vs->zrle->stream;

    buffer_reset(&vs->zrle->zlib);

    if (zstream->opaque != vs) {
        int err;

        zstream->zalloc = vnc_zlib_zalloc;
        zstream->zfree = vnc_zlib_zfree;

        err = deflateInit2(zstream, level, Z_DEFLATED, MAX_WBITS,
                           MAX_MEM_LEVEL, Z_DEFAULT_STRATEGY);

        if (err != Z_OK) {
            fprintf(stderr, "VNC: error initializing zlib\n");
            return -1;
        }

        zstream->opaque = vs;
    }

    /* reserve memory in output buffer */
    buffer_reserve(&vs->zrle->zlib, vs->zrle->zrle.offset + 64);

    /* set pointers */
    zstream->next_in = vs->zrle->zrle.buffer;
    zstream->avail_in = vs->zrle->zrle.offset;
    zstream->next_out = vs->zrle->zlib.buffer;
    zstream->avail_out = vs->zrle->zlib.capacity;
    zstream->data_type = Z_BINARY;

    /* start encoding */
    if (deflate(zstream, Z_SYNC_FLUSH) != Z_OK) {
        fprintf(stderr, "VNC: error during zrle compression\n");
        return -1;
    }

    vs->zrle->zlib.offset = vs->zrle->zlib.capacity - zstream->avail_out;
    return vs->zrle->zlib.offset;
}

/* Try to work out whether to use RLE and/or a palette.  We do this by
 * estimating the number of bytes which will be generated and picking the
 * method which results in the fewest bytes.  Of course this may not result
 * in the fewest bytes after compression... */
static void zrle_choose_palette_rle(VncState *vs, int w, int h,
                                    VncPalette *palette, int bpp_out,
                                    int runs, int single_pixels,
                                    int zywrle_level,
                                    bool *use_rle, bool *use_palette)
{
    size_t estimated_bytes;
    size_t plain_rle_bytes;

    *use_palette = *use_rle = false;

    estimated_bytes = w * h * (bpp_out / 8); /* start assuming raw */

    if (bpp_out != 8) {
        if (zywrle_level > 0 && !(zywrle_level & 0x80))
            estimated_bytes >>= zywrle_level;
    }

    plain_rle_bytes = ((bpp_out / 8) + 1) * (runs + single_pixels);

    if (plain_rle_bytes < estimated_bytes) {
        *use_rle = true;
        estimated_bytes = plain_rle_bytes;
    }

    if (palette_size(palette) < 128) {
        int palette_rle_bytes;

        palette_rle_bytes = (bpp_out / 8) * palette_size(palette);
        palette_rle_bytes += 2 * runs + single_pixels;

        if (palette_rle_bytes < estimated_bytes) {
            *use_rle = true;
            *use_palette = true;
            estimated_bytes = palette_rle_bytes;
        }

        if (palette_size(palette) < 17) {
            int packed_bytes;

            packed_bytes = (bpp_out / 8) * palette_size(palette);
            packed_bytes += w * h *
                bits_per_packed_pixel[palette_size(palette)-1] / 8;

            if (packed_bytes < estimated_bytes) {
                *use_rle = false;
                *use_palette = true;
            }
        }
    }
}

static void zrle_write_u32(VncState *vs, uint32_t value)
{
    vnc_write(vs, (uint8_t *)&value, 4);
}

static void zrle_write_u24a(VncState *vs, uint32_t value)
{
    vnc_write(vs, (uint8_t *)&value, 3);
}

static void zrle_write_u24b(VncState *vs, uint32_t value)
{
    vnc_write(vs, ((uint8_t *)&value) + 1, 3);
}

static void zrle_write_u16(VncState *vs, uint16_t value)
{
    vnc_write(vs, (uint8_t *)&value, 2);
}

static void zrle_write_u8(VncState *vs, uint8_t value)
{
    vnc_write_u8(vs, value);
}

#define ENDIAN_LITTLE 0
#define ENDIAN_BIG    1
#define ENDIAN_NO     2

#define ZRLE_BPP 8
#define ZYWRLE_ENDIAN ENDIAN_NO
#include "vnc-enc-zrle.c.inc"
#undef ZRLE_BPP

#define ZRLE_BPP 15
#undef ZYWRLE_ENDIAN
#define ZYWRLE_ENDIAN ENDIAN_LITTLE
#include "vnc-enc-zrle.c.inc"

#undef ZYWRLE_ENDIAN
#define ZYWRLE_ENDIAN ENDIAN_BIG
#include "vnc-enc-zrle.c.inc"

#undef ZRLE_BPP
#define ZRLE_BPP 16
#undef ZYWRLE_ENDIAN
#define ZYWRLE_ENDIAN ENDIAN_LITTLE
#include "vnc-enc-zrle.c.inc"

#undef ZYWRLE_ENDIAN
#define ZYWRLE_ENDIAN ENDIAN_BIG
#include "vnc-enc-zrle.c.inc"

#undef ZRLE_BPP
#define ZRLE_BPP 32
#undef ZYWRLE_ENDIAN
#define ZYWRLE_ENDIAN ENDIAN_LITTLE
#include "vnc-enc-zrle.c.inc"

#undef ZYWRLE_ENDIAN
#define ZYWRLE_ENDIAN ENDIAN_BIG
#include "vnc-enc-zrle.c.inc"

#define ZRLE_COMPACT_PIXEL 24a
#undef ZYWRLE_ENDIAN
#define ZYWRLE_ENDIAN ENDIAN_LITTLE
#include "vnc-enc-zrle.c.inc"

#undef ZYWRLE_ENDIAN
#define ZYWRLE_ENDIAN ENDIAN_BIG
#include "vnc-enc-zrle.c.inc"

#undef ZRLE_COMPACT_PIXEL
#define ZRLE_COMPACT_PIXEL 24b
#undef ZYWRLE_ENDIAN
#define ZYWRLE_ENDIAN ENDIAN_LITTLE
#include "vnc-enc-zrle.c.inc"

#undef ZYWRLE_ENDIAN
#define ZYWRLE_ENDIAN ENDIAN_BIG
#include "vnc-enc-zrle.c.inc"
#undef ZRLE_COMPACT_PIXEL
#undef ZRLE_BPP

static int zrle_send_framebuffer_update(VncState *vs, int x, int y,
                                        int w, int h)
{
    bool be = vs->client_be;
    size_t bytes;
    int zywrle_level;

    if (vs->zrle->type == VNC_ENCODING_ZYWRLE) {
        if (!vs->vd->lossy || vs->tight->quality == (uint8_t)-1
            || vs->tight->quality == 9) {
            zywrle_level = 0;
            vs->zrle->type = VNC_ENCODING_ZRLE;
        } else if (vs->tight->quality < 3) {
            zywrle_level = 3;
        } else if (vs->tight->quality < 6) {
            zywrle_level = 2;
        } else {
            zywrle_level = 1;
        }
    } else {
        zywrle_level = 0;
    }

    vnc_zrle_start(vs);

    switch (vs->client_pf.bytes_per_pixel) {
    case 1:
        zrle_encode_8ne(vs, x, y, w, h, zywrle_level);
        break;

    case 2:
        if (vs->client_pf.gmax > 0x1F) {
            if (be) {
                zrle_encode_16be(vs, x, y, w, h, zywrle_level);
            } else {
                zrle_encode_16le(vs, x, y, w, h, zywrle_level);
            }
        } else {
            if (be) {
                zrle_encode_15be(vs, x, y, w, h, zywrle_level);
            } else {
                zrle_encode_15le(vs, x, y, w, h, zywrle_level);
            }
        }
        break;

    case 4:
    {
        bool fits_in_ls3bytes;
        bool fits_in_ms3bytes;

        fits_in_ls3bytes =
            ((vs->client_pf.rmax << vs->client_pf.rshift) < (1 << 24) &&
             (vs->client_pf.gmax << vs->client_pf.gshift) < (1 << 24) &&
             (vs->client_pf.bmax << vs->client_pf.bshift) < (1 << 24));

        fits_in_ms3bytes = (vs->client_pf.rshift > 7 &&
                            vs->client_pf.gshift > 7 &&
                            vs->client_pf.bshift > 7);

        if ((fits_in_ls3bytes && !be) || (fits_in_ms3bytes && be)) {
            if (be) {
                zrle_encode_24abe(vs, x, y, w, h, zywrle_level);
            } else {
                zrle_encode_24ale(vs, x, y, w, h, zywrle_level);
          }
        } else if ((fits_in_ls3bytes && be) || (fits_in_ms3bytes && !be)) {
            if (be) {
                zrle_encode_24bbe(vs, x, y, w, h, zywrle_level);
            } else {
                zrle_encode_24ble(vs, x, y, w, h, zywrle_level);
            }
        } else {
            if (be) {
                zrle_encode_32be(vs, x, y, w, h, zywrle_level);
            } else {
                zrle_encode_32le(vs, x, y, w, h, zywrle_level);
            }
        }
    }
    break;
    }

    vnc_zrle_stop(vs);
    bytes = zrle_compress_data(vs, Z_DEFAULT_COMPRESSION);
    vnc_framebuffer_update(vs, x, y, w, h, vs->zrle->type);
    vnc_write_u32(vs, bytes);
    vnc_write(vs, vs->zrle->zlib.buffer, vs->zrle->zlib.offset);
    return 1;
}

int vnc_zrle_send_framebuffer_update(VncState *vs, int x, int y, int w, int h)
{
    vs->zrle->type = VNC_ENCODING_ZRLE;
    return zrle_send_framebuffer_update(vs, x, y, w, h);
}

int vnc_zywrle_send_framebuffer_update(VncState *vs, int x, int y, int w, int h)
{
    vs->zrle->type = VNC_ENCODING_ZYWRLE;
    return zrle_send_framebuffer_update(vs, x, y, w, h);
}

void vnc_zrle_clear(VncState *vs)
{
    if (vs->zrle->stream.opaque) {
        deflateEnd(&vs->zrle->stream);
    }
    buffer_free(&vs->zrle->zrle);
    buffer_free(&vs->zrle->fb);
    buffer_free(&vs->zrle->zlib);
}
