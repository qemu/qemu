/*
 * Copyright (C) 2020 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_ACPI_CXL_H
#define HW_ACPI_CXL_H

#include "hw/acpi/bios-linker-loader.h"
#include "hw/cxl/cxl.h"

void cxl_build_cedt(GArray *table_offsets, GArray *table_data,
                    BIOSLinker *linker, const char *oem_id,
                    const char *oem_table_id, CXLState *cxl_state);
void build_cxl_osc_method(Aml *dev);

#endif
