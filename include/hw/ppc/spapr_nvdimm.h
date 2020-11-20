/*
 * QEMU PowerPC PAPR SCM backend definitions
 *
 * Copyright (c) 2020, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef HW_SPAPR_NVDIMM_H
#define HW_SPAPR_NVDIMM_H

#include "hw/mem/nvdimm.h"
#include "hw/ppc/spapr.h"

/*
 * The nvdimm size should be aligned to SCM block size.
 * The SCM block size should be aligned to SPAPR_MEMORY_BLOCK_SIZE
 * inorder to have SCM regions not to overlap with dimm memory regions.
 * The SCM devices can have variable block sizes. For now, fixing the
 * block size to the minimum value.
 */
#define SPAPR_MINIMUM_SCM_BLOCK_SIZE SPAPR_MEMORY_BLOCK_SIZE

/* Have an explicit check for alignment */
QEMU_BUILD_BUG_ON(SPAPR_MINIMUM_SCM_BLOCK_SIZE % SPAPR_MEMORY_BLOCK_SIZE);

int spapr_pmem_dt_populate(SpaprDrc *drc, SpaprMachineState *spapr,
                           void *fdt, int *fdt_start_offset, Error **errp);
void spapr_dt_persistent_memory(SpaprMachineState *spapr, void *fdt);
bool spapr_nvdimm_validate(HotplugHandler *hotplug_dev, NVDIMMDevice *nvdimm,
                           uint64_t size, Error **errp);
void spapr_add_nvdimm(DeviceState *dev, uint64_t slot);

#endif
