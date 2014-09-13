/*
 *  Alpha emulation cpu helpers for qemu.
 *
 *  Copyright (c) 2007 Jocelyn Mayer
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

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "cpu.h"
#include "fpu/softfloat.h"
#include "exec/helper-proto.h"

uint64_t cpu_alpha_load_fpcr (CPUAlphaState *env)
{
    uint64_t r = 0;
    uint8_t t;

    t = env->fpcr_exc_status;
    if (t) {
        r = FPCR_SUM;
        if (t & float_flag_invalid) {
            r |= FPCR_INV;
        }
        if (t & float_flag_divbyzero) {
            r |= FPCR_DZE;
        }
        if (t & float_flag_overflow) {
            r |= FPCR_OVF;
        }
        if (t & float_flag_underflow) {
            r |= FPCR_UNF;
        }
        if (t & float_flag_inexact) {
            r |= FPCR_INE;
        }
    }

    t = env->fpcr_exc_mask;
    if (t & float_flag_invalid) {
        r |= FPCR_INVD;
    }
    if (t & float_flag_divbyzero) {
        r |= FPCR_DZED;
    }
    if (t & float_flag_overflow) {
        r |= FPCR_OVFD;
    }
    if (t & float_flag_underflow) {
        r |= FPCR_UNFD;
    }
    if (t & float_flag_inexact) {
        r |= FPCR_INED;
    }

    switch (env->fpcr_dyn_round) {
    case float_round_nearest_even:
        r |= FPCR_DYN_NORMAL;
        break;
    case float_round_down:
        r |= FPCR_DYN_MINUS;
        break;
    case float_round_up:
        r |= FPCR_DYN_PLUS;
        break;
    case float_round_to_zero:
        r |= FPCR_DYN_CHOPPED;
        break;
    }

    if (env->fp_status.flush_inputs_to_zero) {
        r |= FPCR_DNZ;
    }
    if (env->fpcr_dnod) {
        r |= FPCR_DNOD;
    }
    if (env->fpcr_undz) {
        r |= FPCR_UNDZ;
    }

    return r;
}

void cpu_alpha_store_fpcr (CPUAlphaState *env, uint64_t val)
{
    uint8_t t;

    t = 0;
    if (val & FPCR_INV) {
        t |= float_flag_invalid;
    }
    if (val & FPCR_DZE) {
        t |= float_flag_divbyzero;
    }
    if (val & FPCR_OVF) {
        t |= float_flag_overflow;
    }
    if (val & FPCR_UNF) {
        t |= float_flag_underflow;
    }
    if (val & FPCR_INE) {
        t |= float_flag_inexact;
    }
    env->fpcr_exc_status = t;

    t = 0;
    if (val & FPCR_INVD) {
        t |= float_flag_invalid;
    }
    if (val & FPCR_DZED) {
        t |= float_flag_divbyzero;
    }
    if (val & FPCR_OVFD) {
        t |= float_flag_overflow;
    }
    if (val & FPCR_UNFD) {
        t |= float_flag_underflow;
    }
    if (val & FPCR_INED) {
        t |= float_flag_inexact;
    }
    env->fpcr_exc_mask = t;

    switch (val & FPCR_DYN_MASK) {
    case FPCR_DYN_CHOPPED:
        t = float_round_to_zero;
        break;
    case FPCR_DYN_MINUS:
        t = float_round_down;
        break;
    case FPCR_DYN_NORMAL:
        t = float_round_nearest_even;
        break;
    case FPCR_DYN_PLUS:
        t = float_round_up;
        break;
    }
    env->fpcr_dyn_round = t;

    env->fpcr_dnod = (val & FPCR_DNOD) != 0;
    env->fpcr_undz = (val & FPCR_UNDZ) != 0;
    env->fpcr_flush_to_zero = env->fpcr_dnod & env->fpcr_undz;
    env->fp_status.flush_inputs_to_zero = (val & FPCR_DNZ) != 0;
}

uint64_t helper_load_fpcr(CPUAlphaState *env)
{
    return cpu_alpha_load_fpcr(env);
}

void helper_store_fpcr(CPUAlphaState *env, uint64_t val)
{
    cpu_alpha_store_fpcr(env, val);
}

#if defined(CONFIG_USER_ONLY)
int alpha_cpu_handle_mmu_fault(CPUState *cs, vaddr address,
                               int rw, int mmu_idx)
{
    AlphaCPU *cpu = ALPHA_CPU(cs);

    cs->exception_index = EXCP_MMFAULT;
    cpu->env.trap_arg0 = address;
    return 1;
}
#else
void swap_shadow_regs(CPUAlphaState *env)
{
    uint64_t i0, i1, i2, i3, i4, i5, i6, i7;

    i0 = env->ir[8];
    i1 = env->ir[9];
    i2 = env->ir[10];
    i3 = env->ir[11];
    i4 = env->ir[12];
    i5 = env->ir[13];
    i6 = env->ir[14];
    i7 = env->ir[25];

    env->ir[8]  = env->shadow[0];
    env->ir[9]  = env->shadow[1];
    env->ir[10] = env->shadow[2];
    env->ir[11] = env->shadow[3];
    env->ir[12] = env->shadow[4];
    env->ir[13] = env->shadow[5];
    env->ir[14] = env->shadow[6];
    env->ir[25] = env->shadow[7];

    env->shadow[0] = i0;
    env->shadow[1] = i1;
    env->shadow[2] = i2;
    env->shadow[3] = i3;
    env->shadow[4] = i4;
    env->shadow[5] = i5;
    env->shadow[6] = i6;
    env->shadow[7] = i7;
}

/* Returns the OSF/1 entMM failure indication, or -1 on success.  */
static int get_physical_address(CPUAlphaState *env, target_ulong addr,
                                int prot_need, int mmu_idx,
                                target_ulong *pphys, int *pprot)
{
    CPUState *cs = CPU(alpha_env_get_cpu(env));
    target_long saddr = addr;
    target_ulong phys = 0;
    target_ulong L1pte, L2pte, L3pte;
    target_ulong pt, index;
    int prot = 0;
    int ret = MM_K_ACV;

    /* Ensure that the virtual address is properly sign-extended from
       the last implemented virtual address bit.  */
    if (saddr >> TARGET_VIRT_ADDR_SPACE_BITS != saddr >> 63) {
        goto exit;
    }

    /* Translate the superpage.  */
    /* ??? When we do more than emulate Unix PALcode, we'll need to
       determine which KSEG is actually active.  */
    if (saddr < 0 && ((saddr >> 41) & 3) == 2) {
        /* User-space cannot access KSEG addresses.  */
        if (mmu_idx != MMU_KERNEL_IDX) {
            goto exit;
        }

        /* For the benefit of the Typhoon chipset, move bit 40 to bit 43.
           We would not do this if the 48-bit KSEG is enabled.  */
        phys = saddr & ((1ull << 40) - 1);
        phys |= (saddr & (1ull << 40)) << 3;

        prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        ret = -1;
        goto exit;
    }

    /* Interpret the page table exactly like PALcode does.  */

    pt = env->ptbr;

    /* L1 page table read.  */
    index = (addr >> (TARGET_PAGE_BITS + 20)) & 0x3ff;
    L1pte = ldq_phys(cs->as, pt + index*8);

    if (unlikely((L1pte & PTE_VALID) == 0)) {
        ret = MM_K_TNV;
        goto exit;
    }
    if (unlikely((L1pte & PTE_KRE) == 0)) {
        goto exit;
    }
    pt = L1pte >> 32 << TARGET_PAGE_BITS;

    /* L2 page table read.  */
    index = (addr >> (TARGET_PAGE_BITS + 10)) & 0x3ff;
    L2pte = ldq_phys(cs->as, pt + index*8);

    if (unlikely((L2pte & PTE_VALID) == 0)) {
        ret = MM_K_TNV;
        goto exit;
    }
    if (unlikely((L2pte & PTE_KRE) == 0)) {
        goto exit;
    }
    pt = L2pte >> 32 << TARGET_PAGE_BITS;

    /* L3 page table read.  */
    index = (addr >> TARGET_PAGE_BITS) & 0x3ff;
    L3pte = ldq_phys(cs->as, pt + index*8);

    phys = L3pte >> 32 << TARGET_PAGE_BITS;
    if (unlikely((L3pte & PTE_VALID) == 0)) {
        ret = MM_K_TNV;
        goto exit;
    }

#if PAGE_READ != 1 || PAGE_WRITE != 2 || PAGE_EXEC != 4
# error page bits out of date
#endif

    /* Check access violations.  */
    if (L3pte & (PTE_KRE << mmu_idx)) {
        prot |= PAGE_READ | PAGE_EXEC;
    }
    if (L3pte & (PTE_KWE << mmu_idx)) {
        prot |= PAGE_WRITE;
    }
    if (unlikely((prot & prot_need) == 0 && prot_need)) {
        goto exit;
    }

    /* Check fault-on-operation violations.  */
    prot &= ~(L3pte >> 1);
    ret = -1;
    if (unlikely((prot & prot_need) == 0)) {
        ret = (prot_need & PAGE_EXEC ? MM_K_FOE :
               prot_need & PAGE_WRITE ? MM_K_FOW :
               prot_need & PAGE_READ ? MM_K_FOR : -1);
    }

 exit:
    *pphys = phys;
    *pprot = prot;
    return ret;
}

