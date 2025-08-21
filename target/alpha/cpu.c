/*
 * QEMU Alpha CPU
 *
 * Copyright (c) 2007 Jocelyn Mayer
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
#include "exec/translation-block.h"
#include "exec/target_page.h"
#include "accel/tcg/cpu-ops.h"
#include "fpu/softfloat.h"


static void alpha_cpu_set_pc(CPUState *cs, vaddr value)
{
    CPUAlphaState *env = cpu_env(cs);
    env->pc = value;
}

static vaddr alpha_cpu_get_pc(CPUState *cs)
{
    CPUAlphaState *env = cpu_env(cs);
    return env->pc;
}

static TCGTBCPUState alpha_get_tb_cpu_state(CPUState *cs)
{
    CPUAlphaState *env = cpu_env(cs);
    uint32_t flags = env->flags & ENV_FLAG_TB_MASK;

#ifdef CONFIG_USER_ONLY
    flags |= TB_FLAG_UNALIGN * !cs->prctl_unalign_sigbus;
#endif

    return (TCGTBCPUState){ .pc = env->pc, .flags = flags };
}

static void alpha_cpu_synchronize_from_tb(CPUState *cs,
                                          const TranslationBlock *tb)
{
    /* The program counter is always up to date with CF_PCREL. */
    if (!(tb_cflags(tb) & CF_PCREL)) {
        CPUAlphaState *env = cpu_env(cs);
        env->pc = tb->pc;
    }
}

static void alpha_restore_state_to_opc(CPUState *cs,
                                       const TranslationBlock *tb,
                                       const uint64_t *data)
{
    CPUAlphaState *env = cpu_env(cs);

    if (tb_cflags(tb) & CF_PCREL) {
        env->pc = (env->pc & TARGET_PAGE_MASK) | data[0];
    } else {
        env->pc = data[0];
    }
}

#ifndef CONFIG_USER_ONLY
static bool alpha_cpu_has_work(CPUState *cs)
{
    /* Here we are checking to see if the CPU should wake up from HALT.
       We will have gotten into this state only for WTINT from PALmode.  */
    /* ??? I'm not sure how the IPL state works with WTINT to keep a CPU
       asleep even if (some) interrupts have been asserted.  For now,
       assume that if a CPU really wants to stay asleep, it will mask
       interrupts at the chipset level, which will prevent these bits
       from being set in the first place.  */
    return cpu_test_interrupt(cs, CPU_INTERRUPT_HARD
                                  | CPU_INTERRUPT_TIMER
                                  | CPU_INTERRUPT_SMP
                                  | CPU_INTERRUPT_MCHK);
}
#endif /* !CONFIG_USER_ONLY */

static int alpha_cpu_mmu_index(CPUState *cs, bool ifetch)
{
    return alpha_env_mmu_index(cpu_env(cs));
}

static void alpha_cpu_disas_set_info(CPUState *cpu, disassemble_info *info)
{
    info->endian = BFD_ENDIAN_LITTLE;
    info->mach = bfd_mach_alpha_ev6;
    info->print_insn = print_insn_alpha;
}

static void alpha_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    AlphaCPUClass *acc = ALPHA_CPU_GET_CLASS(dev);
    Error *local_err = NULL;

#ifndef CONFIG_USER_ONLY
    /* Use pc-relative instructions in system-mode */
    cs->tcg_cflags |= CF_PCREL;
#endif

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    qemu_init_vcpu(cs);

    acc->parent_realize(dev, errp);
}

/* Models */
typedef struct AlphaCPUAlias {
    const char *alias;
    const char *typename;
} AlphaCPUAlias;

static const AlphaCPUAlias alpha_cpu_aliases[] = {
    { "21064",   ALPHA_CPU_TYPE_NAME("ev4") },
    { "21164",   ALPHA_CPU_TYPE_NAME("ev5") },
    { "21164a",  ALPHA_CPU_TYPE_NAME("ev56") },
    { "21164pc", ALPHA_CPU_TYPE_NAME("pca56") },
    { "21264",   ALPHA_CPU_TYPE_NAME("ev6") },
    { "21264a",  ALPHA_CPU_TYPE_NAME("ev67") },
};

