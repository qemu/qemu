#ifndef UI_GTK_H
#define UI_GTK_H

#ifdef CONFIG_PRAGMA_DIAGNOSTIC_AVAILABLE
/* Work around an -Wstrict-prototypes warning in GTK headers */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#endif
#include <gtk/gtk.h>
#ifdef CONFIG_PRAGMA_DIAGNOSTIC_AVAILABLE
#pragma GCC diagnostic pop
#endif

#include <gdk/gdkkeysyms.h>

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#include <X11/XKBlib.h>
#endif

#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif

#if defined(CONFIG_OPENGL)
#include "ui/egl-helpers.h"
#include "ui/egl-context.h"
#endif

typedef struct GtkDisplayState GtkDisplayState;

typedef struct VirtualGfxConsole {
    GtkWidget *drawing_area;
    DisplayChangeListener dcl;
    DisplaySurface *ds;
    pixman_image_t *convert;
    cairo_surface_t *surface;
    double scale_x;
    double scale_y;
#if defined(CONFIG_OPENGL)
    QemuGLShader *gls;
    EGLContext ectx;
    EGLSurface esurface;
    int glupdates;
    int x, y, w, h;
    egl_fb guest_fb;
    egl_fb win_fb;
    egl_fb cursor_fb;
    int cursor_x;
    int cursor_y;
    bool y0_top;
    bool scanout_mode;
#endif
} VirtualGfxConsole;

#if defined(CONFIG_VTE)
typedef struct VirtualVteConsole {
    GtkWidget *box;
    GtkWidget *scrollbar;
    GtkWidget *terminal;
    Chardev *chr;
    bool echo;
} VirtualVteConsole;
#endif

typedef enum VirtualConsoleType {
    GD_VC_GFX,
    GD_VC_VTE,
} VirtualConsoleType;

typedef struct VirtualConsole {
    GtkDisplayState *s;
    char *label;
    GtkWidget *window;
    GtkWidget *menu_item;
    GtkWidget *tab_item;
    GtkWidget *focus;
    VirtualConsoleType type;
    union {
        VirtualGfxConsole gfx;
#if defined(CONFIG_VTE)
        VirtualVteConsole vte;
#endif
    };
} VirtualConsole;

extern bool gtk_use_gl_area;

/* ui/gtk.c */
void gd_update_windowsize(VirtualConsole *vc);

/* ui/gtk-egl.c */
void gd_egl_init(VirtualConsole *vc);
void gd_egl_draw(VirtualConsole *vc);
void gd_egl_update(DisplayChangeListener *dcl,
                   int x, int y, int w, int h);
void gd_egl_refresh(DisplayChangeListener *dcl);
void gd_egl_switch(DisplayChangeListener *dcl,
                   DisplaySurface *surface);
QEMUGLContext gd_egl_create_context(DisplayChangeListener *dcl,
                                    QEMUGLParams *params);
void gd_egl_scanout_disable(DisplayChangeListener *dcl);
void gd_egl_scanout_texture(DisplayChangeListener *dcl,
                            uint32_t backing_id,
                            bool backing_y_0_top,
                            uint32_t backing_width,
                            uint32_t backing_height,
                            uint32_t x, uint32_t y,
                            uint32_t w, uint32_t h);
void gd_egl_scanout_dmabuf(DisplayChangeListener *dcl,
                           QemuDmaBuf *dmabuf);
void gd_egl_cursor_dmabuf(DisplayChangeListener *dcl,
                          QemuDmaBuf *dmabuf, bool have_hot,
                          uint32_t hot_x, uint32_t hot_y);
void gd_egl_cursor_position(DisplayChangeListener *dcl,
                            uint32_t pos_x, uint32_t pos_y);
void gd_egl_release_dmabuf(DisplayChangeListener *dcl,
                           QemuDmaBuf *dmabuf);
void gd_egl_scanout_flush(DisplayChangeListener *dcl,
                          uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void gtk_egl_init(DisplayGLMode mode);
int gd_egl_make_current(DisplayChangeListener *dcl,
                        QEMUGLContext ctx);

/* ui/gtk-gl-area.c */
void gd_gl_area_init(VirtualConsole *vc);
void gd_gl_area_draw(VirtualConsole *vc);
void gd_gl_area_update(DisplayChangeListener *dcl,
                       int x, int y, int w, int h);
void gd_gl_area_refresh(DisplayChangeListener *dcl);
void gd_gl_area_switch(DisplayChangeListener *dcl,
                       DisplaySurface *surface);
QEMUGLContext gd_gl_area_create_context(DisplayChangeListener *dcl,
                                        QEMUGLParams *params);
void gd_gl_area_destroy_context(DisplayChangeListener *dcl,
                                QEMUGLContext ctx);
void gd_gl_area_scanout_texture(DisplayChangeListener *dcl,
                                uint32_t backing_id,
                                bool backing_y_0_top,
                                uint32_t backing_width,
                                uint32_t backing_height,
                                uint32_t x, uint32_t y,
                                uint32_t w, uint32_t h);
void gd_gl_area_scanout_flush(DisplayChangeListener *dcl,
                              uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void gtk_gl_area_init(void);
QEMUGLContext gd_gl_area_get_current_context(DisplayChangeListener *dcl);
int gd_gl_area_make_current(DisplayChangeListener *dcl,
                            QEMUGLContext ctx);

#endif /* UI_GTK_H */
