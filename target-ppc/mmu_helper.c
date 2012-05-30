/*
 *  PowerPC MMU, TLB, SLB and BAT emulation helpers for QEMU.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "cpu.h"
#include "helper.h"

//#define DEBUG_SOFTWARE_TLB

#ifdef DEBUG_SOFTWARE_TLB
#  define LOG_SWTLB(...) qemu_log(__VA_ARGS__)
#else
#  define LOG_SWTLB(...) do { } while (0)
#endif

/*****************************************************************************/
/* SPR accesses */

#if !defined(CONFIG_USER_ONLY)
void helper_store_ibatu(CPUPPCState *env, uint32_t nr, target_ulong val)
{
    ppc_store_ibatu(env, nr, val);
}

void helper_store_ibatl(CPUPPCState *env, uint32_t nr, target_ulong val)
{
    ppc_store_ibatl(env, nr, val);
}

void helper_store_dbatu(CPUPPCState *env, uint32_t nr, target_ulong val)
{
    ppc_store_dbatu(env, nr, val);
}

void helper_store_dbatl(CPUPPCState *env, uint32_t nr, target_ulong val)
{
    ppc_store_dbatl(env, nr, val);
}

void helper_store_601_batl(CPUPPCState *env, uint32_t nr, target_ulong val)
{
    ppc_store_ibatl_601(env, nr, val);
}

void helper_store_601_batu(CPUPPCState *env, uint32_t nr, target_ulong val)
{
    ppc_store_ibatu_601(env, nr, val);
}

/* Segment registers load and store */
target_ulong helper_load_sr(CPUPPCState *env, target_ulong sr_num)
{
#if defined(TARGET_PPC64)
    if (env->mmu_model & POWERPC_MMU_64) {
        return ppc_load_sr(env, sr_num);
    }
#endif
    return env->sr[sr_num];
}

void helper_store_sr(CPUPPCState *env, target_ulong sr_num, target_ulong val)
{
    ppc_store_sr(env, sr_num, val);
}

/* SLB management */
#if defined(TARGET_PPC64)
void helper_store_slb(CPUPPCState *env, target_ulong rb, target_ulong rs)
{
    if (ppc_store_slb(env, rb, rs) < 0) {
        helper_raise_exception_err(env, POWERPC_EXCP_PROGRAM,
                                   POWERPC_EXCP_INVAL);
    }
}

target_ulong helper_load_slb_esid(CPUPPCState *env, target_ulong rb)
{
    target_ulong rt = 0;

    if (ppc_load_slb_esid(env, rb, &rt) < 0) {
        helper_raise_exception_err(env, POWERPC_EXCP_PROGRAM,
                                   POWERPC_EXCP_INVAL);
    }
    return rt;
}

target_ulong helper_load_slb_vsid(CPUPPCState *env, target_ulong rb)
{
    target_ulong rt = 0;

    if (ppc_load_slb_vsid(env, rb, &rt) < 0) {
        helper_raise_exception_err(env, POWERPC_EXCP_PROGRAM,
                                   POWERPC_EXCP_INVAL);
    }
    return rt;
}

void helper_slbia(CPUPPCState *env)
{
    ppc_slb_invalidate_all(env);
}

void helper_slbie(CPUPPCState *env, target_ulong addr)
{
    ppc_slb_invalidate_one(env, addr);
}

#endif /* defined(TARGET_PPC64) */

/* TLB management */
void helper_tlbia(CPUPPCState *env)
{
    ppc_tlb_invalidate_all(env);
}

void helper_tlbie(CPUPPCState *env, target_ulong addr)
{
    ppc_tlb_invalidate_one(env, addr);
}

