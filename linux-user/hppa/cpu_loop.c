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
#include "user/cpu_loop.h"
#include "signal-common.h"

static abi_ulong hppa_lws(CPUHPPAState *env)
{
    CPUState *cs = env_cpu(env);
    uint32_t which = env->gr[20];
    abi_ulong addr = env->gr[26];
    abi_ulong old = env->gr[25];
    abi_ulong new = env->gr[24];
    abi_ulong size, ret;

    switch (which) {
    default:
        return -TARGET_ENOSYS;

    case 0: /* elf32 atomic 32bit cmpxchg */
        if ((addr & 3) || !access_ok(cs, VERIFY_WRITE, addr, 4)) {
            return -TARGET_EFAULT;
        }
        old = tswap32(old);
        new = tswap32(new);
        ret = qatomic_cmpxchg((uint32_t *)g2h(cs, addr), old, new);
        ret = tswap32(ret);
        break;

    case 2: /* elf32 atomic "new" cmpxchg */
        size = env->gr[23];
        if (size >= 4) {
            return -TARGET_ENOSYS;
        }
        if (((addr | old | new) & ((1 << size) - 1))
            || !access_ok(cs, VERIFY_WRITE, addr, 1 << size)
            || !access_ok(cs, VERIFY_READ, old, 1 << size)
            || !access_ok(cs, VERIFY_READ, new, 1 << size)) {
            return -TARGET_EFAULT;
        }
        /* Note that below we use host-endian loads so that the cmpxchg
           can be host-endian as well.  */
        switch (size) {
        case 0:
            old = *(uint8_t *)g2h(cs, old);
            new = *(uint8_t *)g2h(cs, new);
            ret = qatomic_cmpxchg((uint8_t *)g2h(cs, addr), old, new);
            ret = ret != old;
            break;
        case 1:
            old = *(uint16_t *)g2h(cs, old);
            new = *(uint16_t *)g2h(cs, new);
            ret = qatomic_cmpxchg((uint16_t *)g2h(cs, addr), old, new);
            ret = ret != old;
            break;
        case 2:
            old = *(uint32_t *)g2h(cs, old);
            new = *(uint32_t *)g2h(cs, new);
            ret = qatomic_cmpxchg((uint32_t *)g2h(cs, addr), old, new);
            ret = ret != old;
            break;
        case 3:
            {
                uint64_t o64, n64, r64;
                o64 = *(uint64_t *)g2h(cs, old);
                n64 = *(uint64_t *)g2h(cs, new);
#ifdef CONFIG_ATOMIC64
                r64 = qatomic_cmpxchg__nocheck((aligned_uint64_t *)g2h(cs, addr),
                                               o64, n64);
                ret = r64 != o64;
#else
                start_exclusive();
                r64 = *(uint64_t *)g2h(cs, addr);
                ret = 1;
                if (r64 == o64) {
                    *(uint64_t *)g2h(cs, addr) = n64;
                    ret = 0;
                }
                end_exclusive();
#endif
            }
            break;
        default:
            g_assert_not_reached();
        }
        break;
    }

    env->gr[28] = ret;
    return 0;
}

void cpu_loop(CPUHPPAState *env)
{
    CPUState *cs = env_cpu(env);
    abi_ulong ret, si_code = 0;
    int trapnr;

    while (1) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        qemu_process_cpu_events(cs);

        switch (trapnr) {
        case EXCP_SYSCALL:
            ret = do_syscall(env, env->gr[20],
                             env->gr[26], env->gr[25],
                             env->gr[24], env->gr[23],
                             env->gr[22], env->gr[21], 0, 0);
            switch (ret) {
            default:
                env->gr[28] = ret;
                /* We arrived here by faking the gateway page.  Return.  */
                env->iaoq_f = env->gr[31] | PRIV_USER;
                env->iaoq_b = env->iaoq_f + 4;
                break;
            case -QEMU_ERESTARTSYS:
            case -QEMU_ESIGRETURN:
                break;
            }
            break;
        case EXCP_SYSCALL_LWS:
            env->gr[21] = hppa_lws(env);
            /* We arrived here by faking the gateway page.  Return.  */
            env->iaoq_f = env->gr[31] | PRIV_USER;
            env->iaoq_b = env->iaoq_f + 4;
            break;
        case EXCP_IMP:
            force_sig_fault(TARGET_SIGSEGV, TARGET_SEGV_MAPERR, env->iaoq_f);
            break;
        case EXCP_ILL:
            force_sig_fault(TARGET_SIGILL, TARGET_ILL_ILLOPC, env->iaoq_f);
            break;
        case EXCP_PRIV_OPR:
            /* check for glibc ABORT_INSTRUCTION "iitlbp %r0,(%sr0, %r0)" */
            if (env->cr[CR_IIR] == 0x04000000) {
                force_sig_fault(TARGET_SIGILL, TARGET_ILL_ILLOPC, env->iaoq_f);
            } else {
                force_sig_fault(TARGET_SIGILL, TARGET_ILL_PRVOPC, env->iaoq_f);
            }
            break;
        case EXCP_PRIV_REG:
            force_sig_fault(TARGET_SIGILL, TARGET_ILL_PRVREG, env->iaoq_f);
            break;
        case EXCP_OVERFLOW:
            force_sig_fault(TARGET_SIGFPE, TARGET_FPE_INTOVF, env->iaoq_f);
            break;
        case EXCP_COND:
            force_sig_fault(TARGET_SIGFPE, TARGET_FPE_CONDTRAP, env->iaoq_f);
            break;
        case EXCP_ASSIST:
            #define set_si_code(mask, val) \
                if (env->fr[0] & mask) { si_code = val; }
            set_si_code(R_FPSR_FLG_I_MASK, TARGET_FPE_FLTRES);
            set_si_code(R_FPSR_FLG_U_MASK, TARGET_FPE_FLTUND);
            set_si_code(R_FPSR_FLG_O_MASK, TARGET_FPE_FLTOVF);
            set_si_code(R_FPSR_FLG_Z_MASK, TARGET_FPE_FLTDIV);
            set_si_code(R_FPSR_FLG_V_MASK, TARGET_FPE_FLTINV);
            #undef set_si_code
            force_sig_fault(TARGET_SIGFPE, si_code, env->iaoq_f);
            break;
        case EXCP_BREAK:
            force_sig_fault(TARGET_SIGTRAP, TARGET_TRAP_BRKPT, env->iaoq_f);
            break;
        case EXCP_DEBUG:
            force_sig_fault(TARGET_SIGTRAP, TARGET_TRAP_BRKPT, env->iaoq_f);
            break;
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        default:
            EXCP_DUMP(env, "qemu: unhandled CPU exception 0x%x - aborting\n", trapnr);
            abort();
        }
        process_pending_signals(env);
    }
}

void init_main_thread(CPUState *cs, struct image_info *info)
{
    CPUArchState *env = cpu_env(cs);

    env->iaoq_f = info->entry | PRIV_USER;
    env->iaoq_b = env->iaoq_f + 4;
    env->gr[23] = 0;
    env->gr[24] = info->argv;
    env->gr[25] = info->argc;
    /* The top-of-stack contains a linkage buffer.  */
    env->gr[30] = info->start_stack + 64;
    env->gr[31] = info->entry;
}
