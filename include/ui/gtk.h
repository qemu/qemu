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

#if defined(CONFIG_OPENGL)
#include "ui/egl-helpers.h"
#include "ui/egl-context.h"
#endif

/* Compatibility define to let us build on both Gtk2 and Gtk3 */
#if GTK_CHECK_VERSION(3, 0, 0)
static inline void gdk_drawable_get_size(GdkWindow *w, gint *ww, gint *wh)
{
    *ww = gdk_window_get_width(w);
    *wh = gdk_window_get_height(w);
}
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
    ConsoleGLState *gls;
    EGLContext ectx;
    EGLSurface esurface;
    int glupdates;
    int x, y, w, h;
    GLuint tex_id;
    GLuint fbo_id;
    bool y0_top;
    bool scanout_mode;
#endif
} VirtualGfxConsole;

#if defined(CONFIG_VTE)
typedef struct VirtualVteConsole {
    GtkWidget *box;
    GtkWidget *scrollbar;
    GtkWidget *terminal;
    CharDriverState *chr;
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
void gd_egl_scanout(DisplayChangeListener *dcl,
                    uint32_t backing_id, bool backing_y_0_top,
                    uint32_t x, uint32_t y,
                    uint32_t w, uint32_t h);
void gd_egl_scanout_flush(DisplayChangeListener *dcl,
                          uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void gtk_egl_init(void);
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
void gd_gl_area_scanout(DisplayChangeListener *dcl,
                        uint32_t backing_id, bool backing_y_0_top,
                        uint32_t x, uint32_t y,
                        uint32_t w, uint32_t h);
void gd_gl_area_scanout_flush(DisplayChangeListener *dcl,
                              uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void gtk_gl_area_init(void);
QEMUGLContext gd_gl_area_get_current_context(DisplayChangeListener *dcl);
int gd_gl_area_make_current(DisplayChangeListener *dcl,
                            QEMUGLContext ctx);

#endif /* UI_GTK_H */
