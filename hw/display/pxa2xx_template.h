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

# define SKIP_PIXEL(to) do { to += deststep; } while (0)
# define COPY_PIXEL(to, from)    \
    do {                         \
        *(uint32_t *) to = from; \
        SKIP_PIXEL(to);          \
    } while (0)

#ifdef HOST_WORDS_BIGENDIAN
# define SWAP_WORDS	1
#endif

#define FN_2(x)		FN(x + 1) FN(x)
#define FN_4(x)		FN_2(x + 2) FN_2(x)

static void pxa2xx_draw_line2(void *opaque,
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

static void pxa2xx_draw_line4(void *opaque,
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

static void pxa2xx_draw_line8(void *opaque,
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

static void pxa2xx_draw_line16(void *opaque,
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
        COPY_PIXEL(dest, rgb_to_pixel32(r, g, b));
        b = (data & 0x1f) << 3;
        data >>= 5;
        g = (data & 0x3f) << 2;
        data >>= 6;
        r = (data & 0x1f) << 3;
        COPY_PIXEL(dest, rgb_to_pixel32(r, g, b));
        width -= 2;
        src += 4;
    }
}

static void pxa2xx_draw_line16t(void *opaque,
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
        if (data & 1) {
            SKIP_PIXEL(dest);
        } else {
            COPY_PIXEL(dest, rgb_to_pixel32(r, g, b));
        }
        data >>= 1;
        b = (data & 0x1f) << 3;
        data >>= 5;
        g = (data & 0x1f) << 3;
        data >>= 5;
        r = (data & 0x1f) << 3;
        data >>= 5;
        if (data & 1) {
            SKIP_PIXEL(dest);
        } else {
            COPY_PIXEL(dest, rgb_to_pixel32(r, g, b));
        }
        width -= 2;
        src += 4;
    }
}

static void pxa2xx_draw_line18(void *opaque,
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
        COPY_PIXEL(dest, rgb_to_pixel32(r, g, b));
        width -= 1;
        src += 4;
    }
}

/* The wicked packed format */
static void pxa2xx_draw_line18p(void *opaque,
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
        COPY_PIXEL(dest, rgb_to_pixel32(r, g, b));
        b = (data[0] & 0x3f) << 2;
        data[0] >>= 6;
        g = ((data[1] & 0xf) << 4) | (data[0] << 2);
        data[1] >>= 4;
        r = (data[1] & 0x3f) << 2;
        data[1] >>= 12;
        COPY_PIXEL(dest, rgb_to_pixel32(r, g, b));
        b = (data[1] & 0x3f) << 2;
        data[1] >>= 6;
        g = (data[1] & 0x3f) << 2;
        data[1] >>= 6;
        r = ((data[2] & 0x3) << 6) | (data[1] << 2);
        data[2] >>= 8;
        COPY_PIXEL(dest, rgb_to_pixel32(r, g, b));
        b = (data[2] & 0x3f) << 2;
        data[2] >>= 6;
        g = (data[2] & 0x3f) << 2;
        data[2] >>= 6;
        r = data[2] << 2;
        COPY_PIXEL(dest, rgb_to_pixel32(r, g, b));
        width -= 4;
    }
}

static void pxa2xx_draw_line19(void *opaque,
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
        if (data & 1) {
            SKIP_PIXEL(dest);
        } else {
            COPY_PIXEL(dest, rgb_to_pixel32(r, g, b));
        }
        width -= 1;
        src += 4;
    }
}

/* The wicked packed format */
static void pxa2xx_draw_line19p(void *opaque,
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
        if (data[0] & 1) {
            SKIP_PIXEL(dest);
        } else {
            COPY_PIXEL(dest, rgb_to_pixel32(r, g, b));
        }
        data[0] >>= 6;
        b = (data[0] & 0x3f) << 2;
        data[0] >>= 6;
        g = ((data[1] & 0xf) << 4) | (data[0] << 2);
        data[1] >>= 4;
        r = (data[1] & 0x3f) << 2;
        data[1] >>= 6;
        if (data[1] & 1) {
            SKIP_PIXEL(dest);
        } else {
            COPY_PIXEL(dest, rgb_to_pixel32(r, g, b));
        }
        data[1] >>= 6;
        b = (data[1] & 0x3f) << 2;
        data[1] >>= 6;
        g = (data[1] & 0x3f) << 2;
        data[1] >>= 6;
        r = ((data[2] & 0x3) << 6) | (data[1] << 2);
        data[2] >>= 2;
        if (data[2] & 1) {
            SKIP_PIXEL(dest);
        } else {
            COPY_PIXEL(dest, rgb_to_pixel32(r, g, b));
        }
        data[2] >>= 6;
        b = (data[2] & 0x3f) << 2;
        data[2] >>= 6;
        g = (data[2] & 0x3f) << 2;
        data[2] >>= 6;
        r = data[2] << 2;
        data[2] >>= 6;
        if (data[2] & 1) {
            SKIP_PIXEL(dest);
        } else {
            COPY_PIXEL(dest, rgb_to_pixel32(r, g, b));
        }
        width -= 4;
    }
}

static void pxa2xx_draw_line24(void *opaque,
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
        COPY_PIXEL(dest, rgb_to_pixel32(r, g, b));
        width -= 1;
        src += 4;
    }
}

static void pxa2xx_draw_line24t(void *opaque,
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
        if (data & 1) {
            SKIP_PIXEL(dest);
        } else {
            COPY_PIXEL(dest, rgb_to_pixel32(r, g, b));
        }
        width -= 1;
        src += 4;
    }
}

static void pxa2xx_draw_line25(void *opaque,
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
        if (data & 1) {
            SKIP_PIXEL(dest);
        } else {
            COPY_PIXEL(dest, rgb_to_pixel32(r, g, b));
        }
        width -= 1;
        src += 4;
    }
}

/* Overlay planes disabled, no transparency */
static drawfn pxa2xx_draw_fn_32[16] =
{
    [0 ... 0xf]       = NULL,
    [pxa_lcdc_2bpp]   = pxa2xx_draw_line2,
    [pxa_lcdc_4bpp]   = pxa2xx_draw_line4,
    [pxa_lcdc_8bpp]   = pxa2xx_draw_line8,
    [pxa_lcdc_16bpp]  = pxa2xx_draw_line16,
    [pxa_lcdc_18bpp]  = pxa2xx_draw_line18,
    [pxa_lcdc_18pbpp] = pxa2xx_draw_line18p,
    [pxa_lcdc_24bpp]  = pxa2xx_draw_line24,
};

/* Overlay planes enabled, transparency used */
static drawfn pxa2xx_draw_fn_32t[16] =
{
    [0 ... 0xf]       = NULL,
    [pxa_lcdc_4bpp]   = pxa2xx_draw_line4,
    [pxa_lcdc_8bpp]   = pxa2xx_draw_line8,
    [pxa_lcdc_16bpp]  = pxa2xx_draw_line16t,
    [pxa_lcdc_19bpp]  = pxa2xx_draw_line19,
    [pxa_lcdc_19pbpp] = pxa2xx_draw_line19p,
    [pxa_lcdc_24bpp]  = pxa2xx_draw_line24t,
    [pxa_lcdc_25bpp]  = pxa2xx_draw_line25,
};

#undef COPY_PIXEL
#undef SKIP_PIXEL

#ifdef SWAP_WORDS
# undef SWAP_WORDS
#endif
