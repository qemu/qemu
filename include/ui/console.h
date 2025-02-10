#ifndef CONSOLE_H
#define CONSOLE_H

#include "ui/qemu-pixman.h"
#include "qom/object.h"
#include "qemu/notify.h"
#include "qapi/qapi-types-ui.h"
#include "ui/input.h"
#include "ui/surface.h"
#include "ui/dmabuf.h"

#define TYPE_QEMU_CONSOLE "qemu-console"
OBJECT_DECLARE_TYPE(QemuConsole, QemuConsoleClass, QEMU_CONSOLE)

#define TYPE_QEMU_GRAPHIC_CONSOLE "qemu-graphic-console"
OBJECT_DECLARE_SIMPLE_TYPE(QemuGraphicConsole, QEMU_GRAPHIC_CONSOLE)

#define TYPE_QEMU_TEXT_CONSOLE "qemu-text-console"
OBJECT_DECLARE_SIMPLE_TYPE(QemuTextConsole, QEMU_TEXT_CONSOLE)

#define TYPE_QEMU_FIXED_TEXT_CONSOLE "qemu-fixed-text-console"
OBJECT_DECLARE_SIMPLE_TYPE(QemuFixedTextConsole, QEMU_FIXED_TEXT_CONSOLE)

#define QEMU_IS_GRAPHIC_CONSOLE(c) \
    object_dynamic_cast(OBJECT(c), TYPE_QEMU_GRAPHIC_CONSOLE)

#define QEMU_IS_TEXT_CONSOLE(c) \
    object_dynamic_cast(OBJECT(c), TYPE_QEMU_TEXT_CONSOLE)

#define QEMU_IS_FIXED_TEXT_CONSOLE(c) \
    object_dynamic_cast(OBJECT(c), TYPE_QEMU_FIXED_TEXT_CONSOLE)

/* keyboard/mouse support */

#define MOUSE_EVENT_LBUTTON 0x01
#define MOUSE_EVENT_RBUTTON 0x02
#define MOUSE_EVENT_MBUTTON 0x04
#define MOUSE_EVENT_WHEELUP 0x08
#define MOUSE_EVENT_WHEELDN 0x10

/* identical to the ps/2 keyboard bits */
#define QEMU_SCROLL_LOCK_LED (1 << 0)
#define QEMU_NUM_LOCK_LED    (1 << 1)
#define QEMU_CAPS_LOCK_LED   (1 << 2)

/* in ms */
#define GUI_REFRESH_INTERVAL_DEFAULT    30
#define GUI_REFRESH_INTERVAL_IDLE     3000

/* Color number is match to standard vga palette */
enum qemu_color_names {
    QEMU_COLOR_BLACK   = 0,
    QEMU_COLOR_BLUE    = 1,
    QEMU_COLOR_GREEN   = 2,
    QEMU_COLOR_CYAN    = 3,
    QEMU_COLOR_RED     = 4,
    QEMU_COLOR_MAGENTA = 5,
    QEMU_COLOR_YELLOW  = 6,
    QEMU_COLOR_WHITE   = 7
};
/* Convert to curses char attributes */
#define ATTR2CHTYPE(c, fg, bg, bold) \
    ((bold) << 21 | (bg) << 11 | (fg) << 8 | (c))

typedef void QEMUPutKBDEvent(void *opaque, int keycode);
typedef void QEMUPutLEDEvent(void *opaque, int ledstate);
typedef void QEMUPutMouseEvent(void *opaque, int dx, int dy, int dz, int buttons_state);

typedef struct QEMUPutMouseEntry QEMUPutMouseEntry;
typedef struct QEMUPutKbdEntry QEMUPutKbdEntry;
typedef struct QEMUPutLEDEntry QEMUPutLEDEntry;

QEMUPutMouseEntry *qemu_add_mouse_event_handler(QEMUPutMouseEvent *func,
                                                void *opaque, int absolute,
                                                const char *name);
void qemu_remove_mouse_event_handler(QEMUPutMouseEntry *entry);
void qemu_activate_mouse_event_handler(QEMUPutMouseEntry *entry);

QEMUPutLEDEntry *qemu_add_led_event_handler(QEMUPutLEDEvent *func, void *opaque);
void qemu_remove_led_event_handler(QEMUPutLEDEntry *entry);

