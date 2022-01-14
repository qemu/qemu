/*
 * QEMU VGA Emulator.
 *
 * Copyright (c) 2003 Fabrice Bellard
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef QEMU_HW_DISPLAY_VGA_H
#define QEMU_HW_DISPLAY_VGA_H

/*
 * modules can reference this symbol to avoid being loaded
 * into system emulators without vga support
 */
extern bool have_vga;

enum vga_retrace_method {
    VGA_RETRACE_DUMB,
    VGA_RETRACE_PRECISE
};

extern enum vga_retrace_method vga_retrace_method;

#define TYPE_VGA_MMIO "vga-mmio"

#endif
