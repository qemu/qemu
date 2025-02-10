/*
 *  Emulation of Linux signals
 *
 *  Copyright (c) 2003 Fabrice Bellard
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
#include "signal-common.h"
#include "linux-user/trace.h"
#include "user/tswap-target.h"

/* from the Linux kernel - /arch/x86/include/uapi/asm/sigcontext.h */

#define TARGET_FP_XSTATE_MAGIC1         0x46505853U /* FPXS */
#define TARGET_FP_XSTATE_MAGIC2         0x46505845U /* FPXE */
#define TARGET_FP_XSTATE_MAGIC2_SIZE    4

struct target_fpreg {
    uint16_t significand[4];
    uint16_t exponent;
};

/* Legacy x87 fpu state format for FSAVE/FRESTOR. */
struct target_fregs_state {
    uint32_t cwd;
    uint32_t swd;
    uint32_t twd;
    uint32_t fip;
    uint32_t fcs;
    uint32_t foo;
    uint32_t fos;
    struct target_fpreg st[8];

    /* Software status information [not touched by FSAVE]. */
    uint16_t status;
    uint16_t magic;   /* 0xffff: FPU data only, 0x0000: FXSR FPU data */
};
QEMU_BUILD_BUG_ON(sizeof(struct target_fregs_state) != 32 + 80);

struct target_fpx_sw_bytes {
    uint32_t magic1;
    uint32_t extended_size;
    uint64_t xfeatures;
    uint32_t xstate_size;
    uint32_t reserved[7];
};
QEMU_BUILD_BUG_ON(sizeof(struct target_fpx_sw_bytes) != 12*4);

struct target_fpstate_32 {
    struct target_fregs_state fpstate;
    X86LegacyXSaveArea fxstate;
};

struct target_sigcontext_32 {
    uint16_t gs, __gsh;
    uint16_t fs, __fsh;
    uint16_t es, __esh;
    uint16_t ds, __dsh;
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t trapno;
    uint32_t err;
    uint32_t eip;
    uint16_t cs, __csh;
    uint32_t eflags;
    uint32_t esp_at_signal;
    uint16_t ss, __ssh;
    uint32_t fpstate; /* pointer */
    uint32_t oldmask;
    uint32_t cr2;
};

struct target_sigcontext_64 {
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;

    uint64_t rdi;
    uint64_t rsi;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t rdx;
    uint64_t rax;
    uint64_t rcx;
    uint64_t rsp;
    uint64_t rip;

    uint64_t eflags;

    uint16_t cs;
    uint16_t gs;
    uint16_t fs;
    uint16_t ss;

    uint64_t err;
    uint64_t trapno;
    uint64_t oldmask;
    uint64_t cr2;

    uint64_t fpstate; /* pointer */
    uint64_t padding[8];
};

#ifndef TARGET_X86_64
# define target_sigcontext target_sigcontext_32
#else
# define target_sigcontext target_sigcontext_64
#endif

/* see Linux/include/uapi/asm-generic/ucontext.h */
struct target_ucontext {
    abi_ulong         tuc_flags;
    abi_ulong         tuc_link;
    target_stack_t    tuc_stack;
    struct target_sigcontext tuc_mcontext;
    target_sigset_t   tuc_sigmask;  /* mask last for extensibility */
};

#ifndef TARGET_X86_64
struct sigframe {
    abi_ulong pretcode;
    int sig;
    struct target_sigcontext sc;
    /*
     * The actual fpstate is placed after retcode[] below, to make room
     * for the variable-sized xsave data.  The older unused fpstate has
     * to be kept to avoid changing the offset of extramask[], which
     * is part of the ABI.
     */
    struct target_fpstate_32 fpstate_unused;
    abi_ulong extramask[TARGET_NSIG_WORDS-1];
    char retcode[8];
    /* fp state follows here */
};

