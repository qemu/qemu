/*
 *  MIPS emulation helpers for qemu.
 *
 *  Copyright (c) 2004-2005 Jocelyn Mayer
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <stdlib.h>
#include "exec.h"

#include "host-utils.h"

#ifdef __s390__
# define GETPC() ((void*)((unsigned long)__builtin_return_address(0) & 0x7fffffffUL))
#else
# define GETPC() (__builtin_return_address(0))
#endif

/*****************************************************************************/
/* Exceptions processing helpers */

void do_raise_exception_err (uint32_t exception, int error_code)
{
#if 1
    if (logfile && exception < 0x100)
        fprintf(logfile, "%s: %d %d\n", __func__, exception, error_code);
#endif
    env->exception_index = exception;
    env->error_code = error_code;
    T0 = 0;
    cpu_loop_exit();
}

void do_raise_exception (uint32_t exception)
{
    do_raise_exception_err(exception, 0);
}

void do_restore_state (void *pc_ptr)
{
  TranslationBlock *tb;
  unsigned long pc = (unsigned long) pc_ptr;

  tb = tb_find_pc (pc);
  cpu_restore_state (tb, env, pc, NULL);
}

void do_raise_exception_direct_err (uint32_t exception, int error_code)
{
    do_restore_state (GETPC ());
    do_raise_exception_err (exception, error_code);
}

void do_raise_exception_direct (uint32_t exception)
{
    do_raise_exception_direct_err (exception, 0);
}

#if defined(TARGET_MIPS64)
#if TARGET_LONG_BITS > HOST_LONG_BITS
/* Those might call libgcc functions.  */
void do_dsll (void)
{
    T0 = T0 << T1;
}

void do_dsll32 (void)
{
    T0 = T0 << (T1 + 32);
}

void do_dsra (void)
{
    T0 = (int64_t)T0 >> T1;
}

void do_dsra32 (void)
{
    T0 = (int64_t)T0 >> (T1 + 32);
}

void do_dsrl (void)
{
    T0 = T0 >> T1;
}

void do_dsrl32 (void)
{
    T0 = T0 >> (T1 + 32);
}

void do_drotr (void)
{
    target_ulong tmp;

    if (T1) {
        tmp = T0 << (0x40 - T1);
        T0 = (T0 >> T1) | tmp;
    }
}

void do_drotr32 (void)
{
    target_ulong tmp;

    tmp = T0 << (0x40 - (32 + T1));
    T0 = (T0 >> (32 + T1)) | tmp;
}

void do_dsllv (void)
{
    T0 = T1 << (T0 & 0x3F);
}

void do_dsrav (void)
{
    T0 = (int64_t)T1 >> (T0 & 0x3F);
}

void do_dsrlv (void)
{
    T0 = T1 >> (T0 & 0x3F);
}

void do_drotrv (void)
{
    target_ulong tmp;

    T0 &= 0x3F;
    if (T0) {
        tmp = T1 << (0x40 - T0);
        T0 = (T1 >> T0) | tmp;
    } else
        T0 = T1;
}

void do_dclo (void)
{
    T0 = clo64(T0);
}

void do_dclz (void)
{
    T0 = clz64(T0);
}

#endif /* TARGET_LONG_BITS > HOST_LONG_BITS */
#endif /* TARGET_MIPS64 */

/* 64 bits arithmetic for 32 bits hosts */
#if TARGET_LONG_BITS > HOST_LONG_BITS
static always_inline uint64_t get_HILO (void)
{
    return (env->HI[0][env->current_tc] << 32) | (uint32_t)env->LO[0][env->current_tc];
}

static always_inline void set_HILO (uint64_t HILO)
{
    env->LO[0][env->current_tc] = (int32_t)HILO;
    env->HI[0][env->current_tc] = (int32_t)(HILO >> 32);
}

static always_inline void set_HIT0_LO (uint64_t HILO)
{
    env->LO[0][env->current_tc] = (int32_t)(HILO & 0xFFFFFFFF);
    T0 = env->HI[0][env->current_tc] = (int32_t)(HILO >> 32);
}

static always_inline void set_HI_LOT0 (uint64_t HILO)
{
    T0 = env->LO[0][env->current_tc] = (int32_t)(HILO & 0xFFFFFFFF);
    env->HI[0][env->current_tc] = (int32_t)(HILO >> 32);
}

