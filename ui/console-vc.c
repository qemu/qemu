/*
 * SPDX-License-Identifier: MIT
 * QEMU VC
 */
#include "qemu/osdep.h"

#include "chardev/char.h"
#include "qapi/error.h"
#include "qemu/fifo8.h"
#include "qemu/option.h"
#include "ui/console.h"

#include "trace.h"
#include "console-priv.h"

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

#define TEXT_ATTRIBUTES_DEFAULT ((TextAttributes) { \
    .fgcol = QEMU_COLOR_WHITE,                      \
    .bgcol = QEMU_COLOR_BLACK                       \
})

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

typedef struct QemuTextConsole {
    QemuConsole parent;

    int width;
    int height;
    int total_height;
    int backscroll_height;
    int x, y;
    int y_displayed;
    int y_base;
    TextCell *cells;
    int text_x[2], text_y[2], cursor_invalidate;
    int echo;

    int update_x0;
    int update_y0;
    int update_x1;
    int update_y1;

    Chardev *chr;
    /* fifo for key pressed */
    Fifo8 out_fifo;
} QemuTextConsole;

typedef QemuConsoleClass QemuTextConsoleClass;

OBJECT_DEFINE_TYPE(QemuTextConsole, qemu_text_console, QEMU_TEXT_CONSOLE, QEMU_CONSOLE)

typedef struct QemuFixedTextConsole {
    QemuTextConsole parent;
} QemuFixedTextConsole;

typedef QemuTextConsoleClass QemuFixedTextConsoleClass;

OBJECT_DEFINE_TYPE(QemuFixedTextConsole, qemu_fixed_text_console, QEMU_FIXED_TEXT_CONSOLE, QEMU_TEXT_CONSOLE)

struct VCChardev {
    Chardev parent;
    QemuTextConsole *console;

    enum TTYState state;
    int esc_params[MAX_ESC_PARAMS];
    int nb_esc_params;
    TextAttributes t_attrib; /* currently active text attributes */
    int x_saved, y_saved;
};
typedef struct VCChardev VCChardev;

static const pixman_color_t color_table_rgb[2][8] = {
    {   /* dark */
        [QEMU_COLOR_BLACK]   = QEMU_PIXMAN_COLOR_BLACK,
        [QEMU_COLOR_BLUE]    = QEMU_PIXMAN_COLOR(0x00, 0x00, 0xaa),  /* blue */
        [QEMU_COLOR_GREEN]   = QEMU_PIXMAN_COLOR(0x00, 0xaa, 0x00),  /* green */
        [QEMU_COLOR_CYAN]    = QEMU_PIXMAN_COLOR(0x00, 0xaa, 0xaa),  /* cyan */
        [QEMU_COLOR_RED]     = QEMU_PIXMAN_COLOR(0xaa, 0x00, 0x00),  /* red */
        [QEMU_COLOR_MAGENTA] = QEMU_PIXMAN_COLOR(0xaa, 0x00, 0xaa),  /* magenta */
        [QEMU_COLOR_YELLOW]  = QEMU_PIXMAN_COLOR(0xaa, 0xaa, 0x00),  /* yellow */
        [QEMU_COLOR_WHITE]   = QEMU_PIXMAN_COLOR_GRAY,
    },
    {   /* bright */
        [QEMU_COLOR_BLACK]   = QEMU_PIXMAN_COLOR_BLACK,
        [QEMU_COLOR_BLUE]    = QEMU_PIXMAN_COLOR(0x00, 0x00, 0xff),  /* blue */
        [QEMU_COLOR_GREEN]   = QEMU_PIXMAN_COLOR(0x00, 0xff, 0x00),  /* green */
        [QEMU_COLOR_CYAN]    = QEMU_PIXMAN_COLOR(0x00, 0xff, 0xff),  /* cyan */
        [QEMU_COLOR_RED]     = QEMU_PIXMAN_COLOR(0xff, 0x00, 0x00),  /* red */
        [QEMU_COLOR_MAGENTA] = QEMU_PIXMAN_COLOR(0xff, 0x00, 0xff),  /* magenta */
        [QEMU_COLOR_YELLOW]  = QEMU_PIXMAN_COLOR(0xff, 0xff, 0x00),  /* yellow */
        [QEMU_COLOR_WHITE]   = QEMU_PIXMAN_COLOR(0xff, 0xff, 0xff),  /* white */
    }
};

