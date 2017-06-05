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
#include "chardev/char.h"

#ifdef _WIN32
#include "chardev/char-win.h"
#else
#include "chardev/char-fd.h"
#endif

#ifdef _WIN32
#define MAXCONNECT 1
#define NTIMEOUT 5000

static int win_chr_pipe_init(Chardev *chr, const char *filename,
                             Error **errp)
{
    WinChardev *s = WIN_CHARDEV(chr);
    OVERLAPPED ov;
    int ret;
    DWORD size;
    char *openname;

    s->fpipe = TRUE;

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

    openname = g_strdup_printf("\\\\.\\pipe\\%s", filename);
    s->file = CreateNamedPipe(openname,
                              PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                              PIPE_TYPE_BYTE | PIPE_READMODE_BYTE |
                              PIPE_WAIT,
                              MAXCONNECT, NSENDBUF, NRECVBUF, NTIMEOUT, NULL);
    g_free(openname);
    if (s->file == INVALID_HANDLE_VALUE) {
        error_setg(errp, "Failed CreateNamedPipe (%lu)", GetLastError());
        s->file = NULL;
        goto fail;
    }

    ZeroMemory(&ov, sizeof(ov));
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    ret = ConnectNamedPipe(s->file, &ov);
    if (ret) {
        error_setg(errp, "Failed ConnectNamedPipe");
        goto fail;
    }

    ret = GetOverlappedResult(s->file, &ov, &size, TRUE);
    if (!ret) {
        error_setg(errp, "Failed GetOverlappedResult");
        if (ov.hEvent) {
            CloseHandle(ov.hEvent);
            ov.hEvent = NULL;
        }
        goto fail;
    }

    if (ov.hEvent) {
        CloseHandle(ov.hEvent);
        ov.hEvent = NULL;
    }
    qemu_add_polling_cb(win_chr_pipe_poll, chr);
    return 0;

 fail:
    return -1;
}

static void qemu_chr_open_pipe(Chardev *chr,
                               ChardevBackend *backend,
                               bool *be_opened,
                               Error **errp)
{
    ChardevHostdev *opts = backend->u.pipe.data;
    const char *filename = opts->device;

    if (win_chr_pipe_init(chr, filename, errp) < 0) {
        return;
    }
}

#else

static void qemu_chr_open_pipe(Chardev *chr,
                               ChardevBackend *backend,
                               bool *be_opened,
                               Error **errp)
{
    ChardevHostdev *opts = backend->u.pipe.data;
    int fd_in, fd_out;
    char *filename_in;
    char *filename_out;
    const char *filename = opts->device;

    filename_in = g_strdup_printf("%s.in", filename);
    filename_out = g_strdup_printf("%s.out", filename);
    TFR(fd_in = qemu_open(filename_in, O_RDWR | O_BINARY));
    TFR(fd_out = qemu_open(filename_out, O_RDWR | O_BINARY));
    g_free(filename_in);
    g_free(filename_out);
    if (fd_in < 0 || fd_out < 0) {
        if (fd_in >= 0) {
            close(fd_in);
        }
        if (fd_out >= 0) {
            close(fd_out);
        }
        TFR(fd_in = fd_out = qemu_open(filename, O_RDWR | O_BINARY));
        if (fd_in < 0) {
            error_setg_file_open(errp, errno, filename);
            return;
        }
    }
    qemu_chr_open_fd(chr, fd_in, fd_out);
}

#endif /* !_WIN32 */

static void qemu_chr_parse_pipe(QemuOpts *opts, ChardevBackend *backend,
                                Error **errp)
{
    const char *device = qemu_opt_get(opts, "path");
    ChardevHostdev *dev;

    if (device == NULL) {
        error_setg(errp, "chardev: pipe: no device path given");
        return;
    }
    backend->type = CHARDEV_BACKEND_KIND_PIPE;
    dev = backend->u.pipe.data = g_new0(ChardevHostdev, 1);
    qemu_chr_parse_common(opts, qapi_ChardevHostdev_base(dev));
    dev->device = g_strdup(device);
}

static void char_pipe_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->parse = qemu_chr_parse_pipe;
    cc->open = qemu_chr_open_pipe;
}

static const TypeInfo char_pipe_type_info = {
    .name = TYPE_CHARDEV_PIPE,
#ifdef _WIN32
    .parent = TYPE_CHARDEV_WIN,
#else
    .parent = TYPE_CHARDEV_FD,
#endif
    .class_init = char_pipe_class_init,
};

static void register_types(void)
{
    type_register_static(&char_pipe_type_info);
}

type_init(register_types);