void do_mult (void)
{
    set_HILO((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1);
}

void do_multu (void)
{
    set_HILO((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1);
}

void do_madd (void)
{
    int64_t tmp;

    tmp = ((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1);
    set_HILO((int64_t)get_HILO() + tmp);
}

void do_maddu (void)
{
    uint64_t tmp;

    tmp = ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1);
    set_HILO(get_HILO() + tmp);
}

void do_msub (void)
{
    int64_t tmp;

    tmp = ((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1);
    set_HILO((int64_t)get_HILO() - tmp);
}

void do_msubu (void)
{
    uint64_t tmp;

    tmp = ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1);
    set_HILO(get_HILO() - tmp);
}

/* Multiplication variants of the vr54xx. */
void do_muls (void)
{
    set_HI_LOT0(0 - ((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1));
}

void do_mulsu (void)
{
    set_HI_LOT0(0 - ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1));
}

void do_macc (void)
{
    set_HI_LOT0(((int64_t)get_HILO()) + ((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1));
}

void do_macchi (void)
{
    set_HIT0_LO(((int64_t)get_HILO()) + ((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1));
}

void do_maccu (void)
{
    set_HI_LOT0(((uint64_t)get_HILO()) + ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1));
}

void do_macchiu (void)
{
    set_HIT0_LO(((uint64_t)get_HILO()) + ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1));
}

void do_msac (void)
{
    set_HI_LOT0(((int64_t)get_HILO()) - ((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1));
}

void do_msachi (void)
{
    set_HIT0_LO(((int64_t)get_HILO()) - ((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1));
}

void do_msacu (void)
{
    set_HI_LOT0(((uint64_t)get_HILO()) - ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1));
}

void do_msachiu (void)
{
    set_HIT0_LO(((uint64_t)get_HILO()) - ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1));
}

void do_mulhi (void)
{
    set_HIT0_LO((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1);
}

void do_mulhiu (void)
{
    set_HIT0_LO((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1);
}

void do_mulshi (void)
{
    set_HIT0_LO(0 - ((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1));
}

void do_mulshiu (void)
{
    set_HIT0_LO(0 - ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1));
}
#endif /* TARGET_LONG_BITS > HOST_LONG_BITS */

#if HOST_LONG_BITS < 64
void do_div (void)
{
    /* 64bit datatypes because we may see overflow/underflow. */
    if (T1 != 0) {
        env->LO[0][env->current_tc] = (int32_t)((int64_t)(int32_t)T0 / (int32_t)T1);
        env->HI[0][env->current_tc] = (int32_t)((int64_t)(int32_t)T0 % (int32_t)T1);
    }
}
#endif

#if defined(TARGET_MIPS64)
void do_ddiv (void)
{
    if (T1 != 0) {
        int64_t arg0 = (int64_t)T0;
        int64_t arg1 = (int64_t)T1;
        if (arg0 == ((int64_t)-1 << 63) && arg1 == (int64_t)-1) {
            env->LO[0][env->current_tc] = arg0;
            env->HI[0][env->current_tc] = 0;
        } else {
            lldiv_t res = lldiv(arg0, arg1);
            env->LO[0][env->current_tc] = res.quot;
            env->HI[0][env->current_tc] = res.rem;
        }
    }
}

#if TARGET_LONG_BITS > HOST_LONG_BITS
void do_ddivu (void)
{
    if (T1 != 0) {
        env->LO[0][env->current_tc] = T0 / T1;
        env->HI[0][env->current_tc] = T0 % T1;
    }
}
#endif
#endif /* TARGET_MIPS64 */

#if defined(CONFIG_USER_ONLY)
void do_mfc0_random (void)
{
    cpu_abort(env, "mfc0 random\n");
}

void do_mfc0_count (void)
{
    cpu_abort(env, "mfc0 count\n");
}

void cpu_mips_store_count(CPUState *env, uint32_t value)
{
    cpu_abort(env, "mtc0 count\n");
}

void cpu_mips_store_compare(CPUState *env, uint32_t value)
{
    cpu_abort(env, "mtc0 compare\n");
}

void cpu_mips_start_count(CPUState *env)
{
    cpu_abort(env, "start count\n");
}

void cpu_mips_stop_count(CPUState *env)
{
    cpu_abort(env, "stop count\n");
}

void cpu_mips_update_irq(CPUState *env)
{
    cpu_abort(env, "mtc0 status / mtc0 cause\n");
}

void do_mtc0_status_debug(uint32_t old, uint32_t val)
{
    cpu_abort(env, "mtc0 status debug\n");
}

void do_mtc0_status_irqraise_debug (void)
{
    cpu_abort(env, "mtc0 status irqraise debug\n");
}

void cpu_mips_tlb_flush (CPUState *env, int flush_global)
{
    cpu_abort(env, "mips_tlb_flush\n");
}

#else

/* CP0 helpers */
void do_mfc0_random (void)
{
    T0 = (int32_t)cpu_mips_get_random(env);
}

void do_mfc0_count (void)
{
    T0 = (int32_t)cpu_mips_get_count(env);
}

void do_mtc0_status_debug(uint32_t old, uint32_t val)
{
    fprintf(logfile, "Status %08x (%08x) => %08x (%08x) Cause %08x",
            old, old & env->CP0_Cause & CP0Ca_IP_mask,
            val, val & env->CP0_Cause & CP0Ca_IP_mask,
            env->CP0_Cause);
    switch (env->hflags & MIPS_HFLAG_KSU) {
    case MIPS_HFLAG_UM: fputs(", UM\n", logfile); break;
    case MIPS_HFLAG_SM: fputs(", SM\n", logfile); break;
    case MIPS_HFLAG_KM: fputs("\n", logfile); break;
    default: cpu_abort(env, "Invalid MMU mode!\n"); break;
    }
}

void do_mtc0_status_irqraise_debug(void)
{
    fprintf(logfile, "Raise pending IRQs\n");
}

void fpu_handle_exception(void)
{
#ifdef CONFIG_SOFTFLOAT
    int flags = get_float_exception_flags(&env->fpu->fp_status);
    unsigned int cpuflags = 0, enable, cause = 0;

    enable = GET_FP_ENABLE(env->fpu->fcr31);

    /* determine current flags */
    if (flags & float_flag_invalid) {
        cpuflags |= FP_INVALID;
        cause |= FP_INVALID & enable;
    }
    if (flags & float_flag_divbyzero) {
        cpuflags |= FP_DIV0;
        cause |= FP_DIV0 & enable;
    }
    if (flags & float_flag_overflow) {
        cpuflags |= FP_OVERFLOW;
        cause |= FP_OVERFLOW & enable;
    }
    if (flags & float_flag_underflow) {
        cpuflags |= FP_UNDERFLOW;
        cause |= FP_UNDERFLOW & enable;
    }
    if (flags & float_flag_inexact) {
        cpuflags |= FP_INEXACT;
        cause |= FP_INEXACT & enable;
    }
    SET_FP_FLAGS(env->fpu->fcr31, cpuflags);
    SET_FP_CAUSE(env->fpu->fcr31, cause);
#else
    SET_FP_FLAGS(env->fpu->fcr31, 0);
    SET_FP_CAUSE(env->fpu->fcr31, 0);
#endif
}

/* TLB management */
void cpu_mips_tlb_flush (CPUState *env, int flush_global)
{
    /* Flush qemu's TLB and discard all shadowed entries.  */
    tlb_flush (env, flush_global);
    env->tlb->tlb_in_use = env->tlb->nb_tlb;
}

static void r4k_mips_tlb_flush_extra (CPUState *env, int first)
{
    /* Discard entries from env->tlb[first] onwards.  */
    while (env->tlb->tlb_in_use > first) {
        r4k_invalidate_tlb(env, --env->tlb->tlb_in_use, 0);
    }
}

static void r4k_fill_tlb (int idx)
{
    r4k_tlb_t *tlb;

    /* XXX: detect conflicting TLBs and raise a MCHECK exception when needed */
    tlb = &env->tlb->mmu.r4k.tlb[idx];
    tlb->VPN = env->CP0_EntryHi & (TARGET_PAGE_MASK << 1);
#if defined(TARGET_MIPS64)
    tlb->VPN &= env->SEGMask;
#endif
    tlb->ASID = env->CP0_EntryHi & 0xFF;
    tlb->PageMask = env->CP0_PageMask;
    tlb->G = env->CP0_EntryLo0 & env->CP0_EntryLo1 & 1;
    tlb->V0 = (env->CP0_EntryLo0 & 2) != 0;
    tlb->D0 = (env->CP0_EntryLo0 & 4) != 0;
    tlb->C0 = (env->CP0_EntryLo0 >> 3) & 0x7;
    tlb->PFN[0] = (env->CP0_EntryLo0 >> 6) << 12;
    tlb->V1 = (env->CP0_EntryLo1 & 2) != 0;
    tlb->D1 = (env->CP0_EntryLo1 & 4) != 0;
    tlb->C1 = (env->CP0_EntryLo1 >> 3) & 0x7;
    tlb->PFN[1] = (env->CP0_EntryLo1 >> 6) << 12;
}

void r4k_do_tlbwi (void)
{
    /* Discard cached TLB entries.  We could avoid doing this if the
       tlbwi is just upgrading access permissions on the current entry;
       that might be a further win.  */
    r4k_mips_tlb_flush_extra (env, env->tlb->nb_tlb);

    r4k_invalidate_tlb(env, env->CP0_Index % env->tlb->nb_tlb, 0);
    r4k_fill_tlb(env->CP0_Index % env->tlb->nb_tlb);
}

void r4k_do_tlbwr (void)
{
    int r = cpu_mips_get_random(env);

    r4k_invalidate_tlb(env, r, 1);
    r4k_fill_tlb(r);
}

void r4k_do_tlbp (void)
{
    r4k_tlb_t *tlb;
    target_ulong mask;
    target_ulong tag;
    target_ulong VPN;
    uint8_t ASID;
    int i;

    ASID = env->CP0_EntryHi & 0xFF;
    for (i = 0; i < env->tlb->nb_tlb; i++) {
        tlb = &env->tlb->mmu.r4k.tlb[i];
        /* 1k pages are not supported. */
        mask = tlb->PageMask | ~(TARGET_PAGE_MASK << 1);
        tag = env->CP0_EntryHi & ~mask;
        VPN = tlb->VPN & ~mask;
        /* Check ASID, virtual page number & size */
        if ((tlb->G == 1 || tlb->ASID == ASID) && VPN == tag) {
            /* TLB match */
            env->CP0_Index = i;
            break;
        }
    }
    if (i == env->tlb->nb_tlb) {
        /* No match.  Discard any shadow entries, if any of them match.  */
        for (i = env->tlb->nb_tlb; i < env->tlb->tlb_in_use; i++) {
	    tlb = &env->tlb->mmu.r4k.tlb[i];
	    /* 1k pages are not supported. */
	    mask = tlb->PageMask | ~(TARGET_PAGE_MASK << 1);
	    tag = env->CP0_EntryHi & ~mask;
	    VPN = tlb->VPN & ~mask;
	    /* Check ASID, virtual page number & size */
	    if ((tlb->G == 1 || tlb->ASID == ASID) && VPN == tag) {
                r4k_mips_tlb_flush_extra (env, i);
	        break;
	    }
	}

        env->CP0_Index |= 0x80000000;
    }
}

void r4k_do_tlbr (void)
{
    r4k_tlb_t *tlb;
    uint8_t ASID;

    ASID = env->CP0_EntryHi & 0xFF;
    tlb = &env->tlb->mmu.r4k.tlb[env->CP0_Index % env->tlb->nb_tlb];

    /* If this will change the current ASID, flush qemu's TLB.  */
    if (ASID != tlb->ASID)
        cpu_mips_tlb_flush (env, 1);

    r4k_mips_tlb_flush_extra(env, env->tlb->nb_tlb);

    env->CP0_EntryHi = tlb->VPN | tlb->ASID;
    env->CP0_PageMask = tlb->PageMask;
    env->CP0_EntryLo0 = tlb->G | (tlb->V0 << 1) | (tlb->D0 << 2) |
                        (tlb->C0 << 3) | (tlb->PFN[0] >> 6);
    env->CP0_EntryLo1 = tlb->G | (tlb->V1 << 1) | (tlb->D1 << 2) |
                        (tlb->C1 << 3) | (tlb->PFN[1] >> 6);
}

#endif /* !CONFIG_USER_ONLY */

void dump_ldst (const unsigned char *func)
{
    if (loglevel)
        fprintf(logfile, "%s => " TARGET_FMT_lx " " TARGET_FMT_lx "\n", __func__, T0, T1);
}

void dump_sc (void)
{
    if (loglevel) {
        fprintf(logfile, "%s " TARGET_FMT_lx " at " TARGET_FMT_lx " (" TARGET_FMT_lx ")\n", __func__,
                T1, T0, env->CP0_LLAddr);
    }
}

void debug_pre_eret (void)
{
    fprintf(logfile, "ERET: PC " TARGET_FMT_lx " EPC " TARGET_FMT_lx,
            env->PC[env->current_tc], env->CP0_EPC);
    if (env->CP0_Status & (1 << CP0St_ERL))
        fprintf(logfile, " ErrorEPC " TARGET_FMT_lx, env->CP0_ErrorEPC);
    if (env->hflags & MIPS_HFLAG_DM)
        fprintf(logfile, " DEPC " TARGET_FMT_lx, env->CP0_DEPC);
    fputs("\n", logfile);
}

void debug_post_eret (void)
{
    fprintf(logfile, "  =>  PC " TARGET_FMT_lx " EPC " TARGET_FMT_lx,
            env->PC[env->current_tc], env->CP0_EPC);
    if (env->CP0_Status & (1 << CP0St_ERL))
        fprintf(logfile, " ErrorEPC " TARGET_FMT_lx, env->CP0_ErrorEPC);
    if (env->hflags & MIPS_HFLAG_DM)
        fprintf(logfile, " DEPC " TARGET_FMT_lx, env->CP0_DEPC);
    switch (env->hflags & MIPS_HFLAG_KSU) {
    case MIPS_HFLAG_UM: fputs(", UM\n", logfile); break;
    case MIPS_HFLAG_SM: fputs(", SM\n", logfile); break;
    case MIPS_HFLAG_KM: fputs("\n", logfile); break;
    default: cpu_abort(env, "Invalid MMU mode!\n"); break;
    }
}

void do_pmon (int function)
{
    function /= 2;
    switch (function) {
    case 2: /* TODO: char inbyte(int waitflag); */
        if (env->gpr[4][env->current_tc] == 0)
            env->gpr[2][env->current_tc] = -1;
        /* Fall through */
    case 11: /* TODO: char inbyte (void); */
        env->gpr[2][env->current_tc] = -1;
        break;
    case 3:
    case 12:
        printf("%c", (char)(env->gpr[4][env->current_tc] & 0xFF));
        break;
    case 17:
        break;
    case 158:
        {
            unsigned char *fmt = (void *)(unsigned long)env->gpr[4][env->current_tc];
            printf("%s", fmt);
        }
        break;
    }
}

#if !defined(CONFIG_USER_ONLY)

static void do_unaligned_access (target_ulong addr, int is_write, int is_user, void *retaddr);

#define MMUSUFFIX _mmu
#define ALIGNED_ONLY

#define SHIFT 0
#include "softmmu_template.h"

#define SHIFT 1
#include "softmmu_template.h"

#define SHIFT 2
#include "softmmu_template.h"

#define SHIFT 3
#include "softmmu_template.h"

static void do_unaligned_access (target_ulong addr, int is_write, int is_user, void *retaddr)
{
    env->CP0_BadVAddr = addr;
    do_restore_state (retaddr);
    do_raise_exception ((is_write == 1) ? EXCP_AdES : EXCP_AdEL);
}

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
    ret = cpu_mips_handle_mmu_fault(env, addr, is_write, mmu_idx, 1);
    if (ret) {
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
        do_raise_exception_err(env->exception_index, env->error_code);
    }
    env = saved_env;
}

void do_unassigned_access(target_phys_addr_t addr, int is_write, int is_exec,
                          int unused)
{
    if (is_exec)
        do_raise_exception(EXCP_IBE);
    else
        do_raise_exception(EXCP_DBE);
}
#endif

/* Complex FPU operations which may need stack space. */

#define FLOAT_ONE32 make_float32(0x3f8 << 20)
#define FLOAT_ONE64 make_float64(0x3ffULL << 52)
#define FLOAT_TWO32 make_float32(1 << 30)
#define FLOAT_TWO64 make_float64(1ULL << 62)
#define FLOAT_QNAN32 0x7fbfffff
#define FLOAT_QNAN64 0x7ff7ffffffffffffULL
#define FLOAT_SNAN32 0x7fffffff
#define FLOAT_SNAN64 0x7fffffffffffffffULL

/* convert MIPS rounding mode in FCR31 to IEEE library */
unsigned int ieee_rm[] = {
    float_round_nearest_even,
    float_round_to_zero,
    float_round_up,
    float_round_down
};

#define RESTORE_ROUNDING_MODE \
    set_float_rounding_mode(ieee_rm[env->fpu->fcr31 & 3], &env->fpu->fp_status)

void do_cfc1 (int reg)
{
    switch (reg) {
    case 0:
        T0 = (int32_t)env->fpu->fcr0;
        break;
    case 25:
        T0 = ((env->fpu->fcr31 >> 24) & 0xfe) | ((env->fpu->fcr31 >> 23) & 0x1);
        break;
    case 26:
        T0 = env->fpu->fcr31 & 0x0003f07c;
        break;
    case 28:
        T0 = (env->fpu->fcr31 & 0x00000f83) | ((env->fpu->fcr31 >> 22) & 0x4);
        break;
    default:
        T0 = (int32_t)env->fpu->fcr31;
        break;
    }
}

void do_ctc1 (int reg)
{
    switch(reg) {
    case 25:
        if (T0 & 0xffffff00)
            return;
        env->fpu->fcr31 = (env->fpu->fcr31 & 0x017fffff) | ((T0 & 0xfe) << 24) |
                     ((T0 & 0x1) << 23);
        break;
    case 26:
        if (T0 & 0x007c0000)
            return;
        env->fpu->fcr31 = (env->fpu->fcr31 & 0xfffc0f83) | (T0 & 0x0003f07c);
        break;
    case 28:
        if (T0 & 0x007c0000)
            return;
        env->fpu->fcr31 = (env->fpu->fcr31 & 0xfefff07c) | (T0 & 0x00000f83) |
                     ((T0 & 0x4) << 22);
        break;
    case 31:
        if (T0 & 0x007c0000)
            return;
        env->fpu->fcr31 = T0;
        break;
    default:
        return;
    }
    /* set rounding mode */
    RESTORE_ROUNDING_MODE;
    set_float_exception_flags(0, &env->fpu->fp_status);
    if ((GET_FP_ENABLE(env->fpu->fcr31) | 0x20) & GET_FP_CAUSE(env->fpu->fcr31))
        do_raise_exception(EXCP_FPE);
}

static always_inline char ieee_ex_to_mips(char xcpt)
{
    return (xcpt & float_flag_inexact) >> 5 |
           (xcpt & float_flag_underflow) >> 3 |
           (xcpt & float_flag_overflow) >> 1 |
           (xcpt & float_flag_divbyzero) << 1 |
           (xcpt & float_flag_invalid) << 4;
}

static always_inline char mips_ex_to_ieee(char xcpt)
{
    return (xcpt & FP_INEXACT) << 5 |
           (xcpt & FP_UNDERFLOW) << 3 |
           (xcpt & FP_OVERFLOW) << 1 |
           (xcpt & FP_DIV0) >> 1 |
           (xcpt & FP_INVALID) >> 4;
}

static always_inline void update_fcr31(void)
{
    int tmp = ieee_ex_to_mips(get_float_exception_flags(&env->fpu->fp_status));

    SET_FP_CAUSE(env->fpu->fcr31, tmp);
    if (GET_FP_ENABLE(env->fpu->fcr31) & tmp)
        do_raise_exception(EXCP_FPE);
    else
        UPDATE_FP_FLAGS(env->fpu->fcr31, tmp);
}

#define FLOAT_OP(name, p) void do_float_##name##_##p(void)

FLOAT_OP(cvtd, s)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FDT2 = float32_to_float64(FST0, &env->fpu->fp_status);
    update_fcr31();
}
FLOAT_OP(cvtd, w)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FDT2 = int32_to_float64(WT0, &env->fpu->fp_status);
    update_fcr31();
}
FLOAT_OP(cvtd, l)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FDT2 = int64_to_float64(DT0, &env->fpu->fp_status);
    update_fcr31();
}
FLOAT_OP(cvtl, d)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    DT2 = float64_to_int64(FDT0, &env->fpu->fp_status);
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        DT2 = FLOAT_SNAN64;
}
FLOAT_OP(cvtl, s)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    DT2 = float32_to_int64(FST0, &env->fpu->fp_status);
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        DT2 = FLOAT_SNAN64;
}

