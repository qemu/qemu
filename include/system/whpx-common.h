/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SYSTEM_WHPX_COMMON_H
#define SYSTEM_WHPX_COMMON_H

struct AccelCPUState {
    bool window_registered;
    bool interruptable;
    bool ready_for_pic_interrupt;
    uint64_t tpr;
    uint64_t apic_base;
    bool interruption_pending;
    /* Must be the last field as it may have a tail */
    WHV_RUN_VP_EXIT_CONTEXT exit_ctx;
};

int whpx_first_vcpu_starting(CPUState *cpu);
int whpx_last_vcpu_stopping(CPUState *cpu);
void whpx_memory_init(void);
struct whpx_breakpoint *whpx_lookup_breakpoint_by_addr(uint64_t address);
void whpx_flush_cpu_state(CPUState *cpu);
void whpx_get_reg(CPUState *cpu, WHV_REGISTER_NAME reg, WHV_REGISTER_VALUE* val);
void whpx_set_reg(CPUState *cpu, WHV_REGISTER_NAME reg, WHV_REGISTER_VALUE val);

/* On x64: same as WHvX64ExceptionTypeDebugTrapOrFault */
#define WHPX_INTERCEPT_DEBUG_TRAPS 1
#endif
