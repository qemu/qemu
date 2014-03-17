#include "hw/hw.h"
#include "hw/boards.h"

#include "cpu.h"

static void save_tc(QEMUFile *f, TCState *tc)
{
    int i;

    /* Save active TC */
    for(i = 0; i < 32; i++)
        qemu_put_betls(f, &tc->gpr[i]);
    qemu_put_betls(f, &tc->PC);
    for(i = 0; i < MIPS_DSP_ACC; i++)
        qemu_put_betls(f, &tc->HI[i]);
    for(i = 0; i < MIPS_DSP_ACC; i++)
        qemu_put_betls(f, &tc->LO[i]);
    for(i = 0; i < MIPS_DSP_ACC; i++)
        qemu_put_betls(f, &tc->ACX[i]);
    qemu_put_betls(f, &tc->DSPControl);
    qemu_put_sbe32s(f, &tc->CP0_TCStatus);
    qemu_put_sbe32s(f, &tc->CP0_TCBind);
    qemu_put_betls(f, &tc->CP0_TCHalt);
    qemu_put_betls(f, &tc->CP0_TCContext);
    qemu_put_betls(f, &tc->CP0_TCSchedule);
    qemu_put_betls(f, &tc->CP0_TCScheFBack);
    qemu_put_sbe32s(f, &tc->CP0_Debug_tcstatus);
}

static void save_fpu(QEMUFile *f, CPUMIPSFPUContext *fpu)
{
    int i;

    for(i = 0; i < 32; i++)
        qemu_put_be64s(f, &fpu->fpr[i].d);
    qemu_put_s8s(f, &fpu->fp_status.float_detect_tininess);
    qemu_put_s8s(f, &fpu->fp_status.float_rounding_mode);
    qemu_put_s8s(f, &fpu->fp_status.float_exception_flags);
    qemu_put_be32s(f, &fpu->fcr0);
    qemu_put_be32s(f, &fpu->fcr31);
}