void kbd_put_ledstate(int ledstate);

bool qemu_mouse_set(int index, Error **errp);

/* keysym is a unicode code except for special keys (see QEMU_KEY_xxx
   constants) */
#define QEMU_KEY_ESC1(c) ((c) | 0xe100)
#define QEMU_KEY_TAB        0x0009
#define QEMU_KEY_BACKSPACE  0x007f
#define QEMU_KEY_UP         QEMU_KEY_ESC1('A')
#define QEMU_KEY_DOWN       QEMU_KEY_ESC1('B')
#define QEMU_KEY_RIGHT      QEMU_KEY_ESC1('C')
#define QEMU_KEY_LEFT       QEMU_KEY_ESC1('D')
#define QEMU_KEY_HOME       QEMU_KEY_ESC1(1)
#define QEMU_KEY_END        QEMU_KEY_ESC1(4)
#define QEMU_KEY_PAGEUP     QEMU_KEY_ESC1(5)
#define QEMU_KEY_PAGEDOWN   QEMU_KEY_ESC1(6)
#define QEMU_KEY_DELETE     QEMU_KEY_ESC1(3)

#define QEMU_KEY_CTRL_UP         0xe400
#define QEMU_KEY_CTRL_DOWN       0xe401
#define QEMU_KEY_CTRL_LEFT       0xe402
#define QEMU_KEY_CTRL_RIGHT      0xe403
#define QEMU_KEY_CTRL_HOME       0xe404
#define QEMU_KEY_CTRL_END        0xe405
#define QEMU_KEY_CTRL_PAGEUP     0xe406
#define QEMU_KEY_CTRL_PAGEDOWN   0xe407

void qemu_text_console_put_keysym(QemuTextConsole *s, int keysym);
bool qemu_text_console_put_qcode(QemuTextConsole *s, int qcode, bool ctrl);
void qemu_text_console_put_string(QemuTextConsole *s, const char *str, int len);

/* Touch devices */
typedef struct touch_slot {
    int x;
    int y;
    int tracking_id;
} touch_slot;

void console_handle_touch_event(QemuConsole *con,
                                struct touch_slot touch_slots[INPUT_EVENT_SLOTS_MAX],
                                uint64_t num_slot,
                                int width, int height,
                                double x, double y,
                                InputMultiTouchType type,
                                Error **errp);
/* consoles */

struct QemuConsoleClass {
    ObjectClass parent_class;
};

typedef struct ScanoutTexture {
    uint32_t backing_id;
    bool backing_y_0_top;
    uint32_t backing_width;
    uint32_t backing_height;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    void *d3d_tex2d;
} ScanoutTexture;

typedef struct QemuUIInfo {
    /* physical dimension */
    uint16_t width_mm;
    uint16_t height_mm;
    /* geometry */
    int       xoff;
    int       yoff;
    uint32_t  width;
    uint32_t  height;
    uint32_t  refresh_rate;
} QemuUIInfo;

/* cursor data format is 32bit RGBA */
typedef struct QEMUCursor {
    uint16_t            width, height;
    int                 hot_x, hot_y;
    int                 refcount;
    uint32_t            data[];
} QEMUCursor;

QEMUCursor *cursor_alloc(uint16_t width, uint16_t height);
QEMUCursor *cursor_ref(QEMUCursor *c);
void cursor_unref(QEMUCursor *c);
QEMUCursor *cursor_builtin_hidden(void);
QEMUCursor *cursor_builtin_left_ptr(void);
void cursor_print_ascii_art(QEMUCursor *c, const char *prefix);
int cursor_get_mono_bpl(QEMUCursor *c);
void cursor_set_mono(QEMUCursor *c,
                     uint32_t foreground, uint32_t background, uint8_t *image,
                     int transparent, uint8_t *mask);
void cursor_get_mono_mask(QEMUCursor *c, int transparent, uint8_t *mask);

typedef void *QEMUGLContext;
typedef struct QEMUGLParams QEMUGLParams;

struct QEMUGLParams {
    int major_ver;
    int minor_ver;
};

enum display_scanout {
    SCANOUT_NONE,
    SCANOUT_SURFACE,
    SCANOUT_TEXTURE,
    SCANOUT_DMABUF,
};

