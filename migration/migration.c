/*
 * QEMU live migration
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
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
#include "qemu/main-loop.h"
#include "migration/blocker.h"
#include "exec.h"
#include "fd.h"
#include "file.h"
#include "socket.h"
#include "sysemu/runstate.h"
#include "sysemu/sysemu.h"
#include "sysemu/cpu-throttle.h"
#include "rdma.h"
#include "ram.h"
#include "ram-compress.h"
#include "migration/global_state.h"
#include "migration/misc.h"
#include "migration.h"
#include "migration-stats.h"
#include "savevm.h"
#include "qemu-file.h"
#include "channel.h"
#include "migration/vmstate.h"
#include "block/block.h"
#include "qapi/error.h"
#include "qapi/clone-visitor.h"
#include "qapi/qapi-visit-migration.h"
#include "qapi/qapi-visit-sockets.h"
#include "qapi/qapi-commands-migration.h"
#include "qapi/qapi-events-migration.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/qnull.h"
#include "qemu/rcu.h"
#include "block.h"
#include "postcopy-ram.h"
#include "qemu/thread.h"
#include "trace.h"
#include "exec/target_page.h"
#include "io/channel-buffer.h"
#include "io/channel-tls.h"
#include "migration/colo.h"
#include "hw/boards.h"
#include "monitor/monitor.h"
#include "net/announce.h"
#include "qemu/queue.h"
#include "multifd.h"
#include "threadinfo.h"
#include "qemu/yank.h"
#include "sysemu/cpus.h"
#include "yank_functions.h"
#include "sysemu/qtest.h"
#include "options.h"
#include "sysemu/dirtylimit.h"
#include "qemu/sockets.h"

static NotifierList migration_state_notifiers =
    NOTIFIER_LIST_INITIALIZER(migration_state_notifiers);

/* Messages sent on the return path from destination to source */
enum mig_rp_message_type {
    MIG_RP_MSG_INVALID = 0,  /* Must be 0 */
    MIG_RP_MSG_SHUT,         /* sibling will not send any more RP messages */
    MIG_RP_MSG_PONG,         /* Response to a PING; data (seq: be32 ) */

    MIG_RP_MSG_REQ_PAGES_ID, /* data (start: be64, len: be32, id: string) */
    MIG_RP_MSG_REQ_PAGES,    /* data (start: be64, len: be32) */
    MIG_RP_MSG_RECV_BITMAP,  /* send recved_bitmap back to source */
    MIG_RP_MSG_RESUME_ACK,   /* tell source that we are ready to resume */
    MIG_RP_MSG_SWITCHOVER_ACK, /* Tell source it's OK to do switchover */

    MIG_RP_MSG_MAX
};

/* When we add fault tolerance, we could have several
   migrations at once.  For now we don't need to add
   dynamic creation of migration */

static MigrationState *current_migration;
static MigrationIncomingState *current_incoming;

static GSList *migration_blockers[MIG_MODE__MAX];

static bool migration_object_check(MigrationState *ms, Error **errp);
static int migration_maybe_pause(MigrationState *s,
                                 int *current_active_state,
                                 int new_state);
static void migrate_fd_cancel(MigrationState *s);
static bool close_return_path_on_source(MigrationState *s);

static void migration_downtime_start(MigrationState *s)
{
    trace_vmstate_downtime_checkpoint("src-downtime-start");
    s->downtime_start = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
}

static void migration_downtime_end(MigrationState *s)
{
    int64_t now = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);

    /*
     * If downtime already set, should mean that postcopy already set it,
     * then that should be the real downtime already.
     */
    if (!s->downtime) {
        s->downtime = now - s->downtime_start;
    }

    trace_vmstate_downtime_checkpoint("src-downtime-end");
}

static bool migration_needs_multiple_sockets(void)
{
    return migrate_multifd() || migrate_postcopy_preempt();
}

static bool transport_supports_multi_channels(SocketAddress *saddr)
{
    return saddr->type == SOCKET_ADDRESS_TYPE_INET ||
           saddr->type == SOCKET_ADDRESS_TYPE_UNIX ||
           saddr->type == SOCKET_ADDRESS_TYPE_VSOCK;
}

static bool
migration_channels_and_transport_compatible(MigrationAddress *addr,
                                            Error **errp)
{
    if (migration_needs_multiple_sockets() &&
        (addr->transport == MIGRATION_ADDRESS_TYPE_SOCKET) &&
        !transport_supports_multi_channels(&addr->u.socket)) {
        error_setg(errp, "Migration requires multi-channel URIs (e.g. tcp)");
        return false;
    }

    return true;
}

static gint page_request_addr_cmp(gconstpointer ap, gconstpointer bp)
{
    uintptr_t a = (uintptr_t) ap, b = (uintptr_t) bp;

    return (a > b) - (a < b);
}

int migration_stop_vm(RunState state)
{
    int ret = vm_stop_force_state(state);

    trace_vmstate_downtime_checkpoint("src-vm-stopped");

    return ret;
}

void migration_object_init(void)
{
    /* This can only be called once. */
    assert(!current_migration);
    current_migration = MIGRATION_OBJ(object_new(TYPE_MIGRATION));

    /*
     * Init the migrate incoming object as well no matter whether
     * we'll use it or not.
     */
    assert(!current_incoming);
    current_incoming = g_new0(MigrationIncomingState, 1);
    current_incoming->state = MIGRATION_STATUS_NONE;
    current_incoming->postcopy_remote_fds =
        g_array_new(FALSE, TRUE, sizeof(struct PostCopyFD));
    qemu_mutex_init(&current_incoming->rp_mutex);
    qemu_mutex_init(&current_incoming->postcopy_prio_thread_mutex);
    qemu_event_init(&current_incoming->main_thread_load_event, false);
    qemu_sem_init(&current_incoming->postcopy_pause_sem_dst, 0);
    qemu_sem_init(&current_incoming->postcopy_pause_sem_fault, 0);
    qemu_sem_init(&current_incoming->postcopy_pause_sem_fast_load, 0);
    qemu_sem_init(&current_incoming->postcopy_qemufile_dst_done, 0);

    qemu_mutex_init(&current_incoming->page_request_mutex);
    qemu_cond_init(&current_incoming->page_request_cond);
    current_incoming->page_requested = g_tree_new(page_request_addr_cmp);

    migration_object_check(current_migration, &error_fatal);

    blk_mig_init();
    ram_mig_init();
    dirty_bitmap_mig_init();
}

void migration_cancel(const Error *error)
{
    if (error) {
        migrate_set_error(current_migration, error);
    }
    if (migrate_dirty_limit()) {
        qmp_cancel_vcpu_dirty_limit(false, -1, NULL);
    }
    migrate_fd_cancel(current_migration);
}

void migration_shutdown(void)
{
    /*
     * When the QEMU main thread exit, the COLO thread
     * may wait a semaphore. So, we should wakeup the
     * COLO thread before migration shutdown.
     */
    colo_shutdown();
    /*
     * Cancel the current migration - that will (eventually)
     * stop the migration using this structure
     */
    migration_cancel(NULL);
    object_unref(OBJECT(current_migration));

    /*
     * Cancel outgoing migration of dirty bitmaps. It should
     * at least unref used block nodes.
     */
    dirty_bitmap_mig_cancel_outgoing();

    /*
     * Cancel incoming migration of dirty bitmaps. Dirty bitmaps
     * are non-critical data, and their loss never considered as
     * something serious.
     */
    dirty_bitmap_mig_cancel_incoming();
}

/* For outgoing */
MigrationState *migrate_get_current(void)
{
    /* This can only be called after the object created. */
    assert(current_migration);
    return current_migration;
}

MigrationIncomingState *migration_incoming_get_current(void)
{
    assert(current_incoming);
    return current_incoming;
}

void migration_incoming_transport_cleanup(MigrationIncomingState *mis)
{
    if (mis->socket_address_list) {
        qapi_free_SocketAddressList(mis->socket_address_list);
        mis->socket_address_list = NULL;
    }

    if (mis->transport_cleanup) {
        mis->transport_cleanup(mis->transport_data);
        mis->transport_data = mis->transport_cleanup = NULL;
    }
}

void migration_incoming_state_destroy(void)
{
    struct MigrationIncomingState *mis = migration_incoming_get_current();

    multifd_load_cleanup();
    compress_threads_load_cleanup();

    if (mis->to_src_file) {
        /* Tell source that we are done */
        migrate_send_rp_shut(mis, qemu_file_get_error(mis->from_src_file) != 0);
        qemu_fclose(mis->to_src_file);
        mis->to_src_file = NULL;
    }

    if (mis->from_src_file) {
        migration_ioc_unregister_yank_from_file(mis->from_src_file);
        qemu_fclose(mis->from_src_file);
        mis->from_src_file = NULL;
    }
    if (mis->postcopy_remote_fds) {
        g_array_free(mis->postcopy_remote_fds, TRUE);
        mis->postcopy_remote_fds = NULL;
    }

    migration_incoming_transport_cleanup(mis);
    qemu_event_reset(&mis->main_thread_load_event);

    if (mis->page_requested) {
        g_tree_destroy(mis->page_requested);
        mis->page_requested = NULL;
    }

    if (mis->postcopy_qemufile_dst) {
        migration_ioc_unregister_yank_from_file(mis->postcopy_qemufile_dst);
        qemu_fclose(mis->postcopy_qemufile_dst);
        mis->postcopy_qemufile_dst = NULL;
    }

    yank_unregister_instance(MIGRATION_YANK_INSTANCE);
}

static void migrate_generate_event(int new_state)
{
    if (migrate_events()) {
        qapi_event_send_migration(new_state);
    }
}

/*
 * Send a message on the return channel back to the source
 * of the migration.
 */
static int migrate_send_rp_message(MigrationIncomingState *mis,
                                   enum mig_rp_message_type message_type,
                                   uint16_t len, void *data)
{
    int ret = 0;

    trace_migrate_send_rp_message((int)message_type, len);
    QEMU_LOCK_GUARD(&mis->rp_mutex);

    /*
     * It's possible that the file handle got lost due to network
     * failures.
     */
    if (!mis->to_src_file) {
        ret = -EIO;
        return ret;
    }

    qemu_put_be16(mis->to_src_file, (unsigned int)message_type);
    qemu_put_be16(mis->to_src_file, len);
    qemu_put_buffer(mis->to_src_file, data, len);
    return qemu_fflush(mis->to_src_file);
}

/* Request one page from the source VM at the given start address.
 *   rb: the RAMBlock to request the page in
 *   Start: Address offset within the RB
 *   Len: Length in bytes required - must be a multiple of pagesize
 */
int migrate_send_rp_message_req_pages(MigrationIncomingState *mis,
                                      RAMBlock *rb, ram_addr_t start)
{
    uint8_t bufc[12 + 1 + 255]; /* start (8), len (4), rbname up to 256 */
    size_t msglen = 12; /* start + len */
    size_t len = qemu_ram_pagesize(rb);
    enum mig_rp_message_type msg_type;
    const char *rbname;
    int rbname_len;

    *(uint64_t *)bufc = cpu_to_be64((uint64_t)start);
    *(uint32_t *)(bufc + 8) = cpu_to_be32((uint32_t)len);

    /*
     * We maintain the last ramblock that we requested for page.  Note that we
     * don't need locking because this function will only be called within the
     * postcopy ram fault thread.
     */
    if (rb != mis->last_rb) {
        mis->last_rb = rb;

        rbname = qemu_ram_get_idstr(rb);
        rbname_len = strlen(rbname);

        assert(rbname_len < 256);

        bufc[msglen++] = rbname_len;
        memcpy(bufc + msglen, rbname, rbname_len);
        msglen += rbname_len;
        msg_type = MIG_RP_MSG_REQ_PAGES_ID;
    } else {
        msg_type = MIG_RP_MSG_REQ_PAGES;
    }

    return migrate_send_rp_message(mis, msg_type, msglen, bufc);
}

int migrate_send_rp_req_pages(MigrationIncomingState *mis,
                              RAMBlock *rb, ram_addr_t start, uint64_t haddr)
{
    void *aligned = (void *)(uintptr_t)ROUND_DOWN(haddr, qemu_ram_pagesize(rb));
    bool received = false;

    WITH_QEMU_LOCK_GUARD(&mis->page_request_mutex) {
        received = ramblock_recv_bitmap_test_byte_offset(rb, start);
        if (!received && !g_tree_lookup(mis->page_requested, aligned)) {
            /*
             * The page has not been received, and it's not yet in the page
             * request list.  Queue it.  Set the value of element to 1, so that
             * things like g_tree_lookup() will return TRUE (1) when found.
             */
            g_tree_insert(mis->page_requested, aligned, (gpointer)1);
            qatomic_inc(&mis->page_requested_count);
            trace_postcopy_page_req_add(aligned, mis->page_requested_count);
        }
    }

    /*
     * If the page is there, skip sending the message.  We don't even need the
     * lock because as long as the page arrived, it'll be there forever.
     */
    if (received) {
        return 0;
    }

    return migrate_send_rp_message_req_pages(mis, rb, start);
}

static bool migration_colo_enabled;
bool migration_incoming_colo_enabled(void)
{
    return migration_colo_enabled;
}

void migration_incoming_disable_colo(void)
{
    ram_block_discard_disable(false);
    migration_colo_enabled = false;
}

int migration_incoming_enable_colo(void)
{
#ifndef CONFIG_REPLICATION
    error_report("ENABLE_COLO command come in migration stream, but COLO "
                 "module is not built in");
    return -ENOTSUP;
#endif

    if (!migrate_colo()) {
        error_report("ENABLE_COLO command come in migration stream, but c-colo "
                     "capability is not set");
        return -EINVAL;
    }

    if (ram_block_discard_disable(true)) {
        error_report("COLO: cannot disable RAM discard");
        return -EBUSY;
    }
    migration_colo_enabled = true;
    return 0;
}

