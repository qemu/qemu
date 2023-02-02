#include "qemu/osdep.h"
#include "hw/i386/pc.h"
#include "hw/i386/acpi-build.h"

void pc_madt_cpu_entry(int uid, const CPUArchIdList *apic_ids,
                       GArray *entry, bool force_enabled)
{
}

Object *acpi_get_i386_pci_host(void)
{
       return NULL;
}
