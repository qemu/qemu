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

#include "sysemu/hvf.h"
#include "cpu.h"
#include "x86.h"

#define HVF_MAX_VCPU 0x10
#define MAX_VM_ID 0x40
#define MAX_VCPU_ID 0x40

extern struct hvf_state hvf_global;

struct hvf_vm {
    int id;
    struct hvf_vcpu_state *vcpus[HVF_MAX_VCPU];
};

struct hvf_state {
    uint32_t version;
    struct hvf_vm *vm;
    uint64_t mem_quota;
};

#ifdef NEED_CPU_H
/* Functions exported to host specific mode */

/* Host specific functions */
int hvf_inject_interrupt(CPUArchState *env, int vector);
int hvf_vcpu_run(struct hvf_vcpu_state *vcpu);
#endif

#endif