void cpu_save(QEMUFile *f, void *opaque)
{
    CPUMIPSState *env = opaque;
    int i;

    /* Save active TC */
    save_tc(f, &env->active_tc);

    /* Save active FPU */
    save_fpu(f, &env->active_fpu);

    /* Save MVP */
    qemu_put_sbe32s(f, &env->mvp->CP0_MVPControl);
    qemu_put_sbe32s(f, &env->mvp->CP0_MVPConf0);
    qemu_put_sbe32s(f, &env->mvp->CP0_MVPConf1);

    /* Save TLB */
    qemu_put_be32s(f, &env->tlb->nb_tlb);
    qemu_put_be32s(f, &env->tlb->tlb_in_use);
    for(i = 0; i < MIPS_TLB_MAX; i++) {
        uint16_t flags = ((env->tlb->mmu.r4k.tlb[i].G << 10) |
                          (env->tlb->mmu.r4k.tlb[i].C0 << 7) |
                          (env->tlb->mmu.r4k.tlb[i].C1 << 4) |
                          (env->tlb->mmu.r4k.tlb[i].V0 << 3) |
                          (env->tlb->mmu.r4k.tlb[i].V1 << 2) |
                          (env->tlb->mmu.r4k.tlb[i].D0 << 1) |
                          (env->tlb->mmu.r4k.tlb[i].D1 << 0));
        uint8_t asid;

        qemu_put_betls(f, &env->tlb->mmu.r4k.tlb[i].VPN);
        qemu_put_be32s(f, &env->tlb->mmu.r4k.tlb[i].PageMask);
        asid = env->tlb->mmu.r4k.tlb[i].ASID;
        qemu_put_8s(f, &asid);
        qemu_put_be16s(f, &flags);
        qemu_put_betls(f, &env->tlb->mmu.r4k.tlb[i].PFN[0]);
        qemu_put_betls(f, &env->tlb->mmu.r4k.tlb[i].PFN[1]);
    }

    /* Save CPU metastate */
    qemu_put_be32s(f, &env->current_tc);
    qemu_put_be32s(f, &env->current_fpu);
    qemu_put_sbe32s(f, &env->error_code);
    qemu_put_be32s(f, &env->hflags);
    qemu_put_betls(f, &env->btarget);
    i = env->bcond;
    qemu_put_sbe32s(f, &i);

    /* Save remaining CP1 registers */
    qemu_put_sbe32s(f, &env->CP0_Index);
    qemu_put_sbe32s(f, &env->CP0_Random);
    qemu_put_sbe32s(f, &env->CP0_VPEControl);
    qemu_put_sbe32s(f, &env->CP0_VPEConf0);
    qemu_put_sbe32s(f, &env->CP0_VPEConf1);
    qemu_put_betls(f, &env->CP0_YQMask);
    qemu_put_betls(f, &env->CP0_VPESchedule);
    qemu_put_betls(f, &env->CP0_VPEScheFBack);
    qemu_put_sbe32s(f, &env->CP0_VPEOpt);
    qemu_put_betls(f, &env->CP0_EntryLo0);
    qemu_put_betls(f, &env->CP0_EntryLo1);
    qemu_put_betls(f, &env->CP0_Context);
    qemu_put_sbe32s(f, &env->CP0_PageMask);
    qemu_put_sbe32s(f, &env->CP0_PageGrain);
    qemu_put_sbe32s(f, &env->CP0_Wired);
    qemu_put_sbe32s(f, &env->CP0_SRSConf0);
    qemu_put_sbe32s(f, &env->CP0_SRSConf1);
    qemu_put_sbe32s(f, &env->CP0_SRSConf2);
    qemu_put_sbe32s(f, &env->CP0_SRSConf3);
    qemu_put_sbe32s(f, &env->CP0_SRSConf4);
    qemu_put_sbe32s(f, &env->CP0_HWREna);
    qemu_put_betls(f, &env->CP0_BadVAddr);
    qemu_put_sbe32s(f, &env->CP0_Count);
    qemu_put_betls(f, &env->CP0_EntryHi);
    qemu_put_sbe32s(f, &env->CP0_Compare);
    qemu_put_sbe32s(f, &env->CP0_Status);
    qemu_put_sbe32s(f, &env->CP0_IntCtl);
    qemu_put_sbe32s(f, &env->CP0_SRSCtl);
    qemu_put_sbe32s(f, &env->CP0_SRSMap);
    qemu_put_sbe32s(f, &env->CP0_Cause);
    qemu_put_betls(f, &env->CP0_EPC);
    qemu_put_sbe32s(f, &env->CP0_PRid);
    qemu_put_sbe32s(f, &env->CP0_EBase);
    qemu_put_sbe32s(f, &env->CP0_Config0);
    qemu_put_sbe32s(f, &env->CP0_Config1);
    qemu_put_sbe32s(f, &env->CP0_Config2);
    qemu_put_sbe32s(f, &env->CP0_Config3);
    qemu_put_sbe32s(f, &env->CP0_Config6);
    qemu_put_sbe32s(f, &env->CP0_Config7);
    qemu_put_betls(f, &env->lladdr);
    for(i = 0; i < 8; i++)
        qemu_put_betls(f, &env->CP0_WatchLo[i]);
    for(i = 0; i < 8; i++)
        qemu_put_sbe32s(f, &env->CP0_WatchHi[i]);
    qemu_put_betls(f, &env->CP0_XContext);
    qemu_put_sbe32s(f, &env->CP0_Framemask);
    qemu_put_sbe32s(f, &env->CP0_Debug);
    qemu_put_betls(f, &env->CP0_DEPC);
    qemu_put_sbe32s(f, &env->CP0_Performance0);
    qemu_put_sbe32s(f, &env->CP0_TagLo);
    qemu_put_sbe32s(f, &env->CP0_DataLo);
    qemu_put_sbe32s(f, &env->CP0_TagHi);
    qemu_put_sbe32s(f, &env->CP0_DataHi);
    qemu_put_betls(f, &env->CP0_ErrorEPC);
    qemu_put_sbe32s(f, &env->CP0_DESAVE);

    /* Save inactive TC state */
    for (i = 0; i < MIPS_SHADOW_SET_MAX; i++)
        save_tc(f, &env->tcs[i]);
    for (i = 0; i < MIPS_FPU_MAX; i++)
        save_fpu(f, &env->fpus[i]);
}

