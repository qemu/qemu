#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "sysemu/kvm.h"
#include "sysemu/tcg.h"
#include "helper_regs.h"
#include "mmu-hash64.h"
#include "migration/cpu.h"
#include "qapi/error.h"
#include "qemu/main-loop.h"
#include "kvm_ppc.h"
#include "power8-pmu.h"

static void post_load_update_msr(CPUPPCState *env)
{
    target_ulong msr = env->msr;

    /*
     * Invalidate all supported msr bits except MSR_TGPR/MSR_HVB
     * before restoring.  Note that this recomputes hflags.
     */
    env->msr ^= env->msr_mask & ~((1ULL << MSR_TGPR) | MSR_HVB);
    ppc_store_msr(env, msr);

    if (tcg_enabled()) {
        pmu_update_summaries(env);
    }
}

static int get_avr(QEMUFile *f, void *pv, size_t size,
                   const VMStateField *field)
{
    ppc_avr_t *v = pv;

    v->u64[0] = qemu_get_be64(f);
    v->u64[1] = qemu_get_be64(f);

    return 0;
}

static int put_avr(QEMUFile *f, void *pv, size_t size,
                   const VMStateField *field, JSONWriter *vmdesc)
{
    ppc_avr_t *v = pv;

    qemu_put_be64(f, v->u64[0]);
    qemu_put_be64(f, v->u64[1]);
    return 0;
}

static const VMStateInfo vmstate_info_avr = {
    .name = "avr",
    .get  = get_avr,
    .put  = put_avr,
};

#define VMSTATE_AVR_ARRAY_V(_f, _s, _n, _v)                       \
    VMSTATE_SUB_ARRAY(_f, _s, 32, _n, _v, vmstate_info_avr, ppc_avr_t)

#define VMSTATE_AVR_ARRAY(_f, _s, _n)                             \
    VMSTATE_AVR_ARRAY_V(_f, _s, _n, 0)

static int get_fpr(QEMUFile *f, void *pv, size_t size,
                   const VMStateField *field)
{
    ppc_vsr_t *v = pv;

    v->VsrD(0) = qemu_get_be64(f);

    return 0;
}

static int put_fpr(QEMUFile *f, void *pv, size_t size,
                   const VMStateField *field, JSONWriter *vmdesc)
{
    ppc_vsr_t *v = pv;

    qemu_put_be64(f, v->VsrD(0));
    return 0;
}

static const VMStateInfo vmstate_info_fpr = {
    .name = "fpr",
    .get  = get_fpr,
    .put  = put_fpr,
};

#define VMSTATE_FPR_ARRAY_V(_f, _s, _n, _v)                       \
    VMSTATE_SUB_ARRAY(_f, _s, 0, _n, _v, vmstate_info_fpr, ppc_vsr_t)

#define VMSTATE_FPR_ARRAY(_f, _s, _n)                             \
    VMSTATE_FPR_ARRAY_V(_f, _s, _n, 0)

static int get_vsr(QEMUFile *f, void *pv, size_t size,
                   const VMStateField *field)
{
    ppc_vsr_t *v = pv;

    v->VsrD(1) = qemu_get_be64(f);

    return 0;
}

static int put_vsr(QEMUFile *f, void *pv, size_t size,
                   const VMStateField *field, JSONWriter *vmdesc)
{
    ppc_vsr_t *v = pv;

    qemu_put_be64(f, v->VsrD(1));
    return 0;
}

static const VMStateInfo vmstate_info_vsr = {
    .name = "vsr",
    .get  = get_vsr,
    .put  = put_vsr,
};

#define VMSTATE_VSR_ARRAY_V(_f, _s, _n, _v)                       \
    VMSTATE_SUB_ARRAY(_f, _s, 0, _n, _v, vmstate_info_vsr, ppc_vsr_t)

#define VMSTATE_VSR_ARRAY(_f, _s, _n)                             \
    VMSTATE_VSR_ARRAY_V(_f, _s, _n, 0)

static bool cpu_pre_2_8_migration(void *opaque, int version_id)
{
    PowerPCCPU *cpu = opaque;

    return cpu->pre_2_8_migration;
}

