/*
 *  HPPA memory access helper routines
 *
 *  Copyright (c) 2017 Helge Deller
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
#include "qemu/log.h"
#include "cpu.h"
#include "exec/cputlb.h"
#include "accel/tcg/cpu-mmu-index.h"
#include "accel/tcg/probe.h"
#include "exec/page-protection.h"
#include "exec/target_page.h"
#include "exec/helper-proto.h"
#include "hw/core/cpu.h"
#include "trace.h"

hwaddr hppa_abs_to_phys_pa2_w1(vaddr addr)
{
    /*
     * Figure H-8 "62-bit Absolute Accesses when PSW W-bit is 1" describes
     * an algorithm in which a 62-bit absolute address is transformed to
     * a 64-bit physical address.  This must then be combined with that
     * pictured in Figure H-11 "Physical Address Space Mapping", in which
     * the full physical address is truncated to the N-bit physical address
     * supported by the implementation.
     *
     * Since the supported physical address space is below 54 bits, the
     * H-8 algorithm is moot and all that is left is to truncate.
     */
    QEMU_BUILD_BUG_ON(TARGET_PHYS_ADDR_SPACE_BITS > 54);
    return sextract64(addr, 0, TARGET_PHYS_ADDR_SPACE_BITS);
}

hwaddr hppa_abs_to_phys_pa2_w0(vaddr addr)
{
    /*
     * See Figure H-10, "Absolute Accesses when PSW W-bit is 0",
     * combined with Figure H-11, as above.
     */
    if (likely(extract32(addr, 28, 4) != 0xf)) {
        /* Memory address space */
        addr = (uint32_t)addr;
    } else if (extract32(addr, 24, 4) != 0) {
        /* I/O address space */
        addr = (int32_t)addr;
    } else {
        /*
         * PDC address space:
         * Figures H-10 and H-11 of the parisc2.0 spec do not specify
         * where to map into the 64-bit PDC address space.
         * We map with an offset which equals the 32-bit address, which
         * is what can be seen on physical machines too.
         */
        addr = (uint32_t)addr;
        addr |= -1ull << (TARGET_PHYS_ADDR_SPACE_BITS - 4);
    }
    return addr;
}

static HPPATLBEntry *hppa_find_tlb(CPUHPPAState *env, vaddr addr)
{
    IntervalTreeNode *i = interval_tree_iter_first(&env->tlb_root, addr, addr);

    if (i) {
        HPPATLBEntry *ent = container_of(i, HPPATLBEntry, itree);
        trace_hppa_tlb_find_entry(env, ent, ent->entry_valid,
                                  ent->itree.start, ent->itree.last, ent->pa);
        return ent;
    }
    trace_hppa_tlb_find_entry_not_found(env, addr);
    return NULL;
}

static void hppa_flush_tlb_ent(CPUHPPAState *env, HPPATLBEntry *ent,
                               bool force_flush_btlb)
{
    CPUState *cs = env_cpu(env);
    bool is_btlb;

    if (!ent->entry_valid) {
        return;
    }

    trace_hppa_tlb_flush_ent(env, ent, ent->itree.start,
                             ent->itree.last, ent->pa);

    tlb_flush_range_by_mmuidx(cs, ent->itree.start,
                              ent->itree.last - ent->itree.start + 1,
                              HPPA_MMU_FLUSH_MASK, TARGET_LONG_BITS);

    /* Never clear BTLBs, unless forced to do so. */
    is_btlb = ent < &env->tlb[HPPA_BTLB_ENTRIES(env)];
    if (is_btlb && !force_flush_btlb) {
        return;
    }

    interval_tree_remove(&ent->itree, &env->tlb_root);
    memset(ent, 0, sizeof(*ent));

    if (!is_btlb) {
        ent->unused_next = env->tlb_unused;
        env->tlb_unused = ent;
    }
}

static void hppa_flush_tlb_range(CPUHPPAState *env, vaddr va_b, vaddr va_e)
{
    IntervalTreeNode *i, *n;

    i = interval_tree_iter_first(&env->tlb_root, va_b, va_e);
    for (; i ; i = n) {
        HPPATLBEntry *ent = container_of(i, HPPATLBEntry, itree);

        /*
         * Find the next entry now: In the normal case the current entry
         * will be removed, but in the BTLB case it will remain.
         */
        n = interval_tree_iter_next(i, va_b, va_e);
        hppa_flush_tlb_ent(env, ent, false);
    }
}

