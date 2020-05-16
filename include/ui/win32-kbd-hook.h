/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef UI_WIN32_KBD_HOOK_H
#define UI_WIN32_KBD_HOOK_H

void win32_kbd_set_window(void *hwnd);
void win32_kbd_set_grab(bool grab);

#endif
