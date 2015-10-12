/*
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2009 Red Hat, Inc.
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

#ifndef QEMU_NET_QUEUE_H
#define QEMU_NET_QUEUE_H

#include "qemu-common.h"

typedef struct NetPacket NetPacket;
typedef struct NetQueue NetQueue;

typedef void (NetPacketSent) (NetClientState *sender, ssize_t ret);

#define QEMU_NET_PACKET_FLAG_NONE  0
#define QEMU_NET_PACKET_FLAG_RAW  (1<<0)

/* Returns:
 *   >0 - success
 *    0 - queue packet for future redelivery
 *   <0 - failure (discard packet)
 */
typedef ssize_t (NetQueueDeliverFunc)(NetClientState *sender,
                                      unsigned flags,
                                      const struct iovec *iov,
                                      int iovcnt,
                                      void *opaque);

NetQueue *qemu_new_net_queue(NetQueueDeliverFunc *deliver, void *opaque);

void qemu_net_queue_append_iov(NetQueue *queue,
                               NetClientState *sender,
                               unsigned flags,
                               const struct iovec *iov,
                               int iovcnt,
                               NetPacketSent *sent_cb);

void qemu_del_net_queue(NetQueue *queue);

ssize_t qemu_net_queue_send(NetQueue *queue,
                            NetClientState *sender,
                            unsigned flags,
                            const uint8_t *data,
                            size_t size,
                            NetPacketSent *sent_cb);

ssize_t qemu_net_queue_send_iov(NetQueue *queue,
                                NetClientState *sender,
                                unsigned flags,
                                const struct iovec *iov,
                                int iovcnt,
                                NetPacketSent *sent_cb);

void qemu_net_queue_purge(NetQueue *queue, NetClientState *from);
bool qemu_net_queue_flush(NetQueue *queue);

#endif /* QEMU_NET_QUEUE_H */
