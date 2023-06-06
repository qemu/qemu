/*
 * QEMU Hypervisor.framework (HVF) support
 *
 * Copyright Google Inc., 2017
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

/* header to be included in non-HVF-specific code */

#ifndef HVF_H
#define HVF_H

#include "qemu/accel.h"
#include "qom/object.h"

#ifdef NEED_CPU_H
#include "cpu.h"

#ifdef CONFIG_HVF
uint32_t hvf_get_supported_cpuid(uint32_t func, uint32_t idx,
                                 int reg);
extern bool hvf_allowed;
#define hvf_enabled() (hvf_allowed)
#else /* !CONFIG_HVF */
#define hvf_enabled() 0
#define hvf_get_supported_cpuid(func, idx, reg) 0
#endif /* !CONFIG_HVF */

#endif /* NEED_CPU_H */

#define TYPE_HVF_ACCEL ACCEL_CLASS_NAME("hvf")

typedef struct HVFState HVFState;
DECLARE_INSTANCE_CHECKER(HVFState, HVF_STATE,
                         TYPE_HVF_ACCEL)

#ifdef NEED_CPU_H
struct hvf_sw_breakpoint {
    target_ulong pc;
    target_ulong saved_insn;
    int use_count;
    QTAILQ_ENTRY(hvf_sw_breakpoint) entry;
};

struct hvf_sw_breakpoint *hvf_find_sw_breakpoint(CPUState *cpu,
                                                 target_ulong pc);
int hvf_sw_breakpoints_active(CPUState *cpu);

int hvf_arch_insert_sw_breakpoint(CPUState *cpu, struct hvf_sw_breakpoint *bp);
int hvf_arch_remove_sw_breakpoint(CPUState *cpu, struct hvf_sw_breakpoint *bp);
int hvf_arch_insert_hw_breakpoint(target_ulong addr, target_ulong len,
                                  int type);
int hvf_arch_remove_hw_breakpoint(target_ulong addr, target_ulong len,
                                  int type);
void hvf_arch_remove_all_hw_breakpoints(void);

/*
 * hvf_update_guest_debug:
 * @cs: CPUState for the CPU to update
 *
 * Update guest to enable or disable debugging. Per-arch specifics will be
 * handled by calling down to hvf_arch_update_guest_debug.
 */
int hvf_update_guest_debug(CPUState *cpu);
void hvf_arch_update_guest_debug(CPUState *cpu);

/*
 * Return whether the guest supports debugging.
 */
bool hvf_arch_supports_guest_debug(void);
#endif /* NEED_CPU_H */

#endif
