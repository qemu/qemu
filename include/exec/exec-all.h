/*
 * internal execution defines for qemu
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef EXEC_ALL_H
#define EXEC_ALL_H

#include "cpu.h"
#ifdef CONFIG_TCG
#include "exec/cpu_ldst.h"
#endif
#include "qemu/interval-tree.h"
#include "qemu/clang-tsa.h"

/* allow to see translation results - the slowdown should be negligible, so we leave it */
#define DEBUG_DISAS

/* Page tracking code uses ram addresses in system mode, and virtual
   addresses in userspace mode.  Define tb_page_addr_t to be an appropriate
   type.  */
#if defined(CONFIG_USER_ONLY)
typedef abi_ulong tb_page_addr_t;
#define TB_PAGE_ADDR_FMT TARGET_ABI_FMT_lx
#else
typedef ram_addr_t tb_page_addr_t;
#define TB_PAGE_ADDR_FMT RAM_ADDR_FMT
#endif

/**
 * cpu_unwind_state_data:
 * @cpu: the cpu context
 * @host_pc: the host pc within the translation
 * @data: output data
 *
 * Attempt to load the the unwind state for a host pc occurring in
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

G_NORETURN void cpu_loop_exit_noexc(CPUState *cpu);
G_NORETURN void cpu_loop_exit(CPUState *cpu);
G_NORETURN void cpu_loop_exit_restore(CPUState *cpu, uintptr_t pc);
G_NORETURN void cpu_loop_exit_atomic(CPUState *cpu, uintptr_t pc);

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
    return (int32_t)qatomic_read(&cpu_neg(cpu)->icount_decr.u32) < 0;
}

#if !defined(CONFIG_USER_ONLY) && defined(CONFIG_TCG)
/* cputlb.c */
/**
 * tlb_init - initialize a CPU's TLB
 * @cpu: CPU whose TLB should be initialized
 */
void tlb_init(CPUState *cpu);
/**
 * tlb_destroy - destroy a CPU's TLB
 * @cpu: CPU whose TLB should be destroyed
 */
void tlb_destroy(CPUState *cpu);
/**
 * tlb_flush_page:
 * @cpu: CPU whose TLB should be flushed
 * @addr: virtual address of page to be flushed
 *
 * Flush one page from the TLB of the specified CPU, for all
 * MMU indexes.
 */
void tlb_flush_page(CPUState *cpu, target_ulong addr);
/**
 * tlb_flush_page_all_cpus:
 * @cpu: src CPU of the flush
 * @addr: virtual address of page to be flushed
 *
 * Flush one page from the TLB of the specified CPU, for all
 * MMU indexes.
 */
void tlb_flush_page_all_cpus(CPUState *src, target_ulong addr);
/**
 * tlb_flush_page_all_cpus_synced:
 * @cpu: src CPU of the flush
 * @addr: virtual address of page to be flushed
 *
 * Flush one page from the TLB of the specified CPU, for all MMU
 * indexes like tlb_flush_page_all_cpus except the source vCPUs work
 * is scheduled as safe work meaning all flushes will be complete once
 * the source vCPUs safe work is complete. This will depend on when
 * the guests translation ends the TB.
 */
void tlb_flush_page_all_cpus_synced(CPUState *src, target_ulong addr);
/**
 * tlb_flush:
 * @cpu: CPU whose TLB should be flushed
 *
 * Flush the entire TLB for the specified CPU. Most CPU architectures
 * allow the implementation to drop entries from the TLB at any time
 * so this is generally safe. If more selective flushing is required
 * use one of the other functions for efficiency.
 */
void tlb_flush(CPUState *cpu);
/**
 * tlb_flush_all_cpus:
 * @cpu: src CPU of the flush
 */
