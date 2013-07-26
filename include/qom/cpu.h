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
#include "exec/hwaddr.h"
#include "qemu/thread.h"
#include "qemu/tls.h"
#include "qemu/typedefs.h"

typedef int (*WriteCoreDumpFunction)(void *buf, size_t size, void *opaque);

/**
 * vaddr:
 * Type wide enough to contain any #target_ulong virtual address.
 */
typedef uint64_t vaddr;
#define VADDR_PRId PRId64
#define VADDR_PRIu PRIu64
#define VADDR_PRIo PRIo64
#define VADDR_PRIx PRIx64
#define VADDR_PRIX PRIX64
#define VADDR_MAX UINT64_MAX

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

typedef void (*CPUUnassignedAccess)(CPUState *cpu, hwaddr addr,
                                    bool is_write, bool is_exec, int opaque,
                                    unsigned size);

struct TranslationBlock;

/**
 * CPUClass:
 * @class_by_name: Callback to map -cpu command line model name to an
 * instantiatable CPU type.
 * @reset: Callback to reset the #CPUState to its initial state.
 * @reset_dump_flags: #CPUDumpFlags to use for reset logging.
 * @do_interrupt: Callback for interrupt handling.
 * @do_unassigned_access: Callback for unassigned access handling.
 * @memory_rw_debug: Callback for GDB memory access.
 * @dump_state: Callback for dumping state.
 * @dump_statistics: Callback for dumping statistics.
 * @get_arch_id: Callback for getting architecture-dependent CPU ID.
 * @get_paging_enabled: Callback for inquiring whether paging is enabled.
 * @get_memory_mapping: Callback for obtaining the memory mappings.
 * @set_pc: Callback for setting the Program Counter register.
 * @synchronize_from_tb: Callback for synchronizing state from a TCG
 * #TranslationBlock.
 * @get_phys_page_debug: Callback for obtaining a physical address.
 * @gdb_read_register: Callback for letting GDB read a register.
 * @gdb_write_register: Callback for letting GDB write a register.
 * @vmsd: State description for migration.
 * @gdb_num_core_regs: Number of core registers accessible to GDB.
 * @gdb_core_xml_file: File name for core registers GDB XML description.
 *
 * Represents a CPU family or model.
 */
typedef struct CPUClass {
    /*< private >*/
    DeviceClass parent_class;
    /*< public >*/

    ObjectClass *(*class_by_name)(const char *cpu_model);

    void (*reset)(CPUState *cpu);
    int reset_dump_flags;
    void (*do_interrupt)(CPUState *cpu);
    CPUUnassignedAccess do_unassigned_access;
    int (*memory_rw_debug)(CPUState *cpu, vaddr addr,
                           uint8_t *buf, int len, bool is_write);
    void (*dump_state)(CPUState *cpu, FILE *f, fprintf_function cpu_fprintf,
                       int flags);
    void (*dump_statistics)(CPUState *cpu, FILE *f,
                            fprintf_function cpu_fprintf, int flags);
    int64_t (*get_arch_id)(CPUState *cpu);
    bool (*get_paging_enabled)(const CPUState *cpu);
    void (*get_memory_mapping)(CPUState *cpu, MemoryMappingList *list,
                               Error **errp);
    void (*set_pc)(CPUState *cpu, vaddr value);
    void (*synchronize_from_tb)(CPUState *cpu, struct TranslationBlock *tb);
    hwaddr (*get_phys_page_debug)(CPUState *cpu, vaddr addr);
    int (*gdb_read_register)(CPUState *cpu, uint8_t *buf, int reg);
    int (*gdb_write_register)(CPUState *cpu, uint8_t *buf, int reg);

    int (*write_elf64_note)(WriteCoreDumpFunction f, CPUState *cpu,
                            int cpuid, void *opaque);
    int (*write_elf64_qemunote)(WriteCoreDumpFunction f, CPUState *cpu,
                                void *opaque);
    int (*write_elf32_note)(WriteCoreDumpFunction f, CPUState *cpu,
                            int cpuid, void *opaque);
    int (*write_elf32_qemunote)(WriteCoreDumpFunction f, CPUState *cpu,
                                void *opaque);

    const struct VMStateDescription *vmsd;
    int gdb_num_core_regs;
    const char *gdb_core_xml_file;
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
 * @singlestep_enabled: Flags for single-stepping.
 * @env_ptr: Pointer to subclass-specific CPUArchState field.
 * @current_tb: Currently executing TB.
 * @gdb_regs: Additional GDB registers.
 * @gdb_num_regs: Number of total registers accessible to GDB.
 * @next_cpu: Next CPU sharing TB cache.
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
    int singlestep_enabled;

    void *env_ptr; /* CPUArchState */
    struct TranslationBlock *current_tb;
    struct GDBRegisterState *gdb_regs;
    int gdb_num_regs;
    CPUState *next_cpu;

    int kvm_fd;
    bool kvm_vcpu_dirty;
    struct KVMState *kvm_state;
    struct kvm_run *kvm_run;

    /* TODO Move common fields from CPUArchState here. */
    int cpu_index; /* used by alpha TCG */
    uint32_t halted; /* used by alpha, cris, ppc TCG */
};

