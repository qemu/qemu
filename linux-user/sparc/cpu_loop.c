/*
 *  qemu user cpu loop
 *
 *  Copyright (c) 2003-2008 Fabrice Bellard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu.h"
#include "user-internals.h"
#include "cpu_loop-common.h"
#include "signal-common.h"

#define SPARC64_STACK_BIAS 2047

//#define DEBUG_WIN

/* WARNING: dealing with register windows _is_ complicated. More info
   can be found at http://www.sics.se/~psm/sparcstack.html */
static inline int get_reg_index(CPUSPARCState *env, int cwp, int index)
{
    index = (index + cwp * 16) % (16 * env->nwindows);
    /* wrap handling : if cwp is on the last window, then we use the
       registers 'after' the end */
    if (index < 8 && env->cwp == env->nwindows - 1)
        index += 16 * env->nwindows;
    return index;
}

/* save the register window 'cwp1' */
static inline void save_window_offset(CPUSPARCState *env, int cwp1)
{
    unsigned int i;
    abi_ulong sp_ptr;

    sp_ptr = env->regbase[get_reg_index(env, cwp1, 6)];
#ifdef TARGET_SPARC64
    if (sp_ptr & 3)
        sp_ptr += SPARC64_STACK_BIAS;
#endif
#if defined(DEBUG_WIN)
    printf("win_overflow: sp_ptr=0x" TARGET_ABI_FMT_lx " save_cwp=%d\n",
           sp_ptr, cwp1);
#endif
    for(i = 0; i < 16; i++) {
        /* FIXME - what to do if put_user() fails? */
        put_user_ual(env->regbase[get_reg_index(env, cwp1, 8 + i)], sp_ptr);
        sp_ptr += sizeof(abi_ulong);
    }
}

static void save_window(CPUSPARCState *env)
{
#ifndef TARGET_SPARC64
    unsigned int new_wim;
    new_wim = ((env->wim >> 1) | (env->wim << (env->nwindows - 1))) &
        ((1LL << env->nwindows) - 1);
    save_window_offset(env, cpu_cwp_dec(env, env->cwp - 2));
    env->wim = new_wim;
#else
    /*
     * cansave is zero if the spill trap handler is triggered by `save` and
     * nonzero if triggered by a `flushw`
     */
    save_window_offset(env, cpu_cwp_dec(env, env->cwp - env->cansave - 2));
    env->cansave++;
    env->canrestore--;
#endif
}

static void restore_window(CPUSPARCState *env)
{
#ifndef TARGET_SPARC64
    unsigned int new_wim;
#endif
    unsigned int i, cwp1;
    abi_ulong sp_ptr;

#ifndef TARGET_SPARC64
    new_wim = ((env->wim << 1) | (env->wim >> (env->nwindows - 1))) &
        ((1LL << env->nwindows) - 1);
#endif

    /* restore the invalid window */
    cwp1 = cpu_cwp_inc(env, env->cwp + 1);
    sp_ptr = env->regbase[get_reg_index(env, cwp1, 6)];
#ifdef TARGET_SPARC64
    if (sp_ptr & 3)
        sp_ptr += SPARC64_STACK_BIAS;
#endif
#if defined(DEBUG_WIN)
    printf("win_underflow: sp_ptr=0x" TARGET_ABI_FMT_lx " load_cwp=%d\n",
           sp_ptr, cwp1);
#endif
    for(i = 0; i < 16; i++) {
        /* FIXME - what to do if get_user() fails? */
        get_user_ual(env->regbase[get_reg_index(env, cwp1, 8 + i)], sp_ptr);
        sp_ptr += sizeof(abi_ulong);
    }
#ifdef TARGET_SPARC64
    env->canrestore++;
    if (env->cleanwin < env->nwindows - 1)
        env->cleanwin++;
    env->cansave--;
#else
    env->wim = new_wim;
#endif
}

