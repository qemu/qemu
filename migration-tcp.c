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
#include "qemu_socket.h"
#include "migration.h"
#include "qemu-char.h"
#include "sysemu.h"
#include "console.h"
#include "buffered_file.h"
#include "block.h"

//#define DEBUG_MIGRATION_TCP

typedef struct FdMigrationState
{
    MigrationState mig_state;
    QEMUFile *file;
    int64_t bandwidth_limit;
    int fd;
    int detach;
    int state;
} FdMigrationState;

#ifdef DEBUG_MIGRATION_TCP
#define dprintf(fmt, ...) \
    do { printf("migration-tcp: " fmt, ## __VA_ARGS__); } while (0)
#else
#define dprintf(fmt, ...) \
    do { } while (0)
#endif

int debug_me = 0;

static void tcp_cleanup(FdMigrationState *s)
{
    if (s->detach == 2) {
	monitor_resume();
	s->detach = 0;
    }

    qemu_set_fd_handler2(s->fd, NULL, NULL, NULL, NULL);

    if (s->file) {
        debug_me = 1;
        dprintf("closing file\n");
	qemu_fclose(s->file);
    }

    if (s->fd != -1)
	close(s->fd);

    s->fd = -1;
}

static void tcp_error(FdMigrationState *s)
{
    dprintf("setting error state\n");
    s->state = MIG_STATE_ERROR;
    tcp_cleanup(s);
}

static void fd_put_notify(void *opaque)
{
    FdMigrationState *s = opaque;

    qemu_set_fd_handler2(s->fd, NULL, NULL, NULL, NULL);
    qemu_file_put_notify(s->file);
}

static ssize_t fd_put_buffer(void *opaque, const void *data, size_t size)
{
    FdMigrationState *s = opaque;
    ssize_t ret;

    do {
        ret = send(s->fd, data, size, 0);
    } while (ret == -1 && errno == EINTR);

    if (ret == -1)
        ret = -errno;

    if (ret == -EAGAIN)
        qemu_set_fd_handler2(s->fd, NULL, NULL, fd_put_notify, s);

    return ret;
}

static int fd_close(void *opaque)
{
    FdMigrationState *s = opaque;
    dprintf("fd_close\n");
    if (s->fd != -1) {
	close(s->fd);
	s->fd = -1;
    }
    return 0;
}

static void fd_wait_for_unfreeze(void *opaque)
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
    } while (ret == -1 && errno == EINTR);
}

static void fd_put_ready(void *opaque)
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
	tcp_cleanup(s);
    }
}

static void tcp_connect_migrate(FdMigrationState *s)
{
    int ret;

    s->file = qemu_fopen_ops_buffered(s,
                                      s->bandwidth_limit,
                                      fd_put_buffer,
                                      fd_put_ready,
                                      fd_wait_for_unfreeze,
                                      fd_close);

    dprintf("beginning savevm\n");
    ret = qemu_savevm_state_begin(s->file);
    if (ret < 0) {
        dprintf("failed, %d\n", ret);
	tcp_error(s);
        return;
    }

    fd_put_ready(s);
}

static void tcp_wait_for_connect(void *opaque)
{
    FdMigrationState *s = opaque;
    int val, ret;
    int valsize = sizeof(val);

    dprintf("connect completed\n");
    do {
        ret = getsockopt(s->fd, SOL_SOCKET, SO_ERROR, &val, &valsize);
    } while (ret == -1 && errno == EINTR);

    if (ret < 0) {
	tcp_error(s);
        return;
    }

    qemu_set_fd_handler2(s->fd, NULL, NULL, NULL, NULL);

    if (val == 0)
        tcp_connect_migrate(s);
    else {
        dprintf("error connecting %d\n", val);
	tcp_error(s);
    }
}

static FdMigrationState *to_fms(MigrationState *mig_state)
{
    return container_of(mig_state, FdMigrationState, mig_state);
}

static int tcp_get_status(MigrationState *mig_state)
{
    FdMigrationState *s = to_fms(mig_state);

    return s->state;
}

