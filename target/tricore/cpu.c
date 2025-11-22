/*
 *  TriCore emulation for qemu: main translation routines.
 *
 *  Copyright (c) 2012-2014 Bastian Koppelmann C-Lab/University Paderborn
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
#include "qapi/error.h"
#include "cpu.h"
#include "exec/translation-block.h"
#include "exec/cpu-interrupt.h"
#include "qemu/error-report.h"
#include "tcg/debug-assert.h"
#include "accel/tcg/cpu-ops.h"
#include "accel/tcg/cpu-ldst.h"

static inline void set_feature(CPUTriCoreState *env, int feature)
{
    env->features |= 1ULL << feature;
}

static const gchar *tricore_gdb_arch_name(CPUState *cs)
{
    return "tricore";
}

static void tricore_cpu_set_pc(CPUState *cs, vaddr value)
{
    cpu_env(cs)->PC = value & ~1;
}

static vaddr tricore_cpu_get_pc(CPUState *cs)
{
    return cpu_env(cs)->PC;
}

static TCGTBCPUState tricore_get_tb_cpu_state(CPUState *cs)
{
    CPUTriCoreState *env = cpu_env(cs);

    return (TCGTBCPUState){
        .pc = env->PC,
        .flags = FIELD_DP32(0, TB_FLAGS, PRIV, extract32(env->PSW, 10, 2)),
    };
}

static void tricore_cpu_synchronize_from_tb(CPUState *cs,
                                            const TranslationBlock *tb)
{
    tcg_debug_assert(!tcg_cflags_has(cs, CF_PCREL));
    cpu_env(cs)->PC = tb->pc;
}

static void tricore_restore_state_to_opc(CPUState *cs,
                                         const TranslationBlock *tb,
                                         const uint64_t *data)
{
    cpu_env(cs)->PC = data[0];
}

static void tricore_cpu_reset_hold(Object *obj, ResetType type)
{
    CPUState *cs = CPU(obj);
    TriCoreCPUClass *tcc = TRICORE_CPU_GET_CLASS(obj);

    if (tcc->parent_phases.hold) {
        tcc->parent_phases.hold(obj, type);
    }

    cpu_state_reset(cpu_env(cs));
}

static bool tricore_cpu_has_work(CPUState *cs)
{
    return true;
}

static int tricore_cpu_mmu_index(CPUState *cs, bool ifetch)
{
    return 0;
}

static void tricore_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    TriCoreCPU *cpu = TRICORE_CPU(dev);
    TriCoreCPUClass *tcc = TRICORE_CPU_GET_CLASS(dev);
    CPUTriCoreState *env = &cpu->env;
    Error *local_err = NULL;

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    /* Some features automatically imply others */
    if (tricore_has_feature(env, TRICORE_FEATURE_18)) {
        set_feature(env, TRICORE_FEATURE_162);
    }

    if (tricore_has_feature(env, TRICORE_FEATURE_162)) {
        set_feature(env, TRICORE_FEATURE_161);
    }

    if (tricore_has_feature(env, TRICORE_FEATURE_161)) {
        set_feature(env, TRICORE_FEATURE_16);
    }

    if (tricore_has_feature(env, TRICORE_FEATURE_16)) {
        set_feature(env, TRICORE_FEATURE_131);
    }
    if (tricore_has_feature(env, TRICORE_FEATURE_131)) {
        set_feature(env, TRICORE_FEATURE_13);
    }
    cpu_reset(cs);
    qemu_init_vcpu(cs);

    tcc->parent_realize(dev, errp);
}

static ObjectClass *tricore_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *typename;

    typename = g_strdup_printf(TRICORE_CPU_TYPE_NAME("%s"), cpu_model);
    oc = object_class_by_name(typename);
    g_free(typename);

    return oc;
}

static void tc1796_initfn(Object *obj)
{
    TriCoreCPU *cpu = TRICORE_CPU(obj);

    set_feature(&cpu->env, TRICORE_FEATURE_13);
}

static void tc1797_initfn(Object *obj)
{
    TriCoreCPU *cpu = TRICORE_CPU(obj);

    set_feature(&cpu->env, TRICORE_FEATURE_131);
}

static void tc27x_initfn(Object *obj)
{
    TriCoreCPU *cpu = TRICORE_CPU(obj);

    set_feature(&cpu->env, TRICORE_FEATURE_161);
}

static void tc37x_initfn(Object *obj)
{
    TriCoreCPU *cpu = TRICORE_CPU(obj);

    set_feature(&cpu->env, TRICORE_FEATURE_162);
}

static void tc4x_initfn(Object *obj)
{
    TriCoreCPU *cpu = TRICORE_CPU(obj);

    set_feature(&cpu->env, TRICORE_FEATURE_18);
}

