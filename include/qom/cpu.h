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

#include "hw/qdev-core.h"
#include "disas/bfd.h"
#include "exec/hwaddr.h"
#include "exec/memattrs.h"
#include "qemu/queue.h"
#include "qemu/thread.h"

typedef int (*WriteCoreDumpFunction)(const void *buf, size_t size,
                                     void *opaque);

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

/* Since this macro is used a lot in hot code paths and in conjunction with
 * FooCPU *foo_env_get_cpu(), we deviate from usual QOM practice by using
 * an unchecked cast.
 */
#define CPU(obj) ((CPUState *)(obj))

#define CPU_CLASS(class) OBJECT_CLASS_CHECK(CPUClass, (class), TYPE_CPU)
#define CPU_GET_CLASS(obj) OBJECT_GET_CLASS(CPUClass, (obj), TYPE_CPU)

typedef struct CPUWatchpoint CPUWatchpoint;

typedef void (*CPUUnassignedAccess)(CPUState *cpu, hwaddr addr,
                                    bool is_write, bool is_exec, int opaque,
                                    unsigned size);

struct TranslationBlock;

/**
 * CPUClass:
 * @class_by_name: Callback to map -cpu command line model name to an
 * instantiatable CPU type.
 * @parse_features: Callback to parse command line arguments.
 * @reset: Callback to reset the #CPUState to its initial state.
 * @reset_dump_flags: #CPUDumpFlags to use for reset logging.
 * @has_work: Callback for checking if there is work to do.
 * @do_interrupt: Callback for interrupt handling.
 * @do_unassigned_access: Callback for unassigned access handling.
 * @do_unaligned_access: Callback for unaligned access handling, if
 * the target defines #ALIGNED_ONLY.
 * @virtio_is_big_endian: Callback to return %true if a CPU which supports
 * runtime configurable endianness is currently big-endian. Non-configurable
 * CPUs can use the default implementation of this method. This method should
 * not be used by any callers other than the pre-1.0 virtio devices.
 * @memory_rw_debug: Callback for GDB memory access.
 * @dump_state: Callback for dumping state.
 * @dump_statistics: Callback for dumping statistics.
 * @get_arch_id: Callback for getting architecture-dependent CPU ID.
 * @get_paging_enabled: Callback for inquiring whether paging is enabled.
 * @get_memory_mapping: Callback for obtaining the memory mappings.
 * @set_pc: Callback for setting the Program Counter register.
 * @synchronize_from_tb: Callback for synchronizing state from a TCG
 * #TranslationBlock.
 * @handle_mmu_fault: Callback for handling an MMU fault.
 * @get_phys_page_debug: Callback for obtaining a physical address.
 * @get_phys_page_attrs_debug: Callback for obtaining a physical address and the
 *       associated memory transaction attributes to use for the access.
 *       CPUs which use memory transaction attributes should implement this
 *       instead of get_phys_page_debug.
 * @asidx_from_attrs: Callback to return the CPU AddressSpace to use for
 *       a memory access with the specified memory transaction attributes.
 * @gdb_read_register: Callback for letting GDB read a register.
 * @gdb_write_register: Callback for letting GDB write a register.
 * @debug_check_watchpoint: Callback: return true if the architectural
 *       watchpoint whose address has matched should really fire.
 * @debug_excp_handler: Callback for handling debug exceptions.
 * @write_elf64_note: Callback for writing a CPU-specific ELF note to a
 * 64-bit VM coredump.
 * @write_elf32_qemunote: Callback for writing a CPU- and QEMU-specific ELF
 * note to a 32-bit VM coredump.
 * @write_elf32_note: Callback for writing a CPU-specific ELF note to a
 * 32-bit VM coredump.
 * @write_elf32_qemunote: Callback for writing a CPU- and QEMU-specific ELF
 * note to a 32-bit VM coredump.
 * @vmsd: State description for migration.
 * @gdb_num_core_regs: Number of core registers accessible to GDB.
 * @gdb_core_xml_file: File name for core registers GDB XML description.
 * @gdb_stop_before_watchpoint: Indicates whether GDB expects the CPU to stop
 *           before the insn which triggers a watchpoint rather than after it.
 * @gdb_arch_name: Optional callback that returns the architecture name known
 * to GDB. The caller must free the returned string with g_free.
 * @cpu_exec_enter: Callback for cpu_exec preparation.
 * @cpu_exec_exit: Callback for cpu_exec cleanup.
 * @cpu_exec_interrupt: Callback for processing interrupts in cpu_exec.
 * @disas_set_info: Setup architecture specific components of disassembly info
 *
 * Represents a CPU family or model.
 */
