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
#include "monitor/monitor.h"
#include "monitor/qmp-helpers.h"
#include "sysemu/sysemu.h"
#include "sysemu/kvm.h"
#include "sysemu/runstate.h"
#include "sysemu/runstate-action.h"
#include "sysemu/block-backend.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-acpi.h"
#include "qapi/qapi-commands-control.h"
#include "qapi/qapi-commands-misc.h"
#include "qapi/type-helpers.h"
#include "hw/mem/memory-device.h"
#include "hw/acpi/acpi_dev_interface.h"
#include "hw/intc/intc.h"
#include "hw/rdma/rdma.h"

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

    /* Continuing after completed migration. Images have been inactivated to
     * allow the destination to take control. Need to get control back now.
     *
     * If there are no inactive block nodes (e.g. because the VM was just
     * paused rather than completing a migration), bdrv_inactivate_all() simply
     * doesn't do anything. */
    bdrv_activate_all(&local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (runstate_check(RUN_STATE_INMIGRATE)) {
        autostart = 1;
    } else {
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

ACPIOSTInfoList *qmp_query_acpi_ospm_status(Error **errp)
{
    bool ambig;
    ACPIOSTInfoList *head = NULL;
    ACPIOSTInfoList **prev = &head;
    Object *obj = object_resolve_path_type("", TYPE_ACPI_DEVICE_IF, &ambig);

    if (obj) {
        AcpiDeviceIfClass *adevc = ACPI_DEVICE_IF_GET_CLASS(obj);
        AcpiDeviceIf *adev = ACPI_DEVICE_IF(obj);

        adevc->ospm_status(adev, &prev);
    } else {
        error_setg(errp, "command is not supported, missing ACPI device");
    }

    return head;
}
