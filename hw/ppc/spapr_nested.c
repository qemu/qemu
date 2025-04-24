#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "exec/cputlb.h"
#include "exec/target_long.h"
#include "helper_regs.h"
#include "hw/ppc/ppc.h"
#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_cpu_core.h"
#include "hw/ppc/spapr_nested.h"
#include "mmu-book3s-v3.h"
#include "cpu-models.h"
#include "qemu/log.h"

void spapr_nested_reset(SpaprMachineState *spapr)
{
    if (spapr_get_cap(spapr, SPAPR_CAP_NESTED_KVM_HV)) {
        spapr_unregister_nested_hv();
        spapr_register_nested_hv();
    } else if (spapr_get_cap(spapr, SPAPR_CAP_NESTED_PAPR)) {
        spapr->nested.capabilities_set = false;
        spapr_unregister_nested_papr();
        spapr_register_nested_papr();
        spapr_nested_gsb_init();
    } else {
        spapr->nested.api = 0;
    }
}

uint8_t spapr_nested_api(SpaprMachineState *spapr)
{
    return spapr->nested.api;
}

#ifdef CONFIG_TCG

bool spapr_get_pate_nested_hv(SpaprMachineState *spapr, PowerPCCPU *cpu,
                              target_ulong lpid, ppc_v3_pate_t *entry)
{
    uint64_t patb, pats;

    assert(lpid != 0);

    patb = spapr->nested.ptcr & PTCR_PATB;
    pats = spapr->nested.ptcr & PTCR_PATS;

    /* Check if partition table is properly aligned */
    if (patb & MAKE_64BIT_MASK(0, pats + 12)) {
        return false;
    }

    /* Calculate number of entries */
    pats = 1ull << (pats + 12 - 4);
    if (pats <= lpid) {
        return false;
    }

    /* Grab entry */
    patb += 16 * lpid;
    entry->dw0 = ldq_phys(CPU(cpu)->as, patb);
    entry->dw1 = ldq_phys(CPU(cpu)->as, patb + 8);
    return true;
}

static
SpaprMachineStateNestedGuest *spapr_get_nested_guest(SpaprMachineState *spapr,
                                                     target_ulong guestid)
{
    return spapr->nested.guests ?
        g_hash_table_lookup(spapr->nested.guests,
                            GINT_TO_POINTER(guestid)) : NULL;
}

bool spapr_get_pate_nested_papr(SpaprMachineState *spapr, PowerPCCPU *cpu,
                                target_ulong lpid, ppc_v3_pate_t *entry)
{
    SpaprMachineStateNestedGuest *guest;
    assert(lpid != 0);
    guest = spapr_get_nested_guest(spapr, lpid);
    if (!guest) {
        return false;
    }

    entry->dw0 = guest->parttbl[0];
    entry->dw1 = guest->parttbl[1];
    return true;
}

#define PRTS_MASK      0x1f

static target_ulong h_set_ptbl(PowerPCCPU *cpu,
                               SpaprMachineState *spapr,
                               target_ulong opcode,
                               target_ulong *args)
{
    target_ulong ptcr = args[0];

    if (!spapr_get_cap(spapr, SPAPR_CAP_NESTED_KVM_HV)) {
        return H_FUNCTION;
    }

    if ((ptcr & PRTS_MASK) + 12 - 4 > 12) {
        return H_PARAMETER;
    }

    spapr->nested.ptcr = ptcr; /* Save new partition table */

    return H_SUCCESS;
}

static target_ulong h_tlb_invalidate(PowerPCCPU *cpu,
                                     SpaprMachineState *spapr,
                                     target_ulong opcode,
                                     target_ulong *args)
{
    /*
     * The spapr virtual hypervisor nested HV implementation retains no L2
     * translation state except for TLB. And the TLB is always invalidated
     * across L1<->L2 transitions, so nothing is required here.
     */

    return H_SUCCESS;
}

static target_ulong h_copy_tofrom_guest(PowerPCCPU *cpu,
                                        SpaprMachineState *spapr,
                                        target_ulong opcode,
                                        target_ulong *args)
{
    /*
     * This HCALL is not required, L1 KVM will take a slow path and walk the
     * page tables manually to do the data copy.
     */
    return H_FUNCTION;
}

static void nested_save_state(struct nested_ppc_state *save, PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;
    SpaprMachineState *spapr = SPAPR_MACHINE(qdev_get_machine());

    memcpy(save->gpr, env->gpr, sizeof(save->gpr));

    save->lr = env->lr;
    save->ctr = env->ctr;
    save->cfar = env->cfar;
    save->msr = env->msr;
    save->nip = env->nip;

    save->cr = ppc_get_cr(env);
    save->xer = cpu_read_xer(env);

    save->lpcr = env->spr[SPR_LPCR];
    save->lpidr = env->spr[SPR_LPIDR];
    save->pcr = env->spr[SPR_PCR];
    save->dpdes = env->spr[SPR_DPDES];
    save->hfscr = env->spr[SPR_HFSCR];
    save->srr0 = env->spr[SPR_SRR0];
    save->srr1 = env->spr[SPR_SRR1];
    save->sprg0 = env->spr[SPR_SPRG0];
    save->sprg1 = env->spr[SPR_SPRG1];
    save->sprg2 = env->spr[SPR_SPRG2];
    save->sprg3 = env->spr[SPR_SPRG3];
    save->pidr = env->spr[SPR_BOOKS_PID];
    save->ppr = env->spr[SPR_PPR];

    if (spapr_nested_api(spapr) == NESTED_API_PAPR) {
        save->amor = env->spr[SPR_AMOR];
        save->dawr0 = env->spr[SPR_DAWR0];
        save->dawrx0 = env->spr[SPR_DAWRX0];
        save->ciabr = env->spr[SPR_CIABR];
        save->purr = env->spr[SPR_PURR];
        save->spurr = env->spr[SPR_SPURR];
        save->ic = env->spr[SPR_IC];
        save->vtb = env->spr[SPR_VTB];
        save->hdar = env->spr[SPR_HDAR];
        save->hdsisr = env->spr[SPR_HDSISR];
        save->heir = env->spr[SPR_HEIR];
        save->asdr = env->spr[SPR_ASDR];
        save->dawr1 = env->spr[SPR_DAWR1];
        save->dawrx1 = env->spr[SPR_DAWRX1];
        save->dexcr = env->spr[SPR_DEXCR];
        save->hdexcr = env->spr[SPR_HDEXCR];
        save->hashkeyr = env->spr[SPR_HASHKEYR];
        save->hashpkeyr = env->spr[SPR_HASHPKEYR];
        memcpy(save->vsr, env->vsr, sizeof(save->vsr));
        save->ebbhr = env->spr[SPR_EBBHR];
        save->tar = env->spr[SPR_TAR];
        save->ebbrr = env->spr[SPR_EBBRR];
        save->bescr = env->spr[SPR_BESCR];
        save->iamr = env->spr[SPR_IAMR];
        save->amr = env->spr[SPR_AMR];
        save->uamor = env->spr[SPR_UAMOR];
        save->dscr = env->spr[SPR_DSCR];
        save->fscr = env->spr[SPR_FSCR];
        save->pspb = env->spr[SPR_PSPB];
        save->ctrl = env->spr[SPR_CTRL];
        save->vrsave = env->spr[SPR_VRSAVE];
        save->dar = env->spr[SPR_DAR];
        save->dsisr = env->spr[SPR_DSISR];
        save->pmc1 = env->spr[SPR_POWER_PMC1];
        save->pmc2 = env->spr[SPR_POWER_PMC2];
        save->pmc3 = env->spr[SPR_POWER_PMC3];
        save->pmc4 = env->spr[SPR_POWER_PMC4];
        save->pmc5 = env->spr[SPR_POWER_PMC5];
        save->pmc6 = env->spr[SPR_POWER_PMC6];
        save->mmcr0 = env->spr[SPR_POWER_MMCR0];
        save->mmcr1 = env->spr[SPR_POWER_MMCR1];
        save->mmcr2 = env->spr[SPR_POWER_MMCR2];
        save->mmcra = env->spr[SPR_POWER_MMCRA];
        save->sdar = env->spr[SPR_POWER_SDAR];
        save->siar = env->spr[SPR_POWER_SIAR];
        save->sier = env->spr[SPR_POWER_SIER];
        save->vscr = ppc_get_vscr(env);
        save->fpscr = env->fpscr;
    } else if (spapr_nested_api(spapr) == NESTED_API_KVM_HV) {
        save->tb_offset = env->tb_env->tb_offset;
    }
}

static void nested_post_load_state(CPUPPCState *env, CPUState *cs)
{
    /*
     * compute hflags and possible interrupts.
     */
    hreg_compute_hflags(env);
    ppc_maybe_interrupt(env);
    /*
     * Nested HV does not tag TLB entries between L1 and L2, so must
     * flush on transition.
     */
    tlb_flush(cs);
    env->reserve_addr = -1; /* Reset the reservation */
}

