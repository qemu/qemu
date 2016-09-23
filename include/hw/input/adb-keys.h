/*
 * QEMU System Emulator
 *
 * Copyright (c) 2016 John Arbuckle
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/*
 *  adb-keys.h
 *
 *  Provides an enum of all the Macintosh keycodes.
 *  Additional information: http://www.archive.org/stream/apple-guide-macintosh-family-hardware/Apple_Guide_to_the_Macintosh_Family_Hardware_2e#page/n345/mode/2up
 *                          page 308
 */

#ifndef ADB_KEYS_H
#define ADB_KEYS_H

enum {
    ADB_KEY_A = 0x00,
    ADB_KEY_B = 0x0b,
    ADB_KEY_C = 0x08,
    ADB_KEY_D = 0x02,
    ADB_KEY_E = 0x0e,
    ADB_KEY_F = 0x03,
    ADB_KEY_G = 0x05,
    ADB_KEY_H = 0x04,
    ADB_KEY_I = 0x22,
    ADB_KEY_J = 0x26,
    ADB_KEY_K = 0x28,
    ADB_KEY_L = 0x25,
    ADB_KEY_M = 0x2e,
    ADB_KEY_N = 0x2d,
    ADB_KEY_O = 0x1f,
    ADB_KEY_P = 0x23,
    ADB_KEY_Q = 0x0c,
    ADB_KEY_R = 0x0f,
    ADB_KEY_S = 0x01,
    ADB_KEY_T = 0x11,
    ADB_KEY_U = 0x20,
    ADB_KEY_V = 0x09,
    ADB_KEY_W = 0x0d,
    ADB_KEY_X = 0x07,
    ADB_KEY_Y = 0x10,
    ADB_KEY_Z = 0x06,

    ADB_KEY_0 = 0x1d,
    ADB_KEY_1 = 0x12,
    ADB_KEY_2 = 0x13,
    ADB_KEY_3 = 0x14,
    ADB_KEY_4 = 0x15,
    ADB_KEY_5 = 0x17,
    ADB_KEY_6 = 0x16,
    ADB_KEY_7 = 0x1a,
    ADB_KEY_8 = 0x1c,
    ADB_KEY_9 = 0x19,

    ADB_KEY_GRAVE_ACCENT = 0x32,
    ADB_KEY_MINUS = 0x1b,
    ADB_KEY_EQUAL = 0x18,
    ADB_KEY_DELETE = 0x33,
    ADB_KEY_CAPS_LOCK = 0x39,
    ADB_KEY_TAB = 0x30,
    ADB_KEY_RETURN = 0x24,
    ADB_KEY_LEFT_BRACKET = 0x21,
    ADB_KEY_RIGHT_BRACKET = 0x1e,
    ADB_KEY_BACKSLASH = 0x2a,
    ADB_KEY_SEMICOLON = 0x29,
    ADB_KEY_APOSTROPHE = 0x27,
    ADB_KEY_COMMA = 0x2b,
    ADB_KEY_PERIOD = 0x2f,
    ADB_KEY_FORWARD_SLASH = 0x2c,
    ADB_KEY_LEFT_SHIFT = 0x38,
    ADB_KEY_RIGHT_SHIFT = 0x7b,
    ADB_KEY_SPACEBAR = 0x31,
    ADB_KEY_LEFT_CONTROL = 0x36,
    ADB_KEY_RIGHT_CONTROL = 0x7d,
    ADB_KEY_LEFT_OPTION = 0x3a,
    ADB_KEY_RIGHT_OPTION = 0x7c,
    ADB_KEY_COMMAND = 0x37,

    ADB_KEY_KP_0 = 0x52,
    ADB_KEY_KP_1 = 0x53,
    ADB_KEY_KP_2 = 0x54,
    ADB_KEY_KP_3 = 0x55,
    ADB_KEY_KP_4 = 0x56,
    ADB_KEY_KP_5 = 0x57,
    ADB_KEY_KP_6 = 0x58,
    ADB_KEY_KP_7 = 0x59,
    ADB_KEY_KP_8 = 0x5b,
    ADB_KEY_KP_9 = 0x5c,
    ADB_KEY_KP_PERIOD = 0x41,
    ADB_KEY_KP_ENTER = 0x4c,
    ADB_KEY_KP_PLUS = 0x45,
    ADB_KEY_KP_SUBTRACT = 0x4e,
    ADB_KEY_KP_MULTIPLY = 0x43,
    ADB_KEY_KP_DIVIDE = 0x4b,
    ADB_KEY_KP_EQUAL = 0x51,
    ADB_KEY_KP_CLEAR = 0x47,

    ADB_KEY_UP = 0x3e,
    ADB_KEY_DOWN = 0x3d,
    ADB_KEY_LEFT = 0x3b,
    ADB_KEY_RIGHT = 0x3c,

    ADB_KEY_HELP = 0x72,
    ADB_KEY_HOME = 0x73,
    ADB_KEY_PAGE_UP = 0x74,
    ADB_KEY_PAGE_DOWN = 0x79,
    ADB_KEY_END = 0x77,
    ADB_KEY_FORWARD_DELETE = 0x75,

    ADB_KEY_ESC = 0x35,
    ADB_KEY_F1 = 0x7a,
    ADB_KEY_F2 = 0x78,
    ADB_KEY_F3 = 0x63,
    ADB_KEY_F4 = 0x76,
    ADB_KEY_F5 = 0x60,
    ADB_KEY_F6 = 0x61,
    ADB_KEY_F7 = 0x62,
    ADB_KEY_F8 = 0x64,
    ADB_KEY_F9 = 0x65,
    ADB_KEY_F10 = 0x6d,
    ADB_KEY_F11 = 0x67,
    ADB_KEY_F12 = 0x6f,
    ADB_KEY_F13 = 0x69,
    ADB_KEY_F14 = 0x6b,
    ADB_KEY_F15 = 0x71,

    ADB_KEY_VOLUME_UP = 0x48,
    ADB_KEY_VOLUME_DOWN = 0x49,
    ADB_KEY_VOLUME_MUTE = 0x4a,
    ADB_KEY_POWER = 0x7f7f
};

/* Could not find the value for this key. */
/* #define ADB_KEY_EJECT */

#endif /* ADB_KEYS_H */