static bool cursor_visible_phase;
static QEMUTimer *cursor_timer;

const char *
qemu_text_console_get_label(QemuTextConsole *c)
{
    return c->chr ? c->chr->label : NULL;
}

static void qemu_console_fill_rect(QemuConsole *con, int posx, int posy,
                                   int width, int height, pixman_color_t color)
{
    DisplaySurface *surface = qemu_console_surface(con);
    pixman_rectangle16_t rect = {
        .x = posx, .y = posy, .width = width, .height = height
    };

    assert(surface);
    pixman_image_fill_rectangles(PIXMAN_OP_SRC, surface->image,
                                 &color, 1, &rect);
}

/* copy from (xs, ys) to (xd, yd) a rectangle of size (w, h) */
static void qemu_console_bitblt(QemuConsole *con,
                                int xs, int ys, int xd, int yd, int w, int h)
{
    DisplaySurface *surface = qemu_console_surface(con);

    assert(surface);
    pixman_image_composite(PIXMAN_OP_SRC,
                           surface->image, NULL, surface->image,
                           xs, ys, 0, 0, xd, yd, w, h);
}

static void vga_putcharxy(QemuConsole *s, int x, int y, int ch,
                          TextAttributes *t_attrib)
{
    static pixman_image_t *glyphs[256];
    DisplaySurface *surface = qemu_console_surface(s);
    pixman_color_t fgcol, bgcol;

    assert(surface);
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

static void invalidate_xy(QemuTextConsole *s, int x, int y)
{
    if (!qemu_console_is_visible(QEMU_CONSOLE(s))) {
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

static void console_show_cursor(QemuTextConsole *s, int show)
{
    TextCell *c;
    int y, y1;
    int x = s->x;

    s->cursor_invalidate = 1;

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
            TextAttributes t_attrib = TEXT_ATTRIBUTES_DEFAULT;
            t_attrib.invers = !(t_attrib.invers); /* invert fg and bg */
            vga_putcharxy(QEMU_CONSOLE(s), x, y, c->ch, &t_attrib);
        } else {
            vga_putcharxy(QEMU_CONSOLE(s), x, y, c->ch, &(c->t_attrib));
        }
        invalidate_xy(s, x, y);
    }
}

static void console_refresh(QemuTextConsole *s)
{
    DisplaySurface *surface = qemu_console_surface(QEMU_CONSOLE(s));
    TextCell *c;
    int x, y, y1;

    assert(surface);
    s->text_x[0] = 0;
    s->text_y[0] = 0;
    s->text_x[1] = s->width - 1;
    s->text_y[1] = s->height - 1;
    s->cursor_invalidate = 1;

    qemu_console_fill_rect(QEMU_CONSOLE(s), 0, 0, surface_width(surface), surface_height(surface),
                           color_table_rgb[0][QEMU_COLOR_BLACK]);
    y1 = s->y_displayed;
    for (y = 0; y < s->height; y++) {
        c = s->cells + y1 * s->width;
        for (x = 0; x < s->width; x++) {
            vga_putcharxy(QEMU_CONSOLE(s), x, y, c->ch,
                          &(c->t_attrib));
            c++;
        }
        if (++y1 == s->total_height) {
            y1 = 0;
        }
    }
    console_show_cursor(s, 1);
    dpy_gfx_update(QEMU_CONSOLE(s), 0, 0,
                   surface_width(surface), surface_height(surface));
}

static void console_scroll(QemuTextConsole *s, int ydelta)
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

static void kbd_send_chars(QemuTextConsole *s)
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
void qemu_text_console_handle_keysym(QemuTextConsole *s, int keysym)
{
    uint8_t buf[16], *q;
    int c;
    uint32_t num_free;

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
            qemu_chr_write(s->chr, (uint8_t *)"\r", 1, true);
            *q++ = '\n';
        } else {
            *q++ = keysym;
        }
        if (s->echo) {
            qemu_chr_write(s->chr, buf, q - buf, true);
        }
        num_free = fifo8_num_free(&s->out_fifo);
        fifo8_push_all(&s->out_fifo, buf, MIN(num_free, q - buf));
        kbd_send_chars(s);
        break;
    }
}

