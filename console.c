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
#include "qemu-common.h"
#include "console.h"
#include "qemu-timer.h"

//#define DEBUG_CONSOLE
#define DEFAULT_BACKSCROLL 512
#define MAX_CONSOLES 12
#define DEFAULT_MONITOR_SIZE "800x600"

#define QEMU_RGBA(r, g, b, a) (((a) << 24) | ((r) << 16) | ((g) << 8) | (b))
#define QEMU_RGB(r, g, b) QEMU_RGBA(r, g, b, 0xff)

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

typedef struct QEMUFIFO {
    uint8_t *buf;
    int buf_size;
    int count, wptr, rptr;
} QEMUFIFO;

static int qemu_fifo_write(QEMUFIFO *f, const uint8_t *buf, int len1)
{
    int l, len;

    l = f->buf_size - f->count;
    if (len1 > l)
        len1 = l;
    len = len1;
    while (len > 0) {
        l = f->buf_size - f->wptr;
        if (l > len)
            l = len;
        memcpy(f->buf + f->wptr, buf, l);
        f->wptr += l;
        if (f->wptr >= f->buf_size)
            f->wptr = 0;
        buf += l;
        len -= l;
    }
    f->count += len1;
    return len1;
}

static int qemu_fifo_read(QEMUFIFO *f, uint8_t *buf, int len1)
{
    int l, len;

    if (len1 > f->count)
        len1 = f->count;
    len = len1;
    while (len > 0) {
        l = f->buf_size - f->rptr;
        if (l > len)
            l = len;
        memcpy(buf, f->buf + f->rptr, l);
        f->rptr += l;
        if (f->rptr >= f->buf_size)
            f->rptr = 0;
        buf += l;
        len -= l;
    }
    f->count -= len1;
    return len1;
}

typedef enum {
    GRAPHIC_CONSOLE,
    TEXT_CONSOLE,
    TEXT_CONSOLE_FIXED_SIZE
} console_type_t;

/* ??? This is mis-named.
   It is used for both text and graphical consoles.  */
struct TextConsole {
    console_type_t console_type;
    DisplayState *ds;
    /* Graphic console state.  */
    vga_hw_update_ptr hw_update;
    vga_hw_invalidate_ptr hw_invalidate;
    vga_hw_screen_dump_ptr hw_screen_dump;
    vga_hw_text_update_ptr hw_text_update;
    void *hw;

    int g_width, g_height;
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

    enum TTYState state;
    int esc_params[MAX_ESC_PARAMS];
    int nb_esc_params;

    CharDriverState *chr;
    /* fifo for key pressed */
    QEMUFIFO out_fifo;
    uint8_t out_fifo_buf[16];
    QEMUTimer *kbd_timer;
};

static TextConsole *active_console;
static TextConsole *consoles[MAX_CONSOLES];
static int nb_consoles = 0;

void vga_hw_update(void)
{
    if (active_console && active_console->hw_update)
        active_console->hw_update(active_console->hw);
}

void vga_hw_invalidate(void)
{
    if (active_console->hw_invalidate)
        active_console->hw_invalidate(active_console->hw);
}

void vga_hw_screen_dump(const char *filename)
{
    TextConsole *previous_active_console;

    previous_active_console = active_console;
    active_console = consoles[0];
    /* There is currently no way of specifying which screen we want to dump,
       so always dump the first one.  */
    if (consoles[0]->hw_screen_dump)
        consoles[0]->hw_screen_dump(consoles[0]->hw, filename);
    active_console = previous_active_console;
}

void vga_hw_text_update(console_ch_t *chardata)
{
    if (active_console && active_console->hw_text_update)
        active_console->hw_text_update(active_console->hw, chardata);
}

/* convert a RGBA color to a color index usable in graphic primitives */
static unsigned int vga_get_color(DisplayState *ds, unsigned int rgba)
{
    unsigned int r, g, b, color;

    switch(ds_get_bits_per_pixel(ds)) {
#if 0
    case 8:
        r = (rgba >> 16) & 0xff;
        g = (rgba >> 8) & 0xff;
        b = (rgba) & 0xff;
        color = (rgb_to_index[r] * 6 * 6) +
            (rgb_to_index[g] * 6) +
            (rgb_to_index[b]);
        break;
#endif
    case 15:
        r = (rgba >> 16) & 0xff;
        g = (rgba >> 8) & 0xff;
        b = (rgba) & 0xff;
        color = ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
        break;
    case 16:
        r = (rgba >> 16) & 0xff;
        g = (rgba >> 8) & 0xff;
        b = (rgba) & 0xff;
        color = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        break;
    case 32:
    default:
        color = rgba;
        break;
    }
    return color;
}

