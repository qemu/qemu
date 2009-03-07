/*
 * QEMU curses/ncurses display driver
 * 
 * Copyright (c) 2005 Andrzej Zaborowski  <balrog@zabor.org>
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
#include "sysemu.h"

#include <curses.h>

#ifndef _WIN32
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#endif

#ifdef __OpenBSD__
#define resize_term resizeterm
#endif

#define FONT_HEIGHT 16
#define FONT_WIDTH 8

static console_ch_t screen[160 * 100];
static WINDOW *screenpad = NULL;
static int width, height, gwidth, gheight, invalidate;
static int px, py, sminx, sminy, smaxx, smaxy;

static void curses_update(DisplayState *ds, int x, int y, int w, int h)
{
    chtype *line;

    line = ((chtype *) screen) + y * width;
    for (h += y; y < h; y ++, line += width)
        mvwaddchnstr(screenpad, y, 0, line, width);

    pnoutrefresh(screenpad, py, px, sminy, sminx, smaxy - 1, smaxx - 1);
    refresh();
}

static void curses_calc_pad(void)
{
    if (is_fixedsize_console()) {
        width = gwidth;
        height = gheight;
    } else {
        width = COLS;
        height = LINES;
    }

    if (screenpad)
        delwin(screenpad);

    clear();
    refresh();

    screenpad = newpad(height, width);

    if (width > COLS) {
        px = (width - COLS) / 2;
        sminx = 0;
        smaxx = COLS;
    } else {
        px = 0;
        sminx = (COLS - width) / 2;
        smaxx = sminx + width;
    }

    if (height > LINES) {
        py = (height - LINES) / 2;
        sminy = 0;
        smaxy = LINES;
    } else {
        py = 0;
        sminy = (LINES - height) / 2;
        smaxy = sminy + height;
    }
}

static void curses_resize(DisplayState *ds)
{
    if (ds_get_width(ds) == gwidth && ds_get_height(ds) == gheight)
        return;

    gwidth = ds_get_width(ds);
    gheight = ds_get_height(ds);

    curses_calc_pad();
    ds->surface->width = width * FONT_WIDTH;
    ds->surface->height = height * FONT_HEIGHT;
}

#ifndef _WIN32
#if defined(SIGWINCH) && defined(KEY_RESIZE)
static void curses_winch_handler(int signum)
{
    struct winsize {
        unsigned short ws_row;
        unsigned short ws_col;
        unsigned short ws_xpixel;   /* unused */
        unsigned short ws_ypixel;   /* unused */
    } ws;

    /* terminal size changed */
    if (ioctl(1, TIOCGWINSZ, &ws) == -1)
        return;

    resize_term(ws.ws_row, ws.ws_col);
    curses_calc_pad();
    invalidate = 1;

    /* some systems require this */
    signal(SIGWINCH, curses_winch_handler);
}
#endif
#endif

static void curses_cursor_position(DisplayState *ds, int x, int y)
{
    if (x >= 0) {
        x = sminx + x - px;
        y = sminy + y - py;

        if (x >= 0 && y >= 0 && x < COLS && y < LINES) {
            move(y, x);
            curs_set(1);
            /* it seems that curs_set(1) must always be called before
             * curs_set(2) for the latter to have effect */
            if (!is_graphic_console())
                curs_set(2);
            return;
        }
    }

    curs_set(0);
}

/* generic keyboard conversion */

#include "curses_keys.h"

static kbd_layout_t *kbd_layout = 0;
static int keycode2keysym[CURSES_KEYS];

