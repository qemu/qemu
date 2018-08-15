/*
 *  User emulator execution
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
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
#include "disas/disas.h"
#include "exec/exec-all.h"
#include "tcg.h"
#include "qemu/bitops.h"
#include "exec/cpu_ldst.h"
#include "translate-all.h"
#include "exec/helper-proto.h"
#include "qemu/atomic128.h"

#undef EAX
#undef ECX
#undef EDX
#undef EBX
#undef ESP
#undef EBP
#undef ESI
#undef EDI
#undef EIP
#ifdef __linux__
#include <sys/ucontext.h>
#endif

__thread uintptr_t helper_retaddr;

//#define DEBUG_SIGNAL

/* exit the current TB from a signal handler. The host registers are
   restored in a state compatible with the CPU emulator
 */
static void cpu_exit_tb_from_sighandler(CPUState *cpu, sigset_t *old_set)
{
    /* XXX: use siglongjmp ? */
    sigprocmask(SIG_SETMASK, old_set, NULL);
    cpu_loop_exit_noexc(cpu);
}

/* 'pc' is the host PC at which the exception was raised. 'address' is
   the effective address of the memory exception. 'is_write' is 1 if a
   write caused the exception and otherwise 0'. 'old_set' is the
   signal set which should be restored */
static inline int handle_cpu_signal(uintptr_t pc, siginfo_t *info,
                                    int is_write, sigset_t *old_set)
{
    CPUState *cpu = current_cpu;
    CPUClass *cc;
    int ret;
    unsigned long address = (unsigned long)info->si_addr;

    /* We must handle PC addresses from two different sources:
     * a call return address and a signal frame address.
     *
     * Within cpu_restore_state_from_tb we assume the former and adjust
     * the address by -GETPC_ADJ so that the address is within the call
     * insn so that addr does not accidentally match the beginning of the
     * next guest insn.
     *
     * However, when the PC comes from the signal frame, it points to
     * the actual faulting host insn and not a call insn.  Subtracting
     * GETPC_ADJ in that case may accidentally match the previous guest insn.
     *
     * So for the later case, adjust forward to compensate for what
     * will be done later by cpu_restore_state_from_tb.
     */
    if (helper_retaddr) {
        pc = helper_retaddr;
    } else {
        pc += GETPC_ADJ;
    }

    /* For synchronous signals we expect to be coming from the vCPU
     * thread (so current_cpu should be valid) and either from running
     * code or during translation which can fault as we cross pages.
     *
     * If neither is true then something has gone wrong and we should
     * abort rather than try and restart the vCPU execution.
     */
    if (!cpu || !cpu->running) {
        printf("qemu:%s received signal outside vCPU context @ pc=0x%"
               PRIxPTR "\n",  __func__, pc);
        abort();
    }

#if defined(DEBUG_SIGNAL)
    printf("qemu: SIGSEGV pc=0x%08lx address=%08lx w=%d oldset=0x%08lx\n",
           pc, address, is_write, *(unsigned long *)old_set);
#endif
    /* XXX: locking issue */
    /* Note that it is important that we don't call page_unprotect() unless
     * this is really a "write to nonwriteable page" fault, because
     * page_unprotect() assumes that if it is called for an access to
     * a page that's writeable this means we had two threads racing and
     * another thread got there first and already made the page writeable;
     * so we will retry the access. If we were to call page_unprotect()
     * for some other kind of fault that should really be passed to the
     * guest, we'd end up in an infinite loop of retrying the faulting
     * access.
     */
    if (is_write && info->si_signo == SIGSEGV && info->si_code == SEGV_ACCERR &&
        h2g_valid(address)) {
        switch (page_unprotect(h2g(address), pc)) {
        case 0:
            /* Fault not caused by a page marked unwritable to protect
             * cached translations, must be the guest binary's problem.
             */
            break;
        case 1:
            /* Fault caused by protection of cached translation; TBs
             * invalidated, so resume execution.  Retain helper_retaddr
             * for a possible second fault.
             */
            return 1;
        case 2:
            /* Fault caused by protection of cached translation, and the
             * currently executing TB was modified and must be exited
             * immediately.  Clear helper_retaddr for next execution.
             */
            helper_retaddr = 0;
            cpu_exit_tb_from_sighandler(cpu, old_set);
            /* NORETURN */

        default:
            g_assert_not_reached();
        }
    }

    /* Convert forcefully to guest address space, invalid addresses
       are still valid segv ones */
    address = h2g_nocheck(address);

    cc = CPU_GET_CLASS(cpu);
    /* see if it is an MMU fault */
    g_assert(cc->handle_mmu_fault);
    ret = cc->handle_mmu_fault(cpu, address, 0, is_write, MMU_USER_IDX);

    if (ret == 0) {
        /* The MMU fault was handled without causing real CPU fault.
         *  Retain helper_retaddr for a possible second fault.
         */
        return 1;
    }

    /* All other paths lead to cpu_exit; clear helper_retaddr
     * for next execution.
     */
    helper_retaddr = 0;

    if (ret < 0) {
        return 0; /* not an MMU fault */
    }

    /* Now we have a real cpu fault.  */
    cpu_restore_state(cpu, pc, true);

    sigprocmask(SIG_SETMASK, old_set, NULL);
    cpu_loop_exit(cpu);

    /* never comes here */
    return 1;
}

