/*
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/error-report.h"

#include "cpu.h"

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

    e = &x86_ext_save_areas[XSTATE_APX_BIT];
    if (e->size && e->offset && buflen) {
        XSaveAPX *apx = buf + e->offset;

        memcpy(apx, &env->regs[CPU_NB_REGS],
               sizeof(env->regs[CPU_NB_REGS]) * (CPU_NB_EREGS - CPU_NB_REGS));
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

    e = &x86_ext_save_areas[XSTATE_APX_BIT];
    if (e->size && e->offset) {
        const XSaveAPX *apx = buf + e->offset;

        memcpy(&env->regs[CPU_NB_REGS], apx,
               sizeof(env->regs[CPU_NB_REGS]) * (CPU_NB_EREGS - CPU_NB_REGS));
    }
#endif
}

#define XSTATE_BV_IN_HDR  offsetof(X86XSaveHeader, xstate_bv)
#define XCOMP_BV_IN_HDR   offsetof(X86XSaveHeader, xcomp_bvo)

typedef struct X86XSaveAreaView {
    /* 512 bytes */
    X86LegacyXSaveArea legacy;
    /* 64 bytes */
    X86XSaveHeader     header;
    /* ...followed by individual xsave areas */
} X86XSaveAreaView;

#define XSAVE_XSTATE_BV_OFFSET  offsetof(X86XSaveAreaView, header.xstate_bv)
#define XSAVE_XCOMP_BV_OFFSET   offsetof(X86XSaveAreaView, header.xcomp_bv)
#define XSAVE_EXT_OFFSET        (sizeof(X86LegacyXSaveArea) + \
                                 sizeof(X86XSaveHeader))

/**
 * decompact_xsave_area - Convert compacted XSAVE format to standard format
 * @buf: Source buffer containing compacted XSAVE data
 * @buflen: Size of source buffer
 * @env: CPU state where the standard format buffer will be written to
 *
 * Accelerator backends like MSHV might return XSAVE state in compacted format
 * (XSAVEC). The state components have to be packed contiguously without gaps.
 * The XSAVE qemu buffers are in standard format where each component has a
 * fixed offset.
 *
 * Returns: 0 on success, negative errno on failure
 */
int decompact_xsave_area(const void *buf, size_t buflen, CPUX86State *env)
{
    uint64_t compacted_xstate_bv, compacted_xcomp_bv, compacted_layout_bv;
    size_t xsave_offset;
    uint64_t *xcomp_bv;
    size_t i;
    uint32_t eax, ebx, ecx, edx;
    uint32_t size, dst_off;
    bool align64, supervisor;
    uint64_t guest_xcr0, *xstate_bv;

    compacted_xstate_bv = *(uint64_t *)(buf + XSAVE_XSTATE_BV_OFFSET);
    compacted_xcomp_bv  = *(uint64_t *)(buf + XSAVE_XCOMP_BV_OFFSET);

    /* This function only handles compacted format (bit 63 set) */
    assert((compacted_xcomp_bv >> 63) & 1);

    /* Low bits of XCOMP_BV describe which components are in the layout */
    compacted_layout_bv = compacted_xcomp_bv & ~(1ULL << 63);

    /* Zero out buffer, then copy legacy region (FP + SSE) and header as-is */
    memset(env->xsave_buf, 0, env->xsave_buf_len);
    memcpy(env->xsave_buf, buf, XSAVE_EXT_OFFSET);

    /*
     * We mask XSTATE_BV with the guest's supported XCR0 because:
     * 1. Supervisor state (IA32_XSS) is hypervisor-managed, we don't use
     *    this state for migration.
     * 2. Features disabled at partition creation (e.g. AMX) must be excluded
     */
    guest_xcr0 = ((uint64_t)env->features[FEAT_XSAVE_XCR0_HI] << 32) |
                 env->features[FEAT_XSAVE_XCR0_LO];
    xstate_bv = (uint64_t *)(env->xsave_buf + XSAVE_XSTATE_BV_OFFSET);
    *xstate_bv &= guest_xcr0;

    /* Clear bit 63 - output is standard format, not compacted */
    xcomp_bv = (uint64_t *)(env->xsave_buf + XSAVE_XCOMP_BV_OFFSET);
    *xcomp_bv = *xcomp_bv & ~(1ULL << 63);

    /*
     * Process each extended state component in the compacted layout.
     * Components 0 and 1 (FP and SSE) are in the legacy region, so we
     * start at component 2. For each component:
     * - Calculate its offset in the compacted source (contiguous layout)
     * - Get its fixed offset in the standard destination from CPUID
     * - Copy if the component has non-init state (bit set in XSTATE_BV)
     */
    xsave_offset = XSAVE_EXT_OFFSET;
    for (i = 2; i < 63; i++) {
        if (((compacted_layout_bv >> i) & 1) == 0) {
            continue;
        }

        /* Query guest CPUID for this component's size and standard offset */
        cpu_x86_cpuid(env, 0xD, i, &eax, &ebx, &ecx, &edx);

        size = eax;
        dst_off = ebx;
        align64 = (ecx & (1u << 1)) != 0;
        supervisor = (ecx & ESA_FEATURE_XSS_MASK) != 0;

        /* Component is in the layout but unknown to the guest CPUID model */
        if (size == 0) {
            /*
             * The hypervisor might expose a component that has no
             * representation in the guest CPUID model. We query the host to
             * retrieve the size of the component, so we can skip over it.
             */
            host_cpuid(0xD, i, &eax, &ebx, &ecx, &edx);
            size = eax;
            align64 = (ecx & (1u << 1)) != 0;
            if (size == 0) {
                error_report("xsave component %zu: size unknown to both "
                             "guest and host CPUID", i);
                return -EINVAL;
            }

            if (align64) {
                xsave_offset = QEMU_ALIGN_UP(xsave_offset, 64);
            }

            if (xsave_offset + size > buflen) {
                error_report("xsave component %zu overruns source buffer: "
                             "offset=%zu size=%u buflen=%zu",
                             i, xsave_offset, size, buflen);
                return -E2BIG;
            }

            xsave_offset += size;
            continue;
        }

        if (align64) {
            xsave_offset = QEMU_ALIGN_UP(xsave_offset, 64);
        }

        if ((xsave_offset + size) > buflen) {
            error_report("xsave component %zu overruns source buffer: "
                         "offset=%zu size=%u buflen=%zu",
                         i, xsave_offset, size, buflen);
            return -E2BIG;
        }

        if ((dst_off + size) > env->xsave_buf_len) {
            error_report("xsave component %zu overruns destination buffer: "
                         "offset=%u size=%u buflen=%zu",
                         i, dst_off, size, (size_t)env->xsave_buf_len);
            return -E2BIG;
        }

        /*
         * Copy components marked present in XSTATE_BV to guest model.
         *
         * NB: Supervisor state is skipped b/c there is no slot in the
         * standard format XSAVE buffer (CET state is migrated via MSRs,
         * others supervisor state isn't migrated).
         */
        if (((compacted_xstate_bv >> i) & 1) != 0 && !supervisor) {
            memcpy(env->xsave_buf + dst_off, buf + xsave_offset, size);
        }

        xsave_offset += size;
    }

    return 0;
}