static HPPATLBEntry *hppa_alloc_tlb_ent(CPUHPPAState *env)
{
    HPPATLBEntry *ent = env->tlb_unused;

    if (ent == NULL) {
        uint32_t btlb_entries = HPPA_BTLB_ENTRIES(env);
        uint32_t i = env->tlb_last;

        if (i < btlb_entries || i >= ARRAY_SIZE(env->tlb)) {
            i = btlb_entries;
        }
        env->tlb_last = i + 1;

        ent = &env->tlb[i];
        hppa_flush_tlb_ent(env, ent, false);
    }

    env->tlb_unused = ent->unused_next;
    return ent;
}

#define ACCESS_ID_MASK 0xffff

/* Return the set of protections allowed by a PID match. */
static int match_prot_id_1(uint32_t access_id, uint32_t prot_id)
{
    if (((access_id ^ (prot_id >> 1)) & ACCESS_ID_MASK) == 0) {
        return (prot_id & 1
                ? PAGE_EXEC | PAGE_READ
                : PAGE_EXEC | PAGE_READ | PAGE_WRITE);
    }
    return 0;
}

static int match_prot_id32(CPUHPPAState *env, uint32_t access_id)
{
    int r, i;

    for (i = CR_PID1; i <= CR_PID4; ++i) {
        r = match_prot_id_1(access_id, env->cr[i]);
        if (r) {
            return r;
        }
    }
    return 0;
}

static int match_prot_id64(CPUHPPAState *env, uint32_t access_id)
{
    int r, i;

    for (i = CR_PID1; i <= CR_PID4; ++i) {
        r = match_prot_id_1(access_id, env->cr[i]);
        if (r) {
            return r;
        }
        r = match_prot_id_1(access_id, env->cr[i] >> 32);
        if (r) {
            return r;
        }
    }
    return 0;
}

int hppa_get_physical_address(CPUHPPAState *env, vaddr addr, int mmu_idx,
                              int type, MemOp mop, hwaddr *pphys, int *pprot)
{
    hwaddr phys;
    int prot, r_prot, w_prot, x_prot, priv;
    HPPATLBEntry *ent;
    int ret = -1;

    /* Virtual translation disabled.  Map absolute to physical.  */
    if (MMU_IDX_MMU_DISABLED(mmu_idx)) {
        switch (mmu_idx) {
        case MMU_ABS_W_IDX:
            phys = hppa_abs_to_phys_pa2_w1(addr);
            break;
        case MMU_ABS_IDX:
            if (hppa_is_pa20(env)) {
                phys = hppa_abs_to_phys_pa2_w0(addr);
            } else {
                phys = (uint32_t)addr;
            }
            break;
        default:
            g_assert_not_reached();
        }
        prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        goto egress_align;
    }

    /* Find a valid tlb entry that matches the virtual address.  */
    ent = hppa_find_tlb(env, addr);
    if (ent == NULL) {
        phys = 0;
        prot = 0;
        ret = (type == PAGE_EXEC) ? EXCP_ITLB_MISS : EXCP_DTLB_MISS;
        goto egress;
    }

    /* We now know the physical address.  */
    phys = ent->pa + (addr - ent->itree.start);

    /* Map TLB access_rights field to QEMU protection.  */
    priv = MMU_IDX_TO_PRIV(mmu_idx);
    r_prot = (priv <= ent->ar_pl1) * PAGE_READ;
    w_prot = (priv <= ent->ar_pl2) * PAGE_WRITE;
    x_prot = (ent->ar_pl2 <= priv && priv <= ent->ar_pl1) * PAGE_EXEC;
    switch (ent->ar_type) {
    case 0: /* read-only: data page */
        prot = r_prot;
        break;
    case 1: /* read/write: dynamic data page */
        prot = r_prot | w_prot;
        break;
    case 2: /* read/execute: normal code page */
        prot = r_prot | x_prot;
        break;
    case 3: /* read/write/execute: dynamic code page */
        prot = r_prot | w_prot | x_prot;
        break;
    default: /* execute: promote to privilege level type & 3 */
        prot = x_prot;
        break;
    }

    /*
     * No guest access type indicates a non-architectural access from
     * within QEMU.  Bypass checks for access, D, B, P and T bits.
     */
    if (type == 0) {
        goto egress;
    }

    if (unlikely(!(prot & type))) {
        /* Not allowed -- Inst/Data Memory Access Rights Fault. */
        ret = (type & PAGE_EXEC) ? EXCP_IMP : EXCP_DMAR;
        goto egress;
    }

    /* access_id == 0 means public page and no check is performed */
    if (ent->access_id && MMU_IDX_TO_P(mmu_idx)) {
        int access_prot = (hppa_is_pa20(env)
                           ? match_prot_id64(env, ent->access_id)
                           : match_prot_id32(env, ent->access_id));
        if (unlikely(!(type & access_prot))) {
            /* Not allowed -- Inst/Data Memory Protection Id Fault. */
            ret = type & PAGE_EXEC ? EXCP_IMP : EXCP_DMPI;
            goto egress;
        }
        /* Otherwise exclude permissions not allowed (i.e WD). */
        prot &= access_prot;
    }

    /*
     * In reverse priority order, check for conditions which raise faults.
     * Remove PROT bits that cover the condition we want to check,
     * so that the resulting PROT will force a re-check of the
     * architectural TLB entry for the next access.
     */
    if (unlikely(ent->t)) {
        prot &= PAGE_EXEC;
        if (!(type & PAGE_EXEC)) {
            /* The T bit is set -- Page Reference Fault.  */
            ret = EXCP_PAGE_REF;
        }
    }
    if (unlikely(!ent->d)) {
        prot &= PAGE_READ | PAGE_EXEC;
        if (type & PAGE_WRITE) {
            /* The D bit is not set -- TLB Dirty Bit Fault.  */
            ret = EXCP_TLB_DIRTY;
        }
    }
    if (unlikely(ent->b)) {
        prot &= PAGE_READ | PAGE_EXEC;
        if (type & PAGE_WRITE) {
            /*
             * The B bit is set -- Data Memory Break Fault.
             * Except when PSW_X is set, allow this single access to succeed.
             * The write bit will be invalidated for subsequent accesses.
             */
            if (env->psw_xb & PSW_X) {
                prot |= PAGE_WRITE_INV;
            } else {
                ret = EXCP_DMB;
            }
        }
    }

 egress_align:
    if (addr & ((1u << memop_alignment_bits(mop)) - 1)) {
        ret = EXCP_UNALIGN;
    }

 egress:
    *pphys = phys;
    *pprot = prot;
    trace_hppa_tlb_get_physical_address(env, ret, prot, addr, phys);
    return ret;
}