/*
 * Check if an interrupt should be taken.
 * TriCore interrupt conditions:
 * - ICR.IE (Interrupt Enable) must be set
 * - Pending interrupt priority (PIPN) must be > Current CPU Priority (CCPN)
 */
static bool tricore_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    CPUTriCoreState *env = cpu_env(cs);

    if (interrupt_request & CPU_INTERRUPT_HARD) {
        /* Check if interrupts are enabled (ICR.IE) */
        if (!icr_get_ie(env)) {
            return false;
        }

        /* Check if we have a pending interrupt */
        if (!env->pending_int) {
            return false;
        }

        /*
         * Check priority: interrupt is taken if PIPN > CCPN
         * PIPN = Pending Interrupt Priority Number (from ICR)
         * CCPN = Current CPU Priority Number (from ICR)
         */
        uint32_t pipn = env->pending_int_level;
        uint32_t ccpn = icr_get_ccpn(env);

        if (pipn > ccpn) {
            /* Take the interrupt */
            cs->exception_index = TRAPC_IRQ;
            tricore_cpu_do_interrupt(cs);
            return true;
        }
    }
    return false;
}

/*
 * Handle TriCore hardware interrupt.
 * This saves the upper context and jumps to the interrupt vector.
 *
 * Based on TriCore Architecture Manual:
 * 1. Save upper context (registers) to CSA
 * 2. Update PCXI with saved context info
 * 3. Set ICR.CCPN = interrupt priority
 * 4. Clear ICR.IE (disable interrupts)
 * 5. Jump to BIV + (interrupt_number * 32)
 */
void tricore_cpu_do_interrupt(CPUState *cs)
{
    CPUTriCoreState *env = cpu_env(cs);
    uint32_t ea;
    uint32_t new_FCX;
    uint32_t pipn = env->pending_int_level;

    /* Check for Free Context List Underflow */
    if (env->FCX == 0) {
        /* FCU trap - cannot save context */
        /* In real hardware this would be a fatal error */
        return;
    }

    /* Save upper context */
    ea = ((env->FCX & MASK_FCX_FCXS) << 12) +
         ((env->FCX & MASK_FCX_FCXO) << 6);

    /* Get next free CSA */
    new_FCX = cpu_ldl_data(env, ea);

    /* Save upper context to CSA:
     * {PCXI, PSW, A[10], A[11], D[8], D[9], D[10], D[11],
     *  A[12], A[13], A[14], A[15], D[12], D[13], D[14], D[15]}
     */
    cpu_stl_data(env, ea, env->PCXI);
    cpu_stl_data(env, ea + 4, psw_read(env));
    cpu_stl_data(env, ea + 8, env->gpr_a[10]);
    cpu_stl_data(env, ea + 12, env->gpr_a[11]);
    cpu_stl_data(env, ea + 16, env->gpr_d[8]);
    cpu_stl_data(env, ea + 20, env->gpr_d[9]);
    cpu_stl_data(env, ea + 24, env->gpr_d[10]);
    cpu_stl_data(env, ea + 28, env->gpr_d[11]);
    cpu_stl_data(env, ea + 32, env->gpr_a[12]);
    cpu_stl_data(env, ea + 36, env->gpr_a[13]);
    cpu_stl_data(env, ea + 40, env->gpr_a[14]);
    cpu_stl_data(env, ea + 44, env->gpr_a[15]);
    cpu_stl_data(env, ea + 48, env->gpr_d[12]);
    cpu_stl_data(env, ea + 52, env->gpr_d[13]);
    cpu_stl_data(env, ea + 56, env->gpr_d[14]);
    cpu_stl_data(env, ea + 60, env->gpr_d[15]);

    /* Update PCXI */
    /* PCXI.PCPN = ICR.CCPN (save current priority) */
    pcxi_set_pcpn(env, icr_get_ccpn(env));
    /* PCXI.PIE = ICR.IE (save interrupt enable state) */
    pcxi_set_pie(env, icr_get_ie(env));
    /* PCXI.UL = 1 (upper context) */
    pcxi_set_ul(env, 1);
    /* PCXI[19:0] = FCX[19:0] */
    env->PCXI = (env->PCXI & 0xfff00000) + (env->FCX & 0x000fffff);

    /* Update FCX to next free CSA */
    env->FCX = (env->FCX & 0xfff00000) + (new_FCX & 0x000fffff);

    /* Set return address A[11] = PC */
    env->gpr_a[11] = env->PC;

    /* Set stack pointer to ISP if not already using interrupt stack */
    if ((env->PSW & MASK_PSW_IS) == 0) {
        env->gpr_a[10] = env->ISP;
    }

    /* Update PSW for interrupt handling */
    env->PSW |= MASK_PSW_IS;      /* Use interrupt stack */
    env->PSW |= (2 << 10);        /* Set I/O mode to Supervisor */
    env->PSW &= ~MASK_PSW_PRS;    /* Clear protection register set */
    env->PSW &= ~MASK_PSW_CDC;    /* Clear call depth counter */
    env->PSW |= MASK_PSW_CDE;     /* Enable call depth counting */
    env->PSW &= ~MASK_PSW_GW;     /* Disable global register write */

    /* Update ICR */
    /* ICR.CCPN = interrupt priority */
    icr_set_ccpn(env, pipn);
    /* ICR.IE = 0 (disable further interrupts) */
    icr_set_ie(env, 0);
    /* ICR.PIPN = 0 (clear pending interrupt) */
    env->ICR &= ~(0xFF << 16);

    /* Clear pending interrupt state */
    env->pending_int = false;
    env->pending_int_level = 0;

    /* Jump to interrupt vector: BIV + (priority * 32) */
    env->PC = (env->BIV & ~0x1) + (pipn * 32);

    cs->exception_index = -1;
}