extern CPUState *first_cpu;

DECLARE_TLS(CPUState *, current_cpu);
#define current_cpu tls_var(current_cpu)

/**
 * cpu_paging_enabled:
 * @cpu: The CPU whose state is to be inspected.
 *
 * Returns: %true if paging is enabled, %false otherwise.
 */
bool cpu_paging_enabled(const CPUState *cpu);

/**
 * cpu_get_memory_mapping:
 * @cpu: The CPU whose memory mappings are to be obtained.
 * @list: Where to write the memory mappings to.
 * @errp: Pointer for reporting an #Error.
 */
void cpu_get_memory_mapping(CPUState *cpu, MemoryMappingList *list,
                            Error **errp);

/**
 * cpu_write_elf64_note:
 * @f: pointer to a function that writes memory to a file
 * @cpu: The CPU whose memory is to be dumped
 * @cpuid: ID number of the CPU
 * @opaque: pointer to the CPUState struct
 */
int cpu_write_elf64_note(WriteCoreDumpFunction f, CPUState *cpu,
                         int cpuid, void *opaque);

/**
 * cpu_write_elf64_qemunote:
 * @f: pointer to a function that writes memory to a file
 * @cpu: The CPU whose memory is to be dumped
 * @cpuid: ID number of the CPU
 * @opaque: pointer to the CPUState struct
 */
int cpu_write_elf64_qemunote(WriteCoreDumpFunction f, CPUState *cpu,
                             void *opaque);

/**
 * cpu_write_elf32_note:
 * @f: pointer to a function that writes memory to a file
 * @cpu: The CPU whose memory is to be dumped
 * @cpuid: ID number of the CPU
 * @opaque: pointer to the CPUState struct
 */
int cpu_write_elf32_note(WriteCoreDumpFunction f, CPUState *cpu,
                         int cpuid, void *opaque);

/**
 * cpu_write_elf32_qemunote:
 * @f: pointer to a function that writes memory to a file
 * @cpu: The CPU whose memory is to be dumped
 * @cpuid: ID number of the CPU
 * @opaque: pointer to the CPUState struct
 */
int cpu_write_elf32_qemunote(WriteCoreDumpFunction f, CPUState *cpu,
                             void *opaque);

/**
 * CPUDumpFlags:
 * @CPU_DUMP_CODE:
 * @CPU_DUMP_FPU: dump FPU register state, not just integer
 * @CPU_DUMP_CCOP: dump info about TCG QEMU's condition code optimization state
 */
enum CPUDumpFlags {
    CPU_DUMP_CODE = 0x00010000,
    CPU_DUMP_FPU  = 0x00020000,
    CPU_DUMP_CCOP = 0x00040000,
};

/**
 * cpu_dump_state:
 * @cpu: The CPU whose state is to be dumped.
 * @f: File to dump to.
 * @cpu_fprintf: Function to dump with.
 * @flags: Flags what to dump.
 *
 * Dumps CPU state.
 */
void cpu_dump_state(CPUState *cpu, FILE *f, fprintf_function cpu_fprintf,
                    int flags);