hwaddr hppa_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    HPPACPU *cpu = HPPA_CPU(cs);
    hwaddr phys;
    int prot, excp, mmu_idx;

    /* If the (data) mmu is disabled, bypass translation.  */
    /* ??? We really ought to know if the code mmu is disabled too,
       in order to get the correct debugging dumps.  */
    mmu_idx = (cpu->env.psw & PSW_D ? MMU_KERNEL_IDX :
               cpu->env.psw & PSW_W ? MMU_ABS_W_IDX : MMU_ABS_IDX);

    excp = hppa_get_physical_address(&cpu->env, addr, mmu_idx, 0, 0,
                                     &phys, &prot);

    /* Since we're translating for debugging, the only error that is a
       hard error is no translation at all.  Otherwise, while a real cpu
       access might not have permission, the debugger does.  */
    return excp == EXCP_DTLB_MISS ? -1 : phys;
}

void hppa_set_ior_and_isr(CPUHPPAState *env, vaddr addr, bool mmu_disabled)
{
    if (env->psw & PSW_Q) {
        /*
         * For pa1.x, the offset and space never overlap, and so we
         * simply extract the high and low part of the virtual address.
         *
         * For pa2.0, the formation of these are described in section
         * "Interruption Parameter Registers", page 2-15.
         */
        env->cr[CR_IOR] = (uint32_t)addr;
        env->cr[CR_ISR] = addr >> 32;

        if (hppa_is_pa20(env)) {
            if (mmu_disabled) {
                /*
                 * If data translation was disabled, the ISR contains
                 * the upper portion of the abs address, zero-extended.
                 */
                env->cr[CR_ISR] &= 0x3fffffff;
            } else {
                /*
                 * If data translation was enabled, the upper two bits
                 * of the IOR (the b field) are equal to the two space
                 * bits from the base register used to form the gva.
                 */
                uint64_t b;

                b = env->unwind_breg ? env->gr[env->unwind_breg] : 0;
                b >>= (env->psw & PSW_W ? 62 : 30);
                env->cr[CR_IOR] |= b << 62;
            }
        }
    }
}

