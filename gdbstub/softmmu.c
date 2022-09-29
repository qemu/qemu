/*
 * gdb server stub - softmmu specific bits
 *
 * Debug integration depends on support from the individual
 * accelerators so most of this involves calling the ops helpers.
 *
 * Copyright (c) 2022 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "exec/gdbstub.h"
#include "exec/hwaddr.h"
#include "sysemu/cpus.h"
#include "internals.h"

bool gdb_supports_guest_debug(void)
{
    const AccelOpsClass *ops = cpus_get_accel();
    if (ops->supports_guest_debug) {
        return ops->supports_guest_debug();
    }
    return false;
}

int gdb_breakpoint_insert(CPUState *cs, int type, hwaddr addr, hwaddr len)
{
    const AccelOpsClass *ops = cpus_get_accel();
    if (ops->insert_breakpoint) {
        return ops->insert_breakpoint(cs, type, addr, len);
    }
    return -ENOSYS;
}

int gdb_breakpoint_remove(CPUState *cs, int type, hwaddr addr, hwaddr len)
{
    const AccelOpsClass *ops = cpus_get_accel();
    if (ops->remove_breakpoint) {
        return ops->remove_breakpoint(cs, type, addr, len);
    }
    return -ENOSYS;
}

void gdb_breakpoint_remove_all(CPUState *cs)
{
    const AccelOpsClass *ops = cpus_get_accel();
    if (ops->remove_all_breakpoints) {
        ops->remove_all_breakpoints(cs);
    }
}
