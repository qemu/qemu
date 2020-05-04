
#ifndef HW_I386_ACPI_BUILD_H
#define HW_I386_ACPI_BUILD_H
#include "hw/acpi/acpi-defs.h"

extern const struct AcpiGenericAddress x86_nvdimm_acpi_dsmio;

void acpi_setup(void);

#endif
