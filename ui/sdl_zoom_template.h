/*
 * SDL_zoom_template - surface scaling
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

#if BPP == 16
#define SDL_TYPE Uint16
#elif BPP == 32
#define SDL_TYPE Uint32
#else
#error unsupport depth
#endif

/*  
 *  Simple helper functions to make the code looks nicer
 *
 *  Assume spf = source SDL_PixelFormat
 *         dpf = dest SDL_PixelFormat
 *
 */
#define getRed(color)   (((color) & spf->Rmask) >> spf->Rshift)
#define getGreen(color) (((color) & spf->Gmask) >> spf->Gshift)
#define getBlue(color)  (((color) & spf->Bmask) >> spf->Bshift)
#define getAlpha(color) (((color) & spf->Amask) >> spf->Ashift)

#define setRed(r, pcolor) do { \
    *pcolor = ((*pcolor) & (~(dpf->Rmask))) + \
              (((r) & (dpf->Rmask >> dpf->Rshift)) << dpf->Rshift); \
} while (0);

#define setGreen(g, pcolor) do { \
    *pcolor = ((*pcolor) & (~(dpf->Gmask))) + \
              (((g) & (dpf->Gmask >> dpf->Gshift)) << dpf->Gshift); \
} while (0);

#define setBlue(b, pcolor) do { \
    *pcolor = ((*pcolor) & (~(dpf->Bmask))) + \
              (((b) & (dpf->Bmask >> dpf->Bshift)) << dpf->Bshift); \
} while (0);

#define setAlpha(a, pcolor) do { \
    *pcolor = ((*pcolor) & (~(dpf->Amask))) + \
              (((a) & (dpf->Amask >> dpf->Ashift)) << dpf->Ashift); \
} while (0);