/* Software driven TLBs management */
/* PowerPC 602/603 software TLB load instructions helpers */
static void do_6xx_tlb(CPUPPCState *env, target_ulong new_EPN, int is_code)
{
    target_ulong RPN, CMP, EPN;
    int way;

    RPN = env->spr[SPR_RPA];
    if (is_code) {
        CMP = env->spr[SPR_ICMP];
        EPN = env->spr[SPR_IMISS];
    } else {
        CMP = env->spr[SPR_DCMP];
        EPN = env->spr[SPR_DMISS];
    }
    way = (env->spr[SPR_SRR1] >> 17) & 1;
    (void)EPN; /* avoid a compiler warning */
    LOG_SWTLB("%s: EPN " TARGET_FMT_lx " " TARGET_FMT_lx " PTE0 " TARGET_FMT_lx
              " PTE1 " TARGET_FMT_lx " way %d\n", __func__, new_EPN, EPN, CMP,
              RPN, way);
    /* Store this TLB */
    ppc6xx_tlb_store(env, (uint32_t)(new_EPN & TARGET_PAGE_MASK),
                     way, is_code, CMP, RPN);
}

void helper_6xx_tlbd(CPUPPCState *env, target_ulong EPN)
{
    do_6xx_tlb(env, EPN, 0);
}

void helper_6xx_tlbi(CPUPPCState *env, target_ulong EPN)
{
    do_6xx_tlb(env, EPN, 1);
}

/* PowerPC 74xx software TLB load instructions helpers */
static void do_74xx_tlb(CPUPPCState *env, target_ulong new_EPN, int is_code)
{
    target_ulong RPN, CMP, EPN;
    int way;

    RPN = env->spr[SPR_PTELO];
    CMP = env->spr[SPR_PTEHI];
    EPN = env->spr[SPR_TLBMISS] & ~0x3;
    way = env->spr[SPR_TLBMISS] & 0x3;
    (void)EPN; /* avoid a compiler warning */
    LOG_SWTLB("%s: EPN " TARGET_FMT_lx " " TARGET_FMT_lx " PTE0 " TARGET_FMT_lx
              " PTE1 " TARGET_FMT_lx " way %d\n", __func__, new_EPN, EPN, CMP,
              RPN, way);
    /* Store this TLB */
    ppc6xx_tlb_store(env, (uint32_t)(new_EPN & TARGET_PAGE_MASK),
                     way, is_code, CMP, RPN);
}

void helper_74xx_tlbd(CPUPPCState *env, target_ulong EPN)
{
    do_74xx_tlb(env, EPN, 0);
}

void helper_74xx_tlbi(CPUPPCState *env, target_ulong EPN)
{
    do_74xx_tlb(env, EPN, 1);
}

/*****************************************************************************/
/* PowerPC 601 specific instructions (POWER bridge) */

target_ulong helper_rac(CPUPPCState *env, target_ulong addr)
{
    mmu_ctx_t ctx;
    int nb_BATs;
    target_ulong ret = 0;

    /* We don't have to generate many instances of this instruction,
     * as rac is supervisor only.
     */
    /* XXX: FIX THIS: Pretend we have no BAT */
    nb_BATs = env->nb_BATs;
    env->nb_BATs = 0;
    if (get_physical_address(env, &ctx, addr, 0, ACCESS_INT) == 0) {
        ret = ctx.raddr;
    }
    env->nb_BATs = nb_BATs;
    return ret;
}

static inline target_ulong booke_tlb_to_page_size(int size)
{
    return 1024 << (2 * size);
}

static inline int booke_page_size_to_tlb(target_ulong page_size)
{
    int size;

    switch (page_size) {
    case 0x00000400UL:
        size = 0x0;
        break;
    case 0x00001000UL:
        size = 0x1;
        break;
    case 0x00004000UL:
        size = 0x2;
        break;
    case 0x00010000UL:
        size = 0x3;
        break;
    case 0x00040000UL:
        size = 0x4;
        break;
    case 0x00100000UL:
        size = 0x5;
        break;
    case 0x00400000UL:
        size = 0x6;
        break;
    case 0x01000000UL:
        size = 0x7;
        break;
    case 0x04000000UL:
        size = 0x8;
        break;
    case 0x10000000UL:
        size = 0x9;
        break;
    case 0x40000000UL:
        size = 0xA;
        break;
#if defined(TARGET_PPC64)
    case 0x000100000000ULL:
        size = 0xB;
        break;
    case 0x000400000000ULL:
        size = 0xC;
        break;
    case 0x001000000000ULL:
        size = 0xD;
        break;
    case 0x004000000000ULL:
        size = 0xE;
        break;
    case 0x010000000000ULL:
        size = 0xF;
        break;
#endif
    default:
        size = -1;
        break;
    }

    return size;
}

