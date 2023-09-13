/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * QEMU UI Console
 */
#ifndef CONSOLE_PRIV_H
#define CONSOLE_PRIV_H

#include "ui/console.h"
#include "qemu/coroutine.h"
#include "qemu/timer.h"

#include "vgafont.h"

#define FONT_HEIGHT 16
#define FONT_WIDTH 8

struct QemuConsole {
    Object parent;

    int index;
    DisplayState *ds;
    DisplaySurface *surface;
    DisplayScanout scanout;
    int dcls;
    DisplayGLCtx *gl;
    int gl_block;
    QEMUTimer *gl_unblock_timer;
    int window_id;
    QemuUIInfo ui_info;
    QEMUTimer *ui_timer;
    const GraphicHwOps *hw_ops;
    void *hw;
    CoQueue dump_queue;

    QTAILQ_ENTRY(QemuConsole) next;
};

void qemu_text_console_select(QemuTextConsole *c);
const char * qemu_text_console_get_label(QemuTextConsole *c);
void qemu_text_console_update_cursor(void);
void qemu_text_console_handle_keysym(QemuTextConsole *s, int keysym);

#endif
