/*
 *  Offscreen OpenGL abstraction layer - GLX (X11) specific
 *
 *  Copyright (c) 2013 Wayo
 *  Copyright (c) 2014 Jannik Vogel
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
#include <GL/glxext.h>
#include <X11/Xlib.h>

#include "gloffscreen.h"

struct _GloContext {
    GLXDrawable     glx_drawable;
    GLXContext      glx_context;
};

static Display* x_display;


/* Create an OpenGL context */
GloContext *glo_context_create(void)
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

    int fb_attribute_list[] = {
        GLX_RENDER_TYPE, GLX_RGBA_BIT,
        GLX_RED_SIZE, 8,
        GLX_GREEN_SIZE, 8,
        GLX_BLUE_SIZE, 8,
        GLX_ALPHA_SIZE, 8,
        GLX_DEPTH_SIZE, 24,
        GLX_STENCIL_SIZE, 8,
        GLX_DRAWABLE_TYPE, GLX_PBUFFER_BIT,
        None
    };

    int nelements;
    GLXFBConfig* configs = glXChooseFBConfig(x_display,
                                             DefaultScreen(x_display),
                                             fb_attribute_list, &nelements);
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
    PFNGLXCREATECONTEXTATTRIBSARBPROC glXCreateContextAttribsARB =
        (PFNGLXCREATECONTEXTATTRIBSARBPROC)
            glXGetProcAddress((const GLubyte*)"glXCreateContextAttribsARB");
    if (glXCreateContextAttribsARB == NULL) {
        fprintf(stderr,"GLX doesn't support ARB_create_context extension.\n");
        exit(EXIT_FAILURE);
    }
    int context_attribute_list[] = {
        GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
        GLX_CONTEXT_MINOR_VERSION_ARB, 3,
        GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
        None
    };
    context->glx_context = glXCreateContextAttribsARB(x_display, configs[0], 0, True, context_attribute_list);
    XSync(x_display, False);
    if (context->glx_context == NULL) return NULL;
    glo_set_current(context);

    if (!initialized) {
        /* Initialize glew */
        glewExperimental = GL_TRUE;
        if (GLEW_OK != glewInit()) {
            /* GLEW failed! */
            fprintf(stderr,"GLEW init failed.\n");
            exit(EXIT_FAILURE);
        }

        /* Get rid of GLEW errors */
        while(glGetError() != GL_NO_ERROR);
    }

    initialized = true;
    return context;
}

void* glo_get_extension_proc(const char* ext_proc)
{
    return glXGetProcAddress((const GLubyte *)ext_proc);
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

