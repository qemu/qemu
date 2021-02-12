/*
 *  User emulator execution
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
#include "hw/core/tcg-cpu-ops.h"
#include "disas/disas.h"
#include "exec/exec-all.h"
#include "tcg/tcg.h"
#include "qemu/bitops.h"
#include "exec/cpu_ldst.h"
#include "exec/translate-all.h"
#include "exec/helper-proto.h"
#include "qemu/atomic128.h"
#include "trace/trace-root.h"
#include "trace/mem.h"

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
static void QEMU_NORETURN cpu_exit_tb_from_sighandler(CPUState *cpu,
                                                      sigset_t *old_set)
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
    unsigned long address = (unsigned long)info->si_addr;
    MMUAccessType access_type = is_write ? MMU_DATA_STORE : MMU_DATA_LOAD;

    switch (helper_retaddr) {
    default:
        /*
         * Fault during host memory operation within a helper function.
         * The helper's host return address, saved here, gives us a
         * pointer into the generated code that will unwind to the
         * correct guest pc.
         */
        pc = helper_retaddr;
        break;

    case 0:
        /*
         * Fault during host memory operation within generated code.
         * (Or, a unrelated bug within qemu, but we can't tell from here).
         *
         * We take the host pc from the signal frame.  However, we cannot
         * use that value directly.  Within cpu_restore_state_from_tb, we
         * assume PC comes from GETPC(), as used by the helper functions,
         * so we adjust the address by -GETPC_ADJ to form an address that
         * is within the call insn, so that the address does not accidentally
         * match the beginning of the next guest insn.  However, when the
         * pc comes from the signal frame it points to the actual faulting
         * host memory insn and not the return from a call insn.
         *
         * Therefore, adjust to compensate for what will be done later
         * by cpu_restore_state_from_tb.
         */
        pc += GETPC_ADJ;
        break;

    case 1:
        /*
         * Fault during host read for translation, or loosely, "execution".
         *
         * The guest pc is already pointing to the start of the TB for which
         * code is being generated.  If the guest translator manages the
         * page crossings correctly, this is exactly the correct address
         * (and if the translator doesn't handle page boundaries correctly
         * there's little we can do about that here).  Therefore, do not
         * trigger the unwinder.
         *
         * Like tb_gen_code, release the memory lock before cpu_loop_exit.
         */
        pc = 0;
        access_type = MMU_INST_FETCH;
        mmap_unlock();
        break;
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
            clear_helper_retaddr();
            cpu_exit_tb_from_sighandler(cpu, old_set);
            /* NORETURN */

        default:
            g_assert_not_reached();
        }
    }

    /* Convert forcefully to guest address space, invalid addresses
       are still valid segv ones */
    address = h2g_nocheck(address);

    /*
     * There is no way the target can handle this other than raising
     * an exception.  Undo signal and retaddr state prior to longjmp.
     */
    sigprocmask(SIG_SETMASK, old_set, NULL);
    clear_helper_retaddr();

    cc = CPU_GET_CLASS(cpu);
    cc->tcg_ops->tlb_fill(cpu, address, 0, access_type,
                          MMU_USER_IDX, false, pc);
    g_assert_not_reached();
}

static int probe_access_internal(CPUArchState *env, target_ulong addr,
                                 int fault_size, MMUAccessType access_type,
                                 bool nonfault, uintptr_t ra)
{
    int flags;

    switch (access_type) {
    case MMU_DATA_STORE:
        flags = PAGE_WRITE;
        break;
    case MMU_DATA_LOAD:
        flags = PAGE_READ;
        break;
    case MMU_INST_FETCH:
        flags = PAGE_EXEC;
        break;
    default:
        g_assert_not_reached();
    }

    if (!guest_addr_valid_untagged(addr) ||
        page_check_range(addr, 1, flags) < 0) {
        if (nonfault) {
            return TLB_INVALID_MASK;
        } else {
            CPUState *cpu = env_cpu(env);
            CPUClass *cc = CPU_GET_CLASS(cpu);
            cc->tcg_ops->tlb_fill(cpu, addr, fault_size, access_type,
                                  MMU_USER_IDX, false, ra);
            g_assert_not_reached();
        }
    }
    return 0;
}

