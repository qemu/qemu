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
#include "vl.h"

#define DEFAULT_BACKSCROLL 512
#define MAX_CONSOLES 12

#define RGBA(r, g, b, a) (((a) << 24) | ((r) << 16) | ((g) << 8) | (b))
#define RGB(r, g, b) RGBA(r, g, b, 0xff)

typedef struct TextCell {
    uint8_t ch;
    uint8_t bgcol:4;
    uint8_t fgcol:4;
} TextCell;

#define MAX_ESC_PARAMS 3

enum TTYState {
    TTY_STATE_NORM,
    TTY_STATE_ESC,
    TTY_STATE_CSI,
};

struct TextConsole {
    int text_console; /* true if text console */
    DisplayState *ds;
    int g_width, g_height;
    int width;
    int height;
    int total_height;
    int backscroll_height;
    int fgcol;
    int bgcol;
    int x, y;
    int y_displayed;
    int y_base;
    TextCell *cells;

    enum TTYState state;
    int esc_params[MAX_ESC_PARAMS];
    int nb_esc_params;

    /* kbd read handler */
    IOReadHandler *fd_read;
    void *fd_opaque;
};

static TextConsole *active_console;
static TextConsole *consoles[MAX_CONSOLES];
static int nb_consoles = 0;

/* convert a RGBA color to a color index usable in graphic primitives */
static unsigned int vga_get_color(DisplayState *ds, unsigned int rgba)
{
    unsigned int r, g, b, color;

    switch(ds->depth) {
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
    
    bpp = (ds->depth + 7) >> 3;
    d1 = ds->data + 
        ds->linesize * posy + bpp * posx;
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
        d1 += ds->linesize;
    }
}