static void load_tc(QEMUFile *f, TCState *tc)
{
    int i;

    /* Save active TC */
    for(i = 0; i < 32; i++)
        qemu_get_betls(f, &tc->gpr[i]);
    qemu_get_betls(f, &tc->PC);
    for(i = 0; i < MIPS_DSP_ACC; i++)
        qemu_get_betls(f, &tc->HI[i]);
    for(i = 0; i < MIPS_DSP_ACC; i++)
        qemu_get_betls(f, &tc->LO[i]);
    for(i = 0; i < MIPS_DSP_ACC; i++)
        qemu_get_betls(f, &tc->ACX[i]);
    qemu_get_betls(f, &tc->DSPControl);
    qemu_get_sbe32s(f, &tc->CP0_TCStatus);
    qemu_get_sbe32s(f, &tc->CP0_TCBind);
    qemu_get_betls(f, &tc->CP0_TCHalt);
    qemu_get_betls(f, &tc->CP0_TCContext);
    qemu_get_betls(f, &tc->CP0_TCSchedule);
    qemu_get_betls(f, &tc->CP0_TCScheFBack);
    qemu_get_sbe32s(f, &tc->CP0_Debug_tcstatus);
}

static void load_fpu(QEMUFile *f, CPUMIPSFPUContext *fpu)
{
    int i;

    for(i = 0; i < 32; i++)
        qemu_get_be64s(f, &fpu->fpr[i].d);
    qemu_get_s8s(f, &fpu->fp_status.float_detect_tininess);
    qemu_get_s8s(f, &fpu->fp_status.float_rounding_mode);
    qemu_get_s8s(f, &fpu->fp_status.float_exception_flags);
    qemu_get_be32s(f, &fpu->fcr0);
    qemu_get_be32s(f, &fpu->fcr31);
}

