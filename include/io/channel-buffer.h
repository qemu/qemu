/*
 * QEMU I/O channels memory buffer driver
 *
 * Copyright (c) 2015 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef QIO_CHANNEL_BUFFER_H__
#define QIO_CHANNEL_BUFFER_H__

#include "io/channel.h"

#define TYPE_QIO_CHANNEL_BUFFER "qio-channel-buffer"
#define QIO_CHANNEL_BUFFER(obj)                                     \
    OBJECT_CHECK(QIOChannelBuffer, (obj), TYPE_QIO_CHANNEL_BUFFER)

typedef struct QIOChannelBuffer QIOChannelBuffer;

/**
 * QIOChannelBuffer:
 *
 * The QIOChannelBuffer object provides a channel implementation
 * that is able to perform I/O to/from a memory buffer.
 *
 */

struct QIOChannelBuffer {
    QIOChannel parent;
    size_t capacity; /* Total allocated memory */
    size_t usage;    /* Current size of data */
    size_t offset;   /* Offset for future I/O ops */
    uint8_t *data;
};


/**
 * qio_channel_buffer_new:
 * @capacity: the initial buffer capacity to allocate
 *
 * Allocate a new buffer which is initially empty
 *
 * Returns: the new channel object
 */
QIOChannelBuffer *
qio_channel_buffer_new(size_t capacity);

#endif /* QIO_CHANNEL_BUFFER_H__ */
