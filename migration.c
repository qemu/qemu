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

#include "qemu-common.h"
#include "qemu/main-loop.h"
#include "migration/migration.h"
#include "monitor/monitor.h"
#include "migration/qemu-file.h"
#include "sysemu/sysemu.h"
#include "block/block.h"
#include "qemu/sockets.h"
#include "migration/block.h"
#include "qemu/thread.h"
#include "qmp-commands.h"
#include "trace.h"

enum {
    MIG_STATE_ERROR = -1,
    MIG_STATE_NONE,
    MIG_STATE_SETUP,
    MIG_STATE_CANCELLING,
    MIG_STATE_CANCELLED,
    MIG_STATE_ACTIVE,
    MIG_STATE_COMPLETED,
};

#define MAX_THROTTLE  (32 << 20)      /* Migration speed throttling */

/* Amount of time to allocate to each "chunk" of bandwidth-throttled
 * data. */
#define BUFFER_DELAY     100
#define XFER_LIMIT_RATIO (1000 / BUFFER_DELAY)

/* Migration XBZRLE default cache size */
#define DEFAULT_MIGRATE_CACHE_SIZE (64 * 1024 * 1024)

static NotifierList migration_state_notifiers =
    NOTIFIER_LIST_INITIALIZER(migration_state_notifiers);

/* When we add fault tolerance, we could have several
   migrations at once.  For now we don't need to add
   dynamic creation of migration */

MigrationState *migrate_get_current(void)
{
    static MigrationState current_migration = {
        .state = MIG_STATE_NONE,
        .bandwidth_limit = MAX_THROTTLE,
        .xbzrle_cache_size = DEFAULT_MIGRATE_CACHE_SIZE,
        .mbps = -1,
    };

    return &current_migration;
}

void qemu_start_incoming_migration(const char *uri, Error **errp)
{
    const char *p;

    if (strstart(uri, "tcp:", &p))
        tcp_start_incoming_migration(p, errp);
#ifdef CONFIG_RDMA
    else if (strstart(uri, "rdma:", &p))
        rdma_start_incoming_migration(p, errp);
#endif
#if !defined(WIN32)
    else if (strstart(uri, "exec:", &p))
        exec_start_incoming_migration(p, errp);
    else if (strstart(uri, "unix:", &p))
        unix_start_incoming_migration(p, errp);
    else if (strstart(uri, "fd:", &p))
        fd_start_incoming_migration(p, errp);
#endif
    else {
        error_setg(errp, "unknown migration protocol: %s", uri);
    }
}

static void process_incoming_migration_co(void *opaque)
{
    QEMUFile *f = opaque;
    Error *local_err = NULL;
    int ret;

    ret = qemu_loadvm_state(f);
    qemu_fclose(f);
    free_xbzrle_decoded_buf();
    if (ret < 0) {
        fprintf(stderr, "load of migration failed\n");
        exit(EXIT_FAILURE);
    }
    qemu_announce_self();

    bdrv_clear_incoming_migration_all();
    /* Make sure all file formats flush their mutable metadata */
    bdrv_invalidate_cache_all(&local_err);
    if (local_err) {
        qerror_report_err(local_err);
        error_free(local_err);
        exit(EXIT_FAILURE);
    }

    if (autostart) {
        vm_start();
    } else {
        runstate_set(RUN_STATE_PAUSED);
    }
}

void process_incoming_migration(QEMUFile *f)
{
    Coroutine *co = qemu_coroutine_create(process_incoming_migration_co);
    int fd = qemu_get_fd(f);

    assert(fd != -1);
    qemu_set_nonblock(fd);
    qemu_coroutine_enter(co, f);
}

/* amount of nanoseconds we are willing to wait for migration to be down.
 * the choice of nanoseconds is because it is the maximum resolution that
 * get_clock() can achieve. It is an internal measure. All user-visible
 * units must be in seconds */
static uint64_t max_downtime = 30000000;

uint64_t migrate_max_downtime(void)
{
    return max_downtime;
}

