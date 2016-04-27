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
#include "qapi/error.h"
#include "qemu-common.h"
#include "migration/migration.h"
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

    migration_set_outgoing_channel(s, ioc, NULL);
    object_unref(OBJECT(ioc));
}

static gboolean fd_accept_incoming_migration(QIOChannel *ioc,
                                             GIOCondition condition,
                                             gpointer opaque)
{
    migration_set_incoming_channel(migrate_get_current(), ioc);
    object_unref(OBJECT(ioc));
    return FALSE; /* unregister */
}

void fd_start_incoming_migration(const char *infd, Error **errp)
{
    QIOChannel *ioc;
    int fd;

    fd = strtol(infd, NULL, 0);
    trace_migration_fd_incoming(fd);

    ioc = qio_channel_new_fd(fd, errp);
    if (!ioc) {
        close(fd);
        return;
    }

    qio_channel_add_watch(ioc,
                          G_IO_IN,
                          fd_accept_incoming_migration,
                          NULL,
                          NULL);
}
