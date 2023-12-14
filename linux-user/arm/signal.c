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
#include "target/arm/cpu-features.h"
#include "vdso-asmoffset.h"

struct target_sigcontext {
    abi_ulong trap_no;
    abi_ulong error_code;
    abi_ulong oldmask;
    abi_ulong arm_r0;
    abi_ulong arm_r1;
    abi_ulong arm_r2;
    abi_ulong arm_r3;
    abi_ulong arm_r4;
    abi_ulong arm_r5;
    abi_ulong arm_r6;
    abi_ulong arm_r7;
    abi_ulong arm_r8;
    abi_ulong arm_r9;
    abi_ulong arm_r10;
    abi_ulong arm_fp;
    abi_ulong arm_ip;
    abi_ulong arm_sp;
    abi_ulong arm_lr;
    abi_ulong arm_pc;
    abi_ulong arm_cpsr;
    abi_ulong fault_address;
};

struct target_ucontext {
    abi_ulong tuc_flags;
    abi_ulong tuc_link;
    target_stack_t tuc_stack;
    struct target_sigcontext tuc_mcontext;
    target_sigset_t  tuc_sigmask;       /* mask last for extensibility */
    char __unused[128 - sizeof(target_sigset_t)];
    abi_ulong tuc_regspace[128] __attribute__((__aligned__(8)));
};

struct target_user_vfp {
    uint64_t fpregs[32];
    abi_ulong fpscr;
};

struct target_user_vfp_exc {
    abi_ulong fpexc;
    abi_ulong fpinst;
    abi_ulong fpinst2;
};

struct target_vfp_sigframe {
    abi_ulong magic;
    abi_ulong size;
    struct target_user_vfp ufp;
    struct target_user_vfp_exc ufp_exc;
} __attribute__((__aligned__(8)));

struct target_iwmmxt_sigframe {
    abi_ulong magic;
    abi_ulong size;
    uint64_t regs[16];
    /* Note that not all the coprocessor control registers are stored here */
    uint32_t wcssf;
    uint32_t wcasf;
    uint32_t wcgr0;
    uint32_t wcgr1;
    uint32_t wcgr2;
    uint32_t wcgr3;
} __attribute__((__aligned__(8)));

#define TARGET_VFP_MAGIC 0x56465001
#define TARGET_IWMMXT_MAGIC 0x12ef842a

struct sigframe
{
    struct target_ucontext uc;
    abi_ulong retcode[4];
};

struct rt_sigframe
{
    struct target_siginfo info;
    struct sigframe sig;
};

QEMU_BUILD_BUG_ON(offsetof(struct sigframe, retcode[3])
                  != SIGFRAME_RC3_OFFSET);
QEMU_BUILD_BUG_ON(offsetof(struct rt_sigframe, sig.retcode[3])
                  != RT_SIGFRAME_RC3_OFFSET);

static abi_ptr sigreturn_fdpic_tramp;

/*
 * Up to 3 words of 'retcode' in the sigframe are code,
 * with retcode[3] being used by fdpic for the function descriptor.
 * This code is not actually executed, but is retained for ABI compat.
 *
 * We will create a table of 8 retcode variants in the sigtramp page.
 * Let each table entry use 3 words.
 */
#define RETCODE_WORDS  3
#define RETCODE_BYTES  (RETCODE_WORDS * 4)

static inline int valid_user_regs(CPUARMState *regs)
{
    return 1;
}

static void
setup_sigcontext(struct target_sigcontext *sc, /*struct _fpstate *fpstate,*/
                 CPUARMState *env, abi_ulong mask)
{
    __put_user(env->regs[0], &sc->arm_r0);
    __put_user(env->regs[1], &sc->arm_r1);
    __put_user(env->regs[2], &sc->arm_r2);
    __put_user(env->regs[3], &sc->arm_r3);
    __put_user(env->regs[4], &sc->arm_r4);
    __put_user(env->regs[5], &sc->arm_r5);
    __put_user(env->regs[6], &sc->arm_r6);
    __put_user(env->regs[7], &sc->arm_r7);
    __put_user(env->regs[8], &sc->arm_r8);
    __put_user(env->regs[9], &sc->arm_r9);
    __put_user(env->regs[10], &sc->arm_r10);
    __put_user(env->regs[11], &sc->arm_fp);
    __put_user(env->regs[12], &sc->arm_ip);
    __put_user(env->regs[13], &sc->arm_sp);
    __put_user(env->regs[14], &sc->arm_lr);
    __put_user(env->regs[15], &sc->arm_pc);
    __put_user(cpsr_read(env), &sc->arm_cpsr);

    __put_user(/* current->thread.trap_no */ 0, &sc->trap_no);
    __put_user(/* current->thread.error_code */ 0, &sc->error_code);
    __put_user(/* current->thread.address */ 0, &sc->fault_address);
    __put_user(mask, &sc->oldmask);
}

