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
#include "disas/dis-asm.h"
#include "exec/hwaddr.h"
#include "exec/vaddr.h"
#include "exec/memattrs.h"
#include "exec/tlb-common.h"
#include "qapi/qapi-types-run-state.h"
#include "qemu/bitmap.h"
#include "qemu/rcu_queue.h"
#include "qemu/queue.h"
#include "qemu/thread.h"
#include "qom/object.h"

typedef int (*WriteCoreDumpFunction)(const void *buf, size_t size,
                                     void *opaque);

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

/*
 * The class checkers bring in CPU_GET_CLASS() which is potentially
 * expensive given the eventual call to
 * object_class_dynamic_cast_assert(). Because of this the CPUState
 * has a cached value for the class in cs->cc which is set up in
 * cpu_exec_realizefn() for use in hot code paths.
 */
typedef struct CPUClass CPUClass;
DECLARE_CLASS_CHECKERS(CPUClass, CPU,
                       TYPE_CPU)

/**
 * OBJECT_DECLARE_CPU_TYPE:
 * @CpuInstanceType: instance struct name
 * @CpuClassType: class struct name
 * @CPU_MODULE_OBJ_NAME: the CPU name in uppercase with underscore separators
 *
 * This macro is typically used in "cpu-qom.h" header file, and will:
 *
 *   - create the typedefs for the CPU object and class structs
 *   - register the type for use with g_autoptr
 *   - provide three standard type cast functions
 *
 * The object struct and class struct need to be declared manually.
 */
#define OBJECT_DECLARE_CPU_TYPE(CpuInstanceType, CpuClassType, CPU_MODULE_OBJ_NAME) \
    typedef struct ArchCPU CpuInstanceType; \
    OBJECT_DECLARE_TYPE(ArchCPU, CpuClassType, CPU_MODULE_OBJ_NAME);

typedef enum MMUAccessType {
    MMU_DATA_LOAD  = 0,
    MMU_DATA_STORE = 1,
    MMU_INST_FETCH = 2
#define MMU_ACCESS_COUNT 3
} MMUAccessType;

typedef struct CPUWatchpoint CPUWatchpoint;

/* see accel-cpu.h */
struct AccelCPUClass;

/* see sysemu-cpu-ops.h */
struct SysemuCPUOps;

/**
 * CPUClass:
 * @class_by_name: Callback to map -cpu command line model name to an
 *                 instantiatable CPU type.
 * @parse_features: Callback to parse command line arguments.
 * @reset_dump_flags: #CPUDumpFlags to use for reset logging.
 * @has_work: Callback for checking if there is work to do.
 * @mmu_index: Callback for choosing softmmu mmu index;
 *       may be used internally by memory_rw_debug without TCG.
 * @memory_rw_debug: Callback for GDB memory access.
 * @dump_state: Callback for dumping state.
 * @query_cpu_fast:
 *       Fill in target specific information for the "query-cpus-fast"
 *       QAPI call.
 * @get_arch_id: Callback for getting architecture-dependent CPU ID.
 * @set_pc: Callback for setting the Program Counter register. This
 *       should have the semantics used by the target architecture when
 *       setting the PC from a source such as an ELF file entry point;
 *       for example on Arm it will also set the Thumb mode bit based
 *       on the least significant bit of the new PC value.
 *       If the target behaviour here is anything other than "set
 *       the PC register to the value passed in" then the target must
 *       also implement the synchronize_from_tb hook.
 * @get_pc: Callback for getting the Program Counter register.
 *       As above, with the semantics of the target architecture.
 * @gdb_read_register: Callback for letting GDB read a register.
 * @gdb_write_register: Callback for letting GDB write a register.
 * @gdb_adjust_breakpoint: Callback for adjusting the address of a
 *       breakpoint.  Used by AVR to handle a gdb mis-feature with
 *       its Harvard architecture split code and data.
 * @gdb_num_core_regs: Number of core registers accessible to GDB or 0 to infer
 *                     from @gdb_core_xml_file.
 * @gdb_core_xml_file: File name for core registers GDB XML description.
 * @gdb_stop_before_watchpoint: Indicates whether GDB expects the CPU to stop
 *           before the insn which triggers a watchpoint rather than after it.
 * @gdb_arch_name: Optional callback that returns the architecture name known
 * to GDB. The caller must free the returned string with g_free.
 * @disas_set_info: Setup architecture specific components of disassembly info
 * @adjust_watchpoint_address: Perform a target-specific adjustment to an
 * address before attempting to match it against watchpoints.
 * @deprecation_note: If this CPUClass is deprecated, this field provides
 *                    related information.
 *
 * Represents a CPU family or model.
 */
