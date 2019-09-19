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

#include "qemu/osdep.h"

/* This needs to be before jpeglib.h line because of conflict with
   INT32 definitions between jmorecfg.h (included by jpeglib.h) and
   Win32 basetsd.h (included by windows.h). */

#ifdef CONFIG_VNC_PNG
/* The following define is needed by pngconf.h. Otherwise it won't compile,
   because setjmp.h was already included by qemu-common.h. */
#define PNG_SKIP_SETJMP_CHECK
#include <png.h>
#endif
#ifdef CONFIG_VNC_JPEG
#include <jpeglib.h>
#endif

#include "qemu/bswap.h"
#include "vnc.h"
#include "vnc-enc-tight.h"
#include "vnc-palette.h"

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


static int tight_send_framebuffer_update(VncState *vs, int x, int y,
                                         int w, int h);

#ifdef CONFIG_VNC_JPEG
static const struct {
    double jpeg_freq_min;       /* Don't send JPEG if the freq is bellow */
    double jpeg_freq_threshold; /* Always send JPEG if the freq is above */
    int jpeg_idx;               /* Allow indexed JPEG */
    int jpeg_full;              /* Allow full color JPEG */
} tight_jpeg_conf[] = {
    { 0,   8,  1, 1 },
    { 0,   8,  1, 1 },
    { 0,   8,  1, 1 },
    { 0,   8,  1, 1 },
    { 0,   10, 1, 1 },
    { 0.1, 10, 1, 1 },
    { 0.2, 10, 1, 1 },
    { 0.3, 12, 0, 0 },
    { 0.4, 14, 0, 0 },
    { 0.5, 16, 0, 0 },
};
#endif

#ifdef CONFIG_VNC_PNG
static const struct {
    int png_zlib_level, png_filters;
} tight_png_conf[] = {
    { 0, PNG_NO_FILTERS },
    { 1, PNG_NO_FILTERS },
    { 2, PNG_NO_FILTERS },
    { 3, PNG_NO_FILTERS },
    { 4, PNG_NO_FILTERS },
    { 5, PNG_ALL_FILTERS },
    { 6, PNG_ALL_FILTERS },
    { 7, PNG_ALL_FILTERS },
    { 8, PNG_ALL_FILTERS },
    { 9, PNG_ALL_FILTERS },
};

static int send_png_rect(VncState *vs, int x, int y, int w, int h,
                         VncPalette *palette);

static bool tight_can_send_png_rect(VncState *vs, int w, int h)
{
    if (vs->tight->type != VNC_ENCODING_TIGHT_PNG) {
        return false;
    }

    if (surface_bytes_per_pixel(vs->vd->ds) == 1 ||
        vs->client_pf.bytes_per_pixel == 1) {
        return false;
    }

    return true;
}
#endif

/*
 * Code to guess if given rectangle is suitable for smooth image
 * compression (by applying "gradient" filter or JPEG coder).
 */

static unsigned int
tight_detect_smooth_image24(VncState *vs, int w, int h)
{
    int off;
    int x, y, d, dx;
    unsigned int c;
    unsigned int stats[256];
    int pixels = 0;
    int pix, left[3];
    unsigned int errors;
    unsigned char *buf = vs->tight->tight.buffer;

    /*
     * If client is big-endian, color samples begin from the second
     * byte (offset 1) of a 32-bit pixel value.
     */
    off = vs->client_be;

    memset(stats, 0, sizeof (stats));

    for (y = 0, x = 0; y < h && x < w;) {
        for (d = 0; d < h - y && d < w - x - VNC_TIGHT_DETECT_SUBROW_WIDTH;
             d++) {
            for (c = 0; c < 3; c++) {
                left[c] = buf[((y+d)*w+x+d)*4+off+c] & 0xFF;
            }
            for (dx = 1; dx <= VNC_TIGHT_DETECT_SUBROW_WIDTH; dx++) {
                for (c = 0; c < 3; c++) {
                    pix = buf[((y+d)*w+x+d+dx)*4+off+c] & 0xFF;
                    stats[abs(pix - left[c])]++;
                    left[c] = pix;
                }
                pixels++;
            }
        }
        if (w > h) {
            x += h;
            y = 0;
        } else {
            x = 0;
            y += w;
        }
    }

    if (pixels == 0) {
        return 0;
    }

    /* 95% smooth or more ... */
    if (stats[0] * 33 / pixels >= 95) {
        return 0;
    }

    errors = 0;
    for (c = 1; c < 8; c++) {
        errors += stats[c] * (c * c);
        if (stats[c] == 0 || stats[c] > stats[c-1] * 2) {
            return 0;
        }
    }
    for (; c < 256; c++) {
        errors += stats[c] * (c * c);
    }
    errors /= (pixels * 3 - stats[0]);

    return errors;
}

