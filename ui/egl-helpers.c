#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>

#include "ui/egl-helpers.h"

EGLDisplay *qemu_egl_display;
EGLConfig qemu_egl_config;

/* ---------------------------------------------------------------------- */

static bool egl_gles;
static int egl_debug;

#define egl_dbg(_x ...)                          \
    do {                                         \
        if (egl_debug) {                         \
            fprintf(stderr, "egl: " _x);         \
        }                                        \
    } while (0);

/* ---------------------------------------------------------------------- */

EGLSurface qemu_egl_init_surface_x11(EGLContext ectx, Window win)
{
    EGLSurface esurface;
    EGLBoolean b;

    egl_dbg("eglCreateWindowSurface (x11 win id 0x%lx) ...\n",
            (unsigned long) win);
    esurface = eglCreateWindowSurface(qemu_egl_display,
                                      qemu_egl_config,
                                      (EGLNativeWindowType)win, NULL);
    if (esurface == EGL_NO_SURFACE) {
        fprintf(stderr, "egl: eglCreateWindowSurface failed\n");
        return NULL;
    }

    b = eglMakeCurrent(qemu_egl_display, esurface, esurface, ectx);
    if (b == EGL_FALSE) {
        fprintf(stderr, "egl: eglMakeCurrent failed\n");
        return NULL;
    }

    return esurface;
}

/* ---------------------------------------------------------------------- */

int qemu_egl_init_dpy(EGLNativeDisplayType dpy, bool gles, bool debug)
{
    static const EGLint conf_att_gl[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE,   5,
        EGL_GREEN_SIZE, 5,
        EGL_BLUE_SIZE,  5,
        EGL_ALPHA_SIZE, 0,
        EGL_NONE,
    };
    static const EGLint conf_att_gles[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,   5,
        EGL_GREEN_SIZE, 5,
        EGL_BLUE_SIZE,  5,
        EGL_ALPHA_SIZE, 0,
        EGL_NONE,
    };
    EGLint major, minor;
    EGLBoolean b;
    EGLint n;

    if (debug) {
        egl_debug = 1;
        setenv("EGL_LOG_LEVEL", "debug", true);
        setenv("LIBGL_DEBUG", "verbose", true);
    }

    egl_dbg("eglGetDisplay (dpy %p) ...\n", dpy);
    qemu_egl_display = eglGetDisplay(dpy);
    if (qemu_egl_display == EGL_NO_DISPLAY) {
        fprintf(stderr, "egl: eglGetDisplay failed\n");
        return -1;
    }

    egl_dbg("eglInitialize ...\n");
    b = eglInitialize(qemu_egl_display, &major, &minor);
    if (b == EGL_FALSE) {
        fprintf(stderr, "egl: eglInitialize failed\n");
        return -1;
    }

    egl_dbg("eglBindAPI ...\n");
    b = eglBindAPI(gles ? EGL_OPENGL_ES_API : EGL_OPENGL_API);
    if (b == EGL_FALSE) {
        fprintf(stderr, "egl: eglBindAPI failed\n");
        return -1;
    }

    egl_dbg("eglChooseConfig ...\n");
    b = eglChooseConfig(qemu_egl_display,
                        gles ? conf_att_gles : conf_att_gl,
                        &qemu_egl_config, 1, &n);
    if (b == EGL_FALSE || n != 1) {
        fprintf(stderr, "egl: eglChooseConfig failed\n");
        return -1;
    }

    egl_gles = gles;
    return 0;
}

EGLContext qemu_egl_init_ctx(void)
{
    static const EGLint ctx_att_gl[] = {
        EGL_NONE
    };
    static const EGLint ctx_att_gles[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    EGLContext ectx;
    EGLBoolean b;

    egl_dbg("eglCreateContext ...\n");
    ectx = eglCreateContext(qemu_egl_display, qemu_egl_config, EGL_NO_CONTEXT,
                            egl_gles ? ctx_att_gles : ctx_att_gl);
    if (ectx == EGL_NO_CONTEXT) {
        fprintf(stderr, "egl: eglCreateContext failed\n");
        return NULL;
    }

    b = eglMakeCurrent(qemu_egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, ectx);
    if (b == EGL_FALSE) {
        fprintf(stderr, "egl: eglMakeCurrent failed\n");
        return NULL;
    }

    return ectx;
}