#if defined(TARGET_PPC64)
static bool cpu_pre_3_0_migration(void *opaque, int version_id)
{
    PowerPCCPU *cpu = opaque;

    return cpu->pre_3_0_migration;
}
#endif

static int cpu_pre_save(void *opaque)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;
    int i;
    uint64_t insns_compat_mask =
        PPC_INSNS_BASE | PPC_ISEL | PPC_STRING | PPC_MFTB
        | PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES
        | PPC_FLOAT_FSQRT | PPC_FLOAT_FRSQRTE | PPC_FLOAT_FRSQRTES
        | PPC_FLOAT_STFIWX | PPC_FLOAT_EXT
        | PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ
        | PPC_MEM_SYNC | PPC_MEM_EIEIO | PPC_MEM_TLBIE | PPC_MEM_TLBSYNC
        | PPC_64B | PPC_64BX | PPC_ALTIVEC
        | PPC_SEGMENT_64B | PPC_SLBI | PPC_POPCNTB | PPC_POPCNTWD;
    uint64_t insns_compat_mask2 = PPC2_VSX | PPC2_VSX207 | PPC2_DFP | PPC2_DBRX
        | PPC2_PERM_ISA206 | PPC2_DIVE_ISA206
        | PPC2_ATOMIC_ISA206 | PPC2_FP_CVT_ISA206
        | PPC2_FP_TST_ISA206 | PPC2_BCTAR_ISA207
        | PPC2_LSQ_ISA207 | PPC2_ALTIVEC_207
        | PPC2_ISA205 | PPC2_ISA207S | PPC2_FP_CVT_S64 | PPC2_TM;

    env->spr[SPR_LR] = env->lr;
    env->spr[SPR_CTR] = env->ctr;
    env->spr[SPR_XER] = cpu_read_xer(env);
#if defined(TARGET_PPC64)
    env->spr[SPR_CFAR] = env->cfar;
#endif
    env->spr[SPR_BOOKE_SPEFSCR] = env->spe_fscr;

    for (i = 0; (i < 4) && (i < env->nb_BATs); i++) {
        env->spr[SPR_DBAT0U + 2 * i] = env->DBAT[0][i];
        env->spr[SPR_DBAT0U + 2 * i + 1] = env->DBAT[1][i];
        env->spr[SPR_IBAT0U + 2 * i] = env->IBAT[0][i];
        env->spr[SPR_IBAT0U + 2 * i + 1] = env->IBAT[1][i];
    }
    for (i = 0; (i < 4) && ((i + 4) < env->nb_BATs); i++) {
        env->spr[SPR_DBAT4U + 2 * i] = env->DBAT[0][i + 4];
        env->spr[SPR_DBAT4U + 2 * i + 1] = env->DBAT[1][i + 4];
        env->spr[SPR_IBAT4U + 2 * i] = env->IBAT[0][i + 4];
        env->spr[SPR_IBAT4U + 2 * i + 1] = env->IBAT[1][i + 4];
    }

    /* Hacks for migration compatibility between 2.6, 2.7 & 2.8 */
    if (cpu->pre_2_8_migration) {
        /*
         * Mask out bits that got added to msr_mask since the versions
         * which stupidly included it in the migration stream.
         */
        target_ulong metamask = 0
#if defined(TARGET_PPC64)
            | (1ULL << MSR_TS0)
            | (1ULL << MSR_TS1)
#endif
            ;
        cpu->mig_msr_mask = env->msr_mask & ~metamask;
        cpu->mig_insns_flags = env->insns_flags & insns_compat_mask;
        /*
         * CPU models supported by old machines all have
         * PPC_MEM_TLBIE, so we set it unconditionally to allow
         * backward migration from a POWER9 host to a POWER8 host.
         */
        cpu->mig_insns_flags |= PPC_MEM_TLBIE;
        cpu->mig_insns_flags2 = env->insns_flags2 & insns_compat_mask2;
        cpu->mig_nb_BATs = env->nb_BATs;
    }
    if (cpu->pre_3_0_migration) {
        if (cpu->hash64_opts) {
            cpu->mig_slb_nr = cpu->hash64_opts->slb_size;
        }
    }

    /* Used to retain migration compatibility for pre 6.0 for 601 machines. */
    env->hflags_compat_nmsr = 0;

    return 0;
}

