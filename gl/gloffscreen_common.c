/*
 *  Offscreen OpenGL abstraction layer - Common utilities
 *
 *  Copyright (c) 2010 Intel
 *  Written by:
 *    Gordon Williams <gordon.williams@collabora.co.uk>
 *    Ian Molton <ian.molton@collabora.co.uk>
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
#include "gloffscreen.h"

#ifdef _WIN32
#include <windows.h>
#include <GL/gl.h>
#include <GL/glext.h>
#else
#include <GL/gl.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------

extern void glo_surface_getcontents_readpixels(int formatFlags, int stride,
                             int bpp, int width, int height, void *data);

// ---------------------------------------------------

int glo_flags_get_depth_bits(int formatFlags) {
  switch ( formatFlags & GLO_FF_DEPTH_MASK ) {
    case GLO_FF_DEPTH_16: return 16;
    case GLO_FF_DEPTH_24: return 24;
    case GLO_FF_DEPTH_32: return 32;
    default: return 0;
  }
}

int glo_flags_get_stencil_bits(int formatFlags) {
  switch ( formatFlags & GLO_FF_STENCIL_MASK ) {
    case GLO_FF_STENCIL_8: return 8;
    default: return 0;
  }
}

void glo_flags_get_rgba_bits(int formatFlags, int *rgba) {
    int alpha = (formatFlags & GLO_FF_ALPHA) != 0;
    switch ( formatFlags & GLO_FF_BITS_MASK ) {
        case GLO_FF_BITS_16:
          rgba[0] = alpha ? 4 : 5;
          rgba[1] = alpha ? 4 : 6;
          rgba[2] = alpha ? 4 : 5;
          rgba[3] = alpha ? 4 : 0;
          break;
        case GLO_FF_BITS_24:
          // ignore alpha
          rgba[0] = 8;
          rgba[1] = 8;
          rgba[2] = 8;
          rgba[3] = 0;
          break;
        case GLO_FF_BITS_32:
          rgba[0] = 8;
          rgba[1] = 8;
          rgba[2] = 8;
          rgba[3] = 8;
          break;
        default:
          rgba[0] = 8;
          rgba[1] = 8;
          rgba[2] = 8;
          rgba[3] = 0;
          break;
      }
}

int glo_flags_get_bytes_per_pixel(int formatFlags) {
    switch ( formatFlags & GLO_FF_BITS_MASK ) {
      case GLO_FF_BITS_16: return 2;
      case GLO_FF_BITS_24: return 3;
      case GLO_FF_BITS_32: return 4;
      default: return 3;
    }
}

void glo_flags_get_readpixel_type(int formatFlags, int *glFormat, int *glType) {
    GLenum gFormat, gType;

    if (formatFlags & GLO_FF_ALPHA) {
      switch ( formatFlags & GLO_FF_BITS_MASK ) {
       case GLO_FF_BITS_16:
         gFormat = GL_RGBA;
         gType = GL_UNSIGNED_SHORT_4_4_4_4;
         break;
       case GLO_FF_BITS_24:
       case GLO_FF_BITS_32:
       default:
         gFormat = GL_BGRA;
         gType = GL_UNSIGNED_BYTE;
         break;
      }
    } else {
      switch ( formatFlags & GLO_FF_BITS_MASK ) {
       case GLO_FF_BITS_16:
         gFormat = GL_RGB;
         gType = GL_UNSIGNED_SHORT_5_6_5;
         break;
       case GLO_FF_BITS_24:
       case GLO_FF_BITS_32:
       default:
         gFormat = GL_BGR;
         gType = GL_UNSIGNED_BYTE;
         break;
      }
    }

    if (glFormat) *glFormat = gFormat;
    if (glType) *glType = gType;
}

int glo_flags_score(int formatFlagsExpected, int formatFlagsReal) {
  if (formatFlagsExpected == formatFlagsReal) return 0;
  int score = 1;
  // we wanted alpha, but we didn't get it
  if ((formatFlagsExpected&GLO_FF_ALPHA_MASK) <
      (formatFlagsReal&GLO_FF_ALPHA_MASK))
    score++;
  // less bits than we expected
  if ((formatFlagsExpected&GLO_FF_BITS_MASK) <
      !(formatFlagsReal&GLO_FF_BITS_MASK))
    score++;
  // less depth bits than we expected
  if ((formatFlagsExpected&GLO_FF_DEPTH_MASK) <
      !(formatFlagsReal&GLO_FF_DEPTH_MASK))
    score++;
  // less stencil bits than we expected
  if ((formatFlagsExpected&GLO_FF_STENCIL_MASK) <
      !(formatFlagsReal&GLO_FF_STENCIL_MASK))
    score++;
  return score;
}