void tlb_flush_all_cpus(CPUState *src_cpu);
/**
 * tlb_flush_all_cpus_synced:
 * @cpu: src CPU of the flush
 *
 * Like tlb_flush_all_cpus except this except the source vCPUs work is
 * scheduled as safe work meaning all flushes will be complete once
 * the source vCPUs safe work is complete. This will depend on when
 * the guests translation ends the TB.
 */
void tlb_flush_all_cpus_synced(CPUState *src_cpu);
/**
 * tlb_flush_page_by_mmuidx:
 * @cpu: CPU whose TLB should be flushed
 * @addr: virtual address of page to be flushed
 * @idxmap: bitmap of MMU indexes to flush
 *
 * Flush one page from the TLB of the specified CPU, for the specified
 * MMU indexes.
 */
void tlb_flush_page_by_mmuidx(CPUState *cpu, target_ulong addr,
                              uint16_t idxmap);
/**
 * tlb_flush_page_by_mmuidx_all_cpus:
 * @cpu: Originating CPU of the flush
 * @addr: virtual address of page to be flushed
 * @idxmap: bitmap of MMU indexes to flush
 *
 * Flush one page from the TLB of all CPUs, for the specified
 * MMU indexes.
 */
void tlb_flush_page_by_mmuidx_all_cpus(CPUState *cpu, target_ulong addr,
                                       uint16_t idxmap);
/**
 * tlb_flush_page_by_mmuidx_all_cpus_synced:
 * @cpu: Originating CPU of the flush
 * @addr: virtual address of page to be flushed
 * @idxmap: bitmap of MMU indexes to flush
 *
 * Flush one page from the TLB of all CPUs, for the specified MMU
 * indexes like tlb_flush_page_by_mmuidx_all_cpus except the source
 * vCPUs work is scheduled as safe work meaning all flushes will be
 * complete once  the source vCPUs safe work is complete. This will
 * depend on when the guests translation ends the TB.
 */
void tlb_flush_page_by_mmuidx_all_cpus_synced(CPUState *cpu, target_ulong addr,
                                              uint16_t idxmap);
/**
 * tlb_flush_by_mmuidx:
 * @cpu: CPU whose TLB should be flushed
 * @wait: If true ensure synchronisation by exiting the cpu_loop
 * @idxmap: bitmap of MMU indexes to flush
 *
 * Flush all entries from the TLB of the specified CPU, for the specified
 * MMU indexes.
 */
void tlb_flush_by_mmuidx(CPUState *cpu, uint16_t idxmap);
/**
 * tlb_flush_by_mmuidx_all_cpus:
 * @cpu: Originating CPU of the flush
 * @idxmap: bitmap of MMU indexes to flush
 *
 * Flush all entries from all TLBs of all CPUs, for the specified
 * MMU indexes.
 */
void tlb_flush_by_mmuidx_all_cpus(CPUState *cpu, uint16_t idxmap);
/**
 * tlb_flush_by_mmuidx_all_cpus_synced:
 * @cpu: Originating CPU of the flush
 * @idxmap: bitmap of MMU indexes to flush
 *
 * Flush all entries from all TLBs of all CPUs, for the specified
 * MMU indexes like tlb_flush_by_mmuidx_all_cpus except except the source
 * vCPUs work is scheduled as safe work meaning all flushes will be
 * complete once  the source vCPUs safe work is complete. This will
 * depend on when the guests translation ends the TB.
 */
void tlb_flush_by_mmuidx_all_cpus_synced(CPUState *cpu, uint16_t idxmap);

/**
 * tlb_flush_page_bits_by_mmuidx
 * @cpu: CPU whose TLB should be flushed
 * @addr: virtual address of page to be flushed
 * @idxmap: bitmap of mmu indexes to flush
 * @bits: number of significant bits in address
 *
 * Similar to tlb_flush_page_mask, but with a bitmap of indexes.
 */
void tlb_flush_page_bits_by_mmuidx(CPUState *cpu, target_ulong addr,
                                   uint16_t idxmap, unsigned bits);

