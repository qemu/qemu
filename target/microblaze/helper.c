/*
 *  MicroBlaze helper routines.
 *
 *  Copyright (c) 2009 Edgar E. Iglesias <edgar.iglesias@gmail.com>
 *  Copyright (c) 2009-2012 PetaLogix Qld Pty Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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
#include "exec/exec-all.h"
#include "qemu/host-utils.h"
#include "exec/log.h"

#if defined(CONFIG_USER_ONLY)

void mb_cpu_do_interrupt(CPUState *cs)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(cs);
    CPUMBState *env = &cpu->env;

    cs->exception_index = -1;
    env->res_addr = RES_ADDR_NONE;
    env->regs[14] = env->pc;
}

bool mb_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                     MMUAccessType access_type, int mmu_idx,
                     bool probe, uintptr_t retaddr)
{
    cs->exception_index = 0xaa;
    cpu_loop_exit_restore(cs, retaddr);
}

#else /* !CONFIG_USER_ONLY */

bool mb_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                     MMUAccessType access_type, int mmu_idx,
                     bool probe, uintptr_t retaddr)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(cs);
    CPUMBState *env = &cpu->env;
    struct microblaze_mmu_lookup lu;
    unsigned int hit;
    int prot;

    if (mmu_idx == MMU_NOMMU_IDX) {
        /* MMU disabled or not available.  */
        address &= TARGET_PAGE_MASK;
        prot = PAGE_BITS;
        tlb_set_page(cs, address, address, prot, mmu_idx, TARGET_PAGE_SIZE);
        return true;
    }

    hit = mmu_translate(&env->mmu, &lu, address, access_type, mmu_idx);
    if (likely(hit)) {
        uint32_t vaddr = address & TARGET_PAGE_MASK;
        uint32_t paddr = lu.paddr + vaddr - lu.vaddr;

        qemu_log_mask(CPU_LOG_MMU, "MMU map mmu=%d v=%x p=%x prot=%x\n",
                      mmu_idx, vaddr, paddr, lu.prot);
        tlb_set_page(cs, vaddr, paddr, lu.prot, mmu_idx, TARGET_PAGE_SIZE);
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

    /* IMM flag cannot propagate across a branch and into the dslot.  */
    assert(!((env->iflags & D_FLAG) && (env->iflags & IMM_FLAG)));
    assert(!(env->iflags & (DRTI_FLAG | DRTE_FLAG | DRTB_FLAG)));
    env->res_addr = RES_ADDR_NONE;
    switch (cs->exception_index) {
        case EXCP_HW_EXCP:
            if (!(env->pvr.regs[0] & PVR0_USE_EXC_MASK)) {
                qemu_log_mask(LOG_GUEST_ERROR, "Exception raised on system without exceptions!\n");
                return;
            }

            env->regs[17] = env->pc + 4;
            env->esr &= ~(1 << 12);

            /* Exception breaks branch + dslot sequence?  */
            if (env->iflags & D_FLAG) {
                env->esr |= 1 << 12 ;
                env->btr = env->btarget;
            }

            /* Disable the MMU.  */
            t = (msr & (MSR_VM | MSR_UM)) << 1;
            msr &= ~(MSR_VMS | MSR_UMS | MSR_VM | MSR_UM);
            msr |= t;
            /* Exception in progress.  */
            msr |= MSR_EIP;
            mb_cpu_write_msr(env, msr);

            qemu_log_mask(CPU_LOG_INT,
                          "hw exception at pc=%x ear=%" PRIx64 " "
                          "esr=%x iflags=%x\n",
                          env->pc, env->ear,
                          env->esr, env->iflags);
            log_cpu_state_mask(CPU_LOG_INT, cs, 0);
            env->iflags &= ~(IMM_FLAG | D_FLAG);
            env->pc = cpu->cfg.base_vectors + 0x20;
            break;

        case EXCP_MMU:
            env->regs[17] = env->pc;

            qemu_log_mask(CPU_LOG_INT,
                          "MMU exception at pc=%x iflags=%x ear=%" PRIx64 "\n",
                          env->pc, env->iflags, env->ear);

            env->esr &= ~(1 << 12);
            /* Exception breaks branch + dslot sequence?  */
            if (env->iflags & D_FLAG) {
                env->esr |= 1 << 12 ;
                env->btr = env->btarget;

                /* Reexecute the branch.  */
                env->regs[17] -= 4;
                /* was the branch immprefixed?.  */
                if (env->iflags & BIMM_FLAG) {
                    env->regs[17] -= 4;
                    log_cpu_state_mask(CPU_LOG_INT, cs, 0);
                }
            } else if (env->iflags & IMM_FLAG) {
                env->regs[17] -= 4;
            }

            /* Disable the MMU.  */
            t = (msr & (MSR_VM | MSR_UM)) << 1;
            msr &= ~(MSR_VMS | MSR_UMS | MSR_VM | MSR_UM);
            msr |= t;
            /* Exception in progress.  */
            msr |= MSR_EIP;
            mb_cpu_write_msr(env, msr);

            qemu_log_mask(CPU_LOG_INT,
                          "exception at pc=%x ear=%" PRIx64 " iflags=%x\n",
                          env->pc, env->ear, env->iflags);
            log_cpu_state_mask(CPU_LOG_INT, cs, 0);
            env->iflags &= ~(IMM_FLAG | D_FLAG);
            env->pc = cpu->cfg.base_vectors + 0x20;
            break;

        case EXCP_IRQ:
            assert(!(msr & (MSR_EIP | MSR_BIP)));
            assert(msr & MSR_IE);
            assert(!(env->iflags & D_FLAG));

            t = (msr & (MSR_VM | MSR_UM)) << 1;

#if 0
#include "disas/disas.h"

/* Useful instrumentation when debugging interrupt issues in either
   the models or in sw.  */
            {
                const char *sym;

                sym = lookup_symbol(env->pc);
                if (sym
                    && (!strcmp("netif_rx", sym)
                        || !strcmp("process_backlog", sym))) {

                    qemu_log("interrupt at pc=%x msr=%x %x iflags=%x sym=%s\n",
                             env->pc, msr, t, env->iflags, sym);

                    log_cpu_state(cs, 0);
                }
            }
#endif
            qemu_log_mask(CPU_LOG_INT,
                          "interrupt at pc=%x msr=%x %x iflags=%x\n",
                          env->pc, msr, t, env->iflags);

            msr &= ~(MSR_VMS | MSR_UMS | MSR_VM | MSR_UM | MSR_IE);
            msr |= t;
            mb_cpu_write_msr(env, msr);

            env->regs[14] = env->pc;
            env->pc = cpu->cfg.base_vectors + 0x10;
            //log_cpu_state_mask(CPU_LOG_INT, cs, 0);
            break;

        case EXCP_HW_BREAK:
            assert(!(env->iflags & IMM_FLAG));
            assert(!(env->iflags & D_FLAG));
            t = (msr & (MSR_VM | MSR_UM)) << 1;
            qemu_log_mask(CPU_LOG_INT,
                          "break at pc=%x msr=%x %x iflags=%x\n",
                          env->pc, msr, t, env->iflags);
            log_cpu_state_mask(CPU_LOG_INT, cs, 0);
            msr &= ~(MSR_VMS | MSR_UMS | MSR_VM | MSR_UM);
            msr |= t;
            msr |= MSR_BIP;
            env->regs[16] = env->pc;
            env->pc = cpu->cfg.base_vectors + 0x18;
            mb_cpu_write_msr(env, msr);
            break;
        default:
            cpu_abort(cs, "unhandled exception type=%d\n",
                      cs->exception_index);
            break;
    }
}