hwaddr alpha_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    AlphaCPU *cpu = ALPHA_CPU(cs);
    target_ulong phys;
    int prot, fail;

    fail = get_physical_address(&cpu->env, addr, 0, 0, &phys, &prot);
    return (fail >= 0 ? -1 : phys);
}

int alpha_cpu_handle_mmu_fault(CPUState *cs, vaddr addr, int rw,
                               int mmu_idx)
{
    AlphaCPU *cpu = ALPHA_CPU(cs);
    CPUAlphaState *env = &cpu->env;
    target_ulong phys;
    int prot, fail;

    fail = get_physical_address(env, addr, 1 << rw, mmu_idx, &phys, &prot);
    if (unlikely(fail >= 0)) {
        cs->exception_index = EXCP_MMFAULT;
        env->trap_arg0 = addr;
        env->trap_arg1 = fail;
        env->trap_arg2 = (rw == 2 ? -1 : rw);
        return 1;
    }

    tlb_set_page(cs, addr & TARGET_PAGE_MASK, phys & TARGET_PAGE_MASK,
                 prot, mmu_idx, TARGET_PAGE_SIZE);
    return 0;
}
#endif /* USER_ONLY */

void alpha_cpu_do_interrupt(CPUState *cs)
{
    AlphaCPU *cpu = ALPHA_CPU(cs);
    CPUAlphaState *env = &cpu->env;
    int i = cs->exception_index;

    if (qemu_loglevel_mask(CPU_LOG_INT)) {
        static int count;
        const char *name = "<unknown>";

        switch (i) {
        case EXCP_RESET:
            name = "reset";
            break;
        case EXCP_MCHK:
            name = "mchk";
            break;
        case EXCP_SMP_INTERRUPT:
            name = "smp_interrupt";
            break;
        case EXCP_CLK_INTERRUPT:
            name = "clk_interrupt";
            break;
        case EXCP_DEV_INTERRUPT:
            name = "dev_interrupt";
            break;
        case EXCP_MMFAULT:
            name = "mmfault";
            break;
        case EXCP_UNALIGN:
            name = "unalign";
            break;
        case EXCP_OPCDEC:
            name = "opcdec";
            break;
        case EXCP_ARITH:
            name = "arith";
            break;
        case EXCP_FEN:
            name = "fen";
            break;
        case EXCP_CALL_PAL:
            name = "call_pal";
            break;
        case EXCP_STL_C:
            name = "stl_c";
            break;
        case EXCP_STQ_C:
            name = "stq_c";
            break;
        }
        qemu_log("INT %6d: %s(%#x) pc=%016" PRIx64 " sp=%016" PRIx64 "\n",
                 ++count, name, env->error_code, env->pc, env->ir[IR_SP]);
    }

    cs->exception_index = -1;

#if !defined(CONFIG_USER_ONLY)
    switch (i) {
    case EXCP_RESET:
        i = 0x0000;
        break;
    case EXCP_MCHK:
        i = 0x0080;
        break;
    case EXCP_SMP_INTERRUPT:
        i = 0x0100;
        break;
    case EXCP_CLK_INTERRUPT:
        i = 0x0180;
        break;
    case EXCP_DEV_INTERRUPT:
        i = 0x0200;
        break;
    case EXCP_MMFAULT:
        i = 0x0280;
        break;
    case EXCP_UNALIGN:
        i = 0x0300;
        break;
    case EXCP_OPCDEC:
        i = 0x0380;
        break;
    case EXCP_ARITH:
        i = 0x0400;
        break;
    case EXCP_FEN:
        i = 0x0480;
        break;
    case EXCP_CALL_PAL:
        i = env->error_code;
        /* There are 64 entry points for both privileged and unprivileged,
           with bit 0x80 indicating unprivileged.  Each entry point gets
           64 bytes to do its job.  */
        if (i & 0x80) {
            i = 0x2000 + (i - 0x80) * 64;
        } else {
            i = 0x1000 + i * 64;
        }
        break;
    default:
        cpu_abort(cs, "Unhandled CPU exception");
    }

    /* Remember where the exception happened.  Emulate real hardware in
       that the low bit of the PC indicates PALmode.  */
    env->exc_addr = env->pc | env->pal_mode;

    /* Continue execution at the PALcode entry point.  */
    env->pc = env->palbr + i;

    /* Switch to PALmode.  */
    if (!env->pal_mode) {
        env->pal_mode = 1;
        swap_shadow_regs(env);
    }
#endif /* !USER_ONLY */
}