/* Similarly, with broadcast and syncing. */
void tlb_flush_page_bits_by_mmuidx_all_cpus(CPUState *cpu, target_ulong addr,
                                            uint16_t idxmap, unsigned bits);
void tlb_flush_page_bits_by_mmuidx_all_cpus_synced
    (CPUState *cpu, target_ulong addr, uint16_t idxmap, unsigned bits);

/**
 * tlb_flush_range_by_mmuidx
 * @cpu: CPU whose TLB should be flushed
 * @addr: virtual address of the start of the range to be flushed
 * @len: length of range to be flushed
 * @idxmap: bitmap of mmu indexes to flush
 * @bits: number of significant bits in address
 *
 * For each mmuidx in @idxmap, flush all pages within [@addr,@addr+@len),
 * comparing only the low @bits worth of each virtual page.
 */
void tlb_flush_range_by_mmuidx(CPUState *cpu, target_ulong addr,
                               target_ulong len, uint16_t idxmap,
                               unsigned bits);

/* Similarly, with broadcast and syncing. */
void tlb_flush_range_by_mmuidx_all_cpus(CPUState *cpu, target_ulong addr,
                                        target_ulong len, uint16_t idxmap,
                                        unsigned bits);
void tlb_flush_range_by_mmuidx_all_cpus_synced(CPUState *cpu,
                                               target_ulong addr,
                                               target_ulong len,
                                               uint16_t idxmap,
                                               unsigned bits);

/**
 * tlb_set_page_full:
 * @cpu: CPU context
 * @mmu_idx: mmu index of the tlb to modify
 * @vaddr: virtual address of the entry to add
 * @full: the details of the tlb entry
 *
 * Add an entry to @cpu tlb index @mmu_idx.  All of the fields of
 * @full must be filled, except for xlat_section, and constitute
 * the complete description of the translated page.
 *
 * This is generally called by the target tlb_fill function after
 * having performed a successful page table walk to find the physical
 * address and attributes for the translation.
 *
 * At most one entry for a given virtual address is permitted. Only a
 * single TARGET_PAGE_SIZE region is mapped; @full->lg_page_size is only
 * used by tlb_flush_page.
 */
void tlb_set_page_full(CPUState *cpu, int mmu_idx, target_ulong vaddr,
                       CPUTLBEntryFull *full);

/**
 * tlb_set_page_with_attrs:
 * @cpu: CPU to add this TLB entry for
 * @vaddr: virtual address of page to add entry for
 * @paddr: physical address of the page
 * @attrs: memory transaction attributes
 * @prot: access permissions (PAGE_READ/PAGE_WRITE/PAGE_EXEC bits)
 * @mmu_idx: MMU index to insert TLB entry for
 * @size: size of the page in bytes
 *
 * Add an entry to this CPU's TLB (a mapping from virtual address
 * @vaddr to physical address @paddr) with the specified memory
 * transaction attributes. This is generally called by the target CPU
 * specific code after it has been called through the tlb_fill()
 * entry point and performed a successful page table walk to find
 * the physical address and attributes for the virtual address
 * which provoked the TLB miss.
 *
 * At most one entry for a given virtual address is permitted. Only a
 * single TARGET_PAGE_SIZE region is mapped; the supplied @size is only
 * used by tlb_flush_page.
 */
void tlb_set_page_with_attrs(CPUState *cpu, target_ulong vaddr,
                             hwaddr paddr, MemTxAttrs attrs,
                             int prot, int mmu_idx, target_ulong size);
/* tlb_set_page:
 *
 * This function is equivalent to calling tlb_set_page_with_attrs()
 * with an @attrs argument of MEMTXATTRS_UNSPECIFIED. It's provided
 * as a convenience for CPUs which don't use memory transaction attributes.
 */
void tlb_set_page(CPUState *cpu, target_ulong vaddr,
                  hwaddr paddr, int prot,
                  int mmu_idx, target_ulong size);
