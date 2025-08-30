/*
 * QEMU SuperH CPU
 *
 * Copyright (c) 2005 Samuel Tardieu
 * Copyright (c) 2012 SUSE LINUX Products GmbH
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
#include "qapi/error.h"
#include "qemu/qemu-print.h"
#include "cpu.h"
#include "migration/vmstate.h"
#include "exec/translation-block.h"
#include "fpu/softfloat-helpers.h"
#include "accel/tcg/cpu-ops.h"
#include "tcg/tcg.h"

static void superh_cpu_set_pc(CPUState *cs, vaddr value)
{
    SuperHCPU *cpu = SUPERH_CPU(cs);

    cpu->env.pc = value;
}

static vaddr superh_cpu_get_pc(CPUState *cs)
{
    SuperHCPU *cpu = SUPERH_CPU(cs);

    return cpu->env.pc;
}

static TCGTBCPUState superh_get_tb_cpu_state(CPUState *cs)
{
    CPUSH4State *env = cpu_env(cs);
    uint32_t flags;

    flags = env->flags
            | (env->fpscr & TB_FLAG_FPSCR_MASK)
            | (env->sr & TB_FLAG_SR_MASK)
            | (env->movcal_backup ? TB_FLAG_PENDING_MOVCA : 0); /* Bit 3 */
#ifdef CONFIG_USER_ONLY
    flags |= TB_FLAG_UNALIGN * !cs->prctl_unalign_sigbus;
#endif

    return (TCGTBCPUState){
        .pc = env->pc,
        .flags = flags,
#ifdef CONFIG_USER_ONLY
        /* For a gUSA region, notice the end of the region.  */
        .cs_base = flags & TB_FLAG_GUSA_MASK ? env->gregs[0] : 0,
#endif
    };
}

static void superh_cpu_synchronize_from_tb(CPUState *cs,
                                           const TranslationBlock *tb)
{
    SuperHCPU *cpu = SUPERH_CPU(cs);

    tcg_debug_assert(!tcg_cflags_has(cs, CF_PCREL));
    cpu->env.pc = tb->pc;
    cpu->env.flags = tb->flags & TB_FLAG_ENVFLAGS_MASK;
}

static void superh_restore_state_to_opc(CPUState *cs,
                                        const TranslationBlock *tb,
                                        const uint64_t *data)
{
    SuperHCPU *cpu = SUPERH_CPU(cs);

    cpu->env.pc = data[0];
    cpu->env.flags = data[1];
    /*
     * Theoretically delayed_pc should also be restored. In practice the
     * branch instruction is re-executed after exception, so the delayed
     * branch target will be recomputed.
     */
}

#ifndef CONFIG_USER_ONLY
static bool superh_io_recompile_replay_branch(CPUState *cs,
                                              const TranslationBlock *tb)
{
    CPUSH4State *env = cpu_env(cs);

    if ((env->flags & (TB_FLAG_DELAY_SLOT | TB_FLAG_DELAY_SLOT_COND))
        && !tcg_cflags_has(cs, CF_PCREL) && env->pc != tb->pc) {
        env->pc -= 2;
        env->flags &= ~(TB_FLAG_DELAY_SLOT | TB_FLAG_DELAY_SLOT_COND);
        return true;
    }
    return false;
}

static bool superh_cpu_has_work(CPUState *cs)
{
    return cpu_test_interrupt(cs, CPU_INTERRUPT_HARD);
}
#endif /* !CONFIG_USER_ONLY */

static int sh4_cpu_mmu_index(CPUState *cs, bool ifetch)
{
    CPUSH4State *env = cpu_env(cs);

    /*
     * The instruction in a RTE delay slot is fetched in privileged mode,
     * but executed in user mode.
     */
    if (ifetch && (env->flags & TB_FLAG_DELAY_SLOT_RTE)) {
        return 0;
    } else {
        return (env->sr & (1u << SR_MD)) == 0 ? 1 : 0;
    }
}

