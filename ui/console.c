/*
 * QEMU graphical console
 *
 * Copyright (c) 2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "ui/console.h"
#include "hw/qdev-core.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-ui.h"
#include "qemu/coroutine.h"
#include "qemu/error-report.h"
#include "qemu/fifo8.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/timer.h"
#include "chardev/char.h"
#include "trace.h"
#include "exec/memory.h"
#include "io/channel-file.h"
#include "qom/object.h"
#ifdef CONFIG_PNG
#include <png.h>
#endif

#define DEFAULT_BACKSCROLL 512
#define CONSOLE_CURSOR_PERIOD 500

typedef struct TextAttributes {
    uint8_t fgcol:4;
    uint8_t bgcol:4;
    uint8_t bold:1;
    uint8_t uline:1;
    uint8_t blink:1;
    uint8_t invers:1;
    uint8_t unvisible:1;
} TextAttributes;

typedef struct TextCell {
    uint8_t ch;
    TextAttributes t_attrib;
} TextCell;

#define MAX_ESC_PARAMS 3

enum TTYState {
    TTY_STATE_NORM,
    TTY_STATE_ESC,
    TTY_STATE_CSI,
};

typedef enum {
    GRAPHIC_CONSOLE,
    TEXT_CONSOLE,
    TEXT_CONSOLE_FIXED_SIZE
} console_type_t;

struct QemuConsole {
    Object parent;

    int index;
    console_type_t console_type;
    DisplayState *ds;
    DisplaySurface *surface;
    DisplayScanout scanout;
    int dcls;
    DisplayGLCtx *gl;
    int gl_block;
    QEMUTimer *gl_unblock_timer;
    int window_id;

    /* Graphic console state.  */
    Object *device;
    uint32_t head;
    QemuUIInfo ui_info;
    QEMUTimer *ui_timer;
    QEMUCursor *cursor;
    int cursor_x, cursor_y, cursor_on;
    const GraphicHwOps *hw_ops;
    void *hw;

    /* Text console state */
    int width;
    int height;
    int total_height;
    int backscroll_height;
    int x, y;
    int x_saved, y_saved;
    int y_displayed;
    int y_base;
    TextAttributes t_attrib_default; /* default text attributes */
    TextAttributes t_attrib; /* currently active text attributes */
    TextCell *cells;
    int text_x[2], text_y[2], cursor_invalidate;
    int echo;

    int update_x0;
    int update_y0;
    int update_x1;
    int update_y1;

    enum TTYState state;
    int esc_params[MAX_ESC_PARAMS];
    int nb_esc_params;

    Chardev *chr;
    /* fifo for key pressed */
    Fifo8 out_fifo;
    CoQueue dump_queue;

    QTAILQ_ENTRY(QemuConsole) next;
};

struct DisplayState {
    QEMUTimer *gui_timer;
    uint64_t last_update;
    uint64_t update_interval;
    bool refreshing;
    bool have_gfx;
    bool have_text;

    QLIST_HEAD(, DisplayChangeListener) listeners;
};

static DisplayState *display_state;
static QemuConsole *active_console;
static QTAILQ_HEAD(, QemuConsole) consoles =
    QTAILQ_HEAD_INITIALIZER(consoles);
static bool cursor_visible_phase;
static QEMUTimer *cursor_timer;

static void text_console_do_init(Chardev *chr, DisplayState *ds);
static void dpy_refresh(DisplayState *s);
static DisplayState *get_alloc_displaystate(void);
static void text_console_update_cursor_timer(void);
static void text_console_update_cursor(void *opaque);
static bool displaychangelistener_has_dmabuf(DisplayChangeListener *dcl);
static bool console_compatible_with(QemuConsole *con,
                                    DisplayChangeListener *dcl, Error **errp);

static void gui_update(void *opaque)
{
    uint64_t interval = GUI_REFRESH_INTERVAL_IDLE;
    uint64_t dcl_interval;
    DisplayState *ds = opaque;
    DisplayChangeListener *dcl;

    ds->refreshing = true;
    dpy_refresh(ds);
    ds->refreshing = false;

    QLIST_FOREACH(dcl, &ds->listeners, next) {
        dcl_interval = dcl->update_interval ?
            dcl->update_interval : GUI_REFRESH_INTERVAL_DEFAULT;
        if (interval > dcl_interval) {
            interval = dcl_interval;
        }
    }
    if (ds->update_interval != interval) {
        ds->update_interval = interval;
        trace_console_refresh(interval);
    }
    ds->last_update = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    timer_mod(ds->gui_timer, ds->last_update + interval);
}

static void gui_setup_refresh(DisplayState *ds)
{
    DisplayChangeListener *dcl;
    bool need_timer = false;
    bool have_gfx = false;
    bool have_text = false;

    QLIST_FOREACH(dcl, &ds->listeners, next) {
        if (dcl->ops->dpy_refresh != NULL) {
            need_timer = true;
        }
        if (dcl->ops->dpy_gfx_update != NULL) {
            have_gfx = true;
        }
        if (dcl->ops->dpy_text_update != NULL) {
            have_text = true;
        }
    }

    if (need_timer && ds->gui_timer == NULL) {
        ds->gui_timer = timer_new_ms(QEMU_CLOCK_REALTIME, gui_update, ds);
        timer_mod(ds->gui_timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME));
    }
    if (!need_timer && ds->gui_timer != NULL) {
        timer_free(ds->gui_timer);
        ds->gui_timer = NULL;
    }

    ds->have_gfx = have_gfx;
    ds->have_text = have_text;
}

void graphic_hw_update_done(QemuConsole *con)
{
    if (con) {
        qemu_co_enter_all(&con->dump_queue, NULL);
    }
}

void graphic_hw_update(QemuConsole *con)
{
    bool async = false;
    con = con ? con : active_console;
    if (!con) {
        return;
    }
    if (con->hw_ops->gfx_update) {
        con->hw_ops->gfx_update(con->hw);
        async = con->hw_ops->gfx_update_async;
    }
    if (!async) {
        graphic_hw_update_done(con);
    }
}

static void graphic_hw_gl_unblock_timer(void *opaque)
{
    warn_report("console: no gl-unblock within one second");
}

void graphic_hw_gl_block(QemuConsole *con, bool block)
{
    uint64_t timeout;
    assert(con != NULL);

    if (block) {
        con->gl_block++;
    } else {
        con->gl_block--;
    }
    assert(con->gl_block >= 0);
    if (!con->hw_ops->gl_block) {
        return;
    }
    if ((block && con->gl_block != 1) || (!block && con->gl_block != 0)) {
        return;
    }
    con->hw_ops->gl_block(con->hw, block);

    if (block) {
        timeout = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
        timeout += 1000; /* one sec */
        timer_mod(con->gl_unblock_timer, timeout);
    } else {
        timer_del(con->gl_unblock_timer);
    }
}

int qemu_console_get_window_id(QemuConsole *con)
{
    return con->window_id;
}

void qemu_console_set_window_id(QemuConsole *con, int window_id)
{
    con->window_id = window_id;
}

void graphic_hw_invalidate(QemuConsole *con)
{
    if (!con) {
        con = active_console;
    }
    if (con && con->hw_ops->invalidate) {
        con->hw_ops->invalidate(con->hw);
    }
}

#ifdef CONFIG_PNG
/**
 * png_save: Take a screenshot as PNG
 *
 * Saves screendump as a PNG file
 *
 * Returns true for success or false for error.
 *
 * @fd: File descriptor for PNG file.
 * @image: Image data in pixman format.
 * @errp: Pointer to an error.
 */
static bool png_save(int fd, pixman_image_t *image, Error **errp)
{
    int width = pixman_image_get_width(image);
    int height = pixman_image_get_height(image);
    png_struct *png_ptr;
    png_info *info_ptr;
    g_autoptr(pixman_image_t) linebuf =
        qemu_pixman_linebuf_create(PIXMAN_BE_r8g8b8, width);
    uint8_t *buf = (uint8_t *)pixman_image_get_data(linebuf);
    FILE *f = fdopen(fd, "wb");
    int y;
    if (!f) {
        error_setg_errno(errp, errno,
                         "Failed to create file from file descriptor");
        return false;
    }

    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL,
                                      NULL, NULL);
    if (!png_ptr) {
        error_setg(errp, "PNG creation failed. Unable to write struct");
        fclose(f);
        return false;
    }

    info_ptr = png_create_info_struct(png_ptr);

    if (!info_ptr) {
        error_setg(errp, "PNG creation failed. Unable to write info");
        fclose(f);
        png_destroy_write_struct(&png_ptr, &info_ptr);
        return false;
    }

    png_init_io(png_ptr, f);

    png_set_IHDR(png_ptr, info_ptr, width, height, 8,
                 PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

    png_write_info(png_ptr, info_ptr);

    for (y = 0; y < height; ++y) {
        qemu_pixman_linebuf_fill(linebuf, image, width, 0, y);
        png_write_row(png_ptr, buf);
    }

    png_write_end(png_ptr, NULL);

    png_destroy_write_struct(&png_ptr, &info_ptr);

    if (fclose(f) != 0) {
        error_setg_errno(errp, errno,
                         "PNG creation failed. Unable to close file");
        return false;
    }

    return true;
}

#else /* no png support */

static bool png_save(int fd, pixman_image_t *image, Error **errp)
{
    error_setg(errp, "Enable PNG support with libpng for screendump");
    return false;
}

#endif /* CONFIG_PNG */

static bool ppm_save(int fd, pixman_image_t *image, Error **errp)
{
    int width = pixman_image_get_width(image);
    int height = pixman_image_get_height(image);
    g_autoptr(Object) ioc = OBJECT(qio_channel_file_new_fd(fd));
    g_autofree char *header = NULL;
    g_autoptr(pixman_image_t) linebuf = NULL;
    int y;

    trace_ppm_save(fd, image);

    header = g_strdup_printf("P6\n%d %d\n%d\n", width, height, 255);
    if (qio_channel_write_all(QIO_CHANNEL(ioc),
                              header, strlen(header), errp) < 0) {
        return false;
    }

    linebuf = qemu_pixman_linebuf_create(PIXMAN_BE_r8g8b8, width);
    for (y = 0; y < height; y++) {
        qemu_pixman_linebuf_fill(linebuf, image, width, 0, y);
        if (qio_channel_write_all(QIO_CHANNEL(ioc),
                                  (char *)pixman_image_get_data(linebuf),
                                  pixman_image_get_stride(linebuf), errp) < 0) {
            return false;
        }
    }

    return true;
}