struct rt_sigframe {
    abi_ulong pretcode;
    int sig;
    abi_ulong pinfo;
    abi_ulong puc;
    struct target_siginfo info;
    struct target_ucontext uc;
    char retcode[8];
    /* fp state follows here */
};

/*
 * Verify that vdso-asmoffset.h constants match.
 */
#include "i386/vdso-asmoffset.h"

QEMU_BUILD_BUG_ON(offsetof(struct sigframe, sc.eip)
                  != SIGFRAME_SIGCONTEXT_eip);
QEMU_BUILD_BUG_ON(offsetof(struct rt_sigframe, uc.tuc_mcontext.eip)
                  != RT_SIGFRAME_SIGCONTEXT_eip);

#else

struct rt_sigframe {
    abi_ulong pretcode;
    struct target_ucontext uc;
    struct target_siginfo info;
    /* fp state follows here */
};
#endif

typedef enum {
#ifndef TARGET_X86_64
    FPSTATE_FSAVE,
#endif
    FPSTATE_FXSAVE,
    FPSTATE_XSAVE
} FPStateKind;

static FPStateKind get_fpstate_kind(CPUX86State *env)
{
    if (env->features[FEAT_1_ECX] & CPUID_EXT_XSAVE) {
        return FPSTATE_XSAVE;
    }
#ifdef TARGET_X86_64
    return FPSTATE_FXSAVE;
#else
    if (env->features[FEAT_1_EDX] & CPUID_FXSR) {
        return FPSTATE_FXSAVE;
    }
    return FPSTATE_FSAVE;
#endif
}

static unsigned get_fpstate_size(CPUX86State *env, FPStateKind fpkind)
{
    /*
     * Kernel:
     *   fpu__alloc_mathframe
     *     xstate_sigframe_size(current->thread.fpu.fpstate);
     *       size = fpstate->user_size
     *       use_xsave() ? size + FP_XSTATE_MAGIC2_SIZE : size
     *   where fpstate->user_size is computed at init in
     *   fpu__init_system_xstate_size_legacy and
     *   fpu__init_system_xstate.
     *
     * Here we have no place to pre-compute, so inline it all.
     */
    switch (fpkind) {
    case FPSTATE_XSAVE:
        return (xsave_area_size(env->xcr0, false)
                + TARGET_FP_XSTATE_MAGIC2_SIZE);
    case FPSTATE_FXSAVE:
        return sizeof(X86LegacyXSaveArea);
#ifndef TARGET_X86_64
    case FPSTATE_FSAVE:
        return sizeof(struct target_fregs_state);
#endif
    }
    g_assert_not_reached();
}

static abi_ptr get_sigframe(struct target_sigaction *ka, CPUX86State *env,
                            unsigned frame_size, FPStateKind fpkind,
                            abi_ptr *fpstate, abi_ptr *fxstate, abi_ptr *fpend)
{
    abi_ptr sp;
    unsigned math_size;

    /* Default to using normal stack */
    sp = get_sp_from_cpustate(env);
#ifdef TARGET_X86_64
    sp -= 128; /* this is the redzone */
#endif

    /* This is the X/Open sanctioned signal stack switching.  */
    if (ka->sa_flags & TARGET_SA_ONSTACK) {
        sp = target_sigsp(sp, ka);
    } else {
#ifndef TARGET_X86_64
        /* This is the legacy signal stack switching. */
        if ((env->segs[R_SS].selector & 0xffff) != __USER_DS
            && !(ka->sa_flags & TARGET_SA_RESTORER)
            && ka->sa_restorer) {
            sp = ka->sa_restorer;
        }
#endif
    }

    math_size = get_fpstate_size(env, fpkind);
    sp = ROUND_DOWN(sp - math_size, 64);
    *fpend = sp + math_size;
    *fxstate = sp;
#ifndef TARGET_X86_64
    if (fpkind != FPSTATE_FSAVE) {
        sp -= sizeof(struct target_fregs_state);
    }
#endif
    *fpstate = sp;

    sp -= frame_size;
    /*
     * Align the stack pointer according to the ABI, i.e. so that on
     * function entry ((sp + sizeof(return_addr)) & 15) == 0.
     */
    sp += sizeof(target_ulong);
    sp = ROUND_DOWN(sp, 16);
    sp -= sizeof(target_ulong);

    return sp;
}