/*
 * Determine if a given PVR is a "close enough" match to the CPU
 * object.  For TCG and KVM PR it would probably be sufficient to
 * require an exact PVR match.  However for KVM HV the user is
 * restricted to a PVR exactly matching the host CPU.  The correct way
 * to handle this is to put the guest into an architected
 * compatibility mode.  However, to allow a more forgiving transition
 * and migration from before this was widely done, we allow migration
 * between sufficiently similar PVRs, as determined by the CPU class's
 * pvr_match() hook.
 */
static bool pvr_match(PowerPCCPU *cpu, uint32_t pvr)
{
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);

    if (pvr == pcc->pvr) {
        return true;
    }
    return pcc->pvr_match(pcc, pvr);
}

static int cpu_post_load(void *opaque, int version_id)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;
    int i;

    /*
     * If we're operating in compat mode, we should be ok as long as
     * the destination supports the same compatibility mode.
     *
     * Otherwise, however, we require that the destination has exactly
     * the same CPU model as the source.
     */

#if defined(TARGET_PPC64)
    if (cpu->compat_pvr) {
        uint32_t compat_pvr = cpu->compat_pvr;
        Error *local_err = NULL;
        int ret;

        cpu->compat_pvr = 0;
        ret = ppc_set_compat(cpu, compat_pvr, &local_err);
        if (ret < 0) {
            error_report_err(local_err);
            return ret;
        }
    } else
#endif
    {
        if (!pvr_match(cpu, env->spr[SPR_PVR])) {
            return -EINVAL;
        }
    }

    /*
     * If we're running with KVM HV, there is a chance that the guest
     * is running with KVM HV and its kernel does not have the
     * capability of dealing with a different PVR other than this
     * exact host PVR in KVM_SET_SREGS. If that happens, the
     * guest freezes after migration.
     *
     * The function kvmppc_pvr_workaround_required does this verification
     * by first checking if the kernel has the cap, returning true immediately
     * if that is the case. Otherwise, it checks if we're running in KVM PR.
     * If the guest kernel does not have the cap and we're not running KVM-PR
     * (so, it is running KVM-HV), we need to ensure that KVM_SET_SREGS will
     * receive the PVR it expects as a workaround.
     *
     */
    if (kvmppc_pvr_workaround_required(cpu)) {
        env->spr[SPR_PVR] = env->spr_cb[SPR_PVR].default_value;
    }

    env->lr = env->spr[SPR_LR];
    env->ctr = env->spr[SPR_CTR];
    cpu_write_xer(env, env->spr[SPR_XER]);
#if defined(TARGET_PPC64)
    env->cfar = env->spr[SPR_CFAR];
#endif
    env->spe_fscr = env->spr[SPR_BOOKE_SPEFSCR];

    for (i = 0; (i < 4) && (i < env->nb_BATs); i++) {
        env->DBAT[0][i] = env->spr[SPR_DBAT0U + 2 * i];
        env->DBAT[1][i] = env->spr[SPR_DBAT0U + 2 * i + 1];
        env->IBAT[0][i] = env->spr[SPR_IBAT0U + 2 * i];
        env->IBAT[1][i] = env->spr[SPR_IBAT0U + 2 * i + 1];
    }
    for (i = 0; (i < 4) && ((i + 4) < env->nb_BATs); i++) {
        env->DBAT[0][i + 4] = env->spr[SPR_DBAT4U + 2 * i];
        env->DBAT[1][i + 4] = env->spr[SPR_DBAT4U + 2 * i + 1];
        env->IBAT[0][i + 4] = env->spr[SPR_IBAT4U + 2 * i];
        env->IBAT[1][i + 4] = env->spr[SPR_IBAT4U + 2 * i + 1];
    }

    if (!cpu->vhyp) {
        ppc_store_sdr1(env, env->spr[SPR_SDR1]);
    }

    post_load_update_msr(env);

    return 0;
}

static bool fpu_needed(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    return cpu->env.insns_flags & PPC_FLOAT;
}

