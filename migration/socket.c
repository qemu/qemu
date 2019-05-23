/*
 * QEMU live migration via socket
 *
 * Copyright Red Hat, Inc. 2009-2016
 *
 * Authors:
 *  Chris Lalancette <clalance@redhat.com>
 *  Daniel P. Berrange <berrange@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"

#include "qemu/error-report.h"
#include "qapi/error.h"
#include "channel.h"
#include "socket.h"
#include "migration.h"
#include "qemu-file.h"
#include "io/channel-socket.h"
#include "io/net-listener.h"
#include "trace.h"


struct SocketOutgoingArgs {
    SocketAddress *saddr;
} outgoing_args;

void socket_send_channel_create(QIOTaskFunc f, void *data)
{
    QIOChannelSocket *sioc = qio_channel_socket_new();
    qio_channel_socket_connect_async(sioc, outgoing_args.saddr,
                                     f, data, NULL, NULL);
}

int socket_send_channel_destroy(QIOChannel *send)
{
    /* Remove channel */
    object_unref(OBJECT(send));
    if (outgoing_args.saddr) {
        qapi_free_SocketAddress(outgoing_args.saddr);
        outgoing_args.saddr = NULL;
    }
    return 0;
}

static SocketAddress *tcp_build_address(const char *host_port, Error **errp)
{
    SocketAddress *saddr;

    saddr = g_new0(SocketAddress, 1);
    saddr->type = SOCKET_ADDRESS_TYPE_INET;

    if (inet_parse(&saddr->u.inet, host_port, errp)) {
        qapi_free_SocketAddress(saddr);
        return NULL;
    }

    return saddr;
}


static SocketAddress *unix_build_address(const char *path)
{
    SocketAddress *saddr;

    saddr = g_new0(SocketAddress, 1);
    saddr->type = SOCKET_ADDRESS_TYPE_UNIX;
    saddr->u.q_unix.path = g_strdup(path);

    return saddr;
}


struct SocketConnectData {
    MigrationState *s;
    char *hostname;
};

static void socket_connect_data_free(void *opaque)
{
    struct SocketConnectData *data = opaque;
    if (!data) {
        return;
    }
    g_free(data->hostname);
    g_free(data);
}

static void socket_outgoing_migration(QIOTask *task,
                                      gpointer opaque)
{
    struct SocketConnectData *data = opaque;
    QIOChannel *sioc = QIO_CHANNEL(qio_task_get_source(task));
    Error *err = NULL;

    if (qio_task_propagate_error(task, &err)) {
        trace_migration_socket_outgoing_error(error_get_pretty(err));
    } else {
        trace_migration_socket_outgoing_connected(data->hostname);
    }
    migration_channel_connect(data->s, sioc, data->hostname, err);
    object_unref(OBJECT(sioc));
}

static void socket_start_outgoing_migration(MigrationState *s,
                                            SocketAddress *saddr,
                                            Error **errp)
{
    QIOChannelSocket *sioc = qio_channel_socket_new();
    struct SocketConnectData *data = g_new0(struct SocketConnectData, 1);

    data->s = s;

    /* in case previous migration leaked it */
    qapi_free_SocketAddress(outgoing_args.saddr);
    outgoing_args.saddr = saddr;

    if (saddr->type == SOCKET_ADDRESS_TYPE_INET) {
        data->hostname = g_strdup(saddr->u.inet.host);
    }

    qio_channel_set_name(QIO_CHANNEL(sioc), "migration-socket-outgoing");
    qio_channel_socket_connect_async(sioc,
                                     saddr,
                                     socket_outgoing_migration,
                                     data,
                                     socket_connect_data_free,
                                     NULL);
}

void tcp_start_outgoing_migration(MigrationState *s,
                                  const char *host_port,
                                  Error **errp)
{
    Error *err = NULL;
    SocketAddress *saddr = tcp_build_address(host_port, &err);
    if (!err) {
        socket_start_outgoing_migration(s, saddr, &err);
    }
    error_propagate(errp, err);
}

void unix_start_outgoing_migration(MigrationState *s,
                                   const char *path,
                                   Error **errp)
{
    SocketAddress *saddr = unix_build_address(path);
    socket_start_outgoing_migration(s, saddr, errp);
}


static void socket_accept_incoming_migration(QIONetListener *listener,
                                             QIOChannelSocket *cioc,
                                             gpointer opaque)
{
    trace_migration_socket_incoming_accepted();

    qio_channel_set_name(QIO_CHANNEL(cioc), "migration-socket-incoming");
    migration_channel_process_incoming(QIO_CHANNEL(cioc));

    if (migration_has_all_channels()) {
        /* Close listening socket as its no longer needed */
        qio_net_listener_disconnect(listener);
        object_unref(OBJECT(listener));
    }
}


static void socket_start_incoming_migration(SocketAddress *saddr,
                                            Error **errp)
{
    QIONetListener *listener = qio_net_listener_new();
    size_t i;

    qio_net_listener_set_name(listener, "migration-socket-listener");

    if (qio_net_listener_open_sync(listener, saddr, errp) < 0) {
        object_unref(OBJECT(listener));
        return;
    }

    qio_net_listener_set_client_func_full(listener,
                                          socket_accept_incoming_migration,
                                          NULL, NULL,
                                          g_main_context_get_thread_default());

    for (i = 0; i < listener->nsioc; i++)  {
        SocketAddress *address =
            qio_channel_socket_get_local_address(listener->sioc[i], errp);
        if (!address) {
            return;
        }
        migrate_add_address(address);
        qapi_free_SocketAddress(address);
    }
}

void tcp_start_incoming_migration(const char *host_port, Error **errp)
{
    Error *err = NULL;
    SocketAddress *saddr = tcp_build_address(host_port, &err);
    if (!err) {
        socket_start_incoming_migration(saddr, &err);
    }
    qapi_free_SocketAddress(saddr);
    error_propagate(errp, err);
}

void unix_start_incoming_migration(const char *path, Error **errp)
{
    SocketAddress *saddr = unix_build_address(path);
    socket_start_incoming_migration(saddr, errp);
    qapi_free_SocketAddress(saddr);
}
