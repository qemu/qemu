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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 */
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <signal.h>
#include <assert.h>

#include "cpu.h"
#include "exec-all.h"

enum {
    TLBRET_DIRTY = -4,
    TLBRET_INVALID = -3,
    TLBRET_NOMATCH = -2,
    TLBRET_BADADDR = -1,
    TLBRET_MATCH = 0
};

/* no MMU emulation */
int no_mmu_map_address (CPUState *env, target_ulong *physical, int *prot,
                        target_ulong address, int rw, int access_type)
{
    *physical = address;
    *prot = PAGE_READ | PAGE_WRITE;
    return TLBRET_MATCH;
}

/* fixed mapping MMU emulation */
int fixed_mmu_map_address (CPUState *env, target_ulong *physical, int *prot,
                           target_ulong address, int rw, int access_type)
{
    if (address <= (int32_t)0x7FFFFFFFUL) {
        if (!(env->CP0_Status & (1 << CP0St_ERL)))
            *physical = address + 0x40000000UL;
        else
            *physical = address;
    } else if (address <= (int32_t)0xBFFFFFFFUL)
        *physical = address & 0x1FFFFFFF;
    else
        *physical = address;

    *prot = PAGE_READ | PAGE_WRITE;
    return TLBRET_MATCH;
}

/* MIPS32/MIPS64 R4000-style MMU emulation */
int r4k_map_address (CPUState *env, target_ulong *physical, int *prot,
                     target_ulong address, int rw, int access_type)
{
    uint8_t ASID = env->CP0_EntryHi & 0xFF;
    int i;

    for (i = 0; i < env->tlb->tlb_in_use; i++) {
        r4k_tlb_t *tlb = &env->tlb->mmu.r4k.tlb[i];
        /* 1k pages are not supported. */
        target_ulong mask = tlb->PageMask | ~(TARGET_PAGE_MASK << 1);
        target_ulong tag = address & ~mask;
        target_ulong VPN = tlb->VPN & ~mask;
#if defined(TARGET_MIPS64)
        tag &= env->SEGMask;
#endif

        /* Check ASID, virtual page number & size */
        if ((tlb->G == 1 || tlb->ASID == ASID) && VPN == tag) {
            /* TLB match */
            int n = !!(address & mask & ~(mask >> 1));
            /* Check access rights */
            if (!(n ? tlb->V1 : tlb->V0))
                return TLBRET_INVALID;
            if (rw == 0 || (n ? tlb->D1 : tlb->D0)) {
                *physical = tlb->PFN[n] | (address & (mask >> 1));
                *prot = PAGE_READ;
                if (n ? tlb->D1 : tlb->D0)
                    *prot |= PAGE_WRITE;
                return TLBRET_MATCH;
            }
            return TLBRET_DIRTY;
        }
    }
    return TLBRET_NOMATCH;
}

