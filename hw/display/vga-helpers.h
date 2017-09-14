/*
 * QEMU VGA Emulator templates
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

static inline void vga_draw_glyph_line(uint8_t *d, uint32_t font_data,
                                       uint32_t xorcol, uint32_t bgcol)
{
        ((uint32_t *)d)[0] = (-((font_data >> 7)) & xorcol) ^ bgcol;
        ((uint32_t *)d)[1] = (-((font_data >> 6) & 1) & xorcol) ^ bgcol;
        ((uint32_t *)d)[2] = (-((font_data >> 5) & 1) & xorcol) ^ bgcol;
        ((uint32_t *)d)[3] = (-((font_data >> 4) & 1) & xorcol) ^ bgcol;
        ((uint32_t *)d)[4] = (-((font_data >> 3) & 1) & xorcol) ^ bgcol;
        ((uint32_t *)d)[5] = (-((font_data >> 2) & 1) & xorcol) ^ bgcol;
        ((uint32_t *)d)[6] = (-((font_data >> 1) & 1) & xorcol) ^ bgcol;
        ((uint32_t *)d)[7] = (-((font_data >> 0) & 1) & xorcol) ^ bgcol;
}

static void vga_draw_glyph8(uint8_t *d, int linesize,
                            const uint8_t *font_ptr, int h,
                            uint32_t fgcol, uint32_t bgcol)
{
    uint32_t font_data, xorcol;

    xorcol = bgcol ^ fgcol;
    do {
        font_data = font_ptr[0];
        vga_draw_glyph_line(d, font_data, xorcol, bgcol);
        font_ptr += 4;
        d += linesize;
    } while (--h);
}

static void vga_draw_glyph16(uint8_t *d, int linesize,
                                          const uint8_t *font_ptr, int h,
                                          uint32_t fgcol, uint32_t bgcol)
{
    uint32_t font_data, xorcol;

    xorcol = bgcol ^ fgcol;
    do {
        font_data = font_ptr[0];
        vga_draw_glyph_line(d, expand4to8[font_data >> 4],
                            xorcol, bgcol);
        vga_draw_glyph_line(d + 32, expand4to8[font_data & 0x0f],
                            xorcol, bgcol);
        font_ptr += 4;
        d += linesize;
    } while (--h);
}

static void vga_draw_glyph9(uint8_t *d, int linesize,
                            const uint8_t *font_ptr, int h,
                            uint32_t fgcol, uint32_t bgcol, int dup9)
{
    uint32_t font_data, xorcol, v;

    xorcol = bgcol ^ fgcol;
    do {
        font_data = font_ptr[0];
        ((uint32_t *)d)[0] = (-((font_data >> 7)) & xorcol) ^ bgcol;
        ((uint32_t *)d)[1] = (-((font_data >> 6) & 1) & xorcol) ^ bgcol;
        ((uint32_t *)d)[2] = (-((font_data >> 5) & 1) & xorcol) ^ bgcol;
        ((uint32_t *)d)[3] = (-((font_data >> 4) & 1) & xorcol) ^ bgcol;
        ((uint32_t *)d)[4] = (-((font_data >> 3) & 1) & xorcol) ^ bgcol;
        ((uint32_t *)d)[5] = (-((font_data >> 2) & 1) & xorcol) ^ bgcol;
        ((uint32_t *)d)[6] = (-((font_data >> 1) & 1) & xorcol) ^ bgcol;
        v = (-((font_data >> 0) & 1) & xorcol) ^ bgcol;
        ((uint32_t *)d)[7] = v;
        if (dup9)
            ((uint32_t *)d)[8] = v;
        else
            ((uint32_t *)d)[8] = bgcol;
        font_ptr += 4;
        d += linesize;
    } while (--h);
}

static inline uint8_t vga_read_byte(VGACommonState *vga, uint32_t addr)
{
    return vga->vram_ptr[addr & vga->vbe_size_mask];
}

static inline uint16_t vga_read_word_le(VGACommonState *vga, uint32_t addr)
{
    uint32_t offset = addr & vga->vbe_size_mask & ~1;
    uint16_t *ptr = (uint16_t *)(vga->vram_ptr + offset);
    return lduw_le_p(ptr);
}

static inline uint16_t vga_read_word_be(VGACommonState *vga, uint32_t addr)
{
    uint32_t offset = addr & vga->vbe_size_mask & ~1;
    uint16_t *ptr = (uint16_t *)(vga->vram_ptr + offset);
    return lduw_be_p(ptr);
}

static inline uint32_t vga_read_dword_le(VGACommonState *vga, uint32_t addr)
{
    uint32_t offset = addr & vga->vbe_size_mask & ~3;
    uint32_t *ptr = (uint32_t *)(vga->vram_ptr + offset);
    return ldl_le_p(ptr);
}

/*
 * 4 color mode
 */
