/*
 * Keycode and keysyms conversion tables for curses
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

#include <curses.h>
#include "keymaps.h"


#define KEY_RELEASE         0x80
#define KEY_MASK            0x7f
#define GREY_CODE           0xe0
#define GREY                SCANCODE_GREY
#define SHIFT_CODE          0x2a
#define SHIFT               SCANCODE_SHIFT
#define CNTRL_CODE          0x1d
#define CNTRL               SCANCODE_CTRL
#define ALT_CODE            0x38
#define ALT                 SCANCODE_ALT
#define ALTGR               SCANCODE_ALTGR

#define KEYSYM_MASK         0x0ffffff
#define KEYSYM_SHIFT        (SCANCODE_SHIFT << 16)
#define KEYSYM_CNTRL        (SCANCODE_CTRL  << 16)
#define KEYSYM_ALT          (SCANCODE_ALT   << 16)
#define KEYSYM_ALTGR        (SCANCODE_ALTGR << 16)

/* curses won't detect a Control + Alt + 1, so use Alt + 1 */
#define QEMU_KEY_CONSOLE0   (2 | ALT)   /* (curses2keycode['1'] | ALT) */

#define CURSES_KEYS         KEY_MAX     /* KEY_MAX defined in <curses.h> */

static const int curses2keysym[CURSES_KEYS] = {
    [0 ... (CURSES_KEYS - 1)] = -1,

    [0x7f] = KEY_BACKSPACE,
    ['\r'] = KEY_ENTER,
    ['\n'] = KEY_ENTER,
    [27] = 27,
    [KEY_BTAB] = '\t' | KEYSYM_SHIFT,
};

