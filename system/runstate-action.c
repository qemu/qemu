/*
 * Copyright (c) 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "system/runstate-action.h"
#include "system/watchdog.h"
#include "qemu/config-file.h"
#include "qapi/error.h"
#include "qemu/option_int.h"

RebootAction reboot_action = REBOOT_ACTION_RESET;
ShutdownAction shutdown_action = SHUTDOWN_ACTION_POWEROFF;
PanicAction panic_action = PANIC_ACTION_SHUTDOWN;

/*
 * Receives actions to be applied for specific guest events
 * and sets the internal state as requested.
 */
void qmp_set_action(bool has_reboot, RebootAction reboot,
                    bool has_shutdown, ShutdownAction shutdown,
                    bool has_panic, PanicAction panic,
                    bool has_watchdog, WatchdogAction watchdog,
                    Error **errp)
{
    if (has_reboot) {
        reboot_action = reboot;
    }

    if (has_panic) {
        panic_action = panic;
    }

    if (has_watchdog) {
        qmp_watchdog_set_action(watchdog, errp);
    }

    /* Process shutdown last, in case the panic action needs to be altered */
    if (has_shutdown) {
        shutdown_action = shutdown;
    }
}