static void vga_draw_line2(VGACommonState *vga, uint8_t *d,
                           uint32_t addr, int width)
{
    uint32_t plane_mask, *palette, data, v;
    int x;

    palette = vga->last_palette;
    plane_mask = mask16[vga->ar[VGA_ATC_PLANE_ENABLE] & 0xf];
    width >>= 3;
    for(x = 0; x < width; x++) {
        data = vga_read_dword_le(vga, addr);
        data &= plane_mask;
        v = expand2[GET_PLANE(data, 0)];
        v |= expand2[GET_PLANE(data, 2)] << 2;
        ((uint32_t *)d)[0] = palette[v >> 12];
        ((uint32_t *)d)[1] = palette[(v >> 8) & 0xf];
        ((uint32_t *)d)[2] = palette[(v >> 4) & 0xf];
        ((uint32_t *)d)[3] = palette[(v >> 0) & 0xf];

        v = expand2[GET_PLANE(data, 1)];
        v |= expand2[GET_PLANE(data, 3)] << 2;
        ((uint32_t *)d)[4] = palette[v >> 12];
        ((uint32_t *)d)[5] = palette[(v >> 8) & 0xf];
        ((uint32_t *)d)[6] = palette[(v >> 4) & 0xf];
        ((uint32_t *)d)[7] = palette[(v >> 0) & 0xf];
        d += 32;
        addr += 4;
    }
}

#define PUT_PIXEL2(d, n, v) \
((uint32_t *)d)[2*(n)] = ((uint32_t *)d)[2*(n)+1] = (v)

/*
 * 4 color mode, dup2 horizontal
 */
static void vga_draw_line2d2(VGACommonState *vga, uint8_t *d,
                             uint32_t addr, int width)
{
    uint32_t plane_mask, *palette, data, v;
    int x;

    palette = vga->last_palette;
    plane_mask = mask16[vga->ar[VGA_ATC_PLANE_ENABLE] & 0xf];
    width >>= 3;
    for(x = 0; x < width; x++) {
        data = vga_read_dword_le(vga, addr);
        data &= plane_mask;
        v = expand2[GET_PLANE(data, 0)];
        v |= expand2[GET_PLANE(data, 2)] << 2;
        PUT_PIXEL2(d, 0, palette[v >> 12]);
        PUT_PIXEL2(d, 1, palette[(v >> 8) & 0xf]);
        PUT_PIXEL2(d, 2, palette[(v >> 4) & 0xf]);
        PUT_PIXEL2(d, 3, palette[(v >> 0) & 0xf]);

        v = expand2[GET_PLANE(data, 1)];
        v |= expand2[GET_PLANE(data, 3)] << 2;
        PUT_PIXEL2(d, 4, palette[v >> 12]);
        PUT_PIXEL2(d, 5, palette[(v >> 8) & 0xf]);
        PUT_PIXEL2(d, 6, palette[(v >> 4) & 0xf]);
        PUT_PIXEL2(d, 7, palette[(v >> 0) & 0xf]);
        d += 64;
        addr += 4;
    }
}

/*
 * 16 color mode
 */
