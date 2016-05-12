/*
 * QEMU Epson S1D13744/S1D13745 templates
 *
 * Copyright (C) 2008 Nokia Corporation
 * Written by Andrzej Zaborowski <andrew@openedhand.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#define SKIP_PIXEL(to)         (to += deststep)
#if DEPTH == 32
# define PIXEL_TYPE            uint32_t
# define COPY_PIXEL(to, from)  do { *to = from; SKIP_PIXEL(to); } while (0)
# define COPY_PIXEL1(to, from) (*to++ = from)
#else
# error unknown bit depth
#endif

#ifdef HOST_WORDS_BIGENDIAN
# define SWAP_WORDS	1
#endif

static void glue(blizzard_draw_line16_, DEPTH)(PIXEL_TYPE *dest,
                const uint16_t *src, unsigned int width)
{
    uint16_t data;
    unsigned int r, g, b;
    const uint16_t *end = (const void *) src + width;
    while (src < end) {
        data = *src ++;
        b = (data & 0x1f) << 3;
        data >>= 5;
        g = (data & 0x3f) << 2;
        data >>= 6;
        r = (data & 0x1f) << 3;
        data >>= 5;
        COPY_PIXEL1(dest, glue(rgb_to_pixel, DEPTH)(r, g, b));
    }
}

static void glue(blizzard_draw_line24mode1_, DEPTH)(PIXEL_TYPE *dest,
                const uint8_t *src, unsigned int width)
{
    /* TODO: check if SDL 24-bit planes are not in the same format and
     * if so, use memcpy */
    unsigned int r[2], g[2], b[2];
    const uint8_t *end = src + width;
    while (src < end) {
        g[0] = *src ++;
        r[0] = *src ++;
        r[1] = *src ++;
        b[0] = *src ++;
        COPY_PIXEL1(dest, glue(rgb_to_pixel, DEPTH)(r[0], g[0], b[0]));
        b[1] = *src ++;
        g[1] = *src ++;
        COPY_PIXEL1(dest, glue(rgb_to_pixel, DEPTH)(r[1], g[1], b[1]));
    }
}

static void glue(blizzard_draw_line24mode2_, DEPTH)(PIXEL_TYPE *dest,
                const uint8_t *src, unsigned int width)
{
    unsigned int r, g, b;
    const uint8_t *end = src + width;
    while (src < end) {
        r = *src ++;
        src ++;
        b = *src ++;
        g = *src ++;
        COPY_PIXEL1(dest, glue(rgb_to_pixel, DEPTH)(r, g, b));
    }
}

/* No rotation */
static blizzard_fn_t glue(blizzard_draw_fn_, DEPTH)[0x10] = {
    NULL,
    /* RGB 5:6:5*/
    (blizzard_fn_t) glue(blizzard_draw_line16_, DEPTH),
    /* RGB 6:6:6 mode 1 */
    (blizzard_fn_t) glue(blizzard_draw_line24mode1_, DEPTH),
    /* RGB 8:8:8 mode 1 */
    (blizzard_fn_t) glue(blizzard_draw_line24mode1_, DEPTH),
    NULL, NULL,
    /* RGB 6:6:6 mode 2 */
    (blizzard_fn_t) glue(blizzard_draw_line24mode2_, DEPTH),
    /* RGB 8:8:8 mode 2 */
    (blizzard_fn_t) glue(blizzard_draw_line24mode2_, DEPTH),
    /* YUV 4:2:2 */
    NULL,
    /* YUV 4:2:0 */
    NULL,
    NULL, NULL, NULL, NULL, NULL, NULL,
};

/* 90deg, 180deg and 270deg rotation */
static blizzard_fn_t glue(blizzard_draw_fn_r_, DEPTH)[0x10] = {
    /* TODO */
    [0 ... 0xf] = NULL,
};

#undef DEPTH
#undef SKIP_PIXEL
#undef COPY_PIXEL
#undef COPY_PIXEL1
#undef PIXEL_TYPE

#undef SWAP_WORDS