G_NORETURN static void
raise_exception_with_ior(CPUHPPAState *env, int excp, uintptr_t retaddr,
                         vaddr addr, bool mmu_disabled)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = excp;
    cpu_restore_state(cs, retaddr);
    hppa_set_ior_and_isr(env, addr, mmu_disabled);

    cpu_loop_exit(cs);
}

void hppa_cpu_do_transaction_failed(CPUState *cs, hwaddr physaddr,
                                     vaddr addr, unsigned size,
                                     MMUAccessType access_type,
                                     int mmu_idx, MemTxAttrs attrs,
                                     MemTxResult response, uintptr_t retaddr)
{
    CPUHPPAState *env = cpu_env(cs);

    qemu_log_mask(LOG_GUEST_ERROR, "HPMC at " TARGET_FMT_lx ":" TARGET_FMT_lx
                " while accessing I/O at %#08" HWADDR_PRIx "\n",
                env->iasq_f, env->iaoq_f, physaddr);

    /* FIXME: Enable HPMC exceptions when firmware has clean device probing */
    if (0) {
        raise_exception_with_ior(env, EXCP_HPMC, retaddr, addr,
                                 MMU_IDX_MMU_DISABLED(mmu_idx));
    }
}

bool hppa_cpu_tlb_fill_align(CPUState *cs, CPUTLBEntryFull *out, vaddr addr,
                             MMUAccessType type, int mmu_idx,
                             MemOp memop, int size, bool probe, uintptr_t ra)
{
    CPUHPPAState *env = cpu_env(cs);
    int prot, excp, a_prot;
    hwaddr phys;

    switch (type) {
    case MMU_INST_FETCH:
        a_prot = PAGE_EXEC;
        break;
    case MMU_DATA_STORE:
        a_prot = PAGE_WRITE;
        break;
    default:
        a_prot = PAGE_READ;
        break;
    }

    excp = hppa_get_physical_address(env, addr, mmu_idx, a_prot, memop,
                                     &phys, &prot);
    if (unlikely(excp >= 0)) {
        if (probe) {
            return false;
        }
        trace_hppa_tlb_fill_excp(env, addr, size, type, mmu_idx);

        /* Failure.  Raise the indicated exception.  */
        raise_exception_with_ior(env, excp, ra, addr,
                                 MMU_IDX_MMU_DISABLED(mmu_idx));
    }

    trace_hppa_tlb_fill_success(env, addr & TARGET_PAGE_MASK,
                                phys & TARGET_PAGE_MASK, size, type, mmu_idx);

    /*
     * Success!  Store the translation into the QEMU TLB.
     * Note that we always install a single-page entry, because that
     * is what works best with softmmu -- anything else will trigger
     * the large page protection mask.  We do not require this,
     * because we record the large page here in the hppa tlb.
     */
    memset(out, 0, sizeof(*out));
    out->phys_addr = phys;
    out->prot = prot;
    out->attrs = MEMTXATTRS_UNSPECIFIED;
    out->lg_page_size = TARGET_PAGE_BITS;

    return true;
}

/* Insert (Insn/Data) TLB Address.  Note this is PA 1.1 only.  */
void HELPER(itlba_pa11)(CPUHPPAState *env, target_ulong addr, target_ulong reg)
{
    HPPATLBEntry *ent;

    /* Zap any old entries covering ADDR. */
    addr &= TARGET_PAGE_MASK;
    hppa_flush_tlb_range(env, addr, addr + TARGET_PAGE_SIZE - 1);

    ent = env->tlb_partial;
    if (ent == NULL) {
        ent = hppa_alloc_tlb_ent(env);
        env->tlb_partial = ent;
    }

    /* Note that ent->entry_valid == 0 already.  */
    ent->itree.start = addr;
    ent->itree.last = addr + TARGET_PAGE_SIZE - 1;
    ent->pa = extract32(reg, 5, 20) << TARGET_PAGE_BITS;
    trace_hppa_tlb_itlba(env, ent, ent->itree.start, ent->itree.last, ent->pa);
}