static void vga_draw_line4(VGACommonState *vga, uint8_t *d,
                           uint32_t addr, int width)
{
    uint32_t plane_mask, data, v, *palette;
    int x;

    palette = vga->last_palette;
    plane_mask = mask16[vga->ar[VGA_ATC_PLANE_ENABLE] & 0xf];
    width >>= 3;
    for(x = 0; x < width; x++) {
        data = vga_read_dword_le(vga, addr);
        data &= plane_mask;
        v = expand4[GET_PLANE(data, 0)];
        v |= expand4[GET_PLANE(data, 1)] << 1;
        v |= expand4[GET_PLANE(data, 2)] << 2;
        v |= expand4[GET_PLANE(data, 3)] << 3;
        ((uint32_t *)d)[0] = palette[v >> 28];
        ((uint32_t *)d)[1] = palette[(v >> 24) & 0xf];
        ((uint32_t *)d)[2] = palette[(v >> 20) & 0xf];
        ((uint32_t *)d)[3] = palette[(v >> 16) & 0xf];
        ((uint32_t *)d)[4] = palette[(v >> 12) & 0xf];
        ((uint32_t *)d)[5] = palette[(v >> 8) & 0xf];
        ((uint32_t *)d)[6] = palette[(v >> 4) & 0xf];
        ((uint32_t *)d)[7] = palette[(v >> 0) & 0xf];
        d += 32;
        addr += 4;
    }
}

/*
 * 16 color mode, dup2 horizontal
 */
static void vga_draw_line4d2(VGACommonState *vga, uint8_t *d,
                             uint32_t addr, int width)
{
    uint32_t plane_mask, data, v, *palette;
    int x;

    palette = vga->last_palette;
    plane_mask = mask16[vga->ar[VGA_ATC_PLANE_ENABLE] & 0xf];
    width >>= 3;
    for(x = 0; x < width; x++) {
        data = vga_read_dword_le(vga, addr);
        data &= plane_mask;
        v = expand4[GET_PLANE(data, 0)];
        v |= expand4[GET_PLANE(data, 1)] << 1;
        v |= expand4[GET_PLANE(data, 2)] << 2;
        v |= expand4[GET_PLANE(data, 3)] << 3;
        PUT_PIXEL2(d, 0, palette[v >> 28]);
        PUT_PIXEL2(d, 1, palette[(v >> 24) & 0xf]);
        PUT_PIXEL2(d, 2, palette[(v >> 20) & 0xf]);
        PUT_PIXEL2(d, 3, palette[(v >> 16) & 0xf]);
        PUT_PIXEL2(d, 4, palette[(v >> 12) & 0xf]);
        PUT_PIXEL2(d, 5, palette[(v >> 8) & 0xf]);
        PUT_PIXEL2(d, 6, palette[(v >> 4) & 0xf]);
        PUT_PIXEL2(d, 7, palette[(v >> 0) & 0xf]);
        d += 64;
        addr += 4;
    }
}

/*
 * 256 color mode, double pixels
 *
 * XXX: add plane_mask support (never used in standard VGA modes)
 */
static void vga_draw_line8d2(VGACommonState *vga, uint8_t *d,
                             uint32_t addr, int width)
{
    uint32_t *palette;
    int x;

    palette = vga->last_palette;
    width >>= 3;
    for(x = 0; x < width; x++) {
        PUT_PIXEL2(d, 0, palette[vga_read_byte(vga, addr + 0)]);
        PUT_PIXEL2(d, 1, palette[vga_read_byte(vga, addr + 1)]);
        PUT_PIXEL2(d, 2, palette[vga_read_byte(vga, addr + 2)]);
        PUT_PIXEL2(d, 3, palette[vga_read_byte(vga, addr + 3)]);
        d += 32;
        addr += 4;
    }
}

/*
 * standard 256 color mode
 *
 * XXX: add plane_mask support (never used in standard VGA modes)
 */
static void vga_draw_line8(VGACommonState *vga, uint8_t *d,
                           uint32_t addr, int width)
{
    uint32_t *palette;
    int x;

    palette = vga->last_palette;
    width >>= 3;
    for(x = 0; x < width; x++) {
        ((uint32_t *)d)[0] = palette[vga_read_byte(vga, addr + 0)];
        ((uint32_t *)d)[1] = palette[vga_read_byte(vga, addr + 1)];
        ((uint32_t *)d)[2] = palette[vga_read_byte(vga, addr + 2)];
        ((uint32_t *)d)[3] = palette[vga_read_byte(vga, addr + 3)];
        ((uint32_t *)d)[4] = palette[vga_read_byte(vga, addr + 4)];
        ((uint32_t *)d)[5] = palette[vga_read_byte(vga, addr + 5)];
        ((uint32_t *)d)[6] = palette[vga_read_byte(vga, addr + 6)];
        ((uint32_t *)d)[7] = palette[vga_read_byte(vga, addr + 7)];
        d += 32;
        addr += 8;
    }
}

