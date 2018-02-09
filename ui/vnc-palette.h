/*
 * QEMU VNC display driver: palette hash table
 *
 * From libvncserver/libvncserver/tight.c
 * Copyright (C) 2000, 2001 Const Kaplinsky.  All Rights Reserved.
 * Copyright (C) 1999 AT&T Laboratories Cambridge.  All Rights Reserved.
 *
 * Copyright (C) 2010 Corentin Chary <corentin.chary@gmail.com>
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

#ifndef VNC_PALETTE_H
#define VNC_PALETTE_H

#include "qemu/queue.h"

#define VNC_PALETTE_HASH_SIZE 256
#define VNC_PALETTE_MAX_SIZE  256

typedef struct VncPaletteEntry {
    int idx;
    uint32_t color;
    QLIST_ENTRY(VncPaletteEntry) next;
} VncPaletteEntry;

typedef struct VncPalette {
    VncPaletteEntry pool[VNC_PALETTE_MAX_SIZE];
    size_t size;
    size_t max;
    int bpp;
    QLIST_HEAD(,VncPaletteEntry) table[VNC_PALETTE_HASH_SIZE];
} VncPalette;

VncPalette *palette_new(size_t max, int bpp);
void palette_init(VncPalette *palette, size_t max, int bpp);
void palette_destroy(VncPalette *palette);

int palette_put(VncPalette *palette, uint32_t color);
int palette_idx(const VncPalette *palette, uint32_t color);
size_t palette_size(const VncPalette *palette);

void palette_iter(const VncPalette *palette,
                  void (*iter)(int idx, uint32_t color, void *opaque),
                  void *opaque);
uint32_t palette_color(const VncPalette *palette, int idx, bool *found);
size_t palette_fill(const VncPalette *palette,
                    uint32_t colors[VNC_PALETTE_MAX_SIZE]);

#endif /* VNC_PALETTE_H */
