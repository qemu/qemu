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
#define PUTPIXEL()    ROP_OP(d[0], col)
#elif DEPTH == 16
#define PUTPIXEL()    ROP_OP(((uint16_t *)d)[0], col);
#elif DEPTH == 24
#define PUTPIXEL()    ROP_OP(d[0], col); \
                      ROP_OP(d[1], (col >> 8)); \
                      ROP_OP(d[2], (col >> 16))
#elif DEPTH == 32
#define PUTPIXEL()    ROP_OP(((uint32_t *)d)[0], col)
#else
#error unsupported DEPTH
#endif                

static void
glue(glue(glue(cirrus_colorexpand_transp_, ROP_NAME), _),DEPTH)
     (CirrusVGAState * s, uint8_t * dst,
      const uint8_t * src1, 
      int dstpitch, int srcpitch, 
      int bltwidth, int bltheight)
{
    const uint8_t *src;
    uint8_t *d;
    int x, y;
    unsigned bits;
    unsigned int col;
    unsigned bitmask;
    unsigned index;
    int srcskipleft = 0;

    col = s->cirrus_blt_fgcol;
    for(y = 0; y < bltheight; y++) {
        src = src1;
        bitmask = 0x80 >> srcskipleft;
        bits = *src++;
        d = dst;
        for (x = 0; x < bltwidth; x += (DEPTH / 8)) {
            if ((bitmask & 0xff) == 0) {
                bitmask = 0x80;
                bits = *src++;
            }
            index = (bits & bitmask);
            if (index) {
                PUTPIXEL();
            }
            d += (DEPTH / 8);
            bitmask >>= 1;
        }
        src1 += srcpitch;
        dst += dstpitch;
    }
}

static void
glue(glue(glue(cirrus_colorexpand_, ROP_NAME), _),DEPTH)
     (CirrusVGAState * s, uint8_t * dst,
      const uint8_t * src1, 
      int dstpitch, int srcpitch, 
      int bltwidth, int bltheight)
{
    const uint8_t *src;
    uint32_t colors[2];
    uint8_t *d;
    int x, y;
    unsigned bits;
    unsigned int col;
    unsigned bitmask;
    int srcskipleft = 0;

    colors[0] = s->cirrus_blt_bgcol;
    colors[1] = s->cirrus_blt_fgcol;
    for(y = 0; y < bltheight; y++) {
        src = src1;
        bitmask = 0x80 >> srcskipleft;
        bits = *src++;
        d = dst;
        for (x = 0; x < bltwidth; x += (DEPTH / 8)) {
            if ((bitmask & 0xff) == 0) {
                bitmask = 0x80;
                bits = *src++;
            }
            col = colors[!!(bits & bitmask)];
            PUTPIXEL();
            d += (DEPTH / 8);
            bitmask >>= 1;
        }
        src1 += srcpitch;
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