static void nested_load_state(PowerPCCPU *cpu, struct nested_ppc_state *load)
{
    CPUPPCState *env = &cpu->env;
    SpaprMachineState *spapr = SPAPR_MACHINE(qdev_get_machine());

    memcpy(env->gpr, load->gpr, sizeof(env->gpr));

    env->lr = load->lr;
    env->ctr = load->ctr;
    env->cfar = load->cfar;
    env->msr = load->msr;
    env->nip = load->nip;

    ppc_set_cr(env, load->cr);
    cpu_write_xer(env, load->xer);

    env->spr[SPR_LPCR] = load->lpcr;
    env->spr[SPR_LPIDR] = load->lpidr;
    env->spr[SPR_PCR] = load->pcr;
    env->spr[SPR_DPDES] = load->dpdes;
    env->spr[SPR_HFSCR] = load->hfscr;
    env->spr[SPR_SRR0] = load->srr0;
    env->spr[SPR_SRR1] = load->srr1;
    env->spr[SPR_SPRG0] = load->sprg0;
    env->spr[SPR_SPRG1] = load->sprg1;
    env->spr[SPR_SPRG2] = load->sprg2;
    env->spr[SPR_SPRG3] = load->sprg3;
    env->spr[SPR_BOOKS_PID] = load->pidr;
    env->spr[SPR_PPR] = load->ppr;

    if (spapr_nested_api(spapr) == NESTED_API_PAPR) {
        env->spr[SPR_AMOR] = load->amor;
        env->spr[SPR_DAWR0] = load->dawr0;
        env->spr[SPR_DAWRX0] = load->dawrx0;
        env->spr[SPR_CIABR] = load->ciabr;
        env->spr[SPR_PURR] = load->purr;
        env->spr[SPR_SPURR] = load->purr;
        env->spr[SPR_IC] = load->ic;
        env->spr[SPR_VTB] = load->vtb;
        env->spr[SPR_HDAR] = load->hdar;
        env->spr[SPR_HDSISR] = load->hdsisr;
        env->spr[SPR_HEIR] = load->heir;
        env->spr[SPR_ASDR] = load->asdr;
        env->spr[SPR_DAWR1] = load->dawr1;
        env->spr[SPR_DAWRX1] = load->dawrx1;
        env->spr[SPR_DEXCR] = load->dexcr;
        env->spr[SPR_HDEXCR] = load->hdexcr;
        env->spr[SPR_HASHKEYR] = load->hashkeyr;
        env->spr[SPR_HASHPKEYR] = load->hashpkeyr;
        memcpy(env->vsr, load->vsr, sizeof(env->vsr));
        env->spr[SPR_EBBHR] = load->ebbhr;
        env->spr[SPR_TAR] = load->tar;
        env->spr[SPR_EBBRR] = load->ebbrr;
        env->spr[SPR_BESCR] = load->bescr;
        env->spr[SPR_IAMR] = load->iamr;
        env->spr[SPR_AMR] = load->amr;
        env->spr[SPR_UAMOR] = load->uamor;
        env->spr[SPR_DSCR] = load->dscr;
        env->spr[SPR_FSCR] = load->fscr;
        env->spr[SPR_PSPB] = load->pspb;
        env->spr[SPR_CTRL] = load->ctrl;
        env->spr[SPR_VRSAVE] = load->vrsave;
        env->spr[SPR_DAR] = load->dar;
        env->spr[SPR_DSISR] = load->dsisr;
        env->spr[SPR_POWER_PMC1] = load->pmc1;
        env->spr[SPR_POWER_PMC2] = load->pmc2;
        env->spr[SPR_POWER_PMC3] = load->pmc3;
        env->spr[SPR_POWER_PMC4] = load->pmc4;
        env->spr[SPR_POWER_PMC5] = load->pmc5;
        env->spr[SPR_POWER_PMC6] = load->pmc6;
        env->spr[SPR_POWER_MMCR0] = load->mmcr0;
        env->spr[SPR_POWER_MMCR1] = load->mmcr1;
        env->spr[SPR_POWER_MMCR2] = load->mmcr2;
        env->spr[SPR_POWER_MMCRA] = load->mmcra;
        env->spr[SPR_POWER_SDAR] = load->sdar;
        env->spr[SPR_POWER_SIAR] = load->siar;
        env->spr[SPR_POWER_SIER] = load->sier;
        ppc_store_vscr(env, load->vscr);
        ppc_store_fpscr(env, load->fpscr);
    } else if (spapr_nested_api(spapr) == NESTED_API_KVM_HV) {
        env->tb_env->tb_offset = load->tb_offset;
    }
}

/*
 * When this handler returns, the environment is switched to the L2 guest
 * and TCG begins running that. spapr_exit_nested() performs the switch from
 * L2 back to L1 and returns from the H_ENTER_NESTED hcall.
 */
static target_ulong h_enter_nested(PowerPCCPU *cpu,
                                   SpaprMachineState *spapr,
                                   target_ulong opcode,
                                   target_ulong *args)
{
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);
    CPUPPCState *env = &cpu->env;
    CPUState *cs = CPU(cpu);
    SpaprCpuState *spapr_cpu = spapr_cpu_state(cpu);
    struct nested_ppc_state l2_state;
    target_ulong hv_ptr = args[0];
    target_ulong regs_ptr = args[1];
    target_ulong hdec, now = cpu_ppc_load_tbl(env);
    target_ulong lpcr, lpcr_mask;
    struct kvmppc_hv_guest_state *hvstate;
    struct kvmppc_hv_guest_state hv_state;
    struct kvmppc_pt_regs *regs;
    hwaddr len;

    if (spapr->nested.ptcr == 0) {
        return H_NOT_AVAILABLE;
    }

    len = sizeof(*hvstate);
    hvstate = address_space_map(CPU(cpu)->as, hv_ptr, &len, false,
                                MEMTXATTRS_UNSPECIFIED);
    if (len != sizeof(*hvstate)) {
        address_space_unmap(CPU(cpu)->as, hvstate, len, 0, false);
        return H_PARAMETER;
    }

    memcpy(&hv_state, hvstate, len);

    address_space_unmap(CPU(cpu)->as, hvstate, len, len, false);

    /*
     * We accept versions 1 and 2. Version 2 fields are unused because TCG
     * does not implement DAWR*.
     */
    if (hv_state.version > HV_GUEST_STATE_VERSION) {
        return H_PARAMETER;
    }

    if (hv_state.lpid == 0) {
        return H_PARAMETER;
    }

    spapr_cpu->nested_host_state = g_try_new(struct nested_ppc_state, 1);
    if (!spapr_cpu->nested_host_state) {
        return H_NO_MEM;
    }

    assert(env->spr[SPR_LPIDR] == 0);
    assert(env->spr[SPR_DPDES] == 0);
    nested_save_state(spapr_cpu->nested_host_state, cpu);

    len = sizeof(*regs);
    regs = address_space_map(CPU(cpu)->as, regs_ptr, &len, false,
                                MEMTXATTRS_UNSPECIFIED);
    if (!regs || len != sizeof(*regs)) {
        address_space_unmap(CPU(cpu)->as, regs, len, 0, false);
        g_free(spapr_cpu->nested_host_state);
        return H_P2;
    }

    len = sizeof(l2_state.gpr);
    assert(len == sizeof(regs->gpr));
    memcpy(l2_state.gpr, regs->gpr, len);

    l2_state.lr = regs->link;
    l2_state.ctr = regs->ctr;
    l2_state.xer = regs->xer;
    l2_state.cr = regs->ccr;
    l2_state.msr = regs->msr;
    l2_state.nip = regs->nip;

    address_space_unmap(CPU(cpu)->as, regs, len, len, false);

    l2_state.cfar = hv_state.cfar;
    l2_state.lpidr = hv_state.lpid;

    lpcr_mask = LPCR_DPFD | LPCR_ILE | LPCR_AIL | LPCR_LD | LPCR_MER;
    lpcr = (env->spr[SPR_LPCR] & ~lpcr_mask) | (hv_state.lpcr & lpcr_mask);
    lpcr |= LPCR_HR | LPCR_UPRT | LPCR_GTSE | LPCR_HVICE | LPCR_HDICE;
    lpcr &= ~LPCR_LPES0;
    l2_state.lpcr = lpcr & pcc->lpcr_mask;

    l2_state.pcr = hv_state.pcr;
    /* hv_state.amor is not used */
    l2_state.dpdes = hv_state.dpdes;
    l2_state.hfscr = hv_state.hfscr;
    /* TCG does not implement DAWR*, CIABR, PURR, SPURR, IC, VTB, HEIR SPRs*/
    l2_state.srr0 = hv_state.srr0;
    l2_state.srr1 = hv_state.srr1;
    l2_state.sprg0 = hv_state.sprg[0];
    l2_state.sprg1 = hv_state.sprg[1];
    l2_state.sprg2 = hv_state.sprg[2];
    l2_state.sprg3 = hv_state.sprg[3];
    l2_state.pidr = hv_state.pidr;
    l2_state.ppr = hv_state.ppr;
    l2_state.tb_offset = env->tb_env->tb_offset + hv_state.tb_offset;

    /*
     * Switch to the nested guest environment and start the "hdec" timer.
     */
    nested_load_state(cpu, &l2_state);
    nested_post_load_state(env, cs);

    hdec = hv_state.hdec_expiry - now;
    cpu_ppc_hdecr_init(env);
    cpu_ppc_store_hdecr(env, hdec);

    /*
     * The hv_state.vcpu_token is not needed. It is used by the KVM
     * implementation to remember which L2 vCPU last ran on which physical
     * CPU so as to invalidate process scope translations if it is moved
     * between physical CPUs. For now TLBs are always flushed on L1<->L2
     * transitions so this is not a problem.
     *
     * Could validate that the same vcpu_token does not attempt to run on
     * different L1 vCPUs at the same time, but that would be a L1 KVM bug
     * and it's not obviously worth a new data structure to do it.
     */

    spapr_cpu->in_nested = true;

    /*
     * The spapr hcall helper sets env->gpr[3] to the return value, but at
     * this point the L1 is not returning from the hcall but rather we
     * start running the L2, so r3 must not be clobbered, so return env->gpr[3]
     * to leave it unchanged.
     */
    return env->gpr[3];
}

