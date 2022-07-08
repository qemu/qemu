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

struct target_sigcontext {
    uint64_t fault_address;
    /* AArch64 registers */
    uint64_t regs[31];
    uint64_t sp;
    uint64_t pc;
    uint64_t pstate;
    /* 4K reserved for FP/SIMD state and future expansion */
    char __reserved[4096] __attribute__((__aligned__(16)));
};

struct target_ucontext {
    abi_ulong tuc_flags;
    abi_ulong tuc_link;
    target_stack_t tuc_stack;
    target_sigset_t tuc_sigmask;
    /* glibc uses a 1024-bit sigset_t */
    char __unused[1024 / 8 - sizeof(target_sigset_t)];
    /* last for future expansion */
    struct target_sigcontext tuc_mcontext;
};

/*
 * Header to be used at the beginning of structures extending the user
 * context. Such structures must be placed after the rt_sigframe on the stack
 * and be 16-byte aligned. The last structure must be a dummy one with the
 * magic and size set to 0.
 */
struct target_aarch64_ctx {
    uint32_t magic;
    uint32_t size;
};

#define TARGET_FPSIMD_MAGIC 0x46508001

struct target_fpsimd_context {
    struct target_aarch64_ctx head;
    uint32_t fpsr;
    uint32_t fpcr;
    uint64_t vregs[32 * 2]; /* really uint128_t vregs[32] */
};

#define TARGET_EXTRA_MAGIC  0x45585401

struct target_extra_context {
    struct target_aarch64_ctx head;
    uint64_t datap; /* 16-byte aligned pointer to extra space cast to __u64 */
    uint32_t size; /* size in bytes of the extra space */
    uint32_t reserved[3];
};

#define TARGET_SVE_MAGIC    0x53564501

struct target_sve_context {
    struct target_aarch64_ctx head;
    uint16_t vl;
    uint16_t flags;
    uint16_t reserved[2];
    /* The actual SVE data immediately follows.  It is laid out
     * according to TARGET_SVE_SIG_{Z,P}REG_OFFSET, based off of
     * the original struct pointer.
     */
};

#define TARGET_SVE_VQ_BYTES  16

#define TARGET_SVE_SIG_ZREG_SIZE(VQ)  ((VQ) * TARGET_SVE_VQ_BYTES)
#define TARGET_SVE_SIG_PREG_SIZE(VQ)  ((VQ) * (TARGET_SVE_VQ_BYTES / 8))

#define TARGET_SVE_SIG_REGS_OFFSET \
    QEMU_ALIGN_UP(sizeof(struct target_sve_context), TARGET_SVE_VQ_BYTES)
#define TARGET_SVE_SIG_ZREG_OFFSET(VQ, N) \
    (TARGET_SVE_SIG_REGS_OFFSET + TARGET_SVE_SIG_ZREG_SIZE(VQ) * (N))
#define TARGET_SVE_SIG_PREG_OFFSET(VQ, N) \
    (TARGET_SVE_SIG_ZREG_OFFSET(VQ, 32) + TARGET_SVE_SIG_PREG_SIZE(VQ) * (N))
#define TARGET_SVE_SIG_FFR_OFFSET(VQ) \
    (TARGET_SVE_SIG_PREG_OFFSET(VQ, 16))
#define TARGET_SVE_SIG_CONTEXT_SIZE(VQ) \
    (TARGET_SVE_SIG_PREG_OFFSET(VQ, 17))

#define TARGET_SVE_SIG_FLAG_SM  1

#define TARGET_ZA_MAGIC        0x54366345

struct target_za_context {
    struct target_aarch64_ctx head;
    uint16_t vl;
    uint16_t reserved[3];
    /* The actual ZA data immediately follows. */
};

#define TARGET_ZA_SIG_REGS_OFFSET \
    QEMU_ALIGN_UP(sizeof(struct target_za_context), TARGET_SVE_VQ_BYTES)
#define TARGET_ZA_SIG_ZAV_OFFSET(VQ, N) \
    (TARGET_ZA_SIG_REGS_OFFSET + (VQ) * TARGET_SVE_VQ_BYTES * (N))
#define TARGET_ZA_SIG_CONTEXT_SIZE(VQ) \
    TARGET_ZA_SIG_ZAV_OFFSET(VQ, VQ * TARGET_SVE_VQ_BYTES)

