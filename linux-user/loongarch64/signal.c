/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch emulation of Linux signals
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "qemu.h"
#include "user-internals.h"
#include "signal-common.h"
#include "linux-user/trace.h"

#include "target/loongarch/internals.h"

/* FP context was used */
#define SC_USED_FP              (1 << 0)

struct target_sigcontext {
    uint64_t sc_pc;
    uint64_t sc_regs[32];
    uint32_t sc_flags;
    uint64_t sc_extcontext[0]   QEMU_ALIGNED(16);
};


#define FPU_CTX_MAGIC           0x46505501
#define FPU_CTX_ALIGN           8
struct target_fpu_context {
    uint64_t regs[32];
    uint64_t fcc;
    uint32_t fcsr;
} QEMU_ALIGNED(FPU_CTX_ALIGN);

#define CONTEXT_INFO_ALIGN      16
struct target_sctx_info {
    uint32_t magic;
    uint32_t size;
    uint64_t padding;
} QEMU_ALIGNED(CONTEXT_INFO_ALIGN);

struct target_ucontext {
    abi_ulong tuc_flags;
    abi_ptr tuc_link;
    target_stack_t tuc_stack;
    target_sigset_t tuc_sigmask;
    uint8_t __unused[1024 / 8 - sizeof(target_sigset_t)];
    struct target_sigcontext tuc_mcontext;
};

struct target_rt_sigframe {
    struct target_siginfo        rs_info;
    struct target_ucontext       rs_uc;
};

/*
 * These two structures are not present in guest memory, are private
 * to the signal implementation, but are largely copied from the
 * kernel's signal implementation.
 */
struct ctx_layout {
    void *haddr;
    abi_ptr gaddr;
    unsigned int size;
};

struct extctx_layout {
    unsigned int size;
    unsigned int flags;
    struct ctx_layout fpu;
    struct ctx_layout end;
};

static abi_ptr extframe_alloc(struct extctx_layout *extctx,
                              struct ctx_layout *sctx, unsigned size,
                              unsigned align, abi_ptr orig_sp)
{
    abi_ptr sp = orig_sp;

    sp -= sizeof(struct target_sctx_info) + size;
    align = MAX(align, CONTEXT_INFO_ALIGN);
    sp = ROUND_DOWN(sp, align);
    sctx->gaddr = sp;

    size = orig_sp - sp;
    sctx->size = size;
    extctx->size += size;

    return sp;
}

static abi_ptr setup_extcontext(struct extctx_layout *extctx, abi_ptr sp)
{
    memset(extctx, 0, sizeof(struct extctx_layout));

    /* Grow down, alloc "end" context info first. */
    sp = extframe_alloc(extctx, &extctx->end, 0, CONTEXT_INFO_ALIGN, sp);

    /* For qemu, there is no lazy fp context switch, so fp always present. */
    extctx->flags = SC_USED_FP;
    sp = extframe_alloc(extctx, &extctx->fpu,
                        sizeof(struct target_rt_sigframe), FPU_CTX_ALIGN, sp);

    return sp;
}

static void setup_sigframe(CPULoongArchState *env,
                           struct target_sigcontext *sc,
                           struct extctx_layout *extctx)
{
    struct target_sctx_info *info;
    struct target_fpu_context *fpu_ctx;
    int i;

    __put_user(extctx->flags, &sc->sc_flags);
    __put_user(env->pc, &sc->sc_pc);
    __put_user(0, &sc->sc_regs[0]);
    for (i = 1; i < 32; ++i) {
        __put_user(env->gpr[i], &sc->sc_regs[i]);
    }

    /*
     * Set fpu context
     */
    info = extctx->fpu.haddr;
    __put_user(FPU_CTX_MAGIC, &info->magic);
    __put_user(extctx->fpu.size, &info->size);

    fpu_ctx = (struct target_fpu_context *)(info + 1);
    for (i = 0; i < 32; ++i) {
        __put_user(env->fpr[i].vreg.D(0), &fpu_ctx->regs[i]);
    }
    __put_user(read_fcc(env), &fpu_ctx->fcc);
    __put_user(env->fcsr0, &fpu_ctx->fcsr);

    /*
     * Set end context
     */
    info = extctx->end.haddr;
    __put_user(0, &info->magic);
    __put_user(extctx->end.size, &info->size);
}

static bool parse_extcontext(struct extctx_layout *extctx, abi_ptr frame)
{
    memset(extctx, 0, sizeof(*extctx));

    while (1) {
        uint32_t magic, size;

        if (get_user_u32(magic, frame) || get_user_u32(size, frame + 4)) {
            return false;
        }

        switch (magic) {
        case 0: /* END */
            extctx->end.gaddr = frame;
            extctx->end.size = size;
            extctx->size += size;
            return true;

        case FPU_CTX_MAGIC:
            if (size < (sizeof(struct target_sctx_info) +
                        sizeof(struct target_fpu_context))) {
                return false;
            }
            extctx->fpu.gaddr = frame;
            extctx->fpu.size = size;
            extctx->size += size;
            break;
        default:
            return false;
        }

        frame += size;
    }
}

