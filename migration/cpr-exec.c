/*
 * Copyright (c) 2021-2025 Oracle and/or its affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/memfd.h"
#include "qapi/error.h"
#include "io/channel-file.h"
#include "io/channel-socket.h"
#include "migration/cpr.h"
#include "migration/qemu-file.h"
#include "migration/misc.h"
#include "migration/vmstate.h"
#include "system/runstate.h"

#define CPR_EXEC_STATE_NAME "QEMU_CPR_EXEC_STATE"

static QEMUFile *qemu_file_new_fd_input(int fd, const char *name)
{
    g_autoptr(QIOChannelFile) fioc = qio_channel_file_new_fd(fd);
    QIOChannel *ioc = QIO_CHANNEL(fioc);
    qio_channel_set_name(ioc, name);
    return qemu_file_new_input(ioc);
}

static QEMUFile *qemu_file_new_fd_output(int fd, const char *name)
{
    g_autoptr(QIOChannelFile) fioc = qio_channel_file_new_fd(fd);
    QIOChannel *ioc = QIO_CHANNEL(fioc);
    qio_channel_set_name(ioc, name);
    return qemu_file_new_output(ioc);
}

void cpr_exec_persist_state(QEMUFile *f)
{
    QIOChannelFile *fioc = QIO_CHANNEL_FILE(qemu_file_get_ioc(f));
    int mfd = dup(fioc->fd);
    char val[16];

    /* Remember mfd in environment for post-exec load */
    qemu_clear_cloexec(mfd);
    snprintf(val, sizeof(val), "%d", mfd);
    g_setenv(CPR_EXEC_STATE_NAME, val, 1);
}

static int cpr_exec_find_state(void)
{
    const char *val = g_getenv(CPR_EXEC_STATE_NAME);
    int mfd;

    assert(val);
    g_unsetenv(CPR_EXEC_STATE_NAME);
    assert(!qemu_strtoi(val, NULL, 10, &mfd));
    return mfd;
}

bool cpr_exec_has_state(void)
{
    return g_getenv(CPR_EXEC_STATE_NAME) != NULL;
}

void cpr_exec_unpersist_state(void)
{
    int mfd;
    const char *val = g_getenv(CPR_EXEC_STATE_NAME);

    g_unsetenv(CPR_EXEC_STATE_NAME);
    assert(val);
    assert(!qemu_strtoi(val, NULL, 10, &mfd));
    close(mfd);
}

QEMUFile *cpr_exec_output(Error **errp)
{
    int mfd;

#ifdef CONFIG_LINUX
    mfd = qemu_memfd_create(CPR_EXEC_STATE_NAME, 0, false, 0, 0, errp);
#else
    mfd = -1;
#endif

    if (mfd < 0) {
        return NULL;
    }

    return qemu_file_new_fd_output(mfd, CPR_EXEC_STATE_NAME);
}

QEMUFile *cpr_exec_input(Error **errp)
{
    int mfd = cpr_exec_find_state();

    lseek(mfd, 0, SEEK_SET);
    return qemu_file_new_fd_input(mfd, CPR_EXEC_STATE_NAME);
}