static inline abi_ulong
get_sigframe(struct target_sigaction *ka, CPUARMState *regs, int framesize)
{
    unsigned long sp;

    sp = target_sigsp(get_sp_from_cpustate(regs), ka);
    /*
     * ATPCS B01 mandates 8-byte alignment
     */
    return (sp - framesize) & ~7;
}

static void write_arm_sigreturn(uint32_t *rc, int syscall);
static void write_arm_fdpic_sigreturn(uint32_t *rc, int ofs);

static int
setup_return(CPUARMState *env, struct target_sigaction *ka, int usig,
             struct sigframe *frame, abi_ulong sp_addr)
{
    abi_ulong handler = 0;
    abi_ulong handler_fdpic_GOT = 0;
    abi_ulong retcode;
    bool is_fdpic = info_is_fdpic(((TaskState *)thread_cpu->opaque)->info);
    bool is_rt = ka->sa_flags & TARGET_SA_SIGINFO;
    bool thumb;

    if (is_fdpic) {
        /* In FDPIC mode, ka->_sa_handler points to a function
         * descriptor (FD). The first word contains the address of the
         * handler. The second word contains the value of the PIC
         * register (r9).  */
        abi_ulong funcdesc_ptr = ka->_sa_handler;
        if (get_user_ual(handler, funcdesc_ptr)
            || get_user_ual(handler_fdpic_GOT, funcdesc_ptr + 4)) {
            return 1;
        }
    } else {
        handler = ka->_sa_handler;
    }
    thumb = handler & 1;

    uint32_t cpsr = cpsr_read(env);

    cpsr &= ~CPSR_IT;
    if (thumb) {
        cpsr |= CPSR_T;
    } else {
        cpsr &= ~CPSR_T;
    }
    if (env->cp15.sctlr_el[1] & SCTLR_E0E) {
        cpsr |= CPSR_E;
    } else {
        cpsr &= ~CPSR_E;
    }

    /* Our vdso default_sigreturn label is a table of entry points. */
    retcode = default_sigreturn + (is_fdpic * 2 + is_rt) * 8;

    /*
     * Put the sigreturn code on the stack no matter which return
     * mechanism we use in order to remain ABI compliant.
     * Because this is about ABI, always use the A32 instructions,
     * despite the fact that our actual vdso trampoline is T16.
     */
    if (is_fdpic) {
        write_arm_fdpic_sigreturn(frame->retcode,
                                  is_rt ? RT_SIGFRAME_RC3_OFFSET
                                        : SIGFRAME_RC3_OFFSET);
    } else {
        write_arm_sigreturn(frame->retcode,
                            is_rt ? TARGET_NR_rt_sigreturn
                                  : TARGET_NR_sigreturn);
    }

    if (ka->sa_flags & TARGET_SA_RESTORER) {
        if (is_fdpic) {
            /* Place the function descriptor in slot 3. */
            __put_user((abi_ulong)ka->sa_restorer, &frame->retcode[3]);
        } else {
            retcode = ka->sa_restorer;
        }
    }

    env->regs[0] = usig;
    if (is_fdpic) {
        env->regs[9] = handler_fdpic_GOT;
    }
    env->regs[13] = sp_addr;
    env->regs[14] = retcode;
    env->regs[15] = handler & (thumb ? ~1 : ~3);
    cpsr_write(env, cpsr, CPSR_IT | CPSR_T | CPSR_E, CPSRWriteByInstr);

    return 0;
}

