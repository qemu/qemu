
#include "keymaps.h"

static const name2keysym_t name2keysym[]={
/* ascii */
    { "space",                0x020},
    { "exclam",               0x021},
    { "quotedbl",             0x022},
    { "numbersign",           0x023},
    { "dollar",               0x024},
    { "percent",              0x025},
    { "ampersand",            0x026},
    { "apostrophe",           0x027},
    { "parenleft",            0x028},
    { "parenright",           0x029},
    { "asterisk",             0x02a},
    { "plus",                 0x02b},
    { "comma",                0x02c},
    { "minus",                0x02d},
    { "period",               0x02e},
    { "slash",                0x02f},
    { "0",                    0x030},
    { "1",                    0x031},
    { "2",                    0x032},
    { "3",                    0x033},
    { "4",                    0x034},
    { "5",                    0x035},
    { "6",                    0x036},
    { "7",                    0x037},
    { "8",                    0x038},
    { "9",                    0x039},
    { "colon",                0x03a},
    { "semicolon",            0x03b},
    { "less",                 0x03c},
    { "equal",                0x03d},
    { "greater",              0x03e},
    { "question",             0x03f},
    { "at",                   0x040},
    { "A",                    0x041},
    { "B",                    0x042},
    { "C",                    0x043},
    { "D",                    0x044},
    { "E",                    0x045},
    { "F",                    0x046},
    { "G",                    0x047},
    { "H",                    0x048},
    { "I",                    0x049},
    { "J",                    0x04a},
    { "K",                    0x04b},
    { "L",                    0x04c},
    { "M",                    0x04d},
    { "N",                    0x04e},
    { "O",                    0x04f},
    { "P",                    0x050},
    { "Q",                    0x051},
    { "R",                    0x052},
    { "S",                    0x053},
    { "T",                    0x054},
    { "U",                    0x055},
    { "V",                    0x056},
    { "W",                    0x057},
    { "X",                    0x058},
    { "Y",                    0x059},
    { "Z",                    0x05a},
    { "bracketleft",          0x05b},
    { "backslash",            0x05c},
    { "bracketright",         0x05d},
    { "asciicircum",          0x05e},
    { "underscore",           0x05f},
    { "grave",                0x060},
    { "a",                    0x061},
    { "b",                    0x062},
    { "c",                    0x063},
    { "d",                    0x064},
    { "e",                    0x065},
    { "f",                    0x066},
    { "g",                    0x067},
    { "h",                    0x068},
    { "i",                    0x069},
    { "j",                    0x06a},
    { "k",                    0x06b},
    { "l",                    0x06c},
    { "m",                    0x06d},
    { "n",                    0x06e},
    { "o",                    0x06f},
    { "p",                    0x070},
    { "q",                    0x071},
    { "r",                    0x072},
    { "s",                    0x073},
    { "t",                    0x074},
    { "u",                    0x075},
    { "v",                    0x076},
    { "w",                    0x077},
    { "x",                    0x078},
    { "y",                    0x079},
    { "z",                    0x07a},
    { "braceleft",            0x07b},
    { "bar",                  0x07c},
    { "braceright",           0x07d},
    { "asciitilde",           0x07e},

/* latin 1 extensions */
{ "nobreakspace",         0x0a0},
{ "exclamdown",           0x0a1},
{ "cent",         	  0x0a2},
{ "sterling",             0x0a3},
{ "currency",             0x0a4},
{ "yen",                  0x0a5},
{ "brokenbar",            0x0a6},
{ "section",              0x0a7},
{ "diaeresis",            0x0a8},
{ "copyright",            0x0a9},
{ "ordfeminine",          0x0aa},
{ "guillemotleft",        0x0ab},
{ "notsign",              0x0ac},
{ "hyphen",               0x0ad},
{ "registered",           0x0ae},
{ "macron",               0x0af},
{ "degree",               0x0b0},
{ "plusminus",            0x0b1},
{ "twosuperior",          0x0b2},
{ "threesuperior",        0x0b3},
{ "acute",                0x0b4},
{ "mu",                   0x0b5},
{ "paragraph",            0x0b6},
{ "periodcentered",       0x0b7},
{ "cedilla",              0x0b8},
{ "onesuperior",          0x0b9},
{ "masculine",            0x0ba},
{ "guillemotright",       0x0bb},
{ "onequarter",           0x0bc},
{ "onehalf",              0x0bd},
{ "threequarters",        0x0be},
{ "questiondown",         0x0bf},
{ "Agrave",               0x0c0},
{ "Aacute",               0x0c1},
{ "Acircumflex",          0x0c2},
{ "Atilde",               0x0c3},
{ "Adiaeresis",           0x0c4},
{ "Aring",                0x0c5},
{ "AE",                   0x0c6},
{ "Ccedilla",             0x0c7},
{ "Egrave",               0x0c8},
{ "Eacute",               0x0c9},
{ "Ecircumflex",          0x0ca},
{ "Ediaeresis",           0x0cb},
{ "Igrave",               0x0cc},
{ "Iacute",               0x0cd},
{ "Icircumflex",          0x0ce},
{ "Idiaeresis",           0x0cf},
{ "ETH",                  0x0d0},
{ "Eth",                  0x0d0},
{ "Ntilde",               0x0d1},
{ "Ograve",               0x0d2},
{ "Oacute",               0x0d3},
{ "Ocircumflex",          0x0d4},
{ "Otilde",               0x0d5},
{ "Odiaeresis",           0x0d6},
{ "multiply",             0x0d7},
{ "Ooblique",             0x0d8},
{ "Oslash",               0x0d8},
{ "Ugrave",               0x0d9},
{ "Uacute",               0x0da},
{ "Ucircumflex",          0x0db},
{ "Udiaeresis",           0x0dc},
{ "Yacute",               0x0dd},
{ "THORN",                0x0de},
{ "Thorn",                0x0de},
{ "ssharp",               0x0df},
{ "agrave",               0x0e0},
{ "aacute",               0x0e1},
{ "acircumflex",          0x0e2},
{ "atilde",               0x0e3},
{ "adiaeresis",           0x0e4},
{ "aring",                0x0e5},
{ "ae",                   0x0e6},
{ "ccedilla",             0x0e7},
{ "egrave",               0x0e8},
{ "eacute",               0x0e9},
{ "ecircumflex",          0x0ea},
{ "ediaeresis",           0x0eb},
{ "igrave",               0x0ec},
{ "iacute",               0x0ed},
{ "icircumflex",          0x0ee},
{ "idiaeresis",           0x0ef},
{ "eth",                  0x0f0},
{ "ntilde",               0x0f1},
{ "ograve",               0x0f2},
{ "oacute",               0x0f3},
{ "ocircumflex",          0x0f4},
{ "otilde",               0x0f5},
{ "odiaeresis",           0x0f6},
{ "division",             0x0f7},
{ "oslash",               0x0f8},
{ "ooblique",             0x0f8},
{ "ugrave",               0x0f9},
{ "uacute",               0x0fa},
{ "ucircumflex",          0x0fb},
{ "udiaeresis",           0x0fc},
{ "yacute",               0x0fd},
{ "thorn",                0x0fe},
{ "ydiaeresis",           0x0ff},
{"EuroSign", 0x20ac},  /* XK_EuroSign */

    /* modifiers */
{"ISO_Level3_Shift", 0xfe03}, /* XK_ISO_Level3_Shift */
{"Control_L", 0xffe3}, /* XK_Control_L */
{"Control_R", 0xffe4}, /* XK_Control_R */
{"Alt_L", 0xffe9},     /* XK_Alt_L */
{"Alt_R", 0xffea},     /* XK_Alt_R */
{"Caps_Lock", 0xffe5}, /* XK_Caps_Lock */
{"Meta_L", 0xffe7},    /* XK_Meta_L */
{"Meta_R", 0xffe8},    /* XK_Meta_R */
{"Shift_L", 0xffe1},   /* XK_Shift_L */
{"Shift_R", 0xffe2},   /* XK_Shift_R */
{"Super_L", 0xffeb},   /* XK_Super_L */
{"Super_R", 0xffec},   /* XK_Super_R */

    /* special keys */
{"BackSpace", 0xff08}, /* XK_BackSpace */
{"Tab", 0xff09},       /* XK_Tab */
{"Return", 0xff0d},    /* XK_Return */
{"Right", 0xff53},     /* XK_Right */
{"Left", 0xff51},      /* XK_Left */
{"Up", 0xff52},        /* XK_Up */
{"Down", 0xff54},      /* XK_Down */
{"Page_Down", 0xff56}, /* XK_Page_Down */
{"Page_Up", 0xff55},   /* XK_Page_Up */
{"Insert", 0xff63},    /* XK_Insert */
{"Delete", 0xffff},    /* XK_Delete */
{"Home", 0xff50},      /* XK_Home */
{"End", 0xff57},       /* XK_End */
{"Scroll_Lock", 0xff14}, /* XK_Scroll_Lock */
{"KP_Home", 0xff95},
{"KP_Left", 0xff96},
{"KP_Up", 0xff97},
{"KP_Right", 0xff98},
{"KP_Down", 0xff99},
{"KP_Prior", 0xff9a},
{"KP_Page_Up", 0xff9a},
{"KP_Next", 0xff9b},
{"KP_Page_Down", 0xff9b},
{"KP_End", 0xff9c},
{"KP_Begin", 0xff9d},
{"KP_Insert", 0xff9e},
{"KP_Delete", 0xff9f},
{"F1", 0xffbe},        /* XK_F1 */
{"F2", 0xffbf},        /* XK_F2 */
{"F3", 0xffc0},        /* XK_F3 */
{"F4", 0xffc1},        /* XK_F4 */
{"F5", 0xffc2},        /* XK_F5 */
{"F6", 0xffc3},        /* XK_F6 */
{"F7", 0xffc4},        /* XK_F7 */
{"F8", 0xffc5},        /* XK_F8 */
{"F9", 0xffc6},        /* XK_F9 */
{"F10", 0xffc7},       /* XK_F10 */
{"F11", 0xffc8},       /* XK_F11 */
{"F12", 0xffc9},       /* XK_F12 */
{"F13", 0xffca},       /* XK_F13 */
{"F14", 0xffcb},       /* XK_F14 */
{"F15", 0xffcc},       /* XK_F15 */
{"Sys_Req", 0xff15},   /* XK_Sys_Req */
{"KP_0", 0xffb0},      /* XK_KP_0 */
{"KP_1", 0xffb1},      /* XK_KP_1 */
{"KP_2", 0xffb2},      /* XK_KP_2 */
{"KP_3", 0xffb3},      /* XK_KP_3 */
{"KP_4", 0xffb4},      /* XK_KP_4 */
{"KP_5", 0xffb5},      /* XK_KP_5 */
{"KP_6", 0xffb6},      /* XK_KP_6 */
{"KP_7", 0xffb7},      /* XK_KP_7 */
{"KP_8", 0xffb8},      /* XK_KP_8 */
{"KP_9", 0xffb9},      /* XK_KP_9 */
{"KP_Add", 0xffab},    /* XK_KP_Add */
{"KP_Separator", 0xffac},/* XK_KP_Separator */
{"KP_Decimal", 0xffae},  /* XK_KP_Decimal */
{"KP_Divide", 0xffaf},   /* XK_KP_Divide */
{"KP_Enter", 0xff8d},    /* XK_KP_Enter */
{"KP_Equal", 0xffbd},    /* XK_KP_Equal */
{"KP_Multiply", 0xffaa}, /* XK_KP_Multiply */
{"KP_Subtract", 0xffad}, /* XK_KP_Subtract */
{"help", 0xff6a},        /* XK_Help */
{"Menu", 0xff67},        /* XK_Menu */
{"Print", 0xff61},       /* XK_Print */
{"Mode_switch", 0xff7e}, /* XK_Mode_switch */
{"Num_Lock", 0xff7f},    /* XK_Num_Lock */
{"Pause", 0xff13},       /* XK_Pause */
{"Escape", 0xff1b},      /* XK_Escape */

/* dead keys */
{"dead_grave", 0xfe50}, /* XK_dead_grave */
{"dead_acute", 0xfe51}, /* XK_dead_acute */
{"dead_circumflex", 0xfe52}, /* XK_dead_circumflex */
{"dead_tilde", 0xfe53}, /* XK_dead_tilde */
{"dead_macron", 0xfe54}, /* XK_dead_macron */
{"dead_breve", 0xfe55}, /* XK_dead_breve */
{"dead_abovedot", 0xfe56}, /* XK_dead_abovedot */
{"dead_diaeresis", 0xfe57}, /* XK_dead_diaeresis */
{"dead_abovering", 0xfe58}, /* XK_dead_abovering */
{"dead_doubleacute", 0xfe59}, /* XK_dead_doubleacute */
{"dead_caron", 0xfe5a}, /* XK_dead_caron */
{"dead_cedilla", 0xfe5b}, /* XK_dead_cedilla */
{"dead_ogonek", 0xfe5c}, /* XK_dead_ogonek */
{"dead_iota", 0xfe5d}, /* XK_dead_iota */
{"dead_voiced_sound", 0xfe5e}, /* XK_dead_voiced_sound */
{"dead_semivoiced_sound", 0xfe5f}, /* XK_dead_semivoiced_sound */
{"dead_belowdot", 0xfe60}, /* XK_dead_belowdot */
{"dead_hook", 0xfe61}, /* XK_dead_hook */
{"dead_horn", 0xfe62}, /* XK_dead_horn */


    /* localized keys */
{"BackApostrophe", 0xff21},
{"Muhenkan", 0xff22},
{"Katakana", 0xff27},
{"Hankaku", 0xff29},
{"Zenkaku_Hankaku", 0xff2a},
{"Henkan_Mode_Real", 0xff23},
{"Henkan_Mode_Ultra", 0xff3e},
{"backslash_ja", 0xffa5},
{"Katakana_Real", 0xff25},
{"Eisu_toggle", 0xff30},

{0,0},
};