static void spapr_exit_nested_hv(PowerPCCPU *cpu, int excp)
{
    CPUPPCState *env = &cpu->env;
    CPUState *cs = CPU(cpu);
    SpaprCpuState *spapr_cpu = spapr_cpu_state(cpu);
    struct nested_ppc_state l2_state;
    target_ulong hv_ptr = spapr_cpu->nested_host_state->gpr[4];
    target_ulong regs_ptr = spapr_cpu->nested_host_state->gpr[5];
    target_ulong hsrr0, hsrr1, hdar, asdr, hdsisr;
    struct kvmppc_hv_guest_state *hvstate;
    struct kvmppc_pt_regs *regs;
    hwaddr len;

    nested_save_state(&l2_state, cpu);
    hsrr0 = env->spr[SPR_HSRR0];
    hsrr1 = env->spr[SPR_HSRR1];
    hdar = env->spr[SPR_HDAR];
    hdsisr = env->spr[SPR_HDSISR];
    asdr = env->spr[SPR_ASDR];

    /*
     * Switch back to the host environment (including for any error).
     */
    assert(env->spr[SPR_LPIDR] != 0);
    nested_load_state(cpu, spapr_cpu->nested_host_state);
    nested_post_load_state(env, cs);
    env->gpr[3] = env->excp_vectors[excp]; /* hcall return value */

    cpu_ppc_hdecr_exit(env);

    spapr_cpu->in_nested = false;

    g_free(spapr_cpu->nested_host_state);
    spapr_cpu->nested_host_state = NULL;

    len = sizeof(*hvstate);
    hvstate = address_space_map(CPU(cpu)->as, hv_ptr, &len, true,
                                MEMTXATTRS_UNSPECIFIED);
    if (len != sizeof(*hvstate)) {
        address_space_unmap(CPU(cpu)->as, hvstate, len, 0, true);
        env->gpr[3] = H_PARAMETER;
        return;
    }

    hvstate->cfar = l2_state.cfar;
    hvstate->lpcr = l2_state.lpcr;
    hvstate->pcr = l2_state.pcr;
    hvstate->dpdes = l2_state.dpdes;
    hvstate->hfscr = l2_state.hfscr;

    if (excp == POWERPC_EXCP_HDSI) {
        hvstate->hdar = hdar;
        hvstate->hdsisr = hdsisr;
        hvstate->asdr = asdr;
    } else if (excp == POWERPC_EXCP_HISI) {
        hvstate->asdr = asdr;
    }

    /* HEIR should be implemented for HV mode and saved here. */
    hvstate->srr0 = l2_state.srr0;
    hvstate->srr1 = l2_state.srr1;
    hvstate->sprg[0] = l2_state.sprg0;
    hvstate->sprg[1] = l2_state.sprg1;
    hvstate->sprg[2] = l2_state.sprg2;
    hvstate->sprg[3] = l2_state.sprg3;
    hvstate->pidr = l2_state.pidr;
    hvstate->ppr = l2_state.ppr;

    /* Is it okay to specify write length larger than actual data written? */
    address_space_unmap(CPU(cpu)->as, hvstate, len, len, true);

    len = sizeof(*regs);
    regs = address_space_map(CPU(cpu)->as, regs_ptr, &len, true,
                                MEMTXATTRS_UNSPECIFIED);
    if (!regs || len != sizeof(*regs)) {
        address_space_unmap(CPU(cpu)->as, regs, len, 0, true);
        env->gpr[3] = H_P2;
        return;
    }

    len = sizeof(env->gpr);
    assert(len == sizeof(regs->gpr));
    memcpy(regs->gpr, l2_state.gpr, len);

    regs->link = l2_state.lr;
    regs->ctr = l2_state.ctr;
    regs->xer = l2_state.xer;
    regs->ccr = l2_state.cr;

    if (excp == POWERPC_EXCP_MCHECK ||
        excp == POWERPC_EXCP_RESET ||
        excp == POWERPC_EXCP_SYSCALL) {
        regs->nip = l2_state.srr0;
        regs->msr = l2_state.srr1 & env->msr_mask;
    } else {
        regs->nip = hsrr0;
        regs->msr = hsrr1 & env->msr_mask;
    }

    /* Is it okay to specify write length larger than actual data written? */
    address_space_unmap(CPU(cpu)->as, regs, len, len, true);
}

static bool spapr_nested_vcpu_check(SpaprMachineStateNestedGuest *guest,
                                    target_ulong vcpuid, bool inoutbuf)
{
    struct SpaprMachineStateNestedGuestVcpu *vcpu;
    /*
     * Perform sanity checks for the provided vcpuid of a guest.
     * For now, ensure its valid, allocated and enabled for use.
     */

    if (vcpuid >= PAPR_NESTED_GUEST_VCPU_MAX) {
        return false;
    }

    if (!(vcpuid < guest->nr_vcpus)) {
        return false;
    }

    vcpu = &guest->vcpus[vcpuid];
    if (!vcpu->enabled) {
        return false;
    }

    if (!inoutbuf) {
        return true;
    }

    /* Check to see if the in/out buffers are registered */
    if (vcpu->runbufin.addr && vcpu->runbufout.addr) {
        return true;
    }

    return false;
}

static void *get_vcpu_state_ptr(SpaprMachineState *spapr,
                                SpaprMachineStateNestedGuest *guest,
                                target_ulong vcpuid)
{
    assert(spapr_nested_vcpu_check(guest, vcpuid, false));
    return &guest->vcpus[vcpuid].state;
}

static void *get_vcpu_ptr(SpaprMachineState *spapr,
                          SpaprMachineStateNestedGuest *guest,
                          target_ulong vcpuid)
{
    assert(spapr_nested_vcpu_check(guest, vcpuid, false));
    return &guest->vcpus[vcpuid];
}

static void *get_guest_ptr(SpaprMachineState *spapr,
                           SpaprMachineStateNestedGuest *guest,
                           target_ulong vcpuid)
{
    return guest; /* for GSBE_NESTED */
}

static void *get_machine_ptr(SpaprMachineState *spapr,
                             SpaprMachineStateNestedGuest *guest,
                             target_ulong vcpuid)
{
    /* ignore guest and vcpuid for this */
    return &spapr->nested;
}

/*
 * set=1 means the L1 is trying to set some state
 * set=0 means the L1 is trying to get some state
 */
static void copy_state_8to8(void *a, void *b, bool set)
{
    /* set takes from the Big endian element_buf and sets internal buffer */

    if (set) {
        *(uint64_t *)a = be64_to_cpu(*(uint64_t *)b);
    } else {
        *(uint64_t *)b = cpu_to_be64(*(uint64_t *)a);
    }
}

static void copy_state_4to4(void *a, void *b, bool set)
{
    if (set) {
        *(uint32_t *)a = be32_to_cpu(*(uint32_t *)b);
    } else {
        *(uint32_t *)b = cpu_to_be32(*((uint32_t *)a));
    }
}

static void copy_state_16to16(void *a, void *b, bool set)
{
    uint64_t *src, *dst;

    if (set) {
        src = b;
        dst = a;

        dst[1] = be64_to_cpu(src[0]);
        dst[0] = be64_to_cpu(src[1]);
    } else {
        src = a;
        dst = b;

        dst[1] = cpu_to_be64(src[0]);
        dst[0] = cpu_to_be64(src[1]);
    }
}

static void copy_state_4to8(void *a, void *b, bool set)
{
    if (set) {
        *(uint64_t *)a  = (uint64_t) be32_to_cpu(*(uint32_t *)b);
    } else {
        *(uint32_t *)b = cpu_to_be32((uint32_t) (*((uint64_t *)a)));
    }
}

static void copy_state_pagetbl(void *a, void *b, bool set)
{
    uint64_t *pagetbl;
    uint64_t *buf; /* 3 double words */
    uint64_t rts;

    assert(set);

    pagetbl = a;
    buf = b;

    *pagetbl = be64_to_cpu(buf[0]);
    /* as per ISA section 6.7.6.1 */
    *pagetbl |= PATE0_HR; /* Host Radix bit is 1 */

    /* RTS */
    rts = be64_to_cpu(buf[1]);
    assert(rts == 52);
    rts = rts - 31; /* since radix tree size = 2^(RTS+31) */
    *pagetbl |=  ((rts & 0x7) << 5); /* RTS2 is bit 56:58 */
    *pagetbl |=  (((rts >> 3) & 0x3) << 61); /* RTS1 is bit 1:2 */

    /* RPDS {Size = 2^(RPDS+3) , RPDS >=5} */
    *pagetbl |= 63 - clz64(be64_to_cpu(buf[2])) - 3;
}

static void copy_state_proctbl(void *a, void *b, bool set)
{
    uint64_t *proctbl;
    uint64_t *buf; /* 2 double words */

    assert(set);

    proctbl = a;
    buf = b;
    /* PRTB: Process Table Base */
    *proctbl = be64_to_cpu(buf[0]);
    /* PRTS: Process Table Size = 2^(12+PRTS) */
    if (be64_to_cpu(buf[1]) == (1ULL << 12)) {
            *proctbl |= 0;
    } else if (be64_to_cpu(buf[1]) == (1ULL << 24)) {
            *proctbl |= 12;
    } else {
        g_assert_not_reached();
    }
}

