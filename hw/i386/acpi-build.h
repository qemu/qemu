
#ifndef HW_I386_ACPI_BUILD_H
#define HW_I386_ACPI_BUILD_H
#include "hw/acpi/acpi-defs.h"

extern const struct AcpiGenericAddress x86_nvdimm_acpi_dsmio;

/* PCI Hot-plug registers bases. See docs/spec/acpi_pci_hotplug.txt */
#define ACPI_PCIHP_SEJ_BASE 0x8
#define ACPI_PCIHP_BNMR_BASE 0x10

void acpi_setup(void);
Object *acpi_get_i386_pci_host(void);

#endif