struct target_rt_sigframe {
    struct target_siginfo info;
    struct target_ucontext uc;
};

struct target_rt_frame_record {
    uint64_t fp;
    uint64_t lr;
};

static void target_setup_general_frame(struct target_rt_sigframe *sf,
                                       CPUARMState *env, target_sigset_t *set)
{
    int i;

    __put_user(0, &sf->uc.tuc_flags);
    __put_user(0, &sf->uc.tuc_link);

    target_save_altstack(&sf->uc.tuc_stack, env);

    for (i = 0; i < 31; i++) {
        __put_user(env->xregs[i], &sf->uc.tuc_mcontext.regs[i]);
    }
    __put_user(env->xregs[31], &sf->uc.tuc_mcontext.sp);
    __put_user(env->pc, &sf->uc.tuc_mcontext.pc);
    __put_user(pstate_read(env), &sf->uc.tuc_mcontext.pstate);

    __put_user(env->exception.vaddress, &sf->uc.tuc_mcontext.fault_address);

    for (i = 0; i < TARGET_NSIG_WORDS; i++) {
        __put_user(set->sig[i], &sf->uc.tuc_sigmask.sig[i]);
    }
}

static void target_setup_fpsimd_record(struct target_fpsimd_context *fpsimd,
                                       CPUARMState *env)
{
    int i;

    __put_user(TARGET_FPSIMD_MAGIC, &fpsimd->head.magic);
    __put_user(sizeof(struct target_fpsimd_context), &fpsimd->head.size);
    __put_user(vfp_get_fpsr(env), &fpsimd->fpsr);
    __put_user(vfp_get_fpcr(env), &fpsimd->fpcr);

    for (i = 0; i < 32; i++) {
        uint64_t *q = aa64_vfp_qreg(env, i);
#if TARGET_BIG_ENDIAN
        __put_user(q[0], &fpsimd->vregs[i * 2 + 1]);
        __put_user(q[1], &fpsimd->vregs[i * 2]);
#else
        __put_user(q[0], &fpsimd->vregs[i * 2]);
        __put_user(q[1], &fpsimd->vregs[i * 2 + 1]);
#endif
    }
}

static void target_setup_extra_record(struct target_extra_context *extra,
                                      uint64_t datap, uint32_t extra_size)
{
    __put_user(TARGET_EXTRA_MAGIC, &extra->head.magic);
    __put_user(sizeof(struct target_extra_context), &extra->head.size);
    __put_user(datap, &extra->datap);
    __put_user(extra_size, &extra->size);
}

static void target_setup_end_record(struct target_aarch64_ctx *end)
{
    __put_user(0, &end->magic);
    __put_user(0, &end->size);
}

static void target_setup_sve_record(struct target_sve_context *sve,
                                    CPUARMState *env, int size)
{
    int i, j, vq = sve_vq(env);

    memset(sve, 0, sizeof(*sve));
    __put_user(TARGET_SVE_MAGIC, &sve->head.magic);
    __put_user(size, &sve->head.size);
    __put_user(vq * TARGET_SVE_VQ_BYTES, &sve->vl);
    if (FIELD_EX64(env->svcr, SVCR, SM)) {
        __put_user(TARGET_SVE_SIG_FLAG_SM, &sve->flags);
    }

    /* Note that SVE regs are stored as a byte stream, with each byte element
     * at a subsequent address.  This corresponds to a little-endian store
     * of our 64-bit hunks.
     */
    for (i = 0; i < 32; ++i) {
        uint64_t *z = (void *)sve + TARGET_SVE_SIG_ZREG_OFFSET(vq, i);
        for (j = 0; j < vq * 2; ++j) {
            __put_user_e(env->vfp.zregs[i].d[j], z + j, le);
        }
    }
    for (i = 0; i <= 16; ++i) {
        uint16_t *p = (void *)sve + TARGET_SVE_SIG_PREG_OFFSET(vq, i);
        for (j = 0; j < vq; ++j) {
            uint64_t r = env->vfp.pregs[i].p[j >> 2];
            __put_user_e(r >> ((j & 3) * 16), p + j, le);
        }
    }
}