static void copy_state_runbuf(void *a, void *b, bool set)
{
    uint64_t *buf; /* 2 double words */
    struct SpaprMachineStateNestedGuestVcpuRunBuf *runbuf;

    assert(set);

    runbuf = a;
    buf = b;

    runbuf->addr = be64_to_cpu(buf[0]);
    assert(runbuf->addr);

    /* per spec */
    assert(be64_to_cpu(buf[1]) <= 16384);

    /*
     * This will also hit in the input buffer but should be fine for
     * now. If not we can split this function.
     */
    assert(be64_to_cpu(buf[1]) >= VCPU_OUT_BUF_MIN_SZ);

    runbuf->size = be64_to_cpu(buf[1]);
}

/* tell the L1 how big we want the output vcpu run buffer */
static void out_buf_min_size(void *a, void *b, bool set)
{
    uint64_t *buf; /* 1 double word */

    assert(!set);

    buf = b;

    buf[0] = cpu_to_be64(VCPU_OUT_BUF_MIN_SZ);
}

static void copy_logical_pvr(void *a, void *b, bool set)
{
    SpaprMachineStateNestedGuest *guest;
    uint32_t *buf; /* 1 word */
    uint32_t *pvr_logical_ptr;
    uint32_t pvr_logical;
    target_ulong pcr = 0;

    pvr_logical_ptr = a;
    buf = b;

    if (!set) {
        buf[0] = cpu_to_be32(*pvr_logical_ptr);
        return;
    }

    pvr_logical = be32_to_cpu(buf[0]);

    *pvr_logical_ptr = pvr_logical;

    if (*pvr_logical_ptr) {
        switch (*pvr_logical_ptr) {
        case CPU_POWERPC_LOGICAL_3_10_P11:
        case CPU_POWERPC_LOGICAL_3_10:
            pcr = PCR_COMPAT_3_10 | PCR_COMPAT_3_00;
            break;
        case CPU_POWERPC_LOGICAL_3_00:
            pcr = PCR_COMPAT_3_00;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Could not set PCR for LPVR=0x%08x\n",
                          *pvr_logical_ptr);
            return;
        }
    }

    guest = container_of(pvr_logical_ptr,
                         struct SpaprMachineStateNestedGuest,
                         pvr_logical);
    for (int i = 0; i < guest->nr_vcpus; i++) {
        guest->vcpus[i].state.pcr = ~pcr | HVMASK_PCR;
    }
}

static void copy_tb_offset(void *a, void *b, bool set)
{
    SpaprMachineStateNestedGuest *guest;
    uint64_t *buf; /* 1 double word */
    uint64_t *tb_offset_ptr;
    uint64_t tb_offset;

    tb_offset_ptr = a;
    buf = b;

    if (!set) {
        buf[0] = cpu_to_be64(*tb_offset_ptr);
        return;
    }

    tb_offset = be64_to_cpu(buf[0]);
    /* need to copy this to the individual tb_offset for each vcpu */
    guest = container_of(tb_offset_ptr,
                         struct SpaprMachineStateNestedGuest,
                         tb_offset);
    for (int i = 0; i < guest->nr_vcpus; i++) {
        guest->vcpus[i].tb_offset = tb_offset;
    }
}

static void copy_state_hdecr(void *a, void *b, bool set)
{
    uint64_t *buf; /* 1 double word */
    uint64_t *hdecr_expiry_tb;

    hdecr_expiry_tb = a;
    buf = b;

    if (!set) {
        buf[0] = cpu_to_be64(*hdecr_expiry_tb);
        return;
    }

    *hdecr_expiry_tb = be64_to_cpu(buf[0]);
}

struct guest_state_element_type guest_state_element_types[] = {
    GUEST_STATE_ELEMENT_NOP(GSB_HV_VCPU_IGNORED_ID, 0),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR0,  gpr[0]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR1,  gpr[1]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR2,  gpr[2]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR3,  gpr[3]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR4,  gpr[4]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR5,  gpr[5]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR6,  gpr[6]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR7,  gpr[7]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR8,  gpr[8]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR9,  gpr[9]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR10, gpr[10]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR11, gpr[11]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR12, gpr[12]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR13, gpr[13]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR14, gpr[14]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR15, gpr[15]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR16, gpr[16]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR17, gpr[17]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR18, gpr[18]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR19, gpr[19]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR20, gpr[20]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR21, gpr[21]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR22, gpr[22]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR23, gpr[23]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR24, gpr[24]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR25, gpr[25]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR26, gpr[26]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR27, gpr[27]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR28, gpr[28]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR29, gpr[29]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR30, gpr[30]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_GPR31, gpr[31]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_NIA, nip),
    GSE_ENV_DWM(GSB_VCPU_SPR_MSR, msr, HVMASK_MSR),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_CTR, ctr),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_LR, lr),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_XER, xer),
    GUEST_STATE_ELEMENT_ENV_WW(GSB_VCPU_SPR_CR, cr),
    GUEST_STATE_ELEMENT_NOP_DW(GSB_VCPU_SPR_MMCR3),
    GUEST_STATE_ELEMENT_NOP_DW(GSB_VCPU_SPR_SIER2),
    GUEST_STATE_ELEMENT_NOP_DW(GSB_VCPU_SPR_SIER3),
    GUEST_STATE_ELEMENT_NOP_W(GSB_VCPU_SPR_WORT),
    GSE_ENV_DWM(GSB_VCPU_SPR_LPCR, lpcr, HVMASK_LPCR),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_AMOR, amor),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_HFSCR, hfscr),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_DAWR0, dawr0),
    GUEST_STATE_ELEMENT_ENV_W(GSB_VCPU_SPR_DAWRX0, dawrx0),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_CIABR, ciabr),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_PURR,  purr),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_SPURR, spurr),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_IC,    ic),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_VTB,   vtb),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_HDAR,  hdar),
    GUEST_STATE_ELEMENT_ENV_W(GSB_VCPU_SPR_HDSISR, hdsisr),
    GUEST_STATE_ELEMENT_ENV_W(GSB_VCPU_SPR_HEIR,   heir),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_ASDR,  asdr),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_SRR0,  srr0),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_SRR1,  srr1),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_SPRG0, sprg0),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_SPRG1, sprg1),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_SPRG2, sprg2),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_SPRG3, sprg3),
    GUEST_STATE_ELEMENT_ENV_W(GSB_VCPU_SPR_PIDR,   pidr),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_CFAR,  cfar),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_PPR,   ppr),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_DAWR1, dawr1),
    GUEST_STATE_ELEMENT_ENV_W(GSB_VCPU_SPR_DAWRX1, dawrx1),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_DEXCR, dexcr),
    GSE_ENV_DWM(GSB_VCPU_SPR_HDEXCR, hdexcr, HVMASK_HDEXCR),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_HASHKEYR, hashkeyr),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_HASHPKEYR, hashpkeyr),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR0, vsr[0]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR1, vsr[1]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR2, vsr[2]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR3, vsr[3]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR4, vsr[4]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR5, vsr[5]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR6, vsr[6]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR7, vsr[7]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR8, vsr[8]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR9, vsr[9]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR10, vsr[10]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR11, vsr[11]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR12, vsr[12]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR13, vsr[13]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR14, vsr[14]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR15, vsr[15]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR16, vsr[16]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR17, vsr[17]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR18, vsr[18]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR19, vsr[19]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR20, vsr[20]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR21, vsr[21]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR22, vsr[22]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR23, vsr[23]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR24, vsr[24]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR25, vsr[25]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR26, vsr[26]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR27, vsr[27]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR28, vsr[28]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR29, vsr[29]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR30, vsr[30]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR31, vsr[31]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR32, vsr[32]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR33, vsr[33]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR34, vsr[34]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR35, vsr[35]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR36, vsr[36]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR37, vsr[37]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR38, vsr[38]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR39, vsr[39]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR40, vsr[40]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR41, vsr[41]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR42, vsr[42]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR43, vsr[43]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR44, vsr[44]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR45, vsr[45]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR46, vsr[46]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR47, vsr[47]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR48, vsr[48]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR49, vsr[49]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR50, vsr[50]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR51, vsr[51]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR52, vsr[52]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR53, vsr[53]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR54, vsr[54]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR55, vsr[55]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR56, vsr[56]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR57, vsr[57]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR58, vsr[58]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR59, vsr[59]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR60, vsr[60]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR61, vsr[61]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR62, vsr[62]),
    GUEST_STATE_ELEMENT_ENV_QW(GSB_VCPU_SPR_VSR63, vsr[63]),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_EBBHR, ebbhr),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_TAR,   tar),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_EBBRR, ebbrr),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_BESCR, bescr),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_IAMR,  iamr),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_AMR,   amr),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_UAMOR, uamor),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_DSCR,  dscr),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_FSCR,  fscr),
    GUEST_STATE_ELEMENT_ENV_W(GSB_VCPU_SPR_PSPB,   pspb),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_CTRL,  ctrl),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_DPDES, dpdes),
    GUEST_STATE_ELEMENT_ENV_W(GSB_VCPU_SPR_VRSAVE, vrsave),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_DAR,   dar),
    GUEST_STATE_ELEMENT_ENV_W(GSB_VCPU_SPR_DSISR,  dsisr),
    GUEST_STATE_ELEMENT_ENV_W(GSB_VCPU_SPR_PMC1,   pmc1),
    GUEST_STATE_ELEMENT_ENV_W(GSB_VCPU_SPR_PMC2,   pmc2),
    GUEST_STATE_ELEMENT_ENV_W(GSB_VCPU_SPR_PMC3,   pmc3),
    GUEST_STATE_ELEMENT_ENV_W(GSB_VCPU_SPR_PMC4,   pmc4),
    GUEST_STATE_ELEMENT_ENV_W(GSB_VCPU_SPR_PMC5,   pmc5),
    GUEST_STATE_ELEMENT_ENV_W(GSB_VCPU_SPR_PMC6,   pmc6),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_MMCR0, mmcr0),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_MMCR1, mmcr1),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_MMCR2, mmcr2),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_MMCRA, mmcra),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_SDAR , sdar),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_SIAR , siar),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_SIER , sier),
    GUEST_STATE_ELEMENT_ENV_WW(GSB_VCPU_SPR_VSCR,  vscr),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_SPR_FPSCR, fpscr),
    GUEST_STATE_ELEMENT_ENV_DW(GSB_VCPU_DEC_EXPIRE_TB, dec_expiry_tb),
    GSBE_NESTED(GSB_PART_SCOPED_PAGETBL, 0x18, parttbl[0],  copy_state_pagetbl),
    GSBE_NESTED(GSB_PROCESS_TBL,         0x10, parttbl[1],  copy_state_proctbl),
    GSBE_NESTED(GSB_VCPU_LPVR,           0x4,  pvr_logical, copy_logical_pvr),
    GSBE_NESTED_MSK(GSB_TB_OFFSET, 0x8, tb_offset, copy_tb_offset,
                    HVMASK_TB_OFFSET),
    GSBE_NESTED_VCPU(GSB_VCPU_IN_BUFFER, 0x10, runbufin,    copy_state_runbuf),
    GSBE_NESTED_VCPU(GSB_VCPU_OUT_BUFFER, 0x10, runbufout,   copy_state_runbuf),
    GSBE_NESTED_VCPU(GSB_VCPU_OUT_BUF_MIN_SZ, 0x8, runbufout, out_buf_min_size),
    GSBE_NESTED_VCPU(GSB_VCPU_HDEC_EXPIRY_TB, 0x8, hdecr_expiry_tb,
                     copy_state_hdecr),
    GSBE_NESTED_MACHINE_DW(GSB_L0_GUEST_HEAP_INUSE, l0_guest_heap_inuse),
    GSBE_NESTED_MACHINE_DW(GSB_L0_GUEST_HEAP_MAX, l0_guest_heap_max),
    GSBE_NESTED_MACHINE_DW(GSB_L0_GUEST_PGTABLE_SIZE_INUSE,
                           l0_guest_pgtable_size_inuse),
    GSBE_NESTED_MACHINE_DW(GSB_L0_GUEST_PGTABLE_SIZE_MAX,
                           l0_guest_pgtable_size_max),
    GSBE_NESTED_MACHINE_DW(GSB_L0_GUEST_PGTABLE_RECLAIMED,
                           l0_guest_pgtable_reclaimed),
};

