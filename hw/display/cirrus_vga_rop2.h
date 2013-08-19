/*
 * QEMU Cirrus CLGD 54xx VGA Emulator.
 *
 * Copyright (c) 2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#if DEPTH == 8
#define PUTPIXEL()    ROP_OP(&d[0], col)
#elif DEPTH == 16
#define PUTPIXEL()    ROP_OP_16((uint16_t *)&d[0], col)
#elif DEPTH == 24
#define PUTPIXEL()    ROP_OP(&d[0], col);        \
                      ROP_OP(&d[1], (col >> 8)); \
                      ROP_OP(&d[2], (col >> 16))
#elif DEPTH == 32
#define PUTPIXEL()    ROP_OP_32(((uint32_t *)&d[0]), col)
#else
#error unsupported DEPTH
#endif

static void
glue(glue(glue(cirrus_patternfill_, ROP_NAME), _),DEPTH)
     (CirrusVGAState * s, uint8_t * dst,
      const uint8_t * src,
      int dstpitch, int srcpitch,
      int bltwidth, int bltheight)
{
    uint8_t *d;
    int x, y, pattern_y, pattern_pitch, pattern_x;
    unsigned int col;
    const uint8_t *src1;
#if DEPTH == 24
    int skipleft = s->vga.gr[0x2f] & 0x1f;
#else
    int skipleft = (s->vga.gr[0x2f] & 0x07) * (DEPTH / 8);
#endif

#if DEPTH == 8
    pattern_pitch = 8;
#elif DEPTH == 16
    pattern_pitch = 16;
#else
    pattern_pitch = 32;
#endif
    pattern_y = s->cirrus_blt_srcaddr & 7;
    for(y = 0; y < bltheight; y++) {
        pattern_x = skipleft;
        d = dst + skipleft;
        src1 = src + pattern_y * pattern_pitch;
        for (x = skipleft; x < bltwidth; x += (DEPTH / 8)) {
#if DEPTH == 8
            col = src1[pattern_x];
            pattern_x = (pattern_x + 1) & 7;
#elif DEPTH == 16
            col = ((uint16_t *)(src1 + pattern_x))[0];
            pattern_x = (pattern_x + 2) & 15;
#elif DEPTH == 24
            {
                const uint8_t *src2 = src1 + pattern_x * 3;
                col = src2[0] | (src2[1] << 8) | (src2[2] << 16);
                pattern_x = (pattern_x + 1) & 7;
            }
#else
            col = ((uint32_t *)(src1 + pattern_x))[0];
            pattern_x = (pattern_x + 4) & 31;
#endif
            PUTPIXEL();
            d += (DEPTH / 8);
        }
        pattern_y = (pattern_y + 1) & 7;
        dst += dstpitch;
    }
}

/* NOTE: srcpitch is ignored */
static void
glue(glue(glue(cirrus_colorexpand_transp_, ROP_NAME), _),DEPTH)
     (CirrusVGAState * s, uint8_t * dst,
      const uint8_t * src,
      int dstpitch, int srcpitch,
      int bltwidth, int bltheight)
{
    uint8_t *d;
    int x, y;
    unsigned bits, bits_xor;
    unsigned int col;
    unsigned bitmask;
    unsigned index;
#if DEPTH == 24
    int dstskipleft = s->vga.gr[0x2f] & 0x1f;
    int srcskipleft = dstskipleft / 3;
#else
    int srcskipleft = s->vga.gr[0x2f] & 0x07;
    int dstskipleft = srcskipleft * (DEPTH / 8);
#endif

    if (s->cirrus_blt_modeext & CIRRUS_BLTMODEEXT_COLOREXPINV) {
        bits_xor = 0xff;
        col = s->cirrus_blt_bgcol;
    } else {
        bits_xor = 0x00;
        col = s->cirrus_blt_fgcol;
    }

    for(y = 0; y < bltheight; y++) {
        bitmask = 0x80 >> srcskipleft;
        bits = *src++ ^ bits_xor;
        d = dst + dstskipleft;
        for (x = dstskipleft; x < bltwidth; x += (DEPTH / 8)) {
            if ((bitmask & 0xff) == 0) {
                bitmask = 0x80;
                bits = *src++ ^ bits_xor;
            }
            index = (bits & bitmask);
            if (index) {
                PUTPIXEL();
            }
            d += (DEPTH / 8);
            bitmask >>= 1;
        }
        dst += dstpitch;
    }
}

