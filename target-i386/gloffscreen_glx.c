/*
 *  Offscreen OpenGL abstraction layer - GLX specific
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/gl.h>
#include <GL/glx.h>

#include <sys/shm.h>
#include <X11/extensions/XShm.h>

struct GloMain {
  Display *dpy;
  int use_ximage;
  GloSurface *curr_surface;
};
struct GloMain glo;
int glo_inited = 0;

struct _GloContext {
  GLuint                formatFlags;
  GLXFBConfig           fbConfig;
  GLXContext            context;
};

#define MAX_CTX 128
#define MAX_SURF 128
static GloContext *ctx_arr[MAX_CTX];

/* ------------------------------------------------------------------------ */

int glo_initialised(void)
{
    return glo_inited;
}

/* Initialise gloffscreen */
void glo_init(void)
{
    if (glo_inited) {
        printf("gloffscreen already inited\n");
        exit(EXIT_FAILURE);
    }
    /* Open a connection to the X server */
    glo.dpy = XOpenDisplay(NULL);
    if (glo.dpy == NULL) {
        printf("Unable to open a connection to the X server\n");
        exit(EXIT_FAILURE);
    }
    glo_inited = 1;
}

/* Uninitialise gloffscreen */
void glo_kill(void)
{
    XCloseDisplay(glo.dpy);
    glo.dpy = NULL;
}

/* Create an OpenGL context for a certain pixel format. formatflags
 * are from the GLO_ constants */
GloContext *glo_context_create(int formatFlags)
{
    if (!glo_inited) {
        glo_init();
    }

    GLXFBConfig          *fbConfigs;
    int                   numReturned;
    GloContext           *context;
    int                   rgbaBits[4];
    int                   bufferAttributes[] = {
      GLX_DRAWABLE_TYPE, GLX_PIXMAP_BIT,
      GLX_RENDER_TYPE,   GLX_RGBA_BIT,
      GLX_RED_SIZE,      8,
      GLX_GREEN_SIZE,    8,
      GLX_BLUE_SIZE,     8,
      GLX_ALPHA_SIZE,    8,
      GLX_DEPTH_SIZE,    0,
      GLX_STENCIL_SIZE,  0,
      None
    };

    if (!glo_inited)
        glo_init();

    /* set up the surface format from the flags we were given */
    glo_flags_get_rgba_bits(formatFlags, rgbaBits);
    bufferAttributes[5]  = rgbaBits[0];
    bufferAttributes[7]  = rgbaBits[1];
    bufferAttributes[9]  = rgbaBits[2];
    bufferAttributes[11] = rgbaBits[3];
    bufferAttributes[13] = glo_flags_get_depth_bits(formatFlags);
    bufferAttributes[15] = glo_flags_get_stencil_bits(formatFlags);

    fbConfigs = glXChooseFBConfig(glo.dpy, DefaultScreen(glo.dpy),
                                 bufferAttributes, &numReturned);
    if (numReturned == 0) {
        printf("No matching configs found.\n");
        exit(EXIT_FAILURE);
    }
    context = (GloContext *)malloc(sizeof(GloContext));
    memset(context, 0, sizeof(GloContext));
    context->formatFlags = formatFlags;
    context->fbConfig = fbConfigs[0];

    /* Create a GLX context for OpenGL rendering */
    context->context = glXCreateNewContext(glo.dpy, context->fbConfig,
                                         GLX_RGBA_TYPE,
                                         NULL,
                                         True);

    if (!context->context) {
        printf("glXCreateNewContext failed\n");
        exit(EXIT_FAILURE);
    }
    {
        int i;
        for (i = 0; i < MAX_CTX; i++) {
            if (ctx_arr[i] == NULL) {
                ctx_arr[i] = context;
                break;
            }
        }
    }
    fprintf(stderr, "Nct: %p\n", context->context);

    return context;
}

/* Destroy a previously created OpenGL context */
void glo_context_destroy(GloContext *context)
{
    {
        int i;
        if (!context) fprintf(stderr, "CTX NOT FOUND NULL\n");
        for (i = 0 ; i < MAX_CTX ; i++) {
            if (ctx_arr[i] == context) {
                ctx_arr[i] = NULL;
                break;
            }
        }
        if (i == MAX_CTX) {
            fprintf(stderr, "CTX NOT FOUND %p\n", context);
        }
        for (i = 0 ; i < MAX_SURF ; i++) {
            if (sur_arr[i]) {
                if (sur_arr[i]->context == context) {
                    fprintf(stderr, "In USE! %p\n", sur_arr[i]);
                }
            }
        }
    }


    if (!context) {
        return;
    }
    /* TODO: check for GloSurfaces using this? */
    fprintf(stderr, "Dst: %p\n", context->context);
    glXDestroyContext(glo.dpy, context->context);
    free(context);
}
