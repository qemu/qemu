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
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "migration/migration.h"
#include "migration/qemu-file.h"
#include "sysemu/sysemu.h"
#include "block/block.h"
#include "qapi/qmp/qerror.h"
#include "qemu/sockets.h"
#include "qemu/rcu.h"
#include "migration/block.h"
#include "qemu/thread.h"
#include "qmp-commands.h"
#include "trace.h"
#include "qapi/util.h"
#include "qapi-event.h"

#define MAX_THROTTLE  (32 << 20)      /* Migration speed throttling */

/* Amount of time to allocate to each "chunk" of bandwidth-throttled
 * data. */
#define BUFFER_DELAY     100
#define XFER_LIMIT_RATIO (1000 / BUFFER_DELAY)

/* Default compression thread count */
#define DEFAULT_MIGRATE_COMPRESS_THREAD_COUNT 8
/* Default decompression thread count, usually decompression is at
 * least 4 times as fast as compression.*/
#define DEFAULT_MIGRATE_DECOMPRESS_THREAD_COUNT 2
/*0: means nocompress, 1: best speed, ... 9: best compress ratio */
#define DEFAULT_MIGRATE_COMPRESS_LEVEL 1

/* Migration XBZRLE default cache size */
#define DEFAULT_MIGRATE_CACHE_SIZE (64 * 1024 * 1024)

static NotifierList migration_state_notifiers =
    NOTIFIER_LIST_INITIALIZER(migration_state_notifiers);

static bool deferred_incoming;

/* When we add fault tolerance, we could have several
   migrations at once.  For now we don't need to add
   dynamic creation of migration */

/* For outgoing */
MigrationState *migrate_get_current(void)
{
    static MigrationState current_migration = {
        .state = MIGRATION_STATUS_NONE,
        .bandwidth_limit = MAX_THROTTLE,
        .xbzrle_cache_size = DEFAULT_MIGRATE_CACHE_SIZE,
        .mbps = -1,
        .parameters[MIGRATION_PARAMETER_COMPRESS_LEVEL] =
                DEFAULT_MIGRATE_COMPRESS_LEVEL,
        .parameters[MIGRATION_PARAMETER_COMPRESS_THREADS] =
                DEFAULT_MIGRATE_COMPRESS_THREAD_COUNT,
        .parameters[MIGRATION_PARAMETER_DECOMPRESS_THREADS] =
                DEFAULT_MIGRATE_DECOMPRESS_THREAD_COUNT,
    };

    return &current_migration;
}

/* For incoming */
static MigrationIncomingState *mis_current;

MigrationIncomingState *migration_incoming_get_current(void)
{
    return mis_current;
}

MigrationIncomingState *migration_incoming_state_new(QEMUFile* f)
{
    mis_current = g_malloc0(sizeof(MigrationIncomingState));
    mis_current->file = f;
    QLIST_INIT(&mis_current->loadvm_handlers);

    return mis_current;
}

void migration_incoming_state_destroy(void)
{
    loadvm_free_handlers(mis_current);
    g_free(mis_current);
    mis_current = NULL;
}


typedef struct {
    bool optional;
    uint32_t size;
    uint8_t runstate[100];
    RunState state;
    bool received;
} GlobalState;

static GlobalState global_state;

int global_state_store(void)
{
    if (!runstate_store((char *)global_state.runstate,
                        sizeof(global_state.runstate))) {
        error_report("runstate name too big: %s", global_state.runstate);
        trace_migrate_state_too_big();
        return -EINVAL;
    }
    return 0;
}

void global_state_store_running(void)
{
    const char *state = RunState_lookup[RUN_STATE_RUNNING];
    strncpy((char *)global_state.runstate,
           state, sizeof(global_state.runstate));
}

static bool global_state_received(void)
{
    return global_state.received;
}

static RunState global_state_get_runstate(void)
{
    return global_state.state;
}

void global_state_set_optional(void)
{
    global_state.optional = true;
}

static bool global_state_needed(void *opaque)
{
    GlobalState *s = opaque;
    char *runstate = (char *)s->runstate;

    /* If it is not optional, it is mandatory */

    if (s->optional == false) {
        return true;
    }

    /* If state is running or paused, it is not needed */

    if (strcmp(runstate, "running") == 0 ||
        strcmp(runstate, "paused") == 0) {
        return false;
    }

    /* for any other state it is needed */
    return true;
}

