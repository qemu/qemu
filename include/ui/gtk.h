#ifndef UI_GTK_H
#define UI_GTK_H

#ifdef _WIN32
# define _WIN32_WINNT 0x0601 /* needed to get definition of MAPVK_VK_TO_VSC */
#endif

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
} VirtualGfxConsole;

#if defined(CONFIG_VTE)
typedef struct VirtualVteConsole {
    GtkWidget *box;
    GtkWidget *scrollbar;
    GtkWidget *terminal;
    CharDriverState *chr;
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

#endif /* UI_GTK_H */