static void target_setup_za_record(struct target_za_context *za,
                                   CPUARMState *env, int size)
{
    int vq = sme_vq(env);
    int vl = vq * TARGET_SVE_VQ_BYTES;
    int i, j;

    memset(za, 0, sizeof(*za));
    __put_user(TARGET_ZA_MAGIC, &za->head.magic);
    __put_user(size, &za->head.size);
    __put_user(vl, &za->vl);

    if (size == TARGET_ZA_SIG_CONTEXT_SIZE(0)) {
        return;
    }
    assert(size == TARGET_ZA_SIG_CONTEXT_SIZE(vq));

    /*
     * Note that ZA vectors are stored as a byte stream,
     * with each byte element at a subsequent address.
     */
    for (i = 0; i < vl; ++i) {
        uint64_t *z = (void *)za + TARGET_ZA_SIG_ZAV_OFFSET(vq, i);
        for (j = 0; j < vq * 2; ++j) {
            __put_user_e(env->zarray[i].d[j], z + j, le);
        }
    }
}

static void target_restore_general_frame(CPUARMState *env,
                                         struct target_rt_sigframe *sf)
{
    sigset_t set;
    uint64_t pstate;
    int i;

    target_to_host_sigset(&set, &sf->uc.tuc_sigmask);
    set_sigmask(&set);

    for (i = 0; i < 31; i++) {
        __get_user(env->xregs[i], &sf->uc.tuc_mcontext.regs[i]);
    }

    __get_user(env->xregs[31], &sf->uc.tuc_mcontext.sp);
    __get_user(env->pc, &sf->uc.tuc_mcontext.pc);
    __get_user(pstate, &sf->uc.tuc_mcontext.pstate);
    pstate_write(env, pstate);
}

static void target_restore_fpsimd_record(CPUARMState *env,
                                         struct target_fpsimd_context *fpsimd)
{
    uint32_t fpsr, fpcr;
    int i;

    __get_user(fpsr, &fpsimd->fpsr);
    vfp_set_fpsr(env, fpsr);
    __get_user(fpcr, &fpsimd->fpcr);
    vfp_set_fpcr(env, fpcr);

    for (i = 0; i < 32; i++) {
        uint64_t *q = aa64_vfp_qreg(env, i);
#if TARGET_BIG_ENDIAN
        __get_user(q[0], &fpsimd->vregs[i * 2 + 1]);
        __get_user(q[1], &fpsimd->vregs[i * 2]);
#else
        __get_user(q[0], &fpsimd->vregs[i * 2]);
        __get_user(q[1], &fpsimd->vregs[i * 2 + 1]);
#endif
    }
}

static bool target_restore_sve_record(CPUARMState *env,
                                      struct target_sve_context *sve,
                                      int size, int *svcr)
{
    int i, j, vl, vq, flags;
    bool sm;

    __get_user(vl, &sve->vl);
    __get_user(flags, &sve->flags);

    sm = flags & TARGET_SVE_SIG_FLAG_SM;

    /* The cpu must support Streaming or Non-streaming SVE. */
    if (sm
        ? !cpu_isar_feature(aa64_sme, env_archcpu(env))
        : !cpu_isar_feature(aa64_sve, env_archcpu(env))) {
        return false;
    }

    /*
     * Note that we cannot use sve_vq() because that depends on the
     * current setting of PSTATE.SM, not the state to be restored.
     */
    vq = sve_vqm1_for_el_sm(env, 0, sm) + 1;

    /* Reject mismatched VL. */
    if (vl != vq * TARGET_SVE_VQ_BYTES) {
        return false;
    }

    /* Accept empty record -- used to clear PSTATE.SM. */
    if (size <= sizeof(*sve)) {
        return true;
    }

    /* Reject non-empty but incomplete record. */
    if (size < TARGET_SVE_SIG_CONTEXT_SIZE(vq)) {
        return false;
    }

    *svcr = FIELD_DP64(*svcr, SVCR, SM, sm);

    /*
     * Note that SVE regs are stored as a byte stream, with each byte element
     * at a subsequent address.  This corresponds to a little-endian load
     * of our 64-bit hunks.
     */
    for (i = 0; i < 32; ++i) {
        uint64_t *z = (void *)sve + TARGET_SVE_SIG_ZREG_OFFSET(vq, i);
        for (j = 0; j < vq * 2; ++j) {
            __get_user_e(env->vfp.zregs[i].d[j], z + j, le);
        }
    }
    for (i = 0; i <= 16; ++i) {
        uint16_t *p = (void *)sve + TARGET_SVE_SIG_PREG_OFFSET(vq, i);
        for (j = 0; j < vq; ++j) {
            uint16_t r;
            __get_user_e(r, p + j, le);
            if (j & 3) {
                env->vfp.pregs[i].p[j >> 2] |= (uint64_t)r << ((j & 3) * 16);
            } else {
                env->vfp.pregs[i].p[j >> 2] = r;
            }
        }
    }
    return true;
}

