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

uint32_t hvf_get_supported_cpuid(uint32_t func, uint32_t idx, int reg);

void hvf_handle_io(CPUArchState *, uint16_t, void *, int, int, int);

/* Host specific functions */
int hvf_inject_interrupt(CPUArchState *env, int vector);

#endif