void spapr_nested_gsb_init(void)
{
    struct guest_state_element_type *type;

    /* Init the guest state elements lookup table, flags for now */
    for (int i = 0; i < ARRAY_SIZE(guest_state_element_types); i++) {
        type = &guest_state_element_types[i];

        assert(type->id <= GSB_LAST);
        if (type->id >= GSB_VCPU_SPR_HDAR)
            /* 0xf000 - 0xf005 Thread + RO */
            type->flags = GUEST_STATE_ELEMENT_TYPE_FLAG_READ_ONLY;
        else if (type->id >= GSB_VCPU_IN_BUFFER)
            /* 0x0c00 - 0xf000 Thread + RW */
            type->flags = 0;
        else if (type->id >= GSB_L0_GUEST_HEAP_INUSE)

            /*0x0800 - 0x0804 Hostwide Counters + RO */
            type->flags = GUEST_STATE_ELEMENT_TYPE_FLAG_HOST_WIDE |
                          GUEST_STATE_ELEMENT_TYPE_FLAG_READ_ONLY;
        else if (type->id >= GSB_VCPU_LPVR)
            /* 0x0003 - 0x07ff Guest + RW */
            type->flags = GUEST_STATE_ELEMENT_TYPE_FLAG_GUEST_WIDE;
        else if (type->id >= GSB_HV_VCPU_STATE_SIZE)
            /* 0x0001 - 0x0002 Guest + RO */
            type->flags = GUEST_STATE_ELEMENT_TYPE_FLAG_READ_ONLY |
                          GUEST_STATE_ELEMENT_TYPE_FLAG_GUEST_WIDE;
    }
}

static struct guest_state_element *guest_state_element_next(
    struct guest_state_element *element,
    int64_t *len,
    int64_t *num_elements)
{
    uint16_t size;

    /* size is of element->value[] only. Not whole guest_state_element */
    size = be16_to_cpu(element->size);

    if (len) {
        *len -= size + offsetof(struct guest_state_element, value);
    }

    if (num_elements) {
        *num_elements -= 1;
    }

    return (struct guest_state_element *)(element->value + size);
}

static
struct guest_state_element_type *guest_state_element_type_find(uint16_t id)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(guest_state_element_types); i++)
        if (id == guest_state_element_types[i].id) {
            return &guest_state_element_types[i];
        }

    return NULL;
}

static void log_element(struct guest_state_element *element,
                        struct guest_state_request *gsr)
{
    qemu_log_mask(LOG_GUEST_ERROR, "h_guest_%s_state id:0x%04x size:0x%04x",
                  gsr->flags & GUEST_STATE_REQUEST_SET ? "set" : "get",
                  be16_to_cpu(element->id), be16_to_cpu(element->size));
    qemu_log_mask(LOG_GUEST_ERROR, "buf:0x%016"PRIx64" ...\n",
                  be64_to_cpu(*(uint64_t *)element->value));
}

static bool guest_state_request_check(struct guest_state_request *gsr)
{
    int64_t num_elements, len = gsr->len;
    struct guest_state_buffer *gsb = gsr->gsb;
    struct guest_state_element *element;
    struct guest_state_element_type *type;
    uint16_t id, size;

    /* gsb->num_elements = 0 == 32 bits long */
    assert(len >= 4);

    num_elements = be32_to_cpu(gsb->num_elements);
    element = gsb->elements;
    len -= sizeof(gsb->num_elements);

    /* Walk the buffer to validate the length */
    while (num_elements) {

        id = be16_to_cpu(element->id);
        size = be16_to_cpu(element->size);

        if (false) {
            log_element(element, gsr);
        }
        /* buffer size too small */
        if (len < 0) {
            return false;
        }

        type = guest_state_element_type_find(id);
        if (!type) {
            qemu_log_mask(LOG_GUEST_ERROR, "Element ID %04x unknown\n", id);
            log_element(element, gsr);
            return false;
        }

        if (id == GSB_HV_VCPU_IGNORED_ID) {
            goto next_element;
        }

        if (size != type->size) {
            qemu_log_mask(LOG_GUEST_ERROR, "Size mismatch. Element ID:%04x."
                          "Size Exp:%i Got:%i\n", id, type->size, size);
            log_element(element, gsr);
            return false;
        }

        if ((type->flags & GUEST_STATE_ELEMENT_TYPE_FLAG_READ_ONLY) &&
            (gsr->flags & GUEST_STATE_REQUEST_SET)) {
            qemu_log_mask(LOG_GUEST_ERROR, "Trying to set a read-only Element "
                          "ID:%04x.\n", id);
            return false;
        }

        if (type->flags & GUEST_STATE_ELEMENT_TYPE_FLAG_HOST_WIDE) {
            /* Hostwide elements cant be clubbed with other types */
            if (!(gsr->flags & GUEST_STATE_REQUEST_HOST_WIDE)) {
                qemu_log_mask(LOG_GUEST_ERROR, "trying to get/set a host wide "
                              "Element ID:%04x.\n", id);
                return false;
            }
        } else  if (type->flags & GUEST_STATE_ELEMENT_TYPE_FLAG_GUEST_WIDE) {
            /* guest wide element type */
            if (!(gsr->flags & GUEST_STATE_REQUEST_GUEST_WIDE)) {
                qemu_log_mask(LOG_GUEST_ERROR, "trying to get/set a guest wide "
                              "Element ID:%04x.\n", id);
                return false;
            }
        } else {
            /* thread wide element type */
            if (gsr->flags & (GUEST_STATE_REQUEST_GUEST_WIDE |
                              GUEST_STATE_REQUEST_HOST_WIDE)) {
                qemu_log_mask(LOG_GUEST_ERROR, "trying to get/set a thread wide"
                            " Element ID:%04x.\n", id);
                return false;
            }
        }
next_element:
        element = guest_state_element_next(element, &len, &num_elements);

    }
    return true;
}

static bool is_gsr_invalid(struct guest_state_request *gsr,
                                   struct guest_state_element *element,
                                   struct guest_state_element_type *type)
{
    if ((gsr->flags & GUEST_STATE_REQUEST_SET) &&
        (*(uint64_t *)(element->value) & ~(type->mask))) {
        log_element(element, gsr);
        qemu_log_mask(LOG_GUEST_ERROR, "L1 can't set reserved bits "
                      "(allowed mask: 0x%08"PRIx64")\n", type->mask);
        return true;
    }
    return false;
}

