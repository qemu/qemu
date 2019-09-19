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
