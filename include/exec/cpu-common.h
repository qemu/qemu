/*
 * CPU interfaces that are target independent.
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */
#ifndef CPU_COMMON_H
#define CPU_COMMON_H

#include "exec/vaddr.h"
#include "exec/hwaddr.h"
#include "hw/core/cpu.h"
#include "tcg/debug-assert.h"
#include "exec/page-protection.h"

#define EXCP_INTERRUPT  0x10000 /* async interruption */
#define EXCP_HLT        0x10001 /* hlt instruction reached */
#define EXCP_DEBUG      0x10002 /* cpu stopped after a breakpoint or singlestep */
#define EXCP_HALTED     0x10003 /* cpu is halted (waiting for external event) */
#define EXCP_YIELD      0x10004 /* cpu wants to yield timeslice to another */
#define EXCP_ATOMIC     0x10005 /* stop-the-world and emulate atomic */

void cpu_exec_init_all(void);
void cpu_exec_step_atomic(CPUState *cpu);

#define REAL_HOST_PAGE_ALIGN(addr) ROUND_UP((addr), qemu_real_host_page_size())

/* The CPU list lock nests outside page_(un)lock or mmap_(un)lock */
extern QemuMutex qemu_cpu_list_lock;
void qemu_init_cpu_list(void);
void cpu_list_lock(void);
void cpu_list_unlock(void);
unsigned int cpu_list_generation_id_get(void);

int cpu_get_free_index(void);

void tcg_iommu_init_notifier_list(CPUState *cpu);
void tcg_iommu_free_notifier_list(CPUState *cpu);

/**
 * cpu_address_space_init:
 * @cpu: CPU to add this address space to
 * @asidx: integer index of this address space
 * @prefix: prefix to be used as name of address space
 * @mr: the root memory region of address space
 *
 * Add the specified address space to the CPU's cpu_ases list.
 * The address space added with @asidx 0 is the one used for the
 * convenience pointer cpu->as.
 * The target-specific code which registers ASes is responsible
 * for defining what semantics address space 0, 1, 2, etc have.
 *
 * Note that with KVM only one address space is supported.
 */
void cpu_address_space_init(CPUState *cpu, int asidx,
                            const char *prefix, MemoryRegion *mr);
/**
 * cpu_destroy_address_spaces:
 * @cpu: CPU for which address spaces need to be destroyed
 *
 * Destroy all address spaces associated with this CPU; this
 * is called as part of unrealizing the CPU.
 */
void cpu_destroy_address_spaces(CPUState *cpu);

void cpu_physical_memory_read(hwaddr addr, void *buf, hwaddr len);
void cpu_physical_memory_write(hwaddr addr, const void *buf, hwaddr len);
void *cpu_physical_memory_map(hwaddr addr,
                              hwaddr *plen,
                              bool is_write);
void cpu_physical_memory_unmap(void *buffer, hwaddr len,
                               bool is_write, hwaddr access_len);

/* vl.c */
void list_cpus(void);

#ifdef CONFIG_TCG
#include "qemu/atomic.h"

/**
 * cpu_unwind_state_data:
 * @cpu: the cpu context
 * @host_pc: the host pc within the translation
 * @data: output data
 *
 * Attempt to load the unwind state for a host pc occurring in
 * translated code.  If @host_pc is not in translated code, the
 * function returns false; otherwise @data is loaded.
 * This is the same unwind info as given to restore_state_to_opc.
 */
bool cpu_unwind_state_data(CPUState *cpu, uintptr_t host_pc, uint64_t *data);

/**
 * cpu_restore_state:
 * @cpu: the cpu context
 * @host_pc: the host pc within the translation
 * @return: true if state was restored, false otherwise
 *
 * Attempt to restore the state for a fault occurring in translated
 * code. If @host_pc is not in translated code no state is
 * restored and the function returns false.
 */
bool cpu_restore_state(CPUState *cpu, uintptr_t host_pc);

/**
 * cpu_loop_exit_requested:
 * @cpu: The CPU state to be tested
 *
 * Indicate if somebody asked for a return of the CPU to the main loop
 * (e.g., via cpu_exit() or cpu_interrupt()).
 *
 * This is helpful for architectures that support interruptible
 * instructions. After writing back all state to registers/memory, this
 * call can be used to check if it makes sense to return to the main loop
 * or to continue executing the interruptible instruction.
 */
static inline bool cpu_loop_exit_requested(CPUState *cpu)
{
    return (int32_t)qatomic_read(&cpu->neg.icount_decr.u32) < 0;
}

G_NORETURN void cpu_loop_exit_noexc(CPUState *cpu);
G_NORETURN void cpu_loop_exit_atomic(CPUState *cpu, uintptr_t pc);
G_NORETURN void cpu_loop_exit_restore(CPUState *cpu, uintptr_t pc);
#endif /* CONFIG_TCG */
G_NORETURN void cpu_loop_exit(CPUState *cpu);

/* accel/tcg/cpu-exec.c */
int cpu_exec(CPUState *cpu);

/**
 * env_archcpu(env)
 * @env: The architecture environment
 *
 * Return the ArchCPU associated with the environment.
 */
static inline ArchCPU *env_archcpu(CPUArchState *env)
{
    return (void *)env - sizeof(CPUState);
}

/**
 * env_cpu_const(env)
 * @env: The architecture environment
 *
 * Return the CPUState associated with the environment.
 */
static inline const CPUState *env_cpu_const(const CPUArchState *env)
{
    return (void *)env - sizeof(CPUState);
}

/**
 * env_cpu(env)
 * @env: The architecture environment
 *
 * Return the CPUState associated with the environment.
 */
static inline CPUState *env_cpu(CPUArchState *env)
{
    return (CPUState *)env_cpu_const(env);
}

#endif /* CPU_COMMON_H */