static abi_ulong *setup_sigframe_vfp(abi_ulong *regspace, CPUARMState *env)
{
    int i;
    struct target_vfp_sigframe *vfpframe;
    vfpframe = (struct target_vfp_sigframe *)regspace;
    __put_user(TARGET_VFP_MAGIC, &vfpframe->magic);
    __put_user(sizeof(*vfpframe), &vfpframe->size);
    for (i = 0; i < 32; i++) {
        __put_user(*aa32_vfp_dreg(env, i), &vfpframe->ufp.fpregs[i]);
    }
    __put_user(vfp_get_fpscr(env), &vfpframe->ufp.fpscr);
    __put_user(env->vfp.xregs[ARM_VFP_FPEXC], &vfpframe->ufp_exc.fpexc);
    __put_user(env->vfp.xregs[ARM_VFP_FPINST], &vfpframe->ufp_exc.fpinst);
    __put_user(env->vfp.xregs[ARM_VFP_FPINST2], &vfpframe->ufp_exc.fpinst2);
    return (abi_ulong*)(vfpframe+1);
}

static abi_ulong *setup_sigframe_iwmmxt(abi_ulong *regspace, CPUARMState *env)
{
    int i;
    struct target_iwmmxt_sigframe *iwmmxtframe;
    iwmmxtframe = (struct target_iwmmxt_sigframe *)regspace;
    __put_user(TARGET_IWMMXT_MAGIC, &iwmmxtframe->magic);
    __put_user(sizeof(*iwmmxtframe), &iwmmxtframe->size);
    for (i = 0; i < 16; i++) {
        __put_user(env->iwmmxt.regs[i], &iwmmxtframe->regs[i]);
    }
    __put_user(env->vfp.xregs[ARM_IWMMXT_wCSSF], &iwmmxtframe->wcssf);
    __put_user(env->vfp.xregs[ARM_IWMMXT_wCASF], &iwmmxtframe->wcssf);
    __put_user(env->vfp.xregs[ARM_IWMMXT_wCGR0], &iwmmxtframe->wcgr0);
    __put_user(env->vfp.xregs[ARM_IWMMXT_wCGR1], &iwmmxtframe->wcgr1);
    __put_user(env->vfp.xregs[ARM_IWMMXT_wCGR2], &iwmmxtframe->wcgr2);
    __put_user(env->vfp.xregs[ARM_IWMMXT_wCGR3], &iwmmxtframe->wcgr3);
    return (abi_ulong*)(iwmmxtframe+1);
}

static void setup_sigframe(struct target_ucontext *uc,
                           target_sigset_t *set, CPUARMState *env)
{
    struct target_sigaltstack stack;
    int i;
    abi_ulong *regspace;

    /* Clear all the bits of the ucontext we don't use.  */
    memset(uc, 0, offsetof(struct target_ucontext, tuc_mcontext));

    memset(&stack, 0, sizeof(stack));
    target_save_altstack(&stack, env);
    memcpy(&uc->tuc_stack, &stack, sizeof(stack));

    setup_sigcontext(&uc->tuc_mcontext, env, set->sig[0]);
    /* Save coprocessor signal frame.  */
    regspace = uc->tuc_regspace;
    if (cpu_isar_feature(aa32_vfp_simd, env_archcpu(env))) {
        regspace = setup_sigframe_vfp(regspace, env);
    }
    if (arm_feature(env, ARM_FEATURE_IWMMXT)) {
        regspace = setup_sigframe_iwmmxt(regspace, env);
    }

    /* Write terminating magic word */
    __put_user(0, regspace);

    for(i = 0; i < TARGET_NSIG_WORDS; i++) {
        __put_user(set->sig[i], &uc->tuc_sigmask.sig[i]);
    }
}

void setup_frame(int usig, struct target_sigaction *ka,
                 target_sigset_t *set, CPUARMState *regs)
{
    struct sigframe *frame;
    abi_ulong frame_addr = get_sigframe(ka, regs, sizeof(*frame));

    trace_user_setup_frame(regs, frame_addr);
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0)) {
        goto sigsegv;
    }

    setup_sigframe(&frame->uc, set, regs);

    if (setup_return(regs, ka, usig, frame, frame_addr)) {
        goto sigsegv;
    }

    unlock_user_struct(frame, frame_addr, 1);
    return;
