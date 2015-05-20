/*
 * COarse-grain LOck-stepping Virtual Machines for Non-stop Service (COLO)
 * (a.k.a. Fault Tolerance or Continuous Replication)
 *
 * Copyright (c) 2015 HUAWEI TECHNOLOGIES CO., LTD.
 * Copyright (c) 2015 FUJITSU LIMITED
 * Copyright (c) 2015 Intel Corporation
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include <unistd.h>
#include "sysemu/sysemu.h"
#include "migration/colo.h"
#include "trace.h"
#include "qemu/error-report.h"

bool colo_supported(void)
{
    return true;
}

bool migration_in_colo_state(void)
{
    MigrationState *s = migrate_get_current();

    return (s->state == MIGRATION_STATUS_COLO);
}

bool migration_incoming_in_colo_state(void)
{
    MigrationIncomingState *mis = migration_incoming_get_current();

    return mis && (mis->state == MIGRATION_STATUS_COLO);
}

static void colo_put_cmd(QEMUFile *f, COLOCommand cmd,
                         Error **errp)
{
    int ret;

    if (cmd >= COLO_COMMAND__MAX) {
        error_setg(errp, "%s: Invalid cmd", __func__);
        return;
    }
    qemu_put_be32(f, cmd);
    qemu_fflush(f);

    ret = qemu_file_get_error(f);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Can't put COLO command");
    }
    trace_colo_put_cmd(COLOCommand_lookup[cmd]);
}

static COLOCommand colo_get_cmd(QEMUFile *f, Error **errp)
{
    COLOCommand cmd;
    int ret;

    cmd = qemu_get_be32(f);
    ret = qemu_file_get_error(f);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Can't get COLO command");
        return cmd;
    }
    if (cmd >= COLO_COMMAND__MAX) {
        error_setg(errp, "%s: Invalid cmd", __func__);
        return cmd;
    }
    trace_colo_get_cmd(COLOCommand_lookup[cmd]);
    return cmd;
}

static void colo_get_check_cmd(QEMUFile *f, COLOCommand expect_cmd,
                               Error **errp)
{
    COLOCommand cmd;
    Error *local_err = NULL;

    cmd = colo_get_cmd(f, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    if (cmd != expect_cmd) {
        error_setg(errp, "Unexpected COLO command %d, expected %d",
                          expect_cmd, cmd);
    }
}

static int colo_do_checkpoint_transaction(MigrationState *s)
{
    Error *local_err = NULL;

    colo_put_cmd(s->to_dst_file, COLO_COMMAND_CHECKPOINT_REQUEST,
                 &local_err);
    if (local_err) {
        goto out;
    }

    colo_get_check_cmd(s->rp_state.from_dst_file,
                       COLO_COMMAND_CHECKPOINT_REPLY, &local_err);
    if (local_err) {
        goto out;
    }

    /* TODO: suspend and save vm state to colo buffer */

    colo_put_cmd(s->to_dst_file, COLO_COMMAND_VMSTATE_SEND, &local_err);
    if (local_err) {
        goto out;
    }

    /* TODO: send vmstate to Secondary */

    colo_get_check_cmd(s->rp_state.from_dst_file,
                       COLO_COMMAND_VMSTATE_RECEIVED, &local_err);
    if (local_err) {
        goto out;
    }

    colo_get_check_cmd(s->rp_state.from_dst_file,
                       COLO_COMMAND_VMSTATE_LOADED, &local_err);
    if (local_err) {
        goto out;
    }

    /* TODO: resume Primary */

    return 0;
out:
    if (local_err) {
        error_report_err(local_err);
    }
    return -EINVAL;
}

