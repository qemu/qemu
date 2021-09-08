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
#include "qemu-common.h"
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

void cpu_loop (CPUSPARCState *env)
{
    CPUState *cs = env_cpu(env);
    int trapnr;
    abi_long ret;
    target_siginfo_t info;

    while (1) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        /* Compute PSR before exposing state.  */
        if (env->cc_op != CC_OP_FLAGS) {
            cpu_get_psr(env);
        }

        switch (trapnr) {
#ifndef TARGET_SPARC64
        case 0x88:
        case 0x90:
#else
        case 0x110:
        case 0x16d:
#endif
            ret = do_syscall (env, env->gregs[1],
                              env->regwptr[0], env->regwptr[1],
                              env->regwptr[2], env->regwptr[3],
                              env->regwptr[4], env->regwptr[5],
                              0, 0);
            if (ret == -TARGET_ERESTARTSYS || ret == -TARGET_QEMU_ESIGRETURN) {
                break;
            }
            if ((abi_ulong)ret >= (abi_ulong)(-515)) {
#if defined(TARGET_SPARC64) && !defined(TARGET_ABI32)
                env->xcc |= PSR_CARRY;
#else
                env->psr |= PSR_CARRY;
#endif
                ret = -ret;
            } else {
#if defined(TARGET_SPARC64) && !defined(TARGET_ABI32)
                env->xcc &= ~PSR_CARRY;
#else
                env->psr &= ~PSR_CARRY;
#endif
            }
            env->regwptr[0] = ret;
            /* next instruction */
            env->pc = env->npc;
            env->npc = env->npc + 4;
            break;
        case 0x83: /* flush windows */
#ifdef TARGET_ABI32
        case 0x103:
#endif
            flush_windows(env);
            /* next instruction */
            env->pc = env->npc;
            env->npc = env->npc + 4;
            break;
#ifndef TARGET_SPARC64
        case TT_WIN_OVF: /* window overflow */
            save_window(env);
            break;
        case TT_WIN_UNF: /* window underflow */
            restore_window(env);
            break;
        case TT_TFAULT:
        case TT_DFAULT:
            {
                info.si_signo = TARGET_SIGSEGV;
                info.si_errno = 0;
                /* XXX: check env->error_code */
                info.si_code = TARGET_SEGV_MAPERR;
                info._sifields._sigfault._addr = env->mmuregs[4];
                queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            }
            break;
#else
        case TT_SPILL: /* window overflow */
            save_window(env);
            break;
        case TT_FILL: /* window underflow */
            restore_window(env);
            break;
        case TT_TFAULT:
        case TT_DFAULT:
            {
                info.si_signo = TARGET_SIGSEGV;
                info.si_errno = 0;
                /* XXX: check env->error_code */
                info.si_code = TARGET_SEGV_MAPERR;
                if (trapnr == TT_DFAULT)
                    info._sifields._sigfault._addr = env->dmmu.mmuregs[4];
                else
                    info._sifields._sigfault._addr = cpu_tsptr(env)->tpc;
                queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            }
            break;
#ifndef TARGET_ABI32
        case 0x16e:
            flush_windows(env);
            sparc64_get_context(env);
            break;
        case 0x16f:
            flush_windows(env);
            sparc64_set_context(env);
            break;
#endif
#endif
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case TT_ILL_INSN:
            {
                info.si_signo = TARGET_SIGILL;
                info.si_errno = 0;
                info.si_code = TARGET_ILL_ILLOPC;
                info._sifields._sigfault._addr = env->pc;
                queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            }
            break;
        case EXCP_DEBUG:
            info.si_signo = TARGET_SIGTRAP;
            info.si_errno = 0;
            info.si_code = TARGET_TRAP_BRKPT;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
            break;
        default:
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