/* Helpers for 4xx TLB management */
#define PPC4XX_TLB_ENTRY_MASK       0x0000003f  /* Mask for 64 TLB entries */

#define PPC4XX_TLBHI_V              0x00000040
#define PPC4XX_TLBHI_E              0x00000020
#define PPC4XX_TLBHI_SIZE_MIN       0
#define PPC4XX_TLBHI_SIZE_MAX       7
#define PPC4XX_TLBHI_SIZE_DEFAULT   1
#define PPC4XX_TLBHI_SIZE_SHIFT     7
#define PPC4XX_TLBHI_SIZE_MASK      0x00000007

#define PPC4XX_TLBLO_EX             0x00000200
#define PPC4XX_TLBLO_WR             0x00000100
#define PPC4XX_TLBLO_ATTR_MASK      0x000000FF
#define PPC4XX_TLBLO_RPN_MASK       0xFFFFFC00

target_ulong helper_4xx_tlbre_hi(CPUPPCState *env, target_ulong entry)
{
    ppcemb_tlb_t *tlb;
    target_ulong ret;
    int size;

    entry &= PPC4XX_TLB_ENTRY_MASK;
    tlb = &env->tlb.tlbe[entry];
    ret = tlb->EPN;
    if (tlb->prot & PAGE_VALID) {
        ret |= PPC4XX_TLBHI_V;
    }
    size = booke_page_size_to_tlb(tlb->size);
    if (size < PPC4XX_TLBHI_SIZE_MIN || size > PPC4XX_TLBHI_SIZE_MAX) {
        size = PPC4XX_TLBHI_SIZE_DEFAULT;
    }
    ret |= size << PPC4XX_TLBHI_SIZE_SHIFT;
    env->spr[SPR_40x_PID] = tlb->PID;
    return ret;
}

target_ulong helper_4xx_tlbre_lo(CPUPPCState *env, target_ulong entry)
{
    ppcemb_tlb_t *tlb;
    target_ulong ret;

    entry &= PPC4XX_TLB_ENTRY_MASK;
    tlb = &env->tlb.tlbe[entry];
    ret = tlb->RPN;
    if (tlb->prot & PAGE_EXEC) {
        ret |= PPC4XX_TLBLO_EX;
    }
    if (tlb->prot & PAGE_WRITE) {
        ret |= PPC4XX_TLBLO_WR;
    }
    return ret;
}

void helper_4xx_tlbwe_hi(CPUPPCState *env, target_ulong entry,
                         target_ulong val)
{
    ppcemb_tlb_t *tlb;
    target_ulong page, end;

    LOG_SWTLB("%s entry %d val " TARGET_FMT_lx "\n", __func__, (int)entry,
              val);
    entry &= PPC4XX_TLB_ENTRY_MASK;
    tlb = &env->tlb.tlbe[entry];
    /* Invalidate previous TLB (if it's valid) */
    if (tlb->prot & PAGE_VALID) {
        end = tlb->EPN + tlb->size;
        LOG_SWTLB("%s: invalidate old TLB %d start " TARGET_FMT_lx " end "
                  TARGET_FMT_lx "\n", __func__, (int)entry, tlb->EPN, end);
        for (page = tlb->EPN; page < end; page += TARGET_PAGE_SIZE) {
            tlb_flush_page(env, page);
        }
    }
    tlb->size = booke_tlb_to_page_size((val >> PPC4XX_TLBHI_SIZE_SHIFT)
                                       & PPC4XX_TLBHI_SIZE_MASK);
    /* We cannot handle TLB size < TARGET_PAGE_SIZE.
     * If this ever occurs, one should use the ppcemb target instead
     * of the ppc or ppc64 one
     */
    if ((val & PPC4XX_TLBHI_V) && tlb->size < TARGET_PAGE_SIZE) {
        cpu_abort(env, "TLB size " TARGET_FMT_lu " < %u "
                  "are not supported (%d)\n",
                  tlb->size, TARGET_PAGE_SIZE, (int)((val >> 7) & 0x7));
    }
    tlb->EPN = val & ~(tlb->size - 1);
    if (val & PPC4XX_TLBHI_V) {
        tlb->prot |= PAGE_VALID;
        if (val & PPC4XX_TLBHI_E) {
            /* XXX: TO BE FIXED */
            cpu_abort(env,
                      "Little-endian TLB entries are not supported by now\n");
        }
    } else {
        tlb->prot &= ~PAGE_VALID;
    }
    tlb->PID = env->spr[SPR_40x_PID]; /* PID */
    LOG_SWTLB("%s: set up TLB %d RPN " TARGET_FMT_plx " EPN " TARGET_FMT_lx
              " size " TARGET_FMT_lx " prot %c%c%c%c PID %d\n", __func__,
              (int)entry, tlb->RPN, tlb->EPN, tlb->size,
              tlb->prot & PAGE_READ ? 'r' : '-',
              tlb->prot & PAGE_WRITE ? 'w' : '-',
              tlb->prot & PAGE_EXEC ? 'x' : '-',
              tlb->prot & PAGE_VALID ? 'v' : '-', (int)tlb->PID);
    /* Invalidate new TLB (if valid) */
    if (tlb->prot & PAGE_VALID) {
        end = tlb->EPN + tlb->size;
        LOG_SWTLB("%s: invalidate TLB %d start " TARGET_FMT_lx " end "
                  TARGET_FMT_lx "\n", __func__, (int)entry, tlb->EPN, end);
        for (page = tlb->EPN; page < end; page += TARGET_PAGE_SIZE) {
            tlb_flush_page(env, page);
        }
    }
}