#if defined(__i386__)

#if defined(__NetBSD__)
#include <ucontext.h>

#define EIP_sig(context)     ((context)->uc_mcontext.__gregs[_REG_EIP])
#define TRAP_sig(context)    ((context)->uc_mcontext.__gregs[_REG_TRAPNO])
#define ERROR_sig(context)   ((context)->uc_mcontext.__gregs[_REG_ERR])
#define MASK_sig(context)    ((context)->uc_sigmask)
#elif defined(__FreeBSD__) || defined(__DragonFly__)
#include <ucontext.h>

#define EIP_sig(context)  (*((unsigned long *)&(context)->uc_mcontext.mc_eip))
#define TRAP_sig(context)    ((context)->uc_mcontext.mc_trapno)
#define ERROR_sig(context)   ((context)->uc_mcontext.mc_err)
#define MASK_sig(context)    ((context)->uc_sigmask)
#elif defined(__OpenBSD__)
#define EIP_sig(context)     ((context)->sc_eip)
#define TRAP_sig(context)    ((context)->sc_trapno)
#define ERROR_sig(context)   ((context)->sc_err)
#define MASK_sig(context)    ((context)->sc_mask)
#else
#define EIP_sig(context)     ((context)->uc_mcontext.gregs[REG_EIP])
#define TRAP_sig(context)    ((context)->uc_mcontext.gregs[REG_TRAPNO])
#define ERROR_sig(context)   ((context)->uc_mcontext.gregs[REG_ERR])
#define MASK_sig(context)    ((context)->uc_sigmask)
#endif

int cpu_signal_handler(int host_signum, void *pinfo,
                       void *puc)
{
    siginfo_t *info = pinfo;
#if defined(__NetBSD__) || defined(__FreeBSD__) || defined(__DragonFly__)
    ucontext_t *uc = puc;
#elif defined(__OpenBSD__)
    struct sigcontext *uc = puc;
#else
    ucontext_t *uc = puc;
#endif
    unsigned long pc;
    int trapno;

#ifndef REG_EIP
/* for glibc 2.1 */
#define REG_EIP    EIP
#define REG_ERR    ERR
#define REG_TRAPNO TRAPNO
#endif
    pc = EIP_sig(uc);
    trapno = TRAP_sig(uc);
    return handle_cpu_signal(pc, info,
                             trapno == 0xe ? (ERROR_sig(uc) >> 1) & 1 : 0,
                             &MASK_sig(uc));
}

#elif defined(__x86_64__)

#ifdef __NetBSD__
#define PC_sig(context)       _UC_MACHINE_PC(context)
#define TRAP_sig(context)     ((context)->uc_mcontext.__gregs[_REG_TRAPNO])
#define ERROR_sig(context)    ((context)->uc_mcontext.__gregs[_REG_ERR])
#define MASK_sig(context)     ((context)->uc_sigmask)
#elif defined(__OpenBSD__)
#define PC_sig(context)       ((context)->sc_rip)
#define TRAP_sig(context)     ((context)->sc_trapno)
#define ERROR_sig(context)    ((context)->sc_err)
#define MASK_sig(context)     ((context)->sc_mask)
#elif defined(__FreeBSD__) || defined(__DragonFly__)
#include <ucontext.h>

#define PC_sig(context)  (*((unsigned long *)&(context)->uc_mcontext.mc_rip))
#define TRAP_sig(context)     ((context)->uc_mcontext.mc_trapno)
#define ERROR_sig(context)    ((context)->uc_mcontext.mc_err)
#define MASK_sig(context)     ((context)->uc_sigmask)
#else
#define PC_sig(context)       ((context)->uc_mcontext.gregs[REG_RIP])
#define TRAP_sig(context)     ((context)->uc_mcontext.gregs[REG_TRAPNO])
#define ERROR_sig(context)    ((context)->uc_mcontext.gregs[REG_ERR])
#define MASK_sig(context)     ((context)->uc_sigmask)
#endif