static void graphic_hw_update_bh(void *con)
{
    graphic_hw_update(con);
}

/* Safety: coroutine-only, concurrent-coroutine safe, main thread only */
void coroutine_fn
qmp_screendump(const char *filename, const char *device,
               bool has_head, int64_t head,
               bool has_format, ImageFormat format, Error **errp)
{
    g_autoptr(pixman_image_t) image = NULL;
    QemuConsole *con;
    DisplaySurface *surface;
    int fd;

    if (device) {
        con = qemu_console_lookup_by_device_name(device, has_head ? head : 0,
                                                 errp);
        if (!con) {
            return;
        }
    } else {
        if (has_head) {
            error_setg(errp, "'head' must be specified together with 'device'");
            return;
        }
        con = qemu_console_lookup_by_index(0);
        if (!con) {
            error_setg(errp, "There is no console to take a screendump from");
            return;
        }
    }

    if (qemu_co_queue_empty(&con->dump_queue)) {
        /* Defer the update, it will restart the pending coroutines */
        aio_bh_schedule_oneshot(qemu_get_aio_context(),
                                graphic_hw_update_bh, con);
    }
    qemu_co_queue_wait(&con->dump_queue, NULL);

    /*
     * All pending coroutines are woken up, while the BQL is held.  No
     * further graphic update are possible until it is released.  Take
     * an image ref before that.
     */
    surface = qemu_console_surface(con);
    if (!surface) {
        error_setg(errp, "no surface");
        return;
    }
    image = pixman_image_ref(surface->image);

    fd = qemu_open_old(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666);
    if (fd == -1) {
        error_setg(errp, "failed to open file '%s': %s", filename,
                   strerror(errno));
        return;
    }

    /*
     * The image content could potentially be updated as the coroutine
     * yields and releases the BQL. It could produce corrupted dump, but
     * it should be otherwise safe.
     */
    if (has_format && format == IMAGE_FORMAT_PNG) {
        /* PNG format specified for screendump */
        if (!png_save(fd, image, errp)) {
            qemu_unlink(filename);
        }
    } else {
        /* PPM format specified/default for screendump */
        if (!ppm_save(fd, image, errp)) {
            qemu_unlink(filename);
        }
    }
}

void graphic_hw_text_update(QemuConsole *con, console_ch_t *chardata)
{
    if (!con) {
        con = active_console;
    }
    if (con && con->hw_ops->text_update) {
        con->hw_ops->text_update(con->hw, chardata);
    }
}

static void vga_fill_rect(QemuConsole *con,
                          int posx, int posy, int width, int height,
                          pixman_color_t color)
{
    DisplaySurface *surface = qemu_console_surface(con);
    pixman_rectangle16_t rect = {
        .x = posx, .y = posy, .width = width, .height = height
    };

    pixman_image_fill_rectangles(PIXMAN_OP_SRC, surface->image,
                                 &color, 1, &rect);
}

/* copy from (xs, ys) to (xd, yd) a rectangle of size (w, h) */
static void vga_bitblt(QemuConsole *con,
                       int xs, int ys, int xd, int yd, int w, int h)
{
    DisplaySurface *surface = qemu_console_surface(con);

    pixman_image_composite(PIXMAN_OP_SRC,
                           surface->image, NULL, surface->image,
                           xs, ys, 0, 0, xd, yd, w, h);
}

/***********************************************************/
/* basic char display */

#define FONT_HEIGHT 16
#define FONT_WIDTH 8

#include "vgafont.h"

#define QEMU_RGB(r, g, b)                                               \
    { .red = r << 8, .green = g << 8, .blue = b << 8, .alpha = 0xffff }

static const pixman_color_t color_table_rgb[2][8] = {
    {   /* dark */
        [QEMU_COLOR_BLACK]   = QEMU_RGB(0x00, 0x00, 0x00),  /* black */
        [QEMU_COLOR_BLUE]    = QEMU_RGB(0x00, 0x00, 0xaa),  /* blue */
        [QEMU_COLOR_GREEN]   = QEMU_RGB(0x00, 0xaa, 0x00),  /* green */
        [QEMU_COLOR_CYAN]    = QEMU_RGB(0x00, 0xaa, 0xaa),  /* cyan */
        [QEMU_COLOR_RED]     = QEMU_RGB(0xaa, 0x00, 0x00),  /* red */
        [QEMU_COLOR_MAGENTA] = QEMU_RGB(0xaa, 0x00, 0xaa),  /* magenta */
        [QEMU_COLOR_YELLOW]  = QEMU_RGB(0xaa, 0xaa, 0x00),  /* yellow */
        [QEMU_COLOR_WHITE]   = QEMU_RGB(0xaa, 0xaa, 0xaa),  /* white */
    },
    {   /* bright */
        [QEMU_COLOR_BLACK]   = QEMU_RGB(0x00, 0x00, 0x00),  /* black */
        [QEMU_COLOR_BLUE]    = QEMU_RGB(0x00, 0x00, 0xff),  /* blue */
        [QEMU_COLOR_GREEN]   = QEMU_RGB(0x00, 0xff, 0x00),  /* green */
        [QEMU_COLOR_CYAN]    = QEMU_RGB(0x00, 0xff, 0xff),  /* cyan */
        [QEMU_COLOR_RED]     = QEMU_RGB(0xff, 0x00, 0x00),  /* red */
        [QEMU_COLOR_MAGENTA] = QEMU_RGB(0xff, 0x00, 0xff),  /* magenta */
        [QEMU_COLOR_YELLOW]  = QEMU_RGB(0xff, 0xff, 0x00),  /* yellow */
        [QEMU_COLOR_WHITE]   = QEMU_RGB(0xff, 0xff, 0xff),  /* white */
    }
};

static void vga_putcharxy(QemuConsole *s, int x, int y, int ch,
                          TextAttributes *t_attrib)
{
    static pixman_image_t *glyphs[256];
    DisplaySurface *surface = qemu_console_surface(s);
    pixman_color_t fgcol, bgcol;

    if (t_attrib->invers) {
        bgcol = color_table_rgb[t_attrib->bold][t_attrib->fgcol];
        fgcol = color_table_rgb[t_attrib->bold][t_attrib->bgcol];
    } else {
        fgcol = color_table_rgb[t_attrib->bold][t_attrib->fgcol];
        bgcol = color_table_rgb[t_attrib->bold][t_attrib->bgcol];
    }

    if (!glyphs[ch]) {
        glyphs[ch] = qemu_pixman_glyph_from_vgafont(FONT_HEIGHT, vgafont16, ch);
    }
    qemu_pixman_glyph_render(glyphs[ch], surface->image,
                             &fgcol, &bgcol, x, y, FONT_WIDTH, FONT_HEIGHT);
}

static void text_console_resize(QemuConsole *s)
{
    TextCell *cells, *c, *c1;
    int w1, x, y, last_width;

    assert(s->scanout.kind == SCANOUT_SURFACE);

    last_width = s->width;
    s->width = surface_width(s->surface) / FONT_WIDTH;
    s->height = surface_height(s->surface) / FONT_HEIGHT;

    w1 = last_width;
    if (s->width < w1)
        w1 = s->width;

    cells = g_new(TextCell, s->width * s->total_height + 1);
    for(y = 0; y < s->total_height; y++) {
        c = &cells[y * s->width];
        if (w1 > 0) {
            c1 = &s->cells[y * last_width];
            for(x = 0; x < w1; x++) {
                *c++ = *c1++;
            }
        }
        for(x = w1; x < s->width; x++) {
            c->ch = ' ';
            c->t_attrib = s->t_attrib_default;
            c++;
        }
    }
    g_free(s->cells);
    s->cells = cells;
}

static inline void text_update_xy(QemuConsole *s, int x, int y)
{
    s->text_x[0] = MIN(s->text_x[0], x);
    s->text_x[1] = MAX(s->text_x[1], x);
    s->text_y[0] = MIN(s->text_y[0], y);
    s->text_y[1] = MAX(s->text_y[1], y);
}

static void invalidate_xy(QemuConsole *s, int x, int y)
{
    if (!qemu_console_is_visible(s)) {
        return;
    }
    if (s->update_x0 > x * FONT_WIDTH)
        s->update_x0 = x * FONT_WIDTH;
    if (s->update_y0 > y * FONT_HEIGHT)
        s->update_y0 = y * FONT_HEIGHT;
    if (s->update_x1 < (x + 1) * FONT_WIDTH)
        s->update_x1 = (x + 1) * FONT_WIDTH;
    if (s->update_y1 < (y + 1) * FONT_HEIGHT)
        s->update_y1 = (y + 1) * FONT_HEIGHT;
}

static void update_xy(QemuConsole *s, int x, int y)
{
    TextCell *c;
    int y1, y2;

    if (s->ds->have_text) {
        text_update_xy(s, x, y);
    }

    y1 = (s->y_base + y) % s->total_height;
    y2 = y1 - s->y_displayed;
    if (y2 < 0) {
        y2 += s->total_height;
    }
    if (y2 < s->height) {
        if (x >= s->width) {
            x = s->width - 1;
        }
        c = &s->cells[y1 * s->width + x];
        vga_putcharxy(s, x, y2, c->ch,
                      &(c->t_attrib));
        invalidate_xy(s, x, y2);
    }
}

static void console_show_cursor(QemuConsole *s, int show)
{
    TextCell *c;
    int y, y1;
    int x = s->x;

    if (s->ds->have_text) {
        s->cursor_invalidate = 1;
    }

    if (x >= s->width) {
        x = s->width - 1;
    }
    y1 = (s->y_base + s->y) % s->total_height;
    y = y1 - s->y_displayed;
    if (y < 0) {
        y += s->total_height;
    }
    if (y < s->height) {
        c = &s->cells[y1 * s->width + x];
        if (show && cursor_visible_phase) {
            TextAttributes t_attrib = s->t_attrib_default;
            t_attrib.invers = !(t_attrib.invers); /* invert fg and bg */
            vga_putcharxy(s, x, y, c->ch, &t_attrib);
        } else {
            vga_putcharxy(s, x, y, c->ch, &(c->t_attrib));
        }
        invalidate_xy(s, x, y);
    }
}

