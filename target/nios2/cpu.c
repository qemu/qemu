/*
 * QEMU Nios II CPU
 *
 * Copyright (c) 2012 Chris Wulff <crwulff@gmail.com>
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
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "cpu.h"
#include "exec/log.h"
#include "gdbstub/helpers.h"
#include "hw/qdev-properties.h"

static void nios2_cpu_set_pc(CPUState *cs, vaddr value)
{
    Nios2CPU *cpu = NIOS2_CPU(cs);
    CPUNios2State *env = &cpu->env;

    env->pc = value;
}

static vaddr nios2_cpu_get_pc(CPUState *cs)
{
    Nios2CPU *cpu = NIOS2_CPU(cs);
    CPUNios2State *env = &cpu->env;

    return env->pc;
}

static void nios2_restore_state_to_opc(CPUState *cs,
                                       const TranslationBlock *tb,
                                       const uint64_t *data)
{
    Nios2CPU *cpu = NIOS2_CPU(cs);
    CPUNios2State *env = &cpu->env;

    env->pc = data[0];
}

static bool nios2_cpu_has_work(CPUState *cs)
{
    return cs->interrupt_request & CPU_INTERRUPT_HARD;
}

static void nios2_cpu_reset_hold(Object *obj)
{
    CPUState *cs = CPU(obj);
    Nios2CPU *cpu = NIOS2_CPU(cs);
    Nios2CPUClass *ncc = NIOS2_CPU_GET_CLASS(cpu);
    CPUNios2State *env = &cpu->env;

    if (ncc->parent_phases.hold) {
        ncc->parent_phases.hold(obj);
    }

    memset(env->ctrl, 0, sizeof(env->ctrl));
    env->pc = cpu->reset_addr;

#if defined(CONFIG_USER_ONLY)
    /* Start in user mode with interrupts enabled. */
    env->ctrl[CR_STATUS] = CR_STATUS_RSIE | CR_STATUS_U | CR_STATUS_PIE;
    memset(env->regs, 0, sizeof(env->regs));
#else
    env->ctrl[CR_STATUS] = CR_STATUS_RSIE;
    nios2_update_crs(env);
    memset(env->shadow_regs, 0, sizeof(env->shadow_regs));
#endif
}

#ifndef CONFIG_USER_ONLY
static void eic_set_irq(void *opaque, int irq, int level)
{
    Nios2CPU *cpu = opaque;
    CPUState *cs = CPU(cpu);

    if (level) {
        cpu_interrupt(cs, CPU_INTERRUPT_HARD);
    } else {
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
    }
}

static void iic_set_irq(void *opaque, int irq, int level)
{
    Nios2CPU *cpu = opaque;
    CPUNios2State *env = &cpu->env;
    CPUState *cs = CPU(cpu);

    env->ctrl[CR_IPENDING] = deposit32(env->ctrl[CR_IPENDING], irq, 1, !!level);

    if (env->ctrl[CR_IPENDING]) {
        cpu_interrupt(cs, CPU_INTERRUPT_HARD);
    } else {
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
    }
}
#endif

static void nios2_cpu_initfn(Object *obj)
{
    Nios2CPU *cpu = NIOS2_CPU(obj);

    cpu_set_cpustate_pointers(cpu);

#if !defined(CONFIG_USER_ONLY)
    mmu_init(&cpu->env);
#endif
}

static ObjectClass *nios2_cpu_class_by_name(const char *cpu_model)
{
    return object_class_by_name(TYPE_NIOS2_CPU);
}