static int get_physical_address (CPUState *env, target_ulong *physical,
                                int *prot, target_ulong address,
                                int rw, int access_type)
{
    /* User mode can only access useg/xuseg */
    int user_mode = (env->hflags & MIPS_HFLAG_MODE) == MIPS_HFLAG_UM;
    int supervisor_mode = (env->hflags & MIPS_HFLAG_MODE) == MIPS_HFLAG_SM;
    int kernel_mode = !user_mode && !supervisor_mode;
#if defined(TARGET_MIPS64)
    int UX = (env->CP0_Status & (1 << CP0St_UX)) != 0;
    int SX = (env->CP0_Status & (1 << CP0St_SX)) != 0;
    int KX = (env->CP0_Status & (1 << CP0St_KX)) != 0;
#endif
    int ret = TLBRET_MATCH;

#if 0
    if (logfile) {
        fprintf(logfile, "user mode %d h %08x\n",
                user_mode, env->hflags);
    }
#endif

    if (address <= (int32_t)0x7FFFFFFFUL) {
        /* useg */
        if (env->CP0_Status & (1 << CP0St_ERL)) {
            *physical = address & 0xFFFFFFFF;
            *prot = PAGE_READ | PAGE_WRITE;
        } else {
            ret = env->tlb->map_address(env, physical, prot, address, rw, access_type);
        }
#if defined(TARGET_MIPS64)
    } else if (address < 0x4000000000000000ULL) {
        /* xuseg */
	if (UX && address <= (0x3FFFFFFFFFFFFFFFULL & env->SEGMask)) {
            ret = env->tlb->map_address(env, physical, prot, address, rw, access_type);
	} else {
	    ret = TLBRET_BADADDR;
        }
    } else if (address < 0x8000000000000000ULL) {
        /* xsseg */
	if ((supervisor_mode || kernel_mode) &&
	    SX && address <= (0x7FFFFFFFFFFFFFFFULL & env->SEGMask)) {
            ret = env->tlb->map_address(env, physical, prot, address, rw, access_type);
	} else {
	    ret = TLBRET_BADADDR;
        }
    } else if (address < 0xC000000000000000ULL) {
        /* xkphys */
        if (kernel_mode && KX &&
            (address & 0x07FFFFFFFFFFFFFFULL) <= env->PAMask) {
            *physical = address & env->PAMask;
            *prot = PAGE_READ | PAGE_WRITE;
	} else {
	    ret = TLBRET_BADADDR;
	}
    } else if (address < 0xFFFFFFFF80000000ULL) {
        /* xkseg */
	if (kernel_mode && KX &&
	    address <= (0xFFFFFFFF7FFFFFFFULL & env->SEGMask)) {
            ret = env->tlb->map_address(env, physical, prot, address, rw, access_type);
	} else {
	    ret = TLBRET_BADADDR;
	}
#endif
    } else if (address < (int32_t)0xA0000000UL) {
        /* kseg0 */
        if (kernel_mode) {
            *physical = address - (int32_t)0x80000000UL;
            *prot = PAGE_READ | PAGE_WRITE;
        } else {
            ret = TLBRET_BADADDR;
        }
    } else if (address < (int32_t)0xC0000000UL) {
        /* kseg1 */
        if (kernel_mode) {
            *physical = address - (int32_t)0xA0000000UL;
            *prot = PAGE_READ | PAGE_WRITE;
        } else {
            ret = TLBRET_BADADDR;
        }
    } else if (address < (int32_t)0xE0000000UL) {
        /* sseg (kseg2) */
        if (supervisor_mode || kernel_mode) {
            ret = env->tlb->map_address(env, physical, prot, address, rw, access_type);
        } else {
            ret = TLBRET_BADADDR;
        }
    } else {
        /* kseg3 */
        /* XXX: debug segment is not emulated */
        if (kernel_mode) {
            ret = env->tlb->map_address(env, physical, prot, address, rw, access_type);
        } else {
            ret = TLBRET_BADADDR;
        }
    }
#if 0
    if (logfile) {
        fprintf(logfile, TARGET_FMT_lx " %d %d => " TARGET_FMT_lx " %d (%d)\n",
		address, rw, access_type, *physical, *prot, ret);
    }
#endif

    return ret;
}

target_phys_addr_t cpu_get_phys_page_debug(CPUState *env, target_ulong addr)
{
    if (env->user_mode_only)
        return addr;
    else {
        target_ulong phys_addr;
        int prot;

        if (get_physical_address(env, &phys_addr, &prot, addr, 0, ACCESS_INT) != 0)
            return -1;
        return phys_addr;
    }
}

