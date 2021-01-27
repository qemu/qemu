/*
 *  Microblaze MMU emulation for qemu.
 *
 *  Copyright (c) 2009 Edgar E. Iglesias
 *  Copyright (c) 2009-2012 PetaLogix Qld Pty Ltd.
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

static unsigned int tlb_decode_size(unsigned int f)
{
    static const unsigned int sizes[] = {
        1 * 1024, 4 * 1024, 16 * 1024, 64 * 1024, 256 * 1024,
        1 * 1024 * 1024, 4 * 1024 * 1024, 16 * 1024 * 1024
    };
    assert(f < ARRAY_SIZE(sizes));
    return sizes[f];
}

static void mmu_flush_idx(CPUMBState *env, unsigned int idx)
{
    CPUState *cs = env_cpu(env);
    MicroBlazeMMU *mmu = &env->mmu;
    unsigned int tlb_size;
    uint32_t tlb_tag, end, t;

    t = mmu->rams[RAM_TAG][idx];
    if (!(t & TLB_VALID))
        return;

    tlb_tag = t & TLB_EPN_MASK;
    tlb_size = tlb_decode_size((t & TLB_PAGESZ_MASK) >> 7);
    end = tlb_tag + tlb_size;

    while (tlb_tag < end) {
        tlb_flush_page(cs, tlb_tag);
        tlb_tag += TARGET_PAGE_SIZE;
    }
}

static void mmu_change_pid(CPUMBState *env, unsigned int newpid) 
{
    MicroBlazeMMU *mmu = &env->mmu;
    unsigned int i;
    uint32_t t;

    if (newpid & ~0xff)
        qemu_log_mask(LOG_GUEST_ERROR, "Illegal rpid=%x\n", newpid);

    for (i = 0; i < ARRAY_SIZE(mmu->rams[RAM_TAG]); i++) {
        /* Lookup and decode.  */
        t = mmu->rams[RAM_TAG][i];
        if (t & TLB_VALID) {
            if (mmu->tids[i] && ((mmu->regs[MMU_R_PID] & 0xff) == mmu->tids[i]))
                mmu_flush_idx(env, i);
        }
    }
}

/* rw - 0 = read, 1 = write, 2 = fetch.  */
unsigned int mmu_translate(MicroBlazeCPU *cpu, MicroBlazeMMULookup *lu,
                           target_ulong vaddr, MMUAccessType rw, int mmu_idx)
{
    MicroBlazeMMU *mmu = &cpu->env.mmu;
    unsigned int i, hit = 0;
    unsigned int tlb_ex = 0, tlb_wr = 0, tlb_zsel;
    uint64_t tlb_tag, tlb_rpn, mask;
    uint32_t tlb_size, t0;

    lu->err = ERR_MISS;
    for (i = 0; i < ARRAY_SIZE(mmu->rams[RAM_TAG]); i++) {
        uint64_t t, d;

        /* Lookup and decode.  */
        t = mmu->rams[RAM_TAG][i];
        if (t & TLB_VALID) {
            tlb_size = tlb_decode_size((t & TLB_PAGESZ_MASK) >> 7);
            if (tlb_size < TARGET_PAGE_SIZE) {
                qemu_log_mask(LOG_UNIMP, "%d pages not supported\n", tlb_size);
                abort();
            }

            mask = ~((uint64_t)tlb_size - 1);
            tlb_tag = t & TLB_EPN_MASK;
            if ((vaddr & mask) != (tlb_tag & mask)) {
                continue;
            }
            if (mmu->tids[i]
                && ((mmu->regs[MMU_R_PID] & 0xff) != mmu->tids[i])) {
                continue;
            }

            /* Bring in the data part.  */
            d = mmu->rams[RAM_DATA][i];
            tlb_ex = d & TLB_EX;
            tlb_wr = d & TLB_WR;

            /* Now let's see if there is a zone that overrides the protbits.  */
            tlb_zsel = (d >> 4) & 0xf;
            t0 = mmu->regs[MMU_R_ZPR] >> (30 - (tlb_zsel * 2));
            t0 &= 0x3;

            if (tlb_zsel > cpu->cfg.mmu_zones) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "tlb zone select out of range! %d\n", tlb_zsel);
                t0 = 1; /* Ignore.  */
            }

            if (cpu->cfg.mmu == 1) {
                t0 = 1; /* Zones are disabled.  */
            }

            switch (t0) {
                case 0:
                    if (mmu_idx == MMU_USER_IDX)
                        continue;
                    break;
                case 2:
                    if (mmu_idx != MMU_USER_IDX) {
                        tlb_ex = 1;
                        tlb_wr = 1;
                    }
                    break;
                case 3:
                    tlb_ex = 1;
                    tlb_wr = 1;
                    break;
                default: break;
            }

            lu->err = ERR_PROT;
            lu->prot = PAGE_READ;
            if (tlb_wr)
                lu->prot |= PAGE_WRITE;
            else if (rw == 1)
                goto done;
            if (tlb_ex)
                lu->prot |=PAGE_EXEC;
            else if (rw == 2) {
                goto done;
            }

            tlb_rpn = d & TLB_RPN_MASK;

            lu->vaddr = tlb_tag;
            lu->paddr = tlb_rpn & cpu->cfg.addr_mask;
            lu->size = tlb_size;
            lu->err = ERR_HIT;
            lu->idx = i;
            hit = 1;
            goto done;
        }
    }
done:
    qemu_log_mask(CPU_LOG_MMU,
                  "MMU vaddr=%" PRIx64 " rw=%d tlb_wr=%d tlb_ex=%d hit=%d\n",
                  vaddr, rw, tlb_wr, tlb_ex, hit);
    return hit;
}

