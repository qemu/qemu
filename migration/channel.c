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
#include "qemu/cutils.h"
#include "channel.h"
#include "exec.h"
#include "fd.h"
#include "file.h"
#include "io/channel-socket.h"
#include "io/channel-tls.h"
#include "migration.h"
#include "multifd.h"
#include "options.h"
#include "qapi/clone-visitor.h"
#include "qapi/qapi-types-migration.h"
#include "qapi/qapi-visit-migration.h"
#include "qapi/error.h"
#include "qemu-file.h"
#include "qemu/yank.h"
#include "rdma.h"
#include "savevm.h"
#include "socket.h"
#include "tls.h"
#include "trace.h"
#include "yank_functions.h"

void migration_connect_outgoing(MigrationState *s, MigrationAddress *addr,
                                Error **errp)
{
    g_autoptr(QIOChannel) ioc = NULL;

    if (addr->transport == MIGRATION_ADDRESS_TYPE_SOCKET) {
        SocketAddress *saddr = &addr->u.socket;
        if (saddr->type == SOCKET_ADDRESS_TYPE_INET ||
            saddr->type == SOCKET_ADDRESS_TYPE_UNIX ||
            saddr->type == SOCKET_ADDRESS_TYPE_VSOCK) {
            socket_connect_outgoing(s, saddr, errp);
            /*
             * async: after the socket is connected, calls
             * migration_channel_connect_outgoing() directly.
             */
            return;

        } else if (saddr->type == SOCKET_ADDRESS_TYPE_FD) {
            ioc = fd_connect_outgoing(s, saddr->u.fd.str, errp);
        }
#ifdef CONFIG_RDMA
    } else if (addr->transport == MIGRATION_ADDRESS_TYPE_RDMA) {
        ioc = rdma_connect_outgoing(s, &addr->u.rdma, errp);
#endif
    } else if (addr->transport == MIGRATION_ADDRESS_TYPE_EXEC) {
        ioc = exec_connect_outgoing(s, addr->u.exec.args, errp);
    } else if (addr->transport == MIGRATION_ADDRESS_TYPE_FILE) {
        ioc = file_connect_outgoing(s, &addr->u.file, errp);
    } else {
        error_setg(errp, "uri is not a valid migration protocol");
    }

    if (ioc) {
        migration_channel_connect_outgoing(s, ioc);
    }

    return;
}

void migration_connect_incoming(MigrationAddress *addr, Error **errp)
{
    if (addr->transport == MIGRATION_ADDRESS_TYPE_SOCKET) {
        SocketAddress *saddr = &addr->u.socket;
        if (saddr->type == SOCKET_ADDRESS_TYPE_INET ||
            saddr->type == SOCKET_ADDRESS_TYPE_UNIX ||
            saddr->type == SOCKET_ADDRESS_TYPE_VSOCK) {
            socket_connect_incoming(saddr, errp);
        } else if (saddr->type == SOCKET_ADDRESS_TYPE_FD) {
            fd_connect_incoming(saddr->u.fd.str, errp);
        }
#ifdef CONFIG_RDMA
    } else if (addr->transport == MIGRATION_ADDRESS_TYPE_RDMA) {
        rdma_connect_incoming(&addr->u.rdma, errp);
#endif
    } else if (addr->transport == MIGRATION_ADDRESS_TYPE_EXEC) {
        exec_connect_incoming(addr->u.exec.args, errp);
    } else if (addr->transport == MIGRATION_ADDRESS_TYPE_FILE) {
        file_connect_incoming(&addr->u.file, errp);
    } else {
        error_setg(errp, "unknown migration protocol");
    }

    /*
     * async: the above routines all wait for the incoming connection
     * and call back to migration_channel_process_incoming() to start
     * the migration.
     */
}

bool migration_has_main_and_multifd_channels(void)
{
    MigrationIncomingState *mis = migration_incoming_get_current();
    if (!mis->from_src_file) {
        /* main channel not established */
        return false;
    }

    if (migrate_multifd() && !multifd_recv_all_channels_created()) {
        return false;
    }

    /* main and all multifd channels are established */
    return true;
}

/**
 * @migration_has_all_channels: We have received all channels that we need
 *
 * Returns true when we have got connections to all the channels that
 * we need for migration.
 */
