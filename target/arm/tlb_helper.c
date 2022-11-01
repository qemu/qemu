/*
 * ARM TLB (Translation lookaside buffer) helpers.
 *
 * This code is licensed under the GNU GPL v2 or later.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"


/* Return true if the translation regime is using LPAE format page tables */
bool regime_using_lpae_format(CPUARMState *env, ARMMMUIdx mmu_idx)
{
    int el = regime_el(env, mmu_idx);
    if (el == 2 || arm_el_is_aa64(env, el)) {
        return true;
    }
    if (arm_feature(env, ARM_FEATURE_LPAE)
        && (regime_tcr(env, mmu_idx) & TTBCR_EAE)) {
        return true;
    }
    return false;
}

/*
 * Returns true if the stage 1 translation regime is using LPAE format page
 * tables. Used when raising alignment exceptions, whose FSR changes depending
 * on whether the long or short descriptor format is in use.
 */
bool arm_s1_regime_using_lpae_format(CPUARMState *env, ARMMMUIdx mmu_idx)
{
    mmu_idx = stage_1_mmu_idx(mmu_idx);
    return regime_using_lpae_format(env, mmu_idx);
}

static inline uint32_t merge_syn_data_abort(uint32_t template_syn,
                                            unsigned int target_el,
                                            bool same_el, bool ea,
                                            bool s1ptw, bool is_write,
                                            int fsc)
{
    uint32_t syn;

    /*
     * ISV is only set for data aborts routed to EL2 and
     * never for stage-1 page table walks faulting on stage 2.
     *
     * Furthermore, ISV is only set for certain kinds of load/stores.
     * If the template syndrome does not have ISV set, we should leave
     * it cleared.
     *
     * See ARMv8 specs, D7-1974:
     * ISS encoding for an exception from a Data Abort, the
     * ISV field.
     */
    if (!(template_syn & ARM_EL_ISV) || target_el != 2 || s1ptw) {
        syn = syn_data_abort_no_iss(same_el, 0,
                                    ea, 0, s1ptw, is_write, fsc);
    } else {
        /*
         * Fields: IL, ISV, SAS, SSE, SRT, SF and AR come from the template
         * syndrome created at translation time.
         * Now we create the runtime syndrome with the remaining fields.
         */
        syn = syn_data_abort_with_iss(same_el,
                                      0, 0, 0, 0, 0,
                                      ea, 0, s1ptw, is_write, fsc,
                                      true);
        /* Merge the runtime syndrome with the template syndrome.  */
        syn |= template_syn;
    }
    return syn;
}

static uint32_t compute_fsr_fsc(CPUARMState *env, ARMMMUFaultInfo *fi,
                                int target_el, int mmu_idx, uint32_t *ret_fsc)
{
    ARMMMUIdx arm_mmu_idx = core_to_arm_mmu_idx(env, mmu_idx);
    uint32_t fsr, fsc;

    if (target_el == 2 || arm_el_is_aa64(env, target_el) ||
        arm_s1_regime_using_lpae_format(env, arm_mmu_idx)) {
        /*
         * LPAE format fault status register : bottom 6 bits are
         * status code in the same form as needed for syndrome
         */
        fsr = arm_fi_to_lfsc(fi);
        fsc = extract32(fsr, 0, 6);
    } else {
        fsr = arm_fi_to_sfsc(fi);
        /*
         * Short format FSR : this fault will never actually be reported
         * to an EL that uses a syndrome register. Use a (currently)
         * reserved FSR code in case the constructed syndrome does leak
         * into the guest somehow.
         */
        fsc = 0x3f;
    }

    *ret_fsc = fsc;
    return fsr;
}

static G_NORETURN
void arm_deliver_fault(ARMCPU *cpu, vaddr addr,
                       MMUAccessType access_type,
                       int mmu_idx, ARMMMUFaultInfo *fi)
{
    CPUARMState *env = &cpu->env;
    int target_el;
    bool same_el;
    uint32_t syn, exc, fsr, fsc;

    target_el = exception_target_el(env);
    if (fi->stage2) {
        target_el = 2;
        env->cp15.hpfar_el2 = extract64(fi->s2addr, 12, 47) << 4;
        if (arm_is_secure_below_el3(env) && fi->s1ns) {
            env->cp15.hpfar_el2 |= HPFAR_NS;
        }
    }
    same_el = (arm_current_el(env) == target_el);

    fsr = compute_fsr_fsc(env, fi, target_el, mmu_idx, &fsc);

    if (access_type == MMU_INST_FETCH) {
        syn = syn_insn_abort(same_el, fi->ea, fi->s1ptw, fsc);
        exc = EXCP_PREFETCH_ABORT;
    } else {
        syn = merge_syn_data_abort(env->exception.syndrome, target_el,
                                   same_el, fi->ea, fi->s1ptw,
                                   access_type == MMU_DATA_STORE,
                                   fsc);
        if (access_type == MMU_DATA_STORE
            && arm_feature(env, ARM_FEATURE_V6)) {
            fsr |= (1 << 11);
        }
        exc = EXCP_DATA_ABORT;
    }

    env->exception.vaddress = addr;
    env->exception.fsr = fsr;
    raise_exception(env, exc, syn, target_el);
}

