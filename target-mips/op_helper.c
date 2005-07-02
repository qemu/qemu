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
#include <math.h>
#include "exec.h"

#define MIPS_DEBUG_DISAS

/*****************************************************************************/
/* Exceptions processing helpers */
void cpu_loop_exit(void)
{
    longjmp(env->jmp_env, 1);
}

__attribute__ (( regparm(2) ))
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

__attribute__ (( regparm(1) ))
void do_raise_exception (uint32_t exception)
{
    do_raise_exception_err(exception, 0);
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
    set_HILO((int64_t)T0 * (int64_t)T1);
}

void do_multu (void)
{
    set_HILO((uint64_t)T0 * (uint64_t)T1);
}

void do_madd (void)
{
    int64_t tmp;

    tmp = ((int64_t)T0 * (int64_t)T1);
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

    tmp = ((int64_t)T0 * (int64_t)T1);
    set_HILO((int64_t)get_HILO() - tmp);
}

void do_msubu (void)
{
    uint64_t tmp;

    tmp = ((uint64_t)T0 * (uint64_t)T1);
    set_HILO(get_HILO() - tmp);
}
#endif

/* CP0 helpers */
__attribute__ (( regparm(2) ))
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
        if (env->hflags & MIPS_HFLAG_ERL)
            T0 |= (1 << CP0St_ERL);
        if (env->hflags & MIPS_HFLAG_EXL)
            T0 |= (1 << CP0St_EXL);
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

__attribute__ (( regparm(2) ))
void do_mtc0 (int reg, int sel)
{
    const unsigned char *rn;
    uint32_t val, old, mask;
    int i, raise;

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
        val = T0 & 0x03FFFFFFF;
        old = env->CP0_EntryLo0;
        env->CP0_EntryLo0 = val;
        rn = "EntryLo0";
        break;
    case 3:
        val = T0 & 0x03FFFFFFF;
        old = env->CP0_EntryLo1;
        env->CP0_EntryLo1 = val;
        rn = "EntryLo1";
        break;
    case 4:
        val = (env->CP0_Context & 0xFF000000) | (T0 & 0x00FFFFF0);
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
        val = T0 & 0xFFFFF0FF;
        old = env->CP0_EntryHi;
        env->CP0_EntryHi = val;
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
#if 1
        if ((val & (1 << CP0St_IE)) && !(old & (1 << CP0St_IE)) &&
            !(env->hflags & MIPS_HFLAG_EXL) &&
            !(env->hflags & MIPS_HFLAG_ERL) &&
            !(env->hflags & MIPS_HFLAG_DM) && 
            (env->CP0_Status & env->CP0_Cause & mask)) {
            if (logfile)
                fprintf(logfile, "Raise pending IRQs\n");
            env->interrupt_request |= CPU_INTERRUPT_HARD;
            do_raise_exception(EXCP_EXT_INTERRUPT);
        } else if (!(val & 0x00000001) && (old & 0x00000001)) {
            env->interrupt_request &= ~CPU_INTERRUPT_HARD;
        }
#endif
        rn = "Status";
        break;
    case 13:
        val = (env->CP0_Cause & 0xB000F87C) | (T0 & 0x000C00300);
        old = env->CP0_Cause;
        env->CP0_Cause = val;
#if 0
        /* Check if we ever asserted a software IRQ */
        for (i = 0; i < 2; i++) {
            mask = 0x100 << i;
            if ((val & mask) & !(old & mask))
                mips_set_irq(i);
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

/* TLB management */
#if defined(MIPS_USES_R4K_TLB)
__attribute__ (( regparm(1) ))
static void invalidate_tb (int idx)
{
    tlb_t *tlb;
    target_ulong addr, end;

    tlb = &env->tlb[idx];
    if (tlb->V[0]) {
        addr = tlb->PFN[0];
        end = addr + (tlb->end - tlb->VPN);
        tb_invalidate_page_range(addr, end);
    }
    if (tlb->V[1]) {
        addr = tlb->PFN[1];
        end = addr + (tlb->end - tlb->VPN);
        tb_invalidate_page_range(addr, end);
    }
}

__attribute__ (( regparm(1) ))
static void fill_tb (int idx)
{
    tlb_t *tlb;
    int size;

    /* XXX: detect conflicting TLBs and raise a MCHECK exception when needed */
    tlb = &env->tlb[idx];
    tlb->VPN = env->CP0_EntryHi & 0xFFFFE000;
    tlb->ASID = env->CP0_EntryHi & 0x000000FF;
    size = env->CP0_PageMask >> 13;
    size = 4 * (size + 1);
    tlb->end = tlb->VPN + (1 << (8 + size));
    tlb->G = env->CP0_EntryLo0 & env->CP0_EntryLo1 & 1;
    tlb->V[0] = env->CP0_EntryLo0 & 2;
    tlb->D[0] = env->CP0_EntryLo0 & 4;
    tlb->C[0] = (env->CP0_EntryLo0 >> 3) & 0x7;
    tlb->PFN[0] = (env->CP0_EntryLo0 >> 6) << 12;
    tlb->V[1] = env->CP0_EntryLo1 & 2;
    tlb->D[1] = env->CP0_EntryLo1 & 4;
    tlb->C[1] = (env->CP0_EntryLo1 >> 3) & 0x7;
    tlb->PFN[1] = (env->CP0_EntryLo1 >> 6) << 12;
}

void do_tlbwi (void)
{
    invalidate_tb(env->CP0_index & 0xF);
    fill_tb(env->CP0_index & 0xF);
}

void do_tlbwr (void)
{
    int r = cpu_mips_get_random(env);

    invalidate_tb(r);
    fill_tb(r);
}

void do_tlbp (void)
{
    tlb_t *tlb;
    target_ulong tag;
    uint8_t ASID;
    int i;

    tag = (env->CP0_EntryHi & 0xFFFFE000);
    ASID = env->CP0_EntryHi & 0x000000FF;
        for (i = 0; i < 16; i++) {
        tlb = &env->tlb[i];
        /* Check ASID, virtual page number & size */
        if ((tlb->G == 1 || tlb->ASID == ASID) && tlb->VPN == tag) {
            /* TLB match */
            env->CP0_index = i;
            break;
        }
    }
    if (i == 16) {
        env->CP0_index |= 0x80000000;
    }
}

void do_tlbr (void)
{
    tlb_t *tlb;
    int size;

    tlb = &env->tlb[env->CP0_index & 0xF];
    env->CP0_EntryHi = tlb->VPN | tlb->ASID;
    size = (tlb->end - tlb->VPN) >> 12;
    env->CP0_PageMask = (size - 1) << 13;
    env->CP0_EntryLo0 = tlb->V[0] | tlb->D[0] | (tlb->C[0] << 3) |
        (tlb->PFN[0] >> 6);
    env->CP0_EntryLo1 = tlb->V[1] | tlb->D[1] | (tlb->C[1] << 3) |
        (tlb->PFN[1] >> 6);
}
#endif

__attribute__ (( regparm(1) ))
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

__attribute__ (( regparm(1) ))
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
