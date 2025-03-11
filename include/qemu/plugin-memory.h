/*
 * Plugin Memory API
 *
 * Copyright (c) 2019 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PLUGIN_MEMORY_H
#define PLUGIN_MEMORY_H

#include "exec/hwaddr.h"

struct qemu_plugin_hwaddr {
    bool is_io;
    bool is_store;
    hwaddr phys_addr;
    MemoryRegion *mr;
};

/**
 * tlb_plugin_lookup: query last TLB lookup
 * @cpu: cpu environment
 *
 * This function can be used directly after a memory operation to
 * query information about the access. It is used by the plugin
 * infrastructure to expose more information about the address.
 *
 * It would only fail if not called from an instrumented memory access
 * which would be an abuse of the API.
 */
bool tlb_plugin_lookup(CPUState *cpu, vaddr addr, int mmu_idx,
                       bool is_store, struct qemu_plugin_hwaddr *data);

#endif /* PLUGIN_MEMORY_H */