static const int curses2keycode[CURSES_KEYS] = {
    [0 ... (CURSES_KEYS - 1)] = -1,

    [0x01b] = 1, /* Escape */
    ['1'] = 2,
    ['2'] = 3,
    ['3'] = 4,
    ['4'] = 5,
    ['5'] = 6,
    ['6'] = 7,
    ['7'] = 8,
    ['8'] = 9,
    ['9'] = 10,
    ['0'] = 11,
    ['-'] = 12,
    ['='] = 13,
    [0x07f] = 14, /* Backspace */
    [KEY_BACKSPACE] = 14, /* Backspace */

    ['\t'] = 15, /* Tab */
    ['q'] = 16,
    ['w'] = 17,
    ['e'] = 18,
    ['r'] = 19,
    ['t'] = 20,
    ['y'] = 21,
    ['u'] = 22,
    ['i'] = 23,
    ['o'] = 24,
    ['p'] = 25,
    ['['] = 26,
    [']'] = 27,
    ['\n'] = 28, /* Return */
    ['\r'] = 28, /* Return */
    [KEY_ENTER] = 28, /* Return */

    ['a'] = 30,
    ['s'] = 31,
    ['d'] = 32,
    ['f'] = 33,
    ['g'] = 34,
    ['h'] = 35,
    ['j'] = 36,
    ['k'] = 37,
    ['l'] = 38,
    [';'] = 39,
    ['\''] = 40, /* Single quote */
    ['`'] = 41,
    ['\\'] = 43, /* Backslash */

    ['z'] = 44,
    ['x'] = 45,
    ['c'] = 46,
    ['v'] = 47,
    ['b'] = 48,
    ['n'] = 49,
    ['m'] = 50,
    [','] = 51,
    ['.'] = 52,
    ['/'] = 53,

    [' '] = 57,

    [KEY_F(1)] = 59, /* Function Key 1 */
    [KEY_F(2)] = 60, /* Function Key 2 */
    [KEY_F(3)] = 61, /* Function Key 3 */
    [KEY_F(4)] = 62, /* Function Key 4 */
    [KEY_F(5)] = 63, /* Function Key 5 */
    [KEY_F(6)] = 64, /* Function Key 6 */
    [KEY_F(7)] = 65, /* Function Key 7 */
    [KEY_F(8)] = 66, /* Function Key 8 */
    [KEY_F(9)] = 67, /* Function Key 9 */
    [KEY_F(10)] = 68, /* Function Key 10 */
    [KEY_F(11)] = 87, /* Function Key 11 */
    [KEY_F(12)] = 88, /* Function Key 12 */

    [KEY_HOME] = 71 | GREY, /* Home */
    [KEY_UP] = 72 | GREY, /* Up Arrow */
    [KEY_PPAGE] = 73 | GREY, /* Page Up */
    [KEY_LEFT] = 75 | GREY, /* Left Arrow */
    [KEY_RIGHT] = 77 | GREY, /* Right Arrow */
    [KEY_END] = 79 | GREY, /* End */
    [KEY_DOWN] = 80 | GREY, /* Down Arrow */
    [KEY_NPAGE] = 81 | GREY, /* Page Down */
    [KEY_IC] = 82 | GREY, /* Insert */
    [KEY_DC] = 83 | GREY, /* Delete */

    ['!'] = 2 | SHIFT,
    ['@'] = 3 | SHIFT,
    ['#'] = 4 | SHIFT,
    ['$'] = 5 | SHIFT,
    ['%'] = 6 | SHIFT,
    ['^'] = 7 | SHIFT,
    ['&'] = 8 | SHIFT,
    ['*'] = 9 | SHIFT,
    ['('] = 10 | SHIFT,
    [')'] = 11 | SHIFT,
    ['_'] = 12 | SHIFT,
    ['+'] = 13 | SHIFT,

    [KEY_BTAB] = 15 | SHIFT, /* Shift + Tab */
    ['Q'] = 16 | SHIFT,
    ['W'] = 17 | SHIFT,
    ['E'] = 18 | SHIFT,
    ['R'] = 19 | SHIFT,
    ['T'] = 20 | SHIFT,
    ['Y'] = 21 | SHIFT,
    ['U'] = 22 | SHIFT,
    ['I'] = 23 | SHIFT,
    ['O'] = 24 | SHIFT,
    ['P'] = 25 | SHIFT,
    ['{'] = 26 | SHIFT,
    ['}'] = 27 | SHIFT,

    ['A'] = 30 | SHIFT,
    ['S'] = 31 | SHIFT,
    ['D'] = 32 | SHIFT,
    ['F'] = 33 | SHIFT,
    ['G'] = 34 | SHIFT,
    ['H'] = 35 | SHIFT,
    ['J'] = 36 | SHIFT,
    ['K'] = 37 | SHIFT,
    ['L'] = 38 | SHIFT,
    [':'] = 39 | SHIFT,
    ['"'] = 40 | SHIFT,
    ['~'] = 41 | SHIFT,
    ['|'] = 43 | SHIFT,

    ['Z'] = 44 | SHIFT,
    ['X'] = 45 | SHIFT,
    ['C'] = 46 | SHIFT,
    ['V'] = 47 | SHIFT,
    ['B'] = 48 | SHIFT,
    ['N'] = 49 | SHIFT,
    ['M'] = 50 | SHIFT,
    ['<'] = 51 | SHIFT,
    ['>'] = 52 | SHIFT,
    ['?'] = 53 | SHIFT,

    [KEY_F(13)] = 59 | SHIFT, /* Shift + Function Key 1 */
    [KEY_F(14)] = 60 | SHIFT, /* Shift + Function Key 2 */
    [KEY_F(15)] = 61 | SHIFT, /* Shift + Function Key 3 */
    [KEY_F(16)] = 62 | SHIFT, /* Shift + Function Key 4 */
    [KEY_F(17)] = 63 | SHIFT, /* Shift + Function Key 5 */
    [KEY_F(18)] = 64 | SHIFT, /* Shift + Function Key 6 */
    [KEY_F(19)] = 65 | SHIFT, /* Shift + Function Key 7 */
    [KEY_F(20)] = 66 | SHIFT, /* Shift + Function Key 8 */
    [KEY_F(21)] = 67 | SHIFT, /* Shift + Function Key 9 */
    [KEY_F(22)] = 68 | SHIFT, /* Shift + Function Key 10 */
    [KEY_F(23)] = 69 | SHIFT, /* Shift + Function Key 11 */
    [KEY_F(24)] = 70 | SHIFT, /* Shift + Function Key 12 */

    ['Q' - '@'] = 16 | CNTRL, /* Control + q */
    ['W' - '@'] = 17 | CNTRL, /* Control + w */
    ['E' - '@'] = 18 | CNTRL, /* Control + e */
    ['R' - '@'] = 19 | CNTRL, /* Control + r */
    ['T' - '@'] = 20 | CNTRL, /* Control + t */
    ['Y' - '@'] = 21 | CNTRL, /* Control + y */
    ['U' - '@'] = 22 | CNTRL, /* Control + u */
    /* Control + i collides with Tab */
    ['O' - '@'] = 24 | CNTRL, /* Control + o */
    ['P' - '@'] = 25 | CNTRL, /* Control + p */

    ['A' - '@'] = 30 | CNTRL, /* Control + a */
    ['S' - '@'] = 31 | CNTRL, /* Control + s */
    ['D' - '@'] = 32 | CNTRL, /* Control + d */
    ['F' - '@'] = 33 | CNTRL, /* Control + f */
    ['G' - '@'] = 34 | CNTRL, /* Control + g */
    ['H' - '@'] = 35 | CNTRL, /* Control + h */
    /* Control + j collides with Return */
    ['K' - '@'] = 37 | CNTRL, /* Control + k */
    ['L' - '@'] = 38 | CNTRL, /* Control + l */

    ['Z' - '@'] = 44 | CNTRL, /* Control + z */
    ['X' - '@'] = 45 | CNTRL, /* Control + x */
    ['C' - '@'] = 46 | CNTRL, /* Control + c */
    ['V' - '@'] = 47 | CNTRL, /* Control + v */
    ['B' - '@'] = 48 | CNTRL, /* Control + b */
    ['N' - '@'] = 49 | CNTRL, /* Control + n */
    /* Control + m collides with the keycode for Enter */

};