static int global_state_post_load(void *opaque, int version_id)
{
    GlobalState *s = opaque;
    Error *local_err = NULL;
    int r;
    char *runstate = (char *)s->runstate;

    s->received = true;
    trace_migrate_global_state_post_load(runstate);

    r = qapi_enum_parse(RunState_lookup, runstate, RUN_STATE_MAX,
                                -1, &local_err);

    if (r == -1) {
        if (local_err) {
            error_report_err(local_err);
        }
        return -EINVAL;
    }
    s->state = r;

    return 0;
}

static void global_state_pre_save(void *opaque)
{
    GlobalState *s = opaque;

    trace_migrate_global_state_pre_save((char *)s->runstate);
    s->size = strlen((char *)s->runstate) + 1;
}

static const VMStateDescription vmstate_globalstate = {
    .name = "globalstate",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = global_state_post_load,
    .pre_save = global_state_pre_save,
    .needed = global_state_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(size, GlobalState),
        VMSTATE_BUFFER(runstate, GlobalState),
        VMSTATE_END_OF_LIST()
    },
};

void register_global_state(void)
{
    /* We would use it independently that we receive it */
    strcpy((char *)&global_state.runstate, "");
    global_state.received = false;
    vmstate_register(NULL, 0, &vmstate_globalstate, &global_state);
}

static void migrate_generate_event(int new_state)
{
    if (migrate_use_events()) {
        qapi_event_send_migration(new_state, &error_abort);
    }
}

/*
 * Called on -incoming with a defer: uri.
 * The migration can be started later after any parameters have been
 * changed.
 */
static void deferred_incoming_migration(Error **errp)
{
    if (deferred_incoming) {
        error_setg(errp, "Incoming migration already deferred");
    }
    deferred_incoming = true;
}

void qemu_start_incoming_migration(const char *uri, Error **errp)
{
    const char *p;

    qapi_event_send_migration(MIGRATION_STATUS_SETUP, &error_abort);
    if (!strcmp(uri, "defer")) {
        deferred_incoming_migration(errp);
    } else if (strstart(uri, "tcp:", &p)) {
        tcp_start_incoming_migration(p, errp);
#ifdef CONFIG_RDMA
    } else if (strstart(uri, "rdma:", &p)) {
        rdma_start_incoming_migration(p, errp);
#endif
#if !defined(WIN32)
    } else if (strstart(uri, "exec:", &p)) {
        exec_start_incoming_migration(p, errp);
    } else if (strstart(uri, "unix:", &p)) {
        unix_start_incoming_migration(p, errp);
    } else if (strstart(uri, "fd:", &p)) {
        fd_start_incoming_migration(p, errp);
#endif
    } else {
        error_setg(errp, "unknown migration protocol: %s", uri);
    }
}

static void process_incoming_migration_co(void *opaque)
{
    QEMUFile *f = opaque;
    Error *local_err = NULL;
    int ret;

    migration_incoming_state_new(f);
    migrate_generate_event(MIGRATION_STATUS_ACTIVE);
    ret = qemu_loadvm_state(f);

    qemu_fclose(f);
    free_xbzrle_decoded_buf();
    migration_incoming_state_destroy();

    if (ret < 0) {
        migrate_generate_event(MIGRATION_STATUS_FAILED);
        error_report("load of migration failed: %s", strerror(-ret));
        migrate_decompress_threads_join();
        exit(EXIT_FAILURE);
    }
    migrate_generate_event(MIGRATION_STATUS_COMPLETED);
    qemu_announce_self();

    /* Make sure all file formats flush their mutable metadata */
    bdrv_invalidate_cache_all(&local_err);
    if (local_err) {
        error_report_err(local_err);
        migrate_decompress_threads_join();
        exit(EXIT_FAILURE);
    }

    /* If global state section was not received or we are in running
       state, we need to obey autostart. Any other state is set with
       runstate_set. */

    if (!global_state_received() ||
        global_state_get_runstate() == RUN_STATE_RUNNING) {
        if (autostart) {
            vm_start();
        } else {
            runstate_set(RUN_STATE_PAUSED);
        }
    } else {
        runstate_set(global_state_get_runstate());
    }
    migrate_decompress_threads_join();
}

