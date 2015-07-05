/*
 * QEMU texture swizzling routines
 *
 * Copyright (c) 2013 espes
 * Copyright (c) 2007-2010 The Nouveau Project.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include <stdint.h>
#include <string.h>
#include "qemu/osdep.h"

#include "hw/xbox/swizzle.h"

static unsigned int log2i(unsigned int i)
{
    unsigned int r = 0;
    while (i >>= 1) r++;
    return r;
}

static unsigned int get_swizzled_offset(
    unsigned int x, unsigned int y,
    unsigned int width, unsigned int height,
    unsigned int bytes_per_pixel)
{
    unsigned int k = log2i(MIN(width, height));

    unsigned int u = (x & 0x001) << 0 |
        (x & 0x002) << 1 |
        (x & 0x004) << 2 |
        (x & 0x008) << 3 |
        (x & 0x010) << 4 |
        (x & 0x020) << 5 |
        (x & 0x040) << 6 |
        (x & 0x080) << 7 |
        (x & 0x100) << 8 |
        (x & 0x200) << 9 |
        (x & 0x400) << 10 |
        (x & 0x800) << 11;

    unsigned int v = (y & 0x001) << 1 |
        (y & 0x002) << 2 |
        (y & 0x004) << 3 |
        (y & 0x008) << 4 |
        (y & 0x010) << 5 |
        (y & 0x020) << 6 |
        (y & 0x040) << 7 |
        (y & 0x080) << 8 |
        (y & 0x100) << 9 |
        (y & 0x200) << 10 |
        (y & 0x400) << 11 |
        (y & 0x800) << 12;

    return bytes_per_pixel * (((u | v) & ~(~0 << 2*k)) |
             (x & (~0 << k)) << k |
             (y & (~0 << k)) << k);
}

void swizzle_rect(
    const uint8_t *src_buf,
    unsigned int width,
    unsigned int height,
    uint8_t *dst_buf,
    unsigned int pitch,
    unsigned int bytes_per_pixel)
{
    int x, y;
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            const uint8_t *src = src_buf+ (y * pitch + x * bytes_per_pixel);
            uint8_t *dst = dst_buf +
                get_swizzled_offset(x, y, width, height, bytes_per_pixel);
            memcpy(dst, src, bytes_per_pixel);
        }
    }
}

void unswizzle_rect(
    const uint8_t *src_buf,
    unsigned int width,
    unsigned int height,
    uint8_t *dst_buf,
    unsigned int pitch,
    unsigned int bytes_per_pixel)
{
    int x, y;
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            const uint8_t *src = src_buf
                + get_swizzled_offset(x, y, width, height, bytes_per_pixel);
            uint8_t *dst = dst_buf + (y * pitch + x * bytes_per_pixel);
            memcpy(dst, src, bytes_per_pixel);
        }
    }
}