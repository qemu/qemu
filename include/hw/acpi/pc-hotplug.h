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
 * ONLY DEFINEs are permited in this file since it's shared
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

#define ACPI_MEMORY_HOTPLUG_IO_LEN 24
#define ACPI_MEMORY_HOTPLUG_BASE 0x0a00

#define MEMORY_SLOTS_NUMBER          "MDNR"
#define MEMORY_HOTPLUG_IO_REGION     "HPMR"
#define MEMORY_SLOT_ADDR_LOW         "MRBL"
#define MEMORY_SLOT_ADDR_HIGH        "MRBH"
#define MEMORY_SLOT_SIZE_LOW         "MRLL"
#define MEMORY_SLOT_SIZE_HIGH        "MRLH"
#define MEMORY_SLOT_PROXIMITY        "MPX"
#define MEMORY_SLOT_ENABLED          "MES"
#define MEMORY_SLOT_INSERT_EVENT     "MINS"
#define MEMORY_SLOT_REMOVE_EVENT     "MRMV"
#define MEMORY_SLOT_EJECT            "MEJ"
#define MEMORY_SLOT_SLECTOR          "MSEL"
#define MEMORY_SLOT_OST_EVENT        "MOEV"
#define MEMORY_SLOT_OST_STATUS       "MOSC"
#define MEMORY_SLOT_LOCK             "MLCK"
#define MEMORY_SLOT_STATUS_METHOD    "MRST"
#define MEMORY_SLOT_CRS_METHOD       "MCRS"
#define MEMORY_SLOT_OST_METHOD       "MOST"
#define MEMORY_SLOT_PROXIMITY_METHOD "MPXM"
#define MEMORY_SLOT_EJECT_METHOD     "MEJ0"
#define MEMORY_SLOT_NOTIFY_METHOD    "MTFY"

#endif
