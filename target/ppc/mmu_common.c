/*
 *  PowerPC MMU, TLB, SLB and BAT emulation helpers for QEMU.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
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
#include "qemu/units.h"
#include "cpu.h"
#include "sysemu/kvm.h"
#include "kvm_ppc.h"
#include "mmu-hash64.h"
#include "mmu-hash32.h"
#include "exec/exec-all.h"
#include "exec/page-protection.h"
#include "exec/log.h"
#include "helper_regs.h"
#include "qemu/error-report.h"
#include "qemu/qemu-print.h"
#include "internal.h"
#include "mmu-book3s-v3.h"
#include "mmu-radix64.h"
#include "mmu-booke.h"

/* #define DUMP_PAGE_TABLES */

/* Context used internally during MMU translations */
typedef struct {
    hwaddr raddr;      /* Real address             */
    hwaddr eaddr;      /* Effective address        */
    int prot;          /* Protection bits          */
    hwaddr hash[2];    /* Pagetable hash values    */
    target_ulong ptem; /* Virtual segment ID | API */
    int key;           /* Access key               */
    int nx;            /* Non-execute area         */
} mmu_ctx_t;

void ppc_store_sdr1(CPUPPCState *env, target_ulong value)
{
    PowerPCCPU *cpu = env_archcpu(env);
    qemu_log_mask(CPU_LOG_MMU, "%s: " TARGET_FMT_lx "\n", __func__, value);
    assert(!cpu->env.has_hv_mode || !cpu->vhyp);
#if defined(TARGET_PPC64)
    if (mmu_is_64bit(env->mmu_model)) {
        target_ulong sdr_mask = SDR_64_HTABORG | SDR_64_HTABSIZE;
        target_ulong htabsize = value & SDR_64_HTABSIZE;

        if (value & ~sdr_mask) {
            qemu_log_mask(LOG_GUEST_ERROR, "Invalid bits 0x"TARGET_FMT_lx
                     " set in SDR1", value & ~sdr_mask);
            value &= sdr_mask;
        }
        if (htabsize > 28) {
            qemu_log_mask(LOG_GUEST_ERROR, "Invalid HTABSIZE 0x" TARGET_FMT_lx
                     " stored in SDR1", htabsize);
            return;
        }
    }
#endif /* defined(TARGET_PPC64) */
    /* FIXME: Should check for valid HTABMASK values in 32-bit case */
    env->spr[SPR_SDR1] = value;
}

/*****************************************************************************/
/* PowerPC MMU emulation */

int ppc6xx_tlb_getnum(CPUPPCState *env, target_ulong eaddr,
                                    int way, int is_code)
{
    int nr;

    /* Select TLB num in a way from address */
    nr = (eaddr >> TARGET_PAGE_BITS) & (env->tlb_per_way - 1);
    /* Select TLB way */
    nr += env->tlb_per_way * way;
    /* 6xx has separate TLBs for instructions and data */
    if (is_code) {
        nr += env->nb_tlb;
    }

    return nr;
}

static int ppc6xx_tlb_pte_check(mmu_ctx_t *ctx, target_ulong pte0,
                                target_ulong pte1, int h,
                                MMUAccessType access_type)
{
    target_ulong ptem, mmask;
    int ret, pteh, ptev, pp;

    ret = -1;
    /* Check validity and table match */
    ptev = pte_is_valid(pte0);
    pteh = (pte0 >> 6) & 1;
    if (ptev && h == pteh) {
        /* Check vsid & api */
        ptem = pte0 & PTE_PTEM_MASK;
        mmask = PTE_CHECK_MASK;
        pp = pte1 & 0x00000003;
        if (ptem == ctx->ptem) {
            if (ctx->raddr != (hwaddr)-1ULL) {
                /* all matches should have equal RPN, WIMG & PP */
                if ((ctx->raddr & mmask) != (pte1 & mmask)) {
                    qemu_log_mask(CPU_LOG_MMU, "Bad RPN/WIMG/PP\n");
                    return -3;
                }
            }
            /* Keep the matching PTE information */
            ctx->raddr = pte1;
            ctx->prot = ppc_hash32_pp_prot(ctx->key, pp, ctx->nx);
            if (check_prot_access_type(ctx->prot, access_type)) {
                /* Access granted */
                qemu_log_mask(CPU_LOG_MMU, "PTE access granted !\n");
                ret = 0;
            } else {
                /* Access right violation */
                qemu_log_mask(CPU_LOG_MMU, "PTE access rejected\n");
                ret = -2;
            }
        }
    }

    return ret;
}