#define DEFINE_DETECT_FUNCTION(bpp)                                     \
                                                                        \
    static unsigned int                                                 \
    tight_detect_smooth_image##bpp(VncState *vs, int w, int h) {        \
        bool endian;                                                    \
        uint##bpp##_t pix;                                              \
        int max[3], shift[3];                                           \
        int x, y, d, dx;                                                \
        unsigned int c;                                                 \
        unsigned int stats[256];                                        \
        int pixels = 0;                                                 \
        int sample, sum, left[3];                                       \
        unsigned int errors;                                            \
        unsigned char *buf = vs->tight->tight.buffer;                    \
                                                                        \
        endian = 0; /* FIXME */                                         \
                                                                        \
                                                                        \
        max[0] = vs->client_pf.rmax;                                  \
        max[1] = vs->client_pf.gmax;                                  \
        max[2] = vs->client_pf.bmax;                                  \
        shift[0] = vs->client_pf.rshift;                              \
        shift[1] = vs->client_pf.gshift;                              \
        shift[2] = vs->client_pf.bshift;                              \
                                                                        \
        memset(stats, 0, sizeof(stats));                                \
                                                                        \
        y = 0, x = 0;                                                   \
        while (y < h && x < w) {                                        \
            for (d = 0; d < h - y &&                                    \
                     d < w - x - VNC_TIGHT_DETECT_SUBROW_WIDTH; d++) {  \
                pix = ((uint##bpp##_t *)buf)[(y+d)*w+x+d];              \
                if (endian) {                                           \
                    pix = bswap##bpp(pix);                              \
                }                                                       \
                for (c = 0; c < 3; c++) {                               \
                    left[c] = (int)(pix >> shift[c] & max[c]);          \
                }                                                       \
                for (dx = 1; dx <= VNC_TIGHT_DETECT_SUBROW_WIDTH;       \
                     dx++) {                                            \
                    pix = ((uint##bpp##_t *)buf)[(y+d)*w+x+d+dx];       \
                    if (endian) {                                       \
                        pix = bswap##bpp(pix);                          \
                    }                                                   \
                    sum = 0;                                            \
                    for (c = 0; c < 3; c++) {                           \
                        sample = (int)(pix >> shift[c] & max[c]);       \
                        sum += abs(sample - left[c]);                   \
                        left[c] = sample;                               \
                    }                                                   \
                    if (sum > 255) {                                    \
                        sum = 255;                                      \
                    }                                                   \
                    stats[sum]++;                                       \
                    pixels++;                                           \
                }                                                       \
            }                                                           \
            if (w > h) {                                                \
                x += h;                                                 \
                y = 0;                                                  \
            } else {                                                    \
                x = 0;                                                  \
                y += w;                                                 \
            }                                                           \
        }                                                               \
        if (pixels == 0) {                                              \
            return 0;                                                   \
        }                                                               \
        if ((stats[0] + stats[1]) * 100 / pixels >= 90) {               \
            return 0;                                                   \
        }                                                               \
                                                                        \
        errors = 0;                                                     \
        for (c = 1; c < 8; c++) {                                       \
            errors += stats[c] * (c * c);                               \
            if (stats[c] == 0 || stats[c] > stats[c-1] * 2) {           \
                return 0;                                               \
            }                                                           \
        }                                                               \
        for (; c < 256; c++) {                                          \
            errors += stats[c] * (c * c);                               \
        }                                                               \
        errors /= (pixels - stats[0]);                                  \
                                                                        \
        return errors;                                                  \
    }

DEFINE_DETECT_FUNCTION(16)
DEFINE_DETECT_FUNCTION(32)

static int
tight_detect_smooth_image(VncState *vs, int w, int h)
{
    unsigned int errors;
    int compression = vs->tight->compression;
    int quality = vs->tight->quality;

    if (!vs->vd->lossy) {
        return 0;
    }

    if (surface_bytes_per_pixel(vs->vd->ds) == 1 ||
        vs->client_pf.bytes_per_pixel == 1 ||
        w < VNC_TIGHT_DETECT_MIN_WIDTH || h < VNC_TIGHT_DETECT_MIN_HEIGHT) {
        return 0;
    }

    if (vs->tight->quality != (uint8_t)-1) {
        if (w * h < VNC_TIGHT_JPEG_MIN_RECT_SIZE) {
            return 0;
        }
    } else {
        if (w * h < tight_conf[compression].gradient_min_rect_size) {
            return 0;
        }
    }

    if (vs->client_pf.bytes_per_pixel == 4) {
        if (vs->tight->pixel24) {
            errors = tight_detect_smooth_image24(vs, w, h);
            if (vs->tight->quality != (uint8_t)-1) {
                return (errors < tight_conf[quality].jpeg_threshold24);
            }
            return (errors < tight_conf[compression].gradient_threshold24);
        } else {
            errors = tight_detect_smooth_image32(vs, w, h);
        }
    } else {
        errors = tight_detect_smooth_image16(vs, w, h);
    }
    if (quality != (uint8_t)-1) {
        return (errors < tight_conf[quality].jpeg_threshold);
    }
    return (errors < tight_conf[compression].gradient_threshold);
}

/*
 * Code to determine how many different colors used in rectangle.
 */
#define DEFINE_FILL_PALETTE_FUNCTION(bpp)                               \
                                                                        \
    static int                                                          \
    tight_fill_palette##bpp(VncState *vs, int x, int y,                 \
                            int max, size_t count,                      \
                            uint32_t *bg, uint32_t *fg,                 \
                            VncPalette *palette) {                      \
        uint##bpp##_t *data;                                            \
        uint##bpp##_t c0, c1, ci;                                       \
        int i, n0, n1;                                                  \
                                                                        \
        data = (uint##bpp##_t *)vs->tight->tight.buffer;                \
                                                                        \
        c0 = data[0];                                                   \
        i = 1;                                                          \
        while (i < count && data[i] == c0)                              \
            i++;                                                        \
        if (i >= count) {                                               \
            *bg = *fg = c0;                                             \
            return 1;                                                   \
        }                                                               \
                                                                        \
        if (max < 2) {                                                  \
            return 0;                                                   \
        }                                                               \
                                                                        \
        n0 = i;                                                         \
        c1 = data[i];                                                   \
        n1 = 0;                                                         \
        for (i++; i < count; i++) {                                     \
            ci = data[i];                                               \
            if (ci == c0) {                                             \
                n0++;                                                   \
            } else if (ci == c1) {                                      \
                n1++;                                                   \
            } else                                                      \
                break;                                                  \
        }                                                               \
        if (i >= count) {                                               \
            if (n0 > n1) {                                              \
                *bg = (uint32_t)c0;                                     \
                *fg = (uint32_t)c1;                                     \
            } else {                                                    \
                *bg = (uint32_t)c1;                                     \
                *fg = (uint32_t)c0;                                     \
            }                                                           \
            return 2;                                                   \
        }                                                               \
                                                                        \
        if (max == 2) {                                                 \
            return 0;                                                   \
        }                                                               \
                                                                        \
        palette_init(palette, max, bpp);                                \
        palette_put(palette, c0);                                       \
        palette_put(palette, c1);                                       \
        palette_put(palette, ci);                                       \
                                                                        \
        for (i++; i < count; i++) {                                     \
            if (data[i] == ci) {                                        \
                continue;                                               \
            } else {                                                    \
                ci = data[i];                                           \
                if (!palette_put(palette, (uint32_t)ci)) {              \
                    return 0;                                           \
                }                                                       \
            }                                                           \
        }                                                               \
                                                                        \
        return palette_size(palette);                                   \
    }

DEFINE_FILL_PALETTE_FUNCTION(8)
DEFINE_FILL_PALETTE_FUNCTION(16)
DEFINE_FILL_PALETTE_FUNCTION(32)

static int tight_fill_palette(VncState *vs, int x, int y,
                              size_t count, uint32_t *bg, uint32_t *fg,
                              VncPalette *palette)
{
    int max;

    max = count / tight_conf[vs->tight->compression].idx_max_colors_divisor;
    if (max < 2 &&
        count >= tight_conf[vs->tight->compression].mono_min_rect_size) {
        max = 2;
    }
    if (max >= 256) {
        max = 256;
    }

    switch (vs->client_pf.bytes_per_pixel) {
    case 4:
        return tight_fill_palette32(vs, x, y, max, count, bg, fg, palette);
    case 2:
        return tight_fill_palette16(vs, x, y, max, count, bg, fg, palette);
    default:
        max = 2;
        return tight_fill_palette8(vs, x, y, max, count, bg, fg, palette);
    }
    return 0;
}

/*
 * Converting truecolor samples into palette indices.
 */
#define DEFINE_IDX_ENCODE_FUNCTION(bpp)                                 \
                                                                        \
    static void                                                         \
    tight_encode_indexed_rect##bpp(uint8_t *buf, int count,             \
                                   VncPalette *palette) {               \
        uint##bpp##_t *src;                                             \
        uint##bpp##_t rgb;                                              \
        int i, rep;                                                     \
        uint8_t idx;                                                    \
                                                                        \
        src = (uint##bpp##_t *) buf;                                    \
                                                                        \
        for (i = 0; i < count; ) {                                      \
                                                                        \
            rgb = *src++;                                               \
            i++;                                                        \
            rep = 0;                                                    \
            while (i < count && *src == rgb) {                          \
                rep++, src++, i++;                                      \
            }                                                           \
            idx = palette_idx(palette, rgb);                            \
            /*                                                          \
             * Should never happen, but don't break everything          \
             * if it does, use the first color instead                  \
             */                                                         \
            if (idx == (uint8_t)-1) {                                   \
                idx = 0;                                                \
            }                                                           \
            while (rep >= 0) {                                          \
                *buf++ = idx;                                           \
                rep--;                                                  \
            }                                                           \
        }                                                               \
    }

DEFINE_IDX_ENCODE_FUNCTION(16)
DEFINE_IDX_ENCODE_FUNCTION(32)

#define DEFINE_MONO_ENCODE_FUNCTION(bpp)                                \
                                                                        \
    static void                                                         \
    tight_encode_mono_rect##bpp(uint8_t *buf, int w, int h,             \
                                uint##bpp##_t bg, uint##bpp##_t fg) {   \
        uint##bpp##_t *ptr;                                             \
        unsigned int value, mask;                                       \
        int aligned_width;                                              \
        int x, y, bg_bits;                                              \
                                                                        \
        ptr = (uint##bpp##_t *) buf;                                    \
        aligned_width = w - w % 8;                                      \
                                                                        \
        for (y = 0; y < h; y++) {                                       \
            for (x = 0; x < aligned_width; x += 8) {                    \
                for (bg_bits = 0; bg_bits < 8; bg_bits++) {             \
                    if (*ptr++ != bg) {                                 \
                        break;                                          \
                    }                                                   \
                }                                                       \
                if (bg_bits == 8) {                                     \
                    *buf++ = 0;                                         \
                    continue;                                           \
                }                                                       \
                mask = 0x80 >> bg_bits;                                 \
                value = mask;                                           \
                for (bg_bits++; bg_bits < 8; bg_bits++) {               \
                    mask >>= 1;                                         \
                    if (*ptr++ != bg) {                                 \
                        value |= mask;                                  \
                    }                                                   \
                }                                                       \
                *buf++ = (uint8_t)value;                                \
            }                                                           \
                                                                        \
            mask = 0x80;                                                \
            value = 0;                                                  \
            if (x >= w) {                                               \
                continue;                                               \
            }                                                           \
                                                                        \
            for (; x < w; x++) {                                        \
                if (*ptr++ != bg) {                                     \
                    value |= mask;                                      \
                }                                                       \
                mask >>= 1;                                             \
            }                                                           \
            *buf++ = (uint8_t)value;                                    \
        }                                                               \
    }

DEFINE_MONO_ENCODE_FUNCTION(8)
DEFINE_MONO_ENCODE_FUNCTION(16)
DEFINE_MONO_ENCODE_FUNCTION(32)

/*
 * ``Gradient'' filter for 24-bit color samples.
 * Should be called only when redMax, greenMax and blueMax are 255.
 * Color components assumed to be byte-aligned.
 */

static void
tight_filter_gradient24(VncState *vs, uint8_t *buf, int w, int h)
{
    uint32_t *buf32;
    uint32_t pix32;
    int shift[3];
    int *prev;
    int here[3], upper[3], left[3], upperleft[3];
    int prediction;
    int x, y, c;

    buf32 = (uint32_t *)buf;
    memset(vs->tight->gradient.buffer, 0, w * 3 * sizeof(int));

    if (1 /* FIXME */) {
        shift[0] = vs->client_pf.rshift;
        shift[1] = vs->client_pf.gshift;
        shift[2] = vs->client_pf.bshift;
    } else {
        shift[0] = 24 - vs->client_pf.rshift;
        shift[1] = 24 - vs->client_pf.gshift;
        shift[2] = 24 - vs->client_pf.bshift;
    }

    for (y = 0; y < h; y++) {
        for (c = 0; c < 3; c++) {
            upper[c] = 0;
            here[c] = 0;
        }
        prev = (int *)vs->tight->gradient.buffer;
        for (x = 0; x < w; x++) {
            pix32 = *buf32++;
            for (c = 0; c < 3; c++) {
                upperleft[c] = upper[c];
                left[c] = here[c];
                upper[c] = *prev;
                here[c] = (int)(pix32 >> shift[c] & 0xFF);
                *prev++ = here[c];

                prediction = left[c] + upper[c] - upperleft[c];
                if (prediction < 0) {
                    prediction = 0;
                } else if (prediction > 0xFF) {
                    prediction = 0xFF;
                }
                *buf++ = (char)(here[c] - prediction);
            }
        }
    }
}


/*
 * ``Gradient'' filter for other color depths.
 */

#define DEFINE_GRADIENT_FILTER_FUNCTION(bpp)                            \
                                                                        \
    static void                                                         \
    tight_filter_gradient##bpp(VncState *vs, uint##bpp##_t *buf,        \
                               int w, int h) {                          \
        uint##bpp##_t pix, diff;                                        \
        bool endian;                                                    \
        int *prev;                                                      \
        int max[3], shift[3];                                           \
        int here[3], upper[3], left[3], upperleft[3];                   \
        int prediction;                                                 \
        int x, y, c;                                                    \
                                                                        \
        memset(vs->tight->gradient.buffer, 0, w * 3 * sizeof(int));     \
                                                                        \
        endian = 0; /* FIXME */                                         \
                                                                        \
        max[0] = vs->client_pf.rmax;                                    \
        max[1] = vs->client_pf.gmax;                                    \
        max[2] = vs->client_pf.bmax;                                    \
        shift[0] = vs->client_pf.rshift;                                \
        shift[1] = vs->client_pf.gshift;                                \
        shift[2] = vs->client_pf.bshift;                                \
                                                                        \
        for (y = 0; y < h; y++) {                                       \
            for (c = 0; c < 3; c++) {                                   \
                upper[c] = 0;                                           \
                here[c] = 0;                                            \
            }                                                           \
            prev = (int *)vs->tight->gradient.buffer;                    \
            for (x = 0; x < w; x++) {                                   \
                pix = *buf;                                             \
                if (endian) {                                           \
                    pix = bswap##bpp(pix);                              \
                }                                                       \
                diff = 0;                                               \
                for (c = 0; c < 3; c++) {                               \
                    upperleft[c] = upper[c];                            \
                    left[c] = here[c];                                  \
                    upper[c] = *prev;                                   \
                    here[c] = (int)(pix >> shift[c] & max[c]);          \
                    *prev++ = here[c];                                  \
                                                                        \
                    prediction = left[c] + upper[c] - upperleft[c];     \
                    if (prediction < 0) {                               \
                        prediction = 0;                                 \
                    } else if (prediction > max[c]) {                   \
                        prediction = max[c];                            \
                    }                                                   \
                    diff |= ((here[c] - prediction) & max[c])           \
                        << shift[c];                                    \
                }                                                       \
                if (endian) {                                           \
                    diff = bswap##bpp(diff);                            \
                }                                                       \
                *buf++ = diff;                                          \
            }                                                           \
        }                                                               \
    }

DEFINE_GRADIENT_FILTER_FUNCTION(16)
DEFINE_GRADIENT_FILTER_FUNCTION(32)

/*
 * Check if a rectangle is all of the same color. If needSameColor is
 * set to non-zero, then also check that its color equals to the
 * *colorPtr value. The result is 1 if the test is successful, and in
 * that case new color will be stored in *colorPtr.
 */

static bool
check_solid_tile32(VncState *vs, int x, int y, int w, int h,
                   uint32_t *color, bool samecolor)
{
    VncDisplay *vd = vs->vd;
    uint32_t *fbptr;
    uint32_t c;
    int dx, dy;

    fbptr = vnc_server_fb_ptr(vd, x, y);

    c = *fbptr;
    if (samecolor && (uint32_t)c != *color) {
        return false;
    }

    for (dy = 0; dy < h; dy++) {
        for (dx = 0; dx < w; dx++) {
            if (c != fbptr[dx]) {
                return false;
            }
        }
        fbptr = (uint32_t *)
            ((uint8_t *)fbptr + vnc_server_fb_stride(vd));
    }

    *color = (uint32_t)c;
    return true;
}

static bool check_solid_tile(VncState *vs, int x, int y, int w, int h,
                             uint32_t* color, bool samecolor)
{
    QEMU_BUILD_BUG_ON(VNC_SERVER_FB_BYTES != 4);
    return check_solid_tile32(vs, x, y, w, h, color, samecolor);
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
    z_streamp zstream = &vs->tight->stream[stream_id];

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

        vs->tight->levels[stream_id] = level;
        zstream->opaque = vs;
    }

    if (vs->tight->levels[stream_id] != level) {
        if (deflateParams(zstream, level, strategy) != Z_OK) {
            return -1;
        }
        vs->tight->levels[stream_id] = level;
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
    z_streamp zstream = &vs->tight->stream[stream_id];
    int previous_out;

    if (bytes < VNC_TIGHT_MIN_TO_COMPRESS) {
        vnc_write(vs, vs->tight->tight.buffer, vs->tight->tight.offset);
        return bytes;
    }

    if (tight_init_stream(vs, stream_id, level, strategy)) {
        return -1;
    }

    /* reserve memory in output buffer */
    buffer_reserve(&vs->tight->zlib, bytes + 64);

    /* set pointers */
    zstream->next_in = vs->tight->tight.buffer;
    zstream->avail_in = vs->tight->tight.offset;
    zstream->next_out = vs->tight->zlib.buffer + vs->tight->zlib.offset;
    zstream->avail_out = vs->tight->zlib.capacity - vs->tight->zlib.offset;
    previous_out = zstream->avail_out;
    zstream->data_type = Z_BINARY;

    /* start encoding */
    if (deflate(zstream, Z_SYNC_FLUSH) != Z_OK) {
        fprintf(stderr, "VNC: error during tight compression\n");
        return -1;
    }

    vs->tight->zlib.offset = vs->tight->zlib.capacity - zstream->avail_out;
    /* ...how much data has actually been produced by deflate() */
    bytes = previous_out - zstream->avail_out;

    tight_send_compact_size(vs, bytes);
    vnc_write(vs, vs->tight->zlib.buffer, bytes);

    buffer_reset(&vs->tight->zlib);

    return bytes;
}

/*
 * Subencoding implementations.
 */
static void tight_pack24(VncState *vs, uint8_t *buf, size_t count, size_t *ret)
{
    uint8_t *buf8;
    uint32_t pix;
    int rshift, gshift, bshift;

    buf8 = buf;

    if (1 /* FIXME */) {
        rshift = vs->client_pf.rshift;
        gshift = vs->client_pf.gshift;
        bshift = vs->client_pf.bshift;
    } else {
        rshift = 24 - vs->client_pf.rshift;
        gshift = 24 - vs->client_pf.gshift;
        bshift = 24 - vs->client_pf.bshift;
    }

    if (ret) {
        *ret = count * 3;
    }

    while (count--) {
        pix = ldl_he_p(buf8);
        *buf++ = (char)(pix >> rshift);
        *buf++ = (char)(pix >> gshift);
        *buf++ = (char)(pix >> bshift);
        buf8 += 4;
    }
}

static int send_full_color_rect(VncState *vs, int x, int y, int w, int h)
{
    int stream = 0;
    ssize_t bytes;

#ifdef CONFIG_VNC_PNG
    if (tight_can_send_png_rect(vs, w, h)) {
        return send_png_rect(vs, x, y, w, h, NULL);
    }
#endif

    vnc_write_u8(vs, stream << 4); /* no flushing, no filter */

    if (vs->tight->pixel24) {
        tight_pack24(vs, vs->tight->tight.buffer, w * h,
                     &vs->tight->tight.offset);
        bytes = 3;
    } else {
        bytes = vs->client_pf.bytes_per_pixel;
    }

    bytes = tight_compress_data(vs, stream, w * h * bytes,
                            tight_conf[vs->tight->compression].raw_zlib_level,
                            Z_DEFAULT_STRATEGY);

    return (bytes >= 0);
}

static int send_solid_rect(VncState *vs)
{
    size_t bytes;

    vnc_write_u8(vs, VNC_TIGHT_FILL << 4); /* no flushing, no filter */

    if (vs->tight->pixel24) {
        tight_pack24(vs, vs->tight->tight.buffer, 1, &vs->tight->tight.offset);
        bytes = 3;
    } else {
        bytes = vs->client_pf.bytes_per_pixel;
    }

    vnc_write(vs, vs->tight->tight.buffer, bytes);
    return 1;
}

static int send_mono_rect(VncState *vs, int x, int y,
                          int w, int h, uint32_t bg, uint32_t fg)
{
    ssize_t bytes;
    int stream = 1;
    int level = tight_conf[vs->tight->compression].mono_zlib_level;

#ifdef CONFIG_VNC_PNG
    if (tight_can_send_png_rect(vs, w, h)) {
        int ret;
        int bpp = vs->client_pf.bytes_per_pixel * 8;
        VncPalette *palette = palette_new(2, bpp);

        palette_put(palette, bg);
        palette_put(palette, fg);
        ret = send_png_rect(vs, x, y, w, h, palette);
        palette_destroy(palette);
        return ret;
    }
#endif

    bytes = DIV_ROUND_UP(w, 8) * h;

    vnc_write_u8(vs, (stream | VNC_TIGHT_EXPLICIT_FILTER) << 4);
    vnc_write_u8(vs, VNC_TIGHT_FILTER_PALETTE);
    vnc_write_u8(vs, 1);

    switch (vs->client_pf.bytes_per_pixel) {
    case 4:
    {
        uint32_t buf[2] = {bg, fg};
        size_t ret = sizeof (buf);

        if (vs->tight->pixel24) {
            tight_pack24(vs, (unsigned char*)buf, 2, &ret);
        }
        vnc_write(vs, buf, ret);

        tight_encode_mono_rect32(vs->tight->tight.buffer, w, h, bg, fg);
        break;
    }
    case 2:
        vnc_write(vs, &bg, 2);
        vnc_write(vs, &fg, 2);
        tight_encode_mono_rect16(vs->tight->tight.buffer, w, h, bg, fg);
        break;
    default:
        vnc_write_u8(vs, bg);
        vnc_write_u8(vs, fg);
        tight_encode_mono_rect8(vs->tight->tight.buffer, w, h, bg, fg);
        break;
    }
    vs->tight->tight.offset = bytes;

    bytes = tight_compress_data(vs, stream, bytes, level, Z_DEFAULT_STRATEGY);
    return (bytes >= 0);
}

struct palette_cb_priv {
    VncState *vs;
    uint8_t *header;
#ifdef CONFIG_VNC_PNG
    png_colorp png_palette;
#endif
};

static void write_palette(int idx, uint32_t color, void *opaque)
{
    struct palette_cb_priv *priv = opaque;
    VncState *vs = priv->vs;
    uint32_t bytes = vs->client_pf.bytes_per_pixel;

    if (bytes == 4) {
        ((uint32_t*)priv->header)[idx] = color;
    } else {
        ((uint16_t*)priv->header)[idx] = color;
    }
}

static bool send_gradient_rect(VncState *vs, int x, int y, int w, int h)
{
    int stream = 3;
    int level = tight_conf[vs->tight->compression].gradient_zlib_level;
    ssize_t bytes;

    if (vs->client_pf.bytes_per_pixel == 1) {
        return send_full_color_rect(vs, x, y, w, h);
    }

    vnc_write_u8(vs, (stream | VNC_TIGHT_EXPLICIT_FILTER) << 4);
    vnc_write_u8(vs, VNC_TIGHT_FILTER_GRADIENT);

    buffer_reserve(&vs->tight->gradient, w * 3 * sizeof(int));

    if (vs->tight->pixel24) {
        tight_filter_gradient24(vs, vs->tight->tight.buffer, w, h);
        bytes = 3;
    } else if (vs->client_pf.bytes_per_pixel == 4) {
        tight_filter_gradient32(vs, (uint32_t *)vs->tight->tight.buffer, w, h);
        bytes = 4;
    } else {
        tight_filter_gradient16(vs, (uint16_t *)vs->tight->tight.buffer, w, h);
        bytes = 2;
    }

    buffer_reset(&vs->tight->gradient);

    bytes = w * h * bytes;
    vs->tight->tight.offset = bytes;

    bytes = tight_compress_data(vs, stream, bytes,
                                level, Z_FILTERED);
    return (bytes >= 0);
}

static int send_palette_rect(VncState *vs, int x, int y,
                             int w, int h, VncPalette *palette)
{
    int stream = 2;
    int level = tight_conf[vs->tight->compression].idx_zlib_level;
    int colors;
    ssize_t bytes;

#ifdef CONFIG_VNC_PNG
    if (tight_can_send_png_rect(vs, w, h)) {
        return send_png_rect(vs, x, y, w, h, palette);
    }
#endif

    colors = palette_size(palette);

    vnc_write_u8(vs, (stream | VNC_TIGHT_EXPLICIT_FILTER) << 4);
    vnc_write_u8(vs, VNC_TIGHT_FILTER_PALETTE);
    vnc_write_u8(vs, colors - 1);

    switch (vs->client_pf.bytes_per_pixel) {
    case 4:
    {
        size_t old_offset, offset;
        uint32_t header[palette_size(palette)];
        struct palette_cb_priv priv = { vs, (uint8_t *)header };

        old_offset = vs->output.offset;
        palette_iter(palette, write_palette, &priv);
        vnc_write(vs, header, sizeof(header));

        if (vs->tight->pixel24) {
            tight_pack24(vs, vs->output.buffer + old_offset, colors, &offset);
            vs->output.offset = old_offset + offset;
        }

        tight_encode_indexed_rect32(vs->tight->tight.buffer, w * h, palette);
        break;
    }
    case 2:
    {
        uint16_t header[palette_size(palette)];
        struct palette_cb_priv priv = { vs, (uint8_t *)header };

        palette_iter(palette, write_palette, &priv);
        vnc_write(vs, header, sizeof(header));
        tight_encode_indexed_rect16(vs->tight->tight.buffer, w * h, palette);
        break;
    }
    default:
        return -1; /* No palette for 8bits colors */
        break;
    }
    bytes = w * h;
    vs->tight->tight.offset = bytes;

    bytes = tight_compress_data(vs, stream, bytes,
                                level, Z_DEFAULT_STRATEGY);
    return (bytes >= 0);
}

/*
 * JPEG compression stuff.
 */
#ifdef CONFIG_VNC_JPEG
/*
 * Destination manager implementation for JPEG library.
 */

/* This is called once per encoding */
static void jpeg_init_destination(j_compress_ptr cinfo)
{
    VncState *vs = cinfo->client_data;
    Buffer *buffer = &vs->tight->jpeg;

    cinfo->dest->next_output_byte = (JOCTET *)buffer->buffer + buffer->offset;
    cinfo->dest->free_in_buffer = (size_t)(buffer->capacity - buffer->offset);
}

/* This is called when we ran out of buffer (shouldn't happen!) */
static boolean jpeg_empty_output_buffer(j_compress_ptr cinfo)
{
    VncState *vs = cinfo->client_data;
    Buffer *buffer = &vs->tight->jpeg;

    buffer->offset = buffer->capacity;
    buffer_reserve(buffer, 2048);
    jpeg_init_destination(cinfo);
    return TRUE;
}

/* This is called when we are done processing data */
static void jpeg_term_destination(j_compress_ptr cinfo)
{
    VncState *vs = cinfo->client_data;
    Buffer *buffer = &vs->tight->jpeg;

    buffer->offset = buffer->capacity - cinfo->dest->free_in_buffer;
}

static int send_jpeg_rect(VncState *vs, int x, int y, int w, int h, int quality)
{
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    struct jpeg_destination_mgr manager;
    pixman_image_t *linebuf;
    JSAMPROW row[1];
    uint8_t *buf;
    int dy;

    if (surface_bytes_per_pixel(vs->vd->ds) == 1) {
        return send_full_color_rect(vs, x, y, w, h);
    }

    buffer_reserve(&vs->tight->jpeg, 2048);

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    cinfo.client_data = vs;
    cinfo.image_width = w;
    cinfo.image_height = h;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, true);

    manager.init_destination = jpeg_init_destination;
    manager.empty_output_buffer = jpeg_empty_output_buffer;
    manager.term_destination = jpeg_term_destination;
    cinfo.dest = &manager;

    jpeg_start_compress(&cinfo, true);

    linebuf = qemu_pixman_linebuf_create(PIXMAN_BE_r8g8b8, w);
    buf = (uint8_t *)pixman_image_get_data(linebuf);
    row[0] = buf;
    for (dy = 0; dy < h; dy++) {
        qemu_pixman_linebuf_fill(linebuf, vs->vd->server, w, x, y + dy);
        jpeg_write_scanlines(&cinfo, row, 1);
    }
    qemu_pixman_image_unref(linebuf);

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    vnc_write_u8(vs, VNC_TIGHT_JPEG << 4);

    tight_send_compact_size(vs, vs->tight->jpeg.offset);
    vnc_write(vs, vs->tight->jpeg.buffer, vs->tight->jpeg.offset);
    buffer_reset(&vs->tight->jpeg);

    return 1;
}
#endif /* CONFIG_VNC_JPEG */

/*
 * PNG compression stuff.
 */
#ifdef CONFIG_VNC_PNG
static void write_png_palette(int idx, uint32_t pix, void *opaque)
{
    struct palette_cb_priv *priv = opaque;
    VncState *vs = priv->vs;
    png_colorp color = &priv->png_palette[idx];

    if (vs->tight->pixel24)
    {
        color->red = (pix >> vs->client_pf.rshift) & vs->client_pf.rmax;
        color->green = (pix >> vs->client_pf.gshift) & vs->client_pf.gmax;
        color->blue = (pix >> vs->client_pf.bshift) & vs->client_pf.bmax;
    }
    else
    {
        int red, green, blue;

        red = (pix >> vs->client_pf.rshift) & vs->client_pf.rmax;
        green = (pix >> vs->client_pf.gshift) & vs->client_pf.gmax;
        blue = (pix >> vs->client_pf.bshift) & vs->client_pf.bmax;
        color->red = ((red * 255 + vs->client_pf.rmax / 2) /
                      vs->client_pf.rmax);
        color->green = ((green * 255 + vs->client_pf.gmax / 2) /
                        vs->client_pf.gmax);
        color->blue = ((blue * 255 + vs->client_pf.bmax / 2) /
                       vs->client_pf.bmax);
    }
}

static void png_write_data(png_structp png_ptr, png_bytep data,
                           png_size_t length)
{
    VncState *vs = png_get_io_ptr(png_ptr);

    buffer_reserve(&vs->tight->png, vs->tight->png.offset + length);
    memcpy(vs->tight->png.buffer + vs->tight->png.offset, data, length);

    vs->tight->png.offset += length;
}

static void png_flush_data(png_structp png_ptr)
{
}

static void *vnc_png_malloc(png_structp png_ptr, png_size_t size)
{
    return g_malloc(size);
}

static void vnc_png_free(png_structp png_ptr, png_voidp ptr)
{
    g_free(ptr);
}

static int send_png_rect(VncState *vs, int x, int y, int w, int h,
                         VncPalette *palette)
{
    png_byte color_type;
    png_structp png_ptr;
    png_infop info_ptr;
    png_colorp png_palette = NULL;
    pixman_image_t *linebuf;
    int level = tight_png_conf[vs->tight->compression].png_zlib_level;
    int filters = tight_png_conf[vs->tight->compression].png_filters;
    uint8_t *buf;
    int dy;

    png_ptr = png_create_write_struct_2(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL,
                                        NULL, vnc_png_malloc, vnc_png_free);

    if (png_ptr == NULL)
        return -1;

    info_ptr = png_create_info_struct(png_ptr);

    if (info_ptr == NULL) {
        png_destroy_write_struct(&png_ptr, NULL);
        return -1;
    }

    png_set_write_fn(png_ptr, (void *) vs, png_write_data, png_flush_data);
    png_set_compression_level(png_ptr, level);
    png_set_filter(png_ptr, PNG_FILTER_TYPE_DEFAULT, filters);

    if (palette) {
        color_type = PNG_COLOR_TYPE_PALETTE;
    } else {
        color_type = PNG_COLOR_TYPE_RGB;
    }

    png_set_IHDR(png_ptr, info_ptr, w, h,
                 8, color_type, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        struct palette_cb_priv priv;

        png_palette = png_malloc(png_ptr, sizeof(*png_palette) *
                                 palette_size(palette));

        priv.vs = vs;
        priv.png_palette = png_palette;
        palette_iter(palette, write_png_palette, &priv);

        png_set_PLTE(png_ptr, info_ptr, png_palette, palette_size(palette));

        if (vs->client_pf.bytes_per_pixel == 4) {
            tight_encode_indexed_rect32(vs->tight->tight.buffer, w * h,
                                        palette);
        } else {
            tight_encode_indexed_rect16(vs->tight->tight.buffer, w * h,
                                        palette);
        }
    }

    png_write_info(png_ptr, info_ptr);

    buffer_reserve(&vs->tight->png, 2048);
    linebuf = qemu_pixman_linebuf_create(PIXMAN_BE_r8g8b8, w);
    buf = (uint8_t *)pixman_image_get_data(linebuf);
    for (dy = 0; dy < h; dy++)
    {
        if (color_type == PNG_COLOR_TYPE_PALETTE) {
            memcpy(buf, vs->tight->tight.buffer + (dy * w), w);
        } else {
            qemu_pixman_linebuf_fill(linebuf, vs->vd->server, w, x, y + dy);
        }
        png_write_row(png_ptr, buf);
    }
    qemu_pixman_image_unref(linebuf);

    png_write_end(png_ptr, NULL);

    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_free(png_ptr, png_palette);
    }

    png_destroy_write_struct(&png_ptr, &info_ptr);

    vnc_write_u8(vs, VNC_TIGHT_PNG << 4);

    tight_send_compact_size(vs, vs->tight->png.offset);
    vnc_write(vs, vs->tight->png.buffer, vs->tight->png.offset);
    buffer_reset(&vs->tight->png);
    return 1;
}
#endif /* CONFIG_VNC_PNG */

