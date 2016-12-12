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
#include "char-win.h"

static void win_chr_readfile(Chardev *chr)
{
    WinChardev *s = WIN_CHARDEV(chr);

    int ret, err;
    uint8_t buf[CHR_READ_BUF_LEN];
    DWORD size;

    ZeroMemory(&s->orecv, sizeof(s->orecv));
    s->orecv.hEvent = s->hrecv;
    ret = ReadFile(s->hcom, buf, s->len, &size, &s->orecv);
    if (!ret) {
        err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            ret = GetOverlappedResult(s->hcom, &s->orecv, &size, TRUE);
        }
    }

    if (size > 0) {
        qemu_chr_be_write(chr, buf, size);
    }
}

static void win_chr_read(Chardev *chr)
{
    WinChardev *s = WIN_CHARDEV(chr);

    if (s->len > s->max_size) {
        s->len = s->max_size;
    }
    if (s->len == 0) {
        return;
    }

    win_chr_readfile(chr);
}

static int win_chr_read_poll(Chardev *chr)
{
    WinChardev *s = WIN_CHARDEV(chr);

    s->max_size = qemu_chr_be_can_write(chr);
    return s->max_size;
}

static int win_chr_poll(void *opaque)
{
    Chardev *chr = CHARDEV(opaque);
    WinChardev *s = WIN_CHARDEV(opaque);
    COMSTAT status;
    DWORD comerr;

    ClearCommError(s->hcom, &comerr, &status);
    if (status.cbInQue > 0) {
        s->len = status.cbInQue;
        win_chr_read_poll(chr);
        win_chr_read(chr);
        return 1;
    }
    return 0;
}

int win_chr_init(Chardev *chr, const char *filename, Error **errp)
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

    s->hcom = CreateFile(filename, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                      OPEN_EXISTING, FILE_FLAG_OVERLAPPED, 0);
    if (s->hcom == INVALID_HANDLE_VALUE) {
        error_setg(errp, "Failed CreateFile (%lu)", GetLastError());
        s->hcom = NULL;
        goto fail;
    }

    if (!SetupComm(s->hcom, NRECVBUF, NSENDBUF)) {
        error_setg(errp, "Failed SetupComm");
        goto fail;
    }

    ZeroMemory(&comcfg, sizeof(COMMCONFIG));
    size = sizeof(COMMCONFIG);
    GetDefaultCommConfig(filename, &comcfg, &size);
    comcfg.dcb.DCBlength = sizeof(DCB);
    CommConfigDialog(filename, NULL, &comcfg);

    if (!SetCommState(s->hcom, &comcfg.dcb)) {
        error_setg(errp, "Failed SetCommState");
        goto fail;
    }

    if (!SetCommMask(s->hcom, EV_ERR)) {
        error_setg(errp, "Failed SetCommMask");
        goto fail;
    }

    cto.ReadIntervalTimeout = MAXDWORD;
    if (!SetCommTimeouts(s->hcom, &cto)) {
        error_setg(errp, "Failed SetCommTimeouts");
        goto fail;
    }

    if (!ClearCommError(s->hcom, &err, &comstat)) {
        error_setg(errp, "Failed ClearCommError");
        goto fail;
    }
    qemu_add_polling_cb(win_chr_poll, chr);
    return 0;

 fail:
    return -1;
}

int win_chr_pipe_poll(void *opaque)
{
    Chardev *chr = CHARDEV(opaque);
    WinChardev *s = WIN_CHARDEV(opaque);
    DWORD size;

    PeekNamedPipe(s->hcom, NULL, 0, NULL, &size, NULL);
    if (size > 0) {
        s->len = size;
        win_chr_read_poll(chr);
        win_chr_read(chr);
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
            ret = WriteFile(s->hcom, buf, len, &size, &s->osend);
        } else {
            ret = WriteFile(s->hcom, buf, len, &size, NULL);
        }
        if (!ret) {
            err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                ret = GetOverlappedResult(s->hcom, &s->osend, &size, TRUE);
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

    if (s->skip_free) {
        return;
    }

    if (s->hsend) {
        CloseHandle(s->hsend);
    }
    if (s->hrecv) {
        CloseHandle(s->hrecv);
    }
    if (s->hcom) {
        CloseHandle(s->hcom);
    }
    if (s->fpipe) {
        qemu_del_polling_cb(win_chr_pipe_poll, chr);
    } else {
        qemu_del_polling_cb(win_chr_poll, chr);
    }

    qemu_chr_be_event(chr, CHR_EVENT_CLOSED);
}

void qemu_chr_open_win_file(Chardev *chr, HANDLE fd_out)
{
    WinChardev *s = WIN_CHARDEV(chr);

    s->skip_free = true;
    s->hcom = fd_out;
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
