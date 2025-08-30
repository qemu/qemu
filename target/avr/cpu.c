/*
 * QEMU AVR CPU
 *
 * Copyright (c) 2019-2020 Michael Rolnik
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
#include "exec/translation-block.h"
#include "system/address-spaces.h"
#include "cpu.h"
#include "disas/dis-asm.h"
#include "tcg/debug-assert.h"
#include "hw/qdev-properties.h"
#include "accel/tcg/cpu-ops.h"

static void avr_cpu_set_pc(CPUState *cs, vaddr value)
{
    AVRCPU *cpu = AVR_CPU(cs);

    cpu->env.pc_w = value / 2; /* internally PC points to words */
}

static vaddr avr_cpu_get_pc(CPUState *cs)
{
    AVRCPU *cpu = AVR_CPU(cs);

    return cpu->env.pc_w * 2;
}

static bool avr_cpu_has_work(CPUState *cs)
{
    return cpu_test_interrupt(cs, CPU_INTERRUPT_HARD | CPU_INTERRUPT_RESET)
            && cpu_interrupts_enabled(cpu_env(cs));
}

static int avr_cpu_mmu_index(CPUState *cs, bool ifetch)
{
    return ifetch ? MMU_CODE_IDX : MMU_DATA_IDX;
}

static TCGTBCPUState avr_get_tb_cpu_state(CPUState *cs)
{
    CPUAVRState *env = cpu_env(cs);
    uint32_t flags = 0;

    if (env->fullacc) {
        flags |= TB_FLAGS_FULL_ACCESS;
    }
    if (env->skip) {
        flags |= TB_FLAGS_SKIP;
    }

    return (TCGTBCPUState){ .pc = env->pc_w * 2, .flags = flags };
}

static void avr_cpu_synchronize_from_tb(CPUState *cs,
                                        const TranslationBlock *tb)
{
    tcg_debug_assert(!tcg_cflags_has(cs, CF_PCREL));
    cpu_env(cs)->pc_w = tb->pc / 2; /* internally PC points to words */
}

static void avr_restore_state_to_opc(CPUState *cs,
                                     const TranslationBlock *tb,
                                     const uint64_t *data)
{
    cpu_env(cs)->pc_w = data[0];
}

static void avr_cpu_reset_hold(Object *obj, ResetType type)
{
    CPUState *cs = CPU(obj);
    AVRCPU *cpu = AVR_CPU(cs);
    AVRCPUClass *mcc = AVR_CPU_GET_CLASS(obj);
    CPUAVRState *env = &cpu->env;

    if (mcc->parent_phases.hold) {
        mcc->parent_phases.hold(obj, type);
    }

    env->pc_w = 0;
    env->sregI = 1;
    env->sregC = 0;
    env->sregZ = 0;
    env->sregN = 0;
    env->sregV = 0;
    env->sregS = 0;
    env->sregH = 0;
    env->sregT = 0;

    env->rampD = 0;
    env->rampX = 0;
    env->rampY = 0;
    env->rampZ = 0;
    env->eind = 0;
    env->sp = cpu->init_sp;

    env->skip = 0;

    memset(env->r, 0, sizeof(env->r));
}

static void avr_cpu_disas_set_info(CPUState *cpu, disassemble_info *info)
{
    info->endian = BFD_ENDIAN_LITTLE;
    info->mach = bfd_arch_avr;
    info->print_insn = avr_print_insn;
}

static void avr_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    CPUAVRState *env = cpu_env(cs);
    AVRCPU *cpu = env_archcpu(env);
    AVRCPUClass *mcc = AVR_CPU_GET_CLASS(dev);
    Error *local_err = NULL;

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }
    qemu_init_vcpu(cs);
    cpu_reset(cs);

    mcc->parent_realize(dev, errp);

    /*
     * Two blocks in the low data space loop back into cpu registers.
     */
    memory_region_init_io(&cpu->cpu_reg1, OBJECT(cpu), &avr_cpu_reg1, env,
                          "avr-cpu-reg1", 32);
    memory_region_add_subregion(get_system_memory(),
                                OFFSET_DATA, &cpu->cpu_reg1);

    memory_region_init_io(&cpu->cpu_reg2, OBJECT(cpu), &avr_cpu_reg2, env,
                          "avr-cpu-reg2", 8);
    memory_region_add_subregion(get_system_memory(),
                                OFFSET_DATA + 0x58, &cpu->cpu_reg2);
}

