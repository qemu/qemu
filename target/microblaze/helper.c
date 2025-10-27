/*
 *  MicroBlaze helper routines.
 *
 *  Copyright (c) 2009 Edgar E. Iglesias <edgar.iglesias@gmail.com>
 *  Copyright (c) 2009-2012 PetaLogix Qld Pty Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/cputlb.h"
#include "accel/tcg/cpu-mmu-index.h"
#include "exec/page-protection.h"
#include "exec/target_page.h"
#include "qemu/host-utils.h"
#include "exec/log.h"
#include "exec/helper-proto.h"
#include "qemu/plugin.h"


G_NORETURN
static void mb_unaligned_access_internal(CPUState *cs, uint64_t addr,
                                         uintptr_t retaddr)
{
    CPUMBState *env = cpu_env(cs);
    uint32_t esr, iflags;
    uint64_t last_pc = env->pc;

    /* Recover the pc and iflags from the corresponding insn_start.  */
    cpu_restore_state(cs, retaddr);
    iflags = env->iflags;

    qemu_log_mask(CPU_LOG_INT,
                  "Unaligned access addr=0x%" PRIx64 " pc=%x iflags=%x\n",
                  addr, env->pc, iflags);

    esr = ESR_EC_UNALIGNED_DATA;
    if (likely(iflags & ESR_ESS_FLAG)) {
        esr |= iflags & ESR_ESS_MASK;
    } else {
        qemu_log_mask(LOG_UNIMP, "Unaligned access without ESR_ESS_FLAG\n");
    }

    env->ear = addr;
    env->esr = esr;
    cs->exception_index = EXCP_HW_EXCP;
    qemu_plugin_vcpu_exception_cb(cs, last_pc);
    cpu_loop_exit(cs);
}

void mb_cpu_do_unaligned_access(CPUState *cs, vaddr addr,
                                MMUAccessType access_type,
                                int mmu_idx, uintptr_t retaddr)
{
    mb_unaligned_access_internal(cs, addr, retaddr);
}

#ifndef CONFIG_USER_ONLY

void HELPER(unaligned_access)(CPUMBState *env, uint64_t addr)
{
    mb_unaligned_access_internal(env_cpu(env), addr, GETPC());
}

static bool mb_cpu_access_is_secure(MicroBlazeCPU *cpu,
                                    MMUAccessType access_type)
{
    if (access_type == MMU_INST_FETCH) {
        return !cpu->ns_axi_ip;
    } else {
        return !cpu->ns_axi_dp;
    }
}

bool mb_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                     MMUAccessType access_type, int mmu_idx,
                     bool probe, uintptr_t retaddr)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(cs);
    CPUMBState *env = &cpu->env;
    MicroBlazeMMULookup lu;
    unsigned int hit;
    int prot;
    MemTxAttrs attrs = {};

    attrs.secure = mb_cpu_access_is_secure(cpu, access_type);

    if (mmu_idx == MMU_NOMMU_IDX) {
        /* MMU disabled or not available.  */
        address &= TARGET_PAGE_MASK;
        prot = PAGE_RWX;
        tlb_set_page_with_attrs(cs, address, address, attrs, prot, mmu_idx,
                                TARGET_PAGE_SIZE);
        return true;
    }

    hit = mmu_translate(cpu, &lu, address, access_type, mmu_idx);
    if (likely(hit)) {
        uint32_t vaddr = address & TARGET_PAGE_MASK;
        uint32_t paddr = lu.paddr + vaddr - lu.vaddr;

        qemu_log_mask(CPU_LOG_MMU, "MMU map mmu=%d v=%x p=%x prot=%x\n",
                      mmu_idx, vaddr, paddr, lu.prot);
        tlb_set_page_with_attrs(cs, vaddr, paddr, attrs, lu.prot, mmu_idx,
                                TARGET_PAGE_SIZE);
        return true;
    }

    /* TLB miss.  */
    if (probe) {
        return false;
    }

    qemu_log_mask(CPU_LOG_MMU, "mmu=%d miss v=%" VADDR_PRIx "\n",
                  mmu_idx, address);

    env->ear = address;
    switch (lu.err) {
    case ERR_PROT:
        env->esr = access_type == MMU_INST_FETCH ? 17 : 16;
        env->esr |= (access_type == MMU_DATA_STORE) << 10;
        break;
    case ERR_MISS:
        env->esr = access_type == MMU_INST_FETCH ? 19 : 18;
        env->esr |= (access_type == MMU_DATA_STORE) << 10;
        break;
    default:
        abort();
    }

    if (cs->exception_index == EXCP_MMU) {
        cpu_abort(cs, "recursive faults\n");
    }

    /* TLB miss.  */
    cs->exception_index = EXCP_MMU;
    cpu_loop_exit_restore(cs, retaddr);
}

