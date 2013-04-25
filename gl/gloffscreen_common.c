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
#include <string.h>

#include "qemu-common.h"
#include "gloffscreen.h"

#ifdef __APPLE__
#include <OpenGL/gl.h>
#elif defined(_WIN32)
#include <GL/gl.h>
#include <GL/glext.h>
#else
#include <GL/gl.h>
#endif

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


void glo_readpixels(GLenum gl_format, GLenum gl_type,
                    unsigned int bytes_per_pixel, unsigned int stride,
                    unsigned int width, unsigned int height, void *data)
{
    /* TODO: weird strides */
    assert(stride % bytes_per_pixel == 0);

    /* Save guest processes GL state before we ReadPixels() */
    int rl, pa;
    glGetIntegerv(GL_PACK_ROW_LENGTH, &rl);
    glGetIntegerv(GL_PACK_ALIGNMENT, &pa);
    glPixelStorei(GL_PACK_ROW_LENGTH, stride / bytes_per_pixel);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

#ifdef GETCONTENTS_INDIVIDUAL
    GLubyte *b = (GLubyte *) data;
    int irow;

    for (irow = height - 1; irow >= 0; irow--) {
        glReadPixels(0, irow, width, 1, gl_format, gl_type, b);
        b += stride;
    }
#else
    /* Faster buffer flip */
    GLubyte *b = (GLubyte *) data;
    GLubyte *c = &((GLubyte *) data)[stride * (height - 1)];
    GLubyte *tmp = (GLubyte *) g_malloc(stride);
    int irow;

    glReadPixels(0, 0, width, height, gl_format, gl_type, data);

    for (irow = 0; irow < height / 2; irow++) {
        memcpy(tmp, b, stride);
        memcpy(b, c, stride);
        memcpy(c, tmp, stride);
        b += stride;
        c -= stride;
    }
    g_free(tmp);
#endif

    /* Restore GL state */
    glPixelStorei(GL_PACK_ROW_LENGTH, rl);
    glPixelStorei(GL_PACK_ALIGNMENT, pa);
}