struct CPUClass {
    /*< private >*/
    DeviceClass parent_class;
    /*< public >*/

    ObjectClass *(*class_by_name)(const char *cpu_model);
    void (*parse_features)(const char *typename, char *str, Error **errp);

    bool (*has_work)(CPUState *cpu);
    int (*mmu_index)(CPUState *cpu, bool ifetch);
    int (*memory_rw_debug)(CPUState *cpu, vaddr addr,
                           uint8_t *buf, int len, bool is_write);
    void (*dump_state)(CPUState *cpu, FILE *, int flags);
    void (*query_cpu_fast)(CPUState *cpu, CpuInfoFast *value);
    int64_t (*get_arch_id)(CPUState *cpu);
    void (*set_pc)(CPUState *cpu, vaddr value);
    vaddr (*get_pc)(CPUState *cpu);
    int (*gdb_read_register)(CPUState *cpu, GByteArray *buf, int reg);
    int (*gdb_write_register)(CPUState *cpu, uint8_t *buf, int reg);
    vaddr (*gdb_adjust_breakpoint)(CPUState *cpu, vaddr addr);

    const char *gdb_core_xml_file;
    const gchar * (*gdb_arch_name)(CPUState *cpu);

    void (*disas_set_info)(CPUState *cpu, disassemble_info *info);

    const char *deprecation_note;
    struct AccelCPUClass *accel_cpu;

    /* when system emulation is not available, this pointer is NULL */
    const struct SysemuCPUOps *sysemu_ops;

    /* when TCG is not available, this pointer is NULL */
    const TCGCPUOps *tcg_ops;

    /*
     * if not NULL, this is called in order for the CPUClass to initialize
     * class data that depends on the accelerator, see accel/accel-common.c.
     */
    void (*init_accel_cpu)(struct AccelCPUClass *accel_cpu, CPUClass *cc);

    /*
     * Keep non-pointer data at the end to minimize holes.
     */
    int reset_dump_flags;
    int gdb_num_core_regs;
    bool gdb_stop_before_watchpoint;
};

/*
 * Fix the number of mmu modes to 16, which is also the maximum
 * supported by the softmmu tlb api.
 */
#define NB_MMU_MODES 16

/* Use a fully associative victim tlb of 8 entries. */
#define CPU_VTLB_SIZE 8

/*
 * The full TLB entry, which is not accessed by generated TCG code,
 * so the layout is not as critical as that of CPUTLBEntry. This is
 * also why we don't want to combine the two structs.
 */
typedef struct CPUTLBEntryFull {
    /*
     * @xlat_section contains:
     *  - in the lower TARGET_PAGE_BITS, a physical section number
     *  - with the lower TARGET_PAGE_BITS masked off, an offset which
     *    must be added to the virtual address to obtain:
     *     + the ram_addr_t of the target RAM (if the physical section
     *       number is PHYS_SECTION_NOTDIRTY or PHYS_SECTION_ROM)
     *     + the offset within the target MemoryRegion (otherwise)
     */
    hwaddr xlat_section;

    /*
     * @phys_addr contains the physical address in the address space
     * given by cpu_asidx_from_attrs(cpu, @attrs).
     */
    hwaddr phys_addr;

    /* @attrs contains the memory transaction attributes for the page. */
    MemTxAttrs attrs;

    /* @prot contains the complete protections for the page. */
    uint8_t prot;

    /* @lg_page_size contains the log2 of the page size. */
    uint8_t lg_page_size;

    /* Additional tlb flags requested by tlb_fill. */
    uint8_t tlb_fill_flags;

    /*
     * Additional tlb flags for use by the slow path. If non-zero,
     * the corresponding CPUTLBEntry comparator must have TLB_FORCE_SLOW.
     */
    uint8_t slow_flags[MMU_ACCESS_COUNT];

    /*
     * Allow target-specific additions to this structure.
     * This may be used to cache items from the guest cpu
     * page tables for later use by the implementation.
     */
    union {
        /*
         * Cache the attrs and shareability fields from the page table entry.
         *
         * For ARMMMUIdx_Stage2*, pte_attrs is the S2 descriptor bits [5:2].
         * Otherwise, pte_attrs is the same as the MAIR_EL1 8-bit format.
         * For shareability and guarded, as in the SH and GP fields respectively
         * of the VMSAv8-64 PTEs.
         */
        struct {
            uint8_t pte_attrs;
            uint8_t shareability;
            bool guarded;
        } arm;
    } extra;
} CPUTLBEntryFull;