bool migration_has_all_channels(void)
{
    if (!migration_has_main_and_multifd_channels()) {
        return false;
    }

    MigrationIncomingState *mis = migration_incoming_get_current();
    if (migrate_postcopy_preempt() && !mis->postcopy_qemufile_dst) {
        return false;
    }

    return true;
}

static MigChannelType migration_channel_identify(MigrationIncomingState *mis,
                                                 QIOChannel *ioc, Error **errp)
{
    MigChannelType channel = CH_NONE;
    uint32_t channel_magic = 0;
    int ret = 0;

    if (!migration_has_main_and_multifd_channels()) {
        if (qio_channel_has_feature(ioc, QIO_CHANNEL_FEATURE_READ_MSG_PEEK)) {
            /*
             * With multiple channels, it is possible that we receive channels
             * out of order on destination side, causing incorrect mapping of
             * source channels on destination side. Check channel MAGIC to
             * decide type of channel. Please note this is best effort,
             * postcopy preempt channel does not send any magic number so
             * avoid it for postcopy live migration. Also tls live migration
             * already does tls handshake while initializing main channel so
             * with tls this issue is not possible.
             */
            ret = migration_channel_read_peek(ioc, (void *)&channel_magic,
                                              sizeof(channel_magic), errp);
            if (ret != 0) {
                goto out;
            }

            channel_magic = be32_to_cpu(channel_magic);
            if (channel_magic == QEMU_VM_FILE_MAGIC) {
                channel = CH_MAIN;
            } else if (channel_magic == MULTIFD_MAGIC) {
                assert(migrate_multifd());
                channel = CH_MULTIFD;
            } else if (!mis->from_src_file &&
                        mis->state == MIGRATION_STATUS_POSTCOPY_PAUSED) {
                /* reconnect main channel for postcopy recovery */
                channel = CH_MAIN;
            } else {
                error_setg(errp, "unknown channel magic: %u", channel_magic);
            }
        } else if (mis->from_src_file && migrate_multifd()) {
            /*
             * Non-peekable channels like tls/file are processed as
             * multifd channels when multifd is enabled.
             */
            channel = CH_MULTIFD;
        } else if (!mis->from_src_file) {
            channel = CH_MAIN;
        } else {
            error_setg(errp, "non-peekable channel used without multifd");
        }
    } else {
        assert(migrate_postcopy_preempt());
        channel = CH_POSTCOPY;
    }

out:
    return channel;
}

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
    MigrationIncomingState *mis = migration_incoming_get_current();
    Error *local_err = NULL;
    MigChannelType ch;

    trace_migration_set_incoming_channel(
        ioc, object_get_typename(OBJECT(ioc)));

    if (migrate_channel_requires_tls_upgrade(ioc)) {
        migration_tls_channel_process_incoming(ioc, &local_err);
    } else {
        migration_ioc_register_yank(ioc);
        ch = migration_channel_identify(mis, ioc, &local_err);
        if (!ch) {
            goto out;
        }

        if (migration_incoming_setup(ioc, ch, &local_err)) {
            migration_start_incoming();
        }
    }
out:
    if (local_err) {
        error_report_err(local_err);
        migrate_set_state(&mis->state, mis->state, MIGRATION_STATUS_FAILED);
        if (mis->exit_on_error) {
            exit(EXIT_FAILURE);
        }
    }
}

void migration_channel_connect_outgoing(MigrationState *s, QIOChannel *ioc)
{
    trace_migration_set_outgoing_channel(ioc, object_get_typename(OBJECT(ioc)));

    if (migrate_channel_requires_tls_upgrade(ioc)) {
        Error *local_err = NULL;

        migration_tls_channel_connect(s, ioc, &local_err);
        if (local_err) {
            migration_connect_error_propagate(s, local_err);
        }

        /*
         * async: the above will call back to this function after
         * the TLS handshake is successfully completed.
         */
        return;
    }

    migration_ioc_register_yank(ioc);
    migration_outgoing_setup(ioc);
    migration_start_outgoing(s);
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

        if (len < 0 && len != QIO_CHANNEL_ERR_BLOCK) {
            return -1;
        }

        if (len == 0) {
            error_setg(errp, "Failed to peek at channel");
            return -1;
        }

        if (len == buflen) {
            break;
        }

        qio_channel_wait_cond(ioc, G_IO_IN);
    }

    return 0;
}

