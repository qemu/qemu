/*
 * Intel XScale PXA255/270 LCDC emulation.
 *
 * Copyright (c) 2006 Openedhand Ltd.
 * Written by Andrzej Zaborowski <balrog@zabor.org>
 *
 * This code is licensed under the GPLv2.
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

static void glue(pxa2xx_draw_line2_, BITS)(uint32_t *palette,
                uint8_t *dest, const uint8_t *src, int width, int deststep)
{
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

static void glue(pxa2xx_draw_line4_, BITS)(uint32_t *palette,
                uint8_t *dest, const uint8_t *src, int width, int deststep)
{
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

static void glue(pxa2xx_draw_line8_, BITS)(uint32_t *palette,
                uint8_t *dest, const uint8_t *src, int width, int deststep)
{
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

static void glue(pxa2xx_draw_line16_, BITS)(uint32_t *palette,
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
        COPY_PIXEL(dest, glue(rgb_to_pixel, BITS)(r, g, b));
        b = (data & 0x1f) << 3;
        data >>= 5;
        g = (data & 0x3f) << 2;
        data >>= 6;
        r = (data & 0x1f) << 3;
        COPY_PIXEL(dest, glue(rgb_to_pixel, BITS)(r, g, b));
        width -= 2;
        src += 4;
    }
}

static void glue(pxa2xx_draw_line16t_, BITS)(uint32_t *palette,
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
        r = (data & 0x1f) << 3;
        data >>= 5;
        if (data & 1)
            SKIP_PIXEL(dest);
        else
            COPY_PIXEL(dest, glue(rgb_to_pixel, BITS)(r, g, b));
        data >>= 1;
        b = (data & 0x1f) << 3;
        data >>= 5;
        g = (data & 0x1f) << 3;
        data >>= 5;
        r = (data & 0x1f) << 3;
        data >>= 5;
        if (data & 1)
            SKIP_PIXEL(dest);
        else
            COPY_PIXEL(dest, glue(rgb_to_pixel, BITS)(r, g, b));
        width -= 2;
        src += 4;
    }
}

static void glue(pxa2xx_draw_line18_, BITS)(uint32_t *palette,
                uint8_t *dest, const uint8_t *src, int width, int deststep)
{
    uint32_t data;
    unsigned int r, g, b;
    while (width > 0) {
        data = *(uint32_t *) src;
#ifdef SWAP_WORDS
        data = bswap32(data);
#endif
        b = (data & 0x3f) << 2;
        data >>= 6;
        g = (data & 0x3f) << 2;
        data >>= 6;
        r = (data & 0x3f) << 2;
        COPY_PIXEL(dest, glue(rgb_to_pixel, BITS)(r, g, b));
        width -= 1;
        src += 4;
    }
}

/* The wicked packed format */
static void glue(pxa2xx_draw_line18p_, BITS)(uint32_t *palette,
                uint8_t *dest, const uint8_t *src, int width, int deststep)
{
    uint32_t data[3];
    unsigned int r, g, b;
    while (width > 0) {
        data[0] = *(uint32_t *) src;
        src += 4;
        data[1] = *(uint32_t *) src;
        src += 4;
        data[2] = *(uint32_t *) src;
        src += 4;
#ifdef SWAP_WORDS
        data[0] = bswap32(data[0]);
        data[1] = bswap32(data[1]);
        data[2] = bswap32(data[2]);
#endif
        b = (data[0] & 0x3f) << 2;
        data[0] >>= 6;
        g = (data[0] & 0x3f) << 2;
        data[0] >>= 6;
        r = (data[0] & 0x3f) << 2;
        data[0] >>= 12;
        COPY_PIXEL(dest, glue(rgb_to_pixel, BITS)(r, g, b));
        b = (data[0] & 0x3f) << 2;
        data[0] >>= 6;
        g = ((data[1] & 0xf) << 4) | (data[0] << 2);
        data[1] >>= 4;
        r = (data[1] & 0x3f) << 2;
        data[1] >>= 12;
        COPY_PIXEL(dest, glue(rgb_to_pixel, BITS)(r, g, b));
        b = (data[1] & 0x3f) << 2;
        data[1] >>= 6;
        g = (data[1] & 0x3f) << 2;
        data[1] >>= 6;
        r = ((data[2] & 0x3) << 6) | (data[1] << 2);
        data[2] >>= 8;
        COPY_PIXEL(dest, glue(rgb_to_pixel, BITS)(r, g, b));
        b = (data[2] & 0x3f) << 2;
        data[2] >>= 6;
        g = (data[2] & 0x3f) << 2;
        data[2] >>= 6;
        r = data[2] << 2;
        COPY_PIXEL(dest, glue(rgb_to_pixel, BITS)(r, g, b));
        width -= 4;
    }
}

static void glue(pxa2xx_draw_line19_, BITS)(uint32_t *palette,
                uint8_t *dest, const uint8_t *src, int width, int deststep)
{
    uint32_t data;
    unsigned int r, g, b;
    while (width > 0) {
        data = *(uint32_t *) src;
#ifdef SWAP_WORDS
        data = bswap32(data);
#endif
        b = (data & 0x3f) << 2;
        data >>= 6;
        g = (data & 0x3f) << 2;
        data >>= 6;
        r = (data & 0x3f) << 2;
        data >>= 6;
        if (data & 1)
            SKIP_PIXEL(dest);
        else
            COPY_PIXEL(dest, glue(rgb_to_pixel, BITS)(r, g, b));
        width -= 1;
        src += 4;
    }
}

