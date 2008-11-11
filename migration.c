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
#include "console.h"
#include "buffered_file.h"
#include "sysemu.h"
#include "block.h"
#include "qemu_socket.h"

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
#endif
    else
        fprintf(stderr, "unknown migration protocol: %s\n", uri);
}

void do_migrate(int detach, const char *uri)
{
    MigrationState *s = NULL;
    const char *p;

    if (strstart(uri, "tcp:", &p))
        s = tcp_start_outgoing_migration(p, max_throttle, detach);
#if !defined(WIN32)
    else if (strstart(uri, "exec:", &p))
        s = exec_start_outgoing_migration(p, max_throttle, detach);
#endif
    else
        term_printf("unknown migration protocol: %s\n", uri);

    if (s == NULL)
        term_printf("migration failed\n");
    else {
        if (current_migration)
            current_migration->release(current_migration);

        current_migration = s;
    }
}

void do_migrate_cancel(void)
{
    MigrationState *s = current_migration;

    if (s)
        s->cancel(s);
}

void do_migrate_set_speed(const char *value)
{
    double d;
    char *ptr;

    d = strtod(value, &ptr);
    switch (*ptr) {
    case 'G': case 'g':
        d *= 1024;
    case 'M': case 'm':
        d *= 1024;
    case 'K': case 'k':
        d *= 1024;
    default:
        break;
    }

    max_throttle = (uint32_t)d;
}

void do_info_migrate(void)
{
    MigrationState *s = current_migration;
    
    if (s) {
        term_printf("Migration status: ");
        switch (s->get_status(s)) {
        case MIG_STATE_ACTIVE:
            term_printf("active\n");
            break;
        case MIG_STATE_COMPLETED:
            term_printf("completed\n");
            break;
        case MIG_STATE_ERROR:
            term_printf("failed\n");
            break;
        case MIG_STATE_CANCELLED:
            term_printf("cancelled\n");
            break;
        }
    }
}

/* shared migration helpers */

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
    }

    if (s->fd != -1)
        close(s->fd);

    /* Don't resume monitor until we've flushed all of the buffers */
    if (s->detach == 2) {
        monitor_resume();
        s->detach = 0;
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
    } while (ret == -1 && ((s->get_error(s)) == EINTR || (s->get_error(s)) == EWOULDBLOCK));

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
    ret = qemu_savevm_state_begin(s->file);
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
    if (qemu_savevm_state_iterate(s->file) == 1) {
        dprintf("done iterating\n");
        vm_stop(0);

        bdrv_flush_all();
        qemu_savevm_state_complete(s->file);
        s->state = MIG_STATE_COMPLETED;
        migrate_fd_cleanup(s);
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
    return s->close(s);
}
