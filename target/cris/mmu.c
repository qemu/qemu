/*
 *  CRIS mmu emulation.
 *
 *  Copyright (c) 2007 AXIS Communications AB
 *  Written by Edgar E. Iglesias.
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
#include "cpu.h"
#include "exec/exec-all.h"
#include "mmu.h"

#ifdef DEBUG
#define D(x) x
#define D_LOG(...) qemu_log(__VA_ARGS__)
#else
#define D(x) do { } while (0)
#define D_LOG(...) do { } while (0)
#endif

void cris_mmu_init(CPUCRISState *env)
{
    env->mmu_rand_lfsr = 0xcccc;
}

#define SR_POLYNOM 0x8805
static inline unsigned int compute_polynom(unsigned int sr)
{
    unsigned int i;
    unsigned int f;

    f = 0;
    for (i = 0; i < 16; i++) {
        f += ((SR_POLYNOM >> i) & 1) & ((sr >> i) & 1);
    }

    return f;
}

static void cris_mmu_update_rand_lfsr(CPUCRISState *env)
{
    unsigned int f;

    /* Update lfsr at every fault.  */
    f = compute_polynom(env->mmu_rand_lfsr);
    env->mmu_rand_lfsr >>= 1;
    env->mmu_rand_lfsr |= (f << 15);
    env->mmu_rand_lfsr &= 0xffff;
}

static inline int cris_mmu_enabled(uint32_t rw_gc_cfg)
{
    return (rw_gc_cfg & 12) != 0;
}

static inline int cris_mmu_segmented_addr(int seg, uint32_t rw_mm_cfg)
{
    return (1 << seg) & rw_mm_cfg;
}

static uint32_t cris_mmu_translate_seg(CPUCRISState *env, int seg)
{
    uint32_t base;
    int i;

    if (seg < 8) {
        base = env->sregs[SFR_RW_MM_KBASE_LO];
    } else {
        base = env->sregs[SFR_RW_MM_KBASE_HI];
    }

    i = seg & 7;
    base >>= i * 4;
    base &= 15;

    base <<= 28;
    return base;
}

/* Used by the tlb decoder.  */
#define EXTRACT_FIELD(src, start, end)                  \
    (((src) >> start) & ((1 << (end - start + 1)) - 1))

static inline void set_field(uint32_t *dst, unsigned int val,
			     unsigned int offset, unsigned int width)
{
    uint32_t mask;

    mask = (1 << width) - 1;
    mask <<= offset;
    val <<= offset;

    val &= mask;
    *dst &= ~(mask);
    *dst |= val;
}

#ifdef DEBUG
static void dump_tlb(CPUCRISState *env, int mmu)
{
    int set;
    int idx;
    uint32_t hi, lo, tlb_vpn, tlb_pfn;

    for (set = 0; set < 4; set++) {
        for (idx = 0; idx < 16; idx++) {
            lo = env->tlbsets[mmu][set][idx].lo;
            hi = env->tlbsets[mmu][set][idx].hi;
            tlb_vpn = EXTRACT_FIELD(hi, 13, 31);
            tlb_pfn = EXTRACT_FIELD(lo, 13, 31);

            printf("TLB: [%d][%d] hi=%x lo=%x v=%x p=%x\n",
                   set, idx, hi, lo, tlb_vpn, tlb_pfn);
        }
    }
}
#endif

