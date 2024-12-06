/*
 * QEMU Management Protocol commands
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/sockets.h"
#include "monitor-internal.h"
#include "monitor/qdev.h"
#include "monitor/qmp-helpers.h"
#include "system/system.h"
#include "system/kvm.h"
#include "system/runstate.h"
#include "system/runstate-action.h"
#include "system/block-backend.h"
#include "qapi/error.h"
#include "qapi/qapi-init-commands.h"
#include "qapi/qapi-commands-control.h"
#include "qapi/qapi-commands-misc.h"
#include "qapi/qmp/qerror.h"
#include "qapi/type-helpers.h"
#include "hw/mem/memory-device.h"
#include "hw/intc/intc.h"
#include "migration/misc.h"

NameInfo *qmp_query_name(Error **errp)
{
    NameInfo *info = g_malloc0(sizeof(*info));

    info->name = g_strdup(qemu_name);
    return info;
}

void qmp_quit(Error **errp)
{
    shutdown_action = SHUTDOWN_ACTION_POWEROFF;
    qemu_system_shutdown_request(SHUTDOWN_CAUSE_HOST_QMP_QUIT);
}

void qmp_stop(Error **errp)
{
    /* if there is a dump in background, we should wait until the dump
     * finished */
    if (qemu_system_dump_in_progress()) {
        error_setg(errp, "There is a dump in process, please wait.");
        return;
    }

    if (runstate_check(RUN_STATE_INMIGRATE)) {
        autostart = 0;
    } else {
        vm_stop(RUN_STATE_PAUSED);
    }
}

void qmp_cont(Error **errp)
{
    BlockBackend *blk;
    BlockJob *job;
    Error *local_err = NULL;

    /* if there is a dump in background, we should wait until the dump
     * finished */
    if (qemu_system_dump_in_progress()) {
        error_setg(errp, "There is a dump in process, please wait.");
        return;
    }

    if (runstate_needs_reset()) {
        error_setg(errp, "Resetting the Virtual Machine is required");
        return;
    } else if (runstate_check(RUN_STATE_SUSPENDED)) {
        return;
    } else if (runstate_check(RUN_STATE_FINISH_MIGRATE)) {
        error_setg(errp, "Migration is not finalized yet");
        return;
    }

    for (blk = blk_next(NULL); blk; blk = blk_next(blk)) {
        blk_iostatus_reset(blk);
    }

    WITH_JOB_LOCK_GUARD() {
        for (job = block_job_next_locked(NULL); job;
             job = block_job_next_locked(job)) {
            block_job_iostatus_reset_locked(job);
        }
    }

    if (runstate_check(RUN_STATE_INMIGRATE)) {
        autostart = 1;
    } else {
        /*
         * Continuing after completed migration. Images have been
         * inactivated to allow the destination to take control. Need to
         * get control back now.
         */
        if (!migration_block_activate(&local_err)) {
            error_propagate(errp, local_err);
            return;
        }
        vm_start();
    }
}

void qmp_add_client(const char *protocol, const char *fdname,
                    bool has_skipauth, bool skipauth, bool has_tls, bool tls,
                    Error **errp)
{
    static const struct {
        const char *name;
        bool (*add_client)(int fd, bool has_skipauth, bool skipauth,
                           bool has_tls, bool tls, Error **errp);
    } protocol_table[] = {
        { "spice", qmp_add_client_spice },
#ifdef CONFIG_VNC
        { "vnc", qmp_add_client_vnc },
#endif
#ifdef CONFIG_DBUS_DISPLAY
        { "@dbus-display", qmp_add_client_dbus_display },
#endif
    };
    int fd, i;

    fd = monitor_get_fd(monitor_cur(), fdname, errp);
    if (fd < 0) {
        return;
    }

    if (!fd_is_socket(fd)) {
        error_setg(errp, "parameter @fdname must name a socket");
        close(fd);
        return;
    }

    for (i = 0; i < ARRAY_SIZE(protocol_table); i++) {
        if (!strcmp(protocol, protocol_table[i].name)) {
            if (!protocol_table[i].add_client(fd, has_skipauth, skipauth,
                                              has_tls, tls, errp)) {
                close(fd);
            }
            return;
        }
    }

    if (!qmp_add_client_char(fd, has_skipauth, skipauth, has_tls, tls,
                             protocol, errp)) {
        close(fd);
    }
}

char *qmp_human_monitor_command(const char *command_line, bool has_cpu_index,
                                int64_t cpu_index, Error **errp)
{
    char *output = NULL;
    MonitorHMP hmp = {};

    monitor_data_init(&hmp.common, false, true, false);

    if (has_cpu_index) {
        int ret = monitor_set_cpu(&hmp.common, cpu_index);
        if (ret < 0) {
            error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "cpu-index",
                       "a CPU number");
            goto out;
        }
    }

    handle_hmp_command(&hmp, command_line);

    WITH_QEMU_LOCK_GUARD(&hmp.common.mon_lock) {
        output = g_strdup(hmp.common.outbuf->str);
    }

out:
    monitor_data_destroy(&hmp.common);
    return output;
}

static void __attribute__((__constructor__)) monitor_init_qmp_commands(void)
{
    /*
     * Two command lists:
     * - qmp_commands contains all QMP commands
     * - qmp_cap_negotiation_commands contains just
     *   "qmp_capabilities", to enforce capability negotiation
     */

    qmp_init_marshal(&qmp_commands);

    qmp_register_command(&qmp_commands, "device_add",
                         qmp_device_add, 0, 0);

    QTAILQ_INIT(&qmp_cap_negotiation_commands);
    qmp_register_command(&qmp_cap_negotiation_commands, "qmp_capabilities",
                         qmp_marshal_qmp_capabilities,
                         QCO_ALLOW_PRECONFIG, 0);
}