static int pte_update_flags(mmu_ctx_t *ctx, target_ulong *pte1p,
                            int ret, MMUAccessType access_type)
{
    int store = 0;

    /* Update page flags */
    if (!(*pte1p & 0x00000100)) {
        /* Update accessed flag */
        *pte1p |= 0x00000100;
        store = 1;
    }
    if (!(*pte1p & 0x00000080)) {
        if (access_type == MMU_DATA_STORE && ret == 0) {
            /* Update changed flag */
            *pte1p |= 0x00000080;
            store = 1;
        } else {
            /* Force page fault for first write access */
            ctx->prot &= ~PAGE_WRITE;
        }
    }

    return store;
}

/* Software driven TLB helpers */

static int ppc6xx_tlb_check(CPUPPCState *env, mmu_ctx_t *ctx,
                            target_ulong eaddr, MMUAccessType access_type)
{
    ppc6xx_tlb_t *tlb;
    int nr, best, way;
    int ret;

    best = -1;
    ret = -1; /* No TLB found */
    for (way = 0; way < env->nb_ways; way++) {
        nr = ppc6xx_tlb_getnum(env, eaddr, way, access_type == MMU_INST_FETCH);
        tlb = &env->tlb.tlb6[nr];
        /* This test "emulates" the PTE index match for hardware TLBs */
        if ((eaddr & TARGET_PAGE_MASK) != tlb->EPN) {
            qemu_log_mask(CPU_LOG_MMU, "TLB %d/%d %s [" TARGET_FMT_lx
                          " " TARGET_FMT_lx "] <> " TARGET_FMT_lx "\n",
                          nr, env->nb_tlb,
                          pte_is_valid(tlb->pte0) ? "valid" : "inval",
                          tlb->EPN, tlb->EPN + TARGET_PAGE_SIZE, eaddr);
            continue;
        }
        qemu_log_mask(CPU_LOG_MMU, "TLB %d/%d %s " TARGET_FMT_lx " <> "
                      TARGET_FMT_lx " " TARGET_FMT_lx " %c %c\n",
                      nr, env->nb_tlb,
                      pte_is_valid(tlb->pte0) ? "valid" : "inval",
                      tlb->EPN, eaddr, tlb->pte1,
                      access_type == MMU_DATA_STORE ? 'S' : 'L',
                      access_type == MMU_INST_FETCH ? 'I' : 'D');
        switch (ppc6xx_tlb_pte_check(ctx, tlb->pte0, tlb->pte1,
                                     0, access_type)) {
        case -2:
            /* Access violation */
            ret = -2;
            best = nr;
            break;
        case -1: /* No match */
        case -3: /* TLB inconsistency */
        default:
            break;
        case 0:
            /* access granted */
            /*
             * XXX: we should go on looping to check all TLBs
             *      consistency but we can speed-up the whole thing as
             *      the result would be undefined if TLBs are not
             *      consistent.
             */
            ret = 0;
            best = nr;
            goto done;
        }
    }
    if (best != -1) {
done:
        qemu_log_mask(CPU_LOG_MMU, "found TLB at addr " HWADDR_FMT_plx
                      " prot=%01x ret=%d\n",
                      ctx->raddr & TARGET_PAGE_MASK, ctx->prot, ret);
        /* Update page flags */
        pte_update_flags(ctx, &env->tlb.tlb6[best].pte1, ret, access_type);
    }
#if defined(DUMP_PAGE_TABLES)
    if (qemu_loglevel_mask(CPU_LOG_MMU)) {
        CPUState *cs = env_cpu(env);
        hwaddr base = ppc_hash32_hpt_base(env_archcpu(env));
        hwaddr len = ppc_hash32_hpt_mask(env_archcpu(env)) + 0x80;
        uint32_t a0, a1, a2, a3;

        qemu_log("Page table: " HWADDR_FMT_plx " len " HWADDR_FMT_plx "\n",
                 base, len);
        for (hwaddr curaddr = base; curaddr < base + len; curaddr += 16) {
            a0 = ldl_phys(cs->as, curaddr);
            a1 = ldl_phys(cs->as, curaddr + 4);
            a2 = ldl_phys(cs->as, curaddr + 8);
            a3 = ldl_phys(cs->as, curaddr + 12);
            if (a0 != 0 || a1 != 0 || a2 != 0 || a3 != 0) {
                qemu_log(HWADDR_FMT_plx ": %08x %08x %08x %08x\n",
                         curaddr, a0, a1, a2, a3);
            }
        }
    }
#endif
    return ret;
}

/* Perform BAT hit & translation */
static inline void bat_size_prot(CPUPPCState *env, target_ulong *blp,
                                 int *validp, int *protp, target_ulong *BATu,
                                 target_ulong *BATl)
{
    target_ulong bl;
    int pp, valid, prot;

    bl = (*BATu & 0x00001FFC) << 15;
    valid = 0;
    prot = 0;
    if ((!FIELD_EX64(env->msr, MSR, PR) && (*BATu & 0x00000002)) ||
        (FIELD_EX64(env->msr, MSR, PR) && (*BATu & 0x00000001))) {
        valid = 1;
        pp = *BATl & 0x00000003;
        if (pp != 0) {
            prot = PAGE_READ | PAGE_EXEC;
            if (pp == 0x2) {
                prot |= PAGE_WRITE;
            }
        }
    }
    *blp = bl;
    *validp = valid;
    *protp = prot;
}

