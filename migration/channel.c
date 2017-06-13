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
        QEMUFile *f = qemu_fopen_channel_input(ioc);
        migration_fd_process_incoming(f);
    }
}


void migration_channel_connect(MigrationState *s,
                               QIOChannel *ioc,
                               const char *hostname)
{
    trace_migration_set_outgoing_channel(
        ioc, object_get_typename(OBJECT(ioc)), hostname);

    if (s->parameters.tls_creds &&
        *s->parameters.tls_creds &&
        !object_dynamic_cast(OBJECT(ioc),
                             TYPE_QIO_CHANNEL_TLS)) {
        Error *local_err = NULL;
        migration_tls_channel_connect(s, ioc, hostname, &local_err);
        if (local_err) {
            migrate_fd_error(s, local_err);
            error_free(local_err);
        }
    } else {
        QEMUFile *f = qemu_fopen_channel_output(ioc);

        s->to_dst_file = f;

        migrate_fd_connect(s);
    }
}
