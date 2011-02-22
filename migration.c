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
#define DPRINTF(fmt, ...) \
    do { printf("migration: " fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

/* Migration speed throttling */
static int64_t max_throttle = (32 << 20);

static MigrationState *current_migration;

static NotifierList migration_state_notifiers =
    NOTIFIER_LIST_INITIALIZER(migration_state_notifiers);

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

    if (autostart) {
        vm_start();
    } else {
        runstate_set(RUN_STATE_PRELAUNCH);
    }
}

static MigrationState *migrate_new(Monitor *mon, int64_t bandwidth_limit,
                                   int detach, int blk, int inc);

int do_migrate(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    MigrationState *s = NULL;
    const char *p;
    int detach = qdict_get_try_bool(qdict, "detach", 0);
    int blk = qdict_get_try_bool(qdict, "blk", 0);
    int inc = qdict_get_try_bool(qdict, "inc", 0);
    const char *uri = qdict_get_str(qdict, "uri");
    int ret;

    if (current_migration &&
        current_migration->get_status(current_migration) == MIG_STATE_ACTIVE) {
        monitor_printf(mon, "migration already in progress\n");
        return -1;
    }

    if (qemu_savevm_state_blocked(mon)) {
        return -1;
    }

    s = migrate_new(mon, max_throttle, detach, blk, inc);

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
        monitor_printf(mon, "unknown migration protocol: %s\n", uri);
        ret  = -EINVAL;
        goto free_migrate_state;
    }

    if (ret < 0) {
        monitor_printf(mon, "migration failed\n");
        goto free_migrate_state;
    }

    if (current_migration) {
        current_migration->release(current_migration);
    }

    current_migration = s;
    notifier_list_notify(&migration_state_notifiers, NULL);
    return 0;
free_migrate_state:
    g_free(s);
    return -1;
}

int do_migrate_cancel(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    MigrationState *s = current_migration;

    if (s && s->get_status(s) == MIG_STATE_ACTIVE) {
        s->cancel(s);
    }
    return 0;
}

int do_migrate_set_speed(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    int64_t d;
    MigrationState *s;

    d = qdict_get_int(qdict, "value");
    if (d < 0) {
        d = 0;
    }
    max_throttle = d;

    s = current_migration;
    if (s && s->file) {
        qemu_file_set_rate_limit(s->file, max_throttle);
    }

    return 0;
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

int do_migrate_set_downtime(Monitor *mon, const QDict *qdict,
                            QObject **ret_data)
{
    double d;

    d = qdict_get_double(qdict, "value") * 1e9;
    d = MAX(0, MIN(UINT64_MAX, d));
    max_downtime = (uint64_t)d;

    return 0;
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
    qdict_put_obj(qdict, name, obj);
}

void do_info_migrate(Monitor *mon, QObject **ret_data)
{
    QDict *qdict;

    if (current_migration) {
        MigrationState *s = current_migration;

        switch (s->get_status(current_migration)) {
        case MIG_STATE_SETUP:
            /* no migration has happened ever */
            break;
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
    }
}

/* shared migration helpers */

static void migrate_fd_monitor_suspend(MigrationState *s, Monitor *mon)
{
    s->mon = mon;
    if (monitor_suspend(mon) == 0) {
        DPRINTF("suspending monitor\n");
    } else {
        monitor_printf(mon, "terminal does not allow synchronous "
                       "migration, continuing detached\n");
    }
}

static int migrate_fd_cleanup(MigrationState *s)
{
    int ret = 0;

    qemu_set_fd_handler2(s->fd, NULL, NULL, NULL, NULL);

    if (s->file) {
        DPRINTF("closing file\n");
        if (qemu_fclose(s->file) != 0) {
            ret = -1;
        }
        s->file = NULL;
    } else {
        if (s->mon) {
            monitor_resume(s->mon);
        }
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
    notifier_list_notify(&migration_state_notifiers, NULL);
    migrate_fd_cleanup(s);
}

static void migrate_fd_put_notify(void *opaque)
{
    MigrationState *s = opaque;

    qemu_set_fd_handler2(s->fd, NULL, NULL, NULL, NULL);
    qemu_file_put_notify(s->file);
    if (qemu_file_get_error(s->file)) {
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
    ret = qemu_savevm_state_iterate(s->mon, s->file);
    if (ret < 0) {
        migrate_fd_error(s);
    } else if (ret == 1) {
        int old_vm_running = runstate_is_running();

        DPRINTF("done iterating\n");
        vm_stop(RUN_STATE_FINISH_MIGRATE);

        if (qemu_savevm_state_complete(s->mon, s->file) < 0) {
            migrate_fd_error(s);
        } else {
            if (migrate_fd_cleanup(s) < 0) {
                migrate_fd_error(s);
            } else {
                s->state = MIG_STATE_COMPLETED;
                runstate_set(RUN_STATE_POSTMIGRATE);
                notifier_list_notify(&migration_state_notifiers, NULL);
            }
        }
        if (s->get_status(s) != MIG_STATE_COMPLETED) {
            if (old_vm_running) {
                vm_start();
            }
        }
    }
}

static int migrate_fd_get_status(MigrationState *s)
{
    return s->state;
}

static void migrate_fd_cancel(MigrationState *s)
{
    if (s->state != MIG_STATE_ACTIVE)
        return;

    DPRINTF("cancelling migration\n");

    s->state = MIG_STATE_CANCELLED;
    notifier_list_notify(&migration_state_notifiers, NULL);
    qemu_savevm_state_cancel(s->mon, s->file);

    migrate_fd_cleanup(s);
}

static void migrate_fd_release(MigrationState *s)
{

    DPRINTF("releasing state\n");
   
    if (s->state == MIG_STATE_ACTIVE) {
        s->state = MIG_STATE_CANCELLED;
        notifier_list_notify(&migration_state_notifiers, NULL);
        migrate_fd_cleanup(s);
    }
    g_free(s);
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

    if (s->mon) {
        monitor_resume(s->mon);
    }
    qemu_set_fd_handler2(s->fd, NULL, NULL, NULL, NULL);
    return s->close(s);
}

void add_migration_state_change_notifier(Notifier *notify)
{
    notifier_list_add(&migration_state_notifiers, notify);
}

void remove_migration_state_change_notifier(Notifier *notify)
{
    notifier_list_remove(&migration_state_notifiers, notify);
}

int get_migration_state(void)
{
    if (current_migration) {
        return migrate_fd_get_status(current_migration);
    } else {
        return MIG_STATE_ERROR;
    }
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
    ret = qemu_savevm_state_begin(s->mon, s->file, s->blk, s->shared);
    if (ret < 0) {
        DPRINTF("failed, %d\n", ret);
        migrate_fd_error(s);
        return;
    }
    migrate_fd_put_ready(s);
}

static MigrationState *migrate_new(Monitor *mon, int64_t bandwidth_limit,
                                   int detach, int blk, int inc)
{
    MigrationState *s = g_malloc0(sizeof(*s));

    s->cancel = migrate_fd_cancel;
    s->get_status = migrate_fd_get_status;
    s->release = migrate_fd_release;
    s->blk = blk;
    s->shared = inc;
    s->mon = NULL;
    s->bandwidth_limit = bandwidth_limit;
    s->state = MIG_STATE_SETUP;

    if (!detach) {
        migrate_fd_monitor_suspend(s, mon);
    }

    return s;
}