static int get_bat_6xx_tlb(CPUPPCState *env, mmu_ctx_t *ctx,
                           target_ulong virtual, MMUAccessType access_type)
{
    target_ulong *BATlt, *BATut, *BATu, *BATl;
    target_ulong BEPIl, BEPIu, bl;
    int i, valid, prot;
    int ret = -1;
    bool ifetch = access_type == MMU_INST_FETCH;

    qemu_log_mask(CPU_LOG_MMU, "%s: %cBAT v " TARGET_FMT_lx "\n", __func__,
                  ifetch ? 'I' : 'D', virtual);
    if (ifetch) {
        BATlt = env->IBAT[1];
        BATut = env->IBAT[0];
    } else {
        BATlt = env->DBAT[1];
        BATut = env->DBAT[0];
    }
    for (i = 0; i < env->nb_BATs; i++) {
        BATu = &BATut[i];
        BATl = &BATlt[i];
        BEPIu = *BATu & 0xF0000000;
        BEPIl = *BATu & 0x0FFE0000;
        bat_size_prot(env, &bl, &valid, &prot, BATu, BATl);
        qemu_log_mask(CPU_LOG_MMU, "%s: %cBAT%d v " TARGET_FMT_lx " BATu "
                      TARGET_FMT_lx " BATl " TARGET_FMT_lx "\n", __func__,
                      ifetch ? 'I' : 'D', i, virtual, *BATu, *BATl);
        if ((virtual & 0xF0000000) == BEPIu &&
            ((virtual & 0x0FFE0000) & ~bl) == BEPIl) {
            /* BAT matches */
            if (valid != 0) {
                /* Get physical address */
                ctx->raddr = (*BATl & 0xF0000000) |
                    ((virtual & 0x0FFE0000 & bl) | (*BATl & 0x0FFE0000)) |
                    (virtual & 0x0001F000);
                /* Compute access rights */
                ctx->prot = prot;
                if (check_prot_access_type(ctx->prot, access_type)) {
                    qemu_log_mask(CPU_LOG_MMU, "BAT %d match: r " HWADDR_FMT_plx
                                  " prot=%c%c\n", i, ctx->raddr,
                                  ctx->prot & PAGE_READ ? 'R' : '-',
                                  ctx->prot & PAGE_WRITE ? 'W' : '-');
                    ret = 0;
                } else {
                    ret = -2;
                }
                break;
            }
        }
    }
    if (ret < 0) {
        if (qemu_log_enabled()) {
            qemu_log_mask(CPU_LOG_MMU, "no BAT match for "
                          TARGET_FMT_lx ":\n", virtual);
            for (i = 0; i < 4; i++) {
                BATu = &BATut[i];
                BATl = &BATlt[i];
                BEPIu = *BATu & 0xF0000000;
                BEPIl = *BATu & 0x0FFE0000;
                bl = (*BATu & 0x00001FFC) << 15;
                qemu_log_mask(CPU_LOG_MMU, "%s: %cBAT%d v " TARGET_FMT_lx
                              " BATu " TARGET_FMT_lx " BATl " TARGET_FMT_lx
                              "\n\t" TARGET_FMT_lx " " TARGET_FMT_lx " "
                              TARGET_FMT_lx "\n", __func__, ifetch ? 'I' : 'D',
                              i, virtual, *BATu, *BATl, BEPIu, BEPIl, bl);
            }
        }
    }
    /* No hit */
    return ret;
}