int probe_access_flags(CPUArchState *env, target_ulong addr,
                       MMUAccessType access_type, int mmu_idx,
                       bool nonfault, void **phost, uintptr_t ra)
{
    int flags;

    flags = probe_access_internal(env, addr, 0, access_type, nonfault, ra);
    *phost = flags ? NULL : g2h(env_cpu(env), addr);
    return flags;
}

void *probe_access(CPUArchState *env, target_ulong addr, int size,
                   MMUAccessType access_type, int mmu_idx, uintptr_t ra)
{
    int flags;

    g_assert(-(addr | TARGET_PAGE_MASK) >= size);
    flags = probe_access_internal(env, addr, size, access_type, false, ra);
    g_assert(flags == 0);

    return size ? g2h(env_cpu(env), addr) : NULL;
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
#include <sys/siginfo.h>
#endif

int cpu_signal_handler(int host_signum, void *pinfo,
                       void *puc)
{
    siginfo_t *info = pinfo;
#if defined(__NetBSD__)
    ucontext_t *uc = puc;
    siginfo_t *si = pinfo;
#else
    ucontext_t *uc = puc;
#endif
    unsigned long pc;
    uint32_t fsr;
    int is_write;

#if defined(__NetBSD__)
    pc = uc->uc_mcontext.__gregs[_REG_R15];
#elif defined(__GLIBC__) && (__GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ <= 3))
    pc = uc->uc_mcontext.gregs[R15];
#else
    pc = uc->uc_mcontext.arm_pc;
#endif

#ifdef __NetBSD__
    fsr = si->si_trap;
#else
    fsr = uc->uc_mcontext.error_code;
#endif
    /*
     * In the FSR, bit 11 is WnR, assuming a v6 or
     * later processor.  On v5 we will always report
     * this as a read, which will fail later.
     */
    is_write = extract32(fsr, 11, 1);
    return handle_cpu_signal(pc, info, is_write, &uc->uc_sigmask);
}

#elif defined(__aarch64__)

#if defined(__NetBSD__)

#include <ucontext.h>
#include <sys/siginfo.h>

int cpu_signal_handler(int host_signum, void *pinfo, void *puc)
{
    ucontext_t *uc = puc;
    siginfo_t *si = pinfo;
    unsigned long pc;
    int is_write;
    uint32_t esr;

    pc = uc->uc_mcontext.__gregs[_REG_PC];
    esr = si->si_trap;

    /*
     * siginfo_t::si_trap is the ESR value, for data aborts ESR.EC
     * is 0b10010x: then bit 6 is the WnR bit
     */
    is_write = extract32(esr, 27, 5) == 0x12 && extract32(esr, 6, 1) == 1;
    return handle_cpu_signal(pc, si, is_write, &uc->uc_sigmask);
}

#else

#ifndef ESR_MAGIC
/* Pre-3.16 kernel headers don't have these, so provide fallback definitions */
#define ESR_MAGIC 0x45535201
struct esr_context {
    struct _aarch64_ctx head;
    uint64_t esr;
};
#endif

static inline struct _aarch64_ctx *first_ctx(ucontext_t *uc)
{
    return (struct _aarch64_ctx *)&uc->uc_mcontext.__reserved;
}

static inline struct _aarch64_ctx *next_ctx(struct _aarch64_ctx *hdr)
{
    return (struct _aarch64_ctx *)((char *)hdr + hdr->size);
}

int cpu_signal_handler(int host_signum, void *pinfo, void *puc)
{
    siginfo_t *info = pinfo;
    ucontext_t *uc = puc;
    uintptr_t pc = uc->uc_mcontext.pc;
    bool is_write;
    struct _aarch64_ctx *hdr;
    struct esr_context const *esrctx = NULL;

    /* Find the esr_context, which has the WnR bit in it */
    for (hdr = first_ctx(uc); hdr->magic; hdr = next_ctx(hdr)) {
        if (hdr->magic == ESR_MAGIC) {
            esrctx = (struct esr_context const *)hdr;
            break;
        }
    }

    if (esrctx) {
        /* For data aborts ESR.EC is 0b10010x: then bit 6 is the WnR bit */
        uint64_t esr = esrctx->esr;
        is_write = extract32(esr, 27, 5) == 0x12 && extract32(esr, 6, 1) == 1;
    } else {
        /*
         * Fall back to parsing instructions; will only be needed
         * for really ancient (pre-3.16) kernels.
         */
        uint32_t insn = *(uint32_t *)pc;

        is_write = ((insn & 0xbfff0000) == 0x0c000000   /* C3.3.1 */
                    || (insn & 0xbfe00000) == 0x0c800000   /* C3.3.2 */
                    || (insn & 0xbfdf0000) == 0x0d000000   /* C3.3.3 */
                    || (insn & 0xbfc00000) == 0x0d800000   /* C3.3.4 */
                    || (insn & 0x3f400000) == 0x08000000   /* C3.3.6 */
                    || (insn & 0x3bc00000) == 0x39000000   /* C3.3.13 */
                    || (insn & 0x3fc00000) == 0x3d800000   /* ... 128bit */
                    /* Ignore bits 10, 11 & 21, controlling indexing.  */
                    || (insn & 0x3bc00000) == 0x38000000   /* C3.3.8-12 */
                    || (insn & 0x3fe00000) == 0x3c800000   /* ... 128bit */
                    /* Ignore bits 23 & 24, controlling indexing.  */
                    || (insn & 0x3a400000) == 0x28000000); /* C3.3.7,14-16 */
    }
    return handle_cpu_signal(pc, info, is_write, &uc->uc_sigmask);
}
#endif

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

#if defined(__misp16) || defined(__mips_micromips)
#error "Unsupported encoding"
#endif

int cpu_signal_handler(int host_signum, void *pinfo,
                       void *puc)
{
    siginfo_t *info = pinfo;
    ucontext_t *uc = puc;
    uintptr_t pc = uc->uc_mcontext.pc;
    uint32_t insn = *(uint32_t *)pc;
    int is_write = 0;

    /* Detect all store instructions at program counter. */
    switch((insn >> 26) & 077) {
    case 050: /* SB */
    case 051: /* SH */
    case 052: /* SWL */
    case 053: /* SW */
    case 054: /* SDL */
    case 055: /* SDR */
    case 056: /* SWR */
    case 070: /* SC */
    case 071: /* SWC1 */
    case 074: /* SCD */
    case 075: /* SDC1 */
    case 077: /* SD */
#if !defined(__mips_isa_rev) || __mips_isa_rev < 6
    case 072: /* SWC2 */
    case 076: /* SDC2 */
#endif
        is_write = 1;
        break;
    case 023: /* COP1X */
        /* Required in all versions of MIPS64 since
           MIPS64r1 and subsequent versions of MIPS32r2. */
        switch (insn & 077) {
        case 010: /* SWXC1 */
        case 011: /* SDXC1 */
        case 015: /* SUXC1 */
            is_write = 1;
        }
        break;
    }

    return handle_cpu_signal(pc, info, is_write, &uc->uc_sigmask);
}

#elif defined(__riscv)

int cpu_signal_handler(int host_signum, void *pinfo,
                       void *puc)
{
    siginfo_t *info = pinfo;
    ucontext_t *uc = puc;
    greg_t pc = uc->uc_mcontext.__gregs[REG_PC];
    uint32_t insn = *(uint32_t *)pc;
    int is_write = 0;

    /* Detect store by reading the instruction at the program
       counter. Note: we currently only generate 32-bit
       instructions so we thus only detect 32-bit stores */
    switch (((insn >> 0) & 0b11)) {
    case 3:
        switch (((insn >> 2) & 0b11111)) {
        case 8:
            switch (((insn >> 12) & 0b111)) {
            case 0: /* sb */
            case 1: /* sh */
            case 2: /* sw */
            case 3: /* sd */
            case 4: /* sq */
                is_write = 1;
                break;
            default:
                break;
            }
            break;
        case 9:
            switch (((insn >> 12) & 0b111)) {
            case 2: /* fsw */
            case 3: /* fsd */
            case 4: /* fsq */
                is_write = 1;
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
    }

    /* Check for compressed instructions */
    switch (((insn >> 13) & 0b111)) {
    case 7:
        switch (insn & 0b11) {
        case 0: /*c.sd */
        case 2: /* c.sdsp */
            is_write = 1;
            break;
        default:
            break;
        }
        break;
    case 6:
        switch (insn & 0b11) {
        case 0: /* c.sw */
        case 3: /* c.swsp */
            is_write = 1;
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }

    return handle_cpu_signal(pc, info, is_write, &uc->uc_sigmask);
}

#else

#error host CPU specific signal handler needed

#endif

/* The softmmu versions of these helpers are in cputlb.c.  */

uint32_t cpu_ldub_data(CPUArchState *env, abi_ptr ptr)
{
    uint32_t ret;
    uint16_t meminfo = trace_mem_get_info(MO_UB, MMU_USER_IDX, false);

    trace_guest_mem_before_exec(env_cpu(env), ptr, meminfo);
    ret = ldub_p(g2h(env_cpu(env), ptr));
    qemu_plugin_vcpu_mem_cb(env_cpu(env), ptr, meminfo);
    return ret;
}

int cpu_ldsb_data(CPUArchState *env, abi_ptr ptr)
{
    int ret;
    uint16_t meminfo = trace_mem_get_info(MO_SB, MMU_USER_IDX, false);

    trace_guest_mem_before_exec(env_cpu(env), ptr, meminfo);
    ret = ldsb_p(g2h(env_cpu(env), ptr));
    qemu_plugin_vcpu_mem_cb(env_cpu(env), ptr, meminfo);
    return ret;
}

uint32_t cpu_lduw_be_data(CPUArchState *env, abi_ptr ptr)
{
    uint32_t ret;
    uint16_t meminfo = trace_mem_get_info(MO_BEUW, MMU_USER_IDX, false);

    trace_guest_mem_before_exec(env_cpu(env), ptr, meminfo);
    ret = lduw_be_p(g2h(env_cpu(env), ptr));
    qemu_plugin_vcpu_mem_cb(env_cpu(env), ptr, meminfo);
    return ret;
}

int cpu_ldsw_be_data(CPUArchState *env, abi_ptr ptr)
{
    int ret;
    uint16_t meminfo = trace_mem_get_info(MO_BESW, MMU_USER_IDX, false);

    trace_guest_mem_before_exec(env_cpu(env), ptr, meminfo);
    ret = ldsw_be_p(g2h(env_cpu(env), ptr));
    qemu_plugin_vcpu_mem_cb(env_cpu(env), ptr, meminfo);
    return ret;
}

uint32_t cpu_ldl_be_data(CPUArchState *env, abi_ptr ptr)
{
    uint32_t ret;
    uint16_t meminfo = trace_mem_get_info(MO_BEUL, MMU_USER_IDX, false);

    trace_guest_mem_before_exec(env_cpu(env), ptr, meminfo);
    ret = ldl_be_p(g2h(env_cpu(env), ptr));
    qemu_plugin_vcpu_mem_cb(env_cpu(env), ptr, meminfo);
    return ret;
}

uint64_t cpu_ldq_be_data(CPUArchState *env, abi_ptr ptr)
{
    uint64_t ret;
    uint16_t meminfo = trace_mem_get_info(MO_BEQ, MMU_USER_IDX, false);

    trace_guest_mem_before_exec(env_cpu(env), ptr, meminfo);
    ret = ldq_be_p(g2h(env_cpu(env), ptr));
    qemu_plugin_vcpu_mem_cb(env_cpu(env), ptr, meminfo);
    return ret;
}

uint32_t cpu_lduw_le_data(CPUArchState *env, abi_ptr ptr)
{
    uint32_t ret;
    uint16_t meminfo = trace_mem_get_info(MO_LEUW, MMU_USER_IDX, false);

    trace_guest_mem_before_exec(env_cpu(env), ptr, meminfo);
    ret = lduw_le_p(g2h(env_cpu(env), ptr));
    qemu_plugin_vcpu_mem_cb(env_cpu(env), ptr, meminfo);
    return ret;
}

int cpu_ldsw_le_data(CPUArchState *env, abi_ptr ptr)
{
    int ret;
    uint16_t meminfo = trace_mem_get_info(MO_LESW, MMU_USER_IDX, false);

    trace_guest_mem_before_exec(env_cpu(env), ptr, meminfo);
    ret = ldsw_le_p(g2h(env_cpu(env), ptr));
    qemu_plugin_vcpu_mem_cb(env_cpu(env), ptr, meminfo);
    return ret;
}

uint32_t cpu_ldl_le_data(CPUArchState *env, abi_ptr ptr)
{
    uint32_t ret;
    uint16_t meminfo = trace_mem_get_info(MO_LEUL, MMU_USER_IDX, false);

    trace_guest_mem_before_exec(env_cpu(env), ptr, meminfo);
    ret = ldl_le_p(g2h(env_cpu(env), ptr));
    qemu_plugin_vcpu_mem_cb(env_cpu(env), ptr, meminfo);
    return ret;
}

uint64_t cpu_ldq_le_data(CPUArchState *env, abi_ptr ptr)
{
    uint64_t ret;
    uint16_t meminfo = trace_mem_get_info(MO_LEQ, MMU_USER_IDX, false);

    trace_guest_mem_before_exec(env_cpu(env), ptr, meminfo);
    ret = ldq_le_p(g2h(env_cpu(env), ptr));
    qemu_plugin_vcpu_mem_cb(env_cpu(env), ptr, meminfo);
    return ret;
}

uint32_t cpu_ldub_data_ra(CPUArchState *env, abi_ptr ptr, uintptr_t retaddr)
{
    uint32_t ret;

    set_helper_retaddr(retaddr);
    ret = cpu_ldub_data(env, ptr);
    clear_helper_retaddr();
    return ret;
}

int cpu_ldsb_data_ra(CPUArchState *env, abi_ptr ptr, uintptr_t retaddr)
{
    int ret;

    set_helper_retaddr(retaddr);
    ret = cpu_ldsb_data(env, ptr);
    clear_helper_retaddr();
    return ret;
}

uint32_t cpu_lduw_be_data_ra(CPUArchState *env, abi_ptr ptr, uintptr_t retaddr)
{
    uint32_t ret;

    set_helper_retaddr(retaddr);
    ret = cpu_lduw_be_data(env, ptr);
    clear_helper_retaddr();
    return ret;
}

int cpu_ldsw_be_data_ra(CPUArchState *env, abi_ptr ptr, uintptr_t retaddr)
{
    int ret;

    set_helper_retaddr(retaddr);
    ret = cpu_ldsw_be_data(env, ptr);
    clear_helper_retaddr();
    return ret;
}

uint32_t cpu_ldl_be_data_ra(CPUArchState *env, abi_ptr ptr, uintptr_t retaddr)
{
    uint32_t ret;

    set_helper_retaddr(retaddr);
    ret = cpu_ldl_be_data(env, ptr);
    clear_helper_retaddr();
    return ret;
}

uint64_t cpu_ldq_be_data_ra(CPUArchState *env, abi_ptr ptr, uintptr_t retaddr)
{
    uint64_t ret;

    set_helper_retaddr(retaddr);
    ret = cpu_ldq_be_data(env, ptr);
    clear_helper_retaddr();
    return ret;
}

uint32_t cpu_lduw_le_data_ra(CPUArchState *env, abi_ptr ptr, uintptr_t retaddr)
{
    uint32_t ret;

    set_helper_retaddr(retaddr);
    ret = cpu_lduw_le_data(env, ptr);
    clear_helper_retaddr();
    return ret;
}

int cpu_ldsw_le_data_ra(CPUArchState *env, abi_ptr ptr, uintptr_t retaddr)
{
    int ret;

    set_helper_retaddr(retaddr);
    ret = cpu_ldsw_le_data(env, ptr);
    clear_helper_retaddr();
    return ret;
}

uint32_t cpu_ldl_le_data_ra(CPUArchState *env, abi_ptr ptr, uintptr_t retaddr)
{
    uint32_t ret;

    set_helper_retaddr(retaddr);
    ret = cpu_ldl_le_data(env, ptr);
    clear_helper_retaddr();
    return ret;
}

uint64_t cpu_ldq_le_data_ra(CPUArchState *env, abi_ptr ptr, uintptr_t retaddr)
{
    uint64_t ret;

    set_helper_retaddr(retaddr);
    ret = cpu_ldq_le_data(env, ptr);
    clear_helper_retaddr();
    return ret;
}

void cpu_stb_data(CPUArchState *env, abi_ptr ptr, uint32_t val)
{
    uint16_t meminfo = trace_mem_get_info(MO_UB, MMU_USER_IDX, true);

    trace_guest_mem_before_exec(env_cpu(env), ptr, meminfo);
    stb_p(g2h(env_cpu(env), ptr), val);
    qemu_plugin_vcpu_mem_cb(env_cpu(env), ptr, meminfo);
}

void cpu_stw_be_data(CPUArchState *env, abi_ptr ptr, uint32_t val)
{
    uint16_t meminfo = trace_mem_get_info(MO_BEUW, MMU_USER_IDX, true);

    trace_guest_mem_before_exec(env_cpu(env), ptr, meminfo);
    stw_be_p(g2h(env_cpu(env), ptr), val);
    qemu_plugin_vcpu_mem_cb(env_cpu(env), ptr, meminfo);
}

void cpu_stl_be_data(CPUArchState *env, abi_ptr ptr, uint32_t val)
{
    uint16_t meminfo = trace_mem_get_info(MO_BEUL, MMU_USER_IDX, true);

    trace_guest_mem_before_exec(env_cpu(env), ptr, meminfo);
    stl_be_p(g2h(env_cpu(env), ptr), val);
    qemu_plugin_vcpu_mem_cb(env_cpu(env), ptr, meminfo);
}

void cpu_stq_be_data(CPUArchState *env, abi_ptr ptr, uint64_t val)
{
    uint16_t meminfo = trace_mem_get_info(MO_BEQ, MMU_USER_IDX, true);

    trace_guest_mem_before_exec(env_cpu(env), ptr, meminfo);
    stq_be_p(g2h(env_cpu(env), ptr), val);
    qemu_plugin_vcpu_mem_cb(env_cpu(env), ptr, meminfo);
}

void cpu_stw_le_data(CPUArchState *env, abi_ptr ptr, uint32_t val)
{
    uint16_t meminfo = trace_mem_get_info(MO_LEUW, MMU_USER_IDX, true);

    trace_guest_mem_before_exec(env_cpu(env), ptr, meminfo);
    stw_le_p(g2h(env_cpu(env), ptr), val);
    qemu_plugin_vcpu_mem_cb(env_cpu(env), ptr, meminfo);
}

void cpu_stl_le_data(CPUArchState *env, abi_ptr ptr, uint32_t val)
{
    uint16_t meminfo = trace_mem_get_info(MO_LEUL, MMU_USER_IDX, true);

    trace_guest_mem_before_exec(env_cpu(env), ptr, meminfo);
    stl_le_p(g2h(env_cpu(env), ptr), val);
    qemu_plugin_vcpu_mem_cb(env_cpu(env), ptr, meminfo);
}

void cpu_stq_le_data(CPUArchState *env, abi_ptr ptr, uint64_t val)
{
    uint16_t meminfo = trace_mem_get_info(MO_LEQ, MMU_USER_IDX, true);

    trace_guest_mem_before_exec(env_cpu(env), ptr, meminfo);
    stq_le_p(g2h(env_cpu(env), ptr), val);
    qemu_plugin_vcpu_mem_cb(env_cpu(env), ptr, meminfo);
}

void cpu_stb_data_ra(CPUArchState *env, abi_ptr ptr,
                     uint32_t val, uintptr_t retaddr)
{
    set_helper_retaddr(retaddr);
    cpu_stb_data(env, ptr, val);
    clear_helper_retaddr();
}

void cpu_stw_be_data_ra(CPUArchState *env, abi_ptr ptr,
                        uint32_t val, uintptr_t retaddr)
{
    set_helper_retaddr(retaddr);
    cpu_stw_be_data(env, ptr, val);
    clear_helper_retaddr();
}

void cpu_stl_be_data_ra(CPUArchState *env, abi_ptr ptr,
                        uint32_t val, uintptr_t retaddr)
{
    set_helper_retaddr(retaddr);
    cpu_stl_be_data(env, ptr, val);
    clear_helper_retaddr();
}

void cpu_stq_be_data_ra(CPUArchState *env, abi_ptr ptr,
                        uint64_t val, uintptr_t retaddr)
{
    set_helper_retaddr(retaddr);
    cpu_stq_be_data(env, ptr, val);
    clear_helper_retaddr();
}

void cpu_stw_le_data_ra(CPUArchState *env, abi_ptr ptr,
                        uint32_t val, uintptr_t retaddr)
{
    set_helper_retaddr(retaddr);
    cpu_stw_le_data(env, ptr, val);
    clear_helper_retaddr();
}

void cpu_stl_le_data_ra(CPUArchState *env, abi_ptr ptr,
                        uint32_t val, uintptr_t retaddr)
{
    set_helper_retaddr(retaddr);
    cpu_stl_le_data(env, ptr, val);
    clear_helper_retaddr();
}

void cpu_stq_le_data_ra(CPUArchState *env, abi_ptr ptr,
                        uint64_t val, uintptr_t retaddr)
{
    set_helper_retaddr(retaddr);
    cpu_stq_le_data(env, ptr, val);
    clear_helper_retaddr();
}

uint32_t cpu_ldub_code(CPUArchState *env, abi_ptr ptr)
{
    uint32_t ret;

    set_helper_retaddr(1);
    ret = ldub_p(g2h_untagged(ptr));
    clear_helper_retaddr();
    return ret;
}

uint32_t cpu_lduw_code(CPUArchState *env, abi_ptr ptr)
{
    uint32_t ret;

    set_helper_retaddr(1);
    ret = lduw_p(g2h_untagged(ptr));
    clear_helper_retaddr();
    return ret;
}

uint32_t cpu_ldl_code(CPUArchState *env, abi_ptr ptr)
{
    uint32_t ret;

    set_helper_retaddr(1);
    ret = ldl_p(g2h_untagged(ptr));
    clear_helper_retaddr();
    return ret;
}

uint64_t cpu_ldq_code(CPUArchState *env, abi_ptr ptr)
{
    uint64_t ret;

    set_helper_retaddr(1);
    ret = ldq_p(g2h_untagged(ptr));
    clear_helper_retaddr();
    return ret;
}

/* Do not allow unaligned operations to proceed.  Return the host address.  */
static void *atomic_mmu_lookup(CPUArchState *env, target_ulong addr,
                               int size, uintptr_t retaddr)
{
    /* Enforce qemu required alignment.  */
    if (unlikely(addr & (size - 1))) {
        cpu_loop_exit_atomic(env_cpu(env), retaddr);
    }
    void *ret = g2h(env_cpu(env), addr);
    set_helper_retaddr(retaddr);
    return ret;
}

/* Macro to call the above, with local variables from the use context.  */
#define ATOMIC_MMU_DECLS do {} while (0)
#define ATOMIC_MMU_LOOKUP  atomic_mmu_lookup(env, addr, DATA_SIZE, GETPC())
#define ATOMIC_MMU_CLEANUP do { clear_helper_retaddr(); } while (0)
#define ATOMIC_MMU_IDX MMU_USER_IDX

#define ATOMIC_NAME(X)   HELPER(glue(glue(atomic_ ## X, SUFFIX), END))
#define EXTRA_ARGS

#include "atomic_common.c.inc"

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