static target_ulong h_guest_get_capabilities(PowerPCCPU *cpu,
                                             SpaprMachineState *spapr,
                                             target_ulong opcode,
                                             target_ulong *args)
{
    CPUPPCState *env = &cpu->env;
    target_ulong flags = args[0];

    if (flags) { /* don't handle any flags capabilities for now */
        return H_PARAMETER;
    }

    /* P11 capabilities */
    if (ppc_check_compat(cpu, CPU_POWERPC_LOGICAL_3_10_P11, 0,
        spapr->max_compat_pvr)) {
        env->gpr[4] |= H_GUEST_CAPABILITIES_P11_MODE;
    }

    /* P10 capabilities */
    if (ppc_check_compat(cpu, CPU_POWERPC_LOGICAL_3_10, 0,
        spapr->max_compat_pvr)) {
        env->gpr[4] |= H_GUEST_CAPABILITIES_P10_MODE;
    }

    /* P9 capabilities */
    if (ppc_check_compat(cpu, CPU_POWERPC_LOGICAL_3_00, 0,
        spapr->max_compat_pvr)) {
        env->gpr[4] |= H_GUEST_CAPABILITIES_P9_MODE;
    }

    return H_SUCCESS;
}

static target_ulong h_guest_set_capabilities(PowerPCCPU *cpu,
                                             SpaprMachineState *spapr,
                                             target_ulong opcode,
                                              target_ulong *args)
{
    CPUPPCState *env = &cpu->env;
    target_ulong flags = args[0];
    target_ulong capabilities = args[1];
    env->gpr[4] = 0;

    if (flags) { /* don't handle any flags capabilities for now */
        return H_PARAMETER;
    }

    if (capabilities & H_GUEST_CAPABILITIES_COPY_MEM) {
        env->gpr[4] = 1;
        return H_P2; /* isn't supported */
    }

    /*
     * If there are no capabilities configured, set the R5 to the index of
     * the first supported Power Processor Mode
     */
    if (!capabilities) {
        env->gpr[4] = 1;

        /* set R5 to the first supported Power Processor Mode */
        if (ppc_check_compat(cpu, CPU_POWERPC_LOGICAL_3_10_P11, 0,
                             spapr->max_compat_pvr)) {
            env->gpr[5] = H_GUEST_CAP_P11_MODE_BMAP;
        } else if (ppc_check_compat(cpu, CPU_POWERPC_LOGICAL_3_10, 0,
                             spapr->max_compat_pvr)) {
            env->gpr[5] = H_GUEST_CAP_P10_MODE_BMAP;
        } else if (ppc_check_compat(cpu, CPU_POWERPC_LOGICAL_3_00, 0,
                                    spapr->max_compat_pvr)) {
            env->gpr[5] = H_GUEST_CAP_P9_MODE_BMAP;
        }

        return H_P2;
    }

    /*
     * If an invalid capability is set, R5 should contain the index of the
     * invalid capability bit
     */
    if (capabilities & ~H_GUEST_CAP_VALID_MASK) {
        env->gpr[4] = 1;

        /* Set R5 to the index of the invalid capability */
        env->gpr[5] = 63 - ctz64(capabilities);

        return H_P2;
    }

    if (!spapr->nested.capabilities_set) {
        spapr->nested.capabilities_set = true;
        spapr->nested.pvr_base = env->spr[SPR_PVR];
        return H_SUCCESS;
    } else {
        return H_STATE;
    }
}

static void
destroy_guest_helper(gpointer value)
{
    struct SpaprMachineStateNestedGuest *guest = value;
    g_free(guest->vcpus);
    g_free(guest);
}

static target_ulong h_guest_create(PowerPCCPU *cpu,
                                   SpaprMachineState *spapr,
                                   target_ulong opcode,
                                   target_ulong *args)
{
    CPUPPCState *env = &cpu->env;
    target_ulong flags = args[0];
    target_ulong continue_token = args[1];
    uint64_t guestid;
    int nguests = 0;
    struct SpaprMachineStateNestedGuest *guest;

    if (flags) { /* don't handle any flags for now */
        return H_UNSUPPORTED_FLAG;
    }

    if (continue_token != -1) {
        return H_P2;
    }

    if (!spapr->nested.capabilities_set) {
        return H_STATE;
    }

    if (!spapr->nested.guests) {
        spapr->nested.guests = g_hash_table_new_full(NULL,
                                                     NULL,
                                                     NULL,
                                                     destroy_guest_helper);
    }

    nguests = g_hash_table_size(spapr->nested.guests);

    if (nguests == PAPR_NESTED_GUEST_MAX) {
        return H_NO_MEM;
    }

    /* Lookup for available guestid */
    for (guestid = 1; guestid < PAPR_NESTED_GUEST_MAX; guestid++) {
        if (!(g_hash_table_lookup(spapr->nested.guests,
                                  GINT_TO_POINTER(guestid)))) {
            break;
        }
    }

    if (guestid == PAPR_NESTED_GUEST_MAX) {
        return H_NO_MEM;
    }

    guest = g_try_new0(struct SpaprMachineStateNestedGuest, 1);
    if (!guest) {
        return H_NO_MEM;
    }

    guest->pvr_logical = spapr->nested.pvr_base;
    g_hash_table_insert(spapr->nested.guests, GINT_TO_POINTER(guestid), guest);
    env->gpr[4] = guestid;

    return H_SUCCESS;
}

static target_ulong h_guest_delete(PowerPCCPU *cpu,
                                   SpaprMachineState *spapr,
                                   target_ulong opcode,
                                   target_ulong *args)
{
    target_ulong flags = args[0];
    target_ulong guestid = args[1];
    struct SpaprMachineStateNestedGuest *guest;

    /*
     * handle flag deleteAllGuests, if set:
     * guestid is ignored and all guests are deleted
     *
     */
    if (flags & ~H_GUEST_DELETE_ALL_FLAG) {
        return H_UNSUPPORTED_FLAG; /* other flag bits reserved */
    } else if (flags & H_GUEST_DELETE_ALL_FLAG) {
        g_hash_table_destroy(spapr->nested.guests);
        return H_SUCCESS;
    }

    guest = g_hash_table_lookup(spapr->nested.guests, GINT_TO_POINTER(guestid));
    if (!guest) {
        return H_P2;
    }

    g_hash_table_remove(spapr->nested.guests, GINT_TO_POINTER(guestid));

    return H_SUCCESS;
}

static target_ulong h_guest_create_vcpu(PowerPCCPU *cpu,
                                        SpaprMachineState *spapr,
                                        target_ulong opcode,
                                        target_ulong *args)
{
    target_ulong flags = args[0];
    target_ulong guestid = args[1];
    target_ulong vcpuid = args[2];
    SpaprMachineStateNestedGuest *guest;

    if (flags) { /* don't handle any flags for now */
        return H_UNSUPPORTED_FLAG;
    }

    guest = spapr_get_nested_guest(spapr, guestid);
    if (!guest) {
        return H_P2;
    }

    if (vcpuid < guest->nr_vcpus) {
        qemu_log_mask(LOG_UNIMP, "vcpuid " TARGET_FMT_ld " already in use.",
                      vcpuid);
        return H_IN_USE;
    }
    /* linear vcpuid allocation only */
    assert(vcpuid == guest->nr_vcpus);

    if (guest->nr_vcpus >= PAPR_NESTED_GUEST_VCPU_MAX) {
        return H_P3;
    }

    SpaprMachineStateNestedGuestVcpu *vcpus, *curr_vcpu;
    vcpus = g_try_renew(struct SpaprMachineStateNestedGuestVcpu,
                        guest->vcpus,
                        guest->nr_vcpus + 1);
    if (!vcpus) {
        return H_NO_MEM;
    }
    guest->vcpus = vcpus;
    curr_vcpu = &vcpus[guest->nr_vcpus];
    memset(curr_vcpu, 0, sizeof(SpaprMachineStateNestedGuestVcpu));

    curr_vcpu->enabled = true;
    guest->nr_vcpus++;

    return H_SUCCESS;
}

static target_ulong getset_state(SpaprMachineState *spapr,
                                 SpaprMachineStateNestedGuest *guest,
                                 uint64_t vcpuid,
                                 struct guest_state_request *gsr)
{
    void *ptr;
    uint16_t id;
    struct guest_state_element *element;
    struct guest_state_element_type *type;
    int64_t lenleft, num_elements;

    lenleft = gsr->len;

    if (!guest_state_request_check(gsr)) {
        return H_P3;
    }

    num_elements = be32_to_cpu(gsr->gsb->num_elements);
    element = gsr->gsb->elements;
    /* Process the elements */
    while (num_elements) {
        type = NULL;
        /* log_element(element, gsr); */

        id = be16_to_cpu(element->id);
        if (id == GSB_HV_VCPU_IGNORED_ID) {
            goto next_element;
        }

        type = guest_state_element_type_find(id);
        assert(type);

        /* Get pointer to guest data to get/set */
        if (type->location && type->copy) {
            ptr = type->location(spapr, guest, vcpuid);
            assert(ptr);
            if (!~(type->mask) && is_gsr_invalid(gsr, element, type)) {
                return H_INVALID_ELEMENT_VALUE;
            }
            type->copy(ptr + type->offset, element->value,
                       gsr->flags & GUEST_STATE_REQUEST_SET ? true : false);
        }

next_element:
        element = guest_state_element_next(element, &lenleft, &num_elements);
    }

