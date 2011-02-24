/*
 * QEMU VNC display driver: Zlib Run-length Encoding (ZRLE)
 *
 * From libvncserver/libvncserver/zrleencodetemplate.c
 * Copyright (C) 2002 RealVNC Ltd.  All Rights Reserved.
 * Copyright (C) 2003 Sun Microsystems, Inc.
 *
 * Copyright (C) 2010 Corentin Chary <corentin.chary@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/*
 * Before including this file, you must define a number of CPP macros.
 *
 * ZRLE_BPP should be 8, 16 or 32 depending on the bits per pixel.
 *
 * Note that the buf argument to ZRLE_ENCODE needs to be at least one pixel
 * bigger than the largest tile of pixel data, since the ZRLE encoding
 * algorithm writes to the position one past the end of the pixel data.
 */


#include <assert.h>

#undef ZRLE_ENDIAN_SUFFIX

#if ZYWRLE_ENDIAN == ENDIAN_LITTLE
#define ZRLE_ENDIAN_SUFFIX le
#elif ZYWRLE_ENDIAN == ENDIAN_BIG
#define ZRLE_ENDIAN_SUFFIX be
#else
#define ZRLE_ENDIAN_SUFFIX ne
#endif

#ifndef ZRLE_CONCAT
#define ZRLE_CONCAT_I(a, b)    a##b
#define ZRLE_CONCAT2(a, b)     ZRLE_CONCAT_I(a, b)
#define ZRLE_CONCAT3(a, b, c)  ZRLE_CONCAT2(a, ZRLE_CONCAT2(b, c))
#endif

#ifdef ZRLE_COMPACT_PIXEL
#define ZRLE_ENCODE_SUFFIX   ZRLE_CONCAT2(ZRLE_COMPACT_PIXEL,ZRLE_ENDIAN_SUFFIX)
#define ZRLE_WRITE_SUFFIX    ZRLE_COMPACT_PIXEL
#define ZRLE_PIXEL           ZRLE_CONCAT3(uint,ZRLE_BPP,_t)
#define ZRLE_BPP_OUT         24
#elif ZRLE_BPP == 15
#define ZRLE_ENCODE_SUFFIX   ZRLE_CONCAT2(ZRLE_BPP,ZRLE_ENDIAN_SUFFIX)
#define ZRLE_WRITE_SUFFIX    16
#define ZRLE_PIXEL           uint16_t
#define ZRLE_BPP_OUT         16
#else
#define ZRLE_ENCODE_SUFFIX   ZRLE_CONCAT2(ZRLE_BPP,ZRLE_ENDIAN_SUFFIX)
#define ZRLE_WRITE_SUFFIX    ZRLE_BPP
#define ZRLE_BPP_OUT         ZRLE_BPP
#define ZRLE_PIXEL           ZRLE_CONCAT3(uint,ZRLE_BPP,_t)
#endif

#define ZRLE_WRITE_PIXEL     ZRLE_CONCAT2(zrle_write_u,       ZRLE_WRITE_SUFFIX)
#define ZRLE_ENCODE          ZRLE_CONCAT2(zrle_encode_,      ZRLE_ENCODE_SUFFIX)
#define ZRLE_ENCODE_TILE     ZRLE_CONCAT2(zrle_encode_tile,  ZRLE_ENCODE_SUFFIX)
#define ZRLE_WRITE_PALETTE   ZRLE_CONCAT2(zrle_write_palette,ZRLE_ENCODE_SUFFIX)

static void ZRLE_ENCODE_TILE(VncState *vs, ZRLE_PIXEL *data, int w, int h,
                             int zywrle_level);

#if ZRLE_BPP != 8
#include "vnc-enc-zywrle-template.c"
#endif


static void ZRLE_ENCODE(VncState *vs, int x, int y, int w, int h,
                        int zywrle_level)
{
    int ty;

    for (ty = y; ty < y + h; ty += VNC_ZRLE_TILE_HEIGHT) {

        int tx, th;

        th = MIN(VNC_ZRLE_TILE_HEIGHT, y + h - ty);

        for (tx = x; tx < x + w; tx += VNC_ZRLE_TILE_WIDTH) {
            int tw;
            ZRLE_PIXEL *buf;

            tw = MIN(VNC_ZRLE_TILE_WIDTH, x + w - tx);

            buf = zrle_convert_fb(vs, tx, ty, tw, th, ZRLE_BPP);
            ZRLE_ENCODE_TILE(vs, buf, tw, th, zywrle_level);
        }
    }
}

