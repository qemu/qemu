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

#ifndef SDL_ZOOM_H
#define SDL_ZOOM_H

#include <SDL.h>

#define SMOOTHING_OFF		0
#define SMOOTHING_ON		1

int sdl_zoom_blit(SDL_Surface *src_sfc, SDL_Surface *dst_sfc,
                  int smooth, SDL_Rect *src_rect);

#endif /* SDL_ZOOM_H */
