/*
 * Copyright (c) 2022, 2024 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "io/channel-file.h"
#include "io/channel-socket.h"
#include "io/net-listener.h"
#include "migration/cpr.h"
#include "migration/migration.h"
#include "migration/savevm.h"
#include "migration/qemu-file.h"
#include "migration/vmstate.h"
#include "trace.h"

QEMUFile *cpr_transfer_output(MigrationChannel *channel, Error **errp)
{
    MigrationAddress *addr = channel->addr;

    if (addr->transport == MIGRATION_ADDRESS_TYPE_SOCKET &&
        addr->u.socket.type == SOCKET_ADDRESS_TYPE_UNIX) {

        g_autoptr(QIOChannelSocket) sioc = qio_channel_socket_new();
        QIOChannel *ioc = QIO_CHANNEL(sioc);
        SocketAddress *saddr = &addr->u.socket;

        if (qio_channel_socket_connect_sync(sioc, saddr, errp) < 0) {
            return NULL;
        }
        trace_cpr_transfer_output(addr->u.socket.u.q_unix.path);
        qio_channel_set_name(ioc, "cpr-out");
        return qemu_file_new_output(ioc);

    } else {
        error_setg(errp, "bad cpr channel address; must be unix");
        return NULL;
    }
}

QEMUFile *cpr_transfer_input(MigrationChannel *channel, Error **errp)
{
    MigrationAddress *addr = channel->addr;

    if (addr->transport == MIGRATION_ADDRESS_TYPE_SOCKET &&
        (addr->u.socket.type == SOCKET_ADDRESS_TYPE_UNIX ||
            addr->u.socket.type == SOCKET_ADDRESS_TYPE_FD)) {

        g_autoptr(QIOChannelSocket) sioc = NULL;
        SocketAddress *saddr = &addr->u.socket;
        g_autoptr(QIONetListener) listener = qio_net_listener_new();
        QIOChannel *ioc;

        qio_net_listener_set_name(listener, "cpr-socket-listener");
        if (qio_net_listener_open_sync(listener, saddr, 1, errp) < 0) {
            return NULL;
        }

        sioc = qio_net_listener_wait_client(listener);
        ioc = QIO_CHANNEL(sioc);
        trace_cpr_transfer_input(
            addr->u.socket.type == SOCKET_ADDRESS_TYPE_UNIX ?
            addr->u.socket.u.q_unix.path : addr->u.socket.u.fd.str);
        qio_channel_set_name(ioc, "cpr-in");
        return qemu_file_new_input(ioc);

    } else {
        error_setg(errp, "bad cpr channel socket type; must be unix");
        return NULL;
    }
}