void helper_4xx_tlbwe_lo(CPUPPCState *env, target_ulong entry,
                         target_ulong val)
{
    ppcemb_tlb_t *tlb;

    LOG_SWTLB("%s entry %i val " TARGET_FMT_lx "\n", __func__, (int)entry,
              val);
    entry &= PPC4XX_TLB_ENTRY_MASK;
    tlb = &env->tlb.tlbe[entry];
    tlb->attr = val & PPC4XX_TLBLO_ATTR_MASK;
    tlb->RPN = val & PPC4XX_TLBLO_RPN_MASK;
    tlb->prot = PAGE_READ;
    if (val & PPC4XX_TLBLO_EX) {
        tlb->prot |= PAGE_EXEC;
    }
    if (val & PPC4XX_TLBLO_WR) {
        tlb->prot |= PAGE_WRITE;
    }
    LOG_SWTLB("%s: set up TLB %d RPN " TARGET_FMT_plx " EPN " TARGET_FMT_lx
              " size " TARGET_FMT_lx " prot %c%c%c%c PID %d\n", __func__,
              (int)entry, tlb->RPN, tlb->EPN, tlb->size,
              tlb->prot & PAGE_READ ? 'r' : '-',
              tlb->prot & PAGE_WRITE ? 'w' : '-',
              tlb->prot & PAGE_EXEC ? 'x' : '-',
              tlb->prot & PAGE_VALID ? 'v' : '-', (int)tlb->PID);
}

target_ulong helper_4xx_tlbsx(CPUPPCState *env, target_ulong address)
{
    return ppcemb_tlb_search(env, address, env->spr[SPR_40x_PID]);
}