static const int curses2qemu[CURSES_KEYS] = {
    [0 ... (CURSES_KEYS - 1)] = -1,

    ['\n'] = '\n',
    ['\r'] = '\n',

    [0x07f] = QEMU_KEY_BACKSPACE,

    [KEY_DOWN] = QEMU_KEY_DOWN,
    [KEY_UP] = QEMU_KEY_UP,
    [KEY_LEFT] = QEMU_KEY_LEFT,
    [KEY_RIGHT] = QEMU_KEY_RIGHT,
    [KEY_HOME] = QEMU_KEY_HOME,
    [KEY_BACKSPACE] = QEMU_KEY_BACKSPACE,

    [KEY_DC] = QEMU_KEY_DELETE,
    [KEY_NPAGE] = QEMU_KEY_PAGEDOWN,
    [KEY_PPAGE] = QEMU_KEY_PAGEUP,
    [KEY_ENTER] = '\n',
    [KEY_END] = QEMU_KEY_END,

};

static const name2keysym_t name2keysym[] = {
    /* Plain ASCII */
    { "space", 0x020 },
    { "exclam", 0x021 },
    { "quotedbl", 0x022 },
    { "numbersign", 0x023 },
    { "dollar", 0x024 },
    { "percent", 0x025 },
    { "ampersand", 0x026 },
    { "apostrophe", 0x027 },
    { "parenleft", 0x028 },
    { "parenright", 0x029 },
    { "asterisk", 0x02a },
    { "plus", 0x02b },
    { "comma", 0x02c },
    { "minus", 0x02d },
    { "period", 0x02e },
    { "slash", 0x02f },
    { "0", 0x030 },
    { "1", 0x031 },
    { "2", 0x032 },
    { "3", 0x033 },
    { "4", 0x034 },
    { "5", 0x035 },
    { "6", 0x036 },
    { "7", 0x037 },
    { "8", 0x038 },
    { "9", 0x039 },
    { "colon", 0x03a },
    { "semicolon", 0x03b },
    { "less", 0x03c },
    { "equal", 0x03d },
    { "greater", 0x03e },
    { "question", 0x03f },
    { "at", 0x040 },
    { "A", 0x041 },
    { "B", 0x042 },
    { "C", 0x043 },
    { "D", 0x044 },
    { "E", 0x045 },
    { "F", 0x046 },
    { "G", 0x047 },
    { "H", 0x048 },
    { "I", 0x049 },
    { "J", 0x04a },
    { "K", 0x04b },
    { "L", 0x04c },
    { "M", 0x04d },
    { "N", 0x04e },
    { "O", 0x04f },
    { "P", 0x050 },
    { "Q", 0x051 },
    { "R", 0x052 },
    { "S", 0x053 },
    { "T", 0x054 },
    { "U", 0x055 },
    { "V", 0x056 },
    { "W", 0x057 },
    { "X", 0x058 },
    { "Y", 0x059 },
    { "Z", 0x05a },
    { "bracketleft", 0x05b },
    { "backslash", 0x05c },
    { "bracketright", 0x05d },
    { "asciicircum", 0x05e },
    { "underscore", 0x05f },
    { "grave", 0x060 },
    { "a", 0x061 },
    { "b", 0x062 },
    { "c", 0x063 },
    { "d", 0x064 },
    { "e", 0x065 },
    { "f", 0x066 },
    { "g", 0x067 },
    { "h", 0x068 },
    { "i", 0x069 },
    { "j", 0x06a },
    { "k", 0x06b },
    { "l", 0x06c },
    { "m", 0x06d },
    { "n", 0x06e },
    { "o", 0x06f },
    { "p", 0x070 },
    { "q", 0x071 },
    { "r", 0x072 },
    { "s", 0x073 },
    { "t", 0x074 },
    { "u", 0x075 },
    { "v", 0x076 },
    { "w", 0x077 },
    { "x", 0x078 },
    { "y", 0x079 },
    { "z", 0x07a },
    { "braceleft", 0x07b },
    { "bar", 0x07c },
    { "braceright", 0x07d },
    { "asciitilde", 0x07e },

    /* Latin-1 extensions */
    { "nobreakspace", 0x0a0 },
    { "exclamdown", 0x0a1 },
    { "cent", 0x0a2 },
    { "sterling", 0x0a3 },
    { "currency", 0x0a4 },
    { "yen", 0x0a5 },
    { "brokenbar", 0x0a6 },
    { "section", 0x0a7 },
    { "diaeresis", 0x0a8 },
    { "copyright", 0x0a9 },
    { "ordfeminine", 0x0aa },
    { "guillemotleft", 0x0ab },
    { "notsign", 0x0ac },
    { "hyphen", 0x0ad },
    { "registered", 0x0ae },
    { "macron", 0x0af },
    { "degree", 0x0b0 },
    { "plusminus", 0x0b1 },
    { "twosuperior", 0x0b2 },
    { "threesuperior", 0x0b3 },
    { "acute", 0x0b4 },
    { "mu", 0x0b5 },
    { "paragraph", 0x0b6 },
    { "periodcentered", 0x0b7 },
    { "cedilla", 0x0b8 },
    { "onesuperior", 0x0b9 },
    { "masculine", 0x0ba },
    { "guillemotright", 0x0bb },
    { "onequarter", 0x0bc },
    { "onehalf", 0x0bd },
    { "threequarters", 0x0be },
    { "questiondown", 0x0bf },
    { "Agrave", 0x0c0 },
    { "Aacute", 0x0c1 },
    { "Acircumflex", 0x0c2 },
    { "Atilde", 0x0c3 },
    { "Adiaeresis", 0x0c4 },
    { "Aring", 0x0c5 },
    { "AE", 0x0c6 },
    { "Ccedilla", 0x0c7 },
    { "Egrave", 0x0c8 },
    { "Eacute", 0x0c9 },
    { "Ecircumflex", 0x0ca },
    { "Ediaeresis", 0x0cb },
    { "Igrave", 0x0cc },
    { "Iacute", 0x0cd },
    { "Icircumflex", 0x0ce },
    { "Idiaeresis", 0x0cf },
    { "ETH", 0x0d0 },
    { "Eth", 0x0d0 },
    { "Ntilde", 0x0d1 },
    { "Ograve", 0x0d2 },
    { "Oacute", 0x0d3 },
    { "Ocircumflex", 0x0d4 },
    { "Otilde", 0x0d5 },
    { "Odiaeresis", 0x0d6 },
    { "multiply", 0x0d7 },
    { "Ooblique", 0x0d8 },
    { "Oslash", 0x0d8 },
    { "Ugrave", 0x0d9 },
    { "Uacute", 0x0da },
    { "Ucircumflex", 0x0db },
    { "Udiaeresis", 0x0dc },
    { "Yacute", 0x0dd },
    { "THORN", 0x0de },
    { "Thorn", 0x0de },
    { "ssharp", 0x0df },
    { "agrave", 0x0e0 },
    { "aacute", 0x0e1 },
    { "acircumflex", 0x0e2 },
    { "atilde", 0x0e3 },
    { "adiaeresis", 0x0e4 },
    { "aring", 0x0e5 },
    { "ae", 0x0e6 },
    { "ccedilla", 0x0e7 },
    { "egrave", 0x0e8 },
    { "eacute", 0x0e9 },
    { "ecircumflex", 0x0ea },
    { "ediaeresis", 0x0eb },
    { "igrave", 0x0ec },
    { "iacute", 0x0ed },
    { "icircumflex", 0x0ee },
    { "idiaeresis", 0x0ef },
    { "eth", 0x0f0 },
    { "ntilde", 0x0f1 },
    { "ograve", 0x0f2 },
    { "oacute", 0x0f3 },
    { "ocircumflex", 0x0f4 },
    { "otilde", 0x0f5 },
    { "odiaeresis", 0x0f6 },
    { "division", 0x0f7 },
    { "oslash", 0x0f8 },
    { "ooblique", 0x0f8 },
    { "ugrave", 0x0f9 },
    { "uacute", 0x0fa },
    { "ucircumflex", 0x0fb },
    { "udiaeresis", 0x0fc },
    { "yacute", 0x0fd },
    { "thorn", 0x0fe },
    { "ydiaeresis", 0x0ff },

    /* Special keys */
    { "BackSpace", KEY_BACKSPACE },
    { "Tab", '\t' },
    { "Return", KEY_ENTER },
    { "Right", KEY_RIGHT },
    { "Left", KEY_LEFT },
    { "Up", KEY_UP },
    { "Down", KEY_DOWN },
    { "Page_Down", KEY_NPAGE },
    { "Page_Up", KEY_PPAGE },
    { "Insert", KEY_IC },
    { "Delete", KEY_DC },
    { "Home", KEY_HOME },
    { "End", KEY_END },
    { "F1", KEY_F(1) },
    { "F2", KEY_F(2) },
    { "F3", KEY_F(3) },
    { "F4", KEY_F(4) },
    { "F5", KEY_F(5) },
    { "F6", KEY_F(6) },
    { "F7", KEY_F(7) },
    { "F8", KEY_F(8) },
    { "F9", KEY_F(9) },
    { "F10", KEY_F(10) },
    { "F11", KEY_F(11) },
    { "F12", KEY_F(12) },
    { "F13", KEY_F(13) },
    { "F14", KEY_F(14) },
    { "F15", KEY_F(15) },
    { "F16", KEY_F(16) },
    { "F17", KEY_F(17) },
    { "F18", KEY_F(18) },
    { "F19", KEY_F(19) },
    { "F20", KEY_F(20) },
    { "F21", KEY_F(21) },
    { "F22", KEY_F(22) },
    { "F23", KEY_F(23) },
    { "F24", KEY_F(24) },
    { "Escape", 27 },

    { NULL, 0 },
};
