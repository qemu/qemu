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

/* from the Linux kernel - /arch/x86/include/uapi/asm/sigcontext.h */

#define TARGET_FP_XSTATE_MAGIC1         0x46505853U /* FPXS */
#define TARGET_FP_XSTATE_MAGIC2         0x46505845U /* FPXE */
#define TARGET_FP_XSTATE_MAGIC2_SIZE    4

struct target_fpreg {
    uint16_t significand[4];
    uint16_t exponent;
};

struct target_fpxreg {
    uint16_t significand[4];
    uint16_t exponent;
    uint16_t padding[3];
};

struct target_xmmreg {
    uint32_t element[4];
};

struct target_fpx_sw_bytes {
    uint32_t magic1;
    uint32_t extended_size;
    uint64_t xfeatures;
    uint32_t xstate_size;
    uint32_t reserved[7];
};
QEMU_BUILD_BUG_ON(sizeof(struct target_fpx_sw_bytes) != 12*4);

struct target_fpstate_fxsave {
    /* FXSAVE format */
    uint16_t cw;
    uint16_t sw;
    uint16_t twd;
    uint16_t fop;
    uint64_t rip;
    uint64_t rdp;
    uint32_t mxcsr;
    uint32_t mxcsr_mask;
    uint32_t st_space[32];
    uint32_t xmm_space[64];
    uint32_t hw_reserved[12];
    struct target_fpx_sw_bytes sw_reserved;
    uint8_t xfeatures[];
};
#define TARGET_FXSAVE_SIZE   sizeof(struct target_fpstate_fxsave)
QEMU_BUILD_BUG_ON(TARGET_FXSAVE_SIZE != 512);
QEMU_BUILD_BUG_ON(offsetof(struct target_fpstate_fxsave, sw_reserved) != 464);

struct target_fpstate_32 {
    /* Regular FPU environment */
    uint32_t cw;
    uint32_t sw;
    uint32_t tag;
    uint32_t ipoff;
    uint32_t cssel;
    uint32_t dataoff;
    uint32_t datasel;
    struct target_fpreg st[8];
    uint16_t  status;
    uint16_t  magic;          /* 0xffff = regular FPU data only */
    struct target_fpstate_fxsave fxsave;
};

/*
 * For simplicity, setup_frame aligns struct target_fpstate_32 to
 * 16 bytes, so ensure that the FXSAVE area is also aligned.
 */
QEMU_BUILD_BUG_ON(offsetof(struct target_fpstate_32, fxsave) & 15);

#ifndef TARGET_X86_64
# define target_fpstate target_fpstate_32
# define TARGET_FPSTATE_FXSAVE_OFFSET offsetof(struct target_fpstate_32, fxsave)
#else
# define target_fpstate target_fpstate_fxsave
# define TARGET_FPSTATE_FXSAVE_OFFSET 0
#endif

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
     * The actual fpstate is placed after retcode[] below, to make
     * room for the variable-sized xsave data.  The older unused fpstate
     * has to be kept to avoid changing the offset of extramask[], which
     * is part of the ABI.
     */
    struct target_fpstate fpstate_unused;
    abi_ulong extramask[TARGET_NSIG_WORDS-1];
    char retcode[8];

    /*
     * This field will be 16-byte aligned in memory.  Applying QEMU_ALIGNED
     * to it ensures that the base of the frame has an appropriate alignment
     * too.
     */
    struct target_fpstate fpstate QEMU_ALIGNED(8);
};
#define TARGET_SIGFRAME_FXSAVE_OFFSET (                                    \
    offsetof(struct sigframe, fpstate) + TARGET_FPSTATE_FXSAVE_OFFSET)

struct rt_sigframe {
    abi_ulong pretcode;
    int sig;
    abi_ulong pinfo;
    abi_ulong puc;
    struct target_siginfo info;
    struct target_ucontext uc;
    char retcode[8];
    struct target_fpstate fpstate QEMU_ALIGNED(8);
};
#define TARGET_RT_SIGFRAME_FXSAVE_OFFSET (                                 \
    offsetof(struct rt_sigframe, fpstate) + TARGET_FPSTATE_FXSAVE_OFFSET)

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
    struct target_fpstate fpstate QEMU_ALIGNED(16);
};
#define TARGET_RT_SIGFRAME_FXSAVE_OFFSET (                                 \
    offsetof(struct rt_sigframe, fpstate) + TARGET_FPSTATE_FXSAVE_OFFSET)
#endif