/*
 * Data elements that are per MMU mode, minus the bits accessed by
 * the TCG fast path.
 */
typedef struct CPUTLBDesc {
    /*
     * Describe a region covering all of the large pages allocated
     * into the tlb.  When any page within this region is flushed,
     * we must flush the entire tlb.  The region is matched if
     * (addr & large_page_mask) == large_page_addr.
     */
    vaddr large_page_addr;
    vaddr large_page_mask;
    /* host time (in ns) at the beginning of the time window */
    int64_t window_begin_ns;
    /* maximum number of entries observed in the window */
    size_t window_max_entries;
    size_t n_used_entries;
    /* The next index to use in the tlb victim table.  */
    size_t vindex;
    /* The tlb victim table, in two parts.  */
    CPUTLBEntry vtable[CPU_VTLB_SIZE];
    CPUTLBEntryFull vfulltlb[CPU_VTLB_SIZE];
    CPUTLBEntryFull *fulltlb;
} CPUTLBDesc;

/*
 * Data elements that are shared between all MMU modes.
 */
typedef struct CPUTLBCommon {
    /* Serialize updates to f.table and d.vtable, and others as noted. */
    QemuSpin lock;
    /*
     * Within dirty, for each bit N, modifications have been made to
     * mmu_idx N since the last time that mmu_idx was flushed.
     * Protected by tlb_c.lock.
     */
    uint16_t dirty;
    /*
     * Statistics.  These are not lock protected, but are read and
     * written atomically.  This allows the monitor to print a snapshot
     * of the stats without interfering with the cpu.
     */
    size_t full_flush_count;
    size_t part_flush_count;
    size_t elide_flush_count;
} CPUTLBCommon;

/*
 * The entire softmmu tlb, for all MMU modes.
 * The meaning of each of the MMU modes is defined in the target code.
 * Since this is placed within CPUNegativeOffsetState, the smallest
 * negative offsets are at the end of the struct.
 */
typedef struct CPUTLB {
#ifdef CONFIG_TCG
    CPUTLBCommon c;
    CPUTLBDesc d[NB_MMU_MODES];
    CPUTLBDescFast f[NB_MMU_MODES];
#endif
} CPUTLB;

/*
 * Low 16 bits: number of cycles left, used only in icount mode.
 * High 16 bits: Set to -1 to force TCG to stop executing linked TBs
 * for this CPU and return to its top level loop (even in non-icount mode).
 * This allows a single read-compare-cbranch-write sequence to test
 * for both decrementer underflow and exceptions.
 */
typedef union IcountDecr {
    uint32_t u32;
    struct {
#if HOST_BIG_ENDIAN
        uint16_t high;
        uint16_t low;
#else
        uint16_t low;
        uint16_t high;
#endif
    } u16;
} IcountDecr;

/*
 * Elements of CPUState most efficiently accessed from CPUArchState,
 * via small negative offsets.
 */
typedef struct CPUNegativeOffsetState {
    CPUTLB tlb;
    IcountDecr icount_decr;
    bool can_do_io;
} CPUNegativeOffsetState;

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

/* work queue */

/* The union type allows passing of 64 bit target pointers on 32 bit
 * hosts in a single parameter
 */
