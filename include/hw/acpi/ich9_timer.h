/*
 * QEMU ICH9 Timer emulation
 *
 * Copyright (c) 2024 Dominic Prinz <git@dprinz.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_ACPI_ICH9_TIMER_H
#define HW_ACPI_ICH9_TIMER_H

#include "hw/acpi/ich9.h"

void ich9_pm_update_swsmi_timer(ICH9LPCPMRegs *pm, bool enable);

void ich9_pm_swsmi_timer_init(ICH9LPCPMRegs *pm);

void ich9_pm_update_periodic_timer(ICH9LPCPMRegs *pm, bool enable);

void ich9_pm_periodic_timer_init(ICH9LPCPMRegs *pm);

#endif