static void curses_refresh(DisplayState *ds)
{
    int chr, nextchr, keysym, keycode;

    if (invalidate) {
        clear();
        refresh();
        curses_calc_pad();
        ds->surface->width = FONT_WIDTH * width;
        ds->surface->height = FONT_HEIGHT * height;
        vga_hw_invalidate();
        invalidate = 0;
    }

    vga_hw_text_update(screen);

    nextchr = ERR;
    while (1) {
        /* while there are any pending key strokes to process */
        if (nextchr == ERR)
            chr = getch();
        else {
            chr = nextchr;
            nextchr = ERR;
        }

        if (chr == ERR)
            break;

#ifdef KEY_RESIZE
        /* this shouldn't occur when we use a custom SIGWINCH handler */
        if (chr == KEY_RESIZE) {
            clear();
            refresh();
            curses_calc_pad();
            curses_update(ds, 0, 0, width, height);
            ds->surface->width = FONT_WIDTH * width;
            ds->surface->height = FONT_HEIGHT * height;
            continue;
        }
#endif

        keycode = curses2keycode[chr];
        if (keycode == -1)
            continue;

        /* alt key */
        if (keycode == 1) {
            nextchr = getch();

            if (nextchr != ERR) {
                keycode = curses2keycode[nextchr];
                nextchr = ERR;
                if (keycode == -1)
                    continue;

                keycode |= ALT;

                /* process keys reserved for qemu */
                if (keycode >= QEMU_KEY_CONSOLE0 &&
                        keycode < QEMU_KEY_CONSOLE0 + 9) {
                    erase();
                    wnoutrefresh(stdscr);
                    console_select(keycode - QEMU_KEY_CONSOLE0);

                    invalidate = 1;
                    continue;
                }
            }
        }

        if (kbd_layout && !(keycode & GREY)) {
            keysym = keycode2keysym[keycode & KEY_MASK];
            if (keysym == -1)
                keysym = chr;

            keycode &= ~KEY_MASK;
            keycode |= keysym2scancode(kbd_layout, keysym);
        }

        if (is_graphic_console()) {
            /* since terminals don't know about key press and release
             * events, we need to emit both for each key received */
            if (keycode & SHIFT)
                kbd_put_keycode(SHIFT_CODE);
            if (keycode & CNTRL)
                kbd_put_keycode(CNTRL_CODE);
            if (keycode & ALT)
                kbd_put_keycode(ALT_CODE);
            if (keycode & GREY)
                kbd_put_keycode(GREY_CODE);
            kbd_put_keycode(keycode & KEY_MASK);
            if (keycode & GREY)
                kbd_put_keycode(GREY_CODE);
            kbd_put_keycode((keycode & KEY_MASK) | KEY_RELEASE);
            if (keycode & ALT)
                kbd_put_keycode(ALT_CODE | KEY_RELEASE);
            if (keycode & CNTRL)
                kbd_put_keycode(CNTRL_CODE | KEY_RELEASE);
            if (keycode & SHIFT)
                kbd_put_keycode(SHIFT_CODE | KEY_RELEASE);
        } else {
            keysym = curses2keysym[chr];
            if (keysym == -1)
                keysym = chr;

            kbd_put_keysym(keysym);
        }
    }
}

static void curses_cleanup(void *opaque) 
{
    endwin();
}

static void curses_atexit(void)
{
    curses_cleanup(NULL);
}

static void curses_setup(void)
{
    int i, colour_default[8] = {
        COLOR_BLACK, COLOR_BLUE, COLOR_GREEN, COLOR_CYAN,
        COLOR_RED, COLOR_MAGENTA, COLOR_YELLOW, COLOR_WHITE,
    };

    /* input as raw as possible, let everything be interpreted
     * by the guest system */
    initscr(); noecho(); intrflush(stdscr, FALSE);
    nodelay(stdscr, TRUE); nonl(); keypad(stdscr, TRUE);
    start_color(); raw(); scrollok(stdscr, FALSE);

    for (i = 0; i < 64; i ++)
        init_pair(i, colour_default[i & 7], colour_default[i >> 3]);
}

static void curses_keyboard_setup(void)
{
    int i, keycode, keysym;

#if defined(__APPLE__)
    /* always use generic keymaps */
    if (!keyboard_layout)
        keyboard_layout = "en-us";
#endif
    if(keyboard_layout) {
        kbd_layout = init_keyboard_layout(name2keysym, keyboard_layout);
        if (!kbd_layout)
            exit(1);
    }

    for (i = 0; i < CURSES_KEYS; i ++)
        keycode2keysym[i] = -1;

    for (i = 0; i < CURSES_KEYS; i ++) {
        if (curses2keycode[i] == -1)
            continue;

        keycode = curses2keycode[i] & KEY_MASK;
        if (keycode2keysym[keycode] >= 0)
            continue;

        for (keysym = 0; keysym < CURSES_KEYS; keysym ++)
            if (curses2keycode[keysym] == keycode) {
                keycode2keysym[keycode] = keysym;
                break;
            }

        if (keysym >= CURSES_KEYS)
            keycode2keysym[keycode] = i;
    }
}

void curses_display_init(DisplayState *ds, int full_screen)
{
    DisplayChangeListener *dcl;
#ifndef _WIN32
    if (!isatty(1)) {
        fprintf(stderr, "We need a terminal output\n");
        exit(1);
    }
#endif

    curses_setup();
    curses_keyboard_setup();
    atexit(curses_atexit);

#ifndef _WIN32
#if defined(SIGWINCH) && defined(KEY_RESIZE)
    /* some curses implementations provide a handler, but we
     * want to be sure this is handled regardless of the library */
    signal(SIGWINCH, curses_winch_handler);
#endif
#endif

    dcl = (DisplayChangeListener *) qemu_mallocz(sizeof(DisplayChangeListener));
    dcl->dpy_update = curses_update;
    dcl->dpy_resize = curses_resize;
    dcl->dpy_refresh = curses_refresh;
    dcl->dpy_text_cursor = curses_cursor_position;
    register_displaychangelistener(ds, dcl);
    qemu_free_displaysurface(ds->surface);
    ds->surface = qemu_create_displaysurface_from(640, 400, 0, 0, (uint8_t*) screen);

    invalidate = 1;

    /* Standard VGA initial text mode dimensions */
    curses_resize(ds);
}
