/*
 *  CRIS helper routines.
 *
 *  Copyright (c) 2007 AXIS Communications AB
 *  Written by Edgar E. Iglesias.
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
#include "mmu.h"
#include "qemu/host-utils.h"
#include "exec/cpu_ldst.h"


//#define CRIS_HELPER_DEBUG


#ifdef CRIS_HELPER_DEBUG
#define D(x) x
#define D_LOG(...) qemu_log(__VA_ARGS__)
#else
#define D(x)
#define D_LOG(...) do { } while (0)
#endif

#if defined(CONFIG_USER_ONLY)

void cris_cpu_do_interrupt(CPUState *cs)
{
    CRISCPU *cpu = CRIS_CPU(cs);
    CPUCRISState *env = &cpu->env;

    cs->exception_index = -1;
    env->pregs[PR_ERP] = env->pc;
}

void crisv10_cpu_do_interrupt(CPUState *cs)
{
    cris_cpu_do_interrupt(cs);
}

int cris_cpu_handle_mmu_fault(CPUState *cs, vaddr address, int rw,
                              int mmu_idx)
{
    CRISCPU *cpu = CRIS_CPU(cs);

    cs->exception_index = 0xaa;
    cpu->env.pregs[PR_EDA] = address;
    cpu_dump_state(cs, stderr, fprintf, 0);
    return 1;
}

#else /* !CONFIG_USER_ONLY */


static void cris_shift_ccs(CPUCRISState *env)
{
    uint32_t ccs;
    /* Apply the ccs shift.  */
    ccs = env->pregs[PR_CCS];
    ccs = ((ccs & 0xc0000000) | ((ccs << 12) >> 2)) & ~0x3ff;
    env->pregs[PR_CCS] = ccs;
}

int cris_cpu_handle_mmu_fault(CPUState *cs, vaddr address, int rw,
                              int mmu_idx)
{
    CRISCPU *cpu = CRIS_CPU(cs);
    CPUCRISState *env = &cpu->env;
    struct cris_mmu_result res;
    int prot, miss;
    int r = -1;
    target_ulong phy;

    qemu_log_mask(CPU_LOG_MMU, "%s addr=%" VADDR_PRIx " pc=%x rw=%x\n",
            __func__, address, env->pc, rw);
    miss = cris_mmu_translate(&res, env, address & TARGET_PAGE_MASK,
                              rw, mmu_idx, 0);
    if (miss) {
        if (cs->exception_index == EXCP_BUSFAULT) {
            cpu_abort(cs,
                      "CRIS: Illegal recursive bus fault."
                      "addr=%" VADDR_PRIx " rw=%d\n",
                      address, rw);
        }

        env->pregs[PR_EDA] = address;
        cs->exception_index = EXCP_BUSFAULT;
        env->fault_vector = res.bf_vec;
        r = 1;
    } else {
        /*
         * Mask off the cache selection bit. The ETRAX busses do not
         * see the top bit.
         */
        phy = res.phy & ~0x80000000;
        prot = res.prot;
        tlb_set_page(cs, address & TARGET_PAGE_MASK, phy,
                     prot, mmu_idx, TARGET_PAGE_SIZE);
        r = 0;
    }
    if (r > 0) {
        qemu_log_mask(CPU_LOG_MMU,
                "%s returns %d irqreq=%x addr=%" VADDR_PRIx " phy=%x vec=%x"
                " pc=%x\n", __func__, r, cs->interrupt_request, address,
                res.phy, res.bf_vec, env->pc);
    }
    return r;
}

void crisv10_cpu_do_interrupt(CPUState *cs)
{
    CRISCPU *cpu = CRIS_CPU(cs);
    CPUCRISState *env = &cpu->env;
    int ex_vec = -1;

    D_LOG("exception index=%d interrupt_req=%d\n",
          cs->exception_index,
          cs->interrupt_request);

    if (env->dslot) {
        /* CRISv10 never takes interrupts while in a delay-slot.  */
        cpu_abort(cs, "CRIS: Interrupt on delay-slot\n");
    }

    assert(!(env->pregs[PR_CCS] & PFIX_FLAG));
    switch (cs->exception_index) {
    case EXCP_BREAK:
        /* These exceptions are genereated by the core itself.
           ERP should point to the insn following the brk.  */
        ex_vec = env->trap_vector;
        env->pregs[PRV10_BRP] = env->pc;
        break;

    case EXCP_NMI:
        /* NMI is hardwired to vector zero.  */
        ex_vec = 0;
        env->pregs[PR_CCS] &= ~M_FLAG_V10;
        env->pregs[PRV10_BRP] = env->pc;
        break;

    case EXCP_BUSFAULT:
        cpu_abort(cs, "Unhandled busfault");
        break;

    default:
        /* The interrupt controller gives us the vector.  */
        ex_vec = env->interrupt_vector;
        /* Normal interrupts are taken between
           TB's.  env->pc is valid here.  */
        env->pregs[PR_ERP] = env->pc;
        break;
    }

    if (env->pregs[PR_CCS] & U_FLAG) {
        /* Swap stack pointers.  */
        env->pregs[PR_USP] = env->regs[R_SP];
        env->regs[R_SP] = env->ksp;
    }

    /* Now that we are in kernel mode, load the handlers address.  */
    env->pc = cpu_ldl_code(env, env->pregs[PR_EBP] + ex_vec * 4);
    env->locked_irq = 1;
    env->pregs[PR_CCS] |= F_FLAG_V10; /* set F.  */

    qemu_log_mask(CPU_LOG_INT, "%s isr=%x vec=%x ccs=%x pid=%d erp=%x\n",
                  __func__, env->pc, ex_vec,
                  env->pregs[PR_CCS],
                  env->pregs[PR_PID],
                  env->pregs[PR_ERP]);
}