static void vnc_tight_start(VncState *vs)
{
    buffer_reset(&vs->tight->tight);

    // make the output buffer be the zlib buffer, so we can compress it later
    vs->tight->tmp = vs->output;
    vs->output = vs->tight->tight;
}

static void vnc_tight_stop(VncState *vs)
{
    // switch back to normal output/zlib buffers
    vs->tight->tight = vs->output;
    vs->output = vs->tight->tmp;
}

static int send_sub_rect_nojpeg(VncState *vs, int x, int y, int w, int h,
                                int bg, int fg, int colors, VncPalette *palette)
{
    int ret;

    if (colors == 0) {
        if (tight_detect_smooth_image(vs, w, h)) {
            ret = send_gradient_rect(vs, x, y, w, h);
        } else {
            ret = send_full_color_rect(vs, x, y, w, h);
        }
    } else if (colors == 1) {
        ret = send_solid_rect(vs);
    } else if (colors == 2) {
        ret = send_mono_rect(vs, x, y, w, h, bg, fg);
    } else if (colors <= 256) {
        ret = send_palette_rect(vs, x, y, w, h, palette);
    } else {
        ret = 0;
    }
    return ret;
}

#ifdef CONFIG_VNC_JPEG
static int send_sub_rect_jpeg(VncState *vs, int x, int y, int w, int h,
                              int bg, int fg, int colors,
                              VncPalette *palette, bool force)
{
    int ret;

    if (colors == 0) {
        if (force || (tight_jpeg_conf[vs->tight->quality].jpeg_full &&
                      tight_detect_smooth_image(vs, w, h))) {
            int quality = tight_conf[vs->tight->quality].jpeg_quality;

            ret = send_jpeg_rect(vs, x, y, w, h, quality);
        } else {
            ret = send_full_color_rect(vs, x, y, w, h);
        }
    } else if (colors == 1) {
        ret = send_solid_rect(vs);
    } else if (colors == 2) {
        ret = send_mono_rect(vs, x, y, w, h, bg, fg);
    } else if (colors <= 256) {
        if (force || (colors > 96 &&
                      tight_jpeg_conf[vs->tight->quality].jpeg_idx &&
                      tight_detect_smooth_image(vs, w, h))) {
            int quality = tight_conf[vs->tight->quality].jpeg_quality;

            ret = send_jpeg_rect(vs, x, y, w, h, quality);
        } else {
            ret = send_palette_rect(vs, x, y, w, h, palette);
        }
    } else {
        ret = 0;
    }
    return ret;
}
#endif

