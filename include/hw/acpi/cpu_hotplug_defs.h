/*
 * QEMU ACPI hotplug utilities shared defines
 *
 * Copyright (C) 2013 Red Hat Inc
 *
 * Authors:
 *   Igor Mammedov <imammedo@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef ACPI_HOTPLUG_DEFS_H
#define ACPI_HOTPLUG_DEFS_H

/*
 * ONLY DEFINEs are permited in this file since it's shared
 * between C and ASL code.
 */
#define ACPI_CPU_HOTPLUG_STATUS 4
#define ACPI_GPE_PROC_LEN 32
#define ICH9_CPU_HOTPLUG_IO_BASE 0x0CD8
#define PIIX4_CPU_HOTPLUG_IO_BASE 0xaf00

#endif