/* The wicked packed format */
static void glue(pxa2xx_draw_line19p_, BITS)(uint32_t *palette,
                uint8_t *dest, const uint8_t *src, int width, int deststep)
{
    uint32_t data[3];
    unsigned int r, g, b;
    while (width > 0) {
        data[0] = *(uint32_t *) src;
        src += 4;
        data[1] = *(uint32_t *) src;
        src += 4;
        data[2] = *(uint32_t *) src;
        src += 4;
# ifdef SWAP_WORDS
        data[0] = bswap32(data[0]);
        data[1] = bswap32(data[1]);
        data[2] = bswap32(data[2]);
# endif
        b = (data[0] & 0x3f) << 2;
        data[0] >>= 6;
        g = (data[0] & 0x3f) << 2;
        data[0] >>= 6;
        r = (data[0] & 0x3f) << 2;
        data[0] >>= 6;
        if (data[0] & 1)
            SKIP_PIXEL(dest);
        else
            COPY_PIXEL(dest, glue(rgb_to_pixel, BITS)(r, g, b));
        data[0] >>= 6;
        b = (data[0] & 0x3f) << 2;
        data[0] >>= 6;
        g = ((data[1] & 0xf) << 4) | (data[0] << 2);
        data[1] >>= 4;
        r = (data[1] & 0x3f) << 2;
        data[1] >>= 6;
        if (data[1] & 1)
            SKIP_PIXEL(dest);
        else
            COPY_PIXEL(dest, glue(rgb_to_pixel, BITS)(r, g, b));
        data[1] >>= 6;
        b = (data[1] & 0x3f) << 2;
        data[1] >>= 6;
        g = (data[1] & 0x3f) << 2;
        data[1] >>= 6;
        r = ((data[2] & 0x3) << 6) | (data[1] << 2);
        data[2] >>= 2;
        if (data[2] & 1)
            SKIP_PIXEL(dest);
        else
            COPY_PIXEL(dest, glue(rgb_to_pixel, BITS)(r, g, b));
        data[2] >>= 6;
        b = (data[2] & 0x3f) << 2;
        data[2] >>= 6;
        g = (data[2] & 0x3f) << 2;
        data[2] >>= 6;
        r = data[2] << 2;
        data[2] >>= 6;
        if (data[2] & 1)
            SKIP_PIXEL(dest);
        else
            COPY_PIXEL(dest, glue(rgb_to_pixel, BITS)(r, g, b));
        width -= 4;
    }
}

static void glue(pxa2xx_draw_line24_, BITS)(uint32_t *palette,
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
        COPY_PIXEL(dest, glue(rgb_to_pixel, BITS)(r, g, b));
        width -= 1;
        src += 4;
    }
}

static void glue(pxa2xx_draw_line24t_, BITS)(uint32_t *palette,
                uint8_t *dest, const uint8_t *src, int width, int deststep)
{
    uint32_t data;
    unsigned int r, g, b;
    while (width > 0) {
        data = *(uint32_t *) src;
#ifdef SWAP_WORDS
        data = bswap32(data);
#endif
        b = (data & 0x7f) << 1;
        data >>= 7;
        g = data & 0xff;
        data >>= 8;
        r = data & 0xff;
        data >>= 8;
        if (data & 1)
            SKIP_PIXEL(dest);
        else
            COPY_PIXEL(dest, glue(rgb_to_pixel, BITS)(r, g, b));
        width -= 1;
        src += 4;
    }
}

static void glue(pxa2xx_draw_line25_, BITS)(uint32_t *palette,
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
        data >>= 8;
        if (data & 1)
            SKIP_PIXEL(dest);
        else
            COPY_PIXEL(dest, glue(rgb_to_pixel, BITS)(r, g, b));
        width -= 1;
        src += 4;
    }
}

/* Overlay planes disabled, no transparency */
static drawfn glue(pxa2xx_draw_fn_, BITS)[16] =
{
    [0 ... 0xf]       = 0,
    [pxa_lcdc_2bpp]   = glue(pxa2xx_draw_line2_, BITS),
    [pxa_lcdc_4bpp]   = glue(pxa2xx_draw_line4_, BITS),
    [pxa_lcdc_8bpp]   = glue(pxa2xx_draw_line8_, BITS),
    [pxa_lcdc_16bpp]  = glue(pxa2xx_draw_line16_, BITS),
    [pxa_lcdc_18bpp]  = glue(pxa2xx_draw_line18_, BITS),
    [pxa_lcdc_18pbpp] = glue(pxa2xx_draw_line18p_, BITS),
    [pxa_lcdc_24bpp]  = glue(pxa2xx_draw_line24_, BITS),
};

/* Overlay planes enabled, transparency used */
static drawfn glue(glue(pxa2xx_draw_fn_, BITS), t)[16] =
{
    [0 ... 0xf]       = 0,
    [pxa_lcdc_4bpp]   = glue(pxa2xx_draw_line4_, BITS),
    [pxa_lcdc_8bpp]   = glue(pxa2xx_draw_line8_, BITS),
    [pxa_lcdc_16bpp]  = glue(pxa2xx_draw_line16t_, BITS),
    [pxa_lcdc_19bpp]  = glue(pxa2xx_draw_line19_, BITS),
    [pxa_lcdc_19pbpp] = glue(pxa2xx_draw_line19p_, BITS),
    [pxa_lcdc_24bpp]  = glue(pxa2xx_draw_line24t_, BITS),
    [pxa_lcdc_25bpp]  = glue(pxa2xx_draw_line25_, BITS),
};

#undef BITS
#undef COPY_PIXEL
#undef SKIP_PIXEL

#ifdef SWAP_WORDS
# undef SWAP_WORDS
#endif
