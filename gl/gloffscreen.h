/*
 *  Offscreen OpenGL abstraction layer
 *
 *  Copyright (c) 2010 Intel
 *  Written by:
 *    Gordon Williams <gordon.williams@collabora.co.uk>
 *    Ian Molton <ian.molton@collabora.co.uk>
 *  Copyright (c) 2013 Wayo
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

#ifndef GLOFFSCREEN_H_
#define GLOFFSCREEN_H_

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

/* Used to hold data for the OpenGL context */
struct _GloContext;
typedef struct _GloContext GloContext;

/* Format flags for glo_surface_create */
#define GLO_FF_ALPHA_MASK  (0x0001)
#define GLO_FF_NOALPHA     (0x0000)
#define GLO_FF_ALPHA       (0x0001)

#define GLO_FF_BITS_MASK   (0x00F0)
#define GLO_FF_BITS_16     (0x0020)
#define GLO_FF_BITS_24     (0x0030)
#define GLO_FF_BITS_32     (0x0040)

#define GLO_FF_DEPTH_MASK   (0x0F00)
#define GLO_FF_DEPTH_16     (0x0100)
#define GLO_FF_DEPTH_24     (0x0200)
#define GLO_FF_DEPTH_32     (0x0300)

#define GLO_FF_STENCIL_MASK   (0xF000)
#define GLO_FF_STENCIL_8      (0x1000)

/* The only currently supported format */
#define GLO_FF_DEFAULT (GLO_FF_BITS_24|GLO_FF_DEPTH_24)

/* Has gloffscreen been previously initialised? */
extern int glo_initialised(void);

/* Initialise gloffscreen */
extern void glo_init(void);

/* Change current context */
extern void glo_set_current(GloContext *context);

/* Uninitialise gloffscreen */
extern void glo_kill(void);

/* Check GL Extensions */
extern GLboolean glo_check_extension(
    const GLubyte *extName, const GLubyte *extString);

/* Create an OpenGL context for a certain
 * pixel format. formatflags are from the
 * GLO_ constants */
extern GloContext *glo_context_create(int formatFlags);

/* Destroy a previouslu created OpenGL context */
extern void glo_context_destroy(GloContext *context);

/* Functions to decode the format flags */
extern int glo_flags_get_depth_bits(int formatFlags);
extern int glo_flags_get_stencil_bits(int formatFlags);
extern void glo_flags_get_rgba_bits(int formatFlags, int *rgba);
extern int glo_flags_get_bytes_per_pixel(int formatFlags);
extern void glo_flags_get_readpixel_type(int formatFlags, int *glFormat, int *glType);
/* Score how close the given format flags match. 0=great, >0 not so great */
extern int glo_flags_score(int formatFlagsExpected, int formatFlagsReal);

/* Create a set of format flags from a null-terminated list
 * of GLX fbConfig flags. If assumeBooleans is set, items such
 * as GLX_RGBA/GLX_DOUBLEBUFFER are treated as booleans, not key-value pairs
 * (glXChooseVisual treats them as booleans, glXChooseFBConfig as key-value pairs)
 */
extern int glo_flags_get_from_glx(const int *fbConfig, int assumeBooleans);
/* Use in place of glxGetConfig - returns information from flags based on a GLX enum */
extern int glo_get_glx_from_flags(int formatFlags, int glxEnum);


/* In terms of speed, glReadPixels actually seems the best we can do.
 * * On Windows PFB_DRAW_TO_BITMAP is software-only.
 * * http://www.opengl.org/registry/specs/ARB/pixel_buffer_object.txt would be
 * useful if we didn't want the data right away (as we could avoid flushing the
 * pipeline).
 * * The internal data format seems to be GL_BGRA - and this is indeed faster.
 * * Apple suggests using GL_UNSIGNED_INT_8_8_8_8_REV instead of
 * GL_UNSIGNED_BYTE, but there don't appear to be any speed increase from
 * doing this on Windows at least.
 */
 
#endif /* GLOFFSCREEN_H_ */