static void flush_windows(CPUSPARCState *env)
{
    int offset, cwp1;

    offset = 1;
    for(;;) {
        /* if restore would invoke restore_window(), then we can stop */
        cwp1 = cpu_cwp_inc(env, env->cwp + offset);
#ifndef TARGET_SPARC64
        if (env->wim & (1 << cwp1))
            break;
#else
        if (env->canrestore == 0)
            break;
        env->cansave++;
        env->canrestore--;
#endif
        save_window_offset(env, cwp1);
        offset++;
    }
    cwp1 = cpu_cwp_inc(env, env->cwp + 1);
#ifndef TARGET_SPARC64
    /* set wim so that restore will reload the registers */
    env->wim = 1 << cwp1;
#endif
#if defined(DEBUG_WIN)
    printf("flush_windows: nb=%d\n", offset - 1);
#endif
}

static void next_instruction(CPUSPARCState *env)
{
    env->pc = env->npc;
    env->npc = env->npc + 4;
}

static uint32_t do_getcc(CPUSPARCState *env)
{
#ifdef TARGET_SPARC64
    return cpu_get_ccr(env) & 0xf;
#else
    return extract32(cpu_get_psr(env), 20, 4);
#endif
}

static void do_setcc(CPUSPARCState *env, uint32_t icc)
{
#ifdef TARGET_SPARC64
    cpu_put_ccr(env, (cpu_get_ccr(env) & 0xf0) | (icc & 0xf));
#else
    cpu_put_psr(env, deposit32(cpu_get_psr(env), 20, 4, icc));
#endif
}

static uint32_t do_getpsr(CPUSPARCState *env)
{
#ifdef TARGET_SPARC64
    const uint64_t TSTATE_CWP = 0x1f;
    const uint64_t TSTATE_ICC = 0xfull << 32;
    const uint64_t TSTATE_XCC = 0xfull << 36;
    const uint32_t PSR_S      = 0x00000080u;
    const uint32_t PSR_V8PLUS = 0xff000000u;
    uint64_t tstate = sparc64_tstate(env);

    /* See <asm/psrcompat.h>, tstate_to_psr. */
    return ((tstate & TSTATE_CWP)                   |
            PSR_S                                   |
            ((tstate & TSTATE_ICC) >> 12)           |
            ((tstate & TSTATE_XCC) >> 20)           |
            PSR_V8PLUS);
#else
    return (cpu_get_psr(env) & (PSR_ICC | PSR_CWP)) | PSR_S;
#endif
}

/* Avoid ifdefs below for the abi32 and abi64 paths. */
#ifdef TARGET_ABI32
#define TARGET_TT_SYSCALL  (TT_TRAP + 0x10) /* t_linux */
#else
#define TARGET_TT_SYSCALL  (TT_TRAP + 0x6d) /* tl0_linux64 */
#endif

/* Avoid ifdefs below for the v9 and pre-v9 hw traps. */
#ifdef TARGET_SPARC64
#define TARGET_TT_SPILL  TT_SPILL
#define TARGET_TT_FILL   TT_FILL
#else
#define TARGET_TT_SPILL  TT_WIN_OVF
#define TARGET_TT_FILL   TT_WIN_UNF
#endif