static bool target_restore_za_record(CPUARMState *env,
                                     struct target_za_context *za,
                                     int size, int *svcr)
{
    int i, j, vl, vq;

    if (!cpu_isar_feature(aa64_sme, env_archcpu(env))) {
        return false;
    }

    __get_user(vl, &za->vl);
    vq = sme_vq(env);

    /* Reject mismatched VL. */
    if (vl != vq * TARGET_SVE_VQ_BYTES) {
        return false;
    }

    /* Accept empty record -- used to clear PSTATE.ZA. */
    if (size <= TARGET_ZA_SIG_CONTEXT_SIZE(0)) {
        return true;
    }

    /* Reject non-empty but incomplete record. */
    if (size < TARGET_ZA_SIG_CONTEXT_SIZE(vq)) {
        return false;
    }

    *svcr = FIELD_DP64(*svcr, SVCR, ZA, 1);

    for (i = 0; i < vl; ++i) {
        uint64_t *z = (void *)za + TARGET_ZA_SIG_ZAV_OFFSET(vq, i);
        for (j = 0; j < vq * 2; ++j) {
            __get_user_e(env->zarray[i].d[j], z + j, le);
        }
    }
    return true;
}

static int target_restore_sigframe(CPUARMState *env,
                                   struct target_rt_sigframe *sf)
{
    struct target_aarch64_ctx *ctx, *extra = NULL;
    struct target_fpsimd_context *fpsimd = NULL;
    struct target_sve_context *sve = NULL;
    struct target_za_context *za = NULL;
    uint64_t extra_datap = 0;
    bool used_extra = false;
    int sve_size = 0;
    int za_size = 0;
    int svcr = 0;

    target_restore_general_frame(env, sf);

    ctx = (struct target_aarch64_ctx *)sf->uc.tuc_mcontext.__reserved;
    while (ctx) {
        uint32_t magic, size, extra_size;

        __get_user(magic, &ctx->magic);
        __get_user(size, &ctx->size);
        switch (magic) {
        case 0:
            if (size != 0) {
                goto err;
            }
            if (used_extra) {
                ctx = NULL;
            } else {
                ctx = extra;
                used_extra = true;
            }
            continue;

        case TARGET_FPSIMD_MAGIC:
            if (fpsimd || size != sizeof(struct target_fpsimd_context)) {
                goto err;
            }
            fpsimd = (struct target_fpsimd_context *)ctx;
            break;

        case TARGET_SVE_MAGIC:
            if (sve || size < sizeof(struct target_sve_context)) {
                goto err;
            }
            sve = (struct target_sve_context *)ctx;
            sve_size = size;
            break;

        case TARGET_ZA_MAGIC:
            if (za || size < sizeof(struct target_za_context)) {
                goto err;
            }
            za = (struct target_za_context *)ctx;
            za_size = size;
            break;

        case TARGET_EXTRA_MAGIC:
            if (extra || size != sizeof(struct target_extra_context)) {
                goto err;
            }
            __get_user(extra_datap,
                       &((struct target_extra_context *)ctx)->datap);
            __get_user(extra_size,
                       &((struct target_extra_context *)ctx)->size);
            extra = lock_user(VERIFY_READ, extra_datap, extra_size, 0);
            if (!extra) {
                return 1;
            }
            break;

        default:
            /* Unknown record -- we certainly didn't generate it.
             * Did we in fact get out of sync?
             */
            goto err;
        }
        ctx = (void *)ctx + size;
    }

    /* Require FPSIMD always.  */
    if (fpsimd) {
        target_restore_fpsimd_record(env, fpsimd);
    } else {
        goto err;
    }