static int mmu6xx_get_physical_address(CPUPPCState *env, mmu_ctx_t *ctx,
                                       target_ulong eaddr,
                                       MMUAccessType access_type, int type)
{
    PowerPCCPU *cpu = env_archcpu(env);
    hwaddr hash;
    target_ulong vsid, sr, pgidx;
    int ds, target_page_bits;
    bool pr;

    /* First try to find a BAT entry if there are any */
    if (env->nb_BATs && get_bat_6xx_tlb(env, ctx, eaddr, access_type) == 0) {
        return 0;
    }

    /* Perform segment based translation when no BATs matched */
    pr = FIELD_EX64(env->msr, MSR, PR);
    ctx->eaddr = eaddr;

    sr = env->sr[eaddr >> 28];
    ctx->key = (((sr & 0x20000000) && pr) ||
                ((sr & 0x40000000) && !pr)) ? 1 : 0;
    ds = sr & 0x80000000 ? 1 : 0;
    ctx->nx = sr & 0x10000000 ? 1 : 0;
    vsid = sr & 0x00FFFFFF;
    target_page_bits = TARGET_PAGE_BITS;
    qemu_log_mask(CPU_LOG_MMU,
                  "Check segment v=" TARGET_FMT_lx " %d " TARGET_FMT_lx
                  " nip=" TARGET_FMT_lx " lr=" TARGET_FMT_lx
                  " ir=%d dr=%d pr=%d %d t=%d\n",
                  eaddr, (int)(eaddr >> 28), sr, env->nip, env->lr,
                  (int)FIELD_EX64(env->msr, MSR, IR),
                  (int)FIELD_EX64(env->msr, MSR, DR), pr ? 1 : 0,
                  access_type == MMU_DATA_STORE, type);
    pgidx = (eaddr & ~SEGMENT_MASK_256M) >> target_page_bits;
    hash = vsid ^ pgidx;
    ctx->ptem = (vsid << 7) | (pgidx >> 10);

    qemu_log_mask(CPU_LOG_MMU, "pte segment: key=%d ds %d nx %d vsid "
                  TARGET_FMT_lx "\n", ctx->key, ds, ctx->nx, vsid);
    if (!ds) {
        /* Check if instruction fetch is allowed, if needed */
        if (type == ACCESS_CODE && ctx->nx) {
            qemu_log_mask(CPU_LOG_MMU, "No access allowed\n");
            return -3;
        }
        /* Page address translation */
        qemu_log_mask(CPU_LOG_MMU, "htab_base " HWADDR_FMT_plx " htab_mask "
                      HWADDR_FMT_plx " hash " HWADDR_FMT_plx "\n",
                      ppc_hash32_hpt_base(cpu), ppc_hash32_hpt_mask(cpu), hash);
        ctx->hash[0] = hash;
        ctx->hash[1] = ~hash;

        /* Initialize real address with an invalid value */
        ctx->raddr = (hwaddr)-1ULL;
        /* Software TLB search */
        return ppc6xx_tlb_check(env, ctx, eaddr, access_type);
    }

    /* Direct-store segment : absolutely *BUGGY* for now */
    qemu_log_mask(CPU_LOG_MMU, "direct store...\n");
    switch (type) {
    case ACCESS_INT:
        /* Integer load/store : only access allowed */
        break;
    case ACCESS_CODE:
        /* No code fetch is allowed in direct-store areas */
        return -4;
    case ACCESS_FLOAT:
        /* Floating point load/store */
        return -4;
    case ACCESS_RES:
        /* lwarx, ldarx or srwcx. */
        return -4;
    case ACCESS_CACHE:
        /*
         * dcba, dcbt, dcbtst, dcbf, dcbi, dcbst, dcbz, or icbi
         *
         * Should make the instruction do no-op.  As it already do
         * no-op, it's quite easy :-)
         */
        ctx->raddr = eaddr;
        return 0;
    case ACCESS_EXT:
        /* eciwx or ecowx */
        return -4;
    default:
        qemu_log_mask(CPU_LOG_MMU, "ERROR: instruction should not need address"
                                   " translation\n");
        return -4;
    }
    if ((access_type == MMU_DATA_STORE || ctx->key != 1) &&
        (access_type == MMU_DATA_LOAD || ctx->key != 0)) {
        ctx->raddr = eaddr;
        return 2;
    }
    return -2;
}

static const char *book3e_tsize_to_str[32] = {
    "1K", "2K", "4K", "8K", "16K", "32K", "64K", "128K", "256K", "512K",
    "1M", "2M", "4M", "8M", "16M", "32M", "64M", "128M", "256M", "512M",
    "1G", "2G", "4G", "8G", "16G", "32G", "64G", "128G", "256G", "512G",
    "1T", "2T"
};

static void mmubooke_dump_mmu(CPUPPCState *env)
{
    ppcemb_tlb_t *entry;
    int i;

#ifdef CONFIG_KVM
    if (kvm_enabled() && !env->kvm_sw_tlb) {
        qemu_printf("Cannot access KVM TLB\n");
        return;
    }
#endif

    qemu_printf("\nTLB:\n");
    qemu_printf("Effective          Physical           Size PID   Prot     "
                "Attr\n");

    entry = &env->tlb.tlbe[0];
    for (i = 0; i < env->nb_tlb; i++, entry++) {
        hwaddr ea, pa;
        target_ulong mask;
        uint64_t size = (uint64_t)entry->size;
        char size_buf[20];

        /* Check valid flag */
        if (!(entry->prot & PAGE_VALID)) {
            continue;
        }

        mask = ~(entry->size - 1);
        ea = entry->EPN & mask;
        pa = entry->RPN & mask;
        /* Extend the physical address to 36 bits */
        pa |= (hwaddr)(entry->RPN & 0xF) << 32;
        if (size >= 1 * MiB) {
            snprintf(size_buf, sizeof(size_buf), "%3" PRId64 "M", size / MiB);
        } else {
            snprintf(size_buf, sizeof(size_buf), "%3" PRId64 "k", size / KiB);
        }
        qemu_printf("0x%016" PRIx64 " 0x%016" PRIx64 " %s %-5u %08x %08x\n",
                    (uint64_t)ea, (uint64_t)pa, size_buf, (uint32_t)entry->PID,
                    entry->prot, entry->attr);
    }

}

