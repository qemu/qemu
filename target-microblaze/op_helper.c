/*
 *  Microblaze helper routines.
 *
 *  Copyright (c) 2009 Edgar E. Iglesias <edgar.iglesias@gmail.com>.
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

#include <assert.h>
#include "exec.h"
#include "helper.h"
#include "host-utils.h"

#define D(x)

#if !defined(CONFIG_USER_ONLY)
#define MMUSUFFIX _mmu
#define SHIFT 0
#include "softmmu_template.h"
#define SHIFT 1
#include "softmmu_template.h"
#define SHIFT 2
#include "softmmu_template.h"
#define SHIFT 3
#include "softmmu_template.h"

/* Try to fill the TLB and return an exception if error. If retaddr is
   NULL, it means that the function was called in C code (i.e. not
   from generated code or from helper.c) */
/* XXX: fix it to restore all registers */
void tlb_fill (target_ulong addr, int is_write, int mmu_idx, void *retaddr)
{
    TranslationBlock *tb;
    CPUState *saved_env;
    unsigned long pc;
    int ret;

    /* XXX: hack to restore env in all cases, even if not called from
       generated code */
    saved_env = env;
    env = cpu_single_env;

    ret = cpu_mb_handle_mmu_fault(env, addr, is_write, mmu_idx, 1);
    if (unlikely(ret)) {
        if (retaddr) {
            /* now we have a real cpu fault */
            pc = (unsigned long)retaddr;
            tb = tb_find_pc(pc);
            if (tb) {
                /* the PC is inside the translated code. It means that we have
                   a virtual CPU fault */
                cpu_restore_state(tb, env, pc, NULL);
            }
        }
        cpu_loop_exit();
    }
    env = saved_env;
}
#endif

void helper_raise_exception(uint32_t index)
{
    env->exception_index = index;
    cpu_loop_exit();
}

void helper_debug(void)
{
    int i;

    qemu_log("PC=%8.8x\n", env->sregs[SR_PC]);
    qemu_log("rmsr=%x resr=%x debug[%x] imm=%x iflags=%x\n",
             env->sregs[SR_MSR], env->sregs[SR_ESR],
             env->debug, env->imm, env->iflags);
    qemu_log("btaken=%d btarget=%x mode=%s(saved=%s) eip=%d ie=%d\n",
             env->btaken, env->btarget,
             (env->sregs[SR_MSR] & MSR_UM) ? "user" : "kernel",
             (env->sregs[SR_MSR] & MSR_UMS) ? "user" : "kernel",
             (env->sregs[SR_MSR] & MSR_EIP),
             (env->sregs[SR_MSR] & MSR_IE));
    for (i = 0; i < 32; i++) {
        qemu_log("r%2.2d=%8.8x ", i, env->regs[i]);
        if ((i + 1) % 4 == 0)
            qemu_log("\n");
    }
    qemu_log("\n\n");
}

static inline uint32_t compute_carry(uint32_t a, uint32_t b, uint32_t cin)
{
    uint32_t cout = 0;

    if ((b == ~0) && cin)
        cout = 1;
    else if ((~0 - a) < (b + cin))
        cout = 1;
    return cout;
}

uint32_t helper_cmp(uint32_t a, uint32_t b)
{
    uint32_t t;

    t = b + ~a + 1;
    if ((b & 0x80000000) ^ (a & 0x80000000))
        t = (t & 0x7fffffff) | (b & 0x80000000);
    return t;
}

uint32_t helper_cmpu(uint32_t a, uint32_t b)
{
    uint32_t t;

    t = b + ~a + 1;
    if ((b & 0x80000000) ^ (a & 0x80000000))
        t = (t & 0x7fffffff) | (a & 0x80000000);
    return t;
}

uint32_t helper_addkc(uint32_t a, uint32_t b, uint32_t k, uint32_t c)
{
    uint32_t d, cf = 0, ncf;

    if (c)
        cf = env->sregs[SR_MSR] >> 31;
    assert(cf == 0 || cf == 1);
    d = a + b + cf;

    if (!k) {
        ncf = compute_carry(a, b, cf);
        assert(ncf == 0 || ncf == 1);
        if (ncf)
            env->sregs[SR_MSR] |= MSR_C | MSR_CC;
        else
            env->sregs[SR_MSR] &= ~(MSR_C | MSR_CC);
    }
    D(qemu_log("%x = %x + %x cf=%d ncf=%d k=%d c=%d\n",
               d, a, b, cf, ncf, k, c));
    return d;
}