MigrationCapabilityStatusList *qmp_query_migrate_capabilities(Error **errp)
{
    MigrationCapabilityStatusList *head = NULL;
    MigrationCapabilityStatusList *caps;
    MigrationState *s = migrate_get_current();
    int i;

    caps = NULL; /* silence compiler warning */
    for (i = 0; i < MIGRATION_CAPABILITY_MAX; i++) {
        if (head == NULL) {
            head = g_malloc0(sizeof(*caps));
            caps = head;
        } else {
            caps->next = g_malloc0(sizeof(*caps));
            caps = caps->next;
        }
        caps->value =
            g_malloc(sizeof(*caps->value));
        caps->value->capability = i;
        caps->value->state = s->enabled_capabilities[i];
    }

    return head;
}

static void get_xbzrle_cache_stats(MigrationInfo *info)
{
    if (migrate_use_xbzrle()) {
        info->has_xbzrle_cache = true;
        info->xbzrle_cache = g_malloc0(sizeof(*info->xbzrle_cache));
        info->xbzrle_cache->cache_size = migrate_xbzrle_cache_size();
        info->xbzrle_cache->bytes = xbzrle_mig_bytes_transferred();
        info->xbzrle_cache->pages = xbzrle_mig_pages_transferred();
        info->xbzrle_cache->cache_miss = xbzrle_mig_pages_cache_miss();
        info->xbzrle_cache->overflow = xbzrle_mig_pages_overflow();
    }
}

MigrationInfo *qmp_query_migrate(Error **errp)
{
    MigrationInfo *info = g_malloc0(sizeof(*info));
    MigrationState *s = migrate_get_current();

    switch (s->state) {
    case MIG_STATE_NONE:
        /* no migration has happened ever */
        break;
    case MIG_STATE_SETUP:
        info->has_status = true;
        info->status = g_strdup("setup");
        info->has_total_time = false;
        break;
    case MIG_STATE_ACTIVE:
    case MIG_STATE_CANCELLING:
        info->has_status = true;
        info->status = g_strdup("active");
        info->has_total_time = true;
        info->total_time = qemu_clock_get_ms(QEMU_CLOCK_REALTIME)
            - s->total_time;
        info->has_expected_downtime = true;
        info->expected_downtime = s->expected_downtime;
        info->has_setup_time = true;
        info->setup_time = s->setup_time;

        info->has_ram = true;
        info->ram = g_malloc0(sizeof(*info->ram));
        info->ram->transferred = ram_bytes_transferred();
        info->ram->remaining = ram_bytes_remaining();
        info->ram->total = ram_bytes_total();
        info->ram->duplicate = dup_mig_pages_transferred();
        info->ram->skipped = skipped_mig_pages_transferred();
        info->ram->normal = norm_mig_pages_transferred();
        info->ram->normal_bytes = norm_mig_bytes_transferred();
        info->ram->dirty_pages_rate = s->dirty_pages_rate;
        info->ram->mbps = s->mbps;

        if (blk_mig_active()) {
            info->has_disk = true;
            info->disk = g_malloc0(sizeof(*info->disk));
            info->disk->transferred = blk_mig_bytes_transferred();
            info->disk->remaining = blk_mig_bytes_remaining();
            info->disk->total = blk_mig_bytes_total();
        }

        get_xbzrle_cache_stats(info);
        break;
    case MIG_STATE_COMPLETED:
        get_xbzrle_cache_stats(info);

        info->has_status = true;
        info->status = g_strdup("completed");
        info->has_total_time = true;
        info->total_time = s->total_time;
        info->has_downtime = true;
        info->downtime = s->downtime;
        info->has_setup_time = true;
        info->setup_time = s->setup_time;

        info->has_ram = true;
        info->ram = g_malloc0(sizeof(*info->ram));
        info->ram->transferred = ram_bytes_transferred();
        info->ram->remaining = 0;
        info->ram->total = ram_bytes_total();
        info->ram->duplicate = dup_mig_pages_transferred();
        info->ram->skipped = skipped_mig_pages_transferred();
        info->ram->normal = norm_mig_pages_transferred();
        info->ram->normal_bytes = norm_mig_bytes_transferred();
        info->ram->mbps = s->mbps;
        break;
    case MIG_STATE_ERROR:
        info->has_status = true;
        info->status = g_strdup("failed");
        break;
    case MIG_STATE_CANCELLED:
        info->has_status = true;
        info->status = g_strdup("cancelled");
        break;
    }

    return info;
}