FLOAT_OP(cvtps, pw)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FST2 = int32_to_float32(WT0, &env->fpu->fp_status);
    FSTH2 = int32_to_float32(WTH0, &env->fpu->fp_status);
    update_fcr31();
}
FLOAT_OP(cvtpw, ps)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    WT2 = float32_to_int32(FST0, &env->fpu->fp_status);
    WTH2 = float32_to_int32(FSTH0, &env->fpu->fp_status);
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        WT2 = FLOAT_SNAN32;
}
FLOAT_OP(cvts, d)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FST2 = float64_to_float32(FDT0, &env->fpu->fp_status);
    update_fcr31();
}
FLOAT_OP(cvts, w)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FST2 = int32_to_float32(WT0, &env->fpu->fp_status);
    update_fcr31();
}
FLOAT_OP(cvts, l)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FST2 = int64_to_float32(DT0, &env->fpu->fp_status);
    update_fcr31();
}
FLOAT_OP(cvts, pl)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    WT2 = WT0;
    update_fcr31();
}
FLOAT_OP(cvts, pu)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    WT2 = WTH0;
    update_fcr31();
}
FLOAT_OP(cvtw, s)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    WT2 = float32_to_int32(FST0, &env->fpu->fp_status);
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        WT2 = FLOAT_SNAN32;
}
FLOAT_OP(cvtw, d)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    WT2 = float64_to_int32(FDT0, &env->fpu->fp_status);
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        WT2 = FLOAT_SNAN32;
}