uint32_t helper_subkc(uint32_t a, uint32_t b, uint32_t k, uint32_t c)
{
    uint32_t d, cf = 1, ncf;

    if (c)
        cf = env->sregs[SR_MSR] >> 31; 
    assert(cf == 0 || cf == 1);
    d = b + ~a + cf;

    if (!k) {
        ncf = compute_carry(b, ~a, cf);
        assert(ncf == 0 || ncf == 1);
        if (ncf)
            env->sregs[SR_MSR] |= MSR_C | MSR_CC;
        else
            env->sregs[SR_MSR] &= ~(MSR_C | MSR_CC);
    }
    D(qemu_log("%x = %x + %x cf=%d ncf=%d k=%d c=%d\n",
               d, a, b, cf, ncf, k, c));
    return d;
}

static inline int div_prepare(uint32_t a, uint32_t b)
{
    if (b == 0) {
        env->sregs[SR_MSR] |= MSR_DZ;

        if ((env->sregs[SR_MSR] & MSR_EE)
            && !(env->pvr.regs[2] & PVR2_DIV_ZERO_EXC_MASK)) {
            env->sregs[SR_ESR] = ESR_EC_DIVZERO;
            helper_raise_exception(EXCP_HW_EXCP);
        }
        return 0;
    }
    env->sregs[SR_MSR] &= ~MSR_DZ;
    return 1;
}

uint32_t helper_divs(uint32_t a, uint32_t b)
{
    if (!div_prepare(a, b))
        return 0;
    return (int32_t)a / (int32_t)b;
}

uint32_t helper_divu(uint32_t a, uint32_t b)
{
    if (!div_prepare(a, b))
        return 0;
    return a / b;
}

uint32_t helper_pcmpbf(uint32_t a, uint32_t b)
{
    unsigned int i;
    uint32_t mask = 0xff000000;

    for (i = 0; i < 4; i++) {
        if ((a & mask) == (b & mask))
            return i + 1;
        mask >>= 8;
    }
    return 0;
}

void helper_memalign(uint32_t addr, uint32_t dr, uint32_t wr, uint32_t mask)
{
    if (addr & mask) {
            qemu_log_mask(CPU_LOG_INT,
                          "unaligned access addr=%x mask=%x, wr=%d dr=r%d\n",
                          addr, mask, wr, dr);
            env->sregs[SR_EAR] = addr;
            env->sregs[SR_ESR] = ESR_EC_UNALIGNED_DATA | (wr << 10) \
                                 | (dr & 31) << 5;
            if (mask == 3) {
                env->sregs[SR_ESR] |= 1 << 11;
            }
            if (!(env->sregs[SR_MSR] & MSR_EE)) {
                return;
            }
            helper_raise_exception(EXCP_HW_EXCP);
    }
}

#if !defined(CONFIG_USER_ONLY)
/* Writes/reads to the MMU's special regs end up here.  */
uint32_t helper_mmu_read(uint32_t rn)
{
    return mmu_read(env, rn);
}

void helper_mmu_write(uint32_t rn, uint32_t v)
{
    mmu_write(env, rn, v);
}
#endif

void do_unassigned_access(target_phys_addr_t addr, int is_write, int is_exec,
                          int is_asi, int size)
{
    CPUState *saved_env;
    /* XXX: hack to restore env in all cases, even if not called from
       generated code */
    saved_env = env;
    env = cpu_single_env;
    qemu_log_mask(CPU_LOG_INT, "Unassigned " TARGET_FMT_plx " wr=%d exe=%d\n",
             addr, is_write, is_exec);
    if (!(env->sregs[SR_MSR] & MSR_EE)) {
        env = saved_env;
        return;
    }

    env->sregs[SR_EAR] = addr;
    if (is_exec) {
        if ((env->pvr.regs[2] & PVR2_IOPB_BUS_EXC_MASK)) {
            env->sregs[SR_ESR] = ESR_EC_INSN_BUS;
            helper_raise_exception(EXCP_HW_EXCP);
        }
    } else {
        if ((env->pvr.regs[2] & PVR2_DOPB_BUS_EXC_MASK)) {
            env->sregs[SR_ESR] = ESR_EC_DATA_BUS;
            helper_raise_exception(EXCP_HW_EXCP);
        }
    }
    env = saved_env;
}
