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
#include "qemu/osdep.h"

#ifndef _WIN32
#include <sys/ioctl.h>
#include <termios.h>
#endif

#include "qapi/error.h"
#include "qemu-common.h"
#include "ui/console.h"
#include "ui/input.h"
#include "sysemu/sysemu.h"

/* KEY_EVENT is defined in wincon.h and in curses.h. Avoid redefinition. */
#undef KEY_EVENT
#include <curses.h>
#undef KEY_EVENT

#define FONT_HEIGHT 16
#define FONT_WIDTH 8

static DisplayChangeListener *dcl;
static console_ch_t screen[160 * 100];
static WINDOW *screenpad = NULL;
static int width, height, gwidth, gheight, invalidate;
static int px, py, sminx, sminy, smaxx, smaxy;

static chtype vga_to_curses[256];

static void curses_update(DisplayChangeListener *dcl,
                          int x, int y, int w, int h)
{
    console_ch_t *line;
    chtype curses_line[width];

    line = screen + y * width;
    for (h += y; y < h; y ++, line += width) {
        for (x = 0; x < width; x++) {
            chtype ch = line[x] & 0xff;
            chtype at = line[x] & ~0xff;
            if (vga_to_curses[ch]) {
                ch = vga_to_curses[ch];
            }
            curses_line[x] = ch | at;
        }
        mvwaddchnstr(screenpad, y, 0, curses_line, width);
    }

    pnoutrefresh(screenpad, py, px, sminy, sminx, smaxy - 1, smaxx - 1);
    refresh();
}

