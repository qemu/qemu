/*
 * QEMU S/390 CPU
 *
 * Copyright (c) 2009 Ulrich Hecht
 * Copyright (c) 2011 Alexander Graf
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 * Copyright (c) 2012 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "s390x-internal.h"
#include "kvm/kvm_s390x.h"
#include "sysemu/kvm.h"
#include "qemu/module.h"
#include "trace.h"
#include "qapi/qapi-types-machine.h"
#include "sysemu/hw_accel.h"
#include "hw/qdev-properties.h"
#include "fpu/softfloat-helpers.h"
#include "disas/capstone.h"
#include "sysemu/tcg.h"
#ifndef CONFIG_USER_ONLY
#include "sysemu/reset.h"
#endif

#define CR0_RESET       0xE0UL
#define CR14_RESET      0xC2000000UL;

#ifndef CONFIG_USER_ONLY
static bool is_early_exception_psw(uint64_t mask, uint64_t addr)
{
    if (mask & PSW_MASK_RESERVED) {
        return true;
    }

    switch (mask & (PSW_MASK_32 | PSW_MASK_64)) {
    case 0:
        return addr & ~0xffffffULL;
    case PSW_MASK_32:
        return addr & ~0x7fffffffULL;
    case PSW_MASK_32 | PSW_MASK_64:
        return false;
    default: /* PSW_MASK_64 */
        return true;
    }
}
#endif

void s390_cpu_set_psw(CPUS390XState *env, uint64_t mask, uint64_t addr)
{
#ifndef CONFIG_USER_ONLY
    uint64_t old_mask = env->psw.mask;
#endif

    env->psw.addr = addr;
    env->psw.mask = mask;

    /* KVM will handle all WAITs and trigger a WAIT exit on disabled_wait */
    if (!tcg_enabled()) {
        return;
    }
    env->cc_op = (mask >> 44) & 3;

#ifndef CONFIG_USER_ONLY
    if (is_early_exception_psw(mask, addr)) {
        env->int_pgm_ilen = 0;
        trigger_pgm_exception(env, PGM_SPECIFICATION);
        return;
    }

    if ((old_mask ^ mask) & PSW_MASK_PER) {
        s390_cpu_recompute_watchpoints(env_cpu(env));
    }

    if (mask & PSW_MASK_WAIT) {
        s390_handle_wait(env_archcpu(env));
    }
#endif
}

uint64_t s390_cpu_get_psw_mask(CPUS390XState *env)
{
    uint64_t r = env->psw.mask;

    if (tcg_enabled()) {
        uint64_t cc = calc_cc(env, env->cc_op, env->cc_src,
                              env->cc_dst, env->cc_vr);

        assert(cc <= 3);
        r &= ~PSW_MASK_CC;
        r |= cc << 44;
    }

    return r;
}

static void s390_cpu_set_pc(CPUState *cs, vaddr value)
{
    S390CPU *cpu = S390_CPU(cs);

    cpu->env.psw.addr = value;
}

static vaddr s390_cpu_get_pc(CPUState *cs)
{
    S390CPU *cpu = S390_CPU(cs);

    return cpu->env.psw.addr;
}

static bool s390_cpu_has_work(CPUState *cs)
{
    S390CPU *cpu = S390_CPU(cs);

    /* STOPPED cpus can never wake up */
    if (s390_cpu_get_state(cpu) != S390_CPU_STATE_LOAD &&
        s390_cpu_get_state(cpu) != S390_CPU_STATE_OPERATING) {
        return false;
    }

    if (!(cs->interrupt_request & CPU_INTERRUPT_HARD)) {
        return false;
    }

    return s390_cpu_has_int(cpu);
}

