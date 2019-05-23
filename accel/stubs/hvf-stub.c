/*
 * QEMU HVF support
 *
 * Copyright 2017 Red Hat, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2 or later, as published by the Free Software Foundation,
 * and may be copied, distributed, and modified under those terms.
 *
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "sysemu/hvf.h"

int hvf_init_vcpu(CPUState *cpu)
{
    return -ENOSYS;
}

int hvf_vcpu_exec(CPUState *cpu)
{
    return -ENOSYS;
}

void hvf_vcpu_destroy(CPUState *cpu)
{
}
