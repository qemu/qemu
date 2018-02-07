/*
 * QEMU live migration channel operations
 *
 * Copyright Red Hat, Inc. 2016
 *
 * Authors:
 *  Daniel P. Berrange <berrange@redhat.com>
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "channel.h"
#include "tls.h"
#include "migration.h"
#include "qemu-file-channel.h"
#include "trace.h"
#include "qapi/error.h"
#include "io/channel-tls.h"

/**
 * @migration_channel_process_incoming - Create new incoming migration channel
 *
 * Notice that TLS is special.  For it we listen in a listener socket,
 * and then create a new client socket from the TLS library.
 *
 * @ioc: Channel to which we are connecting
 */
void migration_channel_process_incoming(QIOChannel *ioc)
{
    MigrationState *s = migrate_get_current();

    trace_migration_set_incoming_channel(
        ioc, object_get_typename(OBJECT(ioc)));

    if (s->parameters.tls_creds &&
        *s->parameters.tls_creds &&
        !object_dynamic_cast(OBJECT(ioc),
                             TYPE_QIO_CHANNEL_TLS)) {
        Error *local_err = NULL;
        migration_tls_channel_process_incoming(s, ioc, &local_err);
        if (local_err) {
            error_report_err(local_err);
        }
    } else {
        migration_ioc_process_incoming(ioc);
    }
}


/**
 * @migration_channel_connect - Create new outgoing migration channel
 *
 * @s: Current migration state
 * @ioc: Channel to which we are connecting
 * @hostname: Where we want to connect
 * @error: Error indicating failure to connect, free'd here
 */
void migration_channel_connect(MigrationState *s,
                               QIOChannel *ioc,
                               const char *hostname,
                               Error *error)
{
    trace_migration_set_outgoing_channel(
        ioc, object_get_typename(OBJECT(ioc)), hostname, error);

    if (!error) {
        if (s->parameters.tls_creds &&
            *s->parameters.tls_creds &&
            !object_dynamic_cast(OBJECT(ioc),
                                 TYPE_QIO_CHANNEL_TLS)) {
            migration_tls_channel_connect(s, ioc, hostname, &error);
        } else {
            QEMUFile *f = qemu_fopen_channel_output(ioc);

            s->to_dst_file = f;

        }
    }
    migrate_fd_connect(s, error);
    error_free(error);
}
