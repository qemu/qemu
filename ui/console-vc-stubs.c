/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * QEMU VC stubs
 */
#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "chardev/char.h"
#include "ui/console-priv.h"

void qemu_text_console_update_size(QemuTextConsole *c)
{
}

const char *
qemu_text_console_get_label(QemuTextConsole *c)
{
    return NULL;
}

void qemu_text_console_update_cursor(void)
{
}

void qemu_text_console_handle_keysym(QemuTextConsole *s, int keysym)
{
}

void qemu_console_early_init(void)
{
}
