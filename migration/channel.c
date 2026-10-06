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
#include "qemu/main-loop.h"
#include "qemu/error-report.h"
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

static const char *const mig_channel_str[CH_NUM] = {
    [CH_NONE] = "NONE",
    [CH_MAIN] = "MAIN",
    [CH_MULTIFD] = "MULTIFD",
    [CH_POSTCOPY] = "PREEMPT",
};

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

static bool qio_channel_is_peekable(QIOChannel *ioc)
{
    return qio_channel_has_feature(ioc, QIO_CHANNEL_FEATURE_READ_MSG_PEEK);
}

/*
 * With multiple channels, it is possible that we receive channels out of
 * order on destination side, causing incorrect mapping of source channels
 * on destination side.
 *
 * When the channel is peekable (e.g. non-TLS socket channels), check
 * channel MAGIC to decide type of channel.
 *
 * Please note this is best effort, postcopy preempt channel does not send
 * any magic number so avoid it for postcopy live migration.
 *
 * Returns: >0 if success, ==0 (CH_NONE) if error.  If error happened,
 * *errp must be set.
 */
static MigChannelType migration_channel_peek(MigrationIncomingState *mis,
                                             QIOChannel *ioc,
                                             Error **errp)
{
    MigChannelType channel = CH_NONE;
    uint32_t channel_magic = 0;
    int ret = 0;

    assert(qio_channel_is_peekable(ioc));

    ret = migration_channel_read_peek(ioc, (void *)&channel_magic,
                                      sizeof(channel_magic), errp);
    if (ret != 0) {
        /* Failed */
        return channel;
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
        error_setg(errp, "Unknown channel magic: %u", channel_magic);
    }

    return channel;
}

/*
 * Returns: >0 if success, ==0 (CH_NONE) if error.  If error happened,
 * *errp must be set.  NOTE: preempt channel is done separately.
 */
static MigChannelType migration_channel_identify(MigrationIncomingState *mis,
                                                 QIOChannel *ioc, Error **errp)
{
    MigChannelType channel = CH_NONE;

    if (!mis->from_src_file) {
        channel = CH_MAIN;
    } else if (migrate_multifd()) {
        /*
         * Non-peekable channels like tls/file are processed as
         * multifd channels when multifd is enabled.
         */
        channel = CH_MULTIFD;
    } else {
        error_setg(errp, "Unexpected non-peekable channel observed");
    }

    return channel;
}

static void migration_incoming_error_propagate(MigrationIncomingState *mis,
                                               Error *error)
{
    error_report_err(error);
    migrate_set_state(&mis->state, mis->state, MIGRATION_STATUS_FAILED);
    if (mis->exit_on_error) {
        exit(EXIT_FAILURE);
    }
}

/* Must be with mis->channels_early.mutex held */
static void
migration_incoming_early_channel_insert(MigrationIncomingState *mis,
                                        QIOChannel *ioc,
                                        GSource *source)
{
    MigEarlyIncomingChannel chan = {
        .source = source,
        .ioc = ioc,
    };

    object_ref(OBJECT(ioc));
    g_source_ref(source);

    g_array_append_val(mis->channels_early.channels, chan);
}

static void migration_incoming_early_channel_free(GArray *channels, int i)
{
    MigEarlyIncomingChannel *chan;

    chan = &g_array_index(channels, MigEarlyIncomingChannel, i);

    g_source_destroy(chan->source);
    g_source_unref(chan->source);
    object_unref(OBJECT(chan->ioc));

    g_array_remove_index_fast(channels, i);
}

/*
 * Remove this channel from the monitoring of @channels_early array.
 * Return true if found and successful, false otherwise.
 */
static bool
migration_incoming_early_channel_remove(MigrationIncomingState *mis,
                                        QIOChannel *ioc)
{
    GArray *channels = mis->channels_early.channels;
    MigEarlyIncomingChannel *chan;
    int i;

    QEMU_LOCK_GUARD(&mis->channels_early.mutex);

    for (i = 0; i < channels->len; i++) {
        chan = &g_array_index(channels, MigEarlyIncomingChannel, i);
        if (chan->ioc != ioc) {
            continue;
        }
        migration_incoming_early_channel_free(channels, i);
        return true;
    }

    return false;
}