static __thread VncPalette *color_count_palette;
static __thread Notifier vnc_tight_cleanup_notifier;

static void vnc_tight_cleanup(Notifier *n, void *value)
{
    g_free(color_count_palette);
    color_count_palette = NULL;
}

static int send_sub_rect(VncState *vs, int x, int y, int w, int h)
{
    uint32_t bg = 0, fg = 0;
    int colors;
    int ret = 0;
#ifdef CONFIG_VNC_JPEG
    bool force_jpeg = false;
    bool allow_jpeg = true;
#endif

    if (!color_count_palette) {
        color_count_palette = g_malloc(sizeof(VncPalette));
        vnc_tight_cleanup_notifier.notify = vnc_tight_cleanup;
        qemu_thread_atexit_add(&vnc_tight_cleanup_notifier);
    }

    vnc_framebuffer_update(vs, x, y, w, h, vs->tight->type);

    vnc_tight_start(vs);
    vnc_raw_send_framebuffer_update(vs, x, y, w, h);
    vnc_tight_stop(vs);

#ifdef CONFIG_VNC_JPEG
    if (!vs->vd->non_adaptive && vs->tight->quality != (uint8_t)-1) {
        double freq = vnc_update_freq(vs, x, y, w, h);

        if (freq < tight_jpeg_conf[vs->tight->quality].jpeg_freq_min) {
            allow_jpeg = false;
        }
        if (freq >= tight_jpeg_conf[vs->tight->quality].jpeg_freq_threshold) {
            force_jpeg = true;
            vnc_sent_lossy_rect(vs, x, y, w, h);
        }
    }
#endif

    colors = tight_fill_palette(vs, x, y, w * h, &bg, &fg, color_count_palette);

#ifdef CONFIG_VNC_JPEG
    if (allow_jpeg && vs->tight->quality != (uint8_t)-1) {
        ret = send_sub_rect_jpeg(vs, x, y, w, h, bg, fg, colors,
                                 color_count_palette, force_jpeg);
    } else {
        ret = send_sub_rect_nojpeg(vs, x, y, w, h, bg, fg, colors,
                                   color_count_palette);
    }
#else
    ret = send_sub_rect_nojpeg(vs, x, y, w, h, bg, fg, colors,
                               color_count_palette);
#endif

    return ret;
}

