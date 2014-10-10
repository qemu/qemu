/*
 * QEMU S/390 CPU
 *
 * Copyright (c) 2009 Ulrich Hecht
 * Copyright (c) 2011 Alexander Graf
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 * Copyright (c) 2012 IBM Corp.
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
 * Contributions after 2012-12-11 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "cpu.h"
#include "qemu-common.h"
#include "qemu/timer.h"
#include "qemu/error-report.h"
#include "hw/hw.h"
#include "trace.h"
#ifndef CONFIG_USER_ONLY
#include "sysemu/arch_init.h"
#endif

#define CR0_RESET       0xE0UL
#define CR14_RESET      0xC2000000UL;

/* generate CPU information for cpu -? */
void s390_cpu_list(FILE *f, fprintf_function cpu_fprintf)
{
#ifdef CONFIG_KVM
    (*cpu_fprintf)(f, "s390 %16s\n", "host");
#endif
}

#ifndef CONFIG_USER_ONLY
CpuDefinitionInfoList *arch_query_cpu_definitions(Error **errp)
{
    CpuDefinitionInfoList *entry;
    CpuDefinitionInfo *info;

    info = g_malloc0(sizeof(*info));
    info->name = g_strdup("host");

    entry = g_malloc0(sizeof(*entry));
    entry->value = info;

    return entry;
}
#endif

static void s390_cpu_set_pc(CPUState *cs, vaddr value)
{
    S390CPU *cpu = S390_CPU(cs);

    cpu->env.psw.addr = value;
}

static bool s390_cpu_has_work(CPUState *cs)
{
    S390CPU *cpu = S390_CPU(cs);
    CPUS390XState *env = &cpu->env;

    return (cs->interrupt_request & CPU_INTERRUPT_HARD) &&
           (env->psw.mask & PSW_MASK_EXT);
}

#if !defined(CONFIG_USER_ONLY)
/* S390CPUClass::load_normal() */
static void s390_cpu_load_normal(CPUState *s)
{
    S390CPU *cpu = S390_CPU(s);
    cpu->env.psw.addr = ldl_phys(s->as, 4) & PSW_MASK_ESA_ADDR;
    cpu->env.psw.mask = PSW_MASK_32 | PSW_MASK_64;
    s390_cpu_set_state(CPU_STATE_OPERATING, cpu);
}
#endif

/* S390CPUClass::cpu_reset() */
static void s390_cpu_reset(CPUState *s)
{
    S390CPU *cpu = S390_CPU(s);
    S390CPUClass *scc = S390_CPU_GET_CLASS(cpu);
    CPUS390XState *env = &cpu->env;

    env->pfault_token = -1UL;
    scc->parent_reset(s);
    s390_cpu_set_state(CPU_STATE_STOPPED, cpu);
    tlb_flush(s, 1);
}

/* S390CPUClass::initial_reset() */
static void s390_cpu_initial_reset(CPUState *s)
{
    S390CPU *cpu = S390_CPU(s);
    CPUS390XState *env = &cpu->env;

    s390_cpu_reset(s);
    /* initial reset does not touch regs,fregs and aregs */
    memset(&env->fpc, 0, offsetof(CPUS390XState, cpu_num) -
                         offsetof(CPUS390XState, fpc));

    /* architectured initial values for CR 0 and 14 */
    env->cregs[0] = CR0_RESET;
    env->cregs[14] = CR14_RESET;

    env->pfault_token = -1UL;

    /* Reset state inside the kernel that we cannot access yet from QEMU. */
    if (kvm_enabled()) {
        kvm_s390_reset_vcpu(cpu);
    }
}

/* CPUClass:reset() */
static void s390_cpu_full_reset(CPUState *s)
{
    S390CPU *cpu = S390_CPU(s);
    S390CPUClass *scc = S390_CPU_GET_CLASS(cpu);
    CPUS390XState *env = &cpu->env;

    scc->parent_reset(s);
    s390_cpu_set_state(CPU_STATE_STOPPED, cpu);

    memset(env, 0, offsetof(CPUS390XState, cpu_num));

    /* architectured initial values for CR 0 and 14 */
    env->cregs[0] = CR0_RESET;
    env->cregs[14] = CR14_RESET;

    env->pfault_token = -1UL;

    /* Reset state inside the kernel that we cannot access yet from QEMU. */
    if (kvm_enabled()) {
        kvm_s390_reset_vcpu(cpu);
    }
    tlb_flush(s, 1);
}

#if !defined(CONFIG_USER_ONLY)
static void s390_cpu_machine_reset_cb(void *opaque)
{
    S390CPU *cpu = opaque;

    run_on_cpu(CPU(cpu), s390_do_cpu_full_reset, CPU(cpu));
}
#endif

static void s390_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    S390CPUClass *scc = S390_CPU_GET_CLASS(dev);

    s390_cpu_gdb_init(cs);
    qemu_init_vcpu(cs);
#if !defined(CONFIG_USER_ONLY)
    run_on_cpu(cs, s390_do_cpu_full_reset, cs);
#else
    cpu_reset(cs);
#endif

    scc->parent_realize(dev, errp);
}