static void console_refresh(QemuConsole *s)
{
    DisplaySurface *surface = qemu_console_surface(s);
    TextCell *c;
    int x, y, y1;

    if (s->ds->have_text) {
        s->text_x[0] = 0;
        s->text_y[0] = 0;
        s->text_x[1] = s->width - 1;
        s->text_y[1] = s->height - 1;
        s->cursor_invalidate = 1;
    }

    vga_fill_rect(s, 0, 0, surface_width(surface), surface_height(surface),
                  color_table_rgb[0][QEMU_COLOR_BLACK]);
    y1 = s->y_displayed;
    for (y = 0; y < s->height; y++) {
        c = s->cells + y1 * s->width;
        for (x = 0; x < s->width; x++) {
            vga_putcharxy(s, x, y, c->ch,
                          &(c->t_attrib));
            c++;
        }
        if (++y1 == s->total_height) {
            y1 = 0;
        }
    }
    console_show_cursor(s, 1);
    dpy_gfx_update(s, 0, 0,
                   surface_width(surface), surface_height(surface));
}

static void console_scroll(QemuConsole *s, int ydelta)
{
    int i, y1;

    if (ydelta > 0) {
        for(i = 0; i < ydelta; i++) {
            if (s->y_displayed == s->y_base)
                break;
            if (++s->y_displayed == s->total_height)
                s->y_displayed = 0;
        }
    } else {
        ydelta = -ydelta;
        i = s->backscroll_height;
        if (i > s->total_height - s->height)
            i = s->total_height - s->height;
        y1 = s->y_base - i;
        if (y1 < 0)
            y1 += s->total_height;
        for(i = 0; i < ydelta; i++) {
            if (s->y_displayed == y1)
                break;
            if (--s->y_displayed < 0)
                s->y_displayed = s->total_height - 1;
        }
    }
    console_refresh(s);
}

static void console_put_lf(QemuConsole *s)
{
    TextCell *c;
    int x, y1;

    s->y++;
    if (s->y >= s->height) {
        s->y = s->height - 1;

        if (s->y_displayed == s->y_base) {
            if (++s->y_displayed == s->total_height)
                s->y_displayed = 0;
        }
        if (++s->y_base == s->total_height)
            s->y_base = 0;
        if (s->backscroll_height < s->total_height)
            s->backscroll_height++;
        y1 = (s->y_base + s->height - 1) % s->total_height;
        c = &s->cells[y1 * s->width];
        for(x = 0; x < s->width; x++) {
            c->ch = ' ';
            c->t_attrib = s->t_attrib_default;
            c++;
        }
        if (s->y_displayed == s->y_base) {
            if (s->ds->have_text) {
                s->text_x[0] = 0;
                s->text_y[0] = 0;
                s->text_x[1] = s->width - 1;
                s->text_y[1] = s->height - 1;
            }

            vga_bitblt(s, 0, FONT_HEIGHT, 0, 0,
                       s->width * FONT_WIDTH,
                       (s->height - 1) * FONT_HEIGHT);
            vga_fill_rect(s, 0, (s->height - 1) * FONT_HEIGHT,
                          s->width * FONT_WIDTH, FONT_HEIGHT,
                          color_table_rgb[0][s->t_attrib_default.bgcol]);
            s->update_x0 = 0;
            s->update_y0 = 0;
            s->update_x1 = s->width * FONT_WIDTH;
            s->update_y1 = s->height * FONT_HEIGHT;
        }
    }
}

/* Set console attributes depending on the current escape codes.
 * NOTE: I know this code is not very efficient (checking every color for it
 * self) but it is more readable and better maintainable.
 */
static void console_handle_escape(QemuConsole *s)
{
    int i;

    for (i=0; i<s->nb_esc_params; i++) {
        switch (s->esc_params[i]) {
            case 0: /* reset all console attributes to default */
                s->t_attrib = s->t_attrib_default;
                break;
            case 1:
                s->t_attrib.bold = 1;
                break;
            case 4:
                s->t_attrib.uline = 1;
                break;
            case 5:
                s->t_attrib.blink = 1;
                break;
            case 7:
                s->t_attrib.invers = 1;
                break;
            case 8:
                s->t_attrib.unvisible = 1;
                break;
            case 22:
                s->t_attrib.bold = 0;
                break;
            case 24:
                s->t_attrib.uline = 0;
                break;
            case 25:
                s->t_attrib.blink = 0;
                break;
            case 27:
                s->t_attrib.invers = 0;
                break;
            case 28:
                s->t_attrib.unvisible = 0;
                break;
            /* set foreground color */
            case 30:
                s->t_attrib.fgcol = QEMU_COLOR_BLACK;
                break;
            case 31:
                s->t_attrib.fgcol = QEMU_COLOR_RED;
                break;
            case 32:
                s->t_attrib.fgcol = QEMU_COLOR_GREEN;
                break;
            case 33:
                s->t_attrib.fgcol = QEMU_COLOR_YELLOW;
                break;
            case 34:
                s->t_attrib.fgcol = QEMU_COLOR_BLUE;
                break;
            case 35:
                s->t_attrib.fgcol = QEMU_COLOR_MAGENTA;
                break;
            case 36:
                s->t_attrib.fgcol = QEMU_COLOR_CYAN;
                break;
            case 37:
                s->t_attrib.fgcol = QEMU_COLOR_WHITE;
                break;
            /* set background color */
            case 40:
                s->t_attrib.bgcol = QEMU_COLOR_BLACK;
                break;
            case 41:
                s->t_attrib.bgcol = QEMU_COLOR_RED;
                break;
            case 42:
                s->t_attrib.bgcol = QEMU_COLOR_GREEN;
                break;
            case 43:
                s->t_attrib.bgcol = QEMU_COLOR_YELLOW;
                break;
            case 44:
                s->t_attrib.bgcol = QEMU_COLOR_BLUE;
                break;
            case 45:
                s->t_attrib.bgcol = QEMU_COLOR_MAGENTA;
                break;
            case 46:
                s->t_attrib.bgcol = QEMU_COLOR_CYAN;
                break;
            case 47:
                s->t_attrib.bgcol = QEMU_COLOR_WHITE;
                break;
        }
    }
}

static void console_clear_xy(QemuConsole *s, int x, int y)
{
    int y1 = (s->y_base + y) % s->total_height;
    if (x >= s->width) {
        x = s->width - 1;
    }
    TextCell *c = &s->cells[y1 * s->width + x];
    c->ch = ' ';
    c->t_attrib = s->t_attrib_default;
    update_xy(s, x, y);
}

static void console_put_one(QemuConsole *s, int ch)
{
    TextCell *c;
    int y1;
    if (s->x >= s->width) {
        /* line wrap */
        s->x = 0;
        console_put_lf(s);
    }
    y1 = (s->y_base + s->y) % s->total_height;
    c = &s->cells[y1 * s->width + s->x];
    c->ch = ch;
    c->t_attrib = s->t_attrib;
    update_xy(s, s->x, s->y);
    s->x++;
}

static void console_respond_str(QemuConsole *s, const char *buf)
{
    while (*buf) {
        console_put_one(s, *buf);
        buf++;
    }
}

/* set cursor, checking bounds */
static void set_cursor(QemuConsole *s, int x, int y)
{
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (y >= s->height) {
        y = s->height - 1;
    }
    if (x >= s->width) {
        x = s->width - 1;
    }

    s->x = x;
    s->y = y;
}

static void console_putchar(QemuConsole *s, int ch)
{
    int i;
    int x, y;
    char response[40];

    switch(s->state) {
    case TTY_STATE_NORM:
        switch(ch) {
        case '\r':  /* carriage return */
            s->x = 0;
            break;
        case '\n':  /* newline */
            console_put_lf(s);
            break;
        case '\b':  /* backspace */
            if (s->x > 0)
                s->x--;
            break;
        case '\t':  /* tabspace */
            if (s->x + (8 - (s->x % 8)) > s->width) {
                s->x = 0;
                console_put_lf(s);
            } else {
                s->x = s->x + (8 - (s->x % 8));
            }
            break;
        case '\a':  /* alert aka. bell */
            /* TODO: has to be implemented */
            break;
        case 14:
            /* SI (shift in), character set 0 (ignored) */
            break;
        case 15:
            /* SO (shift out), character set 1 (ignored) */
            break;
        case 27:    /* esc (introducing an escape sequence) */
            s->state = TTY_STATE_ESC;
            break;
        default:
            console_put_one(s, ch);
            break;
        }
        break;
    case TTY_STATE_ESC: /* check if it is a terminal escape sequence */
        if (ch == '[') {
            for(i=0;i<MAX_ESC_PARAMS;i++)
                s->esc_params[i] = 0;
            s->nb_esc_params = 0;
            s->state = TTY_STATE_CSI;
        } else {
            s->state = TTY_STATE_NORM;
        }
        break;
    case TTY_STATE_CSI: /* handle escape sequence parameters */
        if (ch >= '0' && ch <= '9') {
            if (s->nb_esc_params < MAX_ESC_PARAMS) {
                int *param = &s->esc_params[s->nb_esc_params];
                int digit = (ch - '0');

                *param = (*param <= (INT_MAX - digit) / 10) ?
                         *param * 10 + digit : INT_MAX;
            }
        } else {
            if (s->nb_esc_params < MAX_ESC_PARAMS)
                s->nb_esc_params++;
            if (ch == ';' || ch == '?') {
                break;
            }
            trace_console_putchar_csi(s->esc_params[0], s->esc_params[1],
                                      ch, s->nb_esc_params);
            s->state = TTY_STATE_NORM;
            switch(ch) {
            case 'A':
                /* move cursor up */
                if (s->esc_params[0] == 0) {
                    s->esc_params[0] = 1;
                }
                set_cursor(s, s->x, s->y - s->esc_params[0]);
                break;
            case 'B':
                /* move cursor down */
                if (s->esc_params[0] == 0) {
                    s->esc_params[0] = 1;
                }
                set_cursor(s, s->x, s->y + s->esc_params[0]);
                break;
            case 'C':
                /* move cursor right */
                if (s->esc_params[0] == 0) {
                    s->esc_params[0] = 1;
                }
                set_cursor(s, s->x + s->esc_params[0], s->y);
                break;
            case 'D':
                /* move cursor left */
                if (s->esc_params[0] == 0) {
                    s->esc_params[0] = 1;
                }
                set_cursor(s, s->x - s->esc_params[0], s->y);
                break;
            case 'G':
                /* move cursor to column */
                set_cursor(s, s->esc_params[0] - 1, s->y);
                break;
            case 'f':
            case 'H':
                /* move cursor to row, column */
                set_cursor(s, s->esc_params[1] - 1, s->esc_params[0] - 1);
                break;
            case 'J':
                switch (s->esc_params[0]) {
                case 0:
                    /* clear to end of screen */
                    for (y = s->y; y < s->height; y++) {
                        for (x = 0; x < s->width; x++) {
                            if (y == s->y && x < s->x) {
                                continue;
                            }
                            console_clear_xy(s, x, y);
                        }
                    }
                    break;
                case 1:
                    /* clear from beginning of screen */
                    for (y = 0; y <= s->y; y++) {
                        for (x = 0; x < s->width; x++) {
                            if (y == s->y && x > s->x) {
                                break;
                            }
                            console_clear_xy(s, x, y);
                        }
                    }
                    break;
                case 2:
                    /* clear entire screen */
                    for (y = 0; y <= s->height; y++) {
                        for (x = 0; x < s->width; x++) {
                            console_clear_xy(s, x, y);
                        }
                    }
                    break;
                }
                break;
            case 'K':
                switch (s->esc_params[0]) {
                case 0:
                    /* clear to eol */
                    for(x = s->x; x < s->width; x++) {
                        console_clear_xy(s, x, s->y);
                    }
                    break;
                case 1:
                    /* clear from beginning of line */
                    for (x = 0; x <= s->x && x < s->width; x++) {
                        console_clear_xy(s, x, s->y);
                    }
                    break;
                case 2:
                    /* clear entire line */
                    for(x = 0; x < s->width; x++) {
                        console_clear_xy(s, x, s->y);
                    }
                    break;
                }
                break;
            case 'm':
                console_handle_escape(s);
                break;
            case 'n':
                switch (s->esc_params[0]) {
                case 5:
                    /* report console status (always succeed)*/
                    console_respond_str(s, "\033[0n");
                    break;
                case 6:
                    /* report cursor position */
                    sprintf(response, "\033[%d;%dR",
                           (s->y_base + s->y) % s->total_height + 1,
                            s->x + 1);
                    console_respond_str(s, response);
                    break;
                }
                break;
            case 's':
                /* save cursor position */
                s->x_saved = s->x;
                s->y_saved = s->y;
                break;
            case 'u':
                /* restore cursor position */
                s->x = s->x_saved;
                s->y = s->y_saved;
                break;
            default:
                trace_console_putchar_unhandled(ch);
                break;
            }
            break;
        }
    }
}