typedef struct CPUClass {
    /*< private >*/
    DeviceClass parent_class;
    /*< public >*/

    ObjectClass *(*class_by_name)(const char *cpu_model);
    void (*parse_features)(CPUState *cpu, char *str, Error **errp);

    void (*reset)(CPUState *cpu);
    int reset_dump_flags;
    bool (*has_work)(CPUState *cpu);
    void (*do_interrupt)(CPUState *cpu);
    CPUUnassignedAccess do_unassigned_access;
    void (*do_unaligned_access)(CPUState *cpu, vaddr addr,
                                int is_write, int is_user, uintptr_t retaddr);
    bool (*virtio_is_big_endian)(CPUState *cpu);
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
    int (*handle_mmu_fault)(CPUState *cpu, vaddr address, int rw,
                            int mmu_index);
    hwaddr (*get_phys_page_debug)(CPUState *cpu, vaddr addr);
    hwaddr (*get_phys_page_attrs_debug)(CPUState *cpu, vaddr addr,
                                        MemTxAttrs *attrs);
    int (*asidx_from_attrs)(CPUState *cpu, MemTxAttrs attrs);
    int (*gdb_read_register)(CPUState *cpu, uint8_t *buf, int reg);
    int (*gdb_write_register)(CPUState *cpu, uint8_t *buf, int reg);
    bool (*debug_check_watchpoint)(CPUState *cpu, CPUWatchpoint *wp);
    void (*debug_excp_handler)(CPUState *cpu);

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
    gchar * (*gdb_arch_name)(CPUState *cpu);
    bool gdb_stop_before_watchpoint;

    void (*cpu_exec_enter)(CPUState *cpu);
    void (*cpu_exec_exit)(CPUState *cpu);
    bool (*cpu_exec_interrupt)(CPUState *cpu, int interrupt_request);

    void (*disas_set_info)(CPUState *cpu, disassemble_info *info);
} CPUClass;

#ifdef HOST_WORDS_BIGENDIAN
typedef struct icount_decr_u16 {
    uint16_t high;
    uint16_t low;
} icount_decr_u16;
#else
typedef struct icount_decr_u16 {
    uint16_t low;
    uint16_t high;
} icount_decr_u16;
#endif

typedef struct CPUBreakpoint {
    vaddr pc;
    int flags; /* BP_* */
    QTAILQ_ENTRY(CPUBreakpoint) entry;
} CPUBreakpoint;

struct CPUWatchpoint {
    vaddr vaddr;
    vaddr len;
    vaddr hitaddr;
    MemTxAttrs hitattrs;
    int flags; /* BP_* */
    QTAILQ_ENTRY(CPUWatchpoint) entry;
};

struct KVMState;
struct kvm_run;

#define TB_JMP_CACHE_BITS 12
#define TB_JMP_CACHE_SIZE (1 << TB_JMP_CACHE_BITS)

/* work queue */
struct qemu_work_item {
    struct qemu_work_item *next;
    void (*func)(void *data);
    void *data;
    int done;
    bool free;
};

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
 * @crash_occurred: Indicates the OS reported a crash (panic) for this CPU
 * @tcg_exit_req: Set to force TCG to stop executing linked TBs for this
 *           CPU and return to its top level loop.
 * @tb_flushed: Indicates the translation buffer has been flushed.
 * @singlestep_enabled: Flags for single-stepping.
 * @icount_extra: Instructions until next timer event.
 * @icount_decr: Number of cycles left, with interrupt flag in high bit.
 * This allows a single read-compare-cbranch-write sequence to test
 * for both decrementer underflow and exceptions.
 * @can_do_io: Nonzero if memory-mapped IO is safe. Deterministic execution
 * requires that IO only be performed on the last instruction of a TB
 * so that interrupts take effect immediately.
 * @cpu_ases: Pointer to array of CPUAddressSpaces (which define the
 *            AddressSpaces this CPU has)
 * @num_ases: number of CPUAddressSpaces in @cpu_ases
 * @as: Pointer to the first AddressSpace, for the convenience of targets which
 *      only have a single AddressSpace
 * @env_ptr: Pointer to subclass-specific CPUArchState field.
 * @gdb_regs: Additional GDB registers.
 * @gdb_num_regs: Number of total registers accessible to GDB.
 * @gdb_num_g_regs: Number of registers in GDB 'g' packets.
 * @next_cpu: Next CPU sharing TB cache.
 * @opaque: User data.
 * @mem_io_pc: Host Program Counter at which the memory was accessed.
 * @mem_io_vaddr: Target virtual address at which the memory was accessed.
 * @kvm_fd: vCPU file descriptor for KVM.
 * @work_mutex: Lock to prevent multiple access to queued_work_*.
 * @queued_work_first: First asynchronous work pending.
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
    bool thread_kicked;
    bool created;
    bool stop;
    bool stopped;
    bool crash_occurred;
    bool exit_request;
    bool tb_flushed;
    uint32_t interrupt_request;
    int singlestep_enabled;
    int64_t icount_extra;
    sigjmp_buf jmp_env;

    QemuMutex work_mutex;
    struct qemu_work_item *queued_work_first, *queued_work_last;

    CPUAddressSpace *cpu_ases;
    int num_ases;
    AddressSpace *as;
    MemoryRegion *memory;

    void *env_ptr; /* CPUArchState */
    struct TranslationBlock *tb_jmp_cache[TB_JMP_CACHE_SIZE];
    struct GDBRegisterState *gdb_regs;
    int gdb_num_regs;
    int gdb_num_g_regs;
    QTAILQ_ENTRY(CPUState) node;

    /* ice debug support */
    QTAILQ_HEAD(breakpoints_head, CPUBreakpoint) breakpoints;

    QTAILQ_HEAD(watchpoints_head, CPUWatchpoint) watchpoints;
    CPUWatchpoint *watchpoint_hit;

    void *opaque;

    /* In order to avoid passing too many arguments to the MMIO helpers,
     * we store some rarely used information in the CPU context.
     */
    uintptr_t mem_io_pc;
    vaddr mem_io_vaddr;

    int kvm_fd;
    bool kvm_vcpu_dirty;
    struct KVMState *kvm_state;
    struct kvm_run *kvm_run;

    /* TODO Move common fields from CPUArchState here. */
    int cpu_index; /* used by alpha TCG */
    uint32_t halted; /* used by alpha, cris, ppc TCG */
    union {
        uint32_t u32;
        icount_decr_u16 u16;
    } icount_decr;
    uint32_t can_do_io;
    int32_t exception_index; /* used by m68k TCG */

    /* Used to keep track of an outstanding cpu throttle thread for migration
     * autoconverge
     */
    bool throttle_thread_scheduled;

    /* Note that this is accessed at the start of every TB via a negative
       offset from AREG0.  Leave this field at the end so as to make the
       (absolute value) offset as small as possible.  This reduces code
       size, especially for hosts without large memory offsets.  */
    uint32_t tcg_exit_req;
};

