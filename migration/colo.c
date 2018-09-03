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
#include "qapi/error.h"
#include "qapi/qapi-commands-migration.h"
#include "qemu-file-channel.h"
#include "migration.h"
#include "qemu-file.h"
#include "savevm.h"
#include "migration/colo.h"
#include "block.h"
#include "io/channel-buffer.h"
#include "trace.h"
#include "qemu/error-report.h"
#include "migration/failover.h"
#include "replication.h"
#include "net/colo-compare.h"
#include "net/colo.h"

static bool vmstate_loading;
static Notifier packets_compare_notifier;

#define COLO_BUFFER_BASE_SIZE (4 * 1024 * 1024)

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

static bool colo_runstate_is_stopped(void)
{
    return runstate_check(RUN_STATE_COLO) || !runstate_is_running();
}

static void secondary_vm_do_failover(void)
{
    int old_state;
    MigrationIncomingState *mis = migration_incoming_get_current();

    /* Can not do failover during the process of VM's loading VMstate, Or
     * it will break the secondary VM.
     */
    if (vmstate_loading) {
        old_state = failover_set_state(FAILOVER_STATUS_ACTIVE,
                        FAILOVER_STATUS_RELAUNCH);
        if (old_state != FAILOVER_STATUS_ACTIVE) {
            error_report("Unknown error while do failover for secondary VM,"
                         "old_state: %s", FailoverStatus_str(old_state));
        }
        return;
    }

    migrate_set_state(&mis->state, MIGRATION_STATUS_COLO,
                      MIGRATION_STATUS_COMPLETED);

    if (!autostart) {
        error_report("\"-S\" qemu option will be ignored in secondary side");
        /* recover runstate to normal migration finish state */
        autostart = true;
    }
    /*
     * Make sure COLO incoming thread not block in recv or send,
     * If mis->from_src_file and mis->to_src_file use the same fd,
     * The second shutdown() will return -1, we ignore this value,
     * It is harmless.
     */
    if (mis->from_src_file) {
        qemu_file_shutdown(mis->from_src_file);
    }
    if (mis->to_src_file) {
        qemu_file_shutdown(mis->to_src_file);
    }

    old_state = failover_set_state(FAILOVER_STATUS_ACTIVE,
                                   FAILOVER_STATUS_COMPLETED);
    if (old_state != FAILOVER_STATUS_ACTIVE) {
        error_report("Incorrect state (%s) while doing failover for "
                     "secondary VM", FailoverStatus_str(old_state));
        return;
    }
    /* Notify COLO incoming thread that failover work is finished */
    qemu_sem_post(&mis->colo_incoming_sem);
    /* For Secondary VM, jump to incoming co */
    if (mis->migration_incoming_co) {
        qemu_coroutine_enter(mis->migration_incoming_co);
    }
}

static void primary_vm_do_failover(void)
{
    MigrationState *s = migrate_get_current();
    int old_state;

    migrate_set_state(&s->state, MIGRATION_STATUS_COLO,
                      MIGRATION_STATUS_COMPLETED);

    /*
     * Wake up COLO thread which may blocked in recv() or send(),
     * The s->rp_state.from_dst_file and s->to_dst_file may use the
     * same fd, but we still shutdown the fd for twice, it is harmless.
     */
    if (s->to_dst_file) {
        qemu_file_shutdown(s->to_dst_file);
    }
    if (s->rp_state.from_dst_file) {
        qemu_file_shutdown(s->rp_state.from_dst_file);
    }

    old_state = failover_set_state(FAILOVER_STATUS_ACTIVE,
                                   FAILOVER_STATUS_COMPLETED);
    if (old_state != FAILOVER_STATUS_ACTIVE) {
        error_report("Incorrect state (%s) while doing failover for Primary VM",
                     FailoverStatus_str(old_state));
        return;
    }
    /* Notify COLO thread that failover work is finished */
    qemu_sem_post(&s->colo_exit_sem);
}

void colo_do_failover(MigrationState *s)
{
    /* Make sure VM stopped while failover happened. */
    if (!colo_runstate_is_stopped()) {
        vm_stop_force_state(RUN_STATE_COLO);
    }

    if (get_colo_mode() == COLO_MODE_PRIMARY) {
        primary_vm_do_failover();
    } else {
        secondary_vm_do_failover();
    }
}