static void superh_cpu_reset_hold(Object *obj, ResetType type)
{
    CPUState *cs = CPU(obj);
    SuperHCPUClass *scc = SUPERH_CPU_GET_CLASS(obj);
    CPUSH4State *env = cpu_env(cs);

    if (scc->parent_phases.hold) {
        scc->parent_phases.hold(obj, type);
    }

    memset(env, 0, offsetof(CPUSH4State, end_reset_fields));

    env->pc = 0xA0000000;
#if defined(CONFIG_USER_ONLY)
    env->fpscr = FPSCR_PR; /* value for userspace according to the kernel */
    set_float_rounding_mode(float_round_nearest_even, &env->fp_status); /* ?! */
#else
    env->sr = (1u << SR_MD) | (1u << SR_RB) | (1u << SR_BL) |
              (1u << SR_I3) | (1u << SR_I2) | (1u << SR_I1) | (1u << SR_I0);
    env->fpscr = FPSCR_DN | FPSCR_RM_ZERO; /* CPU reset value according to SH4 manual */
    set_float_rounding_mode(float_round_to_zero, &env->fp_status);
    set_flush_to_zero(1, &env->fp_status);
#endif
    set_default_nan_mode(1, &env->fp_status);
    set_snan_bit_is_one(true, &env->fp_status);
    /* sign bit clear, set all frac bits other than msb */
    set_float_default_nan_pattern(0b00111111, &env->fp_status);
    /*
     * TODO: "SH-4 CPU Core Architecture ADCS 7182230F" doesn't say whether
     * it detects tininess before or after rounding. Section 6.4 is clear
     * that flush-to-zero happens when the result underflows, though, so
     * either this should be "detect ftz after rounding" or else we should
     * be setting "detect tininess before rounding".
     */
    set_float_ftz_detection(float_ftz_before_rounding, &env->fp_status);
}

static void superh_cpu_disas_set_info(CPUState *cpu, disassemble_info *info)
{
    info->endian = TARGET_BIG_ENDIAN ? BFD_ENDIAN_BIG
                                     : BFD_ENDIAN_LITTLE;
    info->mach = bfd_mach_sh4;
    info->print_insn = print_insn_sh;
}

static ObjectClass *superh_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *s, *typename = NULL;

    s = g_ascii_strdown(cpu_model, -1);
    if (strcmp(s, "any") == 0) {
        oc = object_class_by_name(TYPE_SH7750R_CPU);
        goto out;
    }

    typename = g_strdup_printf(SUPERH_CPU_TYPE_NAME("%s"), s);
    oc = object_class_by_name(typename);

out:
    g_free(s);
    g_free(typename);
    return oc;
}

static void sh7750r_cpu_initfn(Object *obj)
{
    CPUSH4State *env = cpu_env(CPU(obj));

    env->id = SH_CPU_SH7750R;
    env->features = SH_FEATURE_BCR3_AND_BCR4;
}

static void sh7750r_class_init(ObjectClass *oc, const void *data)
{
    SuperHCPUClass *scc = SUPERH_CPU_CLASS(oc);

    scc->pvr = 0x00050000;
    scc->prr = 0x00000100;
    scc->cvr = 0x00110000;
}

static void sh7751r_cpu_initfn(Object *obj)
{
    CPUSH4State *env = cpu_env(CPU(obj));

    env->id = SH_CPU_SH7751R;
    env->features = SH_FEATURE_BCR3_AND_BCR4;
}

static void sh7751r_class_init(ObjectClass *oc, const void *data)
{
    SuperHCPUClass *scc = SUPERH_CPU_CLASS(oc);

    scc->pvr = 0x04050005;
    scc->prr = 0x00000113;
    scc->cvr = 0x00110000; /* Neutered caches, should be 0x20480000 */
}

static void sh7785_cpu_initfn(Object *obj)
{
    CPUSH4State *env = cpu_env(CPU(obj));

    env->id = SH_CPU_SH7785;
    env->features = SH_FEATURE_SH4A;
}

static void sh7785_class_init(ObjectClass *oc, const void *data)
{
    SuperHCPUClass *scc = SUPERH_CPU_CLASS(oc);

    scc->pvr = 0x10300700;
    scc->prr = 0x00000200;
    scc->cvr = 0x71440211;
}

static void superh_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    SuperHCPUClass *scc = SUPERH_CPU_GET_CLASS(dev);
    Error *local_err = NULL;

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    cpu_reset(cs);
    qemu_init_vcpu(cs);

    scc->parent_realize(dev, errp);
}