QTAILQ_HEAD(CPUTailQ, CPUState);
extern struct CPUTailQ cpus;
#define CPU_NEXT(cpu) QTAILQ_NEXT(cpu, node)
#define CPU_FOREACH(cpu) QTAILQ_FOREACH(cpu, &cpus, node)
#define CPU_FOREACH_SAFE(cpu, next_cpu) \
    QTAILQ_FOREACH_SAFE(cpu, &cpus, node, next_cpu)
#define CPU_FOREACH_REVERSE(cpu) \
    QTAILQ_FOREACH_REVERSE(cpu, &cpus, CPUTailQ, node)
#define first_cpu QTAILQ_FIRST(&cpus)

extern __thread CPUState *current_cpu;

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
 * cpu_get_phys_page_attrs_debug:
 * @cpu: The CPU to obtain the physical page address for.
 * @addr: The virtual address.
 * @attrs: Updated on return with the memory transaction attributes to use
 *         for this access.
 *
 * Obtains the physical page corresponding to a virtual one, together
 * with the corresponding memory transaction attributes to use for the access.
 * Use it only for debugging because no protection checks are done.
 *
 * Returns: Corresponding physical page address or -1 if no page found.
 */
static inline hwaddr cpu_get_phys_page_attrs_debug(CPUState *cpu, vaddr addr,
                                                   MemTxAttrs *attrs)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    if (cc->get_phys_page_attrs_debug) {
        return cc->get_phys_page_attrs_debug(cpu, addr, attrs);
    }
    /* Fallback for CPUs which don't implement the _attrs_ hook */
    *attrs = MEMTXATTRS_UNSPECIFIED;
    return cc->get_phys_page_debug(cpu, addr);
}

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
    MemTxAttrs attrs = {};

    return cpu_get_phys_page_attrs_debug(cpu, addr, &attrs);
}

/** cpu_asidx_from_attrs:
 * @cpu: CPU
 * @attrs: memory transaction attributes
 *
 * Returns the address space index specifying the CPU AddressSpace
 * to use for a memory access with the given transaction attributes.
 */
static inline int cpu_asidx_from_attrs(CPUState *cpu, MemTxAttrs attrs)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    if (cc->asidx_from_attrs) {
        return cc->asidx_from_attrs(cpu, attrs);
    }
    return 0;
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
 * cpu_generic_init:
 * @typename: The CPU base type.
 * @cpu_model: The model string including optional parameters.
 *
 * Instantiates a CPU, processes optional parameters and realizes the CPU.
 *
 * Returns: A #CPUState or %NULL if an error occurred.
 */
CPUState *cpu_generic_init(const char *typename, const char *cpu_model);

/**
 * cpu_has_work:
 * @cpu: The vCPU to check.
 *
 * Checks whether the CPU has work to do.
 *
 * Returns: %true if the CPU has work, %false otherwise.
 */