void qmp_migrate_set_capabilities(MigrationCapabilityStatusList *params,
                                  Error **errp)
{
    MigrationState *s = migrate_get_current();
    MigrationCapabilityStatusList *cap;

    if (s->state == MIG_STATE_ACTIVE || s->state == MIG_STATE_SETUP) {
        error_set(errp, QERR_MIGRATION_ACTIVE);
        return;
    }

    for (cap = params; cap; cap = cap->next) {
        s->enabled_capabilities[cap->value->capability] = cap->value->state;
    }
}

/* shared migration helpers */

static void migrate_set_state(MigrationState *s, int old_state, int new_state)
{
    if (atomic_cmpxchg(&s->state, old_state, new_state) == new_state) {
        trace_migrate_set_state(new_state);
    }
}

static void migrate_fd_cleanup(void *opaque)
{
    MigrationState *s = opaque;

    qemu_bh_delete(s->cleanup_bh);
    s->cleanup_bh = NULL;

    if (s->file) {
        trace_migrate_fd_cleanup();
        qemu_mutex_unlock_iothread();
        qemu_thread_join(&s->thread);
        qemu_mutex_lock_iothread();

        qemu_fclose(s->file);
        s->file = NULL;
    }

    assert(s->state != MIG_STATE_ACTIVE);

    if (s->state != MIG_STATE_COMPLETED) {
        qemu_savevm_state_cancel();
        if (s->state == MIG_STATE_CANCELLING) {
            migrate_set_state(s, MIG_STATE_CANCELLING, MIG_STATE_CANCELLED);
        }
    }

    notifier_list_notify(&migration_state_notifiers, s);
}

void migrate_fd_error(MigrationState *s)
{
    trace_migrate_fd_error();
    assert(s->file == NULL);
    s->state = MIG_STATE_ERROR;
    trace_migrate_set_state(MIG_STATE_ERROR);
    notifier_list_notify(&migration_state_notifiers, s);
}

static void migrate_fd_cancel(MigrationState *s)
{
    int old_state ;
    trace_migrate_fd_cancel();

    do {
        old_state = s->state;
        if (old_state != MIG_STATE_SETUP && old_state != MIG_STATE_ACTIVE) {
            break;
        }
        migrate_set_state(s, old_state, MIG_STATE_CANCELLING);
    } while (s->state != MIG_STATE_CANCELLING);
}

void add_migration_state_change_notifier(Notifier *notify)
{
    notifier_list_add(&migration_state_notifiers, notify);
}

void remove_migration_state_change_notifier(Notifier *notify)
{
    notifier_remove(notify);
}

bool migration_in_setup(MigrationState *s)
{
    return s->state == MIG_STATE_SETUP;
}

bool migration_has_finished(MigrationState *s)
{
    return s->state == MIG_STATE_COMPLETED;
}

bool migration_has_failed(MigrationState *s)
{
    return (s->state == MIG_STATE_CANCELLED ||
            s->state == MIG_STATE_ERROR);
}

static MigrationState *migrate_init(const MigrationParams *params)
{
    MigrationState *s = migrate_get_current();
    int64_t bandwidth_limit = s->bandwidth_limit;
    bool enabled_capabilities[MIGRATION_CAPABILITY_MAX];
    int64_t xbzrle_cache_size = s->xbzrle_cache_size;

    memcpy(enabled_capabilities, s->enabled_capabilities,
           sizeof(enabled_capabilities));

    memset(s, 0, sizeof(*s));
    s->params = *params;
    memcpy(s->enabled_capabilities, enabled_capabilities,
           sizeof(enabled_capabilities));
    s->xbzrle_cache_size = xbzrle_cache_size;

    s->bandwidth_limit = bandwidth_limit;
    s->state = MIG_STATE_SETUP;
    trace_migrate_set_state(MIG_STATE_SETUP);

    s->total_time = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    return s;
}

static GSList *migration_blockers;

void migrate_add_blocker(Error *reason)
{
    migration_blockers = g_slist_prepend(migration_blockers, reason);
}

void migrate_del_blocker(Error *reason)
{
    migration_blockers = g_slist_remove(migration_blockers, reason);
}