static void mmubooke206_dump_one_tlb(CPUPPCState *env, int tlbn, int offset,
                                     int tlbsize)
{
    ppcmas_tlb_t *entry;
    int i;

    qemu_printf("\nTLB%d:\n", tlbn);
    qemu_printf("Effective          Physical           Size TID   TS SRWX"
                " URWX WIMGE U0123\n");

    entry = &env->tlb.tlbm[offset];
    for (i = 0; i < tlbsize; i++, entry++) {
        hwaddr ea, pa, size;
        int tsize;

        if (!(entry->mas1 & MAS1_VALID)) {
            continue;
        }

        tsize = (entry->mas1 & MAS1_TSIZE_MASK) >> MAS1_TSIZE_SHIFT;
        size = 1024ULL << tsize;
        ea = entry->mas2 & ~(size - 1);
        pa = entry->mas7_3 & ~(size - 1);

        qemu_printf("0x%016" PRIx64 " 0x%016" PRIx64 " %4s %-5u %1u  S%c%c%c"
                    " U%c%c%c %c%c%c%c%c U%c%c%c%c\n",
                    (uint64_t)ea, (uint64_t)pa,
                    book3e_tsize_to_str[tsize],
                    (entry->mas1 & MAS1_TID_MASK) >> MAS1_TID_SHIFT,
                    (entry->mas1 & MAS1_TS) >> MAS1_TS_SHIFT,
                    entry->mas7_3 & MAS3_SR ? 'R' : '-',
                    entry->mas7_3 & MAS3_SW ? 'W' : '-',
                    entry->mas7_3 & MAS3_SX ? 'X' : '-',
                    entry->mas7_3 & MAS3_UR ? 'R' : '-',
                    entry->mas7_3 & MAS3_UW ? 'W' : '-',
                    entry->mas7_3 & MAS3_UX ? 'X' : '-',
                    entry->mas2 & MAS2_W ? 'W' : '-',
                    entry->mas2 & MAS2_I ? 'I' : '-',
                    entry->mas2 & MAS2_M ? 'M' : '-',
                    entry->mas2 & MAS2_G ? 'G' : '-',
                    entry->mas2 & MAS2_E ? 'E' : '-',
                    entry->mas7_3 & MAS3_U0 ? '0' : '-',
                    entry->mas7_3 & MAS3_U1 ? '1' : '-',
                    entry->mas7_3 & MAS3_U2 ? '2' : '-',
                    entry->mas7_3 & MAS3_U3 ? '3' : '-');
    }
}

static void mmubooke206_dump_mmu(CPUPPCState *env)
{
    int offset = 0;
    int i;

#ifdef CONFIG_KVM
    if (kvm_enabled() && !env->kvm_sw_tlb) {
        qemu_printf("Cannot access KVM TLB\n");
        return;
    }
#endif

    for (i = 0; i < BOOKE206_MAX_TLBN; i++) {
        int size = booke206_tlb_size(env, i);

        if (size == 0) {
            continue;
        }

        mmubooke206_dump_one_tlb(env, i, offset, size);
        offset += size;
    }
}

static void mmu6xx_dump_BATs(CPUPPCState *env, int type)
{
    target_ulong *BATlt, *BATut, *BATu, *BATl;
    target_ulong BEPIl, BEPIu, bl;
    int i;

    switch (type) {
    case ACCESS_CODE:
        BATlt = env->IBAT[1];
        BATut = env->IBAT[0];
        break;
    default:
        BATlt = env->DBAT[1];
        BATut = env->DBAT[0];
        break;
    }

    for (i = 0; i < env->nb_BATs; i++) {
        BATu = &BATut[i];
        BATl = &BATlt[i];
        BEPIu = *BATu & 0xF0000000;
        BEPIl = *BATu & 0x0FFE0000;
        bl = (*BATu & 0x00001FFC) << 15;
        qemu_printf("%s BAT%d BATu " TARGET_FMT_lx
                    " BATl " TARGET_FMT_lx "\n\t" TARGET_FMT_lx " "
                    TARGET_FMT_lx " " TARGET_FMT_lx "\n",
                    type == ACCESS_CODE ? "code" : "data", i,
                    *BATu, *BATl, BEPIu, BEPIl, bl);
    }
}

