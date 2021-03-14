/*
 * Arm PrimeCell PL110 Color LCD Controller
 *
 * Copyright (c) 2005 CodeSourcery, LLC.
 * Written by Paul Brook
 *
 * This code is licensed under the GNU LGPL
 *
 * Framebuffer format conversion routines.
 */

#ifndef ORDER
#error "pl110_template.h is only for inclusion by pl110.c"
#endif

#if ORDER == 0
#define NAME glue(lblp_, BORDER)
#ifdef HOST_WORDS_BIGENDIAN
#define SWAP_WORDS 1
#endif
#elif ORDER == 1
#define NAME glue(bbbp_, BORDER)
#ifndef HOST_WORDS_BIGENDIAN
#define SWAP_WORDS 1
#endif
#else
#define SWAP_PIXELS 1
#define NAME glue(lbbp_, BORDER)
#ifdef HOST_WORDS_BIGENDIAN
#define SWAP_WORDS 1
#endif
#endif

#define FN_2(x, y) FN(x, y) FN(x+1, y)
#define FN_4(x, y) FN_2(x, y) FN_2(x+2, y)
#define FN_8(y) FN_4(0, y) FN_4(4, y)

static void glue(pl110_draw_line1_,NAME)(void *opaque, uint8_t *d, const uint8_t *src, int width, int deststep)
{
    uint32_t *palette = opaque;
    uint32_t data;
    while (width > 0) {
        data = *(uint32_t *)src;
#ifdef SWAP_PIXELS
#define FN(x, y) COPY_PIXEL(d, palette[(data >> (y + 7 - (x))) & 1]);
#else
#define FN(x, y) COPY_PIXEL(d, palette[(data >> ((x) + y)) & 1]);
#endif
#ifdef SWAP_WORDS
        FN_8(24)
        FN_8(16)
        FN_8(8)
        FN_8(0)
#else
        FN_8(0)
        FN_8(8)
        FN_8(16)
        FN_8(24)
#endif
#undef FN
        width -= 32;
        src += 4;
    }
}

static void glue(pl110_draw_line2_,NAME)(void *opaque, uint8_t *d, const uint8_t *src, int width, int deststep)
{
    uint32_t *palette = opaque;
    uint32_t data;
    while (width > 0) {
        data = *(uint32_t *)src;
#ifdef SWAP_PIXELS
#define FN(x, y) COPY_PIXEL(d, palette[(data >> (y + 6 - (x)*2)) & 3]);
#else
#define FN(x, y) COPY_PIXEL(d, palette[(data >> ((x)*2 + y)) & 3]);
#endif
#ifdef SWAP_WORDS
        FN_4(0, 24)
        FN_4(0, 16)
        FN_4(0, 8)
        FN_4(0, 0)
#else
        FN_4(0, 0)
        FN_4(0, 8)
        FN_4(0, 16)
        FN_4(0, 24)
#endif
#undef FN
        width -= 16;
        src += 4;
    }
}

static void glue(pl110_draw_line4_,NAME)(void *opaque, uint8_t *d, const uint8_t *src, int width, int deststep)
{
    uint32_t *palette = opaque;
    uint32_t data;
    while (width > 0) {
        data = *(uint32_t *)src;
#ifdef SWAP_PIXELS
#define FN(x, y) COPY_PIXEL(d, palette[(data >> (y + 4 - (x)*4)) & 0xf]);
#else
#define FN(x, y) COPY_PIXEL(d, palette[(data >> ((x)*4 + y)) & 0xf]);
#endif
#ifdef SWAP_WORDS
        FN_2(0, 24)
        FN_2(0, 16)
        FN_2(0, 8)
        FN_2(0, 0)
#else
        FN_2(0, 0)
        FN_2(0, 8)
        FN_2(0, 16)
        FN_2(0, 24)
#endif
#undef FN
        width -= 8;
        src += 4;
    }
}

static void glue(pl110_draw_line8_,NAME)(void *opaque, uint8_t *d, const uint8_t *src, int width, int deststep)
{
    uint32_t *palette = opaque;
    uint32_t data;
    while (width > 0) {
        data = *(uint32_t *)src;
#define FN(x) COPY_PIXEL(d, palette[(data >> (x)) & 0xff]);
#ifdef SWAP_WORDS
        FN(24)
        FN(16)
        FN(8)
        FN(0)
#else
        FN(0)
        FN(8)
        FN(16)
        FN(24)
#endif
#undef FN
        width -= 4;
        src += 4;
    }
}