typedef union {
    int           host_int;
    unsigned long host_ulong;
    void         *host_ptr;
    vaddr         target_ptr;
} run_on_cpu_data;

#define RUN_ON_CPU_HOST_PTR(p)    ((run_on_cpu_data){.host_ptr = (p)})
#define RUN_ON_CPU_HOST_INT(i)    ((run_on_cpu_data){.host_int = (i)})
#define RUN_ON_CPU_HOST_ULONG(ul) ((run_on_cpu_data){.host_ulong = (ul)})
#define RUN_ON_CPU_TARGET_PTR(v)  ((run_on_cpu_data){.target_ptr = (v)})
#define RUN_ON_CPU_NULL           RUN_ON_CPU_HOST_PTR(NULL)

typedef void (*run_on_cpu_func)(CPUState *cpu, run_on_cpu_data data);

struct qemu_work_item;

#define CPU_UNSET_NUMA_NODE_ID -1

/**
 * CPUState:
 * @cpu_index: CPU index (informative).
 * @cluster_index: Identifies which cluster this CPU is in.
 *   For boards which don't define clusters or for "loose" CPUs not assigned
 *   to a cluster this will be UNASSIGNED_CLUSTER_INDEX; otherwise it will
 *   be the same as the cluster-id property of the CPU object's TYPE_CPU_CLUSTER
 *   QOM parent.
 *   Under TCG this value is propagated to @tcg_cflags.
 *   See TranslationBlock::TCG CF_CLUSTER_MASK.
 * @tcg_cflags: Pre-computed cflags for this cpu.
 * @nr_cores: Number of cores within this CPU package.
 * @nr_threads: Number of threads within this CPU core.
 * @running: #true if CPU is currently running (lockless).
 * @has_waiter: #true if a CPU is currently waiting for the cpu_exec_end;
 * valid under cpu_list_lock.
 * @created: Indicates whether the CPU thread has been successfully created.
 * @interrupt_request: Indicates a pending interrupt request.
 * @halted: Nonzero if the CPU is in suspended state.
 * @stop: Indicates a pending stop request.
 * @stopped: Indicates the CPU has been artificially stopped.
 * @unplug: Indicates a pending CPU unplug request.
 * @crash_occurred: Indicates the OS reported a crash (panic) for this CPU
 * @singlestep_enabled: Flags for single-stepping.
 * @icount_extra: Instructions until next timer event.
 * @neg.can_do_io: True if memory-mapped IO is allowed.
 * @cpu_ases: Pointer to array of CPUAddressSpaces (which define the
 *            AddressSpaces this CPU has)
 * @num_ases: number of CPUAddressSpaces in @cpu_ases
 * @as: Pointer to the first AddressSpace, for the convenience of targets which
 *      only have a single AddressSpace
 * @gdb_regs: Additional GDB registers.
 * @gdb_num_regs: Number of total registers accessible to GDB.
 * @gdb_num_g_regs: Number of registers in GDB 'g' packets.
 * @node: QTAILQ of CPUs sharing TB cache.
 * @opaque: User data.
 * @mem_io_pc: Host Program Counter at which the memory was accessed.
 * @accel: Pointer to accelerator specific state.
 * @kvm_fd: vCPU file descriptor for KVM.
 * @work_mutex: Lock to prevent multiple access to @work_list.
 * @work_list: List of pending asynchronous work.
 * @plugin_mem_cbs: active plugin memory callbacks
 * @plugin_state: per-CPU plugin state
 * @ignore_memory_transaction_failures: Cached copy of the MachineState
 *    flag of the same name: allows the board to suppress calling of the
 *    CPU do_transaction_failed hook function.
 * @kvm_dirty_gfns: Points to the KVM dirty ring for this CPU when KVM dirty
 *    ring is enabled.
 * @kvm_fetch_index: Keeps the index that we last fetched from the per-vCPU
 *    dirty ring structure.
 *
 * State of one CPU core or thread.
 *
 * Align, in order to match possible alignment required by CPUArchState,
 * and eliminate a hole between CPUState and CPUArchState within ArchCPU.
 */
struct CPUState {
    /*< private >*/
    DeviceState parent_obj;
    /* cache to avoid expensive CPU_GET_CLASS */
    CPUClass *cc;
    /*< public >*/