#else
static inline void tlb_init(CPUState *cpu)
{
}
static inline void tlb_destroy(CPUState *cpu)
{
}
static inline void tlb_flush_page(CPUState *cpu, target_ulong addr)
{
}
static inline void tlb_flush_page_all_cpus(CPUState *src, target_ulong addr)
{
}
static inline void tlb_flush_page_all_cpus_synced(CPUState *src,
                                                  target_ulong addr)
{
}
static inline void tlb_flush(CPUState *cpu)
{
}
static inline void tlb_flush_all_cpus(CPUState *src_cpu)
{
}
static inline void tlb_flush_all_cpus_synced(CPUState *src_cpu)
{
}
static inline void tlb_flush_page_by_mmuidx(CPUState *cpu,
                                            target_ulong addr, uint16_t idxmap)
{
}

static inline void tlb_flush_by_mmuidx(CPUState *cpu, uint16_t idxmap)
{
}
static inline void tlb_flush_page_by_mmuidx_all_cpus(CPUState *cpu,
                                                     target_ulong addr,
                                                     uint16_t idxmap)
{
}
static inline void tlb_flush_page_by_mmuidx_all_cpus_synced(CPUState *cpu,
                                                            target_ulong addr,
                                                            uint16_t idxmap)
{
}
static inline void tlb_flush_by_mmuidx_all_cpus(CPUState *cpu, uint16_t idxmap)
{
}

static inline void tlb_flush_by_mmuidx_all_cpus_synced(CPUState *cpu,
                                                       uint16_t idxmap)
{
}
static inline void tlb_flush_page_bits_by_mmuidx(CPUState *cpu,
                                                 target_ulong addr,
                                                 uint16_t idxmap,
                                                 unsigned bits)
{
}
static inline void tlb_flush_page_bits_by_mmuidx_all_cpus(CPUState *cpu,
                                                          target_ulong addr,
                                                          uint16_t idxmap,
                                                          unsigned bits)
{
}
static inline void
tlb_flush_page_bits_by_mmuidx_all_cpus_synced(CPUState *cpu, target_ulong addr,
                                              uint16_t idxmap, unsigned bits)
{
}
static inline void tlb_flush_range_by_mmuidx(CPUState *cpu, target_ulong addr,
                                             target_ulong len, uint16_t idxmap,
                                             unsigned bits)
{
}
static inline void tlb_flush_range_by_mmuidx_all_cpus(CPUState *cpu,
                                                      target_ulong addr,
                                                      target_ulong len,
                                                      uint16_t idxmap,
                                                      unsigned bits)
{
}
static inline void tlb_flush_range_by_mmuidx_all_cpus_synced(CPUState *cpu,
                                                             target_ulong addr,
                                                             target_long len,
                                                             uint16_t idxmap,
                                                             unsigned bits)
{
}
#endif
/**
 * probe_access:
 * @env: CPUArchState
 * @addr: guest virtual address to look up
 * @size: size of the access
 * @access_type: read, write or execute permission
 * @mmu_idx: MMU index to use for lookup
 * @retaddr: return address for unwinding
 *
 * Look up the guest virtual address @addr.  Raise an exception if the
 * page does not satisfy @access_type.  Raise an exception if the
 * access (@addr, @size) hits a watchpoint.  For writes, mark a clean
 * page as dirty.
 *
 * Finally, return the host address for a page that is backed by RAM,
 * or NULL if the page requires I/O.
 */
void *probe_access(CPUArchState *env, target_ulong addr, int size,
                   MMUAccessType access_type, int mmu_idx, uintptr_t retaddr);

static inline void *probe_write(CPUArchState *env, target_ulong addr, int size,
                                int mmu_idx, uintptr_t retaddr)
{
    return probe_access(env, addr, size, MMU_DATA_STORE, mmu_idx, retaddr);
}