/*
 * Set up a signal frame.
 */

static void xsave_sigcontext(CPUX86State *env, struct target_fpstate_fxsave *fxsave,
                             abi_ulong fxsave_addr)
{
    if (!(env->features[FEAT_1_ECX] & CPUID_EXT_XSAVE)) {
        /* fxsave_addr must be 16 byte aligned for fxsave */
        assert(!(fxsave_addr & 0xf));

        cpu_x86_fxsave(env, fxsave_addr);
        __put_user(0, &fxsave->sw_reserved.magic1);
    } else {
        uint32_t xstate_size = xsave_area_size(env->xcr0, false);
        uint32_t xfeatures_size = xstate_size - TARGET_FXSAVE_SIZE;

        /*
         * extended_size is the offset from fpstate_addr to right after the end
         * of the extended save states.  On 32-bit that includes the legacy
         * FSAVE area.
         */
        uint32_t extended_size = TARGET_FPSTATE_FXSAVE_OFFSET
            + xstate_size + TARGET_FP_XSTATE_MAGIC2_SIZE;

        /* fxsave_addr must be 64 byte aligned for xsave */
        assert(!(fxsave_addr & 0x3f));

        /* Zero the header, XSAVE *adds* features to an existing save state.  */
        memset(fxsave->xfeatures, 0, 64);
        cpu_x86_xsave(env, fxsave_addr);
        __put_user(TARGET_FP_XSTATE_MAGIC1, &fxsave->sw_reserved.magic1);
        __put_user(extended_size, &fxsave->sw_reserved.extended_size);
        __put_user(env->xcr0, &fxsave->sw_reserved.xfeatures);
        __put_user(xstate_size, &fxsave->sw_reserved.xstate_size);
        __put_user(TARGET_FP_XSTATE_MAGIC2, (uint32_t *) &fxsave->xfeatures[xfeatures_size]);
    }
}

static void setup_sigcontext(struct target_sigcontext *sc,
        struct target_fpstate *fpstate, CPUX86State *env, abi_ulong mask,
        abi_ulong fpstate_addr)
{
    CPUState *cs = env_cpu(env);
#ifndef TARGET_X86_64
    uint16_t magic;

    /* already locked in setup_frame() */
    __put_user(env->segs[R_GS].selector, (unsigned int *)&sc->gs);
    __put_user(env->segs[R_FS].selector, (unsigned int *)&sc->fs);
    __put_user(env->segs[R_ES].selector, (unsigned int *)&sc->es);
    __put_user(env->segs[R_DS].selector, (unsigned int *)&sc->ds);
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
    __put_user(env->segs[R_CS].selector, (unsigned int *)&sc->cs);
    __put_user(env->eflags, &sc->eflags);
    __put_user(env->regs[R_ESP], &sc->esp_at_signal);
    __put_user(env->segs[R_SS].selector, (unsigned int *)&sc->ss);

    cpu_x86_fsave(env, fpstate_addr, 1);
    fpstate->status = fpstate->sw;
    if (!(env->features[FEAT_1_EDX] & CPUID_FXSR)) {
        magic = 0xffff;
    } else {
        xsave_sigcontext(env, &fpstate->fxsave,
                         fpstate_addr + TARGET_FPSTATE_FXSAVE_OFFSET);
        magic = 0;
    }
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

    xsave_sigcontext(env, fpstate, fpstate_addr);
#endif

    __put_user(fpstate_addr, &sc->fpstate);

    /* non-iBCS2 extensions.. */
    __put_user(mask, &sc->oldmask);
    __put_user(env->cr[2], &sc->cr2);
}

/*
 * Determine which stack to use..
 */

