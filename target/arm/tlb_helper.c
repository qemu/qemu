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

#if !defined(CONFIG_USER_ONLY)

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
        syn = syn_data_abort_no_iss(same_el,
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
                                      false);
        /* Merge the runtime syndrome with the template syndrome.  */
        syn |= template_syn;
    }
    return syn;
}

static void QEMU_NORETURN arm_deliver_fault(ARMCPU *cpu, vaddr addr,
                                            MMUAccessType access_type,
                                            int mmu_idx, ARMMMUFaultInfo *fi)
{
    CPUARMState *env = &cpu->env;
    int target_el;
    bool same_el;
    uint32_t syn, exc, fsr, fsc;
    ARMMMUIdx arm_mmu_idx = core_to_arm_mmu_idx(env, mmu_idx);

    target_el = exception_target_el(env);
    if (fi->stage2) {
        target_el = 2;
        env->cp15.hpfar_el2 = extract64(fi->s2addr, 12, 47) << 4;
    }
    same_el = (arm_current_el(env) == target_el);

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
    cpu_restore_state(cs, retaddr, true);

    fi.type = ARMFault_Alignment;
    arm_deliver_fault(cpu, vaddr, access_type, mmu_idx, &fi);
}

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
    cpu_restore_state(cs, retaddr, true);

    fi.ea = arm_extabort_type(response);
    fi.type = ARMFault_SyncExternal;
    arm_deliver_fault(cpu, addr, access_type, mmu_idx, &fi);
}

#endif /* !defined(CONFIG_USER_ONLY) */

bool arm_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                      MMUAccessType access_type, int mmu_idx,
                      bool probe, uintptr_t retaddr)
{
    ARMCPU *cpu = ARM_CPU(cs);

#ifdef CONFIG_USER_ONLY
    cpu->env.exception.vaddress = address;
    if (access_type == MMU_INST_FETCH) {
        cs->exception_index = EXCP_PREFETCH_ABORT;
    } else {
        cs->exception_index = EXCP_DATA_ABORT;
    }
    cpu_loop_exit_restore(cs, retaddr);
#else
    hwaddr phys_addr;
    target_ulong page_size;
    int prot, ret;
    MemTxAttrs attrs = {};
    ARMMMUFaultInfo fi = {};

    /*
     * Walk the page table and (if the mapping exists) add the page
     * to the TLB.  On success, return true.  Otherwise, if probing,
     * return false.  Otherwise populate fsr with ARM DFSR/IFSR fault
     * register format, and signal the fault.
     */
    ret = get_phys_addr(&cpu->env, address, access_type,
                        core_to_arm_mmu_idx(&cpu->env, mmu_idx),
                        &phys_addr, &attrs, &prot, &page_size, &fi, NULL);
    if (likely(!ret)) {
        /*
         * Map a single [sub]page. Regions smaller than our declared
         * target page size are handled specially, so for those we
         * pass in the exact addresses.
         */
        if (page_size >= TARGET_PAGE_SIZE) {
            phys_addr &= TARGET_PAGE_MASK;
            address &= TARGET_PAGE_MASK;
        }
        tlb_set_page_with_attrs(cs, address, phys_addr, attrs,
                                prot, mmu_idx, page_size);
        return true;
    } else if (probe) {
        return false;
    } else {
        /* now we have a real cpu fault */
        cpu_restore_state(cs, retaddr, true);
        arm_deliver_fault(cpu, address, access_type, mmu_idx, &fi);
    }
#endif
}
