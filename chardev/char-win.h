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
#ifndef CHAR_WIN_H
#define CHAR_WIN_H

#include "sysemu/char.h"

typedef struct {
    Chardev parent;
    int max_size;
    HANDLE hcom, hrecv, hsend;
    OVERLAPPED orecv;
    BOOL fpipe;
    DWORD len;

    /* Protected by the Chardev chr_write_lock.  */
    OVERLAPPED osend;
    /* FIXME: file/console do not finalize */
    bool skip_free;
} WinChardev;

#define NSENDBUF 2048
#define NRECVBUF 2048

#define TYPE_CHARDEV_WIN "chardev-win"
#define WIN_CHARDEV(obj) OBJECT_CHECK(WinChardev, (obj), TYPE_CHARDEV_WIN)

void qemu_chr_open_win_file(Chardev *chr, HANDLE fd_out);
int win_chr_init(Chardev *chr, const char *filename, Error **errp);
int win_chr_pipe_poll(void *opaque);

#endif /* CHAR_WIN_H */