/**
 * compact_xsave_area - Convert standard XSAVE format to compacted format
 * @env: CPU state containing the standard format XSAVE buffer
 * @buf: Destination buffer for compacted XSAVE data (to send to hypervisor)
 * @buflen: Size of destination buffer
 *
 * Accelerator backends like MSHV might expect XSAVE state in compacted format
 * (XSAVEC). The state components are packed contiguously without gaps.
 * The XSAVE qemu buffers are in standard format where each component has a
 * fixed offset.
 *
 * This function converts from standard to compacted format, it accepts a
 * pre-allocated destination buffer of sufficient size, it is the
 * responsibility of the caller to ensure the buffer is big enough.
 *
 * Returns: total size of compacted XSAVE data written to @buf
 */
int compact_xsave_area(CPUX86State *env, void *buf, size_t buflen)
{
    uint64_t *xcomp_bv;
    size_t i;
    uint32_t eax, ebx, ecx, edx;
    uint32_t size, src_off;
    bool align64;
    size_t compact_offset;
    uint64_t host_xcr0_mask, guest_xcr0;

    /* Zero out buffer, then copy legacy region (FP + SSE) and header as-is */
    memset(buf, 0, buflen);
    memcpy(buf, env->xsave_buf, XSAVE_EXT_OFFSET);

    /*
     * Set XCOMP_BV to indicate compacted format (bit 63) and which
     * components are in the layout.
     *
     * We must explicitly set XCOMP_BV because x86_cpu_xsave_all_areas()
     * produces standard format with XCOMP_BV=0 (buffer is zeroed and only
     * XSTATE_BV is set in the header).
     *
     * XCOMP_BV must reflect the partition's XSAVE capability, not the
     * guest's current XCR0 (env->xcr0). These differ b/c:
     * - A guest's XCR0 is what the guest OS has enabled via XSETBV
     * - The partition's XCR0 mask is the hypervisor's save/restore capability
     *
     * The hypervisor uses XSAVES which saves based on its capability, so the
     * XCOMP_BV value in the buffer we send back must match that capability.
     *
     * We intersect the host XCR0 with the guest's supported XCR0 features
     * (FEAT_XSAVE_XCR0_*) so that features disabled at partition creation
     * (e.g. AMX) are excluded from the compacted layout.
     */
    host_cpuid(0xD, 0, &eax, &ebx, &ecx, &edx);
    host_xcr0_mask = ((uint64_t)edx << 32) | eax;
    guest_xcr0 = ((uint64_t)env->features[FEAT_XSAVE_XCR0_HI] << 32) |
                 env->features[FEAT_XSAVE_XCR0_LO];
    host_xcr0_mask &= guest_xcr0;
    xcomp_bv = buf + XSAVE_XCOMP_BV_OFFSET;
    *xcomp_bv = host_xcr0_mask | (1ULL << 63);

    /*
     * Process each extended state component in the host's XCR0.
     * The compacted layout must match XCOMP_BV (host capability).
     *
     * For each component:
     * - Get its size and standard offset from host CPUID
     * - Apply 64-byte alignment if required
     * - Copy data only if guest has this component (bit set in env->xcr0)
     * - Always advance offset to maintain correct layout
     */
    compact_offset = XSAVE_EXT_OFFSET;
    for (i = 2; i < 63; i++) {
        if (!((host_xcr0_mask >> i) & 1)) {
            continue;
        }

        /* Query host CPUID for this component's size and standard offset */
        host_cpuid(0xD, i, &eax, &ebx, &ecx, &edx);
        size = eax;
        src_off = ebx;
        align64 = (ecx >> 1) & 1;

        if (size == 0) {
            /* Component in host xcr0 but unknown - shouldn't happen */
            continue;
        }

        /* Apply 64-byte alignment if required by this component */
        if (align64) {
            compact_offset = QEMU_ALIGN_UP(compact_offset, 64);
        }

        /*
         * Only copy data if guest has this component enabled in XCR0.
         * Otherwise the component remains zeroed (init state), but we
         * still advance the offset to maintain the correct layout.
         */
        if ((env->xcr0 >> i) & 1) {
            memcpy(buf + compact_offset, env->xsave_buf + src_off, size);
        }

        compact_offset += size;
    }

    return compact_offset;
}