int cpu_signal_handler(int host_signum, void *pinfo,
                       void *puc)
{
    siginfo_t *info = pinfo;
    unsigned long pc;
#if defined(__NetBSD__) || defined(__FreeBSD__) || defined(__DragonFly__)
    ucontext_t *uc = puc;
#elif defined(__OpenBSD__)
    struct sigcontext *uc = puc;
#else
    ucontext_t *uc = puc;
#endif

    pc = PC_sig(uc);
    return handle_cpu_signal(pc, info,
                             TRAP_sig(uc) == 0xe ? (ERROR_sig(uc) >> 1) & 1 : 0,
                             &MASK_sig(uc));
}

#elif defined(_ARCH_PPC)

/***********************************************************************
 * signal context platform-specific definitions
 * From Wine
 */
#ifdef linux
/* All Registers access - only for local access */
#define REG_sig(reg_name, context)              \
    ((context)->uc_mcontext.regs->reg_name)
/* Gpr Registers access  */
#define GPR_sig(reg_num, context)              REG_sig(gpr[reg_num], context)
/* Program counter */
#define IAR_sig(context)                       REG_sig(nip, context)
/* Machine State Register (Supervisor) */
#define MSR_sig(context)                       REG_sig(msr, context)
/* Count register */
#define CTR_sig(context)                       REG_sig(ctr, context)
/* User's integer exception register */
#define XER_sig(context)                       REG_sig(xer, context)
/* Link register */
#define LR_sig(context)                        REG_sig(link, context)
/* Condition register */
#define CR_sig(context)                        REG_sig(ccr, context)

/* Float Registers access  */
#define FLOAT_sig(reg_num, context)                                     \
    (((double *)((char *)((context)->uc_mcontext.regs + 48 * 4)))[reg_num])
#define FPSCR_sig(context) \
    (*(int *)((char *)((context)->uc_mcontext.regs + (48 + 32 * 2) * 4)))
/* Exception Registers access */
#define DAR_sig(context)                       REG_sig(dar, context)
#define DSISR_sig(context)                     REG_sig(dsisr, context)
#define TRAP_sig(context)                      REG_sig(trap, context)
#endif /* linux */

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
#include <ucontext.h>
#define IAR_sig(context)               ((context)->uc_mcontext.mc_srr0)
#define MSR_sig(context)               ((context)->uc_mcontext.mc_srr1)
#define CTR_sig(context)               ((context)->uc_mcontext.mc_ctr)
#define XER_sig(context)               ((context)->uc_mcontext.mc_xer)
#define LR_sig(context)                ((context)->uc_mcontext.mc_lr)
#define CR_sig(context)                ((context)->uc_mcontext.mc_cr)
/* Exception Registers access */
#define DAR_sig(context)               ((context)->uc_mcontext.mc_dar)
#define DSISR_sig(context)             ((context)->uc_mcontext.mc_dsisr)
#define TRAP_sig(context)              ((context)->uc_mcontext.mc_exc)
#endif /* __FreeBSD__|| __FreeBSD_kernel__ */

int cpu_signal_handler(int host_signum, void *pinfo,
                       void *puc)
{
    siginfo_t *info = pinfo;
#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
    ucontext_t *uc = puc;
#else
    ucontext_t *uc = puc;
#endif
    unsigned long pc;
    int is_write;

    pc = IAR_sig(uc);
    is_write = 0;
#if 0
    /* ppc 4xx case */
    if (DSISR_sig(uc) & 0x00800000) {
        is_write = 1;
    }
#else
    if (TRAP_sig(uc) != 0x400 && (DSISR_sig(uc) & 0x02000000)) {
        is_write = 1;
    }
#endif
    return handle_cpu_signal(pc, info, is_write, &uc->uc_sigmask);
}

#elif defined(__alpha__)

int cpu_signal_handler(int host_signum, void *pinfo,
                           void *puc)
{
    siginfo_t *info = pinfo;
    ucontext_t *uc = puc;
    uint32_t *pc = uc->uc_mcontext.sc_pc;
    uint32_t insn = *pc;
    int is_write = 0;

    /* XXX: need kernel patch to get write flag faster */
    switch (insn >> 26) {
    case 0x0d: /* stw */
    case 0x0e: /* stb */
    case 0x0f: /* stq_u */
    case 0x24: /* stf */
    case 0x25: /* stg */
    case 0x26: /* sts */
    case 0x27: /* stt */
    case 0x2c: /* stl */
    case 0x2d: /* stq */
    case 0x2e: /* stl_c */
    case 0x2f: /* stq_c */
        is_write = 1;
    }

    return handle_cpu_signal(pc, info, is_write, &uc->uc_sigmask);
}
#elif defined(__sparc__)

