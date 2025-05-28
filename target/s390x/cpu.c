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
#include "system/kvm.h"
#include "qemu/module.h"
#include "trace.h"
#include "qapi/qapi-types-machine.h"
#include "system/hw_accel.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/resettable.h"
#include "fpu/softfloat-helpers.h"
#include "disas/capstone.h"
#include "system/tcg.h"
#ifndef CONFIG_USER_ONLY
#include "system/reset.h"
#endif
#include "hw/s390x/cpu-topology.h"

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

static void s390_query_cpu_fast(CPUState *cpu, CpuInfoFast *value)
{
    S390CPU *s390_cpu = S390_CPU(cpu);

    value->u.s390x.cpu_state = s390_cpu->env.cpu_state;
#if !defined(CONFIG_USER_ONLY)
    if (s390_has_topology()) {
        value->u.s390x.has_dedicated = true;
        value->u.s390x.dedicated = s390_cpu->env.dedicated;
        value->u.s390x.has_entitlement = true;
        value->u.s390x.entitlement = s390_cpu->env.entitlement;
    }
#endif
}

/* S390CPUClass Resettable reset_hold phase method */
static void s390_cpu_reset_hold(Object *obj, ResetType type)
{
    S390CPU *cpu = S390_CPU(obj);
    S390CPUClass *scc = S390_CPU_GET_CLASS(cpu);
    CPUS390XState *env = &cpu->env;

    if (scc->parent_phases.hold) {
        scc->parent_phases.hold(obj, type);
    }
    cpu->env.sigp_order = 0;
    s390_cpu_set_state(S390_CPU_STATE_STOPPED, cpu);

    switch (type) {
    default:
        /* RESET_TYPE_COLD: power on or "clear" reset */
        memset(env, 0, offsetof(CPUS390XState, start_initial_reset_fields));
        /* fall through */
    case RESET_TYPE_S390_CPU_INITIAL:
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
        set_float_2nan_prop_rule(float_2nan_prop_s_ab, &env->fpu_status);
        set_float_3nan_prop_rule(float_3nan_prop_s_abc, &env->fpu_status);
        set_float_infzeronan_rule(float_infzeronan_dnan_always,
                                  &env->fpu_status);
        /* Default NaN value: sign bit clear, frac msb set */
        set_float_default_nan_pattern(0b01000000, &env->fpu_status);
       /* fall through */
    case RESET_TYPE_S390_CPU_NORMAL:
        env->psw.mask &= ~PSW_MASK_RI;
        memset(&env->start_normal_reset_fields, 0,
               offsetof(CPUS390XState, end_reset_fields) -
               offsetof(CPUS390XState, start_normal_reset_fields));

        env->pfault_token = -1UL;
        env->bpbc = false;
        break;
    }

    /* Reset state inside the kernel that we cannot access yet from QEMU. */
    if (kvm_enabled()) {
        switch (type) {
        default:
            kvm_s390_reset_vcpu_clear(cpu);
            break;
        case RESET_TYPE_S390_CPU_INITIAL:
            kvm_s390_reset_vcpu_initial(cpu);
            break;
        case RESET_TYPE_S390_CPU_NORMAL:
            kvm_s390_reset_vcpu_normal(cpu);
            break;
        }
    }
}

