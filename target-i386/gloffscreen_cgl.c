/*
 *  Offscreen OpenGL abstraction layer - CGL (Apple) specific
 *
 *  Copyright (c) 2010 Intel
 *  Written by: 
 *    Wayo
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

#include <OpenGL/gl.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLTypes.h>
#include <OpenGL/CGLCurrent.h>
#include <GLUT/glut.h>

#include "gloffscreen.h"

/* In Windows, you must create a window *before* you can create a pbuffer or
 * get a context. So we create a hidden Window on startup(see glo_init/GloMain).
 *
 * Also, you can't share contexts that have different pixel formats, so we can't
 * just create a new context from the window. We must create a whole new PBuffer 
 * just for a context :(
 */

struct GloMain {
    int init;
    /* Not needed for CGL? */
};

struct GloMain glo; 
int glo_inited = 0;

struct _GloContext {
  CGLContextObj     cglContext;
};

int glo_initialised(void) {
  return glo_inited;
}

/* Initialise gloffscreen */
void glo_init(void) {
    /* TODO: CGL Implementation.
     * Initialization needed for CGL? */
  
    if (glo_inited) {
        printf( "gloffscreen already inited\n" );
        exit( EXIT_FAILURE );
    }

    glo_inited = 1;
}

/* Uninitialise gloffscreen */
void glo_kill(void) {
    glo_inited = 0;
}

/* Create an OpenGL context for a certain pixel format. formatflags are from 
 * the GLO_ constants */
GloContext *glo_context_create(int formatFlags)
{
    GloContext *context;

    context = (GloContext *)malloc(sizeof(GloContext));
    memset(context, 0, sizeof(GloContext));

    /* pixel format attributes */
     CGLPixelFormatAttribute attributes[] = {
        kCGLPFAAccelerated,
        (CGLPixelFormatAttribute)0
    };

    CGLPixelFormatObj pix;
    GLint num;
    CGLChoosePixelFormat(attributes, &pix, &num);
    CGLCreateContext(pix, NULL, &context->cglContext);
    CGLDestroyPixelFormat(pix);

    if (!glo_inited)
        glo_init();

    glo_set_current(context);

    return context;
}

/* Check if an extension is available. */
GLboolean glo_check_extension(const GLubyte *extName,
    const GLubyte *extString)
{
    return gluCheckExtension(extName, extString);
}

/* Set current context */
void glo_set_current(GloContext *context)
{
    if (context == NULL) {
        CGLSetCurrentContext(NULL);
    }
    else {
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