int cpu_mips_handle_mmu_fault (CPUState *env, target_ulong address, int rw,
                               int mmu_idx, int is_softmmu)
{
    target_ulong physical;
    int prot;
    int exception = 0, error_code = 0;
    int access_type;
    int ret = 0;

    if (logfile) {
#if 0
        cpu_dump_state(env, logfile, fprintf, 0);
#endif
        fprintf(logfile, "%s pc " TARGET_FMT_lx " ad " TARGET_FMT_lx " rw %d mmu_idx %d smmu %d\n",
                __func__, env->active_tc.PC, address, rw, mmu_idx, is_softmmu);
    }

    rw &= 1;

    /* data access */
    /* XXX: put correct access by using cpu_restore_state()
       correctly */
    access_type = ACCESS_INT;
    if (env->user_mode_only) {
        /* user mode only emulation */
        ret = TLBRET_NOMATCH;
        goto do_fault;
    }
    ret = get_physical_address(env, &physical, &prot,
                               address, rw, access_type);
    if (logfile) {
        fprintf(logfile, "%s address=" TARGET_FMT_lx " ret %d physical " TARGET_FMT_lx " prot %d\n",
                __func__, address, ret, physical, prot);
    }
    if (ret == TLBRET_MATCH) {
       ret = tlb_set_page(env, address & TARGET_PAGE_MASK,
                          physical & TARGET_PAGE_MASK, prot,
                          mmu_idx, is_softmmu);
    } else if (ret < 0) {
    do_fault:
        switch (ret) {
        default:
        case TLBRET_BADADDR:
            /* Reference to kernel address from user mode or supervisor mode */
            /* Reference to supervisor address from user mode */
            if (rw)
                exception = EXCP_AdES;
            else
                exception = EXCP_AdEL;
            break;
        case TLBRET_NOMATCH:
            /* No TLB match for a mapped address */
            if (rw)
                exception = EXCP_TLBS;
            else
                exception = EXCP_TLBL;
            error_code = 1;
            break;
        case TLBRET_INVALID:
            /* TLB match with no valid bit */
            if (rw)
                exception = EXCP_TLBS;
            else
                exception = EXCP_TLBL;
            break;
        case TLBRET_DIRTY:
            /* TLB match but 'D' bit is cleared */
            exception = EXCP_LTLBL;
            break;

        }
        /* Raise exception */
        env->CP0_BadVAddr = address;
        env->CP0_Context = (env->CP0_Context & ~0x007fffff) |
	                   ((address >> 9) &   0x007ffff0);
        env->CP0_EntryHi =
            (env->CP0_EntryHi & 0xFF) | (address & (TARGET_PAGE_MASK << 1));
#if defined(TARGET_MIPS64)
        env->CP0_EntryHi &= env->SEGMask;
        env->CP0_XContext = (env->CP0_XContext & ((~0ULL) << (env->SEGBITS - 7))) |
                            ((address & 0xC00000000000ULL) >> (55 - env->SEGBITS)) |
                            ((address & ((1ULL << env->SEGBITS) - 1) & 0xFFFFFFFFFFFFE000ULL) >> 9);
#endif
        env->exception_index = exception;
        env->error_code = error_code;
        ret = 1;
    }

    return ret;
}

static const char * const excp_names[EXCP_LAST + 1] = {
    [EXCP_RESET] = "reset",
    [EXCP_SRESET] = "soft reset",
    [EXCP_DSS] = "debug single step",
    [EXCP_DINT] = "debug interrupt",
    [EXCP_NMI] = "non-maskable interrupt",
    [EXCP_MCHECK] = "machine check",
    [EXCP_EXT_INTERRUPT] = "interrupt",
    [EXCP_DFWATCH] = "deferred watchpoint",
    [EXCP_DIB] = "debug instruction breakpoint",
    [EXCP_IWATCH] = "instruction fetch watchpoint",
    [EXCP_AdEL] = "address error load",
    [EXCP_AdES] = "address error store",
    [EXCP_TLBF] = "TLB refill",
    [EXCP_IBE] = "instruction bus error",
    [EXCP_DBp] = "debug breakpoint",
    [EXCP_SYSCALL] = "syscall",
    [EXCP_BREAK] = "break",
    [EXCP_CpU] = "coprocessor unusable",
    [EXCP_RI] = "reserved instruction",
    [EXCP_OVERFLOW] = "arithmetic overflow",
    [EXCP_TRAP] = "trap",
    [EXCP_FPE] = "floating point",
    [EXCP_DDBS] = "debug data break store",
    [EXCP_DWATCH] = "data watchpoint",
    [EXCP_LTLBL] = "TLB modify",
    [EXCP_TLBL] = "TLB load",
    [EXCP_TLBS] = "TLB store",
    [EXCP_DBE] = "data bus error",
    [EXCP_DDBL] = "debug data break load",
    [EXCP_THREAD] = "thread",
    [EXCP_MDMX] = "MDMX",
    [EXCP_C2E] = "precise coprocessor 2",
    [EXCP_CACHE] = "cache error",
};