FLOAT_OP(roundl, d)
{
    set_float_rounding_mode(float_round_nearest_even, &env->fpu->fp_status);
    DT2 = float64_to_int64(FDT0, &env->fpu->fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        DT2 = FLOAT_SNAN64;
}
FLOAT_OP(roundl, s)
{
    set_float_rounding_mode(float_round_nearest_even, &env->fpu->fp_status);
    DT2 = float32_to_int64(FST0, &env->fpu->fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        DT2 = FLOAT_SNAN64;
}
FLOAT_OP(roundw, d)
{
    set_float_rounding_mode(float_round_nearest_even, &env->fpu->fp_status);
    WT2 = float64_to_int32(FDT0, &env->fpu->fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        WT2 = FLOAT_SNAN32;
}
FLOAT_OP(roundw, s)
{
    set_float_rounding_mode(float_round_nearest_even, &env->fpu->fp_status);
    WT2 = float32_to_int32(FST0, &env->fpu->fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        WT2 = FLOAT_SNAN32;
}

FLOAT_OP(truncl, d)
{
    DT2 = float64_to_int64_round_to_zero(FDT0, &env->fpu->fp_status);
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        DT2 = FLOAT_SNAN64;
}
FLOAT_OP(truncl, s)
{
    DT2 = float32_to_int64_round_to_zero(FST0, &env->fpu->fp_status);
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        DT2 = FLOAT_SNAN64;
}
FLOAT_OP(truncw, d)
{
    WT2 = float64_to_int32_round_to_zero(FDT0, &env->fpu->fp_status);
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        WT2 = FLOAT_SNAN32;
}
FLOAT_OP(truncw, s)
{
    WT2 = float32_to_int32_round_to_zero(FST0, &env->fpu->fp_status);
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        WT2 = FLOAT_SNAN32;
}

FLOAT_OP(ceill, d)
{
    set_float_rounding_mode(float_round_up, &env->fpu->fp_status);
    DT2 = float64_to_int64(FDT0, &env->fpu->fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        DT2 = FLOAT_SNAN64;
}
FLOAT_OP(ceill, s)
{
    set_float_rounding_mode(float_round_up, &env->fpu->fp_status);
    DT2 = float32_to_int64(FST0, &env->fpu->fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        DT2 = FLOAT_SNAN64;
}
FLOAT_OP(ceilw, d)
{
    set_float_rounding_mode(float_round_up, &env->fpu->fp_status);
    WT2 = float64_to_int32(FDT0, &env->fpu->fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        WT2 = FLOAT_SNAN32;
}
FLOAT_OP(ceilw, s)
{
    set_float_rounding_mode(float_round_up, &env->fpu->fp_status);
    WT2 = float32_to_int32(FST0, &env->fpu->fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        WT2 = FLOAT_SNAN32;
}

FLOAT_OP(floorl, d)
{
    set_float_rounding_mode(float_round_down, &env->fpu->fp_status);
    DT2 = float64_to_int64(FDT0, &env->fpu->fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        DT2 = FLOAT_SNAN64;
}
FLOAT_OP(floorl, s)
{
    set_float_rounding_mode(float_round_down, &env->fpu->fp_status);
    DT2 = float32_to_int64(FST0, &env->fpu->fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        DT2 = FLOAT_SNAN64;
}
FLOAT_OP(floorw, d)
{
    set_float_rounding_mode(float_round_down, &env->fpu->fp_status);
    WT2 = float64_to_int32(FDT0, &env->fpu->fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        WT2 = FLOAT_SNAN32;
}
FLOAT_OP(floorw, s)
{
    set_float_rounding_mode(float_round_down, &env->fpu->fp_status);
    WT2 = float32_to_int32(FST0, &env->fpu->fp_status);
    RESTORE_ROUNDING_MODE;
    update_fcr31();
    if (GET_FP_CAUSE(env->fpu->fcr31) & (FP_OVERFLOW | FP_INVALID))
        WT2 = FLOAT_SNAN32;
}

/* MIPS specific unary operations */
FLOAT_OP(recip, d)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FDT2 = float64_div(FLOAT_ONE64, FDT0, &env->fpu->fp_status);
    update_fcr31();
}
FLOAT_OP(recip, s)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FST2 = float32_div(FLOAT_ONE32, FST0, &env->fpu->fp_status);
    update_fcr31();
}

FLOAT_OP(rsqrt, d)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FDT2 = float64_sqrt(FDT0, &env->fpu->fp_status);
    FDT2 = float64_div(FLOAT_ONE64, FDT2, &env->fpu->fp_status);
    update_fcr31();
}
FLOAT_OP(rsqrt, s)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FST2 = float32_sqrt(FST0, &env->fpu->fp_status);
    FST2 = float32_div(FLOAT_ONE32, FST2, &env->fpu->fp_status);
    update_fcr31();
}

FLOAT_OP(recip1, d)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FDT2 = float64_div(FLOAT_ONE64, FDT0, &env->fpu->fp_status);
    update_fcr31();
}
FLOAT_OP(recip1, s)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FST2 = float32_div(FLOAT_ONE32, FST0, &env->fpu->fp_status);
    update_fcr31();
}
FLOAT_OP(recip1, ps)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FST2 = float32_div(FLOAT_ONE32, FST0, &env->fpu->fp_status);
    FSTH2 = float32_div(FLOAT_ONE32, FSTH0, &env->fpu->fp_status);
    update_fcr31();
}

FLOAT_OP(rsqrt1, d)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FDT2 = float64_sqrt(FDT0, &env->fpu->fp_status);
    FDT2 = float64_div(FLOAT_ONE64, FDT2, &env->fpu->fp_status);
    update_fcr31();
}
FLOAT_OP(rsqrt1, s)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FST2 = float32_sqrt(FST0, &env->fpu->fp_status);
    FST2 = float32_div(FLOAT_ONE32, FST2, &env->fpu->fp_status);
    update_fcr31();
}
FLOAT_OP(rsqrt1, ps)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FST2 = float32_sqrt(FST0, &env->fpu->fp_status);
    FSTH2 = float32_sqrt(FSTH0, &env->fpu->fp_status);
    FST2 = float32_div(FLOAT_ONE32, FST2, &env->fpu->fp_status);
    FSTH2 = float32_div(FLOAT_ONE32, FSTH2, &env->fpu->fp_status);
    update_fcr31();
}

/* binary operations */
#define FLOAT_BINOP(name) \
FLOAT_OP(name, d)         \
{                         \
    set_float_exception_flags(0, &env->fpu->fp_status);            \
    FDT2 = float64_ ## name (FDT0, FDT1, &env->fpu->fp_status);    \
    update_fcr31();                                                \
    if (GET_FP_CAUSE(env->fpu->fcr31) & FP_INVALID)                \
        DT2 = FLOAT_QNAN64;                                        \
}                         \
FLOAT_OP(name, s)         \
{                         \
    set_float_exception_flags(0, &env->fpu->fp_status);            \
    FST2 = float32_ ## name (FST0, FST1, &env->fpu->fp_status);    \
    update_fcr31();                                                \
    if (GET_FP_CAUSE(env->fpu->fcr31) & FP_INVALID)                \
        WT2 = FLOAT_QNAN32;                                        \
}                         \
FLOAT_OP(name, ps)        \
{                         \
    set_float_exception_flags(0, &env->fpu->fp_status);            \
    FST2 = float32_ ## name (FST0, FST1, &env->fpu->fp_status);    \
    FSTH2 = float32_ ## name (FSTH0, FSTH1, &env->fpu->fp_status); \
    update_fcr31();       \
    if (GET_FP_CAUSE(env->fpu->fcr31) & FP_INVALID) {              \
        WT2 = FLOAT_QNAN32;                                        \
        WTH2 = FLOAT_QNAN32;                                       \
    }                     \
}
FLOAT_BINOP(add)
FLOAT_BINOP(sub)
FLOAT_BINOP(mul)
FLOAT_BINOP(div)
#undef FLOAT_BINOP

/* MIPS specific binary operations */
FLOAT_OP(recip2, d)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FDT2 = float64_mul(FDT0, FDT2, &env->fpu->fp_status);
    FDT2 = float64_chs(float64_sub(FDT2, FLOAT_ONE64, &env->fpu->fp_status));
    update_fcr31();
}
FLOAT_OP(recip2, s)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FST2 = float32_mul(FST0, FST2, &env->fpu->fp_status);
    FST2 = float32_chs(float32_sub(FST2, FLOAT_ONE32, &env->fpu->fp_status));
    update_fcr31();
}
FLOAT_OP(recip2, ps)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FST2 = float32_mul(FST0, FST2, &env->fpu->fp_status);
    FSTH2 = float32_mul(FSTH0, FSTH2, &env->fpu->fp_status);
    FST2 = float32_chs(float32_sub(FST2, FLOAT_ONE32, &env->fpu->fp_status));
    FSTH2 = float32_chs(float32_sub(FSTH2, FLOAT_ONE32, &env->fpu->fp_status));
    update_fcr31();
}

