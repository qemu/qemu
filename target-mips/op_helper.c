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

/* 64 bits arithmetic for 32 bits hosts */
#if (HOST_LONG_BITS == 32)
static inline uint64_t get_HILO (void)
{
    return ((uint64_t)env->HI << 32) | (uint64_t)env->LO;
}

static inline void set_HILO (uint64_t HILO)
{
    env->LO = HILO & 0xFFFFFFFF;
    env->HI = HILO >> 32;
}

void do_mult (void)
{
    set_HILO((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1);
}

void do_multu (void)
{
    set_HILO((uint64_t)T0 * (uint64_t)T1);
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

    tmp = ((uint64_t)T0 * (uint64_t)T1);
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

    tmp = ((uint64_t)T0 * (uint64_t)T1);
    set_HILO(get_HILO() - tmp);
}
#endif

#if defined(CONFIG_USER_ONLY) 
void do_mfc0 (int reg, int sel)
{
    cpu_abort(env, "mfc0 reg=%d sel=%d\n", reg, sel);
}
void do_mtc0 (int reg, int sel)
{
    cpu_abort(env, "mtc0 reg=%d sel=%d\n", reg, sel);
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
#else

/* CP0 helpers */
void do_mfc0 (int reg, int sel)
{
    const unsigned char *rn;

    if (sel != 0 && reg != 16 && reg != 28) {
        rn = "invalid";
        goto print;
    }
    switch (reg) {
    case 0:
        T0 = env->CP0_index;
        rn = "Index";
        break;
    case 1:
        T0 = cpu_mips_get_random(env);
        rn = "Random";
        break;
    case 2:
        T0 = env->CP0_EntryLo0;
        rn = "EntryLo0";
        break;
    case 3:
        T0 = env->CP0_EntryLo1;
        rn = "EntryLo1";
        break;
    case 4:
        T0 = env->CP0_Context;
        rn = "Context";
        break;
    case 5:
        T0 = env->CP0_PageMask;
        rn = "PageMask";
        break;
    case 6:
        T0 = env->CP0_Wired;
        rn = "Wired";
        break;
    case 8:
        T0 = env->CP0_BadVAddr;
        rn = "BadVaddr";
        break;
    case 9:
        T0 = cpu_mips_get_count(env);
        rn = "Count";
        break;
    case 10:
        T0 = env->CP0_EntryHi;
        rn = "EntryHi";
        break;
    case 11:
        T0 = env->CP0_Compare;
        rn = "Compare";
        break;
    case 12:
        T0 = env->CP0_Status;
        if (env->hflags & MIPS_HFLAG_UM)
            T0 |= (1 << CP0St_UM);
        rn = "Status";
        break;
    case 13:
        T0 = env->CP0_Cause;
        rn = "Cause";
        break;
    case 14:
        T0 = env->CP0_EPC;
        rn = "EPC";
        break;
    case 15:
        T0 = env->CP0_PRid;
        rn = "PRid";
        break;
    case 16:
        switch (sel) {
        case 0:
            T0 = env->CP0_Config0;
            rn = "Config";
            break;
        case 1:
            T0 = env->CP0_Config1;
            rn = "Config1";
            break;
        default:
            rn = "Unknown config register";
            break;
        }
        break;
    case 17:
        T0 = env->CP0_LLAddr >> 4;
        rn = "LLAddr";
        break;
    case 18:
        T0 = env->CP0_WatchLo;
        rn = "WatchLo";
        break;
    case 19:
        T0 = env->CP0_WatchHi;
        rn = "WatchHi";
        break;
    case 23:
        T0 = env->CP0_Debug;
        if (env->hflags & MIPS_HFLAG_DM)
            T0 |= 1 << CP0DB_DM;
        rn = "Debug";
        break;
    case 24:
        T0 = env->CP0_DEPC;
        rn = "DEPC";
        break;
    case 28:
        switch (sel) {
        case 0:
            T0 = env->CP0_TagLo;
            rn = "TagLo";
            break;
        case 1:
            T0 = env->CP0_DataLo;
            rn = "DataLo";
            break;
        default:
            rn = "unknown sel";
            break;
        }
        break;
    case 30:
        T0 = env->CP0_ErrorEPC;
        rn = "ErrorEPC";
        break;
    case 31:
        T0 = env->CP0_DESAVE;
        rn = "DESAVE";
        break;
    default:
        rn = "unknown";
        break;
    }
 print:
#if defined MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "%08x mfc0 %s => %08x (%d %d)\n",
                env->PC, rn, T0, reg, sel);
    }
#endif
    return;
}