static void curses_calc_pad(void)
{
    if (qemu_console_is_fixedsize(NULL)) {
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

static void curses_resize(DisplayChangeListener *dcl,
                          int width, int height)
{
    if (width == gwidth && height == gheight) {
        return;
    }

    gwidth = width;
    gheight = height;

    curses_calc_pad();
}

#if !defined(_WIN32) && defined(SIGWINCH) && defined(KEY_RESIZE)
static volatile sig_atomic_t got_sigwinch;
static void curses_winch_check(void)
{
    struct winsize {
        unsigned short ws_row;
        unsigned short ws_col;
        unsigned short ws_xpixel;   /* unused */
        unsigned short ws_ypixel;   /* unused */
    } ws;

    if (!got_sigwinch) {
        return;
    }
    got_sigwinch = false;

    if (ioctl(1, TIOCGWINSZ, &ws) == -1) {
        return;
    }

    resize_term(ws.ws_row, ws.ws_col);
    invalidate = 1;
}

static void curses_winch_handler(int signum)
{
    got_sigwinch = true;
}

static void curses_winch_init(void)
{
    struct sigaction old, winch = {
        .sa_handler  = curses_winch_handler,
    };
    sigaction(SIGWINCH, &winch, &old);
}
#else
static void curses_winch_check(void) {}
static void curses_winch_init(void) {}
#endif

static void curses_cursor_position(DisplayChangeListener *dcl,
                                   int x, int y)
{
    if (x >= 0) {
        x = sminx + x - px;
        y = sminy + y - py;

        if (x >= 0 && y >= 0 && x < COLS && y < LINES) {
            move(y, x);
            curs_set(1);
            /* it seems that curs_set(1) must always be called before
             * curs_set(2) for the latter to have effect */
            if (!qemu_console_is_graphic(NULL)) {
                curs_set(2);
            }
            return;
        }
    }

    curs_set(0);
}

/* generic keyboard conversion */

#include "curses_keys.h"

static kbd_layout_t *kbd_layout = NULL;

static void curses_refresh(DisplayChangeListener *dcl)
{
    int chr, keysym, keycode, keycode_alt;

    curses_winch_check();

    if (invalidate) {
        clear();
        refresh();
        curses_calc_pad();
        graphic_hw_invalidate(NULL);
        invalidate = 0;
    }

    graphic_hw_text_update(NULL, screen);

    while (1) {
        /* while there are any pending key strokes to process */
        chr = getch();

        if (chr == ERR)
            break;

#ifdef KEY_RESIZE
        /* this shouldn't occur when we use a custom SIGWINCH handler */
        if (chr == KEY_RESIZE) {
            clear();
            refresh();
            curses_calc_pad();
            curses_update(dcl, 0, 0, width, height);
            continue;
        }
#endif

        keycode = curses2keycode[chr];
        keycode_alt = 0;

        /* alt key */
        if (keycode == 1) {
            int nextchr = getch();

            if (nextchr != ERR) {
                chr = nextchr;
                keycode_alt = ALT;
                keycode = curses2keycode[chr];

                if (keycode != -1) {
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
        }

        if (kbd_layout) {
            keysym = -1;
            if (chr < CURSES_KEYS)
                keysym = curses2keysym[chr];

            if (keysym == -1) {
                if (chr < ' ') {
                    keysym = chr + '@';
                    if (keysym >= 'A' && keysym <= 'Z')
                        keysym += 'a' - 'A';
                    keysym |= KEYSYM_CNTRL;
                } else
                    keysym = chr;
            }

            keycode = keysym2scancode(kbd_layout, keysym & KEYSYM_MASK,
                                      false, false, false);
            if (keycode == 0)
                continue;

            keycode |= (keysym & ~KEYSYM_MASK) >> 16;
            keycode |= keycode_alt;
        }

        if (keycode == -1)
            continue;

        if (qemu_console_is_graphic(NULL)) {
            /* since terminals don't know about key press and release
             * events, we need to emit both for each key received */
            if (keycode & SHIFT) {
                qemu_input_event_send_key_number(NULL, SHIFT_CODE, true);
                qemu_input_event_send_key_delay(0);
            }
            if (keycode & CNTRL) {
                qemu_input_event_send_key_number(NULL, CNTRL_CODE, true);
                qemu_input_event_send_key_delay(0);
            }
            if (keycode & ALT) {
                qemu_input_event_send_key_number(NULL, ALT_CODE, true);
                qemu_input_event_send_key_delay(0);
            }
            if (keycode & ALTGR) {
                qemu_input_event_send_key_number(NULL, GREY | ALT_CODE, true);
                qemu_input_event_send_key_delay(0);
            }

            qemu_input_event_send_key_number(NULL, keycode & KEY_MASK, true);
            qemu_input_event_send_key_delay(0);
            qemu_input_event_send_key_number(NULL, keycode & KEY_MASK, false);
            qemu_input_event_send_key_delay(0);

            if (keycode & ALTGR) {
                qemu_input_event_send_key_number(NULL, GREY | ALT_CODE, false);
                qemu_input_event_send_key_delay(0);
            }
            if (keycode & ALT) {
                qemu_input_event_send_key_number(NULL, ALT_CODE, false);
                qemu_input_event_send_key_delay(0);
            }
            if (keycode & CNTRL) {
                qemu_input_event_send_key_number(NULL, CNTRL_CODE, false);
                qemu_input_event_send_key_delay(0);
            }
            if (keycode & SHIFT) {
                qemu_input_event_send_key_number(NULL, SHIFT_CODE, false);
                qemu_input_event_send_key_delay(0);
            }
        } else {
            keysym = -1;
            if (chr < CURSES_KEYS) {
                keysym = curses2qemu[chr];
            }
            if (keysym == -1)
                keysym = chr;

            kbd_put_keysym(keysym);
        }
    }
}

static void curses_atexit(void)
{
    endwin();
}

static void curses_setup(void)
{
    int i, colour_default[8] = {
        [QEMU_COLOR_BLACK]   = COLOR_BLACK,
        [QEMU_COLOR_BLUE]    = COLOR_BLUE,
        [QEMU_COLOR_GREEN]   = COLOR_GREEN,
        [QEMU_COLOR_CYAN]    = COLOR_CYAN,
        [QEMU_COLOR_RED]     = COLOR_RED,
        [QEMU_COLOR_MAGENTA] = COLOR_MAGENTA,
        [QEMU_COLOR_YELLOW]  = COLOR_YELLOW,
        [QEMU_COLOR_WHITE]   = COLOR_WHITE,
    };

    /* input as raw as possible, let everything be interpreted
     * by the guest system */
    initscr(); noecho(); intrflush(stdscr, FALSE);
    nodelay(stdscr, TRUE); nonl(); keypad(stdscr, TRUE);
    start_color(); raw(); scrollok(stdscr, FALSE);

    /* Make color pair to match color format (3bits bg:3bits fg) */
    for (i = 0; i < 64; i++) {
        init_pair(i, colour_default[i & 7], colour_default[i >> 3]);
    }
    /* Set default color for more than 64 for safety. */
    for (i = 64; i < COLOR_PAIRS; i++) {
        init_pair(i, COLOR_WHITE, COLOR_BLACK);
    }

    /*
     * Setup mapping for vga to curses line graphics.
     * FIXME: for better font, have to use ncursesw and setlocale()
     */
#if 0
    /* FIXME: map from where? */
    ACS_S1;
    ACS_S3;
    ACS_S7;
    ACS_S9;
#endif
    /* ACS_* is not constant. So, we can't initialize statically. */
    vga_to_curses['\0'] = ' ';
    vga_to_curses[0x04] = ACS_DIAMOND;
    vga_to_curses[0x18] = ACS_UARROW;
    vga_to_curses[0x19] = ACS_DARROW;
    vga_to_curses[0x1a] = ACS_RARROW;
    vga_to_curses[0x1b] = ACS_LARROW;
    vga_to_curses[0x9c] = ACS_STERLING;
    vga_to_curses[0xb0] = ACS_BOARD;
    vga_to_curses[0xb1] = ACS_CKBOARD;
    vga_to_curses[0xb3] = ACS_VLINE;
    vga_to_curses[0xb4] = ACS_RTEE;
    vga_to_curses[0xbf] = ACS_URCORNER;
    vga_to_curses[0xc0] = ACS_LLCORNER;
    vga_to_curses[0xc1] = ACS_BTEE;
    vga_to_curses[0xc2] = ACS_TTEE;
    vga_to_curses[0xc3] = ACS_LTEE;
    vga_to_curses[0xc4] = ACS_HLINE;
    vga_to_curses[0xc5] = ACS_PLUS;
    vga_to_curses[0xce] = ACS_LANTERN;
    vga_to_curses[0xd8] = ACS_NEQUAL;
    vga_to_curses[0xd9] = ACS_LRCORNER;
    vga_to_curses[0xda] = ACS_ULCORNER;
    vga_to_curses[0xdb] = ACS_BLOCK;
    vga_to_curses[0xe3] = ACS_PI;
    vga_to_curses[0xf1] = ACS_PLMINUS;
    vga_to_curses[0xf2] = ACS_GEQUAL;
    vga_to_curses[0xf3] = ACS_LEQUAL;
    vga_to_curses[0xf8] = ACS_DEGREE;
    vga_to_curses[0xfe] = ACS_BULLET;
}

static void curses_keyboard_setup(void)
{
#if defined(__APPLE__)
    /* always use generic keymaps */
    if (!keyboard_layout)
        keyboard_layout = "en-us";
#endif
    if(keyboard_layout) {
        kbd_layout = init_keyboard_layout(name2keysym, keyboard_layout,
                                          &error_fatal);
    }
}

static const DisplayChangeListenerOps dcl_ops = {
    .dpy_name        = "curses",
    .dpy_text_update = curses_update,
    .dpy_text_resize = curses_resize,
    .dpy_refresh     = curses_refresh,
    .dpy_text_cursor = curses_cursor_position,
};

static void curses_display_init(DisplayState *ds, DisplayOptions *opts)
{
#ifndef _WIN32
    if (!isatty(1)) {
        fprintf(stderr, "We need a terminal output\n");
        exit(1);
    }
#endif

    curses_setup();
    curses_keyboard_setup();
    atexit(curses_atexit);

    curses_winch_init();

    dcl = g_new0(DisplayChangeListener, 1);
    dcl->ops = &dcl_ops;
    register_displaychangelistener(dcl);

    invalidate = 1;
}

static QemuDisplay qemu_display_curses = {
    .type       = DISPLAY_TYPE_CURSES,
    .init       = curses_display_init,
};

static void register_curses(void)
{
    qemu_display_register(&qemu_display_curses);
}

type_init(register_curses);
