/*
 * Copyright © 2020, 2021 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL-v2, version 2 or later.
 *
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu-common.h"

#include "hw/remote/machine.h"
#include "io/channel.h"
#include "hw/remote/mpqemu-link.h"
#include "qapi/error.h"
#include "sysemu/runstate.h"

void coroutine_fn mpqemu_remote_msg_loop_co(void *data)
{
    g_autofree RemoteCommDev *com = (RemoteCommDev *)data;
    PCIDevice *pci_dev = NULL;
    Error *local_err = NULL;

    assert(com->ioc);

    pci_dev = com->dev;
    for (; !local_err;) {
        MPQemuMsg msg = {0};

        if (!mpqemu_msg_recv(&msg, com->ioc, &local_err)) {
            break;
        }

        if (!mpqemu_msg_valid(&msg)) {
            error_setg(&local_err, "Received invalid message from proxy"
                                   "in remote process pid="FMT_pid"",
                                   getpid());
            break;
        }

        switch (msg.cmd) {
        default:
            error_setg(&local_err,
                       "Unknown command (%d) received for device %s"
                       " (pid="FMT_pid")",
                       msg.cmd, DEVICE(pci_dev)->id, getpid());
        }
    }

    if (local_err) {
        error_report_err(local_err);
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_HOST_ERROR);
    } else {
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    }
}
