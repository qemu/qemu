/*
 * Samsung S3C2410A LCD controller emulation.
 *
 * Copyright (c) 2007 OpenMoko, Inc.
 * Author: Andrzej Zaborowski <andrew@openedhand.com>
 *
 * This code is licensed under the GNU GPL v2.
 *
 * Framebuffer format conversion routines.
 */

# define SKIP_PIXEL(to)		to += deststep
#if BITS == 8
# define COPY_PIXEL(to, from)	*to = from; SKIP_PIXEL(to)
#elif BITS == 15 || BITS == 16
# define COPY_PIXEL(to, from)	*(uint16_t *) to = from; SKIP_PIXEL(to)
#elif BITS == 24 
# define COPY_PIXEL(to, from)	\
	*(uint16_t *) to = from; *(to + 2) = (from) >> 16; SKIP_PIXEL(to)
#elif BITS == 32
# define COPY_PIXEL(to, from)	*(uint32_t *) to = from; SKIP_PIXEL(to)
#else
# error unknown bit depth
#endif

#ifdef WORDS_BIGENDIAN
# define SWAP_WORDS	1
#endif

#define FN_2(x)		FN(x + 1) FN(x)
#define FN_4(x)		FN_2(x + 2) FN_2(x)
#define FN_8(x)		FN_4(x + 4) FN_4(x)

