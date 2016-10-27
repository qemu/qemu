/*
 * COarse-grain LOck-stepping Virtual Machines for Non-stop Service (COLO)
 * (a.k.a. Fault Tolerance or Continuous Replication)
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 * Copyright (c) 2016 FUJITSU LIMITED
 * Copyright (c) 2016 Intel Corporation
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "sysemu/sysemu.h"
#include "migration/colo.h"
#include "trace.h"
#include "qemu/error-report.h"
#include "qapi/error.h"

bool colo_supported(void)
{
    return false;
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

static void colo_send_message(QEMUFile *f, COLOMessage msg,
                              Error **errp)
{
    int ret;

    if (msg >= COLO_MESSAGE__MAX) {
        error_setg(errp, "%s: Invalid message", __func__);
        return;
    }
    qemu_put_be32(f, msg);
    qemu_fflush(f);

    ret = qemu_file_get_error(f);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Can't send COLO message");
    }
    trace_colo_send_message(COLOMessage_lookup[msg]);
}

static COLOMessage colo_receive_message(QEMUFile *f, Error **errp)
{
    COLOMessage msg;
    int ret;

    msg = qemu_get_be32(f);
    ret = qemu_file_get_error(f);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Can't receive COLO message");
        return msg;
    }
    if (msg >= COLO_MESSAGE__MAX) {
        error_setg(errp, "%s: Invalid message", __func__);
        return msg;
    }
    trace_colo_receive_message(COLOMessage_lookup[msg]);
    return msg;
}

static void colo_receive_check_message(QEMUFile *f, COLOMessage expect_msg,
                                       Error **errp)
{
    COLOMessage msg;
    Error *local_err = NULL;

    msg = colo_receive_message(f, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    if (msg != expect_msg) {
        error_setg(errp, "Unexpected COLO message %d, expected %d",
                          msg, expect_msg);
    }
}

static int colo_do_checkpoint_transaction(MigrationState *s)
{
    Error *local_err = NULL;

    colo_send_message(s->to_dst_file, COLO_MESSAGE_CHECKPOINT_REQUEST,
                      &local_err);
    if (local_err) {
        goto out;
    }

    colo_receive_check_message(s->rp_state.from_dst_file,
                    COLO_MESSAGE_CHECKPOINT_REPLY, &local_err);
    if (local_err) {
        goto out;
    }

    /* TODO: suspend and save vm state to colo buffer */

    colo_send_message(s->to_dst_file, COLO_MESSAGE_VMSTATE_SEND, &local_err);
    if (local_err) {
        goto out;
    }

    /* TODO: send vmstate to Secondary */

    colo_receive_check_message(s->rp_state.from_dst_file,
                       COLO_MESSAGE_VMSTATE_RECEIVED, &local_err);
    if (local_err) {
        goto out;
    }

    colo_receive_check_message(s->rp_state.from_dst_file,
                       COLO_MESSAGE_VMSTATE_LOADED, &local_err);
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
     * Wait for Secondary finish loading VM states and enter COLO
     * restore.
     */
    colo_receive_check_message(s->rp_state.from_dst_file,
                       COLO_MESSAGE_CHECKPOINT_READY, &local_err);
    if (local_err) {
        goto out;
    }

    qemu_mutex_lock_iothread();
    vm_start();
    qemu_mutex_unlock_iothread();
    trace_colo_vm_state_change("stop", "run");

    while (s->state == MIGRATION_STATUS_COLO) {
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

static void colo_wait_handle_message(QEMUFile *f, int *checkpoint_request,
                                     Error **errp)
{
    COLOMessage msg;
    Error *local_err = NULL;

    msg = colo_receive_message(f, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    switch (msg) {
    case COLO_MESSAGE_CHECKPOINT_REQUEST:
        *checkpoint_request = 1;
        break;
    default:
        *checkpoint_request = 0;
        error_setg(errp, "Got unknown COLO message: %d", msg);
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
        error_report("COLO incoming thread: Open QEMUFile to_src_file failed");
        goto out;
    }
    /*
     * Note: the communication between Primary side and Secondary side
     * should be sequential, we set the fd to unblocked in migration incoming
     * coroutine, and here we are in the COLO incoming thread, so it is ok to
     * set the fd back to blocked.
     */
    qemu_file_set_blocking(mis->from_src_file, true);

    colo_send_message(mis->to_src_file, COLO_MESSAGE_CHECKPOINT_READY,
                      &local_err);
    if (local_err) {
        goto out;
    }

    while (mis->state == MIGRATION_STATUS_COLO) {
        int request;

        colo_wait_handle_message(mis->from_src_file, &request, &local_err);
        if (local_err) {
            goto out;
        }
        assert(request);
        /* FIXME: This is unnecessary for periodic checkpoint mode */
        colo_send_message(mis->to_src_file, COLO_MESSAGE_CHECKPOINT_REPLY,
                     &local_err);
        if (local_err) {
            goto out;
        }

        colo_receive_check_message(mis->from_src_file,
                           COLO_MESSAGE_VMSTATE_SEND, &local_err);
        if (local_err) {
            goto out;
        }

        /* TODO: read migration data into colo buffer */

        colo_send_message(mis->to_src_file, COLO_MESSAGE_VMSTATE_RECEIVED,
                     &local_err);
        if (local_err) {
            goto out;
        }

        /* TODO: load vm state */

        colo_send_message(mis->to_src_file, COLO_MESSAGE_VMSTATE_LOADED,
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