static void glue(pl110_draw_line16_,NAME)(void *opaque, uint8_t *d, const uint8_t *src, int width, int deststep)
{
    uint32_t data;
    unsigned int r, g, b;
    while (width > 0) {
        data = *(uint32_t *)src;
#ifdef SWAP_WORDS
        data = bswap32(data);
#endif
#ifdef RGB
#define LSB r
#define MSB b
#else
#define LSB b
#define MSB r
#endif
#if 0
        LSB = data & 0x1f;
        data >>= 5;
        g = data & 0x3f;
        data >>= 6;
        MSB = data & 0x1f;
        data >>= 5;
#else
        LSB = (data & 0x1f) << 3;
        data >>= 5;
        g = (data & 0x3f) << 2;
        data >>= 6;
        MSB = (data & 0x1f) << 3;
        data >>= 5;
#endif
        COPY_PIXEL(d, rgb_to_pixel32(r, g, b));
        LSB = (data & 0x1f) << 3;
        data >>= 5;
        g = (data & 0x3f) << 2;
        data >>= 6;
        MSB = (data & 0x1f) << 3;
        data >>= 5;
        COPY_PIXEL(d, rgb_to_pixel32(r, g, b));
#undef MSB
#undef LSB
        width -= 2;
        src += 4;
    }
}

static void glue(pl110_draw_line32_,NAME)(void *opaque, uint8_t *d, const uint8_t *src, int width, int deststep)
{
    uint32_t data;
    unsigned int r, g, b;
    while (width > 0) {
        data = *(uint32_t *)src;
#ifdef RGB
#define LSB r
#define MSB b
#else
#define LSB b
#define MSB r
#endif
#ifndef SWAP_WORDS
        LSB = data & 0xff;
        g = (data >> 8) & 0xff;
        MSB = (data >> 16) & 0xff;
#else
        LSB = (data >> 24) & 0xff;
        g = (data >> 16) & 0xff;
        MSB = (data >> 8) & 0xff;
#endif
        COPY_PIXEL(d, rgb_to_pixel32(r, g, b));
#undef MSB
#undef LSB
        width--;
        src += 4;
    }
}

static void glue(pl110_draw_line16_555_,NAME)(void *opaque, uint8_t *d, const uint8_t *src, int width, int deststep)
{
    /* RGB 555 plus an intensity bit (which we ignore) */
    uint32_t data;
    unsigned int r, g, b;
    while (width > 0) {
        data = *(uint32_t *)src;
#ifdef SWAP_WORDS
        data = bswap32(data);
#endif
#ifdef RGB
#define LSB r
#define MSB b
#else
#define LSB b
#define MSB r
#endif
        LSB = (data & 0x1f) << 3;
        data >>= 5;
        g = (data & 0x1f) << 3;
        data >>= 5;
        MSB = (data & 0x1f) << 3;
        data >>= 5;
        COPY_PIXEL(d, rgb_to_pixel32(r, g, b));
        LSB = (data & 0x1f) << 3;
        data >>= 5;
        g = (data & 0x1f) << 3;
        data >>= 5;
        MSB = (data & 0x1f) << 3;
        data >>= 6;
        COPY_PIXEL(d, rgb_to_pixel32(r, g, b));
#undef MSB
#undef LSB
        width -= 2;
        src += 4;
    }
}

static void glue(pl110_draw_line12_,NAME)(void *opaque, uint8_t *d, const uint8_t *src, int width, int deststep)
{
    /* RGB 444 with 4 bits of zeroes at the top of each halfword */
    uint32_t data;
    unsigned int r, g, b;
    while (width > 0) {
        data = *(uint32_t *)src;
#ifdef SWAP_WORDS
        data = bswap32(data);
#endif
#ifdef RGB
#define LSB r
#define MSB b
#else
#define LSB b
#define MSB r
#endif
        LSB = (data & 0xf) << 4;
        data >>= 4;
        g = (data & 0xf) << 4;
        data >>= 4;
        MSB = (data & 0xf) << 4;
        data >>= 8;
        COPY_PIXEL(d, rgb_to_pixel32(r, g, b));
        LSB = (data & 0xf) << 4;
        data >>= 4;
        g = (data & 0xf) << 4;
        data >>= 4;
        MSB = (data & 0xf) << 4;
        data >>= 8;
        COPY_PIXEL(d, rgb_to_pixel32(r, g, b));
#undef MSB
#undef LSB
        width -= 2;
        src += 4;
    }
}

#undef SWAP_PIXELS
#undef NAME
#undef SWAP_WORDS
#undef ORDER