static ObjectClass *alpha_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *typename;
    int i;

    oc = object_class_by_name(cpu_model);
    if (oc != NULL && object_class_dynamic_cast(oc, TYPE_ALPHA_CPU) != NULL) {
        return oc;
    }

    for (i = 0; i < ARRAY_SIZE(alpha_cpu_aliases); i++) {
        if (strcmp(cpu_model, alpha_cpu_aliases[i].alias) == 0) {
            oc = object_class_by_name(alpha_cpu_aliases[i].typename);
            assert(oc != NULL && !object_class_is_abstract(oc));
            return oc;
        }
    }

    typename = g_strdup_printf(ALPHA_CPU_TYPE_NAME("%s"), cpu_model);
    oc = object_class_by_name(typename);
    g_free(typename);

    return oc;
}

static void ev4_cpu_initfn(Object *obj)
{
    cpu_env(CPU(obj))->implver = IMPLVER_2106x;
}

static void ev5_cpu_initfn(Object *obj)
{
    cpu_env(CPU(obj))->implver = IMPLVER_21164;
}

static void ev56_cpu_initfn(Object *obj)
{
    cpu_env(CPU(obj))->amask |= AMASK_BWX;
}

static void pca56_cpu_initfn(Object *obj)
{
    cpu_env(CPU(obj))->amask |= AMASK_MVI;
}

static void ev6_cpu_initfn(Object *obj)
{
    CPUAlphaState *env = cpu_env(CPU(obj));

    env->implver = IMPLVER_21264;
    env->amask = AMASK_BWX | AMASK_FIX | AMASK_MVI | AMASK_TRAP;
}

static void ev67_cpu_initfn(Object *obj)
{
    cpu_env(CPU(obj))->amask |= AMASK_CIX | AMASK_PREFETCH;
}

static void alpha_cpu_initfn(Object *obj)
{
    CPUAlphaState *env = cpu_env(CPU(obj));

    /* TODO all this should be done in reset, not init */

    env->lock_addr = -1;

    /*
     * TODO: this is incorrect. The Alpha Architecture Handbook version 4
     * describes NaN propagation in section 4.7.10.4. We should prefer
     * the operand in Fb (whether it is a QNaN or an SNaN), then the
     * operand in Fa. That is float_2nan_prop_ba.
     */
    set_float_2nan_prop_rule(float_2nan_prop_x87, &env->fp_status);
    /* Default NaN: sign bit clear, msb frac bit set */
    set_float_default_nan_pattern(0b01000000, &env->fp_status);
    /*
     * TODO: this is incorrect. The Alpha Architecture Handbook version 4
     * section 4.7.7.11 says that we flush to zero for underflow cases, so
     * this should be float_ftz_after_rounding to match the
     * tininess_after_rounding (which is specified in section 4.7.5).
     */
    set_float_ftz_detection(float_ftz_before_rounding, &env->fp_status);
#if defined(CONFIG_USER_ONLY)
    env->flags = ENV_FLAG_PS_USER | ENV_FLAG_FEN;
    cpu_alpha_store_fpcr(env, (uint64_t)(FPCR_INVD | FPCR_DZED | FPCR_OVFD
                                         | FPCR_UNFD | FPCR_INED | FPCR_DNOD
                                         | FPCR_DYN_NORMAL) << 32);
#else
    env->flags = ENV_FLAG_PAL_MODE | ENV_FLAG_FEN;
#endif
}

#ifndef CONFIG_USER_ONLY
#include "hw/core/sysemu-cpu-ops.h"

static const struct SysemuCPUOps alpha_sysemu_ops = {
    .has_work = alpha_cpu_has_work,
    .get_phys_page_debug = alpha_cpu_get_phys_page_debug,
};
#endif