typedef struct DisplayScanout {
    enum display_scanout kind;
    union {
        /* DisplaySurface *surface; is kept in QemuConsole */
        ScanoutTexture texture;
        QemuDmaBuf *dmabuf;
    };
} DisplayScanout;

typedef struct DisplayState DisplayState;
typedef struct DisplayGLCtx DisplayGLCtx;

typedef struct DisplayChangeListenerOps {
    const char *dpy_name;

    /* optional */
    void (*dpy_refresh)(DisplayChangeListener *dcl);

    /* optional */
    void (*dpy_gfx_update)(DisplayChangeListener *dcl,
                           int x, int y, int w, int h);
    /* optional */
    void (*dpy_gfx_switch)(DisplayChangeListener *dcl,
                           struct DisplaySurface *new_surface);
    /* optional */
    bool (*dpy_gfx_check_format)(DisplayChangeListener *dcl,
                                 pixman_format_code_t format);

    /* optional */
    void (*dpy_text_cursor)(DisplayChangeListener *dcl,
                            int x, int y);
    /* optional */
    void (*dpy_text_resize)(DisplayChangeListener *dcl,
                            int w, int h);
    /* optional */
    void (*dpy_text_update)(DisplayChangeListener *dcl,
                            int x, int y, int w, int h);

    /* optional */
    void (*dpy_mouse_set)(DisplayChangeListener *dcl,
                          int x, int y, bool on);
    /* optional */
    void (*dpy_cursor_define)(DisplayChangeListener *dcl,
                              QEMUCursor *cursor);

    /* required if GL */
    void (*dpy_gl_scanout_disable)(DisplayChangeListener *dcl);
    /* required if GL */
    void (*dpy_gl_scanout_texture)(DisplayChangeListener *dcl,
                                   uint32_t backing_id,
                                   bool backing_y_0_top,
                                   uint32_t backing_width,
                                   uint32_t backing_height,
                                   uint32_t x, uint32_t y,
                                   uint32_t w, uint32_t h,
                                   void *d3d_tex2d);
    /* optional (default to true if has dpy_gl_scanout_dmabuf) */
    bool (*dpy_has_dmabuf)(DisplayChangeListener *dcl);
    /* optional */
    void (*dpy_gl_scanout_dmabuf)(DisplayChangeListener *dcl,
                                  QemuDmaBuf *dmabuf);
    /* optional */
    void (*dpy_gl_cursor_dmabuf)(DisplayChangeListener *dcl,
                                 QemuDmaBuf *dmabuf, bool have_hot,
                                 uint32_t hot_x, uint32_t hot_y);
    /* optional */
    void (*dpy_gl_cursor_position)(DisplayChangeListener *dcl,
                                   uint32_t pos_x, uint32_t pos_y);
    /* optional */
    void (*dpy_gl_release_dmabuf)(DisplayChangeListener *dcl,
                                  QemuDmaBuf *dmabuf);
    /* required if GL */
    void (*dpy_gl_update)(DisplayChangeListener *dcl,
                          uint32_t x, uint32_t y, uint32_t w, uint32_t h);

} DisplayChangeListenerOps;

struct DisplayChangeListener {
    uint64_t update_interval;
    const DisplayChangeListenerOps *ops;
    DisplayState *ds;
    QemuConsole *con;

    QLIST_ENTRY(DisplayChangeListener) next;
};

typedef struct DisplayGLCtxOps {
    bool (*dpy_gl_ctx_is_compatible_dcl)(DisplayGLCtx *dgc,
                                         DisplayChangeListener *dcl);
    QEMUGLContext (*dpy_gl_ctx_create)(DisplayGLCtx *dgc,
                                       QEMUGLParams *params);
    void (*dpy_gl_ctx_destroy)(DisplayGLCtx *dgc,
                               QEMUGLContext ctx);
    int (*dpy_gl_ctx_make_current)(DisplayGLCtx *dgc,
                                   QEMUGLContext ctx);
    void (*dpy_gl_ctx_create_texture)(DisplayGLCtx *dgc,
                                      DisplaySurface *surface);
    void (*dpy_gl_ctx_destroy_texture)(DisplayGLCtx *dgc,
                                      DisplaySurface *surface);
    void (*dpy_gl_ctx_update_texture)(DisplayGLCtx *dgc,
                                      DisplaySurface *surface,
                                      int x, int y, int w, int h);
} DisplayGLCtxOps;

