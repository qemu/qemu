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

#include "cxl_pci.h"
#include "cxl_component.h"
#include "cxl_device.h"

#define CXL_COMPONENT_REG_BAR_IDX 0
#define CXL_DEVICE_REG_BAR_IDX 2

typedef struct CXLState {
    bool is_enabled;
} CXLState;

#endif