static void colo_process_checkpoint(MigrationState *s)
{
    Error *local_err = NULL;
    int ret;

    s->rp_state.from_dst_file = qemu_file_get_return_path(s->to_dst_file);
    if (!s->rp_state.from_dst_file) {
        error_report("Open QEMUFile from_dst_file failed");
        goto out;
    }

    /*
     * Wait for Secondary finish loading vm states and enter COLO
     * restore.
     */
    colo_get_check_cmd(s->rp_state.from_dst_file,
                       COLO_COMMAND_CHECKPOINT_READY, &local_err);
    if (local_err) {
        goto out;
    }

    qemu_mutex_lock_iothread();
    vm_start();
    qemu_mutex_unlock_iothread();
    trace_colo_vm_state_change("stop", "run");

    while (s->state == MIGRATION_STATUS_COLO) {
        /* start a colo checkpoint */
        ret = colo_do_checkpoint_transaction(s);
        if (ret < 0) {
            goto out;
        }
    }

out:
    /* Throw the unreported error message after exited from loop */
    if (local_err) {
        error_report_err(local_err);
    }
    migrate_set_state(&s->state, MIGRATION_STATUS_COLO,
                      MIGRATION_STATUS_COMPLETED);

    if (s->rp_state.from_dst_file) {
        qemu_fclose(s->rp_state.from_dst_file);
    }
}

void migrate_start_colo_process(MigrationState *s)
{
    qemu_mutex_unlock_iothread();
    migrate_set_state(&s->state, MIGRATION_STATUS_ACTIVE,
                      MIGRATION_STATUS_COLO);
    colo_process_checkpoint(s);
    qemu_mutex_lock_iothread();
}

static void colo_wait_handle_cmd(QEMUFile *f, int *checkpoint_request,
                                 Error **errp)
{
    COLOCommand cmd;
    Error *local_err = NULL;

    cmd = colo_get_cmd(f, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    switch (cmd) {
    case COLO_COMMAND_CHECKPOINT_REQUEST:
        *checkpoint_request = 1;
        break;
    default:
        *checkpoint_request = 0;
        error_setg(errp, "Got unknown COLO command: %d", cmd);
        break;
    }
}

void *colo_process_incoming_thread(void *opaque)
{
    MigrationIncomingState *mis = opaque;
    Error *local_err = NULL;

    migrate_set_state(&mis->state, MIGRATION_STATUS_ACTIVE,
                      MIGRATION_STATUS_COLO);

    mis->to_src_file = qemu_file_get_return_path(mis->from_src_file);
    if (!mis->to_src_file) {
        error_report("colo incoming thread: Open QEMUFile to_src_file failed");
        goto out;
    }
    /* Note: We set the fd to unblocked in migration incoming coroutine,
    *  But here we are in the colo incoming thread, so it is ok to set the
    *  fd back to blocked.
    */
    qemu_file_set_blocking(mis->from_src_file, true);

    colo_put_cmd(mis->to_src_file, COLO_COMMAND_CHECKPOINT_READY,
                 &local_err);
    if (local_err) {
        goto out;
    }

    while (mis->state == MIGRATION_STATUS_COLO) {
        int request;

        colo_wait_handle_cmd(mis->from_src_file, &request, &local_err);
        if (local_err) {
            goto out;
        }
        assert(request);
        /* FIXME: This is unnecessary for periodic checkpoint mode */
        colo_put_cmd(mis->to_src_file, COLO_COMMAND_CHECKPOINT_REPLY,
                     &local_err);
        if (local_err) {
            goto out;
        }

        colo_get_check_cmd(mis->from_src_file,
                           COLO_COMMAND_VMSTATE_SEND, &local_err);
        if (local_err) {
            goto out;
        }

        /* TODO: read migration data into colo buffer */

        colo_put_cmd(mis->to_src_file, COLO_COMMAND_VMSTATE_RECEIVED,
                     &local_err);
        if (local_err) {
            goto out;
        }

        /* TODO: load vm state */

        colo_put_cmd(mis->to_src_file, COLO_COMMAND_VMSTATE_LOADED,
                     &local_err);
        if (local_err) {
            goto out;
        }
    }

out:
    /* Throw the unreported error message after exited from loop */
    if (local_err) {
        error_report_err(local_err);
    }

    if (mis->to_src_file) {
        qemu_fclose(mis->to_src_file);
    }
    migration_incoming_exit_colo();

    return NULL;
}