static void vga_fill_rect (DisplayState *ds,
                           int posx, int posy, int width, int height, uint32_t color)
{
    uint8_t *d, *d1;
    int x, y, bpp;

    bpp = (ds_get_bits_per_pixel(ds) + 7) >> 3;
    d1 = ds_get_data(ds) +
        ds_get_linesize(ds) * posy + bpp * posx;
    for (y = 0; y < height; y++) {
        d = d1;
        switch(bpp) {
        case 1:
            for (x = 0; x < width; x++) {
                *((uint8_t *)d) = color;
                d++;
            }
            break;
        case 2:
            for (x = 0; x < width; x++) {
                *((uint16_t *)d) = color;
                d += 2;
            }
            break;
        case 4:
            for (x = 0; x < width; x++) {
                *((uint32_t *)d) = color;
                d += 4;
            }
            break;
        }
        d1 += ds_get_linesize(ds);
    }
}

/* copy from (xs, ys) to (xd, yd) a rectangle of size (w, h) */
static void vga_bitblt(DisplayState *ds, int xs, int ys, int xd, int yd, int w, int h)
{
    const uint8_t *s;
    uint8_t *d;
    int wb, y, bpp;

    bpp = (ds_get_bits_per_pixel(ds) + 7) >> 3;
    wb = w * bpp;
    if (yd <= ys) {
        s = ds_get_data(ds) +
            ds_get_linesize(ds) * ys + bpp * xs;
        d = ds_get_data(ds) +
            ds_get_linesize(ds) * yd + bpp * xd;
        for (y = 0; y < h; y++) {
            memmove(d, s, wb);
            d += ds_get_linesize(ds);
            s += ds_get_linesize(ds);
        }
    } else {
        s = ds_get_data(ds) +
            ds_get_linesize(ds) * (ys + h - 1) + bpp * xs;
        d = ds_get_data(ds) +
            ds_get_linesize(ds) * (yd + h - 1) + bpp * xd;
       for (y = 0; y < h; y++) {
            memmove(d, s, wb);
            d -= ds_get_linesize(ds);
            s -= ds_get_linesize(ds);
        }
    }
}

/***********************************************************/
/* basic char display */

#define FONT_HEIGHT 16
#define FONT_WIDTH 8

#include "vgafont.h"

#define cbswap_32(__x) \
((uint32_t)( \
		(((uint32_t)(__x) & (uint32_t)0x000000ffUL) << 24) | \
		(((uint32_t)(__x) & (uint32_t)0x0000ff00UL) <<  8) | \
		(((uint32_t)(__x) & (uint32_t)0x00ff0000UL) >>  8) | \
		(((uint32_t)(__x) & (uint32_t)0xff000000UL) >> 24) ))

#ifdef WORDS_BIGENDIAN
#define PAT(x) x
#else
#define PAT(x) cbswap_32(x)
#endif

static const uint32_t dmask16[16] = {
    PAT(0x00000000),
    PAT(0x000000ff),
    PAT(0x0000ff00),
    PAT(0x0000ffff),
    PAT(0x00ff0000),
    PAT(0x00ff00ff),
    PAT(0x00ffff00),
    PAT(0x00ffffff),
    PAT(0xff000000),
    PAT(0xff0000ff),
    PAT(0xff00ff00),
    PAT(0xff00ffff),
    PAT(0xffff0000),
    PAT(0xffff00ff),
    PAT(0xffffff00),
    PAT(0xffffffff),
};

static const uint32_t dmask4[4] = {
    PAT(0x00000000),
    PAT(0x0000ffff),
    PAT(0xffff0000),
    PAT(0xffffffff),
};

static uint32_t color_table[2][8];

enum color_names {
    COLOR_BLACK   = 0,
    COLOR_RED     = 1,
    COLOR_GREEN   = 2,
    COLOR_YELLOW  = 3,
    COLOR_BLUE    = 4,
    COLOR_MAGENTA = 5,
    COLOR_CYAN    = 6,
    COLOR_WHITE   = 7
};

static const uint32_t color_table_rgb[2][8] = {
    {   /* dark */
        QEMU_RGB(0x00, 0x00, 0x00),  /* black */
        QEMU_RGB(0xaa, 0x00, 0x00),  /* red */
        QEMU_RGB(0x00, 0xaa, 0x00),  /* green */
        QEMU_RGB(0xaa, 0xaa, 0x00),  /* yellow */
        QEMU_RGB(0x00, 0x00, 0xaa),  /* blue */
        QEMU_RGB(0xaa, 0x00, 0xaa),  /* magenta */
        QEMU_RGB(0x00, 0xaa, 0xaa),  /* cyan */
        QEMU_RGB(0xaa, 0xaa, 0xaa),  /* white */
    },
    {   /* bright */
        QEMU_RGB(0x00, 0x00, 0x00),  /* black */
        QEMU_RGB(0xff, 0x00, 0x00),  /* red */
        QEMU_RGB(0x00, 0xff, 0x00),  /* green */
        QEMU_RGB(0xff, 0xff, 0x00),  /* yellow */
        QEMU_RGB(0x00, 0x00, 0xff),  /* blue */
        QEMU_RGB(0xff, 0x00, 0xff),  /* magenta */
        QEMU_RGB(0x00, 0xff, 0xff),  /* cyan */
        QEMU_RGB(0xff, 0xff, 0xff),  /* white */
    }
};

