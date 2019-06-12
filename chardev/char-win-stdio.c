/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "chardev/char-win.h"
#include "chardev/char-win-stdio.h"

typedef struct {
    Chardev parent;
    HANDLE  hStdIn;
    HANDLE  hInputReadyEvent;
    HANDLE  hInputDoneEvent;
    HANDLE  hInputThread;
    uint8_t win_stdio_buf;
} WinStdioChardev;

#define WIN_STDIO_CHARDEV(obj)                                          \
    OBJECT_CHECK(WinStdioChardev, (obj), TYPE_CHARDEV_WIN_STDIO)

static void win_stdio_wait_func(void *opaque)
{
    Chardev *chr = CHARDEV(opaque);
    WinStdioChardev *stdio = WIN_STDIO_CHARDEV(opaque);
    INPUT_RECORD       buf[4];
    int                ret;
    DWORD              dwSize;
    int                i;

    ret = ReadConsoleInput(stdio->hStdIn, buf, ARRAY_SIZE(buf), &dwSize);

    if (!ret) {
        /* Avoid error storm */
        qemu_del_wait_object(stdio->hStdIn, NULL, NULL);
        return;
    }

    for (i = 0; i < dwSize; i++) {
        KEY_EVENT_RECORD *kev = &buf[i].Event.KeyEvent;

        if (buf[i].EventType == KEY_EVENT && kev->bKeyDown) {
            int j;
            if (kev->uChar.AsciiChar != 0) {
                for (j = 0; j < kev->wRepeatCount; j++) {
                    if (qemu_chr_be_can_write(chr)) {
                        uint8_t c = kev->uChar.AsciiChar;
                        qemu_chr_be_write(chr, &c, 1);
                    }
                }
            }
        }
    }
}

static DWORD WINAPI win_stdio_thread(LPVOID param)
{
    WinStdioChardev *stdio = WIN_STDIO_CHARDEV(param);
    int                ret;
    DWORD              dwSize;

    while (1) {

        /* Wait for one byte */
        ret = ReadFile(stdio->hStdIn, &stdio->win_stdio_buf, 1, &dwSize, NULL);

        /* Exit in case of error, continue if nothing read */
        if (!ret) {
            break;
        }
        if (!dwSize) {
            continue;
        }

        /* Some terminal emulator returns \r\n for Enter, just pass \n */
        if (stdio->win_stdio_buf == '\r') {
            continue;
        }

        /* Signal the main thread and wait until the byte was eaten */
        if (!SetEvent(stdio->hInputReadyEvent)) {
            break;
        }
        if (WaitForSingleObject(stdio->hInputDoneEvent, INFINITE)
            != WAIT_OBJECT_0) {
            break;
        }
    }

    qemu_del_wait_object(stdio->hInputReadyEvent, NULL, NULL);
    return 0;
}

static void win_stdio_thread_wait_func(void *opaque)
{
    Chardev *chr = CHARDEV(opaque);
    WinStdioChardev *stdio = WIN_STDIO_CHARDEV(opaque);

    if (qemu_chr_be_can_write(chr)) {
        qemu_chr_be_write(chr, &stdio->win_stdio_buf, 1);
    }

    SetEvent(stdio->hInputDoneEvent);
}

static void qemu_chr_set_echo_win_stdio(Chardev *chr, bool echo)
{
    WinStdioChardev *stdio = WIN_STDIO_CHARDEV(chr);
    DWORD              dwMode = 0;

    GetConsoleMode(stdio->hStdIn, &dwMode);

    if (echo) {
        SetConsoleMode(stdio->hStdIn, dwMode | ENABLE_ECHO_INPUT);
    } else {
        SetConsoleMode(stdio->hStdIn, dwMode & ~ENABLE_ECHO_INPUT);
    }
}