    int nr_cores;
    int nr_threads;

    struct QemuThread *thread;
#ifdef _WIN32
    QemuSemaphore sem;
#endif
    int thread_id;
    bool running, has_waiter;
    struct QemuCond *halt_cond;
    bool thread_kicked;
    bool created;
    bool stop;
    bool stopped;

    /* Should CPU start in powered-off state? */
    bool start_powered_off;

    bool unplug;
    bool crash_occurred;
    bool exit_request;
    int exclusive_context_count;
    uint32_t cflags_next_tb;
    /* updates protected by BQL */
    uint32_t interrupt_request;
    int singlestep_enabled;
    int64_t icount_budget;
    int64_t icount_extra;
    uint64_t random_seed;
    sigjmp_buf jmp_env;

    QemuMutex work_mutex;
    QSIMPLEQ_HEAD(, qemu_work_item) work_list;

    CPUAddressSpace *cpu_ases;
    int num_ases;
    AddressSpace *as;
    MemoryRegion *memory;

    CPUJumpCache *tb_jmp_cache;

    GArray *gdb_regs;
    int gdb_num_regs;
    int gdb_num_g_regs;
    QTAILQ_ENTRY(CPUState) node;

    /* ice debug support */
    QTAILQ_HEAD(, CPUBreakpoint) breakpoints;

    QTAILQ_HEAD(, CPUWatchpoint) watchpoints;
    CPUWatchpoint *watchpoint_hit;

    void *opaque;

    /* In order to avoid passing too many arguments to the MMIO helpers,
     * we store some rarely used information in the CPU context.
     */
    uintptr_t mem_io_pc;

    /* Only used in KVM */
    int kvm_fd;
    struct KVMState *kvm_state;
    struct kvm_run *kvm_run;
    struct kvm_dirty_gfn *kvm_dirty_gfns;
    uint32_t kvm_fetch_index;
    uint64_t dirty_pages;
    int kvm_vcpu_stats_fd;

    /* Use by accel-block: CPU is executing an ioctl() */
    QemuLockCnt in_ioctl_lock;

#ifdef CONFIG_PLUGIN
    /*
     * The callback pointer stays in the main CPUState as it is
     * accessed via TCG (see gen_empty_mem_helper).
     */
    GArray *plugin_mem_cbs;
    CPUPluginState *plugin_state;
#endif

    /* TODO Move common fields from CPUArchState here. */
    int cpu_index;
    int cluster_index;
    uint32_t tcg_cflags;
    uint32_t halted;
    int32_t exception_index;

    AccelCPUState *accel;
    /* shared by kvm and hvf */
    bool vcpu_dirty;

    /* Used to keep track of an outstanding cpu throttle thread for migration
     * autoconverge
     */
    bool throttle_thread_scheduled;

    /*
     * Sleep throttle_us_per_full microseconds once dirty ring is full
     * if dirty page rate limit is enabled.
     */
    int64_t throttle_us_per_full;

    bool ignore_memory_transaction_failures;

    /* Used for user-only emulation of prctl(PR_SET_UNALIGN). */
    bool prctl_unalign_sigbus;

    /* track IOMMUs whose translations we've cached in the TCG TLB */
    GArray *iommu_notifiers;

    /*
     * MUST BE LAST in order to minimize the displacement to CPUArchState.
     */
    char neg_align[-sizeof(CPUNegativeOffsetState) % 16] QEMU_ALIGNED(16);
    CPUNegativeOffsetState neg;
};

/* Validate placement of CPUNegativeOffsetState. */
QEMU_BUILD_BUG_ON(offsetof(CPUState, neg) !=
                  sizeof(CPUState) - sizeof(CPUNegativeOffsetState));

static inline CPUArchState *cpu_env(CPUState *cpu)
{
    /* We validate that CPUArchState follows CPUState in cpu-all.h. */
    return (CPUArchState *)(cpu + 1);
}

typedef QTAILQ_HEAD(CPUTailQ, CPUState) CPUTailQ;
extern CPUTailQ cpus_queue;

