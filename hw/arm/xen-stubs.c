/*
 * Stubs for unimplemented Xen functions for ARM.
 *
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "qapi/qapi-commands-migration.h"
#include "system/xen.h"
#include "hw/hw.h"
#include "hw/xen/xen-hvm-common.h"
#include "hw/xen/arch_hvm.h"

void arch_handle_ioreq(XenIOState *state, ioreq_t *req)
{
    hw_error("Invalid ioreq type 0x%x\n", req->type);
}

void arch_xen_set_memory(XenIOState *state, MemoryRegionSection *section,
                         bool add)
{
}

void xen_hvm_modified_memory(ram_addr_t start, ram_addr_t length)
{
}

void qmp_xen_set_global_dirty_log(bool enable, Error **errp)
{
}
