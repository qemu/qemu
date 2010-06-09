/*
 * QEMU live migration
 *
 * Copyright IBM, Corp. 2008
 * Copyright Dell MessageOne 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Charles Duffy     <charles_duffy@messageone.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "qemu-common.h"
#include "qemu_socket.h"
#include "migration.h"
#include "qemu-char.h"
#include "sysemu.h"
#include "buffered_file.h"
#include "block.h"
#include <sys/types.h>
#include <sys/wait.h>

//#define DEBUG_MIGRATION_EXEC

#ifdef DEBUG_MIGRATION_EXEC
#define DPRINTF(fmt, ...) \
    do { printf("migration-exec: " fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

static int file_errno(FdMigrationState *s)
{
    return errno;
}

static int file_write(FdMigrationState *s, const void * buf, size_t size)
{
    return write(s->fd, buf, size);
}

static int exec_close(FdMigrationState *s)
{
    int ret = 0;
    DPRINTF("exec_close\n");
    if (s->opaque) {
        ret = qemu_fclose(s->opaque);
        s->opaque = NULL;
        s->fd = -1;
        if (ret != -1 &&
            WIFEXITED(ret)
            && WEXITSTATUS(ret) == 0) {
            ret = 0;
        } else {
            ret = -1;
        }
    }
    return ret;
}

MigrationState *exec_start_outgoing_migration(Monitor *mon,
                                              const char *command,
					      int64_t bandwidth_limit,
					      int detach,
					      int blk,
					      int inc)
{
    FdMigrationState *s;
    FILE *f;

    s = qemu_mallocz(sizeof(*s));

    f = popen(command, "w");
    if (f == NULL) {
        DPRINTF("Unable to popen exec target\n");
        goto err_after_alloc;
    }

    s->fd = fileno(f);
    if (s->fd == -1) {
        DPRINTF("Unable to retrieve file descriptor for popen'd handle\n");
        goto err_after_open;
    }

    socket_set_nonblock(s->fd);

    s->opaque = qemu_popen(f, "w");

    s->close = exec_close;
    s->get_error = file_errno;
    s->write = file_write;
    s->mig_state.cancel = migrate_fd_cancel;
    s->mig_state.get_status = migrate_fd_get_status;
    s->mig_state.release = migrate_fd_release;

    s->mig_state.blk = blk;
    s->mig_state.shared = inc;

    s->state = MIG_STATE_ACTIVE;
    s->mon = NULL;
    s->bandwidth_limit = bandwidth_limit;

    if (!detach) {
        migrate_fd_monitor_suspend(s, mon);
    }

    migrate_fd_connect(s);
    return &s->mig_state;

err_after_open:
    pclose(f);
err_after_alloc:
    qemu_free(s);
    return NULL;
}

static void exec_accept_incoming_migration(void *opaque)
{
    QEMUFile *f = opaque;

    process_incoming_migration(f);
    qemu_set_fd_handler2(qemu_stdio_fd(f), NULL, NULL, NULL, NULL);
    qemu_fclose(f);
}

int exec_start_incoming_migration(const char *command)
{
    QEMUFile *f;

    DPRINTF("Attempting to start an incoming migration\n");
    f = qemu_popen_cmd(command, "r");
    if(f == NULL) {
        DPRINTF("Unable to apply qemu wrapper to popen file\n");
        return -errno;
    }

    qemu_set_fd_handler2(qemu_stdio_fd(f), NULL,
			 exec_accept_incoming_migration, NULL, f);

    return 0;
}