/* PowerPC 440 TLB management */
void helper_440_tlbwe(CPUPPCState *env, uint32_t word, target_ulong entry,
                      target_ulong value)
{
    ppcemb_tlb_t *tlb;
    target_ulong EPN, RPN, size;
    int do_flush_tlbs;

    LOG_SWTLB("%s word %d entry %d value " TARGET_FMT_lx "\n",
              __func__, word, (int)entry, value);
    do_flush_tlbs = 0;
    entry &= 0x3F;
    tlb = &env->tlb.tlbe[entry];
    switch (word) {
    default:
        /* Just here to please gcc */
    case 0:
        EPN = value & 0xFFFFFC00;
        if ((tlb->prot & PAGE_VALID) && EPN != tlb->EPN) {
            do_flush_tlbs = 1;
        }
        tlb->EPN = EPN;
        size = booke_tlb_to_page_size((value >> 4) & 0xF);
        if ((tlb->prot & PAGE_VALID) && tlb->size < size) {
            do_flush_tlbs = 1;
        }
        tlb->size = size;
        tlb->attr &= ~0x1;
        tlb->attr |= (value >> 8) & 1;
        if (value & 0x200) {
            tlb->prot |= PAGE_VALID;
        } else {
            if (tlb->prot & PAGE_VALID) {
                tlb->prot &= ~PAGE_VALID;
                do_flush_tlbs = 1;
            }
        }
        tlb->PID = env->spr[SPR_440_MMUCR] & 0x000000FF;
        if (do_flush_tlbs) {
            tlb_flush(env, 1);
        }
        break;
    case 1:
        RPN = value & 0xFFFFFC0F;
        if ((tlb->prot & PAGE_VALID) && tlb->RPN != RPN) {
            tlb_flush(env, 1);
        }
        tlb->RPN = RPN;
        break;
    case 2:
        tlb->attr = (tlb->attr & 0x1) | (value & 0x0000FF00);
        tlb->prot = tlb->prot & PAGE_VALID;
        if (value & 0x1) {
            tlb->prot |= PAGE_READ << 4;
        }
        if (value & 0x2) {
            tlb->prot |= PAGE_WRITE << 4;
        }
        if (value & 0x4) {
            tlb->prot |= PAGE_EXEC << 4;
        }
        if (value & 0x8) {
            tlb->prot |= PAGE_READ;
        }
        if (value & 0x10) {
            tlb->prot |= PAGE_WRITE;
        }
        if (value & 0x20) {
            tlb->prot |= PAGE_EXEC;
        }
        break;
    }
}

target_ulong helper_440_tlbre(CPUPPCState *env, uint32_t word,
                              target_ulong entry)
{
    ppcemb_tlb_t *tlb;
    target_ulong ret;
    int size;

    entry &= 0x3F;
    tlb = &env->tlb.tlbe[entry];
    switch (word) {
    default:
        /* Just here to please gcc */
    case 0:
        ret = tlb->EPN;
        size = booke_page_size_to_tlb(tlb->size);
        if (size < 0 || size > 0xF) {
            size = 1;
        }
        ret |= size << 4;
        if (tlb->attr & 0x1) {
            ret |= 0x100;
        }
        if (tlb->prot & PAGE_VALID) {
            ret |= 0x200;
        }
        env->spr[SPR_440_MMUCR] &= ~0x000000FF;
        env->spr[SPR_440_MMUCR] |= tlb->PID;
        break;
    case 1:
        ret = tlb->RPN;
        break;
    case 2:
        ret = tlb->attr & ~0x1;
        if (tlb->prot & (PAGE_READ << 4)) {
            ret |= 0x1;
        }
        if (tlb->prot & (PAGE_WRITE << 4)) {
            ret |= 0x2;
        }
        if (tlb->prot & (PAGE_EXEC << 4)) {
            ret |= 0x4;
        }
        if (tlb->prot & PAGE_READ) {
            ret |= 0x8;
        }
        if (tlb->prot & PAGE_WRITE) {
            ret |= 0x10;
        }
        if (tlb->prot & PAGE_EXEC) {
            ret |= 0x20;
        }
        break;
    }
    return ret;
}

target_ulong helper_440_tlbsx(CPUPPCState *env, target_ulong address)
{
    return ppcemb_tlb_search(env, address, env->spr[SPR_440_MMUCR] & 0xFF);
}

/* PowerPC BookE 2.06 TLB management */

static ppcmas_tlb_t *booke206_cur_tlb(CPUPPCState *env)
{
    uint32_t tlbncfg = 0;
    int esel = (env->spr[SPR_BOOKE_MAS0] & MAS0_ESEL_MASK) >> MAS0_ESEL_SHIFT;
    int ea = (env->spr[SPR_BOOKE_MAS2] & MAS2_EPN_MASK);
    int tlb;

    tlb = (env->spr[SPR_BOOKE_MAS0] & MAS0_TLBSEL_MASK) >> MAS0_TLBSEL_SHIFT;
    tlbncfg = env->spr[SPR_BOOKE_TLB0CFG + tlb];

    if ((tlbncfg & TLBnCFG_HES) && (env->spr[SPR_BOOKE_MAS0] & MAS0_HES)) {
        cpu_abort(env, "we don't support HES yet\n");
    }

    return booke206_get_tlbm(env, tlb, ea, esel);
}