static const TCGCPUOps alpha_tcg_ops = {
    /* Alpha processors have a weak memory model */
    .guest_default_memory_order = 0,
    .mttcg_supported = true,

    .initialize = alpha_translate_init,
    .translate_code = alpha_translate_code,
    .get_tb_cpu_state = alpha_get_tb_cpu_state,
    .synchronize_from_tb = alpha_cpu_synchronize_from_tb,
    .restore_state_to_opc = alpha_restore_state_to_opc,
    .mmu_index = alpha_cpu_mmu_index,

#ifdef CONFIG_USER_ONLY
    .record_sigsegv = alpha_cpu_record_sigsegv,
    .record_sigbus = alpha_cpu_record_sigbus,
#else
    .tlb_fill = alpha_cpu_tlb_fill,
    .pointer_wrap = cpu_pointer_wrap_notreached,
    .cpu_exec_interrupt = alpha_cpu_exec_interrupt,
    .cpu_exec_halt = alpha_cpu_has_work,
    .cpu_exec_reset = cpu_reset,
    .do_interrupt = alpha_cpu_do_interrupt,
    .do_transaction_failed = alpha_cpu_do_transaction_failed,
    .do_unaligned_access = alpha_cpu_do_unaligned_access,
#endif /* !CONFIG_USER_ONLY */
};

static void alpha_cpu_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    AlphaCPUClass *acc = ALPHA_CPU_CLASS(oc);

    device_class_set_parent_realize(dc, alpha_cpu_realizefn,
                                    &acc->parent_realize);

    cc->class_by_name = alpha_cpu_class_by_name;
    cc->dump_state = alpha_cpu_dump_state;
    cc->set_pc = alpha_cpu_set_pc;
    cc->get_pc = alpha_cpu_get_pc;
    cc->gdb_read_register = alpha_cpu_gdb_read_register;
    cc->gdb_write_register = alpha_cpu_gdb_write_register;
    cc->gdb_core_xml_file = "alpha-core.xml";
#ifndef CONFIG_USER_ONLY
    dc->vmsd = &vmstate_alpha_cpu;
    cc->sysemu_ops = &alpha_sysemu_ops;
#endif
    cc->disas_set_info = alpha_cpu_disas_set_info;

    cc->tcg_ops = &alpha_tcg_ops;
    cc->gdb_num_core_regs = 67;
}

#define DEFINE_ALPHA_CPU_TYPE(base_type, cpu_model, initfn) \
     {                                                      \
         .parent = base_type,                               \
         .instance_init = initfn,                           \
         .name = ALPHA_CPU_TYPE_NAME(cpu_model),            \
     }

static const TypeInfo alpha_cpu_type_infos[] = {
    {
        .name = TYPE_ALPHA_CPU,
        .parent = TYPE_CPU,
        .instance_size = sizeof(AlphaCPU),
        .instance_align = __alignof(AlphaCPU),
        .instance_init = alpha_cpu_initfn,
        .abstract = true,
        .class_size = sizeof(AlphaCPUClass),
        .class_init = alpha_cpu_class_init,
    },
    DEFINE_ALPHA_CPU_TYPE(TYPE_ALPHA_CPU, "ev4", ev4_cpu_initfn),
    DEFINE_ALPHA_CPU_TYPE(TYPE_ALPHA_CPU, "ev5", ev5_cpu_initfn),
    DEFINE_ALPHA_CPU_TYPE(ALPHA_CPU_TYPE_NAME("ev5"), "ev56", ev56_cpu_initfn),
    DEFINE_ALPHA_CPU_TYPE(ALPHA_CPU_TYPE_NAME("ev56"), "pca56",
                          pca56_cpu_initfn),
    DEFINE_ALPHA_CPU_TYPE(TYPE_ALPHA_CPU, "ev6", ev6_cpu_initfn),
    DEFINE_ALPHA_CPU_TYPE(ALPHA_CPU_TYPE_NAME("ev6"), "ev67", ev67_cpu_initfn),
    DEFINE_ALPHA_CPU_TYPE(ALPHA_CPU_TYPE_NAME("ev67"), "ev68", NULL),
};

DEFINE_TYPES(alpha_cpu_type_infos)
