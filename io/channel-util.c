/*
 * QEMU I/O channels utility APIs
 *
 * Copyright (c) 2016 Red Hat, Inc.
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

#include "qemu/osdep.h"
#include "io/channel-util.h"
#include "io/channel-file.h"
#include "io/channel-socket.h"


QIOChannel *qio_channel_new_fd(int fd,
                               Error **errp)
{
    QIOChannel *ioc;

    if (fd_is_socket(fd)) {
        ioc = QIO_CHANNEL(qio_channel_socket_new_fd(fd, errp));
    } else {
        ioc = QIO_CHANNEL(qio_channel_file_new_fd(fd));
    }
    return ioc;
}