static void set_access_bits_pa11(CPUHPPAState *env, HPPATLBEntry *ent,
                                 target_ulong reg)
{
    ent->access_id = extract32(reg, 1, 18);
    ent->u = extract32(reg, 19, 1);
    ent->ar_pl2 = extract32(reg, 20, 2);
    ent->ar_pl1 = extract32(reg, 22, 2);
    ent->ar_type = extract32(reg, 24, 3);
    ent->b = extract32(reg, 27, 1);
    ent->d = extract32(reg, 28, 1);
    ent->t = extract32(reg, 29, 1);
    ent->entry_valid = 1;

    interval_tree_insert(&ent->itree, &env->tlb_root);
    trace_hppa_tlb_itlbp(env, ent, ent->access_id, ent->u, ent->ar_pl2,
                         ent->ar_pl1, ent->ar_type, ent->b, ent->d, ent->t);
}

/* Insert (Insn/Data) TLB Protection.  Note this is PA 1.1 only.  */
void HELPER(itlbp_pa11)(CPUHPPAState *env, target_ulong addr, target_ulong reg)
{
    HPPATLBEntry *ent = env->tlb_partial;

    if (ent) {
        env->tlb_partial = NULL;
        if (ent->itree.start <= addr && addr <= ent->itree.last) {
            set_access_bits_pa11(env, ent, reg);
            return;
        }
    }
    qemu_log_mask(LOG_GUEST_ERROR, "ITLBP not following ITLBA\n");
}

static void itlbt_pa20(CPUHPPAState *env, target_ulong r1,
                       target_ulong r2, vaddr va_b)
{
    HPPATLBEntry *ent;
    vaddr va_e;
    uint64_t va_size;
    int mask_shift;

    mask_shift = 2 * (r1 & 0xf);
    va_size = (uint64_t)TARGET_PAGE_SIZE << mask_shift;
    va_b &= -va_size;
    va_e = va_b + va_size - 1;

    hppa_flush_tlb_range(env, va_b, va_e);
    ent = hppa_alloc_tlb_ent(env);

    ent->itree.start = va_b;
    ent->itree.last = va_e;

    /* Extract all 52 bits present in the page table entry. */
    ent->pa = r1 << (TARGET_PAGE_BITS - 5);
    /* Align per the page size. */
    ent->pa &= TARGET_PAGE_MASK << mask_shift;
    /* Ignore the bits beyond physical address space. */
    ent->pa = sextract64(ent->pa, 0, TARGET_PHYS_ADDR_SPACE_BITS);

    ent->t = extract64(r2, 61, 1);
    ent->d = extract64(r2, 60, 1);
    ent->b = extract64(r2, 59, 1);
    ent->ar_type = extract64(r2, 56, 3);
    ent->ar_pl1 = extract64(r2, 54, 2);
    ent->ar_pl2 = extract64(r2, 52, 2);
    ent->u = extract64(r2, 51, 1);
    /* o = bit 50 */
    /* p = bit 49 */
    ent->access_id = extract64(r2, 1, 31);
    ent->entry_valid = 1;

    interval_tree_insert(&ent->itree, &env->tlb_root);
    trace_hppa_tlb_itlba(env, ent, ent->itree.start, ent->itree.last, ent->pa);
    trace_hppa_tlb_itlbp(env, ent, ent->access_id, ent->u,
                         ent->ar_pl2, ent->ar_pl1, ent->ar_type,
                         ent->b, ent->d, ent->t);
}

void HELPER(idtlbt_pa20)(CPUHPPAState *env, target_ulong r1, target_ulong r2)
{
    vaddr va_b = deposit64(env->cr[CR_IOR], 32, 32, env->cr[CR_ISR]);
    itlbt_pa20(env, r1, r2, va_b);
}

void HELPER(iitlbt_pa20)(CPUHPPAState *env, target_ulong r1, target_ulong r2)
{
    vaddr va_b = deposit64(env->cr[CR_IIAOQ], 32, 32, env->cr[CR_IIASQ]);
    itlbt_pa20(env, r1, r2, va_b);
}

/* Purge (Insn/Data) TLB. */
static void ptlb_work(CPUState *cpu, run_on_cpu_data data)
{
    vaddr start = data.target_ptr;
    vaddr end;

    /*
     * PA2.0 allows a range of pages encoded into GR[b], which we have
     * copied into the bottom bits of the otherwise page-aligned address.
     * PA1.x will always provide zero here, for a single page flush.
     */
    end = start & 0xf;
    start &= TARGET_PAGE_MASK;
    end = (vaddr)TARGET_PAGE_SIZE << (2 * end);
    end = start + end - 1;

    hppa_flush_tlb_range(cpu_env(cpu), start, end);
}