static const VMStateDescription vmstate_fpu = {
    .name = "cpu/fpu",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = fpu_needed,
    .fields = (VMStateField[]) {
        VMSTATE_FPR_ARRAY(env.vsr, PowerPCCPU, 32),
        VMSTATE_UINTTL(env.fpscr, PowerPCCPU),
        VMSTATE_END_OF_LIST()
    },
};

static bool altivec_needed(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    return cpu->env.insns_flags & PPC_ALTIVEC;
}

static int get_vscr(QEMUFile *f, void *opaque, size_t size,
                    const VMStateField *field)
{
    PowerPCCPU *cpu = opaque;
    ppc_store_vscr(&cpu->env, qemu_get_be32(f));
    return 0;
}

static int put_vscr(QEMUFile *f, void *opaque, size_t size,
                    const VMStateField *field, JSONWriter *vmdesc)
{
    PowerPCCPU *cpu = opaque;
    qemu_put_be32(f, ppc_get_vscr(&cpu->env));
    return 0;
}

static const VMStateInfo vmstate_vscr = {
    .name = "cpu/altivec/vscr",
    .get = get_vscr,
    .put = put_vscr,
};

static const VMStateDescription vmstate_altivec = {
    .name = "cpu/altivec",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = altivec_needed,
    .fields = (VMStateField[]) {
        VMSTATE_AVR_ARRAY(env.vsr, PowerPCCPU, 32),
        /*
         * Save the architecture value of the vscr, not the internally
         * expanded version.  Since this architecture value does not
         * exist in memory to be stored, this requires a but of hoop
         * jumping.  We want OFFSET=0 so that we effectively pass CPU
         * to the helper functions.
         */
        {
            .name = "vscr",
            .version_id = 0,
            .size = sizeof(uint32_t),
            .info = &vmstate_vscr,
            .flags = VMS_SINGLE,
            .offset = 0
        },
        VMSTATE_END_OF_LIST()
    },
};

static bool vsx_needed(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    return cpu->env.insns_flags2 & PPC2_VSX;
}

static const VMStateDescription vmstate_vsx = {
    .name = "cpu/vsx",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = vsx_needed,
    .fields = (VMStateField[]) {
        VMSTATE_VSR_ARRAY(env.vsr, PowerPCCPU, 32),
        VMSTATE_END_OF_LIST()
    },
};

#ifdef TARGET_PPC64
/* Transactional memory state */
static bool tm_needed(void *opaque)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;
    return msr_ts;
}

static const VMStateDescription vmstate_tm = {
    .name = "cpu/tm",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = tm_needed,
    .fields      = (VMStateField []) {
        VMSTATE_UINTTL_ARRAY(env.tm_gpr, PowerPCCPU, 32),
        VMSTATE_AVR_ARRAY(env.tm_vsr, PowerPCCPU, 64),
        VMSTATE_UINT64(env.tm_cr, PowerPCCPU),
        VMSTATE_UINT64(env.tm_lr, PowerPCCPU),
        VMSTATE_UINT64(env.tm_ctr, PowerPCCPU),
        VMSTATE_UINT64(env.tm_fpscr, PowerPCCPU),
        VMSTATE_UINT64(env.tm_amr, PowerPCCPU),
        VMSTATE_UINT64(env.tm_ppr, PowerPCCPU),
        VMSTATE_UINT64(env.tm_vrsave, PowerPCCPU),
        VMSTATE_UINT32(env.tm_vscr, PowerPCCPU),
        VMSTATE_UINT64(env.tm_dscr, PowerPCCPU),
        VMSTATE_UINT64(env.tm_tar, PowerPCCPU),
        VMSTATE_END_OF_LIST()
    },
};
#endif

static bool sr_needed(void *opaque)
{
#ifdef TARGET_PPC64
    PowerPCCPU *cpu = opaque;

    return !mmu_is_64bit(cpu->env.mmu_model);
#else
    return true;
#endif
}

static const VMStateDescription vmstate_sr = {
    .name = "cpu/sr",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = sr_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINTTL_ARRAY(env.sr, PowerPCCPU, 32),
        VMSTATE_END_OF_LIST()
    },
};