int cpu_load(QEMUFile *f, void *opaque, int version_id)
{
    CPUMIPSState *env = opaque;
    MIPSCPU *cpu = mips_env_get_cpu(env);
    int i;

    if (version_id != 3)
        return -EINVAL;

    /* Load active TC */
    load_tc(f, &env->active_tc);

    /* Load active FPU */
    load_fpu(f, &env->active_fpu);

    /* Load MVP */
    qemu_get_sbe32s(f, &env->mvp->CP0_MVPControl);
    qemu_get_sbe32s(f, &env->mvp->CP0_MVPConf0);
    qemu_get_sbe32s(f, &env->mvp->CP0_MVPConf1);

    /* Load TLB */
    qemu_get_be32s(f, &env->tlb->nb_tlb);
    qemu_get_be32s(f, &env->tlb->tlb_in_use);
    for(i = 0; i < MIPS_TLB_MAX; i++) {
        uint16_t flags;
        uint8_t asid;

        qemu_get_betls(f, &env->tlb->mmu.r4k.tlb[i].VPN);
        qemu_get_be32s(f, &env->tlb->mmu.r4k.tlb[i].PageMask);
        qemu_get_8s(f, &asid);
        env->tlb->mmu.r4k.tlb[i].ASID = asid;
        qemu_get_be16s(f, &flags);
        env->tlb->mmu.r4k.tlb[i].G = (flags >> 10) & 1;
        env->tlb->mmu.r4k.tlb[i].C0 = (flags >> 7) & 3;
        env->tlb->mmu.r4k.tlb[i].C1 = (flags >> 4) & 3;
        env->tlb->mmu.r4k.tlb[i].V0 = (flags >> 3) & 1;
        env->tlb->mmu.r4k.tlb[i].V1 = (flags >> 2) & 1;
        env->tlb->mmu.r4k.tlb[i].D0 = (flags >> 1) & 1;
        env->tlb->mmu.r4k.tlb[i].D1 = (flags >> 0) & 1;
        qemu_get_betls(f, &env->tlb->mmu.r4k.tlb[i].PFN[0]);
        qemu_get_betls(f, &env->tlb->mmu.r4k.tlb[i].PFN[1]);
    }

    /* Load CPU metastate */
    qemu_get_be32s(f, &env->current_tc);
    qemu_get_be32s(f, &env->current_fpu);
    qemu_get_sbe32s(f, &env->error_code);
    qemu_get_be32s(f, &env->hflags);
    qemu_get_betls(f, &env->btarget);
    qemu_get_sbe32s(f, &i);
    env->bcond = i;

    /* Load remaining CP1 registers */
    qemu_get_sbe32s(f, &env->CP0_Index);
    qemu_get_sbe32s(f, &env->CP0_Random);
    qemu_get_sbe32s(f, &env->CP0_VPEControl);
    qemu_get_sbe32s(f, &env->CP0_VPEConf0);
    qemu_get_sbe32s(f, &env->CP0_VPEConf1);
    qemu_get_betls(f, &env->CP0_YQMask);
    qemu_get_betls(f, &env->CP0_VPESchedule);
    qemu_get_betls(f, &env->CP0_VPEScheFBack);
    qemu_get_sbe32s(f, &env->CP0_VPEOpt);
    qemu_get_betls(f, &env->CP0_EntryLo0);
    qemu_get_betls(f, &env->CP0_EntryLo1);
    qemu_get_betls(f, &env->CP0_Context);
    qemu_get_sbe32s(f, &env->CP0_PageMask);
    qemu_get_sbe32s(f, &env->CP0_PageGrain);
    qemu_get_sbe32s(f, &env->CP0_Wired);
    qemu_get_sbe32s(f, &env->CP0_SRSConf0);
    qemu_get_sbe32s(f, &env->CP0_SRSConf1);
    qemu_get_sbe32s(f, &env->CP0_SRSConf2);
    qemu_get_sbe32s(f, &env->CP0_SRSConf3);
    qemu_get_sbe32s(f, &env->CP0_SRSConf4);
    qemu_get_sbe32s(f, &env->CP0_HWREna);
    qemu_get_betls(f, &env->CP0_BadVAddr);
    qemu_get_sbe32s(f, &env->CP0_Count);
    qemu_get_betls(f, &env->CP0_EntryHi);
    qemu_get_sbe32s(f, &env->CP0_Compare);
    qemu_get_sbe32s(f, &env->CP0_Status);
    qemu_get_sbe32s(f, &env->CP0_IntCtl);
    qemu_get_sbe32s(f, &env->CP0_SRSCtl);
    qemu_get_sbe32s(f, &env->CP0_SRSMap);
    qemu_get_sbe32s(f, &env->CP0_Cause);
    qemu_get_betls(f, &env->CP0_EPC);
    qemu_get_sbe32s(f, &env->CP0_PRid);
    qemu_get_sbe32s(f, &env->CP0_EBase);
    qemu_get_sbe32s(f, &env->CP0_Config0);
    qemu_get_sbe32s(f, &env->CP0_Config1);
    qemu_get_sbe32s(f, &env->CP0_Config2);
    qemu_get_sbe32s(f, &env->CP0_Config3);
    qemu_get_sbe32s(f, &env->CP0_Config6);
    qemu_get_sbe32s(f, &env->CP0_Config7);
    qemu_get_betls(f, &env->lladdr);
    for(i = 0; i < 8; i++)
        qemu_get_betls(f, &env->CP0_WatchLo[i]);
    for(i = 0; i < 8; i++)
        qemu_get_sbe32s(f, &env->CP0_WatchHi[i]);
    qemu_get_betls(f, &env->CP0_XContext);
    qemu_get_sbe32s(f, &env->CP0_Framemask);
    qemu_get_sbe32s(f, &env->CP0_Debug);
    qemu_get_betls(f, &env->CP0_DEPC);
    qemu_get_sbe32s(f, &env->CP0_Performance0);
    qemu_get_sbe32s(f, &env->CP0_TagLo);
    qemu_get_sbe32s(f, &env->CP0_DataLo);
    qemu_get_sbe32s(f, &env->CP0_TagHi);
    qemu_get_sbe32s(f, &env->CP0_DataHi);
    qemu_get_betls(f, &env->CP0_ErrorEPC);
    qemu_get_sbe32s(f, &env->CP0_DESAVE);

    /* Load inactive TC state */
    for (i = 0; i < MIPS_SHADOW_SET_MAX; i++)
        load_tc(f, &env->tcs[i]);
    for (i = 0; i < MIPS_FPU_MAX; i++)
        load_fpu(f, &env->fpus[i]);

    /* XXX: ensure compatibility for halted bit ? */
    tlb_flush(CPU(cpu), 1);
    return 0;
}
