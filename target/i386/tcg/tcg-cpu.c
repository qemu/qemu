/*
 * i386 TCG cpu class initialization
 *
 *  Copyright (c) 2003 Fabrice Bellard
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
#include "helper-tcg.h"
#include "qemu/accel.h"
#include "hw/core/accel-cpu.h"

#include "tcg-cpu.h"

/* Frob eflags into and out of the CPU temporary format.  */

static void x86_cpu_exec_enter(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    CC_SRC = env->eflags & (CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
    env->df = 1 - (2 * ((env->eflags >> 10) & 1));
    CC_OP = CC_OP_EFLAGS;
    env->eflags &= ~(DF_MASK | CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
}

static void x86_cpu_exec_exit(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    env->eflags = cpu_compute_eflags(env);
}

static void x86_cpu_synchronize_from_tb(CPUState *cs,
                                        const TranslationBlock *tb)
{
    /* The instruction pointer is always up to date with TARGET_TB_PCREL. */
    if (!TARGET_TB_PCREL) {
        CPUX86State *env = cs->env_ptr;
        env->eip = tb_pc(tb) - tb->cs_base;
    }
}

static void x86_restore_state_to_opc(CPUState *cs,
                                     const TranslationBlock *tb,
                                     const uint64_t *data)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    int cc_op = data[1];

    if (TARGET_TB_PCREL) {
        env->eip = (env->eip & TARGET_PAGE_MASK) | data[0];
    } else {
        env->eip = data[0] - tb->cs_base;
    }
    if (cc_op != CC_OP_DYNAMIC) {
        env->cc_op = cc_op;
    }
}

#ifndef CONFIG_USER_ONLY
static bool x86_debug_check_breakpoint(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    /* RF disables all architectural breakpoints. */
    return !(env->eflags & RF_MASK);
}
#endif

#include "hw/core/tcg-cpu-ops.h"

static const struct TCGCPUOps x86_tcg_ops = {
    .initialize = tcg_x86_init,
    .synchronize_from_tb = x86_cpu_synchronize_from_tb,
    .restore_state_to_opc = x86_restore_state_to_opc,
    .cpu_exec_enter = x86_cpu_exec_enter,
    .cpu_exec_exit = x86_cpu_exec_exit,
#ifdef CONFIG_USER_ONLY
    .fake_user_interrupt = x86_cpu_do_interrupt,
    .record_sigsegv = x86_cpu_record_sigsegv,
    .record_sigbus = x86_cpu_record_sigbus,
#else
    .tlb_fill = x86_cpu_tlb_fill,
    .do_interrupt = x86_cpu_do_interrupt,
    .cpu_exec_interrupt = x86_cpu_exec_interrupt,
    .do_unaligned_access = x86_cpu_do_unaligned_access,
    .debug_excp_handler = breakpoint_handler,
    .debug_check_breakpoint = x86_debug_check_breakpoint,
#endif /* !CONFIG_USER_ONLY */
};

static void tcg_cpu_init_ops(AccelCPUClass *accel_cpu, CPUClass *cc)
{
    /* for x86, all cpus use the same set of operations */
    cc->tcg_ops = &x86_tcg_ops;
}

static void tcg_cpu_class_init(CPUClass *cc)
{
    cc->init_accel_cpu = tcg_cpu_init_ops;
}

static void tcg_cpu_xsave_init(void)
{
#define XO(bit, field) \
    x86_ext_save_areas[bit].offset = offsetof(X86XSaveArea, field);

    XO(XSTATE_FP_BIT, legacy);
    XO(XSTATE_SSE_BIT, legacy);
    XO(XSTATE_YMM_BIT, avx_state);
    XO(XSTATE_BNDREGS_BIT, bndreg_state);
    XO(XSTATE_BNDCSR_BIT, bndcsr_state);
    XO(XSTATE_OPMASK_BIT, opmask_state);
    XO(XSTATE_ZMM_Hi256_BIT, zmm_hi256_state);
    XO(XSTATE_Hi16_ZMM_BIT, hi16_zmm_state);
    XO(XSTATE_PKRU_BIT, pkru_state);

#undef XO
}

/*
 * TCG-specific defaults that override cpudef models when using TCG.
 * Only for builtin_x86_defs models initialized with x86_register_cpudef_types.
 */
static PropValue tcg_default_props[] = {
    { "vme", "off" },
    { NULL, NULL },
};

static void tcg_cpu_instance_init(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    X86CPUClass *xcc = X86_CPU_GET_CLASS(cpu);

    if (xcc->model) {
        /* Special cases not set in the X86CPUDefinition structs: */
        x86_cpu_apply_props(cpu, tcg_default_props);
    }

    tcg_cpu_xsave_init();
}

static void tcg_cpu_accel_class_init(ObjectClass *oc, void *data)
{
    AccelCPUClass *acc = ACCEL_CPU_CLASS(oc);

#ifndef CONFIG_USER_ONLY
    acc->cpu_realizefn = tcg_cpu_realizefn;
#endif /* CONFIG_USER_ONLY */

    acc->cpu_class_init = tcg_cpu_class_init;
    acc->cpu_instance_init = tcg_cpu_instance_init;
}
static const TypeInfo tcg_cpu_accel_type_info = {
    .name = ACCEL_CPU_NAME("tcg"),

    .parent = TYPE_ACCEL_CPU,
    .class_init = tcg_cpu_accel_class_init,
    .abstract = true,
};
static void tcg_cpu_accel_register_types(void)
{
    type_register_static(&tcg_cpu_accel_type_info);
}
type_init(tcg_cpu_accel_register_types);