static void text_console_update(void *opaque, console_ch_t *chardata)
{
    QemuTextConsole *s = QEMU_TEXT_CONSOLE(opaque);
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
        dpy_text_update(QEMU_CONSOLE(s), s->text_x[0], s->text_y[0],
                        s->text_x[1] - s->text_x[0], i - s->text_y[0]);
        s->text_x[0] = s->width;
        s->text_y[0] = s->height;
        s->text_x[1] = 0;
        s->text_y[1] = 0;
    }
    if (s->cursor_invalidate) {
        dpy_text_cursor(QEMU_CONSOLE(s), s->x, s->y);
        s->cursor_invalidate = 0;
    }
}

static void text_console_resize(QemuTextConsole *t)
{
    QemuConsole *s = QEMU_CONSOLE(t);
    TextCell *cells, *c, *c1;
    int w1, x, y, last_width, w, h;

    assert(s->scanout.kind == SCANOUT_SURFACE);

    w = surface_width(s->surface) / FONT_WIDTH;
    h = surface_height(s->surface) / FONT_HEIGHT;
    if (w == t->width && h == t->height) {
        return;
    }

    last_width = t->width;
    t->width = w;
    t->height = h;

    w1 = MIN(t->width, last_width);

    cells = g_new(TextCell, t->width * t->total_height + 1);
    for (y = 0; y < t->total_height; y++) {
        c = &cells[y * t->width];
        if (w1 > 0) {
            c1 = &t->cells[y * last_width];
            for (x = 0; x < w1; x++) {
                *c++ = *c1++;
            }
        }
        for (x = w1; x < t->width; x++) {
            c->ch = ' ';
            c->t_attrib = TEXT_ATTRIBUTES_DEFAULT;
            c++;
        }
    }
    g_free(t->cells);
    t->cells = cells;
}

static void vc_put_lf(VCChardev *vc)
{
    QemuTextConsole *s = vc->console;
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
            c->t_attrib = TEXT_ATTRIBUTES_DEFAULT;
            c++;
        }
        if (s->y_displayed == s->y_base) {
            s->text_x[0] = 0;
            s->text_y[0] = 0;
            s->text_x[1] = s->width - 1;
            s->text_y[1] = s->height - 1;

            qemu_console_bitblt(QEMU_CONSOLE(s), 0, FONT_HEIGHT, 0, 0,
                                s->width * FONT_WIDTH,
                                (s->height - 1) * FONT_HEIGHT);
            qemu_console_fill_rect(QEMU_CONSOLE(s), 0, (s->height - 1) * FONT_HEIGHT,
                                   s->width * FONT_WIDTH, FONT_HEIGHT,
                                   color_table_rgb[0][TEXT_ATTRIBUTES_DEFAULT.bgcol]);
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
static void vc_handle_escape(VCChardev *vc)
{
    int i;

    for (i = 0; i < vc->nb_esc_params; i++) {
        switch (vc->esc_params[i]) {
            case 0: /* reset all console attributes to default */
                vc->t_attrib = TEXT_ATTRIBUTES_DEFAULT;
                break;
            case 1:
                vc->t_attrib.bold = 1;
                break;
            case 4:
                vc->t_attrib.uline = 1;
                break;
            case 5:
                vc->t_attrib.blink = 1;
                break;
            case 7:
                vc->t_attrib.invers = 1;
                break;
            case 8:
                vc->t_attrib.unvisible = 1;
                break;
            case 22:
                vc->t_attrib.bold = 0;
                break;
            case 24:
                vc->t_attrib.uline = 0;
                break;
            case 25:
                vc->t_attrib.blink = 0;
                break;
            case 27:
                vc->t_attrib.invers = 0;
                break;
            case 28:
                vc->t_attrib.unvisible = 0;
                break;
            /* set foreground color */
            case 30:
                vc->t_attrib.fgcol = QEMU_COLOR_BLACK;
                break;
            case 31:
                vc->t_attrib.fgcol = QEMU_COLOR_RED;
                break;
            case 32:
                vc->t_attrib.fgcol = QEMU_COLOR_GREEN;
                break;
            case 33:
                vc->t_attrib.fgcol = QEMU_COLOR_YELLOW;
                break;
            case 34:
                vc->t_attrib.fgcol = QEMU_COLOR_BLUE;
                break;
            case 35:
                vc->t_attrib.fgcol = QEMU_COLOR_MAGENTA;
                break;
            case 36:
                vc->t_attrib.fgcol = QEMU_COLOR_CYAN;
                break;
            case 37:
                vc->t_attrib.fgcol = QEMU_COLOR_WHITE;
                break;
            /* set background color */
            case 40:
                vc->t_attrib.bgcol = QEMU_COLOR_BLACK;
                break;
            case 41:
                vc->t_attrib.bgcol = QEMU_COLOR_RED;
                break;
            case 42:
                vc->t_attrib.bgcol = QEMU_COLOR_GREEN;
                break;
            case 43:
                vc->t_attrib.bgcol = QEMU_COLOR_YELLOW;
                break;
            case 44:
                vc->t_attrib.bgcol = QEMU_COLOR_BLUE;
                break;
            case 45:
                vc->t_attrib.bgcol = QEMU_COLOR_MAGENTA;
                break;
            case 46:
                vc->t_attrib.bgcol = QEMU_COLOR_CYAN;
                break;
            case 47:
                vc->t_attrib.bgcol = QEMU_COLOR_WHITE;
                break;
        }
    }
}

static void vc_update_xy(VCChardev *vc, int x, int y)
{
    QemuTextConsole *s = vc->console;
    TextCell *c;
    int y1, y2;

    s->text_x[0] = MIN(s->text_x[0], x);
    s->text_x[1] = MAX(s->text_x[1], x);
    s->text_y[0] = MIN(s->text_y[0], y);
    s->text_y[1] = MAX(s->text_y[1], y);

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
        vga_putcharxy(QEMU_CONSOLE(s), x, y2, c->ch,
                      &(c->t_attrib));
        invalidate_xy(s, x, y2);
    }
}

