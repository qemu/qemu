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
#include "migration.h"
#include "monitor.h"
#include "buffered_file.h"
#include "sysemu.h"
#include "block.h"
#include "qemu_socket.h"
#include "block-migration.h"
#include "qmp-commands.h"

//#define DEBUG_MIGRATION

#ifdef DEBUG_MIGRATION
#define DPRINTF(fmt, ...) \
    do { printf("migration: " fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

enum {
    MIG_STATE_ERROR,
    MIG_STATE_SETUP,
    MIG_STATE_CANCELLED,
    MIG_STATE_ACTIVE,
    MIG_STATE_COMPLETED,
};

#define MAX_THROTTLE  (32 << 20)      /* Migration speed throttling */

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
        .state = MIG_STATE_SETUP,
        .bandwidth_limit = MAX_THROTTLE,
        .xbzrle_cache_size = DEFAULT_MIGRATE_CACHE_SIZE,
    };

    return &current_migration;
}

void qemu_start_incoming_migration(const char *uri, Error **errp)
{
    const char *p;

    if (strstart(uri, "tcp:", &p))
        tcp_start_incoming_migration(p, errp);
#if !defined(WIN32)
    else if (strstart(uri, "exec:", &p))
        exec_start_incoming_migration(p, errp);
    else if (strstart(uri, "unix:", &p))
        unix_start_incoming_migration(p, errp);
    else if (strstart(uri, "fd:", &p))
        fd_start_incoming_migration(p, errp);
#endif
    else {
        error_setg(errp, "unknown migration protocol: %s\n", uri);
    }
}

static void process_incoming_migration_co(void *opaque)
{
    QEMUFile *f = opaque;
    int ret;

    ret = qemu_loadvm_state(f);
    qemu_set_fd_handler(qemu_get_fd(f), NULL, NULL, NULL);
    qemu_fclose(f);
    if (ret < 0) {
        fprintf(stderr, "load of migration failed\n");
        exit(0);
    }
    qemu_announce_self();
    DPRINTF("successfully loaded vm state\n");

    bdrv_clear_incoming_migration_all();
    /* Make sure all file formats flush their mutable metadata */
    bdrv_invalidate_cache_all();

    if (autostart) {
        vm_start();
    } else {
        runstate_set(RUN_STATE_PAUSED);
    }
}

static void enter_migration_coroutine(void *opaque)
{
    Coroutine *co = opaque;
    qemu_coroutine_enter(co, NULL);
}

void process_incoming_migration(QEMUFile *f)
{
    Coroutine *co = qemu_coroutine_create(process_incoming_migration_co);
    int fd = qemu_get_fd(f);

    assert(fd != -1);
    socket_set_nonblock(fd);
    qemu_set_fd_handler(fd, enter_migration_coroutine, NULL, co);
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
    case MIG_STATE_SETUP:
        /* no migration has happened ever */
        break;
    case MIG_STATE_ACTIVE:
        info->has_status = true;
        info->status = g_strdup("active");
        info->has_total_time = true;
        info->total_time = qemu_get_clock_ms(rt_clock)
            - s->total_time;
        info->has_expected_downtime = true;
        info->expected_downtime = s->expected_downtime;

        info->has_ram = true;
        info->ram = g_malloc0(sizeof(*info->ram));
        info->ram->transferred = ram_bytes_transferred();
        info->ram->remaining = ram_bytes_remaining();
        info->ram->total = ram_bytes_total();
        info->ram->duplicate = dup_mig_pages_transferred();
        info->ram->normal = norm_mig_pages_transferred();
        info->ram->normal_bytes = norm_mig_bytes_transferred();
        info->ram->dirty_pages_rate = s->dirty_pages_rate;


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
        info->total_time = s->total_time;
        info->has_downtime = true;
        info->downtime = s->downtime;

        info->has_ram = true;
        info->ram = g_malloc0(sizeof(*info->ram));
        info->ram->transferred = ram_bytes_transferred();
        info->ram->remaining = 0;
        info->ram->total = ram_bytes_total();
        info->ram->duplicate = dup_mig_pages_transferred();
        info->ram->normal = norm_mig_pages_transferred();
        info->ram->normal_bytes = norm_mig_bytes_transferred();
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

    if (s->state == MIG_STATE_ACTIVE) {
        error_set(errp, QERR_MIGRATION_ACTIVE);
        return;
    }

    for (cap = params; cap; cap = cap->next) {
        s->enabled_capabilities[cap->value->capability] = cap->value->state;
    }
}

/* shared migration helpers */

static int migrate_fd_cleanup(MigrationState *s)
{
    int ret = 0;

    if (s->file) {
        DPRINTF("closing file\n");
        ret = qemu_fclose(s->file);
        s->file = NULL;
    }

    migrate_fd_close(s);
    return ret;
}

void migrate_fd_error(MigrationState *s)
{
    DPRINTF("setting error state\n");
    s->state = MIG_STATE_ERROR;
    notifier_list_notify(&migration_state_notifiers, s);
    migrate_fd_cleanup(s);
}

static void migrate_fd_completed(MigrationState *s)
{
    DPRINTF("setting completed state\n");
    if (migrate_fd_cleanup(s) < 0) {
        s->state = MIG_STATE_ERROR;
    } else {
        s->state = MIG_STATE_COMPLETED;
        runstate_set(RUN_STATE_POSTMIGRATE);
    }
    notifier_list_notify(&migration_state_notifiers, s);
}

static void migrate_fd_put_notify(void *opaque)
{
    MigrationState *s = opaque;
    int ret;

    qemu_set_fd_handler2(s->fd, NULL, NULL, NULL, NULL);
    ret = qemu_file_put_notify(s->file);
    if (ret) {
        migrate_fd_error(s);
    }
}

