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
#include "sysemu/hvf_int.h"
#include "cpu.h"
#include "x86.h"

void hvf_handle_io(CPUArchState *, uint16_t, void *, int, int, int);

/* Host specific functions */
int hvf_inject_interrupt(CPUArchState *env, int vector);

#endif