static void avr_cpu_set_int(void *opaque, int irq, int level)
{
    AVRCPU *cpu = opaque;
    CPUAVRState *env = &cpu->env;
    CPUState *cs = CPU(cpu);
    uint64_t mask = (1ull << irq);

    if (level) {
        env->intsrc |= mask;
        cpu_interrupt(cs, CPU_INTERRUPT_HARD);
    } else {
        env->intsrc &= ~mask;
        if (env->intsrc == 0) {
            cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
        }
    }
}

static void avr_cpu_initfn(Object *obj)
{
    AVRCPU *cpu = AVR_CPU(obj);

    /* Set the number of interrupts supported by the CPU. */
    qdev_init_gpio_in(DEVICE(cpu), avr_cpu_set_int,
                      sizeof(cpu->env.intsrc) * 8);
}

static const Property avr_cpu_properties[] = {
    DEFINE_PROP_UINT32("init-sp", AVRCPU, init_sp, 0),
};

static ObjectClass *avr_cpu_class_by_name(const char *cpu_model)
{
    return object_class_by_name(cpu_model);
}

static void avr_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    CPUAVRState *env = cpu_env(cs);
    int i;

    qemu_fprintf(f, "\n");
    qemu_fprintf(f, "PC:    %06x\n", env->pc_w * 2); /* PC points to words */
    qemu_fprintf(f, "SP:      %04x\n", env->sp);
    qemu_fprintf(f, "rampD:     %02x\n", env->rampD >> 16);
    qemu_fprintf(f, "rampX:     %02x\n", env->rampX >> 16);
    qemu_fprintf(f, "rampY:     %02x\n", env->rampY >> 16);
    qemu_fprintf(f, "rampZ:     %02x\n", env->rampZ >> 16);
    qemu_fprintf(f, "EIND:      %02x\n", env->eind >> 16);
    qemu_fprintf(f, "X:       %02x%02x\n", env->r[27], env->r[26]);
    qemu_fprintf(f, "Y:       %02x%02x\n", env->r[29], env->r[28]);
    qemu_fprintf(f, "Z:       %02x%02x\n", env->r[31], env->r[30]);
    qemu_fprintf(f, "SREG:    [ %c %c %c %c %c %c %c %c ]\n",
                 env->sregI ? 'I' : '-',
                 env->sregT ? 'T' : '-',
                 env->sregH ? 'H' : '-',
                 env->sregS ? 'S' : '-',
                 env->sregV ? 'V' : '-',
                 env->sregN ? '-' : 'N', /* Zf has negative logic */
                 env->sregZ ? 'Z' : '-',
                 env->sregC ? 'I' : '-');
    qemu_fprintf(f, "SKIP:    %02x\n", env->skip);

    qemu_fprintf(f, "\n");
    for (i = 0; i < ARRAY_SIZE(env->r); i++) {
        qemu_fprintf(f, "R[%02d]:  %02x   ", i, env->r[i]);

        if ((i % 8) == 7) {
            qemu_fprintf(f, "\n");
        }
    }
    qemu_fprintf(f, "\n");
}

#include "hw/core/sysemu-cpu-ops.h"

static const struct SysemuCPUOps avr_sysemu_ops = {
    .has_work = avr_cpu_has_work,
    .get_phys_page_debug = avr_cpu_get_phys_page_debug,
};

static const TCGCPUOps avr_tcg_ops = {
    .guest_default_memory_order = 0,
    .mttcg_supported = false,
    .initialize = avr_cpu_tcg_init,
    .translate_code = avr_cpu_translate_code,
    .get_tb_cpu_state = avr_get_tb_cpu_state,
    .synchronize_from_tb = avr_cpu_synchronize_from_tb,
    .restore_state_to_opc = avr_restore_state_to_opc,
    .mmu_index = avr_cpu_mmu_index,
    .cpu_exec_interrupt = avr_cpu_exec_interrupt,
    .cpu_exec_halt = avr_cpu_has_work,
    .cpu_exec_reset = cpu_reset,
    .tlb_fill = avr_cpu_tlb_fill,
    .do_interrupt = avr_cpu_do_interrupt,
    /*
     * TODO: code and data wrapping are different, but for the most part
     * AVR only references bytes or aligned code fetches.  But we use
     * non-aligned MO_16 accesses for stack push/pop.
     */
    .pointer_wrap = cpu_pointer_wrap_uint32,
};

