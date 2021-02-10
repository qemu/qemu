/*
 * HMAT ACPI Implementation Header
 *
 * Copyright(C) 2019 Intel Corporation.
 *
 * Author:
 *  Liu jingqi <jingqi.liu@linux.intel.com>
 *  Tao Xu <tao3.xu@intel.com>
 *
 * HMAT is defined in ACPI 6.3: 5.2.27 Heterogeneous Memory Attribute Table
 * (HMAT)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#ifndef HMAT_H
#define HMAT_H

#include "hw/acpi/aml-build.h"

/*
 * ACPI 6.3: 5.2.27.3 Memory Proximity Domain Attributes Structure,
 * Table 5-145, Field "flag", Bit [0]: set to 1 to indicate that data in
 * the Proximity Domain for the Attached Initiator field is valid.
 * Other bits reserved.
 */
#define HMAT_PROXIMITY_INITIATOR_VALID  0x1

void build_hmat(GArray *table_data, BIOSLinker *linker, NumaState *numa_state,
                const char *oem_id, const char *oem_table_id);

#endif