void do_mtc0 (int reg, int sel)
{
    const unsigned char *rn;
    uint32_t val, old, mask;

    if (sel != 0 && reg != 16 && reg != 28) {
        val = -1;
        old = -1;
        rn = "invalid";
        goto print;
    }
    switch (reg) {
    case 0:
        val = (env->CP0_index & 0x80000000) | (T0 & 0x0000000F);
        old = env->CP0_index;
        env->CP0_index = val;
        rn = "Index";
        break;
    case 2:
        val = T0 & 0x3FFFFFFF;
        old = env->CP0_EntryLo0;
        env->CP0_EntryLo0 = val;
        rn = "EntryLo0";
        break;
    case 3:
        val = T0 & 0x3FFFFFFF;
        old = env->CP0_EntryLo1;
        env->CP0_EntryLo1 = val;
        rn = "EntryLo1";
        break;
    case 4:
        val = (env->CP0_Context & 0xFF800000) | (T0 & 0x007FFFF0);
        old = env->CP0_Context;
        env->CP0_Context = val;
        rn = "Context";
        break;
    case 5:
        val = T0 & 0x01FFE000;
        old = env->CP0_PageMask;
        env->CP0_PageMask = val;
        rn = "PageMask";
        break;
    case 6:
        val = T0 & 0x0000000F;
        old = env->CP0_Wired;
        env->CP0_Wired = val;
        rn = "Wired";
        break;
    case 9:
        val = T0;
        old = cpu_mips_get_count(env);
        cpu_mips_store_count(env, val);
        rn = "Count";
        break;
    case 10:
        val = T0 & 0xFFFFE0FF;
        old = env->CP0_EntryHi;
        env->CP0_EntryHi = val;
	/* If the ASID changes, flush qemu's TLB.  */
	if ((old & 0xFF) != (val & 0xFF))
	  tlb_flush (env, 1);
        rn = "EntryHi";
        break;
    case 11:
        val = T0;
        old = env->CP0_Compare;
        cpu_mips_store_compare(env, val);
        rn = "Compare";
        break;
    case 12:
        val = T0 & 0xFA78FF01;
        if (T0 & (1 << CP0St_UM))
            env->hflags |= MIPS_HFLAG_UM;
        else
            env->hflags &= ~MIPS_HFLAG_UM;
        if (T0 & (1 << CP0St_ERL))
            env->hflags |= MIPS_HFLAG_ERL;
        else
            env->hflags &= ~MIPS_HFLAG_ERL;
        if (T0 & (1 << CP0St_EXL))
            env->hflags |= MIPS_HFLAG_EXL;
        else
            env->hflags &= ~MIPS_HFLAG_EXL;
        old = env->CP0_Status;
        env->CP0_Status = val;
        /* If we unmasked an asserted IRQ, raise it */
        mask = 0x0000FF00;
        if (loglevel & CPU_LOG_TB_IN_ASM) {
            fprintf(logfile, "Status %08x => %08x Cause %08x (%08x %08x %08x)\n",
                    old, val, env->CP0_Cause, old & mask, val & mask,
                    env->CP0_Cause & mask);
        }
        if ((val & (1 << CP0St_IE)) && !(old & (1 << CP0St_IE)) &&
            !(env->hflags & MIPS_HFLAG_EXL) &&
            !(env->hflags & MIPS_HFLAG_ERL) &&
            !(env->hflags & MIPS_HFLAG_DM) &&
            (env->CP0_Status & env->CP0_Cause & mask)) {
            if (logfile)
                fprintf(logfile, "Raise pending IRQs\n");
            env->interrupt_request |= CPU_INTERRUPT_HARD;
        } else if (!(val & (1 << CP0St_IE)) && (old & (1 << CP0St_IE))) {
            env->interrupt_request &= ~CPU_INTERRUPT_HARD;
        }
        rn = "Status";
        break;
    case 13:
        val = (env->CP0_Cause & 0xB000F87C) | (T0 & 0x000C00300);
        old = env->CP0_Cause;
        env->CP0_Cause = val;
#if 0
        {
            int i;
            /* Check if we ever asserted a software IRQ */
            for (i = 0; i < 2; i++) {
                mask = 0x100 << i;
                if ((val & mask) & !(old & mask))
                    mips_set_irq(i);
            }
        }
#endif
        rn = "Cause";
        break;
    case 14:
        val = T0;
        old = env->CP0_EPC;
        env->CP0_EPC = val;
        rn = "EPC";
        break;
    case 16:
        switch (sel) {
        case 0:
#if defined(MIPS_USES_R4K_TLB)
            val = (env->CP0_Config0 & 0x8017FF80) | (T0 & 0x7E000001);
#else
            val = (env->CP0_Config0 & 0xFE17FF80) | (T0 & 0x00000001);
#endif
            old = env->CP0_Config0;
            env->CP0_Config0 = val;
            rn = "Config0";
            break;
        default:
            val = -1;
            old = -1;
            rn = "bad config selector";
            break;
        }
        break;
    case 18:
        val = T0;
        old = env->CP0_WatchLo;
        env->CP0_WatchLo = val;
        rn = "WatchLo";
        break;
    case 19:
        val = T0 & 0x40FF0FF8;
        old = env->CP0_WatchHi;
        env->CP0_WatchHi = val;
        rn = "WatchHi";
        break;
    case 23:
        val = (env->CP0_Debug & 0x8C03FC1F) | (T0 & 0x13300120);
        if (T0 & (1 << CP0DB_DM))
            env->hflags |= MIPS_HFLAG_DM;
        else
            env->hflags &= ~MIPS_HFLAG_DM;
        old = env->CP0_Debug;
        env->CP0_Debug = val;
        rn = "Debug";
        break;
    case 24:
        val = T0;
        old = env->CP0_DEPC;
        env->CP0_DEPC = val;
        rn = "DEPC";
        break;
    case 28:
        switch (sel) {
        case 0:
            val = T0 & 0xFFFFFCF6;
            old = env->CP0_TagLo;
            env->CP0_TagLo = val;
            rn = "TagLo";
            break;
        default:
            val = -1;
            old = -1;
            rn = "invalid sel";
            break;
        }
        break;
    case 30:
        val = T0;
        old = env->CP0_ErrorEPC;
        env->CP0_ErrorEPC = val;
        rn = "EPC";
        break;
    case 31:
        val = T0;
        old = env->CP0_DESAVE;
        env->CP0_DESAVE = val;
        rn = "DESAVE";
        break;
    default:
        val = -1;
        old = -1;
        rn = "unknown";
        break;
    }
 print:
#if defined MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "%08x mtc0 %s %08x => %08x (%d %d %08x)\n",
                env->PC, rn, T0, val, reg, sel, old);
    }