hwaddr mb_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(cs);
    CPUMBState *env = &cpu->env;
    target_ulong vaddr, paddr = 0;
    struct microblaze_mmu_lookup lu;
    int mmu_idx = cpu_mmu_index(env, false);
    unsigned int hit;

    if (mmu_idx != MMU_NOMMU_IDX) {
        hit = mmu_translate(&env->mmu, &lu, addr, 0, 0);
        if (hit) {
            vaddr = addr & TARGET_PAGE_MASK;
            paddr = lu.paddr + vaddr - lu.vaddr;
        } else
            paddr = 0; /* ???.  */
    } else
        paddr = addr & TARGET_PAGE_MASK;

    return paddr;
}
#endif

bool mb_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(cs);
    CPUMBState *env = &cpu->env;

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

void mb_cpu_do_unaligned_access(CPUState *cs, vaddr addr,
                                MMUAccessType access_type,
                                int mmu_idx, uintptr_t retaddr)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(cs);
    uint32_t esr, iflags;

    /* Recover the pc and iflags from the corresponding insn_start.  */
    cpu_restore_state(cs, retaddr, true);
    iflags = cpu->env.iflags;

    qemu_log_mask(CPU_LOG_INT,
                  "Unaligned access addr=" TARGET_FMT_lx
                  " pc=%x iflags=%x\n", addr, cpu->env.pc, iflags);

    esr = ESR_EC_UNALIGNED_DATA;
    if (likely(iflags & ESR_ESS_FLAG)) {
        esr |= iflags & ESR_ESS_MASK;
    } else {
        qemu_log_mask(LOG_UNIMP, "Unaligned access without ESR_ESS_FLAG\n");
    }

    cpu->env.ear = addr;
    cpu->env.esr = esr;
    cs->exception_index = EXCP_HW_EXCP;
    cpu_loop_exit(cs);
}