static void avr_cpu_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    AVRCPUClass *mcc = AVR_CPU_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    device_class_set_parent_realize(dc, avr_cpu_realizefn, &mcc->parent_realize);

    device_class_set_props(dc, avr_cpu_properties);

    resettable_class_set_parent_phases(rc, NULL, avr_cpu_reset_hold, NULL,
                                       &mcc->parent_phases);

    cc->class_by_name = avr_cpu_class_by_name;

    cc->dump_state = avr_cpu_dump_state;
    cc->set_pc = avr_cpu_set_pc;
    cc->get_pc = avr_cpu_get_pc;
    dc->vmsd = &vms_avr_cpu;
    cc->sysemu_ops = &avr_sysemu_ops;
    cc->disas_set_info = avr_cpu_disas_set_info;
    cc->gdb_read_register = avr_cpu_gdb_read_register;
    cc->gdb_write_register = avr_cpu_gdb_write_register;
    cc->gdb_adjust_breakpoint = avr_cpu_gdb_adjust_breakpoint;
    cc->gdb_core_xml_file = "avr-cpu.xml";
    cc->tcg_ops = &avr_tcg_ops;
}

/*
 * Setting features of AVR core type avr5
 * --------------------------------------
 *
 * This type of AVR core is present in the following AVR MCUs:
 *
 * ata5702m322, ata5782, ata5790, ata5790n, ata5791, ata5795, ata5831, ata6613c,
 * ata6614q, ata8210, ata8510, atmega16, atmega16a, atmega161, atmega162,
 * atmega163, atmega164a, atmega164p, atmega164pa, atmega165, atmega165a,
 * atmega165p, atmega165pa, atmega168, atmega168a, atmega168p, atmega168pa,
 * atmega168pb, atmega169, atmega169a, atmega169p, atmega169pa, atmega16hvb,
 * atmega16hvbrevb, atmega16m1, atmega16u4, atmega32a, atmega32, atmega323,
 * atmega324a, atmega324p, atmega324pa, atmega325, atmega325a, atmega325p,
 * atmega325pa, atmega3250, atmega3250a, atmega3250p, atmega3250pa, atmega328,
 * atmega328p, atmega328pb, atmega329, atmega329a, atmega329p, atmega329pa,
 * atmega3290, atmega3290a, atmega3290p, atmega3290pa, atmega32c1, atmega32m1,
 * atmega32u4, atmega32u6, atmega406, atmega64, atmega64a, atmega640, atmega644,
 * atmega644a, atmega644p, atmega644pa, atmega645, atmega645a, atmega645p,
 * atmega6450, atmega6450a, atmega6450p, atmega649, atmega649a, atmega649p,
 * atmega6490, atmega16hva, atmega16hva2, atmega32hvb, atmega6490a, atmega6490p,
 * atmega64c1, atmega64m1, atmega64hve, atmega64hve2, atmega64rfr2,
 * atmega644rfr2, atmega32hvbrevb, at90can32, at90can64, at90pwm161, at90pwm216,
 * at90pwm316, at90scr100, at90usb646, at90usb647, at94k, m3000
 */
static void avr_avr5_initfn(Object *obj)
{
    CPUAVRState *env = cpu_env(CPU(obj));

    set_avr_feature(env, AVR_FEATURE_LPM);
    set_avr_feature(env, AVR_FEATURE_IJMP_ICALL);
    set_avr_feature(env, AVR_FEATURE_ADIW_SBIW);
    set_avr_feature(env, AVR_FEATURE_SRAM);
    set_avr_feature(env, AVR_FEATURE_BREAK);

    set_avr_feature(env, AVR_FEATURE_2_BYTE_PC);
    set_avr_feature(env, AVR_FEATURE_2_BYTE_SP);
    set_avr_feature(env, AVR_FEATURE_JMP_CALL);
    set_avr_feature(env, AVR_FEATURE_LPMX);
    set_avr_feature(env, AVR_FEATURE_MOVW);
    set_avr_feature(env, AVR_FEATURE_MUL);
}

