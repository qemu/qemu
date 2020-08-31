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
#ifndef CHAR_FD_H
#define CHAR_FD_H

#include "io/channel.h"
#include "chardev/char.h"
#include "qom/object.h"

struct FDChardev {
    Chardev parent;

    QIOChannel *ioc_in, *ioc_out;
    int max_size;
};
typedef struct FDChardev FDChardev;

#define TYPE_CHARDEV_FD "chardev-fd"

DECLARE_INSTANCE_CHECKER(FDChardev, FD_CHARDEV,
                         TYPE_CHARDEV_FD)

void qemu_chr_open_fd(Chardev *chr, int fd_in, int fd_out);
int qmp_chardev_open_file_source(char *src, int flags, Error **errp);

#endif /* CHAR_FD_H */