void qmp_migrate(const char *uri, bool has_blk, bool blk,
                 bool has_inc, bool inc, bool has_detach, bool detach,
                 Error **errp)
{
    Error *local_err = NULL;
    MigrationState *s = migrate_get_current();
    MigrationParams params;
    const char *p;

    params.blk = has_blk && blk;
    params.shared = has_inc && inc;

    if (s->state == MIG_STATE_ACTIVE || s->state == MIG_STATE_SETUP ||
        s->state == MIG_STATE_CANCELLING) {
        error_set(errp, QERR_MIGRATION_ACTIVE);
        return;
    }

    if (runstate_check(RUN_STATE_INMIGRATE)) {
        error_setg(errp, "Guest is waiting for an incoming migration");
        return;
    }

    if (qemu_savevm_state_blocked(errp)) {
        return;
    }

    if (migration_blockers) {
        *errp = error_copy(migration_blockers->data);
        return;
    }

    s = migrate_init(&params);

    if (strstart(uri, "tcp:", &p)) {
        tcp_start_outgoing_migration(s, p, &local_err);
#ifdef CONFIG_RDMA
    } else if (strstart(uri, "rdma:", &p)) {
        rdma_start_outgoing_migration(s, p, &local_err);
#endif
#if !defined(WIN32)
    } else if (strstart(uri, "exec:", &p)) {
        exec_start_outgoing_migration(s, p, &local_err);
    } else if (strstart(uri, "unix:", &p)) {
        unix_start_outgoing_migration(s, p, &local_err);
    } else if (strstart(uri, "fd:", &p)) {
        fd_start_outgoing_migration(s, p, &local_err);
#endif
    } else {
        error_set(errp, QERR_INVALID_PARAMETER_VALUE, "uri", "a valid migration protocol");
        s->state = MIG_STATE_ERROR;
        return;
    }

    if (local_err) {
        migrate_fd_error(s);
        error_propagate(errp, local_err);
        return;
    }
}

void qmp_migrate_cancel(Error **errp)
{
    migrate_fd_cancel(migrate_get_current());
}

void qmp_migrate_set_cache_size(int64_t value, Error **errp)
{
    MigrationState *s = migrate_get_current();
    int64_t new_size;

    /* Check for truncation */
    if (value != (size_t)value) {
        error_set(errp, QERR_INVALID_PARAMETER_VALUE, "cache size",
                  "exceeding address space");
        return;
    }

    /* Cache should not be larger than guest ram size */
    if (value > ram_bytes_total()) {
        error_set(errp, QERR_INVALID_PARAMETER_VALUE, "cache size",
                  "exceeds guest ram size ");
        return;
    }

    new_size = xbzrle_cache_resize(value);
    if (new_size < 0) {
        error_set(errp, QERR_INVALID_PARAMETER_VALUE, "cache size",
                  "is smaller than page size");
        return;
    }

    s->xbzrle_cache_size = new_size;
}

int64_t qmp_query_migrate_cache_size(Error **errp)
{
    return migrate_xbzrle_cache_size();
}

void qmp_migrate_set_speed(int64_t value, Error **errp)
{
    MigrationState *s;

    if (value < 0) {
        value = 0;
    }
    if (value > SIZE_MAX) {
        value = SIZE_MAX;
    }

    s = migrate_get_current();
    s->bandwidth_limit = value;
    if (s->file) {
        qemu_file_set_rate_limit(s->file, s->bandwidth_limit / XFER_LIMIT_RATIO);
    }
}

void qmp_migrate_set_downtime(double value, Error **errp)
{
    value *= 1e9;
    value = MAX(0, MIN(UINT64_MAX, value));
    max_downtime = (uint64_t)value;
}

bool migrate_rdma_pin_all(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->enabled_capabilities[MIGRATION_CAPABILITY_RDMA_PIN_ALL];
}

bool migrate_auto_converge(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->enabled_capabilities[MIGRATION_CAPABILITY_AUTO_CONVERGE];
}

bool migrate_zero_blocks(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->enabled_capabilities[MIGRATION_CAPABILITY_ZERO_BLOCKS];
}

int migrate_use_xbzrle(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->enabled_capabilities[MIGRATION_CAPABILITY_XBZRLE];
}

int64_t migrate_xbzrle_cache_size(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->xbzrle_cache_size;
}

/* migration thread support */

