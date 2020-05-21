/*
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 *
 * The win32 keyboard hooking code was imported from project spice-gtk.
 */

#include "qemu/osdep.h"
#include "sysemu/sysemu.h"
#include "ui/win32-kbd-hook.h"

static Notifier win32_unhook_notifier;
static HHOOK win32_keyboard_hook;
static HWND win32_window;
static DWORD win32_grab;

static LRESULT CALLBACK keyboard_hook_cb(int code, WPARAM wparam, LPARAM lparam)
{
    if  (win32_window && code == HC_ACTION && win32_window == GetFocus()) {
        KBDLLHOOKSTRUCT *hooked = (KBDLLHOOKSTRUCT *)lparam;

        if (wparam != WM_KEYUP) {
            DWORD dwmsg = (hooked->flags << 24) |
                          ((hooked->scanCode & 0xff) << 16) | 1;

            switch (hooked->vkCode) {
            case VK_CAPITAL:
                /* fall through */
            case VK_SCROLL:
                /* fall through */
            case VK_NUMLOCK:
                /* fall through */
            case VK_LSHIFT:
                /* fall through */
            case VK_RSHIFT:
                /* fall through */
            case VK_RCONTROL:
                /* fall through */
            case VK_LMENU:
                /* fall through */
            case VK_RMENU:
                break;

            case VK_LCONTROL:
                /*
                 * When pressing AltGr, an extra VK_LCONTROL with a special
                 * scancode with bit 9 set is sent. Let's ignore the extra
                 * VK_LCONTROL, as that will make AltGr misbehave.
                 */
                if (hooked->scanCode & 0x200) {
                    return 1;
                }
                break;

            default:
                if (win32_grab) {
                    SendMessage(win32_window, wparam, hooked->vkCode, dwmsg);
                    return 1;
                }
                break;
            }

        } else {
            switch (hooked->vkCode) {
            case VK_LCONTROL:
                if (hooked->scanCode & 0x200) {
                    return 1;
                }
                break;
            }
        }
    }

    return CallNextHookEx(NULL, code, wparam, lparam);
}

static void keyboard_hook_unhook(Notifier *n, void *data)
{
    UnhookWindowsHookEx(win32_keyboard_hook);
    win32_keyboard_hook = NULL;
}

void win32_kbd_set_window(void *hwnd)
{
    if (hwnd && !win32_keyboard_hook) {
        /* note: the installing thread must have a message loop */
        win32_keyboard_hook = SetWindowsHookEx(WH_KEYBOARD_LL, keyboard_hook_cb,
                                               GetModuleHandle(NULL), 0);
        if (win32_keyboard_hook) {
            win32_unhook_notifier.notify = keyboard_hook_unhook;
            qemu_add_exit_notifier(&win32_unhook_notifier);
        }
    }

    win32_window = hwnd;
}

void win32_kbd_set_grab(bool grab)
{
    win32_grab = grab;
}