static void vc_clear_xy(VCChardev *vc, int x, int y)
{
    QemuTextConsole *s = vc->console;
    int y1 = (s->y_base + y) % s->total_height;
    if (x >= s->width) {
        x = s->width - 1;
    }
    TextCell *c = &s->cells[y1 * s->width + x];
    c->ch = ' ';
    c->t_attrib = TEXT_ATTRIBUTES_DEFAULT;
    vc_update_xy(vc, x, y);
}

static void vc_put_one(VCChardev *vc, int ch)
{
    QemuTextConsole *s = vc->console;
    TextCell *c;
    int y1;
    if (s->x >= s->width) {
        /* line wrap */
        s->x = 0;
        vc_put_lf(vc);
    }
    y1 = (s->y_base + s->y) % s->total_height;
    c = &s->cells[y1 * s->width + s->x];
    c->ch = ch;
    c->t_attrib = vc->t_attrib;
    vc_update_xy(vc, s->x, s->y);
    s->x++;
}

static void vc_respond_str(VCChardev *vc, const char *buf)
{
    while (*buf) {
        vc_put_one(vc, *buf);
        buf++;
    }
}

/* set cursor, checking bounds */
static void vc_set_cursor(VCChardev *vc, int x, int y)
{
    QemuTextConsole *s = vc->console;

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

static void vc_putchar(VCChardev *vc, int ch)
{
    QemuTextConsole *s = vc->console;
    int i;
    int x, y;
    char response[40];

    switch(vc->state) {
    case TTY_STATE_NORM:
        switch(ch) {
        case '\r':  /* carriage return */
            s->x = 0;
            break;
        case '\n':  /* newline */
            vc_put_lf(vc);
            break;
        case '\b':  /* backspace */
            if (s->x > 0)
                s->x--;
            break;
        case '\t':  /* tabspace */
            if (s->x + (8 - (s->x % 8)) > s->width) {
                s->x = 0;
                vc_put_lf(vc);
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
            vc->state = TTY_STATE_ESC;
            break;
        default:
            vc_put_one(vc, ch);
            break;
        }
        break;
    case TTY_STATE_ESC: /* check if it is a terminal escape sequence */
        if (ch == '[') {
            for(i=0;i<MAX_ESC_PARAMS;i++)
                vc->esc_params[i] = 0;
            vc->nb_esc_params = 0;
            vc->state = TTY_STATE_CSI;
        } else {
            vc->state = TTY_STATE_NORM;
        }
        break;
    case TTY_STATE_CSI: /* handle escape sequence parameters */
        if (ch >= '0' && ch <= '9') {
            if (vc->nb_esc_params < MAX_ESC_PARAMS) {
                int *param = &vc->esc_params[vc->nb_esc_params];
                int digit = (ch - '0');

                *param = (*param <= (INT_MAX - digit) / 10) ?
                         *param * 10 + digit : INT_MAX;
            }
        } else {
            if (vc->nb_esc_params < MAX_ESC_PARAMS)
                vc->nb_esc_params++;
            if (ch == ';' || ch == '?') {
                break;
            }
            trace_console_putchar_csi(vc->esc_params[0], vc->esc_params[1],
                                      ch, vc->nb_esc_params);
            vc->state = TTY_STATE_NORM;
            switch(ch) {
            case 'A':
                /* move cursor up */
                if (vc->esc_params[0] == 0) {
                    vc->esc_params[0] = 1;
                }
                vc_set_cursor(vc, s->x, s->y - vc->esc_params[0]);
                break;
            case 'B':
                /* move cursor down */
                if (vc->esc_params[0] == 0) {
                    vc->esc_params[0] = 1;
                }
                vc_set_cursor(vc, s->x, s->y + vc->esc_params[0]);
                break;
            case 'C':
                /* move cursor right */
                if (vc->esc_params[0] == 0) {
                    vc->esc_params[0] = 1;
                }
                vc_set_cursor(vc, s->x + vc->esc_params[0], s->y);
                break;
            case 'D':
                /* move cursor left */
                if (vc->esc_params[0] == 0) {
                    vc->esc_params[0] = 1;
                }
                vc_set_cursor(vc, s->x - vc->esc_params[0], s->y);
                break;
            case 'G':
                /* move cursor to column */
                vc_set_cursor(vc, vc->esc_params[0] - 1, s->y);
                break;
            case 'f':
            case 'H':
                /* move cursor to row, column */
                vc_set_cursor(vc, vc->esc_params[1] - 1, vc->esc_params[0] - 1);
                break;
            case 'J':
                switch (vc->esc_params[0]) {
                case 0:
                    /* clear to end of screen */
                    for (y = s->y; y < s->height; y++) {
                        for (x = 0; x < s->width; x++) {
                            if (y == s->y && x < s->x) {
                                continue;
                            }
                            vc_clear_xy(vc, x, y);
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
                            vc_clear_xy(vc, x, y);
                        }
                    }
                    break;
                case 2:
                    /* clear entire screen */
                    for (y = 0; y <= s->height; y++) {
                        for (x = 0; x < s->width; x++) {
                            vc_clear_xy(vc, x, y);
                        }
                    }
                    break;
                }
                break;
            case 'K':
                switch (vc->esc_params[0]) {
                case 0:
                    /* clear to eol */
                    for(x = s->x; x < s->width; x++) {
                        vc_clear_xy(vc, x, s->y);
                    }
                    break;
                case 1:
                    /* clear from beginning of line */
                    for (x = 0; x <= s->x && x < s->width; x++) {
                        vc_clear_xy(vc, x, s->y);
                    }
                    break;
                case 2:
                    /* clear entire line */
                    for(x = 0; x < s->width; x++) {
                        vc_clear_xy(vc, x, s->y);
                    }
                    break;
                }
                break;
            case 'm':
                vc_handle_escape(vc);
                break;
            case 'n':
                switch (vc->esc_params[0]) {
                case 5:
                    /* report console status (always succeed)*/
                    vc_respond_str(vc, "\033[0n");
                    break;
                case 6:
                    /* report cursor position */
                    sprintf(response, "\033[%d;%dR",
                           (s->y_base + s->y) % s->total_height + 1,
                            s->x + 1);
                    vc_respond_str(vc, response);
                    break;
                }
                break;
            case 's':
                /* save cursor position */
                vc->x_saved = s->x;
                vc->y_saved = s->y;
                break;
            case 'u':
                /* restore cursor position */
                s->x = vc->x_saved;
                s->y = vc->y_saved;
                break;
            default:
                trace_console_putchar_unhandled(ch);
                break;
            }
            break;
        }
    }
}

#define TYPE_CHARDEV_VC "chardev-vc"
DECLARE_INSTANCE_CHECKER(VCChardev, VC_CHARDEV,
                         TYPE_CHARDEV_VC)

static int vc_chr_write(Chardev *chr, const uint8_t *buf, int len)
{
    VCChardev *drv = VC_CHARDEV(chr);
    QemuTextConsole *s = drv->console;
    int i;

    s->update_x0 = s->width * FONT_WIDTH;
    s->update_y0 = s->height * FONT_HEIGHT;
    s->update_x1 = 0;
    s->update_y1 = 0;
    console_show_cursor(s, 0);
    for(i = 0; i < len; i++) {
        vc_putchar(drv, buf[i]);
    }
    console_show_cursor(s, 1);
    if (s->update_x0 < s->update_x1) {
        dpy_gfx_update(QEMU_CONSOLE(s), s->update_x0, s->update_y0,
                       s->update_x1 - s->update_x0,
                       s->update_y1 - s->update_y0);
    }
    return len;
}

void qemu_text_console_update_cursor(void)
{
    cursor_visible_phase = !cursor_visible_phase;

    if (qemu_invalidate_text_consoles()) {
        timer_mod(cursor_timer,
                  qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + CONSOLE_CURSOR_PERIOD / 2);
    }
}

static void
cursor_timer_cb(void *opaque)
{
    qemu_text_console_update_cursor();
}

static void text_console_invalidate(void *opaque)
{
    QemuTextConsole *s = QEMU_TEXT_CONSOLE(opaque);

    if (!QEMU_IS_FIXED_TEXT_CONSOLE(s)) {
        text_console_resize(QEMU_TEXT_CONSOLE(s));
    }
    console_refresh(s);
}

static void
qemu_text_console_finalize(Object *obj)
{
}

static void
qemu_text_console_class_init(ObjectClass *oc, void *data)
{
    if (!cursor_timer) {
        cursor_timer = timer_new_ms(QEMU_CLOCK_REALTIME, cursor_timer_cb, NULL);
    }
}

static const GraphicHwOps text_console_ops = {
    .invalidate  = text_console_invalidate,
    .text_update = text_console_update,
};

static void
qemu_text_console_init(Object *obj)
{
    QemuTextConsole *c = QEMU_TEXT_CONSOLE(obj);

    fifo8_create(&c->out_fifo, 16);
    c->total_height = DEFAULT_BACKSCROLL;
    QEMU_CONSOLE(c)->hw_ops = &text_console_ops;
    QEMU_CONSOLE(c)->hw = c;
}

static void
qemu_fixed_text_console_finalize(Object *obj)
{
}

static void
qemu_fixed_text_console_class_init(ObjectClass *oc, void *data)
{
}

static void
qemu_fixed_text_console_init(Object *obj)
{
}

static void vc_chr_accept_input(Chardev *chr)
{
    VCChardev *drv = VC_CHARDEV(chr);

    kbd_send_chars(drv->console);
}

static void vc_chr_set_echo(Chardev *chr, bool echo)
{
    VCChardev *drv = VC_CHARDEV(chr);

    drv->console->echo = echo;
}

void qemu_text_console_select(QemuTextConsole *c)
{
    dpy_text_resize(QEMU_CONSOLE(c), c->width, c->height);
    qemu_text_console_update_cursor();
}

static void vc_chr_open(Chardev *chr,
                        ChardevBackend *backend,
                        bool *be_opened,
                        Error **errp)
{
    ChardevVC *vc = backend->u.vc.data;
    VCChardev *drv = VC_CHARDEV(chr);
    QemuTextConsole *s;
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
        s = QEMU_TEXT_CONSOLE(object_new(TYPE_QEMU_TEXT_CONSOLE));
        width = qemu_console_get_width(NULL, 80 * FONT_WIDTH);
        height = qemu_console_get_height(NULL, 24 * FONT_HEIGHT);
    } else {
        s = QEMU_TEXT_CONSOLE(object_new(TYPE_QEMU_FIXED_TEXT_CONSOLE));
    }

    dpy_gfx_replace_surface(QEMU_CONSOLE(s), qemu_create_displaysurface(width, height));

    s->chr = chr;
    drv->console = s;

    /* set current text attributes to default */
    drv->t_attrib = TEXT_ATTRIBUTES_DEFAULT;
    text_console_resize(s);

    if (chr->label) {
        char *msg;

        drv->t_attrib.bgcol = QEMU_COLOR_BLUE;
        msg = g_strdup_printf("%s console\r\n", chr->label);
        qemu_chr_write(chr, (uint8_t *)msg, strlen(msg), true);
        g_free(msg);
        drv->t_attrib = TEXT_ATTRIBUTES_DEFAULT;
    }

    *be_opened = true;
}

static void vc_chr_parse(QemuOpts *opts, ChardevBackend *backend, Error **errp)
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

static void char_vc_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->parse = vc_chr_parse;
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
