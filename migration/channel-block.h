/*
 * QEMU I/O channels block driver
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

#ifndef QIO_CHANNEL_BLOCK_H
#define QIO_CHANNEL_BLOCK_H

#include "io/channel.h"
#include "qom/object.h"

#define TYPE_QIO_CHANNEL_BLOCK "qio-channel-block"
OBJECT_DECLARE_SIMPLE_TYPE(QIOChannelBlock, QIO_CHANNEL_BLOCK)


/**
 * QIOChannelBlock:
 *
 * The QIOChannelBlock object provides a channel implementation
 * that is able to perform I/O on the BlockDriverState objects
 * to the VMState region.
 */

struct QIOChannelBlock {
    QIOChannel parent;
    BlockDriverState *bs;
    off_t offset;
};


/**
 * qio_channel_block_new:
 * @bs: the block driver state
 *
 * Create a new IO channel object that can perform
 * I/O on a BlockDriverState object to the VMState
 * region
 *
 * Returns: the new channel object
 */
QIOChannelBlock *
qio_channel_block_new(BlockDriverState *bs);

#endif /* QIO_CHANNEL_BLOCK_H */
