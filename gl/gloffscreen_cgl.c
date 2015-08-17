/*
 *  Offscreen OpenGL abstraction layer - CGL (Apple) specific
 *
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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>

#include "qemu-common.h"

#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLTypes.h>
#include <OpenGL/CGLCurrent.h>

#include "gloffscreen.h"

struct _GloContext {
  CGLContextObj     cglContext;
};

/* Create an OpenGL context for a certain pixel format. formatflags are from 
 * the GLO_ constants */
GloContext *glo_context_create(void)
{
    CGLError err;

    GloContext *context = (GloContext *)g_malloc0(sizeof(GloContext));

    /* pixel format attributes */
    CGLPixelFormatAttribute attributes[] = {
        kCGLPFAAccelerated,
        kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)kCGLOGLPVersion_GL3_Core,
        (CGLPixelFormatAttribute)0
    };

    CGLPixelFormatObj pix;
    GLint num;
    err = CGLChoosePixelFormat(attributes, &pix, &num);
    if (err) return NULL;

    err = CGLCreateContext(pix, NULL, &context->cglContext);
    if (err) return NULL;

    CGLDestroyPixelFormat(pix);

    glo_set_current(context);

    return context;
}

void* glo_get_extension_proc(const char* ext_proc)
{
    return dlsym(RTLD_NEXT, ext_proc);
}

/* Set current context */
void glo_set_current(GloContext *context)
{
    if (context == NULL) {
        CGLSetCurrentContext(NULL);
    } else {
        CGLSetCurrentContext(context->cglContext);
    }
}

/* Destroy a previously created OpenGL context */
void glo_context_destroy(GloContext *context)
{
    if (!context) return;
    glo_set_current(NULL);
    CGLDestroyContext(context->cglContext);
}
