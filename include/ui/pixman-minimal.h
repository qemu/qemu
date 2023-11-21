/*
 * SPDX-License-Identifier: MIT
 *
 * Tiny subset of PIXMAN API commonly used by QEMU.
 *
 * Copyright 1987, 1988, 1989, 1998  The Open Group
 * Copyright 1987, 1988, 1989 Digital Equipment Corporation
 * Copyright 1999, 2004, 2008 Keith Packard
 * Copyright 2000 SuSE, Inc.
 * Copyright 2000 Keith Packard, member of The XFree86 Project, Inc.
 * Copyright 2004, 2005, 2007, 2008, 2009, 2010 Red Hat, Inc.
 * Copyright 2004 Nicholas Miell
 * Copyright 2005 Lars Knoll & Zack Rusin, Trolltech
 * Copyright 2005 Trolltech AS
 * Copyright 2007 Luca Barbato
 * Copyright 2008 Aaron Plattner, NVIDIA Corporation
 * Copyright 2008 Rodrigo Kumpera
 * Copyright 2008 André Tupinambá
 * Copyright 2008 Mozilla Corporation
 * Copyright 2008 Frederic Plourde
 * Copyright 2009, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2009, 2010 Nokia Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef PIXMAN_MINIMAL_H
#define PIXMAN_MINIMAL_H

#define PIXMAN_TYPE_OTHER       0
#define PIXMAN_TYPE_ARGB        2
#define PIXMAN_TYPE_ABGR        3
#define PIXMAN_TYPE_BGRA        8
#define PIXMAN_TYPE_RGBA        9

#define PIXMAN_FORMAT(bpp, type, a, r, g, b) (((bpp) << 24) |   \
                                              ((type) << 16) |  \
                                              ((a) << 12) |     \
                                              ((r) << 8) |      \
                                              ((g) << 4) |      \
                                              ((b)))

#define PIXMAN_FORMAT_RESHIFT(val, ofs, num)                            \
        (((val >> (ofs)) & ((1 << (num)) - 1)) << ((val >> 22) & 3))

#define PIXMAN_FORMAT_BPP(f)    PIXMAN_FORMAT_RESHIFT(f, 24, 8)
#define PIXMAN_FORMAT_TYPE(f)   (((f) >> 16) & 0x3f)
#define PIXMAN_FORMAT_A(f)      PIXMAN_FORMAT_RESHIFT(f, 12, 4)
#define PIXMAN_FORMAT_R(f)      PIXMAN_FORMAT_RESHIFT(f, 8, 4)
#define PIXMAN_FORMAT_G(f)      PIXMAN_FORMAT_RESHIFT(f, 4, 4)
#define PIXMAN_FORMAT_B(f)      PIXMAN_FORMAT_RESHIFT(f, 0, 4)
#define PIXMAN_FORMAT_DEPTH(f)  (PIXMAN_FORMAT_A(f) +   \
                                 PIXMAN_FORMAT_R(f) +   \
                                 PIXMAN_FORMAT_G(f) +   \
                                 PIXMAN_FORMAT_B(f))

typedef enum {
    /* 32bpp formats */
    PIXMAN_a8r8g8b8 =    PIXMAN_FORMAT(32, PIXMAN_TYPE_ARGB, 8, 8, 8, 8),
    PIXMAN_x8r8g8b8 =    PIXMAN_FORMAT(32, PIXMAN_TYPE_ARGB, 0, 8, 8, 8),
    PIXMAN_a8b8g8r8 =    PIXMAN_FORMAT(32, PIXMAN_TYPE_ABGR, 8, 8, 8, 8),
    PIXMAN_x8b8g8r8 =    PIXMAN_FORMAT(32, PIXMAN_TYPE_ABGR, 0, 8, 8, 8),
    PIXMAN_b8g8r8a8 =    PIXMAN_FORMAT(32, PIXMAN_TYPE_BGRA, 8, 8, 8, 8),
    PIXMAN_b8g8r8x8 =    PIXMAN_FORMAT(32, PIXMAN_TYPE_BGRA, 0, 8, 8, 8),
    PIXMAN_r8g8b8a8 =    PIXMAN_FORMAT(32, PIXMAN_TYPE_RGBA, 8, 8, 8, 8),
    PIXMAN_r8g8b8x8 =    PIXMAN_FORMAT(32, PIXMAN_TYPE_RGBA, 0, 8, 8, 8),
    /* 24bpp formats */
    PIXMAN_r8g8b8 =      PIXMAN_FORMAT(24, PIXMAN_TYPE_ARGB, 0, 8, 8, 8),
    PIXMAN_b8g8r8 =      PIXMAN_FORMAT(24, PIXMAN_TYPE_ABGR, 0, 8, 8, 8),
    /* 16bpp formats */
    PIXMAN_r5g6b5 =      PIXMAN_FORMAT(16, PIXMAN_TYPE_ARGB, 0, 5, 6, 5),
    PIXMAN_a1r5g5b5 =    PIXMAN_FORMAT(16, PIXMAN_TYPE_ARGB, 1, 5, 5, 5),
    PIXMAN_x1r5g5b5 =    PIXMAN_FORMAT(16, PIXMAN_TYPE_ARGB, 0, 5, 5, 5),
} pixman_format_code_t;

