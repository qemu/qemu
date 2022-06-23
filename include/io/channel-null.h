/*
 * QEMU I/O channels null driver
 *
 * Copyright (c) 2022 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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

#ifndef QIO_CHANNEL_FILE_H
#define QIO_CHANNEL_FILE_H

#include "io/channel.h"
#include "qom/object.h"

#define TYPE_QIO_CHANNEL_NULL "qio-channel-null"
OBJECT_DECLARE_SIMPLE_TYPE(QIOChannelNull, QIO_CHANNEL_NULL)


/**
 * QIOChannelNull:
 *
 * The QIOChannelNull object provides a channel implementation
 * that discards all writes and returns EOF for all reads.
 */

struct QIOChannelNull {
    QIOChannel parent;
    bool closed;
};


/**
 * qio_channel_null_new:
 *
 * Create a new IO channel object that discards all writes
 * and returns EOF for all reads.
 *
 * Returns: the new channel object
 */
QIOChannelNull *
qio_channel_null_new(void);

#endif /* QIO_CHANNEL_NULL_H */
