/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef UI_INPUT_BARRIER_H
#define UI_INPUT_BARRIER_H

/* Barrier protocol */
#define BARRIER_VERSION_MAJOR 1
#define BARRIER_VERSION_MINOR 6

enum barrierCmd {
    barrierCmdCNoop,
    barrierCmdCClose,
    barrierCmdCEnter,
    barrierCmdCLeave,
    barrierCmdCClipboard,
    barrierCmdCScreenSaver,
    barrierCmdCResetOptions,
    barrierCmdCInfoAck,
    barrierCmdCKeepAlive,
    barrierCmdDKeyDown,
    barrierCmdDKeyRepeat,
    barrierCmdDKeyUp,
    barrierCmdDMouseDown,
    barrierCmdDMouseUp,
    barrierCmdDMouseMove,
    barrierCmdDMouseRelMove,
    barrierCmdDMouseWheel,
    barrierCmdDClipboard,
    barrierCmdDInfo,
    barrierCmdDSetOptions,
    barrierCmdDFileTransfer,
    barrierCmdDDragInfo,
    barrierCmdQInfo,
    barrierCmdEIncompatible,
    barrierCmdEBusy,
    barrierCmdEUnknown,
    barrierCmdEBad,
    /* connection sequence */
    barrierCmdHello,
    barrierCmdHelloBack,
};

enum {
   barrierButtonNone,
   barrierButtonLeft,
   barrierButtonMiddle,
   barrierButtonRight,
   barrierButtonExtra0
};

struct barrierVersion {
    int16_t major;
    int16_t minor;
};

struct barrierMouseButton {
    int8_t buttonid;
};

struct barrierEnter {
    int16_t x;
    int16_t y;
    int32_t seqn;
    int16_t modifier;
};

struct barrierMousePos {
    int16_t x;
    int16_t y;
};

struct barrierKey {
    int16_t keyid;
    int16_t modifier;
    int16_t button;
};

struct barrierRepeat {
    int16_t keyid;
    int16_t modifier;
    int16_t repeat;
    int16_t button;
};

#define BARRIER_MAX_OPTIONS 32
struct barrierSet {
    int nb;
    struct {
        int id;
        char nul;
        int value;
    } option[BARRIER_MAX_OPTIONS];
};

struct barrierMsg {
    enum barrierCmd cmd;
    union {
        struct barrierVersion version;
        struct barrierMouseButton mousebutton;
        struct barrierMousePos mousepos;
        struct barrierEnter enter;
        struct barrierKey key;
        struct barrierRepeat repeat;
        struct barrierSet set;
    };
};
#endif
