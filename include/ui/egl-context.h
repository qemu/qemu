#ifndef EGL_CONTEXT_H
#define EGL_CONTEXT_H

#include "ui/console.h"
#include "ui/egl-helpers.h"

QEMUGLContext qemu_egl_create_context(DisplayChangeListener *dcl,
                                      QEMUGLParams *params);
void qemu_egl_destroy_context(DisplayChangeListener *dcl, QEMUGLContext ctx);
int qemu_egl_make_context_current(DisplayChangeListener *dcl,
                                  QEMUGLContext ctx);

#endif /* EGL_CONTEXT_H */