sigsegv:
    unlock_user_struct(frame, frame_addr, 1);
    force_sigsegv(usig);
}

void setup_rt_frame(int usig, struct target_sigaction *ka,
                    target_siginfo_t *info,
                    target_sigset_t *set, CPUARMState *env)
{
    struct rt_sigframe *frame;
    abi_ulong frame_addr = get_sigframe(ka, env, sizeof(*frame));
    abi_ulong info_addr, uc_addr;

    trace_user_setup_rt_frame(env, frame_addr);
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0)) {
        goto sigsegv;
    }

    info_addr = frame_addr + offsetof(struct rt_sigframe, info);
    uc_addr = frame_addr + offsetof(struct rt_sigframe, sig.uc);
    tswap_siginfo(&frame->info, info);

    setup_sigframe(&frame->sig.uc, set, env);

    if (setup_return(env, ka, usig, &frame->sig, frame_addr)) {
        goto sigsegv;
    }

    env->regs[1] = info_addr;
    env->regs[2] = uc_addr;

    unlock_user_struct(frame, frame_addr, 1);
    return;
sigsegv:
    unlock_user_struct(frame, frame_addr, 1);
    force_sigsegv(usig);
}

static int
restore_sigcontext(CPUARMState *env, struct target_sigcontext *sc)
{
    int err = 0;
    uint32_t cpsr;

    __get_user(env->regs[0], &sc->arm_r0);
    __get_user(env->regs[1], &sc->arm_r1);
    __get_user(env->regs[2], &sc->arm_r2);
    __get_user(env->regs[3], &sc->arm_r3);
    __get_user(env->regs[4], &sc->arm_r4);
    __get_user(env->regs[5], &sc->arm_r5);
    __get_user(env->regs[6], &sc->arm_r6);
    __get_user(env->regs[7], &sc->arm_r7);
    __get_user(env->regs[8], &sc->arm_r8);
    __get_user(env->regs[9], &sc->arm_r9);
    __get_user(env->regs[10], &sc->arm_r10);
    __get_user(env->regs[11], &sc->arm_fp);
    __get_user(env->regs[12], &sc->arm_ip);
    __get_user(env->regs[13], &sc->arm_sp);
    __get_user(env->regs[14], &sc->arm_lr);
    __get_user(env->regs[15], &sc->arm_pc);
    __get_user(cpsr, &sc->arm_cpsr);
    cpsr_write(env, cpsr, CPSR_USER | CPSR_EXEC, CPSRWriteByInstr);

    err |= !valid_user_regs(env);

    return err;
}

static abi_ulong *restore_sigframe_vfp(CPUARMState *env, abi_ulong *regspace)
{
    int i;
    abi_ulong magic, sz;
    uint32_t fpscr, fpexc;
    struct target_vfp_sigframe *vfpframe;
    vfpframe = (struct target_vfp_sigframe *)regspace;

    __get_user(magic, &vfpframe->magic);
    __get_user(sz, &vfpframe->size);
    if (magic != TARGET_VFP_MAGIC || sz != sizeof(*vfpframe)) {
        return 0;
    }
    for (i = 0; i < 32; i++) {
        __get_user(*aa32_vfp_dreg(env, i), &vfpframe->ufp.fpregs[i]);
    }
    __get_user(fpscr, &vfpframe->ufp.fpscr);
    vfp_set_fpscr(env, fpscr);
    __get_user(fpexc, &vfpframe->ufp_exc.fpexc);
    /* Sanitise FPEXC: ensure VFP is enabled, FPINST2 is invalid
     * and the exception flag is cleared
     */
    fpexc |= (1 << 30);
    fpexc &= ~((1 << 31) | (1 << 28));
    env->vfp.xregs[ARM_VFP_FPEXC] = fpexc;
    __get_user(env->vfp.xregs[ARM_VFP_FPINST], &vfpframe->ufp_exc.fpinst);
    __get_user(env->vfp.xregs[ARM_VFP_FPINST2], &vfpframe->ufp_exc.fpinst2);
    return (abi_ulong*)(vfpframe + 1);
}

