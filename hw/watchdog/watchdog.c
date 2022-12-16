/*
 * Virtual hardware watchdog.
 *
 * Copyright (C) 2009 Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * By Richard W.M. Jones (rjones@redhat.com).
 */

#include "qemu/osdep.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "qemu/queue.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-run-state.h"
#include "qapi/qapi-events-run-state.h"
#include "sysemu/runstate.h"
#include "sysemu/watchdog.h"
#include "hw/nmi.h"
#include "qemu/help_option.h"
#include "trace.h"

static WatchdogAction watchdog_action = WATCHDOG_ACTION_RESET;

WatchdogAction get_watchdog_action(void)
{
    return watchdog_action;
}

/* This actually performs the "action" once a watchdog has expired,
 * ie. reboot, shutdown, exit, etc.
 */
void watchdog_perform_action(void)
{
    trace_watchdog_perform_action(watchdog_action);

    switch (watchdog_action) {
    case WATCHDOG_ACTION_RESET:     /* same as 'system_reset' in monitor */
        qapi_event_send_watchdog(WATCHDOG_ACTION_RESET);
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        break;

    case WATCHDOG_ACTION_SHUTDOWN:  /* same as 'system_powerdown' in monitor */
        qapi_event_send_watchdog(WATCHDOG_ACTION_SHUTDOWN);
        qemu_system_powerdown_request();
        break;

    case WATCHDOG_ACTION_POWEROFF:  /* same as 'quit' command in monitor */
        qapi_event_send_watchdog(WATCHDOG_ACTION_POWEROFF);
        exit(0);

    case WATCHDOG_ACTION_PAUSE:     /* same as 'stop' command in monitor */
        /* In a timer callback, when vm_stop calls qemu_clock_enable
         * you would get a deadlock.  Bypass the problem.
         */
        qemu_system_vmstop_request_prepare();
        qapi_event_send_watchdog(WATCHDOG_ACTION_PAUSE);
        qemu_system_vmstop_request(RUN_STATE_WATCHDOG);
        break;

    case WATCHDOG_ACTION_DEBUG:
        qapi_event_send_watchdog(WATCHDOG_ACTION_DEBUG);
        fprintf(stderr, "watchdog: timer fired\n");
        break;

    case WATCHDOG_ACTION_NONE:
        qapi_event_send_watchdog(WATCHDOG_ACTION_NONE);
        break;

    case WATCHDOG_ACTION_INJECT_NMI:
        qapi_event_send_watchdog(WATCHDOG_ACTION_INJECT_NMI);
        nmi_monitor_handle(0, NULL);
        break;

    default:
        assert(0);
    }
}

void qmp_watchdog_set_action(WatchdogAction action, Error **errp)
{
    watchdog_action = action;
    trace_watchdog_set_action(watchdog_action);
}