/* S390CPUClass::reset() */
static void s390_cpu_reset(CPUState *s, cpu_reset_type type)
{
    S390CPU *cpu = S390_CPU(s);
    S390CPUClass *scc = S390_CPU_GET_CLASS(cpu);
    CPUS390XState *env = &cpu->env;
    DeviceState *dev = DEVICE(s);

    scc->parent_reset(dev);
    cpu->env.sigp_order = 0;
    s390_cpu_set_state(S390_CPU_STATE_STOPPED, cpu);

    switch (type) {
    case S390_CPU_RESET_CLEAR:
        memset(env, 0, offsetof(CPUS390XState, start_initial_reset_fields));
        /* fall through */
    case S390_CPU_RESET_INITIAL:
        /* initial reset does not clear everything! */
        memset(&env->start_initial_reset_fields, 0,
               offsetof(CPUS390XState, start_normal_reset_fields) -
               offsetof(CPUS390XState, start_initial_reset_fields));

        /* architectured initial value for Breaking-Event-Address register */
        env->gbea = 1;

        /* architectured initial values for CR 0 and 14 */
        env->cregs[0] = CR0_RESET;
        env->cregs[14] = CR14_RESET;

#if defined(CONFIG_USER_ONLY)
        /* user mode should always be allowed to use the full FPU */
        env->cregs[0] |= CR0_AFP;
        if (s390_has_feat(S390_FEAT_VECTOR)) {
            env->cregs[0] |= CR0_VECTOR;
        }
#endif

        /* tininess for underflow is detected before rounding */
        set_float_detect_tininess(float_tininess_before_rounding,
                                  &env->fpu_status);
       /* fall through */
    case S390_CPU_RESET_NORMAL:
        env->psw.mask &= ~PSW_MASK_RI;
        memset(&env->start_normal_reset_fields, 0,
               offsetof(CPUS390XState, end_reset_fields) -
               offsetof(CPUS390XState, start_normal_reset_fields));

        env->pfault_token = -1UL;
        env->bpbc = false;
        break;
    default:
        g_assert_not_reached();
    }

    /* Reset state inside the kernel that we cannot access yet from QEMU. */
    if (kvm_enabled()) {
        switch (type) {
        case S390_CPU_RESET_CLEAR:
            kvm_s390_reset_vcpu_clear(cpu);
            break;
        case S390_CPU_RESET_INITIAL:
            kvm_s390_reset_vcpu_initial(cpu);
            break;
        case S390_CPU_RESET_NORMAL:
            kvm_s390_reset_vcpu_normal(cpu);
            break;
        }
    }
}

static void s390_cpu_disas_set_info(CPUState *cpu, disassemble_info *info)
{
    info->mach = bfd_mach_s390_64;
    info->cap_arch = CS_ARCH_SYSZ;
    info->cap_insn_unit = 2;
    info->cap_insn_split = 6;
}

static void s390_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    S390CPUClass *scc = S390_CPU_GET_CLASS(dev);
    Error *err = NULL;

    /* the model has to be realized before qemu_init_vcpu() due to kvm */
    s390_realize_cpu_model(cs, &err);
    if (err) {
        goto out;
    }

#if !defined(CONFIG_USER_ONLY)
    if (!s390_cpu_realize_sysemu(dev, &err)) {
        goto out;
    }
#endif

    cpu_exec_realizefn(cs, &err);
    if (err != NULL) {
        goto out;
    }

#if !defined(CONFIG_USER_ONLY)
    qemu_register_reset(s390_cpu_machine_reset_cb, S390_CPU(dev));
#endif
    s390_cpu_gdb_init(cs);
    qemu_init_vcpu(cs);

    /*
     * KVM requires the initial CPU reset ioctl to be executed on the target
     * CPU thread. CPU hotplug under single-threaded TCG will not work with
     * run_on_cpu(), as run_on_cpu() will not work properly if called while
     * the main thread is already running but the CPU hasn't been realized.
     */
    if (kvm_enabled()) {
        run_on_cpu(cs, s390_do_cpu_full_reset, RUN_ON_CPU_NULL);
    } else {
        cpu_reset(cs);
    }

    scc->parent_realize(dev, &err);
out:
    error_propagate(errp, err);
}