static void mmu6xx_dump_mmu(CPUPPCState *env)
{
    PowerPCCPU *cpu = env_archcpu(env);
    ppc6xx_tlb_t *tlb;
    target_ulong sr;
    int type, way, entry, i;

    qemu_printf("HTAB base = 0x%"HWADDR_PRIx"\n", ppc_hash32_hpt_base(cpu));
    qemu_printf("HTAB mask = 0x%"HWADDR_PRIx"\n", ppc_hash32_hpt_mask(cpu));

    qemu_printf("\nSegment registers:\n");
    for (i = 0; i < 32; i++) {
        sr = env->sr[i];
        if (sr & 0x80000000) {
            qemu_printf("%02d T=%d Ks=%d Kp=%d BUID=0x%03x "
                        "CNTLR_SPEC=0x%05x\n", i,
                        sr & 0x80000000 ? 1 : 0, sr & 0x40000000 ? 1 : 0,
                        sr & 0x20000000 ? 1 : 0, (uint32_t)((sr >> 20) & 0x1FF),
                        (uint32_t)(sr & 0xFFFFF));
        } else {
            qemu_printf("%02d T=%d Ks=%d Kp=%d N=%d VSID=0x%06x\n", i,
                        sr & 0x80000000 ? 1 : 0, sr & 0x40000000 ? 1 : 0,
                        sr & 0x20000000 ? 1 : 0, sr & 0x10000000 ? 1 : 0,
                        (uint32_t)(sr & 0x00FFFFFF));
        }
    }

    qemu_printf("\nBATs:\n");
    mmu6xx_dump_BATs(env, ACCESS_INT);
    mmu6xx_dump_BATs(env, ACCESS_CODE);

    qemu_printf("\nTLBs                       [EPN    EPN + SIZE]\n");
    for (type = 0; type < 2; type++) {
        for (way = 0; way < env->nb_ways; way++) {
            for (entry = env->nb_tlb * type + env->tlb_per_way * way;
                 entry < (env->nb_tlb * type + env->tlb_per_way * (way + 1));
                 entry++) {

                tlb = &env->tlb.tlb6[entry];
                qemu_printf("%s TLB %02d/%02d way:%d %s ["
                            TARGET_FMT_lx " " TARGET_FMT_lx "]\n",
                            type ? "code" : "data", entry % env->nb_tlb,
                            env->nb_tlb, way,
                            pte_is_valid(tlb->pte0) ? "valid" : "inval",
                            tlb->EPN, tlb->EPN + TARGET_PAGE_SIZE);
            }
        }
    }
}

void dump_mmu(CPUPPCState *env)
{
    switch (env->mmu_model) {
    case POWERPC_MMU_BOOKE:
        mmubooke_dump_mmu(env);
        break;
    case POWERPC_MMU_BOOKE206:
        mmubooke206_dump_mmu(env);
        break;
    case POWERPC_MMU_SOFT_6xx:
        mmu6xx_dump_mmu(env);
        break;
#if defined(TARGET_PPC64)
    case POWERPC_MMU_64B:
    case POWERPC_MMU_2_03:
    case POWERPC_MMU_2_06:
    case POWERPC_MMU_2_07:
        dump_slb(env_archcpu(env));
        break;
    case POWERPC_MMU_3_00:
        if (ppc64_v3_radix(env_archcpu(env))) {
            qemu_log_mask(LOG_UNIMP, "%s: the PPC64 MMU is unsupported\n",
                          __func__);
        } else {
            dump_slb(env_archcpu(env));
        }
        break;
#endif
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented\n", __func__);
    }
}


static bool ppc_real_mode_xlate(PowerPCCPU *cpu, vaddr eaddr,
                                MMUAccessType access_type,
                                hwaddr *raddrp, int *psizep, int *protp)
{
    CPUPPCState *env = &cpu->env;

    if (access_type == MMU_INST_FETCH ? !FIELD_EX64(env->msr, MSR, IR)
                                      : !FIELD_EX64(env->msr, MSR, DR)) {
        *raddrp = eaddr;
        *protp = PAGE_RWX;
        *psizep = TARGET_PAGE_BITS;
        return true;
    } else if (env->mmu_model == POWERPC_MMU_REAL) {
        cpu_abort(CPU(cpu), "PowerPC in real mode shold not do translation\n");
    }
    return false;
}

