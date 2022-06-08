/*
 * CXL host parameter parsing routine stubs
 *
 * Copyright (c) 2022 Huawei
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/cxl/cxl.h"
#include "hw/cxl/cxl_host.h"

void cxl_fixed_memory_window_link_targets(Error **errp) {};
void cxl_machine_init(Object *obj, CXLState *state) {};

const MemoryRegionOps cfmws_ops;
