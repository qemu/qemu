/*
 * QEMU texture swizzling routines
 *
 * Copyright (c) 2015 Jannik Vogel
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
#include <assert.h>
#include "qemu/osdep.h"

#include "hw/xbox/swizzle.h"

/* This should be pretty straightforward.
 * It creates a bit pattern like ..zyxzyxzyx from ..xxx, ..yyy and ..zzz
 * If there are no bits left from any component it will pack the other masks
 * more tighly (Example: zzxzxzyx = Fewer x than z and even fewer y)
 */
static void generate_swizzle_masks(unsigned int width,
                                   unsigned int height,
                                   unsigned int depth,
                                   uint32_t* mask_x,
                                   uint32_t* mask_y,
                                   uint32_t* mask_z)
{
    uint32_t x = 0, y = 0, z = 0;
    uint32_t bit = 1;
    uint32_t mask_bit = 1;
    bool done;
    do {
        done = true;
        if (bit < width) { x |= mask_bit; mask_bit <<= 1; done = false; }
        if (bit < height) { y |= mask_bit; mask_bit <<= 1; done = false; }
        if (bit < depth) { z |= mask_bit; mask_bit <<= 1; done = false; }
        bit <<= 1;
    } while(!done);
    assert(x ^ y ^ z == (mask_bit - 1));
    *mask_x = x;
    *mask_y = y;
    *mask_z = z;
}

/* This fills a pattern with a value if your value has bits abcd and your
 * pattern is 11010100100 this will return: 0a0b0c00d00
 */
static uint32_t fill_pattern(uint32_t pattern, uint32_t value)
{
    uint32_t result = 0;
    uint32_t bit = 1;
    while(value) {
        if (pattern & bit) {
            /* Copy bit to result */
            result |= value & 1 ? bit : 0;
            value >>= 1;
        }
        bit <<= 1;
    }
    return result;
}

static unsigned int get_swizzled_offset(
    unsigned int x, unsigned int y, unsigned int z,
    uint32_t mask_x, uint32_t mask_y, uint32_t mask_z,
    unsigned int bytes_per_pixel)
{
    return bytes_per_pixel * (fill_pattern(mask_x, x)
                           | fill_pattern(mask_y, y)
                           | fill_pattern(mask_z, z));
}

void swizzle_box(
    const uint8_t *src_buf,
    unsigned int width,
    unsigned int height,
    unsigned int depth,
    uint8_t *dst_buf,
    unsigned int row_pitch,
    unsigned int slice_pitch,
    unsigned int bytes_per_pixel)
{
    uint32_t mask_x, mask_y, mask_z;
    generate_swizzle_masks(width, height, depth, &mask_x, &mask_y, &mask_z);

    int x, y, z;
    for (z = 0; z < depth; z++) {
        for (y = 0; y < height; y++) {
            for (x = 0; x < width; x++) {
                const uint8_t *src = src_buf
                                         + y * row_pitch + x * bytes_per_pixel;
                uint8_t *dst = dst_buf + get_swizzled_offset(x, y, 0,
                                                             mask_x, mask_y, 0,
                                                             bytes_per_pixel);
                memcpy(dst, src, bytes_per_pixel);
            }
        }
        src_buf += slice_pitch;
    }
}

void unswizzle_box(
    const uint8_t *src_buf,
    unsigned int width,
    unsigned int height,
    unsigned int depth,
    uint8_t *dst_buf,
    unsigned int row_pitch,
    unsigned int slice_pitch,
    unsigned int bytes_per_pixel)
{
    uint32_t mask_x, mask_y, mask_z;
    generate_swizzle_masks(width, height, depth, &mask_x, &mask_y, &mask_z);

    int x, y, z;
    for (z = 0; z < depth; z++) {
        for (y = 0; y < height; y++) {
            for (x = 0; x < width; x++) {
                const uint8_t *src = src_buf
                    + get_swizzled_offset(x, y, z, mask_x, mask_y, mask_z,
                                          bytes_per_pixel);
                uint8_t *dst = dst_buf + y * row_pitch + x * bytes_per_pixel;
                memcpy(dst, src, bytes_per_pixel);
            }
        }
        dst_buf += slice_pitch;
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
    unswizzle_box(src_buf, width, height, 1, dst_buf, pitch, 0, bytes_per_pixel);
}

void swizzle_rect(
    const uint8_t *src_buf,
    unsigned int width,
    unsigned int height,
    uint8_t *dst_buf,
    unsigned int pitch,
    unsigned int bytes_per_pixel)
{
    swizzle_box(src_buf, width, height, 1, dst_buf, pitch, 0, bytes_per_pixel);
}
