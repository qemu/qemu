/*
 * Watchdog Action Table (WDAT) definitions
 *
 * Copyright Red Hat, Inc. 2026
 * Author(s): Igor Mammedov <imammedo@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef QEMU_HW_ACPI_WDAT_H
#define QEMU_HW_ACPI_WDAT_H

#include "hw/acpi/acpi-defs.h"

/*
 * Watchdog actions as described in
 *  "Hardware Watchdog Timers Design Specification"
 * for link to spec see https://uefi.org/acpi
 *     'Watchdog Action Table (WDAT)'
 */
typedef enum {
    /*
     * Restarts the watchdog timer's countdown. This action is
     * required.
     */
    WDAT_ACTION_RESET = 0x1,
    /*
     * Returns the current countdown value of the watchdog hardware
     * (in count intervals).
     */
    WDAT_ACTION_QUERY_CURRENT_COUNTDOWN_PERIOD = 0x4,
    /*
     * Returns the countdown value the watchdog hardware is
     * configured to use when reset (in count intervals).
     */
    WDAT_ACTION_QUERY_COUNTDOWN_PERIOD = 0x5,
    /*
     * Sets the countdown value (in count intervals) to be used when
     * the watchdog timer is reset. This action is required if
     * WDAT_ACTION_RESET does not explicitly write a new
     * countdown value to a register during a reset. Otherwise, this
     * action is optional.
     */
    WDAT_ACTION_SET_COUNTDOWN_PERIOD = 0x6,
    /*
     * Determines if the watchdog hardware is currently in enabled/
     * running state. The same result must occur when performed from
     * both from enabled/stopped state and enabled/running state. If
     * the watchdog hardware is disabled, results are indeterminate.
     * This action is required.
     */
    WDAT_ACTION_QUERY_RUNNING_STATE = 0x8,
    /*
     * Starts the watchdog, if not already in running state. If the
     * watchdog hardware is disabled, results are indeterminate.
     * This action is required.
     */
    WDAT_ACTION_SET_RUNNING_STATE = 0x9,
    /*
     * Determines if the watchdog hardware is currently in enabled/
     * stopped state. The same result must occur when performed from
     * both the enabled/stopped state and enabled/running state. If
     * the watchdog hardware is disabled, results are indeterminate.
     * This action is required.
     */
    WDAT_ACTION_QUERY_STOPPED_STATE = 0xA,
    /*
     * Stops the watchdog, if not already in stopped state. If the
     * watchdog hardware is disabled, results are indeterminate.
     * This action is required.
     */
    WDAT_ACTION_SET_STOPPED_STATE = 0xB,
    /*
     * Determines if the watchdog hardware is configured to perform a
     * reboot when the watchdog is fired.
     */
    WDAT_ACTION_QUERY_REBOOT = 0x10,
    /*
     * Configures the watchdog hardware to perform a reboot when it
     * is fired.
     */
    WDAT_ACTION_SET_REBOOT = 0x11,
    /*
     * Determines if the watchdog hardware is configured to perform a
     * system shutdown when fired.
     */
    WDAT_ACTION_QUERY_SHUTDOWN = 0x12,
    /*
     * Configures the watchdog hardware to perform a system shutdown
     * when fired.
     */
    WDAT_ACTION_SET_SHUTDOWN = 0x13,
    /*
     * Determines if the current boot was caused by the watchdog
     * firing. The boot status is required to be set if the watchdog
     * fired and caused a reboot. It is recommended that the
     * Watchdog Status be set if the watchdog fired and caused a
     * shutdown. This action is required.
     */
    WDAT_ACTION_QUERY_WATCHDOG_STATUS = 0x20,
    /*
     * Sets the watchdog's boot status to the default value. This
     * action is required.
     */
    WDAT_ACTION_SET_WATCHDOG_STATUS = 0x21,
} WDATAction;

#define WDAT_INS_READ_VALUE 0x0
#define WDAT_INS_READ_COUNTDOWN 0x1
#define WDAT_INS_WRITE_VALUE 0x2
#define WDAT_INS_WRITE_COUNTDOWN 0x3
#define WDAT_INS_PRESERVE_REGISTER 0x80

void build_append_wdat_ins(GArray *table_data,
                           WDATAction action, uint8_t flags,
                           struct AcpiGenericAddress as,
                           uint32_t val, uint32_t mask);

#endif /* QEMU_HW_ACPI_WDAT_H */