#ifdef TARGET_PPC64
static int get_slbe(QEMUFile *f, void *pv, size_t size,
                    const VMStateField *field)
{
    ppc_slb_t *v = pv;

    v->esid = qemu_get_be64(f);
    v->vsid = qemu_get_be64(f);

    return 0;
}

static int put_slbe(QEMUFile *f, void *pv, size_t size,
                    const VMStateField *field, JSONWriter *vmdesc)
{
    ppc_slb_t *v = pv;

    qemu_put_be64(f, v->esid);
    qemu_put_be64(f, v->vsid);
    return 0;
}

static const VMStateInfo vmstate_info_slbe = {
    .name = "slbe",
    .get  = get_slbe,
    .put  = put_slbe,
};

#define VMSTATE_SLB_ARRAY_V(_f, _s, _n, _v)                       \
    VMSTATE_ARRAY(_f, _s, _n, _v, vmstate_info_slbe, ppc_slb_t)

#define VMSTATE_SLB_ARRAY(_f, _s, _n)                             \
    VMSTATE_SLB_ARRAY_V(_f, _s, _n, 0)

static bool slb_needed(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    /* We don't support any of the old segment table based 64-bit CPUs */
    return mmu_is_64bit(cpu->env.mmu_model);
}

static int slb_post_load(void *opaque, int version_id)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;
    int i;

    /*
     * We've pulled in the raw esid and vsid values from the migration
     * stream, but we need to recompute the page size pointers
     */
    for (i = 0; i < cpu->hash64_opts->slb_size; i++) {
        if (ppc_store_slb(cpu, i, env->slb[i].esid, env->slb[i].vsid) < 0) {
            /* Migration source had bad values in its SLB */
            return -1;
        }
    }

    return 0;
}

static const VMStateDescription vmstate_slb = {
    .name = "cpu/slb",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = slb_needed,
    .post_load = slb_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_INT32_TEST(mig_slb_nr, PowerPCCPU, cpu_pre_3_0_migration),
        VMSTATE_SLB_ARRAY(env.slb, PowerPCCPU, MAX_SLB_ENTRIES),
        VMSTATE_END_OF_LIST()
    }
};
#endif /* TARGET_PPC64 */

static const VMStateDescription vmstate_tlb6xx_entry = {
    .name = "cpu/tlb6xx_entry",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINTTL(pte0, ppc6xx_tlb_t),
        VMSTATE_UINTTL(pte1, ppc6xx_tlb_t),
        VMSTATE_UINTTL(EPN, ppc6xx_tlb_t),
        VMSTATE_END_OF_LIST()
    },
};

static bool tlb6xx_needed(void *opaque)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;

    return env->nb_tlb && (env->tlb_type == TLB_6XX);
}

static const VMStateDescription vmstate_tlb6xx = {
    .name = "cpu/tlb6xx",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = tlb6xx_needed,
    .fields = (VMStateField[]) {
        VMSTATE_INT32_EQUAL(env.nb_tlb, PowerPCCPU, NULL),
        VMSTATE_STRUCT_VARRAY_POINTER_INT32(env.tlb.tlb6, PowerPCCPU,
                                            env.nb_tlb,
                                            vmstate_tlb6xx_entry,
                                            ppc6xx_tlb_t),
        VMSTATE_UINTTL_ARRAY(env.tgpr, PowerPCCPU, 4),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_tlbemb_entry = {
    .name = "cpu/tlbemb_entry",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(RPN, ppcemb_tlb_t),
        VMSTATE_UINTTL(EPN, ppcemb_tlb_t),
        VMSTATE_UINTTL(PID, ppcemb_tlb_t),
        VMSTATE_UINTTL(size, ppcemb_tlb_t),
        VMSTATE_UINT32(prot, ppcemb_tlb_t),
        VMSTATE_UINT32(attr, ppcemb_tlb_t),
        VMSTATE_END_OF_LIST()
    },
};

static bool tlbemb_needed(void *opaque)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;

    return env->nb_tlb && (env->tlb_type == TLB_EMB);
}