void helper_booke_setpid(CPUPPCState *env, uint32_t pidn, target_ulong pid)
{
    env->spr[pidn] = pid;
    /* changing PIDs mean we're in a different address space now */
    tlb_flush(env, 1);
}

void helper_booke206_tlbwe(CPUPPCState *env)
{
    uint32_t tlbncfg, tlbn;
    ppcmas_tlb_t *tlb;
    uint32_t size_tlb, size_ps;

    switch (env->spr[SPR_BOOKE_MAS0] & MAS0_WQ_MASK) {
    case MAS0_WQ_ALWAYS:
        /* good to go, write that entry */
        break;
    case MAS0_WQ_COND:
        /* XXX check if reserved */
        if (0) {
            return;
        }
        break;
    case MAS0_WQ_CLR_RSRV:
        /* XXX clear entry */
        return;
    default:
        /* no idea what to do */
        return;
    }

    if (((env->spr[SPR_BOOKE_MAS0] & MAS0_ATSEL) == MAS0_ATSEL_LRAT) &&
        !msr_gs) {
        /* XXX we don't support direct LRAT setting yet */
        fprintf(stderr, "cpu: don't support LRAT setting yet\n");
        return;
    }

    tlbn = (env->spr[SPR_BOOKE_MAS0] & MAS0_TLBSEL_MASK) >> MAS0_TLBSEL_SHIFT;
    tlbncfg = env->spr[SPR_BOOKE_TLB0CFG + tlbn];

    tlb = booke206_cur_tlb(env);

    if (!tlb) {
        helper_raise_exception_err(env, POWERPC_EXCP_PROGRAM,
                                   POWERPC_EXCP_INVAL |
                                   POWERPC_EXCP_INVAL_INVAL);
    }

    /* check that we support the targeted size */
    size_tlb = (env->spr[SPR_BOOKE_MAS1] & MAS1_TSIZE_MASK) >> MAS1_TSIZE_SHIFT;
    size_ps = booke206_tlbnps(env, tlbn);
    if ((env->spr[SPR_BOOKE_MAS1] & MAS1_VALID) && (tlbncfg & TLBnCFG_AVAIL) &&
        !(size_ps & (1 << size_tlb))) {
        helper_raise_exception_err(env, POWERPC_EXCP_PROGRAM,
                                   POWERPC_EXCP_INVAL |
                                   POWERPC_EXCP_INVAL_INVAL);
    }

    if (msr_gs) {
        cpu_abort(env, "missing HV implementation\n");
    }
    tlb->mas7_3 = ((uint64_t)env->spr[SPR_BOOKE_MAS7] << 32) |
        env->spr[SPR_BOOKE_MAS3];
    tlb->mas1 = env->spr[SPR_BOOKE_MAS1];

    /* MAV 1.0 only */
    if (!(tlbncfg & TLBnCFG_AVAIL)) {
        /* force !AVAIL TLB entries to correct page size */
        tlb->mas1 &= ~MAS1_TSIZE_MASK;
        /* XXX can be configured in MMUCSR0 */
        tlb->mas1 |= (tlbncfg & TLBnCFG_MINSIZE) >> 12;
    }

    /* XXX needs to change when supporting 64-bit e500 */
    tlb->mas2 = env->spr[SPR_BOOKE_MAS2] & 0xffffffff;

    if (!(tlbncfg & TLBnCFG_IPROT)) {
        /* no IPROT supported by TLB */
        tlb->mas1 &= ~MAS1_IPROT;
    }

    if (booke206_tlb_to_page_size(env, tlb) == TARGET_PAGE_SIZE) {
        tlb_flush_page(env, tlb->mas2 & MAS2_EPN_MASK);
    } else {
        tlb_flush(env, 1);
    }
}

