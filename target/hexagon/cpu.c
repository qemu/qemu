/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */

/*
 * QEMU Hexagon
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "qapi/error.h"
#include "migration/vmstate.h"

static void hexagon_v67_cpu_init(Object *obj)
{
}

static ObjectClass *hexagon_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *typename;
    char **cpuname;

    cpuname = g_strsplit(cpu_model, ",", 1);
    typename = g_strdup_printf(HEXAGON_CPU_TYPE_NAME("%s"), cpuname[0]);
    oc = object_class_by_name(typename);
    g_strfreev(cpuname);
    g_free(typename);
    if (!oc || !object_class_dynamic_cast(oc, TYPE_HEXAGON_CPU) ||
        object_class_is_abstract(oc)) {
        return NULL;
    }
    return oc;
}

const char * const hexagon_regnames[] = {
   "r0", "r1",  "r2",  "r3",  "r4",   "r5",  "r6",  "r7",
   "r8", "r9",  "r10", "r11", "r12",  "r13", "r14", "r15",
  "r16", "r17", "r18", "r19", "r20",  "r21", "r22", "r23",
  "r24", "r25", "r26", "r27", "r28",  "r29", "r30", "r31",
  "sa0", "lc0", "sa1", "lc1", "p3_0", "c5",  "m0",  "m1",
  "usr", "pc",  "ugp", "gp",  "cs0",  "cs1", "c14", "c15",
  "c16", "c17", "c18", "c19", "c20",  "c21", "c22", "c23",
  "c24", "c25", "c26", "c27", "c28",  "c29", "c30", "c31",
};

const char * const hexagon_prednames[] = {
  "p0 ", "p1 ", "p2 ", "p3 "
};

static inline target_ulong hack_stack_ptrs(CPUHexagonState *env,
                                           target_ulong addr)
{
    target_ulong stack_start = env->stack_start;
    target_ulong stack_size = 0x10000;
    target_ulong stack_adjust = env->stack_adjust;

    if (stack_start + 0x1000 >= addr && addr >= (stack_start - stack_size)) {
        return addr - stack_adjust;
    }
    return addr;
}

static void print_reg(FILE *f, CPUHexagonState *env, int regnum)
{
    fprintf(f, "  %s = 0x" TARGET_FMT_lx "\n",
        hexagon_regnames[regnum],
        regnum < 32 ? hack_stack_ptrs(env, env->gpr[regnum])
                    : env->gpr[regnum]);
}

static void print_vreg(FILE *f, CPUHexagonState *env, int regnum)
{
    int i;
    fprintf(f, "  v%d = (", regnum);
    fprintf(f, "0x%02x", env->VRegs[regnum].ub[MAX_VEC_SIZE_BYTES - 1]);
    for (i = MAX_VEC_SIZE_BYTES - 2; i >= 0; i--) {
        fprintf(f, ", 0x%02x", env->VRegs[regnum].ub[i]);
    }
    fprintf(f, ")\n");
}

void hexagon_debug_vreg(CPUHexagonState *env, int regnum)
{
    print_vreg(stdout, env, regnum);
}

static void print_qreg(FILE *f, CPUHexagonState *env, int regnum)
{
    int i;
    fprintf(f, "  q%d = (", regnum);
    fprintf(f, ", 0x%02x",
                env->QRegs[regnum].ub[MAX_VEC_SIZE_BYTES / 8 - 1]);
    for (i = MAX_VEC_SIZE_BYTES / 8 - 2; i >= 0; i--) {
        fprintf(f, ", 0x%02x", env->QRegs[regnum].ub[i]);
    }
    fprintf(f, ")\n");
}

void hexagon_debug_qreg(CPUHexagonState *env, int regnum)
{
    print_qreg(stdout, env, regnum);
}

static void hexagon_dump(CPUHexagonState *env, FILE *f)
{
    static target_ulong last_pc;
    int i;

    /*
     * When comparing with LLDB, it doesn't step through single-cycle
     * hardware loops the same way.  So, we just skip them here
     */
    if (env->gpr[HEX_REG_PC] == last_pc) {
        return;
    }
    last_pc = env->gpr[HEX_REG_PC];
    fprintf(f, "General Purpose Registers = {\n");
    for (i = 0; i < 32; i++) {
        print_reg(f, env, i);
    }
    print_reg(f, env, HEX_REG_SA0);
    print_reg(f, env, HEX_REG_LC0);
    print_reg(f, env, HEX_REG_SA1);
    print_reg(f, env, HEX_REG_LC1);
    print_reg(f, env, HEX_REG_M0);
    print_reg(f, env, HEX_REG_M1);
    print_reg(f, env, HEX_REG_USR);
    print_reg(f, env, HEX_REG_P3_0);