void cris_cpu_do_interrupt(CPUState *cs)
{
    CRISCPU *cpu = CRIS_CPU(cs);
    CPUCRISState *env = &cpu->env;
    int ex_vec = -1;

    D_LOG("exception index=%d interrupt_req=%d\n",
          cs->exception_index,
          cs->interrupt_request);

    switch (cs->exception_index) {
    case EXCP_BREAK:
        /* These exceptions are genereated by the core itself.
           ERP should point to the insn following the brk.  */
        ex_vec = env->trap_vector;
        env->pregs[PR_ERP] = env->pc;
        break;

    case EXCP_NMI:
        /* NMI is hardwired to vector zero.  */
        ex_vec = 0;
        env->pregs[PR_CCS] &= ~M_FLAG_V32;
        env->pregs[PR_NRP] = env->pc;
        break;

    case EXCP_BUSFAULT:
        ex_vec = env->fault_vector;
        env->pregs[PR_ERP] = env->pc;
        break;

    default:
        /* The interrupt controller gives us the vector.  */
        ex_vec = env->interrupt_vector;
        /* Normal interrupts are taken between
           TB's.  env->pc is valid here.  */
        env->pregs[PR_ERP] = env->pc;
        break;
    }

    /* Fill in the IDX field.  */
    env->pregs[PR_EXS] = (ex_vec & 0xff) << 8;

    if (env->dslot) {
        D_LOG("excp isr=%x PC=%x ds=%d SP=%x"
              " ERP=%x pid=%x ccs=%x cc=%d %x\n",
              ex_vec, env->pc, env->dslot,
              env->regs[R_SP],
              env->pregs[PR_ERP], env->pregs[PR_PID],
              env->pregs[PR_CCS],
              env->cc_op, env->cc_mask);
        /* We loose the btarget, btaken state here so rexec the
           branch.  */
        env->pregs[PR_ERP] -= env->dslot;
        /* Exception starts with dslot cleared.  */
        env->dslot = 0;
    }
	
    if (env->pregs[PR_CCS] & U_FLAG) {
        /* Swap stack pointers.  */
        env->pregs[PR_USP] = env->regs[R_SP];
        env->regs[R_SP] = env->ksp;
    }

    /* Apply the CRIS CCS shift. Clears U if set.  */
    cris_shift_ccs(env);

    /* Now that we are in kernel mode, load the handlers address.
       This load may not fault, real hw leaves that behaviour as
       undefined.  */
    env->pc = cpu_ldl_code(env, env->pregs[PR_EBP] + ex_vec * 4);

    /* Clear the excption_index to avoid spurios hw_aborts for recursive
       bus faults.  */
    cs->exception_index = -1;

    D_LOG("%s isr=%x vec=%x ccs=%x pid=%d erp=%x\n",
          __func__, env->pc, ex_vec,
          env->pregs[PR_CCS],
          env->pregs[PR_PID],
          env->pregs[PR_ERP]);
}

hwaddr cris_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    CRISCPU *cpu = CRIS_CPU(cs);
    uint32_t phy = addr;
    struct cris_mmu_result res;
    int miss;

    miss = cris_mmu_translate(&res, &cpu->env, addr, 0, 0, 1);
    /* If D TLB misses, try I TLB.  */
    if (miss) {
        miss = cris_mmu_translate(&res, &cpu->env, addr, 2, 0, 1);
    }

    if (!miss) {
        phy = res.phy;
    }
    D(fprintf(stderr, "%s %x -> %x\n", __func__, addr, phy));
    return phy;
}
#endif

bool cris_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    CPUClass *cc = CPU_GET_CLASS(cs);
    CRISCPU *cpu = CRIS_CPU(cs);
    CPUCRISState *env = &cpu->env;
    bool ret = false;

    if (interrupt_request & CPU_INTERRUPT_HARD
        && (env->pregs[PR_CCS] & I_FLAG)
        && !env->locked_irq) {
        cs->exception_index = EXCP_IRQ;
        cc->do_interrupt(cs);
        ret = true;
    }
    if (interrupt_request & CPU_INTERRUPT_NMI) {
        unsigned int m_flag_archval;
        if (env->pregs[PR_VR] < 32) {
            m_flag_archval = M_FLAG_V10;
        } else {
            m_flag_archval = M_FLAG_V32;
        }
        if ((env->pregs[PR_CCS] & m_flag_archval)) {
            cs->exception_index = EXCP_NMI;
            cc->do_interrupt(cs);
            ret = true;
        }
    }

    return ret;
}
