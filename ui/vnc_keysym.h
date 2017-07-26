
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

/* latin 2 - Polish national characters */
{ "eogonek",              0x1ea},
{ "Eogonek",              0x1ca},
{ "aogonek",              0x1b1},
{ "Aogonek",              0x1a1},
{ "sacute",               0x1b6},
{ "Sacute",               0x1a6},
{ "lstroke",              0x1b3},
{ "Lstroke",              0x1a3},
{ "zabovedot",            0x1bf},
{ "Zabovedot",            0x1af},
{ "zacute",               0x1bc},
{ "Zacute",               0x1ac},
{ "Odoubleacute",         0x1d5},
{ "Udoubleacute",         0x1db},
{ "cacute",               0x1e6},
{ "Cacute",               0x1c6},
{ "nacute",               0x1f1},
{ "Nacute",               0x1d1},
{ "odoubleacute",         0x1f5},
{ "udoubleacute",         0x1fb},

/* Czech national characters */
{ "ecaron",               0x1ec},
{ "scaron",               0x1b9},
{ "ccaron",               0x1e8},
{ "rcaron",               0x1f8},
{ "zcaron",               0x1be},
{ "uring",                0x1f9},

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
{"Next", 0xff56},
{"Page_Down", 0xff56}, /* XK_Page_Down */
{"Prior", 0xff55},
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