#endif
    return;
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
static void invalidate_tlb (int idx)
{
    tlb_t *tlb;
    target_ulong addr;

    tlb = &env->tlb[idx];
    if (tlb->V0) {
        tb_invalidate_page_range(tlb->PFN[0], tlb->end - tlb->VPN);
        addr = tlb->VPN;
        while (addr < tlb->end) {
            tlb_flush_page (env, addr);
            addr += TARGET_PAGE_SIZE;
        }
    }
    if (tlb->V1) {
        tb_invalidate_page_range(tlb->PFN[1], tlb->end2 - tlb->end);
        addr = tlb->end;
        while (addr < tlb->end2) {
            tlb_flush_page (env, addr);
            addr += TARGET_PAGE_SIZE;
        }
    }
}

static void fill_tlb (int idx)
{
    tlb_t *tlb;
    int size;

    /* XXX: detect conflicting TLBs and raise a MCHECK exception when needed */
    tlb = &env->tlb[idx];
    tlb->VPN = env->CP0_EntryHi & 0xFFFFE000;
    tlb->ASID = env->CP0_EntryHi & 0xFF;
    size = env->CP0_PageMask >> 13;
    size = 4 * (size + 1);
    tlb->end = tlb->VPN + (1 << (8 + size));
    tlb->end2 = tlb->end + (1 << (8 + size));
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
    /* Wildly undefined effects for CP0_index containing a too high value and
       MIPS_TLB_NB not being a power of two.  But so does real silicon.  */
    invalidate_tlb(env->CP0_index & (MIPS_TLB_NB - 1));
    fill_tlb(env->CP0_index & (MIPS_TLB_NB - 1));
}

