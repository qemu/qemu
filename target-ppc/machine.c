#include "qemu/osdep.h"
#include "qemu-common.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "hw/hw.h"
#include "hw/boards.h"
#include "sysemu/kvm.h"
#include "helper_regs.h"
#include "mmu-hash64.h"
#include "migration/cpu.h"
#include "exec/exec-all.h"

static int cpu_load_old(QEMUFile *f, void *opaque, int version_id)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;
    unsigned int i, j;
    target_ulong sdr1;
    uint32_t fpscr;
    target_ulong xer;

    for (i = 0; i < 32; i++)
        qemu_get_betls(f, &env->gpr[i]);
#if !defined(TARGET_PPC64)
    for (i = 0; i < 32; i++)
        qemu_get_betls(f, &env->gprh[i]);
#endif
    qemu_get_betls(f, &env->lr);
    qemu_get_betls(f, &env->ctr);
    for (i = 0; i < 8; i++)
        qemu_get_be32s(f, &env->crf[i]);
    qemu_get_betls(f, &xer);
    cpu_write_xer(env, xer);
    qemu_get_betls(f, &env->reserve_addr);
    qemu_get_betls(f, &env->msr);
    for (i = 0; i < 4; i++)
        qemu_get_betls(f, &env->tgpr[i]);
    for (i = 0; i < 32; i++) {
        union {
            float64 d;
            uint64_t l;
        } u;
        u.l = qemu_get_be64(f);
        env->fpr[i] = u.d;
    }
    qemu_get_be32s(f, &fpscr);
    env->fpscr = fpscr;
    qemu_get_sbe32s(f, &env->access_type);
#if defined(TARGET_PPC64)
    qemu_get_betls(f, &env->spr[SPR_ASR]);
    qemu_get_sbe32s(f, &env->slb_nr);
#endif
    qemu_get_betls(f, &sdr1);
    for (i = 0; i < 32; i++)
        qemu_get_betls(f, &env->sr[i]);
    for (i = 0; i < 2; i++)
        for (j = 0; j < 8; j++)
            qemu_get_betls(f, &env->DBAT[i][j]);
    for (i = 0; i < 2; i++)
        for (j = 0; j < 8; j++)
            qemu_get_betls(f, &env->IBAT[i][j]);
    qemu_get_sbe32s(f, &env->nb_tlb);
    qemu_get_sbe32s(f, &env->tlb_per_way);
    qemu_get_sbe32s(f, &env->nb_ways);
    qemu_get_sbe32s(f, &env->last_way);
    qemu_get_sbe32s(f, &env->id_tlbs);
    qemu_get_sbe32s(f, &env->nb_pids);
    if (env->tlb.tlb6) {
        // XXX assumes 6xx
        for (i = 0; i < env->nb_tlb; i++) {
            qemu_get_betls(f, &env->tlb.tlb6[i].pte0);
            qemu_get_betls(f, &env->tlb.tlb6[i].pte1);
            qemu_get_betls(f, &env->tlb.tlb6[i].EPN);
        }
    }
    for (i = 0; i < 4; i++)
        qemu_get_betls(f, &env->pb[i]);
    for (i = 0; i < 1024; i++)
        qemu_get_betls(f, &env->spr[i]);
    if (!env->external_htab) {
        ppc_store_sdr1(env, sdr1);
    }
    qemu_get_be32s(f, &env->vscr);
    qemu_get_be64s(f, &env->spe_acc);
    qemu_get_be32s(f, &env->spe_fscr);
    qemu_get_betls(f, &env->msr_mask);
    qemu_get_be32s(f, &env->flags);
    qemu_get_sbe32s(f, &env->error_code);
    qemu_get_be32s(f, &env->pending_interrupts);
    qemu_get_be32s(f, &env->irq_input_state);
    for (i = 0; i < POWERPC_EXCP_NB; i++)
        qemu_get_betls(f, &env->excp_vectors[i]);
    qemu_get_betls(f, &env->excp_prefix);
    qemu_get_betls(f, &env->ivor_mask);
    qemu_get_betls(f, &env->ivpr_mask);
    qemu_get_betls(f, &env->hreset_vector);
    qemu_get_betls(f, &env->nip);
    qemu_get_betls(f, &env->hflags);
    qemu_get_betls(f, &env->hflags_nmsr);
    qemu_get_sbe32(f); /* Discard unused mmu_idx */
    qemu_get_sbe32(f); /* Discard unused power_mode */

    /* Recompute mmu indices */
    hreg_compute_mem_idx(env);

    return 0;
}