static void glue(sdl_zoom_rgb, BPP)(SDL_Surface *src, SDL_Surface *dst, int smooth,
                                   SDL_Rect *dst_rect)
{
    int x, y, sx, sy, *sax, *say, *csax, *csay, csx, csy, ex, ey, t1, t2, sstep, sstep_jump;
    SDL_TYPE *c00, *c01, *c10, *c11, *sp, *csp, *dp;
    int d_gap;
    SDL_PixelFormat *spf = src->format;
    SDL_PixelFormat *dpf = dst->format;

    if (smooth) { 
        /* For interpolation: assume source dimension is one pixel.
         * Smaller here to avoid overflow on right and bottom edge.
         */
        sx = (int) (65536.0 * (float) (src->w - 1) / (float) dst->w);
        sy = (int) (65536.0 * (float) (src->h - 1) / (float) dst->h);
    } else {
        sx = (int) (65536.0 * (float) src->w / (float) dst->w);
        sy = (int) (65536.0 * (float) src->h / (float) dst->h);
    }

    sax = g_new(int, dst->w + 1);
    say = g_new(int, dst->h + 1);

    sp = csp = (SDL_TYPE *) src->pixels;
    dp = (SDL_TYPE *) (dst->pixels + dst_rect->y * dst->pitch +
                       dst_rect->x * dst->format->BytesPerPixel);

    csx = 0;
    csax = sax;
    for (x = 0; x <= dst->w; x++) {
        *csax = csx;
        csax++;
        csx &= 0xffff;
        csx += sx;
    }
    csy = 0;
    csay = say;
    for (y = 0; y <= dst->h; y++) {
        *csay = csy;
        csay++;
        csy &= 0xffff;
        csy += sy;
    }

    d_gap = dst->pitch - dst_rect->w * dst->format->BytesPerPixel;

    if (smooth) {
        csay = say;
        for (y = 0; y < dst_rect->y; y++) {
            csay++;
            sstep = (*csay >> 16) * src->pitch;
            csp = (SDL_TYPE *) ((Uint8 *) csp + sstep);
        }

        /* Calculate sstep_jump */
        csax = sax; 
        sstep_jump = 0;
        for (x = 0; x < dst_rect->x; x++) {
            csax++; 
            sstep = (*csax >> 16);
            sstep_jump += sstep;
        }

        for (y = 0; y < dst_rect->h ; y++) {
            /* Setup colour source pointers */
            c00 = csp + sstep_jump;
            c01 = c00 + 1;
            c10 = (SDL_TYPE *) ((Uint8 *) csp + src->pitch) + sstep_jump;
            c11 = c10 + 1;
            csax = sax + dst_rect->x; 

            for (x = 0; x < dst_rect->w; x++) {

                /* Interpolate colours */
                ex = (*csax & 0xffff);
                ey = (*csay & 0xffff);
                t1 = ((((getRed(*c01) - getRed(*c00)) * ex) >> 16) +
                     getRed(*c00)) & (dpf->Rmask >> dpf->Rshift);
                t2 = ((((getRed(*c11) - getRed(*c10)) * ex) >> 16) +
                     getRed(*c10)) & (dpf->Rmask >> dpf->Rshift);
                setRed((((t2 - t1) * ey) >> 16) + t1, dp);
                t1 = ((((getGreen(*c01) - getGreen(*c00)) * ex) >> 16) +
                     getGreen(*c00)) & (dpf->Gmask >> dpf->Gshift);
                t2 = ((((getGreen(*c11) - getGreen(*c10)) * ex) >> 16) +
                     getGreen(*c10)) & (dpf->Gmask >> dpf->Gshift);
                setGreen((((t2 - t1) * ey) >> 16) + t1, dp);
                t1 = ((((getBlue(*c01) - getBlue(*c00)) * ex) >> 16) +
                     getBlue(*c00)) & (dpf->Bmask >> dpf->Bshift);
                t2 = ((((getBlue(*c11) - getBlue(*c10)) * ex) >> 16) +
                     getBlue(*c10)) & (dpf->Bmask >> dpf->Bshift);
                setBlue((((t2 - t1) * ey) >> 16) + t1, dp);
                t1 = ((((getAlpha(*c01) - getAlpha(*c00)) * ex) >> 16) +
                     getAlpha(*c00)) & (dpf->Amask >> dpf->Ashift);
                t2 = ((((getAlpha(*c11) - getAlpha(*c10)) * ex) >> 16) +
                     getAlpha(*c10)) & (dpf->Amask >> dpf->Ashift);
                setAlpha((((t2 - t1) * ey) >> 16) + t1, dp); 

                /* Advance source pointers */
                csax++; 
                sstep = (*csax >> 16);
                c00 += sstep;
                c01 += sstep;
                c10 += sstep;
                c11 += sstep;
                /* Advance destination pointer */
                dp++;
            }
            /* Advance source pointer */
            csay++;
            csp = (SDL_TYPE *) ((Uint8 *) csp + (*csay >> 16) * src->pitch);
            /* Advance destination pointers */
            dp = (SDL_TYPE *) ((Uint8 *) dp + d_gap);
        }


    } else {
        csay = say;

        for (y = 0; y < dst_rect->y; y++) {
            csay++;
            sstep = (*csay >> 16) * src->pitch;
            csp = (SDL_TYPE *) ((Uint8 *) csp + sstep);
        }

        /* Calculate sstep_jump */
        csax = sax; 
        sstep_jump = 0;
        for (x = 0; x < dst_rect->x; x++) {
            csax++; 
            sstep = (*csax >> 16);
            sstep_jump += sstep;
        }

        for (y = 0 ; y < dst_rect->h ; y++) {
            sp = csp + sstep_jump;
            csax = sax + dst_rect->x;

            for (x = 0; x < dst_rect->w; x++) {

                /* Draw */
                *dp = *sp;

                /* Advance source pointers */
                csax++;
                sstep = (*csax >> 16);
                sp += sstep;

                /* Advance destination pointer */
                dp++;
            }
            /* Advance source pointers */
            csay++;
            sstep = (*csay >> 16) * src->pitch;
            csp = (SDL_TYPE *) ((Uint8 *) csp + sstep);

            /* Advance destination pointer */
            dp = (SDL_TYPE *) ((Uint8 *) dp + d_gap);
        }
    }

    g_free(sax);
    g_free(say);
}

#undef SDL_TYPE

