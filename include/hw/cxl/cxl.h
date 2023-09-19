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


#include "qapi/qapi-types-machine.h"
#include "qapi/qapi-visit-machine.h"
#include "hw/pci/pci_host.h"
#include "cxl_pci.h"
#include "cxl_component.h"
#include "cxl_device.h"

#define CXL_CACHE_LINE_SIZE 64
#define CXL_COMPONENT_REG_BAR_IDX 0
#define CXL_DEVICE_REG_BAR_IDX 2

#define CXL_WINDOW_MAX 10

typedef struct PXBCXLDev PXBCXLDev;

typedef struct CXLFixedWindow {
    uint64_t size;
    char **targets;
    PXBCXLDev *target_hbs[16];
    uint8_t num_targets;
    uint8_t enc_int_ways;
    uint8_t enc_int_gran;
    /* Todo: XOR based interleaving */
    MemoryRegion mr;
    hwaddr base;
} CXLFixedWindow;

typedef struct CXLState {
    bool is_enabled;
    MemoryRegion host_mr;
    unsigned int next_mr_idx;
    GList *fixed_windows;
    CXLFixedMemoryWindowOptionsList *cfmw_list;
} CXLState;

struct CXLHost {
    PCIHostState parent_obj;

    CXLComponentState cxl_cstate;
    bool passthrough;
};

#define TYPE_PXB_CXL_HOST "pxb-cxl-host"
OBJECT_DECLARE_SIMPLE_TYPE(CXLHost, PXB_CXL_HOST)

#define TYPE_CXL_USP "cxl-upstream"

typedef struct CXLUpstreamPort CXLUpstreamPort;
DECLARE_INSTANCE_CHECKER(CXLUpstreamPort, CXL_USP, TYPE_CXL_USP)
CXLComponentState *cxl_usp_to_cstate(CXLUpstreamPort *usp);
#endif