static int get_avr(QEMUFile *f, void *pv, size_t size)
{
    ppc_avr_t *v = pv;

    v->u64[0] = qemu_get_be64(f);
    v->u64[1] = qemu_get_be64(f);

    return 0;
}

static void put_avr(QEMUFile *f, void *pv, size_t size)
{
    ppc_avr_t *v = pv;

    qemu_put_be64(f, v->u64[0]);
    qemu_put_be64(f, v->u64[1]);
}

static const VMStateInfo vmstate_info_avr = {
    .name = "avr",
    .get  = get_avr,
    .put  = put_avr,
};

#define VMSTATE_AVR_ARRAY_V(_f, _s, _n, _v)                       \
    VMSTATE_ARRAY(_f, _s, _n, _v, vmstate_info_avr, ppc_avr_t)

#define VMSTATE_AVR_ARRAY(_f, _s, _n)                             \
    VMSTATE_AVR_ARRAY_V(_f, _s, _n, 0)

static void cpu_pre_save(void *opaque)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;
    int i;

    env->spr[SPR_LR] = env->lr;
    env->spr[SPR_CTR] = env->ctr;
    env->spr[SPR_XER] = cpu_read_xer(env);
#if defined(TARGET_PPC64)
    env->spr[SPR_CFAR] = env->cfar;
#endif
    env->spr[SPR_BOOKE_SPEFSCR] = env->spe_fscr;

    for (i = 0; (i < 4) && (i < env->nb_BATs); i++) {
        env->spr[SPR_DBAT0U + 2*i] = env->DBAT[0][i];
        env->spr[SPR_DBAT0U + 2*i + 1] = env->DBAT[1][i];
        env->spr[SPR_IBAT0U + 2*i] = env->IBAT[0][i];
        env->spr[SPR_IBAT0U + 2*i + 1] = env->IBAT[1][i];
    }
    for (i = 0; (i < 4) && ((i+4) < env->nb_BATs); i++) {
        env->spr[SPR_DBAT4U + 2*i] = env->DBAT[0][i+4];
        env->spr[SPR_DBAT4U + 2*i + 1] = env->DBAT[1][i+4];
        env->spr[SPR_IBAT4U + 2*i] = env->IBAT[0][i+4];
        env->spr[SPR_IBAT4U + 2*i + 1] = env->IBAT[1][i+4];
    }
}

static int cpu_post_load(void *opaque, int version_id)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;
    int i;
    target_ulong msr;

    /*
     * We always ignore the source PVR. The user or management
     * software has to take care of running QEMU in a compatible mode.
     */
    env->spr[SPR_PVR] = env->spr_cb[SPR_PVR].default_value;
    env->lr = env->spr[SPR_LR];
    env->ctr = env->spr[SPR_CTR];
    cpu_write_xer(env, env->spr[SPR_XER]);
#if defined(TARGET_PPC64)
    env->cfar = env->spr[SPR_CFAR];
#endif
    env->spe_fscr = env->spr[SPR_BOOKE_SPEFSCR];

    for (i = 0; (i < 4) && (i < env->nb_BATs); i++) {
        env->DBAT[0][i] = env->spr[SPR_DBAT0U + 2*i];
        env->DBAT[1][i] = env->spr[SPR_DBAT0U + 2*i + 1];
        env->IBAT[0][i] = env->spr[SPR_IBAT0U + 2*i];
        env->IBAT[1][i] = env->spr[SPR_IBAT0U + 2*i + 1];
    }
    for (i = 0; (i < 4) && ((i+4) < env->nb_BATs); i++) {
        env->DBAT[0][i+4] = env->spr[SPR_DBAT4U + 2*i];
        env->DBAT[1][i+4] = env->spr[SPR_DBAT4U + 2*i + 1];
        env->IBAT[0][i+4] = env->spr[SPR_IBAT4U + 2*i];
        env->IBAT[1][i+4] = env->spr[SPR_IBAT4U + 2*i + 1];
    }

    if (!env->external_htab) {
        /* Restore htab_base and htab_mask variables */
        ppc_store_sdr1(env, env->spr[SPR_SDR1]);
    }

    /* Invalidate all msr bits except MSR_TGPR/MSR_HVB before restoring */
    msr = env->msr;
    env->msr ^= ~((1ULL << MSR_TGPR) | MSR_HVB);
    ppc_store_msr(env, msr);

    hreg_compute_mem_idx(env);

    return 0;
}