    /* SVE data, if present, overwrites FPSIMD data.  */
    if (sve && !target_restore_sve_record(env, sve, sve_size, &svcr)) {
        goto err;
    }
    if (za && !target_restore_za_record(env, za, za_size, &svcr)) {
        goto err;
    }
    if (env->svcr != svcr) {
        env->svcr = svcr;
        arm_rebuild_hflags(env);
    }
    unlock_user(extra, extra_datap, 0);
    return 0;

 err:
    unlock_user(extra, extra_datap, 0);
    return 1;
}

static abi_ulong get_sigframe(struct target_sigaction *ka,
                              CPUARMState *env, int size)
{
    abi_ulong sp;

    sp = target_sigsp(get_sp_from_cpustate(env), ka);

    sp = (sp - size) & ~15;

    return sp;
}

typedef struct {
    int total_size;
    int extra_base;
    int extra_size;
    int std_end_ofs;
    int extra_ofs;
    int extra_end_ofs;
} target_sigframe_layout;

static int alloc_sigframe_space(int this_size, target_sigframe_layout *l)
{
    /* Make sure there will always be space for the end marker.  */
    const int std_size = sizeof(struct target_rt_sigframe)
                         - sizeof(struct target_aarch64_ctx);
    int this_loc = l->total_size;

    if (l->extra_base) {
        /* Once we have begun an extra space, all allocations go there.  */
        l->extra_size += this_size;
    } else if (this_size + this_loc > std_size) {
        /* This allocation does not fit in the standard space.  */
        /* Allocate the extra record.  */
        l->extra_ofs = this_loc;
        l->total_size += sizeof(struct target_extra_context);

        /* Allocate the standard end record.  */
        l->std_end_ofs = l->total_size;
        l->total_size += sizeof(struct target_aarch64_ctx);

        /* Allocate the requested record.  */
        l->extra_base = this_loc = l->total_size;
        l->extra_size = this_size;
    }
    l->total_size += this_size;

    return this_loc;
}