#define first_cpu        QTAILQ_FIRST_RCU(&cpus_queue)
#define CPU_NEXT(cpu)    QTAILQ_NEXT_RCU(cpu, node)
#define CPU_FOREACH(cpu) QTAILQ_FOREACH_RCU(cpu, &cpus_queue, node)
#define CPU_FOREACH_SAFE(cpu, next_cpu) \
    QTAILQ_FOREACH_SAFE_RCU(cpu, &cpus_queue, node, next_cpu)

extern __thread CPUState *current_cpu;

/**
 * qemu_tcg_mttcg_enabled:
 * Check whether we are running MultiThread TCG or not.
 *
 * Returns: %true if we are in MTTCG mode %false otherwise.
 */
extern bool mttcg_enabled;
#define qemu_tcg_mttcg_enabled() (mttcg_enabled)

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
 *
 * Returns: %true on success, %false otherwise.
 */
bool cpu_get_memory_mapping(CPUState *cpu, MemoryMappingList *list,
                            Error **errp);

#if !defined(CONFIG_USER_ONLY)

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
 * cpu_get_crash_info:
 * @cpu: The CPU to get crash information for
 *
 * Gets the previously saved crash information.
 * Caller is responsible for freeing the data.
 */
GuestPanicInformation *cpu_get_crash_info(CPUState *cpu);

#endif /* !CONFIG_USER_ONLY */

/**
 * CPUDumpFlags:
 * @CPU_DUMP_CODE:
 * @CPU_DUMP_FPU: dump FPU register state, not just integer
 * @CPU_DUMP_CCOP: dump info about TCG QEMU's condition code optimization state
 * @CPU_DUMP_VPU: dump VPU registers
 */
enum CPUDumpFlags {
    CPU_DUMP_CODE = 0x00010000,
    CPU_DUMP_FPU  = 0x00020000,
    CPU_DUMP_CCOP = 0x00040000,
    CPU_DUMP_VPU  = 0x00080000,
};

/**
 * cpu_dump_state:
 * @cpu: The CPU whose state is to be dumped.
 * @f: If non-null, dump to this stream, else to current print sink.
 *
 * Dumps CPU state.
 */
void cpu_dump_state(CPUState *cpu, FILE *f, int flags);

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
hwaddr cpu_get_phys_page_attrs_debug(CPUState *cpu, vaddr addr,
                                     MemTxAttrs *attrs);

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
hwaddr cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);

/** cpu_asidx_from_attrs:
 * @cpu: CPU
 * @attrs: memory transaction attributes
 *
 * Returns the address space index specifying the CPU AddressSpace
 * to use for a memory access with the given transaction attributes.
 */
int cpu_asidx_from_attrs(CPUState *cpu, MemTxAttrs attrs);

/**
 * cpu_virtio_is_big_endian:
 * @cpu: CPU

 * Returns %true if a CPU which supports runtime configurable endianness
 * is currently big-endian.
 */
bool cpu_virtio_is_big_endian(CPUState *cpu);

#endif /* CONFIG_USER_ONLY */

/**
 * cpu_list_add:
 * @cpu: The CPU to be added to the list of CPUs.
 */
void cpu_list_add(CPUState *cpu);

/**
 * cpu_list_remove:
 * @cpu: The CPU to be removed from the list of CPUs.
 */
void cpu_list_remove(CPUState *cpu);

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
 * Looks up a concrete CPU #ObjectClass matching name @cpu_model.
 *
 * Returns: A concrete #CPUClass or %NULL if no matching class is found
 *          or if the matching class is abstract.
 */
ObjectClass *cpu_class_by_name(const char *typename, const char *cpu_model);

/**
 * cpu_model_from_type:
 * @typename: The CPU type name
 *
 * Extract the CPU model name from the CPU type name. The
 * CPU type name is either the combination of the CPU model
 * name and suffix, or same to the CPU model name.
 *
 * Returns: CPU model name or NULL if the CPU class doesn't exist
 *          The user should g_free() the string once no longer needed.
 */
char *cpu_model_from_type(const char *typename);