struct DisplayGLCtx {
    const DisplayGLCtxOps *ops;
#ifdef CONFIG_OPENGL
    QemuGLShader *gls; /* optional shared shader */
#endif
};

DisplayState *init_displaystate(void);

void register_displaychangelistener(DisplayChangeListener *dcl);
void update_displaychangelistener(DisplayChangeListener *dcl,
                                  uint64_t interval);
void unregister_displaychangelistener(DisplayChangeListener *dcl);

bool dpy_ui_info_supported(const QemuConsole *con);
const QemuUIInfo *dpy_get_ui_info(const QemuConsole *con);
int dpy_set_ui_info(QemuConsole *con, QemuUIInfo *info, bool delay);

void dpy_gfx_update(QemuConsole *con, int x, int y, int w, int h);
void dpy_gfx_update_full(QemuConsole *con);
void dpy_gfx_replace_surface(QemuConsole *con,
                             DisplaySurface *surface);
void dpy_text_cursor(QemuConsole *con, int x, int y);
void dpy_text_update(QemuConsole *con, int x, int y, int w, int h);
void dpy_text_resize(QemuConsole *con, int w, int h);
void dpy_mouse_set(QemuConsole *con, int x, int y, bool on);
void dpy_cursor_define(QemuConsole *con, QEMUCursor *cursor);
bool dpy_gfx_check_format(QemuConsole *con,
                          pixman_format_code_t format);

void dpy_gl_scanout_disable(QemuConsole *con);
void dpy_gl_scanout_texture(QemuConsole *con,
                            uint32_t backing_id, bool backing_y_0_top,
                            uint32_t backing_width, uint32_t backing_height,
                            uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                            void *d3d_tex2d);
void dpy_gl_scanout_dmabuf(QemuConsole *con,
                           QemuDmaBuf *dmabuf);
void dpy_gl_cursor_dmabuf(QemuConsole *con, QemuDmaBuf *dmabuf,
                          bool have_hot, uint32_t hot_x, uint32_t hot_y);
void dpy_gl_cursor_position(QemuConsole *con,
                            uint32_t pos_x, uint32_t pos_y);
void dpy_gl_release_dmabuf(QemuConsole *con,
                           QemuDmaBuf *dmabuf);
void dpy_gl_update(QemuConsole *con,
                   uint32_t x, uint32_t y, uint32_t w, uint32_t h);

QEMUGLContext dpy_gl_ctx_create(QemuConsole *con,
                                QEMUGLParams *params);
void dpy_gl_ctx_destroy(QemuConsole *con, QEMUGLContext ctx);
int dpy_gl_ctx_make_current(QemuConsole *con, QEMUGLContext ctx);

bool console_has_gl(QemuConsole *con);

typedef uint32_t console_ch_t;

static inline void console_write_ch(console_ch_t *dest, uint32_t ch)
{
    *dest = ch;
}

enum {
    GRAPHIC_FLAGS_NONE     = 0,
    /* require a console/display with GL callbacks */
    GRAPHIC_FLAGS_GL       = 1 << 0,
    /* require a console/display with DMABUF import */
    GRAPHIC_FLAGS_DMABUF   = 1 << 1,
};

typedef struct GraphicHwOps {
    int (*get_flags)(void *opaque); /* optional, default 0 */
    void (*invalidate)(void *opaque);
    void (*gfx_update)(void *opaque);
    bool gfx_update_async; /* if true, calls graphic_hw_update_done() */
    void (*text_update)(void *opaque, console_ch_t *text);
    void (*ui_info)(void *opaque, uint32_t head, QemuUIInfo *info);
    void (*gl_block)(void *opaque, bool block);
} GraphicHwOps;

QemuConsole *graphic_console_init(DeviceState *dev, uint32_t head,
                                  const GraphicHwOps *ops,
                                  void *opaque);
void graphic_console_set_hwops(QemuConsole *con,
                               const GraphicHwOps *hw_ops,
                               void *opaque);
