/*
 * QEMU CXL Support
 *
 * Copyright (c) 2020 Intel
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See the
 * COPYING file in the top-level directory.
 */

#ifndef CXL_H
#define CXL_H

#include "hw/pci/pci_host.h"
#include "cxl_pci.h"
#include "cxl_component.h"
#include "cxl_device.h"

#define CXL_COMPONENT_REG_BAR_IDX 0
#define CXL_DEVICE_REG_BAR_IDX 2

#define CXL_WINDOW_MAX 10

typedef struct CXLState {
    bool is_enabled;
    MemoryRegion host_mr;
    unsigned int next_mr_idx;
} CXLState;

struct CXLHost {
    PCIHostState parent_obj;

    CXLComponentState cxl_cstate;
};

#define TYPE_PXB_CXL_HOST "pxb-cxl-host"
OBJECT_DECLARE_SIMPLE_TYPE(CXLHost, PXB_CXL_HOST)

#endif