static void s390_cpu_initfn(Object *obj)
{
    CPUState *cs = CPU(obj);
    S390CPU *cpu = S390_CPU(obj);

    cpu_set_cpustate_pointers(cpu);
    cs->exception_index = EXCP_HLT;

#if !defined(CONFIG_USER_ONLY)
    s390_cpu_init_sysemu(obj);
#endif
}

static gchar *s390_gdb_arch_name(CPUState *cs)
{
    return g_strdup("s390:64-bit");
}

static Property s390x_cpu_properties[] = {
#if !defined(CONFIG_USER_ONLY)
    DEFINE_PROP_UINT32("core-id", S390CPU, env.core_id, 0),
#endif
    DEFINE_PROP_END_OF_LIST()
};

static void s390_cpu_reset_full(DeviceState *dev)
{
    CPUState *s = CPU(dev);
    return s390_cpu_reset(s, S390_CPU_RESET_CLEAR);
}

#ifdef CONFIG_TCG
#include "hw/core/tcg-cpu-ops.h"

static const struct TCGCPUOps s390_tcg_ops = {
    .initialize = s390x_translate_init,
    .restore_state_to_opc = s390x_restore_state_to_opc,

#ifdef CONFIG_USER_ONLY
    .record_sigsegv = s390_cpu_record_sigsegv,
    .record_sigbus = s390_cpu_record_sigbus,
#else
    .tlb_fill = s390_cpu_tlb_fill,
    .cpu_exec_interrupt = s390_cpu_exec_interrupt,
    .do_interrupt = s390_cpu_do_interrupt,
    .debug_excp_handler = s390x_cpu_debug_excp_handler,
    .do_unaligned_access = s390x_cpu_do_unaligned_access,
#endif /* !CONFIG_USER_ONLY */
};
#endif /* CONFIG_TCG */

static void s390_cpu_class_init(ObjectClass *oc, void *data)
{
    S390CPUClass *scc = S390_CPU_CLASS(oc);
    CPUClass *cc = CPU_CLASS(scc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_parent_realize(dc, s390_cpu_realizefn,
                                    &scc->parent_realize);
    device_class_set_props(dc, s390x_cpu_properties);
    dc->user_creatable = true;

    device_class_set_parent_reset(dc, s390_cpu_reset_full, &scc->parent_reset);

    scc->reset = s390_cpu_reset;
    cc->class_by_name = s390_cpu_class_by_name,
    cc->has_work = s390_cpu_has_work;
    cc->dump_state = s390_cpu_dump_state;
    cc->set_pc = s390_cpu_set_pc;
    cc->get_pc = s390_cpu_get_pc;
    cc->gdb_read_register = s390_cpu_gdb_read_register;
    cc->gdb_write_register = s390_cpu_gdb_write_register;
#ifndef CONFIG_USER_ONLY
    s390_cpu_class_init_sysemu(cc);
#endif
    cc->disas_set_info = s390_cpu_disas_set_info;
    cc->gdb_num_core_regs = S390_NUM_CORE_REGS;
    cc->gdb_core_xml_file = "s390x-core64.xml";
    cc->gdb_arch_name = s390_gdb_arch_name;

    s390_cpu_model_class_register_props(oc);

#ifdef CONFIG_TCG
    cc->tcg_ops = &s390_tcg_ops;
#endif /* CONFIG_TCG */
}

static const TypeInfo s390_cpu_type_info = {
    .name = TYPE_S390_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(S390CPU),
    .instance_align = __alignof__(S390CPU),
    .instance_init = s390_cpu_initfn,

#ifndef CONFIG_USER_ONLY
    .instance_finalize = s390_cpu_finalize,
#endif /* !CONFIG_USER_ONLY */

    .abstract = true,
    .class_size = sizeof(S390CPUClass),
    .class_init = s390_cpu_class_init,
};

static void s390_cpu_register_types(void)
{
    type_register_static(&s390_cpu_type_info);
}

type_init(s390_cpu_register_types)