static bool ppc_40x_xlate(PowerPCCPU *cpu, vaddr eaddr,
                          MMUAccessType access_type,
                          hwaddr *raddrp, int *psizep, int *protp,
                          int mmu_idx, bool guest_visible)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    int ret;

    if (ppc_real_mode_xlate(cpu, eaddr, access_type, raddrp, psizep, protp)) {
        return true;
    }

    ret = mmu40x_get_physical_address(env, raddrp, protp, eaddr, access_type);
    if (ret == 0) {
        *psizep = TARGET_PAGE_BITS;
        return true;
    } else if (!guest_visible) {
        return false;
    }

    log_cpu_state_mask(CPU_LOG_MMU, cs, 0);
    if (access_type == MMU_INST_FETCH) {
        switch (ret) {
        case -1:
            /* No matches in page tables or TLB */
            cs->exception_index = POWERPC_EXCP_ITLB;
            env->error_code = 0;
            env->spr[SPR_40x_DEAR] = eaddr;
            env->spr[SPR_40x_ESR] = 0x00000000;
            break;
        case -2:
            /* Access rights violation */
            cs->exception_index = POWERPC_EXCP_ISI;
            env->error_code = 0x08000000;
            break;
        default:
            g_assert_not_reached();
        }
    } else {
        switch (ret) {
        case -1:
            /* No matches in page tables or TLB */
            cs->exception_index = POWERPC_EXCP_DTLB;
            env->error_code = 0;
            env->spr[SPR_40x_DEAR] = eaddr;
            if (access_type == MMU_DATA_STORE) {
                env->spr[SPR_40x_ESR] = 0x00800000;
            } else {
                env->spr[SPR_40x_ESR] = 0x00000000;
            }
            break;
        case -2:
            /* Access rights violation */
            cs->exception_index = POWERPC_EXCP_DSI;
            env->error_code = 0;
            env->spr[SPR_40x_DEAR] = eaddr;
            if (access_type == MMU_DATA_STORE) {
                env->spr[SPR_40x_ESR] |= 0x00800000;
            }
            break;
        default:
            g_assert_not_reached();
        }
    }
    return false;
}

static bool ppc_6xx_xlate(PowerPCCPU *cpu, vaddr eaddr,
                          MMUAccessType access_type,
                          hwaddr *raddrp, int *psizep, int *protp,
                          int mmu_idx, bool guest_visible)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    mmu_ctx_t ctx;
    int type;
    int ret;

    if (ppc_real_mode_xlate(cpu, eaddr, access_type, raddrp, psizep, protp)) {
        return true;
    }

    if (access_type == MMU_INST_FETCH) {
        /* code access */
        type = ACCESS_CODE;
    } else if (guest_visible) {
        /* data access */
        type = env->access_type;
    } else {
        type = ACCESS_INT;
    }

    ctx.prot = 0;
    ctx.hash[0] = 0;
    ctx.hash[1] = 0;
    ret = mmu6xx_get_physical_address(env, &ctx, eaddr, access_type, type);
    if (ret == 0) {
        *raddrp = ctx.raddr;
        *protp = ctx.prot;
        *psizep = TARGET_PAGE_BITS;
        return true;
    } else if (!guest_visible) {
        return false;
    }

    log_cpu_state_mask(CPU_LOG_MMU, cs, 0);
    if (type == ACCESS_CODE) {
        switch (ret) {
        case -1:
            /* No matches in page tables or TLB */
            cs->exception_index = POWERPC_EXCP_IFTLB;
            env->error_code = 1 << 18;
            env->spr[SPR_IMISS] = eaddr;
            env->spr[SPR_ICMP] = 0x80000000 | ctx.ptem;
            goto tlb_miss;
        case -2:
            /* Access rights violation */
            cs->exception_index = POWERPC_EXCP_ISI;
            env->error_code = 0x08000000;
            break;
        case -3:
            /* No execute protection violation */
            cs->exception_index = POWERPC_EXCP_ISI;
            env->error_code = 0x10000000;
            break;
        case -4:
            /* Direct store exception */
            /* No code fetch is allowed in direct-store areas */
            cs->exception_index = POWERPC_EXCP_ISI;
            env->error_code = 0x10000000;
            break;
        }
    } else {
        switch (ret) {
        case -1:
            /* No matches in page tables or TLB */
            if (access_type == MMU_DATA_STORE) {
                cs->exception_index = POWERPC_EXCP_DSTLB;
                env->error_code = 1 << 16;
            } else {
                cs->exception_index = POWERPC_EXCP_DLTLB;
                env->error_code = 0;
            }
            env->spr[SPR_DMISS] = eaddr;
            env->spr[SPR_DCMP] = 0x80000000 | ctx.ptem;
tlb_miss:
            env->error_code |= ctx.key << 19;
            env->spr[SPR_HASH1] = ppc_hash32_hpt_base(cpu) +
                                  get_pteg_offset32(cpu, ctx.hash[0]);
            env->spr[SPR_HASH2] = ppc_hash32_hpt_base(cpu) +
                                  get_pteg_offset32(cpu, ctx.hash[1]);
            break;
        case -2:
            /* Access rights violation */
            cs->exception_index = POWERPC_EXCP_DSI;
            env->error_code = 0;
            env->spr[SPR_DAR] = eaddr;
            if (access_type == MMU_DATA_STORE) {
                env->spr[SPR_DSISR] = 0x0A000000;
            } else {
                env->spr[SPR_DSISR] = 0x08000000;
            }
            break;
        case -4:
            /* Direct store exception */
            switch (type) {
            case ACCESS_FLOAT:
                /* Floating point load/store */
                cs->exception_index = POWERPC_EXCP_ALIGN;
                env->error_code = POWERPC_EXCP_ALIGN_FP;
                env->spr[SPR_DAR] = eaddr;
                break;
            case ACCESS_RES:
                /* lwarx, ldarx or stwcx. */
                cs->exception_index = POWERPC_EXCP_DSI;
                env->error_code = 0;
                env->spr[SPR_DAR] = eaddr;
                if (access_type == MMU_DATA_STORE) {
                    env->spr[SPR_DSISR] = 0x06000000;
                } else {
                    env->spr[SPR_DSISR] = 0x04000000;
                }
                break;
            case ACCESS_EXT:
                /* eciwx or ecowx */
                cs->exception_index = POWERPC_EXCP_DSI;
                env->error_code = 0;
                env->spr[SPR_DAR] = eaddr;
                if (access_type == MMU_DATA_STORE) {
                    env->spr[SPR_DSISR] = 0x06100000;
                } else {
                    env->spr[SPR_DSISR] = 0x04100000;
                }
                break;
            default:
                printf("DSI: invalid exception (%d)\n", ret);
                cs->exception_index = POWERPC_EXCP_PROGRAM;
                env->error_code = POWERPC_EXCP_INVAL | POWERPC_EXCP_INVAL_INVAL;
                env->spr[SPR_DAR] = eaddr;
                break;
            }
            break;
        }
    }
    return false;
}

