/*
 * gdbstub user-mode helper routines.
 *
 * We know for user-mode we are using TCG so we can call stuff directly.
 *
 * Copyright (c) 2022 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "exec/hwaddr.h"
#include "exec/gdbstub.h"
#include "hw/core/cpu.h"
#include "internals.h"

bool gdb_supports_guest_debug(void)
{
    /* user-mode == TCG == supported */
    return true;
}

int gdb_breakpoint_insert(CPUState *cs, int type, hwaddr addr, hwaddr len)
{
    CPUState *cpu;
    int err = 0;

    switch (type) {
    case GDB_BREAKPOINT_SW:
    case GDB_BREAKPOINT_HW:
        CPU_FOREACH(cpu) {
            err = cpu_breakpoint_insert(cpu, addr, BP_GDB, NULL);
            if (err) {
                break;
            }
        }
        return err;
    default:
        /* user-mode doesn't support watchpoints */
        return -ENOSYS;
    }
}

int gdb_breakpoint_remove(CPUState *cs, int type, hwaddr addr, hwaddr len)
{
    CPUState *cpu;
    int err = 0;

    switch (type) {
    case GDB_BREAKPOINT_SW:
    case GDB_BREAKPOINT_HW:
        CPU_FOREACH(cpu) {
            err = cpu_breakpoint_remove(cpu, addr, BP_GDB);
            if (err) {
                break;
            }
        }
        return err;
    default:
        /* user-mode doesn't support watchpoints */
        return -ENOSYS;
    }
}

void gdb_breakpoint_remove_all(CPUState *cs)
{
    cpu_breakpoint_remove_all(cs, BP_GDB);
}
