/*
 * CSKY helper
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
#include "translate.h"
#include "exec/gdbstub.h"
#include "exec/cpu_ldst.h"
#include "exec/helper-proto.h"
#include "qemu-common.h"
#include "qemu/host-utils.h"
#include "exec/exec-all.h"
#if !defined(CONFIG_USER_ONLY)
#include "hw/loader.h"
#endif

/* Sort alphabetically, except for "any". */
static gint csky_cpu_list_compare(gconstpointer a, gconstpointer b)
{
    ObjectClass *class_a = (ObjectClass *)a;
    ObjectClass *class_b = (ObjectClass *)b;
    const char *name_a, *name_b;

    name_a = object_class_get_name(class_a);
    name_b = object_class_get_name(class_b);
    if (strcmp(name_a, "any-" TYPE_CSKY_CPU) == 0) {
        return 1;
    } else if (strcmp(name_b, "any-" TYPE_CSKY_CPU) == 0) {
        return -1;
    } else {
        return strcasecmp(name_a, name_b);
    }
}

static void csky_cpu_list_entry(gpointer data, gpointer user_data)
{
    ObjectClass *c = data;
    CPUListState *s = user_data;
    const char *typename;
    char *name;

    typename = object_class_get_name(c);
    name = g_strndup(typename, strlen(typename) - strlen("-" TYPE_CSKY_CPU));
    (*s->cpu_fprintf)(s->file, "%s\n", name);
    g_free(name);
}

void csky_cpu_list(FILE *f, fprintf_function cpu_fprintf)
{
    CPUListState s = {
        .file = f,
        .cpu_fprintf = cpu_fprintf,
    };
    GSList *list;

    list = object_class_get_list(TYPE_CSKY_CPU, false);
    list = g_slist_sort(list, csky_cpu_list_compare);
    (*cpu_fprintf)(f, "Available CPUs:\n");
    g_slist_foreach(list, csky_cpu_list_entry, &s);
    g_slist_free(list);
}

#if !defined(CONFIG_USER_ONLY)
void csky_nommu_init(CPUCSKYState *env)
{
    env->tlb_context = g_malloc0(sizeof(CPUCSKYTLBContext));
    if (env->features & ABIV2_TEE) {
        env->tlb_context->tlb = env->tlb_context->t_tlb;
        env->tlb_context->round_robin = env->tlb_context->t_round_robin;
    } else {
        env->tlb_context->tlb = env->tlb_context->nt_tlb;
        env->tlb_context->round_robin = env->tlb_context->nt_round_robin;
    }

    /* mmu_get_physical_address */
    env->tlb_context->get_physical_address = nommu_get_physical_address;
    env->tlb_context->helper_tlbp = csky_tlbp;
    env->tlb_context->helper_tlbwi = csky_tlbwi;
    env->tlb_context->helper_tlbwr = csky_tlbwr;
    env->tlb_context->helper_tlbr = csky_tlbr;

}
#endif

CSKYCPU *cpu_csky_init(const char *cpu_model)
{
    return CSKY_CPU(cpu_generic_init(TYPE_CSKY_CPU, cpu_model));
}

void csky_cpu_do_unaligned_access(CPUState *cs, vaddr vaddr,
                                  MMUAccessType access_type,
                                  int mmu_idx, uintptr_t retaddr)
{
    CSKYCPU *cpu = CSKY_CPU(cs);
    CPUCSKYState *env = &cpu->env;

    if ((env->features & UNALIGNED_ACCESS) == 0) {
        cpu_restore_state(cs, retaddr);
        helper_exception(env, EXCP_CSKY_ALIGN);
    }
}

#if defined(CONFIG_USER_ONLY)

/* the meaning of return value
 *  < 0    not a MMU fault
 * == 0    the MMU fault was handled without causing real CPU fault
 *  > 0    a real cpu fault
 */
int csky_cpu_handle_mmu_fault(CPUState *cs, vaddr address,
                              int rw, int mmu_idx)
{
    /* ??? which excep should we return */
    cs->exception_index = EXCP_CSKY_DATA_ABORT;

    return 1;
}

/* for user mode */
void csky_cpu_do_interrupt(CPUState *cs)
{
    cs->exception_index = -1;
}

#else /* !defined(CONFIG_USER_ONLY) */

static inline int cskycpu_need_respond_interrupts(CPUCSKYState *env)
{
    if (env->idly4_counter != 0) {
        return 0;
    }

    if (env->intc_signals.fint_b && PSR_FE(env->cp0.psr)) {
        return 1;
    }

    if (env->intc_signals.int_b && PSR_IE(env->cp0.psr)) {
        return 1;
    }

    return 0;
}

