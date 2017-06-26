/*
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qapi/error.h"

#include "qemu-common.h"
#include "cpu.h"

void x86_cpu_xsave_all_areas(X86CPU *cpu, X86XSaveArea *buf)
{
    CPUX86State *env = &cpu->env;
    X86XSaveArea *xsave = buf;

    uint16_t cwd, swd, twd;
    int i;
    memset(xsave, 0, sizeof(X86XSaveArea));
    twd = 0;
    swd = env->fpus & ~(7 << 11);
    swd |= (env->fpstt & 7) << 11;
    cwd = env->fpuc;
    for (i = 0; i < 8; ++i) {
        twd |= (!env->fptags[i]) << i;
    }
    xsave->legacy.fcw = cwd;
    xsave->legacy.fsw = swd;
    xsave->legacy.ftw = twd;
    xsave->legacy.fpop = env->fpop;
    xsave->legacy.fpip = env->fpip;
    xsave->legacy.fpdp = env->fpdp;
    memcpy(&xsave->legacy.fpregs, env->fpregs,
            sizeof env->fpregs);
    xsave->legacy.mxcsr = env->mxcsr;
    xsave->header.xstate_bv = env->xstate_bv;
    memcpy(&xsave->bndreg_state.bnd_regs, env->bnd_regs,
            sizeof env->bnd_regs);
    xsave->bndcsr_state.bndcsr = env->bndcs_regs;
    memcpy(&xsave->opmask_state.opmask_regs, env->opmask_regs,
            sizeof env->opmask_regs);

    for (i = 0; i < CPU_NB_REGS; i++) {
        uint8_t *xmm = xsave->legacy.xmm_regs[i];
        uint8_t *ymmh = xsave->avx_state.ymmh[i];
        uint8_t *zmmh = xsave->zmm_hi256_state.zmm_hi256[i];
        stq_p(xmm,     env->xmm_regs[i].ZMM_Q(0));
        stq_p(xmm+8,   env->xmm_regs[i].ZMM_Q(1));
        stq_p(ymmh,    env->xmm_regs[i].ZMM_Q(2));
        stq_p(ymmh+8,  env->xmm_regs[i].ZMM_Q(3));
        stq_p(zmmh,    env->xmm_regs[i].ZMM_Q(4));
        stq_p(zmmh+8,  env->xmm_regs[i].ZMM_Q(5));
        stq_p(zmmh+16, env->xmm_regs[i].ZMM_Q(6));
        stq_p(zmmh+24, env->xmm_regs[i].ZMM_Q(7));
    }

#ifdef TARGET_X86_64
    memcpy(&xsave->hi16_zmm_state.hi16_zmm, &env->xmm_regs[16],
            16 * sizeof env->xmm_regs[16]);
    memcpy(&xsave->pkru_state, &env->pkru, sizeof env->pkru);
#endif

}

void x86_cpu_xrstor_all_areas(X86CPU *cpu, const X86XSaveArea *buf)
{

    CPUX86State *env = &cpu->env;
    const X86XSaveArea *xsave = buf;

    int i;
    uint16_t cwd, swd, twd;
    cwd = xsave->legacy.fcw;
    swd = xsave->legacy.fsw;
    twd = xsave->legacy.ftw;
    env->fpop = xsave->legacy.fpop;
    env->fpstt = (swd >> 11) & 7;
    env->fpus = swd;
    env->fpuc = cwd;
    for (i = 0; i < 8; ++i) {
        env->fptags[i] = !((twd >> i) & 1);
    }
    env->fpip = xsave->legacy.fpip;
    env->fpdp = xsave->legacy.fpdp;
    env->mxcsr = xsave->legacy.mxcsr;
    memcpy(env->fpregs, &xsave->legacy.fpregs,
            sizeof env->fpregs);
    env->xstate_bv = xsave->header.xstate_bv;
    memcpy(env->bnd_regs, &xsave->bndreg_state.bnd_regs,
            sizeof env->bnd_regs);
    env->bndcs_regs = xsave->bndcsr_state.bndcsr;
    memcpy(env->opmask_regs, &xsave->opmask_state.opmask_regs,
            sizeof env->opmask_regs);

    for (i = 0; i < CPU_NB_REGS; i++) {
        const uint8_t *xmm = xsave->legacy.xmm_regs[i];
        const uint8_t *ymmh = xsave->avx_state.ymmh[i];
        const uint8_t *zmmh = xsave->zmm_hi256_state.zmm_hi256[i];
        env->xmm_regs[i].ZMM_Q(0) = ldq_p(xmm);
        env->xmm_regs[i].ZMM_Q(1) = ldq_p(xmm+8);
        env->xmm_regs[i].ZMM_Q(2) = ldq_p(ymmh);
        env->xmm_regs[i].ZMM_Q(3) = ldq_p(ymmh+8);
        env->xmm_regs[i].ZMM_Q(4) = ldq_p(zmmh);
        env->xmm_regs[i].ZMM_Q(5) = ldq_p(zmmh+8);
        env->xmm_regs[i].ZMM_Q(6) = ldq_p(zmmh+16);
        env->xmm_regs[i].ZMM_Q(7) = ldq_p(zmmh+24);
    }

#ifdef TARGET_X86_64
    memcpy(&env->xmm_regs[16], &xsave->hi16_zmm_state.hi16_zmm,
           16 * sizeof env->xmm_regs[16]);
    memcpy(&env->pkru, &xsave->pkru_state, sizeof env->pkru);
#endif

}
