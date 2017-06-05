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
#include "qemu-common.h"
#include "qapi/error.h"
#include "chardev/char-win.h"

static void win_chr_read(Chardev *chr, DWORD len)
{
    WinChardev *s = WIN_CHARDEV(chr);
    int max_size = qemu_chr_be_can_write(chr);
    int ret, err;
    uint8_t buf[CHR_READ_BUF_LEN];
    DWORD size;

    if (len > max_size) {
        len = max_size;
    }
    if (len == 0) {
        return;
    }

    ZeroMemory(&s->orecv, sizeof(s->orecv));
    s->orecv.hEvent = s->hrecv;
    ret = ReadFile(s->file, buf, len, &size, &s->orecv);
    if (!ret) {
        err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            ret = GetOverlappedResult(s->file, &s->orecv, &size, TRUE);
        }
    }

    if (size > 0) {
        qemu_chr_be_write(chr, buf, size);
    }
}

static int win_chr_serial_poll(void *opaque)
{
    Chardev *chr = CHARDEV(opaque);
    WinChardev *s = WIN_CHARDEV(opaque);
    COMSTAT status;
    DWORD comerr;

    ClearCommError(s->file, &comerr, &status);
    if (status.cbInQue > 0) {
        win_chr_read(chr, status.cbInQue);
        return 1;
    }
    return 0;
}

int win_chr_serial_init(Chardev *chr, const char *filename, Error **errp)
{
    WinChardev *s = WIN_CHARDEV(chr);
    COMMCONFIG comcfg;
    COMMTIMEOUTS cto = { 0, 0, 0, 0, 0};
    COMSTAT comstat;
    DWORD size;
    DWORD err;

    s->hsend = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!s->hsend) {
        error_setg(errp, "Failed CreateEvent");
        goto fail;
    }
    s->hrecv = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!s->hrecv) {
        error_setg(errp, "Failed CreateEvent");
        goto fail;
    }

    s->file = CreateFile(filename, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                      OPEN_EXISTING, FILE_FLAG_OVERLAPPED, 0);
    if (s->file == INVALID_HANDLE_VALUE) {
        error_setg(errp, "Failed CreateFile (%lu)", GetLastError());
        s->file = NULL;
        goto fail;
    }

    if (!SetupComm(s->file, NRECVBUF, NSENDBUF)) {
        error_setg(errp, "Failed SetupComm");
        goto fail;
    }

    ZeroMemory(&comcfg, sizeof(COMMCONFIG));
    size = sizeof(COMMCONFIG);
    GetDefaultCommConfig(filename, &comcfg, &size);
    comcfg.dcb.DCBlength = sizeof(DCB);
    CommConfigDialog(filename, NULL, &comcfg);

    if (!SetCommState(s->file, &comcfg.dcb)) {
        error_setg(errp, "Failed SetCommState");
        goto fail;
    }

    if (!SetCommMask(s->file, EV_ERR)) {
        error_setg(errp, "Failed SetCommMask");
        goto fail;
    }

    cto.ReadIntervalTimeout = MAXDWORD;
    if (!SetCommTimeouts(s->file, &cto)) {
        error_setg(errp, "Failed SetCommTimeouts");
        goto fail;
    }

    if (!ClearCommError(s->file, &err, &comstat)) {
        error_setg(errp, "Failed ClearCommError");
        goto fail;
    }
    qemu_add_polling_cb(win_chr_serial_poll, chr);
    return 0;

 fail:
    return -1;
}

int win_chr_pipe_poll(void *opaque)
{
    Chardev *chr = CHARDEV(opaque);
    WinChardev *s = WIN_CHARDEV(opaque);
    DWORD size;

    PeekNamedPipe(s->file, NULL, 0, NULL, &size, NULL);
    if (size > 0) {
        win_chr_read(chr, size);
        return 1;
    }
    return 0;
}

/* Called with chr_write_lock held.  */
static int win_chr_write(Chardev *chr, const uint8_t *buf, int len1)
{
    WinChardev *s = WIN_CHARDEV(chr);
    DWORD len, ret, size, err;

    len = len1;
    ZeroMemory(&s->osend, sizeof(s->osend));
    s->osend.hEvent = s->hsend;
    while (len > 0) {
        if (s->hsend) {
            ret = WriteFile(s->file, buf, len, &size, &s->osend);
        } else {
            ret = WriteFile(s->file, buf, len, &size, NULL);
        }
        if (!ret) {
            err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                ret = GetOverlappedResult(s->file, &s->osend, &size, TRUE);
                if (ret) {
                    buf += size;
                    len -= size;
                } else {
                    break;
                }
            } else {
                break;
            }
        } else {
            buf += size;
            len -= size;
        }
    }
    return len1 - len;
}

static void char_win_finalize(Object *obj)
{
    Chardev *chr = CHARDEV(obj);
    WinChardev *s = WIN_CHARDEV(chr);

    if (s->hsend) {
        CloseHandle(s->hsend);
    }
    if (s->hrecv) {
        CloseHandle(s->hrecv);
    }
    if (!s->keep_open && s->file) {
        CloseHandle(s->file);
    }
    if (s->fpipe) {
        qemu_del_polling_cb(win_chr_pipe_poll, chr);
    } else {
        qemu_del_polling_cb(win_chr_serial_poll, chr);
    }

    qemu_chr_be_event(chr, CHR_EVENT_CLOSED);
}

void win_chr_set_file(Chardev *chr, HANDLE file, bool keep_open)
{
    WinChardev *s = WIN_CHARDEV(chr);

    s->keep_open = keep_open;
    s->file = file;
}

static void char_win_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->chr_write = win_chr_write;
}

static const TypeInfo char_win_type_info = {
    .name = TYPE_CHARDEV_WIN,
    .parent = TYPE_CHARDEV,
    .instance_size = sizeof(WinChardev),
    .instance_finalize = char_win_finalize,
    .class_init = char_win_class_init,
    .abstract = true,
};

static void register_types(void)
{
    type_register_static(&char_win_type_info);
}

type_init(register_types);