static int send_sub_rect_solid(VncState *vs, int x, int y, int w, int h)
{
    vnc_framebuffer_update(vs, x, y, w, h, vs->tight->type);

    vnc_tight_start(vs);
    vnc_raw_send_framebuffer_update(vs, x, y, w, h);
    vnc_tight_stop(vs);

    return send_solid_rect(vs);
}

static int send_rect_simple(VncState *vs, int x, int y, int w, int h,
                            bool split)
{
    int max_size, max_width;
    int max_sub_width, max_sub_height;
    int dx, dy;
    int rw, rh;
    int n = 0;

    max_size = tight_conf[vs->tight->compression].max_rect_size;
    max_width = tight_conf[vs->tight->compression].max_rect_width;

    if (split && (w > max_width || w * h > max_size)) {
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
            n += send_rect_simple(vs, x, y, w, max_rows, true);
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
                n += send_rect_simple(vs, x, y, w, y_best-y, true);
            }
            if (x_best != x) {
                n += tight_send_framebuffer_update(vs, x, y_best,
                                                   x_best-x, h_best);
            }

            /* Send solid-color rectangle. */
            n += send_sub_rect_solid(vs, x_best, y_best, w_best, h_best);

            /* Send remaining rectangles (at right and bottom). */

            if (x_best + w_best != x + w) {
                n += tight_send_framebuffer_update(vs, x_best+w_best,
                                                   y_best,
                                                   w-(x_best-x)-w_best,
                                                   h_best);
            }
            if (y_best + h_best != y + h) {
                n += tight_send_framebuffer_update(vs, x, y_best+h_best,
                                                   w, h-(y_best-y)-h_best);
            }

            /* Return after all recursive calls are done. */
            return n;
        }
    }
    return n + send_rect_simple(vs, x, y, w, h, true);
}