static inline unsigned int col_expand(DisplayState *ds, unsigned int col)
{
    switch(ds_get_bits_per_pixel(ds)) {
    case 8:
        col |= col << 8;
        col |= col << 16;
        break;
    case 15:
    case 16:
        col |= col << 16;
        break;
    default:
        break;
    }

    return col;
}
#ifdef DEBUG_CONSOLE
static void console_print_text_attributes(TextAttributes *t_attrib, char ch)
{
    if (t_attrib->bold) {
        printf("b");
    } else {
        printf(" ");
    }
    if (t_attrib->uline) {
        printf("u");
    } else {
        printf(" ");
    }
    if (t_attrib->blink) {
        printf("l");
    } else {
        printf(" ");
    }
    if (t_attrib->invers) {
        printf("i");
    } else {
        printf(" ");
    }
    if (t_attrib->unvisible) {
        printf("n");
    } else {
        printf(" ");
    }

    printf(" fg: %d bg: %d ch:'%2X' '%c'\n", t_attrib->fgcol, t_attrib->bgcol, ch, ch);
}
#endif

static void vga_putcharxy(DisplayState *ds, int x, int y, int ch,
                          TextAttributes *t_attrib)
{
    uint8_t *d;
    const uint8_t *font_ptr;
    unsigned int font_data, linesize, xorcol, bpp;
    int i;
    unsigned int fgcol, bgcol;

#ifdef DEBUG_CONSOLE
    printf("x: %2i y: %2i", x, y);
    console_print_text_attributes(t_attrib, ch);
#endif

    if (t_attrib->invers) {
        bgcol = color_table[t_attrib->bold][t_attrib->fgcol];
        fgcol = color_table[t_attrib->bold][t_attrib->bgcol];
    } else {
        fgcol = color_table[t_attrib->bold][t_attrib->fgcol];
        bgcol = color_table[t_attrib->bold][t_attrib->bgcol];
    }

    bpp = (ds_get_bits_per_pixel(ds) + 7) >> 3;
    d = ds_get_data(ds) +
        ds_get_linesize(ds) * y * FONT_HEIGHT + bpp * x * FONT_WIDTH;
    linesize = ds_get_linesize(ds);
    font_ptr = vgafont16 + FONT_HEIGHT * ch;
    xorcol = bgcol ^ fgcol;
    switch(ds_get_bits_per_pixel(ds)) {
    case 8:
        for(i = 0; i < FONT_HEIGHT; i++) {
            font_data = *font_ptr++;
            if (t_attrib->uline
                && ((i == FONT_HEIGHT - 2) || (i == FONT_HEIGHT - 3))) {
                font_data = 0xFFFF;
            }
            ((uint32_t *)d)[0] = (dmask16[(font_data >> 4)] & xorcol) ^ bgcol;
            ((uint32_t *)d)[1] = (dmask16[(font_data >> 0) & 0xf] & xorcol) ^ bgcol;
            d += linesize;
        }
        break;
    case 16:
    case 15:
        for(i = 0; i < FONT_HEIGHT; i++) {
            font_data = *font_ptr++;
            if (t_attrib->uline
                && ((i == FONT_HEIGHT - 2) || (i == FONT_HEIGHT - 3))) {
                font_data = 0xFFFF;
            }
            ((uint32_t *)d)[0] = (dmask4[(font_data >> 6)] & xorcol) ^ bgcol;
            ((uint32_t *)d)[1] = (dmask4[(font_data >> 4) & 3] & xorcol) ^ bgcol;
            ((uint32_t *)d)[2] = (dmask4[(font_data >> 2) & 3] & xorcol) ^ bgcol;
            ((uint32_t *)d)[3] = (dmask4[(font_data >> 0) & 3] & xorcol) ^ bgcol;
            d += linesize;
        }
        break;
    case 32:
        for(i = 0; i < FONT_HEIGHT; i++) {
            font_data = *font_ptr++;
            if (t_attrib->uline && ((i == FONT_HEIGHT - 2) || (i == FONT_HEIGHT - 3))) {
                font_data = 0xFFFF;
            }
            ((uint32_t *)d)[0] = (-((font_data >> 7)) & xorcol) ^ bgcol;
            ((uint32_t *)d)[1] = (-((font_data >> 6) & 1) & xorcol) ^ bgcol;
            ((uint32_t *)d)[2] = (-((font_data >> 5) & 1) & xorcol) ^ bgcol;
            ((uint32_t *)d)[3] = (-((font_data >> 4) & 1) & xorcol) ^ bgcol;
            ((uint32_t *)d)[4] = (-((font_data >> 3) & 1) & xorcol) ^ bgcol;
            ((uint32_t *)d)[5] = (-((font_data >> 2) & 1) & xorcol) ^ bgcol;
            ((uint32_t *)d)[6] = (-((font_data >> 1) & 1) & xorcol) ^ bgcol;
            ((uint32_t *)d)[7] = (-((font_data >> 0) & 1) & xorcol) ^ bgcol;
            d += linesize;
        }
        break;
    }
}