static inline bool cpu_has_work(CPUState *cpu)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    g_assert(cc->has_work);
    return cc->has_work(cpu);
}

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

/**
 * cpu_throttle_set:
 * @new_throttle_pct: Percent of sleep time. Valid range is 1 to 99.
 *
 * Throttles all vcpus by forcing them to sleep for the given percentage of
 * time. A throttle_percentage of 25 corresponds to a 75% duty cycle roughly.
 * (example: 10ms sleep for every 30ms awake).
 *
 * cpu_throttle_set can be called as needed to adjust new_throttle_pct.
 * Once the throttling starts, it will remain in effect until cpu_throttle_stop
 * is called.
 */
void cpu_throttle_set(int new_throttle_pct);

/**
 * cpu_throttle_stop:
 *
 * Stops the vcpu throttling started by cpu_throttle_set.
 */
void cpu_throttle_stop(void);

/**
 * cpu_throttle_active:
 *
 * Returns: %true if the vcpus are currently being throttled, %false otherwise.
 */
bool cpu_throttle_active(void);

/**
 * cpu_throttle_get_percentage:
 *
 * Returns the vcpu throttle percentage. See cpu_throttle_set for details.
 *
 * Returns: The throttle percentage in range 1 to 99.
 */
int cpu_throttle_get_percentage(void);

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

#ifdef CONFIG_SOFTMMU
static inline void cpu_unassigned_access(CPUState *cpu, hwaddr addr,
                                         bool is_write, bool is_exec,
                                         int opaque, unsigned size)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    if (cc->do_unassigned_access) {
        cc->do_unassigned_access(cpu, addr, is_write, is_exec, opaque, size);
    }
}

static inline void cpu_unaligned_access(CPUState *cpu, vaddr addr,
                                        int is_write, int is_user,
                                        uintptr_t retaddr)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    cc->do_unaligned_access(cpu, addr, is_write, is_user, retaddr);
}
#endif

/**
 * cpu_set_pc:
 * @cpu: The CPU to set the program counter for.
 * @addr: Program counter value.
 *
 * Sets the program counter for a CPU.
 */
static inline void cpu_set_pc(CPUState *cpu, vaddr addr)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    cc->set_pc(cpu, addr);
}

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

/* Breakpoint/watchpoint flags */
#define BP_MEM_READ           0x01
#define BP_MEM_WRITE          0x02
#define BP_MEM_ACCESS         (BP_MEM_READ | BP_MEM_WRITE)
#define BP_STOP_BEFORE_ACCESS 0x04
/* 0x08 currently unused */
#define BP_GDB                0x10
#define BP_CPU                0x20
#define BP_ANY                (BP_GDB | BP_CPU)
#define BP_WATCHPOINT_HIT_READ 0x40
#define BP_WATCHPOINT_HIT_WRITE 0x80
#define BP_WATCHPOINT_HIT (BP_WATCHPOINT_HIT_READ | BP_WATCHPOINT_HIT_WRITE)

int cpu_breakpoint_insert(CPUState *cpu, vaddr pc, int flags,
                          CPUBreakpoint **breakpoint);
int cpu_breakpoint_remove(CPUState *cpu, vaddr pc, int flags);
void cpu_breakpoint_remove_by_ref(CPUState *cpu, CPUBreakpoint *breakpoint);
void cpu_breakpoint_remove_all(CPUState *cpu, int mask);

/* Return true if PC matches an installed breakpoint.  */
static inline bool cpu_breakpoint_test(CPUState *cpu, vaddr pc, int mask)
{
    CPUBreakpoint *bp;

    if (unlikely(!QTAILQ_EMPTY(&cpu->breakpoints))) {
        QTAILQ_FOREACH(bp, &cpu->breakpoints, entry) {
            if (bp->pc == pc && (bp->flags & mask)) {
                return true;
            }
        }
    }
    return false;
}

int cpu_watchpoint_insert(CPUState *cpu, vaddr addr, vaddr len,
                          int flags, CPUWatchpoint **watchpoint);
int cpu_watchpoint_remove(CPUState *cpu, vaddr addr,
                          vaddr len, int flags);
void cpu_watchpoint_remove_by_ref(CPUState *cpu, CPUWatchpoint *watchpoint);
void cpu_watchpoint_remove_all(CPUState *cpu, int mask);

/**
 * cpu_get_address_space:
 * @cpu: CPU to get address space from
 * @asidx: index identifying which address space to get
 *
 * Return the requested address space of this CPU. @asidx
 * specifies which address space to read.
 */
AddressSpace *cpu_get_address_space(CPUState *cpu, int asidx);

void QEMU_NORETURN cpu_abort(CPUState *cpu, const char *fmt, ...)
    GCC_FMT_ATTR(2, 3);
void cpu_exec_exit(CPUState *cpu);

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