void do_interrupt (CPUState *env)
{
    if (!env->user_mode_only) {
        target_ulong offset;
        int cause = -1;
        const char *name;

        if (logfile && env->exception_index != EXCP_EXT_INTERRUPT) {
            if (env->exception_index < 0 || env->exception_index > EXCP_LAST)
                name = "unknown";
            else
                name = excp_names[env->exception_index];

            fprintf(logfile, "%s enter: PC " TARGET_FMT_lx " EPC " TARGET_FMT_lx " %s exception\n",
                    __func__, env->active_tc.PC, env->CP0_EPC, name);
        }
        if (env->exception_index == EXCP_EXT_INTERRUPT &&
            (env->hflags & MIPS_HFLAG_DM))
            env->exception_index = EXCP_DINT;
        offset = 0x180;
        switch (env->exception_index) {
        case EXCP_DSS:
            env->CP0_Debug |= 1 << CP0DB_DSS;
            /* Debug single step cannot be raised inside a delay slot and
               resume will always occur on the next instruction
               (but we assume the pc has always been updated during
               code translation). */
            env->CP0_DEPC = env->active_tc.PC;
            goto enter_debug_mode;
        case EXCP_DINT:
            env->CP0_Debug |= 1 << CP0DB_DINT;
            goto set_DEPC;
        case EXCP_DIB:
            env->CP0_Debug |= 1 << CP0DB_DIB;
            goto set_DEPC;
        case EXCP_DBp:
            env->CP0_Debug |= 1 << CP0DB_DBp;
            goto set_DEPC;
        case EXCP_DDBS:
            env->CP0_Debug |= 1 << CP0DB_DDBS;
            goto set_DEPC;
        case EXCP_DDBL:
            env->CP0_Debug |= 1 << CP0DB_DDBL;
        set_DEPC:
            if (env->hflags & MIPS_HFLAG_BMASK) {
                /* If the exception was raised from a delay slot,
                   come back to the jump.  */
                env->CP0_DEPC = env->active_tc.PC - 4;
                env->hflags &= ~MIPS_HFLAG_BMASK;
            } else {
                env->CP0_DEPC = env->active_tc.PC;
            }
 enter_debug_mode:
            env->hflags |= MIPS_HFLAG_DM | MIPS_HFLAG_64 | MIPS_HFLAG_CP0;
            env->hflags &= ~(MIPS_HFLAG_KSU);
            /* EJTAG probe trap enable is not implemented... */
            if (!(env->CP0_Status & (1 << CP0St_EXL)))
                env->CP0_Cause &= ~(1 << CP0Ca_BD);
            env->active_tc.PC = (int32_t)0xBFC00480;
            break;
        case EXCP_RESET:
            cpu_reset(env);
            break;
        case EXCP_SRESET:
            env->CP0_Status |= (1 << CP0St_SR);
            memset(env->CP0_WatchLo, 0, sizeof(*env->CP0_WatchLo));
            goto set_error_EPC;
        case EXCP_NMI:
            env->CP0_Status |= (1 << CP0St_NMI);
 set_error_EPC:
            if (env->hflags & MIPS_HFLAG_BMASK) {
                /* If the exception was raised from a delay slot,
                   come back to the jump.  */
                env->CP0_ErrorEPC = env->active_tc.PC - 4;
                env->hflags &= ~MIPS_HFLAG_BMASK;
            } else {
                env->CP0_ErrorEPC = env->active_tc.PC;
            }
            env->CP0_Status |= (1 << CP0St_ERL) | (1 << CP0St_BEV);
            env->hflags |= MIPS_HFLAG_64 | MIPS_HFLAG_CP0;
            env->hflags &= ~(MIPS_HFLAG_KSU);
            if (!(env->CP0_Status & (1 << CP0St_EXL)))
                env->CP0_Cause &= ~(1 << CP0Ca_BD);
            env->active_tc.PC = (int32_t)0xBFC00000;
            break;
        case EXCP_EXT_INTERRUPT:
            cause = 0;
            if (env->CP0_Cause & (1 << CP0Ca_IV))
                offset = 0x200;
            goto set_EPC;
        case EXCP_LTLBL:
            cause = 1;
            goto set_EPC;
        case EXCP_TLBL:
            cause = 2;
            if (env->error_code == 1 && !(env->CP0_Status & (1 << CP0St_EXL))) {
#if defined(TARGET_MIPS64)
                int R = env->CP0_BadVAddr >> 62;
                int UX = (env->CP0_Status & (1 << CP0St_UX)) != 0;
                int SX = (env->CP0_Status & (1 << CP0St_SX)) != 0;
                int KX = (env->CP0_Status & (1 << CP0St_KX)) != 0;

                if ((R == 0 && UX) || (R == 1 && SX) || (R == 3 && KX))
                    offset = 0x080;
                else
#endif
                    offset = 0x000;
            }
            goto set_EPC;
        case EXCP_TLBS:
            cause = 3;
            if (env->error_code == 1 && !(env->CP0_Status & (1 << CP0St_EXL))) {
#if defined(TARGET_MIPS64)
                int R = env->CP0_BadVAddr >> 62;
                int UX = (env->CP0_Status & (1 << CP0St_UX)) != 0;
                int SX = (env->CP0_Status & (1 << CP0St_SX)) != 0;
                int KX = (env->CP0_Status & (1 << CP0St_KX)) != 0;

                if ((R == 0 && UX) || (R == 1 && SX) || (R == 3 && KX))
                    offset = 0x080;
                else
#endif
                    offset = 0x000;
            }
            goto set_EPC;
        case EXCP_AdEL:
            cause = 4;
            goto set_EPC;
        case EXCP_AdES:
            cause = 5;
            goto set_EPC;
        case EXCP_IBE:
            cause = 6;
            goto set_EPC;
        case EXCP_DBE:
            cause = 7;
            goto set_EPC;
        case EXCP_SYSCALL:
            cause = 8;
            goto set_EPC;
        case EXCP_BREAK:
            cause = 9;
            goto set_EPC;
        case EXCP_RI:
            cause = 10;
            goto set_EPC;
        case EXCP_CpU:
            cause = 11;
            env->CP0_Cause = (env->CP0_Cause & ~(0x3 << CP0Ca_CE)) |
                             (env->error_code << CP0Ca_CE);
            goto set_EPC;
        case EXCP_OVERFLOW:
            cause = 12;
            goto set_EPC;
        case EXCP_TRAP:
            cause = 13;
            goto set_EPC;
        case EXCP_FPE:
            cause = 15;
            goto set_EPC;
        case EXCP_C2E:
            cause = 18;
            goto set_EPC;
        case EXCP_MDMX:
            cause = 22;
            goto set_EPC;
        case EXCP_DWATCH:
            cause = 23;
            /* XXX: TODO: manage defered watch exceptions */
            goto set_EPC;
        case EXCP_MCHECK:
            cause = 24;
            goto set_EPC;
        case EXCP_THREAD:
            cause = 25;
            goto set_EPC;
        case EXCP_CACHE:
            cause = 30;
            if (env->CP0_Status & (1 << CP0St_BEV)) {
                offset = 0x100;
            } else {
                offset = 0x20000100;
            }
 set_EPC:
            if (!(env->CP0_Status & (1 << CP0St_EXL))) {
                if (env->hflags & MIPS_HFLAG_BMASK) {
                    /* If the exception was raised from a delay slot,
                       come back to the jump.  */
                    env->CP0_EPC = env->active_tc.PC - 4;
                    env->CP0_Cause |= (1 << CP0Ca_BD);
                } else {
                    env->CP0_EPC = env->active_tc.PC;
                    env->CP0_Cause &= ~(1 << CP0Ca_BD);
                }
                env->CP0_Status |= (1 << CP0St_EXL);
                env->hflags |= MIPS_HFLAG_64 | MIPS_HFLAG_CP0;
                env->hflags &= ~(MIPS_HFLAG_KSU);
            }
            env->hflags &= ~MIPS_HFLAG_BMASK;
            if (env->CP0_Status & (1 << CP0St_BEV)) {
                env->active_tc.PC = (int32_t)0xBFC00200;
            } else {
                env->active_tc.PC = (int32_t)(env->CP0_EBase & ~0x3ff);
            }
            env->active_tc.PC += offset;
            env->CP0_Cause = (env->CP0_Cause & ~(0x1f << CP0Ca_EC)) | (cause << CP0Ca_EC);
            break;
        default:
            if (logfile) {
                fprintf(logfile, "Invalid MIPS exception %d. Exiting\n",
                        env->exception_index);
            }
            printf("Invalid MIPS exception %d. Exiting\n", env->exception_index);
            exit(1);
        }
        if (logfile && env->exception_index != EXCP_EXT_INTERRUPT) {
            fprintf(logfile, "%s: PC " TARGET_FMT_lx " EPC " TARGET_FMT_lx " cause %d\n"
                    "    S %08x C %08x A " TARGET_FMT_lx " D " TARGET_FMT_lx "\n",
                    __func__, env->active_tc.PC, env->CP0_EPC, cause,
                    env->CP0_Status, env->CP0_Cause, env->CP0_BadVAddr,
                    env->CP0_DEPC);
        }
    }
    env->exception_index = EXCP_NONE;
}