static abi_ulong *restore_sigframe_iwmmxt(CPUARMState *env,
                                          abi_ulong *regspace)
{
    int i;
    abi_ulong magic, sz;
    struct target_iwmmxt_sigframe *iwmmxtframe;
    iwmmxtframe = (struct target_iwmmxt_sigframe *)regspace;

    __get_user(magic, &iwmmxtframe->magic);
    __get_user(sz, &iwmmxtframe->size);
    if (magic != TARGET_IWMMXT_MAGIC || sz != sizeof(*iwmmxtframe)) {
        return 0;
    }
    for (i = 0; i < 16; i++) {
        __get_user(env->iwmmxt.regs[i], &iwmmxtframe->regs[i]);
    }
    __get_user(env->vfp.xregs[ARM_IWMMXT_wCSSF], &iwmmxtframe->wcssf);
    __get_user(env->vfp.xregs[ARM_IWMMXT_wCASF], &iwmmxtframe->wcssf);
    __get_user(env->vfp.xregs[ARM_IWMMXT_wCGR0], &iwmmxtframe->wcgr0);
    __get_user(env->vfp.xregs[ARM_IWMMXT_wCGR1], &iwmmxtframe->wcgr1);
    __get_user(env->vfp.xregs[ARM_IWMMXT_wCGR2], &iwmmxtframe->wcgr2);
    __get_user(env->vfp.xregs[ARM_IWMMXT_wCGR3], &iwmmxtframe->wcgr3);
    return (abi_ulong*)(iwmmxtframe + 1);
}

static int do_sigframe_return(CPUARMState *env,
                              target_ulong context_addr,
                              struct target_ucontext *uc)
{
    sigset_t host_set;
    abi_ulong *regspace;

    target_to_host_sigset(&host_set, &uc->tuc_sigmask);
    set_sigmask(&host_set);

    if (restore_sigcontext(env, &uc->tuc_mcontext)) {
        return 1;
    }

    /* Restore coprocessor signal frame */
    regspace = uc->tuc_regspace;
    if (cpu_isar_feature(aa32_vfp_simd, env_archcpu(env))) {
        regspace = restore_sigframe_vfp(env, regspace);
        if (!regspace) {
            return 1;
        }
    }
    if (arm_feature(env, ARM_FEATURE_IWMMXT)) {
        regspace = restore_sigframe_iwmmxt(env, regspace);
        if (!regspace) {
            return 1;
        }
    }

    target_restore_altstack(&uc->tuc_stack, env);

#if 0
    /* Send SIGTRAP if we're single-stepping */
    if (ptrace_cancel_bpt(current))
        send_sig(SIGTRAP, current, 1);
#endif

    return 0;
}

long do_sigreturn(CPUARMState *env)
{
    abi_ulong frame_addr;
    struct sigframe *frame = NULL;

    /*
     * Since we stacked the signal on a 64-bit boundary,
     * then 'sp' should be word aligned here.  If it's
     * not, then the user is trying to mess with us.
     */
    frame_addr = env->regs[13];
    trace_user_do_sigreturn(env, frame_addr);
    if (frame_addr & 7) {
        goto badframe;
    }

    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1)) {
        goto badframe;
    }

    if (do_sigframe_return(env,
                           frame_addr + offsetof(struct sigframe, uc),
                           &frame->uc)) {
        goto badframe;
    }

    unlock_user_struct(frame, frame_addr, 0);
    return -QEMU_ESIGRETURN;

badframe:
    unlock_user_struct(frame, frame_addr, 0);
    force_sig(TARGET_SIGSEGV);
    return -QEMU_ESIGRETURN;
}

long do_rt_sigreturn(CPUARMState *env)
{
    abi_ulong frame_addr;
    struct rt_sigframe *frame = NULL;

    /*
     * Since we stacked the signal on a 64-bit boundary,
     * then 'sp' should be word aligned here.  If it's
     * not, then the user is trying to mess with us.
     */
    frame_addr = env->regs[13];
    trace_user_do_rt_sigreturn(env, frame_addr);
    if (frame_addr & 7) {
        goto badframe;
    }

    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1)) {
        goto badframe;
    }

    if (do_sigframe_return(env,
                           frame_addr + offsetof(struct rt_sigframe, sig.uc),
                           &frame->sig.uc)) {
        goto badframe;
    }

    unlock_user_struct(frame, frame_addr, 0);
    return -QEMU_ESIGRETURN;

