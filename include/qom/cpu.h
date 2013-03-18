/*
 * QEMU CPU model
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 */
#ifndef QEMU_CPU_H
#define QEMU_CPU_H

#include <signal.h>
#include "hw/qdev-core.h"
#include "qemu/thread.h"

/**
 * SECTION:cpu
 * @section_id: QEMU-cpu
 * @title: CPU Class
 * @short_description: Base class for all CPUs
 */

#define TYPE_CPU "cpu"

#define CPU(obj) OBJECT_CHECK(CPUState, (obj), TYPE_CPU)
#define CPU_CLASS(class) OBJECT_CLASS_CHECK(CPUClass, (class), TYPE_CPU)
#define CPU_GET_CLASS(obj) OBJECT_GET_CLASS(CPUClass, (obj), TYPE_CPU)

typedef struct CPUState CPUState;

/**
 * CPUClass:
 * @class_by_name: Callback to map -cpu command line model name to an
 * instantiatable CPU type.
 * @reset: Callback to reset the #CPUState to its initial state.
 * @do_interrupt: Callback for interrupt handling.
 * @vmsd: State description for migration.
 *
 * Represents a CPU family or model.
 */
typedef struct CPUClass {
    /*< private >*/
    DeviceClass parent_class;
    /*< public >*/

    ObjectClass *(*class_by_name)(const char *cpu_model);

    void (*reset)(CPUState *cpu);
    void (*do_interrupt)(CPUState *cpu);

    const struct VMStateDescription *vmsd;
} CPUClass;

struct KVMState;
struct kvm_run;

/**
 * CPUState:
 * @cpu_index: CPU index (informative).
 * @nr_cores: Number of cores within this CPU package.
 * @nr_threads: Number of threads within this CPU.
 * @numa_node: NUMA node this CPU is belonging to.
 * @host_tid: Host thread ID.
 * @running: #true if CPU is currently running (usermode).
 * @created: Indicates whether the CPU thread has been successfully created.
 * @interrupt_request: Indicates a pending interrupt request.
 * @halted: Nonzero if the CPU is in suspended state.
 * @stop: Indicates a pending stop request.
 * @stopped: Indicates the CPU has been artificially stopped.
 * @tcg_exit_req: Set to force TCG to stop executing linked TBs for this
 *           CPU and return to its top level loop.
 * @env_ptr: Pointer to subclass-specific CPUArchState field.
 * @current_tb: Currently executing TB.
 * @kvm_fd: vCPU file descriptor for KVM.
 *
 * State of one CPU core or thread.
 */
struct CPUState {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/

    int nr_cores;
    int nr_threads;
    int numa_node;

    struct QemuThread *thread;
#ifdef _WIN32
    HANDLE hThread;
#endif
    int thread_id;
    uint32_t host_tid;
    bool running;
    struct QemuCond *halt_cond;
    struct qemu_work_item *queued_work_first, *queued_work_last;
    bool thread_kicked;
    bool created;
    bool stop;
    bool stopped;
    volatile sig_atomic_t exit_request;
    volatile sig_atomic_t tcg_exit_req;
    uint32_t interrupt_request;

    void *env_ptr; /* CPUArchState */
    struct TranslationBlock *current_tb;

    int kvm_fd;
    bool kvm_vcpu_dirty;
    struct KVMState *kvm_state;
    struct kvm_run *kvm_run;

    /* TODO Move common fields from CPUArchState here. */
    int cpu_index; /* used by alpha TCG */
    uint32_t halted; /* used by alpha, cris, ppc TCG */
};


/**
 * cpu_reset:
 * @cpu: The CPU whose state is to be reset.
 */
void cpu_reset(CPUState *cpu);

/**
 * cpu_class_by_name:
 * @typename: The CPU base type.
 * @cpu_model: The model string without any parameters.
 *
 * Looks up a CPU #ObjectClass matching name @cpu_model.
 *
 * Returns: A #CPUClass or %NULL if not matching class is found.
 */
ObjectClass *cpu_class_by_name(const char *typename, const char *cpu_model);

/**
 * cpu_class_set_vmsd:
 * @cc: CPU class
 * @value: Value to set. Unused for %CONFIG_USER_ONLY.
 *
 * Sets #VMStateDescription for @cc.
 *
 * The @value argument is intentionally discarded for the non-softmmu targets
 * to avoid linker errors or excessive preprocessor usage. If this behavior
 * is undesired, you should assign #CPUState.vmsd directly instead.
 */
#ifndef CONFIG_USER_ONLY
static inline void cpu_class_set_vmsd(CPUClass *cc,
                                      const struct VMStateDescription *value)
{
    cc->vmsd = value;
}
#else
#define cpu_class_set_vmsd(cc, value) ((cc)->vmsd = NULL)
#endif

/**
 * qemu_cpu_has_work:
 * @cpu: The vCPU to check.
 *
 * Checks whether the CPU has work to do.
 *
 * Returns: %true if the CPU has work, %false otherwise.
 */
bool qemu_cpu_has_work(CPUState *cpu);

/**
 * qemu_cpu_is_self:
 * @cpu: The vCPU to check against.
 *
 * Checks whether the caller is executing on the vCPU thread.
 *
 * Returns: %true if called from @cpu's thread, %false otherwise.
 */
bool qemu_cpu_is_self(CPUState *cpu);

/**
 * qemu_cpu_kick:
 * @cpu: The vCPU to kick.
 *
 * Kicks @cpu's thread.
 */
void qemu_cpu_kick(CPUState *cpu);

/**
 * cpu_is_stopped:
 * @cpu: The CPU to check.
 *
 * Checks whether the CPU is stopped.
 *
 * Returns: %true if run state is not running or if artificially stopped;
 * %false otherwise.
 */
bool cpu_is_stopped(CPUState *cpu);

/**
 * run_on_cpu:
 * @cpu: The vCPU to run on.
 * @func: The function to be executed.
 * @data: Data to pass to the function.
 *
 * Schedules the function @func for execution on the vCPU @cpu.
 */
void run_on_cpu(CPUState *cpu, void (*func)(void *data), void *data);

/**
 * qemu_get_cpu:
 * @index: The CPUState@cpu_index value of the CPU to obtain.
 *
 * Gets a CPU matching @index.
 *
 * Returns: The CPU or %NULL if there is no matching CPU.
 */
CPUState *qemu_get_cpu(int index);

#ifndef CONFIG_USER_ONLY

typedef void (*CPUInterruptHandler)(CPUState *, int);

extern CPUInterruptHandler cpu_interrupt_handler;

/**
 * cpu_interrupt:
 * @cpu: The CPU to set an interrupt on.
 * @mask: The interupts to set.
 *
 * Invokes the interrupt handler.
 */
static inline void cpu_interrupt(CPUState *cpu, int mask)
{
    cpu_interrupt_handler(cpu, mask);
}

#else /* USER_ONLY */

void cpu_interrupt(CPUState *cpu, int mask);

#endif /* USER_ONLY */

/**
 * cpu_reset_interrupt:
 * @cpu: The CPU to clear the interrupt on.
 * @mask: The interrupt mask to clear.
 *
 * Resets interrupts on the vCPU @cpu.
 */
void cpu_reset_interrupt(CPUState *cpu, int mask);


#endif