/* This is local to the current cpu. */
void HELPER(ptlb_l)(CPUHPPAState *env, target_ulong addr)
{
    trace_hppa_tlb_ptlb_local(env);
    ptlb_work(env_cpu(env), RUN_ON_CPU_TARGET_PTR(addr));
}

/* This is synchronous across all processors.  */
void HELPER(ptlb)(CPUHPPAState *env, target_ulong addr)
{
    CPUState *src = env_cpu(env);
    CPUState *cpu;
    bool wait = false;

    trace_hppa_tlb_ptlb(env);
    run_on_cpu_data data = RUN_ON_CPU_TARGET_PTR(addr);

    CPU_FOREACH(cpu) {
        if (cpu != src) {
            async_run_on_cpu(cpu, ptlb_work, data);
            wait = true;
        }
    }
    if (wait) {
        async_safe_run_on_cpu(src, ptlb_work, data);
    } else {
        ptlb_work(src, data);
    }
}

void hppa_ptlbe(CPUHPPAState *env)
{
    uint32_t btlb_entries = HPPA_BTLB_ENTRIES(env);
    uint32_t i;

    /* Zap the (non-btlb) tlb entries themselves. */
    memset(&env->tlb[btlb_entries], 0,
           sizeof(env->tlb) - btlb_entries * sizeof(env->tlb[0]));
    env->tlb_last = btlb_entries;
    env->tlb_partial = NULL;

    /* Put them all onto the unused list. */
    env->tlb_unused = &env->tlb[btlb_entries];
    for (i = btlb_entries; i < ARRAY_SIZE(env->tlb) - 1; ++i) {
        env->tlb[i].unused_next = &env->tlb[i + 1];
    }

    /* Re-initialize the interval tree with only the btlb entries. */
    memset(&env->tlb_root, 0, sizeof(env->tlb_root));
    for (i = 0; i < btlb_entries; ++i) {
        if (env->tlb[i].entry_valid) {
            interval_tree_insert(&env->tlb[i].itree, &env->tlb_root);
        }
    }

    tlb_flush_by_mmuidx(env_cpu(env), HPPA_MMU_FLUSH_MASK);
}

/* Purge (Insn/Data) TLB entry.  This affects an implementation-defined
   number of pages/entries (we choose all), and is local to the cpu.  */
void HELPER(ptlbe)(CPUHPPAState *env)
{
    trace_hppa_tlb_ptlbe(env);
    qemu_log_mask(CPU_LOG_MMU, "FLUSH ALL TLB ENTRIES\n");
    hppa_ptlbe(env);
}

void cpu_hppa_change_prot_id(CPUHPPAState *env)
{
    tlb_flush_by_mmuidx(env_cpu(env), HPPA_MMU_FLUSH_P_MASK);
}

void HELPER(change_prot_id)(CPUHPPAState *env)
{
    cpu_hppa_change_prot_id(env);
}

target_ulong HELPER(lpa)(CPUHPPAState *env, target_ulong addr)
{
    hwaddr phys;
    int prot, excp;

    excp = hppa_get_physical_address(env, addr, MMU_KERNEL_IDX, 0, 0,
                                     &phys, &prot);
    if (excp >= 0) {
        if (excp == EXCP_DTLB_MISS) {
            excp = EXCP_NA_DTLB_MISS;
        }
        trace_hppa_tlb_lpa_failed(env, addr);
        raise_exception_with_ior(env, excp, GETPC(), addr, false);
    }
    trace_hppa_tlb_lpa_success(env, addr, phys);
    return phys;
}

/*
 * diag_btlb() emulates the PDC PDC_BLOCK_TLB firmware call to
 * allow operating systems to modify the Block TLB (BTLB) entries.
 * For implementation details see page 1-13 in
 * https://parisc.wiki.kernel.org/images-parisc/e/ef/Pdc11-v0.96-Ch1-procs.pdf
 */