/**
 * cpu_create:
 * @typename: The CPU type.
 *
 * Instantiates a CPU and realizes the CPU.
 *
 * Returns: A #CPUState or %NULL if an error occurred.
 */
CPUState *cpu_create(const char *typename);

/**
 * parse_cpu_option:
 * @cpu_option: The -cpu option including optional parameters.
 *
 * processes optional parameters and registers them as global properties
 *
 * Returns: type of CPU to create or prints error and terminates process
 *          if an error occurred.
 */
const char *parse_cpu_option(const char *cpu_option);

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
 * do_run_on_cpu:
 * @cpu: The vCPU to run on.
 * @func: The function to be executed.
 * @data: Data to pass to the function.
 * @mutex: Mutex to release while waiting for @func to run.
 *
 * Used internally in the implementation of run_on_cpu.
 */
void do_run_on_cpu(CPUState *cpu, run_on_cpu_func func, run_on_cpu_data data,
                   QemuMutex *mutex);

/**
 * run_on_cpu:
 * @cpu: The vCPU to run on.
 * @func: The function to be executed.
 * @data: Data to pass to the function.
 *
 * Schedules the function @func for execution on the vCPU @cpu.
 */
void run_on_cpu(CPUState *cpu, run_on_cpu_func func, run_on_cpu_data data);

/**
 * async_run_on_cpu:
 * @cpu: The vCPU to run on.
 * @func: The function to be executed.
 * @data: Data to pass to the function.
 *
 * Schedules the function @func for execution on the vCPU @cpu asynchronously.
 */
void async_run_on_cpu(CPUState *cpu, run_on_cpu_func func, run_on_cpu_data data);

/**
 * async_safe_run_on_cpu:
 * @cpu: The vCPU to run on.
 * @func: The function to be executed.
 * @data: Data to pass to the function.
 *
 * Schedules the function @func for execution on the vCPU @cpu asynchronously,
 * while all other vCPUs are sleeping.
 *
 * Unlike run_on_cpu and async_run_on_cpu, the function is run outside the
 * BQL.
 */
void async_safe_run_on_cpu(CPUState *cpu, run_on_cpu_func func, run_on_cpu_data data);

/**
 * cpu_in_exclusive_context()
 * @cpu: The vCPU to check
 *
 * Returns true if @cpu is an exclusive context, for example running
 * something which has previously been queued via async_safe_run_on_cpu().
 */
static inline bool cpu_in_exclusive_context(const CPUState *cpu)
{
    return cpu->exclusive_context_count;
}

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
 * cpu_by_arch_id:
 * @id: Guest-exposed CPU ID of the CPU to obtain.
 *
 * Get a CPU with matching @id.
 *
 * Returns: The CPU or %NULL if there is no matching CPU.
 */
CPUState *cpu_by_arch_id(int64_t id);

/**
 * cpu_interrupt:
 * @cpu: The CPU to set an interrupt on.
 * @mask: The interrupts to set.
 *
 * Invokes the interrupt handler.
 */

void cpu_interrupt(CPUState *cpu, int mask);

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
 * cpu_remove_sync:
 * @cpu: The CPU to remove.
 *
 * Requests the CPU to be removed and waits till it is removed.
 */
void cpu_remove_sync(CPUState *cpu);

/**
 * process_queued_cpu_work() - process all items on CPU work queue
 * @cpu: The CPU which work queue to process.
 */
void process_queued_cpu_work(CPUState *cpu);

/**
 * cpu_exec_start:
 * @cpu: The CPU for the current thread.
 *
 * Record that a CPU has started execution and can be interrupted with
 * cpu_exit.
 */
void cpu_exec_start(CPUState *cpu);

/**
 * cpu_exec_end:
 * @cpu: The CPU for the current thread.
 *
 * Record that a CPU has stopped execution and exclusive sections
 * can be executed without interrupting it.
 */
void cpu_exec_end(CPUState *cpu);

/**
 * start_exclusive:
 *
 * Wait for a concurrent exclusive section to end, and then start
 * a section of work that is run while other CPUs are not running
 * between cpu_exec_start and cpu_exec_end.  CPUs that are running
 * cpu_exec are exited immediately.  CPUs that call cpu_exec_start
 * during the exclusive section go to sleep until this CPU calls
 * end_exclusive.
 */