static void qemu_chr_open_stdio(Chardev *chr,
                                ChardevBackend *backend,
                                bool *be_opened,
                                Error **errp)
{
    WinStdioChardev *stdio = WIN_STDIO_CHARDEV(chr);
    DWORD              dwMode;
    int                is_console = 0;

    stdio->hStdIn = GetStdHandle(STD_INPUT_HANDLE);
    if (stdio->hStdIn == INVALID_HANDLE_VALUE) {
        error_setg(errp, "cannot open stdio: invalid handle");
        return;
    }

    is_console = GetConsoleMode(stdio->hStdIn, &dwMode) != 0;

    if (is_console) {
        if (qemu_add_wait_object(stdio->hStdIn,
                                 win_stdio_wait_func, chr)) {
            error_setg(errp, "qemu_add_wait_object: failed");
            goto err1;
        }
    } else {
        DWORD   dwId;

        stdio->hInputReadyEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        stdio->hInputDoneEvent  = CreateEvent(NULL, FALSE, FALSE, NULL);
        if (stdio->hInputReadyEvent == INVALID_HANDLE_VALUE
            || stdio->hInputDoneEvent == INVALID_HANDLE_VALUE) {
            error_setg(errp, "cannot create event");
            goto err2;
        }
        if (qemu_add_wait_object(stdio->hInputReadyEvent,
                                 win_stdio_thread_wait_func, chr)) {
            error_setg(errp, "qemu_add_wait_object: failed");
            goto err2;
        }
        stdio->hInputThread     = CreateThread(NULL, 0, win_stdio_thread,
                                               chr, 0, &dwId);

        if (stdio->hInputThread == INVALID_HANDLE_VALUE) {
            error_setg(errp, "cannot create stdio thread");
            goto err3;
        }
    }

    dwMode |= ENABLE_LINE_INPUT;

    if (is_console) {
        /* set the terminal in raw mode */
        /* ENABLE_QUICK_EDIT_MODE | ENABLE_EXTENDED_FLAGS */
        dwMode |= ENABLE_PROCESSED_INPUT;
    }

    SetConsoleMode(stdio->hStdIn, dwMode);

    qemu_chr_set_echo_win_stdio(chr, false);

    return;

err3:
    qemu_del_wait_object(stdio->hInputReadyEvent, NULL, NULL);
err2:
    CloseHandle(stdio->hInputReadyEvent);
    CloseHandle(stdio->hInputDoneEvent);
err1:
    qemu_del_wait_object(stdio->hStdIn, NULL, NULL);
}

static void char_win_stdio_finalize(Object *obj)
{
    WinStdioChardev *stdio = WIN_STDIO_CHARDEV(obj);

    if (stdio->hInputReadyEvent != INVALID_HANDLE_VALUE) {
        CloseHandle(stdio->hInputReadyEvent);
    }
    if (stdio->hInputDoneEvent != INVALID_HANDLE_VALUE) {
        CloseHandle(stdio->hInputDoneEvent);
    }
    if (stdio->hInputThread != INVALID_HANDLE_VALUE) {
        TerminateThread(stdio->hInputThread, 0);
    }
}

static int win_stdio_write(Chardev *chr, const uint8_t *buf, int len)
{
    HANDLE  hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD   dwSize;
    int     len1;

    len1 = len;

    while (len1 > 0) {
        if (!WriteFile(hStdOut, buf, len1, &dwSize, NULL)) {
            break;
        }
        buf  += dwSize;
        len1 -= dwSize;
    }

    return len - len1;
}

static void char_win_stdio_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->open = qemu_chr_open_stdio;
    cc->chr_write = win_stdio_write;
    cc->chr_set_echo = qemu_chr_set_echo_win_stdio;
}

static const TypeInfo char_win_stdio_type_info = {
    .name = TYPE_CHARDEV_WIN_STDIO,
    .parent = TYPE_CHARDEV,
    .instance_size = sizeof(WinStdioChardev),
    .instance_finalize = char_win_stdio_finalize,
    .class_init = char_win_stdio_class_init,
    .abstract = true,
};

static void register_types(void)
{
    type_register_static(&char_win_stdio_type_info);
}

type_init(register_types);