/*
 * Set up a signal frame.
 */

static void fxsave_sigcontext(CPUX86State *env, X86LegacyXSaveArea *fxstate)
{
    struct target_fpx_sw_bytes *sw = (void *)&fxstate->sw_reserved;

    cpu_x86_fxsave(env, fxstate, sizeof(*fxstate));
    __put_user(0, &sw->magic1);
}

static void xsave_sigcontext(CPUX86State *env,
                             X86LegacyXSaveArea *fxstate,
                             abi_ptr fpstate_addr,
                             abi_ptr xstate_addr,
                             abi_ptr fpend_addr)
{
    struct target_fpx_sw_bytes *sw = (void *)&fxstate->sw_reserved;
    /*
     * extended_size is the offset from fpstate_addr to right after
     * the end of the extended save states.  On 32-bit that includes
     * the legacy FSAVE area.
     */
    uint32_t extended_size = fpend_addr - fpstate_addr;
    /* Recover xstate_size by removing magic2. */
    uint32_t xstate_size = (fpend_addr - xstate_addr
                            - TARGET_FP_XSTATE_MAGIC2_SIZE);
    /* magic2 goes just after xstate. */
    uint32_t *magic2 = (void *)fxstate + xstate_size;

    /* xstate_addr must be 64 byte aligned for xsave */
    assert(!(xstate_addr & 0x3f));

    /* Zero the header, XSAVE *adds* features to an existing save state.  */
    memset(fxstate + 1, 0, sizeof(X86XSaveHeader));
    cpu_x86_xsave(env, fxstate, fpend_addr - xstate_addr, env->xcr0);

    __put_user(TARGET_FP_XSTATE_MAGIC1, &sw->magic1);
    __put_user(extended_size, &sw->extended_size);
    __put_user(env->xcr0, &sw->xfeatures);
    __put_user(xstate_size, &sw->xstate_size);
    __put_user(TARGET_FP_XSTATE_MAGIC2, magic2);
}