void graphic_console_close(QemuConsole *con);

void graphic_hw_update(QemuConsole *con);
void graphic_hw_update_done(QemuConsole *con);
void graphic_hw_invalidate(QemuConsole *con);
void graphic_hw_text_update(QemuConsole *con, console_ch_t *chardata);
void graphic_hw_gl_block(QemuConsole *con, bool block);

void qemu_console_early_init(void);

void qemu_console_set_display_gl_ctx(QemuConsole *con, DisplayGLCtx *ctx);

QemuConsole *qemu_console_lookup_default(void);
QemuConsole *qemu_console_lookup_by_index(unsigned int index);
QemuConsole *qemu_console_lookup_by_device(DeviceState *dev, uint32_t head);
QemuConsole *qemu_console_lookup_by_device_name(const char *device_id,
                                                uint32_t head, Error **errp);
QEMUCursor *qemu_console_get_cursor(QemuConsole *con);
bool qemu_console_is_visible(QemuConsole *con);
bool qemu_console_is_graphic(QemuConsole *con);
bool qemu_console_is_fixedsize(QemuConsole *con);
bool qemu_console_is_gl_blocked(QemuConsole *con);
char *qemu_console_get_label(QemuConsole *con);
int qemu_console_get_index(QemuConsole *con);
uint32_t qemu_console_get_head(QemuConsole *con);
int qemu_console_get_width(QemuConsole *con, int fallback);
int qemu_console_get_height(QemuConsole *con, int fallback);
/* Return the low-level window id for the console */
int qemu_console_get_window_id(QemuConsole *con);
/* Set the low-level window id for the console */
void qemu_console_set_window_id(QemuConsole *con, int window_id);

void qemu_console_resize(QemuConsole *con, int width, int height);
DisplaySurface *qemu_console_surface(QemuConsole *con);
void coroutine_fn qemu_console_co_wait_update(QemuConsole *con);
int qemu_invalidate_text_consoles(void);

/* console-gl.c */
#ifdef CONFIG_OPENGL
bool console_gl_check_format(DisplayChangeListener *dcl,
                             pixman_format_code_t format);
void surface_gl_create_texture(QemuGLShader *gls,
                               DisplaySurface *surface);
void surface_gl_update_texture(QemuGLShader *gls,
                               DisplaySurface *surface,
                               int x, int y, int w, int h);
void surface_gl_render_texture(QemuGLShader *gls,
                               DisplaySurface *surface);
void surface_gl_destroy_texture(QemuGLShader *gls,
                               DisplaySurface *surface);
void surface_gl_setup_viewport(QemuGLShader *gls,
                               DisplaySurface *surface,
                               int ww, int wh);
#endif

typedef struct QemuDisplay QemuDisplay;

struct QemuDisplay {
    DisplayType type;
    void (*early_init)(DisplayOptions *opts);
    void (*init)(DisplayState *ds, DisplayOptions *opts);
    const char *vc;
};

void qemu_display_register(QemuDisplay *ui);
bool qemu_display_find_default(DisplayOptions *opts);
void qemu_display_early_init(DisplayOptions *opts);
void qemu_display_init(DisplayState *ds, DisplayOptions *opts);
const char *qemu_display_get_vc(DisplayOptions *opts);
void qemu_display_help(void);

/* vnc.c */
void vnc_display_init(const char *id, Error **errp);
void vnc_display_open(const char *id, Error **errp);
void vnc_display_add_client(const char *id, int csock, bool skipauth);
int vnc_display_password(const char *id, const char *password);
int vnc_display_pw_expire(const char *id, time_t expires);
void vnc_parse(const char *str);
int vnc_init_func(void *opaque, QemuOpts *opts, Error **errp);
bool vnc_display_reload_certs(const char *id,  Error **errp);
bool vnc_display_update(DisplayUpdateOptionsVNC *arg, Error **errp);

/* input.c */
int index_from_key(const char *key, size_t key_length);

#ifdef CONFIG_LINUX
/* udmabuf.c */
int udmabuf_fd(void);
#endif

/* util.c */
bool qemu_console_fill_device_address(QemuConsole *con,
                                      char *device_address,
                                      size_t size,
                                      Error **errp);

#endif
