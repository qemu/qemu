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
 */

#include "qemu-common.h"
#include "migration.h"
#include "monitor.h"
#include "buffered_file.h"
#include "sysemu.h"
#include "block.h"
#include "qemu_socket.h"
#include "block-migration.h"
#include "qemu-objects.h"

//#define DEBUG_MIGRATION

#ifdef DEBUG_MIGRATION
#define dprintf(fmt, ...) \
    do { printf("migration: " fmt, ## __VA_ARGS__); } while (0)
#else
#define dprintf(fmt, ...) \
    do { } while (0)
#endif

/* Migration speed throttling */
static uint32_t max_throttle = (32 << 20);

static MigrationState *current_migration;

void qemu_start_incoming_migration(const char *uri)
{
    const char *p;

    if (strstart(uri, "tcp:", &p))
        tcp_start_incoming_migration(p);
#if !defined(WIN32)
    else if (strstart(uri, "exec:", &p))
        exec_start_incoming_migration(p);
    else if (strstart(uri, "unix:", &p))
        unix_start_incoming_migration(p);
    else if (strstart(uri, "fd:", &p))
        fd_start_incoming_migration(p);
#endif
    else
        fprintf(stderr, "unknown migration protocol: %s\n", uri);
}

void do_migrate(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    MigrationState *s = NULL;
    const char *p;
    int detach = qdict_get_int(qdict, "detach");
    const char *uri = qdict_get_str(qdict, "uri");

    if (current_migration &&
        current_migration->get_status(current_migration) == MIG_STATE_ACTIVE) {
        monitor_printf(mon, "migration already in progress\n");
        return;
    }

    if (strstart(uri, "tcp:", &p))
        s = tcp_start_outgoing_migration(mon, p, max_throttle, detach,
                                         (int)qdict_get_int(qdict, "blk"), 
                                         (int)qdict_get_int(qdict, "inc"));
#if !defined(WIN32)
    else if (strstart(uri, "exec:", &p))
        s = exec_start_outgoing_migration(mon, p, max_throttle, detach,
                                          (int)qdict_get_int(qdict, "blk"), 
                                          (int)qdict_get_int(qdict, "inc"));
    else if (strstart(uri, "unix:", &p))
        s = unix_start_outgoing_migration(mon, p, max_throttle, detach,
					  (int)qdict_get_int(qdict, "blk"), 
                                          (int)qdict_get_int(qdict, "inc"));
    else if (strstart(uri, "fd:", &p))
        s = fd_start_outgoing_migration(mon, p, max_throttle, detach, 
                                        (int)qdict_get_int(qdict, "blk"), 
                                        (int)qdict_get_int(qdict, "inc"));
#endif
    else
        monitor_printf(mon, "unknown migration protocol: %s\n", uri);

    if (s == NULL)
        monitor_printf(mon, "migration failed\n");
    else {
        if (current_migration)
            current_migration->release(current_migration);

        current_migration = s;
    }
}

void do_migrate_cancel(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    MigrationState *s = current_migration;

    if (s)
        s->cancel(s);
}

void do_migrate_set_speed(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    double d;
    FdMigrationState *s;

    d = qdict_get_double(qdict, "value");
    d = MAX(0, MIN(UINT32_MAX, d));
    max_throttle = d;

    s = migrate_to_fms(current_migration);
    if (s && s->file) {
        qemu_file_set_rate_limit(s->file, max_throttle);
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

void do_migrate_set_downtime(Monitor *mon, const QDict *qdict,
                             QObject **ret_data)
{
    double d;

    d = qdict_get_double(qdict, "value") * 1e9;
    d = MAX(0, MIN(UINT64_MAX, d));
    max_downtime = (uint64_t)d;
}

static void migrate_print_status(Monitor *mon, const char *name,
                                 const QDict *status_dict)
{
    QDict *qdict;

    qdict = qobject_to_qdict(qdict_get(status_dict, name));

    monitor_printf(mon, "transferred %s: %" PRIu64 " kbytes\n", name,
                        qdict_get_int(qdict, "transferred") >> 10);
    monitor_printf(mon, "remaining %s: %" PRIu64 " kbytes\n", name,
                        qdict_get_int(qdict, "remaining") >> 10);
    monitor_printf(mon, "total %s: %" PRIu64 " kbytes\n", name,
                        qdict_get_int(qdict, "total") >> 10);
}

void do_info_migrate_print(Monitor *mon, const QObject *data)
{
    QDict *qdict;

    qdict = qobject_to_qdict(data);

    monitor_printf(mon, "Migration status: %s\n",
                   qdict_get_str(qdict, "status"));

    if (qdict_haskey(qdict, "ram")) {
        migrate_print_status(mon, "ram", qdict);
    }

    if (qdict_haskey(qdict, "disk")) {
        migrate_print_status(mon, "disk", qdict);
    }
}

static void migrate_put_status(QDict *qdict, const char *name,
                               uint64_t trans, uint64_t rem, uint64_t total)
{
    QObject *obj;

    obj = qobject_from_jsonf("{ 'transferred': %" PRId64 ", "
                               "'remaining': %" PRId64 ", "
                               "'total': %" PRId64 " }", trans, rem, total);
    assert(obj != NULL);

    qdict_put_obj(qdict, name, obj);
}

/**
 * do_info_migrate(): Migration status
 *
 * Return a QDict. If migration is active there will be another
 * QDict with RAM migration status and if block migration is active
 * another one with block migration status.
 *
 * The main QDict contains the following:
 *
 * - "status": migration status
 * - "ram": only present if "status" is "active", it is a QDict with the
 *   following RAM information (in bytes):
 *          - "transferred": amount transferred
 *          - "remaining": amount remaining
 *          - "total": total
 * - "disk": only present if "status" is "active" and it is a block migration,
 *   it is a QDict with the following disk information (in bytes):
 *          - "transferred": amount transferred
 *          - "remaining": amount remaining
 *          - "total": total
 *
 * Examples:
 *
 * 1. Migration is "completed":
 *
 * { "status": "completed" }
 *
 * 2. Migration is "active" and it is not a block migration:
 *
 * { "status": "active",
 *            "ram": { "transferred": 123, "remaining": 123, "total": 246 } }
 *
 * 3. Migration is "active" and it is a block migration:
 *
 * { "status": "active",
 *   "ram": { "total": 1057024, "remaining": 1053304, "transferred": 3720 },
 *   "disk": { "total": 20971520, "remaining": 20880384, "transferred": 91136 }}
 */
void do_info_migrate(Monitor *mon, QObject **ret_data)
{
    QDict *qdict;
    MigrationState *s = current_migration;

    if (s) {
        switch (s->get_status(s)) {
        case MIG_STATE_ACTIVE:
            qdict = qdict_new();
            qdict_put(qdict, "status", qstring_from_str("active"));

            migrate_put_status(qdict, "ram", ram_bytes_transferred(),
                               ram_bytes_remaining(), ram_bytes_total());

            if (blk_mig_active()) {
                migrate_put_status(qdict, "disk", blk_mig_bytes_transferred(),
                                   blk_mig_bytes_remaining(),
                                   blk_mig_bytes_total());
            }

            *ret_data = QOBJECT(qdict);
            break;
        case MIG_STATE_COMPLETED:
            *ret_data = qobject_from_jsonf("{ 'status': 'completed' }");
            break;
        case MIG_STATE_ERROR:
            *ret_data = qobject_from_jsonf("{ 'status': 'failed' }");
            break;
        case MIG_STATE_CANCELLED:
            *ret_data = qobject_from_jsonf("{ 'status': 'cancelled' }");
            break;
        }
        assert(*ret_data != NULL);
    }
}

/* shared migration helpers */

void migrate_fd_monitor_suspend(FdMigrationState *s, Monitor *mon)
{
    s->mon = mon;
    if (monitor_suspend(mon) == 0) {
        dprintf("suspending monitor\n");
    } else {
        monitor_printf(mon, "terminal does not allow synchronous "
                       "migration, continuing detached\n");
    }
}

void migrate_fd_error(FdMigrationState *s)
{
    dprintf("setting error state\n");
    s->state = MIG_STATE_ERROR;
    migrate_fd_cleanup(s);
}

void migrate_fd_cleanup(FdMigrationState *s)
{
    qemu_set_fd_handler2(s->fd, NULL, NULL, NULL, NULL);

    if (s->file) {
        dprintf("closing file\n");
        qemu_fclose(s->file);
        s->file = NULL;
    }

    if (s->fd != -1)
        close(s->fd);

    /* Don't resume monitor until we've flushed all of the buffers */
    if (s->mon) {
        monitor_resume(s->mon);
    }

    s->fd = -1;
}

void migrate_fd_put_notify(void *opaque)
{
    FdMigrationState *s = opaque;

    qemu_set_fd_handler2(s->fd, NULL, NULL, NULL, NULL);
    qemu_file_put_notify(s->file);
}

ssize_t migrate_fd_put_buffer(void *opaque, const void *data, size_t size)
{
    FdMigrationState *s = opaque;
    ssize_t ret;

    do {
        ret = s->write(s, data, size);
    } while (ret == -1 && ((s->get_error(s)) == EINTR));

    if (ret == -1)
        ret = -(s->get_error(s));

    if (ret == -EAGAIN)
        qemu_set_fd_handler2(s->fd, NULL, NULL, migrate_fd_put_notify, s);

    return ret;
}

void migrate_fd_connect(FdMigrationState *s)
{
    int ret;

    s->file = qemu_fopen_ops_buffered(s,
                                      s->bandwidth_limit,
                                      migrate_fd_put_buffer,
                                      migrate_fd_put_ready,
                                      migrate_fd_wait_for_unfreeze,
                                      migrate_fd_close);

    dprintf("beginning savevm\n");
    ret = qemu_savevm_state_begin(s->mon, s->file, s->mig_state.blk,
                                  s->mig_state.shared);
    if (ret < 0) {
        dprintf("failed, %d\n", ret);
        migrate_fd_error(s);
        return;
    }
    
    migrate_fd_put_ready(s);
}

void migrate_fd_put_ready(void *opaque)
{
    FdMigrationState *s = opaque;

    if (s->state != MIG_STATE_ACTIVE) {
        dprintf("put_ready returning because of non-active state\n");
        return;
    }

    dprintf("iterate\n");
    if (qemu_savevm_state_iterate(s->mon, s->file) == 1) {
        int state;
        int old_vm_running = vm_running;

        dprintf("done iterating\n");
        vm_stop(0);

        qemu_aio_flush();
        bdrv_flush_all();
        if ((qemu_savevm_state_complete(s->mon, s->file)) < 0) {
            if (old_vm_running) {
                vm_start();
            }
            state = MIG_STATE_ERROR;
        } else {
            state = MIG_STATE_COMPLETED;
        }
        migrate_fd_cleanup(s);
        s->state = state;
    }
}

int migrate_fd_get_status(MigrationState *mig_state)
{
    FdMigrationState *s = migrate_to_fms(mig_state);
    return s->state;
}

void migrate_fd_cancel(MigrationState *mig_state)
{
    FdMigrationState *s = migrate_to_fms(mig_state);

    if (s->state != MIG_STATE_ACTIVE)
        return;

    dprintf("cancelling migration\n");

    s->state = MIG_STATE_CANCELLED;
    qemu_savevm_state_cancel(s->mon, s->file);

    migrate_fd_cleanup(s);
}

void migrate_fd_release(MigrationState *mig_state)
{
    FdMigrationState *s = migrate_to_fms(mig_state);

    dprintf("releasing state\n");
   
    if (s->state == MIG_STATE_ACTIVE) {
        s->state = MIG_STATE_CANCELLED;
        migrate_fd_cleanup(s);
    }
    free(s);
}

void migrate_fd_wait_for_unfreeze(void *opaque)
{
    FdMigrationState *s = opaque;
    int ret;

    dprintf("wait for unfreeze\n");
    if (s->state != MIG_STATE_ACTIVE)
        return;

    do {
        fd_set wfds;

        FD_ZERO(&wfds);
        FD_SET(s->fd, &wfds);

        ret = select(s->fd + 1, NULL, &wfds, NULL, NULL);
    } while (ret == -1 && (s->get_error(s)) == EINTR);
}

int migrate_fd_close(void *opaque)
{
    FdMigrationState *s = opaque;

    qemu_set_fd_handler2(s->fd, NULL, NULL, NULL, NULL);
    return s->close(s);
}
