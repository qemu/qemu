#ifndef EGL_CONTEXT_H
#define EGL_CONTEXT_H

#include "ui/console.h"
#include "ui/egl-helpers.h"

QEMUGLContext qemu_egl_create_context(DisplayChangeListener *dcl,
                                      QEMUGLParams *params);
void qemu_egl_destroy_context(DisplayChangeListener *dcl, QEMUGLContext ctx);
int qemu_egl_make_context_current(DisplayChangeListener *dcl,
                                  QEMUGLContext ctx);
QEMUGLContext qemu_egl_get_current_context(DisplayChangeListener *dcl);

#endif /* EGL_CONTEXT_H */
