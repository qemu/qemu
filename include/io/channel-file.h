/*
 * QEMU I/O channels files driver
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

#ifndef QIO_CHANNEL_FILE_H
#define QIO_CHANNEL_FILE_H

#include "io/channel.h"

#define TYPE_QIO_CHANNEL_FILE "qio-channel-file"
#define QIO_CHANNEL_FILE(obj)                                     \
    OBJECT_CHECK(QIOChannelFile, (obj), TYPE_QIO_CHANNEL_FILE)

typedef struct QIOChannelFile QIOChannelFile;

/**
 * QIOChannelFile:
 *
 * The QIOChannelFile object provides a channel implementation
 * that is able to perform I/O on block devices, character
 * devices, FIFOs, pipes and plain files. While it is technically
 * able to work on sockets too on the UNIX platform, this is not
 * portable to Windows and lacks some extra sockets specific
 * functionality. So the QIOChannelSocket object is recommended
 * for that use case.
 *
 */

struct QIOChannelFile {
    QIOChannel parent;
    int fd;
};


/**
 * qio_channel_file_new_fd:
 * @fd: the file descriptor
 *
 * Create a new IO channel object for a file represented
 * by the @fd parameter. @fd can be associated with a
 * block device, character device, fifo, pipe, or a
 * regular file. For sockets, the QIOChannelSocket class
 * should be used instead, as this provides greater
 * functionality and cross platform portability.
 *
 * The channel will own the passed in file descriptor
 * and will take responsibility for closing it, so the
 * caller must not close it. If appropriate the caller
 * should dup() its FD before opening the channel.
 *
 * Returns: the new channel object
 */
QIOChannelFile *
qio_channel_file_new_fd(int fd);

/**
 * qio_channel_file_new_path:
 * @fd: the file descriptor
 * @flags: the open flags (O_RDONLY|O_WRONLY|O_RDWR, etc)
 * @mode: the file creation mode if O_WRONLY is set in @flags
 * @errp: pointer to initialized error object
 *
 * Create a new IO channel object for a file represented
 * by the @path parameter. @path can point to any
 * type of file on which sequential I/O can be
 * performed, whether it be a plain file, character
 * device or block device.
 *
 * Returns: the new channel object
 */
QIOChannelFile *
qio_channel_file_new_path(const char *path,
                          int flags,
                          mode_t mode,
                          Error **errp);

#endif /* QIO_CHANNEL_FILE_H */