static inline void booke206_tlb_to_mas(CPUPPCState *env, ppcmas_tlb_t *tlb)
{
    int tlbn = booke206_tlbm_to_tlbn(env, tlb);
    int way = booke206_tlbm_to_way(env, tlb);

    env->spr[SPR_BOOKE_MAS0] = tlbn << MAS0_TLBSEL_SHIFT;
    env->spr[SPR_BOOKE_MAS0] |= way << MAS0_ESEL_SHIFT;
    env->spr[SPR_BOOKE_MAS0] |= env->last_way << MAS0_NV_SHIFT;

    env->spr[SPR_BOOKE_MAS1] = tlb->mas1;
    env->spr[SPR_BOOKE_MAS2] = tlb->mas2;
    env->spr[SPR_BOOKE_MAS3] = tlb->mas7_3;
    env->spr[SPR_BOOKE_MAS7] = tlb->mas7_3 >> 32;
}

void helper_booke206_tlbre(CPUPPCState *env)
{
    ppcmas_tlb_t *tlb = NULL;

    tlb = booke206_cur_tlb(env);
    if (!tlb) {
        env->spr[SPR_BOOKE_MAS1] = 0;
    } else {
        booke206_tlb_to_mas(env, tlb);
    }
}

void helper_booke206_tlbsx(CPUPPCState *env, target_ulong address)
{
    ppcmas_tlb_t *tlb = NULL;
    int i, j;
    target_phys_addr_t raddr;
    uint32_t spid, sas;

    spid = (env->spr[SPR_BOOKE_MAS6] & MAS6_SPID_MASK) >> MAS6_SPID_SHIFT;
    sas = env->spr[SPR_BOOKE_MAS6] & MAS6_SAS;

    for (i = 0; i < BOOKE206_MAX_TLBN; i++) {
        int ways = booke206_tlb_ways(env, i);

        for (j = 0; j < ways; j++) {
            tlb = booke206_get_tlbm(env, i, address, j);

            if (!tlb) {
                continue;
            }

            if (ppcmas_tlb_check(env, tlb, &raddr, address, spid)) {
                continue;
            }

            if (sas != ((tlb->mas1 & MAS1_TS) >> MAS1_TS_SHIFT)) {
                continue;
            }

            booke206_tlb_to_mas(env, tlb);
            return;
        }
    }

    /* no entry found, fill with defaults */
    env->spr[SPR_BOOKE_MAS0] = env->spr[SPR_BOOKE_MAS4] & MAS4_TLBSELD_MASK;
    env->spr[SPR_BOOKE_MAS1] = env->spr[SPR_BOOKE_MAS4] & MAS4_TSIZED_MASK;
    env->spr[SPR_BOOKE_MAS2] = env->spr[SPR_BOOKE_MAS4] & MAS4_WIMGED_MASK;
    env->spr[SPR_BOOKE_MAS3] = 0;
    env->spr[SPR_BOOKE_MAS7] = 0;

    if (env->spr[SPR_BOOKE_MAS6] & MAS6_SAS) {
        env->spr[SPR_BOOKE_MAS1] |= MAS1_TS;
    }

    env->spr[SPR_BOOKE_MAS1] |= (env->spr[SPR_BOOKE_MAS6] >> 16)
        << MAS1_TID_SHIFT;

    /* next victim logic */
    env->spr[SPR_BOOKE_MAS0] |= env->last_way << MAS0_ESEL_SHIFT;
    env->last_way++;
    env->last_way &= booke206_tlb_ways(env, 0) - 1;
    env->spr[SPR_BOOKE_MAS0] |= env->last_way << MAS0_NV_SHIFT;
}

static inline void booke206_invalidate_ea_tlb(CPUPPCState *env, int tlbn,
                                              uint32_t ea)
{
    int i;
    int ways = booke206_tlb_ways(env, tlbn);
    target_ulong mask;

    for (i = 0; i < ways; i++) {
        ppcmas_tlb_t *tlb = booke206_get_tlbm(env, tlbn, ea, i);
        if (!tlb) {
            continue;
        }
        mask = ~(booke206_tlb_to_page_size(env, tlb) - 1);
        if (((tlb->mas2 & MAS2_EPN_MASK) == (ea & mask)) &&
            !(tlb->mas1 & MAS1_IPROT)) {
            tlb->mas1 &= ~MAS1_VALID;
        }
    }
}