static void setup_sigcontext(CPUX86State *env,
                             struct target_sigcontext *sc,
                             abi_ulong mask, FPStateKind fpkind,
                             struct target_fregs_state *fpstate,
                             abi_ptr fpstate_addr,
                             X86LegacyXSaveArea *fxstate,
                             abi_ptr fxstate_addr,
                             abi_ptr fpend_addr)
{
    CPUState *cs = env_cpu(env);

#ifndef TARGET_X86_64
    uint16_t magic;

    /* already locked in setup_frame() */
    __put_user(env->segs[R_GS].selector, (uint32_t *)&sc->gs);
    __put_user(env->segs[R_FS].selector, (uint32_t *)&sc->fs);
    __put_user(env->segs[R_ES].selector, (uint32_t *)&sc->es);
    __put_user(env->segs[R_DS].selector, (uint32_t *)&sc->ds);
    __put_user(env->regs[R_EDI], &sc->edi);
    __put_user(env->regs[R_ESI], &sc->esi);
    __put_user(env->regs[R_EBP], &sc->ebp);
    __put_user(env->regs[R_ESP], &sc->esp);
    __put_user(env->regs[R_EBX], &sc->ebx);
    __put_user(env->regs[R_EDX], &sc->edx);
    __put_user(env->regs[R_ECX], &sc->ecx);
    __put_user(env->regs[R_EAX], &sc->eax);
    __put_user(cs->exception_index, &sc->trapno);
    __put_user(env->error_code, &sc->err);
    __put_user(env->eip, &sc->eip);
    __put_user(env->segs[R_CS].selector, (uint32_t *)&sc->cs);
    __put_user(env->eflags, &sc->eflags);
    __put_user(env->regs[R_ESP], &sc->esp_at_signal);
    __put_user(env->segs[R_SS].selector, (uint32_t *)&sc->ss);

    cpu_x86_fsave(env, fpstate, sizeof(*fpstate));
    fpstate->status = fpstate->swd;
    magic = (fpkind == FPSTATE_FSAVE ? 0 : 0xffff);
    __put_user(magic, &fpstate->magic);
#else
    __put_user(env->regs[R_EDI], &sc->rdi);
    __put_user(env->regs[R_ESI], &sc->rsi);
    __put_user(env->regs[R_EBP], &sc->rbp);
    __put_user(env->regs[R_ESP], &sc->rsp);
    __put_user(env->regs[R_EBX], &sc->rbx);
    __put_user(env->regs[R_EDX], &sc->rdx);
    __put_user(env->regs[R_ECX], &sc->rcx);
    __put_user(env->regs[R_EAX], &sc->rax);

    __put_user(env->regs[8], &sc->r8);
    __put_user(env->regs[9], &sc->r9);
    __put_user(env->regs[10], &sc->r10);
    __put_user(env->regs[11], &sc->r11);
    __put_user(env->regs[12], &sc->r12);
    __put_user(env->regs[13], &sc->r13);
    __put_user(env->regs[14], &sc->r14);
    __put_user(env->regs[15], &sc->r15);

    __put_user(cs->exception_index, &sc->trapno);
    __put_user(env->error_code, &sc->err);
    __put_user(env->eip, &sc->rip);

    __put_user(env->eflags, &sc->eflags);
    __put_user(env->segs[R_CS].selector, &sc->cs);
    __put_user((uint16_t)0, &sc->gs);
    __put_user((uint16_t)0, &sc->fs);
    __put_user(env->segs[R_SS].selector, &sc->ss);
#endif

    switch (fpkind) {
    case FPSTATE_XSAVE:
        xsave_sigcontext(env, fxstate, fpstate_addr, fxstate_addr, fpend_addr);
        break;
    case FPSTATE_FXSAVE:
        fxsave_sigcontext(env, fxstate);
        break;
    default:
        break;
    }

    __put_user(fpstate_addr, &sc->fpstate);
    /* non-iBCS2 extensions.. */
    __put_user(mask, &sc->oldmask);
    __put_user(env->cr[2], &sc->cr2);
}

#ifndef TARGET_X86_64
static void install_sigtramp(void *tramp)
{
    /* This is popl %eax ; movl $syscall,%eax ; int $0x80 */
    __put_user(0xb858, (uint16_t *)(tramp + 0));
    __put_user(TARGET_NR_sigreturn, (int32_t *)(tramp + 2));
    __put_user(0x80cd, (uint16_t *)(tramp + 6));
}

static void install_rt_sigtramp(void *tramp)
{
    /* This is movl $syscall,%eax ; int $0x80 */
    __put_user(0xb8, (uint8_t *)(tramp + 0));
    __put_user(TARGET_NR_rt_sigreturn, (int32_t *)(tramp + 1));
    __put_user(0x80cd, (uint16_t *)(tramp + 5));
}

