#ifndef HW_I386_ACPI_COMMON_H
#define HW_I386_ACPI_COMMON_H

#include "hw/boards.h"
#include "hw/acpi/bios-linker-loader.h"
#include "hw/i386/x86.h"

/* Default IOAPIC ID */
#define ACPI_BUILD_IOAPIC_ID 0x0

void pc_madt_cpu_entry(int uid, const CPUArchIdList *apic_ids,
                       GArray *entry, bool force_enabled);
void acpi_build_madt(GArray *table_data, BIOSLinker *linker,
                     X86MachineState *x86ms,
                     const char *oem_id, const char *oem_table_id);

#endif
