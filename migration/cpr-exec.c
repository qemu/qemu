/*
 * Copyright (c) 2021-2025 Oracle and/or its affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qemu/memfd.h"
#include "qapi/error.h"
#include "qapi/type-helpers.h"
#include "io/channel-file.h"
#include "io/channel-socket.h"
#include "block/block-global-state.h"
#include "qemu/main-loop.h"
#include "migration/cpr.h"
#include "migration/qemu-file.h"
#include "migration/migration.h"
#include "migration/misc.h"
#include "migration/vmstate.h"
#include "system/runstate.h"
#include "trace.h"

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

bool cpr_exec_persist_state(QEMUFile *f, Error **errp)
{
    QIOChannelFile *fioc = QIO_CHANNEL_FILE(qemu_file_get_ioc(f));
    /* coverity[leaked_storage] - mfd intentionally kept open across exec() */
    int mfd = dup(fioc->fd);
    char val[16];

    /* Remember mfd in environment for post-exec load */
    qemu_clear_cloexec(mfd);
    snprintf(val, sizeof(val), "%d", mfd);
    if (!g_setenv(CPR_EXEC_STATE_NAME, val, 1)) {
        error_setg(errp, "Setting env %s = %s failed", CPR_EXEC_STATE_NAME, val);
        return false;
    }

    return true;
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

static bool preserve_fd(int fd)
{
    qemu_clear_cloexec(fd);
    return true;
}

static bool unpreserve_fd(int fd)
{
    qemu_set_cloexec(fd);
    return true;
}

static void cpr_exec_preserve_fds(void)
{
    cpr_walk_fd(preserve_fd);
}

void cpr_exec_unpreserve_fds(void)
{
    cpr_walk_fd(unpreserve_fd);
}

static void cpr_exec_cb(void *opaque)
{
    MigrationState *s = migrate_get_current();
    char **argv = strv_from_str_list(s->parameters.cpr_exec_command);
    Error *err = NULL;

    /*
     * Clear the close-on-exec flag for all preserved fd's.  We cannot do so
     * earlier because they should not persist across miscellaneous fork and
     * exec calls that are performed during normal operation.
     */
    cpr_exec_preserve_fds();

    trace_cpr_exec();
    execvp(argv[0], argv);

    /*
     * exec should only fail if argv[0] is bogus, or has a permissions problem,
     * or the system is very short on resources.
     */
    error_setg_errno(&err, errno, "execvp %s failed", argv[0]);
    g_clear_pointer(&argv, g_strfreev);
    cpr_exec_unpreserve_fds();

    error_report_err(error_copy(err));
    migrate_set_state(&s->state, s->state, MIGRATION_STATUS_FAILED);
    migrate_set_error(s, err);
    error_free(err);
    err = NULL;

    /* Note, we can go from state COMPLETED to FAILED */
    migration_call_notifiers(s, MIG_EVENT_PRECOPY_FAILED, NULL);

    if (!migration_block_activate(&err)) {
        /* error was already reported */
        error_free(err);
        return;
    }

    if (runstate_is_live(s->vm_old_state)) {
        vm_start();
    }
}

static int cpr_exec_notifier(NotifierWithReturn *notifier, MigrationEvent *e,
                             Error **errp)
{
    MigrationState *s = migrate_get_current();

    if (e->type == MIG_EVENT_PRECOPY_DONE) {
        QEMUBH *cpr_exec_bh = qemu_bh_new(cpr_exec_cb, NULL);
        assert(s->state == MIGRATION_STATUS_COMPLETED);
        qemu_bh_schedule(cpr_exec_bh);
        qemu_notify_event();
    } else if (e->type == MIG_EVENT_PRECOPY_FAILED) {
        cpr_exec_unpersist_state();
    }
    return 0;
}

void cpr_exec_init(void)
{
    static NotifierWithReturn exec_notifier;

    migration_add_notifier_mode(&exec_notifier, cpr_exec_notifier,
                                MIG_MODE_CPR_EXEC);
}