static void displaychangelistener_gfx_switch(DisplayChangeListener *dcl,
                                             struct DisplaySurface *new_surface,
                                             bool update)
{
    if (dcl->ops->dpy_gfx_switch) {
        dcl->ops->dpy_gfx_switch(dcl, new_surface);
    }

    if (update && dcl->ops->dpy_gfx_update) {
        dcl->ops->dpy_gfx_update(dcl, 0, 0,
                                 surface_width(new_surface),
                                 surface_height(new_surface));
    }
}

static void dpy_gfx_create_texture(QemuConsole *con, DisplaySurface *surface)
{
    if (con->gl && con->gl->ops->dpy_gl_ctx_create_texture) {
        con->gl->ops->dpy_gl_ctx_create_texture(con->gl, surface);
    }
}

static void dpy_gfx_destroy_texture(QemuConsole *con, DisplaySurface *surface)
{
    if (con->gl && con->gl->ops->dpy_gl_ctx_destroy_texture) {
        con->gl->ops->dpy_gl_ctx_destroy_texture(con->gl, surface);
    }
}

static void dpy_gfx_update_texture(QemuConsole *con, DisplaySurface *surface,
                                   int x, int y, int w, int h)
{
    if (con->gl && con->gl->ops->dpy_gl_ctx_update_texture) {
        con->gl->ops->dpy_gl_ctx_update_texture(con->gl, surface, x, y, w, h);
    }
}

static void displaychangelistener_display_console(DisplayChangeListener *dcl,
                                                  QemuConsole *con,
                                                  Error **errp)
{
    static const char nodev[] =
        "This VM has no graphic display device.";
    static DisplaySurface *dummy;

    if (!con || !console_compatible_with(con, dcl, errp)) {
        if (!dummy) {
            dummy = qemu_create_placeholder_surface(640, 480, nodev);
        }
        if (con) {
            dpy_gfx_create_texture(con, dummy);
        }
        displaychangelistener_gfx_switch(dcl, dummy, TRUE);
        return;
    }

    dpy_gfx_create_texture(con, con->surface);
    displaychangelistener_gfx_switch(dcl, con->surface,
                                     con->scanout.kind == SCANOUT_SURFACE);

    if (con->scanout.kind == SCANOUT_DMABUF &&
        displaychangelistener_has_dmabuf(dcl)) {
        dcl->ops->dpy_gl_scanout_dmabuf(dcl, con->scanout.dmabuf);
    } else if (con->scanout.kind == SCANOUT_TEXTURE &&
               dcl->ops->dpy_gl_scanout_texture) {
        dcl->ops->dpy_gl_scanout_texture(dcl,
                                         con->scanout.texture.backing_id,
                                         con->scanout.texture.backing_y_0_top,
                                         con->scanout.texture.backing_width,
                                         con->scanout.texture.backing_height,
                                         con->scanout.texture.x,
                                         con->scanout.texture.y,
                                         con->scanout.texture.width,
                                         con->scanout.texture.height);
    }
}

void console_select(unsigned int index)
{
    DisplayChangeListener *dcl;
    QemuConsole *s;

    trace_console_select(index);
    s = qemu_console_lookup_by_index(index);
    if (s) {
        DisplayState *ds = s->ds;

        active_console = s;
        if (ds->have_gfx) {
            QLIST_FOREACH(dcl, &ds->listeners, next) {
                if (dcl->con != NULL) {
                    continue;
                }
                displaychangelistener_display_console(dcl, s, NULL);
            }
        }
        if (ds->have_text) {
            dpy_text_resize(s, s->width, s->height);
        }
        text_console_update_cursor(NULL);
    }
}

struct VCChardev {
    Chardev parent;
    QemuConsole *console;
};
typedef struct VCChardev VCChardev;

#define TYPE_CHARDEV_VC "chardev-vc"
DECLARE_INSTANCE_CHECKER(VCChardev, VC_CHARDEV,
                         TYPE_CHARDEV_VC)

static int vc_chr_write(Chardev *chr, const uint8_t *buf, int len)
{
    VCChardev *drv = VC_CHARDEV(chr);
    QemuConsole *s = drv->console;
    int i;

    if (!s->ds) {
        return 0;
    }

    s->update_x0 = s->width * FONT_WIDTH;
    s->update_y0 = s->height * FONT_HEIGHT;
    s->update_x1 = 0;
    s->update_y1 = 0;
    console_show_cursor(s, 0);
    for(i = 0; i < len; i++) {
        console_putchar(s, buf[i]);
    }
    console_show_cursor(s, 1);
    if (s->ds->have_gfx && s->update_x0 < s->update_x1) {
        dpy_gfx_update(s, s->update_x0, s->update_y0,
                       s->update_x1 - s->update_x0,
                       s->update_y1 - s->update_y0);
    }
    return len;
}

static void kbd_send_chars(QemuConsole *s)
{
    uint32_t len, avail;

    len = qemu_chr_be_can_write(s->chr);
    avail = fifo8_num_used(&s->out_fifo);
    while (len > 0 && avail > 0) {
        const uint8_t *buf;
        uint32_t size;

        buf = fifo8_pop_buf(&s->out_fifo, MIN(len, avail), &size);
        qemu_chr_be_write(s->chr, buf, size);
        len = qemu_chr_be_can_write(s->chr);
        avail -= size;
    }
}

/* called when an ascii key is pressed */
void kbd_put_keysym_console(QemuConsole *s, int keysym)
{
    uint8_t buf[16], *q;
    int c;
    uint32_t num_free;

    if (!s || (s->console_type == GRAPHIC_CONSOLE))
        return;

    switch(keysym) {
    case QEMU_KEY_CTRL_UP:
        console_scroll(s, -1);
        break;
    case QEMU_KEY_CTRL_DOWN:
        console_scroll(s, 1);
        break;
    case QEMU_KEY_CTRL_PAGEUP:
        console_scroll(s, -10);
        break;
    case QEMU_KEY_CTRL_PAGEDOWN:
        console_scroll(s, 10);
        break;
    default:
        /* convert the QEMU keysym to VT100 key string */
        q = buf;
        if (keysym >= 0xe100 && keysym <= 0xe11f) {
            *q++ = '\033';
            *q++ = '[';
            c = keysym - 0xe100;
            if (c >= 10)
                *q++ = '0' + (c / 10);
            *q++ = '0' + (c % 10);
            *q++ = '~';
        } else if (keysym >= 0xe120 && keysym <= 0xe17f) {
            *q++ = '\033';
            *q++ = '[';
            *q++ = keysym & 0xff;
        } else if (s->echo && (keysym == '\r' || keysym == '\n')) {
            vc_chr_write(s->chr, (const uint8_t *) "\r", 1);
            *q++ = '\n';
        } else {
            *q++ = keysym;
        }
        if (s->echo) {
            vc_chr_write(s->chr, buf, q - buf);
        }
        num_free = fifo8_num_free(&s->out_fifo);
        fifo8_push_all(&s->out_fifo, buf, MIN(num_free, q - buf));
        kbd_send_chars(s);
        break;
    }
}

static const int qcode_to_keysym[Q_KEY_CODE__MAX] = {
    [Q_KEY_CODE_UP]     = QEMU_KEY_UP,
    [Q_KEY_CODE_DOWN]   = QEMU_KEY_DOWN,
    [Q_KEY_CODE_RIGHT]  = QEMU_KEY_RIGHT,
    [Q_KEY_CODE_LEFT]   = QEMU_KEY_LEFT,
    [Q_KEY_CODE_HOME]   = QEMU_KEY_HOME,
    [Q_KEY_CODE_END]    = QEMU_KEY_END,
    [Q_KEY_CODE_PGUP]   = QEMU_KEY_PAGEUP,
    [Q_KEY_CODE_PGDN]   = QEMU_KEY_PAGEDOWN,
    [Q_KEY_CODE_DELETE] = QEMU_KEY_DELETE,
    [Q_KEY_CODE_TAB]    = QEMU_KEY_TAB,
    [Q_KEY_CODE_BACKSPACE] = QEMU_KEY_BACKSPACE,
};

