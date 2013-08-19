/*
 * QEMU Cirrus VGA Emulator templates
 *
 * Copyright (c) 2003 Fabrice Bellard
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
#define BPP 1
#elif DEPTH == 15 || DEPTH == 16
#define BPP 2
#elif DEPTH == 32
#define BPP 4
#else
#error unsupported depth
#endif

static void glue(vga_draw_cursor_line_, DEPTH)(uint8_t *d1,
                                               const uint8_t *src1,
                                               int poffset, int w,
                                               unsigned int color0,
                                               unsigned int color1,
                                               unsigned int color_xor)
{
    const uint8_t *plane0, *plane1;
    int x, b0, b1;
    uint8_t *d;

    d = d1;
    plane0 = src1;
    plane1 = src1 + poffset;
    for (x = 0; x < w; x++) {
        b0 = (plane0[x >> 3] >> (7 - (x & 7))) & 1;
        b1 = (plane1[x >> 3] >> (7 - (x & 7))) & 1;
#if DEPTH == 8
        switch (b0 | (b1 << 1)) {
        case 0:
            break;
        case 1:
            d[0] ^= color_xor;
            break;
        case 2:
            d[0] = color0;
            break;
        case 3:
            d[0] = color1;
            break;
        }
#elif DEPTH == 16
        switch (b0 | (b1 << 1)) {
        case 0:
            break;
        case 1:
            ((uint16_t *)d)[0] ^= color_xor;
            break;
        case 2:
            ((uint16_t *)d)[0] = color0;
            break;
        case 3:
            ((uint16_t *)d)[0] = color1;
            break;
        }
#elif DEPTH == 32
        switch (b0 | (b1 << 1)) {
        case 0:
            break;
        case 1:
            ((uint32_t *)d)[0] ^= color_xor;
            break;
        case 2:
            ((uint32_t *)d)[0] = color0;
            break;
        case 3:
            ((uint32_t *)d)[0] = color1;
            break;
        }
#else
#error unsupported depth
#endif
        d += BPP;
    }
}

#undef DEPTH
#undef BPP