void mb_cpu_do_interrupt(CPUState *cs)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(cs);
    CPUMBState *env = &cpu->env;
    uint32_t t, msr = mb_cpu_read_msr(env);
    bool set_esr;
    uint64_t last_pc = env->pc;

    /* IMM flag cannot propagate across a branch and into the dslot.  */
    assert((env->iflags & (D_FLAG | IMM_FLAG)) != (D_FLAG | IMM_FLAG));
    /* BIMM flag cannot be set without D_FLAG. */
    assert((env->iflags & (D_FLAG | BIMM_FLAG)) != BIMM_FLAG);
    /* RTI flags are private to translate. */
    assert(!(env->iflags & (DRTI_FLAG | DRTE_FLAG | DRTB_FLAG)));

    switch (cs->exception_index) {
    case EXCP_HW_EXCP:
        if (!(cpu->cfg.pvr_regs[0] & PVR0_USE_EXC_MASK)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Exception raised on system without exceptions!\n");
            return;
        }

        qemu_log_mask(CPU_LOG_INT,
                      "INT: HWE at pc=%08x msr=%08x iflags=%x\n",
                      env->pc, msr, env->iflags);

        /* Exception breaks branch + dslot sequence?  */
        set_esr = true;
        env->esr &= ~D_FLAG;
        if (env->iflags & D_FLAG) {
            env->esr |= D_FLAG;
            env->btr = env->btarget;
        }

        /* Exception in progress. */
        msr |= MSR_EIP;
        env->regs[17] = env->pc + 4;
        env->pc = cpu->cfg.base_vectors + 0x20;
        break;

    case EXCP_MMU:
        qemu_log_mask(CPU_LOG_INT,
                      "INT: MMU at pc=%08x msr=%08x "
                      "ear=%" PRIx64 " iflags=%x\n",
                      env->pc, msr, env->ear, env->iflags);

        /* Exception breaks branch + dslot sequence? */
        set_esr = true;
        env->esr &= ~D_FLAG;
        if (env->iflags & D_FLAG) {
            env->esr |= D_FLAG;
            env->btr = env->btarget;
            /* Reexecute the branch. */
            env->regs[17] = env->pc - (env->iflags & BIMM_FLAG ? 8 : 4);
        } else if (env->iflags & IMM_FLAG) {
            /* Reexecute the imm. */
            env->regs[17] = env->pc - 4;
        } else {
            env->regs[17] = env->pc;
        }

        /* Exception in progress. */
        msr |= MSR_EIP;
        env->pc = cpu->cfg.base_vectors + 0x20;
        break;

    case EXCP_IRQ:
        assert(!(msr & (MSR_EIP | MSR_BIP)));
        assert(msr & MSR_IE);
        assert(!(env->iflags & (D_FLAG | IMM_FLAG)));

        qemu_log_mask(CPU_LOG_INT,
                      "INT: DEV at pc=%08x msr=%08x iflags=%x\n",
                      env->pc, msr, env->iflags);
        set_esr = false;

        /* Disable interrupts.  */
        msr &= ~MSR_IE;
        env->regs[14] = env->pc;
        env->pc = cpu->cfg.base_vectors + 0x10;
        break;

    case EXCP_HW_BREAK:
        assert(!(env->iflags & (D_FLAG | IMM_FLAG)));

        qemu_log_mask(CPU_LOG_INT,
                      "INT: BRK at pc=%08x msr=%08x iflags=%x\n",
                      env->pc, msr, env->iflags);
        set_esr = false;

        /* Break in progress. */
        msr |= MSR_BIP;
        env->regs[16] = env->pc;
        env->pc = cpu->cfg.base_vectors + 0x18;
        break;

    default:
        cpu_abort(cs, "unhandled exception type=%d\n", cs->exception_index);
        /* not reached */
    }

    /* Save previous mode, disable mmu, disable user-mode. */
    t = (msr & (MSR_VM | MSR_UM)) << 1;
    msr &= ~(MSR_VMS | MSR_UMS | MSR_VM | MSR_UM);
    msr |= t;
    mb_cpu_write_msr(env, msr);

    env->res_addr = RES_ADDR_NONE;
    env->iflags = 0;

    if (cs->exception_index == EXCP_IRQ) {
        qemu_plugin_vcpu_interrupt_cb(cs, last_pc);
    } else {
        qemu_plugin_vcpu_exception_cb(cs, last_pc);
    }

    if (!set_esr) {
        qemu_log_mask(CPU_LOG_INT,
                      "         to pc=%08x msr=%08x\n", env->pc, msr);
    } else if (env->esr & D_FLAG) {
        qemu_log_mask(CPU_LOG_INT,
                      "         to pc=%08x msr=%08x esr=%04x btr=%08x\n",
                      env->pc, msr, env->esr, env->btr);
    } else {
        qemu_log_mask(CPU_LOG_INT,
                      "         to pc=%08x msr=%08x esr=%04x\n",
                      env->pc, msr, env->esr);
    }
}

hwaddr mb_cpu_get_phys_page_attrs_debug(CPUState *cs, vaddr addr,
                                        MemTxAttrs *attrs)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(cs);
    vaddr vaddr;
    hwaddr paddr = 0;
    MicroBlazeMMULookup lu;
    int mmu_idx = cpu_mmu_index(cs, false);
    unsigned int hit;

    /* Caller doesn't initialize */
    *attrs = (MemTxAttrs) {};
    attrs->secure = mb_cpu_access_is_secure(cpu, MMU_DATA_LOAD);

    if (mmu_idx != MMU_NOMMU_IDX) {
        hit = mmu_translate(cpu, &lu, addr, 0, 0);
        if (hit) {
            vaddr = addr & TARGET_PAGE_MASK;
            paddr = lu.paddr + vaddr - lu.vaddr;
        } else
            paddr = 0; /* ???.  */
    } else
        paddr = addr & TARGET_PAGE_MASK;

    return paddr;
}

bool mb_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    CPUMBState *env = cpu_env(cs);

    if ((interrupt_request & CPU_INTERRUPT_HARD)
        && (env->msr & MSR_IE)
        && !(env->msr & (MSR_EIP | MSR_BIP))
        && !(env->iflags & (D_FLAG | IMM_FLAG))) {
        cs->exception_index = EXCP_IRQ;
        mb_cpu_do_interrupt(cs);
        return true;
    }
    return false;
}

#endif /* !CONFIG_USER_ONLY */
