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

#include "qemu-common.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "qemu/queue.h"
#include "qapi/qmp/types.h"
#include "sysemu/sysemu.h"
#include "sysemu/watchdog.h"
#include "qapi-event.h"

/* Possible values for action parameter. */
#define WDT_RESET        1	/* Hard reset. */
#define WDT_SHUTDOWN     2	/* Shutdown. */
#define WDT_POWEROFF     3	/* Quit. */
#define WDT_PAUSE        4	/* Pause. */
#define WDT_DEBUG        5	/* Prints a message and continues running. */
#define WDT_NONE         6	/* Do nothing. */

static int watchdog_action = WDT_RESET;
static QLIST_HEAD(watchdog_list, WatchdogTimerModel) watchdog_list;

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
            qemu_opt_set(opts, "driver", p);
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
    if (strcasecmp(p, "reset") == 0)
        watchdog_action = WDT_RESET;
    else if (strcasecmp(p, "shutdown") == 0)
        watchdog_action = WDT_SHUTDOWN;
    else if (strcasecmp(p, "poweroff") == 0)
        watchdog_action = WDT_POWEROFF;
    else if (strcasecmp(p, "pause") == 0)
        watchdog_action = WDT_PAUSE;
    else if (strcasecmp(p, "debug") == 0)
        watchdog_action = WDT_DEBUG;
    else if (strcasecmp(p, "none") == 0)
        watchdog_action = WDT_NONE;
    else
        return -1;

    return 0;
}

/* This actually performs the "action" once a watchdog has expired,
 * ie. reboot, shutdown, exit, etc.
 */
void watchdog_perform_action(void)
{
    switch (watchdog_action) {
    case WDT_RESET:             /* same as 'system_reset' in monitor */
        qapi_event_send_watchdog(WATCHDOG_EXPIRATION_ACTION_RESET, &error_abort);
        qemu_system_reset_request();
        break;

    case WDT_SHUTDOWN:          /* same as 'system_powerdown' in monitor */
        qapi_event_send_watchdog(WATCHDOG_EXPIRATION_ACTION_SHUTDOWN, &error_abort);
        qemu_system_powerdown_request();
        break;

    case WDT_POWEROFF:          /* same as 'quit' command in monitor */
        qapi_event_send_watchdog(WATCHDOG_EXPIRATION_ACTION_POWEROFF, &error_abort);
        exit(0);

    case WDT_PAUSE:             /* same as 'stop' command in monitor */
        /* In a timer callback, when vm_stop calls qemu_clock_enable
         * you would get a deadlock.  Bypass the problem.
         */
        qemu_system_vmstop_request_prepare();
        qapi_event_send_watchdog(WATCHDOG_EXPIRATION_ACTION_PAUSE, &error_abort);
        qemu_system_vmstop_request(RUN_STATE_WATCHDOG);
        break;

    case WDT_DEBUG:
        qapi_event_send_watchdog(WATCHDOG_EXPIRATION_ACTION_DEBUG, &error_abort);
        fprintf(stderr, "watchdog: timer fired\n");
        break;

    case WDT_NONE:
        qapi_event_send_watchdog(WATCHDOG_EXPIRATION_ACTION_NONE, &error_abort);
        break;
    }
}
