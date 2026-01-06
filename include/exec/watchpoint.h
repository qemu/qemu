/*
 * CPU watchpoints
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef EXEC_WATCHPOINT_H
#define EXEC_WATCHPOINT_H

int cpu_watchpoint_insert(CPUState *cpu, vaddr addr, vaddr len,
                          int flags, CPUWatchpoint **watchpoint);
int cpu_watchpoint_remove(CPUState *cpu, vaddr addr,
                          vaddr len, int flags);
void cpu_watchpoint_remove_by_ref(CPUState *cpu, CPUWatchpoint *watchpoint);
void cpu_watchpoint_remove_all(CPUState *cpu, int mask);

#endif /* EXEC_WATCHPOINT_H */
