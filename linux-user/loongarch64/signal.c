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
#include "target/loongarch/vec.h"
#include "vdso-asmoffset.h"

/* FP context was used */
#define SC_USED_FP              (1 << 0)

struct target_sigcontext {
    abi_ulong sc_pc;
    abi_ulong sc_regs[32];
    abi_uint  sc_flags;
    abi_ulong sc_extcontext[0]   QEMU_ALIGNED(16);
};

QEMU_BUILD_BUG_ON(sizeof(struct target_sigcontext) != sizeof_sigcontext);
QEMU_BUILD_BUG_ON(offsetof(struct target_sigcontext, sc_pc)
                  != offsetof_sigcontext_pc);
QEMU_BUILD_BUG_ON(offsetof(struct target_sigcontext, sc_regs)
                  != offsetof_sigcontext_gr);

#define FPU_CTX_MAGIC           0x46505501
#define FPU_CTX_ALIGN           8
struct target_fpu_context {
    abi_ulong regs[32];
    abi_ulong fcc;
    abi_uint  fcsr;
} QEMU_ALIGNED(FPU_CTX_ALIGN);

QEMU_BUILD_BUG_ON(offsetof(struct target_fpu_context, regs)
                  != offsetof_fpucontext_fr);

#define LSX_CTX_MAGIC           0x53580001
#define LSX_CTX_ALIGN           16
struct target_lsx_context {
    abi_ulong regs[2 * 32];
    abi_ulong fcc;
    abi_uint  fcsr;
} QEMU_ALIGNED(LSX_CTX_ALIGN);

#define LASX_CTX_MAGIC          0x41535801
#define LASX_CTX_ALIGN          32
struct target_lasx_context {
    abi_ulong regs[4 * 32];
    abi_ulong fcc;
    abi_uint  fcsr;
} QEMU_ALIGNED(LASX_CTX_ALIGN);

#define CONTEXT_INFO_ALIGN      16
struct target_sctx_info {
    abi_uint  magic;
    abi_uint  size;
    abi_ulong padding;
} QEMU_ALIGNED(CONTEXT_INFO_ALIGN);

QEMU_BUILD_BUG_ON(sizeof(struct target_sctx_info) != sizeof_sctx_info);

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

QEMU_BUILD_BUG_ON(sizeof(struct target_rt_sigframe)
                  != sizeof_rt_sigframe);
QEMU_BUILD_BUG_ON(offsetof(struct target_rt_sigframe, rs_uc.tuc_mcontext)
                  != offsetof_sigcontext);

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
    unsigned long size;
    unsigned int flags;
    struct ctx_layout fpu;
    struct ctx_layout lsx;
    struct ctx_layout lasx;
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

static abi_ptr setup_extcontext(CPULoongArchState *env,
                                struct extctx_layout *extctx, abi_ptr sp)
{
    memset(extctx, 0, sizeof(struct extctx_layout));

    /* Grow down, alloc "end" context info first. */
    sp = extframe_alloc(extctx, &extctx->end, 0, CONTEXT_INFO_ALIGN, sp);

    /* For qemu, there is no lazy fp context switch, so fp always present. */
    extctx->flags = SC_USED_FP;

    if (FIELD_EX64(env->CSR_EUEN, CSR_EUEN, ASXE)) {
        sp = extframe_alloc(extctx, &extctx->lasx,
                        sizeof(struct target_lasx_context), LASX_CTX_ALIGN, sp);
    } else if (FIELD_EX64(env->CSR_EUEN, CSR_EUEN, SXE)) {
        sp = extframe_alloc(extctx, &extctx->lsx,
                        sizeof(struct target_lsx_context), LSX_CTX_ALIGN, sp);
    } else {
        sp = extframe_alloc(extctx, &extctx->fpu,
                        sizeof(struct target_fpu_context), FPU_CTX_ALIGN, sp);
    }

    return sp;
}

static void setup_sigframe(CPULoongArchState *env,
                           struct target_sigcontext *sc,
                           struct extctx_layout *extctx)
{
    struct target_sctx_info *info;
    int i;

    __put_user(extctx->flags, &sc->sc_flags);
    __put_user(env->pc, &sc->sc_pc);
    __put_user(0, &sc->sc_regs[0]);
    for (i = 1; i < 32; ++i) {
        __put_user(env->gpr[i], &sc->sc_regs[i]);
    }

    /*
     * Set extension context
     */

