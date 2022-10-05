/*
 *  User emulator execution
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
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
#include "qemu/osdep.h"
#include "hw/core/tcg-cpu-ops.h"
#include "disas/disas.h"
#include "exec/exec-all.h"
#include "tcg/tcg.h"
#include "qemu/bitops.h"
#include "exec/cpu_ldst.h"
#include "exec/translate-all.h"
#include "exec/helper-proto.h"
#include "qemu/atomic128.h"
#include "trace/trace-root.h"
#include "tcg/tcg-ldst.h"
#include "internal.h"

__thread uintptr_t helper_retaddr;

//#define DEBUG_SIGNAL

/*
 * Adjust the pc to pass to cpu_restore_state; return the memop type.
 */
MMUAccessType adjust_signal_pc(uintptr_t *pc, bool is_write)
{
    switch (helper_retaddr) {
    default:
        /*
         * Fault during host memory operation within a helper function.
         * The helper's host return address, saved here, gives us a
         * pointer into the generated code that will unwind to the
         * correct guest pc.
         */
        *pc = helper_retaddr;
        break;

    case 0:
        /*
         * Fault during host memory operation within generated code.
         * (Or, a unrelated bug within qemu, but we can't tell from here).
         *
         * We take the host pc from the signal frame.  However, we cannot
         * use that value directly.  Within cpu_restore_state_from_tb, we
         * assume PC comes from GETPC(), as used by the helper functions,
         * so we adjust the address by -GETPC_ADJ to form an address that
         * is within the call insn, so that the address does not accidentally
         * match the beginning of the next guest insn.  However, when the
         * pc comes from the signal frame it points to the actual faulting
         * host memory insn and not the return from a call insn.
         *
         * Therefore, adjust to compensate for what will be done later
         * by cpu_restore_state_from_tb.
         */
        *pc += GETPC_ADJ;
        break;

    case 1:
        /*
         * Fault during host read for translation, or loosely, "execution".
         *
         * The guest pc is already pointing to the start of the TB for which
         * code is being generated.  If the guest translator manages the
         * page crossings correctly, this is exactly the correct address
         * (and if the translator doesn't handle page boundaries correctly
         * there's little we can do about that here).  Therefore, do not
         * trigger the unwinder.
         */
        *pc = 0;
        return MMU_INST_FETCH;
    }

    return is_write ? MMU_DATA_STORE : MMU_DATA_LOAD;
}

/**
 * handle_sigsegv_accerr_write:
 * @cpu: the cpu context
 * @old_set: the sigset_t from the signal ucontext_t
 * @host_pc: the host pc, adjusted for the signal
 * @guest_addr: the guest address of the fault
 *
 * Return true if the write fault has been handled, and should be re-tried.
 *
 * Note that it is important that we don't call page_unprotect() unless
 * this is really a "write to nonwritable page" fault, because
 * page_unprotect() assumes that if it is called for an access to
 * a page that's writable this means we had two threads racing and
 * another thread got there first and already made the page writable;
 * so we will retry the access. If we were to call page_unprotect()
 * for some other kind of fault that should really be passed to the
 * guest, we'd end up in an infinite loop of retrying the faulting access.
 */
bool handle_sigsegv_accerr_write(CPUState *cpu, sigset_t *old_set,
                                 uintptr_t host_pc, abi_ptr guest_addr)
{
    switch (page_unprotect(guest_addr, host_pc)) {
    case 0:
        /*
         * Fault not caused by a page marked unwritable to protect
         * cached translations, must be the guest binary's problem.
         */
        return false;
    case 1:
        /*
         * Fault caused by protection of cached translation; TBs
         * invalidated, so resume execution.
         */
        return true;
    case 2:
        /*
         * Fault caused by protection of cached translation, and the
         * currently executing TB was modified and must be exited immediately.
         */
        sigprocmask(SIG_SETMASK, old_set, NULL);
        cpu_loop_exit_noexc(cpu);
        /* NORETURN */
    default:
        g_assert_not_reached();
    }
}

/*
 * Walks guest process memory "regions" one by one
 * and calls callback function 'fn' for each region.
 */
struct walk_memory_regions_data {
    walk_memory_regions_fn fn;
    void *priv;
    target_ulong start;
    int prot;
};

