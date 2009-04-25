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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 *
 * By Richard W.M. Jones (rjones@redhat.com).
 */

#include "qemu-common.h"
#include "sys-queue.h"
#include "sysemu.h"
#include "hw/watchdog.h"

static LIST_HEAD(watchdog_list, WatchdogTimerModel) watchdog_list;

void watchdog_add_model(WatchdogTimerModel *model)
{
    LIST_INSERT_HEAD(&watchdog_list, model, entry);
}

/* Returns:
 *   0 = continue
 *   1 = exit program with error
 *   2 = exit program without error
 */
int select_watchdog(const char *p)
{
    WatchdogTimerModel *model;

    if (watchdog) {
        fprintf(stderr,
                 "qemu: only one watchdog option may be given\n");
        return 1;
    }

    /* -watchdog ? lists available devices and exits cleanly. */
    if (strcmp(p, "?") == 0) {
        LIST_FOREACH(model, &watchdog_list, entry) {
            fprintf(stderr, "\t%s\t%s\n",
                     model->wdt_name, model->wdt_description);
        }
        return 2;
    }

    LIST_FOREACH(model, &watchdog_list, entry) {
        if (strcasecmp(model->wdt_name, p) == 0) {
            watchdog = model;
            return 0;
        }
    }

    fprintf(stderr, "Unknown -watchdog device. Supported devices are:\n");
    LIST_FOREACH(model, &watchdog_list, entry) {
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
    switch(watchdog_action) {
    case WDT_RESET:             /* same as 'system_reset' in monitor */
        qemu_system_reset_request();
        break;

    case WDT_SHUTDOWN:          /* same as 'system_powerdown' in monitor */
        qemu_system_powerdown_request();
        break;

    case WDT_POWEROFF:          /* same as 'quit' command in monitor */
        exit(0);
        break;

    case WDT_PAUSE:             /* same as 'stop' command in monitor */
        vm_stop(0);
        break;

    case WDT_DEBUG:
        fprintf(stderr, "watchdog: timer fired\n");
        break;

    case WDT_NONE:
        break;
    }
}

void watchdog_pc_init(PCIBus *pci_bus)
{
    if (watchdog)
        watchdog->wdt_pc_init(pci_bus);
}

void register_watchdogs(void)
{
    wdt_ib700_init();
    wdt_i6300esb_init();
}