void qmp_xen_set_replication(bool enable, bool primary,
                             bool has_failover, bool failover,
                             Error **errp)
{
#ifdef CONFIG_REPLICATION
    ReplicationMode mode = primary ?
                           REPLICATION_MODE_PRIMARY :
                           REPLICATION_MODE_SECONDARY;

    if (has_failover && enable) {
        error_setg(errp, "Parameter 'failover' is only for"
                   " stopping replication");
        return;
    }

    if (enable) {
        replication_start_all(mode, errp);
    } else {
        if (!has_failover) {
            failover = NULL;
        }
        replication_stop_all(failover, failover ? NULL : errp);
    }
#else
    abort();
#endif
}

ReplicationStatus *qmp_query_xen_replication_status(Error **errp)
{
#ifdef CONFIG_REPLICATION
    Error *err = NULL;
    ReplicationStatus *s = g_new0(ReplicationStatus, 1);

    replication_get_error_all(&err);
    if (err) {
        s->error = true;
        s->has_desc = true;
        s->desc = g_strdup(error_get_pretty(err));
    } else {
        s->error = false;
    }

    error_free(err);
    return s;
#else
    abort();
#endif
}

void qmp_xen_colo_do_checkpoint(Error **errp)
{
#ifdef CONFIG_REPLICATION
    replication_do_checkpoint_all(errp);
#else
    abort();
#endif
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
    trace_colo_send_message(COLOMessage_str(msg));
}

static void colo_send_message_value(QEMUFile *f, COLOMessage msg,
                                    uint64_t value, Error **errp)
{
    Error *local_err = NULL;
    int ret;

    colo_send_message(f, msg, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    qemu_put_be64(f, value);
    qemu_fflush(f);

    ret = qemu_file_get_error(f);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to send value for message:%s",
                         COLOMessage_str(msg));
    }
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
    trace_colo_receive_message(COLOMessage_str(msg));
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

static uint64_t colo_receive_message_value(QEMUFile *f, uint32_t expect_msg,
                                           Error **errp)
{
    Error *local_err = NULL;
    uint64_t value;
    int ret;

    colo_receive_check_message(f, expect_msg, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return 0;
    }

    value = qemu_get_be64(f);
    ret = qemu_file_get_error(f);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to get value for COLO message: %s",
                         COLOMessage_str(expect_msg));
    }
    return value;
}

static int colo_do_checkpoint_transaction(MigrationState *s,
                                          QIOChannelBuffer *bioc,
                                          QEMUFile *fb)
{
    Error *local_err = NULL;
    int ret = -1;

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
    /* Reset channel-buffer directly */
    qio_channel_io_seek(QIO_CHANNEL(bioc), 0, 0, NULL);
    bioc->usage = 0;

    qemu_mutex_lock_iothread();
    if (failover_get_state() != FAILOVER_STATUS_NONE) {
        qemu_mutex_unlock_iothread();
        goto out;
    }
    vm_stop_force_state(RUN_STATE_COLO);
    qemu_mutex_unlock_iothread();
    trace_colo_vm_state_change("run", "stop");
    /*
     * Failover request bh could be called after vm_stop_force_state(),
     * So we need check failover_request_is_active() again.
     */
    if (failover_get_state() != FAILOVER_STATUS_NONE) {
        goto out;
    }

    colo_notify_compares_event(NULL, COLO_EVENT_CHECKPOINT, &local_err);
    if (local_err) {
        goto out;
    }

    /* Disable block migration */
    migrate_set_block_enabled(false, &local_err);
    qemu_savevm_state_header(fb);
    qemu_savevm_state_setup(fb);
    qemu_mutex_lock_iothread();
    qemu_savevm_state_complete_precopy(fb, false, false);
    qemu_mutex_unlock_iothread();

    qemu_fflush(fb);

    colo_send_message(s->to_dst_file, COLO_MESSAGE_VMSTATE_SEND, &local_err);
    if (local_err) {
        goto out;
    }
    /*
     * We need the size of the VMstate data in Secondary side,
     * With which we can decide how much data should be read.
     */
    colo_send_message_value(s->to_dst_file, COLO_MESSAGE_VMSTATE_SIZE,
                            bioc->usage, &local_err);
    if (local_err) {
        goto out;
    }

    qemu_put_buffer(s->to_dst_file, bioc->data, bioc->usage);
    qemu_fflush(s->to_dst_file);
    ret = qemu_file_get_error(s->to_dst_file);
    if (ret < 0) {
        goto out;
    }

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

    ret = 0;

    qemu_mutex_lock_iothread();
    vm_start();
    qemu_mutex_unlock_iothread();
    trace_colo_vm_state_change("stop", "run");

out:
    if (local_err) {
        error_report_err(local_err);
    }
    return ret;
}