static void text_console_resize(TextConsole *s)
{
    TextCell *cells, *c, *c1;
    int w1, x, y, last_width;

    last_width = s->width;
    s->width = s->g_width / FONT_WIDTH;
    s->height = s->g_height / FONT_HEIGHT;

    w1 = last_width;
    if (s->width < w1)
        w1 = s->width;

    cells = qemu_malloc(s->width * s->total_height * sizeof(TextCell));
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
    qemu_free(s->cells);
    s->cells = cells;
}

static inline void text_update_xy(TextConsole *s, int x, int y)
{
    s->text_x[0] = MIN(s->text_x[0], x);
    s->text_x[1] = MAX(s->text_x[1], x);
    s->text_y[0] = MIN(s->text_y[0], y);
    s->text_y[1] = MAX(s->text_y[1], y);
}

static void update_xy(TextConsole *s, int x, int y)
{
    TextCell *c;
    int y1, y2;

    if (s == active_console) {
        if (!ds_get_bits_per_pixel(s->ds)) {
            text_update_xy(s, x, y);
            return;
        }

        y1 = (s->y_base + y) % s->total_height;
        y2 = y1 - s->y_displayed;
        if (y2 < 0)
            y2 += s->total_height;
        if (y2 < s->height) {
            c = &s->cells[y1 * s->width + x];
            vga_putcharxy(s->ds, x, y2, c->ch,
                          &(c->t_attrib));
            dpy_update(s->ds, x * FONT_WIDTH, y2 * FONT_HEIGHT,
                       FONT_WIDTH, FONT_HEIGHT);
        }
    }
}

static void console_show_cursor(TextConsole *s, int show)
{
    TextCell *c;
    int y, y1;

    if (s == active_console) {
        int x = s->x;

        if (!ds_get_bits_per_pixel(s->ds)) {
            s->cursor_invalidate = 1;
            return;
        }

        if (x >= s->width) {
            x = s->width - 1;
        }
        y1 = (s->y_base + s->y) % s->total_height;
        y = y1 - s->y_displayed;
        if (y < 0)
            y += s->total_height;
        if (y < s->height) {
            c = &s->cells[y1 * s->width + x];
            if (show) {
                TextAttributes t_attrib = s->t_attrib_default;
                t_attrib.invers = !(t_attrib.invers); /* invert fg and bg */
                vga_putcharxy(s->ds, x, y, c->ch, &t_attrib);
            } else {
                vga_putcharxy(s->ds, x, y, c->ch, &(c->t_attrib));
            }
            dpy_update(s->ds, x * FONT_WIDTH, y * FONT_HEIGHT,
                       FONT_WIDTH, FONT_HEIGHT);
        }
    }
}

static void console_refresh(TextConsole *s)
{
    TextCell *c;
    int x, y, y1;

    if (s != active_console)
        return;
    if (!ds_get_bits_per_pixel(s->ds)) {
        s->text_x[0] = 0;
        s->text_y[0] = 0;
        s->text_x[1] = s->width - 1;
        s->text_y[1] = s->height - 1;
        s->cursor_invalidate = 1;
        return;
    }

    vga_fill_rect(s->ds, 0, 0, ds_get_width(s->ds), ds_get_height(s->ds),
                  color_table[0][COLOR_BLACK]);
    y1 = s->y_displayed;
    for(y = 0; y < s->height; y++) {
        c = s->cells + y1 * s->width;
        for(x = 0; x < s->width; x++) {
            vga_putcharxy(s->ds, x, y, c->ch,
                          &(c->t_attrib));
            c++;
        }
        if (++y1 == s->total_height)
            y1 = 0;
    }
    dpy_update(s->ds, 0, 0, ds_get_width(s->ds), ds_get_height(s->ds));
    console_show_cursor(s, 1);
}

static void console_scroll(int ydelta)
{
    TextConsole *s;
    int i, y1;

    s = active_console;
    if (!s || (s->console_type == GRAPHIC_CONSOLE))
        return;

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

static void console_put_lf(TextConsole *s)
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
        if (s == active_console && s->y_displayed == s->y_base) {
            if (!ds_get_bits_per_pixel(s->ds)) {
                s->text_x[0] = 0;
                s->text_y[0] = 0;
                s->text_x[1] = s->width - 1;
                s->text_y[1] = s->height - 1;
                return;
            }

            vga_bitblt(s->ds, 0, FONT_HEIGHT, 0, 0,
                       s->width * FONT_WIDTH,
                       (s->height - 1) * FONT_HEIGHT);
            vga_fill_rect(s->ds, 0, (s->height - 1) * FONT_HEIGHT,
                          s->width * FONT_WIDTH, FONT_HEIGHT,
                          color_table[0][s->t_attrib_default.bgcol]);
            dpy_update(s->ds, 0, 0,
                       s->width * FONT_WIDTH, s->height * FONT_HEIGHT);
        }
    }
}