int cpu_signal_handler(int host_signum, void *pinfo,
                       void *puc)
{
    siginfo_t *info = pinfo;
    int is_write;
    uint32_t insn;
#if !defined(__arch64__) || defined(CONFIG_SOLARIS)
    uint32_t *regs = (uint32_t *)(info + 1);
    void *sigmask = (regs + 20);
    /* XXX: is there a standard glibc define ? */
    unsigned long pc = regs[1];
#else
#ifdef __linux__
    struct sigcontext *sc = puc;
    unsigned long pc = sc->sigc_regs.tpc;
    void *sigmask = (void *)sc->sigc_mask;
#elif defined(__OpenBSD__)
    struct sigcontext *uc = puc;
    unsigned long pc = uc->sc_pc;
    void *sigmask = (void *)(long)uc->sc_mask;
#elif defined(__NetBSD__)
    ucontext_t *uc = puc;
    unsigned long pc = _UC_MACHINE_PC(uc);
    void *sigmask = (void *)&uc->uc_sigmask;
#endif
#endif

    /* XXX: need kernel patch to get write flag faster */
    is_write = 0;
    insn = *(uint32_t *)pc;
    if ((insn >> 30) == 3) {
        switch ((insn >> 19) & 0x3f) {
        case 0x05: /* stb */
        case 0x15: /* stba */
        case 0x06: /* sth */
        case 0x16: /* stha */
        case 0x04: /* st */
        case 0x14: /* sta */
        case 0x07: /* std */
        case 0x17: /* stda */
        case 0x0e: /* stx */
        case 0x1e: /* stxa */
        case 0x24: /* stf */
        case 0x34: /* stfa */
        case 0x27: /* stdf */
        case 0x37: /* stdfa */
        case 0x26: /* stqf */
        case 0x36: /* stqfa */
        case 0x25: /* stfsr */
        case 0x3c: /* casa */
        case 0x3e: /* casxa */
            is_write = 1;
            break;
        }
    }
    return handle_cpu_signal(pc, info, is_write, sigmask);
}

#elif defined(__arm__)

#if defined(__NetBSD__)
#include <ucontext.h>
#endif

int cpu_signal_handler(int host_signum, void *pinfo,
                       void *puc)
{
    siginfo_t *info = pinfo;
#if defined(__NetBSD__)
    ucontext_t *uc = puc;
#else
    ucontext_t *uc = puc;
#endif
    unsigned long pc;
    int is_write;

#if defined(__NetBSD__)
    pc = uc->uc_mcontext.__gregs[_REG_R15];
#elif defined(__GLIBC__) && (__GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ <= 3))
    pc = uc->uc_mcontext.gregs[R15];
#else
    pc = uc->uc_mcontext.arm_pc;
#endif

    /* error_code is the FSR value, in which bit 11 is WnR (assuming a v6 or
     * later processor; on v5 we will always report this as a read).
     */
    is_write = extract32(uc->uc_mcontext.error_code, 11, 1);
    return handle_cpu_signal(pc, info, is_write, &uc->uc_sigmask);
}

#elif defined(__aarch64__)

int cpu_signal_handler(int host_signum, void *pinfo, void *puc)
{
    siginfo_t *info = pinfo;
    ucontext_t *uc = puc;
    uintptr_t pc = uc->uc_mcontext.pc;
    uint32_t insn = *(uint32_t *)pc;
    bool is_write;

    /* XXX: need kernel patch to get write flag faster.  */
    is_write = (   (insn & 0xbfff0000) == 0x0c000000   /* C3.3.1 */
                || (insn & 0xbfe00000) == 0x0c800000   /* C3.3.2 */
                || (insn & 0xbfdf0000) == 0x0d000000   /* C3.3.3 */
                || (insn & 0xbfc00000) == 0x0d800000   /* C3.3.4 */
                || (insn & 0x3f400000) == 0x08000000   /* C3.3.6 */
                || (insn & 0x3bc00000) == 0x39000000   /* C3.3.13 */
                || (insn & 0x3fc00000) == 0x3d800000   /* ... 128bit */
                /* Ingore bits 10, 11 & 21, controlling indexing.  */
                || (insn & 0x3bc00000) == 0x38000000   /* C3.3.8-12 */
                || (insn & 0x3fe00000) == 0x3c800000   /* ... 128bit */
                /* Ignore bits 23 & 24, controlling indexing.  */
                || (insn & 0x3a400000) == 0x28000000); /* C3.3.7,14-16 */

    return handle_cpu_signal(pc, info, is_write, &uc->uc_sigmask);
}