static void s390_cpu_initfn(Object *obj)
{
    CPUState *cs = CPU(obj);
    S390CPU *cpu = S390_CPU(obj);
    CPUS390XState *env = &cpu->env;
    static bool inited;
    static int cpu_num = 0;
#if !defined(CONFIG_USER_ONLY)
    struct tm tm;
#endif

    cs->env_ptr = env;
    cpu_exec_init(env);
#if !defined(CONFIG_USER_ONLY)
    qemu_register_reset(s390_cpu_machine_reset_cb, cpu);
    qemu_get_timedate(&tm, 0);
    env->tod_offset = TOD_UNIX_EPOCH +
                      (time2tod(mktimegm(&tm)) * 1000000000ULL);
    env->tod_basetime = 0;
    env->tod_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, s390x_tod_timer, cpu);
    env->cpu_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, s390x_cpu_timer, cpu);
    s390_cpu_set_state(CPU_STATE_STOPPED, cpu);
#endif
    env->cpu_num = cpu_num++;
    env->ext_index = -1;

    if (tcg_enabled() && !inited) {
        inited = true;
        s390x_translate_init();
    }
}

static void s390_cpu_finalize(Object *obj)
{
#if !defined(CONFIG_USER_ONLY)
    S390CPU *cpu = S390_CPU(obj);

    qemu_unregister_reset(s390_cpu_machine_reset_cb, cpu);
#endif
}

#if !defined(CONFIG_USER_ONLY)
static bool disabled_wait(CPUState *cpu)
{
    return cpu->halted && !(S390_CPU(cpu)->env.psw.mask &
                            (PSW_MASK_IO | PSW_MASK_EXT | PSW_MASK_MCHECK));
}

static unsigned s390_count_running_cpus(void)
{
    CPUState *cpu;
    int nr_running = 0;

    CPU_FOREACH(cpu) {
        uint8_t state = S390_CPU(cpu)->env.cpu_state;
        if (state == CPU_STATE_OPERATING ||
            state == CPU_STATE_LOAD) {
            if (!disabled_wait(cpu)) {
                nr_running++;
            }
        }
    }

    return nr_running;
}

unsigned int s390_cpu_halt(S390CPU *cpu)
{
    CPUState *cs = CPU(cpu);
    trace_cpu_halt(cs->cpu_index);

    if (!cs->halted) {
        cs->halted = 1;
        cs->exception_index = EXCP_HLT;
    }

    return s390_count_running_cpus();
}

void s390_cpu_unhalt(S390CPU *cpu)
{
    CPUState *cs = CPU(cpu);
    trace_cpu_unhalt(cs->cpu_index);

    if (cs->halted) {
        cs->halted = 0;
        cs->exception_index = -1;
    }
}

unsigned int s390_cpu_set_state(uint8_t cpu_state, S390CPU *cpu)
 {
    trace_cpu_set_state(CPU(cpu)->cpu_index, cpu_state);

    switch (cpu_state) {
    case CPU_STATE_STOPPED:
    case CPU_STATE_CHECK_STOP:
        /* halt the cpu for common infrastructure */
        s390_cpu_halt(cpu);
        break;
    case CPU_STATE_OPERATING:
    case CPU_STATE_LOAD:
        /* unhalt the cpu for common infrastructure */
        s390_cpu_unhalt(cpu);
        break;
    default:
        error_report("Requested CPU state is not a valid S390 CPU state: %u",
                     cpu_state);
        exit(1);
    }
    if (kvm_enabled() && cpu->env.cpu_state != cpu_state) {
        kvm_s390_set_cpu_state(cpu, cpu_state);
    }
    cpu->env.cpu_state = cpu_state;

    return s390_count_running_cpus();
}
#endif

static void s390_cpu_class_init(ObjectClass *oc, void *data)
{
    S390CPUClass *scc = S390_CPU_CLASS(oc);
    CPUClass *cc = CPU_CLASS(scc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    scc->parent_realize = dc->realize;
    dc->realize = s390_cpu_realizefn;

    scc->parent_reset = cc->reset;
#if !defined(CONFIG_USER_ONLY)
    scc->load_normal = s390_cpu_load_normal;
#endif
    scc->cpu_reset = s390_cpu_reset;
    scc->initial_cpu_reset = s390_cpu_initial_reset;
    cc->reset = s390_cpu_full_reset;
    cc->has_work = s390_cpu_has_work;
    cc->do_interrupt = s390_cpu_do_interrupt;
    cc->dump_state = s390_cpu_dump_state;
    cc->set_pc = s390_cpu_set_pc;
    cc->gdb_read_register = s390_cpu_gdb_read_register;
    cc->gdb_write_register = s390_cpu_gdb_write_register;
#ifdef CONFIG_USER_ONLY
    cc->handle_mmu_fault = s390_cpu_handle_mmu_fault;
#else
    cc->get_phys_page_debug = s390_cpu_get_phys_page_debug;
    cc->vmsd = &vmstate_s390_cpu;
    cc->write_elf64_note = s390_cpu_write_elf64_note;
    cc->write_elf64_qemunote = s390_cpu_write_elf64_qemunote;
    cc->cpu_exec_interrupt = s390_cpu_exec_interrupt;
#endif
    cc->gdb_num_core_regs = S390_NUM_CORE_REGS;
    cc->gdb_core_xml_file = "s390x-core64.xml";
}

static const TypeInfo s390_cpu_type_info = {
    .name = TYPE_S390_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(S390CPU),
    .instance_init = s390_cpu_initfn,
    .instance_finalize = s390_cpu_finalize,
    .abstract = false,
    .class_size = sizeof(S390CPUClass),
    .class_init = s390_cpu_class_init,
};

static void s390_cpu_register_types(void)
{
    type_register_static(&s390_cpu_type_info);
}

type_init(s390_cpu_register_types)