static const int ctrl_qcode_to_keysym[Q_KEY_CODE__MAX] = {
    [Q_KEY_CODE_UP]     = QEMU_KEY_CTRL_UP,
    [Q_KEY_CODE_DOWN]   = QEMU_KEY_CTRL_DOWN,
    [Q_KEY_CODE_RIGHT]  = QEMU_KEY_CTRL_RIGHT,
    [Q_KEY_CODE_LEFT]   = QEMU_KEY_CTRL_LEFT,
    [Q_KEY_CODE_HOME]   = QEMU_KEY_CTRL_HOME,
    [Q_KEY_CODE_END]    = QEMU_KEY_CTRL_END,
    [Q_KEY_CODE_PGUP]   = QEMU_KEY_CTRL_PAGEUP,
    [Q_KEY_CODE_PGDN]   = QEMU_KEY_CTRL_PAGEDOWN,
};

bool kbd_put_qcode_console(QemuConsole *s, int qcode, bool ctrl)
{
    int keysym;

    keysym = ctrl ? ctrl_qcode_to_keysym[qcode] : qcode_to_keysym[qcode];
    if (keysym == 0) {
        return false;
    }
    kbd_put_keysym_console(s, keysym);
    return true;
}

void kbd_put_string_console(QemuConsole *s, const char *str, int len)
{
    int i;

    for (i = 0; i < len && str[i]; i++) {
        kbd_put_keysym_console(s, str[i]);
    }
}

void kbd_put_keysym(int keysym)
{
    kbd_put_keysym_console(active_console, keysym);
}

static void text_console_invalidate(void *opaque)
{
    QemuConsole *s = (QemuConsole *) opaque;

    if (s->ds->have_text && s->console_type == TEXT_CONSOLE) {
        text_console_resize(s);
    }
    console_refresh(s);
}

static void text_console_update(void *opaque, console_ch_t *chardata)
{
    QemuConsole *s = (QemuConsole *) opaque;
    int i, j, src;

    if (s->text_x[0] <= s->text_x[1]) {
        src = (s->y_base + s->text_y[0]) * s->width;
        chardata += s->text_y[0] * s->width;
        for (i = s->text_y[0]; i <= s->text_y[1]; i ++)
            for (j = 0; j < s->width; j++, src++) {
                console_write_ch(chardata ++,
                                 ATTR2CHTYPE(s->cells[src].ch,
                                             s->cells[src].t_attrib.fgcol,
                                             s->cells[src].t_attrib.bgcol,
                                             s->cells[src].t_attrib.bold));
            }
        dpy_text_update(s, s->text_x[0], s->text_y[0],
                        s->text_x[1] - s->text_x[0], i - s->text_y[0]);
        s->text_x[0] = s->width;
        s->text_y[0] = s->height;
        s->text_x[1] = 0;
        s->text_y[1] = 0;
    }
    if (s->cursor_invalidate) {
        dpy_text_cursor(s, s->x, s->y);
        s->cursor_invalidate = 0;
    }
}

static QemuConsole *new_console(DisplayState *ds, console_type_t console_type,
                                uint32_t head)
{
    Object *obj;
    QemuConsole *s;
    int i;

    obj = object_new(TYPE_QEMU_CONSOLE);
    s = QEMU_CONSOLE(obj);
    qemu_co_queue_init(&s->dump_queue);
    s->head = head;
    object_property_add_link(obj, "device", TYPE_DEVICE,
                             (Object **)&s->device,
                             object_property_allow_set_link,
                             OBJ_PROP_LINK_STRONG);
    object_property_add_uint32_ptr(obj, "head", &s->head,
                                   OBJ_PROP_FLAG_READ);

    if (!active_console || ((active_console->console_type != GRAPHIC_CONSOLE) &&
        (console_type == GRAPHIC_CONSOLE))) {
        active_console = s;
    }
    s->ds = ds;
    s->console_type = console_type;
    s->window_id = -1;

    if (QTAILQ_EMPTY(&consoles)) {
        s->index = 0;
        QTAILQ_INSERT_TAIL(&consoles, s, next);
    } else if (console_type != GRAPHIC_CONSOLE || phase_check(PHASE_MACHINE_READY)) {
        QemuConsole *last = QTAILQ_LAST(&consoles);
        s->index = last->index + 1;
        QTAILQ_INSERT_TAIL(&consoles, s, next);
    } else {
        /*
         * HACK: Put graphical consoles before text consoles.
         *
         * Only do that for coldplugged devices.  After initial device
         * initialization we will not renumber the consoles any more.
         */
        QemuConsole *c = QTAILQ_FIRST(&consoles);

        while (QTAILQ_NEXT(c, next) != NULL &&
               c->console_type == GRAPHIC_CONSOLE) {
            c = QTAILQ_NEXT(c, next);
        }
        if (c->console_type == GRAPHIC_CONSOLE) {
            /* have no text consoles */
            s->index = c->index + 1;
            QTAILQ_INSERT_AFTER(&consoles, c, s, next);
        } else {
            s->index = c->index;
            QTAILQ_INSERT_BEFORE(c, s, next);
            /* renumber text consoles */
            for (i = s->index + 1; c != NULL; c = QTAILQ_NEXT(c, next), i++) {
                c->index = i;
            }
        }
    }
    return s;
}

DisplaySurface *qemu_create_displaysurface(int width, int height)
{
    DisplaySurface *surface = g_new0(DisplaySurface, 1);

    trace_displaysurface_create(surface, width, height);
    surface->format = PIXMAN_x8r8g8b8;
    surface->image = pixman_image_create_bits(surface->format,
                                              width, height,
                                              NULL, width * 4);
    assert(surface->image != NULL);
    surface->flags = QEMU_ALLOCATED_FLAG;

    return surface;
}

DisplaySurface *qemu_create_displaysurface_from(int width, int height,
                                                pixman_format_code_t format,
                                                int linesize, uint8_t *data)
{
    DisplaySurface *surface = g_new0(DisplaySurface, 1);

    trace_displaysurface_create_from(surface, width, height, format);
    surface->format = format;
    surface->image = pixman_image_create_bits(surface->format,
                                              width, height,
                                              (void *)data, linesize);
    assert(surface->image != NULL);

    return surface;
}

DisplaySurface *qemu_create_displaysurface_pixman(pixman_image_t *image)
{
    DisplaySurface *surface = g_new0(DisplaySurface, 1);

    trace_displaysurface_create_pixman(surface);
    surface->format = pixman_image_get_format(image);
    surface->image = pixman_image_ref(image);

    return surface;
}

DisplaySurface *qemu_create_placeholder_surface(int w, int h,
                                                const char *msg)
{
    DisplaySurface *surface = qemu_create_displaysurface(w, h);
    pixman_color_t bg = color_table_rgb[0][QEMU_COLOR_BLACK];
    pixman_color_t fg = color_table_rgb[0][QEMU_COLOR_WHITE];
    pixman_image_t *glyph;
    int len, x, y, i;

    len = strlen(msg);
    x = (w / FONT_WIDTH  - len) / 2;
    y = (h / FONT_HEIGHT - 1)   / 2;
    for (i = 0; i < len; i++) {
        glyph = qemu_pixman_glyph_from_vgafont(FONT_HEIGHT, vgafont16, msg[i]);
        qemu_pixman_glyph_render(glyph, surface->image, &fg, &bg,
                                 x+i, y, FONT_WIDTH, FONT_HEIGHT);
        qemu_pixman_image_unref(glyph);
    }
    surface->flags |= QEMU_PLACEHOLDER_FLAG;
    return surface;
}

void qemu_free_displaysurface(DisplaySurface *surface)
{
    if (surface == NULL) {
        return;
    }
    trace_displaysurface_free(surface);
    qemu_pixman_image_unref(surface->image);
    g_free(surface);
}

bool console_has_gl(QemuConsole *con)
{
    return con->gl != NULL;
}

static bool displaychangelistener_has_dmabuf(DisplayChangeListener *dcl)
{
    if (dcl->ops->dpy_has_dmabuf) {
        return dcl->ops->dpy_has_dmabuf(dcl);
    }

    if (dcl->ops->dpy_gl_scanout_dmabuf) {
        return true;
    }

    return false;
}

static bool console_compatible_with(QemuConsole *con,
                                    DisplayChangeListener *dcl, Error **errp)
{
    int flags;

    flags = con->hw_ops->get_flags ? con->hw_ops->get_flags(con->hw) : 0;

    if (console_has_gl(con) &&
        !con->gl->ops->dpy_gl_ctx_is_compatible_dcl(con->gl, dcl)) {
        error_setg(errp, "Display %s is incompatible with the GL context",
                   dcl->ops->dpy_name);
        return false;
    }

    if (flags & GRAPHIC_FLAGS_GL &&
        !console_has_gl(con)) {
        error_setg(errp, "The console requires a GL context.");
        return false;

    }

    if (flags & GRAPHIC_FLAGS_DMABUF &&
        !displaychangelistener_has_dmabuf(dcl)) {
        error_setg(errp, "The console requires display DMABUF support.");
        return false;
    }

    return true;
}

void qemu_console_set_display_gl_ctx(QemuConsole *con, DisplayGLCtx *gl)
{
    /* display has opengl support */
    assert(con);
    if (con->gl) {
        error_report("The console already has an OpenGL context.");
        exit(1);
    }
    con->gl = gl;
}

void register_displaychangelistener(DisplayChangeListener *dcl)
{
    QemuConsole *con;

    assert(!dcl->ds);

    trace_displaychangelistener_register(dcl, dcl->ops->dpy_name);
    dcl->ds = get_alloc_displaystate();
    QLIST_INSERT_HEAD(&dcl->ds->listeners, dcl, next);
    gui_setup_refresh(dcl->ds);
    if (dcl->con) {
        dcl->con->dcls++;
        con = dcl->con;
    } else {
        con = active_console;
    }
    displaychangelistener_display_console(dcl, con, dcl->con ? &error_fatal : NULL);
    if (con && con->cursor && dcl->ops->dpy_cursor_define) {
        dcl->ops->dpy_cursor_define(dcl, con->cursor);
    }
    if (con && dcl->ops->dpy_mouse_set) {
        dcl->ops->dpy_mouse_set(dcl, con->cursor_x, con->cursor_y, con->cursor_on);
    }
    text_console_update_cursor(NULL);
}