/*****************************************************************************/

bool ppc_xlate(PowerPCCPU *cpu, vaddr eaddr, MMUAccessType access_type,
                      hwaddr *raddrp, int *psizep, int *protp,
                      int mmu_idx, bool guest_visible)
{
    switch (cpu->env.mmu_model) {
#if defined(TARGET_PPC64)
    case POWERPC_MMU_3_00:
        if (ppc64_v3_radix(cpu)) {
            return ppc_radix64_xlate(cpu, eaddr, access_type, raddrp,
                                     psizep, protp, mmu_idx, guest_visible);
        }
        /* fall through */
    case POWERPC_MMU_64B:
    case POWERPC_MMU_2_03:
    case POWERPC_MMU_2_06:
    case POWERPC_MMU_2_07:
        return ppc_hash64_xlate(cpu, eaddr, access_type,
                                raddrp, psizep, protp, mmu_idx, guest_visible);
#endif

    case POWERPC_MMU_32B:
        return ppc_hash32_xlate(cpu, eaddr, access_type, raddrp,
                               psizep, protp, mmu_idx, guest_visible);
    case POWERPC_MMU_BOOKE:
    case POWERPC_MMU_BOOKE206:
        return ppc_booke_xlate(cpu, eaddr, access_type, raddrp,
                               psizep, protp, mmu_idx, guest_visible);
    case POWERPC_MMU_SOFT_4xx:
        return ppc_40x_xlate(cpu, eaddr, access_type, raddrp,
                             psizep, protp, mmu_idx, guest_visible);
    case POWERPC_MMU_SOFT_6xx:
        return ppc_6xx_xlate(cpu, eaddr, access_type, raddrp,
                             psizep, protp, mmu_idx, guest_visible);
    case POWERPC_MMU_REAL:
        return ppc_real_mode_xlate(cpu, eaddr, access_type, raddrp, psizep,
                                   protp);
    case POWERPC_MMU_MPC8xx:
        cpu_abort(env_cpu(&cpu->env), "MPC8xx MMU model is not implemented\n");
    default:
        cpu_abort(CPU(cpu), "Unknown or invalid MMU model\n");
    }
}

hwaddr ppc_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    hwaddr raddr;
    int s, p;

    /*
     * Some MMUs have separate TLBs for code and data. If we only
     * try an MMU_DATA_LOAD, we may not be able to read instructions
     * mapped by code TLBs, so we also try a MMU_INST_FETCH.
     */
    if (ppc_xlate(cpu, addr, MMU_DATA_LOAD, &raddr, &s, &p,
                  ppc_env_mmu_index(&cpu->env, false), false) ||
        ppc_xlate(cpu, addr, MMU_INST_FETCH, &raddr, &s, &p,
                  ppc_env_mmu_index(&cpu->env, true), false)) {
        return raddr & TARGET_PAGE_MASK;
    }
    return -1;
}
