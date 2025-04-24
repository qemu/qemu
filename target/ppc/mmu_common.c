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
#include "system/kvm.h"
#include "kvm_ppc.h"
#include "mmu-hash64.h"
#include "mmu-hash32.h"
#include "exec/exec-all.h"
#include "exec/page-protection.h"
#include "exec/target_page.h"
#include "exec/log.h"
#include "helper_regs.h"
#include "qemu/error-report.h"
#include "qemu/qemu-print.h"
#include "internal.h"
#include "mmu-book3s-v3.h"
#include "mmu-radix64.h"
#include "mmu-booke.h"

/* #define DUMP_PAGE_TABLES */

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

/* Software driven TLB helpers */

static int ppc6xx_tlb_check(CPUPPCState *env, hwaddr *raddr, int *prot,
                            target_ulong eaddr, MMUAccessType access_type,
                            target_ulong ptem, bool key, bool nx)
{
    ppc6xx_tlb_t *tlb;
    target_ulong *pte1p;
    int nr, best, way, ret;
    bool is_code = (access_type == MMU_INST_FETCH);

    /* Initialize real address with an invalid value */
    *raddr = (hwaddr)-1ULL;
    best = -1;
    ret = -1; /* No TLB found */
    for (way = 0; way < env->nb_ways; way++) {
        nr = ppc6xx_tlb_getnum(env, eaddr, way, is_code);
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
        /* Check validity and table match */
        if (!pte_is_valid(tlb->pte0) || ((tlb->pte0 >> 6) & 1) != 0 ||
            (tlb->pte0 & PTE_PTEM_MASK) != ptem) {
            continue;
        }
        /* all matches should have equal RPN, WIMG & PP */
        if (*raddr != (hwaddr)-1ULL &&
            (*raddr & PTE_CHECK_MASK) != (tlb->pte1 & PTE_CHECK_MASK)) {
            qemu_log_mask(CPU_LOG_MMU, "Bad RPN/WIMG/PP\n");
            /* TLB inconsistency */
            continue;
        }
        /* Keep the matching PTE information */
        best = nr;
        *raddr = tlb->pte1;
        *prot = ppc_hash32_prot(key, tlb->pte1 & HPTE32_R_PP, nx);
        if (check_prot_access_type(*prot, access_type)) {
            qemu_log_mask(CPU_LOG_MMU, "PTE access granted !\n");
            ret = 0;
            break;
        } else {
            qemu_log_mask(CPU_LOG_MMU, "PTE access rejected\n");
            ret = -2;
        }
    }
    if (best != -1) {
        qemu_log_mask(CPU_LOG_MMU, "found TLB at addr " HWADDR_FMT_plx
                      " prot=%01x ret=%d\n",
                      *raddr & TARGET_PAGE_MASK, *prot, ret);
        /* Update page flags */
        pte1p = &env->tlb.tlb6[best].pte1;
        *pte1p |= 0x00000100; /* Update accessed flag */
        if (!(*pte1p & 0x00000080)) {
            if (access_type == MMU_DATA_STORE && ret == 0) {
                /* Update changed flag */
                *pte1p |= 0x00000080;
            } else {
                /* Force page fault for first write access */
                *prot &= ~PAGE_WRITE;
            }
        }
    }
    if (ret == -1) {
        int r = is_code ? SPR_ICMP : SPR_DCMP;
        env->spr[r] = ptem;
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

static int get_bat_6xx_tlb(CPUPPCState *env, hwaddr *raddr, int *prot,
                           target_ulong eaddr, MMUAccessType access_type,
                           bool pr)
{
    target_ulong *BATlt, *BATut, *BATu, *BATl;
    target_ulong BEPIl, BEPIu, bl;
    int i, ret = -1;
    bool ifetch = access_type == MMU_INST_FETCH;

    qemu_log_mask(CPU_LOG_MMU, "%s: %cBAT v " TARGET_FMT_lx "\n", __func__,
                  ifetch ? 'I' : 'D', eaddr);
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
        BEPIu = *BATu & BATU32_BEPIU;
        BEPIl = *BATu & BATU32_BEPIL;
        qemu_log_mask(CPU_LOG_MMU, "%s: %cBAT%d v " TARGET_FMT_lx " BATu "
                      TARGET_FMT_lx " BATl " TARGET_FMT_lx "\n", __func__,
                      ifetch ? 'I' : 'D', i, eaddr, *BATu, *BATl);
        bl = (*BATu & BATU32_BL) << 15;
        if ((!pr && (*BATu & BATU32_VS)) || (pr && (*BATu & BATU32_VP))) {
            if ((eaddr & BATU32_BEPIU) == BEPIu &&
                ((eaddr & BATU32_BEPIL) & ~bl) == BEPIl) {
                /* Get physical address */
                *raddr = (*BATl & BATU32_BEPIU) |
                    ((eaddr & BATU32_BEPIL & bl) | (*BATl & BATU32_BEPIL)) |
                    (eaddr & 0x0001F000);
                /* Compute access rights */
                *prot = ppc_hash32_bat_prot(*BATu, *BATl);
                if (check_prot_access_type(*prot, access_type)) {
                    qemu_log_mask(CPU_LOG_MMU, "BAT %d match: r " HWADDR_FMT_plx
                                  " prot=%c%c\n", i, *raddr,
                                  *prot & PAGE_READ ? 'R' : '-',
                                  *prot & PAGE_WRITE ? 'W' : '-');
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
                          TARGET_FMT_lx ":\n", eaddr);
            for (i = 0; i < 4; i++) {
                BATu = &BATut[i];
                BATl = &BATlt[i];
                BEPIu = *BATu & BATU32_BEPIU;
                BEPIl = *BATu & BATU32_BEPIL;
                bl = (*BATu & BATU32_BL) << 15;
                qemu_log_mask(CPU_LOG_MMU, "%s: %cBAT%d v " TARGET_FMT_lx
                              " BATu " TARGET_FMT_lx " BATl " TARGET_FMT_lx
                              "\n\t" TARGET_FMT_lx " " TARGET_FMT_lx " "
                              TARGET_FMT_lx "\n", __func__, ifetch ? 'I' : 'D',
                              i, eaddr, *BATu, *BATl, BEPIu, BEPIl, bl);
            }
        }
    }
    /* No hit */
    return ret;
}

static int mmu6xx_get_physical_address(CPUPPCState *env, hwaddr *raddr,
                                       int *prot, target_ulong eaddr,
                                       hwaddr *hashp, bool *keyp,
                                       MMUAccessType access_type, int type)
{
    PowerPCCPU *cpu = env_archcpu(env);
    hwaddr hash;
    target_ulong vsid, sr, pgidx, ptem;
    bool key, ds, nx;
    bool pr = FIELD_EX64(env->msr, MSR, PR);

    /* First try to find a BAT entry if there are any */
    if (env->nb_BATs &&
        get_bat_6xx_tlb(env, raddr, prot, eaddr, access_type, pr) == 0) {
        return 0;
    }

    /* Perform segment based translation when no BATs matched */
    sr = env->sr[eaddr >> 28];
    key = ppc_hash32_key(pr, sr);
    *keyp = key;
    ds = sr & SR32_T;
    nx = sr & SR32_NX;
    vsid = sr & SR32_VSID;
    qemu_log_mask(CPU_LOG_MMU,
                  "Check segment v=" TARGET_FMT_lx " %d " TARGET_FMT_lx
                  " nip=" TARGET_FMT_lx " lr=" TARGET_FMT_lx
                  " ir=%d dr=%d pr=%d %d t=%d\n",
                  eaddr, (int)(eaddr >> 28), sr, env->nip, env->lr,
                  (int)FIELD_EX64(env->msr, MSR, IR),
                  (int)FIELD_EX64(env->msr, MSR, DR), pr ? 1 : 0,
                  access_type == MMU_DATA_STORE, type);
    pgidx = (eaddr & ~SEGMENT_MASK_256M) >> TARGET_PAGE_BITS;
    hash = vsid ^ pgidx;
    ptem = (vsid << 7) | (pgidx >> 10); /* Virtual segment ID | API */

    qemu_log_mask(CPU_LOG_MMU, "pte segment: key=%d ds %d nx %d vsid "
                  TARGET_FMT_lx "\n", key, ds, nx, vsid);
    if (!ds) {
        /* Check if instruction fetch is allowed, if needed */
        if (type == ACCESS_CODE && nx) {
            qemu_log_mask(CPU_LOG_MMU, "No access allowed\n");
            return -3;
        }
        /* Page address translation */
        qemu_log_mask(CPU_LOG_MMU, "htab_base " HWADDR_FMT_plx " htab_mask "
                      HWADDR_FMT_plx " hash " HWADDR_FMT_plx "\n",
                      ppc_hash32_hpt_base(cpu), ppc_hash32_hpt_mask(cpu), hash);
        *hashp = hash;

        /* Software TLB search */
        return ppc6xx_tlb_check(env, raddr, prot, eaddr,
                                access_type, ptem, key, nx);
    }

    /* Direct-store segment : absolutely *BUGGY* for now */
    qemu_log_mask(CPU_LOG_MMU, "direct store...\n");
    switch (type) {
    case ACCESS_INT:
        /* Integer load/store : only access allowed */
        break;
    case ACCESS_CACHE:
        /*
         * dcba, dcbt, dcbtst, dcbf, dcbi, dcbst, dcbz, or icbi
         *
         * Should make the instruction do no-op.  As it already do
         * no-op, it's quite easy :-)
         */
        *raddr = eaddr;
        return 0;
    case ACCESS_CODE: /* No code fetch is allowed in direct-store areas */
    case ACCESS_FLOAT: /* Floating point load/store */
    case ACCESS_RES: /* lwarx, ldarx or srwcx. */
    case ACCESS_EXT: /* eciwx or ecowx */
        return -4;
    }
    if ((access_type == MMU_DATA_STORE || !key) &&
        (access_type == MMU_DATA_LOAD || key)) {
        *raddr = eaddr;
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
        BEPIu = *BATu & BATU32_BEPIU;
        BEPIl = *BATu & BATU32_BEPIL;
        bl = (*BATu & BATU32_BL) << 15;
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
    hwaddr hash = 0; /* init to 0 to avoid used uninit warning */
    bool key;
    int type, ret;

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

    ret = mmu6xx_get_physical_address(env, raddrp, protp, eaddr, &hash, &key,
                                      access_type, type);
    if (ret == 0) {
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
            env->spr[SPR_ICMP] |= 0x80000000;
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
            env->spr[SPR_DCMP] |= 0x80000000;
tlb_miss:
            env->error_code |= key << 19;
            env->spr[SPR_HASH1] = ppc_hash32_hpt_base(cpu) +
                                  get_pteg_offset32(cpu, hash);
            env->spr[SPR_HASH2] = ppc_hash32_hpt_base(cpu) +
                                  get_pteg_offset32(cpu, ~hash);
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