static int walk_memory_regions_end(struct walk_memory_regions_data *data,
                                   target_ulong end, int new_prot)
{
    if (data->start != -1u) {
        int rc = data->fn(data->priv, data->start, end, data->prot);
        if (rc != 0) {
            return rc;
        }
    }

    data->start = (new_prot ? end : -1u);
    data->prot = new_prot;

    return 0;
}

static int walk_memory_regions_1(struct walk_memory_regions_data *data,
                                 target_ulong base, int level, void **lp)
{
    target_ulong pa;
    int i, rc;

    if (*lp == NULL) {
        return walk_memory_regions_end(data, base, 0);
    }

    if (level == 0) {
        PageDesc *pd = *lp;

        for (i = 0; i < V_L2_SIZE; ++i) {
            int prot = pd[i].flags;

            pa = base | (i << TARGET_PAGE_BITS);
            if (prot != data->prot) {
                rc = walk_memory_regions_end(data, pa, prot);
                if (rc != 0) {
                    return rc;
                }
            }
        }
    } else {
        void **pp = *lp;

        for (i = 0; i < V_L2_SIZE; ++i) {
            pa = base | ((target_ulong)i <<
                (TARGET_PAGE_BITS + V_L2_BITS * level));
            rc = walk_memory_regions_1(data, pa, level - 1, pp + i);
            if (rc != 0) {
                return rc;
            }
        }
    }

    return 0;
}

int walk_memory_regions(void *priv, walk_memory_regions_fn fn)
{
    struct walk_memory_regions_data data;
    uintptr_t i, l1_sz = v_l1_size;

    data.fn = fn;
    data.priv = priv;
    data.start = -1u;
    data.prot = 0;

    for (i = 0; i < l1_sz; i++) {
        target_ulong base = i << (v_l1_shift + TARGET_PAGE_BITS);
        int rc = walk_memory_regions_1(&data, base, v_l2_levels, l1_map + i);
        if (rc != 0) {
            return rc;
        }
    }

    return walk_memory_regions_end(&data, 0, 0);
}

static int dump_region(void *priv, target_ulong start,
    target_ulong end, unsigned long prot)
{
    FILE *f = (FILE *)priv;

    (void) fprintf(f, TARGET_FMT_lx"-"TARGET_FMT_lx
        " "TARGET_FMT_lx" %c%c%c\n",
        start, end, end - start,
        ((prot & PAGE_READ) ? 'r' : '-'),
        ((prot & PAGE_WRITE) ? 'w' : '-'),
        ((prot & PAGE_EXEC) ? 'x' : '-'));

    return 0;
}

/* dump memory mappings */
void page_dump(FILE *f)
{
    const int length = sizeof(target_ulong) * 2;
    (void) fprintf(f, "%-*s %-*s %-*s %s\n",
            length, "start", length, "end", length, "size", "prot");
    walk_memory_regions(f, dump_region);
}

int page_get_flags(target_ulong address)
{
    PageDesc *p;

    p = page_find(address >> TARGET_PAGE_BITS);
    if (!p) {
        return 0;
    }
    return p->flags;
}

/*
 * Allow the target to decide if PAGE_TARGET_[12] may be reset.
 * By default, they are not kept.
 */
#ifndef PAGE_TARGET_STICKY
#define PAGE_TARGET_STICKY  0
#endif
#define PAGE_STICKY  (PAGE_ANON | PAGE_PASSTHROUGH | PAGE_TARGET_STICKY)

/*
 * Modify the flags of a page and invalidate the code if necessary.
 * The flag PAGE_WRITE_ORG is positioned automatically depending
 * on PAGE_WRITE.  The mmap_lock should already be held.
 */
