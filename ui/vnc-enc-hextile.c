/*
 * QEMU VNC display driver: hextile encoding
 *
 * Copyright (C) 2006 Anthony Liguori <anthony@codemonkey.ws>
 * Copyright (C) 2006 Fabrice Bellard
 * Copyright (C) 2009 Red Hat, Inc
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

#include "vnc.h"

static void hextile_enc_cord(uint8_t *ptr, int x, int y, int w, int h)
{
    ptr[0] = ((x & 0x0F) << 4) | (y & 0x0F);
    ptr[1] = (((w - 1) & 0x0F) << 4) | ((h - 1) & 0x0F);
}

#define BPP 8
#include "vnc-enc-hextile-template.h"
#undef BPP

#define BPP 16
#include "vnc-enc-hextile-template.h"
#undef BPP

#define BPP 32
#include "vnc-enc-hextile-template.h"
#undef BPP

#define GENERIC
#define BPP 8
#include "vnc-enc-hextile-template.h"
#undef BPP
#undef GENERIC

#define GENERIC
#define BPP 16
#include "vnc-enc-hextile-template.h"
#undef BPP
#undef GENERIC

#define GENERIC
#define BPP 32
#include "vnc-enc-hextile-template.h"
#undef BPP
#undef GENERIC

int vnc_hextile_send_framebuffer_update(VncState *vs, int x,
                                        int y, int w, int h)
{
    int i, j;
    int has_fg, has_bg;
    uint8_t *last_fg, *last_bg;
    VncDisplay *vd = vs->vd;

    last_fg = (uint8_t *) g_malloc(vd->server->pf.bytes_per_pixel);
    last_bg = (uint8_t *) g_malloc(vd->server->pf.bytes_per_pixel);
    has_fg = has_bg = 0;
    for (j = y; j < (y + h); j += 16) {
        for (i = x; i < (x + w); i += 16) {
            vs->hextile.send_tile(vs, i, j,
                                  MIN(16, x + w - i), MIN(16, y + h - j),
                                  last_bg, last_fg, &has_bg, &has_fg);
        }
    }
    free(last_fg);
    free(last_bg);

    return 1;
}

void vnc_hextile_set_pixel_conversion(VncState *vs, int generic)
{
    if (!generic) {
        switch (vs->ds->surface->pf.bits_per_pixel) {
            case 8:
                vs->hextile.send_tile = send_hextile_tile_8;
                break;
            case 16:
                vs->hextile.send_tile = send_hextile_tile_16;
                break;
            case 32:
                vs->hextile.send_tile = send_hextile_tile_32;
                break;
        }
    } else {
        switch (vs->ds->surface->pf.bits_per_pixel) {
            case 8:
                vs->hextile.send_tile = send_hextile_tile_generic_8;
                break;
            case 16:
                vs->hextile.send_tile = send_hextile_tile_generic_16;
                break;
            case 32:
                vs->hextile.send_tile = send_hextile_tile_generic_32;
                break;
        }
    }
}