/* Writes/reads to the MMU's special regs end up here.  */
uint32_t mmu_read(CPUMBState *env, bool ext, uint32_t rn)
{
    MicroBlazeCPU *cpu = env_archcpu(env);
    unsigned int i;
    uint32_t r = 0;

    if (cpu->cfg.mmu < 2 || !cpu->cfg.mmu_tlb_access) {
        qemu_log_mask(LOG_GUEST_ERROR, "MMU access on MMU-less system\n");
        return 0;
    }
    if (ext && rn != MMU_R_TLBLO) {
        qemu_log_mask(LOG_GUEST_ERROR, "Extended access only to TLBLO.\n");
        return 0;
    }

    switch (rn) {
        /* Reads to HI/LO trig reads from the mmu rams.  */
        case MMU_R_TLBLO:
        case MMU_R_TLBHI:
            if (!(cpu->cfg.mmu_tlb_access & 1)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "Invalid access to MMU reg %d\n", rn);
                return 0;
            }

            i = env->mmu.regs[MMU_R_TLBX] & 0xff;
            r = extract64(env->mmu.rams[rn & 1][i], ext * 32, 32);
            if (rn == MMU_R_TLBHI)
                env->mmu.regs[MMU_R_PID] = env->mmu.tids[i];
            break;
        case MMU_R_PID:
        case MMU_R_ZPR:
            if (!(cpu->cfg.mmu_tlb_access & 1)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "Invalid access to MMU reg %d\n", rn);
                return 0;
            }
            r = env->mmu.regs[rn];
            break;
        case MMU_R_TLBX:
            r = env->mmu.regs[rn];
            break;
        case MMU_R_TLBSX:
            qemu_log_mask(LOG_GUEST_ERROR, "TLBSX is write-only.\n");
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "Invalid MMU register %d.\n", rn);
            break;
    }
    qemu_log_mask(CPU_LOG_MMU, "%s rn=%d=%x\n", __func__, rn, r);
    return r;
}

void mmu_write(CPUMBState *env, bool ext, uint32_t rn, uint32_t v)
{
    MicroBlazeCPU *cpu = env_archcpu(env);
    uint64_t tmp64;
    unsigned int i;

    qemu_log_mask(CPU_LOG_MMU,
                  "%s rn=%d=%x old=%x\n", __func__, rn, v,
                  rn < 3 ? env->mmu.regs[rn] : env->mmu.regs[MMU_R_TLBX]);

    if (cpu->cfg.mmu < 2 || !cpu->cfg.mmu_tlb_access) {
        qemu_log_mask(LOG_GUEST_ERROR, "MMU access on MMU-less system\n");
        return;
    }
    if (ext && rn != MMU_R_TLBLO) {
        qemu_log_mask(LOG_GUEST_ERROR, "Extended access only to TLBLO.\n");
        return;
    }

    switch (rn) {
        /* Writes to HI/LO trig writes to the mmu rams.  */
        case MMU_R_TLBLO:
        case MMU_R_TLBHI:
            i = env->mmu.regs[MMU_R_TLBX] & 0xff;
            if (rn == MMU_R_TLBHI) {
                if (i < 3 && !(v & TLB_VALID) && qemu_loglevel_mask(~0))
                    qemu_log_mask(LOG_GUEST_ERROR,
                                  "invalidating index %x at pc=%x\n",
                                  i, env->pc);
                env->mmu.tids[i] = env->mmu.regs[MMU_R_PID] & 0xff;
                mmu_flush_idx(env, i);
            }
            tmp64 = env->mmu.rams[rn & 1][i];
            env->mmu.rams[rn & 1][i] = deposit64(tmp64, ext * 32, 32, v);
            break;
        case MMU_R_ZPR:
            if (cpu->cfg.mmu_tlb_access <= 1) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "Invalid access to MMU reg %d\n", rn);
                return;
            }

            /* Changes to the zone protection reg flush the QEMU TLB.
               Fortunately, these are very uncommon.  */
            if (v != env->mmu.regs[rn]) {
                tlb_flush(env_cpu(env));
            }
            env->mmu.regs[rn] = v;
            break;
        case MMU_R_PID:
            if (cpu->cfg.mmu_tlb_access <= 1) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "Invalid access to MMU reg %d\n", rn);
                return;
            }

            if (v != env->mmu.regs[rn]) {
                mmu_change_pid(env, v);
                env->mmu.regs[rn] = v;
            }
            break;
        case MMU_R_TLBX:
            /* Bit 31 is read-only.  */
            env->mmu.regs[rn] = deposit32(env->mmu.regs[rn], 0, 31, v);
            break;
        case MMU_R_TLBSX:
        {
            MicroBlazeMMULookup lu;
            int hit;

            if (cpu->cfg.mmu_tlb_access <= 1) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "Invalid access to MMU reg %d\n", rn);
                return;
            }

            hit = mmu_translate(cpu, &lu, v & TLB_EPN_MASK,
                                0, cpu_mmu_index(env, false));
            if (hit) {
                env->mmu.regs[MMU_R_TLBX] = lu.idx;
            } else {
                env->mmu.regs[MMU_R_TLBX] |= R_TBLX_MISS_MASK;
            }
            break;
        }
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "Invalid MMU register %d.\n", rn);
            break;
   }
}

void mmu_init(MicroBlazeMMU *mmu)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(mmu->regs); i++) {
        mmu->regs[i] = 0;
    }
}