static void *migration_thread(void *opaque)
{
    MigrationState *s = opaque;
    int64_t initial_time = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    int64_t setup_start = qemu_clock_get_ms(QEMU_CLOCK_HOST);
    int64_t initial_bytes = 0;
    int64_t max_size = 0;
    int64_t start_time = initial_time;
    bool old_vm_running = false;

    qemu_savevm_state_begin(s->file, &s->params);

    s->setup_time = qemu_clock_get_ms(QEMU_CLOCK_HOST) - setup_start;
    migrate_set_state(s, MIG_STATE_SETUP, MIG_STATE_ACTIVE);

    while (s->state == MIG_STATE_ACTIVE) {
        int64_t current_time;
        uint64_t pending_size;

        if (!qemu_file_rate_limit(s->file)) {
            pending_size = qemu_savevm_state_pending(s->file, max_size);
            trace_migrate_pending(pending_size, max_size);
            if (pending_size && pending_size >= max_size) {
                qemu_savevm_state_iterate(s->file);
            } else {
                int ret;

                qemu_mutex_lock_iothread();
                start_time = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
                qemu_system_wakeup_request(QEMU_WAKEUP_REASON_OTHER);
                old_vm_running = runstate_is_running();

                ret = vm_stop_force_state(RUN_STATE_FINISH_MIGRATE);
                if (ret >= 0) {
                    qemu_file_set_rate_limit(s->file, INT64_MAX);
                    qemu_savevm_state_complete(s->file);
                }
                qemu_mutex_unlock_iothread();

                if (ret < 0) {
                    migrate_set_state(s, MIG_STATE_ACTIVE, MIG_STATE_ERROR);
                    break;
                }

                if (!qemu_file_get_error(s->file)) {
                    migrate_set_state(s, MIG_STATE_ACTIVE, MIG_STATE_COMPLETED);
                    break;
                }
            }
        }

        if (qemu_file_get_error(s->file)) {
            migrate_set_state(s, MIG_STATE_ACTIVE, MIG_STATE_ERROR);
            break;
        }
        current_time = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
        if (current_time >= initial_time + BUFFER_DELAY) {
            uint64_t transferred_bytes = qemu_ftell(s->file) - initial_bytes;
            uint64_t time_spent = current_time - initial_time;
            double bandwidth = transferred_bytes / time_spent;
            max_size = bandwidth * migrate_max_downtime() / 1000000;

            s->mbps = time_spent ? (((double) transferred_bytes * 8.0) /
                    ((double) time_spent / 1000.0)) / 1000.0 / 1000.0 : -1;

            trace_migrate_transferred(transferred_bytes, time_spent,
                                      bandwidth, max_size);
            /* if we haven't sent anything, we don't want to recalculate
               10000 is a small enough number for our purposes */
            if (s->dirty_bytes_rate && transferred_bytes > 10000) {
                s->expected_downtime = s->dirty_bytes_rate / bandwidth;
            }

            qemu_file_reset_rate_limit(s->file);
            initial_time = current_time;
            initial_bytes = qemu_ftell(s->file);
        }
        if (qemu_file_rate_limit(s->file)) {
            /* usleep expects microseconds */
            g_usleep((initial_time + BUFFER_DELAY - current_time)*1000);
        }
    }

    qemu_mutex_lock_iothread();
    if (s->state == MIG_STATE_COMPLETED) {
        int64_t end_time = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
        s->total_time = end_time - s->total_time;
        s->downtime = end_time - start_time;
        runstate_set(RUN_STATE_POSTMIGRATE);
    } else {
        if (old_vm_running) {
            vm_start();
        }
    }
    qemu_bh_schedule(s->cleanup_bh);
    qemu_mutex_unlock_iothread();

    return NULL;
}

void migrate_fd_connect(MigrationState *s)
{
    s->state = MIG_STATE_SETUP;
    trace_migrate_set_state(MIG_STATE_SETUP);

    /* This is a best 1st approximation. ns to ms */
    s->expected_downtime = max_downtime/1000000;
    s->cleanup_bh = qemu_bh_new(migrate_fd_cleanup, s);

    qemu_file_set_rate_limit(s->file,
                             s->bandwidth_limit / XFER_LIMIT_RATIO);

    /* Notify before starting migration thread */
    notifier_list_notify(&migration_state_notifiers, s);

    qemu_thread_create(&s->thread, "migration", migration_thread, s,
                       QEMU_THREAD_JOINABLE);
}