void migration_incoming_free_early_channels(MigrationIncomingState *mis)
{
    GArray *channels = mis->channels_early.channels;

    QEMU_LOCK_GUARD(&mis->channels_early.mutex);

    while (channels->len) {
        migration_incoming_early_channel_free(channels, 0);
    }
}

static bool migration_incoming_setup_channel(QIOChannel *ioc,
                                             MigChannelType ch,
                                             Error **errp)
{
    trace_migration_incoming_channel_set(ioc,
                                         object_get_typename(OBJECT(ioc)),
                                         mig_channel_str[ch]);
    migration_ioc_register_yank(ioc);

    return migration_incoming_setup(ioc, ch, errp);
}

static bool migration_incoming_channel_install(MigrationIncomingState *mis,
                                               QIOChannel *ioc,
                                               MigChannelType ch,
                                               Error **errp)
{
    if (!migration_incoming_setup_channel(ioc, ch, errp)) {
        return false;
    }

    /* Installation succeeded, kickoff migration if needed */
    if (migration_has_main_and_multifd_channels()) {
        migration_start_incoming();
    }

    return true;
}

static void
migration_incoming_channel_preempt_setup(QIOChannel *ioc)
{
    assert(migrate_postcopy_preempt());
    /* Installation of preempt channel should never fail */
    migration_incoming_setup_channel(ioc, CH_POSTCOPY, &error_abort);
}

static void
migration_incoming_detect_preempt_channel(MigrationIncomingState *mis)
{
    GArray *channels = mis->channels_early.channels;
    MigEarlyIncomingChannel *chan;

    QEMU_LOCK_GUARD(&mis->channels_early.mutex);

    /* When preempt mode not enabled, nothing to detect.. */
    if (!migrate_postcopy_preempt()) {
        /*
         * .. but if we found something pending, throw an error only, which
         * should not happen.  Even if it happens, resources will still be
         * released after incoming migration is completedly.
         */
        if (channels->len) {
            error_report("%s: Found %u unused channels",
                         __func__, channels->len);
        }
        return;
    }

    /* Preempt channel hasn't yet arrived?  Process it later */
    if (!channels->len) {
        return;
    }

    /*
     * More than one channel should never happen.. capture it in case if
     * it happens, then there's not much we can do.
     */
    if (channels->len > 1) {
        error_report("%s: Found %u unused channels, "
                     "can't identify preempt channel",
                     __func__, channels->len);
        return;
    }

    assert(channels->len == 1);
    /* This is the preempt channel, install it directly */
    chan = &g_array_index(channels, MigEarlyIncomingChannel, 0);
    migration_incoming_channel_preempt_setup(chan->ioc);
    migration_incoming_early_channel_free(channels, 0);
}

static gboolean migration_incoming_channel_readable(QIOChannel *ioc,
                                                    GIOCondition condition,
                                                    gpointer opaque)
{
    MigrationIncomingState *mis = opaque;
    Error *local_err = NULL;
    MigChannelType ch;

    /*
     * No need to monitor this channel anymore as long as anything arrived,
     * remove it from tracking.
     *
     * NOTE: this means if partial data arrived we may still block here,
     * but it shouldn't happen in production, only malicious stream.  Since
     * migration stream is trusted (either due to trusted network, or TLS),
     * that's non-issue.
     *
     * NOTE2: it's theoretically possible that this entry is gone reaching
     * here. Example: the main thread is doing incoming cleanup having this
     * one removed, while this watch can be registered on the monitor
     * iothread's context and fired at the exact same time but in the
     * iothread instead.  If it happens, skip the rest.  I'm not sure if
     * this could happen at all, may depend on iothread lifespan management
     * in the main thread, but be prepared.
     */
    if (!migration_incoming_early_channel_remove(mis, ioc)) {
        goto out;
    }

    ch = migration_channel_peek(mis, ioc, &local_err);
    if (!ch) {
        goto out;
    }

    if (!migration_incoming_channel_install(mis, ioc, ch, &local_err)) {
        goto out;
    }

    if (migration_has_main_and_multifd_channels()) {
        /*
         * Possibilities when reaching here:
         *
         * (1) if preempt not enabled, this should be no-op, all done,
         * (2) if preempt enabled,
         *   (2.a) preempt channel arrived @channels_early, handle it now
         *   (2.b) preempt channel not arrived, to be handled in
         *         migration_channel_process_incoming() later
         */
        migration_incoming_detect_preempt_channel(mis);
    }

out:
    if (local_err) {
        migration_incoming_error_propagate(mis, local_err);
    }

    /*
     * NOTE: we should have already detached the GSource, returning
     * G_SOURCE_REMOVE to be logically consistent only.
     */
    return G_SOURCE_REMOVE;
}