bool alpha_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    AlphaCPU *cpu = ALPHA_CPU(cs);
    CPUAlphaState *env = &cpu->env;
    int idx = -1;

    /* We never take interrupts while in PALmode.  */
    if (env->pal_mode) {
        return false;
    }

    /* Fall through the switch, collecting the highest priority
       interrupt that isn't masked by the processor status IPL.  */
    /* ??? This hard-codes the OSF/1 interrupt levels.  */
    switch (env->ps & PS_INT_MASK) {
    case 0 ... 3:
        if (interrupt_request & CPU_INTERRUPT_HARD) {
            idx = EXCP_DEV_INTERRUPT;
        }
        /* FALLTHRU */
    case 4:
        if (interrupt_request & CPU_INTERRUPT_TIMER) {
            idx = EXCP_CLK_INTERRUPT;
        }
        /* FALLTHRU */
    case 5:
        if (interrupt_request & CPU_INTERRUPT_SMP) {
            idx = EXCP_SMP_INTERRUPT;
        }
        /* FALLTHRU */
    case 6:
        if (interrupt_request & CPU_INTERRUPT_MCHK) {
            idx = EXCP_MCHK;
        }
    }
    if (idx >= 0) {
        cs->exception_index = idx;
        env->error_code = 0;
        alpha_cpu_do_interrupt(cs);
        return true;
    }
    return false;
}

