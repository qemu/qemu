#ifndef UI_GTK_H
#define UI_GTK_H

/* Work around an -Wstrict-prototypes warning in GTK headers */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#include <gtk/gtk.h>
#pragma GCC diagnostic pop

#include <gdk/gdkkeysyms.h>

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#include <X11/XKBlib.h>
#endif

#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif

#include "ui/clipboard.h"
#include "ui/console.h"
#include "ui/kbd-state.h"
#if defined(CONFIG_OPENGL)
#include "ui/egl-helpers.h"
#include "ui/egl-context.h"
#endif
#ifdef CONFIG_VTE
#include "qemu/fifo8.h"
#endif

#define MAX_VCS 10

typedef struct GtkDisplayState GtkDisplayState;

typedef struct VirtualGfxConsole {
    GtkWidget *drawing_area;
    DisplayGLCtx dgc;
    DisplayChangeListener dcl;
    QKbdState *kbd;
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
    bool has_dmabuf;
#endif
} VirtualGfxConsole;

#if defined(CONFIG_VTE)
typedef struct VirtualVteConsole {
    GtkWidget *box;
    GtkWidget *scrollbar;
    GtkWidget *terminal;
    Chardev *chr;
    Fifo8 out_fifo;
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

struct GtkDisplayState {
    GtkWidget *window;

    GtkWidget *menu_bar;

    GtkAccelGroup *accel_group;

    GtkWidget *machine_menu_item;
    GtkWidget *machine_menu;
    GtkWidget *pause_item;
    GtkWidget *reset_item;
    GtkWidget *powerdown_item;
    GtkWidget *quit_item;

    GtkWidget *view_menu_item;
    GtkWidget *view_menu;
    GtkWidget *full_screen_item;
    GtkWidget *copy_item;
    GtkWidget *zoom_in_item;
    GtkWidget *zoom_out_item;
    GtkWidget *zoom_fixed_item;
    GtkWidget *zoom_fit_item;
    GtkWidget *grab_item;
    GtkWidget *grab_on_hover_item;

    int nb_vcs;
    VirtualConsole vc[MAX_VCS];

    GtkWidget *show_tabs_item;
    GtkWidget *untabify_item;
    GtkWidget *show_menubar_item;

    GtkWidget *vbox;
    GtkWidget *notebook;
    int button_mask;
    gboolean last_set;
    int last_x;
    int last_y;
    int grab_x_root;
    int grab_y_root;
    VirtualConsole *kbd_owner;
    VirtualConsole *ptr_owner;

    gboolean full_screen;

    GdkCursor *null_cursor;
    Notifier mouse_mode_notifier;
    gboolean free_scale;

    bool external_pause_update;

    QemuClipboardPeer cbpeer;
    uint32_t cbpending[QEMU_CLIPBOARD_SELECTION__COUNT];
    GtkClipboard *gtkcb[QEMU_CLIPBOARD_SELECTION__COUNT];
    bool cbowner[QEMU_CLIPBOARD_SELECTION__COUNT];

    DisplayOptions *opts;
};

extern bool gtk_use_gl_area;

/* ui/gtk.c */
void gd_update_windowsize(VirtualConsole *vc);
void gd_update_monitor_refresh_rate(VirtualConsole *vc, GtkWidget *widget);
void gd_hw_gl_flushed(void *vc);

/* ui/gtk-egl.c */
void gd_egl_init(VirtualConsole *vc);
void gd_egl_draw(VirtualConsole *vc);
void gd_egl_update(DisplayChangeListener *dcl,
                   int x, int y, int w, int h);
void gd_egl_refresh(DisplayChangeListener *dcl);
void gd_egl_switch(DisplayChangeListener *dcl,
                   DisplaySurface *surface);
QEMUGLContext gd_egl_create_context(DisplayGLCtx *dgc,
                                    QEMUGLParams *params);
void gd_egl_scanout_disable(DisplayChangeListener *dcl);
void gd_egl_scanout_texture(DisplayChangeListener *dcl,
                            uint32_t backing_id,
                            bool backing_y_0_top,
                            uint32_t backing_width,
                            uint32_t backing_height,
                            uint32_t x, uint32_t y,
                            uint32_t w, uint32_t h,
                            void *d3d_tex2d);
void gd_egl_scanout_dmabuf(DisplayChangeListener *dcl,
                           QemuDmaBuf *dmabuf);
void gd_egl_cursor_dmabuf(DisplayChangeListener *dcl,
                          QemuDmaBuf *dmabuf, bool have_hot,
                          uint32_t hot_x, uint32_t hot_y);
void gd_egl_cursor_position(DisplayChangeListener *dcl,
                            uint32_t pos_x, uint32_t pos_y);
void gd_egl_flush(DisplayChangeListener *dcl,
                  uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void gd_egl_scanout_flush(DisplayChangeListener *dcl,
                          uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void gtk_egl_init(DisplayGLMode mode);
int gd_egl_make_current(DisplayGLCtx *dgc,
                        QEMUGLContext ctx);

/* ui/gtk-gl-area.c */
void gd_gl_area_init(VirtualConsole *vc);
void gd_gl_area_draw(VirtualConsole *vc);
void gd_gl_area_update(DisplayChangeListener *dcl,
                       int x, int y, int w, int h);
void gd_gl_area_refresh(DisplayChangeListener *dcl);
void gd_gl_area_switch(DisplayChangeListener *dcl,
                       DisplaySurface *surface);
QEMUGLContext gd_gl_area_create_context(DisplayGLCtx *dgc,
                                        QEMUGLParams *params);
void gd_gl_area_destroy_context(DisplayGLCtx *dgc,
                                QEMUGLContext ctx);
void gd_gl_area_scanout_dmabuf(DisplayChangeListener *dcl,
                               QemuDmaBuf *dmabuf);
void gd_gl_area_scanout_texture(DisplayChangeListener *dcl,
                                uint32_t backing_id,
                                bool backing_y_0_top,
                                uint32_t backing_width,
                                uint32_t backing_height,
                                uint32_t x, uint32_t y,
                                uint32_t w, uint32_t h,
                                void *d3d_tex2d);
void gd_gl_area_scanout_disable(DisplayChangeListener *dcl);
void gd_gl_area_scanout_flush(DisplayChangeListener *dcl,
                              uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void gtk_gl_area_init(void);
int gd_gl_area_make_current(DisplayGLCtx *dgc,
                            QEMUGLContext ctx);

/* gtk-clipboard.c */
void gd_clipboard_init(GtkDisplayState *gd);

#endif /* UI_GTK_H */