void helper_booke206_tlbivax(CPUPPCState *env, target_ulong address)
{
    if (address & 0x4) {
        /* flush all entries */
        if (address & 0x8) {
            /* flush all of TLB1 */
            booke206_flush_tlb(env, BOOKE206_FLUSH_TLB1, 1);
        } else {
            /* flush all of TLB0 */
            booke206_flush_tlb(env, BOOKE206_FLUSH_TLB0, 0);
        }
        return;
    }

    if (address & 0x8) {
        /* flush TLB1 entries */
        booke206_invalidate_ea_tlb(env, 1, address);
        tlb_flush(env, 1);
    } else {
        /* flush TLB0 entries */
        booke206_invalidate_ea_tlb(env, 0, address);
        tlb_flush_page(env, address & MAS2_EPN_MASK);
    }
}

void helper_booke206_tlbilx0(CPUPPCState *env, target_ulong address)
{
    /* XXX missing LPID handling */
    booke206_flush_tlb(env, -1, 1);
}

void helper_booke206_tlbilx1(CPUPPCState *env, target_ulong address)
{
    int i, j;
    int tid = (env->spr[SPR_BOOKE_MAS6] & MAS6_SPID);
    ppcmas_tlb_t *tlb = env->tlb.tlbm;
    int tlb_size;

    /* XXX missing LPID handling */
    for (i = 0; i < BOOKE206_MAX_TLBN; i++) {
        tlb_size = booke206_tlb_size(env, i);
        for (j = 0; j < tlb_size; j++) {
            if (!(tlb[j].mas1 & MAS1_IPROT) &&
                ((tlb[j].mas1 & MAS1_TID_MASK) == tid)) {
                tlb[j].mas1 &= ~MAS1_VALID;
            }
        }
        tlb += booke206_tlb_size(env, i);
    }
    tlb_flush(env, 1);
}

void helper_booke206_tlbilx3(CPUPPCState *env, target_ulong address)
{
    int i, j;
    ppcmas_tlb_t *tlb;
    int tid = (env->spr[SPR_BOOKE_MAS6] & MAS6_SPID);
    int pid = tid >> MAS6_SPID_SHIFT;
    int sgs = env->spr[SPR_BOOKE_MAS5] & MAS5_SGS;
    int ind = (env->spr[SPR_BOOKE_MAS6] & MAS6_SIND) ? MAS1_IND : 0;
    /* XXX check for unsupported isize and raise an invalid opcode then */
    int size = env->spr[SPR_BOOKE_MAS6] & MAS6_ISIZE_MASK;
    /* XXX implement MAV2 handling */
    bool mav2 = false;

    /* XXX missing LPID handling */
    /* flush by pid and ea */
    for (i = 0; i < BOOKE206_MAX_TLBN; i++) {
        int ways = booke206_tlb_ways(env, i);

        for (j = 0; j < ways; j++) {
            tlb = booke206_get_tlbm(env, i, address, j);
            if (!tlb) {
                continue;
            }
            if ((ppcmas_tlb_check(env, tlb, NULL, address, pid) != 0) ||
                (tlb->mas1 & MAS1_IPROT) ||
                ((tlb->mas1 & MAS1_IND) != ind) ||
                ((tlb->mas8 & MAS8_TGS) != sgs)) {
                continue;
            }
            if (mav2 && ((tlb->mas1 & MAS1_TSIZE_MASK) != size)) {
                /* XXX only check when MMUCFG[TWC] || TLBnCFG[HES] */
                continue;
            }
            /* XXX e500mc doesn't match SAS, but other cores might */
            tlb->mas1 &= ~MAS1_VALID;
        }
    }
    tlb_flush(env, 1);
}

void helper_booke206_tlbflush(CPUPPCState *env, uint32_t type)
{
    int flags = 0;

    if (type & 2) {
        flags |= BOOKE206_FLUSH_TLB1;
    }

    if (type & 4) {
        flags |= BOOKE206_FLUSH_TLB0;
    }

    booke206_flush_tlb(env, flags, 1);
}
#endif