static void tcp_cancel(MigrationState *mig_state)
{
    FdMigrationState *s = to_fms(mig_state);

    if (s->state != MIG_STATE_ACTIVE)
	return;

    dprintf("cancelling migration\n");

    s->state = MIG_STATE_CANCELLED;

    tcp_cleanup(s);
}

static void tcp_release(MigrationState *mig_state)
{
    FdMigrationState *s = to_fms(mig_state);

    dprintf("releasing state\n");
   
    if (s->state == MIG_STATE_ACTIVE) {
	s->state = MIG_STATE_CANCELLED;
	tcp_cleanup(s);
    }
    free(s);
}

MigrationState *tcp_start_outgoing_migration(const char *host_port,
					     int64_t bandwidth_limit,
					     int async)
{
    struct sockaddr_in addr;
    FdMigrationState *s;
    int ret;

    if (parse_host_port(&addr, host_port) < 0)
        return NULL;

    s = qemu_mallocz(sizeof(*s));
    if (s == NULL)
        return NULL;

    s->mig_state.cancel = tcp_cancel;
    s->mig_state.get_status = tcp_get_status;
    s->mig_state.release = tcp_release;

    s->state = MIG_STATE_ACTIVE;
    s->detach = !async;
    s->bandwidth_limit = bandwidth_limit;
    s->fd = socket(PF_INET, SOCK_STREAM, 0);
    if (s->fd == -1) {
        qemu_free(s);
	return NULL;
    }

    socket_set_nonblock(s->fd);

    if (s->detach == 1) {
        dprintf("detaching from monitor\n");
        monitor_suspend();
	s->detach = 2;
    }

    do {
        ret = connect(s->fd, (struct sockaddr *)&addr, sizeof(addr));
        if (ret == -1)
            ret = -errno;

        if (ret == -EINPROGRESS)
            qemu_set_fd_handler2(s->fd, NULL, NULL, tcp_wait_for_connect, s);
    } while (ret == -EINTR);

    if (ret < 0 && ret != -EINPROGRESS) {
        dprintf("connect failed\n");
        close(s->fd);
        qemu_free(s);
	s = NULL;
    } else if (ret >= 0)
        tcp_connect_migrate(s);

    return &s->mig_state;
}

static void tcp_accept_incoming_migration(void *opaque)
{
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    int s = (unsigned long)opaque;
    QEMUFile *f;
    int c, ret;

    do {
        c = accept(s, (struct sockaddr *)&addr, &addrlen);
    } while (c == -1 && errno == EINTR);

    dprintf("accepted migration\n");

    if (c == -1) {
        fprintf(stderr, "could not accept migration connection\n");
        return;
    }

    f = qemu_fopen_fd(c);
    if (f == NULL) {
        fprintf(stderr, "could not qemu_fopen socket\n");
        goto out;
    }

    vm_stop(0); /* just in case */
    ret = qemu_loadvm_state(f);
    if (ret < 0) {
        fprintf(stderr, "load of migration failed\n");
        goto out_fopen;
    }
    qemu_announce_self();
    dprintf("successfully loaded vm state\n");

    /* we've successfully migrated, close the server socket */
    qemu_set_fd_handler2(s, NULL, NULL, NULL, NULL);
    close(s);

    vm_start();

out_fopen:
    qemu_fclose(f);
out:
    close(c);
}

int tcp_start_incoming_migration(const char *host_port)
{
    struct sockaddr_in addr;
    int val;
    int s;

    if (parse_host_port(&addr, host_port) < 0) {
        fprintf(stderr, "invalid host/port combination: %s\n", host_port);
        return -EINVAL;
    }

    s = socket(PF_INET, SOCK_STREAM, 0);
    if (s == -1)
        return -errno;

    val = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&val, sizeof(val));

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == -1)
        goto err;

    if (listen(s, 1) == -1)
        goto err;

    qemu_set_fd_handler2(s, NULL, tcp_accept_incoming_migration, NULL,
                         (void *)(unsigned long)s);

    return 0;

err:
    close(s);
    return -errno;
}