/* Set console attributes depending on the current escape codes.
 * NOTE: I know this code is not very efficient (checking every color for it
 * self) but it is more readable and better maintainable.
 */
static void console_handle_escape(TextConsole *s)
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
                s->t_attrib.fgcol=COLOR_BLACK;
                break;
            case 31:
                s->t_attrib.fgcol=COLOR_RED;
                break;
            case 32:
                s->t_attrib.fgcol=COLOR_GREEN;
                break;
            case 33:
                s->t_attrib.fgcol=COLOR_YELLOW;
                break;
            case 34:
                s->t_attrib.fgcol=COLOR_BLUE;
                break;
            case 35:
                s->t_attrib.fgcol=COLOR_MAGENTA;
                break;
            case 36:
                s->t_attrib.fgcol=COLOR_CYAN;
                break;
            case 37:
                s->t_attrib.fgcol=COLOR_WHITE;
                break;
            /* set background color */
            case 40:
                s->t_attrib.bgcol=COLOR_BLACK;
                break;
            case 41:
                s->t_attrib.bgcol=COLOR_RED;
                break;
            case 42:
                s->t_attrib.bgcol=COLOR_GREEN;
                break;
            case 43:
                s->t_attrib.bgcol=COLOR_YELLOW;
                break;
            case 44:
                s->t_attrib.bgcol=COLOR_BLUE;
                break;
            case 45:
                s->t_attrib.bgcol=COLOR_MAGENTA;
                break;
            case 46:
                s->t_attrib.bgcol=COLOR_CYAN;
                break;
            case 47:
                s->t_attrib.bgcol=COLOR_WHITE;
                break;
        }
    }
}

static void console_clear_xy(TextConsole *s, int x, int y)
{
    int y1 = (s->y_base + y) % s->total_height;
    TextCell *c = &s->cells[y1 * s->width + x];
    c->ch = ' ';
    c->t_attrib = s->t_attrib_default;
    c++;
    update_xy(s, x, y);
}

static void console_putchar(TextConsole *s, int ch)
{
    TextCell *c;
    int y1, i;
    int x, y;

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
                s->esc_params[s->nb_esc_params] =
                    s->esc_params[s->nb_esc_params] * 10 + ch - '0';
            }
        } else {
            s->nb_esc_params++;
            if (ch == ';')
                break;
#ifdef DEBUG_CONSOLE
            fprintf(stderr, "escape sequence CSI%d;%d%c, %d parameters\n",
                    s->esc_params[0], s->esc_params[1], ch, s->nb_esc_params);
#endif
            s->state = TTY_STATE_NORM;
            switch(ch) {
            case 'A':
                /* move cursor up */
                if (s->esc_params[0] == 0) {
                    s->esc_params[0] = 1;
                }
                s->y -= s->esc_params[0];
                if (s->y < 0) {
                    s->y = 0;
                }
                break;
            case 'B':
                /* move cursor down */
                if (s->esc_params[0] == 0) {
                    s->esc_params[0] = 1;
                }
                s->y += s->esc_params[0];
                if (s->y >= s->height) {
                    s->y = s->height - 1;
                }
                break;
            case 'C':
                /* move cursor right */
                if (s->esc_params[0] == 0) {
                    s->esc_params[0] = 1;
                }
                s->x += s->esc_params[0];
                if (s->x >= s->width) {
                    s->x = s->width - 1;
                }
                break;
            case 'D':
                /* move cursor left */
                if (s->esc_params[0] == 0) {
                    s->esc_params[0] = 1;
                }
                s->x -= s->esc_params[0];
                if (s->x < 0) {
                    s->x = 0;
                }
                break;
            case 'G':
                /* move cursor to column */
                s->x = s->esc_params[0] - 1;
                if (s->x < 0) {
                    s->x = 0;
                }
                break;
            case 'f':
            case 'H':
                /* move cursor to row, column */
                s->x = s->esc_params[1] - 1;
                if (s->x < 0) {
                    s->x = 0;
                }
                s->y = s->esc_params[0] - 1;
                if (s->y < 0) {
                    s->y = 0;
                }
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
                    for (x = 0; x <= s->x; x++) {
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
                /* report cursor position */
                /* TODO: send ESC[row;colR */
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
#ifdef DEBUG_CONSOLE
                fprintf(stderr, "unhandled escape character '%c'\n", ch);
#endif
                break;
            }
            break;
        }
    }
}