void page_set_flags(target_ulong start, target_ulong end, int flags)
{
    target_ulong addr, len;
    bool reset, inval_tb = false;

    /* This function should never be called with addresses outside the
       guest address space.  If this assert fires, it probably indicates
       a missing call to h2g_valid.  */
    assert(end - 1 <= GUEST_ADDR_MAX);
    assert(start < end);
    /* Only set PAGE_ANON with new mappings. */
    assert(!(flags & PAGE_ANON) || (flags & PAGE_RESET));
    assert_memory_lock();

    start = start & TARGET_PAGE_MASK;
    end = TARGET_PAGE_ALIGN(end);

    if (flags & PAGE_WRITE) {
        flags |= PAGE_WRITE_ORG;
    }
    reset = !(flags & PAGE_VALID) || (flags & PAGE_RESET);
    if (reset) {
        page_reset_target_data(start, end);
    }
    flags &= ~PAGE_RESET;

    for (addr = start, len = end - start;
         len != 0;
         len -= TARGET_PAGE_SIZE, addr += TARGET_PAGE_SIZE) {
        PageDesc *p = page_find_alloc(addr >> TARGET_PAGE_BITS, true);

        /*
         * If the page was executable, but is reset, or is no longer
         * executable, or has become writable, then invalidate any code.
         */
        if ((p->flags & PAGE_EXEC)
            && (reset ||
                !(flags & PAGE_EXEC) ||
                (flags & ~p->flags & PAGE_WRITE))) {
            inval_tb = true;
        }
        /* Using mprotect on a page does not change sticky bits. */
        p->flags = (reset ? 0 : p->flags & PAGE_STICKY) | flags;
    }

    if (inval_tb) {
        tb_invalidate_phys_range(start, end);
    }
}

int page_check_range(target_ulong start, target_ulong len, int flags)
{
    PageDesc *p;
    target_ulong end;
    target_ulong addr;

    /*
     * This function should never be called with addresses outside the
     * guest address space.  If this assert fires, it probably indicates
     * a missing call to h2g_valid.
     */
    if (TARGET_ABI_BITS > L1_MAP_ADDR_SPACE_BITS) {
        assert(start < ((target_ulong)1 << L1_MAP_ADDR_SPACE_BITS));
    }

    if (len == 0) {
        return 0;
    }
    if (start + len - 1 < start) {
        /* We've wrapped around.  */
        return -1;
    }

    /* must do before we loose bits in the next step */
    end = TARGET_PAGE_ALIGN(start + len);
    start = start & TARGET_PAGE_MASK;

    for (addr = start, len = end - start;
         len != 0;
         len -= TARGET_PAGE_SIZE, addr += TARGET_PAGE_SIZE) {
        p = page_find(addr >> TARGET_PAGE_BITS);
        if (!p) {
            return -1;
        }
        if (!(p->flags & PAGE_VALID)) {
            return -1;
        }

        if ((flags & PAGE_READ) && !(p->flags & PAGE_READ)) {
            return -1;
        }
        if (flags & PAGE_WRITE) {
            if (!(p->flags & PAGE_WRITE_ORG)) {
                return -1;
            }
            /* unprotect the page if it was put read-only because it
               contains translated code */
            if (!(p->flags & PAGE_WRITE)) {
                if (!page_unprotect(addr, 0)) {
                    return -1;
                }
            }
        }
    }
    return 0;
}

void page_protect(tb_page_addr_t page_addr)
{
    target_ulong addr;
    PageDesc *p;
    int prot;

    p = page_find(page_addr >> TARGET_PAGE_BITS);
    if (p && (p->flags & PAGE_WRITE)) {
        /*
         * Force the host page as non writable (writes will have a page fault +
         * mprotect overhead).
         */
        page_addr &= qemu_host_page_mask;
        prot = 0;
        for (addr = page_addr; addr < page_addr + qemu_host_page_size;
             addr += TARGET_PAGE_SIZE) {

            p = page_find(addr >> TARGET_PAGE_BITS);
            if (!p) {
                continue;
            }
            prot |= p->flags;
            p->flags &= ~PAGE_WRITE;
        }
        mprotect(g2h_untagged(page_addr), qemu_host_page_size,
                 (prot & PAGE_BITS) & ~PAGE_WRITE);
    }
}

/*
 * Called from signal handler: invalidate the code and unprotect the
 * page. Return 0 if the fault was not handled, 1 if it was handled,
 * and 2 if it was handled but the caller must cause the TB to be
 * immediately exited. (We can only return 2 if the 'pc' argument is
 * non-zero.)
 */
