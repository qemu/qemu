#ifndef EGL_HELPERS_H
#define EGL_HELPERS_H

#include <epoxy/gl.h>
#include <epoxy/egl.h>

extern EGLDisplay *qemu_egl_display;
extern EGLConfig qemu_egl_config;

EGLSurface qemu_egl_init_surface_x11(EGLContext ectx, Window win);

int qemu_egl_init_dpy(EGLNativeDisplayType dpy, bool gles, bool debug);
EGLContext qemu_egl_init_ctx(void);
bool qemu_egl_has_ext(const char *haystack, const char *needle);

#endif /* EGL_HELPERS_H */
