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

#include "qemu/osdep.h"

#include "cpu.h"
#include "exec/exec-all.h"
#include "fpu/softfloat.h"
#include "exec/helper-proto.h"
#include "qemu/qemu-print.h"


#define CONVERT_BIT(X, SRC, DST) \
    (SRC > DST ? (X) / (SRC / DST) & (DST) : ((X) & SRC) * (DST / SRC))

uint64_t cpu_alpha_load_fpcr(CPUAlphaState *env)
{
    return (uint64_t)env->fpcr << 32;
}

void cpu_alpha_store_fpcr(CPUAlphaState *env, uint64_t val)
{
    uint32_t fpcr = val >> 32;
    uint32_t t = 0;

    t |= CONVERT_BIT(fpcr, FPCR_INED, FPCR_INE);
    t |= CONVERT_BIT(fpcr, FPCR_UNFD, FPCR_UNF);
    t |= CONVERT_BIT(fpcr, FPCR_OVFD, FPCR_OVF);
    t |= CONVERT_BIT(fpcr, FPCR_DZED, FPCR_DZE);
    t |= CONVERT_BIT(fpcr, FPCR_INVD, FPCR_INV);

    env->fpcr = fpcr;
    env->fpcr_exc_enable = ~t & FPCR_STATUS_MASK;

    switch (fpcr & FPCR_DYN_MASK) {
    case FPCR_DYN_NORMAL:
    default:
        t = float_round_nearest_even;
        break;
    case FPCR_DYN_CHOPPED:
        t = float_round_to_zero;
        break;
    case FPCR_DYN_MINUS:
        t = float_round_down;
        break;
    case FPCR_DYN_PLUS:
        t = float_round_up;
        break;
    }
    env->fpcr_dyn_round = t;

    env->fpcr_flush_to_zero = (fpcr & FPCR_UNFD) && (fpcr & FPCR_UNDZ);
    env->fp_status.flush_inputs_to_zero = (fpcr & FPCR_DNZ) != 0;

#ifdef CONFIG_USER_ONLY
    /*
     * Override some of these bits with the contents of ENV->SWCR.
     * In system mode, some of these would trap to the kernel, at
     * which point the kernel's handler would emulate and apply
     * the software exception mask.
     */
    if (env->swcr & SWCR_MAP_DMZ) {
        env->fp_status.flush_inputs_to_zero = 1;
    }
    if (env->swcr & SWCR_MAP_UMZ) {
        env->fp_status.flush_to_zero = 1;
    }
    env->fpcr_exc_enable &= ~(alpha_ieee_swcr_to_fpcr(env->swcr) >> 32);
#endif
}

uint64_t helper_load_fpcr(CPUAlphaState *env)
{
    return cpu_alpha_load_fpcr(env);
}

void helper_store_fpcr(CPUAlphaState *env, uint64_t val)
{
    cpu_alpha_store_fpcr(env, val);
}

static uint64_t *cpu_alpha_addr_gr(CPUAlphaState *env, unsigned reg)
{
#ifndef CONFIG_USER_ONLY
    if (env->flags & ENV_FLAG_PAL_MODE) {
        if (reg >= 8 && reg <= 14) {
            return &env->shadow[reg - 8];
        } else if (reg == 25) {
            return &env->shadow[7];
        }
    }
#endif
    return &env->ir[reg];
}

uint64_t cpu_alpha_load_gr(CPUAlphaState *env, unsigned reg)
{
    return *cpu_alpha_addr_gr(env, reg);
}

void cpu_alpha_store_gr(CPUAlphaState *env, unsigned reg, uint64_t val)
{
    *cpu_alpha_addr_gr(env, reg) = val;
}

