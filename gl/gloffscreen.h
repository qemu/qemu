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

#include <stdbool.h>

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#include <OpenGL/glext.h>
#else
#include <GL/glew.h>
#include <GL/gl.h>
#endif

/* Used to hold data for the OpenGL context */
struct _GloContext;
typedef struct _GloContext GloContext;

/* Change current context */
void glo_set_current(GloContext *context);

/* Check GL Extensions */
bool glo_check_extension(const char* ext_name);
void* glo_get_extension_proc(const char* extProc);

/* Create an OpenGL context */
GloContext *glo_context_create(void);

/* Destroy a previouslu created OpenGL context */
void glo_context_destroy(GloContext *context);

 /* Note that this is top-down, not bottom-up as glReadPixels would do. */
void glo_readpixels(GLenum gl_format, GLenum gl_type,
                    unsigned int bytes_per_pixel, unsigned int stride,
                    unsigned int width, unsigned int height, void *data);
 
#endif /* GLOFFSCREEN_H_ */
