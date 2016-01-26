/*
 * QEMU TILE-Gx CPU
 *
 *  Copyright (c) 2015 Chen Gang
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
#include "cpu.h"
#include "qemu-common.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "linux-user/syscall_defs.h"

static void tilegx_cpu_dump_state(CPUState *cs, FILE *f,
                                  fprintf_function cpu_fprintf, int flags)
{
    static const char * const reg_names[TILEGX_R_COUNT] = {
         "r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",
         "r8",  "r9", "r10", "r11", "r12", "r13", "r14", "r15",
        "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
        "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31",
        "r32", "r33", "r34", "r35", "r36", "r37", "r38", "r39",
        "r40", "r41", "r42", "r43", "r44", "r45", "r46", "r47",
        "r48", "r49", "r50", "r51",  "bp",  "tp",  "sp",  "lr"
    };

    TileGXCPU *cpu = TILEGX_CPU(cs);
    CPUTLGState *env = &cpu->env;
    int i;

    for (i = 0; i < TILEGX_R_COUNT; i++) {
        cpu_fprintf(f, "%-4s" TARGET_FMT_lx "%s",
                    reg_names[i], env->regs[i],
                    (i % 4) == 3 ? "\n" : " ");
    }
    cpu_fprintf(f, "PC  " TARGET_FMT_lx " CEX " TARGET_FMT_lx "\n\n",
                env->pc, env->spregs[TILEGX_SPR_CMPEXCH]);
}

TileGXCPU *cpu_tilegx_init(const char *cpu_model)
{
    TileGXCPU *cpu;

    cpu = TILEGX_CPU(object_new(TYPE_TILEGX_CPU));

    object_property_set_bool(OBJECT(cpu), true, "realized", NULL);

    return cpu;
}

static void tilegx_cpu_set_pc(CPUState *cs, vaddr value)
{
    TileGXCPU *cpu = TILEGX_CPU(cs);

    cpu->env.pc = value;
}

static bool tilegx_cpu_has_work(CPUState *cs)
{
    return true;
}

static void tilegx_cpu_reset(CPUState *s)
{
    TileGXCPU *cpu = TILEGX_CPU(s);
    TileGXCPUClass *tcc = TILEGX_CPU_GET_CLASS(cpu);
    CPUTLGState *env = &cpu->env;

    tcc->parent_reset(s);

    memset(env, 0, sizeof(CPUTLGState));
    tlb_flush(s, 1);
}

static void tilegx_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    TileGXCPUClass *tcc = TILEGX_CPU_GET_CLASS(dev);

    cpu_reset(cs);
    qemu_init_vcpu(cs);

    tcc->parent_realize(dev, errp);
}

static void tilegx_cpu_initfn(Object *obj)
{
    CPUState *cs = CPU(obj);
    TileGXCPU *cpu = TILEGX_CPU(obj);
    CPUTLGState *env = &cpu->env;
    static bool tcg_initialized;

    cs->env_ptr = env;
    cpu_exec_init(cs, &error_abort);

    if (tcg_enabled() && !tcg_initialized) {
        tcg_initialized = true;
        tilegx_tcg_init();
    }
}

static void tilegx_cpu_do_interrupt(CPUState *cs)
{
    cs->exception_index = -1;
}

static int tilegx_cpu_handle_mmu_fault(CPUState *cs, vaddr address, int rw,
                                       int mmu_idx)
{
    TileGXCPU *cpu = TILEGX_CPU(cs);

    /* The sigcode field will be filled in by do_signal in main.c.  */
    cs->exception_index = TILEGX_EXCP_SIGNAL;
    cpu->env.excaddr = address;
    cpu->env.signo = TARGET_SIGSEGV;
    cpu->env.sigcode = 0;

    return 1;
}

static bool tilegx_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    if (interrupt_request & CPU_INTERRUPT_HARD) {
        tilegx_cpu_do_interrupt(cs);
        return true;
    }
    return false;
}

static void tilegx_cpu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    TileGXCPUClass *tcc = TILEGX_CPU_CLASS(oc);

    tcc->parent_realize = dc->realize;
    dc->realize = tilegx_cpu_realizefn;

    tcc->parent_reset = cc->reset;
    cc->reset = tilegx_cpu_reset;

    cc->has_work = tilegx_cpu_has_work;
    cc->do_interrupt = tilegx_cpu_do_interrupt;
    cc->cpu_exec_interrupt = tilegx_cpu_exec_interrupt;
    cc->dump_state = tilegx_cpu_dump_state;
    cc->set_pc = tilegx_cpu_set_pc;
    cc->handle_mmu_fault = tilegx_cpu_handle_mmu_fault;
    cc->gdb_num_core_regs = 0;

    /*
     * Reason: tilegx_cpu_initfn() calls cpu_exec_init(), which saves
     * the object in cpus -> dangling pointer after final
     * object_unref().
     */
    dc->cannot_destroy_with_object_finalize_yet = true;
}

static const TypeInfo tilegx_cpu_type_info = {
    .name = TYPE_TILEGX_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(TileGXCPU),
    .instance_init = tilegx_cpu_initfn,
    .class_size = sizeof(TileGXCPUClass),
    .class_init = tilegx_cpu_class_init,
};

static void tilegx_cpu_register_types(void)
{
    type_register_static(&tilegx_cpu_type_info);
}

type_init(tilegx_cpu_register_types)
