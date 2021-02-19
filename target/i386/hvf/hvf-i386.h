/*
 * QEMU Hypervisor.framework (HVF) support
 *
 * Copyright 2017 Google Inc
 *
 * Adapted from target-i386/hax-i386.h:
 * Copyright (c) 2011 Intel Corporation
 *  Written by:
 *  Jiang Yunhong<yunhong.jiang@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef HVF_I386_H
#define HVF_I386_H

#include "qemu/accel.h"
#include "sysemu/hvf.h"
#include "cpu.h"
#include "x86.h"

/* hvf_slot flags */
#define HVF_SLOT_LOG (1 << 0)

typedef struct hvf_slot {
    uint64_t start;
    uint64_t size;
    uint8_t *mem;
    int slot_id;
    uint32_t flags;
    MemoryRegion *region;
} hvf_slot;

typedef struct hvf_vcpu_caps {
    uint64_t vmx_cap_pinbased;
    uint64_t vmx_cap_procbased;
    uint64_t vmx_cap_procbased2;
    uint64_t vmx_cap_entry;
    uint64_t vmx_cap_exit;
    uint64_t vmx_cap_preemption_timer;
} hvf_vcpu_caps;

struct HVFState {
    AccelState parent;
    hvf_slot slots[32];
    int num_slots;

    hvf_vcpu_caps *hvf_caps;
};
extern HVFState *hvf_state;

void hvf_set_phys_mem(MemoryRegionSection *, bool);
void hvf_handle_io(CPUArchState *, uint16_t, void *, int, int, int);
hvf_slot *hvf_find_overlap_slot(uint64_t, uint64_t);

#ifdef NEED_CPU_H
/* Functions exported to host specific mode */

/* Host specific functions */
int hvf_inject_interrupt(CPUArchState *env, int vector);
#endif

#endif
