/*
 * Pixel drawing function templates for QEMU SM501 Device
 *
 * Copyright (c) 2008 Shin-ichiro KAWASAKI
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

static void draw_line8_32(uint8_t *d, const uint8_t *s, int width,
                          const uint32_t *pal)
{
    uint8_t v, r, g, b;
    do {
        v = ldub_p(s);
        r = (pal[v] >> 16) & 0xff;
        g = (pal[v] >>  8) & 0xff;
        b = (pal[v] >>  0) & 0xff;
        *(uint32_t *)d = rgb_to_pixel32(r, g, b);
        s++;
        d += 4;
    } while (--width != 0);
}

static void draw_line16_32(uint8_t *d, const uint8_t *s, int width,
                           const uint32_t *pal)
{
    uint16_t rgb565;
    uint8_t r, g, b;

    do {
        rgb565 = lduw_le_p(s);
        r = (rgb565 >> 8) & 0xf8;
        g = (rgb565 >> 3) & 0xfc;
        b = (rgb565 << 3) & 0xf8;
        *(uint32_t *)d = rgb_to_pixel32(r, g, b);
        s += 2;
        d += 4;
    } while (--width != 0);
}

static void draw_line32_32(uint8_t *d, const uint8_t *s, int width,
                           const uint32_t *pal)
{
    uint8_t r, g, b;

    do {
        r = s[2];
        g = s[1];
        b = s[0];
        *(uint32_t *)d = rgb_to_pixel32(r, g, b);
        s += 4;
        d += 4;
    } while (--width != 0);
}

/**
 * Draw hardware cursor image on the given line.
 */
static void draw_hwc_line_32(uint8_t *d, const uint8_t *s, int width,
                             const uint8_t *palette, int c_x, int c_y)
{
    int i;
    uint8_t r, g, b, v, bitset = 0;

    /* get cursor position */
    assert(0 <= c_y && c_y < SM501_HWC_HEIGHT);
    s += SM501_HWC_WIDTH * c_y / 4;  /* 4 pixels per byte */
    d += c_x * 4;

    for (i = 0; i < SM501_HWC_WIDTH && c_x + i < width; i++) {
        /* get pixel value */
        if (i % 4 == 0) {
            bitset = ldub_p(s);
            s++;
        }
        v = bitset & 3;
        bitset >>= 2;

        /* write pixel */
        if (v) {
            v--;
            r = palette[v * 3 + 0];
            g = palette[v * 3 + 1];
            b = palette[v * 3 + 2];
            *(uint32_t *)d = rgb_to_pixel32(r, g, b);
        }
        d += 4;
    }
}