void alpha_cpu_dump_state(CPUState *cs, FILE *f, fprintf_function cpu_fprintf,
                          int flags)
{
    static const char *linux_reg_names[] = {
        "v0 ", "t0 ", "t1 ", "t2 ", "t3 ", "t4 ", "t5 ", "t6 ",
        "t7 ", "s0 ", "s1 ", "s2 ", "s3 ", "s4 ", "s5 ", "fp ",
        "a0 ", "a1 ", "a2 ", "a3 ", "a4 ", "a5 ", "t8 ", "t9 ",
        "t10", "t11", "ra ", "t12", "at ", "gp ", "sp ", "zero",
    };
    AlphaCPU *cpu = ALPHA_CPU(cs);
    CPUAlphaState *env = &cpu->env;
    int i;

    cpu_fprintf(f, "     PC  " TARGET_FMT_lx "      PS  %02x\n",
                env->pc, env->ps);
    for (i = 0; i < 31; i++) {
        cpu_fprintf(f, "IR%02d %s " TARGET_FMT_lx " ", i,
                    linux_reg_names[i], env->ir[i]);
        if ((i % 3) == 2)
            cpu_fprintf(f, "\n");
    }

    cpu_fprintf(f, "lock_a   " TARGET_FMT_lx " lock_v   " TARGET_FMT_lx "\n",
                env->lock_addr, env->lock_value);

    for (i = 0; i < 31; i++) {
        cpu_fprintf(f, "FIR%02d    " TARGET_FMT_lx " ", i,
                    *((uint64_t *)(&env->fir[i])));
        if ((i % 3) == 2)
            cpu_fprintf(f, "\n");
    }
    cpu_fprintf(f, "\n");
}

/* This should only be called from translate, via gen_excp.
   We expect that ENV->PC has already been updated.  */
void QEMU_NORETURN helper_excp(CPUAlphaState *env, int excp, int error)
{
    AlphaCPU *cpu = alpha_env_get_cpu(env);
    CPUState *cs = CPU(cpu);

    cs->exception_index = excp;
    env->error_code = error;
    cpu_loop_exit(cs);
}

/* This may be called from any of the helpers to set up EXCEPTION_INDEX.  */
void QEMU_NORETURN dynamic_excp(CPUAlphaState *env, uintptr_t retaddr,
                                int excp, int error)
{
    AlphaCPU *cpu = alpha_env_get_cpu(env);
    CPUState *cs = CPU(cpu);

    cs->exception_index = excp;
    env->error_code = error;
    if (retaddr) {
        cpu_restore_state(cs, retaddr);
    }
    cpu_loop_exit(cs);
}

void QEMU_NORETURN arith_excp(CPUAlphaState *env, uintptr_t retaddr,
                              int exc, uint64_t mask)
{
    env->trap_arg0 = exc;
    env->trap_arg1 = mask;
    dynamic_excp(env, retaddr, EXCP_ARITH, 0);
}