static inline void *probe_read(CPUArchState *env, target_ulong addr, int size,
                               int mmu_idx, uintptr_t retaddr)
{
    return probe_access(env, addr, size, MMU_DATA_LOAD, mmu_idx, retaddr);
}

/**
 * probe_access_flags:
 * @env: CPUArchState
 * @addr: guest virtual address to look up
 * @access_type: read, write or execute permission
 * @mmu_idx: MMU index to use for lookup
 * @nonfault: suppress the fault
 * @phost: return value for host address
 * @retaddr: return address for unwinding
 *
 * Similar to probe_access, loosely returning the TLB_FLAGS_MASK for
 * the page, and storing the host address for RAM in @phost.
 *
 * If @nonfault is set, do not raise an exception but return TLB_INVALID_MASK.
 * Do not handle watchpoints, but include TLB_WATCHPOINT in the returned flags.
 * Do handle clean pages, so exclude TLB_NOTDIRY from the returned flags.
 * For simplicity, all "mmio-like" flags are folded to TLB_MMIO.
 */
int probe_access_flags(CPUArchState *env, target_ulong addr,
                       MMUAccessType access_type, int mmu_idx,
                       bool nonfault, void **phost, uintptr_t retaddr);

#ifndef CONFIG_USER_ONLY
/**
 * probe_access_full:
 * Like probe_access_flags, except also return into @pfull.
 *
 * The CPUTLBEntryFull structure returned via @pfull is transient
 * and must be consumed or copied immediately, before any further
 * access or changes to TLB @mmu_idx.
 */
int probe_access_full(CPUArchState *env, target_ulong addr,
                      MMUAccessType access_type, int mmu_idx,
                      bool nonfault, void **phost,
                      CPUTLBEntryFull **pfull, uintptr_t retaddr);
#endif

#define CODE_GEN_ALIGN           16 /* must be >= of the size of a icache line */

/* Estimated block size for TB allocation.  */
/* ??? The following is based on a 2015 survey of x86_64 host output.
   Better would seem to be some sort of dynamically sized TB array,
   adapting to the block sizes actually being produced.  */
#if defined(CONFIG_SOFTMMU)
#define CODE_GEN_AVG_BLOCK_SIZE 400
#else
#define CODE_GEN_AVG_BLOCK_SIZE 150
#endif

/*
 * Translation Cache-related fields of a TB.
 * This struct exists just for convenience; we keep track of TB's in a binary
 * search tree, and the only fields needed to compare TB's in the tree are
 * @ptr and @size.
 * Note: the address of search data can be obtained by adding @size to @ptr.
 */
struct tb_tc {
    const void *ptr;    /* pointer to the translated code */
    size_t size;
};

struct TranslationBlock {
#if !TARGET_TB_PCREL
    /*
     * Guest PC corresponding to this block.  This must be the true
     * virtual address.  Therefore e.g. x86 stores EIP + CS_BASE, and
     * targets like Arm, MIPS, HP-PA, which reuse low bits for ISA or
     * privilege, must store those bits elsewhere.
     *
     * If TARGET_TB_PCREL, the opcodes for the TranslationBlock are
     * written such that the TB is associated only with the physical
     * page and may be run in any virtual address context.  In this case,
     * PC must always be taken from ENV in a target-specific manner.
     * Unwind information is taken as offsets from the page, to be
     * deposited into the "current" PC.
     */
    target_ulong pc;
#endif

    /*
     * Target-specific data associated with the TranslationBlock, e.g.:
     * x86: the original user, the Code Segment virtual base,
     * arm: an extension of tb->flags,
     * s390x: instruction data for EXECUTE,
     * sparc: the next pc of the instruction queue (for delay slots).
     */
    target_ulong cs_base;

