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
#include "qemu-file.h"
#include "trace.h"
#include "qapi/error.h"
#include "io/channel-tls.h"
#include "io/channel-socket.h"
#include "qemu/yank.h"
#include "yank_functions.h"

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
    Error *local_err = NULL;

    trace_migration_set_incoming_channel(
        ioc, object_get_typename(OBJECT(ioc)));

    if (migrate_channel_requires_tls_upgrade(ioc)) {
        migration_tls_channel_process_incoming(s, ioc, &local_err);
    } else {
        migration_ioc_register_yank(ioc);
        migration_ioc_process_incoming(ioc, &local_err);
    }

    if (local_err) {
        error_report_err(local_err);
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
        if (migrate_channel_requires_tls_upgrade(ioc)) {
            migration_tls_channel_connect(s, ioc, hostname, &error);

            if (!error) {
                /* tls_channel_connect will call back to this
                 * function after the TLS handshake,
                 * so we mustn't call migrate_fd_connect until then
                 */

                return;
            }
        } else {
            QEMUFile *f = qemu_file_new_output(ioc);

            migration_ioc_register_yank(ioc);

            qemu_mutex_lock(&s->qemu_file_lock);
            s->to_dst_file = f;
            qemu_mutex_unlock(&s->qemu_file_lock);
        }
    }
    migrate_fd_connect(s, error);
    error_free(error);
}


/**
 * @migration_channel_read_peek - Peek at migration channel, without
 *     actually removing it from channel buffer.
 *
 * @ioc: the channel object
 * @buf: the memory region to read data into
 * @buflen: the number of bytes to read in @buf
 * @errp: pointer to a NULL-initialized error object
 *
 * Returns 0 if successful, returns -1 and sets @errp if fails.
 */
int migration_channel_read_peek(QIOChannel *ioc,
                                const char *buf,
                                const size_t buflen,
                                Error **errp)
{
    ssize_t len = 0;
    struct iovec iov = { .iov_base = (char *)buf, .iov_len = buflen };

    while (true) {
        len = qio_channel_readv_full(ioc, &iov, 1, NULL, NULL,
                                     QIO_CHANNEL_READ_FLAG_MSG_PEEK, errp);

        if (len <= 0 && len != QIO_CHANNEL_ERR_BLOCK) {
            error_setg(errp,
                       "Failed to peek at channel");
            return -1;
        }

        if (len == buflen) {
            break;
        }

        /* 1ms sleep. */
        if (qemu_in_coroutine()) {
            qemu_co_sleep_ns(QEMU_CLOCK_REALTIME, 1000000);
        } else {
            g_usleep(1000);
        }
    }

    return 0;
}