void process_incoming_migration(QEMUFile *f)
{
    Coroutine *co = qemu_coroutine_create(process_incoming_migration_co);
    int fd = qemu_get_fd(f);

    assert(fd != -1);
    migrate_decompress_threads_create();
    qemu_set_nonblock(fd);
    qemu_coroutine_enter(co, f);
}

/* amount of nanoseconds we are willing to wait for migration to be down.
 * the choice of nanoseconds is because it is the maximum resolution that
 * get_clock() can achieve. It is an internal measure. All user-visible
 * units must be in seconds */
static uint64_t max_downtime = 300000000;

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

MigrationParameters *qmp_query_migrate_parameters(Error **errp)
{
    MigrationParameters *params;
    MigrationState *s = migrate_get_current();

    params = g_malloc0(sizeof(*params));
    params->compress_level = s->parameters[MIGRATION_PARAMETER_COMPRESS_LEVEL];
    params->compress_threads =
            s->parameters[MIGRATION_PARAMETER_COMPRESS_THREADS];
    params->decompress_threads =
            s->parameters[MIGRATION_PARAMETER_DECOMPRESS_THREADS];

    return params;
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
        info->xbzrle_cache->cache_miss_rate = xbzrle_mig_cache_miss_rate();
        info->xbzrle_cache->overflow = xbzrle_mig_pages_overflow();
    }
}

MigrationInfo *qmp_query_migrate(Error **errp)
{
    MigrationInfo *info = g_malloc0(sizeof(*info));
    MigrationState *s = migrate_get_current();

    switch (s->state) {
    case MIGRATION_STATUS_NONE:
        /* no migration has happened ever */
        break;
    case MIGRATION_STATUS_SETUP:
        info->has_status = true;
        info->has_total_time = false;
        break;
    case MIGRATION_STATUS_ACTIVE:
    case MIGRATION_STATUS_CANCELLING:
        info->has_status = true;
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
        info->ram->dirty_sync_count = s->dirty_sync_count;

        if (blk_mig_active()) {
            info->has_disk = true;
            info->disk = g_malloc0(sizeof(*info->disk));
            info->disk->transferred = blk_mig_bytes_transferred();
            info->disk->remaining = blk_mig_bytes_remaining();
            info->disk->total = blk_mig_bytes_total();
        }

        get_xbzrle_cache_stats(info);
        break;
    case MIGRATION_STATUS_COMPLETED:
        get_xbzrle_cache_stats(info);

        info->has_status = true;
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
        info->ram->dirty_sync_count = s->dirty_sync_count;
        break;
    case MIGRATION_STATUS_FAILED:
        info->has_status = true;
        break;
    case MIGRATION_STATUS_CANCELLED:
        info->has_status = true;
        break;
    }
    info->status = s->state;

    return info;
}

void qmp_migrate_set_capabilities(MigrationCapabilityStatusList *params,
                                  Error **errp)
{
    MigrationState *s = migrate_get_current();
    MigrationCapabilityStatusList *cap;

    if (s->state == MIGRATION_STATUS_ACTIVE ||
        s->state == MIGRATION_STATUS_SETUP) {
        error_setg(errp, QERR_MIGRATION_ACTIVE);
        return;
    }

    for (cap = params; cap; cap = cap->next) {
        s->enabled_capabilities[cap->value->capability] = cap->value->state;
    }
}

void qmp_migrate_set_parameters(bool has_compress_level,
                                int64_t compress_level,
                                bool has_compress_threads,
                                int64_t compress_threads,
                                bool has_decompress_threads,
                                int64_t decompress_threads, Error **errp)
{
    MigrationState *s = migrate_get_current();

    if (has_compress_level && (compress_level < 0 || compress_level > 9)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "compress_level",
                   "is invalid, it should be in the range of 0 to 9");
        return;
    }
    if (has_compress_threads &&
            (compress_threads < 1 || compress_threads > 255)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                   "compress_threads",
                   "is invalid, it should be in the range of 1 to 255");
        return;
    }
    if (has_decompress_threads &&
            (decompress_threads < 1 || decompress_threads > 255)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                   "decompress_threads",
                   "is invalid, it should be in the range of 1 to 255");
        return;
    }

    if (has_compress_level) {
        s->parameters[MIGRATION_PARAMETER_COMPRESS_LEVEL] = compress_level;
    }
    if (has_compress_threads) {
        s->parameters[MIGRATION_PARAMETER_COMPRESS_THREADS] = compress_threads;
    }
    if (has_decompress_threads) {
        s->parameters[MIGRATION_PARAMETER_DECOMPRESS_THREADS] =
                                                    decompress_threads;
    }
}