static int cris_mmu_translate_page(struct cris_mmu_result *res,
                                   CPUCRISState *env, uint32_t vaddr,
                                   MMUAccessType access_type,
                                   int usermode, int debug)
{
    unsigned int vpage;
    unsigned int idx;
    uint32_t pid, lo, hi;
    uint32_t tlb_vpn, tlb_pfn = 0;
    int tlb_pid, tlb_g, tlb_v, tlb_k, tlb_w, tlb_x;
    int cfg_v, cfg_k, cfg_w, cfg_x;
    int set, match = 0;
    uint32_t r_cause;
    uint32_t r_cfg;
    int rwcause;
    int mmu = 1; /* Data mmu is default.  */
    int vect_base;

    r_cause = env->sregs[SFR_R_MM_CAUSE];
    r_cfg = env->sregs[SFR_RW_MM_CFG];
    pid = env->pregs[PR_PID] & 0xff;

    switch (access_type) {
    case MMU_INST_FETCH:
        rwcause = CRIS_MMU_ERR_EXEC;
        mmu = 0;
        break;
    case MMU_DATA_STORE:
        rwcause = CRIS_MMU_ERR_WRITE;
        break;
    default:
    case MMU_DATA_LOAD:
        rwcause = CRIS_MMU_ERR_READ;
        break;
    }

    /* I exception vectors 4 - 7, D 8 - 11.  */
    vect_base = (mmu + 1) * 4;

    vpage = vaddr >> 13;

    /*
     * We know the index which to check on each set.
     * Scan both I and D.
     */
    idx = vpage & 15;
    for (set = 0; set < 4; set++) {
        lo = env->tlbsets[mmu][set][idx].lo;
        hi = env->tlbsets[mmu][set][idx].hi;

        tlb_vpn = hi >> 13;
        tlb_pid = EXTRACT_FIELD(hi, 0, 7);
        tlb_g  = EXTRACT_FIELD(lo, 4, 4);

        D_LOG("TLB[%d][%d][%d] v=%x vpage=%x lo=%x hi=%x\n",
              mmu, set, idx, tlb_vpn, vpage, lo, hi);
        if ((tlb_g || (tlb_pid == pid)) && tlb_vpn == vpage) {
            match = 1;
            break;
        }
    }

    res->bf_vec = vect_base;
    if (match) {
        cfg_w  = EXTRACT_FIELD(r_cfg, 19, 19);
        cfg_k  = EXTRACT_FIELD(r_cfg, 18, 18);
        cfg_x  = EXTRACT_FIELD(r_cfg, 17, 17);
        cfg_v  = EXTRACT_FIELD(r_cfg, 16, 16);

        tlb_pfn = EXTRACT_FIELD(lo, 13, 31);
        tlb_v = EXTRACT_FIELD(lo, 3, 3);
        tlb_k = EXTRACT_FIELD(lo, 2, 2);
        tlb_w = EXTRACT_FIELD(lo, 1, 1);
        tlb_x = EXTRACT_FIELD(lo, 0, 0);

        /*
         * set_exception_vector(0x04, i_mmu_refill);
         * set_exception_vector(0x05, i_mmu_invalid);
         * set_exception_vector(0x06, i_mmu_access);
         * set_exception_vector(0x07, i_mmu_execute);
         * set_exception_vector(0x08, d_mmu_refill);
         * set_exception_vector(0x09, d_mmu_invalid);
         * set_exception_vector(0x0a, d_mmu_access);
         * set_exception_vector(0x0b, d_mmu_write);
         */
        if (cfg_k && tlb_k && usermode) {
            D(printf("tlb: kernel protected %x lo=%x pc=%x\n",
                     vaddr, lo, env->pc));
            match = 0;
            res->bf_vec = vect_base + 2;
        } else if (access_type == MMU_DATA_STORE && cfg_w && !tlb_w) {
            D(printf("tlb: write protected %x lo=%x pc=%x\n",
                     vaddr, lo, env->pc));
            match = 0;
            /* write accesses never go through the I mmu.  */
            res->bf_vec = vect_base + 3;
        } else if (access_type == MMU_INST_FETCH && cfg_x && !tlb_x) {
            D(printf("tlb: exec protected %x lo=%x pc=%x\n",
                     vaddr, lo, env->pc));
            match = 0;
            res->bf_vec = vect_base + 3;
        } else if (cfg_v && !tlb_v) {
            D(printf("tlb: invalid %x\n", vaddr));
            match = 0;
            res->bf_vec = vect_base + 1;
        }

        res->prot = 0;
        if (match) {
            res->prot |= PAGE_READ;
            if (tlb_w) {
                res->prot |= PAGE_WRITE;
            }
            if (mmu == 0 && (cfg_x || tlb_x)) {
                res->prot |= PAGE_EXEC;
            }
        } else {
            D(dump_tlb(env, mmu));
        }
    } else {
        /* If refill, provide a randomized set.  */
        set = env->mmu_rand_lfsr & 3;
    }

    if (!match && !debug) {
        cris_mmu_update_rand_lfsr(env);

        /* Compute index.  */
        idx = vpage & 15;

        /* Update RW_MM_TLB_SEL.  */
        env->sregs[SFR_RW_MM_TLB_SEL] = 0;
        set_field(&env->sregs[SFR_RW_MM_TLB_SEL], idx, 0, 4);
        set_field(&env->sregs[SFR_RW_MM_TLB_SEL], set, 4, 2);

        /* Update RW_MM_CAUSE.  */
        set_field(&r_cause, rwcause, 8, 2);
        set_field(&r_cause, vpage, 13, 19);
        set_field(&r_cause, pid, 0, 8);
        env->sregs[SFR_R_MM_CAUSE] = r_cause;
        D(printf("refill vaddr=%x pc=%x\n", vaddr, env->pc));
    }

    D(printf("%s access=%u mtch=%d pc=%x va=%x vpn=%x tlbvpn=%x pfn=%x pid=%x"
             " %x cause=%x sel=%x sp=%x %x %x\n",
             __func__, access_type, match, env->pc,
             vaddr, vpage,
             tlb_vpn, tlb_pfn, tlb_pid,
             pid,
             r_cause,
             env->sregs[SFR_RW_MM_TLB_SEL],
             env->regs[R_SP], env->pregs[PR_USP], env->ksp));

    res->phy = tlb_pfn << TARGET_PAGE_BITS;
    return !match;
}