static void realize_cr_status(CPUState *cs)
{
    Nios2CPU *cpu = NIOS2_CPU(cs);

    /* Begin with all fields of all registers are reserved. */
    memset(cpu->cr_state, 0, sizeof(cpu->cr_state));

    /*
     * The combination of writable and readonly is the set of all
     * non-reserved fields.  We apply writable as a mask to bits,
     * and merge in existing readonly bits, before storing.
     */
#define WR_REG(C)       cpu->cr_state[C].writable = -1
#define RO_REG(C)       cpu->cr_state[C].readonly = -1
#define WR_FIELD(C, F)  cpu->cr_state[C].writable |= R_##C##_##F##_MASK
#define RO_FIELD(C, F)  cpu->cr_state[C].readonly |= R_##C##_##F##_MASK

    WR_FIELD(CR_STATUS, PIE);
    WR_REG(CR_ESTATUS);
    WR_REG(CR_BSTATUS);
    RO_REG(CR_CPUID);
    RO_REG(CR_EXCEPTION);
    WR_REG(CR_BADADDR);

    if (cpu->eic_present) {
        WR_FIELD(CR_STATUS, RSIE);
        RO_FIELD(CR_STATUS, NMI);
        WR_FIELD(CR_STATUS, PRS);
        RO_FIELD(CR_STATUS, CRS);
        WR_FIELD(CR_STATUS, IL);
        WR_FIELD(CR_STATUS, IH);
    } else {
        RO_FIELD(CR_STATUS, RSIE);
        WR_REG(CR_IENABLE);
        RO_REG(CR_IPENDING);
    }

    if (cpu->mmu_present) {
        WR_FIELD(CR_STATUS, U);
        WR_FIELD(CR_STATUS, EH);

        WR_FIELD(CR_PTEADDR, VPN);
        WR_FIELD(CR_PTEADDR, PTBASE);

        RO_FIELD(CR_TLBMISC, D);
        RO_FIELD(CR_TLBMISC, PERM);
        RO_FIELD(CR_TLBMISC, BAD);
        RO_FIELD(CR_TLBMISC, DBL);
        WR_FIELD(CR_TLBMISC, PID);
        WR_FIELD(CR_TLBMISC, WE);
        WR_FIELD(CR_TLBMISC, RD);
        WR_FIELD(CR_TLBMISC, WAY);

        WR_REG(CR_TLBACC);
    }

    /*
     * TODO: ECC (config, eccinj) and MPU (config, mpubase, mpuacc) are
     * unimplemented, so their corresponding control regs remain reserved.
     */

#undef WR_REG
#undef RO_REG
#undef WR_FIELD
#undef RO_FIELD
}

static void nios2_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    Nios2CPU *cpu = NIOS2_CPU(cs);
    Nios2CPUClass *ncc = NIOS2_CPU_GET_CLASS(dev);
    Error *local_err = NULL;

#ifndef CONFIG_USER_ONLY
    if (cpu->eic_present) {
        qdev_init_gpio_in_named(DEVICE(cpu), eic_set_irq, "EIC", 1);
    } else {
        qdev_init_gpio_in_named(DEVICE(cpu), iic_set_irq, "IRQ", 32);
    }
#endif

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    realize_cr_status(cs);
    qemu_init_vcpu(cs);
    cpu_reset(cs);

    /* We have reserved storage for cpuid; might as well use it. */
    cpu->env.ctrl[CR_CPUID] = cs->cpu_index;

    ncc->parent_realize(dev, errp);
}

#ifndef CONFIG_USER_ONLY
static bool eic_take_interrupt(Nios2CPU *cpu)
{
    CPUNios2State *env = &cpu->env;
    const uint32_t status = env->ctrl[CR_STATUS];

    if (cpu->rnmi) {
        return !(status & CR_STATUS_NMI);
    }
    if (!(status & CR_STATUS_PIE)) {
        return false;
    }
    if (cpu->ril <= FIELD_EX32(status, CR_STATUS, IL)) {
        return false;
    }
    if (cpu->rrs != FIELD_EX32(status, CR_STATUS, CRS)) {
        return true;
    }
    return status & CR_STATUS_RSIE;
}

static bool iic_take_interrupt(Nios2CPU *cpu)
{
    CPUNios2State *env = &cpu->env;

    if (!(env->ctrl[CR_STATUS] & CR_STATUS_PIE)) {
        return false;
    }
    return env->ctrl[CR_IPENDING] & env->ctrl[CR_IENABLE];
}

static bool nios2_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    Nios2CPU *cpu = NIOS2_CPU(cs);

    if (interrupt_request & CPU_INTERRUPT_HARD) {
        if (cpu->eic_present
            ? eic_take_interrupt(cpu)
            : iic_take_interrupt(cpu)) {
            cs->exception_index = EXCP_IRQ;
            nios2_cpu_do_interrupt(cs);
            return true;
        }
    }
    return false;
}
#endif /* !CONFIG_USER_ONLY */

static void nios2_cpu_disas_set_info(CPUState *cpu, disassemble_info *info)
{
    /* NOTE: NiosII R2 is not supported yet. */
    info->mach = bfd_arch_nios2;
    info->print_insn = print_insn_nios2;
}