static void migration_incoming_channel_watch(MigrationIncomingState *mis,
                                             QIOChannel *ioc)
{
    GMainContext *context = g_main_context_get_thread_default();
    GSource *source;
    guint io_tag;

    trace_migration_incoming_channel_watch(ioc,
                                           object_get_typename(OBJECT(ioc)));

    /*
     * Careful: this can be run from either the main thread or the monitor
     * iothread when migrate_recover is used with OOB=on, so we need to
     * take the lock and use the full version to specify the correct
     * context.
     */
    QEMU_LOCK_GUARD(&mis->channels_early.mutex);

    /*
     * We should never watch an @ioc that is not peekable, because there's
     * no point.  What is worse is we lose the real order of accept()s via
     * the asynchronous IO watch operation.
     *
     * Another note is TLS channel (non-peekable) may or may not work
     * properly with IO watch due to its current .io_create_watch() impl,
     * which is another story.  Just guard both points.
     */
    assert(qio_channel_is_peekable(ioc));
    io_tag = qio_channel_add_watch_full(ioc, G_IO_IN,
                                        migration_incoming_channel_readable,
                                        mis, NULL, context);

    /*
     * Replace this if one day qio_channel_add_watch*() API can directly
     * return the GSource*.. for now, stick with it.
     */
    source = g_main_context_find_source_by_id(context, io_tag);
    /*
     * Nothing can race with adding the IO watch, aka, concurrent removal
     * is not possible when we have the lock.  So it must be present.
     */
    assert(source);

    migration_incoming_early_channel_insert(mis, ioc, source);
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

    trace_migration_incoming_channel_process(
        ioc, object_get_typename(OBJECT(ioc)));

    if (migrate_channel_requires_tls_upgrade(ioc)) {
        migration_tls_channel_process_incoming(ioc, &local_err);
    } else {
        if (migration_has_main_and_multifd_channels()) {
            /*
             * If all main+multifd channels are present already, this must
             * be the preempt channel.
             *
             * QEMU can't register an IO watch for it if there is only the
             * last preempt channel left, because it means the IO watch
             * will never fire and nobody will pick it up: we rely on the
             * one before the last one to pick both.
             *
             * See comment in migration_incoming_channel_readable() on
             * the migration_incoming_detect_preempt_channel() call.
             */
            migration_incoming_channel_preempt_setup(ioc);
        } else {
            if (qio_channel_is_peekable(ioc)) {
                /*
                 * Register an IO watch for the peekable channel, deciding
                 * the type of channel later by peeking at it whenever
                 * possible.  migration_incoming_channel_install() will be
                 * postponed.
                 */
                migration_incoming_channel_watch(mis, ioc);
            } else {
                /*
                 * Non-peekable channels (file, TLS, etc.), cannot register
                 * IO watch because (1) there's nothing to peek, (2) after
                 * registering IO watch, QEMU will lose the real ordering
                 * it gets from accept(), which is still so far the only
                 * source of truth to identify a channel in this case.
                 */
                ch = migration_channel_identify(mis, ioc, &local_err);
                if (ch) {
                    migration_incoming_channel_install(mis, ioc, ch,
                                                       &local_err);
                }
            }
        }
    }

    if (local_err) {
        migration_incoming_error_propagate(mis, local_err);
    }
}

void migration_channel_connect_outgoing(MigrationState *s, QIOChannel *ioc)
{
    trace_migration_outgoing_channel_set(ioc, object_get_typename(OBJECT(ioc)));

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
        } else if (len == QIO_CHANNEL_ERR_BLOCK) {
            qio_channel_wait_cond(ioc, G_IO_IN);
        } else {
            /*
             * When partially ready, we can't use qio_channel_wait_cond()
             * because it will return immediately.  Apply a manual wait.
             */
            assert(!qemu_in_coroutine());
            g_usleep(1000);
        }
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
