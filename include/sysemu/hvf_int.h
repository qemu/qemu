/*
 * QEMU Hypervisor.framework (HVF) support
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

/* header to be included in HVF-specific code */

#ifndef HVF_INT_H
#define HVF_INT_H

#include <Hypervisor/hv.h>

void hvf_set_phys_mem(MemoryRegionSection *, bool);
void assert_hvf_ok(hv_return_t ret);
hvf_slot *hvf_find_overlap_slot(uint64_t, uint64_t);
int hvf_put_registers(CPUState *);
int hvf_get_registers(CPUState *);

#endif
