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
#include <locale.h>
#include <wchar.h>
#include <iconv.h>

#include "qapi/error.h"
#include "qemu/module.h"
#include "ui/console.h"
#include "ui/input.h"
#include "sysemu/sysemu.h"

#if defined(__APPLE__) || defined(__OpenBSD__)
#define _XOPEN_SOURCE_EXTENDED 1
#endif

/* KEY_EVENT is defined in wincon.h and in curses.h. Avoid redefinition. */
#undef KEY_EVENT
#include <curses.h>
#undef KEY_EVENT

#define FONT_HEIGHT 16
#define FONT_WIDTH 8

enum maybe_keycode {
    CURSES_KEYCODE,
    CURSES_CHAR,
    CURSES_CHAR_OR_KEYCODE,
};

static DisplayChangeListener *dcl;
static console_ch_t *screen;
static WINDOW *screenpad = NULL;
static int width, height, gwidth, gheight, invalidate;
static int px, py, sminx, sminy, smaxx, smaxy;

static const char *font_charset = "CP437";
static cchar_t *vga_to_curses;

static void curses_update(DisplayChangeListener *dcl,
                          int x, int y, int w, int h)
{
    console_ch_t *line;
    g_autofree cchar_t *curses_line = g_new(cchar_t, width);
    wchar_t wch[CCHARW_MAX];
    attr_t attrs;
    short colors;
    int ret;

    line = screen + y * width;
    for (h += y; y < h; y ++, line += width) {
        for (x = 0; x < width; x++) {
            chtype ch = line[x] & A_CHARTEXT;
            chtype at = line[x] & A_ATTRIBUTES;
            short color_pair = PAIR_NUMBER(line[x]);

            ret = getcchar(&vga_to_curses[ch], wch, &attrs, &colors, NULL);
            if (ret == ERR || wch[0] == 0) {
                wch[0] = ch;
                wch[1] = 0;
            }
            setcchar(&curses_line[x], wch, at, color_pair, NULL);
        }
        mvwadd_wchnstr(screenpad, y, 0, curses_line, width);
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

static wint_t console_getch(enum maybe_keycode *maybe_keycode)
{
    wint_t ret;
    switch (get_wch(&ret)) {
    case KEY_CODE_YES:
        *maybe_keycode = CURSES_KEYCODE;
        break;
    case OK:
        *maybe_keycode = CURSES_CHAR;
        break;
    case ERR:
        ret = -1;
        break;
    default:
        abort();
    }
    return ret;
}

static int curses2foo(const int _curses2foo[], const int _curseskey2foo[],
                      int chr, enum maybe_keycode maybe_keycode)
{
    int ret = -1;
    if (maybe_keycode == CURSES_CHAR) {
        if (chr < CURSES_CHARS) {
            ret = _curses2foo[chr];
        }
    } else {
        if (chr < CURSES_KEYS) {
            ret = _curseskey2foo[chr];
        }
        if (ret == -1 && maybe_keycode == CURSES_CHAR_OR_KEYCODE &&
            chr < CURSES_CHARS) {
            ret = _curses2foo[chr];
        }
    }
    return ret;
}

#define curses2keycode(chr, maybe_keycode) \
    curses2foo(_curses2keycode, _curseskey2keycode, chr, maybe_keycode)
#define curses2keysym(chr, maybe_keycode) \
    curses2foo(_curses2keysym, _curseskey2keysym, chr, maybe_keycode)
#define curses2qemu(chr, maybe_keycode) \
    curses2foo(_curses2qemu, _curseskey2qemu, chr, maybe_keycode)

static void curses_refresh(DisplayChangeListener *dcl)
{
    int chr, keysym, keycode, keycode_alt;
    enum maybe_keycode maybe_keycode = CURSES_KEYCODE;

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
        chr = console_getch(&maybe_keycode);

        if (chr == -1)
            break;

#ifdef KEY_RESIZE
        /* this shouldn't occur when we use a custom SIGWINCH handler */
        if (maybe_keycode != CURSES_CHAR && chr == KEY_RESIZE) {
            clear();
            refresh();
            curses_calc_pad();
            curses_update(dcl, 0, 0, width, height);
            continue;
        }
#endif

        keycode = curses2keycode(chr, maybe_keycode);
        keycode_alt = 0;

        /* alt or esc key */
        if (keycode == 1) {
            enum maybe_keycode next_maybe_keycode = CURSES_KEYCODE;
            int nextchr = console_getch(&next_maybe_keycode);

            if (nextchr != -1) {
                chr = nextchr;
                maybe_keycode = next_maybe_keycode;
                keycode_alt = ALT;
                keycode = curses2keycode(chr, maybe_keycode);

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
            keysym = curses2keysym(chr, maybe_keycode);

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
                                      NULL, false);
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
            keysym = curses2qemu(chr, maybe_keycode);
            if (keysym == -1)
                keysym = chr;

            qemu_text_console_put_keysym(NULL, keysym);
        }
    }
}

static void curses_atexit(void)
{
    endwin();
    g_free(vga_to_curses);
    g_free(screen);
}

/*
 * In the following:
 * - fch is the font glyph number
 * - uch is the unicode value
 * - wch is the wchar_t value (may not be unicode, e.g. on BSD/solaris)
 * - mbch is the native local-dependent multibyte representation
 */

/* Setup wchar glyph for one UCS-2 char */
static void convert_ucs(unsigned char fch, uint16_t uch, iconv_t conv)
{
    char mbch[MB_LEN_MAX];
    wchar_t wch[2];
    char *puch, *pmbch;
    size_t such, smbch;
    mbstate_t ps;

    puch = (char *) &uch;
    pmbch = (char *) mbch;
    such = sizeof(uch);
    smbch = sizeof(mbch);

    if (iconv(conv, &puch, &such, &pmbch, &smbch) == (size_t) -1) {
        fprintf(stderr, "Could not convert 0x%04x "
                        "from UCS-2 to a multibyte character: %s\n",
                        uch, strerror(errno));
        return;
    }

    memset(&ps, 0, sizeof(ps));
    if (mbrtowc(&wch[0], mbch, sizeof(mbch) - smbch, &ps) == -1) {
        fprintf(stderr, "Could not convert 0x%04x "
                        "from a multibyte character to wchar_t: %s\n",
                        uch, strerror(errno));
        return;
    }

    wch[1] = 0;
    setcchar(&vga_to_curses[fch], wch, 0, 0, NULL);
}

/* Setup wchar glyph for one font character */
static void convert_font(unsigned char fch, iconv_t conv)
{
    char mbch[MB_LEN_MAX];
    wchar_t wch[2];
    char *pfch, *pmbch;
    size_t sfch, smbch;
    mbstate_t ps;

    pfch = (char *) &fch;
    pmbch = (char *) &mbch;
    sfch = sizeof(fch);
    smbch = sizeof(mbch);

    if (iconv(conv, &pfch, &sfch, &pmbch, &smbch) == (size_t) -1) {
        fprintf(stderr, "Could not convert font glyph 0x%02x "
                        "from %s to a multibyte character: %s\n",
                        fch, font_charset, strerror(errno));
        return;
    }

    memset(&ps, 0, sizeof(ps));
    if (mbrtowc(&wch[0], mbch, sizeof(mbch) - smbch, &ps) == -1) {
        fprintf(stderr, "Could not convert font glyph 0x%02x "
                        "from a multibyte character to wchar_t: %s\n",
                        fch, strerror(errno));
        return;
    }

    wch[1] = 0;
    setcchar(&vga_to_curses[fch], wch, 0, 0, NULL);
}

/* Convert one wchar to UCS-2 */
static uint16_t get_ucs(wchar_t wch, iconv_t conv)
{
    char mbch[MB_LEN_MAX];
    uint16_t uch;
    char *pmbch, *puch;
    size_t smbch, such;
    mbstate_t ps;
    int ret;

    memset(&ps, 0, sizeof(ps));
    ret = wcrtomb(mbch, wch, &ps);
    if (ret == -1) {
        fprintf(stderr, "Could not convert 0x%04lx "
                        "from wchar_t to a multibyte character: %s\n",
                        (unsigned long)wch, strerror(errno));
        return 0xFFFD;
    }

    pmbch = (char *) mbch;
    puch = (char *) &uch;
    smbch = ret;
    such = sizeof(uch);

    if (iconv(conv, &pmbch, &smbch, &puch, &such) == (size_t) -1) {
        fprintf(stderr, "Could not convert 0x%04lx "
                        "from a multibyte character to UCS-2 : %s\n",
                        (unsigned long)wch, strerror(errno));
        return 0xFFFD;
    }

    return uch;
}

/*
 * Setup mapping for vga to curses line graphics.
 */
static void font_setup(void)
{
    iconv_t ucs2_to_nativecharset;
    iconv_t nativecharset_to_ucs2;
    iconv_t font_conv;
    int i;
    g_autofree gchar *local_codeset = g_get_codeset();

    /*
     * Control characters are normally non-printable, but VGA does have
     * well-known glyphs for them.
     */
    static const uint16_t control_characters[0x20] = {
      0x0020,
      0x263a,
      0x263b,
      0x2665,
      0x2666,
      0x2663,
      0x2660,
      0x2022,
      0x25d8,
      0x25cb,
      0x25d9,
      0x2642,
      0x2640,
      0x266a,
      0x266b,
      0x263c,
      0x25ba,
      0x25c4,
      0x2195,
      0x203c,
      0x00b6,
      0x00a7,
      0x25ac,
      0x21a8,
      0x2191,
      0x2193,
      0x2192,
      0x2190,
      0x221f,
      0x2194,
      0x25b2,
      0x25bc
    };

    ucs2_to_nativecharset = iconv_open(local_codeset, "UCS-2");
    if (ucs2_to_nativecharset == (iconv_t) -1) {
        fprintf(stderr, "Could not convert font glyphs from UCS-2: '%s'\n",
                        strerror(errno));
        exit(1);
    }

    nativecharset_to_ucs2 = iconv_open("UCS-2", local_codeset);
    if (nativecharset_to_ucs2 == (iconv_t) -1) {
        iconv_close(ucs2_to_nativecharset);
        fprintf(stderr, "Could not convert font glyphs to UCS-2: '%s'\n",
                        strerror(errno));
        exit(1);
    }

    font_conv = iconv_open(local_codeset, font_charset);
    if (font_conv == (iconv_t) -1) {
        iconv_close(ucs2_to_nativecharset);
        iconv_close(nativecharset_to_ucs2);
        fprintf(stderr, "Could not convert font glyphs from %s: '%s'\n",
                        font_charset, strerror(errno));
        exit(1);
    }

    /* Control characters */
    for (i = 0; i <= 0x1F; i++) {
        convert_ucs(i, control_characters[i], ucs2_to_nativecharset);
    }

    for (i = 0x20; i <= 0xFF; i++) {
        convert_font(i, font_conv);
    }

    /* DEL */
    convert_ucs(0x7F, 0x2302, ucs2_to_nativecharset);

    if (strcmp(local_codeset, "UTF-8")) {
        /* Non-Unicode capable, use termcap equivalents for those available */
        for (i = 0; i <= 0xFF; i++) {
            wchar_t wch[CCHARW_MAX];
            attr_t attr;
            short color;
            int ret;

            ret = getcchar(&vga_to_curses[i], wch, &attr, &color, NULL);
            if (ret == ERR)
                continue;

            switch (get_ucs(wch[0], nativecharset_to_ucs2)) {
            case 0x00a3:
                vga_to_curses[i] = *WACS_STERLING;
                break;
            case 0x2591:
                vga_to_curses[i] = *WACS_BOARD;
                break;
            case 0x2592:
                vga_to_curses[i] = *WACS_CKBOARD;
                break;
            case 0x2502:
                vga_to_curses[i] = *WACS_VLINE;
                break;
            case 0x2524:
                vga_to_curses[i] = *WACS_RTEE;
                break;
            case 0x2510:
                vga_to_curses[i] = *WACS_URCORNER;
                break;
            case 0x2514:
                vga_to_curses[i] = *WACS_LLCORNER;
                break;
            case 0x2534:
                vga_to_curses[i] = *WACS_BTEE;
                break;
            case 0x252c:
                vga_to_curses[i] = *WACS_TTEE;
                break;
            case 0x251c:
                vga_to_curses[i] = *WACS_LTEE;
                break;
            case 0x2500:
                vga_to_curses[i] = *WACS_HLINE;
                break;
            case 0x253c:
                vga_to_curses[i] = *WACS_PLUS;
                break;
            case 0x256c:
                vga_to_curses[i] = *WACS_LANTERN;
                break;
            case 0x256a:
                vga_to_curses[i] = *WACS_NEQUAL;
                break;
            case 0x2518:
                vga_to_curses[i] = *WACS_LRCORNER;
                break;
            case 0x250c:
                vga_to_curses[i] = *WACS_ULCORNER;
                break;
            case 0x2588:
                vga_to_curses[i] = *WACS_BLOCK;
                break;
            case 0x03c0:
                vga_to_curses[i] = *WACS_PI;
                break;
            case 0x00b1:
                vga_to_curses[i] = *WACS_PLMINUS;
                break;
            case 0x2265:
                vga_to_curses[i] = *WACS_GEQUAL;
                break;
            case 0x2264:
                vga_to_curses[i] = *WACS_LEQUAL;
                break;
            case 0x00b0:
                vga_to_curses[i] = *WACS_DEGREE;
                break;
            case 0x25a0:
                vga_to_curses[i] = *WACS_BULLET;
                break;
            case 0x2666:
                vga_to_curses[i] = *WACS_DIAMOND;
                break;
            case 0x2192:
                vga_to_curses[i] = *WACS_RARROW;
                break;
            case 0x2190:
                vga_to_curses[i] = *WACS_LARROW;
                break;
            case 0x2191:
                vga_to_curses[i] = *WACS_UARROW;
                break;
            case 0x2193:
                vga_to_curses[i] = *WACS_DARROW;
                break;
            case 0x23ba:
                vga_to_curses[i] = *WACS_S1;
                break;
            case 0x23bb:
                vga_to_curses[i] = *WACS_S3;
                break;
            case 0x23bc:
                vga_to_curses[i] = *WACS_S7;
                break;
            case 0x23bd:
                vga_to_curses[i] = *WACS_S9;
                break;
            }
        }
    }
    iconv_close(ucs2_to_nativecharset);
    iconv_close(nativecharset_to_ucs2);
    iconv_close(font_conv);
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
    set_escdelay(25);

    /* Make color pair to match color format (3bits bg:3bits fg) */
    for (i = 0; i < 64; i++) {
        init_pair(i, colour_default[i & 7], colour_default[i >> 3]);
    }
    /* Set default color for more than 64 for safety. */
    for (i = 64; i < COLOR_PAIRS; i++) {
        init_pair(i, COLOR_WHITE, COLOR_BLACK);
    }

    font_setup();
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

    setlocale(LC_CTYPE, "");
    if (opts->u.curses.charset) {
        font_charset = opts->u.curses.charset;
    }
    screen = g_new0(console_ch_t, 160 * 100);
    vga_to_curses = g_new0(cchar_t, 256);
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