#elif defined(__s390__)

int cpu_signal_handler(int host_signum, void *pinfo,
                       void *puc)
{
    siginfo_t *info = pinfo;
    ucontext_t *uc = puc;
    unsigned long pc;
    uint16_t *pinsn;
    int is_write = 0;

    pc = uc->uc_mcontext.psw.addr;

    /* ??? On linux, the non-rt signal handler has 4 (!) arguments instead
       of the normal 2 arguments.  The 3rd argument contains the "int_code"
       from the hardware which does in fact contain the is_write value.
       The rt signal handler, as far as I can tell, does not give this value
       at all.  Not that we could get to it from here even if it were.  */
    /* ??? This is not even close to complete, since it ignores all
       of the read-modify-write instructions.  */
    pinsn = (uint16_t *)pc;
    switch (pinsn[0] >> 8) {
    case 0x50: /* ST */
    case 0x42: /* STC */
    case 0x40: /* STH */
        is_write = 1;
        break;
    case 0xc4: /* RIL format insns */
        switch (pinsn[0] & 0xf) {
        case 0xf: /* STRL */
        case 0xb: /* STGRL */
        case 0x7: /* STHRL */
            is_write = 1;
        }
        break;
    case 0xe3: /* RXY format insns */
        switch (pinsn[2] & 0xff) {
        case 0x50: /* STY */
        case 0x24: /* STG */
        case 0x72: /* STCY */
        case 0x70: /* STHY */
        case 0x8e: /* STPQ */
        case 0x3f: /* STRVH */
        case 0x3e: /* STRV */
        case 0x2f: /* STRVG */
            is_write = 1;
        }
        break;
    }
    return handle_cpu_signal(pc, info, is_write, &uc->uc_sigmask);
}

#elif defined(__mips__)

int cpu_signal_handler(int host_signum, void *pinfo,
                       void *puc)
{
    siginfo_t *info = pinfo;
    ucontext_t *uc = puc;
    greg_t pc = uc->uc_mcontext.pc;
    int is_write;

    /* XXX: compute is_write */
    is_write = 0;
    return handle_cpu_signal(pc, info, is_write, &uc->uc_sigmask);
}

#else

#error host CPU specific signal handler needed

#endif

/* The softmmu versions of these helpers are in cputlb.c.  */

/* Do not allow unaligned operations to proceed.  Return the host address.  */
static void *atomic_mmu_lookup(CPUArchState *env, target_ulong addr,
                               int size, uintptr_t retaddr)
{
    /* Enforce qemu required alignment.  */
    if (unlikely(addr & (size - 1))) {
        cpu_loop_exit_atomic(ENV_GET_CPU(env), retaddr);
    }
    helper_retaddr = retaddr;
    return g2h(addr);
}

/* Macro to call the above, with local variables from the use context.  */
#define ATOMIC_MMU_DECLS do {} while (0)
#define ATOMIC_MMU_LOOKUP  atomic_mmu_lookup(env, addr, DATA_SIZE, GETPC())
#define ATOMIC_MMU_CLEANUP do { helper_retaddr = 0; } while (0)

#define ATOMIC_NAME(X)   HELPER(glue(glue(atomic_ ## X, SUFFIX), END))
#define EXTRA_ARGS

#define DATA_SIZE 1
#include "atomic_template.h"

#define DATA_SIZE 2
#include "atomic_template.h"

#define DATA_SIZE 4
#include "atomic_template.h"

#ifdef CONFIG_ATOMIC64
#define DATA_SIZE 8
#include "atomic_template.h"
#endif

/* The following is only callable from other helpers, and matches up
   with the softmmu version.  */

#if HAVE_ATOMIC128 || HAVE_CMPXCHG128

#undef EXTRA_ARGS
#undef ATOMIC_NAME
#undef ATOMIC_MMU_LOOKUP

#define EXTRA_ARGS     , TCGMemOpIdx oi, uintptr_t retaddr
#define ATOMIC_NAME(X) \
    HELPER(glue(glue(glue(atomic_ ## X, SUFFIX), END), _mmu))
#define ATOMIC_MMU_LOOKUP  atomic_mmu_lookup(env, addr, DATA_SIZE, retaddr)

#define DATA_SIZE 16
#include "atomic_template.h"
#endif
