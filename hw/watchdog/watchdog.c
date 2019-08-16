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

static WatchdogAction watchdog_action = WATCHDOG_ACTION_RESET;
static QLIST_HEAD(, WatchdogTimerModel) watchdog_list;

void watchdog_add_model(WatchdogTimerModel *model)
{
    QLIST_INSERT_HEAD(&watchdog_list, model, entry);
}

/* Returns:
 *   0 = continue
 *   1 = exit program with error
 *   2 = exit program without error
 */
int select_watchdog(const char *p)
{
    WatchdogTimerModel *model;
    QemuOpts *opts;

    /* -watchdog ? lists available devices and exits cleanly. */
    if (is_help_option(p)) {
        QLIST_FOREACH(model, &watchdog_list, entry) {
            fprintf(stderr, "\t%s\t%s\n",
                     model->wdt_name, model->wdt_description);
        }
        return 2;
    }

    QLIST_FOREACH(model, &watchdog_list, entry) {
        if (strcasecmp(model->wdt_name, p) == 0) {
            /* add the device */
            opts = qemu_opts_create(qemu_find_opts("device"), NULL, 0,
                                    &error_abort);
            qemu_opt_set(opts, "driver", p, &error_abort);
            return 0;
        }
    }

    fprintf(stderr, "Unknown -watchdog device. Supported devices are:\n");
    QLIST_FOREACH(model, &watchdog_list, entry) {
        fprintf(stderr, "\t%s\t%s\n",
                 model->wdt_name, model->wdt_description);
    }
    return 1;
}

int select_watchdog_action(const char *p)
{
    int action;
    char *qapi_value;

    qapi_value = g_ascii_strdown(p, -1);
    action = qapi_enum_parse(&WatchdogAction_lookup, qapi_value, -1, NULL);
    g_free(qapi_value);
    if (action < 0)
        return -1;
    qmp_watchdog_set_action(action, &error_abort);
    return 0;
}

WatchdogAction get_watchdog_action(void)
{
    return watchdog_action;
}

/* This actually performs the "action" once a watchdog has expired,
 * ie. reboot, shutdown, exit, etc.
 */
void watchdog_perform_action(void)
{
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
}