void update_displaychangelistener(DisplayChangeListener *dcl,
                                  uint64_t interval)
{
    DisplayState *ds = dcl->ds;

    dcl->update_interval = interval;
    if (!ds->refreshing && ds->update_interval > interval) {
        timer_mod(ds->gui_timer, ds->last_update + interval);
    }
}

void unregister_displaychangelistener(DisplayChangeListener *dcl)
{
    DisplayState *ds = dcl->ds;
    trace_displaychangelistener_unregister(dcl, dcl->ops->dpy_name);
    if (dcl->con) {
        dcl->con->dcls--;
    }
    QLIST_REMOVE(dcl, next);
    dcl->ds = NULL;
    gui_setup_refresh(ds);
}

static void dpy_set_ui_info_timer(void *opaque)
{
    QemuConsole *con = opaque;

    con->hw_ops->ui_info(con->hw, con->head, &con->ui_info);
}

bool dpy_ui_info_supported(QemuConsole *con)
{
    if (con == NULL) {
        con = active_console;
    }
    if (con == NULL) {
        return false;
    }

    return con->hw_ops->ui_info != NULL;
}

const QemuUIInfo *dpy_get_ui_info(const QemuConsole *con)
{
    if (con == NULL) {
        con = active_console;
    }

    return &con->ui_info;
}

int dpy_set_ui_info(QemuConsole *con, QemuUIInfo *info, bool delay)
{
    if (con == NULL) {
        con = active_console;
    }

    if (!dpy_ui_info_supported(con)) {
        return -1;
    }
    if (memcmp(&con->ui_info, info, sizeof(con->ui_info)) == 0) {
        /* nothing changed -- ignore */
        return 0;
    }

    /*
     * Typically we get a flood of these as the user resizes the window.
     * Wait until the dust has settled (one second without updates), then
     * go notify the guest.
     */
    con->ui_info = *info;
    timer_mod(con->ui_timer,
              qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + (delay ? 1000 : 0));
    return 0;
}

void dpy_gfx_update(QemuConsole *con, int x, int y, int w, int h)
{
    DisplayState *s = con->ds;
    DisplayChangeListener *dcl;
    int width = qemu_console_get_width(con, x + w);
    int height = qemu_console_get_height(con, y + h);

    x = MAX(x, 0);
    y = MAX(y, 0);
    x = MIN(x, width);
    y = MIN(y, height);
    w = MIN(w, width - x);
    h = MIN(h, height - y);

    if (!qemu_console_is_visible(con)) {
        return;
    }
    dpy_gfx_update_texture(con, con->surface, x, y, w, h);
    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (con != (dcl->con ? dcl->con : active_console)) {
            continue;
        }
        if (dcl->ops->dpy_gfx_update) {
            dcl->ops->dpy_gfx_update(dcl, x, y, w, h);
        }
    }
}

void dpy_gfx_update_full(QemuConsole *con)
{
    int w = qemu_console_get_width(con, 0);
    int h = qemu_console_get_height(con, 0);

    dpy_gfx_update(con, 0, 0, w, h);
}

void dpy_gfx_replace_surface(QemuConsole *con,
                             DisplaySurface *surface)
{
    static const char placeholder_msg[] = "Display output is not active.";
    DisplayState *s = con->ds;
    DisplaySurface *old_surface = con->surface;
    DisplayChangeListener *dcl;
    int width;
    int height;

    if (!surface) {
        if (old_surface) {
            width = surface_width(old_surface);
            height = surface_height(old_surface);
        } else {
            width = 640;
            height = 480;
        }

        surface = qemu_create_placeholder_surface(width, height, placeholder_msg);
    }

    assert(old_surface != surface);

    con->scanout.kind = SCANOUT_SURFACE;
    con->surface = surface;
    dpy_gfx_create_texture(con, surface);
    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (con != (dcl->con ? dcl->con : active_console)) {
            continue;
        }
        displaychangelistener_gfx_switch(dcl, surface, FALSE);
    }
    dpy_gfx_destroy_texture(con, old_surface);
    qemu_free_displaysurface(old_surface);
}

bool dpy_gfx_check_format(QemuConsole *con,
                          pixman_format_code_t format)
{
    DisplayChangeListener *dcl;
    DisplayState *s = con->ds;

    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (dcl->con && dcl->con != con) {
            /* dcl bound to another console -> skip */
            continue;
        }
        if (dcl->ops->dpy_gfx_check_format) {
            if (!dcl->ops->dpy_gfx_check_format(dcl, format)) {
                return false;
            }
        } else {
            /* default is to allow native 32 bpp only */
            if (format != qemu_default_pixman_format(32, true)) {
                return false;
            }
        }
    }
    return true;
}

static void dpy_refresh(DisplayState *s)
{
    DisplayChangeListener *dcl;

    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (dcl->ops->dpy_refresh) {
            dcl->ops->dpy_refresh(dcl);
        }
    }
}

void dpy_text_cursor(QemuConsole *con, int x, int y)
{
    DisplayState *s = con->ds;
    DisplayChangeListener *dcl;

    if (!qemu_console_is_visible(con)) {
        return;
    }
    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (con != (dcl->con ? dcl->con : active_console)) {
            continue;
        }
        if (dcl->ops->dpy_text_cursor) {
            dcl->ops->dpy_text_cursor(dcl, x, y);
        }
    }
}

void dpy_text_update(QemuConsole *con, int x, int y, int w, int h)
{
    DisplayState *s = con->ds;
    DisplayChangeListener *dcl;

    if (!qemu_console_is_visible(con)) {
        return;
    }
    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (con != (dcl->con ? dcl->con : active_console)) {
            continue;
        }
        if (dcl->ops->dpy_text_update) {
            dcl->ops->dpy_text_update(dcl, x, y, w, h);
        }
    }
}

void dpy_text_resize(QemuConsole *con, int w, int h)
{
    DisplayState *s = con->ds;
    DisplayChangeListener *dcl;

    if (!qemu_console_is_visible(con)) {
        return;
    }
    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (con != (dcl->con ? dcl->con : active_console)) {
            continue;
        }
        if (dcl->ops->dpy_text_resize) {
            dcl->ops->dpy_text_resize(dcl, w, h);
        }
    }
}

void dpy_mouse_set(QemuConsole *con, int x, int y, int on)
{
    DisplayState *s = con->ds;
    DisplayChangeListener *dcl;

    con->cursor_x = x;
    con->cursor_y = y;
    con->cursor_on = on;
    if (!qemu_console_is_visible(con)) {
        return;
    }
    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (con != (dcl->con ? dcl->con : active_console)) {
            continue;
        }
        if (dcl->ops->dpy_mouse_set) {
            dcl->ops->dpy_mouse_set(dcl, x, y, on);
        }
    }
}

void dpy_cursor_define(QemuConsole *con, QEMUCursor *cursor)
{
    DisplayState *s = con->ds;
    DisplayChangeListener *dcl;

    cursor_unref(con->cursor);
    con->cursor = cursor_ref(cursor);
    if (!qemu_console_is_visible(con)) {
        return;
    }
    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (con != (dcl->con ? dcl->con : active_console)) {
            continue;
        }
        if (dcl->ops->dpy_cursor_define) {
            dcl->ops->dpy_cursor_define(dcl, cursor);
        }
    }
}

bool dpy_cursor_define_supported(QemuConsole *con)
{
    DisplayState *s = con->ds;
    DisplayChangeListener *dcl;

    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (dcl->ops->dpy_cursor_define) {
            return true;
        }
    }
    return false;
}

QEMUGLContext dpy_gl_ctx_create(QemuConsole *con,
                                struct QEMUGLParams *qparams)
{
    assert(con->gl);
    return con->gl->ops->dpy_gl_ctx_create(con->gl, qparams);
}

void dpy_gl_ctx_destroy(QemuConsole *con, QEMUGLContext ctx)
{
    assert(con->gl);
    con->gl->ops->dpy_gl_ctx_destroy(con->gl, ctx);
}

int dpy_gl_ctx_make_current(QemuConsole *con, QEMUGLContext ctx)
{
    assert(con->gl);
    return con->gl->ops->dpy_gl_ctx_make_current(con->gl, ctx);
}

void dpy_gl_scanout_disable(QemuConsole *con)
{
    DisplayState *s = con->ds;
    DisplayChangeListener *dcl;

    if (con->scanout.kind != SCANOUT_SURFACE) {
        con->scanout.kind = SCANOUT_NONE;
    }
    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (con != (dcl->con ? dcl->con : active_console)) {
            continue;
        }
        if (dcl->ops->dpy_gl_scanout_disable) {
            dcl->ops->dpy_gl_scanout_disable(dcl);
        }
    }
}

void dpy_gl_scanout_texture(QemuConsole *con,
                            uint32_t backing_id,
                            bool backing_y_0_top,
                            uint32_t backing_width,
                            uint32_t backing_height,
                            uint32_t x, uint32_t y,
                            uint32_t width, uint32_t height)
{
    DisplayState *s = con->ds;
    DisplayChangeListener *dcl;

    con->scanout.kind = SCANOUT_TEXTURE;
    con->scanout.texture = (ScanoutTexture) {
        backing_id, backing_y_0_top, backing_width, backing_height,
        x, y, width, height
    };
    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (con != (dcl->con ? dcl->con : active_console)) {
            continue;
        }
        if (dcl->ops->dpy_gl_scanout_texture) {
            dcl->ops->dpy_gl_scanout_texture(dcl, backing_id,
                                             backing_y_0_top,
                                             backing_width, backing_height,
                                             x, y, width, height);
        }
    }
}

void dpy_gl_scanout_dmabuf(QemuConsole *con,
                           QemuDmaBuf *dmabuf)
{
    DisplayState *s = con->ds;
    DisplayChangeListener *dcl;

    con->scanout.kind = SCANOUT_DMABUF;
    con->scanout.dmabuf = dmabuf;
    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (con != (dcl->con ? dcl->con : active_console)) {
            continue;
        }
        if (dcl->ops->dpy_gl_scanout_dmabuf) {
            dcl->ops->dpy_gl_scanout_dmabuf(dcl, dmabuf);
        }
    }
}