int page_unprotect(target_ulong address, uintptr_t pc)
{
    unsigned int prot;
    bool current_tb_invalidated;
    PageDesc *p;
    target_ulong host_start, host_end, addr;

    /*
     * Technically this isn't safe inside a signal handler.  However we
     * know this only ever happens in a synchronous SEGV handler, so in
     * practice it seems to be ok.
     */
    mmap_lock();

    p = page_find(address >> TARGET_PAGE_BITS);
    if (!p) {
        mmap_unlock();
        return 0;
    }

    /*
     * If the page was really writable, then we change its
     * protection back to writable.
     */
    if (p->flags & PAGE_WRITE_ORG) {
        current_tb_invalidated = false;
        if (p->flags & PAGE_WRITE) {
            /*
             * If the page is actually marked WRITE then assume this is because
             * this thread raced with another one which got here first and
             * set the page to PAGE_WRITE and did the TB invalidate for us.
             */
#ifdef TARGET_HAS_PRECISE_SMC
            TranslationBlock *current_tb = tcg_tb_lookup(pc);
            if (current_tb) {
                current_tb_invalidated = tb_cflags(current_tb) & CF_INVALID;
            }
#endif
        } else {
            host_start = address & qemu_host_page_mask;
            host_end = host_start + qemu_host_page_size;

            prot = 0;
            for (addr = host_start; addr < host_end; addr += TARGET_PAGE_SIZE) {
                p = page_find(addr >> TARGET_PAGE_BITS);
                p->flags |= PAGE_WRITE;
                prot |= p->flags;

                /*
                 * Since the content will be modified, we must invalidate
                 * the corresponding translated code.
                 */
                current_tb_invalidated |=
                    tb_invalidate_phys_page_unwind(addr, pc);
            }
            mprotect((void *)g2h_untagged(host_start), qemu_host_page_size,
                     prot & PAGE_BITS);
        }
        mmap_unlock();
        /* If current TB was invalidated return to main loop */
        return current_tb_invalidated ? 2 : 1;
    }
    mmap_unlock();
    return 0;
}

static int probe_access_internal(CPUArchState *env, target_ulong addr,
                                 int fault_size, MMUAccessType access_type,
                                 bool nonfault, uintptr_t ra)
{
    int acc_flag;
    bool maperr;

    switch (access_type) {
    case MMU_DATA_STORE:
        acc_flag = PAGE_WRITE_ORG;
        break;
    case MMU_DATA_LOAD:
        acc_flag = PAGE_READ;
        break;
    case MMU_INST_FETCH:
        acc_flag = PAGE_EXEC;
        break;
    default:
        g_assert_not_reached();
    }

    if (guest_addr_valid_untagged(addr)) {
        int page_flags = page_get_flags(addr);
        if (page_flags & acc_flag) {
            return 0; /* success */
        }
        maperr = !(page_flags & PAGE_VALID);
    } else {
        maperr = true;
    }

    if (nonfault) {
        return TLB_INVALID_MASK;
    }

    cpu_loop_exit_sigsegv(env_cpu(env), addr, access_type, maperr, ra);
}

int probe_access_flags(CPUArchState *env, target_ulong addr,
                       MMUAccessType access_type, int mmu_idx,
                       bool nonfault, void **phost, uintptr_t ra)
{
    int flags;

    flags = probe_access_internal(env, addr, 0, access_type, nonfault, ra);
    *phost = flags ? NULL : g2h(env_cpu(env), addr);
    return flags;
}

void *probe_access(CPUArchState *env, target_ulong addr, int size,
                   MMUAccessType access_type, int mmu_idx, uintptr_t ra)
{
    int flags;

    g_assert(-(addr | TARGET_PAGE_MASK) >= size);
    flags = probe_access_internal(env, addr, size, access_type, false, ra);
    g_assert(flags == 0);

    return size ? g2h(env_cpu(env), addr) : NULL;
}

tb_page_addr_t get_page_addr_code_hostp(CPUArchState *env, target_ulong addr,
                                        void **hostp)
{
    int flags;

    flags = probe_access_internal(env, addr, 1, MMU_INST_FETCH, false, 0);
    g_assert(flags == 0);

    if (hostp) {
        *hostp = g2h_untagged(addr);
    }
    return addr;
}

#ifdef TARGET_PAGE_DATA_SIZE
/*
 * Allocate chunks of target data together.  For the only current user,
 * if we allocate one hunk per page, we have overhead of 40/128 or 40%.
 * Therefore, allocate memory for 64 pages at a time for overhead < 1%.
 */
#define TPD_PAGES  64
#define TBD_MASK   (TARGET_PAGE_MASK * TPD_PAGES)

typedef struct TargetPageDataNode {
    IntervalTreeNode itree;
    char data[TPD_PAGES][TARGET_PAGE_DATA_SIZE] __attribute__((aligned));
} TargetPageDataNode;

static IntervalTreeRoot targetdata_root;