void console_select(unsigned int index)
{
    TextConsole *s;

    if (index >= MAX_CONSOLES)
        return;
    active_console->g_width = ds_get_width(active_console->ds);
    active_console->g_height = ds_get_height(active_console->ds);
    s = consoles[index];
    if (s) {
        DisplayState *ds = s->ds;
        active_console = s;
        ds->surface = qemu_resize_displaysurface(ds->surface, s->g_width,
                                                s->g_height, 32, 4 * s->g_width);
        dpy_resize(s->ds);
        vga_hw_invalidate();
    }
}

static int console_puts(CharDriverState *chr, const uint8_t *buf, int len)
{
    TextConsole *s = chr->opaque;
    int i;

    console_show_cursor(s, 0);
    for(i = 0; i < len; i++) {
        console_putchar(s, buf[i]);
    }
    console_show_cursor(s, 1);
    return len;
}

static void console_send_event(CharDriverState *chr, int event)
{
    TextConsole *s = chr->opaque;
    int i;

    if (event == CHR_EVENT_FOCUS) {
        for(i = 0; i < nb_consoles; i++) {
            if (consoles[i] == s) {
                console_select(i);
                break;
            }
        }
    }
}

static void kbd_send_chars(void *opaque)
{
    TextConsole *s = opaque;
    int len;
    uint8_t buf[16];

    len = qemu_chr_can_read(s->chr);
    if (len > s->out_fifo.count)
        len = s->out_fifo.count;
    if (len > 0) {
        if (len > sizeof(buf))
            len = sizeof(buf);
        qemu_fifo_read(&s->out_fifo, buf, len);
        qemu_chr_read(s->chr, buf, len);
    }
    /* characters are pending: we send them a bit later (XXX:
       horrible, should change char device API) */
    if (s->out_fifo.count > 0) {
        qemu_mod_timer(s->kbd_timer, qemu_get_clock(rt_clock) + 1);
    }
}

/* called when an ascii key is pressed */
void kbd_put_keysym(int keysym)
{
    TextConsole *s;
    uint8_t buf[16], *q;
    int c;

    s = active_console;
    if (!s || (s->console_type == GRAPHIC_CONSOLE))
        return;

    switch(keysym) {
    case QEMU_KEY_CTRL_UP:
        console_scroll(-1);
        break;
    case QEMU_KEY_CTRL_DOWN:
        console_scroll(1);
        break;
    case QEMU_KEY_CTRL_PAGEUP:
        console_scroll(-10);
        break;
    case QEMU_KEY_CTRL_PAGEDOWN:
        console_scroll(10);
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
        } else {
                *q++ = keysym;
        }
        if (s->chr->chr_read) {
            qemu_fifo_write(&s->out_fifo, buf, q - buf);
            kbd_send_chars(s);
        }
        break;
    }
}

static void text_console_invalidate(void *opaque)
{
    TextConsole *s = (TextConsole *) opaque;
    console_refresh(s);
}

static void text_console_update(void *opaque, console_ch_t *chardata)
{
    TextConsole *s = (TextConsole *) opaque;
    int i, j, src;

    if (s->text_x[0] <= s->text_x[1]) {
        src = (s->y_base + s->text_y[0]) * s->width;
        chardata += s->text_y[0] * s->width;
        for (i = s->text_y[0]; i <= s->text_y[1]; i ++)
            for (j = 0; j < s->width; j ++, src ++)
                console_write_ch(chardata ++, s->cells[src].ch |
                                (s->cells[src].t_attrib.fgcol << 12) |
                                (s->cells[src].t_attrib.bgcol << 8) |
                                (s->cells[src].t_attrib.bold << 21));
        dpy_update(s->ds, s->text_x[0], s->text_y[0],
                   s->text_x[1] - s->text_x[0], i - s->text_y[0]);
        s->text_x[0] = s->width;
        s->text_y[0] = s->height;
        s->text_x[1] = 0;
        s->text_y[1] = 0;
    }
    if (s->cursor_invalidate) {
        dpy_cursor(s->ds, s->x, s->y);
        s->cursor_invalidate = 0;
    }
}

static TextConsole *get_graphic_console(void)
{
    int i;
    TextConsole *s;
    for (i = 0; i < nb_consoles; i++) {
        s = consoles[i];
        if (s->console_type == GRAPHIC_CONSOLE)
            return s;
    }
    return NULL;
}

static TextConsole *new_console(DisplayState *ds, console_type_t console_type)
{
    TextConsole *s;
    int i;

    if (nb_consoles >= MAX_CONSOLES)
        return NULL;
    s = qemu_mallocz(sizeof(TextConsole));
    if (!s) {
        return NULL;
    }
    if (!active_console || ((active_console->console_type != GRAPHIC_CONSOLE) &&
        (console_type == GRAPHIC_CONSOLE))) {
        active_console = s;
    }
    s->ds = ds;
    s->console_type = console_type;
    if (console_type != GRAPHIC_CONSOLE) {
        consoles[nb_consoles++] = s;
    } else {
        /* HACK: Put graphical consoles before text consoles.  */
        for (i = nb_consoles; i > 0; i--) {
            if (consoles[i - 1]->console_type == GRAPHIC_CONSOLE)
                break;
            consoles[i] = consoles[i - 1];
        }
        consoles[i] = s;
        nb_consoles++;
    }
    return s;
}