void migrate_add_address(SocketAddress *address)
{
    MigrationIncomingState *mis = migration_incoming_get_current();

    QAPI_LIST_PREPEND(mis->socket_address_list,
                      QAPI_CLONE(SocketAddress, address));
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

static void qemu_start_incoming_migration(const char *uri, bool has_channels,
                                          MigrationChannelList *channels,
                                          Error **errp)
{
    g_autoptr(MigrationChannel) channel = NULL;
    MigrationAddress *addr = NULL;
    MigrationIncomingState *mis = migration_incoming_get_current();

    /*
     * Having preliminary checks for uri and channel
     */
    if (uri && has_channels) {
        error_setg(errp, "'uri' and 'channels' arguments are mutually "
                   "exclusive; exactly one of the two should be present in "
                   "'migrate-incoming' qmp command ");
        return;
    } else if (channels) {
        /* To verify that Migrate channel list has only item */
        if (channels->next) {
            error_setg(errp, "Channel list has more than one entries");
            return;
        }
        addr = channels->value->addr;
    } else if (uri) {
        /* caller uses the old URI syntax */
        if (!migrate_uri_parse(uri, &channel, errp)) {
            return;
        }
        addr = channel->addr;
    } else {
        error_setg(errp, "neither 'uri' or 'channels' argument are "
                   "specified in 'migrate-incoming' qmp command ");
        return;
    }

    /* transport mechanism not suitable for migration? */
    if (!migration_channels_and_transport_compatible(addr, errp)) {
        return;
    }

    migrate_set_state(&mis->state, MIGRATION_STATUS_NONE,
                      MIGRATION_STATUS_SETUP);

    if (addr->transport == MIGRATION_ADDRESS_TYPE_SOCKET) {
        SocketAddress *saddr = &addr->u.socket;
        if (saddr->type == SOCKET_ADDRESS_TYPE_INET ||
            saddr->type == SOCKET_ADDRESS_TYPE_UNIX ||
            saddr->type == SOCKET_ADDRESS_TYPE_VSOCK) {
            socket_start_incoming_migration(saddr, errp);
        } else if (saddr->type == SOCKET_ADDRESS_TYPE_FD) {
            fd_start_incoming_migration(saddr->u.fd.str, errp);
        }
#ifdef CONFIG_RDMA
    } else if (addr->transport == MIGRATION_ADDRESS_TYPE_RDMA) {
        if (migrate_compress()) {
            error_setg(errp, "RDMA and compression can't be used together");
            return;
        }
        if (migrate_xbzrle()) {
            error_setg(errp, "RDMA and XBZRLE can't be used together");
            return;
        }
        if (migrate_multifd()) {
            error_setg(errp, "RDMA and multifd can't be used together");
            return;
        }
        rdma_start_incoming_migration(&addr->u.rdma, errp);
#endif
    } else if (addr->transport == MIGRATION_ADDRESS_TYPE_EXEC) {
        exec_start_incoming_migration(addr->u.exec.args, errp);
    } else if (addr->transport == MIGRATION_ADDRESS_TYPE_FILE) {
        file_start_incoming_migration(&addr->u.file, errp);
    } else {
        error_setg(errp, "unknown migration protocol: %s", uri);
    }
}

static void process_incoming_migration_bh(void *opaque)
{
    Error *local_err = NULL;
    MigrationIncomingState *mis = opaque;

    trace_vmstate_downtime_checkpoint("dst-precopy-bh-enter");

    /* If capability late_block_activate is set:
     * Only fire up the block code now if we're going to restart the
     * VM, else 'cont' will do it.
     * This causes file locking to happen; so we don't want it to happen
     * unless we really are starting the VM.
     */
    if (!migrate_late_block_activate() ||
         (autostart && (!global_state_received() ||
            global_state_get_runstate() == RUN_STATE_RUNNING))) {
        /* Make sure all file formats throw away their mutable metadata.
         * If we get an error here, just don't restart the VM yet. */
        bdrv_activate_all(&local_err);
        if (local_err) {
            error_report_err(local_err);
            local_err = NULL;
            autostart = false;
        }
    }

    /*
     * This must happen after all error conditions are dealt with and
     * we're sure the VM is going to be running on this host.
     */
    qemu_announce_self(&mis->announce_timer, migrate_announce_params());

    trace_vmstate_downtime_checkpoint("dst-precopy-bh-announced");

    multifd_load_shutdown();

    dirty_bitmap_mig_before_vm_start();

    if (!global_state_received() ||
        global_state_get_runstate() == RUN_STATE_RUNNING) {
        if (autostart) {
            vm_start();
        } else {
            runstate_set(RUN_STATE_PAUSED);
        }
    } else if (migration_incoming_colo_enabled()) {
        migration_incoming_disable_colo();
        vm_start();
    } else {
        runstate_set(global_state_get_runstate());
    }
    trace_vmstate_downtime_checkpoint("dst-precopy-bh-vm-started");
    /*
     * This must happen after any state changes since as soon as an external
     * observer sees this event they might start to prod at the VM assuming
     * it's ready to use.
     */
    migrate_set_state(&mis->state, MIGRATION_STATUS_ACTIVE,
                      MIGRATION_STATUS_COMPLETED);
    qemu_bh_delete(mis->bh);
    migration_incoming_state_destroy();
}

static void coroutine_fn
process_incoming_migration_co(void *opaque)
{
    MigrationIncomingState *mis = migration_incoming_get_current();
    PostcopyState ps;
    int ret;

    assert(mis->from_src_file);

    if (compress_threads_load_setup(mis->from_src_file)) {
        error_report("Failed to setup decompress threads");
        goto fail;
    }

    mis->largest_page_size = qemu_ram_pagesize_largest();
    postcopy_state_set(POSTCOPY_INCOMING_NONE);
    migrate_set_state(&mis->state, MIGRATION_STATUS_SETUP,
                      MIGRATION_STATUS_ACTIVE);

    mis->loadvm_co = qemu_coroutine_self();
    ret = qemu_loadvm_state(mis->from_src_file);
    mis->loadvm_co = NULL;

    trace_vmstate_downtime_checkpoint("dst-precopy-loadvm-completed");

    ps = postcopy_state_get();
    trace_process_incoming_migration_co_end(ret, ps);
    if (ps != POSTCOPY_INCOMING_NONE) {
        if (ps == POSTCOPY_INCOMING_ADVISE) {
            /*
             * Where a migration had postcopy enabled (and thus went to advise)
             * but managed to complete within the precopy period, we can use
             * the normal exit.
             */
            postcopy_ram_incoming_cleanup(mis);
        } else if (ret >= 0) {
            /*
             * Postcopy was started, cleanup should happen at the end of the
             * postcopy thread.
             */
            trace_process_incoming_migration_co_postcopy_end_main();
            return;
        }
        /* Else if something went wrong then just fall out of the normal exit */
    }

    if (ret < 0) {
        error_report("load of migration failed: %s", strerror(-ret));
        goto fail;
    }

    if (colo_incoming_co() < 0) {
        goto fail;
    }

    mis->bh = qemu_bh_new(process_incoming_migration_bh, mis);
    qemu_bh_schedule(mis->bh);
    return;
fail:
    migrate_set_state(&mis->state, MIGRATION_STATUS_ACTIVE,
                      MIGRATION_STATUS_FAILED);
    qemu_fclose(mis->from_src_file);

    multifd_load_cleanup();
    compress_threads_load_cleanup();

    exit(EXIT_FAILURE);
}

/**
 * migration_incoming_setup: Setup incoming migration
 * @f: file for main migration channel
 * @errp: where to put errors
 *
 * Returns: %true on success, %false on error.
 */
static bool migration_incoming_setup(QEMUFile *f, Error **errp)
{
    MigrationIncomingState *mis = migration_incoming_get_current();

    if (!mis->from_src_file) {
        mis->from_src_file = f;
    }
    qemu_file_set_blocking(f, false);
    return true;
}

void migration_incoming_process(void)
{
    Coroutine *co = qemu_coroutine_create(process_incoming_migration_co, NULL);
    qemu_coroutine_enter(co);
}

/* Returns true if recovered from a paused migration, otherwise false */
static bool postcopy_try_recover(void)
{
    MigrationIncomingState *mis = migration_incoming_get_current();

    if (mis->state == MIGRATION_STATUS_POSTCOPY_PAUSED) {
        /* Resumed from a paused postcopy migration */

        /* This should be set already in migration_incoming_setup() */
        assert(mis->from_src_file);
        /* Postcopy has standalone thread to do vm load */
        qemu_file_set_blocking(mis->from_src_file, true);

        /* Re-configure the return path */
        mis->to_src_file = qemu_file_get_return_path(mis->from_src_file);

        migrate_set_state(&mis->state, MIGRATION_STATUS_POSTCOPY_PAUSED,
                          MIGRATION_STATUS_POSTCOPY_RECOVER);

        /*
         * Here, we only wake up the main loading thread (while the
         * rest threads will still be waiting), so that we can receive
         * commands from source now, and answer it if needed. The
         * rest threads will be woken up afterwards until we are sure
         * that source is ready to reply to page requests.
         */
        qemu_sem_post(&mis->postcopy_pause_sem_dst);
        return true;
    }

    return false;
}

void migration_fd_process_incoming(QEMUFile *f, Error **errp)
{
    if (!migration_incoming_setup(f, errp)) {
        return;
    }
    if (postcopy_try_recover()) {
        return;
    }
    migration_incoming_process();
}

/*
 * Returns true when we want to start a new incoming migration process,
 * false otherwise.
 */
static bool migration_should_start_incoming(bool main_channel)
{
    /* Multifd doesn't start unless all channels are established */
    if (migrate_multifd()) {
        return migration_has_all_channels();
    }

    /* Preempt channel only starts when the main channel is created */
    if (migrate_postcopy_preempt()) {
        return main_channel;
    }

    /*
     * For all the rest types of migration, we should only reach here when
     * it's the main channel that's being created, and we should always
     * proceed with this channel.
     */
    assert(main_channel);
    return true;
}

void migration_ioc_process_incoming(QIOChannel *ioc, Error **errp)
{
    MigrationIncomingState *mis = migration_incoming_get_current();
    Error *local_err = NULL;
    QEMUFile *f;
    bool default_channel = true;
    uint32_t channel_magic = 0;
    int ret = 0;

    if (migrate_multifd() && !migrate_postcopy_ram() &&
        qio_channel_has_feature(ioc, QIO_CHANNEL_FEATURE_READ_MSG_PEEK)) {
        /*
         * With multiple channels, it is possible that we receive channels
         * out of order on destination side, causing incorrect mapping of
         * source channels on destination side. Check channel MAGIC to
         * decide type of channel. Please note this is best effort, postcopy
         * preempt channel does not send any magic number so avoid it for
         * postcopy live migration. Also tls live migration already does
         * tls handshake while initializing main channel so with tls this
         * issue is not possible.
         */
        ret = migration_channel_read_peek(ioc, (void *)&channel_magic,
                                          sizeof(channel_magic), &local_err);

        if (ret != 0) {
            error_propagate(errp, local_err);
            return;
        }

        default_channel = (channel_magic == cpu_to_be32(QEMU_VM_FILE_MAGIC));
    } else {
        default_channel = !mis->from_src_file;
    }

    if (multifd_load_setup(errp) != 0) {
        error_setg(errp, "Failed to setup multifd channels");
        return;
    }

    if (default_channel) {
        f = qemu_file_new_input(ioc);

        if (!migration_incoming_setup(f, errp)) {
            return;
        }
    } else {
        /* Multiple connections */
        assert(migration_needs_multiple_sockets());
        if (migrate_multifd()) {
            multifd_recv_new_channel(ioc, &local_err);
        } else {
            assert(migrate_postcopy_preempt());
            f = qemu_file_new_input(ioc);
            postcopy_preempt_new_channel(mis, f);
        }
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }

    if (migration_should_start_incoming(default_channel)) {
        /* If it's a recovery, we're done */
        if (postcopy_try_recover()) {
            return;
        }
        migration_incoming_process();
    }
}

/**
 * @migration_has_all_channels: We have received all channels that we need
 *
 * Returns true when we have got connections to all the channels that
 * we need for migration.
 */
bool migration_has_all_channels(void)
{
    MigrationIncomingState *mis = migration_incoming_get_current();

    if (!mis->from_src_file) {
        return false;
    }

    if (migrate_multifd()) {
        return multifd_recv_all_channels_created();
    }

    if (migrate_postcopy_preempt()) {
        return mis->postcopy_qemufile_dst != NULL;
    }

    return true;
}

int migrate_send_rp_switchover_ack(MigrationIncomingState *mis)
{
    return migrate_send_rp_message(mis, MIG_RP_MSG_SWITCHOVER_ACK, 0, NULL);
}

/*
 * Send a 'SHUT' message on the return channel with the given value
 * to indicate that we've finished with the RP.  Non-0 value indicates
 * error.
 */
void migrate_send_rp_shut(MigrationIncomingState *mis,
                          uint32_t value)
{
    uint32_t buf;

    buf = cpu_to_be32(value);
    migrate_send_rp_message(mis, MIG_RP_MSG_SHUT, sizeof(buf), &buf);
}

/*
 * Send a 'PONG' message on the return channel with the given value
 * (normally in response to a 'PING')
 */
void migrate_send_rp_pong(MigrationIncomingState *mis,
                          uint32_t value)
{
    uint32_t buf;

    buf = cpu_to_be32(value);
    migrate_send_rp_message(mis, MIG_RP_MSG_PONG, sizeof(buf), &buf);
}

void migrate_send_rp_recv_bitmap(MigrationIncomingState *mis,
                                 char *block_name)
{
    char buf[512];
    int len;
    int64_t res;

    /*
     * First, we send the header part. It contains only the len of
     * idstr, and the idstr itself.
     */
    len = strlen(block_name);
    buf[0] = len;
    memcpy(buf + 1, block_name, len);

    if (mis->state != MIGRATION_STATUS_POSTCOPY_RECOVER) {
        error_report("%s: MSG_RP_RECV_BITMAP only used for recovery",
                     __func__);
        return;
    }

    migrate_send_rp_message(mis, MIG_RP_MSG_RECV_BITMAP, len + 1, buf);

    /*
     * Next, we dump the received bitmap to the stream.
     *
     * TODO: currently we are safe since we are the only one that is
     * using the to_src_file handle (fault thread is still paused),
     * and it's ok even not taking the mutex. However the best way is
     * to take the lock before sending the message header, and release
     * the lock after sending the bitmap.
     */
    qemu_mutex_lock(&mis->rp_mutex);
    res = ramblock_recv_bitmap_send(mis->to_src_file, block_name);
    qemu_mutex_unlock(&mis->rp_mutex);

    trace_migrate_send_rp_recv_bitmap(block_name, res);
}

void migrate_send_rp_resume_ack(MigrationIncomingState *mis, uint32_t value)
{
    uint32_t buf;

    buf = cpu_to_be32(value);
    migrate_send_rp_message(mis, MIG_RP_MSG_RESUME_ACK, sizeof(buf), &buf);
}

/*
 * Return true if we're already in the middle of a migration
 * (i.e. any of the active or setup states)
 */
bool migration_is_setup_or_active(int state)
{
    switch (state) {
    case MIGRATION_STATUS_ACTIVE:
    case MIGRATION_STATUS_POSTCOPY_ACTIVE:
    case MIGRATION_STATUS_POSTCOPY_PAUSED:
    case MIGRATION_STATUS_POSTCOPY_RECOVER:
    case MIGRATION_STATUS_SETUP:
    case MIGRATION_STATUS_PRE_SWITCHOVER:
    case MIGRATION_STATUS_DEVICE:
    case MIGRATION_STATUS_WAIT_UNPLUG:
    case MIGRATION_STATUS_COLO:
        return true;

    default:
        return false;

    }
}

bool migration_is_running(int state)
{
    switch (state) {
    case MIGRATION_STATUS_ACTIVE:
    case MIGRATION_STATUS_POSTCOPY_ACTIVE:
    case MIGRATION_STATUS_POSTCOPY_PAUSED:
    case MIGRATION_STATUS_POSTCOPY_RECOVER:
    case MIGRATION_STATUS_SETUP:
    case MIGRATION_STATUS_PRE_SWITCHOVER:
    case MIGRATION_STATUS_DEVICE:
    case MIGRATION_STATUS_WAIT_UNPLUG:
    case MIGRATION_STATUS_CANCELLING:
        return true;

    default:
        return false;

    }
}

static bool migrate_show_downtime(MigrationState *s)
{
    return (s->state == MIGRATION_STATUS_COMPLETED) || migration_in_postcopy();
}

static void populate_time_info(MigrationInfo *info, MigrationState *s)
{
    info->has_status = true;
    info->has_setup_time = true;
    info->setup_time = s->setup_time;

    if (s->state == MIGRATION_STATUS_COMPLETED) {
        info->has_total_time = true;
        info->total_time = s->total_time;
    } else {
        info->has_total_time = true;
        info->total_time = qemu_clock_get_ms(QEMU_CLOCK_REALTIME) -
                           s->start_time;
    }

    if (migrate_show_downtime(s)) {
        info->has_downtime = true;
        info->downtime = s->downtime;
    } else {
        info->has_expected_downtime = true;
        info->expected_downtime = s->expected_downtime;
    }
}

static void populate_ram_info(MigrationInfo *info, MigrationState *s)
{
    size_t page_size = qemu_target_page_size();

    info->ram = g_malloc0(sizeof(*info->ram));
    info->ram->transferred = migration_transferred_bytes();
    info->ram->total = ram_bytes_total();
    info->ram->duplicate = stat64_get(&mig_stats.zero_pages);
    /* legacy value.  It is not used anymore */
    info->ram->skipped = 0;
    info->ram->normal = stat64_get(&mig_stats.normal_pages);
    info->ram->normal_bytes = info->ram->normal * page_size;
    info->ram->mbps = s->mbps;
    info->ram->dirty_sync_count =
        stat64_get(&mig_stats.dirty_sync_count);
    info->ram->dirty_sync_missed_zero_copy =
        stat64_get(&mig_stats.dirty_sync_missed_zero_copy);
    info->ram->postcopy_requests =
        stat64_get(&mig_stats.postcopy_requests);
    info->ram->page_size = page_size;
    info->ram->multifd_bytes = stat64_get(&mig_stats.multifd_bytes);
    info->ram->pages_per_second = s->pages_per_second;
    info->ram->precopy_bytes = stat64_get(&mig_stats.precopy_bytes);
    info->ram->downtime_bytes = stat64_get(&mig_stats.downtime_bytes);
    info->ram->postcopy_bytes = stat64_get(&mig_stats.postcopy_bytes);

    if (migrate_xbzrle()) {
        info->xbzrle_cache = g_malloc0(sizeof(*info->xbzrle_cache));
        info->xbzrle_cache->cache_size = migrate_xbzrle_cache_size();
        info->xbzrle_cache->bytes = xbzrle_counters.bytes;
        info->xbzrle_cache->pages = xbzrle_counters.pages;
        info->xbzrle_cache->cache_miss = xbzrle_counters.cache_miss;
        info->xbzrle_cache->cache_miss_rate = xbzrle_counters.cache_miss_rate;
        info->xbzrle_cache->encoding_rate = xbzrle_counters.encoding_rate;
        info->xbzrle_cache->overflow = xbzrle_counters.overflow;
    }

    populate_compress(info);

    if (cpu_throttle_active()) {
        info->has_cpu_throttle_percentage = true;
        info->cpu_throttle_percentage = cpu_throttle_get_percentage();
    }

    if (s->state != MIGRATION_STATUS_COMPLETED) {
        info->ram->remaining = ram_bytes_remaining();
        info->ram->dirty_pages_rate =
           stat64_get(&mig_stats.dirty_pages_rate);
    }

    if (migrate_dirty_limit() && dirtylimit_in_service()) {
        info->has_dirty_limit_throttle_time_per_round = true;
        info->dirty_limit_throttle_time_per_round =
                            dirtylimit_throttle_time_per_round();

        info->has_dirty_limit_ring_full_time = true;
        info->dirty_limit_ring_full_time = dirtylimit_ring_full_time();
    }
}

static void populate_disk_info(MigrationInfo *info)
{
    if (blk_mig_active()) {
        info->disk = g_malloc0(sizeof(*info->disk));
        info->disk->transferred = blk_mig_bytes_transferred();
        info->disk->remaining = blk_mig_bytes_remaining();
        info->disk->total = blk_mig_bytes_total();
    }
}

static void fill_source_migration_info(MigrationInfo *info)
{
    MigrationState *s = migrate_get_current();
    int state = qatomic_read(&s->state);
    GSList *cur_blocker = migration_blockers[migrate_mode()];

    info->blocked_reasons = NULL;

    /*
     * There are two types of reasons a migration might be blocked;
     * a) devices marked in VMState as non-migratable, and
     * b) Explicit migration blockers
     * We need to add both of them here.
     */
    qemu_savevm_non_migratable_list(&info->blocked_reasons);

    while (cur_blocker) {
        QAPI_LIST_PREPEND(info->blocked_reasons,
                          g_strdup(error_get_pretty(cur_blocker->data)));
        cur_blocker = g_slist_next(cur_blocker);
    }
    info->has_blocked_reasons = info->blocked_reasons != NULL;

    switch (state) {
    case MIGRATION_STATUS_NONE:
        /* no migration has happened ever */
        /* do not overwrite destination migration status */
        return;
    case MIGRATION_STATUS_SETUP:
        info->has_status = true;
        info->has_total_time = false;
        break;
    case MIGRATION_STATUS_ACTIVE:
    case MIGRATION_STATUS_CANCELLING:
    case MIGRATION_STATUS_POSTCOPY_ACTIVE:
    case MIGRATION_STATUS_PRE_SWITCHOVER:
    case MIGRATION_STATUS_DEVICE:
    case MIGRATION_STATUS_POSTCOPY_PAUSED:
    case MIGRATION_STATUS_POSTCOPY_RECOVER:
        /* TODO add some postcopy stats */
        populate_time_info(info, s);
        populate_ram_info(info, s);
        populate_disk_info(info);
        migration_populate_vfio_info(info);
        break;
    case MIGRATION_STATUS_COLO:
        info->has_status = true;
        /* TODO: display COLO specific information (checkpoint info etc.) */
        break;
    case MIGRATION_STATUS_COMPLETED:
        populate_time_info(info, s);
        populate_ram_info(info, s);
        migration_populate_vfio_info(info);
        break;
    case MIGRATION_STATUS_FAILED:
        info->has_status = true;
        break;
    case MIGRATION_STATUS_CANCELLED:
        info->has_status = true;
        break;
    case MIGRATION_STATUS_WAIT_UNPLUG:
        info->has_status = true;
        break;
    }
    info->status = state;

    QEMU_LOCK_GUARD(&s->error_mutex);
    if (s->error) {
        info->error_desc = g_strdup(error_get_pretty(s->error));
    }
}

static void fill_destination_migration_info(MigrationInfo *info)
{
    MigrationIncomingState *mis = migration_incoming_get_current();

    if (mis->socket_address_list) {
        info->has_socket_address = true;
        info->socket_address =
            QAPI_CLONE(SocketAddressList, mis->socket_address_list);
    }

    switch (mis->state) {
    case MIGRATION_STATUS_NONE:
        return;
    case MIGRATION_STATUS_SETUP:
    case MIGRATION_STATUS_CANCELLING:
    case MIGRATION_STATUS_CANCELLED:
    case MIGRATION_STATUS_ACTIVE:
    case MIGRATION_STATUS_POSTCOPY_ACTIVE:
    case MIGRATION_STATUS_POSTCOPY_PAUSED:
    case MIGRATION_STATUS_POSTCOPY_RECOVER:
    case MIGRATION_STATUS_FAILED:
    case MIGRATION_STATUS_COLO:
        info->has_status = true;
        break;
    case MIGRATION_STATUS_COMPLETED:
        info->has_status = true;
        fill_destination_postcopy_migration_info(info);
        break;
    }
    info->status = mis->state;
}

MigrationInfo *qmp_query_migrate(Error **errp)
{
    MigrationInfo *info = g_malloc0(sizeof(*info));

    fill_destination_migration_info(info);
    fill_source_migration_info(info);

    return info;
}

void qmp_migrate_start_postcopy(Error **errp)
{
    MigrationState *s = migrate_get_current();

    if (!migrate_postcopy()) {
        error_setg(errp, "Enable postcopy with migrate_set_capability before"
                         " the start of migration");
        return;
    }

    if (s->state == MIGRATION_STATUS_NONE) {
        error_setg(errp, "Postcopy must be started after migration has been"
                         " started");
        return;
    }
    /*
     * we don't error if migration has finished since that would be racy
     * with issuing this command.
     */
    qatomic_set(&s->start_postcopy, true);
}

/* shared migration helpers */

void migrate_set_state(int *state, int old_state, int new_state)
{
    assert(new_state < MIGRATION_STATUS__MAX);
    if (qatomic_cmpxchg(state, old_state, new_state) == old_state) {
        trace_migrate_set_state(MigrationStatus_str(new_state));
        migrate_generate_event(new_state);
    }
}

static void migrate_fd_cleanup(MigrationState *s)
{
    qemu_bh_delete(s->cleanup_bh);
    s->cleanup_bh = NULL;

    g_free(s->hostname);
    s->hostname = NULL;
    json_writer_free(s->vmdesc);
    s->vmdesc = NULL;

    qemu_savevm_state_cleanup();

    if (s->to_dst_file) {
        QEMUFile *tmp;

        trace_migrate_fd_cleanup();
        qemu_mutex_unlock_iothread();
        if (s->migration_thread_running) {
            qemu_thread_join(&s->thread);
            s->migration_thread_running = false;
        }
        qemu_mutex_lock_iothread();

        multifd_save_cleanup();
        qemu_mutex_lock(&s->qemu_file_lock);
        tmp = s->to_dst_file;
        s->to_dst_file = NULL;
        qemu_mutex_unlock(&s->qemu_file_lock);
        /*
         * Close the file handle without the lock to make sure the
         * critical section won't block for long.
         */
        migration_ioc_unregister_yank_from_file(tmp);
        qemu_fclose(tmp);
    }

    /*
     * We already cleaned up to_dst_file, so errors from the return
     * path might be due to that, ignore them.
     */
    close_return_path_on_source(s);

    assert(!migration_is_active(s));

    if (s->state == MIGRATION_STATUS_CANCELLING) {
        migrate_set_state(&s->state, MIGRATION_STATUS_CANCELLING,
                          MIGRATION_STATUS_CANCELLED);
    }

    if (s->error) {
        /* It is used on info migrate.  We can't free it */
        error_report_err(error_copy(s->error));
    }
    migration_call_notifiers(s);
    block_cleanup_parameters();
    yank_unregister_instance(MIGRATION_YANK_INSTANCE);
}

static void migrate_fd_cleanup_schedule(MigrationState *s)
{
    /*
     * Ref the state for bh, because it may be called when
     * there're already no other refs
     */
    object_ref(OBJECT(s));
    qemu_bh_schedule(s->cleanup_bh);
}

static void migrate_fd_cleanup_bh(void *opaque)
{
    MigrationState *s = opaque;
    migrate_fd_cleanup(s);
    object_unref(OBJECT(s));
}

void migrate_set_error(MigrationState *s, const Error *error)
{
    QEMU_LOCK_GUARD(&s->error_mutex);
    if (!s->error) {
        s->error = error_copy(error);
    }
}

bool migrate_has_error(MigrationState *s)
{
    /* The lock is not helpful here, but still follow the rule */
    QEMU_LOCK_GUARD(&s->error_mutex);
    return qatomic_read(&s->error);
}

static void migrate_error_free(MigrationState *s)
{
    QEMU_LOCK_GUARD(&s->error_mutex);
    if (s->error) {
        error_free(s->error);
        s->error = NULL;
    }
}

static void migrate_fd_error(MigrationState *s, const Error *error)
{
    trace_migrate_fd_error(error_get_pretty(error));
    assert(s->to_dst_file == NULL);
    migrate_set_state(&s->state, MIGRATION_STATUS_SETUP,
                      MIGRATION_STATUS_FAILED);
    migrate_set_error(s, error);
}

static void migrate_fd_cancel(MigrationState *s)
{
    int old_state ;

    trace_migrate_fd_cancel();

    WITH_QEMU_LOCK_GUARD(&s->qemu_file_lock) {
        if (s->rp_state.from_dst_file) {
            /* shutdown the rp socket, so causing the rp thread to shutdown */
            qemu_file_shutdown(s->rp_state.from_dst_file);
        }
    }

    do {
        old_state = s->state;
        if (!migration_is_running(old_state)) {
            break;
        }
        /* If the migration is paused, kick it out of the pause */
        if (old_state == MIGRATION_STATUS_PRE_SWITCHOVER) {
            qemu_sem_post(&s->pause_sem);
        }
        migrate_set_state(&s->state, old_state, MIGRATION_STATUS_CANCELLING);
    } while (s->state != MIGRATION_STATUS_CANCELLING);

    /*
     * If we're unlucky the migration code might be stuck somewhere in a
     * send/write while the network has failed and is waiting to timeout;
     * if we've got shutdown(2) available then we can force it to quit.
     */
    if (s->state == MIGRATION_STATUS_CANCELLING) {
        WITH_QEMU_LOCK_GUARD(&s->qemu_file_lock) {
            if (s->to_dst_file) {
                qemu_file_shutdown(s->to_dst_file);
            }
        }
    }
    if (s->state == MIGRATION_STATUS_CANCELLING && s->block_inactive) {
        Error *local_err = NULL;

        bdrv_activate_all(&local_err);
        if (local_err) {
            error_report_err(local_err);
        } else {
            s->block_inactive = false;
        }
    }
}

void migration_add_notifier(Notifier *notify,
                            void (*func)(Notifier *notifier, void *data))
{
    notify->notify = func;
    notifier_list_add(&migration_state_notifiers, notify);
}

void migration_remove_notifier(Notifier *notify)
{
    if (notify->notify) {
        notifier_remove(notify);
        notify->notify = NULL;
    }
}

void migration_call_notifiers(MigrationState *s)
{
    notifier_list_notify(&migration_state_notifiers, s);
}

bool migration_in_setup(MigrationState *s)
{
    return s->state == MIGRATION_STATUS_SETUP;
}

bool migration_has_finished(MigrationState *s)
{
    return s->state == MIGRATION_STATUS_COMPLETED;
}

bool migration_has_failed(MigrationState *s)
{
    return (s->state == MIGRATION_STATUS_CANCELLED ||
            s->state == MIGRATION_STATUS_FAILED);
}

bool migration_in_postcopy(void)
{
    MigrationState *s = migrate_get_current();

    switch (s->state) {
    case MIGRATION_STATUS_POSTCOPY_ACTIVE:
    case MIGRATION_STATUS_POSTCOPY_PAUSED:
    case MIGRATION_STATUS_POSTCOPY_RECOVER:
        return true;
    default:
        return false;
    }
}

bool migration_postcopy_is_alive(int state)
{
    switch (state) {
    case MIGRATION_STATUS_POSTCOPY_ACTIVE:
    case MIGRATION_STATUS_POSTCOPY_RECOVER:
        return true;
    default:
        return false;
    }
}

bool migration_in_postcopy_after_devices(MigrationState *s)
{
    return migration_in_postcopy() && s->postcopy_after_devices;
}

bool migration_in_incoming_postcopy(void)
{
    PostcopyState ps = postcopy_state_get();

    return ps >= POSTCOPY_INCOMING_DISCARD && ps < POSTCOPY_INCOMING_END;
}

bool migration_incoming_postcopy_advised(void)
{
    PostcopyState ps = postcopy_state_get();

    return ps >= POSTCOPY_INCOMING_ADVISE && ps < POSTCOPY_INCOMING_END;
}

bool migration_in_bg_snapshot(void)
{
    MigrationState *s = migrate_get_current();

    return migrate_background_snapshot() &&
            migration_is_setup_or_active(s->state);
}

bool migration_is_idle(void)
{
    MigrationState *s = current_migration;

    if (!s) {
        return true;
    }

    switch (s->state) {
    case MIGRATION_STATUS_NONE:
    case MIGRATION_STATUS_CANCELLED:
    case MIGRATION_STATUS_COMPLETED:
    case MIGRATION_STATUS_FAILED:
        return true;
    case MIGRATION_STATUS_SETUP:
    case MIGRATION_STATUS_CANCELLING:
    case MIGRATION_STATUS_ACTIVE:
    case MIGRATION_STATUS_POSTCOPY_ACTIVE:
    case MIGRATION_STATUS_COLO:
    case MIGRATION_STATUS_PRE_SWITCHOVER:
    case MIGRATION_STATUS_DEVICE:
    case MIGRATION_STATUS_WAIT_UNPLUG:
        return false;
    case MIGRATION_STATUS__MAX:
        g_assert_not_reached();
    }

    return false;
}

bool migration_is_active(MigrationState *s)
{
    return (s->state == MIGRATION_STATUS_ACTIVE ||
            s->state == MIGRATION_STATUS_POSTCOPY_ACTIVE);
}

int migrate_init(MigrationState *s, Error **errp)
{
    int ret;

    ret = qemu_savevm_state_prepare(errp);
    if (ret) {
        return ret;
    }

    /*
     * Reinitialise all migration state, except
     * parameters/capabilities that the user set, and
     * locks.
     */
    s->cleanup_bh = 0;
    s->vm_start_bh = 0;
    s->to_dst_file = NULL;
    s->state = MIGRATION_STATUS_NONE;
    s->rp_state.from_dst_file = NULL;
    s->mbps = 0.0;
    s->pages_per_second = 0.0;
    s->downtime = 0;
    s->expected_downtime = 0;
    s->setup_time = 0;
    s->start_postcopy = false;
    s->postcopy_after_devices = false;
    s->migration_thread_running = false;
    error_free(s->error);
    s->error = NULL;
    s->hostname = NULL;
    s->vmdesc = NULL;

    migrate_set_state(&s->state, MIGRATION_STATUS_NONE, MIGRATION_STATUS_SETUP);

    s->start_time = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    s->total_time = 0;
    s->vm_old_state = -1;
    s->iteration_initial_bytes = 0;
    s->threshold_size = 0;
    s->switchover_acked = false;
    s->rdma_migration = false;
    /*
     * set mig_stats memory to zero for a new migration
     */
    memset(&mig_stats, 0, sizeof(mig_stats));
    migration_reset_vfio_bytes_transferred();

    return 0;
}

static bool is_busy(Error **reasonp, Error **errp)
{
    ERRP_GUARD();

    /* Snapshots are similar to migrations, so check RUN_STATE_SAVE_VM too. */
    if (runstate_check(RUN_STATE_SAVE_VM) || !migration_is_idle()) {
        error_propagate_prepend(errp, *reasonp,
                                "disallowing migration blocker "
                                "(migration/snapshot in progress) for: ");
        *reasonp = NULL;
        return true;
    }
    return false;
}

static bool is_only_migratable(Error **reasonp, Error **errp, int modes)
{
    ERRP_GUARD();

    if (only_migratable && (modes & BIT(MIG_MODE_NORMAL))) {
        error_propagate_prepend(errp, *reasonp,
                                "disallowing migration blocker "
                                "(--only-migratable) for: ");
        *reasonp = NULL;
        return true;
    }
    return false;
}

static int get_modes(MigMode mode, va_list ap)
{
    int modes = 0;

    while (mode != -1 && mode != MIG_MODE_ALL) {
        assert(mode >= MIG_MODE_NORMAL && mode < MIG_MODE__MAX);
        modes |= BIT(mode);
        mode = va_arg(ap, MigMode);
    }
    if (mode == MIG_MODE_ALL) {
        modes = BIT(MIG_MODE__MAX) - 1;
    }
    return modes;
}

static int add_blockers(Error **reasonp, Error **errp, int modes)
{
    for (MigMode mode = 0; mode < MIG_MODE__MAX; mode++) {
        if (modes & BIT(mode)) {
            migration_blockers[mode] = g_slist_prepend(migration_blockers[mode],
                                                       *reasonp);
        }
    }
    return 0;
}

int migrate_add_blocker(Error **reasonp, Error **errp)
{
    return migrate_add_blocker_modes(reasonp, errp, MIG_MODE_ALL);
}

int migrate_add_blocker_normal(Error **reasonp, Error **errp)
{
    return migrate_add_blocker_modes(reasonp, errp, MIG_MODE_NORMAL, -1);
}

int migrate_add_blocker_modes(Error **reasonp, Error **errp, MigMode mode, ...)
{
    int modes;
    va_list ap;

    va_start(ap, mode);
    modes = get_modes(mode, ap);
    va_end(ap);

    if (is_only_migratable(reasonp, errp, modes)) {
        return -EACCES;
    } else if (is_busy(reasonp, errp)) {
        return -EBUSY;
    }
    return add_blockers(reasonp, errp, modes);
}

int migrate_add_blocker_internal(Error **reasonp, Error **errp)
{
    int modes = BIT(MIG_MODE__MAX) - 1;

    if (is_busy(reasonp, errp)) {
        return -EBUSY;
    }
    return add_blockers(reasonp, errp, modes);
}

void migrate_del_blocker(Error **reasonp)
{
    if (*reasonp) {
        for (MigMode mode = 0; mode < MIG_MODE__MAX; mode++) {
            migration_blockers[mode] = g_slist_remove(migration_blockers[mode],
                                                      *reasonp);
        }
        error_free(*reasonp);
        *reasonp = NULL;
    }
}

void qmp_migrate_incoming(const char *uri, bool has_channels,
                          MigrationChannelList *channels, Error **errp)
{
    Error *local_err = NULL;
    static bool once = true;

    if (!once) {
        error_setg(errp, "The incoming migration has already been started");
        return;
    }
    if (!runstate_check(RUN_STATE_INMIGRATE)) {
        error_setg(errp, "'-incoming' was not specified on the command line");
        return;
    }

    if (!yank_register_instance(MIGRATION_YANK_INSTANCE, errp)) {
        return;
    }

    qemu_start_incoming_migration(uri, has_channels, channels, &local_err);

    if (local_err) {
        yank_unregister_instance(MIGRATION_YANK_INSTANCE);
        error_propagate(errp, local_err);
        return;
    }

    once = false;
}

void qmp_migrate_recover(const char *uri, Error **errp)
{
    MigrationIncomingState *mis = migration_incoming_get_current();

    /*
     * Don't even bother to use ERRP_GUARD() as it _must_ always be set by
     * callers (no one should ignore a recover failure); if there is, it's a
     * programming error.
     */
    assert(errp);

    if (mis->state != MIGRATION_STATUS_POSTCOPY_PAUSED) {
        error_setg(errp, "Migrate recover can only be run "
                   "when postcopy is paused.");
        return;
    }

    /* If there's an existing transport, release it */
    migration_incoming_transport_cleanup(mis);

    /*
     * Note that this call will never start a real migration; it will
     * only re-setup the migration stream and poke existing migration
     * to continue using that newly established channel.
     */
    qemu_start_incoming_migration(uri, false, NULL, errp);
}

void qmp_migrate_pause(Error **errp)
{
    MigrationState *ms = migrate_get_current();
    MigrationIncomingState *mis = migration_incoming_get_current();
    int ret = 0;

    if (migration_postcopy_is_alive(ms->state)) {
        /* Source side, during postcopy */
        Error *error = NULL;

        /* Tell the core migration that we're pausing */
        error_setg(&error, "Postcopy migration is paused by the user");
        migrate_set_error(ms, error);
        error_free(error);

        qemu_mutex_lock(&ms->qemu_file_lock);
        if (ms->to_dst_file) {
            ret = qemu_file_shutdown(ms->to_dst_file);
        }
        qemu_mutex_unlock(&ms->qemu_file_lock);
        if (ret) {
            error_setg(errp, "Failed to pause source migration");
        }

        /*
         * Kick the migration thread out of any waiting windows (on behalf
         * of the rp thread).
         */
        migration_rp_kick(ms);

        return;
    }

    if (migration_postcopy_is_alive(mis->state)) {
        ret = qemu_file_shutdown(mis->from_src_file);
        if (ret) {
            error_setg(errp, "Failed to pause destination migration");
        }
        return;
    }

    error_setg(errp, "migrate-pause is currently only supported "
               "during postcopy-active or postcopy-recover state");
}

bool migration_is_blocked(Error **errp)
{
    GSList *blockers = migration_blockers[migrate_mode()];

    if (qemu_savevm_state_blocked(errp)) {
        return true;
    }

    if (blockers) {
        error_propagate(errp, error_copy(blockers->data));
        return true;
    }

    return false;
}

/* Returns true if continue to migrate, or false if error detected */
static bool migrate_prepare(MigrationState *s, bool blk, bool blk_inc,
                            bool resume, Error **errp)
{
    Error *local_err = NULL;

    if (blk_inc) {
        warn_report("parameter 'inc' is deprecated;"
                    " use blockdev-mirror with NBD instead");
    }

    if (blk) {
        warn_report("parameter 'blk' is deprecated;"
                    " use blockdev-mirror with NBD instead");
    }

    if (resume) {
        if (s->state != MIGRATION_STATUS_POSTCOPY_PAUSED) {
            error_setg(errp, "Cannot resume if there is no "
                       "paused migration");
            return false;
        }

        /*
         * Postcopy recovery won't work well with release-ram
         * capability since release-ram will drop the page buffer as
         * long as the page is put into the send buffer.  So if there
         * is a network failure happened, any page buffers that have
         * not yet reached the destination VM but have already been
         * sent from the source VM will be lost forever.  Let's refuse
         * the client from resuming such a postcopy migration.
         * Luckily release-ram was designed to only be used when src
         * and destination VMs are on the same host, so it should be
         * fine.
         */
        if (migrate_release_ram()) {
            error_setg(errp, "Postcopy recovery cannot work "
                       "when release-ram capability is set");
            return false;
        }

        /* This is a resume, skip init status */
        return true;
    }

    if (migration_is_running(s->state)) {
        error_setg(errp, QERR_MIGRATION_ACTIVE);
        return false;
    }

    if (runstate_check(RUN_STATE_INMIGRATE)) {
        error_setg(errp, "Guest is waiting for an incoming migration");
        return false;
    }

    if (runstate_check(RUN_STATE_POSTMIGRATE)) {
        error_setg(errp, "Can't migrate the vm that was paused due to "
                   "previous migration");
        return false;
    }

    if (migration_is_blocked(errp)) {
        return false;
    }

    if (blk || blk_inc) {
        if (migrate_colo()) {
            error_setg(errp, "No disk migration is required in COLO mode");
            return false;
        }
        if (migrate_block() || migrate_block_incremental()) {
            error_setg(errp, "Command options are incompatible with "
                       "current migration capabilities");
            return false;
        }
        if (!migrate_cap_set(MIGRATION_CAPABILITY_BLOCK, true, &local_err)) {
            error_propagate(errp, local_err);
            return false;
        }
        s->must_remove_block_options = true;
    }

    if (blk_inc) {
        migrate_set_block_incremental(true);
    }

    if (migrate_init(s, errp)) {
        return false;
    }

    return true;
}

void qmp_migrate(const char *uri, bool has_channels,
                 MigrationChannelList *channels, bool has_blk, bool blk,
                 bool has_inc, bool inc, bool has_detach, bool detach,
                 bool has_resume, bool resume, Error **errp)
{
    bool resume_requested;
    Error *local_err = NULL;
    MigrationState *s = migrate_get_current();
    g_autoptr(MigrationChannel) channel = NULL;
    MigrationAddress *addr = NULL;

    /*
     * Having preliminary checks for uri and channel
     */
    if (uri && has_channels) {
        error_setg(errp, "'uri' and 'channels' arguments are mutually "
                   "exclusive; exactly one of the two should be present in "
                   "'migrate' qmp command ");
        return;
    } else if (channels) {
        /* To verify that Migrate channel list has only item */
        if (channels->next) {
            error_setg(errp, "Channel list has more than one entries");
            return;
        }
        addr = channels->value->addr;
    } else if (uri) {
        /* caller uses the old URI syntax */
        if (!migrate_uri_parse(uri, &channel, errp)) {
            return;
        }
        addr = channel->addr;
    } else {
        error_setg(errp, "neither 'uri' or 'channels' argument are "
                   "specified in 'migrate' qmp command ");
        return;
    }

    /* transport mechanism not suitable for migration? */
    if (!migration_channels_and_transport_compatible(addr, errp)) {
        return;
    }

    resume_requested = has_resume && resume;
    if (!migrate_prepare(s, has_blk && blk, has_inc && inc,
                         resume_requested, errp)) {
        /* Error detected, put into errp */
        return;
    }

    if (!resume_requested) {
        if (!yank_register_instance(MIGRATION_YANK_INSTANCE, errp)) {
            return;
        }
    }

    if (addr->transport == MIGRATION_ADDRESS_TYPE_SOCKET) {
        SocketAddress *saddr = &addr->u.socket;
        if (saddr->type == SOCKET_ADDRESS_TYPE_INET ||
            saddr->type == SOCKET_ADDRESS_TYPE_UNIX ||
            saddr->type == SOCKET_ADDRESS_TYPE_VSOCK) {
            socket_start_outgoing_migration(s, saddr, &local_err);
        } else if (saddr->type == SOCKET_ADDRESS_TYPE_FD) {
            fd_start_outgoing_migration(s, saddr->u.fd.str, &local_err);
        }
#ifdef CONFIG_RDMA
    } else if (addr->transport == MIGRATION_ADDRESS_TYPE_RDMA) {
        rdma_start_outgoing_migration(s, &addr->u.rdma, &local_err);
#endif
    } else if (addr->transport == MIGRATION_ADDRESS_TYPE_EXEC) {
        exec_start_outgoing_migration(s, addr->u.exec.args, &local_err);
    } else if (addr->transport == MIGRATION_ADDRESS_TYPE_FILE) {
        file_start_outgoing_migration(s, &addr->u.file, &local_err);
    } else {
        error_setg(&local_err, QERR_INVALID_PARAMETER_VALUE, "uri",
                   "a valid migration protocol");
        migrate_set_state(&s->state, MIGRATION_STATUS_SETUP,
                          MIGRATION_STATUS_FAILED);
        block_cleanup_parameters();
    }

    if (local_err) {
        if (!resume_requested) {
            yank_unregister_instance(MIGRATION_YANK_INSTANCE);
        }
        migrate_fd_error(s, local_err);
        error_propagate(errp, local_err);
        return;
    }
}

void qmp_migrate_cancel(Error **errp)
{
    migration_cancel(NULL);
}

void qmp_migrate_continue(MigrationStatus state, Error **errp)
{
    MigrationState *s = migrate_get_current();
    if (s->state != state) {
        error_setg(errp,  "Migration not in expected state: %s",
                   MigrationStatus_str(s->state));
        return;
    }
    qemu_sem_post(&s->pause_sem);
}

int migration_rp_wait(MigrationState *s)
{
    /* If migration has failure already, ignore the wait */
    if (migrate_has_error(s)) {
        return -1;
    }

    qemu_sem_wait(&s->rp_state.rp_sem);

    /* After wait, double check that there's no failure */
    if (migrate_has_error(s)) {
        return -1;
    }

    return 0;
}

void migration_rp_kick(MigrationState *s)
{
    qemu_sem_post(&s->rp_state.rp_sem);
}

static struct rp_cmd_args {
    ssize_t     len; /* -1 = variable */
    const char *name;
} rp_cmd_args[] = {
    [MIG_RP_MSG_INVALID]        = { .len = -1, .name = "INVALID" },
    [MIG_RP_MSG_SHUT]           = { .len =  4, .name = "SHUT" },
    [MIG_RP_MSG_PONG]           = { .len =  4, .name = "PONG" },
    [MIG_RP_MSG_REQ_PAGES]      = { .len = 12, .name = "REQ_PAGES" },
    [MIG_RP_MSG_REQ_PAGES_ID]   = { .len = -1, .name = "REQ_PAGES_ID" },
    [MIG_RP_MSG_RECV_BITMAP]    = { .len = -1, .name = "RECV_BITMAP" },
    [MIG_RP_MSG_RESUME_ACK]     = { .len =  4, .name = "RESUME_ACK" },
    [MIG_RP_MSG_SWITCHOVER_ACK] = { .len =  0, .name = "SWITCHOVER_ACK" },
    [MIG_RP_MSG_MAX]            = { .len = -1, .name = "MAX" },
};

/*
 * Process a request for pages received on the return path,
 * We're allowed to send more than requested (e.g. to round to our page size)
 * and we don't need to send pages that have already been sent.
 */
static void
migrate_handle_rp_req_pages(MigrationState *ms, const char* rbname,
                            ram_addr_t start, size_t len, Error **errp)
{
    long our_host_ps = qemu_real_host_page_size();

    trace_migrate_handle_rp_req_pages(rbname, start, len);

    /*
     * Since we currently insist on matching page sizes, just sanity check
     * we're being asked for whole host pages.
     */
    if (!QEMU_IS_ALIGNED(start, our_host_ps) ||
        !QEMU_IS_ALIGNED(len, our_host_ps)) {
        error_setg(errp, "MIG_RP_MSG_REQ_PAGES: Misaligned page request, start:"
                   RAM_ADDR_FMT " len: %zd", start, len);
        return;
    }

    ram_save_queue_pages(rbname, start, len, errp);
}

static bool migrate_handle_rp_recv_bitmap(MigrationState *s, char *block_name,
                                          Error **errp)
{
    RAMBlock *block = qemu_ram_block_by_name(block_name);

    if (!block) {
        error_setg(errp, "MIG_RP_MSG_RECV_BITMAP has invalid block name '%s'",
                   block_name);
        return false;
    }

    /* Fetch the received bitmap and refresh the dirty bitmap */
    return ram_dirty_bitmap_reload(s, block, errp);
}

static bool migrate_handle_rp_resume_ack(MigrationState *s,
                                         uint32_t value, Error **errp)
{
    trace_source_return_path_thread_resume_ack(value);

    if (value != MIGRATION_RESUME_ACK_VALUE) {
        error_setg(errp, "illegal resume_ack value %"PRIu32, value);
        return false;
    }

    /* Now both sides are active. */
    migrate_set_state(&s->state, MIGRATION_STATUS_POSTCOPY_RECOVER,
                      MIGRATION_STATUS_POSTCOPY_ACTIVE);

    /* Notify send thread that time to continue send pages */
    migration_rp_kick(s);

    return true;
}

/*
 * Release ms->rp_state.from_dst_file (and postcopy_qemufile_src if
 * existed) in a safe way.
 */
static void migration_release_dst_files(MigrationState *ms)
{
    QEMUFile *file;

    WITH_QEMU_LOCK_GUARD(&ms->qemu_file_lock) {
        /*
         * Reset the from_dst_file pointer first before releasing it, as we
         * can't block within lock section
         */
        file = ms->rp_state.from_dst_file;
        ms->rp_state.from_dst_file = NULL;
    }

    /*
     * Do the same to postcopy fast path socket too if there is.  No
     * locking needed because this qemufile should only be managed by
     * return path thread.
     */
    if (ms->postcopy_qemufile_src) {
        migration_ioc_unregister_yank_from_file(ms->postcopy_qemufile_src);
        qemu_file_shutdown(ms->postcopy_qemufile_src);
        qemu_fclose(ms->postcopy_qemufile_src);
        ms->postcopy_qemufile_src = NULL;
    }

    qemu_fclose(file);
}

/*
 * Handles messages sent on the return path towards the source VM
 *
 */
static void *source_return_path_thread(void *opaque)
{
    MigrationState *ms = opaque;
    QEMUFile *rp = ms->rp_state.from_dst_file;
    uint16_t header_len, header_type;
    uint8_t buf[512];
    uint32_t tmp32, sibling_error;
    ram_addr_t start = 0; /* =0 to silence warning */
    size_t  len = 0, expected_len;
    Error *err = NULL;
    int res;

    trace_source_return_path_thread_entry();
    rcu_register_thread();

    while (migration_is_setup_or_active(ms->state)) {
        trace_source_return_path_thread_loop_top();

        header_type = qemu_get_be16(rp);
        header_len = qemu_get_be16(rp);

        if (qemu_file_get_error(rp)) {
            qemu_file_get_error_obj(rp, &err);
            goto out;
        }

        if (header_type >= MIG_RP_MSG_MAX ||
            header_type == MIG_RP_MSG_INVALID) {
            error_setg(&err, "Received invalid message 0x%04x length 0x%04x",
                       header_type, header_len);
            goto out;
        }

        if ((rp_cmd_args[header_type].len != -1 &&
            header_len != rp_cmd_args[header_type].len) ||
            header_len > sizeof(buf)) {
            error_setg(&err, "Received '%s' message (0x%04x) with"
                       "incorrect length %d expecting %zu",
                       rp_cmd_args[header_type].name, header_type, header_len,
                       (size_t)rp_cmd_args[header_type].len);
            goto out;
        }

        /* We know we've got a valid header by this point */
        res = qemu_get_buffer(rp, buf, header_len);
        if (res != header_len) {
            error_setg(&err, "Failed reading data for message 0x%04x"
                       " read %d expected %d",
                       header_type, res, header_len);
            goto out;
        }

        /* OK, we have the message and the data */
        switch (header_type) {
        case MIG_RP_MSG_SHUT:
            sibling_error = ldl_be_p(buf);
            trace_source_return_path_thread_shut(sibling_error);
            if (sibling_error) {
                error_setg(&err, "Sibling indicated error %d", sibling_error);
            }
            /*
             * We'll let the main thread deal with closing the RP
             * we could do a shutdown(2) on it, but we're the only user
             * anyway, so there's nothing gained.
             */
            goto out;

        case MIG_RP_MSG_PONG:
            tmp32 = ldl_be_p(buf);
            trace_source_return_path_thread_pong(tmp32);
            qemu_sem_post(&ms->rp_state.rp_pong_acks);
            break;

        case MIG_RP_MSG_REQ_PAGES:
            start = ldq_be_p(buf);
            len = ldl_be_p(buf + 8);
            migrate_handle_rp_req_pages(ms, NULL, start, len, &err);
            if (err) {
                goto out;
            }
            break;

        case MIG_RP_MSG_REQ_PAGES_ID:
            expected_len = 12 + 1; /* header + termination */

            if (header_len >= expected_len) {
                start = ldq_be_p(buf);
                len = ldl_be_p(buf + 8);
                /* Now we expect an idstr */
                tmp32 = buf[12]; /* Length of the following idstr */
                buf[13 + tmp32] = '\0';
                expected_len += tmp32;
            }
            if (header_len != expected_len) {
                error_setg(&err, "Req_Page_id with length %d expecting %zd",
                           header_len, expected_len);
                goto out;
            }
            migrate_handle_rp_req_pages(ms, (char *)&buf[13], start, len,
                                        &err);
            if (err) {
                goto out;
            }
            break;

        case MIG_RP_MSG_RECV_BITMAP:
            if (header_len < 1) {
                error_setg(&err, "MIG_RP_MSG_RECV_BITMAP missing block name");
                goto out;
            }
            /* Format: len (1B) + idstr (<255B). This ends the idstr. */
            buf[buf[0] + 1] = '\0';
            if (!migrate_handle_rp_recv_bitmap(ms, (char *)(buf + 1), &err)) {
                goto out;
            }
            break;

        case MIG_RP_MSG_RESUME_ACK:
            tmp32 = ldl_be_p(buf);
            if (!migrate_handle_rp_resume_ack(ms, tmp32, &err)) {
                goto out;
            }
            break;

        case MIG_RP_MSG_SWITCHOVER_ACK:
            ms->switchover_acked = true;
            trace_source_return_path_thread_switchover_acked();
            break;

        default:
            break;
        }
    }

out:
    if (err) {
        migrate_set_error(ms, err);
        error_free(err);
        trace_source_return_path_thread_bad_end();
    }

    if (ms->state == MIGRATION_STATUS_POSTCOPY_RECOVER) {
        /*
         * this will be extremely unlikely: that we got yet another network
         * issue during recovering of the 1st network failure.. during this
         * period the main migration thread can be waiting on rp_sem for
         * this thread to sync with the other side.
         *
         * When this happens, explicitly kick the migration thread out of
         * RECOVER stage and back to PAUSED, so the admin can try
         * everything again.
         */
        migration_rp_kick(ms);
    }

    trace_source_return_path_thread_end();
    rcu_unregister_thread();

    return NULL;
}

static int open_return_path_on_source(MigrationState *ms)
{
    ms->rp_state.from_dst_file = qemu_file_get_return_path(ms->to_dst_file);
    if (!ms->rp_state.from_dst_file) {
        return -1;
    }

    trace_open_return_path_on_source();

    qemu_thread_create(&ms->rp_state.rp_thread, "return path",
                       source_return_path_thread, ms, QEMU_THREAD_JOINABLE);
    ms->rp_state.rp_thread_created = true;

    trace_open_return_path_on_source_continue();

    return 0;
}

/* Return true if error detected, or false otherwise */
static bool close_return_path_on_source(MigrationState *ms)
{
    if (!ms->rp_state.rp_thread_created) {
        return false;
    }

    trace_migration_return_path_end_before();

    /*
     * If this is a normal exit then the destination will send a SHUT
     * and the rp_thread will exit, however if there's an error we
     * need to cause it to exit. shutdown(2), if we have it, will
     * cause it to unblock if it's stuck waiting for the destination.
     */
    WITH_QEMU_LOCK_GUARD(&ms->qemu_file_lock) {
        if (ms->to_dst_file && ms->rp_state.from_dst_file &&
            qemu_file_get_error(ms->to_dst_file)) {
            qemu_file_shutdown(ms->rp_state.from_dst_file);
        }
    }

    qemu_thread_join(&ms->rp_state.rp_thread);
    ms->rp_state.rp_thread_created = false;
    migration_release_dst_files(ms);
    trace_migration_return_path_end_after();

    /* Return path will persist the error in MigrationState when quit */
    return migrate_has_error(ms);
}

static inline void
migration_wait_main_channel(MigrationState *ms)
{
    /* Wait until one PONG message received */
    qemu_sem_wait(&ms->rp_state.rp_pong_acks);
}

/*
 * Switch from normal iteration to postcopy
 * Returns non-0 on error
 */
static int postcopy_start(MigrationState *ms, Error **errp)
{
    int ret;
    QIOChannelBuffer *bioc;
    QEMUFile *fb;
    uint64_t bandwidth = migrate_max_postcopy_bandwidth();
    bool restart_block = false;
    int cur_state = MIGRATION_STATUS_ACTIVE;

    if (migrate_postcopy_preempt()) {
        migration_wait_main_channel(ms);
        if (postcopy_preempt_establish_channel(ms)) {
            migrate_set_state(&ms->state, ms->state, MIGRATION_STATUS_FAILED);
            return -1;
        }
    }

    if (!migrate_pause_before_switchover()) {
        migrate_set_state(&ms->state, MIGRATION_STATUS_ACTIVE,
                          MIGRATION_STATUS_POSTCOPY_ACTIVE);
    }

    trace_postcopy_start();
    qemu_mutex_lock_iothread();
    trace_postcopy_start_set_run();

    migration_downtime_start(ms);

    qemu_system_wakeup_request(QEMU_WAKEUP_REASON_OTHER, NULL);
    global_state_store();
    ret = migration_stop_vm(RUN_STATE_FINISH_MIGRATE);
    if (ret < 0) {
        goto fail;
    }

    ret = migration_maybe_pause(ms, &cur_state,
                                MIGRATION_STATUS_POSTCOPY_ACTIVE);
    if (ret < 0) {
        goto fail;
    }

    ret = bdrv_inactivate_all();
    if (ret < 0) {
        goto fail;
    }
    restart_block = true;

    /*
     * Cause any non-postcopiable, but iterative devices to
     * send out their final data.
     */
    qemu_savevm_state_complete_precopy(ms->to_dst_file, true, false);

    /*
     * in Finish migrate and with the io-lock held everything should
     * be quiet, but we've potentially still got dirty pages and we
     * need to tell the destination to throw any pages it's already received
     * that are dirty
     */
    if (migrate_postcopy_ram()) {
        ram_postcopy_send_discard_bitmap(ms);
    }

    /*
     * send rest of state - note things that are doing postcopy
     * will notice we're in POSTCOPY_ACTIVE and not actually
     * wrap their state up here
     */
    migration_rate_set(bandwidth);
    if (migrate_postcopy_ram()) {
        /* Ping just for debugging, helps line traces up */
        qemu_savevm_send_ping(ms->to_dst_file, 2);
    }

    /*
     * While loading the device state we may trigger page transfer
     * requests and the fd must be free to process those, and thus
     * the destination must read the whole device state off the fd before
     * it starts processing it.  Unfortunately the ad-hoc migration format
     * doesn't allow the destination to know the size to read without fully
     * parsing it through each devices load-state code (especially the open
     * coded devices that use get/put).
     * So we wrap the device state up in a package with a length at the start;
     * to do this we use a qemu_buf to hold the whole of the device state.
     */
    bioc = qio_channel_buffer_new(4096);
    qio_channel_set_name(QIO_CHANNEL(bioc), "migration-postcopy-buffer");
    fb = qemu_file_new_output(QIO_CHANNEL(bioc));
    object_unref(OBJECT(bioc));

    /*
     * Make sure the receiver can get incoming pages before we send the rest
     * of the state
     */
    qemu_savevm_send_postcopy_listen(fb);

    qemu_savevm_state_complete_precopy(fb, false, false);
    if (migrate_postcopy_ram()) {
        qemu_savevm_send_ping(fb, 3);
    }

    qemu_savevm_send_postcopy_run(fb);

    /* <><> end of stuff going into the package */

    /* Last point of recovery; as soon as we send the package the destination
     * can open devices and potentially start running.
     * Lets just check again we've not got any errors.
     */
    ret = qemu_file_get_error(ms->to_dst_file);
    if (ret) {
        error_setg(errp, "postcopy_start: Migration stream errored (pre package)");
        goto fail_closefb;
    }

    restart_block = false;

    /* Now send that blob */
    if (qemu_savevm_send_packaged(ms->to_dst_file, bioc->data, bioc->usage)) {
        goto fail_closefb;
    }
    qemu_fclose(fb);

    /* Send a notify to give a chance for anything that needs to happen
     * at the transition to postcopy and after the device state; in particular
     * spice needs to trigger a transition now
     */
    ms->postcopy_after_devices = true;
    migration_call_notifiers(ms);

    migration_downtime_end(ms);

    qemu_mutex_unlock_iothread();

    if (migrate_postcopy_ram()) {
        /*
         * Although this ping is just for debug, it could potentially be
         * used for getting a better measurement of downtime at the source.
         */
        qemu_savevm_send_ping(ms->to_dst_file, 4);
    }

    if (migrate_release_ram()) {
        ram_postcopy_migrated_memory_release(ms);
    }

    ret = qemu_file_get_error(ms->to_dst_file);
    if (ret) {
        error_setg(errp, "postcopy_start: Migration stream errored");
        migrate_set_state(&ms->state, MIGRATION_STATUS_POSTCOPY_ACTIVE,
                              MIGRATION_STATUS_FAILED);
    }

    trace_postcopy_preempt_enabled(migrate_postcopy_preempt());

    return ret;

fail_closefb:
    qemu_fclose(fb);
fail:
    migrate_set_state(&ms->state, MIGRATION_STATUS_POSTCOPY_ACTIVE,
                          MIGRATION_STATUS_FAILED);
    if (restart_block) {
        /* A failure happened early enough that we know the destination hasn't
         * accessed block devices, so we're safe to recover.
         */
        Error *local_err = NULL;

        bdrv_activate_all(&local_err);
        if (local_err) {
            error_report_err(local_err);
        }
    }
    qemu_mutex_unlock_iothread();
    return -1;
}

/**
 * migration_maybe_pause: Pause if required to by
 * migrate_pause_before_switchover called with the iothread locked
 * Returns: 0 on success
 */
static int migration_maybe_pause(MigrationState *s,
                                 int *current_active_state,
                                 int new_state)
{
    if (!migrate_pause_before_switchover()) {
        return 0;
    }

    /* Since leaving this state is not atomic with posting the semaphore
     * it's possible that someone could have issued multiple migrate_continue
     * and the semaphore is incorrectly positive at this point;
     * the docs say it's undefined to reinit a semaphore that's already
     * init'd, so use timedwait to eat up any existing posts.
     */
    while (qemu_sem_timedwait(&s->pause_sem, 1) == 0) {
        /* This block intentionally left blank */
    }

    /*
     * If the migration is cancelled when it is in the completion phase,
     * the migration state is set to MIGRATION_STATUS_CANCELLING.
     * So we don't need to wait a semaphore, otherwise we would always
     * wait for the 'pause_sem' semaphore.
     */
    if (s->state != MIGRATION_STATUS_CANCELLING) {
        qemu_mutex_unlock_iothread();
        migrate_set_state(&s->state, *current_active_state,
                          MIGRATION_STATUS_PRE_SWITCHOVER);
        qemu_sem_wait(&s->pause_sem);
        migrate_set_state(&s->state, MIGRATION_STATUS_PRE_SWITCHOVER,
                          new_state);
        *current_active_state = new_state;
        qemu_mutex_lock_iothread();
    }

    return s->state == new_state ? 0 : -EINVAL;
}

static int migration_completion_precopy(MigrationState *s,
                                        int *current_active_state)
{
    int ret;

    qemu_mutex_lock_iothread();
    migration_downtime_start(s);
    qemu_system_wakeup_request(QEMU_WAKEUP_REASON_OTHER, NULL);

    s->vm_old_state = runstate_get();
    global_state_store();

    ret = migration_stop_vm(RUN_STATE_FINISH_MIGRATE);
    trace_migration_completion_vm_stop(ret);
    if (ret < 0) {
        goto out_unlock;
    }

    ret = migration_maybe_pause(s, current_active_state,
                                MIGRATION_STATUS_DEVICE);
    if (ret < 0) {
        goto out_unlock;
    }

    /*
     * Inactivate disks except in COLO, and track that we have done so in order
     * to remember to reactivate them if migration fails or is cancelled.
     */
    s->block_inactive = !migrate_colo();
    migration_rate_set(RATE_LIMIT_DISABLED);
    ret = qemu_savevm_state_complete_precopy(s->to_dst_file, false,
                                             s->block_inactive);
out_unlock:
    qemu_mutex_unlock_iothread();
    return ret;
}

static void migration_completion_postcopy(MigrationState *s)
{
    trace_migration_completion_postcopy_end();

    qemu_mutex_lock_iothread();
    qemu_savevm_state_complete_postcopy(s->to_dst_file);
    qemu_mutex_unlock_iothread();

    /*
     * Shutdown the postcopy fast path thread.  This is only needed when dest
     * QEMU binary is old (7.1/7.2).  QEMU 8.0+ doesn't need this.
     */
    if (migrate_postcopy_preempt() && s->preempt_pre_7_2) {
        postcopy_preempt_shutdown_file(s);
    }

    trace_migration_completion_postcopy_end_after_complete();
}

static void migration_completion_failed(MigrationState *s,
                                        int current_active_state)
{
    if (s->block_inactive && (s->state == MIGRATION_STATUS_ACTIVE ||
                              s->state == MIGRATION_STATUS_DEVICE)) {
        /*
         * If not doing postcopy, vm_start() will be called: let's
         * regain control on images.
         */
        Error *local_err = NULL;

        qemu_mutex_lock_iothread();
        bdrv_activate_all(&local_err);
        if (local_err) {
            error_report_err(local_err);
        } else {
            s->block_inactive = false;
        }
        qemu_mutex_unlock_iothread();
    }

    migrate_set_state(&s->state, current_active_state,
                      MIGRATION_STATUS_FAILED);
}

/**
 * migration_completion: Used by migration_thread when there's not much left.
 *   The caller 'breaks' the loop when this returns.
 *
 * @s: Current migration state
 */
static void migration_completion(MigrationState *s)
{
    int ret = 0;
    int current_active_state = s->state;

    if (s->state == MIGRATION_STATUS_ACTIVE) {
        ret = migration_completion_precopy(s, &current_active_state);
    } else if (s->state == MIGRATION_STATUS_POSTCOPY_ACTIVE) {
        migration_completion_postcopy(s);
    } else {
        ret = -1;
    }

    if (ret < 0) {
        goto fail;
    }

    if (close_return_path_on_source(s)) {
        goto fail;
    }

    if (qemu_file_get_error(s->to_dst_file)) {
        trace_migration_completion_file_err();
        goto fail;
    }

    if (migrate_colo() && s->state == MIGRATION_STATUS_ACTIVE) {
        /* COLO does not support postcopy */
        migrate_set_state(&s->state, MIGRATION_STATUS_ACTIVE,
                          MIGRATION_STATUS_COLO);
    } else {
        migrate_set_state(&s->state, current_active_state,
                          MIGRATION_STATUS_COMPLETED);
    }

    return;

fail:
    migration_completion_failed(s, current_active_state);
}

/**
 * bg_migration_completion: Used by bg_migration_thread when after all the
 *   RAM has been saved. The caller 'breaks' the loop when this returns.
 *
 * @s: Current migration state
 */
static void bg_migration_completion(MigrationState *s)
{
    int current_active_state = s->state;

    if (s->state == MIGRATION_STATUS_ACTIVE) {
        /*
         * By this moment we have RAM content saved into the migration stream.
         * The next step is to flush the non-RAM content (device state)
         * right after the ram content. The device state has been stored into
         * the temporary buffer before RAM saving started.
         */
        qemu_put_buffer(s->to_dst_file, s->bioc->data, s->bioc->usage);
        qemu_fflush(s->to_dst_file);
    } else if (s->state == MIGRATION_STATUS_CANCELLING) {
        goto fail;
    }

    if (qemu_file_get_error(s->to_dst_file)) {
        trace_migration_completion_file_err();
        goto fail;
    }

    migrate_set_state(&s->state, current_active_state,
                      MIGRATION_STATUS_COMPLETED);
    return;

fail:
    migrate_set_state(&s->state, current_active_state,
                      MIGRATION_STATUS_FAILED);
}

typedef enum MigThrError {
    /* No error detected */
    MIG_THR_ERR_NONE = 0,
    /* Detected error, but resumed successfully */
    MIG_THR_ERR_RECOVERED = 1,
    /* Detected fatal error, need to exit */
    MIG_THR_ERR_FATAL = 2,
} MigThrError;

static int postcopy_resume_handshake(MigrationState *s)
{
    qemu_savevm_send_postcopy_resume(s->to_dst_file);

    while (s->state == MIGRATION_STATUS_POSTCOPY_RECOVER) {
        if (migration_rp_wait(s)) {
            return -1;
        }
    }

    if (s->state == MIGRATION_STATUS_POSTCOPY_ACTIVE) {
        return 0;
    }

    return -1;
}

/* Return zero if success, or <0 for error */
static int postcopy_do_resume(MigrationState *s)
{
    int ret;

    /*
     * Call all the resume_prepare() hooks, so that modules can be
     * ready for the migration resume.
     */
    ret = qemu_savevm_state_resume_prepare(s);
    if (ret) {
        error_report("%s: resume_prepare() failure detected: %d",
                     __func__, ret);
        return ret;
    }

    /*
     * If preempt is enabled, re-establish the preempt channel.  Note that
     * we do it after resume prepare to make sure the main channel will be
     * created before the preempt channel.  E.g. with weak network, the
     * dest QEMU may get messed up with the preempt and main channels on
     * the order of connection setup.  This guarantees the correct order.
     */
    ret = postcopy_preempt_establish_channel(s);
    if (ret) {
        error_report("%s: postcopy_preempt_establish_channel(): %d",
                     __func__, ret);
        return ret;
    }

    /*
     * Last handshake with destination on the resume (destination will
     * switch to postcopy-active afterwards)
     */
    ret = postcopy_resume_handshake(s);
    if (ret) {
        error_report("%s: handshake failed: %d", __func__, ret);
        return ret;
    }

    return 0;
}

/*
 * We don't return until we are in a safe state to continue current
 * postcopy migration.  Returns MIG_THR_ERR_RECOVERED if recovered, or
 * MIG_THR_ERR_FATAL if unrecovery failure happened.
 */
static MigThrError postcopy_pause(MigrationState *s)
{
    assert(s->state == MIGRATION_STATUS_POSTCOPY_ACTIVE);

    while (true) {
        QEMUFile *file;

        /*
         * Current channel is possibly broken. Release it.  Note that this is
         * guaranteed even without lock because to_dst_file should only be
         * modified by the migration thread.  That also guarantees that the
         * unregister of yank is safe too without the lock.  It should be safe
         * even to be within the qemu_file_lock, but we didn't do that to avoid
         * taking more mutex (yank_lock) within qemu_file_lock.  TL;DR: we make
         * the qemu_file_lock critical section as small as possible.
         */
        assert(s->to_dst_file);
        migration_ioc_unregister_yank_from_file(s->to_dst_file);
        qemu_mutex_lock(&s->qemu_file_lock);
        file = s->to_dst_file;
        s->to_dst_file = NULL;
        qemu_mutex_unlock(&s->qemu_file_lock);

        qemu_file_shutdown(file);
        qemu_fclose(file);

        /*
         * We're already pausing, so ignore any errors on the return
         * path and just wait for the thread to finish. It will be
         * re-created when we resume.
         */
        close_return_path_on_source(s);

        migrate_set_state(&s->state, s->state,
                          MIGRATION_STATUS_POSTCOPY_PAUSED);

        error_report("Detected IO failure for postcopy. "
                     "Migration paused.");

        /*
         * We wait until things fixed up. Then someone will setup the
         * status back for us.
         */
        while (s->state == MIGRATION_STATUS_POSTCOPY_PAUSED) {
            qemu_sem_wait(&s->postcopy_pause_sem);
        }

        if (s->state == MIGRATION_STATUS_POSTCOPY_RECOVER) {
            /* Woken up by a recover procedure. Give it a shot */

            /* Do the resume logic */
            if (postcopy_do_resume(s) == 0) {
                /* Let's continue! */
                trace_postcopy_pause_continued();
                return MIG_THR_ERR_RECOVERED;
            } else {
                /*
                 * Something wrong happened during the recovery, let's
                 * pause again. Pause is always better than throwing
                 * data away.
                 */
                continue;
            }
        } else {
            /* This is not right... Time to quit. */
            return MIG_THR_ERR_FATAL;
        }
    }
}

static MigThrError migration_detect_error(MigrationState *s)
{
    int ret;
    int state = s->state;
    Error *local_error = NULL;

    if (state == MIGRATION_STATUS_CANCELLING ||
        state == MIGRATION_STATUS_CANCELLED) {
        /* End the migration, but don't set the state to failed */
        return MIG_THR_ERR_FATAL;
    }

    /*
     * Try to detect any file errors.  Note that postcopy_qemufile_src will
     * be NULL when postcopy preempt is not enabled.
     */
    ret = qemu_file_get_error_obj_any(s->to_dst_file,
                                      s->postcopy_qemufile_src,
                                      &local_error);
    if (!ret) {
        /* Everything is fine */
        assert(!local_error);
        return MIG_THR_ERR_NONE;
    }

    if (local_error) {
        migrate_set_error(s, local_error);
        error_free(local_error);
    }

    if (state == MIGRATION_STATUS_POSTCOPY_ACTIVE && ret) {
        /*
         * For postcopy, we allow the network to be down for a
         * while. After that, it can be continued by a
         * recovery phase.
         */
        return postcopy_pause(s);
    } else {
        /*
         * For precopy (or postcopy with error outside IO), we fail
         * with no time.
         */
        migrate_set_state(&s->state, state, MIGRATION_STATUS_FAILED);
        trace_migration_thread_file_err();

        /* Time to stop the migration, now. */
        return MIG_THR_ERR_FATAL;
    }
}

static void migration_calculate_complete(MigrationState *s)
{
    uint64_t bytes = migration_transferred_bytes();
    int64_t end_time = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    int64_t transfer_time;

    migration_downtime_end(s);
    s->total_time = end_time - s->start_time;
    transfer_time = s->total_time - s->setup_time;
    if (transfer_time) {
        s->mbps = ((double) bytes * 8.0) / transfer_time / 1000;
    }
}

static void update_iteration_initial_status(MigrationState *s)
{
    /*
     * Update these three fields at the same time to avoid mismatch info lead
     * wrong speed calculation.
     */
    s->iteration_start_time = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    s->iteration_initial_bytes = migration_transferred_bytes();
    s->iteration_initial_pages = ram_get_total_transferred_pages();
}

static void migration_update_counters(MigrationState *s,
                                      int64_t current_time)
{
    uint64_t transferred, transferred_pages, time_spent;
    uint64_t current_bytes; /* bytes transferred since the beginning */
    uint64_t switchover_bw;
    /* Expected bandwidth when switching over to destination QEMU */
    double expected_bw_per_ms;
    double bandwidth;

    if (current_time < s->iteration_start_time + BUFFER_DELAY) {
        return;
    }

    switchover_bw = migrate_avail_switchover_bandwidth();
    current_bytes = migration_transferred_bytes();
    transferred = current_bytes - s->iteration_initial_bytes;
    time_spent = current_time - s->iteration_start_time;
    bandwidth = (double)transferred / time_spent;

    if (switchover_bw) {
        /*
         * If the user specified a switchover bandwidth, let's trust the
         * user so that can be more accurate than what we estimated.
         */
        expected_bw_per_ms = switchover_bw / 1000;
    } else {
        /* If the user doesn't specify bandwidth, we use the estimated */
        expected_bw_per_ms = bandwidth;
    }

    s->threshold_size = expected_bw_per_ms * migrate_downtime_limit();

    s->mbps = (((double) transferred * 8.0) /
               ((double) time_spent / 1000.0)) / 1000.0 / 1000.0;

    transferred_pages = ram_get_total_transferred_pages() -
                            s->iteration_initial_pages;
    s->pages_per_second = (double) transferred_pages /
                             (((double) time_spent / 1000.0));

    /*
     * if we haven't sent anything, we don't want to
     * recalculate. 10000 is a small enough number for our purposes
     */
    if (stat64_get(&mig_stats.dirty_pages_rate) &&
        transferred > 10000) {
        s->expected_downtime =
            stat64_get(&mig_stats.dirty_bytes_last_sync) / expected_bw_per_ms;
    }

    migration_rate_reset();

    update_iteration_initial_status(s);

    trace_migrate_transferred(transferred, time_spent,
                              /* Both in unit bytes/ms */
                              bandwidth, switchover_bw / 1000,
                              s->threshold_size);
}

static bool migration_can_switchover(MigrationState *s)
{
    if (!migrate_switchover_ack()) {
        return true;
    }

    /* No reason to wait for switchover ACK if VM is stopped */
    if (!runstate_is_running()) {
        return true;
    }

    return s->switchover_acked;
}

/* Migration thread iteration status */
typedef enum {
    MIG_ITERATE_RESUME,         /* Resume current iteration */
    MIG_ITERATE_SKIP,           /* Skip current iteration */
    MIG_ITERATE_BREAK,          /* Break the loop */
} MigIterateState;

/*
 * Return true if continue to the next iteration directly, false
 * otherwise.
 */
static MigIterateState migration_iteration_run(MigrationState *s)
{
    uint64_t must_precopy, can_postcopy;
    Error *local_err = NULL;
    bool in_postcopy = s->state == MIGRATION_STATUS_POSTCOPY_ACTIVE;
    bool can_switchover = migration_can_switchover(s);

    qemu_savevm_state_pending_estimate(&must_precopy, &can_postcopy);
    uint64_t pending_size = must_precopy + can_postcopy;

    trace_migrate_pending_estimate(pending_size, must_precopy, can_postcopy);

    if (must_precopy <= s->threshold_size) {
        qemu_savevm_state_pending_exact(&must_precopy, &can_postcopy);
        pending_size = must_precopy + can_postcopy;
        trace_migrate_pending_exact(pending_size, must_precopy, can_postcopy);
    }

    if ((!pending_size || pending_size < s->threshold_size) && can_switchover) {
        trace_migration_thread_low_pending(pending_size);
        migration_completion(s);
        return MIG_ITERATE_BREAK;
    }

    /* Still a significant amount to transfer */
    if (!in_postcopy && must_precopy <= s->threshold_size && can_switchover &&
        qatomic_read(&s->start_postcopy)) {
        if (postcopy_start(s, &local_err)) {
            migrate_set_error(s, local_err);
            error_report_err(local_err);
        }
        return MIG_ITERATE_SKIP;
    }

    /* Just another iteration step */
    qemu_savevm_state_iterate(s->to_dst_file, in_postcopy);
    return MIG_ITERATE_RESUME;
}

static void migration_iteration_finish(MigrationState *s)
{
    /* If we enabled cpu throttling for auto-converge, turn it off. */
    cpu_throttle_stop();

    qemu_mutex_lock_iothread();
    switch (s->state) {
    case MIGRATION_STATUS_COMPLETED:
        migration_calculate_complete(s);
        runstate_set(RUN_STATE_POSTMIGRATE);
        break;
    case MIGRATION_STATUS_COLO:
        assert(migrate_colo());
        migrate_start_colo_process(s);
        s->vm_old_state = RUN_STATE_RUNNING;
        /* Fallthrough */
    case MIGRATION_STATUS_FAILED:
    case MIGRATION_STATUS_CANCELLED:
    case MIGRATION_STATUS_CANCELLING:
        if (s->vm_old_state == RUN_STATE_RUNNING) {
            if (!runstate_check(RUN_STATE_SHUTDOWN)) {
                vm_start();
            }
        } else {
            if (runstate_check(RUN_STATE_FINISH_MIGRATE)) {
                runstate_set(s->vm_old_state);
            }
        }
        break;

    default:
        /* Should not reach here, but if so, forgive the VM. */
        error_report("%s: Unknown ending state %d", __func__, s->state);
        break;
    }
    migrate_fd_cleanup_schedule(s);
    qemu_mutex_unlock_iothread();
}

static void bg_migration_iteration_finish(MigrationState *s)
{
    /*
     * Stop tracking RAM writes - un-protect memory, un-register UFFD
     * memory ranges, flush kernel wait queues and wake up threads
     * waiting for write fault to be resolved.
     */
    ram_write_tracking_stop();

    qemu_mutex_lock_iothread();
    switch (s->state) {
    case MIGRATION_STATUS_COMPLETED:
        migration_calculate_complete(s);
        break;

    case MIGRATION_STATUS_ACTIVE:
    case MIGRATION_STATUS_FAILED:
    case MIGRATION_STATUS_CANCELLED:
    case MIGRATION_STATUS_CANCELLING:
        break;

    default:
        /* Should not reach here, but if so, forgive the VM. */
        error_report("%s: Unknown ending state %d", __func__, s->state);
        break;
    }

    migrate_fd_cleanup_schedule(s);
    qemu_mutex_unlock_iothread();
}

/*
 * Return true if continue to the next iteration directly, false
 * otherwise.
 */
static MigIterateState bg_migration_iteration_run(MigrationState *s)
{
    int res;

    res = qemu_savevm_state_iterate(s->to_dst_file, false);
    if (res > 0) {
        bg_migration_completion(s);
        return MIG_ITERATE_BREAK;
    }

    return MIG_ITERATE_RESUME;
}

void migration_make_urgent_request(void)
{
    qemu_sem_post(&migrate_get_current()->rate_limit_sem);
}

void migration_consume_urgent_request(void)
{
    qemu_sem_wait(&migrate_get_current()->rate_limit_sem);
}

/* Returns true if the rate limiting was broken by an urgent request */
bool migration_rate_limit(void)
{
    int64_t now = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    MigrationState *s = migrate_get_current();

    bool urgent = false;
    migration_update_counters(s, now);
    if (migration_rate_exceeded(s->to_dst_file)) {

        if (qemu_file_get_error(s->to_dst_file)) {
            return false;
        }
        /*
         * Wait for a delay to do rate limiting OR
         * something urgent to post the semaphore.
         */
        int ms = s->iteration_start_time + BUFFER_DELAY - now;
        trace_migration_rate_limit_pre(ms);
        if (qemu_sem_timedwait(&s->rate_limit_sem, ms) == 0) {
            /*
             * We were woken by one or more urgent things but
             * the timedwait will have consumed one of them.
             * The service routine for the urgent wake will dec
             * the semaphore itself for each item it consumes,
             * so add this one we just eat back.
             */
            qemu_sem_post(&s->rate_limit_sem);
            urgent = true;
        }
        trace_migration_rate_limit_post(urgent);
    }
    return urgent;
}

/*
 * if failover devices are present, wait they are completely
 * unplugged
 */

static void qemu_savevm_wait_unplug(MigrationState *s, int old_state,
                                    int new_state)
{
    if (qemu_savevm_state_guest_unplug_pending()) {
        migrate_set_state(&s->state, old_state, MIGRATION_STATUS_WAIT_UNPLUG);

        while (s->state == MIGRATION_STATUS_WAIT_UNPLUG &&
               qemu_savevm_state_guest_unplug_pending()) {
            qemu_sem_timedwait(&s->wait_unplug_sem, 250);
        }
        if (s->state != MIGRATION_STATUS_WAIT_UNPLUG) {
            int timeout = 120; /* 30 seconds */
            /*
             * migration has been canceled
             * but as we have started an unplug we must wait the end
             * to be able to plug back the card
             */
            while (timeout-- && qemu_savevm_state_guest_unplug_pending()) {
                qemu_sem_timedwait(&s->wait_unplug_sem, 250);
            }
            if (qemu_savevm_state_guest_unplug_pending() &&
                !qtest_enabled()) {
                warn_report("migration: partially unplugged device on "
                            "failure");
            }
        }

        migrate_set_state(&s->state, MIGRATION_STATUS_WAIT_UNPLUG, new_state);
    } else {
        migrate_set_state(&s->state, old_state, new_state);
    }
}

/*
 * Master migration thread on the source VM.
 * It drives the migration and pumps the data down the outgoing channel.
 */
static void *migration_thread(void *opaque)
{
    MigrationState *s = opaque;
    MigrationThread *thread = NULL;
    int64_t setup_start = qemu_clock_get_ms(QEMU_CLOCK_HOST);
    MigThrError thr_error;
    bool urgent = false;

    thread = migration_threads_add("live_migration", qemu_get_thread_id());

    rcu_register_thread();

    object_ref(OBJECT(s));
    update_iteration_initial_status(s);

    qemu_mutex_lock_iothread();
    qemu_savevm_state_header(s->to_dst_file);
    qemu_mutex_unlock_iothread();

    /*
     * If we opened the return path, we need to make sure dst has it
     * opened as well.
     */
    if (s->rp_state.rp_thread_created) {
        /* Now tell the dest that it should open its end so it can reply */
        qemu_savevm_send_open_return_path(s->to_dst_file);

        /* And do a ping that will make stuff easier to debug */
        qemu_savevm_send_ping(s->to_dst_file, 1);
    }

    if (migrate_postcopy()) {
        /*
         * Tell the destination that we *might* want to do postcopy later;
         * if the other end can't do postcopy it should fail now, nice and
         * early.
         */
        qemu_savevm_send_postcopy_advise(s->to_dst_file);
    }

    if (migrate_colo()) {
        /* Notify migration destination that we enable COLO */
        qemu_savevm_send_colo_enable(s->to_dst_file);
    }

    qemu_mutex_lock_iothread();
    qemu_savevm_state_setup(s->to_dst_file);
    qemu_mutex_unlock_iothread();

    qemu_savevm_wait_unplug(s, MIGRATION_STATUS_SETUP,
                               MIGRATION_STATUS_ACTIVE);

    s->setup_time = qemu_clock_get_ms(QEMU_CLOCK_HOST) - setup_start;

    trace_migration_thread_setup_complete();

    while (migration_is_active(s)) {
        if (urgent || !migration_rate_exceeded(s->to_dst_file)) {
            MigIterateState iter_state = migration_iteration_run(s);
            if (iter_state == MIG_ITERATE_SKIP) {
                continue;
            } else if (iter_state == MIG_ITERATE_BREAK) {
                break;
            }
        }

        /*
         * Try to detect any kind of failures, and see whether we
         * should stop the migration now.
         */
        thr_error = migration_detect_error(s);
        if (thr_error == MIG_THR_ERR_FATAL) {
            /* Stop migration */
            break;
        } else if (thr_error == MIG_THR_ERR_RECOVERED) {
            /*
             * Just recovered from a e.g. network failure, reset all
             * the local variables. This is important to avoid
             * breaking transferred_bytes and bandwidth calculation
             */
            update_iteration_initial_status(s);
        }

        urgent = migration_rate_limit();
    }

    trace_migration_thread_after_loop();
    migration_iteration_finish(s);
    object_unref(OBJECT(s));
    rcu_unregister_thread();
    migration_threads_remove(thread);
    return NULL;
}

static void bg_migration_vm_start_bh(void *opaque)
{
    MigrationState *s = opaque;

    qemu_bh_delete(s->vm_start_bh);
    s->vm_start_bh = NULL;

    vm_start();
    migration_downtime_end(s);
}

/**
 * Background snapshot thread, based on live migration code.
 * This is an alternative implementation of live migration mechanism
 * introduced specifically to support background snapshots.
 *
 * It takes advantage of userfault_fd write protection mechanism introduced
 * in v5.7 kernel. Compared to existing dirty page logging migration much
 * lesser stream traffic is produced resulting in smaller snapshot images,
 * simply cause of no page duplicates can get into the stream.
 *
 * Another key point is that generated vmstate stream reflects machine state
 * 'frozen' at the beginning of snapshot creation compared to dirty page logging
 * mechanism, which effectively results in that saved snapshot is the state of VM
 * at the end of the process.
 */
static void *bg_migration_thread(void *opaque)
{
    MigrationState *s = opaque;
    int64_t setup_start;
    MigThrError thr_error;
    QEMUFile *fb;
    bool early_fail = true;

    rcu_register_thread();
    object_ref(OBJECT(s));

    migration_rate_set(RATE_LIMIT_DISABLED);

    setup_start = qemu_clock_get_ms(QEMU_CLOCK_HOST);
    /*
     * We want to save vmstate for the moment when migration has been
     * initiated but also we want to save RAM content while VM is running.
     * The RAM content should appear first in the vmstate. So, we first
     * stash the non-RAM part of the vmstate to the temporary buffer,
     * then write RAM part of the vmstate to the migration stream
     * with vCPUs running and, finally, write stashed non-RAM part of
     * the vmstate from the buffer to the migration stream.
     */
    s->bioc = qio_channel_buffer_new(512 * 1024);
    qio_channel_set_name(QIO_CHANNEL(s->bioc), "vmstate-buffer");
    fb = qemu_file_new_output(QIO_CHANNEL(s->bioc));
    object_unref(OBJECT(s->bioc));

    update_iteration_initial_status(s);

    /*
     * Prepare for tracking memory writes with UFFD-WP - populate
     * RAM pages before protecting.
     */
#ifdef __linux__
    ram_write_tracking_prepare();
#endif

    qemu_mutex_lock_iothread();
    qemu_savevm_state_header(s->to_dst_file);
    qemu_savevm_state_setup(s->to_dst_file);
    qemu_mutex_unlock_iothread();

    qemu_savevm_wait_unplug(s, MIGRATION_STATUS_SETUP,
                               MIGRATION_STATUS_ACTIVE);

    s->setup_time = qemu_clock_get_ms(QEMU_CLOCK_HOST) - setup_start;

    trace_migration_thread_setup_complete();
    migration_downtime_start(s);

    qemu_mutex_lock_iothread();

    /*
     * If VM is currently in suspended state, then, to make a valid runstate
     * transition in vm_stop_force_state() we need to wakeup it up.
     */
    qemu_system_wakeup_request(QEMU_WAKEUP_REASON_OTHER, NULL);
    s->vm_old_state = runstate_get();

    global_state_store();
    /* Forcibly stop VM before saving state of vCPUs and devices */
    if (migration_stop_vm(RUN_STATE_PAUSED)) {
        goto fail;
    }
    /*
     * Put vCPUs in sync with shadow context structures, then
     * save their state to channel-buffer along with devices.
     */
    cpu_synchronize_all_states();
    if (qemu_savevm_state_complete_precopy_non_iterable(fb, false, false)) {
        goto fail;
    }
    /*
     * Since we are going to get non-iterable state data directly
     * from s->bioc->data, explicit flush is needed here.
     */
    qemu_fflush(fb);

    /* Now initialize UFFD context and start tracking RAM writes */
    if (ram_write_tracking_start()) {
        goto fail;
    }
    early_fail = false;

    /*
     * Start VM from BH handler to avoid write-fault lock here.
     * UFFD-WP protection for the whole RAM is already enabled so
     * calling VM state change notifiers from vm_start() would initiate
     * writes to virtio VQs memory which is in write-protected region.
     */
    s->vm_start_bh = qemu_bh_new(bg_migration_vm_start_bh, s);
    qemu_bh_schedule(s->vm_start_bh);

    qemu_mutex_unlock_iothread();

    while (migration_is_active(s)) {
        MigIterateState iter_state = bg_migration_iteration_run(s);
        if (iter_state == MIG_ITERATE_SKIP) {
            continue;
        } else if (iter_state == MIG_ITERATE_BREAK) {
            break;
        }

        /*
         * Try to detect any kind of failures, and see whether we
         * should stop the migration now.
         */
        thr_error = migration_detect_error(s);
        if (thr_error == MIG_THR_ERR_FATAL) {
            /* Stop migration */
            break;
        }

        migration_update_counters(s, qemu_clock_get_ms(QEMU_CLOCK_REALTIME));
    }

    trace_migration_thread_after_loop();

fail:
    if (early_fail) {
        migrate_set_state(&s->state, MIGRATION_STATUS_ACTIVE,
                MIGRATION_STATUS_FAILED);
        qemu_mutex_unlock_iothread();
    }

    bg_migration_iteration_finish(s);

    qemu_fclose(fb);
    object_unref(OBJECT(s));
    rcu_unregister_thread();

    return NULL;
}

void migrate_fd_connect(MigrationState *s, Error *error_in)
{
    Error *local_err = NULL;
    uint64_t rate_limit;
    bool resume = s->state == MIGRATION_STATUS_POSTCOPY_PAUSED;

    /*
     * If there's a previous error, free it and prepare for another one.
     * Meanwhile if migration completes successfully, there won't have an error
     * dumped when calling migrate_fd_cleanup().
     */
    migrate_error_free(s);

    s->expected_downtime = migrate_downtime_limit();
    if (resume) {
        assert(s->cleanup_bh);
    } else {
        assert(!s->cleanup_bh);
        s->cleanup_bh = qemu_bh_new(migrate_fd_cleanup_bh, s);
    }
    if (error_in) {
        migrate_fd_error(s, error_in);
        if (resume) {
            /*
             * Don't do cleanup for resume if channel is invalid, but only dump
             * the error.  We wait for another channel connect from the user.
             * The error_report still gives HMP user a hint on what failed.
             * It's normally done in migrate_fd_cleanup(), but call it here
             * explicitly.
             */
            error_report_err(error_copy(s->error));
        } else {
            migrate_fd_cleanup(s);
        }
        return;
    }

    if (resume) {
        /* This is a resumed migration */
        rate_limit = migrate_max_postcopy_bandwidth();
    } else {
        /* This is a fresh new migration */
        rate_limit = migrate_max_bandwidth();

        /* Notify before starting migration thread */
        migration_call_notifiers(s);
    }

    migration_rate_set(rate_limit);
    qemu_file_set_blocking(s->to_dst_file, true);

    /*
     * Open the return path. For postcopy, it is used exclusively. For
     * precopy, only if user specified "return-path" capability would
     * QEMU uses the return path.
     */
    if (migrate_postcopy_ram() || migrate_return_path()) {
        if (open_return_path_on_source(s)) {
            error_setg(&local_err, "Unable to open return-path for postcopy");
            migrate_set_state(&s->state, s->state, MIGRATION_STATUS_FAILED);
            migrate_set_error(s, local_err);
            error_report_err(local_err);
            migrate_fd_cleanup(s);
            return;
        }
    }

    /*
     * This needs to be done before resuming a postcopy.  Note: for newer
     * QEMUs we will delay the channel creation until postcopy_start(), to
     * avoid disorder of channel creations.
     */
    if (migrate_postcopy_preempt() && s->preempt_pre_7_2) {
        postcopy_preempt_setup(s);
    }

    if (resume) {
        /* Wakeup the main migration thread to do the recovery */
        migrate_set_state(&s->state, MIGRATION_STATUS_POSTCOPY_PAUSED,
                          MIGRATION_STATUS_POSTCOPY_RECOVER);
        qemu_sem_post(&s->postcopy_pause_sem);
        return;
    }

    if (multifd_save_setup(&local_err) != 0) {
        migrate_set_error(s, local_err);
        error_report_err(local_err);
        migrate_set_state(&s->state, MIGRATION_STATUS_SETUP,
                          MIGRATION_STATUS_FAILED);
        migrate_fd_cleanup(s);
        return;
    }

    if (migrate_background_snapshot()) {
        qemu_thread_create(&s->thread, "bg_snapshot",
                bg_migration_thread, s, QEMU_THREAD_JOINABLE);
    } else {
        qemu_thread_create(&s->thread, "live_migration",
                migration_thread, s, QEMU_THREAD_JOINABLE);
    }
    s->migration_thread_running = true;
}

static void migration_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->user_creatable = false;
    device_class_set_props(dc, migration_properties);
}

