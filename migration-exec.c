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
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu-common.h"
#include "qemu_socket.h"
#include "migration.h"
#include "qemu-char.h"
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

static int file_errno(MigrationState *s)
{
    return errno;
}

static int file_write(MigrationState *s, const void * buf, size_t size)
{
    return write(s->fd, buf, size);
}

static int exec_close(MigrationState *s)
{
    int ret = 0;
    DPRINTF("exec_close\n");
    if (s->opaque) {
        ret = qemu_fclose(s->opaque);
        s->opaque = NULL;
        s->fd = -1;
        if (ret >= 0 && !(WIFEXITED(ret) && WEXITSTATUS(ret) == 0)) {
            /* close succeeded, but non-zero exit code: */
            ret = -EIO; /* fake errno value */
        }
    }
    return ret;
}

int exec_start_outgoing_migration(MigrationState *s, const char *command)
{
    FILE *f;

    f = popen(command, "w");
    if (f == NULL) {
        DPRINTF("Unable to popen exec target\n");
        goto err_after_popen;
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

    migrate_fd_connect(s);
    return 0;

err_after_open:
    pclose(f);
err_after_popen:
    return -1;
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