void page_reset_target_data(target_ulong start, target_ulong end)
{
    IntervalTreeNode *n, *next;
    target_ulong last;

    assert_memory_lock();

    start = start & TARGET_PAGE_MASK;
    last = TARGET_PAGE_ALIGN(end) - 1;

    for (n = interval_tree_iter_first(&targetdata_root, start, last),
         next = n ? interval_tree_iter_next(n, start, last) : NULL;
         n != NULL;
         n = next,
         next = next ? interval_tree_iter_next(n, start, last) : NULL) {
        target_ulong n_start, n_last, p_ofs, p_len;
        TargetPageDataNode *t;

        if (n->start >= start && n->last <= last) {
            interval_tree_remove(n, &targetdata_root);
            g_free(n);
            continue;
        }

        if (n->start < start) {
            n_start = start;
            p_ofs = (start - n->start) >> TARGET_PAGE_BITS;
        } else {
            n_start = n->start;
            p_ofs = 0;
        }
        n_last = MIN(last, n->last);
        p_len = (n_last + 1 - n_start) >> TARGET_PAGE_BITS;

        t = container_of(n, TargetPageDataNode, itree);
        memset(t->data[p_ofs], 0, p_len * TARGET_PAGE_DATA_SIZE);
    }
}

void *page_get_target_data(target_ulong address)
{
    IntervalTreeNode *n;
    TargetPageDataNode *t;
    target_ulong page, region;

    page = address & TARGET_PAGE_MASK;
    region = address & TBD_MASK;

    n = interval_tree_iter_first(&targetdata_root, page, page);
    if (!n) {
        /*
         * See util/interval-tree.c re lockless lookups: no false positives
         * but there are false negatives.  If we find nothing, retry with
         * the mmap lock acquired.  We also need the lock for the
         * allocation + insert.
         */
        mmap_lock();
        n = interval_tree_iter_first(&targetdata_root, page, page);
        if (!n) {
            t = g_new0(TargetPageDataNode, 1);
            n = &t->itree;
            n->start = region;
            n->last = region | ~TBD_MASK;
            interval_tree_insert(n, &targetdata_root);
        }
        mmap_unlock();
    }

    t = container_of(n, TargetPageDataNode, itree);
    return t->data[(page - region) >> TARGET_PAGE_BITS];
}
#else
void page_reset_target_data(target_ulong start, target_ulong end) { }
#endif /* TARGET_PAGE_DATA_SIZE */

/* The softmmu versions of these helpers are in cputlb.c.  */

/*
 * Verify that we have passed the correct MemOp to the correct function.
 *
 * We could present one function to target code, and dispatch based on
 * the MemOp, but so far we have worked hard to avoid an indirect function
 * call along the memory path.
 */
static void validate_memop(MemOpIdx oi, MemOp expected)
{
#ifdef CONFIG_DEBUG_TCG
    MemOp have = get_memop(oi) & (MO_SIZE | MO_BSWAP);
    assert(have == expected);
#endif
}

void helper_unaligned_ld(CPUArchState *env, target_ulong addr)
{
    cpu_loop_exit_sigbus(env_cpu(env), addr, MMU_DATA_LOAD, GETPC());
}

void helper_unaligned_st(CPUArchState *env, target_ulong addr)
{
    cpu_loop_exit_sigbus(env_cpu(env), addr, MMU_DATA_STORE, GETPC());
}

static void *cpu_mmu_lookup(CPUArchState *env, target_ulong addr,
                            MemOpIdx oi, uintptr_t ra, MMUAccessType type)
{
    MemOp mop = get_memop(oi);
    int a_bits = get_alignment_bits(mop);
    void *ret;

    /* Enforce guest required alignment.  */
    if (unlikely(addr & ((1 << a_bits) - 1))) {
        cpu_loop_exit_sigbus(env_cpu(env), addr, type, ra);
    }

    ret = g2h(env_cpu(env), addr);
    set_helper_retaddr(ra);
    return ret;
}

uint8_t cpu_ldb_mmu(CPUArchState *env, abi_ptr addr,
                    MemOpIdx oi, uintptr_t ra)
{
    void *haddr;
    uint8_t ret;

    validate_memop(oi, MO_UB);
    haddr = cpu_mmu_lookup(env, addr, oi, ra, MMU_DATA_LOAD);
    ret = ldub_p(haddr);
    clear_helper_retaddr();
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_R);
    return ret;
}

