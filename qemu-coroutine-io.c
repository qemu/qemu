/*
 * Coroutine-aware I/O functions
 *
 * Copyright (C) 2009-2010 Nippon Telegraph and Telephone Corporation.
 * Copyright (c) 2011, Red Hat, Inc.
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
#include "qemu-common.h"
#include "qemu_socket.h"
#include "qemu-coroutine.h"

int coroutine_fn qemu_co_recvv(int sockfd, struct iovec *iov,
                               int len, int iov_offset)
{
    int total = 0;
    int ret;
    while (len) {
        ret = qemu_recvv(sockfd, iov, len, iov_offset + total);
        if (ret < 0) {
            if (errno == EAGAIN) {
                qemu_coroutine_yield();
                continue;
            }
            if (total == 0) {
                total = -1;
            }
            break;
        }
        if (ret == 0) {
            break;
        }
        total += ret, len -= ret;
    }

    return total;
}

int coroutine_fn qemu_co_sendv(int sockfd, struct iovec *iov,
                               int len, int iov_offset)
{
    int total = 0;
    int ret;
    while (len) {
        ret = qemu_sendv(sockfd, iov, len, iov_offset + total);
        if (ret < 0) {
            if (errno == EAGAIN) {
                qemu_coroutine_yield();
                continue;
            }
            if (total == 0) {
                total = -1;
            }
            break;
        }
        total += ret, len -= ret;
    }

    return total;
}

int coroutine_fn qemu_co_recv(int sockfd, void *buf, int len)
{
    struct iovec iov;

    iov.iov_base = buf;
    iov.iov_len = len;

    return qemu_co_recvv(sockfd, &iov, len, 0);
}

int coroutine_fn qemu_co_send(int sockfd, void *buf, int len)
{
    struct iovec iov;

    iov.iov_base = buf;
    iov.iov_len = len;

    return qemu_co_sendv(sockfd, &iov, len, 0);
}
