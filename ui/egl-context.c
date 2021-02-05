#include "qemu/osdep.h"
#include "ui/egl-context.h"

QEMUGLContext qemu_egl_create_context(DisplayChangeListener *dcl,
                                      QEMUGLParams *params)
{
   EGLContext ctx;
   EGLint ctx_att_core[] = {
       EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
       EGL_CONTEXT_CLIENT_VERSION, params->major_ver,
       EGL_CONTEXT_MINOR_VERSION_KHR, params->minor_ver,
       EGL_NONE
   };
   EGLint ctx_att_gles[] = {
       EGL_CONTEXT_CLIENT_VERSION, params->major_ver,
       EGL_CONTEXT_MINOR_VERSION_KHR, params->minor_ver,
       EGL_NONE
   };
   bool gles = (qemu_egl_mode == DISPLAYGL_MODE_ES);

   ctx = eglCreateContext(qemu_egl_display, qemu_egl_config,
                          eglGetCurrentContext(),
                          gles ? ctx_att_gles : ctx_att_core);
   return ctx;
}

void qemu_egl_destroy_context(DisplayChangeListener *dcl, QEMUGLContext ctx)
{
    eglDestroyContext(qemu_egl_display, ctx);
}

int qemu_egl_make_context_current(DisplayChangeListener *dcl,
                                  QEMUGLContext ctx)
{
   return eglMakeCurrent(qemu_egl_display,
                         EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
}
