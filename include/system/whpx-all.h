/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SYSTEM_WHPX_ALL_H
#define SYSTEM_WHPX_ALL_H

/* Called by whpx-common */
int whpx_vcpu_run(CPUState *cpu);
void whpx_get_registers(CPUState *cpu);
void whpx_set_registers(CPUState *cpu, int level);
int whpx_accel_init(AccelState *as, MachineState *ms);
void whpx_cpu_instance_init(CPUState *cs);
HRESULT whpx_set_exception_exit_bitmap(UINT64 exceptions);
void whpx_apply_breakpoints(
struct whpx_breakpoint_collection *breakpoints,
    CPUState *cpu,
    bool resuming);
void whpx_translate_cpu_breakpoints(
    struct whpx_breakpoints *breakpoints,
    CPUState *cpu,
    int cpu_breakpoint_count);
#endif