static void restore_sigframe(CPULoongArchState *env,
                             struct target_sigcontext *sc,
                             struct extctx_layout *extctx)
{
    int i;

    __get_user(env->pc, &sc->sc_pc);
    for (i = 1; i < 32; ++i) {
        __get_user(env->gpr[i], &sc->sc_regs[i]);
    }

    if (extctx->fpu.haddr) {
        struct target_fpu_context *fpu_ctx =
            extctx->fpu.haddr + sizeof(struct target_sctx_info);
        uint64_t fcc;

        for (i = 0; i < 32; ++i) {
            __get_user(env->fpr[i].vreg.D(0), &fpu_ctx->regs[i]);
        }
        __get_user(fcc, &fpu_ctx->fcc);
        write_fcc(env, fcc);
        __get_user(env->fcsr0, &fpu_ctx->fcsr);
        restore_fp_status(env);
    }
}

/*
 * Determine which stack to use.
 */
static abi_ptr get_sigframe(struct target_sigaction *ka,
                            CPULoongArchState *env,
                            struct extctx_layout *extctx)
{
    abi_ulong sp;

    sp = target_sigsp(get_sp_from_cpustate(env), ka);
    sp = ROUND_DOWN(sp, 16);
    sp = setup_extcontext(extctx, sp);
    sp -= sizeof(struct target_rt_sigframe);

    assert(QEMU_IS_ALIGNED(sp, 16));

    return sp;
}

void setup_rt_frame(int sig, struct target_sigaction *ka,
                    target_siginfo_t *info,
                    target_sigset_t *set, CPULoongArchState *env)
{
    struct target_rt_sigframe *frame;
    struct extctx_layout extctx;
    abi_ptr frame_addr;
    int i;

    frame_addr = get_sigframe(ka, env, &extctx);
    trace_user_setup_rt_frame(env, frame_addr);

    frame = lock_user(VERIFY_WRITE, frame_addr,
                      sizeof(*frame) + extctx.size, 0);
    if (!frame) {
        force_sigsegv(sig);
        return;
    }
    extctx.fpu.haddr = (void *)frame + (extctx.fpu.gaddr - frame_addr);
    extctx.end.haddr = (void *)frame + (extctx.end.gaddr - frame_addr);

    tswap_siginfo(&frame->rs_info, info);

    __put_user(0, &frame->rs_uc.tuc_flags);
    __put_user(0, &frame->rs_uc.tuc_link);
    target_save_altstack(&frame->rs_uc.tuc_stack, env);

    setup_sigframe(env, &frame->rs_uc.tuc_mcontext, &extctx);

    for (i = 0; i < TARGET_NSIG_WORDS; i++) {
        __put_user(set->sig[i], &frame->rs_uc.tuc_sigmask.sig[i]);
    }

    env->gpr[4] = sig;
    env->gpr[5] = frame_addr + offsetof(struct target_rt_sigframe, rs_info);
    env->gpr[6] = frame_addr + offsetof(struct target_rt_sigframe, rs_uc);
    env->gpr[3] = frame_addr;
    env->gpr[1] = default_rt_sigreturn;

    env->pc = ka->_sa_handler;
    unlock_user(frame, frame_addr, sizeof(*frame) + extctx.size);
}

long do_rt_sigreturn(CPULoongArchState *env)
{
    struct target_rt_sigframe *frame;
    struct extctx_layout extctx;
    abi_ulong frame_addr;
    sigset_t blocked;

    frame_addr = env->gpr[3];
    trace_user_do_rt_sigreturn(env, frame_addr);

    if (!parse_extcontext(&extctx, frame_addr + sizeof(*frame))) {
        goto badframe;
    }

    frame = lock_user(VERIFY_READ, frame_addr,
                      sizeof(*frame) + extctx.size, 1);
    if (!frame) {
        goto badframe;
    }
    if (extctx.fpu.gaddr) {
        extctx.fpu.haddr = (void *)frame + (extctx.fpu.gaddr - frame_addr);
    }

    target_to_host_sigset(&blocked, &frame->rs_uc.tuc_sigmask);
    set_sigmask(&blocked);

    restore_sigframe(env, &frame->rs_uc.tuc_mcontext, &extctx);

    target_restore_altstack(&frame->rs_uc.tuc_stack, env);

    unlock_user(frame, frame_addr, 0);
    return -QEMU_ESIGRETURN;

 badframe:
    force_sig(TARGET_SIGSEGV);
    return -QEMU_ESIGRETURN;
}

void setup_sigtramp(abi_ulong sigtramp_page)
{
    uint32_t *tramp = lock_user(VERIFY_WRITE, sigtramp_page, 8, 0);
    assert(tramp != NULL);

    __put_user(0x03822c0b, tramp + 0);  /* ori     a7, zero, 0x8b */
    __put_user(0x002b0000, tramp + 1);  /* syscall 0 */

    default_rt_sigreturn = sigtramp_page;
    unlock_user(tramp, sigtramp_page, 8);
}
