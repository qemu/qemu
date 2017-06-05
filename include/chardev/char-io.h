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
#ifndef CHAR_IO_H
#define CHAR_IO_H

#include "qemu-common.h"
#include "io/channel.h"
#include "chardev/char.h"

/* Can only be used for read */
GSource *io_add_watch_poll(Chardev *chr,
                        QIOChannel *ioc,
                        IOCanReadHandler *fd_can_read,
                        QIOChannelFunc fd_read,
                        gpointer user_data,
                        GMainContext *context);

void remove_fd_in_watch(Chardev *chr);

int io_channel_send(QIOChannel *ioc, const void *buf, size_t len);

int io_channel_send_full(QIOChannel *ioc, const void *buf, size_t len,
                         int *fds, size_t nfds);

#endif /* CHAR_IO_H */