DisplayState *graphic_console_init(vga_hw_update_ptr update,
                                   vga_hw_invalidate_ptr invalidate,
                                   vga_hw_screen_dump_ptr screen_dump,
                                   vga_hw_text_update_ptr text_update,
                                   void *opaque)
{
    TextConsole *s;
    DisplayState *ds;
    
    ds = (DisplayState *) qemu_mallocz(sizeof(DisplayState));
    if (ds == NULL)
        return NULL;
    ds->surface = qemu_create_displaysurface(640, 480, 32, 640 * 4);

    s = new_console(ds, GRAPHIC_CONSOLE);
    if (s == NULL) {
        qemu_free_displaysurface(ds->surface);
        qemu_free(ds);
        return NULL;
    }
    s->hw_update = update;
    s->hw_invalidate = invalidate;
    s->hw_screen_dump = screen_dump;
    s->hw_text_update = text_update;
    s->hw = opaque;

    register_displaystate(ds); 
    return ds;
}

int is_graphic_console(void)
{
    return active_console && active_console->console_type == GRAPHIC_CONSOLE;
}

int is_fixedsize_console(void)
{
    return active_console && active_console->console_type != TEXT_CONSOLE;
}

void console_color_init(DisplayState *ds)
{
    int i, j;
    for (j = 0; j < 2; j++) {
        for (i = 0; i < 8; i++) {
            color_table[j][i] = col_expand(ds, 
                   vga_get_color(ds, color_table_rgb[j][i]));
        }
    }
}

static int n_text_consoles;
static CharDriverState *text_consoles[128];
static char *text_console_strs[128];

static void text_console_do_init(CharDriverState *chr, DisplayState *ds, const char *p)
{
    TextConsole *s;
    unsigned width;
    unsigned height;
    static int color_inited;

    s = new_console(ds, (p == 0) ? TEXT_CONSOLE : TEXT_CONSOLE_FIXED_SIZE);
    if (!s) {
        free(chr);
        return NULL;
    }
    chr->opaque = s;
    chr->chr_write = console_puts;
    chr->chr_send_event = console_send_event;

    s->chr = chr;
    s->out_fifo.buf = s->out_fifo_buf;
    s->out_fifo.buf_size = sizeof(s->out_fifo_buf);
    s->kbd_timer = qemu_new_timer(rt_clock, kbd_send_chars, s);
    s->ds = ds;

    if (!color_inited) {
        color_inited = 1;
        console_color_init(s->ds);
    }
    s->y_displayed = 0;
    s->y_base = 0;
    s->total_height = DEFAULT_BACKSCROLL;
    s->x = 0;
    s->y = 0;
    width = ds_get_width(s->ds);
    height = ds_get_height(s->ds);
    if (p != 0) {
        width = strtoul(p, (char **)&p, 10);
        if (*p == 'C') {
            p++;
            width *= FONT_WIDTH;
        }
        if (*p == 'x') {
            p++;
            height = strtoul(p, (char **)&p, 10);
            if (*p == 'C') {
                p++;
                height *= FONT_HEIGHT;
            }
        }
    }
    s->g_width = width;
    s->g_height = height;

    s->hw_invalidate = text_console_invalidate;
    s->hw_text_update = text_console_update;
    s->hw = s;

    /* Set text attribute defaults */
    s->t_attrib_default.bold = 0;
    s->t_attrib_default.uline = 0;
    s->t_attrib_default.blink = 0;
    s->t_attrib_default.invers = 0;
    s->t_attrib_default.unvisible = 0;
    s->t_attrib_default.fgcol = COLOR_WHITE;
    s->t_attrib_default.bgcol = COLOR_BLACK;
    /* set current text attributes to default */
    s->t_attrib = s->t_attrib_default;
    text_console_resize(s);

    qemu_chr_reset(chr);

    return chr;
}

CharDriverState *text_console_init(const char *p)
{
    CharDriverState *chr;

    chr = qemu_mallocz(sizeof(CharDriverState));
    if (!chr)
        return NULL;

    if (n_text_consoles == 128) {
        fprintf(stderr, "Too many text consoles\n");
        exit(1);
    }
    text_consoles[n_text_consoles] = chr;
    text_console_strs[n_text_consoles] = p ? qemu_strdup(p) : NULL;
    n_text_consoles++;

    return chr;
}

void text_consoles_set_display(DisplayState *ds)
{
    int i;

    for (i = 0; i < n_text_consoles; i++) {
        text_console_do_init(text_consoles[i], ds, text_console_strs[i]);
        qemu_free(text_console_strs[i]);
    }

    n_text_consoles = 0;
}