void do_tlbwr (void)
{
    int r = cpu_mips_get_random(env);

    invalidate_tlb(r);
    fill_tlb(r);
}

void do_tlbp (void)
{
    tlb_t *tlb;
    target_ulong tag;
    uint8_t ASID;
    int i;

    tag = env->CP0_EntryHi & 0xFFFFE000;
    ASID = env->CP0_EntryHi & 0xFF;
    for (i = 0; i < MIPS_TLB_NB; i++) {
        tlb = &env->tlb[i];
        /* Check ASID, virtual page number & size */
        if ((tlb->G == 1 || tlb->ASID == ASID) && tlb->VPN == tag) {
            /* TLB match */
            env->CP0_index = i;
            break;
        }
    }
    if (i == MIPS_TLB_NB) {
        env->CP0_index |= 0x80000000;
    }
}

void do_tlbr (void)
{
    tlb_t *tlb;
    uint8_t ASID;
    int size;

    ASID = env->CP0_EntryHi & 0xFF;
    tlb = &env->tlb[env->CP0_index & (MIPS_TLB_NB - 1)];

    /* If this will change the current ASID, flush qemu's TLB.  */
    if (ASID != tlb->ASID && tlb->G != 1)
      tlb_flush (env, 1);

    env->CP0_EntryHi = tlb->VPN | tlb->ASID;
    size = (tlb->end - tlb->VPN) >> 12;
    env->CP0_PageMask = (size - 1) << 13;
    env->CP0_EntryLo0 = tlb->G | (tlb->V0 << 1) | (tlb->D0 << 2)
		| (tlb->C0 << 3) | (tlb->PFN[0] >> 6);
    env->CP0_EntryLo1 = tlb->G | (tlb->V1 << 1) | (tlb->D1 << 2)
		| (tlb->C1 << 3) | (tlb->PFN[1] >> 6);
}
#endif

#endif /* !CONFIG_USER_ONLY */

void op_dump_ldst (const unsigned char *func)
{
    if (loglevel)
        fprintf(logfile, "%s => %08x %08x\n", __func__, T0, T1);
}

void dump_sc (void)
{
    if (loglevel) {
        fprintf(logfile, "%s %08x at %08x (%08x)\n", __func__,
                T1, T0, env->CP0_LLAddr);
    }
}

void debug_eret (void)
{
    if (loglevel) {
        fprintf(logfile, "ERET: pc %08x EPC %08x ErrorEPC %08x (%d)\n",
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
        printf("%c", env->gpr[4] & 0xFF);
        break;
    case 17:
        break;
    case 158:
        {
            unsigned char *fmt = (void *)env->gpr[4];
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