static bool fpu_needed(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    return (cpu->env.insns_flags & PPC_FLOAT);
}

static const VMStateDescription vmstate_fpu = {
    .name = "cpu/fpu",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = fpu_needed,
    .fields = (VMStateField[]) {
        VMSTATE_FLOAT64_ARRAY(env.fpr, PowerPCCPU, 32),
        VMSTATE_UINTTL(env.fpscr, PowerPCCPU),
        VMSTATE_END_OF_LIST()
    },
};

static bool altivec_needed(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    return (cpu->env.insns_flags & PPC_ALTIVEC);
}

static const VMStateDescription vmstate_altivec = {
    .name = "cpu/altivec",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = altivec_needed,
    .fields = (VMStateField[]) {
        VMSTATE_AVR_ARRAY(env.avr, PowerPCCPU, 32),
        VMSTATE_UINT32(env.vscr, PowerPCCPU),
        VMSTATE_END_OF_LIST()
    },
};

static bool vsx_needed(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    return (cpu->env.insns_flags2 & PPC2_VSX);
}

static const VMStateDescription vmstate_vsx = {
    .name = "cpu/vsx",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = vsx_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64_ARRAY(env.vsr, PowerPCCPU, 32),
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
    .minimum_version_id_old = 1,
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

    return !(cpu->env.mmu_model & POWERPC_MMU_64);
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
static int get_slbe(QEMUFile *f, void *pv, size_t size)
{
    ppc_slb_t *v = pv;

    v->esid = qemu_get_be64(f);
    v->vsid = qemu_get_be64(f);

    return 0;
}

static void put_slbe(QEMUFile *f, void *pv, size_t size)
{
    ppc_slb_t *v = pv;

    qemu_put_be64(f, v->esid);
    qemu_put_be64(f, v->vsid);
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
    return (cpu->env.mmu_model & POWERPC_MMU_64);
}

static int slb_post_load(void *opaque, int version_id)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;
    int i;

    /* We've pulled in the raw esid and vsid values from the migration
     * stream, but we need to recompute the page size pointers */
    for (i = 0; i < env->slb_nr; i++) {
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
        VMSTATE_INT32_EQUAL(env.slb_nr, PowerPCCPU),
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
        VMSTATE_INT32_EQUAL(env.nb_tlb, PowerPCCPU),
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

static bool pbr403_needed(void *opaque)
{
    PowerPCCPU *cpu = opaque;
    uint32_t pvr = cpu->env.spr[SPR_PVR];

    return (pvr & 0xffff0000) == 0x00200000;
}

static const VMStateDescription vmstate_pbr403 = {
    .name = "cpu/pbr403",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = pbr403_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINTTL_ARRAY(env.pb, PowerPCCPU, 4),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_tlbemb = {
    .name = "cpu/tlb6xx",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = tlbemb_needed,
    .fields = (VMStateField[]) {
        VMSTATE_INT32_EQUAL(env.nb_tlb, PowerPCCPU),
        VMSTATE_STRUCT_VARRAY_POINTER_INT32(env.tlb.tlbe, PowerPCCPU,
                                            env.nb_tlb,
                                            vmstate_tlbemb_entry,
                                            ppcemb_tlb_t),
        /* 403 protection registers */
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription*[]) {
        &vmstate_pbr403,
        NULL
    }
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
        VMSTATE_INT32_EQUAL(env.nb_tlb, PowerPCCPU),
        VMSTATE_STRUCT_VARRAY_POINTER_INT32(env.tlb.tlbm, PowerPCCPU,
                                            env.nb_tlb,
                                            vmstate_tlbmas_entry,
                                            ppcmas_tlb_t),
        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_ppc_cpu = {
    .name = "cpu",
    .version_id = 5,
    .minimum_version_id = 5,
    .minimum_version_id_old = 4,
    .load_state_old = cpu_load_old,
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

        /* Internal state */
        VMSTATE_UINTTL(env.hflags_nmsr, PowerPCCPU),
        /* FIXME: access_type? */

        /* Sanity checking */
        VMSTATE_UINTTL_EQUAL(env.msr_mask, PowerPCCPU),
        VMSTATE_UINT64_EQUAL(env.insns_flags, PowerPCCPU),
        VMSTATE_UINT64_EQUAL(env.insns_flags2, PowerPCCPU),
        VMSTATE_UINT32_EQUAL(env.nb_BATs, PowerPCCPU),
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
        NULL
    }
};
