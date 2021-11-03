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

#ifndef QEMU_WATCHDOG_H
#define QEMU_WATCHDOG_H

#include "qemu/queue.h"
#include "qapi/qapi-types-run-state.h"

struct WatchdogTimerModel {
    QLIST_ENTRY(WatchdogTimerModel) entry;

    /* Short name of the device - used to select it on the command line. */
    const char *wdt_name;
    /* Longer description (eg. manufacturer and full model number). */
    const char *wdt_description;
};
typedef struct WatchdogTimerModel WatchdogTimerModel;

/* in hw/watchdog.c */
int select_watchdog(const char *p);
WatchdogAction get_watchdog_action(void);
void watchdog_add_model(WatchdogTimerModel *model);
void watchdog_perform_action(void);

#endif /* QEMU_WATCHDOG_H */
