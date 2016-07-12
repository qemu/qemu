/*
 * QEMU I/O channels external command driver
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

#ifndef QIO_CHANNEL_COMMAND_H
#define QIO_CHANNEL_COMMAND_H

#include "io/channel.h"

#define TYPE_QIO_CHANNEL_COMMAND "qio-channel-command"
#define QIO_CHANNEL_COMMAND(obj)                                     \
    OBJECT_CHECK(QIOChannelCommand, (obj), TYPE_QIO_CHANNEL_COMMAND)

typedef struct QIOChannelCommand QIOChannelCommand;


/**
 * QIOChannelCommand:
 *
 * The QIOChannelCommand class provides a channel implementation
 * that can transport data with an externally running command
 * via its stdio streams.
 */

struct QIOChannelCommand {
    QIOChannel parent;
    int writefd;
    int readfd;
    pid_t pid;
};


/**
 * qio_channel_command_new_pid:
 * @writefd: the FD connected to the command's stdin
 * @readfd: the FD connected to the command's stdout
 * @pid: the PID of the running child command
 * @errp: pointer to a NULL-initialized error object
 *
 * Create a channel for performing I/O with the
 * previously spawned command identified by @pid.
 * The two file descriptors provide the connection
 * to command's stdio streams, either one or which
 * may be -1 to indicate that stream is not open.
 *
 * The channel will take ownership of the process
 * @pid and will kill it when closing the channel.
 * Similarly it will take responsibility for
 * closing the file descriptors @writefd and @readfd.
 *
 * Returns: the command channel object, or NULL on error
 */
QIOChannelCommand *
qio_channel_command_new_pid(int writefd,
                            int readfd,
                            pid_t pid);

/**
 * qio_channel_command_new_spawn:
 * @argv: the NULL terminated list of command arguments
 * @flags: the I/O mode, one of O_RDONLY, O_WRONLY, O_RDWR
 * @errp: pointer to a NULL-initialized error object
 *
 * Create a channel for performing I/O with the
 * command to be spawned with arguments @argv.
 *
 * Returns: the command channel object, or NULL on error
 */
QIOChannelCommand *
qio_channel_command_new_spawn(const char *const argv[],
                              int flags,
                              Error **errp);


#endif /* QIO_CHANNEL_COMMAND_H */