/*
 * Setting features of AVR core type avr51
 * --------------------------------------
 *
 * This type of AVR core is present in the following AVR MCUs:
 *
 * atmega128, atmega128a, atmega1280, atmega1281, atmega1284, atmega1284p,
 * atmega128rfa1, atmega128rfr2, atmega1284rfr2, at90can128, at90usb1286,
 * at90usb1287
 */
static void avr_avr51_initfn(Object *obj)
{
    CPUAVRState *env = cpu_env(CPU(obj));

    set_avr_feature(env, AVR_FEATURE_LPM);
    set_avr_feature(env, AVR_FEATURE_IJMP_ICALL);
    set_avr_feature(env, AVR_FEATURE_ADIW_SBIW);
    set_avr_feature(env, AVR_FEATURE_SRAM);
    set_avr_feature(env, AVR_FEATURE_BREAK);

    set_avr_feature(env, AVR_FEATURE_2_BYTE_PC);
    set_avr_feature(env, AVR_FEATURE_2_BYTE_SP);
    set_avr_feature(env, AVR_FEATURE_RAMPZ);
    set_avr_feature(env, AVR_FEATURE_ELPMX);
    set_avr_feature(env, AVR_FEATURE_ELPM);
    set_avr_feature(env, AVR_FEATURE_JMP_CALL);
    set_avr_feature(env, AVR_FEATURE_LPMX);
    set_avr_feature(env, AVR_FEATURE_MOVW);
    set_avr_feature(env, AVR_FEATURE_MUL);
}

/*
 * Setting features of AVR core type avr6
 * --------------------------------------
 *
 * This type of AVR core is present in the following AVR MCUs:
 *
 * atmega2560, atmega2561, atmega256rfr2, atmega2564rfr2
 */
static void avr_avr6_initfn(Object *obj)
{
    CPUAVRState *env = cpu_env(CPU(obj));

    set_avr_feature(env, AVR_FEATURE_LPM);
    set_avr_feature(env, AVR_FEATURE_IJMP_ICALL);
    set_avr_feature(env, AVR_FEATURE_ADIW_SBIW);
    set_avr_feature(env, AVR_FEATURE_SRAM);
    set_avr_feature(env, AVR_FEATURE_BREAK);

    set_avr_feature(env, AVR_FEATURE_3_BYTE_PC);
    set_avr_feature(env, AVR_FEATURE_2_BYTE_SP);
    set_avr_feature(env, AVR_FEATURE_RAMPZ);
    set_avr_feature(env, AVR_FEATURE_EIJMP_EICALL);
    set_avr_feature(env, AVR_FEATURE_ELPMX);
    set_avr_feature(env, AVR_FEATURE_ELPM);
    set_avr_feature(env, AVR_FEATURE_JMP_CALL);
    set_avr_feature(env, AVR_FEATURE_LPMX);
    set_avr_feature(env, AVR_FEATURE_MOVW);
    set_avr_feature(env, AVR_FEATURE_MUL);
}

typedef struct AVRCPUInfo {
    const char *name;
    void (*initfn)(Object *obj);
} AVRCPUInfo;


#define DEFINE_AVR_CPU_TYPE(model, initfn) \
    { \
        .parent = TYPE_AVR_CPU, \
        .instance_init = initfn, \
        .name = AVR_CPU_TYPE_NAME(model), \
    }

static const TypeInfo avr_cpu_type_info[] = {
    {
        .name = TYPE_AVR_CPU,
        .parent = TYPE_CPU,
        .instance_size = sizeof(AVRCPU),
        .instance_align = __alignof(AVRCPU),
        .instance_init = avr_cpu_initfn,
        .class_size = sizeof(AVRCPUClass),
        .class_init = avr_cpu_class_init,
        .abstract = true,
    },
    DEFINE_AVR_CPU_TYPE("avr5", avr_avr5_initfn),
    DEFINE_AVR_CPU_TYPE("avr51", avr_avr51_initfn),
    DEFINE_AVR_CPU_TYPE("avr6", avr_avr6_initfn),
};

DEFINE_TYPES(avr_cpu_type_info)
