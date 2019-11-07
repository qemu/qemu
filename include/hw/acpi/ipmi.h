/*
 * QEMU IPMI ACPI handling
 *
 * Copyright (c) 2015,2016 Corey Minyard <cminyard@mvista.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef HW_ACPI_IPMI_H
#define HW_ACPI_IPMI_H

#include "hw/acpi/aml-build.h"

/*
 * Add ACPI IPMI entries for all registered IPMI devices whose parent
 * bus matches the given bus.  The resource is the ACPI resource that
 * contains the IPMI device, this is required for the I2C CRS.
 */
void build_acpi_ipmi_devices(Aml *table, BusState *bus, const char *resource);

#endif /* HW_ACPI_IPMI_H */