    uint32_t flags; /* flags defining in which context the code was generated */
    uint32_t cflags;    /* compile flags */

/* Note that TCG_MAX_INSNS is 512; we validate this match elsewhere. */
#define CF_COUNT_MASK    0x000001ff
#define CF_NO_GOTO_TB    0x00000200 /* Do not chain with goto_tb */
#define CF_NO_GOTO_PTR   0x00000400 /* Do not chain with goto_ptr */
#define CF_SINGLE_STEP   0x00000800 /* gdbstub single-step in effect */
#define CF_LAST_IO       0x00008000 /* Last insn may be an IO access.  */
#define CF_MEMI_ONLY     0x00010000 /* Only instrument memory ops */
#define CF_USE_ICOUNT    0x00020000
#define CF_INVALID       0x00040000 /* TB is stale. Set with @jmp_lock held */
#define CF_PARALLEL      0x00080000 /* Generate code for a parallel context */
#define CF_NOIRQ         0x00100000 /* Generate an uninterruptible TB */
#define CF_CLUSTER_MASK  0xff000000 /* Top 8 bits are cluster ID */
#define CF_CLUSTER_SHIFT 24

    /* Per-vCPU dynamic tracing state used to generate this TB */
    uint32_t trace_vcpu_dstate;

    /*
     * Above fields used for comparing
     */

    /* size of target code for this block (1 <= size <= TARGET_PAGE_SIZE) */
    uint16_t size;
    uint16_t icount;

    struct tb_tc tc;

    /*
     * Track tb_page_addr_t intervals that intersect this TB.
     * For user-only, the virtual addresses are always contiguous,
     * and we use a unified interval tree.  For system, we use a
     * linked list headed in each PageDesc.  Within the list, the lsb
     * of the previous pointer tells the index of page_next[], and the
     * list is protected by the PageDesc lock(s).
     */
#ifdef CONFIG_USER_ONLY
    IntervalTreeNode itree;
#else
    uintptr_t page_next[2];
    tb_page_addr_t page_addr[2];
#endif

    /* jmp_lock placed here to fill a 4-byte hole. Its documentation is below */
    QemuSpin jmp_lock;

    /* The following data are used to directly call another TB from
     * the code of this one. This can be done either by emitting direct or
     * indirect native jump instructions. These jumps are reset so that the TB
     * just continues its execution. The TB can be linked to another one by
     * setting one of the jump targets (or patching the jump instruction). Only
     * two of such jumps are supported.
     */
#define TB_JMP_OFFSET_INVALID 0xffff /* indicates no jump generated */
    uint16_t jmp_reset_offset[2]; /* offset of original jump target */
    uint16_t jmp_insn_offset[2];  /* offset of direct jump insn */
    uintptr_t jmp_target_addr[2]; /* target address */

    /*
     * Each TB has a NULL-terminated list (jmp_list_head) of incoming jumps.
     * Each TB can have two outgoing jumps, and therefore can participate
     * in two lists. The list entries are kept in jmp_list_next[2]. The least
     * significant bit (LSB) of the pointers in these lists is used to encode
     * which of the two list entries is to be used in the pointed TB.
     *
     * List traversals are protected by jmp_lock. The destination TB of each
     * outgoing jump is kept in jmp_dest[] so that the appropriate jmp_lock
     * can be acquired from any origin TB.
     *
     * jmp_dest[] are tagged pointers as well. The LSB is set when the TB is
     * being invalidated, so that no further outgoing jumps from it can be set.
     *
     * jmp_lock also protects the CF_INVALID cflag; a jump must not be chained
     * to a destination TB that has CF_INVALID set.
     */
    uintptr_t jmp_list_head;
    uintptr_t jmp_list_next[2];
    uintptr_t jmp_dest[2];
};

/* Hide the read to avoid ifdefs for TARGET_TB_PCREL. */
static inline target_ulong tb_pc(const TranslationBlock *tb)
{
#if TARGET_TB_PCREL
    qemu_build_not_reached();
#else
    return tb->pc;
#endif
}