void r4k_invalidate_tlb (CPUState *env, int idx, int use_extra)
{
    r4k_tlb_t *tlb;
    target_ulong addr;
    target_ulong end;
    uint8_t ASID = env->CP0_EntryHi & 0xFF;
    target_ulong mask;

    tlb = &env->tlb->mmu.r4k.tlb[idx];
    /* The qemu TLB is flushed when the ASID changes, so no need to
       flush these entries again.  */
    if (tlb->G == 0 && tlb->ASID != ASID) {
        return;
    }

    if (use_extra && env->tlb->tlb_in_use < MIPS_TLB_MAX) {
        /* For tlbwr, we can shadow the discarded entry into
	   a new (fake) TLB entry, as long as the guest can not
	   tell that it's there.  */
        env->tlb->mmu.r4k.tlb[env->tlb->tlb_in_use] = *tlb;
        env->tlb->tlb_in_use++;
        return;
    }

    /* 1k pages are not supported. */
    mask = tlb->PageMask | ~(TARGET_PAGE_MASK << 1);
    if (tlb->V0) {
        addr = tlb->VPN & ~mask;
#if defined(TARGET_MIPS64)
        if (addr >= (0xFFFFFFFF80000000ULL & env->SEGMask)) {
            addr |= 0x3FFFFF0000000000ULL;
        }
#endif
        end = addr | (mask >> 1);
        while (addr < end) {
            tlb_flush_page (env, addr);
            addr += TARGET_PAGE_SIZE;
        }
    }
    if (tlb->V1) {
        addr = (tlb->VPN & ~mask) | ((mask >> 1) + 1);
#if defined(TARGET_MIPS64)
        if (addr >= (0xFFFFFFFF80000000ULL & env->SEGMask)) {
            addr |= 0x3FFFFF0000000000ULL;
        }
#endif
        end = addr | mask;
        while (addr - 1 < end) {
            tlb_flush_page (env, addr);
            addr += TARGET_PAGE_SIZE;
        }
    }
}