void qemu_console_resize(DisplayState *ds, int width, int height)
{
    TextConsole *s = get_graphic_console();
    s->g_width = width;
    s->g_height = height;
    if (is_graphic_console()) {
        ds->surface = qemu_resize_displaysurface(ds->surface, width, height, 32, 4 * width);
        dpy_resize(ds);
    }
}

void qemu_console_copy(DisplayState *ds, int src_x, int src_y,
                       int dst_x, int dst_y, int w, int h)
{
    if (is_graphic_console()) {
        dpy_copy(ds, src_x, src_y, dst_x, dst_y, w, h);
    }
}

static PixelFormat qemu_default_pixelformat(int bpp)
{
    PixelFormat pf;

    memset(&pf, 0x00, sizeof(PixelFormat));

    pf.bits_per_pixel = bpp;
    pf.bytes_per_pixel = bpp / 8;
    pf.depth = bpp == 32 ? 24 : bpp;

    switch (bpp) {
        case 8:
            pf.rmask = 0x000000E0;
            pf.gmask = 0x0000001C;
            pf.bmask = 0x00000003;
            pf.rmax = 7;
            pf.gmax = 7;
            pf.bmax = 3;
            pf.rshift = 5;
            pf.gshift = 2;
            pf.bshift = 0;
            break;
        case 16:
            pf.rmask = 0x0000F800;
            pf.gmask = 0x000007E0;
            pf.bmask = 0x0000001F;
            pf.rmax = 31;
            pf.gmax = 63;
            pf.bmax = 31;
            pf.rshift = 11;
            pf.gshift = 5;
            pf.bshift = 0;
            break;
        case 24:
        case 32:
            pf.rmask = 0x00FF0000;
            pf.gmask = 0x0000FF00;
            pf.bmask = 0x000000FF;
            pf.rmax = 255;
            pf.gmax = 255;
            pf.bmax = 255;
            pf.rshift = 16;
            pf.gshift = 8;
            pf.bshift = 0;
            break;
        default:
            break;
    }
    return pf;
}

DisplaySurface* qemu_create_displaysurface(int width, int height, int bpp, int linesize)
{
    DisplaySurface *surface = (DisplaySurface*) qemu_mallocz(sizeof(DisplaySurface));
    if (surface == NULL) {
        fprintf(stderr, "qemu_create_displaysurface: malloc failed\n");
        exit(1);
    }

    surface->width = width;
    surface->height = height;
    surface->linesize = linesize;
    surface->pf = qemu_default_pixelformat(bpp);
#ifdef WORDS_BIGENDIAN
    surface->flags = QEMU_ALLOCATED_FLAG | QEMU_BIG_ENDIAN_FLAG;
#else
    surface->flags = QEMU_ALLOCATED_FLAG;
#endif
    surface->data = (uint8_t*) qemu_mallocz(surface->linesize * surface->height);
    if (surface->data == NULL) {
        fprintf(stderr, "qemu_create_displaysurface: malloc failed\n");
        exit(1);
    }

    return surface;
}

DisplaySurface* qemu_resize_displaysurface(DisplaySurface *surface,
                                          int width, int height, int bpp, int linesize)
{
    surface->width = width;
    surface->height = height;
    surface->linesize = linesize;
    surface->pf = qemu_default_pixelformat(bpp);
    if (surface->flags & QEMU_ALLOCATED_FLAG)
        surface->data = (uint8_t*) qemu_realloc(surface->data, surface->linesize * surface->height);
    else
        surface->data = (uint8_t*) qemu_malloc(surface->linesize * surface->height);
    if (surface->data == NULL) {
        fprintf(stderr, "qemu_resize_displaysurface: malloc failed\n");
        exit(1);
    }
#ifdef WORDS_BIGENDIAN
    surface->flags = QEMU_ALLOCATED_FLAG | QEMU_BIG_ENDIAN_FLAG;
#else
    surface->flags = QEMU_ALLOCATED_FLAG;
#endif

    return surface;
}

DisplaySurface* qemu_create_displaysurface_from(int width, int height, int bpp,
                                              int linesize, uint8_t *data)
{
    DisplaySurface *surface = (DisplaySurface*) qemu_mallocz(sizeof(DisplaySurface));
    if (surface == NULL) {
        fprintf(stderr, "qemu_create_displaysurface_from: malloc failed\n");
        exit(1);
    }

    surface->width = width;
    surface->height = height;
    surface->linesize = linesize;
    surface->pf = qemu_default_pixelformat(bpp);
#ifdef WORDS_BIGENDIAN
    surface->flags = QEMU_BIG_ENDIAN_FLAG;
#endif
    surface->data = data;

    return surface;
}

void qemu_free_displaysurface(DisplaySurface *surface)
{
    if (surface == NULL)
        return;
    if (surface->flags & QEMU_ALLOCATED_FLAG)
        qemu_free(surface->data);
    qemu_free(surface);
}