badframe:
    unlock_user_struct(frame, frame_addr, 0);
    force_sig(TARGET_SIGSEGV);
    return -QEMU_ESIGRETURN;
}

/*
 * EABI syscalls pass the number via r7.
 * Note that the kernel still adds the OABI syscall number to the trap,
 * presumably for backward ABI compatibility with unwinders.
 */
#define ARM_MOV_R7_IMM(X)       (0xe3a07000 | (X))
#define ARM_SWI_SYS(X)          (0xef000000 | (X) | ARM_SYSCALL_BASE)

#define THUMB_MOVS_R7_IMM(X)    (0x2700 | (X))
#define THUMB_SWI_SYS           0xdf00

static void write_arm_sigreturn(uint32_t *rc, int syscall)
{
    __put_user(ARM_MOV_R7_IMM(syscall), rc);
    __put_user(ARM_SWI_SYS(syscall), rc + 1);
    /* Wrote 8 of 12 bytes */
}

static void write_thm_sigreturn(uint32_t *rc, int syscall)
{
    __put_user(THUMB_SWI_SYS << 16 | THUMB_MOVS_R7_IMM(syscall), rc);
    /* Wrote 4 of 12 bytes */
}

/*
 * Stub needed to make sure the FD register (r9) contains the right value.
 * Use the same instruction sequence as the kernel.
 */
static void write_arm_fdpic_sigreturn(uint32_t *rc, int ofs)
{
    assert(ofs <= 0xfff);
    __put_user(0xe59d3000 | ofs, rc + 0);   /* ldr r3, [sp, #ofs] */
    __put_user(0xe8930908, rc + 1);         /* ldm r3, { r3, r9 } */
    __put_user(0xe12fff13, rc + 2);         /* bx  r3 */
    /* Wrote 12 of 12 bytes */
}

static void write_thm_fdpic_sigreturn(void *vrc, int ofs)
{
    uint16_t *rc = vrc;

    assert((ofs & ~0x3fc) == 0);
    __put_user(0x9b00 | (ofs >> 2), rc + 0);      /* ldr r3, [sp, #ofs] */
    __put_user(0xcb0c, rc + 1);                   /* ldm r3, { r2, r3 } */
    __put_user(0x4699, rc + 2);                   /* mov r9, r3 */
    __put_user(0x4710, rc + 3);                   /* bx  r2 */
    /* Wrote 8 of 12 bytes */
}

void setup_sigtramp(abi_ulong sigtramp_page)
{
    uint32_t total_size = 8 * RETCODE_BYTES;
    uint32_t *tramp = lock_user(VERIFY_WRITE, sigtramp_page, total_size, 0);

    assert(tramp != NULL);

    default_sigreturn = sigtramp_page;
    write_arm_sigreturn(&tramp[0 * RETCODE_WORDS], TARGET_NR_sigreturn);
    write_thm_sigreturn(&tramp[1 * RETCODE_WORDS], TARGET_NR_sigreturn);
    write_arm_sigreturn(&tramp[2 * RETCODE_WORDS], TARGET_NR_rt_sigreturn);
    write_thm_sigreturn(&tramp[3 * RETCODE_WORDS], TARGET_NR_rt_sigreturn);

    sigreturn_fdpic_tramp = sigtramp_page + 4 * RETCODE_BYTES;
    write_arm_fdpic_sigreturn(tramp + 4 * RETCODE_WORDS,
                              offsetof(struct sigframe, retcode[3]));
    write_thm_fdpic_sigreturn(tramp + 5 * RETCODE_WORDS,
                                offsetof(struct sigframe, retcode[3]));
    write_arm_fdpic_sigreturn(tramp + 6 * RETCODE_WORDS,
                              offsetof(struct rt_sigframe, sig.retcode[3]));
    write_thm_fdpic_sigreturn(tramp + 7 * RETCODE_WORDS,
                              offsetof(struct rt_sigframe, sig.retcode[3]));

    unlock_user(tramp, sigtramp_page, total_size);
}