static const VMStateDescription vmstate_tlbemb = {
    .name = "cpu/tlb6xx",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = tlbemb_needed,
    .fields = (VMStateField[]) {
        VMSTATE_INT32_EQUAL(env.nb_tlb, PowerPCCPU, NULL),
        VMSTATE_STRUCT_VARRAY_POINTER_INT32(env.tlb.tlbe, PowerPCCPU,
                                            env.nb_tlb,
                                            vmstate_tlbemb_entry,
                                            ppcemb_tlb_t),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_tlbmas_entry = {
    .name = "cpu/tlbmas_entry",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(mas8, ppcmas_tlb_t),
        VMSTATE_UINT32(mas1, ppcmas_tlb_t),
        VMSTATE_UINT64(mas2, ppcmas_tlb_t),
        VMSTATE_UINT64(mas7_3, ppcmas_tlb_t),
        VMSTATE_END_OF_LIST()
    },
};

static bool tlbmas_needed(void *opaque)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;

    return env->nb_tlb && (env->tlb_type == TLB_MAS);
}

static const VMStateDescription vmstate_tlbmas = {
    .name = "cpu/tlbmas",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = tlbmas_needed,
    .fields = (VMStateField[]) {
        VMSTATE_INT32_EQUAL(env.nb_tlb, PowerPCCPU, NULL),
        VMSTATE_STRUCT_VARRAY_POINTER_INT32(env.tlb.tlbm, PowerPCCPU,
                                            env.nb_tlb,
                                            vmstate_tlbmas_entry,
                                            ppcmas_tlb_t),
        VMSTATE_END_OF_LIST()
    }
};

static bool compat_needed(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    assert(!(cpu->compat_pvr && !cpu->vhyp));
    return !cpu->pre_2_10_migration && cpu->compat_pvr != 0;
}

static const VMStateDescription vmstate_compat = {
    .name = "cpu/compat",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = compat_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(compat_pvr, PowerPCCPU),
        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_ppc_cpu = {
    .name = "cpu",
    .version_id = 5,
    .minimum_version_id = 5,
    .pre_save = cpu_pre_save,
    .post_load = cpu_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UNUSED(sizeof(target_ulong)), /* was _EQUAL(env.spr[SPR_PVR]) */

        /* User mode architected state */
        VMSTATE_UINTTL_ARRAY(env.gpr, PowerPCCPU, 32),
#if !defined(TARGET_PPC64)
        VMSTATE_UINTTL_ARRAY(env.gprh, PowerPCCPU, 32),
#endif
        VMSTATE_UINT32_ARRAY(env.crf, PowerPCCPU, 8),
        VMSTATE_UINTTL(env.nip, PowerPCCPU),

        /* SPRs */
        VMSTATE_UINTTL_ARRAY(env.spr, PowerPCCPU, 1024),
        VMSTATE_UINT64(env.spe_acc, PowerPCCPU),

        /* Reservation */
        VMSTATE_UINTTL(env.reserve_addr, PowerPCCPU),

        /* Supervisor mode architected state */
        VMSTATE_UINTTL(env.msr, PowerPCCPU),

        /* Backward compatible internal state */
        VMSTATE_UINTTL(env.hflags_compat_nmsr, PowerPCCPU),

        /* Sanity checking */
        VMSTATE_UINTTL_TEST(mig_msr_mask, PowerPCCPU, cpu_pre_2_8_migration),
        VMSTATE_UINT64_TEST(mig_insns_flags, PowerPCCPU, cpu_pre_2_8_migration),
        VMSTATE_UINT64_TEST(mig_insns_flags2, PowerPCCPU,
                            cpu_pre_2_8_migration),
        VMSTATE_UINT32_TEST(mig_nb_BATs, PowerPCCPU, cpu_pre_2_8_migration),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription*[]) {
        &vmstate_fpu,
        &vmstate_altivec,
        &vmstate_vsx,
        &vmstate_sr,
#ifdef TARGET_PPC64
        &vmstate_tm,
        &vmstate_slb,
#endif /* TARGET_PPC64 */
        &vmstate_tlb6xx,
        &vmstate_tlbemb,
        &vmstate_tlbmas,
        &vmstate_compat,
        NULL
    }
};
