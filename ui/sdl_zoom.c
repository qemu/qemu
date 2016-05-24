/*
 * SDL_zoom - surface scaling
 * 
 * Copyright (c) 2009 Citrix Systems, Inc.
 *
 * Derived from: SDL_rotozoom,  LGPL (c) A. Schiffler from the SDL_gfx library.
 * Modifications by Stefano Stabellini.
 *
 * This work is licensed under the terms of the GNU GPL version 2.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "sdl_zoom.h"

static void sdl_zoom_rgb16(SDL_Surface *src, SDL_Surface *dst, int smooth,
                           SDL_Rect *dst_rect);
static void sdl_zoom_rgb32(SDL_Surface *src, SDL_Surface *dst, int smooth,
                           SDL_Rect *dst_rect);

#define BPP 32
#include  "sdl_zoom_template.h"
#undef BPP
#define BPP 16
#include  "sdl_zoom_template.h"
#undef BPP

int sdl_zoom_blit(SDL_Surface *src_sfc, SDL_Surface *dst_sfc, int smooth,
                  SDL_Rect *in_rect)
{
    SDL_Rect zoom, src_rect;
    int extra;

    /* Grow the size of the modified rectangle to avoid edge artefacts */
    src_rect.x = (in_rect->x > 0) ? (in_rect->x - 1) : 0;
    src_rect.y = (in_rect->y > 0) ? (in_rect->y - 1) : 0;

    src_rect.w = in_rect->w + 1;
    if (src_rect.x + src_rect.w > src_sfc->w)
        src_rect.w = src_sfc->w - src_rect.x;

    src_rect.h = in_rect->h + 1;
    if (src_rect.y + src_rect.h > src_sfc->h)
        src_rect.h = src_sfc->h - src_rect.y;

    /* (x,y) : round down */
    zoom.x = (int)(((float)(src_rect.x * dst_sfc->w)) / (float)(src_sfc->w));
    zoom.y = (int)(((float)(src_rect.y * dst_sfc->h)) / (float)(src_sfc->h));

    /* (w,h) : round up */
    zoom.w = (int)( ((double)((src_rect.w * dst_sfc->w) + (src_sfc->w - 1))) /
                     (double)(src_sfc->w));

    zoom.h = (int)( ((double)((src_rect.h * dst_sfc->h) + (src_sfc->h - 1))) /
                     (double)(src_sfc->h));

    /* Account for any (x,y) rounding by adding one-source-pixel's worth
     * of destination pixels and then edge checking.
     */

    extra = ((dst_sfc->w-1) / src_sfc->w) + 1;

    if ((zoom.x + zoom.w) < (dst_sfc->w - extra))
        zoom.w += extra;
    else
        zoom.w = dst_sfc->w - zoom.x;

    extra = ((dst_sfc->h-1) / src_sfc->h) + 1;

    if ((zoom.y + zoom.h) < (dst_sfc->h - extra))
        zoom.h += extra;
    else
        zoom.h = dst_sfc->h - zoom.y;

    /* The rectangle (zoom.x, zoom.y, zoom.w, zoom.h) is the area on the
     * destination surface that needs to be updated.
     */
    if (src_sfc->format->BitsPerPixel == 32)
        sdl_zoom_rgb32(src_sfc, dst_sfc, smooth, &zoom);
    else if (src_sfc->format->BitsPerPixel == 16)
        sdl_zoom_rgb16(src_sfc, dst_sfc, smooth, &zoom);
    else {
        fprintf(stderr, "pixel format not supported\n");
        return -1;
    }

    /* Return the rectangle of the update to the caller */
    *in_rect = zoom;

    return 0;
}