static void colo_compare_notify_checkpoint(Notifier *notifier, void *data)
{
    colo_checkpoint_notify(data);
}

static void colo_process_checkpoint(MigrationState *s)
{
    QIOChannelBuffer *bioc;
    QEMUFile *fb = NULL;
    int64_t current_time = qemu_clock_get_ms(QEMU_CLOCK_HOST);
    Error *local_err = NULL;
    int ret;

    failover_init_state();

    s->rp_state.from_dst_file = qemu_file_get_return_path(s->to_dst_file);
    if (!s->rp_state.from_dst_file) {
        error_report("Open QEMUFile from_dst_file failed");
        goto out;
    }

    packets_compare_notifier.notify = colo_compare_notify_checkpoint;
    colo_compare_register_notifier(&packets_compare_notifier);

    /*
     * Wait for Secondary finish loading VM states and enter COLO
     * restore.
     */
    colo_receive_check_message(s->rp_state.from_dst_file,
                       COLO_MESSAGE_CHECKPOINT_READY, &local_err);
    if (local_err) {
        goto out;
    }
    bioc = qio_channel_buffer_new(COLO_BUFFER_BASE_SIZE);
    fb = qemu_fopen_channel_output(QIO_CHANNEL(bioc));
    object_unref(OBJECT(bioc));

    qemu_mutex_lock_iothread();
    vm_start();
    qemu_mutex_unlock_iothread();
    trace_colo_vm_state_change("stop", "run");

    timer_mod(s->colo_delay_timer,
            current_time + s->parameters.x_checkpoint_delay);

    while (s->state == MIGRATION_STATUS_COLO) {
        if (failover_get_state() != FAILOVER_STATUS_NONE) {
            error_report("failover request");
            goto out;
        }

        qemu_sem_wait(&s->colo_checkpoint_sem);

        ret = colo_do_checkpoint_transaction(s, bioc, fb);
        if (ret < 0) {
            goto out;
        }
    }

out:
    /* Throw the unreported error message after exited from loop */
    if (local_err) {
        error_report_err(local_err);
    }

    if (fb) {
        qemu_fclose(fb);
    }

    /* Hope this not to be too long to wait here */
    qemu_sem_wait(&s->colo_exit_sem);
    qemu_sem_destroy(&s->colo_exit_sem);

    /*
     * It is safe to unregister notifier after failover finished.
     * Besides, colo_delay_timer and colo_checkpoint_sem can't be
     * released befor unregister notifier, or there will be use-after-free
     * error.
     */
    colo_compare_unregister_notifier(&packets_compare_notifier);
    timer_del(s->colo_delay_timer);
    timer_free(s->colo_delay_timer);
    qemu_sem_destroy(&s->colo_checkpoint_sem);

    /*
     * Must be called after failover BH is completed,
     * Or the failover BH may shutdown the wrong fd that
     * re-used by other threads after we release here.
     */
    if (s->rp_state.from_dst_file) {
        qemu_fclose(s->rp_state.from_dst_file);
    }
}

void colo_checkpoint_notify(void *opaque)
{
    MigrationState *s = opaque;
    int64_t next_notify_time;

    qemu_sem_post(&s->colo_checkpoint_sem);
    s->colo_checkpoint_time = qemu_clock_get_ms(QEMU_CLOCK_HOST);
    next_notify_time = s->colo_checkpoint_time +
                    s->parameters.x_checkpoint_delay;
    timer_mod(s->colo_delay_timer, next_notify_time);
}