{"abovedot",                      0x01ff},  /* U+02D9 DOT ABOVE */
{"amacron",                       0x03e0},  /* U+0101 LATIN SMALL LETTER A WITH MACRON */
{"Amacron",                       0x03c0},  /* U+0100 LATIN CAPITAL LETTER A WITH MACRON */
{"Arabic_ain",                    0x05d9},  /* U+0639 ARABIC LETTER AIN */
{"Arabic_alef",                   0x05c7},  /* U+0627 ARABIC LETTER ALEF */
{"Arabic_alefmaksura",            0x05e9},  /* U+0649 ARABIC LETTER ALEF MAKSURA */
{"Arabic_beh",                    0x05c8},  /* U+0628 ARABIC LETTER BEH */
{"Arabic_comma",                  0x05ac},  /* U+060C ARABIC COMMA */
{"Arabic_dad",                    0x05d6},  /* U+0636 ARABIC LETTER DAD */
{"Arabic_dal",                    0x05cf},  /* U+062F ARABIC LETTER DAL */
{"Arabic_damma",                  0x05ef},  /* U+064F ARABIC DAMMA */
{"Arabic_dammatan",               0x05ec},  /* U+064C ARABIC DAMMATAN */
{"Arabic_fatha",                  0x05ee},  /* U+064E ARABIC FATHA */
{"Arabic_fathatan",               0x05eb},  /* U+064B ARABIC FATHATAN */
{"Arabic_feh",                    0x05e1},  /* U+0641 ARABIC LETTER FEH */
{"Arabic_ghain",                  0x05da},  /* U+063A ARABIC LETTER GHAIN */
{"Arabic_ha",                     0x05e7},  /* U+0647 ARABIC LETTER HEH */
{"Arabic_hah",                    0x05cd},  /* U+062D ARABIC LETTER HAH */
{"Arabic_hamza",                  0x05c1},  /* U+0621 ARABIC LETTER HAMZA */
{"Arabic_hamzaonalef",            0x05c3},  /* U+0623 ARABIC LETTER ALEF WITH HAMZA ABOVE */
{"Arabic_hamzaonwaw",             0x05c4},  /* U+0624 ARABIC LETTER WAW WITH HAMZA ABOVE */
{"Arabic_hamzaonyeh",             0x05c6},  /* U+0626 ARABIC LETTER YEH WITH HAMZA ABOVE */
{"Arabic_hamzaunderalef",         0x05c5},  /* U+0625 ARABIC LETTER ALEF WITH HAMZA BELOW */
{"Arabic_jeem",                   0x05cc},  /* U+062C ARABIC LETTER JEEM */
{"Arabic_kaf",                    0x05e3},  /* U+0643 ARABIC LETTER KAF */
{"Arabic_kasra",                  0x05f0},  /* U+0650 ARABIC KASRA */
{"Arabic_kasratan",               0x05ed},  /* U+064D ARABIC KASRATAN */
{"Arabic_khah",                   0x05ce},  /* U+062E ARABIC LETTER KHAH */
{"Arabic_lam",                    0x05e4},  /* U+0644 ARABIC LETTER LAM */
{"Arabic_maddaonalef",            0x05c2},  /* U+0622 ARABIC LETTER ALEF WITH MADDA ABOVE */
{"Arabic_meem",                   0x05e5},  /* U+0645 ARABIC LETTER MEEM */
{"Arabic_noon",                   0x05e6},  /* U+0646 ARABIC LETTER NOON */
{"Arabic_qaf",                    0x05e2},  /* U+0642 ARABIC LETTER QAF */
{"Arabic_question_mark",          0x05bf},  /* U+061F ARABIC QUESTION MARK */
{"Arabic_ra",                     0x05d1},  /* U+0631 ARABIC LETTER REH */
{"Arabic_sad",                    0x05d5},  /* U+0635 ARABIC LETTER SAD */
{"Arabic_seen",                   0x05d3},  /* U+0633 ARABIC LETTER SEEN */
{"Arabic_semicolon",              0x05bb},  /* U+061B ARABIC SEMICOLON */
{"Arabic_shadda",                 0x05f1},  /* U+0651 ARABIC SHADDA */
{"Arabic_sheen",                  0x05d4},  /* U+0634 ARABIC LETTER SHEEN */
{"Arabic_sukun",                  0x05f2},  /* U+0652 ARABIC SUKUN */
{"Arabic_tah",                    0x05d7},  /* U+0637 ARABIC LETTER TAH */
{"Arabic_tatweel",                0x05e0},  /* U+0640 ARABIC TATWEEL */
{"Arabic_teh",                    0x05ca},  /* U+062A ARABIC LETTER TEH */
{"Arabic_tehmarbuta",             0x05c9},  /* U+0629 ARABIC LETTER TEH MARBUTA */
{"Arabic_thal",                   0x05d0},  /* U+0630 ARABIC LETTER THAL */
{"Arabic_theh",                   0x05cb},  /* U+062B ARABIC LETTER THEH */
{"Arabic_waw",                    0x05e8},  /* U+0648 ARABIC LETTER WAW */
{"Arabic_yeh",                    0x05ea},  /* U+064A ARABIC LETTER YEH */
{"Arabic_zah",                    0x05d8},  /* U+0638 ARABIC LETTER ZAH */
{"Arabic_zain",                   0x05d2},  /* U+0632 ARABIC LETTER ZAIN */
{"breve",                         0x01a2},  /* U+02D8 BREVE */
{"caron",                         0x01b7},  /* U+02C7 CARON */
{"Ccaron",                        0x01c8},  /* U+010C LATIN CAPITAL LETTER C WITH CARON */
{"numerosign",                    0x06b0},  /* U+2116 NUMERO SIGN */
{"Cyrillic_a",                    0x06c1},  /* U+0430 CYRILLIC SMALL LETTER A */
{"Cyrillic_A",                    0x06e1},  /* U+0410 CYRILLIC CAPITAL LETTER A */
{"Cyrillic_be",                   0x06c2},  /* U+0431 CYRILLIC SMALL LETTER BE */
{"Cyrillic_BE",                   0x06e2},  /* U+0411 CYRILLIC CAPITAL LETTER BE */
{"Cyrillic_che",                  0x06de},  /* U+0447 CYRILLIC SMALL LETTER CHE */
{"Cyrillic_CHE",                  0x06fe},  /* U+0427 CYRILLIC CAPITAL LETTER CHE */
{"Cyrillic_de",                   0x06c4},  /* U+0434 CYRILLIC SMALL LETTER DE */
{"Cyrillic_DE",                   0x06e4},  /* U+0414 CYRILLIC CAPITAL LETTER DE */
{"Cyrillic_dzhe",                 0x06af},  /* U+045F CYRILLIC SMALL LETTER DZHE */
{"Cyrillic_DZHE",                 0x06bf},  /* U+040F CYRILLIC CAPITAL LETTER DZHE */
{"Cyrillic_e",                    0x06dc},  /* U+044D CYRILLIC SMALL LETTER E */
{"Cyrillic_E",                    0x06fc},  /* U+042D CYRILLIC CAPITAL LETTER E */
{"Cyrillic_ef",                   0x06c6},  /* U+0444 CYRILLIC SMALL LETTER EF */
{"Cyrillic_EF",                   0x06e6},  /* U+0424 CYRILLIC CAPITAL LETTER EF */
{"Cyrillic_el",                   0x06cc},  /* U+043B CYRILLIC SMALL LETTER EL */
{"Cyrillic_EL",                   0x06ec},  /* U+041B CYRILLIC CAPITAL LETTER EL */
{"Cyrillic_em",                   0x06cd},  /* U+043C CYRILLIC SMALL LETTER EM */
{"Cyrillic_EM",                   0x06ed},  /* U+041C CYRILLIC CAPITAL LETTER EM */
{"Cyrillic_en",                   0x06ce},  /* U+043D CYRILLIC SMALL LETTER EN */
{"Cyrillic_EN",                   0x06ee},  /* U+041D CYRILLIC CAPITAL LETTER EN */
{"Cyrillic_er",                   0x06d2},  /* U+0440 CYRILLIC SMALL LETTER ER */
{"Cyrillic_ER",                   0x06f2},  /* U+0420 CYRILLIC CAPITAL LETTER ER */
{"Cyrillic_es",                   0x06d3},  /* U+0441 CYRILLIC SMALL LETTER ES */
{"Cyrillic_ES",                   0x06f3},  /* U+0421 CYRILLIC CAPITAL LETTER ES */
{"Cyrillic_ghe",                  0x06c7},  /* U+0433 CYRILLIC SMALL LETTER GHE */
{"Cyrillic_GHE",                  0x06e7},  /* U+0413 CYRILLIC CAPITAL LETTER GHE */
{"Cyrillic_ha",                   0x06c8},  /* U+0445 CYRILLIC SMALL LETTER HA */
{"Cyrillic_HA",                   0x06e8},  /* U+0425 CYRILLIC CAPITAL LETTER HA */
{"Cyrillic_hardsign",             0x06df},  /* U+044A CYRILLIC SMALL LETTER HARD SIGN */
{"Cyrillic_HARDSIGN",             0x06ff},  /* U+042A CYRILLIC CAPITAL LETTER HARD SIGN */
{"Cyrillic_i",                    0x06c9},  /* U+0438 CYRILLIC SMALL LETTER I */
{"Cyrillic_I",                    0x06e9},  /* U+0418 CYRILLIC CAPITAL LETTER I */
{"Cyrillic_ie",                   0x06c5},  /* U+0435 CYRILLIC SMALL LETTER IE */
{"Cyrillic_IE",                   0x06e5},  /* U+0415 CYRILLIC CAPITAL LETTER IE */
{"Cyrillic_io",                   0x06a3},  /* U+0451 CYRILLIC SMALL LETTER IO */
{"Cyrillic_IO",                   0x06b3},  /* U+0401 CYRILLIC CAPITAL LETTER IO */
{"Cyrillic_je",                   0x06a8},  /* U+0458 CYRILLIC SMALL LETTER JE */
{"Cyrillic_JE",                   0x06b8},  /* U+0408 CYRILLIC CAPITAL LETTER JE */
{"Cyrillic_ka",                   0x06cb},  /* U+043A CYRILLIC SMALL LETTER KA */
{"Cyrillic_KA",                   0x06eb},  /* U+041A CYRILLIC CAPITAL LETTER KA */
{"Cyrillic_lje",                  0x06a9},  /* U+0459 CYRILLIC SMALL LETTER LJE */
{"Cyrillic_LJE",                  0x06b9},  /* U+0409 CYRILLIC CAPITAL LETTER LJE */
{"Cyrillic_nje",                  0x06aa},  /* U+045A CYRILLIC SMALL LETTER NJE */
{"Cyrillic_NJE",                  0x06ba},  /* U+040A CYRILLIC CAPITAL LETTER NJE */
{"Cyrillic_o",                    0x06cf},  /* U+043E CYRILLIC SMALL LETTER O */
{"Cyrillic_O",                    0x06ef},  /* U+041E CYRILLIC CAPITAL LETTER O */
{"Cyrillic_pe",                   0x06d0},  /* U+043F CYRILLIC SMALL LETTER PE */
{"Cyrillic_PE",                   0x06f0},  /* U+041F CYRILLIC CAPITAL LETTER PE */
{"Cyrillic_sha",                  0x06db},  /* U+0448 CYRILLIC SMALL LETTER SHA */
{"Cyrillic_SHA",                  0x06fb},  /* U+0428 CYRILLIC CAPITAL LETTER SHA */
{"Cyrillic_shcha",                0x06dd},  /* U+0449 CYRILLIC SMALL LETTER SHCHA */
{"Cyrillic_SHCHA",                0x06fd},  /* U+0429 CYRILLIC CAPITAL LETTER SHCHA */
{"Cyrillic_shorti",               0x06ca},  /* U+0439 CYRILLIC SMALL LETTER SHORT I */
{"Cyrillic_SHORTI",               0x06ea},  /* U+0419 CYRILLIC CAPITAL LETTER SHORT I */
{"Cyrillic_softsign",             0x06d8},  /* U+044C CYRILLIC SMALL LETTER SOFT SIGN */
{"Cyrillic_SOFTSIGN",             0x06f8},  /* U+042C CYRILLIC CAPITAL LETTER SOFT SIGN */
{"Cyrillic_te",                   0x06d4},  /* U+0442 CYRILLIC SMALL LETTER TE */
{"Cyrillic_TE",                   0x06f4},  /* U+0422 CYRILLIC CAPITAL LETTER TE */
{"Cyrillic_tse",                  0x06c3},  /* U+0446 CYRILLIC SMALL LETTER TSE */
{"Cyrillic_TSE",                  0x06e3},  /* U+0426 CYRILLIC CAPITAL LETTER TSE */
{"Cyrillic_u",                    0x06d5},  /* U+0443 CYRILLIC SMALL LETTER U */
{"Cyrillic_U",                    0x06f5},  /* U+0423 CYRILLIC CAPITAL LETTER U */
{"Cyrillic_ve",                   0x06d7},  /* U+0432 CYRILLIC SMALL LETTER VE */
{"Cyrillic_VE",                   0x06f7},  /* U+0412 CYRILLIC CAPITAL LETTER VE */
{"Cyrillic_ya",                   0x06d1},  /* U+044F CYRILLIC SMALL LETTER YA */
{"Cyrillic_YA",                   0x06f1},  /* U+042F CYRILLIC CAPITAL LETTER YA */
{"Cyrillic_yeru",                 0x06d9},  /* U+044B CYRILLIC SMALL LETTER YERU */
{"Cyrillic_YERU",                 0x06f9},  /* U+042B CYRILLIC CAPITAL LETTER YERU */
{"Cyrillic_yu",                   0x06c0},  /* U+044E CYRILLIC SMALL LETTER YU */
{"Cyrillic_YU",                   0x06e0},  /* U+042E CYRILLIC CAPITAL LETTER YU */
{"Cyrillic_ze",                   0x06da},  /* U+0437 CYRILLIC SMALL LETTER ZE */
{"Cyrillic_ZE",                   0x06fa},  /* U+0417 CYRILLIC CAPITAL LETTER ZE */
{"Cyrillic_zhe",                  0x06d6},  /* U+0436 CYRILLIC SMALL LETTER ZHE */
{"Cyrillic_ZHE",                  0x06f6},  /* U+0416 CYRILLIC CAPITAL LETTER ZHE */
{"doubleacute",                   0x01bd},  /* U+02DD DOUBLE ACUTE ACCENT */
{"doublelowquotemark",            0x0afe},  /* U+201E DOUBLE LOW-9 QUOTATION MARK */
{"downarrow",                     0x08fe},  /* U+2193 DOWNWARDS ARROW */
{"dstroke",                       0x01f0},  /* U+0111 LATIN SMALL LETTER D WITH STROKE */
{"Dstroke",                       0x01d0},  /* U+0110 LATIN CAPITAL LETTER D WITH STROKE */
{"eabovedot",                     0x03ec},  /* U+0117 LATIN SMALL LETTER E WITH DOT ABOVE */
{"Eabovedot",                     0x03cc},  /* U+0116 LATIN CAPITAL LETTER E WITH DOT ABOVE */
{"emacron",                       0x03ba},  /* U+0113 LATIN SMALL LETTER E WITH MACRON */
{"Emacron",                       0x03aa},  /* U+0112 LATIN CAPITAL LETTER E WITH MACRON */
{"endash",                        0x0aaa},  /* U+2013 EN DASH */
{"eng",                           0x03bf},  /* U+014B LATIN SMALL LETTER ENG */
{"ENG",                           0x03bd},  /* U+014A LATIN CAPITAL LETTER ENG */
{"Execute",                       0xff62},  /* Execute, run, do */
{"F16",                           0xffcd},
{"F17",                           0xffce},
{"F18",                           0xffcf},
{"F19",                           0xffd0},
{"F20",                           0xffd1},
{"F21",                           0xffd2},
{"F22",                           0xffd3},
{"F23",                           0xffd4},
{"F24",                           0xffd5},
{"F25",                           0xffd6},
{"F26",                           0xffd7},
{"F27",                           0xffd8},
{"F28",                           0xffd9},
{"F29",                           0xffda},
{"F30",                           0xffdb},
{"F31",                           0xffdc},
{"F32",                           0xffdd},
{"F33",                           0xffde},
{"F34",                           0xffdf},
{"F35",                           0xffe0},
{"fiveeighths",                   0x0ac5},  /* U+215D VULGAR FRACTION FIVE EIGHTHS */
{"gbreve",                        0x02bb},  /* U+011F LATIN SMALL LETTER G WITH BREVE */
{"Gbreve",                        0x02ab},  /* U+011E LATIN CAPITAL LETTER G WITH BREVE */
{"gcedilla",                      0x03bb},  /* U+0123 LATIN SMALL LETTER G WITH CEDILLA */
{"Gcedilla",                      0x03ab},  /* U+0122 LATIN CAPITAL LETTER G WITH CEDILLA */
{"Greek_OMEGA",                   0x07d9},  /* U+03A9 GREEK CAPITAL LETTER OMEGA */
{"Henkan_Mode",                   0xff23},  /* Start/Stop Conversion */
{"horizconnector",                0x08a3},  /*(U+2500 BOX DRAWINGS LIGHT HORIZONTAL)*/
{"hstroke",                       0x02b1},  /* U+0127 LATIN SMALL LETTER H WITH STROKE */
{"Hstroke",                       0x02a1},  /* U+0126 LATIN CAPITAL LETTER H WITH STROKE */
{"Iabovedot",                     0x02a9},  /* U+0130 LATIN CAPITAL LETTER I WITH DOT ABOVE */
{"idotless",                      0x02b9},  /* U+0131 LATIN SMALL LETTER DOTLESS I */
{"imacron",                       0x03ef},  /* U+012B LATIN SMALL LETTER I WITH MACRON */
{"Imacron",                       0x03cf},  /* U+012A LATIN CAPITAL LETTER I WITH MACRON */
{"iogonek",                       0x03e7},  /* U+012F LATIN SMALL LETTER I WITH OGONEK */
{"Iogonek",                       0x03c7},  /* U+012E LATIN CAPITAL LETTER I WITH OGONEK */
{"ISO_First_Group",               0xfe0c},
{"ISO_Last_Group",                0xfe0e},
{"ISO_Next_Group",                0xfe08},
{"kana_a",                        0x04a7},  /* U+30A1 KATAKANA LETTER SMALL A */
{"kana_A",                        0x04b1},  /* U+30A2 KATAKANA LETTER A */
{"kana_CHI",                      0x04c1},  /* U+30C1 KATAKANA LETTER TI */
{"kana_closingbracket",           0x04a3},  /* U+300D RIGHT CORNER BRACKET */
{"kana_comma",                    0x04a4},  /* U+3001 IDEOGRAPHIC COMMA */
{"kana_conjunctive",              0x04a5},  /* U+30FB KATAKANA MIDDLE DOT */
{"kana_e",                        0x04aa},  /* U+30A7 KATAKANA LETTER SMALL E */
{"kana_E",                        0x04b4},  /* U+30A8 KATAKANA LETTER E */
{"kana_FU",                       0x04cc},  /* U+30D5 KATAKANA LETTER HU */
{"kana_fullstop",                 0x04a1},  /* U+3002 IDEOGRAPHIC FULL STOP */
{"kana_HA",                       0x04ca},  /* U+30CF KATAKANA LETTER HA */
{"kana_HE",                       0x04cd},  /* U+30D8 KATAKANA LETTER HE */
{"kana_HI",                       0x04cb},  /* U+30D2 KATAKANA LETTER HI */
{"kana_HO",                       0x04ce},  /* U+30DB KATAKANA LETTER HO */
{"kana_i",                        0x04a8},  /* U+30A3 KATAKANA LETTER SMALL I */
{"kana_I",                        0x04b2},  /* U+30A4 KATAKANA LETTER I */
{"kana_KA",                       0x04b6},  /* U+30AB KATAKANA LETTER KA */
{"kana_KE",                       0x04b9},  /* U+30B1 KATAKANA LETTER KE */
{"kana_KI",                       0x04b7},  /* U+30AD KATAKANA LETTER KI */
{"kana_KO",                       0x04ba},  /* U+30B3 KATAKANA LETTER KO */
{"kana_KU",                       0x04b8},  /* U+30AF KATAKANA LETTER KU */
{"kana_MA",                       0x04cf},  /* U+30DE KATAKANA LETTER MA */
{"kana_ME",                       0x04d2},  /* U+30E1 KATAKANA LETTER ME */
{"kana_MI",                       0x04d0},  /* U+30DF KATAKANA LETTER MI */
{"kana_MO",                       0x04d3},  /* U+30E2 KATAKANA LETTER MO */
{"kana_MU",                       0x04d1},  /* U+30E0 KATAKANA LETTER MU */
{"kana_N",                        0x04dd},  /* U+30F3 KATAKANA LETTER N */
{"kana_NA",                       0x04c5},  /* U+30CA KATAKANA LETTER NA */
{"kana_NE",                       0x04c8},  /* U+30CD KATAKANA LETTER NE */
{"kana_NI",                       0x04c6},  /* U+30CB KATAKANA LETTER NI */
{"kana_NO",                       0x04c9},  /* U+30CE KATAKANA LETTER NO */
{"kana_NU",                       0x04c7},  /* U+30CC KATAKANA LETTER NU */
{"kana_o",                        0x04ab},  /* U+30A9 KATAKANA LETTER SMALL O */
{"kana_O",                        0x04b5},  /* U+30AA KATAKANA LETTER O */
{"kana_openingbracket",           0x04a2},  /* U+300C LEFT CORNER BRACKET */
{"kana_RA",                       0x04d7},  /* U+30E9 KATAKANA LETTER RA */
{"kana_RE",                       0x04da},  /* U+30EC KATAKANA LETTER RE */
{"kana_RI",                       0x04d8},  /* U+30EA KATAKANA LETTER RI */
{"kana_RU",                       0x04d9},  /* U+30EB KATAKANA LETTER RU */
{"kana_SA",                       0x04bb},  /* U+30B5 KATAKANA LETTER SA */
{"kana_SE",                       0x04be},  /* U+30BB KATAKANA LETTER SE */
{"kana_SHI",                      0x04bc},  /* U+30B7 KATAKANA LETTER SI */
{"kana_SO",                       0x04bf},  /* U+30BD KATAKANA LETTER SO */
{"kana_SU",                       0x04bd},  /* U+30B9 KATAKANA LETTER SU */
{"kana_TA",                       0x04c0},  /* U+30BF KATAKANA LETTER TA */
{"kana_TE",                       0x04c3},  /* U+30C6 KATAKANA LETTER TE */
{"kana_TO",                       0x04c4},  /* U+30C8 KATAKANA LETTER TO */
{"kana_tsu",                      0x04af},  /* U+30C3 KATAKANA LETTER SMALL TU */
{"kana_TSU",                      0x04c2},  /* U+30C4 KATAKANA LETTER TU */
{"kana_u",                        0x04a9},  /* U+30A5 KATAKANA LETTER SMALL U */
{"kana_U",                        0x04b3},  /* U+30A6 KATAKANA LETTER U */
{"kana_WA",                       0x04dc},  /* U+30EF KATAKANA LETTER WA */
{"kana_WO",                       0x04a6},  /* U+30F2 KATAKANA LETTER WO */
{"kana_ya",                       0x04ac},  /* U+30E3 KATAKANA LETTER SMALL YA */
{"kana_YA",                       0x04d4},  /* U+30E4 KATAKANA LETTER YA */
{"kana_yo",                       0x04ae},  /* U+30E7 KATAKANA LETTER SMALL YO */
{"kana_YO",                       0x04d6},  /* U+30E8 KATAKANA LETTER YO */
{"kana_yu",                       0x04ad},  /* U+30E5 KATAKANA LETTER SMALL YU */
{"kana_YU",                       0x04d5},  /* U+30E6 KATAKANA LETTER YU */
{"Kanji",                         0xff21},  /* Kanji, Kanji convert */
{"kcedilla",                      0x03f3},  /* U+0137 LATIN SMALL LETTER K WITH CEDILLA */
{"Kcedilla",                      0x03d3},  /* U+0136 LATIN CAPITAL LETTER K WITH CEDILLA */
{"kra",                           0x03a2},  /* U+0138 LATIN SMALL LETTER KRA */
{"lcedilla",                      0x03b6},  /* U+013C LATIN SMALL LETTER L WITH CEDILLA */
{"Lcedilla",                      0x03a6},  /* U+013B LATIN CAPITAL LETTER L WITH CEDILLA */
{"leftarrow",                     0x08fb},  /* U+2190 LEFTWARDS ARROW */
{"leftdoublequotemark",           0x0ad2},  /* U+201C LEFT DOUBLE QUOTATION MARK */
{"Macedonia_dse",                 0x06a5},  /* U+0455 CYRILLIC SMALL LETTER DZE */
{"Macedonia_DSE",                 0x06b5},  /* U+0405 CYRILLIC CAPITAL LETTER DZE */
{"Macedonia_gje",                 0x06a2},  /* U+0453 CYRILLIC SMALL LETTER GJE */
{"Macedonia_GJE",                 0x06b2},  /* U+0403 CYRILLIC CAPITAL LETTER GJE */
{"Macedonia_kje",                 0x06ac},  /* U+045C CYRILLIC SMALL LETTER KJE */
{"Macedonia_KJE",                 0x06bc},  /* U+040C CYRILLIC CAPITAL LETTER KJE */
{"ncedilla",                      0x03f1},  /* U+0146 LATIN SMALL LETTER N WITH CEDILLA */
{"Ncedilla",                      0x03d1},  /* U+0145 LATIN CAPITAL LETTER N WITH CEDILLA */
{"oe",                            0x13bd},  /* U+0153 LATIN SMALL LIGATURE OE */
{"OE",                            0x13bc},  /* U+0152 LATIN CAPITAL LIGATURE OE */
{"ogonek",                        0x01b2},  /* U+02DB OGONEK */
{"omacron",                       0x03f2},  /* U+014D LATIN SMALL LETTER O WITH MACRON */
{"Omacron",                       0x03d2},  /* U+014C LATIN CAPITAL LETTER O WITH MACRON */
{"oneeighth",                     0x0ac3},  /* U+215B VULGAR FRACTION ONE EIGHTH */
{"rcedilla",                      0x03b3},  /* U+0157 LATIN SMALL LETTER R WITH CEDILLA */
{"Rcedilla",                      0x03a3},  /* U+0156 LATIN CAPITAL LETTER R WITH CEDILLA */
{"rightarrow",                    0x08fd},  /* U+2192 RIGHTWARDS ARROW */
{"rightdoublequotemark",          0x0ad3},  /* U+201D RIGHT DOUBLE QUOTATION MARK */
{"Scaron",                        0x01a9},  /* U+0160 LATIN CAPITAL LETTER S WITH CARON */
{"scedilla",                      0x01ba},  /* U+015F LATIN SMALL LETTER S WITH CEDILLA */
{"Scedilla",                      0x01aa},  /* U+015E LATIN CAPITAL LETTER S WITH CEDILLA */
{"semivoicedsound",               0x04df},  /* U+309C KATAKANA-HIRAGANA SEMI-VOICED SOUND MARK */
{"seveneighths",                  0x0ac6},  /* U+215E VULGAR FRACTION SEVEN EIGHTHS */
{"Thai_baht",                     0x0ddf},  /* U+0E3F THAI CURRENCY SYMBOL BAHT */
{"Thai_bobaimai",                 0x0dba},  /* U+0E1A THAI CHARACTER BO BAIMAI */
{"Thai_chochan",                  0x0da8},  /* U+0E08 THAI CHARACTER CHO CHAN */
{"Thai_chochang",                 0x0daa},  /* U+0E0A THAI CHARACTER CHO CHANG */
{"Thai_choching",                 0x0da9},  /* U+0E09 THAI CHARACTER CHO CHING */
{"Thai_chochoe",                  0x0dac},  /* U+0E0C THAI CHARACTER CHO CHOE */
{"Thai_dochada",                  0x0dae},  /* U+0E0E THAI CHARACTER DO CHADA */
{"Thai_dodek",                    0x0db4},  /* U+0E14 THAI CHARACTER DO DEK */
{"Thai_fofa",                     0x0dbd},  /* U+0E1D THAI CHARACTER FO FA */
{"Thai_fofan",                    0x0dbf},  /* U+0E1F THAI CHARACTER FO FAN */
{"Thai_hohip",                    0x0dcb},  /* U+0E2B THAI CHARACTER HO HIP */
{"Thai_honokhuk",                 0x0dce},  /* U+0E2E THAI CHARACTER HO NOKHUK */
{"Thai_khokhai",                  0x0da2},  /* U+0E02 THAI CHARACTER KHO KHAI */
{"Thai_khokhon",                  0x0da5},  /* U+0E05 THAI CHARACTER KHO KHON */
{"Thai_khokhuat",                 0x0da3},  /* U+0E03 THAI CHARACTER KHO KHUAT */
{"Thai_khokhwai",                 0x0da4},  /* U+0E04 THAI CHARACTER KHO KHWAI */
{"Thai_khorakhang",               0x0da6},  /* U+0E06 THAI CHARACTER KHO RAKHANG */
{"Thai_kokai",                    0x0da1},  /* U+0E01 THAI CHARACTER KO KAI */
{"Thai_lakkhangyao",              0x0de5},  /* U+0E45 THAI CHARACTER LAKKHANGYAO */
{"Thai_lekchet",                  0x0df7},  /* U+0E57 THAI DIGIT SEVEN */
{"Thai_lekha",                    0x0df5},  /* U+0E55 THAI DIGIT FIVE */
{"Thai_lekhok",                   0x0df6},  /* U+0E56 THAI DIGIT SIX */
{"Thai_lekkao",                   0x0df9},  /* U+0E59 THAI DIGIT NINE */
{"Thai_leknung",                  0x0df1},  /* U+0E51 THAI DIGIT ONE */
{"Thai_lekpaet",                  0x0df8},  /* U+0E58 THAI DIGIT EIGHT */
{"Thai_leksam",                   0x0df3},  /* U+0E53 THAI DIGIT THREE */
{"Thai_leksi",                    0x0df4},  /* U+0E54 THAI DIGIT FOUR */
{"Thai_leksong",                  0x0df2},  /* U+0E52 THAI DIGIT TWO */
{"Thai_leksun",                   0x0df0},  /* U+0E50 THAI DIGIT ZERO */
{"Thai_lochula",                  0x0dcc},  /* U+0E2C THAI CHARACTER LO CHULA */
{"Thai_loling",                   0x0dc5},  /* U+0E25 THAI CHARACTER LO LING */
{"Thai_lu",                       0x0dc6},  /* U+0E26 THAI CHARACTER LU */
{"Thai_maichattawa",              0x0deb},  /* U+0E4B THAI CHARACTER MAI CHATTAWA */
{"Thai_maiek",                    0x0de8},  /* U+0E48 THAI CHARACTER MAI EK */
{"Thai_maihanakat",               0x0dd1},  /* U+0E31 THAI CHARACTER MAI HAN-AKAT */
{"Thai_maitaikhu",                0x0de7},  /* U+0E47 THAI CHARACTER MAITAIKHU */
{"Thai_maitho",                   0x0de9},  /* U+0E49 THAI CHARACTER MAI THO */
{"Thai_maitri",                   0x0dea},  /* U+0E4A THAI CHARACTER MAI TRI */
{"Thai_maiyamok",                 0x0de6},  /* U+0E46 THAI CHARACTER MAIYAMOK */
{"Thai_moma",                     0x0dc1},  /* U+0E21 THAI CHARACTER MO MA */
{"Thai_ngongu",                   0x0da7},  /* U+0E07 THAI CHARACTER NGO NGU */
{"Thai_nikhahit",                 0x0ded},  /* U+0E4D THAI CHARACTER NIKHAHIT */
{"Thai_nonen",                    0x0db3},  /* U+0E13 THAI CHARACTER NO NEN */
{"Thai_nonu",                     0x0db9},  /* U+0E19 THAI CHARACTER NO NU */
{"Thai_oang",                     0x0dcd},  /* U+0E2D THAI CHARACTER O ANG */
{"Thai_paiyannoi",                0x0dcf},  /* U+0E2F THAI CHARACTER PAIYANNOI */
{"Thai_phinthu",                  0x0dda},  /* U+0E3A THAI CHARACTER PHINTHU */
{"Thai_phophan",                  0x0dbe},  /* U+0E1E THAI CHARACTER PHO PHAN */
{"Thai_phophung",                 0x0dbc},  /* U+0E1C THAI CHARACTER PHO PHUNG */
{"Thai_phosamphao",               0x0dc0},  /* U+0E20 THAI CHARACTER PHO SAMPHAO */
{"Thai_popla",                    0x0dbb},  /* U+0E1B THAI CHARACTER PO PLA */
{"Thai_rorua",                    0x0dc3},  /* U+0E23 THAI CHARACTER RO RUA */
{"Thai_ru",                       0x0dc4},  /* U+0E24 THAI CHARACTER RU */
{"Thai_saraa",                    0x0dd0},  /* U+0E30 THAI CHARACTER SARA A */
{"Thai_saraaa",                   0x0dd2},  /* U+0E32 THAI CHARACTER SARA AA */
{"Thai_saraae",                   0x0de1},  /* U+0E41 THAI CHARACTER SARA AE */
{"Thai_saraaimaimalai",           0x0de4},  /* U+0E44 THAI CHARACTER SARA AI MAIMALAI */
{"Thai_saraaimaimuan",            0x0de3},  /* U+0E43 THAI CHARACTER SARA AI MAIMUAN */
{"Thai_saraam",                   0x0dd3},  /* U+0E33 THAI CHARACTER SARA AM */
{"Thai_sarae",                    0x0de0},  /* U+0E40 THAI CHARACTER SARA E */
{"Thai_sarai",                    0x0dd4},  /* U+0E34 THAI CHARACTER SARA I */
{"Thai_saraii",                   0x0dd5},  /* U+0E35 THAI CHARACTER SARA II */
{"Thai_sarao",                    0x0de2},  /* U+0E42 THAI CHARACTER SARA O */
{"Thai_sarau",                    0x0dd8},  /* U+0E38 THAI CHARACTER SARA U */
{"Thai_saraue",                   0x0dd6},  /* U+0E36 THAI CHARACTER SARA UE */
{"Thai_sarauee",                  0x0dd7},  /* U+0E37 THAI CHARACTER SARA UEE */
{"Thai_sarauu",                   0x0dd9},  /* U+0E39 THAI CHARACTER SARA UU */
{"Thai_sorusi",                   0x0dc9},  /* U+0E29 THAI CHARACTER SO RUSI */
{"Thai_sosala",                   0x0dc8},  /* U+0E28 THAI CHARACTER SO SALA */
{"Thai_soso",                     0x0dab},  /* U+0E0B THAI CHARACTER SO SO */
{"Thai_sosua",                    0x0dca},  /* U+0E2A THAI CHARACTER SO SUA */
{"Thai_thanthakhat",              0x0dec},  /* U+0E4C THAI CHARACTER THANTHAKHAT */
{"Thai_thonangmontho",            0x0db1},  /* U+0E11 THAI CHARACTER THO NANGMONTHO */
{"Thai_thophuthao",               0x0db2},  /* U+0E12 THAI CHARACTER THO PHUTHAO */
{"Thai_thothahan",                0x0db7},  /* U+0E17 THAI CHARACTER THO THAHAN */
{"Thai_thothan",                  0x0db0},  /* U+0E10 THAI CHARACTER THO THAN */
{"Thai_thothong",                 0x0db8},  /* U+0E18 THAI CHARACTER THO THONG */
{"Thai_thothung",                 0x0db6},  /* U+0E16 THAI CHARACTER THO THUNG */
{"Thai_topatak",                  0x0daf},  /* U+0E0F THAI CHARACTER TO PATAK */
{"Thai_totao",                    0x0db5},  /* U+0E15 THAI CHARACTER TO TAO */
{"Thai_wowaen",                   0x0dc7},  /* U+0E27 THAI CHARACTER WO WAEN */
{"Thai_yoyak",                    0x0dc2},  /* U+0E22 THAI CHARACTER YO YAK */
{"Thai_yoying",                   0x0dad},  /* U+0E0D THAI CHARACTER YO YING */
{"threeeighths",                  0x0ac4},  /* U+215C VULGAR FRACTION THREE EIGHTHS */
{"trademark",                     0x0ac9},  /* U+2122 TRADE MARK SIGN */
{"tslash",                        0x03bc},  /* U+0167 LATIN SMALL LETTER T WITH STROKE */
{"Tslash",                        0x03ac},  /* U+0166 LATIN CAPITAL LETTER T WITH STROKE */
{"umacron",                       0x03fe},  /* U+016B LATIN SMALL LETTER U WITH MACRON */
{"Umacron",                       0x03de},  /* U+016A LATIN CAPITAL LETTER U WITH MACRON */
{"uogonek",                       0x03f9},  /* U+0173 LATIN SMALL LETTER U WITH OGONEK */
{"Uogonek",                       0x03d9},  /* U+0172 LATIN CAPITAL LETTER U WITH OGONEK */
{"uparrow",                       0x08fc},  /* U+2191 UPWARDS ARROW */
{"voicedsound",                   0x04de},  /* U+309B KATAKANA-HIRAGANA VOICED SOUND MARK */
{"Zcaron",                        0x01ae},  /* U+017D LATIN CAPITAL LETTER Z WITH CARON */

{NULL,0},
};