static void
glue(glue(glue(cirrus_colorexpand_, ROP_NAME), _),DEPTH)
     (CirrusVGAState * s, uint8_t * dst,
      const uint8_t * src,
      int dstpitch, int srcpitch,
      int bltwidth, int bltheight)
{
    uint32_t colors[2];
    uint8_t *d;
    int x, y;
    unsigned bits;
    unsigned int col;
    unsigned bitmask;
    int srcskipleft = s->vga.gr[0x2f] & 0x07;
    int dstskipleft = srcskipleft * (DEPTH / 8);

    colors[0] = s->cirrus_blt_bgcol;
    colors[1] = s->cirrus_blt_fgcol;
    for(y = 0; y < bltheight; y++) {
        bitmask = 0x80 >> srcskipleft;
        bits = *src++;
        d = dst + dstskipleft;
        for (x = dstskipleft; x < bltwidth; x += (DEPTH / 8)) {
            if ((bitmask & 0xff) == 0) {
                bitmask = 0x80;
                bits = *src++;
            }
            col = colors[!!(bits & bitmask)];
            PUTPIXEL();
            d += (DEPTH / 8);
            bitmask >>= 1;
        }
        dst += dstpitch;
    }
}

static void
glue(glue(glue(cirrus_colorexpand_pattern_transp_, ROP_NAME), _),DEPTH)
     (CirrusVGAState * s, uint8_t * dst,
      const uint8_t * src,
      int dstpitch, int srcpitch,
      int bltwidth, int bltheight)
{
    uint8_t *d;
    int x, y, bitpos, pattern_y;
    unsigned int bits, bits_xor;
    unsigned int col;
#if DEPTH == 24
    int dstskipleft = s->vga.gr[0x2f] & 0x1f;
    int srcskipleft = dstskipleft / 3;
#else
    int srcskipleft = s->vga.gr[0x2f] & 0x07;
    int dstskipleft = srcskipleft * (DEPTH / 8);
#endif

    if (s->cirrus_blt_modeext & CIRRUS_BLTMODEEXT_COLOREXPINV) {
        bits_xor = 0xff;
        col = s->cirrus_blt_bgcol;
    } else {
        bits_xor = 0x00;
        col = s->cirrus_blt_fgcol;
    }
    pattern_y = s->cirrus_blt_srcaddr & 7;

    for(y = 0; y < bltheight; y++) {
        bits = src[pattern_y] ^ bits_xor;
        bitpos = 7 - srcskipleft;
        d = dst + dstskipleft;
        for (x = dstskipleft; x < bltwidth; x += (DEPTH / 8)) {
            if ((bits >> bitpos) & 1) {
                PUTPIXEL();
            }
            d += (DEPTH / 8);
            bitpos = (bitpos - 1) & 7;
        }
        pattern_y = (pattern_y + 1) & 7;
        dst += dstpitch;
    }
}

static void
glue(glue(glue(cirrus_colorexpand_pattern_, ROP_NAME), _),DEPTH)
     (CirrusVGAState * s, uint8_t * dst,
      const uint8_t * src,
      int dstpitch, int srcpitch,
      int bltwidth, int bltheight)
{
    uint32_t colors[2];
    uint8_t *d;
    int x, y, bitpos, pattern_y;
    unsigned int bits;
    unsigned int col;
    int srcskipleft = s->vga.gr[0x2f] & 0x07;
    int dstskipleft = srcskipleft * (DEPTH / 8);

    colors[0] = s->cirrus_blt_bgcol;
    colors[1] = s->cirrus_blt_fgcol;
    pattern_y = s->cirrus_blt_srcaddr & 7;

    for(y = 0; y < bltheight; y++) {
        bits = src[pattern_y];
        bitpos = 7 - srcskipleft;
        d = dst + dstskipleft;
        for (x = dstskipleft; x < bltwidth; x += (DEPTH / 8)) {
            col = colors[(bits >> bitpos) & 1];
            PUTPIXEL();
            d += (DEPTH / 8);
            bitpos = (bitpos - 1) & 7;
        }
        pattern_y = (pattern_y + 1) & 7;
        dst += dstpitch;
    }
}

static void
glue(glue(glue(cirrus_fill_, ROP_NAME), _),DEPTH)
     (CirrusVGAState *s,
      uint8_t *dst, int dst_pitch,
      int width, int height)
{
    uint8_t *d, *d1;
    uint32_t col;
    int x, y;

    col = s->cirrus_blt_fgcol;

    d1 = dst;
    for(y = 0; y < height; y++) {
        d = d1;
        for(x = 0; x < width; x += (DEPTH / 8)) {
            PUTPIXEL();
            d += (DEPTH / 8);
        }
        d1 += dst_pitch;
    }
}

#undef DEPTH
#undef PUTPIXEL