static void ZRLE_ENCODE_TILE(VncState *vs, ZRLE_PIXEL *data, int w, int h,
                             int zywrle_level)
{
    VncPalette *palette = &vs->zrle.palette;

    int runs = 0;
    int single_pixels = 0;

    bool use_rle;
    bool use_palette;

    int i;

    ZRLE_PIXEL *ptr = data;
    ZRLE_PIXEL *end = ptr + h * w;
    *end = ~*(end-1); /* one past the end is different so the while loop ends */

    /* Real limit is 127 but we wan't a way to know if there is more than 127 */
    palette_init(palette, 256, ZRLE_BPP);

    while (ptr < end) {
        ZRLE_PIXEL pix = *ptr;
        if (*++ptr != pix) { /* FIXME */
            single_pixels++;
        } else {
            while (*++ptr == pix) ;
            runs++;
        }
        palette_put(palette, pix);
    }

    /* Solid tile is a special case */

    if (palette_size(palette) == 1) {
        bool found;

        vnc_write_u8(vs, 1);
        ZRLE_WRITE_PIXEL(vs, palette_color(palette, 0, &found));
        return;
    }

    zrle_choose_palette_rle(vs, w, h, palette, ZRLE_BPP_OUT,
                            runs, single_pixels, zywrle_level,
                            &use_rle, &use_palette);

    if (!use_palette) {
        vnc_write_u8(vs, (use_rle ? 128 : 0));
    } else {
        uint32_t colors[VNC_PALETTE_MAX_SIZE];
        size_t size = palette_size(palette);

        vnc_write_u8(vs, (use_rle ? 128 : 0) | size);
        palette_fill(palette, colors);

        for (i = 0; i < size; i++) {
            ZRLE_WRITE_PIXEL(vs, colors[i]);
        }
    }

    if (use_rle) {
        ZRLE_PIXEL *ptr = data;
        ZRLE_PIXEL *end = ptr + w * h;
        ZRLE_PIXEL *run_start;
        ZRLE_PIXEL pix;

        while (ptr < end) {
            int len;
            int index = 0;

            run_start = ptr;
            pix = *ptr++;

            while (*ptr == pix && ptr < end) {
                ptr++;
            }

            len = ptr - run_start;

            if (use_palette)
                index = palette_idx(palette, pix);

            if (len <= 2 && use_palette) {
                if (len == 2) {
                    vnc_write_u8(vs, index);
                }
                vnc_write_u8(vs, index);
                continue;
            }
            if (use_palette) {
                vnc_write_u8(vs, index | 128);
            } else {
                ZRLE_WRITE_PIXEL(vs, pix);
            }

            len -= 1;

            while (len >= 255) {
                vnc_write_u8(vs, 255);
                len -= 255;
            }

            vnc_write_u8(vs, len);
        }
    } else if (use_palette) { /* no RLE */
        int bppp;
        ZRLE_PIXEL *ptr = data;

        /* packed pixels */

        assert (palette_size(palette) < 17);

        bppp = bits_per_packed_pixel[palette_size(palette)-1];

        for (i = 0; i < h; i++) {
            uint8_t nbits = 0;
            uint8_t byte = 0;

            ZRLE_PIXEL *eol = ptr + w;

            while (ptr < eol) {
                ZRLE_PIXEL pix = *ptr++;
                uint8_t index = palette_idx(palette, pix);

                byte = (byte << bppp) | index;
                nbits += bppp;
                if (nbits >= 8) {
                    vnc_write_u8(vs, byte);
                    nbits = 0;
                }
            }
            if (nbits > 0) {
                byte <<= 8 - nbits;
                vnc_write_u8(vs, byte);
            }
        }
    } else {

        /* raw */

#if ZRLE_BPP != 8
        if (zywrle_level > 0 && !(zywrle_level & 0x80)) {
            ZYWRLE_ANALYZE(data, data, w, h, w, zywrle_level, vs->zywrle.buf);
            ZRLE_ENCODE_TILE(vs, data, w, h, zywrle_level | 0x80);
        }
        else
#endif
        {
#ifdef ZRLE_COMPACT_PIXEL
            ZRLE_PIXEL *ptr;

            for (ptr = data; ptr < data + w * h; ptr++) {
                ZRLE_WRITE_PIXEL(vs, *ptr);
            }
#else
            vnc_write(vs, data, w * h * (ZRLE_BPP / 8));
#endif
        }
    }
}

#undef ZRLE_PIXEL
#undef ZRLE_WRITE_PIXEL
#undef ZRLE_ENCODE
#undef ZRLE_ENCODE_TILE
#undef ZYWRLE_ENCODE_TILE
#undef ZRLE_BPP_OUT
#undef ZRLE_WRITE_SUFFIX
#undef ZRLE_ENCODE_SUFFIX