uint16_t cpu_ldw_be_mmu(CPUArchState *env, abi_ptr addr,
                        MemOpIdx oi, uintptr_t ra)
{
    void *haddr;
    uint16_t ret;

    validate_memop(oi, MO_BEUW);
    haddr = cpu_mmu_lookup(env, addr, oi, ra, MMU_DATA_LOAD);
    ret = lduw_be_p(haddr);
    clear_helper_retaddr();
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_R);
    return ret;
}

uint32_t cpu_ldl_be_mmu(CPUArchState *env, abi_ptr addr,
                        MemOpIdx oi, uintptr_t ra)
{
    void *haddr;
    uint32_t ret;

    validate_memop(oi, MO_BEUL);
    haddr = cpu_mmu_lookup(env, addr, oi, ra, MMU_DATA_LOAD);
    ret = ldl_be_p(haddr);
    clear_helper_retaddr();
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_R);
    return ret;
}

uint64_t cpu_ldq_be_mmu(CPUArchState *env, abi_ptr addr,
                        MemOpIdx oi, uintptr_t ra)
{
    void *haddr;
    uint64_t ret;

    validate_memop(oi, MO_BEUQ);
    haddr = cpu_mmu_lookup(env, addr, oi, ra, MMU_DATA_LOAD);
    ret = ldq_be_p(haddr);
    clear_helper_retaddr();
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_R);
    return ret;
}

uint16_t cpu_ldw_le_mmu(CPUArchState *env, abi_ptr addr,
                        MemOpIdx oi, uintptr_t ra)
{
    void *haddr;
    uint16_t ret;

    validate_memop(oi, MO_LEUW);
    haddr = cpu_mmu_lookup(env, addr, oi, ra, MMU_DATA_LOAD);
    ret = lduw_le_p(haddr);
    clear_helper_retaddr();
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_R);
    return ret;
}

uint32_t cpu_ldl_le_mmu(CPUArchState *env, abi_ptr addr,
                        MemOpIdx oi, uintptr_t ra)
{
    void *haddr;
    uint32_t ret;

    validate_memop(oi, MO_LEUL);
    haddr = cpu_mmu_lookup(env, addr, oi, ra, MMU_DATA_LOAD);
    ret = ldl_le_p(haddr);
    clear_helper_retaddr();
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_R);
    return ret;
}

uint64_t cpu_ldq_le_mmu(CPUArchState *env, abi_ptr addr,
                        MemOpIdx oi, uintptr_t ra)
{
    void *haddr;
    uint64_t ret;

    validate_memop(oi, MO_LEUQ);
    haddr = cpu_mmu_lookup(env, addr, oi, ra, MMU_DATA_LOAD);
    ret = ldq_le_p(haddr);
    clear_helper_retaddr();
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_R);
    return ret;
}

void cpu_stb_mmu(CPUArchState *env, abi_ptr addr, uint8_t val,
                 MemOpIdx oi, uintptr_t ra)
{
    void *haddr;

    validate_memop(oi, MO_UB);
    haddr = cpu_mmu_lookup(env, addr, oi, ra, MMU_DATA_STORE);
    stb_p(haddr, val);
    clear_helper_retaddr();
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_W);
}

void cpu_stw_be_mmu(CPUArchState *env, abi_ptr addr, uint16_t val,
                    MemOpIdx oi, uintptr_t ra)
{
    void *haddr;

    validate_memop(oi, MO_BEUW);
    haddr = cpu_mmu_lookup(env, addr, oi, ra, MMU_DATA_STORE);
    stw_be_p(haddr, val);
    clear_helper_retaddr();
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_W);
}

void cpu_stl_be_mmu(CPUArchState *env, abi_ptr addr, uint32_t val,
                    MemOpIdx oi, uintptr_t ra)
{
    void *haddr;

    validate_memop(oi, MO_BEUL);
    haddr = cpu_mmu_lookup(env, addr, oi, ra, MMU_DATA_STORE);
    stl_be_p(haddr, val);
    clear_helper_retaddr();
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_W);
}

void cpu_stq_be_mmu(CPUArchState *env, abi_ptr addr, uint64_t val,
                    MemOpIdx oi, uintptr_t ra)
{
    void *haddr;

    validate_memop(oi, MO_BEUQ);
    haddr = cpu_mmu_lookup(env, addr, oi, ra, MMU_DATA_STORE);
    stq_be_p(haddr, val);
    clear_helper_retaddr();
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_W);
}