typedef struct pixman_image pixman_image_t;

typedef void (*pixman_image_destroy_func_t)(pixman_image_t *image, void *data);

struct pixman_image {
    int ref_count;
    pixman_format_code_t format;
    int width;
    int height;
    int stride;
    uint32_t *data;
    uint32_t *free_me;
    pixman_image_destroy_func_t destroy_func;
    void *destroy_data;
};

typedef struct pixman_color {
    uint16_t    red;
    uint16_t    green;
    uint16_t    blue;
    uint16_t    alpha;
} pixman_color_t;

static inline uint32_t *create_bits(pixman_format_code_t format,
                                    int width,
                                    int height,
                                    int *rowstride_bytes)
{
    int stride = 0;
    size_t buf_size = 0;
    int bpp = PIXMAN_FORMAT_BPP(format);

    /*
     * Calculate the following while checking for overflow truncation:
     * stride = ((width * bpp + 0x1f) >> 5) * sizeof(uint32_t);
     */

    if (unlikely(__builtin_mul_overflow(width, bpp, &stride))) {
        return NULL;
    }

    if (unlikely(__builtin_add_overflow(stride, 0x1f, &stride))) {
        return NULL;
    }

    stride >>= 5;

    stride *= sizeof(uint32_t);

    if (unlikely(__builtin_mul_overflow((size_t) height,
                                        (size_t) stride,
                                        &buf_size))) {
        return NULL;
    }

    if (rowstride_bytes) {
        *rowstride_bytes = stride;
    }

    return g_malloc0(buf_size);
}

static inline pixman_image_t *pixman_image_create_bits(pixman_format_code_t format,
                                                       int width,
                                                       int height,
                                                       uint32_t *bits,
                                                       int rowstride_bytes)
{
    pixman_image_t *i = g_new0(pixman_image_t, 1);

    i->width = width;
    i->height = height;
    i->format = format;
    if (bits) {
        i->data = bits;
    } else {
        i->free_me = i->data =
            create_bits(format, width, height, &rowstride_bytes);
        if (width && height) {
            assert(i->data);
        }
    }
    i->stride = rowstride_bytes ? rowstride_bytes :
                            width * DIV_ROUND_UP(PIXMAN_FORMAT_BPP(format), 8);
    i->ref_count = 1;

    return i;
}

static inline pixman_image_t *pixman_image_ref(pixman_image_t *i)
{
    i->ref_count++;
    return i;
}

static inline bool pixman_image_unref(pixman_image_t *i)
{
    i->ref_count--;

    if (i->ref_count == 0) {
        if (i->destroy_func) {
            i->destroy_func(i, i->destroy_data);
        }
        g_free(i->free_me);
        g_free(i);

        return true;
    }

    return false;
}

static inline void pixman_image_set_destroy_function(pixman_image_t *i,
                                                     pixman_image_destroy_func_t func,
                                                     void *data)

{
    i->destroy_func = func;
    i->destroy_data = data;
}

static inline uint32_t *pixman_image_get_data(pixman_image_t *i)
{
    return i->data;
}

static inline int pixman_image_get_height(pixman_image_t *i)
{
    return i->height;
}

static inline int pixman_image_get_width(pixman_image_t *i)
{
    return i->width;
}

static inline int pixman_image_get_stride(pixman_image_t *i)
{
    return i->stride;
}

static inline pixman_format_code_t pixman_image_get_format(pixman_image_t *i)
{
    return i->format;
}

#endif /* PIXMAN_MINIMAL_H */
