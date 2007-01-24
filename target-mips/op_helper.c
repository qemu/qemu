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
#include "exec.h"

#define MIPS_DEBUG_DISAS

#define GETPC() (__builtin_return_address(0))

/*****************************************************************************/
/* Exceptions processing helpers */
void cpu_loop_exit(void)
{
    longjmp(env->jmp_env, 1);
}

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

void do_raise_exception_direct (uint32_t exception)
{
    do_restore_state (GETPC ());
    do_raise_exception_err (exception, 0);
}

#define MEMSUFFIX _raw
#include "op_helper_mem.c"
#undef MEMSUFFIX
#if !defined(CONFIG_USER_ONLY)
#define MEMSUFFIX _user
#include "op_helper_mem.c"
#undef MEMSUFFIX
#define MEMSUFFIX _kernel
#include "op_helper_mem.c"
#undef MEMSUFFIX
#endif

#ifdef MIPS_HAS_MIPS64
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
    } else
       T0 = T1;
}

void do_drotr32 (void)
{
    target_ulong tmp;

    if (T1) {
       tmp = T0 << (0x40 - (32 + T1));
       T0 = (T0 >> (32 + T1)) | tmp;
    } else
       T0 = T1;
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
#endif /* TARGET_LONG_BITS > HOST_LONG_BITS */
#endif /* MIPS_HAS_MIPS64 */

/* 64 bits arithmetic for 32 bits hosts */
#if TARGET_LONG_BITS > HOST_LONG_BITS
static inline uint64_t get_HILO (void)
{
    return (env->HI << 32) | (uint32_t)env->LO;
}

static inline void set_HILO (uint64_t HILO)
{
    env->LO = (int32_t)HILO;
    env->HI = (int32_t)(HILO >> 32);
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
#endif

#ifdef MIPS_HAS_MIPS64
void do_dmult (void)
{
    /* XXX */
    set_HILO((int64_t)T0 * (int64_t)T1);
}

void do_dmultu (void)
{
    /* XXX */
    set_HILO((uint64_t)T0 * (uint64_t)T1);
}

void do_ddiv (void)
{
    if (T1 != 0) {
        env->LO = (int64_t)T0 / (int64_t)T1;
        env->HI = (int64_t)T0 % (int64_t)T1;
    }
}

void do_ddivu (void)
{
    if (T1 != 0) {
        env->LO = T0 / T1;
        env->HI = T0 % T1;
    }
}
#endif

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

void do_tlbwi (void)
{
    cpu_abort(env, "tlbwi\n");
}

void do_tlbwr (void)
{
    cpu_abort(env, "tlbwr\n");
}

void do_tlbp (void)
{
    cpu_abort(env, "tlbp\n");
}

void do_tlbr (void)
{
    cpu_abort(env, "tlbr\n");
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
    const uint32_t mask = 0x0000FF00;
    fprintf(logfile, "Status %08x => %08x Cause %08x (%08x %08x %08x)\n",
            old, val, env->CP0_Cause, old & mask, val & mask,
            env->CP0_Cause & mask);
}

void do_mtc0_status_irqraise_debug(void)
{
    fprintf(logfile, "Raise pending IRQs\n");
}

#ifdef MIPS_USES_FPU
#include "softfloat.h"

void fpu_handle_exception(void)
{
#ifdef CONFIG_SOFTFLOAT
    int flags = get_float_exception_flags(&env->fp_status);
    unsigned int cpuflags = 0, enable, cause = 0;

    enable = GET_FP_ENABLE(env->fcr31);

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
    SET_FP_FLAGS(env->fcr31, cpuflags);
    SET_FP_CAUSE(env->fcr31, cause);
#else
    SET_FP_FLAGS(env->fcr31, 0);
    SET_FP_CAUSE(env->fcr31, 0);
#endif
}
#endif /* MIPS_USES_FPU */

/* TLB management */
#if defined(MIPS_USES_R4K_TLB)
void cpu_mips_tlb_flush (CPUState *env, int flush_global)
{
    /* Flush qemu's TLB and discard all shadowed entries.  */
    tlb_flush (env, flush_global);
    env->tlb_in_use = MIPS_TLB_NB;
}

static void mips_tlb_flush_extra (CPUState *env, int first)
{
    /* Discard entries from env->tlb[first] onwards.  */
    while (env->tlb_in_use > first) {
        invalidate_tlb(env, --env->tlb_in_use, 0);
    }
}

static void fill_tlb (int idx)
{
    tlb_t *tlb;

    /* XXX: detect conflicting TLBs and raise a MCHECK exception when needed */
    tlb = &env->tlb[idx];
    tlb->VPN = env->CP0_EntryHi & (int32_t)0xFFFFE000;
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

void do_tlbwi (void)
{
    /* Discard cached TLB entries.  We could avoid doing this if the
       tlbwi is just upgrading access permissions on the current entry;
       that might be a further win.  */
    mips_tlb_flush_extra (env, MIPS_TLB_NB);

    /* Wildly undefined effects for CP0_Index containing a too high value and
       MIPS_TLB_NB not being a power of two.  But so does real silicon.  */
    invalidate_tlb(env, env->CP0_Index & (MIPS_TLB_NB - 1), 0);
    fill_tlb(env->CP0_Index & (MIPS_TLB_NB - 1));
}

void do_tlbwr (void)
{
    int r = cpu_mips_get_random(env);

    invalidate_tlb(env, r, 1);
    fill_tlb(r);
}

void do_tlbp (void)
{
    tlb_t *tlb;
    target_ulong tag;
    uint8_t ASID;
    int i;

    tag = env->CP0_EntryHi & (int32_t)0xFFFFE000;
    ASID = env->CP0_EntryHi & 0xFF;
    for (i = 0; i < MIPS_TLB_NB; i++) {
        tlb = &env->tlb[i];
        /* Check ASID, virtual page number & size */
        if ((tlb->G == 1 || tlb->ASID == ASID) && tlb->VPN == tag) {
            /* TLB match */
            env->CP0_Index = i;
            break;
        }
    }
    if (i == MIPS_TLB_NB) {
        /* No match.  Discard any shadow entries, if any of them match.  */
        for (i = MIPS_TLB_NB; i < env->tlb_in_use; i++) {
	    tlb = &env->tlb[i];

	    /* Check ASID, virtual page number & size */
	    if ((tlb->G == 1 || tlb->ASID == ASID) && tlb->VPN == tag) {
                mips_tlb_flush_extra (env, i);
	        break;
	    }
	}

        env->CP0_Index |= 0x80000000;
    }
}

void do_tlbr (void)
{
    tlb_t *tlb;
    uint8_t ASID;

    ASID = env->CP0_EntryHi & 0xFF;
    tlb = &env->tlb[env->CP0_Index & (MIPS_TLB_NB - 1)];

    /* If this will change the current ASID, flush qemu's TLB.  */
    if (ASID != tlb->ASID)
        cpu_mips_tlb_flush (env, 1);

    mips_tlb_flush_extra(env, MIPS_TLB_NB);

    env->CP0_EntryHi = tlb->VPN | tlb->ASID;
    env->CP0_PageMask = tlb->PageMask;
    env->CP0_EntryLo0 = tlb->G | (tlb->V0 << 1) | (tlb->D0 << 2) |
                        (tlb->C0 << 3) | (tlb->PFN[0] >> 6);
    env->CP0_EntryLo1 = tlb->G | (tlb->V1 << 1) | (tlb->D1 << 2) |
                        (tlb->C1 << 3) | (tlb->PFN[1] >> 6);
}
#endif

#endif /* !CONFIG_USER_ONLY */

void dump_ldst (const unsigned char *func)
{
    if (loglevel)
        fprintf(logfile, "%s => " TLSZ " " TLSZ "\n", __func__, T0, T1);
}

void dump_sc (void)
{
    if (loglevel) {
        fprintf(logfile, "%s " TLSZ " at " TLSZ " (" TLSZ ")\n", __func__,
                T1, T0, env->CP0_LLAddr);
    }
}

void debug_eret (void)
{
    if (loglevel) {
        fprintf(logfile, "ERET: pc " TLSZ " EPC " TLSZ " ErrorEPC " TLSZ " (%d)\n",
                env->PC, env->CP0_EPC, env->CP0_ErrorEPC,
                env->hflags & MIPS_HFLAG_ERL ? 1 : 0);
    }
}

void do_pmon (int function)
{
    function /= 2;
    switch (function) {
    case 2: /* TODO: char inbyte(int waitflag); */
        if (env->gpr[4] == 0)
            env->gpr[2] = -1;
        /* Fall through */
    case 11: /* TODO: char inbyte (void); */
        env->gpr[2] = -1;
        break;
    case 3:
    case 12:
        printf("%c", (char)(env->gpr[4] & 0xFF));
        break;
    case 17:
        break;
    case 158:
        {
            unsigned char *fmt = (void *)(unsigned long)env->gpr[4];
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

void tlb_fill (target_ulong addr, int is_write, int is_user, void *retaddr)
{
    TranslationBlock *tb;
    CPUState *saved_env;
    unsigned long pc;
    int ret;

    /* XXX: hack to restore env in all cases, even if not called from
       generated code */
    saved_env = env;
    env = cpu_single_env;
    ret = cpu_mips_handle_mmu_fault(env, addr, is_write, is_user, 1);
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

#endif
