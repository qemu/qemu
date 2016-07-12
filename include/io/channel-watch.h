/*
 * QEMU I/O channels watch helper APIs
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

#ifndef QIO_CHANNEL_WATCH_H
#define QIO_CHANNEL_WATCH_H

#include "io/channel.h"

/*
 * This module provides helper functions that will be needed by
 * the various QIOChannel implementations, for creating watches
 * on file descriptors / sockets
 */

/**
 * qio_channel_create_fd_watch:
 * @ioc: the channel object
 * @fd: the file descriptor
 * @condition: the I/O condition
 *
 * Create a new main loop source that is able to
 * monitor the file descriptor @fd for the
 * I/O conditions in @condition. This is able
 * monitor block devices, character devices,
 * pipes but not plain files or, on Win32, sockets.
 *
 * Returns: the new main loop source
 */
GSource *qio_channel_create_fd_watch(QIOChannel *ioc,
                                     int fd,
                                     GIOCondition condition);

/**
 * qio_channel_create_socket_watch:
 * @ioc: the channel object
 * @fd: the file descriptor
 * @condition: the I/O condition
 *
 * Create a new main loop source that is able to
 * monitor the file descriptor @fd for the
 * I/O conditions in @condition. This is equivalent
 * to qio_channel_create_fd_watch on POSIX systems
 * but not on Windows.
 *
 * Returns: the new main loop source
 */
GSource *qio_channel_create_socket_watch(QIOChannel *ioc,
                                         int fd,
                                         GIOCondition condition);

/**
 * qio_channel_create_fd_pair_watch:
 * @ioc: the channel object
 * @fdread: the file descriptor for reading
 * @fdwrite: the file descriptor for writing
 * @condition: the I/O condition
 *
 * Create a new main loop source that is able to
 * monitor the pair of file descriptors @fdread
 * and @fdwrite for the I/O conditions in @condition.
 * This is intended for monitoring unidirectional
 * file descriptors such as pipes, where a pair
 * of descriptors is required for bidirectional
 * I/O
 *
 * Returns: the new main loop source
 */
GSource *qio_channel_create_fd_pair_watch(QIOChannel *ioc,
                                          int fdread,
                                          int fdwrite,
                                          GIOCondition condition);

#endif /* QIO_CHANNEL_WATCH_H */