/**
 * cpu_dump_statistics:
 * @cpu: The CPU whose state is to be dumped.
 * @f: File to dump to.
 * @cpu_fprintf: Function to dump with.
 * @flags: Flags what to dump.
 *
 * Dumps CPU statistics.
 */
void cpu_dump_statistics(CPUState *cpu, FILE *f, fprintf_function cpu_fprintf,
                         int flags);

#ifndef CONFIG_USER_ONLY
/**
 * cpu_get_phys_page_debug:
 * @cpu: The CPU to obtain the physical page address for.
 * @addr: The virtual address.
 *
 * Obtains the physical page corresponding to a virtual one.
 * Use it only for debugging because no protection checks are done.
 *
 * Returns: Corresponding physical page address or -1 if no page found.
 */
static inline hwaddr cpu_get_phys_page_debug(CPUState *cpu, vaddr addr)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    return cc->get_phys_page_debug(cpu, addr);
}
#endif

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
 * async_run_on_cpu:
 * @cpu: The vCPU to run on.
 * @func: The function to be executed.
 * @data: Data to pass to the function.
 *
 * Schedules the function @func for execution on the vCPU @cpu asynchronously.
 */
void async_run_on_cpu(CPUState *cpu, void (*func)(void *data), void *data);

/**
 * qemu_for_each_cpu:
 * @func: The function to be executed.
 * @data: Data to pass to the function.
 *
 * Executes @func for each CPU.
 */
void qemu_for_each_cpu(void (*func)(CPUState *cpu, void *data), void *data);

/**
 * qemu_get_cpu:
 * @index: The CPUState@cpu_index value of the CPU to obtain.
 *
 * Gets a CPU matching @index.
 *
 * Returns: The CPU or %NULL if there is no matching CPU.
 */
CPUState *qemu_get_cpu(int index);

/**
 * cpu_exists:
 * @id: Guest-exposed CPU ID to lookup.
 *
 * Search for CPU with specified ID.
 *
 * Returns: %true - CPU is found, %false - CPU isn't found.
 */
bool cpu_exists(int64_t id);

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

#ifndef CONFIG_USER_ONLY

static inline void cpu_unassigned_access(CPUState *cpu, hwaddr addr,
                                         bool is_write, bool is_exec,
                                         int opaque, unsigned size)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    if (cc->do_unassigned_access) {
        cc->do_unassigned_access(cpu, addr, is_write, is_exec, opaque, size);
    }
}

#endif

/**
 * cpu_reset_interrupt:
 * @cpu: The CPU to clear the interrupt on.
 * @mask: The interrupt mask to clear.
 *
 * Resets interrupts on the vCPU @cpu.
 */
void cpu_reset_interrupt(CPUState *cpu, int mask);

/**
 * cpu_exit:
 * @cpu: The CPU to exit.
 *
 * Requests the CPU @cpu to exit execution.
 */
void cpu_exit(CPUState *cpu);

/**
 * cpu_resume:
 * @cpu: The CPU to resume.
 *
 * Resumes CPU, i.e. puts CPU into runnable state.
 */
void cpu_resume(CPUState *cpu);

/**
 * qemu_init_vcpu:
 * @cpu: The vCPU to initialize.
 *
 * Initializes a vCPU.
 */
void qemu_init_vcpu(CPUState *cpu);

#define SSTEP_ENABLE  0x1  /* Enable simulated HW single stepping */
#define SSTEP_NOIRQ   0x2  /* Do not use IRQ while single stepping */
#define SSTEP_NOTIMER 0x4  /* Do not Timers while single stepping */

/**
 * cpu_single_step:
 * @cpu: CPU to the flags for.
 * @enabled: Flags to enable.
 *
 * Enables or disables single-stepping for @cpu.
 */
void cpu_single_step(CPUState *cpu, int enabled);

#ifdef CONFIG_SOFTMMU
extern const struct VMStateDescription vmstate_cpu_common;
#else
#define vmstate_cpu_common vmstate_dummy
#endif

#define VMSTATE_CPU() {                                                     \
    .name = "parent_obj",                                                   \
    .size = sizeof(CPUState),                                               \
    .vmsd = &vmstate_cpu_common,                                            \
    .flags = VMS_STRUCT,                                                    \
    .offset = 0,                                                            \
}

#endif