void migrate_start_colo_process(MigrationState *s)
{
    qemu_mutex_unlock_iothread();
    qemu_sem_init(&s->colo_checkpoint_sem, 0);
    s->colo_delay_timer =  timer_new_ms(QEMU_CLOCK_HOST,
                                colo_checkpoint_notify, s);

    qemu_sem_init(&s->colo_exit_sem, 0);
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
    QEMUFile *fb = NULL;
    QIOChannelBuffer *bioc = NULL; /* Cache incoming device state */
    uint64_t total_size;
    uint64_t value;
    Error *local_err = NULL;

    rcu_register_thread();
    qemu_sem_init(&mis->colo_incoming_sem, 0);

    migrate_set_state(&mis->state, MIGRATION_STATUS_ACTIVE,
                      MIGRATION_STATUS_COLO);

    failover_init_state();

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

    bioc = qio_channel_buffer_new(COLO_BUFFER_BASE_SIZE);
    fb = qemu_fopen_channel_input(QIO_CHANNEL(bioc));
    object_unref(OBJECT(bioc));

    qemu_mutex_lock_iothread();
    vm_start();
    trace_colo_vm_state_change("stop", "run");
    qemu_mutex_unlock_iothread();

    colo_send_message(mis->to_src_file, COLO_MESSAGE_CHECKPOINT_READY,
                      &local_err);
    if (local_err) {
        goto out;
    }

    while (mis->state == MIGRATION_STATUS_COLO) {
        int request = 0;

        colo_wait_handle_message(mis->from_src_file, &request, &local_err);
        if (local_err) {
            goto out;
        }
        assert(request);
        if (failover_get_state() != FAILOVER_STATUS_NONE) {
            error_report("failover request");
            goto out;
        }

        qemu_mutex_lock_iothread();
        vm_stop_force_state(RUN_STATE_COLO);
        trace_colo_vm_state_change("run", "stop");
        qemu_mutex_unlock_iothread();

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

        value = colo_receive_message_value(mis->from_src_file,
                                 COLO_MESSAGE_VMSTATE_SIZE, &local_err);
        if (local_err) {
            goto out;
        }

        /*
         * Read VM device state data into channel buffer,
         * It's better to re-use the memory allocated.
         * Here we need to handle the channel buffer directly.
         */
        if (value > bioc->capacity) {
            bioc->capacity = value;
            bioc->data = g_realloc(bioc->data, bioc->capacity);
        }
        total_size = qemu_get_buffer(mis->from_src_file, bioc->data, value);
        if (total_size != value) {
            error_report("Got %" PRIu64 " VMState data, less than expected"
                        " %" PRIu64, total_size, value);
            goto out;
        }
        bioc->usage = total_size;
        qio_channel_io_seek(QIO_CHANNEL(bioc), 0, 0, NULL);

        colo_send_message(mis->to_src_file, COLO_MESSAGE_VMSTATE_RECEIVED,
                     &local_err);
        if (local_err) {
            goto out;
        }

        qemu_mutex_lock_iothread();
        qemu_system_reset(SHUTDOWN_CAUSE_NONE);
        vmstate_loading = true;
        if (qemu_loadvm_state(fb) < 0) {
            error_report("COLO: loadvm failed");
            qemu_mutex_unlock_iothread();
            goto out;
        }

        vmstate_loading = false;
        vm_start();
        trace_colo_vm_state_change("stop", "run");
        qemu_mutex_unlock_iothread();

        if (failover_get_state() == FAILOVER_STATUS_RELAUNCH) {
            failover_set_state(FAILOVER_STATUS_RELAUNCH,
                            FAILOVER_STATUS_NONE);
            failover_request_active(NULL);
            goto out;
        }

        colo_send_message(mis->to_src_file, COLO_MESSAGE_VMSTATE_LOADED,
                     &local_err);
        if (local_err) {
            goto out;
        }
    }

out:
    vmstate_loading = false;
    /* Throw the unreported error message after exited from loop */
    if (local_err) {
        error_report_err(local_err);
    }

    if (fb) {
        qemu_fclose(fb);
    }

    /* Hope this not to be too long to loop here */
    qemu_sem_wait(&mis->colo_incoming_sem);
    qemu_sem_destroy(&mis->colo_incoming_sem);
    /* Must be called after failover BH is completed */
    if (mis->to_src_file) {
        qemu_fclose(mis->to_src_file);
    }
    migration_incoming_exit_colo();

    rcu_unregister_thread();
    return NULL;
}