/* Hide the qatomic_read to make code a little easier on the eyes */
static inline uint32_t tb_cflags(const TranslationBlock *tb)
{
    return qatomic_read(&tb->cflags);
}

static inline tb_page_addr_t tb_page_addr0(const TranslationBlock *tb)
{
#ifdef CONFIG_USER_ONLY
    return tb->itree.start;
#else
    return tb->page_addr[0];
#endif
}

static inline tb_page_addr_t tb_page_addr1(const TranslationBlock *tb)
{
#ifdef CONFIG_USER_ONLY
    tb_page_addr_t next = tb->itree.last & TARGET_PAGE_MASK;
    return next == (tb->itree.start & TARGET_PAGE_MASK) ? -1 : next;
#else
    return tb->page_addr[1];
#endif
}

static inline void tb_set_page_addr0(TranslationBlock *tb,
                                     tb_page_addr_t addr)
{
#ifdef CONFIG_USER_ONLY
    tb->itree.start = addr;
    /*
     * To begin, we record an interval of one byte.  When the translation
     * loop encounters a second page, the interval will be extended to
     * include the first byte of the second page, which is sufficient to
     * allow tb_page_addr1() above to work properly.  The final corrected
     * interval will be set by tb_page_add() from tb->size before the
     * node is added to the interval tree.
     */
    tb->itree.last = addr;
#else
    tb->page_addr[0] = addr;
#endif
}

static inline void tb_set_page_addr1(TranslationBlock *tb,
                                     tb_page_addr_t addr)
{
#ifdef CONFIG_USER_ONLY
    /* Extend the interval to the first byte of the second page.  See above. */
    tb->itree.last = addr;
#else
    tb->page_addr[1] = addr;
#endif
}

/* current cflags for hashing/comparison */
uint32_t curr_cflags(CPUState *cpu);

/* TranslationBlock invalidate API */
#if defined(CONFIG_USER_ONLY)
void tb_invalidate_phys_addr(target_ulong addr);
#else
void tb_invalidate_phys_addr(AddressSpace *as, hwaddr addr, MemTxAttrs attrs);
#endif
void tb_flush(CPUState *cpu);
void tb_phys_invalidate(TranslationBlock *tb, tb_page_addr_t page_addr);
void tb_invalidate_phys_range(tb_page_addr_t start, tb_page_addr_t end);
void tb_set_jmp_target(TranslationBlock *tb, int n, uintptr_t addr);

/* GETPC is the true target of the return instruction that we'll execute.  */
#if defined(CONFIG_TCG_INTERPRETER)
extern __thread uintptr_t tci_tb_ptr;
# define GETPC() tci_tb_ptr
#else
# define GETPC() \
    ((uintptr_t)__builtin_extract_return_addr(__builtin_return_address(0)))
#endif

/* The true return address will often point to a host insn that is part of
   the next translated guest insn.  Adjust the address backward to point to
   the middle of the call insn.  Subtracting one would do the job except for
   several compressed mode architectures (arm, mips) which set the low bit
   to indicate the compressed mode; subtracting two works around that.  It
   is also the case that there are no host isas that contain a call insn
   smaller than 4 bytes, so we don't worry about special-casing this.  */
#define GETPC_ADJ   2

#if !defined(CONFIG_USER_ONLY)

/**
 * iotlb_to_section:
 * @cpu: CPU performing the access
 * @index: TCG CPU IOTLB entry
 *
 * Given a TCG CPU IOTLB entry, return the MemoryRegionSection that
 * it refers to. @index will have been initially created and returned
 * by memory_region_section_get_iotlb().
 */
struct MemoryRegionSection *iotlb_to_section(CPUState *cpu,
                                             hwaddr index, MemTxAttrs attrs);
#endif