FLOAT_OP(rsqrt2, d)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FDT2 = float64_mul(FDT0, FDT2, &env->fpu->fp_status);
    FDT2 = float64_sub(FDT2, FLOAT_ONE64, &env->fpu->fp_status);
    FDT2 = float64_chs(float64_div(FDT2, FLOAT_TWO64, &env->fpu->fp_status));
    update_fcr31();
}
FLOAT_OP(rsqrt2, s)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FST2 = float32_mul(FST0, FST2, &env->fpu->fp_status);
    FST2 = float32_sub(FST2, FLOAT_ONE32, &env->fpu->fp_status);
    FST2 = float32_chs(float32_div(FST2, FLOAT_TWO32, &env->fpu->fp_status));
    update_fcr31();
}
FLOAT_OP(rsqrt2, ps)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FST2 = float32_mul(FST0, FST2, &env->fpu->fp_status);
    FSTH2 = float32_mul(FSTH0, FSTH2, &env->fpu->fp_status);
    FST2 = float32_sub(FST2, FLOAT_ONE32, &env->fpu->fp_status);
    FSTH2 = float32_sub(FSTH2, FLOAT_ONE32, &env->fpu->fp_status);
    FST2 = float32_chs(float32_div(FST2, FLOAT_TWO32, &env->fpu->fp_status));
    FSTH2 = float32_chs(float32_div(FSTH2, FLOAT_TWO32, &env->fpu->fp_status));
    update_fcr31();
}