/* Raise a data fault alignment exception for the specified virtual address */
void arm_cpu_do_unaligned_access(CPUState *cs, vaddr vaddr,
                                 MMUAccessType access_type,
                                 int mmu_idx, uintptr_t retaddr)
{
    ARMCPU *cpu = ARM_CPU(cs);
    ARMMMUFaultInfo fi = {};

    /* now we have a real cpu fault */
    cpu_restore_state(cs, retaddr);

    fi.type = ARMFault_Alignment;
    arm_deliver_fault(cpu, vaddr, access_type, mmu_idx, &fi);
}

void helper_exception_pc_alignment(CPUARMState *env, target_ulong pc)
{
    ARMMMUFaultInfo fi = { .type = ARMFault_Alignment };
    int target_el = exception_target_el(env);
    int mmu_idx = cpu_mmu_index(env, true);
    uint32_t fsc;

    env->exception.vaddress = pc;

    /*
     * Note that the fsc is not applicable to this exception,
     * since any syndrome is pcalignment not insn_abort.
     */
    env->exception.fsr = compute_fsr_fsc(env, &fi, target_el, mmu_idx, &fsc);
    raise_exception(env, EXCP_PREFETCH_ABORT, syn_pcalignment(), target_el);
}

#if !defined(CONFIG_USER_ONLY)

/*
 * arm_cpu_do_transaction_failed: handle a memory system error response
 * (eg "no device/memory present at address") by raising an external abort
 * exception
 */
void arm_cpu_do_transaction_failed(CPUState *cs, hwaddr physaddr,
                                   vaddr addr, unsigned size,
                                   MMUAccessType access_type,
                                   int mmu_idx, MemTxAttrs attrs,
                                   MemTxResult response, uintptr_t retaddr)
{
    ARMCPU *cpu = ARM_CPU(cs);
    ARMMMUFaultInfo fi = {};

    /* now we have a real cpu fault */
    cpu_restore_state(cs, retaddr);

    fi.ea = arm_extabort_type(response);
    fi.type = ARMFault_SyncExternal;
    arm_deliver_fault(cpu, addr, access_type, mmu_idx, &fi);
}

bool arm_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                      MMUAccessType access_type, int mmu_idx,
                      bool probe, uintptr_t retaddr)
{
    ARMCPU *cpu = ARM_CPU(cs);
    GetPhysAddrResult res = {};
    ARMMMUFaultInfo local_fi, *fi;
    int ret;

    /*
     * Allow S1_ptw_translate to see any fault generated here.
     * Since this may recurse, read and clear.
     */
    fi = cpu->env.tlb_fi;
    if (fi) {
        cpu->env.tlb_fi = NULL;
    } else {
        fi = memset(&local_fi, 0, sizeof(local_fi));
    }

    /*
     * Walk the page table and (if the mapping exists) add the page
     * to the TLB.  On success, return true.  Otherwise, if probing,
     * return false.  Otherwise populate fsr with ARM DFSR/IFSR fault
     * register format, and signal the fault.
     */
    ret = get_phys_addr(&cpu->env, address, access_type,
                        core_to_arm_mmu_idx(&cpu->env, mmu_idx),
                        &res, fi);
    if (likely(!ret)) {
        /*
         * Map a single [sub]page. Regions smaller than our declared
         * target page size are handled specially, so for those we
         * pass in the exact addresses.
         */
        if (res.f.lg_page_size >= TARGET_PAGE_BITS) {
            res.f.phys_addr &= TARGET_PAGE_MASK;
            address &= TARGET_PAGE_MASK;
        }

        res.f.pte_attrs = res.cacheattrs.attrs;
        res.f.shareability = res.cacheattrs.shareability;

        tlb_set_page_full(cs, mmu_idx, address, &res.f);
        return true;
    } else if (probe) {
        return false;
    } else {
        /* now we have a real cpu fault */
        cpu_restore_state(cs, retaddr);
        arm_deliver_fault(cpu, address, access_type, mmu_idx, fi);
    }
}
#else
void arm_cpu_record_sigsegv(CPUState *cs, vaddr addr,
                            MMUAccessType access_type,
                            bool maperr, uintptr_t ra)
{
    ARMMMUFaultInfo fi = {
        .type = maperr ? ARMFault_Translation : ARMFault_Permission,
        .level = 3,
    };
    ARMCPU *cpu = ARM_CPU(cs);

    /*
     * We report both ESR and FAR to signal handlers.
     * For now, it's easiest to deliver the fault normally.
     */
    cpu_restore_state(cs, ra);
    arm_deliver_fault(cpu, addr, access_type, MMU_USER_IDX, &fi);
}

void arm_cpu_record_sigbus(CPUState *cs, vaddr addr,
                           MMUAccessType access_type, uintptr_t ra)
{
    arm_cpu_do_unaligned_access(cs, addr, access_type, MMU_USER_IDX, ra);
}
#endif /* !defined(CONFIG_USER_ONLY) */