#ifdef FIXME
    /*
     * LLDB bug swaps gp and ugp
     * FIXME when the LLDB bug is fixed
     */
    print_reg(f, env, HEX_REG_GP);
    print_reg(f, env, HEX_REG_UGP);
#else
    fprintf(f, "  %s = 0x" TARGET_FMT_lx "\n",
        hexagon_regnames[HEX_REG_GP],
        hack_stack_ptrs(env, env->gpr[HEX_REG_UGP]));
    fprintf(f, "  %s = 0x" TARGET_FMT_lx "\n",
        hexagon_regnames[HEX_REG_UGP],
        hack_stack_ptrs(env, env->gpr[HEX_REG_GP]));
#endif
    print_reg(f, env, HEX_REG_PC);
#ifdef FIXME
    /*
     * Not modelled in qemu, print junk to minimize the diff's
     * with LLDB output
     */
    print_reg(f, env, HEX_REG_CAUSE);
    print_reg(f, env, HEX_REG_BADVA);
    print_reg(f, env, HEX_REG_CS0);
    print_reg(f, env, HEX_REG_CS1);
#else
    fprintf(f, "  cause = 0x000000db\n");
    fprintf(f, "  badva = 0x00000000\n");
    fprintf(f, "  cs0 = 0x00000000\n");
    fprintf(f, "  cs1 = 0x00000000\n");
#endif
    fprintf(f, "}\n");


#if 1
    fprintf(f, "Vector Registers = {\n");
    for (i = 0; i < NUM_VREGS; i++) {
        print_vreg(f, env, i);
    }
    for (i = 0; i < NUM_QREGS; i++) {
        print_qreg(f, env, i);
    }
    fprintf(f, "}\n");
#endif
}

static void hexagon_dump_state(CPUState *cs, FILE *f, int flags)
{
    HexagonCPU *cpu = HEXAGON_CPU(cs);
    CPUHexagonState *env = &cpu->env;

    hexagon_dump(env, f);
}

void hexagon_debug(CPUHexagonState *env)
{
    hexagon_dump(env, stdout);
}

static void hexagon_cpu_set_pc(CPUState *cs, vaddr value)
{
    HexagonCPU *cpu = HEXAGON_CPU(cs);
    CPUHexagonState *env = &cpu->env;
    env->gpr[HEX_REG_PC] = value;
}

static void hexagon_cpu_synchronize_from_tb(CPUState *cs, TranslationBlock *tb)
{
    HexagonCPU *cpu = HEXAGON_CPU(cs);
    CPUHexagonState *env = &cpu->env;
    env->gpr[HEX_REG_PC] = tb->pc;
}

static bool hexagon_cpu_has_work(CPUState *cs)
{
    return true;
}

void restore_state_to_opc(CPUHexagonState *env, TranslationBlock *tb,
                          target_ulong *data)
{
    env->gpr[HEX_REG_PC] = data[0];
}

static void hexagon_cpu_reset(CPUState *cs)
{
    HexagonCPU *cpu = HEXAGON_CPU(cs);
    HexagonCPUClass *mcc = HEXAGON_CPU_GET_CLASS(cpu);

    mcc->parent_reset(cs);
}

