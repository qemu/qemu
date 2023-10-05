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
#if defined(CONFIG_USER_ONLY)
#include "exec/cpu_ldst.h"
#endif
#include "exec/translation-block.h"
#include "qemu/clang-tsa.h"

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
void tlb_flush_page(CPUState *cpu, vaddr addr);
/**
 * tlb_flush_page_all_cpus:
 * @cpu: src CPU of the flush
 * @addr: virtual address of page to be flushed
 *
 * Flush one page from the TLB of the specified CPU, for all
 * MMU indexes.
 */
void tlb_flush_page_all_cpus(CPUState *src, vaddr addr);
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
void tlb_flush_page_all_cpus_synced(CPUState *src, vaddr addr);
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
void tlb_flush_page_by_mmuidx(CPUState *cpu, vaddr addr,
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
void tlb_flush_page_by_mmuidx_all_cpus(CPUState *cpu, vaddr addr,
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
void tlb_flush_page_by_mmuidx_all_cpus_synced(CPUState *cpu, vaddr addr,
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
void tlb_flush_page_bits_by_mmuidx(CPUState *cpu, vaddr addr,
                                   uint16_t idxmap, unsigned bits);

/* Similarly, with broadcast and syncing. */
void tlb_flush_page_bits_by_mmuidx_all_cpus(CPUState *cpu, vaddr addr,
                                            uint16_t idxmap, unsigned bits);
void tlb_flush_page_bits_by_mmuidx_all_cpus_synced
    (CPUState *cpu, vaddr addr, uint16_t idxmap, unsigned bits);

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
void tlb_flush_range_by_mmuidx(CPUState *cpu, vaddr addr,
                               vaddr len, uint16_t idxmap,
                               unsigned bits);

/* Similarly, with broadcast and syncing. */
void tlb_flush_range_by_mmuidx_all_cpus(CPUState *cpu, vaddr addr,
                                        vaddr len, uint16_t idxmap,
                                        unsigned bits);
void tlb_flush_range_by_mmuidx_all_cpus_synced(CPUState *cpu,
                                               vaddr addr,
                                               vaddr len,
                                               uint16_t idxmap,
                                               unsigned bits);

/**
 * tlb_set_page_full:
 * @cpu: CPU context
 * @mmu_idx: mmu index of the tlb to modify
 * @addr: virtual address of the entry to add
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
void tlb_set_page_full(CPUState *cpu, int mmu_idx, vaddr addr,
                       CPUTLBEntryFull *full);

/**
 * tlb_set_page_with_attrs:
 * @cpu: CPU to add this TLB entry for
 * @addr: virtual address of page to add entry for
 * @paddr: physical address of the page
 * @attrs: memory transaction attributes
 * @prot: access permissions (PAGE_READ/PAGE_WRITE/PAGE_EXEC bits)
 * @mmu_idx: MMU index to insert TLB entry for
 * @size: size of the page in bytes
 *
 * Add an entry to this CPU's TLB (a mapping from virtual address
 * @addr to physical address @paddr) with the specified memory
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
void tlb_set_page_with_attrs(CPUState *cpu, vaddr addr,
                             hwaddr paddr, MemTxAttrs attrs,
                             int prot, int mmu_idx, vaddr size);
/* tlb_set_page:
 *
 * This function is equivalent to calling tlb_set_page_with_attrs()
 * with an @attrs argument of MEMTXATTRS_UNSPECIFIED. It's provided
 * as a convenience for CPUs which don't use memory transaction attributes.
 */
void tlb_set_page(CPUState *cpu, vaddr addr,
                  hwaddr paddr, int prot,
                  int mmu_idx, vaddr size);
#else
static inline void tlb_init(CPUState *cpu)
{
}
static inline void tlb_destroy(CPUState *cpu)
{
}
static inline void tlb_flush_page(CPUState *cpu, vaddr addr)
{
}
static inline void tlb_flush_page_all_cpus(CPUState *src, vaddr addr)
{
}
static inline void tlb_flush_page_all_cpus_synced(CPUState *src, vaddr addr)
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
                                            vaddr addr, uint16_t idxmap)
{
}

static inline void tlb_flush_by_mmuidx(CPUState *cpu, uint16_t idxmap)
{
}
static inline void tlb_flush_page_by_mmuidx_all_cpus(CPUState *cpu,
                                                     vaddr addr,
                                                     uint16_t idxmap)
{
}
static inline void tlb_flush_page_by_mmuidx_all_cpus_synced(CPUState *cpu,
                                                            vaddr addr,
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
                                                 vaddr addr,
                                                 uint16_t idxmap,
                                                 unsigned bits)
{
}
static inline void tlb_flush_page_bits_by_mmuidx_all_cpus(CPUState *cpu,
                                                          vaddr addr,
                                                          uint16_t idxmap,
                                                          unsigned bits)
{
}
static inline void
tlb_flush_page_bits_by_mmuidx_all_cpus_synced(CPUState *cpu, vaddr addr,
                                              uint16_t idxmap, unsigned bits)
{
}
static inline void tlb_flush_range_by_mmuidx(CPUState *cpu, vaddr addr,
                                             vaddr len, uint16_t idxmap,
                                             unsigned bits)
{
}
static inline void tlb_flush_range_by_mmuidx_all_cpus(CPUState *cpu,
                                                      vaddr addr,
                                                      vaddr len,
                                                      uint16_t idxmap,
                                                      unsigned bits)
{
}
static inline void tlb_flush_range_by_mmuidx_all_cpus_synced(CPUState *cpu,
                                                             vaddr addr,
                                                             vaddr len,
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
void *probe_access(CPUArchState *env, vaddr addr, int size,
                   MMUAccessType access_type, int mmu_idx, uintptr_t retaddr);

static inline void *probe_write(CPUArchState *env, vaddr addr, int size,
                                int mmu_idx, uintptr_t retaddr)
{
    return probe_access(env, addr, size, MMU_DATA_STORE, mmu_idx, retaddr);
}

static inline void *probe_read(CPUArchState *env, vaddr addr, int size,
                               int mmu_idx, uintptr_t retaddr)
{
    return probe_access(env, addr, size, MMU_DATA_LOAD, mmu_idx, retaddr);
}

/**
 * probe_access_flags:
 * @env: CPUArchState
 * @addr: guest virtual address to look up
 * @size: size of the access
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
int probe_access_flags(CPUArchState *env, vaddr addr, int size,
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
int probe_access_full(CPUArchState *env, vaddr addr, int size,
                      MMUAccessType access_type, int mmu_idx,
                      bool nonfault, void **phost,
                      CPUTLBEntryFull **pfull, uintptr_t retaddr);

/**
 * probe_access_mmu() - Like probe_access_full except cannot fault and
 * doesn't trigger instrumentation.
 *
 * @env: CPUArchState
 * @vaddr: virtual address to probe
 * @size: size of the probe
 * @access_type: read, write or execute permission
 * @mmu_idx: softmmu index
 * @phost: ptr to return value host address or NULL
 * @pfull: ptr to return value CPUTLBEntryFull structure or NULL
 *
 * The CPUTLBEntryFull structure returned via @pfull is transient
 * and must be consumed or copied immediately, before any further
 * access or changes to TLB @mmu_idx.
 *
 * Returns: TLB flags as per probe_access_flags()
 */
int probe_access_full_mmu(CPUArchState *env, vaddr addr, int size,
                          MMUAccessType access_type, int mmu_idx,
                          void **phost, CPUTLBEntryFull **pfull);

#endif

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
void tb_invalidate_phys_addr(hwaddr addr);
#else
void tb_invalidate_phys_addr(AddressSpace *as, hwaddr addr, MemTxAttrs attrs);
#endif
void tb_phys_invalidate(TranslationBlock *tb, tb_page_addr_t page_addr);
void tb_invalidate_phys_range(tb_page_addr_t start, tb_page_addr_t last);
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
tb_page_addr_t get_page_addr_code_hostp(CPUArchState *env, vaddr addr,
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
                                                vaddr addr)
{
    return get_page_addr_code_hostp(env, addr, NULL);
}

#if defined(CONFIG_USER_ONLY)
void TSA_NO_TSA mmap_lock(void);
void TSA_NO_TSA mmap_unlock(void);
bool have_mmap_lock(void);

static inline void mmap_unlock_guard(void *unused)
{
    mmap_unlock();
}

#define WITH_MMAP_LOCK_GUARD()                                            \
    for (int _mmap_lock_iter __attribute__((cleanup(mmap_unlock_guard)))  \
         = (mmap_lock(), 0); _mmap_lock_iter == 0; _mmap_lock_iter = 1)

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
#define WITH_MMAP_LOCK_GUARD()

void tlb_reset_dirty(CPUState *cpu, ram_addr_t start1, ram_addr_t length);
void tlb_set_dirty(CPUState *cpu, vaddr addr);

MemoryRegionSection *
address_space_translate_for_iotlb(CPUState *cpu, int asidx, hwaddr addr,
                                  hwaddr *xlat, hwaddr *plen,
                                  MemTxAttrs attrs, int *prot);
hwaddr memory_region_section_get_iotlb(CPUState *cpu,
                                       MemoryRegionSection *section);
#endif

#endif