FLOAT_OP(addr, ps)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FST2 = float32_add (FST0, FSTH0, &env->fpu->fp_status);
    FSTH2 = float32_add (FST1, FSTH1, &env->fpu->fp_status);
    update_fcr31();
}

FLOAT_OP(mulr, ps)
{
    set_float_exception_flags(0, &env->fpu->fp_status);
    FST2 = float32_mul (FST0, FSTH0, &env->fpu->fp_status);
    FSTH2 = float32_mul (FST1, FSTH1, &env->fpu->fp_status);
    update_fcr31();
}

/* compare operations */
#define FOP_COND_D(op, cond)                   \
void do_cmp_d_ ## op (long cc)                 \
{                                              \
    int c = cond;                              \
    update_fcr31();                            \
    if (c)                                     \
        SET_FP_COND(cc, env->fpu);             \
    else                                       \
        CLEAR_FP_COND(cc, env->fpu);           \
}                                              \
void do_cmpabs_d_ ## op (long cc)              \
{                                              \
    int c;                                     \
    FDT0 = float64_chs(FDT0);                  \
    FDT1 = float64_chs(FDT1);                  \
    c = cond;                                  \
    update_fcr31();                            \
    if (c)                                     \
        SET_FP_COND(cc, env->fpu);             \
    else                                       \
        CLEAR_FP_COND(cc, env->fpu);           \
}

int float64_is_unordered(int sig, float64 a, float64 b STATUS_PARAM)
{
    if (float64_is_signaling_nan(a) ||
        float64_is_signaling_nan(b) ||
        (sig && (float64_is_nan(a) || float64_is_nan(b)))) {
        float_raise(float_flag_invalid, status);
        return 1;
    } else if (float64_is_nan(a) || float64_is_nan(b)) {
        return 1;
    } else {
        return 0;
    }
}

/* NOTE: the comma operator will make "cond" to eval to false,
 * but float*_is_unordered() is still called. */
