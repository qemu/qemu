#include "hw/hw.h"
#include "hw/boards.h"
#include "sysemu/kvm.h"

void cpu_save(QEMUFile *f, void *opaque)
{
    CPUPPCState *env = (CPUPPCState *)opaque;
    unsigned int i, j;
    uint32_t fpscr;
    target_ulong xer;

    for (i = 0; i < 32; i++)
        qemu_put_betls(f, &env->gpr[i]);
#if !defined(TARGET_PPC64)
    for (i = 0; i < 32; i++)
        qemu_put_betls(f, &env->gprh[i]);
#endif
    qemu_put_betls(f, &env->lr);
    qemu_put_betls(f, &env->ctr);
    for (i = 0; i < 8; i++)
        qemu_put_be32s(f, &env->crf[i]);
    xer = cpu_read_xer(env);
    qemu_put_betls(f, &xer);
    qemu_put_betls(f, &env->reserve_addr);
    qemu_put_betls(f, &env->msr);
    for (i = 0; i < 4; i++)
        qemu_put_betls(f, &env->tgpr[i]);
    for (i = 0; i < 32; i++) {
        union {
            float64 d;
            uint64_t l;
        } u;
        u.d = env->fpr[i];
        qemu_put_be64(f, u.l);
    }
    fpscr = env->fpscr;
    qemu_put_be32s(f, &fpscr);
    qemu_put_sbe32s(f, &env->access_type);
#if defined(TARGET_PPC64)
    qemu_put_betls(f, &env->spr[SPR_ASR]);
    qemu_put_sbe32s(f, &env->slb_nr);
#endif
    qemu_put_betls(f, &env->spr[SPR_SDR1]);
    for (i = 0; i < 32; i++)
        qemu_put_betls(f, &env->sr[i]);
    for (i = 0; i < 2; i++)
        for (j = 0; j < 8; j++)
            qemu_put_betls(f, &env->DBAT[i][j]);
    for (i = 0; i < 2; i++)
        for (j = 0; j < 8; j++)
            qemu_put_betls(f, &env->IBAT[i][j]);
    qemu_put_sbe32s(f, &env->nb_tlb);
    qemu_put_sbe32s(f, &env->tlb_per_way);
    qemu_put_sbe32s(f, &env->nb_ways);
    qemu_put_sbe32s(f, &env->last_way);
    qemu_put_sbe32s(f, &env->id_tlbs);
    qemu_put_sbe32s(f, &env->nb_pids);
    if (env->tlb.tlb6) {
        // XXX assumes 6xx
        for (i = 0; i < env->nb_tlb; i++) {
            qemu_put_betls(f, &env->tlb.tlb6[i].pte0);
            qemu_put_betls(f, &env->tlb.tlb6[i].pte1);
            qemu_put_betls(f, &env->tlb.tlb6[i].EPN);
        }
    }
    for (i = 0; i < 4; i++)
        qemu_put_betls(f, &env->pb[i]);
    for (i = 0; i < 1024; i++)
        qemu_put_betls(f, &env->spr[i]);
    qemu_put_be32s(f, &env->vscr);
    qemu_put_be64s(f, &env->spe_acc);
    qemu_put_be32s(f, &env->spe_fscr);
    qemu_put_betls(f, &env->msr_mask);
    qemu_put_be32s(f, &env->flags);
    qemu_put_sbe32s(f, &env->error_code);
    qemu_put_be32s(f, &env->pending_interrupts);
    qemu_put_be32s(f, &env->irq_input_state);
    for (i = 0; i < POWERPC_EXCP_NB; i++)
        qemu_put_betls(f, &env->excp_vectors[i]);
    qemu_put_betls(f, &env->excp_prefix);
    qemu_put_betls(f, &env->ivor_mask);
    qemu_put_betls(f, &env->ivpr_mask);
    qemu_put_betls(f, &env->hreset_vector);
    qemu_put_betls(f, &env->nip);
    qemu_put_betls(f, &env->hflags);
    qemu_put_betls(f, &env->hflags_nmsr);
    qemu_put_sbe32s(f, &env->mmu_idx);
    qemu_put_sbe32(f, 0);
}

int cpu_load(QEMUFile *f, void *opaque, int version_id)
{
    CPUPPCState *env = (CPUPPCState *)opaque;
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
    ppc_store_sdr1(env, sdr1);
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
    qemu_get_sbe32s(f, &env->mmu_idx);
    qemu_get_sbe32(f); /* Discard unused power_mode */

    return 0;
}