/**
 * get_page_addr_code_hostp()
 * @env: CPUArchState
 * @addr: guest virtual address of guest code
 *
 * See get_page_addr_code() (full-system version) for documentation on the
 * return value.
 *
 * Sets *@hostp (when @hostp is non-NULL) as follows.
 * If the return value is -1, sets *@hostp to NULL. Otherwise, sets *@hostp
 * to the host address where @addr's content is kept.
 *
 * Note: this function can trigger an exception.
 */
tb_page_addr_t get_page_addr_code_hostp(CPUArchState *env, target_ulong addr,
                                        void **hostp);

/**
 * get_page_addr_code()
 * @env: CPUArchState
 * @addr: guest virtual address of guest code
 *
 * If we cannot translate and execute from the entire RAM page, or if
 * the region is not backed by RAM, returns -1. Otherwise, returns the
 * ram_addr_t corresponding to the guest code at @addr.
 *
 * Note: this function can trigger an exception.
 */
static inline tb_page_addr_t get_page_addr_code(CPUArchState *env,
                                                target_ulong addr)
{
    return get_page_addr_code_hostp(env, addr, NULL);
}

#if defined(CONFIG_USER_ONLY)
void TSA_NO_TSA mmap_lock(void);
void TSA_NO_TSA mmap_unlock(void);
bool have_mmap_lock(void);

/**
 * adjust_signal_pc:
 * @pc: raw pc from the host signal ucontext_t.
 * @is_write: host memory operation was write, or read-modify-write.
 *
 * Alter @pc as required for unwinding.  Return the type of the
 * guest memory access -- host reads may be for guest execution.
 */
MMUAccessType adjust_signal_pc(uintptr_t *pc, bool is_write);

/**
 * handle_sigsegv_accerr_write:
 * @cpu: the cpu context
 * @old_set: the sigset_t from the signal ucontext_t
 * @host_pc: the host pc, adjusted for the signal
 * @host_addr: the host address of the fault
 *
 * Return true if the write fault has been handled, and should be re-tried.
 */
bool handle_sigsegv_accerr_write(CPUState *cpu, sigset_t *old_set,
                                 uintptr_t host_pc, abi_ptr guest_addr);

/**
 * cpu_loop_exit_sigsegv:
 * @cpu: the cpu context
 * @addr: the guest address of the fault
 * @access_type: access was read/write/execute
 * @maperr: true for invalid page, false for permission fault
 * @ra: host pc for unwinding
 *
 * Use the TCGCPUOps hook to record cpu state, do guest operating system
 * specific things to raise SIGSEGV, and jump to the main cpu loop.
 */
G_NORETURN void cpu_loop_exit_sigsegv(CPUState *cpu, target_ulong addr,
                                      MMUAccessType access_type,
                                      bool maperr, uintptr_t ra);

/**
 * cpu_loop_exit_sigbus:
 * @cpu: the cpu context
 * @addr: the guest address of the alignment fault
 * @access_type: access was read/write/execute
 * @ra: host pc for unwinding
 *
 * Use the TCGCPUOps hook to record cpu state, do guest operating system
 * specific things to raise SIGBUS, and jump to the main cpu loop.
 */
G_NORETURN void cpu_loop_exit_sigbus(CPUState *cpu, target_ulong addr,
                                     MMUAccessType access_type,
                                     uintptr_t ra);

#else
static inline void mmap_lock(void) {}
static inline void mmap_unlock(void) {}

void tlb_reset_dirty(CPUState *cpu, ram_addr_t start1, ram_addr_t length);
void tlb_set_dirty(CPUState *cpu, target_ulong vaddr);

MemoryRegionSection *
address_space_translate_for_iotlb(CPUState *cpu, int asidx, hwaddr addr,
                                  hwaddr *xlat, hwaddr *plen,
                                  MemTxAttrs attrs, int *prot);
hwaddr memory_region_section_get_iotlb(CPUState *cpu,
                                       MemoryRegionSection *section);
#endif

#endif
