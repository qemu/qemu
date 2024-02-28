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
#include "postcopy-ram.h"
#include "options.h"
#include "qapi/clone-visitor.h"
#include "qapi/qapi-visit-sockets.h"

struct SocketOutgoingArgs {
    SocketAddress *saddr;
} outgoing_args;

void socket_send_channel_create(QIOTaskFunc f, void *data)
{
    QIOChannelSocket *sioc = qio_channel_socket_new();
    qio_channel_socket_connect_async(sioc, outgoing_args.saddr,
                                     f, data, NULL, NULL);
}

QIOChannel *socket_send_channel_create_sync(Error **errp)
{
    QIOChannelSocket *sioc = qio_channel_socket_new();

    if (!outgoing_args.saddr) {
        object_unref(OBJECT(sioc));
        error_setg(errp, "Initial sock address not set!");
        return NULL;
    }

    if (qio_channel_socket_connect_sync(sioc, outgoing_args.saddr, errp) < 0) {
        object_unref(OBJECT(sioc));
        return NULL;
    }

    return QIO_CHANNEL(sioc);
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
           goto out;
    }

    trace_migration_socket_outgoing_connected(data->hostname);

    if (migrate_zero_copy_send() &&
        !qio_channel_has_feature(sioc, QIO_CHANNEL_FEATURE_WRITE_ZERO_COPY)) {
        error_setg(&err, "Zero copy send feature not detected in host kernel");
    }

out:
    migration_channel_connect(data->s, sioc, data->hostname, err);
    object_unref(OBJECT(sioc));
}

void socket_start_outgoing_migration(MigrationState *s,
                                     SocketAddress *saddr,
                                     Error **errp)
{
    QIOChannelSocket *sioc = qio_channel_socket_new();
    struct SocketConnectData *data = g_new0(struct SocketConnectData, 1);
    SocketAddress *addr = QAPI_CLONE(SocketAddress, saddr);

    data->s = s;

    /* in case previous migration leaked it */
    qapi_free_SocketAddress(outgoing_args.saddr);
    outgoing_args.saddr = addr;

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

void socket_cleanup_outgoing_migration(void)
{
    if (outgoing_args.saddr) {
        qapi_free_SocketAddress(outgoing_args.saddr);
        outgoing_args.saddr = NULL;
    }
}

static void socket_accept_incoming_migration(QIONetListener *listener,
                                             QIOChannelSocket *cioc,
                                             gpointer opaque)
{
    trace_migration_socket_incoming_accepted();

    if (migration_has_all_channels()) {
        error_report("%s: Extra incoming migration connection; ignoring",
                     __func__);
        return;
    }

    qio_channel_set_name(QIO_CHANNEL(cioc), "migration-socket-incoming");
    migration_channel_process_incoming(QIO_CHANNEL(cioc));
}

static void
socket_incoming_migration_end(void *opaque)
{
    QIONetListener *listener = opaque;

    qio_net_listener_disconnect(listener);
    object_unref(OBJECT(listener));
}

void socket_start_incoming_migration(SocketAddress *saddr,
                                     Error **errp)
{
    QIONetListener *listener = qio_net_listener_new();
    MigrationIncomingState *mis = migration_incoming_get_current();
    size_t i;
    int num = 1;

    qio_net_listener_set_name(listener, "migration-socket-listener");

    if (migrate_multifd()) {
        num = migrate_multifd_channels();
    } else if (migrate_postcopy_preempt()) {
        num = RAM_CHANNEL_MAX;
    }

    if (qio_net_listener_open_sync(listener, saddr, num, errp) < 0) {
        object_unref(OBJECT(listener));
        return;
    }

    mis->transport_data = listener;
    mis->transport_cleanup = socket_incoming_migration_end;

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