void cpu_stw_le_mmu(CPUArchState *env, abi_ptr addr, uint16_t val,
                    MemOpIdx oi, uintptr_t ra)
{
    void *haddr;

    validate_memop(oi, MO_LEUW);
    haddr = cpu_mmu_lookup(env, addr, oi, ra, MMU_DATA_STORE);
    stw_le_p(haddr, val);
    clear_helper_retaddr();
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_W);
}

void cpu_stl_le_mmu(CPUArchState *env, abi_ptr addr, uint32_t val,
                    MemOpIdx oi, uintptr_t ra)
{
    void *haddr;

    validate_memop(oi, MO_LEUL);
    haddr = cpu_mmu_lookup(env, addr, oi, ra, MMU_DATA_STORE);
    stl_le_p(haddr, val);
    clear_helper_retaddr();
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_W);
}

void cpu_stq_le_mmu(CPUArchState *env, abi_ptr addr, uint64_t val,
                    MemOpIdx oi, uintptr_t ra)
{
    void *haddr;

    validate_memop(oi, MO_LEUQ);
    haddr = cpu_mmu_lookup(env, addr, oi, ra, MMU_DATA_STORE);
    stq_le_p(haddr, val);
    clear_helper_retaddr();
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_W);
}

uint32_t cpu_ldub_code(CPUArchState *env, abi_ptr ptr)
{
    uint32_t ret;

    set_helper_retaddr(1);
    ret = ldub_p(g2h_untagged(ptr));
    clear_helper_retaddr();
    return ret;
}

uint32_t cpu_lduw_code(CPUArchState *env, abi_ptr ptr)
{
    uint32_t ret;

    set_helper_retaddr(1);
    ret = lduw_p(g2h_untagged(ptr));
    clear_helper_retaddr();
    return ret;
}

uint32_t cpu_ldl_code(CPUArchState *env, abi_ptr ptr)
{
    uint32_t ret;

    set_helper_retaddr(1);
    ret = ldl_p(g2h_untagged(ptr));
    clear_helper_retaddr();
    return ret;
}

uint64_t cpu_ldq_code(CPUArchState *env, abi_ptr ptr)
{
    uint64_t ret;

    set_helper_retaddr(1);
    ret = ldq_p(g2h_untagged(ptr));
    clear_helper_retaddr();
    return ret;
}

#include "ldst_common.c.inc"

/*
 * Do not allow unaligned operations to proceed.  Return the host address.
 *
 * @prot may be PAGE_READ, PAGE_WRITE, or PAGE_READ|PAGE_WRITE.
 */
static void *atomic_mmu_lookup(CPUArchState *env, target_ulong addr,
                               MemOpIdx oi, int size, int prot,
                               uintptr_t retaddr)
{
    MemOp mop = get_memop(oi);
    int a_bits = get_alignment_bits(mop);
    void *ret;

    /* Enforce guest required alignment.  */
    if (unlikely(addr & ((1 << a_bits) - 1))) {
        MMUAccessType t = prot == PAGE_READ ? MMU_DATA_LOAD : MMU_DATA_STORE;
        cpu_loop_exit_sigbus(env_cpu(env), addr, t, retaddr);
    }

    /* Enforce qemu required alignment.  */
    if (unlikely(addr & (size - 1))) {
        cpu_loop_exit_atomic(env_cpu(env), retaddr);
    }

    ret = g2h(env_cpu(env), addr);
    set_helper_retaddr(retaddr);
    return ret;
}

#include "atomic_common.c.inc"

/*
 * First set of functions passes in OI and RETADDR.
 * This makes them callable from other helpers.
 */

#define ATOMIC_NAME(X) \
    glue(glue(glue(cpu_atomic_ ## X, SUFFIX), END), _mmu)
#define ATOMIC_MMU_CLEANUP do { clear_helper_retaddr(); } while (0)

#define DATA_SIZE 1
#include "atomic_template.h"

#define DATA_SIZE 2
#include "atomic_template.h"

#define DATA_SIZE 4
#include "atomic_template.h"

#ifdef CONFIG_ATOMIC64
#define DATA_SIZE 8
#include "atomic_template.h"
#endif

#if HAVE_ATOMIC128 || HAVE_CMPXCHG128
#define DATA_SIZE 16
#include "atomic_template.h"
#endif