/* compare linux/arch/i386/kernel/signal.c:setup_frame() */
void setup_frame(int sig, struct target_sigaction *ka,
                 target_sigset_t *set, CPUX86State *env)
{
    abi_ptr frame_addr, fpstate_addr, fxstate_addr, fpend_addr;
    struct sigframe *frame;
    struct target_fregs_state *fpstate;
    X86LegacyXSaveArea *fxstate;
    unsigned total_size;
    FPStateKind fpkind;

    fpkind = get_fpstate_kind(env);
    frame_addr = get_sigframe(ka, env, sizeof(struct sigframe), fpkind,
                              &fpstate_addr, &fxstate_addr, &fpend_addr);
    trace_user_setup_frame(env, frame_addr);

    total_size = fpend_addr - frame_addr;
    frame = lock_user(VERIFY_WRITE, frame_addr, total_size, 0);
    if (!frame) {
        force_sigsegv(sig);
        return;
    }

    fxstate = (void *)frame + (fxstate_addr - frame_addr);
#ifdef TARGET_X86_64
    fpstate = NULL;
#else
    fpstate = (void *)frame + (fpstate_addr - frame_addr);
#endif

    setup_sigcontext(env, &frame->sc, set->sig[0], fpkind,
                     fpstate, fpstate_addr, fxstate, fxstate_addr, fpend_addr);

    for (int i = 1; i < TARGET_NSIG_WORDS; i++) {
        __put_user(set->sig[i], &frame->extramask[i - 1]);
    }

    /* Set up to return from userspace.  If provided, use a stub
       already in userspace.  */
    if (ka->sa_flags & TARGET_SA_RESTORER) {
        __put_user(ka->sa_restorer, &frame->pretcode);
    } else {
        /* This is no longer used, but is retained for ABI compatibility. */
        install_sigtramp(frame->retcode);
        __put_user(default_sigreturn, &frame->pretcode);
    }
    unlock_user(frame, frame_addr, total_size);

    /* Set up registers for signal handler */
    env->regs[R_ESP] = frame_addr;
    env->eip = ka->_sa_handler;

    /* Store argument for both -mregparm=3 and standard. */
    env->regs[R_EAX] = sig;
    __put_user(sig, &frame->sig);
    /* The kernel clears EDX and ECX even though there is only one arg. */
    env->regs[R_EDX] = 0;
    env->regs[R_ECX] = 0;

    cpu_x86_load_seg(env, R_DS, __USER_DS);
    cpu_x86_load_seg(env, R_ES, __USER_DS);
    cpu_x86_load_seg(env, R_SS, __USER_DS);
    cpu_x86_load_seg(env, R_CS, __USER_CS);
    env->eflags &= ~TF_MASK;
}
#endif