void cris_mmu_flush_pid(CPUCRISState *env, uint32_t pid)
{
    target_ulong vaddr;
    unsigned int idx;
    uint32_t lo, hi;
    uint32_t tlb_vpn;
    int tlb_pid, tlb_g, tlb_v;
    unsigned int set;
    unsigned int mmu;

    pid &= 0xff;
    for (mmu = 0; mmu < 2; mmu++) {
        for (set = 0; set < 4; set++) {
            for (idx = 0; idx < 16; idx++) {
                lo = env->tlbsets[mmu][set][idx].lo;
                hi = env->tlbsets[mmu][set][idx].hi;

                tlb_vpn = EXTRACT_FIELD(hi, 13, 31);
                tlb_pid = EXTRACT_FIELD(hi, 0, 7);
                tlb_g  = EXTRACT_FIELD(lo, 4, 4);
                tlb_v = EXTRACT_FIELD(lo, 3, 3);

                if (tlb_v && !tlb_g && (tlb_pid == pid)) {
                    vaddr = tlb_vpn << TARGET_PAGE_BITS;
                    D_LOG("flush pid=%x vaddr=%x\n", pid, vaddr);
                    tlb_flush_page(env_cpu(env), vaddr);
                }
            }
        }
    }
}

int cris_mmu_translate(struct cris_mmu_result *res,
                       CPUCRISState *env, uint32_t vaddr,
                       MMUAccessType access_type, int mmu_idx, int debug)
{
    int seg;
    int miss = 0;
    int is_user = mmu_idx == MMU_USER_IDX;
    uint32_t old_srs;

    old_srs = env->pregs[PR_SRS];

    env->pregs[PR_SRS] = access_type == MMU_INST_FETCH ? 1 : 2;

    if (!cris_mmu_enabled(env->sregs[SFR_RW_GC_CFG])) {
        res->phy = vaddr;
        res->prot = PAGE_BITS;
        goto done;
    }

    seg = vaddr >> 28;
    if (!is_user && cris_mmu_segmented_addr(seg, env->sregs[SFR_RW_MM_CFG])) {
        uint32_t base;

        miss = 0;
        base = cris_mmu_translate_seg(env, seg);
        res->phy = base | (0x0fffffff & vaddr);
        res->prot = PAGE_BITS;
    } else {
        miss = cris_mmu_translate_page(res, env, vaddr, access_type,
                                       is_user, debug);
    }
 done:
    env->pregs[PR_SRS] = old_srs;
    return miss;
}
