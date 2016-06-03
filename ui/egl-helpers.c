#include "qemu/osdep.h"
#include <glob.h>
#include <dirent.h>

#include "qemu/error-report.h"
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

#ifdef CONFIG_OPENGL_DMABUF

int qemu_egl_rn_fd;
struct gbm_device *qemu_egl_rn_gbm_dev;
EGLContext qemu_egl_rn_ctx;

int qemu_egl_rendernode_open(void)
{
    DIR *dir;
    struct dirent *e;
    int r, fd;
    char *p;

    dir = opendir("/dev/dri");
    if (!dir) {
        return -1;
    }

    fd = -1;
    while ((e = readdir(dir))) {
        if (e->d_type != DT_CHR) {
            continue;
        }

        if (strncmp(e->d_name, "renderD", 7)) {
            continue;
        }

        p = g_strdup_printf("/dev/dri/%s", e->d_name);

        r = open(p, O_RDWR | O_CLOEXEC | O_NOCTTY | O_NONBLOCK);
        if (r < 0) {
            g_free(p);
            continue;
        }
        fd = r;
        g_free(p);
        break;
    }

    closedir(dir);
    if (fd < 0) {
        return -1;
    }
    return fd;
}

int egl_rendernode_init(void)
{
    qemu_egl_rn_fd = -1;

    qemu_egl_rn_fd = qemu_egl_rendernode_open();
    if (qemu_egl_rn_fd == -1) {
        error_report("egl: no drm render node available");
        goto err;
    }

    qemu_egl_rn_gbm_dev = gbm_create_device(qemu_egl_rn_fd);
    if (!qemu_egl_rn_gbm_dev) {
        error_report("egl: gbm_create_device failed");
        goto err;
    }

    qemu_egl_init_dpy((EGLNativeDisplayType)qemu_egl_rn_gbm_dev, false, false);

    if (!epoxy_has_egl_extension(qemu_egl_display,
                                 "EGL_KHR_surfaceless_context")) {
        error_report("egl: EGL_KHR_surfaceless_context not supported");
        goto err;
    }
    if (!epoxy_has_egl_extension(qemu_egl_display,
                                 "EGL_MESA_image_dma_buf_export")) {
        error_report("egl: EGL_MESA_image_dma_buf_export not supported");
        goto err;
    }

    qemu_egl_rn_ctx = qemu_egl_init_ctx();
    if (!qemu_egl_rn_ctx) {
        error_report("egl: egl_init_ctx failed");
        goto err;
    }

    return 0;

err:
    if (qemu_egl_rn_gbm_dev) {
        gbm_device_destroy(qemu_egl_rn_gbm_dev);
    }
    if (qemu_egl_rn_fd != -1) {
        close(qemu_egl_rn_fd);
    }

    return -1;
}

int egl_get_fd_for_texture(uint32_t tex_id, EGLint *stride, EGLint *fourcc)
{
    EGLImageKHR image;
    EGLint num_planes, fd;

    image = eglCreateImageKHR(qemu_egl_display, eglGetCurrentContext(),
                              EGL_GL_TEXTURE_2D_KHR,
                              (EGLClientBuffer)(unsigned long)tex_id,
                              NULL);
    if (!image) {
        return -1;
    }

    eglExportDMABUFImageQueryMESA(qemu_egl_display, image, fourcc,
                                  &num_planes, NULL);
    if (num_planes != 1) {
        eglDestroyImageKHR(qemu_egl_display, image);
        return -1;
    }
    eglExportDMABUFImageMESA(qemu_egl_display, image, &fd, stride, NULL);
    eglDestroyImageKHR(qemu_egl_display, image);

    return fd;
}

#endif /* CONFIG_OPENGL_DMABUF */

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
        error_report("egl: eglCreateWindowSurface failed");
        return NULL;
    }

    b = eglMakeCurrent(qemu_egl_display, esurface, esurface, ectx);
    if (b == EGL_FALSE) {
        error_report("egl: eglMakeCurrent failed");
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
        error_report("egl: eglGetDisplay failed");
        return -1;
    }

    egl_dbg("eglInitialize ...\n");
    b = eglInitialize(qemu_egl_display, &major, &minor);
    if (b == EGL_FALSE) {
        error_report("egl: eglInitialize failed");
        return -1;
    }

    egl_dbg("eglBindAPI ...\n");
    b = eglBindAPI(gles ? EGL_OPENGL_ES_API : EGL_OPENGL_API);
    if (b == EGL_FALSE) {
        error_report("egl: eglBindAPI failed");
        return -1;
    }

    egl_dbg("eglChooseConfig ...\n");
    b = eglChooseConfig(qemu_egl_display,
                        gles ? conf_att_gles : conf_att_gl,
                        &qemu_egl_config, 1, &n);
    if (b == EGL_FALSE || n != 1) {
        error_report("egl: eglChooseConfig failed");
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
        error_report("egl: eglCreateContext failed");
        return NULL;
    }

    b = eglMakeCurrent(qemu_egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, ectx);
    if (b == EGL_FALSE) {
        error_report("egl: eglMakeCurrent failed");
        return NULL;
    }

    return ectx;
}