ssize_t migrate_fd_put_buffer(MigrationState *s, const void *data,
                              size_t size)
{
    ssize_t ret;

    if (s->state != MIG_STATE_ACTIVE) {
        return -EIO;
    }

    do {
        ret = s->write(s, data, size);
    } while (ret == -1 && ((s->get_error(s)) == EINTR));

    if (ret == -1)
        ret = -(s->get_error(s));

    if (ret == -EAGAIN) {
        qemu_set_fd_handler2(s->fd, NULL, NULL, migrate_fd_put_notify, s);
    }

    return ret;
}

void migrate_fd_put_ready(MigrationState *s)
{
    int ret;

    if (s->state != MIG_STATE_ACTIVE) {
        DPRINTF("put_ready returning because of non-active state\n");
        return;
    }

    DPRINTF("iterate\n");
    ret = qemu_savevm_state_iterate(s->file);
    if (ret < 0) {
        migrate_fd_error(s);
    } else if (ret == 1) {
        int old_vm_running = runstate_is_running();
        int64_t start_time, end_time;

        DPRINTF("done iterating\n");
        start_time = qemu_get_clock_ms(rt_clock);
        qemu_system_wakeup_request(QEMU_WAKEUP_REASON_OTHER);
        vm_stop_force_state(RUN_STATE_FINISH_MIGRATE);

        if (qemu_savevm_state_complete(s->file) < 0) {
            migrate_fd_error(s);
        } else {
            migrate_fd_completed(s);
        }
        end_time = qemu_get_clock_ms(rt_clock);
        s->total_time = end_time - s->total_time;
        s->downtime = end_time - start_time;
        if (s->state != MIG_STATE_COMPLETED) {
            if (old_vm_running) {
                vm_start();
            }
        }
    }
}

static void migrate_fd_cancel(MigrationState *s)
{
    if (s->state != MIG_STATE_ACTIVE)
        return;

    DPRINTF("cancelling migration\n");

    s->state = MIG_STATE_CANCELLED;
    notifier_list_notify(&migration_state_notifiers, s);
    qemu_savevm_state_cancel(s->file);

    migrate_fd_cleanup(s);
}

int migrate_fd_wait_for_unfreeze(MigrationState *s)
{
    int ret;

    DPRINTF("wait for unfreeze\n");
    if (s->state != MIG_STATE_ACTIVE)
        return -EINVAL;

    do {
        fd_set wfds;

        FD_ZERO(&wfds);
        FD_SET(s->fd, &wfds);

        ret = select(s->fd + 1, NULL, &wfds, NULL, NULL);
    } while (ret == -1 && (s->get_error(s)) == EINTR);

    if (ret == -1) {
        return -s->get_error(s);
    }
    return 0;
}

int migrate_fd_close(MigrationState *s)
{
    int rc = 0;
    if (s->fd != -1) {
        qemu_set_fd_handler2(s->fd, NULL, NULL, NULL, NULL);
        rc = s->close(s);
        s->fd = -1;
    }
    return rc;
}

void add_migration_state_change_notifier(Notifier *notify)
{
    notifier_list_add(&migration_state_notifiers, notify);
}

void remove_migration_state_change_notifier(Notifier *notify)
{
    notifier_remove(notify);
}

bool migration_is_active(MigrationState *s)
{
    return s->state == MIG_STATE_ACTIVE;
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

void migrate_fd_connect(MigrationState *s)
{
    int ret;

    s->state = MIG_STATE_ACTIVE;
    s->file = qemu_fopen_ops_buffered(s);

    DPRINTF("beginning savevm\n");
    ret = qemu_savevm_state_begin(s->file, &s->params);
    if (ret < 0) {
        DPRINTF("failed, %d\n", ret);
        migrate_fd_error(s);
        return;
    }
    migrate_fd_put_ready(s);
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
    s->bandwidth_limit = bandwidth_limit;
    s->params = *params;
    memcpy(s->enabled_capabilities, enabled_capabilities,
           sizeof(enabled_capabilities));
    s->xbzrle_cache_size = xbzrle_cache_size;

    s->bandwidth_limit = bandwidth_limit;
    s->state = MIG_STATE_SETUP;
    s->total_time = qemu_get_clock_ms(rt_clock);

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

    params.blk = blk;
    params.shared = inc;

    if (s->state == MIG_STATE_ACTIVE) {
        error_set(errp, QERR_MIGRATION_ACTIVE);
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
        return;
    }

    if (local_err) {
        migrate_fd_error(s);
        error_propagate(errp, local_err);
        return;
    }

    notifier_list_notify(&migration_state_notifiers, s);
}

void qmp_migrate_cancel(Error **errp)
{
    migrate_fd_cancel(migrate_get_current());
}

void qmp_migrate_set_cache_size(int64_t value, Error **errp)
{
    MigrationState *s = migrate_get_current();

    /* Check for truncation */
    if (value != (size_t)value) {
        error_set(errp, QERR_INVALID_PARAMETER_VALUE, "cache size",
                  "exceeding address space");
        return;
    }

    s->xbzrle_cache_size = xbzrle_cache_resize(value);
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

    s = migrate_get_current();
    s->bandwidth_limit = value;
    qemu_file_set_rate_limit(s->file, s->bandwidth_limit);
}

void qmp_migrate_set_downtime(double value, Error **errp)
{
    value *= 1e9;
    value = MAX(0, MIN(UINT64_MAX, value));
    max_downtime = (uint64_t)value;
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