#include "hw/core/sysemu-cpu-ops.h"

static const struct SysemuCPUOps tricore_sysemu_ops = {
    .has_work = tricore_cpu_has_work,
    .get_phys_page_debug = tricore_cpu_get_phys_page_debug,
};

static const TCGCPUOps tricore_tcg_ops = {
    /* MTTCG not yet supported: require strict ordering */
    .guest_default_memory_order = TCG_MO_ALL,
    .mttcg_supported = false,
    .initialize = tricore_tcg_init,
    .translate_code = tricore_translate_code,
    .get_tb_cpu_state = tricore_get_tb_cpu_state,
    .synchronize_from_tb = tricore_cpu_synchronize_from_tb,
    .restore_state_to_opc = tricore_restore_state_to_opc,
    .mmu_index = tricore_cpu_mmu_index,
    .tlb_fill = tricore_cpu_tlb_fill,
    .pointer_wrap = cpu_pointer_wrap_uint32,
    .cpu_exec_interrupt = tricore_cpu_exec_interrupt,
    .do_interrupt = tricore_cpu_do_interrupt,
    .cpu_exec_halt = tricore_cpu_has_work,
    .cpu_exec_reset = cpu_reset,
};

static void tricore_cpu_class_init(ObjectClass *c, const void *data)
{
    TriCoreCPUClass *mcc = TRICORE_CPU_CLASS(c);
    CPUClass *cc = CPU_CLASS(c);
    DeviceClass *dc = DEVICE_CLASS(c);
    ResettableClass *rc = RESETTABLE_CLASS(c);

    device_class_set_parent_realize(dc, tricore_cpu_realizefn,
                                    &mcc->parent_realize);

    resettable_class_set_parent_phases(rc, NULL, tricore_cpu_reset_hold, NULL,
                                       &mcc->parent_phases);
    cc->class_by_name = tricore_cpu_class_by_name;

    cc->gdb_read_register = tricore_cpu_gdb_read_register;
    cc->gdb_write_register = tricore_cpu_gdb_write_register;
    cc->gdb_num_core_regs = 44;
    cc->gdb_arch_name = tricore_gdb_arch_name;

    cc->dump_state = tricore_cpu_dump_state;
    cc->set_pc = tricore_cpu_set_pc;
    cc->get_pc = tricore_cpu_get_pc;
    cc->sysemu_ops = &tricore_sysemu_ops;
    cc->tcg_ops = &tricore_tcg_ops;
}

#define DEFINE_TRICORE_CPU_TYPE(cpu_model, initfn) \
    {                                              \
        .parent = TYPE_TRICORE_CPU,                \
        .instance_init = initfn,                   \
        .name = TRICORE_CPU_TYPE_NAME(cpu_model),  \
    }

static const TypeInfo tricore_cpu_type_infos[] = {
    {
        .name = TYPE_TRICORE_CPU,
        .parent = TYPE_CPU,
        .instance_size = sizeof(TriCoreCPU),
        .instance_align = __alignof(TriCoreCPU),
        .abstract = true,
        .class_size = sizeof(TriCoreCPUClass),
        .class_init = tricore_cpu_class_init,
    },
    DEFINE_TRICORE_CPU_TYPE("tc1796", tc1796_initfn),
    DEFINE_TRICORE_CPU_TYPE("tc1797", tc1797_initfn),
    DEFINE_TRICORE_CPU_TYPE("tc27x", tc27x_initfn),
    DEFINE_TRICORE_CPU_TYPE("tc37x", tc37x_initfn),
    DEFINE_TRICORE_CPU_TYPE("tc4x", tc4x_initfn),
};

DEFINE_TYPES(tricore_cpu_type_infos)