/* copy from (xs, ys) to (xd, yd) a rectangle of size (w, h) */
static void vga_bitblt(DisplayState *ds, int xs, int ys, int xd, int yd, int w, int h)
{
    const uint8_t *s;
    uint8_t *d;
    int wb, y, bpp;

    bpp = (ds->depth + 7) >> 3;
    wb = w * bpp;
    if (yd <= ys) {
        s = ds->data + 
            ds->linesize * ys + bpp * xs;
        d = ds->data + 
            ds->linesize * yd + bpp * xd;
        for (y = 0; y < h; y++) {
            memmove(d, s, wb);
            d += ds->linesize;
            s += ds->linesize;
        }
    } else {
        s = ds->data + 
            ds->linesize * (ys + h - 1) + bpp * xs;
        d = ds->data + 
            ds->linesize * (yd + h - 1) + bpp * xd;
       for (y = 0; y < h; y++) {
            memmove(d, s, wb);
            d -= ds->linesize;
            s -= ds->linesize;
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

static uint32_t color_table[8];

static const uint32_t color_table_rgb[8] = {
    RGB(0x00, 0x00, 0x00),
    RGB(0xff, 0x00, 0x00),
    RGB(0x00, 0xff, 0x00),
    RGB(0xff, 0xff, 0x00),
    RGB(0x00, 0x00, 0xff),
    RGB(0xff, 0x00, 0xff),
    RGB(0x00, 0xff, 0xff),
    RGB(0xff, 0xff, 0xff),
};

static inline unsigned int col_expand(DisplayState *ds, unsigned int col)
{
    switch(ds->depth) {
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

static void vga_putcharxy(DisplayState *ds, int x, int y, int ch, 
                          unsigned int fgcol, unsigned int bgcol)
{
    uint8_t *d;
    const uint8_t *font_ptr;
    unsigned int font_data, linesize, xorcol, bpp;
    int i;

    bpp = (ds->depth + 7) >> 3;
    d = ds->data + 
        ds->linesize * y * FONT_HEIGHT + bpp * x * FONT_WIDTH;
    linesize = ds->linesize;
    font_ptr = vgafont16 + FONT_HEIGHT * ch;
    xorcol = bgcol ^ fgcol;
    switch(ds->depth) {
    case 8:
        for(i = 0; i < FONT_HEIGHT; i++) {
            font_data = *font_ptr++;
            ((uint32_t *)d)[0] = (dmask16[(font_data >> 4)] & xorcol) ^ bgcol;
            ((uint32_t *)d)[1] = (dmask16[(font_data >> 0) & 0xf] & xorcol) ^ bgcol;
            d += linesize;
        }
        break;
    case 16:
    case 15:
        for(i = 0; i < FONT_HEIGHT; i++) {
            font_data = *font_ptr++;
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
            c->fgcol = 7;
            c->bgcol = 0;
            c++;
        }
    }
    free(s->cells);
    s->cells = cells;
}

static void update_xy(TextConsole *s, int x, int y)
{
    TextCell *c;
    int y1, y2;

    if (s == active_console) {
        y1 = (s->y_base + y) % s->total_height;
        y2 = y1 - s->y_displayed;
        if (y2 < 0)
            y2 += s->total_height;
        if (y2 < s->height) {
            c = &s->cells[y1 * s->width + x];
            vga_putcharxy(s->ds, x, y2, c->ch, 
                          color_table[c->fgcol], color_table[c->bgcol]);
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
        y1 = (s->y_base + s->y) % s->total_height;
        y = y1 - s->y_displayed;
        if (y < 0)
            y += s->total_height;
        if (y < s->height) {
            c = &s->cells[y1 * s->width + s->x];
            if (show) {
                vga_putcharxy(s->ds, s->x, y, c->ch, 
                              color_table[0], color_table[7]);
            } else {
                vga_putcharxy(s->ds, s->x, y, c->ch, 
                              color_table[c->fgcol], color_table[c->bgcol]);
            }
            dpy_update(s->ds, s->x * FONT_WIDTH, y * FONT_HEIGHT, 
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

    vga_fill_rect(s->ds, 0, 0, s->ds->width, s->ds->height,
                  color_table[0]);
    y1 = s->y_displayed;
    for(y = 0; y < s->height; y++) {
        c = s->cells + y1 * s->width;
        for(x = 0; x < s->width; x++) {
            vga_putcharxy(s->ds, x, y, c->ch, 
                          color_table[c->fgcol], color_table[c->bgcol]);
            c++;
        }
        if (++y1 == s->total_height)
            y1 = 0;
    }
    dpy_update(s->ds, 0, 0, s->ds->width, s->ds->height);
    console_show_cursor(s, 1);
}

static void console_scroll(int ydelta)
{
    TextConsole *s;
    int i, y1;
    
    s = active_console;
    if (!s || !s->text_console)
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

    s->x = 0;
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
            c->fgcol = s->fgcol;
            c->bgcol = s->bgcol;
            c++;
        }
        if (s == active_console && s->y_displayed == s->y_base) {
            vga_bitblt(s->ds, 0, FONT_HEIGHT, 0, 0, 
                       s->width * FONT_WIDTH, 
                       (s->height - 1) * FONT_HEIGHT);
            vga_fill_rect(s->ds, 0, (s->height - 1) * FONT_HEIGHT,
                          s->width * FONT_WIDTH, FONT_HEIGHT, 
                          color_table[s->bgcol]);
            dpy_update(s->ds, 0, 0, 
                       s->width * FONT_WIDTH, s->height * FONT_HEIGHT);
        }
    }
}

static void console_putchar(TextConsole *s, int ch)
{
    TextCell *c;
    int y1, i, x;

    switch(s->state) {
    case TTY_STATE_NORM:
        switch(ch) {
        case '\r':
            s->x = 0;
            break;
        case '\n':
            console_put_lf(s);
            break;
        case 27:
            s->state = TTY_STATE_ESC;
            break;
        default:
            y1 = (s->y_base + s->y) % s->total_height;
            c = &s->cells[y1 * s->width + s->x];
            c->ch = ch;
            c->fgcol = s->fgcol;
            c->bgcol = s->bgcol;
            update_xy(s, s->x, s->y);
            s->x++;
            if (s->x >= s->width)
                console_put_lf(s);
            break;
        }
        break;
    case TTY_STATE_ESC:
        if (ch == '[') {
            for(i=0;i<MAX_ESC_PARAMS;i++)
                s->esc_params[i] = 0;
            s->nb_esc_params = 0;
            s->state = TTY_STATE_CSI;
        } else {
            s->state = TTY_STATE_NORM;
        }
        break;
    case TTY_STATE_CSI:
        if (ch >= '0' && ch <= '9') {
            if (s->nb_esc_params < MAX_ESC_PARAMS) {
                s->esc_params[s->nb_esc_params] = 
                    s->esc_params[s->nb_esc_params] * 10 + ch - '0';
            }
        } else {
            s->nb_esc_params++;
            if (ch == ';')
                break;
            s->state = TTY_STATE_NORM;
            switch(ch) {
            case 'D':
                if (s->x > 0)
                    s->x--;
                break;
            case 'C':
                if (s->x < (s->width - 1))
                    s->x++;
                break;
            case 'K':
                /* clear to eol */
                y1 = (s->y_base + s->y) % s->total_height;
                for(x = s->x; x < s->width; x++) {
                    c = &s->cells[y1 * s->width + x];
                    c->ch = ' ';
                    c->fgcol = s->fgcol;
                    c->bgcol = s->bgcol;
                    c++;
                    update_xy(s, x, s->y);
                }
                break;
            default:
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
    s = consoles[index];
    if (s) {
        active_console = s;
        if (s->text_console) {
            if (s->g_width != s->ds->width ||
                s->g_height != s->ds->height)
                text_console_resize(s);
            console_refresh(s);
        }
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

static void console_chr_add_read_handler(CharDriverState *chr, 
                                         IOCanRWHandler *fd_can_read, 
                                         IOReadHandler *fd_read, void *opaque)
{
    TextConsole *s = chr->opaque;
    s->fd_read = fd_read;
    s->fd_opaque = opaque;
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

/* called when an ascii key is pressed */
void kbd_put_keysym(int keysym)
{
    TextConsole *s;
    uint8_t buf[16], *q;
    int c;

    s = active_console;
    if (!s || !s->text_console)
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
        if (s->fd_read) {
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
            s->fd_read(s->fd_opaque, buf, q - buf);
        }
        break;
    }
}

TextConsole *graphic_console_init(DisplayState *ds)
{
    TextConsole *s;

    if (nb_consoles >= MAX_CONSOLES)
        return NULL;
    s = qemu_mallocz(sizeof(TextConsole));
    if (!s) {
        return NULL;
    }
    if (!active_console)
        active_console = s;
    s->ds = ds;
    consoles[nb_consoles++] = s;
    return s;
}

int is_active_console(TextConsole *s)
{
    return s == active_console;
}

CharDriverState *text_console_init(DisplayState *ds)
{
    CharDriverState *chr;
    TextConsole *s;
    int i;
    static int color_inited;
    
    chr = qemu_mallocz(sizeof(CharDriverState));
    if (!chr)
        return NULL;
    s = graphic_console_init(ds);
    if (!s) {
        free(chr);
        return NULL;
    }
    s->text_console = 1;
    chr->opaque = s;
    chr->chr_write = console_puts;
    chr->chr_add_read_handler = console_chr_add_read_handler;
    chr->chr_send_event = console_send_event;

    if (!color_inited) {
        color_inited = 1;
        for(i = 0; i < 8; i++) {
            color_table[i] = col_expand(s->ds, 
                                        vga_get_color(s->ds, color_table_rgb[i]));
        }
    }
    s->y_displayed = 0;
    s->y_base = 0;
    s->total_height = DEFAULT_BACKSCROLL;
    s->x = 0;
    s->y = 0;
    s->fgcol = 7;
    s->bgcol = 0;
    s->g_width = s->ds->width;
    s->g_height = s->ds->height;
    text_console_resize(s);

    return chr;
}