void HELPER(diag_btlb)(CPUHPPAState *env)
{
    unsigned int phys_page, len, slot;
    int mmu_idx = cpu_mmu_index(env_cpu(env), 0);
    uintptr_t ra = GETPC();
    HPPATLBEntry *btlb;
    uint64_t virt_page;
    uint32_t *vaddr;
    uint32_t btlb_entries = HPPA_BTLB_ENTRIES(env);

    /* BTLBs are not supported on 64-bit CPUs */
    if (btlb_entries == 0) {
        env->gr[28] = -1; /* nonexistent procedure */
        return;
    }

    env->gr[28] = 0; /* PDC_OK */

    switch (env->gr[25]) {
    case 0:
        /* return BTLB parameters */
        qemu_log_mask(CPU_LOG_MMU, "PDC_BLOCK_TLB: PDC_BTLB_INFO\n");
        vaddr = probe_access(env, env->gr[24], 4 * sizeof(uint32_t),
                             MMU_DATA_STORE, mmu_idx, ra);
        if (vaddr == NULL) {
            env->gr[28] = -10; /* invalid argument */
        } else {
            vaddr[0] = cpu_to_be32(1);
            vaddr[1] = cpu_to_be32(16 * 1024);
            vaddr[2] = cpu_to_be32(PA10_BTLB_FIXED);
            vaddr[3] = cpu_to_be32(PA10_BTLB_VARIABLE);
        }
        break;
    case 1:
        /* insert BTLB entry */
        virt_page = env->gr[24];        /* upper 32 bits */
        virt_page <<= 32;
        virt_page |= env->gr[23];       /* lower 32 bits */
        phys_page = env->gr[22];
        len = env->gr[21];
        slot = env->gr[19];
        qemu_log_mask(CPU_LOG_MMU, "PDC_BLOCK_TLB: PDC_BTLB_INSERT "
                    "0x%08llx-0x%08llx: vpage 0x%llx for phys page 0x%04x len %d "
                    "into slot %d\n",
                    (long long) virt_page << TARGET_PAGE_BITS,
                    (long long) (virt_page + len) << TARGET_PAGE_BITS,
                    (long long) virt_page, phys_page, len, slot);
        if (slot < btlb_entries) {
            btlb = &env->tlb[slot];

            /* Force flush of possibly existing BTLB entry. */
            hppa_flush_tlb_ent(env, btlb, true);

            /* Create new BTLB entry */
            btlb->itree.start = virt_page << TARGET_PAGE_BITS;
            btlb->itree.last = btlb->itree.start + len * TARGET_PAGE_SIZE - 1;
            btlb->pa = phys_page << TARGET_PAGE_BITS;
            set_access_bits_pa11(env, btlb, env->gr[20]);
            btlb->t = 0;
            btlb->d = 1;
        } else {
            env->gr[28] = -10; /* invalid argument */
        }
        break;
    case 2:
        /* Purge BTLB entry */
        slot = env->gr[22];
        qemu_log_mask(CPU_LOG_MMU, "PDC_BLOCK_TLB: PDC_BTLB_PURGE slot %d\n",
                                    slot);
        if (slot < btlb_entries) {
            btlb = &env->tlb[slot];
            hppa_flush_tlb_ent(env, btlb, true);
        } else {
            env->gr[28] = -10; /* invalid argument */
        }
        break;
    case 3:
        /* Purge all BTLB entries */
        qemu_log_mask(CPU_LOG_MMU, "PDC_BLOCK_TLB: PDC_BTLB_PURGE_ALL\n");
        for (slot = 0; slot < btlb_entries; slot++) {
            btlb = &env->tlb[slot];
            hppa_flush_tlb_ent(env, btlb, true);
        }
        break;
    default:
        env->gr[28] = -2; /* nonexistent option */
        break;
    }
}

uint64_t HELPER(b_gate_priv)(CPUHPPAState *env, uint64_t iaoq_f)
{
    vaddr gva = hppa_form_gva(env, env->iasq_f, iaoq_f);
    HPPATLBEntry *ent = hppa_find_tlb(env, gva);

    if (ent == NULL) {
        raise_exception_with_ior(env, EXCP_ITLB_MISS, GETPC(), gva, false);
    }

    /*
     * There should be no need to check page permissions, as that will
     * already have been done by tb_lookup via get_page_addr_code.
     * All we need at this point is to check the ar_type.
     *
     * No change for non-gateway pages or for priv decrease.
     */
    if (ent->ar_type & 4) {
        int old_priv = iaoq_f & 3;
        int new_priv = ent->ar_type & 3;

        if (new_priv < old_priv) {
            iaoq_f = (iaoq_f & -4) | new_priv;
        }
    }
    return iaoq_f;
}

void HELPER(update_gva_offset_mask)(CPUHPPAState *env)
{
    update_gva_offset_mask(env);
}
