#ifndef EGL_HELPERS_H
#define EGL_HELPERS_H

#include <epoxy/gl.h>
#include <epoxy/egl.h>
#include <gbm.h>

extern EGLDisplay *qemu_egl_display;
extern EGLConfig qemu_egl_config;

#ifdef CONFIG_OPENGL_DMABUF

extern int qemu_egl_rn_fd;
extern struct gbm_device *qemu_egl_rn_gbm_dev;
extern EGLContext qemu_egl_rn_ctx;

int qemu_egl_rendernode_open(void);
int egl_rendernode_init(void);
int egl_get_fd_for_texture(uint32_t tex_id, EGLint *stride, EGLint *fourcc);

#endif

EGLSurface qemu_egl_init_surface_x11(EGLContext ectx, Window win);

int qemu_egl_init_dpy(EGLNativeDisplayType dpy, bool gles, bool debug);
EGLContext qemu_egl_init_ctx(void);

#endif /* EGL_HELPERS_H */
