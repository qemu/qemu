/*
 *  Offscreen OpenGL abstraction layer - EGL specific
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
#include <EGL/egl.h>
#include <GL/glut.h>

#include "gloffscreen.h"

struct _GloContext {
    EGLSurface     egl_surface;
    EGLContext     egl_context;
};

static EGLDisplay egl_display;

static const char* eglGetErrorString(void)
{
    EGLint err = eglGetError();
    if (err == EGL_SUCCESS            ) { return "";                        }
    if (err == EGL_NOT_INITIALIZED    ) { return "EGL_NOT_INITIALIZED";     }
    if (err == EGL_BAD_ACCESS         ) { return "EGL_BAD_ACCESS";          }
    if (err == EGL_BAD_ALLOC          ) { return "EGL_BAD_ALLOC";           }
    if (err == EGL_BAD_ATTRIBUTE      ) { return "EGL_BAD_ATTRIBUTE";       }
    if (err == EGL_BAD_CONTEXT        ) { return "EGL_BAD_CONTEXT";         }
    if (err == EGL_BAD_CONFIG         ) { return "EGL_BAD_CONFIG";          }
    if (err == EGL_BAD_CURRENT_SURFACE) { return "EGL_BAD_CURRENT_SURFACE"; }
    if (err == EGL_BAD_DISPLAY        ) { return "EGL_BAD_DISPLAY";         }
    if (err == EGL_BAD_SURFACE        ) { return "EGL_BAD_SURFACE";         }
    if (err == EGL_BAD_MATCH          ) { return "EGL_BAD_MATCH";           }
    if (err == EGL_BAD_PARAMETER      ) { return "EGL_BAD_PARAMETER";       }
    if (err == EGL_BAD_NATIVE_PIXMAP  ) { return "EGL_BAD_NATIVE_PIXMAP";   }
    if (err == EGL_BAD_NATIVE_WINDOW  ) { return "EGL_BAD_NATIVE_WINDOW";   }
    if (err == EGL_CONTEXT_LOST       ) { return "EGL_CONTEXT_LOST";        }
    return "<Unknown EGL Error>";
}

/* Create an OpenGL context for a certain pixel format. formatflags are from 
 * the GLO_ constants */
GloContext *glo_context_create(int formatFlags)
{
    EGLint err;

    static bool initialized = false;

    if (!initialized) {
        EGLint major, minor;
        egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (egl_display == EGL_NO_DISPLAY) { return NULL; }
        err = eglInitialize(egl_display, &major, &minor);
        eglBindAPI(EGL_OPENGL_API); // Necessary once to make sure all EGL calls are done properly here
	      printf("gloffscreen: EGL version = %d.%d\n", major, minor);
	      printf("gloffscreen: EGL_VENDOR = %s\n", eglQueryString(egl_display, EGL_VENDOR));
        if (err != EGL_TRUE) { return NULL; }
    } else {
        printf("gloffscreen already inited\n");
        exit(EXIT_FAILURE);
    }
    GloContext *context = (GloContext *)g_malloc0(sizeof(GloContext));

    int          rgbaBits[4];
    glo_flags_get_rgba_bits(formatFlags, rgbaBits);

    EGLint attr[] = {
        EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE,        rgbaBits[0],
        EGL_GREEN_SIZE,      rgbaBits[1],
        EGL_BLUE_SIZE,       rgbaBits[2],
        EGL_ALPHA_SIZE,    rgbaBits[3],
        EGL_DEPTH_SIZE,      glo_flags_get_depth_bits(formatFlags),
        EGL_STENCIL_SIZE,    glo_flags_get_stencil_bits(formatFlags),
        EGL_NONE
    };
    EGLConfig  config;
    EGLint     num_config;
    err = eglChooseConfig(egl_display, attr, &config, 1, &num_config);
    if (err != EGL_TRUE) { return NULL; }
    if (num_config != 1) { return NULL; }

#if 1
    /* Tiny surface because apitrace doesn't handle no surface yet */
    EGLint surface_attr[] = {
        EGL_WIDTH,16,
        EGL_HEIGHT,16,
        EGL_LARGEST_PBUFFER, EGL_TRUE,
        EGL_NONE
    };

    context->egl_surface = eglCreatePbufferSurface(egl_display, config, surface_attr);
    if (context->egl_surface == EGL_NO_SURFACE) { return NULL; }
#else
    context->egl_surface = EGL_NO_SURFACE;
#endif

    EGLint ctxattr[] = {
      EGL_NONE
    };
    context->egl_context = eglCreateContext(egl_display, config, EGL_NO_CONTEXT, ctxattr);
    if (context->egl_context == EGL_NO_CONTEXT) return NULL;
    glo_set_current(context);

    if (!initialized) {
/*TODO: Enable once glew supports EGL.. */
#if 0
        /* Initialize glew */
        if (GLEW_OK != glewInit()) {
            /* GLEW failed! */
            printf("Glew init failed.");
            exit(1);
        }
#endif
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
    return eglGetProcAddress((const char*)extProc); 
}

/* Set current context */
void glo_set_current(GloContext *context)
{
    eglBindAPI(EGL_OPENGL_API);
    if (context == NULL) {
        eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    } else {
        eglMakeCurrent(egl_display, context->egl_surface, context->egl_surface, context->egl_context);
    }
}

/* Destroy a previously created OpenGL context */
void glo_context_destroy(GloContext *context)
{
    if (!context) { return; }
    glo_set_current(NULL);
    eglDestroyContext(egl_display, context->egl_context);
    if (context->egl_surface == EGL_NO_SURFACE) { return; }
    eglDestroySurface(egl_display, context->egl_surface);
}