static void migration_instance_finalize(Object *obj)
{
    MigrationState *ms = MIGRATION_OBJ(obj);

    qemu_mutex_destroy(&ms->error_mutex);
    qemu_mutex_destroy(&ms->qemu_file_lock);
    qemu_sem_destroy(&ms->wait_unplug_sem);
    qemu_sem_destroy(&ms->rate_limit_sem);
    qemu_sem_destroy(&ms->pause_sem);
    qemu_sem_destroy(&ms->postcopy_pause_sem);
    qemu_sem_destroy(&ms->rp_state.rp_sem);
    qemu_sem_destroy(&ms->rp_state.rp_pong_acks);
    qemu_sem_destroy(&ms->postcopy_qemufile_src_sem);
    error_free(ms->error);
}

static void migration_instance_init(Object *obj)
{
    MigrationState *ms = MIGRATION_OBJ(obj);

    ms->state = MIGRATION_STATUS_NONE;
    ms->mbps = -1;
    ms->pages_per_second = -1;
    qemu_sem_init(&ms->pause_sem, 0);
    qemu_mutex_init(&ms->error_mutex);

    migrate_params_init(&ms->parameters);

    qemu_sem_init(&ms->postcopy_pause_sem, 0);
    qemu_sem_init(&ms->rp_state.rp_sem, 0);
    qemu_sem_init(&ms->rp_state.rp_pong_acks, 0);
    qemu_sem_init(&ms->rate_limit_sem, 0);
    qemu_sem_init(&ms->wait_unplug_sem, 0);
    qemu_sem_init(&ms->postcopy_qemufile_src_sem, 0);
    qemu_mutex_init(&ms->qemu_file_lock);
}

/*
 * Return true if check pass, false otherwise. Error will be put
 * inside errp if provided.
 */
static bool migration_object_check(MigrationState *ms, Error **errp)
{
    /* Assuming all off */
    bool old_caps[MIGRATION_CAPABILITY__MAX] = { 0 };

    if (!migrate_params_check(&ms->parameters, errp)) {
        return false;
    }

    return migrate_caps_check(old_caps, ms->capabilities, errp);
}

static const TypeInfo migration_type = {
    .name = TYPE_MIGRATION,
    /*
     * NOTE: TYPE_MIGRATION is not really a device, as the object is
     * not created using qdev_new(), it is not attached to the qdev
     * device tree, and it is never realized.
     *
     * TODO: Make this TYPE_OBJECT once QOM provides something like
     * TYPE_DEVICE's "-global" properties.
     */
    .parent = TYPE_DEVICE,
    .class_init = migration_class_init,
    .class_size = sizeof(MigrationClass),
    .instance_size = sizeof(MigrationState),
    .instance_init = migration_instance_init,
    .instance_finalize = migration_instance_finalize,
};

static void register_migration_types(void)
{
    type_register_static(&migration_type);
}

type_init(register_migration_types);