/*
 * 15 bit color
 */
static void vga_draw_line15_le(VGACommonState *vga, uint8_t *d,
                               uint32_t addr, int width)
{
    int w;
    uint32_t v, r, g, b;

    w = width;
    do {
        v = vga_read_word_le(vga, addr);
        r = (v >> 7) & 0xf8;
        g = (v >> 2) & 0xf8;
        b = (v << 3) & 0xf8;
        ((uint32_t *)d)[0] = rgb_to_pixel32(r, g, b);
        addr += 2;
        d += 4;
    } while (--w != 0);
}

static void vga_draw_line15_be(VGACommonState *vga, uint8_t *d,
                               uint32_t addr, int width)
{
    int w;
    uint32_t v, r, g, b;

    w = width;
    do {
        v = vga_read_word_be(vga, addr);
        r = (v >> 7) & 0xf8;
        g = (v >> 2) & 0xf8;
        b = (v << 3) & 0xf8;
        ((uint32_t *)d)[0] = rgb_to_pixel32(r, g, b);
        addr += 2;
        d += 4;
    } while (--w != 0);
}

/*
 * 16 bit color
 */
static void vga_draw_line16_le(VGACommonState *vga, uint8_t *d,
                               uint32_t addr, int width)
{
    int w;
    uint32_t v, r, g, b;

    w = width;
    do {
        v = vga_read_word_le(vga, addr);
        r = (v >> 8) & 0xf8;
        g = (v >> 3) & 0xfc;
        b = (v << 3) & 0xf8;
        ((uint32_t *)d)[0] = rgb_to_pixel32(r, g, b);
        addr += 2;
        d += 4;
    } while (--w != 0);
}

static void vga_draw_line16_be(VGACommonState *vga, uint8_t *d,
                               uint32_t addr, int width)
{
    int w;
    uint32_t v, r, g, b;

    w = width;
    do {
        v = vga_read_word_be(vga, addr);
        r = (v >> 8) & 0xf8;
        g = (v >> 3) & 0xfc;
        b = (v << 3) & 0xf8;
        ((uint32_t *)d)[0] = rgb_to_pixel32(r, g, b);
        addr += 2;
        d += 4;
    } while (--w != 0);
}

/*
 * 24 bit color
 */
static void vga_draw_line24_le(VGACommonState *vga, uint8_t *d,
                               uint32_t addr, int width)
{
    int w;
    uint32_t r, g, b;

    w = width;
    do {
        b = vga_read_byte(vga, addr + 0);
        g = vga_read_byte(vga, addr + 1);
        r = vga_read_byte(vga, addr + 2);
        ((uint32_t *)d)[0] = rgb_to_pixel32(r, g, b);
        addr += 3;
        d += 4;
    } while (--w != 0);
}

static void vga_draw_line24_be(VGACommonState *vga, uint8_t *d,
                               uint32_t addr, int width)
{
    int w;
    uint32_t r, g, b;

    w = width;
    do {
        r = vga_read_byte(vga, addr + 0);
        g = vga_read_byte(vga, addr + 1);
        b = vga_read_byte(vga, addr + 2);
        ((uint32_t *)d)[0] = rgb_to_pixel32(r, g, b);
        addr += 3;
        d += 4;
    } while (--w != 0);
}

/*
 * 32 bit color
 */
static void vga_draw_line32_le(VGACommonState *vga, uint8_t *d,
                               uint32_t addr, int width)
{
    int w;
    uint32_t r, g, b;

    w = width;
    do {
        b = vga_read_byte(vga, addr + 0);
        g = vga_read_byte(vga, addr + 1);
        r = vga_read_byte(vga, addr + 2);
        ((uint32_t *)d)[0] = rgb_to_pixel32(r, g, b);
        addr += 4;
        d += 4;
    } while (--w != 0);
}

static void vga_draw_line32_be(VGACommonState *vga, uint8_t *d,
                               uint32_t addr, int width)
{
    int w;
    uint32_t r, g, b;

    w = width;
    do {
        r = vga_read_byte(vga, addr + 1);
        g = vga_read_byte(vga, addr + 2);
        b = vga_read_byte(vga, addr + 3);
        ((uint32_t *)d)[0] = rgb_to_pixel32(r, g, b);
        addr += 4;
        d += 4;
    } while (--w != 0);
}