FOP_COND_D(f,   (float64_is_unordered(0, FDT1, FDT0, &env->fpu->fp_status), 0))
FOP_COND_D(un,  float64_is_unordered(0, FDT1, FDT0, &env->fpu->fp_status))
FOP_COND_D(eq,  !float64_is_unordered(0, FDT1, FDT0, &env->fpu->fp_status) && float64_eq(FDT0, FDT1, &env->fpu->fp_status))
FOP_COND_D(ueq, float64_is_unordered(0, FDT1, FDT0, &env->fpu->fp_status)  || float64_eq(FDT0, FDT1, &env->fpu->fp_status))
FOP_COND_D(olt, !float64_is_unordered(0, FDT1, FDT0, &env->fpu->fp_status) && float64_lt(FDT0, FDT1, &env->fpu->fp_status))
FOP_COND_D(ult, float64_is_unordered(0, FDT1, FDT0, &env->fpu->fp_status)  || float64_lt(FDT0, FDT1, &env->fpu->fp_status))
FOP_COND_D(ole, !float64_is_unordered(0, FDT1, FDT0, &env->fpu->fp_status) && float64_le(FDT0, FDT1, &env->fpu->fp_status))
FOP_COND_D(ule, float64_is_unordered(0, FDT1, FDT0, &env->fpu->fp_status)  || float64_le(FDT0, FDT1, &env->fpu->fp_status))
/* NOTE: the comma operator will make "cond" to eval to false,
 * but float*_is_unordered() is still called. */
FOP_COND_D(sf,  (float64_is_unordered(1, FDT1, FDT0, &env->fpu->fp_status), 0))
FOP_COND_D(ngle,float64_is_unordered(1, FDT1, FDT0, &env->fpu->fp_status))
FOP_COND_D(seq, !float64_is_unordered(1, FDT1, FDT0, &env->fpu->fp_status) && float64_eq(FDT0, FDT1, &env->fpu->fp_status))
FOP_COND_D(ngl, float64_is_unordered(1, FDT1, FDT0, &env->fpu->fp_status)  || float64_eq(FDT0, FDT1, &env->fpu->fp_status))
FOP_COND_D(lt,  !float64_is_unordered(1, FDT1, FDT0, &env->fpu->fp_status) && float64_lt(FDT0, FDT1, &env->fpu->fp_status))
FOP_COND_D(nge, float64_is_unordered(1, FDT1, FDT0, &env->fpu->fp_status)  || float64_lt(FDT0, FDT1, &env->fpu->fp_status))
FOP_COND_D(le,  !float64_is_unordered(1, FDT1, FDT0, &env->fpu->fp_status) && float64_le(FDT0, FDT1, &env->fpu->fp_status))
FOP_COND_D(ngt, float64_is_unordered(1, FDT1, FDT0, &env->fpu->fp_status)  || float64_le(FDT0, FDT1, &env->fpu->fp_status))

#define FOP_COND_S(op, cond)                   \
void do_cmp_s_ ## op (long cc)                 \
{                                              \
    int c = cond;                              \
    update_fcr31();                            \
    if (c)                                     \
        SET_FP_COND(cc, env->fpu);             \
    else                                       \
        CLEAR_FP_COND(cc, env->fpu);           \
}                                              \
void do_cmpabs_s_ ## op (long cc)              \
{                                              \
    int c;                                     \
    FST0 = float32_abs(FST0);                  \
    FST1 = float32_abs(FST1);                  \
    c = cond;                                  \
    update_fcr31();                            \
    if (c)                                     \
        SET_FP_COND(cc, env->fpu);             \
    else                                       \
        CLEAR_FP_COND(cc, env->fpu);           \
}

flag float32_is_unordered(int sig, float32 a, float32 b STATUS_PARAM)
{
    if (float32_is_signaling_nan(a) ||
        float32_is_signaling_nan(b) ||
        (sig && (float32_is_nan(a) || float32_is_nan(b)))) {
        float_raise(float_flag_invalid, status);
        return 1;
    } else if (float32_is_nan(a) || float32_is_nan(b)) {
        return 1;
    } else {
        return 0;
    }
}

/* NOTE: the comma operator will make "cond" to eval to false,
 * but float*_is_unordered() is still called. */
FOP_COND_S(f,   (float32_is_unordered(0, FST1, FST0, &env->fpu->fp_status), 0))
FOP_COND_S(un,  float32_is_unordered(0, FST1, FST0, &env->fpu->fp_status))
FOP_COND_S(eq,  !float32_is_unordered(0, FST1, FST0, &env->fpu->fp_status) && float32_eq(FST0, FST1, &env->fpu->fp_status))
FOP_COND_S(ueq, float32_is_unordered(0, FST1, FST0, &env->fpu->fp_status)  || float32_eq(FST0, FST1, &env->fpu->fp_status))
FOP_COND_S(olt, !float32_is_unordered(0, FST1, FST0, &env->fpu->fp_status) && float32_lt(FST0, FST1, &env->fpu->fp_status))
FOP_COND_S(ult, float32_is_unordered(0, FST1, FST0, &env->fpu->fp_status)  || float32_lt(FST0, FST1, &env->fpu->fp_status))
FOP_COND_S(ole, !float32_is_unordered(0, FST1, FST0, &env->fpu->fp_status) && float32_le(FST0, FST1, &env->fpu->fp_status))
FOP_COND_S(ule, float32_is_unordered(0, FST1, FST0, &env->fpu->fp_status)  || float32_le(FST0, FST1, &env->fpu->fp_status))
/* NOTE: the comma operator will make "cond" to eval to false,
 * but float*_is_unordered() is still called. */
FOP_COND_S(sf,  (float32_is_unordered(1, FST1, FST0, &env->fpu->fp_status), 0))
FOP_COND_S(ngle,float32_is_unordered(1, FST1, FST0, &env->fpu->fp_status))
FOP_COND_S(seq, !float32_is_unordered(1, FST1, FST0, &env->fpu->fp_status) && float32_eq(FST0, FST1, &env->fpu->fp_status))
FOP_COND_S(ngl, float32_is_unordered(1, FST1, FST0, &env->fpu->fp_status)  || float32_eq(FST0, FST1, &env->fpu->fp_status))
FOP_COND_S(lt,  !float32_is_unordered(1, FST1, FST0, &env->fpu->fp_status) && float32_lt(FST0, FST1, &env->fpu->fp_status))
FOP_COND_S(nge, float32_is_unordered(1, FST1, FST0, &env->fpu->fp_status)  || float32_lt(FST0, FST1, &env->fpu->fp_status))
FOP_COND_S(le,  !float32_is_unordered(1, FST1, FST0, &env->fpu->fp_status) && float32_le(FST0, FST1, &env->fpu->fp_status))
FOP_COND_S(ngt, float32_is_unordered(1, FST1, FST0, &env->fpu->fp_status)  || float32_le(FST0, FST1, &env->fpu->fp_status))