void start_exclusive(void);

/**
 * end_exclusive:
 *
 * Concludes an exclusive execution section started by start_exclusive.
 */
void end_exclusive(void);

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
#define BP_HIT_SHIFT          6
#define BP_WATCHPOINT_HIT_READ  (BP_MEM_READ << BP_HIT_SHIFT)
#define BP_WATCHPOINT_HIT_WRITE (BP_MEM_WRITE << BP_HIT_SHIFT)
#define BP_WATCHPOINT_HIT       (BP_MEM_ACCESS << BP_HIT_SHIFT)

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

#if defined(CONFIG_USER_ONLY)
static inline int cpu_watchpoint_insert(CPUState *cpu, vaddr addr, vaddr len,
                                        int flags, CPUWatchpoint **watchpoint)
{
    return -ENOSYS;
}

static inline int cpu_watchpoint_remove(CPUState *cpu, vaddr addr,
                                        vaddr len, int flags)
{
    return -ENOSYS;
}

static inline void cpu_watchpoint_remove_by_ref(CPUState *cpu,
                                                CPUWatchpoint *wp)
{
}

static inline void cpu_watchpoint_remove_all(CPUState *cpu, int mask)
{
}
#else
int cpu_watchpoint_insert(CPUState *cpu, vaddr addr, vaddr len,
                          int flags, CPUWatchpoint **watchpoint);
int cpu_watchpoint_remove(CPUState *cpu, vaddr addr,
                          vaddr len, int flags);
void cpu_watchpoint_remove_by_ref(CPUState *cpu, CPUWatchpoint *watchpoint);
void cpu_watchpoint_remove_all(CPUState *cpu, int mask);
#endif

/**
 * cpu_plugin_mem_cbs_enabled() - are plugin memory callbacks enabled?
 * @cs: CPUState pointer
 *
 * The memory callbacks are installed if a plugin has instrumented an
 * instruction for memory. This can be useful to know if you want to
 * force a slow path for a series of memory accesses.
 */
static inline bool cpu_plugin_mem_cbs_enabled(const CPUState *cpu)
{
#ifdef CONFIG_PLUGIN
    return !!cpu->plugin_mem_cbs;
#else
    return false;
#endif
}

/**
 * cpu_get_address_space:
 * @cpu: CPU to get address space from
 * @asidx: index identifying which address space to get
 *
 * Return the requested address space of this CPU. @asidx
 * specifies which address space to read.
 */
AddressSpace *cpu_get_address_space(CPUState *cpu, int asidx);

G_NORETURN void cpu_abort(CPUState *cpu, const char *fmt, ...)
    G_GNUC_PRINTF(2, 3);

/* $(top_srcdir)/cpu.c */
void cpu_class_init_props(DeviceClass *dc);
void cpu_exec_initfn(CPUState *cpu);
bool cpu_exec_realizefn(CPUState *cpu, Error **errp);
void cpu_exec_unrealizefn(CPUState *cpu);
void cpu_exec_reset_hold(CPUState *cpu);

/**
 * target_words_bigendian:
 * Returns true if the (default) endianness of the target is big endian,
 * false otherwise. Note that in target-specific code, you can use
 * TARGET_BIG_ENDIAN directly instead. On the other hand, common
 * code should normally never need to know about the endianness of the
 * target, so please do *not* use this function unless you know very well
 * what you are doing!
 */
bool target_words_bigendian(void);

const char *target_name(void);

#ifdef NEED_CPU_H

#ifndef CONFIG_USER_ONLY

extern const VMStateDescription vmstate_cpu_common;

#define VMSTATE_CPU() {                                                     \
    .name = "parent_obj",                                                   \
    .size = sizeof(CPUState),                                               \
    .vmsd = &vmstate_cpu_common,                                            \
    .flags = VMS_STRUCT,                                                    \
    .offset = 0,                                                            \
}
#endif /* !CONFIG_USER_ONLY */

#endif /* NEED_CPU_H */

#define UNASSIGNED_CPU_INDEX -1
#define UNASSIGNED_CLUSTER_INDEX -1

#endif