static void s390_cpu_disas_set_info(CPUState *cpu, disassemble_info *info)
{
    info->mach = bfd_mach_s390_64;
    info->cap_arch = CS_ARCH_SYSZ;
    info->endian = BFD_ENDIAN_BIG;
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
    if (!s390_cpu_system_realize(dev, &err)) {
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

    cs->exception_index = EXCP_HLT;

#if !defined(CONFIG_USER_ONLY)
    s390_cpu_system_init(obj);
#endif
}

static const gchar *s390_gdb_arch_name(CPUState *cs)
{
    return "s390:64-bit";
}

#ifndef CONFIG_USER_ONLY
static const Property s390x_cpu_properties[] = {
    DEFINE_PROP_UINT32("core-id", S390CPU, env.core_id, 0),
    DEFINE_PROP_INT32("socket-id", S390CPU, env.socket_id, -1),
    DEFINE_PROP_INT32("book-id", S390CPU, env.book_id, -1),
    DEFINE_PROP_INT32("drawer-id", S390CPU, env.drawer_id, -1),
    DEFINE_PROP_BOOL("dedicated", S390CPU, env.dedicated, false),
    DEFINE_PROP_CPUS390ENTITLEMENT("entitlement", S390CPU, env.entitlement,
                                   S390_CPU_ENTITLEMENT_AUTO),
};
#endif

#ifdef CONFIG_TCG
#include "accel/tcg/cpu-ops.h"
#include "tcg/tcg_s390x.h"

static int s390x_cpu_mmu_index(CPUState *cs, bool ifetch)
{
    return s390x_env_mmu_index(cpu_env(cs), ifetch);
}

static TCGTBCPUState s390x_get_tb_cpu_state(CPUState *cs)
{
    CPUS390XState *env = cpu_env(cs);
    uint32_t flags;

    if (env->psw.addr & 1) {
        /*
         * Instructions must be at even addresses.
         * This needs to be checked before address translation.
         */
        env->int_pgm_ilen = 2; /* see s390_cpu_tlb_fill() */
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, 0);
    }

    flags = (env->psw.mask >> FLAG_MASK_PSW_SHIFT) & FLAG_MASK_PSW;
    if (env->psw.mask & PSW_MASK_PER) {
        flags |= env->cregs[9] & (FLAG_MASK_PER_BRANCH |
                                  FLAG_MASK_PER_IFETCH |
                                  FLAG_MASK_PER_IFETCH_NULLIFY);
        if ((env->cregs[9] & PER_CR9_EVENT_STORE) &&
            (env->cregs[9] & PER_CR9_EVENT_STORE_REAL)) {
            flags |= FLAG_MASK_PER_STORE_REAL;
        }
    }
    if (env->cregs[0] & CR0_AFP) {
        flags |= FLAG_MASK_AFP;
    }
    if (env->cregs[0] & CR0_VECTOR) {
        flags |= FLAG_MASK_VECTOR;
    }

    return (TCGTBCPUState){
        .pc = env->psw.addr,
        .flags = flags,
        .cs_base = env->ex_value,
    };
}

#ifndef CONFIG_USER_ONLY
static vaddr s390_pointer_wrap(CPUState *cs, int mmu_idx,
                               vaddr result, vaddr base)
{
    return wrap_address(cpu_env(cs), result);
}
#endif

static const TCGCPUOps s390_tcg_ops = {
    .mttcg_supported = true,
    .precise_smc = true,
    /*
     * The z/Architecture has a strong memory model with some
     * store-after-load re-ordering.
     */
    .guest_default_memory_order = TCG_MO_ALL & ~TCG_MO_ST_LD,

    .initialize = s390x_translate_init,
    .translate_code = s390x_translate_code,
    .get_tb_cpu_state = s390x_get_tb_cpu_state,
    .restore_state_to_opc = s390x_restore_state_to_opc,
    .mmu_index = s390x_cpu_mmu_index,

#ifdef CONFIG_USER_ONLY
    .record_sigsegv = s390_cpu_record_sigsegv,
    .record_sigbus = s390_cpu_record_sigbus,
#else
    .tlb_fill = s390_cpu_tlb_fill,
    .pointer_wrap = s390_pointer_wrap,
    .cpu_exec_interrupt = s390_cpu_exec_interrupt,
    .cpu_exec_halt = s390_cpu_has_work,
    .cpu_exec_reset = cpu_reset,
    .do_interrupt = s390_cpu_do_interrupt,
    .debug_excp_handler = s390x_cpu_debug_excp_handler,
    .do_unaligned_access = s390x_cpu_do_unaligned_access,
#endif /* !CONFIG_USER_ONLY */
};
#endif /* CONFIG_TCG */

static void s390_cpu_class_init(ObjectClass *oc, const void *data)
{
    S390CPUClass *scc = S390_CPU_CLASS(oc);
    CPUClass *cc = CPU_CLASS(scc);
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    device_class_set_parent_realize(dc, s390_cpu_realizefn,
                                    &scc->parent_realize);
    dc->user_creatable = true;

    resettable_class_set_parent_phases(rc, NULL, s390_cpu_reset_hold, NULL,
                                       &scc->parent_phases);

    cc->class_by_name = s390_cpu_class_by_name;
    cc->list_cpus = s390_cpu_list;
    cc->dump_state = s390_cpu_dump_state;
    cc->query_cpu_fast = s390_query_cpu_fast;
    cc->set_pc = s390_cpu_set_pc;
    cc->get_pc = s390_cpu_get_pc;
    cc->gdb_read_register = s390_cpu_gdb_read_register;
    cc->gdb_write_register = s390_cpu_gdb_write_register;
#ifndef CONFIG_USER_ONLY
    device_class_set_props(dc, s390x_cpu_properties);
    s390_cpu_system_class_init(cc);
#endif
    cc->disas_set_info = s390_cpu_disas_set_info;
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