/* shared migration helpers */

static void migrate_set_state(MigrationState *s, int old_state, int new_state)
{
    if (atomic_cmpxchg(&s->state, old_state, new_state) == old_state) {
        trace_migrate_set_state(new_state);
        migrate_generate_event(new_state);
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

        migrate_compress_threads_join();
        qemu_fclose(s->file);
        s->file = NULL;
    }

    assert(s->state != MIGRATION_STATUS_ACTIVE);

    if (s->state != MIGRATION_STATUS_COMPLETED) {
        qemu_savevm_state_cancel();
        if (s->state == MIGRATION_STATUS_CANCELLING) {
            migrate_set_state(s, MIGRATION_STATUS_CANCELLING,
                              MIGRATION_STATUS_CANCELLED);
        }
    }

    notifier_list_notify(&migration_state_notifiers, s);
}

void migrate_fd_error(MigrationState *s)
{
    trace_migrate_fd_error();
    assert(s->file == NULL);
    migrate_set_state(s, MIGRATION_STATUS_SETUP, MIGRATION_STATUS_FAILED);
    notifier_list_notify(&migration_state_notifiers, s);
}

static void migrate_fd_cancel(MigrationState *s)
{
    int old_state ;
    QEMUFile *f = migrate_get_current()->file;
    trace_migrate_fd_cancel();

    do {
        old_state = s->state;
        if (old_state != MIGRATION_STATUS_SETUP &&
            old_state != MIGRATION_STATUS_ACTIVE) {
            break;
        }
        migrate_set_state(s, old_state, MIGRATION_STATUS_CANCELLING);
    } while (s->state != MIGRATION_STATUS_CANCELLING);

    /*
     * If we're unlucky the migration code might be stuck somewhere in a
     * send/write while the network has failed and is waiting to timeout;
     * if we've got shutdown(2) available then we can force it to quit.
     * The outgoing qemu file gets closed in migrate_fd_cleanup that is
     * called in a bh, so there is no race against this cancel.
     */
    if (s->state == MIGRATION_STATUS_CANCELLING && f) {
        qemu_file_shutdown(f);
    }
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

static MigrationState *migrate_init(const MigrationParams *params)
{
    MigrationState *s = migrate_get_current();
    int64_t bandwidth_limit = s->bandwidth_limit;
    bool enabled_capabilities[MIGRATION_CAPABILITY_MAX];
    int64_t xbzrle_cache_size = s->xbzrle_cache_size;
    int compress_level = s->parameters[MIGRATION_PARAMETER_COMPRESS_LEVEL];
    int compress_thread_count =
            s->parameters[MIGRATION_PARAMETER_COMPRESS_THREADS];
    int decompress_thread_count =
            s->parameters[MIGRATION_PARAMETER_DECOMPRESS_THREADS];

    memcpy(enabled_capabilities, s->enabled_capabilities,
           sizeof(enabled_capabilities));

    memset(s, 0, sizeof(*s));
    s->params = *params;
    memcpy(s->enabled_capabilities, enabled_capabilities,
           sizeof(enabled_capabilities));
    s->xbzrle_cache_size = xbzrle_cache_size;

    s->parameters[MIGRATION_PARAMETER_COMPRESS_LEVEL] = compress_level;
    s->parameters[MIGRATION_PARAMETER_COMPRESS_THREADS] =
               compress_thread_count;
    s->parameters[MIGRATION_PARAMETER_DECOMPRESS_THREADS] =
               decompress_thread_count;
    s->bandwidth_limit = bandwidth_limit;
    migrate_set_state(s, MIGRATION_STATUS_NONE, MIGRATION_STATUS_SETUP);

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

void qmp_migrate_incoming(const char *uri, Error **errp)
{
    Error *local_err = NULL;
    static bool once = true;

    if (!deferred_incoming) {
        error_setg(errp, "For use with '-incoming defer'");
        return;
    }
    if (!once) {
        error_setg(errp, "The incoming migration has already been started");
    }

    qemu_start_incoming_migration(uri, &local_err);

    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    once = false;
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

    if (s->state == MIGRATION_STATUS_ACTIVE ||
        s->state == MIGRATION_STATUS_SETUP ||
        s->state == MIGRATION_STATUS_CANCELLING) {
        error_setg(errp, QERR_MIGRATION_ACTIVE);
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

    /* We are starting a new migration, so we want to start in a clean
       state.  This change is only needed if previous migration
       failed/was cancelled.  We don't use migrate_set_state() because
       we are setting the initial state, not changing it. */
    s->state = MIGRATION_STATUS_NONE;

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
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "uri",
                   "a valid migration protocol");
        migrate_set_state(s, MIGRATION_STATUS_SETUP, MIGRATION_STATUS_FAILED);
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
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "cache size",
                   "exceeding address space");
        return;
    }

    /* Cache should not be larger than guest ram size */
    if (value > ram_bytes_total()) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "cache size",
                   "exceeds guest ram size ");
        return;
    }

    new_size = xbzrle_cache_resize(value);
    if (new_size < 0) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "cache size",
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