void dpy_gl_cursor_dmabuf(QemuConsole *con, QemuDmaBuf *dmabuf,
                          bool have_hot, uint32_t hot_x, uint32_t hot_y)
{
    DisplayState *s = con->ds;
    DisplayChangeListener *dcl;

    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (con != (dcl->con ? dcl->con : active_console)) {
            continue;
        }
        if (dcl->ops->dpy_gl_cursor_dmabuf) {
            dcl->ops->dpy_gl_cursor_dmabuf(dcl, dmabuf,
                                           have_hot, hot_x, hot_y);
        }
    }
}

void dpy_gl_cursor_position(QemuConsole *con,
                            uint32_t pos_x, uint32_t pos_y)
{
    DisplayState *s = con->ds;
    DisplayChangeListener *dcl;

    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (con != (dcl->con ? dcl->con : active_console)) {
            continue;
        }
        if (dcl->ops->dpy_gl_cursor_position) {
            dcl->ops->dpy_gl_cursor_position(dcl, pos_x, pos_y);
        }
    }
}

void dpy_gl_release_dmabuf(QemuConsole *con,
                          QemuDmaBuf *dmabuf)
{
    DisplayState *s = con->ds;
    DisplayChangeListener *dcl;

    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (con != (dcl->con ? dcl->con : active_console)) {
            continue;
        }
        if (dcl->ops->dpy_gl_release_dmabuf) {
            dcl->ops->dpy_gl_release_dmabuf(dcl, dmabuf);
        }
    }
}

void dpy_gl_update(QemuConsole *con,
                   uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    DisplayState *s = con->ds;
    DisplayChangeListener *dcl;

    assert(con->gl);

    graphic_hw_gl_block(con, true);
    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (con != (dcl->con ? dcl->con : active_console)) {
            continue;
        }
        if (dcl->ops->dpy_gl_update) {
            dcl->ops->dpy_gl_update(dcl, x, y, w, h);
        }
    }
    graphic_hw_gl_block(con, false);
}

/***********************************************************/
/* register display */

/* console.c internal use only */
static DisplayState *get_alloc_displaystate(void)
{
    if (!display_state) {
        display_state = g_new0(DisplayState, 1);
        cursor_timer = timer_new_ms(QEMU_CLOCK_REALTIME,
                                    text_console_update_cursor, NULL);
    }
    return display_state;
}

/*
 * Called by main(), after creating QemuConsoles
 * and before initializing ui (sdl/vnc/...).
 */
DisplayState *init_displaystate(void)
{
    gchar *name;
    QemuConsole *con;

    get_alloc_displaystate();
    QTAILQ_FOREACH(con, &consoles, next) {
        if (con->console_type != GRAPHIC_CONSOLE &&
            con->ds == NULL) {
            text_console_do_init(con->chr, display_state);
        }

        /* Hook up into the qom tree here (not in new_console()), once
         * all QemuConsoles are created and the order / numbering
         * doesn't change any more */
        name = g_strdup_printf("console[%d]", con->index);
        object_property_add_child(container_get(object_get_root(), "/backend"),
                                  name, OBJECT(con));
        g_free(name);
    }

    return display_state;
}

void graphic_console_set_hwops(QemuConsole *con,
                               const GraphicHwOps *hw_ops,
                               void *opaque)
{
    con->hw_ops = hw_ops;
    con->hw = opaque;
}

QemuConsole *graphic_console_init(DeviceState *dev, uint32_t head,
                                  const GraphicHwOps *hw_ops,
                                  void *opaque)
{
    static const char noinit[] =
        "Guest has not initialized the display (yet).";
    int width = 640;
    int height = 480;
    QemuConsole *s;
    DisplayState *ds;
    DisplaySurface *surface;

    ds = get_alloc_displaystate();
    s = qemu_console_lookup_unused();
    if (s) {
        trace_console_gfx_reuse(s->index);
        width = qemu_console_get_width(s, 0);
        height = qemu_console_get_height(s, 0);
    } else {
        trace_console_gfx_new();
        s = new_console(ds, GRAPHIC_CONSOLE, head);
        s->ui_timer = timer_new_ms(QEMU_CLOCK_REALTIME,
                                   dpy_set_ui_info_timer, s);
    }
    graphic_console_set_hwops(s, hw_ops, opaque);
    if (dev) {
        object_property_set_link(OBJECT(s), "device", OBJECT(dev),
                                 &error_abort);
    }

    surface = qemu_create_placeholder_surface(width, height, noinit);
    dpy_gfx_replace_surface(s, surface);
    s->gl_unblock_timer = timer_new_ms(QEMU_CLOCK_REALTIME,
                                       graphic_hw_gl_unblock_timer, s);
    return s;
}

static const GraphicHwOps unused_ops = {
    /* no callbacks */
};

void graphic_console_close(QemuConsole *con)
{
    static const char unplugged[] =
        "Guest display has been unplugged";
    DisplaySurface *surface;
    int width = qemu_console_get_width(con, 640);
    int height = qemu_console_get_height(con, 480);

    trace_console_gfx_close(con->index);
    object_property_set_link(OBJECT(con), "device", NULL, &error_abort);
    graphic_console_set_hwops(con, &unused_ops, NULL);

    if (con->gl) {
        dpy_gl_scanout_disable(con);
    }
    surface = qemu_create_placeholder_surface(width, height, unplugged);
    dpy_gfx_replace_surface(con, surface);
}

QemuConsole *qemu_console_lookup_by_index(unsigned int index)
{
    QemuConsole *con;

    QTAILQ_FOREACH(con, &consoles, next) {
        if (con->index == index) {
            return con;
        }
    }
    return NULL;
}

QemuConsole *qemu_console_lookup_by_device(DeviceState *dev, uint32_t head)
{
    QemuConsole *con;
    Object *obj;
    uint32_t h;

    QTAILQ_FOREACH(con, &consoles, next) {
        obj = object_property_get_link(OBJECT(con),
                                       "device", &error_abort);
        if (DEVICE(obj) != dev) {
            continue;
        }
        h = object_property_get_uint(OBJECT(con),
                                     "head", &error_abort);
        if (h != head) {
            continue;
        }
        return con;
    }
    return NULL;
}

QemuConsole *qemu_console_lookup_by_device_name(const char *device_id,
                                                uint32_t head, Error **errp)
{
    DeviceState *dev;
    QemuConsole *con;

    dev = qdev_find_recursive(sysbus_get_default(), device_id);
    if (dev == NULL) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", device_id);
        return NULL;
    }

    con = qemu_console_lookup_by_device(dev, head);
    if (con == NULL) {
        error_setg(errp, "Device %s (head %d) is not bound to a QemuConsole",
                   device_id, head);
        return NULL;
    }

    return con;
}

QemuConsole *qemu_console_lookup_unused(void)
{
    QemuConsole *con;
    Object *obj;

    QTAILQ_FOREACH(con, &consoles, next) {
        if (con->hw_ops != &unused_ops) {
            continue;
        }
        obj = object_property_get_link(OBJECT(con),
                                       "device", &error_abort);
        if (obj != NULL) {
            continue;
        }
        return con;
    }
    return NULL;
}

QEMUCursor *qemu_console_get_cursor(QemuConsole *con)
{
    if (con == NULL) {
        con = active_console;
    }
    return con ? con->cursor : NULL;
}

bool qemu_console_is_visible(QemuConsole *con)
{
    return (con == active_console) || (con->dcls > 0);
}

bool qemu_console_is_graphic(QemuConsole *con)
{
    if (con == NULL) {
        con = active_console;
    }
    return con && (con->console_type == GRAPHIC_CONSOLE);
}

bool qemu_console_is_fixedsize(QemuConsole *con)
{
    if (con == NULL) {
        con = active_console;
    }
    return con && (con->console_type != TEXT_CONSOLE);
}

bool qemu_console_is_gl_blocked(QemuConsole *con)
{
    assert(con != NULL);
    return con->gl_block;
}

bool qemu_console_is_multihead(DeviceState *dev)
{
    QemuConsole *con;
    Object *obj;
    uint32_t f = 0xffffffff;
    uint32_t h;

    QTAILQ_FOREACH(con, &consoles, next) {
        obj = object_property_get_link(OBJECT(con),
                                       "device", &error_abort);
        if (DEVICE(obj) != dev) {
            continue;
        }

        h = object_property_get_uint(OBJECT(con),
                                     "head", &error_abort);
        if (f == 0xffffffff) {
            f = h;
        } else if (h != f) {
            return true;
        }
    }
    return false;
}

char *qemu_console_get_label(QemuConsole *con)
{
    if (con->console_type == GRAPHIC_CONSOLE) {
        if (con->device) {
            DeviceState *dev;
            bool multihead;

            dev = DEVICE(con->device);
            multihead = qemu_console_is_multihead(dev);
            if (multihead) {
                return g_strdup_printf("%s.%d", dev->id ?
                                       dev->id :
                                       object_get_typename(con->device),
                                       con->head);
            } else {
                return g_strdup_printf("%s", dev->id ?
                                       dev->id :
                                       object_get_typename(con->device));
            }
        }
        return g_strdup("VGA");
    } else {
        if (con->chr && con->chr->label) {
            return g_strdup(con->chr->label);
        }
        return g_strdup_printf("vc%d", con->index);
    }
}

int qemu_console_get_index(QemuConsole *con)
{
    if (con == NULL) {
        con = active_console;
    }
    return con ? con->index : -1;
}

uint32_t qemu_console_get_head(QemuConsole *con)
{
    if (con == NULL) {
        con = active_console;
    }
    return con ? con->head : -1;
}

int qemu_console_get_width(QemuConsole *con, int fallback)
{
    if (con == NULL) {
        con = active_console;
    }
    if (con == NULL) {
        return fallback;
    }
    switch (con->scanout.kind) {
    case SCANOUT_DMABUF:
        return con->scanout.dmabuf->width;
    case SCANOUT_TEXTURE:
        return con->scanout.texture.width;
    case SCANOUT_SURFACE:
        return surface_width(con->surface);
    default:
        return fallback;
    }
}

