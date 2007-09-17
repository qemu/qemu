/*
 * QEMU OMAP LCD Emulator templates
 *
 * Copyright (c) 2006 Andrzej Zaborowski  <balrog@zabor.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#if DEPTH == 8
# define BPP 1
# define PIXEL_TYPE uint8_t
#elif DEPTH == 15 || DEPTH == 16
# define BPP 2
# define PIXEL_TYPE uint16_t
#elif DEPTH == 32
# define BPP 4
# define PIXEL_TYPE uint32_t
#else
# error unsupport depth
#endif

/*
 * 2-bit colour
 */
static void glue(draw_line2_, DEPTH)(
                uint8_t *d, const uint8_t *s, int width, const uint16_t *pal)
{
    uint8_t v, r, g, b;

    do {
        v = ldub_raw((void *) s);
        r = (pal[v & 3] >> 4) & 0xf0;
        g = pal[v & 3] & 0xf0;
        b = (pal[v & 3] << 4) & 0xf0;
        ((PIXEL_TYPE *) d)[0] = glue(rgb_to_pixel, DEPTH)(r, g, b);
        d += BPP;
        v >>= 2;
        r = (pal[v & 3] >> 4) & 0xf0;
        g = pal[v & 3] & 0xf0;
        b = (pal[v & 3] << 4) & 0xf0;
        ((PIXEL_TYPE *) d)[0] = glue(rgb_to_pixel, DEPTH)(r, g, b);
        d += BPP;
        v >>= 2;
        r = (pal[v & 3] >> 4) & 0xf0;
        g = pal[v & 3] & 0xf0;
        b = (pal[v & 3] << 4) & 0xf0;
        ((PIXEL_TYPE *) d)[0] = glue(rgb_to_pixel, DEPTH)(r, g, b);
        d += BPP;
        v >>= 2;
        r = (pal[v & 3] >> 4) & 0xf0;
        g = pal[v & 3] & 0xf0;
        b = (pal[v & 3] << 4) & 0xf0;
        ((PIXEL_TYPE *) d)[0] = glue(rgb_to_pixel, DEPTH)(r, g, b);
        d += BPP;
        s ++;
        width -= 4;
    } while (width > 0);
}

/*
 * 4-bit colour
 */
static void glue(draw_line4_, DEPTH)(
                uint8_t *d, const uint8_t *s, int width, const uint16_t *pal)
{
    uint8_t v, r, g, b;

    do {
        v = ldub_raw((void *) s);
        r = (pal[v & 0xf] >> 4) & 0xf0;
        g = pal[v & 0xf] & 0xf0;
        b = (pal[v & 0xf] << 4) & 0xf0;
        ((PIXEL_TYPE *) d)[0] = glue(rgb_to_pixel, DEPTH)(r, g, b);
        d += BPP;
        v >>= 4;
        r = (pal[v & 0xf] >> 4) & 0xf0;
        g = pal[v & 0xf] & 0xf0;
        b = (pal[v & 0xf] << 4) & 0xf0;
        ((PIXEL_TYPE *) d)[0] = glue(rgb_to_pixel, DEPTH)(r, g, b);
        d += BPP;
        s ++;
        width -= 2;
    } while (width > 0);
}

/*
 * 8-bit colour
 */
static void glue(draw_line8_, DEPTH)(
                uint8_t *d, const uint8_t *s, int width, const uint16_t *pal)
{
    uint8_t v, r, g, b;

    do {
        v = ldub_raw((void *) s);
        r = (pal[v] >> 4) & 0xf0;
        g = pal[v] & 0xf0;
        b = (pal[v] << 4) & 0xf0;
        ((PIXEL_TYPE *) d)[0] = glue(rgb_to_pixel, DEPTH)(r, g, b);
        s ++;
        d += BPP;
    } while (-- width != 0);
}

/*
 * 12-bit colour
 */
static void glue(draw_line12_, DEPTH)(
                uint8_t *d, const uint8_t *s, int width, const uint16_t *pal)
{
    uint16_t v;
    uint8_t r, g, b;

    do {
        v = lduw_raw((void *) s);
        r = (v >> 4) & 0xf0;
        g = v & 0xf0;
        b = (v << 4) & 0xf0;
        ((PIXEL_TYPE *) d)[0] = glue(rgb_to_pixel, DEPTH)(r, g, b);
        s += 2;
        d += BPP;
    } while (-- width != 0);
}

/*
 * 16-bit colour
 */
static void glue(draw_line16_, DEPTH)(
                uint8_t *d, const uint8_t *s, int width, const uint16_t *pal)
{
#if DEPTH == 16 && defined(WORDS_BIGENDIAN) == defined(TARGET_WORDS_BIGENDIAN)
    memcpy(d, s, width * 2);
#else
    uint16_t v;
    uint8_t r, g, b;

    do {
        v = lduw_raw((void *) s);
        r = (v >> 8) & 0xf8;
        g = (v >> 3) & 0xfc;
        b = (v << 3) & 0xf8;
        ((PIXEL_TYPE *) d)[0] = glue(rgb_to_pixel, DEPTH)(r, g, b);
        s += 2;
        d += BPP;
    } while (-- width != 0);
#endif
}

#undef DEPTH
#undef BPP
#undef PIXEL_TYPE