#define FOP_COND_PS(op, condl, condh)          \
void do_cmp_ps_ ## op (long cc)                \
{                                              \
    int cl = condl;                            \
    int ch = condh;                            \
    update_fcr31();                            \
    if (cl)                                    \
        SET_FP_COND(cc, env->fpu);             \
    else                                       \
        CLEAR_FP_COND(cc, env->fpu);           \
    if (ch)                                    \
        SET_FP_COND(cc + 1, env->fpu);         \
    else                                       \
        CLEAR_FP_COND(cc + 1, env->fpu);       \
}                                              \
void do_cmpabs_ps_ ## op (long cc)             \
{                                              \
    int cl, ch;                                \
    FST0 = float32_abs(FST0);                  \
    FSTH0 = float32_abs(FSTH0);                \
    FST1 = float32_abs(FST1);                  \
    FSTH1 = float32_abs(FSTH1);                \
    cl = condl;                                \
    ch = condh;                                \
    update_fcr31();                            \
    if (cl)                                    \
        SET_FP_COND(cc, env->fpu);             \
    else                                       \
        CLEAR_FP_COND(cc, env->fpu);           \
    if (ch)                                    \
        SET_FP_COND(cc + 1, env->fpu);         \
    else                                       \
        CLEAR_FP_COND(cc + 1, env->fpu);       \
}

/* NOTE: the comma operator will make "cond" to eval to false,
 * but float*_is_unordered() is still called. */
FOP_COND_PS(f,   (float32_is_unordered(0, FST1, FST0, &env->fpu->fp_status), 0),
                 (float32_is_unordered(0, FSTH1, FSTH0, &env->fpu->fp_status), 0))
FOP_COND_PS(un,  float32_is_unordered(0, FST1, FST0, &env->fpu->fp_status),
                 float32_is_unordered(0, FSTH1, FSTH0, &env->fpu->fp_status))
FOP_COND_PS(eq,  !float32_is_unordered(0, FST1, FST0, &env->fpu->fp_status)   && float32_eq(FST0, FST1, &env->fpu->fp_status),
                 !float32_is_unordered(0, FSTH1, FSTH0, &env->fpu->fp_status) && float32_eq(FSTH0, FSTH1, &env->fpu->fp_status))
FOP_COND_PS(ueq, float32_is_unordered(0, FST1, FST0, &env->fpu->fp_status)    || float32_eq(FST0, FST1, &env->fpu->fp_status),
                 float32_is_unordered(0, FSTH1, FSTH0, &env->fpu->fp_status)  || float32_eq(FSTH0, FSTH1, &env->fpu->fp_status))
FOP_COND_PS(olt, !float32_is_unordered(0, FST1, FST0, &env->fpu->fp_status)   && float32_lt(FST0, FST1, &env->fpu->fp_status),
                 !float32_is_unordered(0, FSTH1, FSTH0, &env->fpu->fp_status) && float32_lt(FSTH0, FSTH1, &env->fpu->fp_status))
FOP_COND_PS(ult, float32_is_unordered(0, FST1, FST0, &env->fpu->fp_status)    || float32_lt(FST0, FST1, &env->fpu->fp_status),
                 float32_is_unordered(0, FSTH1, FSTH0, &env->fpu->fp_status)  || float32_lt(FSTH0, FSTH1, &env->fpu->fp_status))
FOP_COND_PS(ole, !float32_is_unordered(0, FST1, FST0, &env->fpu->fp_status)   && float32_le(FST0, FST1, &env->fpu->fp_status),
                 !float32_is_unordered(0, FSTH1, FSTH0, &env->fpu->fp_status) && float32_le(FSTH0, FSTH1, &env->fpu->fp_status))
FOP_COND_PS(ule, float32_is_unordered(0, FST1, FST0, &env->fpu->fp_status)    || float32_le(FST0, FST1, &env->fpu->fp_status),
                 float32_is_unordered(0, FSTH1, FSTH0, &env->fpu->fp_status)  || float32_le(FSTH0, FSTH1, &env->fpu->fp_status))
/* NOTE: the comma operator will make "cond" to eval to false,
 * but float*_is_unordered() is still called. */
FOP_COND_PS(sf,  (float32_is_unordered(1, FST1, FST0, &env->fpu->fp_status), 0),
                 (float32_is_unordered(1, FSTH1, FSTH0, &env->fpu->fp_status), 0))
FOP_COND_PS(ngle,float32_is_unordered(1, FST1, FST0, &env->fpu->fp_status),
                 float32_is_unordered(1, FSTH1, FSTH0, &env->fpu->fp_status))
FOP_COND_PS(seq, !float32_is_unordered(1, FST1, FST0, &env->fpu->fp_status)   && float32_eq(FST0, FST1, &env->fpu->fp_status),
                 !float32_is_unordered(1, FSTH1, FSTH0, &env->fpu->fp_status) && float32_eq(FSTH0, FSTH1, &env->fpu->fp_status))
FOP_COND_PS(ngl, float32_is_unordered(1, FST1, FST0, &env->fpu->fp_status)    || float32_eq(FST0, FST1, &env->fpu->fp_status),
                 float32_is_unordered(1, FSTH1, FSTH0, &env->fpu->fp_status)  || float32_eq(FSTH0, FSTH1, &env->fpu->fp_status))
FOP_COND_PS(lt,  !float32_is_unordered(1, FST1, FST0, &env->fpu->fp_status)   && float32_lt(FST0, FST1, &env->fpu->fp_status),
                 !float32_is_unordered(1, FSTH1, FSTH0, &env->fpu->fp_status) && float32_lt(FSTH0, FSTH1, &env->fpu->fp_status))
FOP_COND_PS(nge, float32_is_unordered(1, FST1, FST0, &env->fpu->fp_status)    || float32_lt(FST0, FST1, &env->fpu->fp_status),
                 float32_is_unordered(1, FSTH1, FSTH0, &env->fpu->fp_status)  || float32_lt(FSTH0, FSTH1, &env->fpu->fp_status))
FOP_COND_PS(le,  !float32_is_unordered(1, FST1, FST0, &env->fpu->fp_status)   && float32_le(FST0, FST1, &env->fpu->fp_status),
                 !float32_is_unordered(1, FSTH1, FSTH0, &env->fpu->fp_status) && float32_le(FSTH0, FSTH1, &env->fpu->fp_status))
FOP_COND_PS(ngt, float32_is_unordered(1, FST1, FST0, &env->fpu->fp_status)    || float32_le(FST0, FST1, &env->fpu->fp_status),
                 float32_is_unordered(1, FSTH1, FSTH0, &env->fpu->fp_status)  || float32_le(FSTH0, FSTH1, &env->fpu->fp_status))