static int nios2_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    Nios2CPU *cpu = NIOS2_CPU(cs);
    CPUNios2State *env = &cpu->env;
    uint32_t val;

    if (n < 32) {          /* GP regs */
        val = env->regs[n];
    } else if (n == 32) {    /* PC */
        val = env->pc;
    } else if (n < 49) {     /* Status regs */
        unsigned cr = n - 33;
        if (nios2_cr_reserved(&cpu->cr_state[cr])) {
            val = 0;
        } else {
            val = env->ctrl[n - 33];
        }
    } else {
        /* Invalid regs */
        return 0;
    }

    return gdb_get_reg32(mem_buf, val);
}

static int nios2_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    Nios2CPU *cpu = NIOS2_CPU(cs);
    CPUClass *cc = CPU_GET_CLASS(cs);
    CPUNios2State *env = &cpu->env;
    uint32_t val;

    if (n > cc->gdb_num_core_regs) {
        return 0;
    }
    val = ldl_p(mem_buf);

    if (n < 32) {            /* GP regs */
        env->regs[n] = val;
    } else if (n == 32) {    /* PC */
        env->pc = val;
    } else if (n < 49) {     /* Status regs */
        unsigned cr = n - 33;
        /* ??? Maybe allow the debugger to write to readonly fields. */
        val &= cpu->cr_state[cr].writable;
        val |= cpu->cr_state[cr].readonly & env->ctrl[cr];
        env->ctrl[cr] = val;
    } else {
        g_assert_not_reached();
    }

    return 4;
}

static Property nios2_properties[] = {
    DEFINE_PROP_BOOL("diverr_present", Nios2CPU, diverr_present, true),
    DEFINE_PROP_BOOL("mmu_present", Nios2CPU, mmu_present, true),
    /* ALTR,pid-num-bits */
    DEFINE_PROP_UINT32("mmu_pid_num_bits", Nios2CPU, pid_num_bits, 8),
    /* ALTR,tlb-num-ways */
    DEFINE_PROP_UINT32("mmu_tlb_num_ways", Nios2CPU, tlb_num_ways, 16),
    /* ALTR,tlb-num-entries */
    DEFINE_PROP_UINT32("mmu_pid_num_entries", Nios2CPU, tlb_num_entries, 256),
    DEFINE_PROP_END_OF_LIST(),
};

#ifndef CONFIG_USER_ONLY
#include "hw/core/sysemu-cpu-ops.h"

static const struct SysemuCPUOps nios2_sysemu_ops = {
    .get_phys_page_debug = nios2_cpu_get_phys_page_debug,
};
#endif

#include "hw/core/tcg-cpu-ops.h"

static const struct TCGCPUOps nios2_tcg_ops = {
    .initialize = nios2_tcg_init,
    .restore_state_to_opc = nios2_restore_state_to_opc,

#ifndef CONFIG_USER_ONLY
    .tlb_fill = nios2_cpu_tlb_fill,
    .cpu_exec_interrupt = nios2_cpu_exec_interrupt,
    .do_interrupt = nios2_cpu_do_interrupt,
    .do_unaligned_access = nios2_cpu_do_unaligned_access,
#endif /* !CONFIG_USER_ONLY */
};

static void nios2_cpu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    Nios2CPUClass *ncc = NIOS2_CPU_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    device_class_set_parent_realize(dc, nios2_cpu_realizefn,
                                    &ncc->parent_realize);
    device_class_set_props(dc, nios2_properties);
    resettable_class_set_parent_phases(rc, NULL, nios2_cpu_reset_hold, NULL,
                                       &ncc->parent_phases);

    cc->class_by_name = nios2_cpu_class_by_name;
    cc->has_work = nios2_cpu_has_work;
    cc->dump_state = nios2_cpu_dump_state;
    cc->set_pc = nios2_cpu_set_pc;
    cc->get_pc = nios2_cpu_get_pc;
    cc->disas_set_info = nios2_cpu_disas_set_info;
#ifndef CONFIG_USER_ONLY
    cc->sysemu_ops = &nios2_sysemu_ops;
#endif
    cc->gdb_read_register = nios2_cpu_gdb_read_register;
    cc->gdb_write_register = nios2_cpu_gdb_write_register;
    cc->gdb_num_core_regs = 49;
    cc->tcg_ops = &nios2_tcg_ops;
}

static const TypeInfo nios2_cpu_type_info = {
    .name = TYPE_NIOS2_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(Nios2CPU),
    .instance_init = nios2_cpu_initfn,
    .class_size = sizeof(Nios2CPUClass),
    .class_init = nios2_cpu_class_init,
};

static void nios2_cpu_register_types(void)
{
    type_register_static(&nios2_cpu_type_info);
}

type_init(nios2_cpu_register_types)