static void superh_cpu_initfn(Object *obj)
{
    CPUSH4State *env = cpu_env(CPU(obj));

    env->movcal_backup_tail = &(env->movcal_backup);
}

#ifndef CONFIG_USER_ONLY
static const VMStateDescription vmstate_sh_cpu = {
    .name = "cpu",
    .unmigratable = 1,
};

#include "hw/core/sysemu-cpu-ops.h"

static const struct SysemuCPUOps sh4_sysemu_ops = {
    .has_work = superh_cpu_has_work,
    .get_phys_page_debug = superh_cpu_get_phys_page_debug,
};
#endif

static const TCGCPUOps superh_tcg_ops = {
    /* MTTCG not yet supported: require strict ordering */
    .guest_default_memory_order = TCG_MO_ALL,
    .mttcg_supported = false,

    .initialize = sh4_translate_init,
    .translate_code = sh4_translate_code,
    .get_tb_cpu_state = superh_get_tb_cpu_state,
    .synchronize_from_tb = superh_cpu_synchronize_from_tb,
    .restore_state_to_opc = superh_restore_state_to_opc,
    .mmu_index = sh4_cpu_mmu_index,

#ifndef CONFIG_USER_ONLY
    .tlb_fill = superh_cpu_tlb_fill,
    .pointer_wrap = cpu_pointer_wrap_notreached,
    .cpu_exec_interrupt = superh_cpu_exec_interrupt,
    .cpu_exec_halt = superh_cpu_has_work,
    .cpu_exec_reset = cpu_reset,
    .do_interrupt = superh_cpu_do_interrupt,
    .do_unaligned_access = superh_cpu_do_unaligned_access,
    .io_recompile_replay_branch = superh_io_recompile_replay_branch,
#endif /* !CONFIG_USER_ONLY */
};

static void superh_cpu_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    SuperHCPUClass *scc = SUPERH_CPU_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    device_class_set_parent_realize(dc, superh_cpu_realizefn,
                                    &scc->parent_realize);

    resettable_class_set_parent_phases(rc, NULL, superh_cpu_reset_hold, NULL,
                                       &scc->parent_phases);

    cc->class_by_name = superh_cpu_class_by_name;
    cc->dump_state = superh_cpu_dump_state;
    cc->set_pc = superh_cpu_set_pc;
    cc->get_pc = superh_cpu_get_pc;
    cc->gdb_read_register = superh_cpu_gdb_read_register;
    cc->gdb_write_register = superh_cpu_gdb_write_register;
#ifndef CONFIG_USER_ONLY
    cc->sysemu_ops = &sh4_sysemu_ops;
    dc->vmsd = &vmstate_sh_cpu;
#endif
    cc->disas_set_info = superh_cpu_disas_set_info;

    cc->gdb_num_core_regs = 59;
    cc->tcg_ops = &superh_tcg_ops;
}

#define DEFINE_SUPERH_CPU_TYPE(type_name, cinit, initfn) \
    {                                                    \
        .name = type_name,                               \
        .parent = TYPE_SUPERH_CPU,                       \
        .class_init = cinit,                             \
        .instance_init = initfn,                         \
    }
static const TypeInfo superh_cpu_type_infos[] = {
    {
        .name = TYPE_SUPERH_CPU,
        .parent = TYPE_CPU,
        .instance_size = sizeof(SuperHCPU),
        .instance_align = __alignof(SuperHCPU),
        .instance_init = superh_cpu_initfn,
        .abstract = true,
        .class_size = sizeof(SuperHCPUClass),
        .class_init = superh_cpu_class_init,
    },
    DEFINE_SUPERH_CPU_TYPE(TYPE_SH7750R_CPU, sh7750r_class_init,
                           sh7750r_cpu_initfn),
    DEFINE_SUPERH_CPU_TYPE(TYPE_SH7751R_CPU, sh7751r_class_init,
                           sh7751r_cpu_initfn),
    DEFINE_SUPERH_CPU_TYPE(TYPE_SH7785_CPU, sh7785_class_init,
                           sh7785_cpu_initfn),

};

DEFINE_TYPES(superh_cpu_type_infos)