static inline int cskycpu_excp_from_sig(CPUCSKYState *env)
{
    if (env->intc_signals.avec_b) {
        if (env->intc_signals.fint_b) {
            return EXCP_CSKY_FIQ;
        } else {
            return EXCP_CSKY_IRQ;
        }
    } else {
        return env->intc_signals.vec_b;
    }
}

#if defined(TARGET_CSKYV2)
/* For system mode */
/* For tee interrupts */
static inline void do_helper_tee_interrupt(CPUState *cs, CPUCSKYState *env)
{
    helper_tee_save_cr(env);
    if (env->psr_t &&
        !(env->intc_signals.issr
          & (1 << (cs->exception_index - 32)))) {
        /* Trust world switch to Non-Trust world */
        cpu_stl_data(env, env->stackpoint.t_ssp - 4, env->pc);
        cpu_stl_data(env, env->stackpoint.t_ssp - 8, env->cp0.psr);
        env->stackpoint.t_ssp -= 8;
        /* save GPRs to trust-supervised stack. */
        helper_tee_save_gpr(env);
        env->tee.t_psr |= PSR_HS_MASK;
        env->tee.nt_epsr = env->tee.nt_psr;
        env->tee.nt_psr |= PSR_SP_MASK;
        env->tee.nt_psr |= PSR_S_MASK;
        env->cp0.psr = env->tee.nt_psr;
    } else if (!env->psr_t &&
               (env->intc_signals.issr
                & (1 << (cs->exception_index - 32)))) {
        /* Non-Trust world switch to Trust world */
        cpu_stl_data(env, env->stackpoint.nt_ssp - 4, env->pc);
        cpu_stl_data(env, env->stackpoint.nt_ssp - 8, env->cp0.psr);
        env->stackpoint.nt_ssp -= 8;
        env->tee.t_epsr = env->tee.t_psr;
        env->tee.t_psr &= ~PSR_SP_MASK;
        env->tee.t_psr &= ~PSR_SC_MASK;
        env->tee.t_psr |= PSR_S_MASK;
        env->cp0.psr = env->tee.t_psr;
    } else {
        env->cp0.epc = env->pc;
        env->cp0.epsr = env->cp0.psr;
    }
    helper_record_psr_bits(env);
    helper_tee_choose_cr(env);
}