static int tight_send_framebuffer_update(VncState *vs, int x, int y,
                                         int w, int h)
{
    int max_rows;

    if (vs->client_pf.bytes_per_pixel == 4 && vs->client_pf.rmax == 0xFF &&
        vs->client_pf.bmax == 0xFF && vs->client_pf.gmax == 0xFF) {
        vs->tight->pixel24 = true;
    } else {
        vs->tight->pixel24 = false;
    }

#ifdef CONFIG_VNC_JPEG
    if (vs->tight->quality != (uint8_t)-1) {
        double freq = vnc_update_freq(vs, x, y, w, h);

        if (freq > tight_jpeg_conf[vs->tight->quality].jpeg_freq_threshold) {
            return send_rect_simple(vs, x, y, w, h, false);
        }
    }
#endif

    if (w * h < VNC_TIGHT_MIN_SPLIT_RECT_SIZE) {
        return send_rect_simple(vs, x, y, w, h, true);
    }

    /* Calculate maximum number of rows in one non-solid rectangle. */

    max_rows = tight_conf[vs->tight->compression].max_rect_size;
    max_rows /= MIN(tight_conf[vs->tight->compression].max_rect_width, w);

    return find_large_solid_color_rect(vs, x, y, w, h, max_rows);
}

int vnc_tight_send_framebuffer_update(VncState *vs, int x, int y,
                                      int w, int h)
{
    vs->tight->type = VNC_ENCODING_TIGHT;
    return tight_send_framebuffer_update(vs, x, y, w, h);
}

int vnc_tight_png_send_framebuffer_update(VncState *vs, int x, int y,
                                          int w, int h)
{
    vs->tight->type = VNC_ENCODING_TIGHT_PNG;
    return tight_send_framebuffer_update(vs, x, y, w, h);
}

void vnc_tight_clear(VncState *vs)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(vs->tight->stream); i++) {
        if (vs->tight->stream[i].opaque) {
            deflateEnd(&vs->tight->stream[i]);
        }
    }

    buffer_free(&vs->tight->tight);
    buffer_free(&vs->tight->zlib);
    buffer_free(&vs->tight->gradient);
#ifdef CONFIG_VNC_JPEG
    buffer_free(&vs->tight->jpeg);
#endif
#ifdef CONFIG_VNC_PNG
    buffer_free(&vs->tight->png);
#endif
}