static void hexagon_cpu_disas_set_info(CPUState *s, disassemble_info *info)
{
    info->print_insn = print_insn_hexagon;
}

static void hexagon_cpu_realize(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    HexagonCPUClass *mcc = HEXAGON_CPU_GET_CLASS(dev);
    Error *local_err = NULL;

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    qemu_init_vcpu(cs);
    cpu_reset(cs);

    mcc->parent_realize(dev, errp);
}

static void hexagon_cpu_init(Object *obj)
{
    HexagonCPU *cpu = HEXAGON_CPU(obj);

    cpu_set_cpustate_pointers(cpu);
}

static bool hexagon_tlb_fill(CPUState *cs, vaddr address, int size,
                             MMUAccessType access_type, int mmu_idx,
                             bool probe, uintptr_t retaddr)
{
#ifdef CONFIG_USER_ONLY
    switch (access_type) {
    case MMU_INST_FETCH:
        cs->exception_index = HEX_EXCP_FETCH_NO_UPAGE;
        break;
    case MMU_DATA_LOAD:
        cs->exception_index = HEX_EXCP_PRIV_NO_UREAD;
        break;
    case MMU_DATA_STORE:
        cs->exception_index = HEX_EXCP_PRIV_NO_UWRITE;
        break;
    }
    cpu_loop_exit_restore(cs, retaddr);
#else
#error System mode not implemented for Hexagon
#endif
}

static const VMStateDescription vmstate_hexagon_cpu = {
    .name = "cpu",
    .unmigratable = 1,
};

static void hexagon_cpu_class_init(ObjectClass *c, void *data)
{
    HexagonCPUClass *mcc = HEXAGON_CPU_CLASS(c);
    CPUClass *cc = CPU_CLASS(c);
    DeviceClass *dc = DEVICE_CLASS(c);

    mcc->parent_realize = dc->realize;
    dc->realize = hexagon_cpu_realize;

    mcc->parent_reset = cc->reset;
    cc->reset = hexagon_cpu_reset;

    cc->class_by_name = hexagon_cpu_class_by_name;
    cc->has_work = hexagon_cpu_has_work;
    cc->dump_state = hexagon_dump_state;
    cc->set_pc = hexagon_cpu_set_pc;
    cc->synchronize_from_tb = hexagon_cpu_synchronize_from_tb;
    cc->gdb_core_xml_file = "hexagon-core.xml";
    cc->gdb_read_register = hexagon_gdb_read_register;
    cc->gdb_write_register = hexagon_gdb_write_register;
    cc->gdb_num_core_regs = TOTAL_PER_THREAD_REGS + NUM_VREGS + NUM_QREGS;
    cc->gdb_stop_before_watchpoint = true;
    cc->disas_set_info = hexagon_cpu_disas_set_info;
#ifdef CONFIG_TCG
    cc->tcg_initialize = hexagon_translate_init;
    cc->tlb_fill = hexagon_tlb_fill;
#endif
    /* For now, mark unmigratable: */
    cc->vmsd = &vmstate_hexagon_cpu;
}

#define DEFINE_CPU(type_name, initfn)      \
    {                                      \
        .name = type_name,                 \
        .parent = TYPE_HEXAGON_CPU,        \
        .instance_init = initfn            \
    }

static const TypeInfo hexagon_cpu_type_infos[] = {
    {
        .name = TYPE_HEXAGON_CPU,
        .parent = TYPE_CPU,
        .instance_size = sizeof(HexagonCPU),
        .instance_init = hexagon_cpu_init,
        .abstract = true,
        .class_size = sizeof(HexagonCPUClass),
        .class_init = hexagon_cpu_class_init,
    },
    DEFINE_CPU(TYPE_HEXAGON_CPU_V67,              hexagon_v67_cpu_init),
};

DEFINE_TYPES(hexagon_cpu_type_infos)