void cpu_loop (CPUSPARCState *env)
{
    CPUState *cs = env_cpu(env);
    int trapnr;
    abi_long ret;

    while (1) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        switch (trapnr) {
        case TARGET_TT_SYSCALL:
            ret = do_syscall (env, env->gregs[1],
                              env->regwptr[0], env->regwptr[1],
                              env->regwptr[2], env->regwptr[3],
                              env->regwptr[4], env->regwptr[5],
                              0, 0);
            if (ret == -QEMU_ERESTARTSYS || ret == -QEMU_ESIGRETURN) {
                break;
            }
            if ((abi_ulong)ret >= (abi_ulong)(-515)) {
                set_syscall_C(env, 1);
                ret = -ret;
            } else {
                set_syscall_C(env, 0);
            }
            env->regwptr[0] = ret;
            /* next instruction */
            env->pc = env->npc;
            env->npc = env->npc + 4;
            break;

        case TT_TRAP + 0x01: /* breakpoint */
        case EXCP_DEBUG:
            force_sig_fault(TARGET_SIGTRAP, TARGET_TRAP_BRKPT, env->pc);
            break;

        case TT_TRAP + 0x02: /* div0 */
        case TT_DIV_ZERO:
            force_sig_fault(TARGET_SIGFPE, TARGET_FPE_INTDIV, env->pc);
            break;

        case TT_TRAP + 0x03: /* flush windows */
            flush_windows(env);
            next_instruction(env);
            break;

        case TT_TRAP + 0x20: /* getcc */
            env->gregs[1] = do_getcc(env);
            next_instruction(env);
            break;
        case TT_TRAP + 0x21: /* setcc */
            do_setcc(env, env->gregs[1]);
            next_instruction(env);
            break;
        case TT_TRAP + 0x22: /* getpsr */
            env->gregs[1] = do_getpsr(env);
            next_instruction(env);
            break;

#ifdef TARGET_SPARC64
        case TT_TRAP + 0x6e:
            flush_windows(env);
            sparc64_get_context(env);
            break;
        case TT_TRAP + 0x6f:
            flush_windows(env);
            sparc64_set_context(env);
            break;
#endif

        case TARGET_TT_SPILL: /* window overflow */
            save_window(env);
            break;
        case TARGET_TT_FILL:  /* window underflow */
            restore_window(env);
            break;

        case TT_FP_EXCP:
            {
                int code = TARGET_FPE_FLTUNK;
                target_ulong fsr = cpu_get_fsr(env);

                if ((fsr & FSR_FTT_MASK) == FSR_FTT_IEEE_EXCP) {
                    if (fsr & FSR_NVC) {
                        code = TARGET_FPE_FLTINV;
                    } else if (fsr & FSR_OFC) {
                        code = TARGET_FPE_FLTOVF;
                    } else if (fsr & FSR_UFC) {
                        code = TARGET_FPE_FLTUND;
                    } else if (fsr & FSR_DZC) {
                        code = TARGET_FPE_FLTDIV;
                    } else if (fsr & FSR_NXC) {
                        code = TARGET_FPE_FLTRES;
                    }
                }
                force_sig_fault(TARGET_SIGFPE, code, env->pc);
            }
            break;

        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case TT_ILL_INSN:
            force_sig_fault(TARGET_SIGILL, TARGET_ILL_ILLOPC, env->pc);
            break;
        case TT_PRIV_INSN:
            force_sig_fault(TARGET_SIGILL, TARGET_ILL_PRVOPC, env->pc);
            break;
        case TT_TOVF:
            force_sig_fault(TARGET_SIGEMT, TARGET_EMT_TAGOVF, env->pc);
            break;
#ifdef TARGET_SPARC64
        case TT_PRIV_ACT:
            /* Note do_privact defers to do_privop. */
            force_sig_fault(TARGET_SIGILL, TARGET_ILL_PRVOPC, env->pc);
            break;
#else
        case TT_NCP_INSN:
            force_sig_fault(TARGET_SIGILL, TARGET_ILL_COPROC, env->pc);
            break;
        case TT_UNIMP_FLUSH:
            next_instruction(env);
            break;
#endif
        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
            break;
        default:
            /*
             * Most software trap numbers vector to BAD_TRAP.
             * Handle anything not explicitly matched above.
             */
            if (trapnr >= TT_TRAP && trapnr <= TT_TRAP + 0x7f) {
                force_sig_fault(TARGET_SIGILL, ILL_ILLTRP, env->pc);
                break;
            }
            fprintf(stderr, "Unhandled trap: 0x%x\n", trapnr);
            cpu_dump_state(cs, stderr, 0);
            exit(EXIT_FAILURE);
        }
        process_pending_signals (env);
    }
}

void target_cpu_copy_regs(CPUArchState *env, struct target_pt_regs *regs)
{
    int i;
    env->pc = regs->pc;
    env->npc = regs->npc;
    env->y = regs->y;
    for(i = 0; i < 8; i++)
        env->gregs[i] = regs->u_regs[i];
    for(i = 0; i < 8; i++)
        env->regwptr[i] = regs->u_regs[i + 8];
}
