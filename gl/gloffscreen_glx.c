/*
 *  Offscreen OpenGL abstraction layer - GLX (X11) specific
 *
 *  Copyright (c) 2013 Wayo
 *  Copyright (c) 2014 JayFoxRox
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

#include "qemu-common.h"

#include <GL/glew.h>
#include <GL/glx.h>
#include <GL/glut.h>
#include <X11/Xlib.h>

#include "gloffscreen.h"

struct _GloContext {
    GLXDrawable     glx_drawable;
    GLXContext      glx_context;
};

static Display* x_display;


/* Create an OpenGL context for a certain pixel format. formatflags are from 
 * the GLO_ constants */
GloContext *glo_context_create(int formatFlags)
{

    static bool initialized = false;

    if (!initialized) {    
        x_display = XOpenDisplay(0);     
	      printf("gloffscreen: GLX_VERSION = %s\n", glXGetClientString(x_display, GLX_VERSION));
	      printf("gloffscreen: GLX_VENDOR = %s\n", glXGetClientString(x_display, GLX_VENDOR));
    } else {
        printf("gloffscreen already inited\n");
        exit(EXIT_FAILURE);
    }
    GloContext *context = (GloContext *)g_malloc0(sizeof(GloContext));

    int rgbaBits[4];
    glo_flags_get_rgba_bits(formatFlags, rgbaBits);

    int fb_attribute_list[] = {
        GLX_RENDER_TYPE, GLX_RGBA_BIT,
        GLX_RED_SIZE, rgbaBits[0],
        GLX_GREEN_SIZE, rgbaBits[1],
        GLX_BLUE_SIZE, rgbaBits[2],
        GLX_ALPHA_SIZE, rgbaBits[3],
        GLX_DEPTH_SIZE, glo_flags_get_depth_bits(formatFlags),
        GLX_STENCIL_SIZE, glo_flags_get_stencil_bits(formatFlags),
        GLX_DRAWABLE_TYPE, GLX_PBUFFER_BIT,
        None
    };

    int nelements;
    GLXFBConfig* configs = glXChooseFBConfig(x_display, DefaultScreen(x_display), fb_attribute_list, &nelements);
    if (configs == NULL) { return NULL; }
    if (nelements == 0) { return NULL; }

#if 1
    /* Tiny surface because apitrace doesn't handle no surface yet */
    int surface_attribute_list[] = {
        GLX_PBUFFER_WIDTH,16,
        GLX_PBUFFER_HEIGHT,16,
        GLX_LARGEST_PBUFFER, True,
        None
    };
    context->glx_drawable = glXCreatePbuffer(x_display, configs[0], surface_attribute_list);
    if (context->glx_drawable == None) { return NULL; }
#else
    context->glx_drawable = None;
#endif

    /* Create GLX context */
    context->glx_context = glXCreateNewContext(x_display, configs[0], GLX_RGBA_TYPE, NULL, True);
    if (context->glx_context == NULL) return NULL;
    glo_set_current(context);

    if (!initialized) {
        /* Initialize glew */
        if (GLEW_OK != glewInit()) {
            /* GLEW failed! */
            printf("Glew init failed.");
            exit(1);
        }
    }

    initialized = true;
    return context;
}

/* Check if an extension is available. */
GLboolean glo_check_extension(const GLubyte *extName,
    const GLubyte *extString)
{
    return gluCheckExtension(extName, extString);
}

void* glo_get_extension_proc(const GLubyte *extProc)
{
    return glXGetProcAddress(extProc); 
}

/* Set current context */
void glo_set_current(GloContext *context)
{
    if (context == NULL) {
        glXMakeCurrent(x_display, None, NULL);
    } else {
        glXMakeCurrent(x_display, context->glx_drawable, context->glx_context);
    }
}

/* Destroy a previously created OpenGL context */
void glo_context_destroy(GloContext *context)
{
    if (!context) { return; }
    glo_set_current(NULL);
   	glXDestroyContext(x_display, context->glx_context);
}