static bool migrate_channels_parse(MigrationChannelList *channels,
                                   MigrationChannel **main_channelp,
                                   MigrationChannel **cpr_channelp,
                                   Error **errp)
{
    MigrationChannel *channelv[MIGRATION_CHANNEL_TYPE__MAX] = { NULL };

    if (!cpr_channelp && channels->next) {
        error_setg(errp, "Channel list must have only one entry, "
                   "for type 'main'");
        return false;
    }

    for ( ; channels; channels = channels->next) {
        MigrationChannelType type;

        type = channels->value->channel_type;
        if (channelv[type]) {
            error_setg(errp, "Channel list has more than one %s entry",
                       MigrationChannelType_str(type));
            return false;
        }
        channelv[type] = channels->value;
    }

    if (cpr_channelp) {
        *cpr_channelp = QAPI_CLONE(MigrationChannel,
                                   channelv[MIGRATION_CHANNEL_TYPE_CPR]);

        if (migrate_mode() == MIG_MODE_CPR_TRANSFER && !*cpr_channelp) {
            error_setg(errp, "missing 'cpr' migration channel");
            return false;
        }
    }

    *main_channelp = QAPI_CLONE(MigrationChannel,
                                channelv[MIGRATION_CHANNEL_TYPE_MAIN]);

    if (!(*main_channelp)->addr) {
        error_setg(errp, "Channel list has no main entry");
        return false;
    }

    return true;
}

bool migrate_uri_parse(const char *uri, MigrationChannel **channel,
                       Error **errp)
{
    g_autoptr(MigrationChannel) val = g_new0(MigrationChannel, 1);
    g_autoptr(MigrationAddress) addr = g_new0(MigrationAddress, 1);
    InetSocketAddress *isock = &addr->u.rdma;
    strList **tail = &addr->u.exec.args;

    if (strstart(uri, "exec:", NULL)) {
        addr->transport = MIGRATION_ADDRESS_TYPE_EXEC;
#ifdef WIN32
        QAPI_LIST_APPEND(tail, g_strdup(exec_get_cmd_path()));
        QAPI_LIST_APPEND(tail, g_strdup("/c"));
#else
        QAPI_LIST_APPEND(tail, g_strdup("/bin/sh"));
        QAPI_LIST_APPEND(tail, g_strdup("-c"));
#endif
        QAPI_LIST_APPEND(tail, g_strdup(uri + strlen("exec:")));
    } else if (strstart(uri, "rdma:", NULL)) {
        if (inet_parse(isock, uri + strlen("rdma:"), errp)) {
            qapi_free_InetSocketAddress(isock);
            return false;
        }
        addr->transport = MIGRATION_ADDRESS_TYPE_RDMA;
    } else if (strstart(uri, "tcp:", NULL) ||
                strstart(uri, "unix:", NULL) ||
                strstart(uri, "vsock:", NULL) ||
                strstart(uri, "fd:", NULL)) {
        addr->transport = MIGRATION_ADDRESS_TYPE_SOCKET;
        SocketAddress *saddr = socket_parse(uri, errp);
        if (!saddr) {
            return false;
        }
        addr->u.socket.type = saddr->type;
        addr->u.socket.u = saddr->u;
        /* Don't free the objects inside; their ownership moved to "addr" */
        g_free(saddr);
    } else if (strstart(uri, "file:", NULL)) {
        addr->transport = MIGRATION_ADDRESS_TYPE_FILE;
        addr->u.file.filename = g_strdup(uri + strlen("file:"));
        if (file_parse_offset(addr->u.file.filename, &addr->u.file.offset,
                              errp)) {
            return false;
        }
    } else {
        error_setg(errp, "unknown migration protocol: %s", uri);
        return false;
    }

    val->channel_type = MIGRATION_CHANNEL_TYPE_MAIN;
    val->addr = g_steal_pointer(&addr);
    *channel = g_steal_pointer(&val);
    return true;
}

bool migration_channel_parse_input(const char *uri,
                                   MigrationChannelList *channels,
                                   MigrationChannel **main_channelp,
                                   MigrationChannel **cpr_channelp,
                                   Error **errp)
{
    if (!uri == !channels) {
        error_setg(errp, "need either 'uri' or 'channels' argument");
        return false;
    }

    if (channels) {
        return migrate_channels_parse(channels, main_channelp, cpr_channelp,
                                      errp);
    } else {
        return migrate_uri_parse(uri, main_channelp, errp);
    }
}
