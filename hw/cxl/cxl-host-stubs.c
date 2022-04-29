/*
 * CXL host parameter parsing routine stubs
 *
 * Copyright (c) 2022 Huawei
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/cxl/cxl.h"

void cxl_fixed_memory_window_config(MachineState *ms,
                                    CXLFixedMemoryWindowOptions *object,
                                    Error **errp) {};

void cxl_fixed_memory_window_link_targets(Error **errp) {};
