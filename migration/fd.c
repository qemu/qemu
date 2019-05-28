/*
 * QEMU live migration via generic fd
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
#include "channel.h"
#include "fd.h"
#include "migration.h"
#include "monitor/monitor.h"
#include "io/channel-util.h"
#include "trace.h"


void fd_start_outgoing_migration(MigrationState *s, const char *fdname, Error **errp)
{
    QIOChannel *ioc;
    int fd = monitor_get_fd(cur_mon, fdname, errp);
    if (fd == -1) {
        return;
    }

    trace_migration_fd_outgoing(fd);
    ioc = qio_channel_new_fd(fd, errp);
    if (!ioc) {
        close(fd);
        return;
    }

    qio_channel_set_name(QIO_CHANNEL(ioc), "migration-fd-outgoing");
    migration_channel_connect(s, ioc, NULL, NULL);
    object_unref(OBJECT(ioc));
}

static gboolean fd_accept_incoming_migration(QIOChannel *ioc,
                                             GIOCondition condition,
                                             gpointer opaque)
{
    migration_channel_process_incoming(ioc);
    object_unref(OBJECT(ioc));
    return G_SOURCE_REMOVE;
}

void fd_start_incoming_migration(const char *fdname, Error **errp)
{
    QIOChannel *ioc;
    int fd = monitor_fd_param(cur_mon, fdname, errp);
    if (fd == -1) {
        return;
    }

    trace_migration_fd_incoming(fd);

    ioc = qio_channel_new_fd(fd, errp);
    if (!ioc) {
        close(fd);
        return;
    }

    qio_channel_set_name(QIO_CHANNEL(ioc), "migration-fd-incoming");
    qio_channel_add_watch_full(ioc, G_IO_IN,
                               fd_accept_incoming_migration,
                               NULL, NULL,
                               g_main_context_get_thread_default());
}
