#ifndef HW_ACPI_UTILS_H
#define HW_ACPI_UTILS_H

#include "hw/nvram/fw_cfg.h"

MemoryRegion *acpi_add_rom_blob(FWCfgCallback update, void *opaque,
                                GArray *blob, const char *name);
#endif