/* compare linux/arch/x86/kernel/signal.c:setup_rt_frame() */
void setup_rt_frame(int sig, struct target_sigaction *ka,
                    target_siginfo_t *info,
                    target_sigset_t *set, CPUX86State *env)
{
    abi_ptr frame_addr, fpstate_addr, fxstate_addr, fpend_addr;
    struct rt_sigframe *frame;
    X86LegacyXSaveArea *fxstate;
    struct target_fregs_state *fpstate;
    unsigned total_size;
    FPStateKind fpkind;

    fpkind = get_fpstate_kind(env);
    frame_addr = get_sigframe(ka, env, sizeof(struct rt_sigframe), fpkind,
                              &fpstate_addr, &fxstate_addr, &fpend_addr);
    trace_user_setup_rt_frame(env, frame_addr);

    total_size = fpend_addr - frame_addr;
    frame = lock_user(VERIFY_WRITE, frame_addr, total_size, 0);
    if (!frame) {
        goto give_sigsegv;
    }

    if (ka->sa_flags & TARGET_SA_SIGINFO) {
        frame->info = *info;
    }

    /* Create the ucontext.  */
    __put_user(fpkind == FPSTATE_XSAVE, &frame->uc.tuc_flags);
    __put_user(0, &frame->uc.tuc_link);
    target_save_altstack(&frame->uc.tuc_stack, env);

    fxstate = (void *)frame + (fxstate_addr - frame_addr);
#ifdef TARGET_X86_64
    fpstate = NULL;
#else
    fpstate = (void *)frame + (fpstate_addr - frame_addr);
#endif

    setup_sigcontext(env, &frame->uc.tuc_mcontext, set->sig[0], fpkind,
                     fpstate, fpstate_addr, fxstate, fxstate_addr, fpend_addr);

    for (int i = 0; i < TARGET_NSIG_WORDS; i++) {
        __put_user(set->sig[i], &frame->uc.tuc_sigmask.sig[i]);
    }

    /*
     * Set up to return from userspace.  If provided, use a stub
     * already in userspace.
     */
    if (ka->sa_flags & TARGET_SA_RESTORER) {
        __put_user(ka->sa_restorer, &frame->pretcode);
    } else {
#ifdef TARGET_X86_64
        /* For x86_64, SA_RESTORER is required ABI.  */
        goto give_sigsegv;
#else
        /* This is no longer used, but is retained for ABI compatibility. */
        install_rt_sigtramp(frame->retcode);
        __put_user(default_rt_sigreturn, &frame->pretcode);
#endif
    }

    /* Set up registers for signal handler */
    env->regs[R_ESP] = frame_addr;
    env->eip = ka->_sa_handler;

#ifndef TARGET_X86_64
    /* Store arguments for both -mregparm=3 and standard. */
    env->regs[R_EAX] = sig;
    __put_user(sig, &frame->sig);
    env->regs[R_EDX] = frame_addr + offsetof(struct rt_sigframe, info);
    __put_user(env->regs[R_EDX], &frame->pinfo);
    env->regs[R_ECX] = frame_addr + offsetof(struct rt_sigframe, uc);
    __put_user(env->regs[R_ECX], &frame->puc);
#else
    env->regs[R_EAX] = 0;
    env->regs[R_EDI] = sig;
    env->regs[R_ESI] = frame_addr + offsetof(struct rt_sigframe, info);
    env->regs[R_EDX] = frame_addr + offsetof(struct rt_sigframe, uc);
#endif
    unlock_user(frame, frame_addr, total_size);

    cpu_x86_load_seg(env, R_DS, __USER_DS);
    cpu_x86_load_seg(env, R_ES, __USER_DS);
    cpu_x86_load_seg(env, R_CS, __USER_CS);
    cpu_x86_load_seg(env, R_SS, __USER_DS);
    env->eflags &= ~TF_MASK;
    return;

give_sigsegv:
    force_sigsegv(sig);
}

/*
 * Restore a signal frame.
 */

static bool xrstor_sigcontext(CPUX86State *env, FPStateKind fpkind,
                              X86LegacyXSaveArea *fxstate,
                              abi_ptr fxstate_addr)
{
    struct target_fpx_sw_bytes *sw = (void *)&fxstate->sw_reserved;
    uint32_t magic1, magic2;
    uint32_t extended_size, xstate_size, min_size, max_size;
    uint64_t xfeatures;
    void *xstate;
    bool ok;

    switch (fpkind) {
    case FPSTATE_XSAVE:
        magic1 = tswap32(sw->magic1);
        extended_size = tswap32(sw->extended_size);
        xstate_size = tswap32(sw->xstate_size);
        min_size = sizeof(X86LegacyXSaveArea) + sizeof(X86XSaveHeader);
        max_size = xsave_area_size(env->xcr0, false);

        /* Check for the first magic field and other error scenarios. */
        if (magic1 != TARGET_FP_XSTATE_MAGIC1 ||
            xstate_size < min_size ||
            xstate_size > max_size ||
            xstate_size > extended_size) {
            break;
        }

        /*
         * Restore the features indicated in the frame, masked by
         * those currently enabled.  Re-check the frame size.
         * ??? It is not clear where the kernel does this, but it
         * is not in check_xstate_in_sigframe, and so (probably)
         * does not fall back to fxrstor.
         */
        xfeatures = tswap64(sw->xfeatures) & env->xcr0;
        min_size = xsave_area_size(xfeatures, false);
        if (xstate_size < min_size) {
            return false;
        }

        /* Re-lock the entire xstate area, with the extensions and magic. */
        xstate = lock_user(VERIFY_READ, fxstate_addr,
                           xstate_size + TARGET_FP_XSTATE_MAGIC2_SIZE, 1);
        if (!xstate) {
            return false;
        }

        /*
         * Check for the presence of second magic word at the end of memory
         * layout. This detects the case where the user just copied the legacy
         * fpstate layout with out copying the extended state information
         * in the memory layout.
         */
        magic2 = tswap32(*(uint32_t *)(xstate + xstate_size));
        if (magic2 != TARGET_FP_XSTATE_MAGIC2) {
            unlock_user(xstate, fxstate_addr, 0);
            break;
        }

        ok = cpu_x86_xrstor(env, xstate, xstate_size, xfeatures);
        unlock_user(xstate, fxstate_addr, 0);
        return ok;

    default:
        break;
    }

    cpu_x86_fxrstor(env, fxstate, sizeof(*fxstate));
    return true;
}