    if (FIELD_EX64(env->CSR_EUEN, CSR_EUEN, ASXE)) {
        struct target_lasx_context *lasx_ctx;
        info = extctx->lasx.haddr;

        __put_user(LASX_CTX_MAGIC, &info->magic);
        __put_user(extctx->lasx.size, &info->size);

        lasx_ctx = (struct target_lasx_context *)(info + 1);

        for (i = 0; i < 32; ++i) {
            __put_user(env->fpr[i].vreg.UD(0), &lasx_ctx->regs[4 * i]);
            __put_user(env->fpr[i].vreg.UD(1), &lasx_ctx->regs[4 * i + 1]);
            __put_user(env->fpr[i].vreg.UD(2), &lasx_ctx->regs[4 * i + 2]);
            __put_user(env->fpr[i].vreg.UD(3), &lasx_ctx->regs[4 * i + 3]);
        }
        __put_user(read_fcc(env), &lasx_ctx->fcc);
        __put_user(env->fcsr0, &lasx_ctx->fcsr);
    } else if (FIELD_EX64(env->CSR_EUEN, CSR_EUEN, SXE)) {
        struct target_lsx_context *lsx_ctx;
        info = extctx->lsx.haddr;

        __put_user(LSX_CTX_MAGIC, &info->magic);
        __put_user(extctx->lsx.size, &info->size);

        lsx_ctx = (struct target_lsx_context *)(info + 1);

        for (i = 0; i < 32; ++i) {
            __put_user(env->fpr[i].vreg.UD(0), &lsx_ctx->regs[2 * i]);
            __put_user(env->fpr[i].vreg.UD(1), &lsx_ctx->regs[2 * i + 1]);
        }
        __put_user(read_fcc(env), &lsx_ctx->fcc);
        __put_user(env->fcsr0, &lsx_ctx->fcsr);
    } else {
        struct target_fpu_context *fpu_ctx;
        info = extctx->fpu.haddr;

        __put_user(FPU_CTX_MAGIC, &info->magic);
        __put_user(extctx->fpu.size, &info->size);

        fpu_ctx = (struct target_fpu_context *)(info + 1);

        for (i = 0; i < 32; ++i) {
            __put_user(env->fpr[i].vreg.UD(0), &fpu_ctx->regs[i]);
        }
        __put_user(read_fcc(env), &fpu_ctx->fcc);
        __put_user(env->fcsr0, &fpu_ctx->fcsr);
    }

    /*
     * Set end context
     */
    info = extctx->end.haddr;
    __put_user(0, &info->magic);
    __put_user(0, &info->size);
}

static bool parse_extcontext(struct extctx_layout *extctx, abi_ptr frame)
{
    memset(extctx, 0, sizeof(*extctx));

    while (1) {
        abi_uint magic, size;

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
        case LSX_CTX_MAGIC:
            if (size < (sizeof(struct target_sctx_info) +
                        sizeof(struct target_lsx_context))) {
                return false;
            }
            extctx->lsx.gaddr = frame;
            extctx->lsx.size = size;
            extctx->size += size;
            break;
        case LASX_CTX_MAGIC:
            if (size < (sizeof(struct target_sctx_info) +
                        sizeof(struct target_lasx_context))) {
                return false;
            }
            extctx->lasx.gaddr = frame;
            extctx->lasx.size = size;
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
    abi_ulong fcc;

    __get_user(env->pc, &sc->sc_pc);
    for (i = 1; i < 32; ++i) {
        __get_user(env->gpr[i], &sc->sc_regs[i]);
    }

    if (extctx->lasx.haddr) {
        struct target_lasx_context *lasx_ctx =
            extctx->lasx.haddr + sizeof(struct target_sctx_info);

        for (i = 0; i < 32; ++i) {
            __get_user(env->fpr[i].vreg.UD(0), &lasx_ctx->regs[4 * i]);
            __get_user(env->fpr[i].vreg.UD(1), &lasx_ctx->regs[4 * i + 1]);
            __get_user(env->fpr[i].vreg.UD(2), &lasx_ctx->regs[4 * i + 2]);
            __get_user(env->fpr[i].vreg.UD(3), &lasx_ctx->regs[4 * i + 3]);
        }
        __get_user(fcc, &lasx_ctx->fcc);
        write_fcc(env, fcc);
        __get_user(env->fcsr0, &lasx_ctx->fcsr);
        restore_fp_status(env);
    } else if (extctx->lsx.haddr) {
        struct target_lsx_context *lsx_ctx =
            extctx->lsx.haddr + sizeof(struct target_sctx_info);

        for (i = 0; i < 32; ++i) {
            __get_user(env->fpr[i].vreg.UD(0), &lsx_ctx->regs[2 * i]);
            __get_user(env->fpr[i].vreg.UD(1), &lsx_ctx->regs[2 * i + 1]);
        }
        __get_user(fcc, &lsx_ctx->fcc);
        write_fcc(env, fcc);
        __get_user(env->fcsr0, &lsx_ctx->fcsr);
        restore_fp_status(env);
    } else if (extctx->fpu.haddr) {
        struct target_fpu_context *fpu_ctx =
            extctx->fpu.haddr + sizeof(struct target_sctx_info);

        for (i = 0; i < 32; ++i) {
            __get_user(env->fpr[i].vreg.UD(0), &fpu_ctx->regs[i]);
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
    sp = setup_extcontext(env, extctx, sp);
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

    if (FIELD_EX64(env->CSR_EUEN, CSR_EUEN, ASXE)) {
        extctx.lasx.haddr = (void *)frame + (extctx.lasx.gaddr - frame_addr);
        extctx.end.haddr = (void *)frame + (extctx.end.gaddr - frame_addr);
    } else if (FIELD_EX64(env->CSR_EUEN, CSR_EUEN, SXE)) {
        extctx.lsx.haddr = (void *)frame + (extctx.lsx.gaddr - frame_addr);
        extctx.end.haddr = (void *)frame + (extctx.end.gaddr - frame_addr);
    } else {
        extctx.fpu.haddr = (void *)frame + (extctx.fpu.gaddr - frame_addr);
        extctx.end.haddr = (void *)frame + (extctx.end.gaddr - frame_addr);
    }

    frame->rs_info = *info;

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

    if (extctx.lasx.gaddr) {
        extctx.lasx.haddr = (void *)frame + (extctx.lasx.gaddr - frame_addr);
    } else if (extctx.lsx.gaddr) {
        extctx.lsx.haddr = (void *)frame + (extctx.lsx.gaddr - frame_addr);
    } else if (extctx.fpu.gaddr) {
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
