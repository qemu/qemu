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

#include "vnc-palette.h"
#include <glib.h>
#include <string.h>

static VncPaletteEntry *palette_find(const VncPalette *palette,
                                     uint32_t color, unsigned int hash)
{
    VncPaletteEntry *entry;

    QLIST_FOREACH(entry, &palette->table[hash], next) {
        if (entry->color == color) {
            return entry;
        }
    }

    return NULL;
}

static unsigned int palette_hash(uint32_t rgb, int bpp)
{
    if (bpp == 16) {
        return ((unsigned int)(((rgb >> 8) + rgb) & 0xFF));
    } else {
        return ((unsigned int)(((rgb >> 16) + (rgb >> 8)) & 0xFF));
    }
}

VncPalette *palette_new(size_t max, int bpp)
{
    VncPalette *palette;

    palette = g_malloc0(sizeof(*palette));
    palette_init(palette, max, bpp);
    return palette;
}

void palette_init(VncPalette *palette, size_t max, int bpp)
{
    memset(palette, 0, sizeof (*palette));
    palette->max = max;
    palette->bpp = bpp;
}

void palette_destroy(VncPalette *palette)
{
    g_free(palette);
}

int palette_put(VncPalette *palette, uint32_t color)
{
    unsigned int hash;
    unsigned int idx = palette->size;
    VncPaletteEntry *entry;

    hash = palette_hash(color, palette->bpp) % VNC_PALETTE_HASH_SIZE;
    entry = palette_find(palette, color, hash);

    if (!entry && palette->size >= palette->max) {
        return 0;
    }
    if (!entry) {
        VncPaletteEntry *entry;

        entry = &palette->pool[palette->size];
        entry->color = color;
        entry->idx = idx;
        QLIST_INSERT_HEAD(&palette->table[hash], entry, next);
        palette->size++;
    }
    return palette->size;
}

int palette_idx(const VncPalette *palette, uint32_t color)
{
    VncPaletteEntry *entry;
    unsigned int hash;

    hash = palette_hash(color, palette->bpp) % VNC_PALETTE_HASH_SIZE;
    entry = palette_find(palette, color, hash);
    return (entry == NULL ? -1 : entry->idx);
}

size_t palette_size(const VncPalette *palette)
{
    return palette->size;
}

void palette_iter(const VncPalette *palette,
                  void (*iter)(int idx, uint32_t color, void *opaque),
                  void *opaque)
{
    int i;
    VncPaletteEntry *entry;

    for (i = 0; i < VNC_PALETTE_HASH_SIZE; i++) {
        QLIST_FOREACH(entry, &palette->table[i], next) {
            iter(entry->idx, entry->color, opaque);
        }
    }
}

uint32_t palette_color(const VncPalette *palette, int idx, bool *found)
{
    int i;
    VncPaletteEntry *entry;

    for (i = 0; i < VNC_PALETTE_HASH_SIZE; i++) {
        QLIST_FOREACH(entry, &palette->table[i], next) {
            if (entry->idx == idx) {
                *found = true;
                return entry->color;
            }
        }
    }

    *found = false;
    return -1;
}

static void palette_fill_cb(int idx, uint32_t color, void *opaque)
{
    uint32_t *colors = opaque;

    colors[idx] = color;
}

size_t palette_fill(const VncPalette *palette,
                    uint32_t colors[VNC_PALETTE_MAX_SIZE])
{
    palette_iter(palette, palette_fill_cb, colors);
    return palette_size(palette);
}