bool migrate_use_compression(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->enabled_capabilities[MIGRATION_CAPABILITY_COMPRESS];
}

int migrate_compress_level(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->parameters[MIGRATION_PARAMETER_COMPRESS_LEVEL];
}

int migrate_compress_threads(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->parameters[MIGRATION_PARAMETER_COMPRESS_THREADS];
}

int migrate_decompress_threads(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->parameters[MIGRATION_PARAMETER_DECOMPRESS_THREADS];
}

bool migrate_use_events(void)
{
    MigrationState *s;

    s = migrate_get_current();

    return s->enabled_capabilities[MIGRATION_CAPABILITY_EVENTS];
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

    rcu_register_thread();

    qemu_savevm_state_header(s->file);
    qemu_savevm_state_begin(s->file, &s->params);

    s->setup_time = qemu_clock_get_ms(QEMU_CLOCK_HOST) - setup_start;
    migrate_set_state(s, MIGRATION_STATUS_SETUP, MIGRATION_STATUS_ACTIVE);

    while (s->state == MIGRATION_STATUS_ACTIVE) {
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

                ret = global_state_store();
                if (!ret) {
                    ret = vm_stop_force_state(RUN_STATE_FINISH_MIGRATE);
                    if (ret >= 0) {
                        qemu_file_set_rate_limit(s->file, INT64_MAX);
                        qemu_savevm_state_complete(s->file);
                    }
                }
                qemu_mutex_unlock_iothread();

                if (ret < 0) {
                    migrate_set_state(s, MIGRATION_STATUS_ACTIVE,
                                      MIGRATION_STATUS_FAILED);
                    break;
                }

                if (!qemu_file_get_error(s->file)) {
                    migrate_set_state(s, MIGRATION_STATUS_ACTIVE,
                                      MIGRATION_STATUS_COMPLETED);
                    break;
                }
            }
        }

        if (qemu_file_get_error(s->file)) {
            migrate_set_state(s, MIGRATION_STATUS_ACTIVE,
                              MIGRATION_STATUS_FAILED);
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
    if (s->state == MIGRATION_STATUS_COMPLETED) {
        int64_t end_time = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
        uint64_t transferred_bytes = qemu_ftell(s->file);
        s->total_time = end_time - s->total_time;
        s->downtime = end_time - start_time;
        if (s->total_time) {
            s->mbps = (((double) transferred_bytes * 8.0) /
                       ((double) s->total_time)) / 1000;
        }
        runstate_set(RUN_STATE_POSTMIGRATE);
    } else {
        if (old_vm_running) {
            vm_start();
        }
    }
    qemu_bh_schedule(s->cleanup_bh);
    qemu_mutex_unlock_iothread();

    rcu_unregister_thread();
    return NULL;
}

void migrate_fd_connect(MigrationState *s)
{
    /* This is a best 1st approximation. ns to ms */
    s->expected_downtime = max_downtime/1000000;
    s->cleanup_bh = qemu_bh_new(migrate_fd_cleanup, s);

    qemu_file_set_rate_limit(s->file,
                             s->bandwidth_limit / XFER_LIMIT_RATIO);

    /* Notify before starting migration thread */
    notifier_list_notify(&migration_state_notifiers, s);

    migrate_compress_threads_create();
    qemu_thread_create(&s->thread, "migration", migration_thread, s,
                       QEMU_THREAD_JOINABLE);
}