int qemu_console_get_height(QemuConsole *con, int fallback)
{
    if (con == NULL) {
        con = active_console;
    }
    if (con == NULL) {
        return fallback;
    }
    switch (con->scanout.kind) {
    case SCANOUT_DMABUF:
        return con->scanout.dmabuf->height;
    case SCANOUT_TEXTURE:
        return con->scanout.texture.height;
    case SCANOUT_SURFACE:
        return surface_height(con->surface);
    default:
        return fallback;
    }
}

static void vc_chr_accept_input(Chardev *chr)
{
    VCChardev *drv = VC_CHARDEV(chr);
    QemuConsole *s = drv->console;

    kbd_send_chars(s);
}

static void vc_chr_set_echo(Chardev *chr, bool echo)
{
    VCChardev *drv = VC_CHARDEV(chr);
    QemuConsole *s = drv->console;

    s->echo = echo;
}

static void text_console_update_cursor_timer(void)
{
    timer_mod(cursor_timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME)
              + CONSOLE_CURSOR_PERIOD / 2);
}

static void text_console_update_cursor(void *opaque)
{
    QemuConsole *s;
    int count = 0;

    cursor_visible_phase = !cursor_visible_phase;

    QTAILQ_FOREACH(s, &consoles, next) {
        if (qemu_console_is_graphic(s) ||
            !qemu_console_is_visible(s)) {
            continue;
        }
        count++;
        graphic_hw_invalidate(s);
    }

    if (count) {
        text_console_update_cursor_timer();
    }
}

static const GraphicHwOps text_console_ops = {
    .invalidate  = text_console_invalidate,
    .text_update = text_console_update,
};

static void text_console_do_init(Chardev *chr, DisplayState *ds)
{
    VCChardev *drv = VC_CHARDEV(chr);
    QemuConsole *s = drv->console;
    int g_width = 80 * FONT_WIDTH;
    int g_height = 24 * FONT_HEIGHT;

    fifo8_create(&s->out_fifo, 16);
    s->ds = ds;

    s->y_displayed = 0;
    s->y_base = 0;
    s->total_height = DEFAULT_BACKSCROLL;
    s->x = 0;
    s->y = 0;
    if (s->scanout.kind != SCANOUT_SURFACE) {
        if (active_console && active_console->scanout.kind == SCANOUT_SURFACE) {
            g_width = qemu_console_get_width(active_console, g_width);
            g_height = qemu_console_get_height(active_console, g_height);
        }
        s->surface = qemu_create_displaysurface(g_width, g_height);
        s->scanout.kind = SCANOUT_SURFACE;
    }

    s->hw_ops = &text_console_ops;
    s->hw = s;

    /* Set text attribute defaults */
    s->t_attrib_default.bold = 0;
    s->t_attrib_default.uline = 0;
    s->t_attrib_default.blink = 0;
    s->t_attrib_default.invers = 0;
    s->t_attrib_default.unvisible = 0;
    s->t_attrib_default.fgcol = QEMU_COLOR_WHITE;
    s->t_attrib_default.bgcol = QEMU_COLOR_BLACK;
    /* set current text attributes to default */
    s->t_attrib = s->t_attrib_default;
    text_console_resize(s);

    if (chr->label) {
        char *msg;

        s->t_attrib.bgcol = QEMU_COLOR_BLUE;
        msg = g_strdup_printf("%s console\r\n", chr->label);
        vc_chr_write(chr, (uint8_t *)msg, strlen(msg));
        g_free(msg);
        s->t_attrib = s->t_attrib_default;
    }

    qemu_chr_be_event(chr, CHR_EVENT_OPENED);
}

static void vc_chr_open(Chardev *chr,
                        ChardevBackend *backend,
                        bool *be_opened,
                        Error **errp)
{
    ChardevVC *vc = backend->u.vc.data;
    VCChardev *drv = VC_CHARDEV(chr);
    QemuConsole *s;
    unsigned width = 0;
    unsigned height = 0;

    if (vc->has_width) {
        width = vc->width;
    } else if (vc->has_cols) {
        width = vc->cols * FONT_WIDTH;
    }

    if (vc->has_height) {
        height = vc->height;
    } else if (vc->has_rows) {
        height = vc->rows * FONT_HEIGHT;
    }

    trace_console_txt_new(width, height);
    if (width == 0 || height == 0) {
        s = new_console(NULL, TEXT_CONSOLE, 0);
    } else {
        s = new_console(NULL, TEXT_CONSOLE_FIXED_SIZE, 0);
        s->scanout.kind = SCANOUT_SURFACE;
        s->surface = qemu_create_displaysurface(width, height);
    }

    if (!s) {
        error_setg(errp, "cannot create text console");
        return;
    }

    s->chr = chr;
    drv->console = s;

    if (display_state) {
        text_console_do_init(chr, display_state);
    }

    /* console/chardev init sometimes completes elsewhere in a 2nd
     * stage, so defer OPENED events until they are fully initialized
     */
    *be_opened = false;
}

void qemu_console_resize(QemuConsole *s, int width, int height)
{
    DisplaySurface *surface = qemu_console_surface(s);

    assert(s->console_type == GRAPHIC_CONSOLE);

    if ((s->scanout.kind != SCANOUT_SURFACE ||
         (surface && surface->flags & QEMU_ALLOCATED_FLAG)) &&
        qemu_console_get_width(s, -1) == width &&
        qemu_console_get_height(s, -1) == height) {
        return;
    }

    surface = qemu_create_displaysurface(width, height);
    dpy_gfx_replace_surface(s, surface);
}

DisplaySurface *qemu_console_surface(QemuConsole *console)
{
    switch (console->scanout.kind) {
    case SCANOUT_SURFACE:
        return console->surface;
    default:
        return NULL;
    }
}

PixelFormat qemu_default_pixelformat(int bpp)
{
    pixman_format_code_t fmt = qemu_default_pixman_format(bpp, true);
    PixelFormat pf = qemu_pixelformat_from_pixman(fmt);
    return pf;
}

static QemuDisplay *dpys[DISPLAY_TYPE__MAX];

void qemu_display_register(QemuDisplay *ui)
{
    assert(ui->type < DISPLAY_TYPE__MAX);
    dpys[ui->type] = ui;
}

bool qemu_display_find_default(DisplayOptions *opts)
{
    static DisplayType prio[] = {
#if defined(CONFIG_GTK)
        DISPLAY_TYPE_GTK,
#endif
#if defined(CONFIG_SDL)
        DISPLAY_TYPE_SDL,
#endif
#if defined(CONFIG_COCOA)
        DISPLAY_TYPE_COCOA
#endif
    };
    int i;

    for (i = 0; i < (int)ARRAY_SIZE(prio); i++) {
        if (dpys[prio[i]] == NULL) {
            Error *local_err = NULL;
            int rv = ui_module_load(DisplayType_str(prio[i]), &local_err);
            if (rv < 0) {
                error_report_err(local_err);
            }
        }
        if (dpys[prio[i]] == NULL) {
            continue;
        }
        opts->type = prio[i];
        return true;
    }
    return false;
}

void qemu_display_early_init(DisplayOptions *opts)
{
    assert(opts->type < DISPLAY_TYPE__MAX);
    if (opts->type == DISPLAY_TYPE_NONE) {
        return;
    }
    if (dpys[opts->type] == NULL) {
        Error *local_err = NULL;
        int rv = ui_module_load(DisplayType_str(opts->type), &local_err);
        if (rv < 0) {
            error_report_err(local_err);
        }
    }
    if (dpys[opts->type] == NULL) {
        error_report("Display '%s' is not available.",
                     DisplayType_str(opts->type));
        exit(1);
    }
    if (dpys[opts->type]->early_init) {
        dpys[opts->type]->early_init(opts);
    }
}

void qemu_display_init(DisplayState *ds, DisplayOptions *opts)
{
    assert(opts->type < DISPLAY_TYPE__MAX);
    if (opts->type == DISPLAY_TYPE_NONE) {
        return;
    }
    assert(dpys[opts->type] != NULL);
    dpys[opts->type]->init(ds, opts);
}

void qemu_display_help(void)
{
    int idx;

    printf("Available display backend types:\n");
    printf("none\n");
    for (idx = DISPLAY_TYPE_NONE; idx < DISPLAY_TYPE__MAX; idx++) {
        if (!dpys[idx]) {
            Error *local_err = NULL;
            int rv = ui_module_load(DisplayType_str(idx), &local_err);
            if (rv < 0) {
                error_report_err(local_err);
            }
        }
        if (dpys[idx]) {
            printf("%s\n",  DisplayType_str(dpys[idx]->type));
        }
    }
}

void qemu_chr_parse_vc(QemuOpts *opts, ChardevBackend *backend, Error **errp)
{
    int val;
    ChardevVC *vc;

    backend->type = CHARDEV_BACKEND_KIND_VC;
    vc = backend->u.vc.data = g_new0(ChardevVC, 1);
    qemu_chr_parse_common(opts, qapi_ChardevVC_base(vc));

    val = qemu_opt_get_number(opts, "width", 0);
    if (val != 0) {
        vc->has_width = true;
        vc->width = val;
    }

    val = qemu_opt_get_number(opts, "height", 0);
    if (val != 0) {
        vc->has_height = true;
        vc->height = val;
    }

    val = qemu_opt_get_number(opts, "cols", 0);
    if (val != 0) {
        vc->has_cols = true;
        vc->cols = val;
    }

    val = qemu_opt_get_number(opts, "rows", 0);
    if (val != 0) {
        vc->has_rows = true;
        vc->rows = val;
    }
}

static const TypeInfo qemu_console_info = {
    .name = TYPE_QEMU_CONSOLE,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(QemuConsole),
    .class_size = sizeof(QemuConsoleClass),
};

static void char_vc_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->parse = qemu_chr_parse_vc;
    cc->open = vc_chr_open;
    cc->chr_write = vc_chr_write;
    cc->chr_accept_input = vc_chr_accept_input;
    cc->chr_set_echo = vc_chr_set_echo;
}

static const TypeInfo char_vc_type_info = {
    .name = TYPE_CHARDEV_VC,
    .parent = TYPE_CHARDEV,
    .instance_size = sizeof(VCChardev),
    .class_init = char_vc_class_init,
};

void qemu_console_early_init(void)
{
    /* set the default vc driver */
    if (!object_class_by_name(TYPE_CHARDEV_VC)) {
        type_register(&char_vc_type_info);
    }
}

static void register_types(void)
{
    type_register_static(&qemu_console_info);
}

type_init(register_types);