#ifndef TARGET_X86_64
static bool frstor_sigcontext(CPUX86State *env, FPStateKind fpkind,
                              struct target_fregs_state *fpstate,
                              abi_ptr fpstate_addr,
                              X86LegacyXSaveArea *fxstate,
                              abi_ptr fxstate_addr)
{
    switch (fpkind) {
    case FPSTATE_XSAVE:
        if (!xrstor_sigcontext(env, fpkind, fxstate, fxstate_addr)) {
            return false;
        }
        break;
    case FPSTATE_FXSAVE:
        cpu_x86_fxrstor(env, fxstate, sizeof(*fxstate));
        break;
    case FPSTATE_FSAVE:
        break;
    default:
        g_assert_not_reached();
    }

    /*
     * Copy the legacy state because the FP portion of the FX frame has
     * to be ignored for histerical raisins.  The kernel folds the two
     * states together and then performs a single load; here we perform
     * the merge within ENV by loading XSTATE/FXSTATE first, then
     * overriding with the FSTATE afterward.
     */
    cpu_x86_frstor(env, fpstate, sizeof(*fpstate));
    return true;
}
#endif

static bool restore_sigcontext(CPUX86State *env, struct target_sigcontext *sc)
{
    abi_ptr fpstate_addr;
    unsigned tmpflags, math_size;
    FPStateKind fpkind;
    void *fpstate;
    bool ok;

#ifndef TARGET_X86_64
    cpu_x86_load_seg(env, R_GS, tswap16(sc->gs));
    cpu_x86_load_seg(env, R_FS, tswap16(sc->fs));
    cpu_x86_load_seg(env, R_ES, tswap16(sc->es));
    cpu_x86_load_seg(env, R_DS, tswap16(sc->ds));

    env->regs[R_EDI] = tswapl(sc->edi);
    env->regs[R_ESI] = tswapl(sc->esi);
    env->regs[R_EBP] = tswapl(sc->ebp);
    env->regs[R_ESP] = tswapl(sc->esp);
    env->regs[R_EBX] = tswapl(sc->ebx);
    env->regs[R_EDX] = tswapl(sc->edx);
    env->regs[R_ECX] = tswapl(sc->ecx);
    env->regs[R_EAX] = tswapl(sc->eax);

    env->eip = tswapl(sc->eip);
#else
    env->regs[8] = tswapl(sc->r8);
    env->regs[9] = tswapl(sc->r9);
    env->regs[10] = tswapl(sc->r10);
    env->regs[11] = tswapl(sc->r11);
    env->regs[12] = tswapl(sc->r12);
    env->regs[13] = tswapl(sc->r13);
    env->regs[14] = tswapl(sc->r14);
    env->regs[15] = tswapl(sc->r15);

    env->regs[R_EDI] = tswapl(sc->rdi);
    env->regs[R_ESI] = tswapl(sc->rsi);
    env->regs[R_EBP] = tswapl(sc->rbp);
    env->regs[R_EBX] = tswapl(sc->rbx);
    env->regs[R_EDX] = tswapl(sc->rdx);
    env->regs[R_EAX] = tswapl(sc->rax);
    env->regs[R_ECX] = tswapl(sc->rcx);
    env->regs[R_ESP] = tswapl(sc->rsp);

    env->eip = tswapl(sc->rip);
#endif

    cpu_x86_load_seg(env, R_CS, lduw_le_p(&sc->cs) | 3);
    cpu_x86_load_seg(env, R_SS, lduw_le_p(&sc->ss) | 3);

    tmpflags = tswapl(sc->eflags);
    env->eflags = (env->eflags & ~0x40DD5) | (tmpflags & 0x40DD5);

    fpstate_addr = tswapl(sc->fpstate);
    if (fpstate_addr == 0) {
        return true;
    }

    fpkind = get_fpstate_kind(env);
    math_size = get_fpstate_size(env, fpkind);
#ifndef TARGET_X86_64
    if (fpkind != FPSTATE_FSAVE) {
        math_size += sizeof(struct target_fregs_state);
    }
#endif
    fpstate = lock_user(VERIFY_READ, fpstate_addr, math_size, 1);
    if (!fpstate) {
        return false;
    }

#ifdef TARGET_X86_64
    ok = xrstor_sigcontext(env, fpkind, fpstate, fpstate_addr);
#else
    ok = frstor_sigcontext(env, fpkind, fpstate, fpstate_addr,
                           fpstate + sizeof(struct target_fregs_state),
                           fpstate_addr + sizeof(struct target_fregs_state));
#endif

    unlock_user(fpstate, fpstate_addr, 0);
    return ok;
}

