/*
 * QEMU CXL Host Setup
 *
 * Copyright (c) 2022 Huawei
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See the
 * COPYING file in the top-level directory.
 */

#include "hw/cxl/cxl.h"
#include "hw/boards.h"

#ifndef CXL_HOST_H
#define CXL_HOST_H

void cxl_machine_init(Object *obj, CXLState *state);
void cxl_fmws_link_targets(Error **errp);
void cxl_hook_up_pxb_registers(PCIBus *bus, CXLState *state, Error **errp);
hwaddr cxl_fmws_set_memmap(hwaddr base, hwaddr max_addr);
void cxl_fmws_update_mmio(void);
GSList *cxl_fmws_get_all_sorted(void);

extern const MemoryRegionOps cfmws_ops;

#endif