static void target_setup_frame(int usig, struct target_sigaction *ka,
                               target_siginfo_t *info, target_sigset_t *set,
                               CPUARMState *env)
{
    target_sigframe_layout layout = {
        /* Begin with the size pointing to the reserved space.  */
        .total_size = offsetof(struct target_rt_sigframe,
                               uc.tuc_mcontext.__reserved),
    };
    int fpsimd_ofs, fr_ofs, sve_ofs = 0, za_ofs = 0;
    int sve_size = 0, za_size = 0;
    struct target_rt_sigframe *frame;
    struct target_rt_frame_record *fr;
    abi_ulong frame_addr, return_addr;

    /* FPSIMD record is always in the standard space.  */
    fpsimd_ofs = alloc_sigframe_space(sizeof(struct target_fpsimd_context),
                                      &layout);

    /* SVE state needs saving only if it exists.  */
    if (cpu_isar_feature(aa64_sve, env_archcpu(env)) ||
        cpu_isar_feature(aa64_sme, env_archcpu(env))) {
        sve_size = QEMU_ALIGN_UP(TARGET_SVE_SIG_CONTEXT_SIZE(sve_vq(env)), 16);
        sve_ofs = alloc_sigframe_space(sve_size, &layout);
    }
    if (cpu_isar_feature(aa64_sme, env_archcpu(env))) {
        /* ZA state needs saving only if it is enabled.  */
        if (FIELD_EX64(env->svcr, SVCR, ZA)) {
            za_size = TARGET_ZA_SIG_CONTEXT_SIZE(sme_vq(env));
        } else {
            za_size = TARGET_ZA_SIG_CONTEXT_SIZE(0);
        }
        za_ofs = alloc_sigframe_space(za_size, &layout);
    }

    if (layout.extra_ofs) {
        /* Reserve space for the extra end marker.  The standard end marker
         * will have been allocated when we allocated the extra record.
         */
        layout.extra_end_ofs
            = alloc_sigframe_space(sizeof(struct target_aarch64_ctx), &layout);
    } else {
        /* Reserve space for the standard end marker.
         * Do not use alloc_sigframe_space because we cheat
         * std_size therein to reserve space for this.
         */
        layout.std_end_ofs = layout.total_size;
        layout.total_size += sizeof(struct target_aarch64_ctx);
    }

    /* We must always provide at least the standard 4K reserved space,
     * even if we don't use all of it (this is part of the ABI)
     */
    layout.total_size = MAX(layout.total_size,
                            sizeof(struct target_rt_sigframe));

    /*
     * Reserve space for the standard frame unwind pair: fp, lr.
     * Despite the name this is not a "real" record within the frame.
     */
    fr_ofs = layout.total_size;
    layout.total_size += sizeof(struct target_rt_frame_record);

    frame_addr = get_sigframe(ka, env, layout.total_size);
    trace_user_setup_frame(env, frame_addr);
    frame = lock_user(VERIFY_WRITE, frame_addr, layout.total_size, 0);
    if (!frame) {
        goto give_sigsegv;
    }

    target_setup_general_frame(frame, env, set);
    target_setup_fpsimd_record((void *)frame + fpsimd_ofs, env);
    target_setup_end_record((void *)frame + layout.std_end_ofs);
    if (layout.extra_ofs) {
        target_setup_extra_record((void *)frame + layout.extra_ofs,
                                  frame_addr + layout.extra_base,
                                  layout.extra_size);
        target_setup_end_record((void *)frame + layout.extra_end_ofs);
    }
    if (sve_ofs) {
        target_setup_sve_record((void *)frame + sve_ofs, env, sve_size);
    }
    if (za_ofs) {
        target_setup_za_record((void *)frame + za_ofs, env, za_size);
    }

    /* Set up the stack frame for unwinding.  */
    fr = (void *)frame + fr_ofs;
    __put_user(env->xregs[29], &fr->fp);
    __put_user(env->xregs[30], &fr->lr);

    if (ka->sa_flags & TARGET_SA_RESTORER) {
        return_addr = ka->sa_restorer;
    } else {
        return_addr = default_rt_sigreturn;
    }
    env->xregs[0] = usig;
    env->xregs[29] = frame_addr + fr_ofs;
    env->xregs[30] = return_addr;
    env->xregs[31] = frame_addr;
    env->pc = ka->_sa_handler;

    /* Invoke the signal handler as if by indirect call.  */
    if (cpu_isar_feature(aa64_bti, env_archcpu(env))) {
        env->btype = 2;
    }

    /*
     * Invoke the signal handler with both SM and ZA disabled.
     * When clearing SM, ResetSVEState, per SMSTOP.
     */
    if (FIELD_EX64(env->svcr, SVCR, SM)) {
        arm_reset_sve_state(env);
    }
    if (env->svcr) {
        env->svcr = 0;
        arm_rebuild_hflags(env);
    }

    if (info) {
        tswap_siginfo(&frame->info, info);
        env->xregs[1] = frame_addr + offsetof(struct target_rt_sigframe, info);
        env->xregs[2] = frame_addr + offsetof(struct target_rt_sigframe, uc);
    }

    unlock_user(frame, frame_addr, layout.total_size);
    return;

 give_sigsegv:
    unlock_user(frame, frame_addr, layout.total_size);
    force_sigsegv(usig);
}

void setup_rt_frame(int sig, struct target_sigaction *ka,
                    target_siginfo_t *info, target_sigset_t *set,
                    CPUARMState *env)
{
    target_setup_frame(sig, ka, info, set, env);
}

void setup_frame(int sig, struct target_sigaction *ka,
                 target_sigset_t *set, CPUARMState *env)
{
    target_setup_frame(sig, ka, 0, set, env);
}

long do_rt_sigreturn(CPUARMState *env)
{
    struct target_rt_sigframe *frame = NULL;
    abi_ulong frame_addr = env->xregs[31];

    trace_user_do_rt_sigreturn(env, frame_addr);
    if (frame_addr & 15) {
        goto badframe;
    }

    if  (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1)) {
        goto badframe;
    }

    if (target_restore_sigframe(env, frame)) {
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

long do_sigreturn(CPUARMState *env)
{
    return do_rt_sigreturn(env);
}

void setup_sigtramp(abi_ulong sigtramp_page)
{
    uint32_t *tramp = lock_user(VERIFY_WRITE, sigtramp_page, 8, 0);
    assert(tramp != NULL);

    /*
     * mov x8,#__NR_rt_sigreturn; svc #0
     * Since these are instructions they need to be put as little-endian
     * regardless of target default or current CPU endianness.
     */
    __put_user_e(0xd2801168, &tramp[0], le);
    __put_user_e(0xd4000001, &tramp[1], le);

    default_rt_sigreturn = sigtramp_page;
    unlock_user(tramp, sigtramp_page, 8);
}
