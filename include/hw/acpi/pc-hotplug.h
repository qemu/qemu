/*
 * QEMU ACPI hotplug utilities shared defines
 *
 * Copyright (C) 2014 Red Hat Inc
 *
 * Authors:
 *   Igor Mammedov <imammedo@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef PC_HOTPLUG_H
#define PC_HOTPLUG_H

/*
 * ONLY DEFINEs are permitted in this file since it's shared
 * between C and ASL code.
 */

/* Limit for CPU arch IDs for CPU hotplug. All hotpluggable CPUs should
 * have CPUClass.get_arch_id() < ACPI_CPU_HOTPLUG_ID_LIMIT.
 */
#define ACPI_CPU_HOTPLUG_ID_LIMIT 256

/* 256 CPU IDs, 8 bits per entry: */
#define ACPI_GPE_PROC_LEN 32

#define ICH9_CPU_HOTPLUG_IO_BASE 0x0CD8
#define PIIX4_CPU_HOTPLUG_IO_BASE 0xaf00
#define CPU_HOTPLUG_RESOURCE_DEVICE PRES

#define ACPI_MEMORY_HOTPLUG_BASE 0x0a00

#endif