static inline abi_ulong
get_sigframe(struct target_sigaction *ka, CPUX86State *env, size_t fxsave_offset)
{
    unsigned long esp;

    /* Default to using normal stack */
    esp = get_sp_from_cpustate(env);
#ifdef TARGET_X86_64
    esp -= 128; /* this is the redzone */
#endif

    /* This is the X/Open sanctioned signal stack switching.  */
    if (ka->sa_flags & TARGET_SA_ONSTACK) {
        esp = target_sigsp(esp, ka);
    } else {
#ifndef TARGET_X86_64
        /* This is the legacy signal stack switching. */
        if ((env->segs[R_SS].selector & 0xffff) != __USER_DS &&
                !(ka->sa_flags & TARGET_SA_RESTORER) &&
                ka->sa_restorer) {
            esp = (unsigned long) ka->sa_restorer;
        }
#endif
    }

    if (!(env->features[FEAT_1_EDX] & CPUID_FXSR)) {
        return (esp - (fxsave_offset + TARGET_FXSAVE_SIZE)) & -8ul;
    } else if (!(env->features[FEAT_1_ECX] & CPUID_EXT_XSAVE)) {
        return ((esp - TARGET_FXSAVE_SIZE) & -16ul) - fxsave_offset;
    } else {
        size_t xstate_size =
               xsave_area_size(env->xcr0, false) + TARGET_FP_XSTATE_MAGIC2_SIZE;
        return ((esp - xstate_size) & -64ul) - fxsave_offset;
    }
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
    abi_ulong frame_addr;
    struct sigframe *frame;
    int i;

    frame_addr = get_sigframe(ka, env, TARGET_SIGFRAME_FXSAVE_OFFSET);
    trace_user_setup_frame(env, frame_addr);

    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0))
        goto give_sigsegv;

    __put_user(sig, &frame->sig);

    setup_sigcontext(&frame->sc, &frame->fpstate, env, set->sig[0],
            frame_addr + offsetof(struct sigframe, fpstate));

    for(i = 1; i < TARGET_NSIG_WORDS; i++) {
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

    /* Set up registers for signal handler */
    env->regs[R_ESP] = frame_addr;
    env->eip = ka->_sa_handler;

    cpu_x86_load_seg(env, R_DS, __USER_DS);
    cpu_x86_load_seg(env, R_ES, __USER_DS);
    cpu_x86_load_seg(env, R_SS, __USER_DS);
    cpu_x86_load_seg(env, R_CS, __USER_CS);
    env->eflags &= ~TF_MASK;

    unlock_user_struct(frame, frame_addr, 1);

    return;

give_sigsegv:
    force_sigsegv(sig);
}
#endif

/* compare linux/arch/x86/kernel/signal.c:setup_rt_frame() */
void setup_rt_frame(int sig, struct target_sigaction *ka,
                    target_siginfo_t *info,
                    target_sigset_t *set, CPUX86State *env)
{
    abi_ulong frame_addr;
#ifndef TARGET_X86_64
    abi_ulong addr;
#endif
    struct rt_sigframe *frame;
    int i;

    frame_addr = get_sigframe(ka, env, TARGET_RT_SIGFRAME_FXSAVE_OFFSET);
    trace_user_setup_rt_frame(env, frame_addr);

    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0))
        goto give_sigsegv;

    /* These fields are only in rt_sigframe on 32 bit */
#ifndef TARGET_X86_64
    __put_user(sig, &frame->sig);
    addr = frame_addr + offsetof(struct rt_sigframe, info);
    __put_user(addr, &frame->pinfo);
    addr = frame_addr + offsetof(struct rt_sigframe, uc);
    __put_user(addr, &frame->puc);
#endif
    if (ka->sa_flags & TARGET_SA_SIGINFO) {
        tswap_siginfo(&frame->info, info);
    }

    /* Create the ucontext.  */
    if (env->features[FEAT_1_ECX] & CPUID_EXT_XSAVE) {
        __put_user(1, &frame->uc.tuc_flags);
    } else {
        __put_user(0, &frame->uc.tuc_flags);
    }
    __put_user(0, &frame->uc.tuc_link);
    target_save_altstack(&frame->uc.tuc_stack, env);
    setup_sigcontext(&frame->uc.tuc_mcontext, &frame->fpstate, env,
            set->sig[0], frame_addr + offsetof(struct rt_sigframe, fpstate));

    for(i = 0; i < TARGET_NSIG_WORDS; i++) {
        __put_user(set->sig[i], &frame->uc.tuc_sigmask.sig[i]);
    }

    /* Set up to return from userspace.  If provided, use a stub
       already in userspace.  */
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
    env->regs[R_EAX] = sig;
    env->regs[R_EDX] = frame_addr + offsetof(struct rt_sigframe, info);
    env->regs[R_ECX] = frame_addr + offsetof(struct rt_sigframe, uc);
#else
    env->regs[R_EAX] = 0;
    env->regs[R_EDI] = sig;
    env->regs[R_ESI] = frame_addr + offsetof(struct rt_sigframe, info);
    env->regs[R_EDX] = frame_addr + offsetof(struct rt_sigframe, uc);
