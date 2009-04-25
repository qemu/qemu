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

#ifndef QEMU_WATCHDOG_H
#define QEMU_WATCHDOG_H

extern void wdt_i6300esb_init(void);
extern void wdt_ib700_init(void);

/* Possible values for action parameter. */
#define WDT_RESET        1	/* Hard reset. */
#define WDT_SHUTDOWN     2	/* Shutdown. */
#define WDT_POWEROFF     3	/* Quit. */
#define WDT_PAUSE        4	/* Pause. */
#define WDT_DEBUG        5	/* Prints a message and continues running. */
#define WDT_NONE         6	/* Do nothing. */

struct WatchdogTimerModel {
    LIST_ENTRY(WatchdogTimerModel) entry;

    /* Short name of the device - used to select it on the command line. */
    const char *wdt_name;
    /* Longer description (eg. manufacturer and full model number). */
    const char *wdt_description;

    /* This callback should create/register the device.  It is called
     * indirectly from hw/pc.c when the virtual PC is being set up.
     */
    void (*wdt_pc_init)(PCIBus *pci_bus);
};
typedef struct WatchdogTimerModel WatchdogTimerModel;

/* in vl.c */
extern WatchdogTimerModel *watchdog;
extern int watchdog_action;

/* in hw/watchdog.c */
extern int select_watchdog(const char *p);
extern int select_watchdog_action(const char *action);
extern void watchdog_add_model(WatchdogTimerModel *model);
extern void watchdog_perform_action(void);
extern void watchdog_pc_init(PCIBus *pci_bus);
extern void register_watchdogs(void);

#endif /* QEMU_WATCHDOG_H */