    return H_SUCCESS;
}

static target_ulong map_and_getset_state(PowerPCCPU *cpu,
                                         SpaprMachineState *spapr,
                                         SpaprMachineStateNestedGuest *guest,
                                         uint64_t vcpuid,
                                         struct guest_state_request *gsr)
{
    target_ulong rc;
    int64_t len;
    bool is_write;

    len = gsr->len;
    /* only get_state would require write access to the provided buffer */
    is_write = (gsr->flags & GUEST_STATE_REQUEST_SET) ? false : true;
    gsr->gsb = address_space_map(CPU(cpu)->as, gsr->buf, (uint64_t *)&len,
                                 is_write, MEMTXATTRS_UNSPECIFIED);
    if (!gsr->gsb) {
        rc = H_P3;
        goto out1;
    }

    if (len != gsr->len) {
        rc = H_P3;
        goto out1;
    }

    rc = getset_state(spapr, guest, vcpuid, gsr);

out1:
    address_space_unmap(CPU(cpu)->as, gsr->gsb, len, is_write, len);
    return rc;
}

static target_ulong h_guest_getset_state(PowerPCCPU *cpu,
                                         SpaprMachineState *spapr,
                                         target_ulong *args,
                                         bool set)
{
    target_ulong flags = args[0];
    target_ulong lpid = args[1];
    target_ulong vcpuid = args[2];
    target_ulong buf = args[3];
    target_ulong buflen = args[4];
    struct guest_state_request gsr;
    SpaprMachineStateNestedGuest *guest = NULL;

    gsr.buf = buf;
    assert(buflen <= GSB_MAX_BUF_SIZE);
    gsr.len = buflen;
    gsr.flags = 0;

    /* Works for both get/set state */
    if ((flags & H_GUEST_GET_STATE_FLAGS_GUEST_WIDE) ||
        (flags & H_GUEST_SET_STATE_FLAGS_GUEST_WIDE)) {
        gsr.flags |= GUEST_STATE_REQUEST_GUEST_WIDE;
    }

    if (set) {
        if (flags & ~H_GUEST_SET_STATE_FLAGS_MASK) {
            return H_PARAMETER;
        }
        gsr.flags |= GUEST_STATE_REQUEST_SET;
    } else {
        /*
         * No reserved fields to be set in flags nor both
         * GUEST/HOST wide bits
         */
        if ((flags & ~H_GUEST_GET_STATE_FLAGS_MASK) ||
            (flags == H_GUEST_GET_STATE_FLAGS_MASK)) {
            return H_PARAMETER;
        }

        if (flags & H_GUEST_GET_STATE_FLAGS_HOST_WIDE) {
            gsr.flags |= GUEST_STATE_REQUEST_HOST_WIDE;
        }
    }

    if (!(gsr.flags & GUEST_STATE_REQUEST_HOST_WIDE)) {
        guest = spapr_get_nested_guest(spapr, lpid);
        if (!guest) {
            return H_P2;
        }
    }
    return map_and_getset_state(cpu, spapr, guest, vcpuid, &gsr);
}

static target_ulong h_guest_set_state(PowerPCCPU *cpu,
                                      SpaprMachineState *spapr,
                                      target_ulong opcode,
                                      target_ulong *args)
{
    return h_guest_getset_state(cpu, spapr, args, true);
}

static target_ulong h_guest_get_state(PowerPCCPU *cpu,
                                      SpaprMachineState *spapr,
                                      target_ulong opcode,
                                      target_ulong *args)
{
    return h_guest_getset_state(cpu, spapr, args, false);
}

static void exit_nested_store_l2(PowerPCCPU *cpu, int excp,
                                 SpaprMachineStateNestedGuestVcpu *vcpu)
{
    CPUPPCState *env = &cpu->env;
    SpaprCpuState *spapr_cpu = spapr_cpu_state(cpu);
    target_ulong now, hdar, hdsisr, asdr;

    assert(sizeof(env->gpr) == sizeof(vcpu->state.gpr)); /* sanity check */

    now = cpu_ppc_load_tbl(env); /* L2 timebase */
    now -= vcpu->tb_offset; /* L1 timebase */
    vcpu->state.dec_expiry_tb = now - cpu_ppc_load_decr(env);
    cpu_ppc_store_decr(env, spapr_cpu->nested_host_state->dec_expiry_tb - now);
    /* backup hdar, hdsisr, asdr if reqd later below */
    hdar   = vcpu->state.hdar;
    hdsisr = vcpu->state.hdsisr;
    asdr   = vcpu->state.asdr;

    nested_save_state(&vcpu->state, cpu);

    if (excp == POWERPC_EXCP_MCHECK ||
        excp == POWERPC_EXCP_RESET ||
        excp == POWERPC_EXCP_SYSCALL) {
        vcpu->state.nip = env->spr[SPR_SRR0];
        vcpu->state.msr = env->spr[SPR_SRR1] & env->msr_mask;
    } else {
        vcpu->state.nip = env->spr[SPR_HSRR0];
        vcpu->state.msr = env->spr[SPR_HSRR1] & env->msr_mask;
    }

    /* hdar, hdsisr, asdr should be retained unless certain exceptions */
    if ((excp != POWERPC_EXCP_HDSI) && (excp != POWERPC_EXCP_HISI)) {
        vcpu->state.asdr = asdr;
    } else if (excp != POWERPC_EXCP_HDSI) {
        vcpu->state.hdar   = hdar;
        vcpu->state.hdsisr = hdsisr;
    }
}

static int get_exit_ids(uint64_t srr0, uint16_t ids[16])
{
    int nr;

    switch (srr0) {
    case 0xc00:
        nr = 10;
        ids[0] = GSB_VCPU_GPR3;
        ids[1] = GSB_VCPU_GPR4;
        ids[2] = GSB_VCPU_GPR5;
        ids[3] = GSB_VCPU_GPR6;
        ids[4] = GSB_VCPU_GPR7;
        ids[5] = GSB_VCPU_GPR8;
        ids[6] = GSB_VCPU_GPR9;
        ids[7] = GSB_VCPU_GPR10;
        ids[8] = GSB_VCPU_GPR11;
        ids[9] = GSB_VCPU_GPR12;
        break;
    case 0xe00:
        nr = 5;
        ids[0] = GSB_VCPU_SPR_HDAR;
        ids[1] = GSB_VCPU_SPR_HDSISR;
        ids[2] = GSB_VCPU_SPR_ASDR;
        ids[3] = GSB_VCPU_SPR_NIA;
        ids[4] = GSB_VCPU_SPR_MSR;
        break;
    case 0xe20:
        nr = 4;
        ids[0] = GSB_VCPU_SPR_HDAR;
        ids[1] = GSB_VCPU_SPR_ASDR;
        ids[2] = GSB_VCPU_SPR_NIA;
        ids[3] = GSB_VCPU_SPR_MSR;
        break;
    case 0xe40:
        nr = 3;
        ids[0] = GSB_VCPU_SPR_HEIR;
        ids[1] = GSB_VCPU_SPR_NIA;
        ids[2] = GSB_VCPU_SPR_MSR;
        break;
    case 0xf80:
        nr = 3;
        ids[0] = GSB_VCPU_SPR_HFSCR;
        ids[1] = GSB_VCPU_SPR_NIA;
        ids[2] = GSB_VCPU_SPR_MSR;
        break;
    default:
        nr = 0;
        break;
    }

    return nr;
}

static void exit_process_output_buffer(SpaprMachineState *spapr,
                                       PowerPCCPU *cpu,
                                       SpaprMachineStateNestedGuest *guest,
                                       target_ulong vcpuid,
                                       target_ulong *r3)
{
    SpaprMachineStateNestedGuestVcpu *vcpu = &guest->vcpus[vcpuid];
    struct guest_state_request gsr;
    struct guest_state_buffer *gsb;
    struct guest_state_element *element;
    struct guest_state_element_type *type;
    int exit_id_count = 0;
    uint16_t exit_cause_ids[16];
    hwaddr len;

    len = vcpu->runbufout.size;
    gsb = address_space_map(CPU(cpu)->as, vcpu->runbufout.addr, &len, true,
                            MEMTXATTRS_UNSPECIFIED);
    if (!gsb || len != vcpu->runbufout.size) {
        address_space_unmap(CPU(cpu)->as, gsb, len, true, len);
        *r3 = H_P2;
        return;
    }

    exit_id_count = get_exit_ids(*r3, exit_cause_ids);

    /* Create a buffer of elements to send back */
    gsb->num_elements = cpu_to_be32(exit_id_count);
    element = gsb->elements;
    for (int i = 0; i < exit_id_count; i++) {
        type = guest_state_element_type_find(exit_cause_ids[i]);
        assert(type);
        element->id = cpu_to_be16(exit_cause_ids[i]);
        element->size = cpu_to_be16(type->size);
        element = guest_state_element_next(element, NULL, NULL);
    }
    gsr.gsb = gsb;
    gsr.len = VCPU_OUT_BUF_MIN_SZ;
    gsr.flags = 0; /* get + never guest wide */
    getset_state(spapr, guest, vcpuid, &gsr);

    address_space_unmap(CPU(cpu)->as, gsb, len, true, len);
}

