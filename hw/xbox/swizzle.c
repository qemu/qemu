/*
 * QEMU texture swizzling routines
 *
 * Copyright (c) 2013 espes
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

void unswizzle_rect(
    uint8_t *src_buf,
    unsigned int width,
    unsigned int height,
    unsigned int depth,
    uint8_t *dst_buf,
    unsigned int pitch,
    unsigned int bytes_per_pixel)
{
    unsigned int offset_u = 0, offset_v = 0, offset_w = 0;
    uint32_t mask_u = 0, mask_v = 0, mask_w = 0;

    unsigned int i = 1, j = 1;

    while( (i <= width) || (i <= height) || (i <= depth) ) {
        if(i < width) {
            mask_u |= j;
            j<<=1;
        }
        if(i < height) {
            mask_v |= j;
            j<<=1;
        }
        if(i < depth) {
            mask_w |= j;
            j<<=1;
        }
        i<<=1;
    }

    uint32_t start_u = 0;
    uint32_t start_v = 0;
    uint32_t start_w = 0;
    uint32_t mask_max = 0;

    // get the biggest mask
    if(mask_u > mask_v)
        mask_max = mask_u;
    else
        mask_max = mask_v;
    if(mask_w > mask_max)
        mask_max = mask_w;

    for(i = 1; i <= mask_max; i<<=1) {
        if(i<=mask_u) {
            if(mask_u & i) start_u |= (offset_u & i);
            else offset_u <<= 1;
        }

        if(i <= mask_v) {
            if(mask_v & i) start_v |= (offset_v & i);
            else offset_v<<=1;
        }

        if(i <= mask_w) {
            if(mask_w & i) start_w |= (offset_w & i);
            else offset_w <<= 1;
        }
    }

    uint32_t w = start_w;
    unsigned int z;
    for(z=0; z<depth; z++) {
        uint32_t v = start_v;

        unsigned int y;
        for(y=0; y<height; y++) {
            uint32_t u = start_u;

            unsigned int x;
            for (x=0; x<width; x++) {
                memcpy(dst_buf,
                       src_buf + ( (u|v|w)*bytes_per_pixel ),
                       bytes_per_pixel);
                dst_buf += bytes_per_pixel;

                u = (u - mask_u) & mask_u;
            }
            dst_buf += pitch - width * bytes_per_pixel;

            v = (v - mask_v) & mask_v;
        }
        w = (w - mask_w) & mask_w;
    }
}