static void glue(s3c24xx_draw_line1_, BITS)(void *opaque,
                uint8_t *dest, const uint8_t *src, int width, int deststep)
{
    uint32_t *palette = opaque;
    uint32_t data;
    while (width > 0) {
        data = *(uint32_t *) src;
#define FN(x)		COPY_PIXEL(dest, palette[(data >> (x)) & 1]);
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

static void glue(s3c24xx_draw_line2_, BITS)(void *opaque,
                uint8_t *dest, const uint8_t *src, int width, int deststep)
{
    uint32_t *palette = opaque;
    uint32_t data;
    while (width > 0) {
        data = *(uint32_t *) src;
#define FN(x)		COPY_PIXEL(dest, palette[(data >> ((x) * 2)) & 3]);
#ifdef SWAP_WORDS
        FN_4(12)
        FN_4(8)
        FN_4(4)
        FN_4(0)
#else
        FN_4(0)
        FN_4(4)
        FN_4(8)
        FN_4(12)
#endif
#undef FN
        width -= 16;
        src += 4;
    }
}

static void glue(s3c24xx_draw_line4_, BITS)(void *opaque,
                uint8_t *dest, const uint8_t *src, int width, int deststep)
{
    uint32_t *palette = opaque;
    uint32_t data;
    while (width > 0) {
        data = *(uint32_t *) src;
#define FN(x)		COPY_PIXEL(dest, palette[(data >> ((x) * 4)) & 0xf]);
#ifdef SWAP_WORDS
        FN_2(6)
        FN_2(4)
        FN_2(2)
        FN_2(0)
#else
        FN_2(0)
        FN_2(2)
        FN_2(4)
        FN_2(6)
#endif
#undef FN
        width -= 8;
        src += 4;
    }
}

static void glue(s3c24xx_draw_line8_, BITS)(void *opaque,
                uint8_t *dest, const uint8_t *src, int width, int deststep)
{
    uint32_t *palette = opaque;
    uint32_t data;
    while (width > 0) {
        data = *(uint32_t *) src;
#define FN(x)		COPY_PIXEL(dest, palette[(data >> (x)) & 0xff]);
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

static void glue(s3c24xx_draw_line16a_, BITS)(void *opaque,
                uint8_t *dest, const uint8_t *src, int width, int deststep)
{
    uint32_t data;
    unsigned int r, g, b;
    while (width > 0) {
        data = *(uint32_t *) src;
#ifdef SWAP_WORDS
        data = bswap32(data);
#endif
        b = (data & 0x1f) << 3;
        data >>= 5;
        g = (data & 0x3f) << 2;
        data >>= 6;
        r = (data & 0x1f) << 3;
        data >>= 5;
        COPY_PIXEL(dest, glue(s3c24xx_rgb_to_pixel, BITS)(r, g, b));
        b = (data & 0x1f) << 3;
        data >>= 5;
        g = (data & 0x3f) << 2;
        data >>= 6;
        r = (data & 0x1f) << 3;
        COPY_PIXEL(dest, glue(s3c24xx_rgb_to_pixel, BITS)(r, g, b));
        width -= 2;
        src += 4;
    }
}

static void glue(s3c24xx_draw_line16b_, BITS)(void *opaque,
                uint8_t *dest, const uint8_t *src, int width, int deststep)
{
    uint32_t data;
    unsigned int r, g, b;
    while (width > 0) {
        data = *(uint32_t *) src;
#ifdef SWAP_WORDS
        data = bswap32(data);
#endif
        b = (data & 0x1f) << 3;
        data >>= 5;
        g = (data & 0x1f) << 3;
        data >>= 5;
        r = (data & 0x3f) << 2;
        data >>= 5;
        COPY_PIXEL(dest, glue(s3c24xx_rgb_to_pixel, BITS)(r, g, b));
        b = (data & 0x1f) << 3;
        data >>= 5;
        g = (data & 0x1f) << 3;
        data >>= 5;
        r = (data & 0x3f) << 2;
        COPY_PIXEL(dest, glue(s3c24xx_rgb_to_pixel, BITS)(r, g, b));
        width -= 2;
        src += 4;
    }
}

static void glue(s3c24xx_draw_line12_, BITS)(void *opaque,
                uint8_t *dest, const uint8_t *src, int width, int deststep)
{
    uint32_t data;
    unsigned int r, g, b;
    while (width > 0) {
        data = *(uint32_t *) src;
        src += 3;
#ifdef SWAP_WORDS
        data = bswap32(data);
#endif
        /* XXX should use (x & 0xf) << 4) | (x & 0xf) for natural
         * colours.  Otherwise the image may be a bit darkened.  */
        b = (data & 0xf00) >> 4;
        g = (data & 0xf0) << 0;
        r = (data & 0xf) << 4;
        data >>= 12;
        COPY_PIXEL(dest, glue(s3c24xx_rgb_to_pixel, BITS)(r, g, b));
        b = (data & 0xf00) >> 4;
        g = (data & 0xf0) << 0;
        r = (data & 0xf) << 4;
        data >>= 12;
        COPY_PIXEL(dest, glue(s3c24xx_rgb_to_pixel, BITS)(r, g, b));
        b = (data & 0xf00) >> 4;
        g = (data & 0xf0) << 0;
        r = (data & 0xf) << 4;
        data >>= 12;
        COPY_PIXEL(dest, glue(s3c24xx_rgb_to_pixel, BITS)(r, g, b));
        b = (data & 0xf00) >> 4;
        g = (data & 0xf0) << 0;
        r = (data & 0xf) << 4;
        data >>= 12;
        COPY_PIXEL(dest, glue(s3c24xx_rgb_to_pixel, BITS)(r, g, b));
        width -= 4;
    }
}

static void glue(s3c24xx_draw_line24_, BITS)(void *opaque,
                uint8_t *dest, const uint8_t *src, int width, int deststep)
{
    uint32_t data;
    unsigned int r, g, b;
    while (width > 0) {
        data = *(uint32_t *) src;
#ifdef SWAP_WORDS
        data = bswap32(data);
#endif
        b = data & 0xff;
        data >>= 8;
        g = data & 0xff;
        data >>= 8;
        r = data & 0xff;
        COPY_PIXEL(dest, glue(s3c24xx_rgb_to_pixel, BITS)(r, g, b));
        width -= 1;
        src += 4;
    }
}

static drawfn glue(s3c24xx_draw_fn_, BITS)[] =
{
    glue(s3c24xx_draw_line1_, BITS),
    glue(s3c24xx_draw_line2_, BITS),
    glue(s3c24xx_draw_line4_, BITS),
    glue(s3c24xx_draw_line8_, BITS),
    glue(s3c24xx_draw_line12_, BITS),
    glue(s3c24xx_draw_line16a_, BITS),
    glue(s3c24xx_draw_line16b_, BITS),
    glue(s3c24xx_draw_line24_, BITS),
};

#undef BITS
#undef COPY_PIXEL
#undef SKIP_PIXEL

#ifdef SWAP_WORDS
# undef SWAP_WORDS
#endif