/* Interface for interrupts and exceptions */
void csky_cpu_do_interrupt(CPUState *cs)
{
    CSKYCPU *cpu = CSKY_CPU(cs);
    CPUCSKYState *env = &cpu->env;
    int af_bk;

    if ((cs->exception_index == EXCP_CSKY_TRACE)
            && cs->interrupt_request
            && cskycpu_need_respond_interrupts(env)) {
        env->cp0.psr |= PSR_TP_MASK;
        return;
    }

    if (unlikely(env->idly4_counter != 0)) {
        /* Set the psr_c */
        env->psr_c = 1;
    }

    /* FIXME  backup the sce_cond_bit to psr[cpidfiels] in cskyv2*/
    helper_save_sp(env);
    helper_update_psr(env);
    af_bk = (env->cp0.psr & 0x2) >> 1;
    if (unlikely(env->intc_signals.fint_b
            && (cs->exception_index == EXCP_CSKY_FIQ
                || cs->exception_index > EXCP_CSKY_CPU_END))) {
        env->cp0.fpc = env->pc;
        env->cp0.fpsr = env->cp0.psr;
        /* clear FE bit */
        env->cp0.psr &= ~PSR_FE_MASK;
    } else if ((env->cp0.psr & PSR_EE_MASK) || env->intc_signals.int_b) {
        if (env->features & ABIV2_TEE && (cs->exception_index >= 32)) {
            /* tee interrupt */
            do_helper_tee_interrupt(cs, env);
        } else {
            /* interrupt for cpu without tee, or exceptions. */
            env->cp0.epc = env->pc;
            env->cp0.epsr = env->cp0.psr;
        }
    } else {
        cs->exception_index = EXCP_CSKY_URESTORE;
    }

    /* Set the vec in the psr */
    env->cp0.psr &= ~(0xff << 16);
    env->cp0.psr |= cs->exception_index << 16;
    env->cp0.psr |= PSR_S_MASK;
    env->cp0.psr &= ~PSR_TP_MASK;
    env->cp0.psr &= ~PSR_EE_MASK;
    env->cp0.psr &= ~PSR_IE_MASK;
    env->cp0.psr &= ~PSR_TM_MASK;
    helper_record_psr_bits(env);
    env->pc = cpu_ldl_code(env, env->cp0.vbr + cs->exception_index * 4);

    /* check the AF and pc. */
    if (unlikely((env->pc & 0x1) != af_bk)) {
        if (env->features & (CPU_807 | CPU_810)) {
            qemu_log_mask(CPU_LOG_EXEC,
                          "11.epc:%x:env->regs[2] = 0x%x:%x:%x:%x\n",
                          env->cp0.epc, env->regs[2], env->banked_regs[2],
                          env->cp0.psr, env->cp0.epsr);
            helper_switch_regs(env);
            env->cp0.psr &= ~0x1;
            env->cp0.psr |= (env->pc & 0x1) << 1;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Only CK610 CK807 CK810 have alternative registers");
        }
    }

    helper_choose_sp(env);
    env->pc &= ~0x1;
    env->sce_condexec_bits_bk = env->sce_condexec_bits;
    env->sce_condexec_bits = 1;
    env->intc_signals.vec_b = 0;
    env->intc_signals.avec_b = 0;
    env->intc_signals.int_b = 0;
    env->intc_signals.fint_b = 0;
    cs->exception_index = -1;
}
#else
/* For system mode */
void csky_cpu_do_interrupt(CPUState *cs)
{
    CSKYCPU *cpu = CSKY_CPU(cs);
    CPUCSKYState *env = &cpu->env;

    if ((cs->exception_index == EXCP_CSKY_TRACE)
            && cs->interrupt_request
            && cskycpu_need_respond_interrupts(env)) {
        env->cp0.psr |= PSR_TP_MASK;
        return;
    }

    if (unlikely(env->idly4_counter != 0)) {
        /* Set the psr_c */
        env->psr_c = 1;
    }

    /* Set the vec in the psr */
    env->cp0.psr &= ~(0x7f << 16);
    env->cp0.psr |= cs->exception_index << 16;

    /* FIXME  backup the sce_cond_bit to psr[cpidfiels] in cskyv2*/
    helper_update_psr(env);
    if (unlikely(env->intc_signals.fint_b
            && (cs->exception_index == EXCP_CSKY_FIQ
                || cs->exception_index > EXCP_CSKY_CPU_END))) {
        env->cp0.fpc = env->pc;
        env->cp0.fpsr = env->cp0.psr;
        /* clear FE bit */
        env->cp0.psr &= ~PSR_FE_MASK;
    } else if ((env->cp0.psr & PSR_EE_MASK) || env->intc_signals.int_b) {
        env->cp0.epc = env->pc;
        env->cp0.epsr = env->cp0.psr;
    } else {
        cs->exception_index = EXCP_CSKY_URESTORE;
    }

    env->psr_s = 1;
    env->psr_tm = 0;
    env->cp0.psr &= ~PSR_TP_MASK;
    env->cp0.psr &= ~PSR_EE_MASK;
    env->cp0.psr &= ~PSR_IE_MASK;

    env->pc = cpu_ldl_code(env, env->cp0.vbr + cs->exception_index * 4);
    if (unlikely((env->pc & 0x1) != ((env->cp0.psr & 0x2) >> 1))) {
        if (env->features & CPU_610) {
            qemu_log_mask(CPU_LOG_EXEC,
                          "11.epc:%x:env->regs[2] = 0x%x:%x:%x:%x\n",
                          env->cp0.epc, env->regs[2], env->banked_regs[2],
                          env->cp0.psr, env->cp0.epsr);
            helper_switch_regs(env);
            env->cp0.psr |= (env->pc & 0x1) << 1;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Only CK610 CK807 CK810 have alternative registers");
        }
    }

    env->pc &= ~0x1;

    cs->exception_index = -1;

}
#endif

hwaddr csky_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    CSKYCPU *cpu = CSKY_CPU(cs);
    CPUCSKYState *env = &cpu->env;

    hwaddr phys_addr;
    int prot;

    if (env->tlb_context->get_physical_address(env, &phys_addr,
                                               &prot, addr, 0) != 0) {
        return -1;
    }
    return phys_addr;
}

#endif

bool csky_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    CPUClass *cc = CPU_GET_CLASS(cs);
    CSKYCPU *cpu = CSKY_CPU(cs);
    CPUCSKYState *env = &cpu->env;
    bool next_tb = false;

#if defined(CONFIG_USER_ONLY)
    if (interrupt_request & CPU_INTERRUPT_FIQ
        && !(PSR_FE(env->cp0.psr))) {
        cs->exception_index = EXCP_CSKY_FIQ;
        cc->do_interrupt(cs);
        next_tb = true;
    }

    if (interrupt_request & CPU_INTERRUPT_HARD
        && (!(PSR_IE(env->cp0.psr)))) {
        cs->exception_index = EXCP_CSKY_IRQ;
        cc->do_interrupt(cs);
        next_tb = true;
    }
#else
    if ((interrupt_request & CPU_INTERRUPT_HARD)
        && cskycpu_need_respond_interrupts(env)) {
        cs->exception_index = cskycpu_excp_from_sig(env);
        cc->do_interrupt(cs);
        next_tb = true;
    }
#endif
    return next_tb;
}