#if defined(CONFIG_USER_ONLY)
bool alpha_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                        MMUAccessType access_type, int mmu_idx,
                        bool probe, uintptr_t retaddr)
{
    AlphaCPU *cpu = ALPHA_CPU(cs);

    cs->exception_index = EXCP_MMFAULT;
    cpu->env.trap_arg0 = address;
    cpu_loop_exit_restore(cs, retaddr);
}
#else
/* Returns the OSF/1 entMM failure indication, or -1 on success.  */
static int get_physical_address(CPUAlphaState *env, target_ulong addr,
                                int prot_need, int mmu_idx,
                                target_ulong *pphys, int *pprot)
{
    CPUState *cs = env_cpu(env);
    target_long saddr = addr;
    target_ulong phys = 0;
    target_ulong L1pte, L2pte, L3pte;
    target_ulong pt, index;
    int prot = 0;
    int ret = MM_K_ACV;

    /* Handle physical accesses.  */
    if (mmu_idx == MMU_PHYS_IDX) {
        phys = addr;
        prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        ret = -1;
        goto exit;
    }

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

    /* TODO: rather than using ldq_phys() to read the page table we should
     * use address_space_ldq() so that we can handle the case when
     * the page table read gives a bus fault, rather than ignoring it.
     * For the existing code the zero data that ldq_phys will return for
     * an access to invalid memory will result in our treating the page
     * table as invalid, which may even be the right behaviour.
     */

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

bool alpha_cpu_tlb_fill(CPUState *cs, vaddr addr, int size,
                        MMUAccessType access_type, int mmu_idx,
                        bool probe, uintptr_t retaddr)
{
    AlphaCPU *cpu = ALPHA_CPU(cs);
    CPUAlphaState *env = &cpu->env;
    target_ulong phys;
    int prot, fail;

    fail = get_physical_address(env, addr, 1 << access_type,
                                mmu_idx, &phys, &prot);
    if (unlikely(fail >= 0)) {
        if (probe) {
            return false;
        }
        cs->exception_index = EXCP_MMFAULT;
        env->trap_arg0 = addr;
        env->trap_arg1 = fail;
        env->trap_arg2 = (access_type == MMU_INST_FETCH ? -1 : access_type);
        cpu_loop_exit_restore(cs, retaddr);
    }

    tlb_set_page(cs, addr & TARGET_PAGE_MASK, phys & TARGET_PAGE_MASK,
                 prot, mmu_idx, TARGET_PAGE_SIZE);
    return true;
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
        }
        qemu_log("INT %6d: %s(%#x) cpu=%d pc=%016"
                 PRIx64 " sp=%016" PRIx64 "\n",
                 ++count, name, env->error_code, cs->cpu_index,
                 env->pc, env->ir[IR_SP]);
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
    env->exc_addr = env->pc | (env->flags & ENV_FLAG_PAL_MODE);

    /* Continue execution at the PALcode entry point.  */
    env->pc = env->palbr + i;

    /* Switch to PALmode.  */
    env->flags |= ENV_FLAG_PAL_MODE;
#endif /* !USER_ONLY */
}

bool alpha_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    AlphaCPU *cpu = ALPHA_CPU(cs);
    CPUAlphaState *env = &cpu->env;
    int idx = -1;

    /* We never take interrupts while in PALmode.  */
    if (env->flags & ENV_FLAG_PAL_MODE) {
        return false;
    }

    /* Fall through the switch, collecting the highest priority
       interrupt that isn't masked by the processor status IPL.  */
    /* ??? This hard-codes the OSF/1 interrupt levels.  */
    switch ((env->flags >> ENV_FLAG_PS_SHIFT) & PS_INT_MASK) {
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

void alpha_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    static const char linux_reg_names[31][4] = {
        "v0",  "t0",  "t1", "t2",  "t3", "t4", "t5", "t6",
        "t7",  "s0",  "s1", "s2",  "s3", "s4", "s5", "fp",
        "a0",  "a1",  "a2", "a3",  "a4", "a5", "t8", "t9",
        "t10", "t11", "ra", "t12", "at", "gp", "sp"
    };
    AlphaCPU *cpu = ALPHA_CPU(cs);
    CPUAlphaState *env = &cpu->env;
    int i;

    qemu_fprintf(f, "PC      " TARGET_FMT_lx " PS      %02x\n",
                 env->pc, extract32(env->flags, ENV_FLAG_PS_SHIFT, 8));
    for (i = 0; i < 31; i++) {
        qemu_fprintf(f, "%-8s" TARGET_FMT_lx "%c",
                     linux_reg_names[i], cpu_alpha_load_gr(env, i),
                     (i % 3) == 2 ? '\n' : ' ');
    }

    qemu_fprintf(f, "lock_a  " TARGET_FMT_lx " lock_v  " TARGET_FMT_lx "\n",
                 env->lock_addr, env->lock_value);

    if (flags & CPU_DUMP_FPU) {
        for (i = 0; i < 31; i++) {
            qemu_fprintf(f, "f%-7d%016" PRIx64 "%c", i, env->fir[i],
                         (i % 3) == 2 ? '\n' : ' ');
        }
        qemu_fprintf(f, "fpcr    %016" PRIx64 "\n", cpu_alpha_load_fpcr(env));
    }
    qemu_fprintf(f, "\n");
}

/* This should only be called from translate, via gen_excp.
   We expect that ENV->PC has already been updated.  */
void QEMU_NORETURN helper_excp(CPUAlphaState *env, int excp, int error)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = excp;
    env->error_code = error;
    cpu_loop_exit(cs);
}

/* This may be called from any of the helpers to set up EXCEPTION_INDEX.  */
void QEMU_NORETURN dynamic_excp(CPUAlphaState *env, uintptr_t retaddr,
                                int excp, int error)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = excp;
    env->error_code = error;
    if (retaddr) {
        cpu_restore_state(cs, retaddr, true);
        /* Floating-point exceptions (our only users) point to the next PC.  */
        env->pc += 4;
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