/* Note: there is no sigreturn on x86_64, there is only rt_sigreturn */
#ifndef TARGET_X86_64
long do_sigreturn(CPUX86State *env)
{
    struct sigframe *frame;
    abi_ulong frame_addr = env->regs[R_ESP] - 8;
    target_sigset_t target_set;
    sigset_t set;

    trace_user_do_sigreturn(env, frame_addr);
    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1)) {
        force_sig(TARGET_SIGSEGV);
        return -QEMU_ESIGRETURN;
    }

    /* Set blocked signals. */
    __get_user(target_set.sig[0], &frame->sc.oldmask);
    for (int i = 1; i < TARGET_NSIG_WORDS; i++) {
        __get_user(target_set.sig[i], &frame->extramask[i - 1]);
    }
    target_to_host_sigset_internal(&set, &target_set);
    set_sigmask(&set);

    /* Restore registers */
    if (!restore_sigcontext(env, &frame->sc)) {
        force_sig(TARGET_SIGSEGV);
    }

    unlock_user_struct(frame, frame_addr, 0);
    return -QEMU_ESIGRETURN;
}
#endif

long do_rt_sigreturn(CPUX86State *env)
{
    abi_ulong frame_addr;
    struct rt_sigframe *frame;
    sigset_t set;

    frame_addr = env->regs[R_ESP] - sizeof(abi_ulong);
    trace_user_do_rt_sigreturn(env, frame_addr);
    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1))
        goto badframe;
    target_to_host_sigset(&set, &frame->uc.tuc_sigmask);
    set_sigmask(&set);

    if (!restore_sigcontext(env, &frame->uc.tuc_mcontext)) {
        goto badframe;
    }

    target_restore_altstack(&frame->uc.tuc_stack, env);

    unlock_user_struct(frame, frame_addr, 0);
    return -QEMU_ESIGRETURN;

badframe:
    unlock_user_struct(frame, frame_addr, 0);
    force_sig(TARGET_SIGSEGV);
    return -QEMU_ESIGRETURN;
}

#ifndef TARGET_X86_64
void setup_sigtramp(abi_ulong sigtramp_page)
{
    uint16_t *tramp = lock_user(VERIFY_WRITE, sigtramp_page, 2 * 8, 0);
    assert(tramp != NULL);

    default_sigreturn = sigtramp_page;
    install_sigtramp(tramp);

    default_rt_sigreturn = sigtramp_page + 8;
    install_rt_sigtramp(tramp + 8);

    unlock_user(tramp, sigtramp_page, 2 * 8);
}
#endif