#endif

    cpu_x86_load_seg(env, R_DS, __USER_DS);
    cpu_x86_load_seg(env, R_ES, __USER_DS);
    cpu_x86_load_seg(env, R_CS, __USER_CS);
    cpu_x86_load_seg(env, R_SS, __USER_DS);
    env->eflags &= ~TF_MASK;

    unlock_user_struct(frame, frame_addr, 1);

    return;

give_sigsegv:
    force_sigsegv(sig);
}

static int xrstor_sigcontext(CPUX86State *env, struct target_fpstate_fxsave *fxsave,
                             abi_ulong fxsave_addr)
{
    if (env->features[FEAT_1_ECX] & CPUID_EXT_XSAVE) {
        uint32_t extended_size = tswapl(fxsave->sw_reserved.extended_size);
        uint32_t xstate_size = tswapl(fxsave->sw_reserved.xstate_size);
        uint32_t xfeatures_size = xstate_size - TARGET_FXSAVE_SIZE;

        /* Linux checks MAGIC2 using xstate_size, not extended_size.  */
        if (tswapl(fxsave->sw_reserved.magic1) == TARGET_FP_XSTATE_MAGIC1 &&
            extended_size >= TARGET_FPSTATE_FXSAVE_OFFSET + xstate_size + TARGET_FP_XSTATE_MAGIC2_SIZE) {
            if (!access_ok(env_cpu(env), VERIFY_READ, fxsave_addr,
                           extended_size - TARGET_FPSTATE_FXSAVE_OFFSET)) {
                return 1;
            }
            if (tswapl(*(uint32_t *) &fxsave->xfeatures[xfeatures_size]) == TARGET_FP_XSTATE_MAGIC2) {
                cpu_x86_xrstor(env, fxsave_addr);
                return 0;
            }
        }
        /* fall through to fxrstor */
    }

    cpu_x86_fxrstor(env, fxsave_addr);
    return 0;
}

static int
restore_sigcontext(CPUX86State *env, struct target_sigcontext *sc)
{
    int err = 1;
    abi_ulong fpstate_addr;
    unsigned int tmpflags;

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

    cpu_x86_load_seg(env, R_CS, lduw_p(&sc->cs) | 3);
    cpu_x86_load_seg(env, R_SS, lduw_p(&sc->ss) | 3);

    tmpflags = tswapl(sc->eflags);
    env->eflags = (env->eflags & ~0x40DD5) | (tmpflags & 0x40DD5);
    //          regs->orig_eax = -1;            /* disable syscall checks */

    fpstate_addr = tswapl(sc->fpstate);
    if (fpstate_addr != 0) {
        struct target_fpstate *fpstate;
        if (!lock_user_struct(VERIFY_READ, fpstate, fpstate_addr,
                              sizeof(struct target_fpstate))) {
            return err;
        }
#ifndef TARGET_X86_64
        if (!(env->features[FEAT_1_EDX] & CPUID_FXSR)) {
            cpu_x86_frstor(env, fpstate_addr, 1);
            err = 0;
        } else {
            err = xrstor_sigcontext(env, &fpstate->fxsave,
                                    fpstate_addr + TARGET_FPSTATE_FXSAVE_OFFSET);
        }
#else
        err = xrstor_sigcontext(env, fpstate, fpstate_addr);
#endif
        unlock_user_struct(fpstate, fpstate_addr, 0);
    } else {
        err = 0;
    }

    return err;
}

/* Note: there is no sigreturn on x86_64, there is only rt_sigreturn */
#ifndef TARGET_X86_64
long do_sigreturn(CPUX86State *env)
{
    struct sigframe *frame;
    abi_ulong frame_addr = env->regs[R_ESP] - 8;
    target_sigset_t target_set;
    sigset_t set;
    int i;

    trace_user_do_sigreturn(env, frame_addr);
    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1))
        goto badframe;
    /* set blocked signals */
    __get_user(target_set.sig[0], &frame->sc.oldmask);
    for(i = 1; i < TARGET_NSIG_WORDS; i++) {
        __get_user(target_set.sig[i], &frame->extramask[i - 1]);
    }

    target_to_host_sigset_internal(&set, &target_set);
    set_sigmask(&set);

    /* restore registers */
    if (restore_sigcontext(env, &frame->sc))
        goto badframe;
    unlock_user_struct(frame, frame_addr, 0);
    return -QEMU_ESIGRETURN;

badframe:
    unlock_user_struct(frame, frame_addr, 0);
    force_sig(TARGET_SIGSEGV);
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

    if (restore_sigcontext(env, &frame->uc.tuc_mcontext)) {
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
