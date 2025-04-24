/*
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"

#include "cpu.h"
#include "exec/tswap.h"

void x86_cpu_xsave_all_areas(X86CPU *cpu, void *buf, uint32_t buflen)
{
    CPUX86State *env = &cpu->env;
    const ExtSaveArea *e, *f;
    int i;

    X86LegacyXSaveArea *legacy;
    X86XSaveHeader *header;
    uint16_t cwd, swd, twd;

    memset(buf, 0, buflen);

    e = &x86_ext_save_areas[XSTATE_FP_BIT];

    legacy = buf + e->offset;
    header = buf + e->offset + sizeof(*legacy);

    twd = 0;
    swd = env->fpus & ~(7 << 11);
    swd |= (env->fpstt & 7) << 11;
    cwd = env->fpuc;
    for (i = 0; i < 8; ++i) {
        twd |= (!env->fptags[i]) << i;
    }
    legacy->fcw = cwd;
    legacy->fsw = swd;
    legacy->ftw = twd;
    legacy->fpop = env->fpop;
    legacy->fpip = env->fpip;
    legacy->fpdp = env->fpdp;
    memcpy(&legacy->fpregs, env->fpregs,
           sizeof(env->fpregs));
    legacy->mxcsr = env->mxcsr;

    for (i = 0; i < CPU_NB_REGS; i++) {
        uint8_t *xmm = legacy->xmm_regs[i];

        stq_p(xmm,     env->xmm_regs[i].ZMM_Q(0));
        stq_p(xmm + 8, env->xmm_regs[i].ZMM_Q(1));
    }

    header->xstate_bv = env->xstate_bv;

    e = &x86_ext_save_areas[XSTATE_YMM_BIT];
    if (e->size && e->offset) {
        XSaveAVX *avx;

        avx = buf + e->offset;

        for (i = 0; i < CPU_NB_REGS; i++) {
            uint8_t *ymmh = avx->ymmh[i];

            stq_p(ymmh,     env->xmm_regs[i].ZMM_Q(2));
            stq_p(ymmh + 8, env->xmm_regs[i].ZMM_Q(3));
        }
    }

    e = &x86_ext_save_areas[XSTATE_BNDREGS_BIT];
    if (e->size && e->offset) {
        XSaveBNDREG *bndreg;
        XSaveBNDCSR *bndcsr;

        f = &x86_ext_save_areas[XSTATE_BNDCSR_BIT];
        assert(f->size);
        assert(f->offset);

        bndreg = buf + e->offset;
        bndcsr = buf + f->offset;

        memcpy(&bndreg->bnd_regs, env->bnd_regs,
               sizeof(env->bnd_regs));
        bndcsr->bndcsr = env->bndcs_regs;
    }

    e = &x86_ext_save_areas[XSTATE_OPMASK_BIT];
    if (e->size && e->offset) {
        XSaveOpmask *opmask;
        XSaveZMM_Hi256 *zmm_hi256;
#ifdef TARGET_X86_64
        XSaveHi16_ZMM *hi16_zmm;
#endif

        f = &x86_ext_save_areas[XSTATE_ZMM_Hi256_BIT];
        assert(f->size);
        assert(f->offset);

        opmask = buf + e->offset;
        zmm_hi256 = buf + f->offset;

        memcpy(&opmask->opmask_regs, env->opmask_regs,
               sizeof(env->opmask_regs));

        for (i = 0; i < CPU_NB_REGS; i++) {
            uint8_t *zmmh = zmm_hi256->zmm_hi256[i];

            stq_p(zmmh,      env->xmm_regs[i].ZMM_Q(4));
            stq_p(zmmh + 8,  env->xmm_regs[i].ZMM_Q(5));
            stq_p(zmmh + 16, env->xmm_regs[i].ZMM_Q(6));
            stq_p(zmmh + 24, env->xmm_regs[i].ZMM_Q(7));
        }

#ifdef TARGET_X86_64
        f = &x86_ext_save_areas[XSTATE_Hi16_ZMM_BIT];
        assert(f->size);
        assert(f->offset);

        hi16_zmm = buf + f->offset;

        memcpy(&hi16_zmm->hi16_zmm, &env->xmm_regs[16],
               16 * sizeof(env->xmm_regs[16]));
#endif
    }

#ifdef TARGET_X86_64
    e = &x86_ext_save_areas[XSTATE_PKRU_BIT];
    if (e->size && e->offset) {
        XSavePKRU *pkru = buf + e->offset;

        memcpy(pkru, &env->pkru, sizeof(env->pkru));
    }

    e = &x86_ext_save_areas[XSTATE_XTILE_CFG_BIT];
    if (e->size && e->offset) {
        XSaveXTILECFG *tilecfg = buf + e->offset;

        memcpy(tilecfg, &env->xtilecfg, sizeof(env->xtilecfg));
    }

    e = &x86_ext_save_areas[XSTATE_XTILE_DATA_BIT];
    if (e->size && e->offset && buflen >= e->size + e->offset) {
        XSaveXTILEDATA *tiledata = buf + e->offset;

        memcpy(tiledata, &env->xtiledata, sizeof(env->xtiledata));
    }
#endif
}

void x86_cpu_xrstor_all_areas(X86CPU *cpu, const void *buf, uint32_t buflen)
{
    CPUX86State *env = &cpu->env;
    const ExtSaveArea *e, *f, *g;
    int i;

    const X86LegacyXSaveArea *legacy;
    const X86XSaveHeader *header;
    uint16_t cwd, swd, twd;

    e = &x86_ext_save_areas[XSTATE_FP_BIT];

    legacy = buf + e->offset;
    header = buf + e->offset + sizeof(*legacy);

    cwd = legacy->fcw;
    swd = legacy->fsw;
    twd = legacy->ftw;
    env->fpop = legacy->fpop;
    env->fpstt = (swd >> 11) & 7;
    env->fpus = swd;
    env->fpuc = cwd;
    for (i = 0; i < 8; ++i) {
        env->fptags[i] = !((twd >> i) & 1);
    }
    env->fpip = legacy->fpip;
    env->fpdp = legacy->fpdp;
    env->mxcsr = legacy->mxcsr;
    memcpy(env->fpregs, &legacy->fpregs,
           sizeof(env->fpregs));

    for (i = 0; i < CPU_NB_REGS; i++) {
        const uint8_t *xmm = legacy->xmm_regs[i];

        env->xmm_regs[i].ZMM_Q(0) = ldq_p(xmm);
        env->xmm_regs[i].ZMM_Q(1) = ldq_p(xmm + 8);
    }

    env->xstate_bv = header->xstate_bv;

    e = &x86_ext_save_areas[XSTATE_YMM_BIT];
    if (e->size && e->offset) {
        const XSaveAVX *avx;

        avx = buf + e->offset;
        for (i = 0; i < CPU_NB_REGS; i++) {
            const uint8_t *ymmh = avx->ymmh[i];

            env->xmm_regs[i].ZMM_Q(2) = ldq_p(ymmh);
            env->xmm_regs[i].ZMM_Q(3) = ldq_p(ymmh + 8);
        }
    }

    e = &x86_ext_save_areas[XSTATE_BNDREGS_BIT];
    if (e->size && e->offset) {
        const XSaveBNDREG *bndreg;
        const XSaveBNDCSR *bndcsr;

        f = &x86_ext_save_areas[XSTATE_BNDCSR_BIT];
        assert(f->size);
        assert(f->offset);

        bndreg = buf + e->offset;
        bndcsr = buf + f->offset;

        memcpy(env->bnd_regs, &bndreg->bnd_regs,
               sizeof(env->bnd_regs));
        env->bndcs_regs = bndcsr->bndcsr;
    }

    e = &x86_ext_save_areas[XSTATE_OPMASK_BIT];
    if (e->size && e->offset) {
        const XSaveOpmask *opmask;
        const XSaveZMM_Hi256 *zmm_hi256;
#ifdef TARGET_X86_64
        const XSaveHi16_ZMM *hi16_zmm;
#endif

        f = &x86_ext_save_areas[XSTATE_ZMM_Hi256_BIT];
        assert(f->size);
        assert(f->offset);

        g = &x86_ext_save_areas[XSTATE_Hi16_ZMM_BIT];
        assert(g->size);
        assert(g->offset);

        opmask = buf + e->offset;
        zmm_hi256 = buf + f->offset;
#ifdef TARGET_X86_64
        hi16_zmm = buf + g->offset;
#endif

        memcpy(env->opmask_regs, &opmask->opmask_regs,
               sizeof(env->opmask_regs));

        for (i = 0; i < CPU_NB_REGS; i++) {
            const uint8_t *zmmh = zmm_hi256->zmm_hi256[i];

            env->xmm_regs[i].ZMM_Q(4) = ldq_p(zmmh);
            env->xmm_regs[i].ZMM_Q(5) = ldq_p(zmmh + 8);
            env->xmm_regs[i].ZMM_Q(6) = ldq_p(zmmh + 16);
            env->xmm_regs[i].ZMM_Q(7) = ldq_p(zmmh + 24);
        }

#ifdef TARGET_X86_64
        memcpy(&env->xmm_regs[16], &hi16_zmm->hi16_zmm,
               16 * sizeof(env->xmm_regs[16]));
#endif
    }

#ifdef TARGET_X86_64
    e = &x86_ext_save_areas[XSTATE_PKRU_BIT];
    if (e->size && e->offset) {
        const XSavePKRU *pkru;

        pkru = buf + e->offset;
        memcpy(&env->pkru, pkru, sizeof(env->pkru));
    }

    e = &x86_ext_save_areas[XSTATE_XTILE_CFG_BIT];
    if (e->size && e->offset) {
        const XSaveXTILECFG *tilecfg = buf + e->offset;

        memcpy(&env->xtilecfg, tilecfg, sizeof(env->xtilecfg));
    }

    e = &x86_ext_save_areas[XSTATE_XTILE_DATA_BIT];
    if (e->size && e->offset && buflen >= e->size + e->offset) {
        const XSaveXTILEDATA *tiledata = buf + e->offset;

        memcpy(&env->xtiledata, tiledata, sizeof(env->xtiledata));
    }
#endif
}
