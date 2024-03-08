#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "exec/exec-all.h"
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
        spapr->nested.api = NESTED_API_KVM_HV;
        spapr_unregister_nested_hv();
        spapr_register_nested_hv();
    } else {
        spapr->nested.api = 0;
        spapr->nested.capabilities_set = false;
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
        save->pvr = env->spr[SPR_PVR];
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
    }

    save->tb_offset = env->tb_env->tb_offset;
}

static void nested_load_state(PowerPCCPU *cpu, struct nested_ppc_state *load)
{
    CPUState *cs = CPU(cpu);
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
        env->spr[SPR_PVR] = load->pvr;
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
    }

    env->tb_env->tb_offset = load->tb_offset;

    /*
     * MSR updated, compute hflags and possible interrupts.
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

void spapr_exit_nested(PowerPCCPU *cpu, int excp)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(qdev_get_machine());
    SpaprCpuState *spapr_cpu = spapr_cpu_state(cpu);

    assert(spapr_cpu->in_nested);
    if (spapr_nested_api(spapr) == NESTED_API_KVM_HV) {
        spapr_exit_nested_hv(cpu, excp);
    } else {
        g_assert_not_reached();
    }
}

static
SpaprMachineStateNestedGuest *spapr_get_nested_guest(SpaprMachineState *spapr,
                                                     target_ulong guestid)
{
    SpaprMachineStateNestedGuest *guest;

    guest = g_hash_table_lookup(spapr->nested.guests, GINT_TO_POINTER(guestid));
    return guest;
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
        if (ppc_check_compat(cpu, CPU_POWERPC_LOGICAL_3_10, 0,
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
}

void spapr_unregister_nested_papr(void)
{
    spapr_unregister_hypercall(H_GUEST_GET_CAPABILITIES);
    spapr_unregister_hypercall(H_GUEST_SET_CAPABILITIES);
    spapr_unregister_hypercall(H_GUEST_CREATE);
    spapr_unregister_hypercall(H_GUEST_DELETE);
    spapr_unregister_hypercall(H_GUEST_CREATE_VCPU);
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

void spapr_register_nested_papr(void)
{
    /* DO NOTHING */
}

void spapr_unregister_nested_papr(void)
{
    /* DO NOTHING */
}

#endif
