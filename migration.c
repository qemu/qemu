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

static NotifierList migration_state_notifiers =
    NOTIFIER_LIST_INITIALIZER(migration_state_notifiers);

/* When we add fault tolerance, we could have several
   migrations at once.  For now we don't need to add
   dynamic creation of migration */

static MigrationState *migrate_get_current(void)
{
    static MigrationState current_migration = {
        .state = MIG_STATE_SETUP,
        .bandwidth_limit = MAX_THROTTLE,
    };

    return &current_migration;
}

int qemu_start_incoming_migration(const char *uri)
{
    const char *p;
    int ret;

    if (strstart(uri, "tcp:", &p))
        ret = tcp_start_incoming_migration(p);
#if !defined(WIN32)
    else if (strstart(uri, "exec:", &p))
        ret =  exec_start_incoming_migration(p);
    else if (strstart(uri, "unix:", &p))
        ret = unix_start_incoming_migration(p);
    else if (strstart(uri, "fd:", &p))
        ret = fd_start_incoming_migration(p);
#endif
    else {
        fprintf(stderr, "unknown migration protocol: %s\n", uri);
        ret = -EPROTONOSUPPORT;
    }
    return ret;
}

void process_incoming_migration(QEMUFile *f)
{
    if (qemu_loadvm_state(f) < 0) {
        fprintf(stderr, "load of migration failed\n");
        exit(0);
    }
    qemu_announce_self();
    DPRINTF("successfully loaded vm state\n");

    /* Make sure all file formats flush their mutable metadata */
    bdrv_invalidate_cache_all();

    if (autostart) {
        vm_start();
    } else {
        runstate_set(RUN_STATE_PRELAUNCH);
    }
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

        info->has_ram = true;
        info->ram = g_malloc0(sizeof(*info->ram));
        info->ram->transferred = ram_bytes_transferred();
        info->ram->remaining = ram_bytes_remaining();
        info->ram->total = ram_bytes_total();

        if (blk_mig_active()) {
            info->has_disk = true;
            info->disk = g_malloc0(sizeof(*info->disk));
            info->disk->transferred = blk_mig_bytes_transferred();
            info->disk->remaining = blk_mig_bytes_remaining();
            info->disk->total = blk_mig_bytes_total();
        }
        break;
    case MIG_STATE_COMPLETED:
        info->has_status = true;
        info->status = g_strdup("completed");
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

/* shared migration helpers */

static int migrate_fd_cleanup(MigrationState *s)
{
    int ret = 0;

    qemu_set_fd_handler2(s->fd, NULL, NULL, NULL, NULL);

    if (s->file) {
        DPRINTF("closing file\n");
        ret = qemu_fclose(s->file);
        s->file = NULL;
    }

    if (s->fd != -1) {
        close(s->fd);
        s->fd = -1;
    }

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

    qemu_set_fd_handler2(s->fd, NULL, NULL, NULL, NULL);
    qemu_file_put_notify(s->file);
    if (s->file && qemu_file_get_error(s->file)) {
        migrate_fd_error(s);
    }
}

static ssize_t migrate_fd_put_buffer(void *opaque, const void *data,
                                     size_t size)
{
    MigrationState *s = opaque;
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

static void migrate_fd_put_ready(void *opaque)
{
    MigrationState *s = opaque;
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

        DPRINTF("done iterating\n");
        vm_stop_force_state(RUN_STATE_FINISH_MIGRATE);

        if (qemu_savevm_state_complete(s->file) < 0) {
            migrate_fd_error(s);
        } else {
            migrate_fd_completed(s);
        }
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

static void migrate_fd_wait_for_unfreeze(void *opaque)
{
    MigrationState *s = opaque;
    int ret;

    DPRINTF("wait for unfreeze\n");
    if (s->state != MIG_STATE_ACTIVE)
        return;

    do {
        fd_set wfds;

        FD_ZERO(&wfds);
        FD_SET(s->fd, &wfds);

        ret = select(s->fd + 1, NULL, &wfds, NULL, NULL);
    } while (ret == -1 && (s->get_error(s)) == EINTR);

    if (ret == -1) {
        qemu_file_set_error(s->file, -s->get_error(s));
    }
}

static int migrate_fd_close(void *opaque)
{
    MigrationState *s = opaque;

    qemu_set_fd_handler2(s->fd, NULL, NULL, NULL, NULL);
    return s->close(s);
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
    s->file = qemu_fopen_ops_buffered(s,
                                      s->bandwidth_limit,
                                      migrate_fd_put_buffer,
                                      migrate_fd_put_ready,
                                      migrate_fd_wait_for_unfreeze,
                                      migrate_fd_close);

    DPRINTF("beginning savevm\n");
    ret = qemu_savevm_state_begin(s->file, s->blk, s->shared);
    if (ret < 0) {
        DPRINTF("failed, %d\n", ret);
        migrate_fd_error(s);
        return;
    }
    migrate_fd_put_ready(s);
}

static MigrationState *migrate_init(int blk, int inc)
{
    MigrationState *s = migrate_get_current();
    int64_t bandwidth_limit = s->bandwidth_limit;

    memset(s, 0, sizeof(*s));
    s->bandwidth_limit = bandwidth_limit;
    s->blk = blk;
    s->shared = inc;

    s->bandwidth_limit = bandwidth_limit;
    s->state = MIG_STATE_SETUP;

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
    MigrationState *s = migrate_get_current();
    const char *p;
    int ret;

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

    s = migrate_init(blk, inc);

    if (strstart(uri, "tcp:", &p)) {
        ret = tcp_start_outgoing_migration(s, p);
#if !defined(WIN32)
    } else if (strstart(uri, "exec:", &p)) {
        ret = exec_start_outgoing_migration(s, p);
    } else if (strstart(uri, "unix:", &p)) {
        ret = unix_start_outgoing_migration(s, p);
    } else if (strstart(uri, "fd:", &p)) {
        ret = fd_start_outgoing_migration(s, p);
#endif
    } else {
        error_set(errp, QERR_INVALID_PARAMETER_VALUE, "uri", "a valid migration protocol");
        return;
    }

    if (ret < 0) {
        DPRINTF("migration failed: %s\n", strerror(-ret));
        /* FIXME: we should return meaningful errors */
        error_set(errp, QERR_UNDEFINED_ERROR);
        return;
    }

    notifier_list_notify(&migration_state_notifiers, s);
}

void qmp_migrate_cancel(Error **errp)
{
    migrate_fd_cancel(migrate_get_current());
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