static
void spapr_exit_nested_papr(SpaprMachineState *spapr, PowerPCCPU *cpu, int excp)
{
    CPUPPCState *env = &cpu->env;
    CPUState *cs = CPU(cpu);
    SpaprCpuState *spapr_cpu = spapr_cpu_state(cpu);
    target_ulong r3_return = env->excp_vectors[excp]; /* hcall return value */
    target_ulong lpid = 0, vcpuid = 0;
    struct SpaprMachineStateNestedGuestVcpu *vcpu = NULL;
    struct SpaprMachineStateNestedGuest *guest = NULL;

    lpid = spapr_cpu->nested_host_state->gpr[5];
    vcpuid = spapr_cpu->nested_host_state->gpr[6];
    guest = spapr_get_nested_guest(spapr, lpid);
    assert(guest);
    spapr_nested_vcpu_check(guest, vcpuid, false);
    vcpu = &guest->vcpus[vcpuid];

    exit_nested_store_l2(cpu, excp, vcpu);
    /* do the output buffer for run_vcpu*/
    exit_process_output_buffer(spapr, cpu, guest, vcpuid, &r3_return);

    assert(env->spr[SPR_LPIDR] != 0);
    nested_load_state(cpu, spapr_cpu->nested_host_state);
    cpu_ppc_decrease_tb_by_offset(env, vcpu->tb_offset);
    env->gpr[3] = H_SUCCESS;
    env->gpr[4] = r3_return;
    nested_post_load_state(env, cs);
    cpu_ppc_hdecr_exit(env);

    spapr_cpu->in_nested = false;
    g_free(spapr_cpu->nested_host_state);
    spapr_cpu->nested_host_state = NULL;
}

void spapr_exit_nested(PowerPCCPU *cpu, int excp)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(qdev_get_machine());
    SpaprCpuState *spapr_cpu = spapr_cpu_state(cpu);

    assert(spapr_cpu->in_nested);
    if (spapr_nested_api(spapr) == NESTED_API_KVM_HV) {
        spapr_exit_nested_hv(cpu, excp);
    } else if (spapr_nested_api(spapr) == NESTED_API_PAPR) {
        spapr_exit_nested_papr(spapr, cpu, excp);
    } else {
        g_assert_not_reached();
    }
}

static void nested_papr_load_l2(PowerPCCPU *cpu,
                                CPUPPCState *env,
                                SpaprMachineStateNestedGuestVcpu *vcpu,
                                target_ulong now)
{
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);
    target_ulong lpcr, lpcr_mask, hdec;
    lpcr_mask = LPCR_DPFD | LPCR_ILE | LPCR_AIL | LPCR_LD | LPCR_MER;

    assert(vcpu);
    assert(sizeof(env->gpr) == sizeof(vcpu->state.gpr));
    nested_load_state(cpu, &vcpu->state);
    lpcr = (env->spr[SPR_LPCR] & ~lpcr_mask) |
           (vcpu->state.lpcr & lpcr_mask);
    lpcr |= LPCR_HR | LPCR_UPRT | LPCR_GTSE | LPCR_HVICE | LPCR_HDICE;
    lpcr &= ~LPCR_LPES0;
    env->spr[SPR_LPCR] = lpcr & pcc->lpcr_mask;

    hdec = vcpu->hdecr_expiry_tb - now;
    cpu_ppc_store_decr(env, vcpu->state.dec_expiry_tb - now);
    cpu_ppc_hdecr_init(env);
    cpu_ppc_store_hdecr(env, hdec);

    cpu_ppc_increase_tb_by_offset(env, vcpu->tb_offset);
}

static void nested_papr_run_vcpu(PowerPCCPU *cpu,
                                 uint64_t lpid,
                                 SpaprMachineStateNestedGuestVcpu *vcpu)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(qdev_get_machine());
    CPUPPCState *env = &cpu->env;
    CPUState *cs = CPU(cpu);
    SpaprCpuState *spapr_cpu = spapr_cpu_state(cpu);
    target_ulong now = cpu_ppc_load_tbl(env);

    assert(env->spr[SPR_LPIDR] == 0);
    assert(spapr->nested.api); /* ensure API version is initialized */
    spapr_cpu->nested_host_state = g_try_new(struct nested_ppc_state, 1);
    assert(spapr_cpu->nested_host_state);
    nested_save_state(spapr_cpu->nested_host_state, cpu);
    spapr_cpu->nested_host_state->dec_expiry_tb = now - cpu_ppc_load_decr(env);
    nested_papr_load_l2(cpu, env, vcpu, now);
    env->spr[SPR_LPIDR] = lpid; /* post load l2 */

    spapr_cpu->in_nested = true;
    nested_post_load_state(env, cs);
}

static target_ulong h_guest_run_vcpu(PowerPCCPU *cpu,
                                     SpaprMachineState *spapr,
                                     target_ulong opcode,
                                     target_ulong *args)
{
    CPUPPCState *env = &cpu->env;
    target_ulong flags = args[0];
    target_ulong lpid = args[1];
    target_ulong vcpuid = args[2];
    struct SpaprMachineStateNestedGuestVcpu *vcpu;
    struct guest_state_request gsr;
    SpaprMachineStateNestedGuest *guest;
    target_ulong rc;

    if (flags) /* don't handle any flags for now */
        return H_PARAMETER;

    guest = spapr_get_nested_guest(spapr, lpid);
    if (!guest) {
        return H_P2;
    }
    if (!spapr_nested_vcpu_check(guest, vcpuid, true)) {
        return H_P3;
    }

    if (guest->parttbl[0] == 0) {
        /* At least need a partition scoped radix tree */
        return H_NOT_AVAILABLE;
    }

    vcpu = &guest->vcpus[vcpuid];

    /* Read run_vcpu input buffer to update state */
    gsr.buf = vcpu->runbufin.addr;
    gsr.len = vcpu->runbufin.size;
    gsr.flags = GUEST_STATE_REQUEST_SET; /* Thread wide + writing */
    rc = map_and_getset_state(cpu, spapr,  guest, vcpuid, &gsr);
    if (rc == H_SUCCESS) {
        nested_papr_run_vcpu(cpu, lpid, vcpu);
    } else {
        env->gpr[3] = rc;
    }
    return env->gpr[3];
}

void spapr_register_nested_hv(void)
{
    spapr_register_hypercall(KVMPPC_H_SET_PARTITION_TABLE, h_set_ptbl);
    spapr_register_hypercall(KVMPPC_H_ENTER_NESTED, h_enter_nested);
    spapr_register_hypercall(KVMPPC_H_TLB_INVALIDATE, h_tlb_invalidate);
    spapr_register_hypercall(KVMPPC_H_COPY_TOFROM_GUEST, h_copy_tofrom_guest);
}

void spapr_unregister_nested_hv(void)
{
    spapr_unregister_hypercall(KVMPPC_H_SET_PARTITION_TABLE);
    spapr_unregister_hypercall(KVMPPC_H_ENTER_NESTED);
    spapr_unregister_hypercall(KVMPPC_H_TLB_INVALIDATE);
    spapr_unregister_hypercall(KVMPPC_H_COPY_TOFROM_GUEST);
}

void spapr_register_nested_papr(void)
{
    spapr_register_hypercall(H_GUEST_GET_CAPABILITIES,
                             h_guest_get_capabilities);
    spapr_register_hypercall(H_GUEST_SET_CAPABILITIES,
                             h_guest_set_capabilities);
    spapr_register_hypercall(H_GUEST_CREATE, h_guest_create);
    spapr_register_hypercall(H_GUEST_DELETE, h_guest_delete);
    spapr_register_hypercall(H_GUEST_CREATE_VCPU, h_guest_create_vcpu);
    spapr_register_hypercall(H_GUEST_SET_STATE, h_guest_set_state);
    spapr_register_hypercall(H_GUEST_GET_STATE, h_guest_get_state);
    spapr_register_hypercall(H_GUEST_RUN_VCPU, h_guest_run_vcpu);
}

void spapr_unregister_nested_papr(void)
{
    spapr_unregister_hypercall(H_GUEST_GET_CAPABILITIES);
    spapr_unregister_hypercall(H_GUEST_SET_CAPABILITIES);
    spapr_unregister_hypercall(H_GUEST_CREATE);
    spapr_unregister_hypercall(H_GUEST_DELETE);
    spapr_unregister_hypercall(H_GUEST_CREATE_VCPU);
    spapr_unregister_hypercall(H_GUEST_SET_STATE);
    spapr_unregister_hypercall(H_GUEST_GET_STATE);
    spapr_unregister_hypercall(H_GUEST_RUN_VCPU);
}

#else
void spapr_exit_nested(PowerPCCPU *cpu, int excp)
{
    g_assert_not_reached();
}

void spapr_register_nested_hv(void)
{
    /* DO NOTHING */
}

void spapr_unregister_nested_hv(void)
{
    /* DO NOTHING */
}

bool spapr_get_pate_nested_hv(SpaprMachineState *spapr, PowerPCCPU *cpu,
                              target_ulong lpid, ppc_v3_pate_t *entry)
{
    return false;
}

bool spapr_get_pate_nested_papr(SpaprMachineState *spapr, PowerPCCPU *cpu,
                                target_ulong lpid, ppc_v3_pate_t *entry)
{
    return false;
}

void spapr_register_nested_papr(void)
{
    /* DO NOTHING */
}

void spapr_unregister_nested_papr(void)
{
    /* DO NOTHING */
}

void spapr_nested_gsb_init(void)
{
    /* DO NOTHING */
}

#endif
