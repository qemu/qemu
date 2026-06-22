/*
 *  Copyright(c) 2019-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/qemu-print.h"
#include "cpu.h"
#include "internal.h"
#include "exec/translation-block.h"
#include "qapi/error.h"
#include "hw/core/qdev-properties.h"
#include "fpu/softfloat-helpers.h"
#include "hw/hexagon/hexagon_tlb.h"
#include "tcg/tcg.h"
#include "exec/gdbstub.h"
#include "accel/tcg/cpu-ops.h"
#include "cpu_helper.h"
#include "hex_mmu.h"

#ifndef CONFIG_USER_ONLY
#include "sys_macros.h"
#include "accel/tcg/cpu-ldst.h"
#endif

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

    return oc;
}

static const Property hexagon_cpu_properties[] = {
#ifndef CONFIG_USER_ONLY
    DEFINE_PROP_LINK("tlb", HexagonCPU, tlb, TYPE_HEXAGON_TLB,
                     HexagonTLBState *),
    DEFINE_PROP_UINT32("htid", HexagonCPU, htid, 0),
#endif
    DEFINE_PROP_BOOL("lldb-compat", HexagonCPU, lldb_compat, false),
    DEFINE_PROP_UNSIGNED("lldb-stack-adjust", HexagonCPU, lldb_stack_adjust, 0,
                         qdev_prop_uint32, target_ulong),
    DEFINE_PROP_BOOL("short-circuit", HexagonCPU, short_circuit, true),
};

const char * const hexagon_regnames[TOTAL_PER_THREAD_REGS] = {
   "r0", "r1",  "r2",  "r3",  "r4",   "r5",  "r6",  "r7",
   "r8", "r9",  "r10", "r11", "r12",  "r13", "r14", "r15",
  "r16", "r17", "r18", "r19", "r20",  "r21", "r22", "r23",
  "r24", "r25", "r26", "r27", "r28",  "r29", "r30", "r31",
  "sa0", "lc0", "sa1", "lc1", "p3_0", "c5",  "m0",  "m1",
  "usr", "pc",  "ugp", "gp",  "cs0",  "cs1", "c14", "c15",
  "c16", "c17", "c18", "c19", "pkt_cnt",  "insn_cnt", "hvx_cnt", "c23",
  "c24", "c25", "c26", "c27", "c28",  "c29", "c30", "c31",
};

/*
 * One of the main debugging techniques is to use "-d cpu" and compare against
 * LLDB output when single stepping.  However, the target and qemu put the
 * stacks at different locations.  This is used to compensate so the diff is
 * cleaner.
 */
static target_ulong adjust_stack_ptrs(CPUHexagonState *env, target_ulong addr)
{
    HexagonCPU *cpu = env_archcpu(env);
    target_ulong stack_adjust = cpu->lldb_stack_adjust;
    target_ulong stack_start = env->stack_start;
    target_ulong stack_size = 0x10000;

    if (stack_adjust == 0) {
        return addr;
    }

    if (stack_start + 0x1000 >= addr && addr >= (stack_start - stack_size)) {
        return addr - stack_adjust;
    }
    return addr;
}

/* HEX_REG_P3_0_ALIASED (aka C4) is an alias for the predicate registers */
static target_ulong read_p3_0(CPUHexagonState *env)
{
    int32_t control_reg = 0;
    int i;
    for (i = NUM_PREGS - 1; i >= 0; i--) {
        control_reg <<= 8;
        control_reg |= env->pred[i] & 0xff;
    }
    return control_reg;
}

static void print_reg(FILE *f, CPUHexagonState *env, int regnum)
{
    target_ulong value;

    if (regnum == HEX_REG_P3_0_ALIASED) {
        value = read_p3_0(env);
    } else {
        value = regnum < 32 ? adjust_stack_ptrs(env, env->gpr[regnum])
                            : env->gpr[regnum];
    }

    qemu_fprintf(f, "  %s = 0x" TARGET_FMT_lx "\n",
                 hexagon_regnames[regnum], value);
}

#ifndef CONFIG_USER_ONLY
static void print_t_sreg(FILE *f, const CPUHexagonState *env, int regnum)
{
    qemu_fprintf(f, "  %s = 0x" TARGET_FMT_lx "\n",
                 hexagon_sregnames[regnum], env->t_sreg[regnum]);
}
#endif

static void print_vreg(FILE *f, CPUHexagonState *env, int regnum,
                       bool skip_if_zero)
{
    if (skip_if_zero) {
        bool nonzero_found = false;
        for (int i = 0; i < MAX_VEC_SIZE_BYTES; i++) {
            if (env->VRegs[regnum].ub[i] != 0) {
                nonzero_found = true;
                break;
            }
        }
        if (!nonzero_found) {
            return;
        }
    }

    qemu_fprintf(f, "  v%d = ( ", regnum);
    qemu_fprintf(f, "0x%02x", env->VRegs[regnum].ub[MAX_VEC_SIZE_BYTES - 1]);
    for (int i = MAX_VEC_SIZE_BYTES - 2; i >= 0; i--) {
        qemu_fprintf(f, ", 0x%02x", env->VRegs[regnum].ub[i]);
    }
    qemu_fprintf(f, " )\n");
}

void hexagon_debug_vreg(CPUHexagonState *env, int regnum)
{
    print_vreg(stdout, env, regnum, false);
}

static void print_qreg(FILE *f, CPUHexagonState *env, int regnum,
                       bool skip_if_zero)
{
    if (skip_if_zero) {
        bool nonzero_found = false;
        for (int i = 0; i < MAX_VEC_SIZE_BYTES / 8; i++) {
            if (env->QRegs[regnum].ub[i] != 0) {
                nonzero_found = true;
                break;
            }
        }
        if (!nonzero_found) {
            return;
        }
    }

    qemu_fprintf(f, "  q%d = ( ", regnum);
    qemu_fprintf(f, "0x%02x",
                 env->QRegs[regnum].ub[MAX_VEC_SIZE_BYTES / 8 - 1]);
    for (int i = MAX_VEC_SIZE_BYTES / 8 - 2; i >= 0; i--) {
        qemu_fprintf(f, ", 0x%02x", env->QRegs[regnum].ub[i]);
    }
    qemu_fprintf(f, " )\n");
}

void hexagon_debug_qreg(CPUHexagonState *env, int regnum)
{
    print_qreg(stdout, env, regnum, false);
}

static void hexagon_dump(CPUHexagonState *env, FILE *f, int flags)
{
    HexagonCPU *cpu = env_archcpu(env);

    if (cpu->lldb_compat) {
        /*
         * When comparing with LLDB, it doesn't step through single-cycle
         * hardware loops the same way.  So, we just skip them here
         */
        if (env->gpr[HEX_REG_PC] == env->last_pc_dumped) {
            return;
        }
        env->last_pc_dumped = env->gpr[HEX_REG_PC];
    }

    qemu_fprintf(f, "General Purpose Registers = {\n");
    for (int i = 0; i < 32; i++) {
        print_reg(f, env, i);
    }
    print_reg(f, env, HEX_REG_SA0);
    print_reg(f, env, HEX_REG_LC0);
    print_reg(f, env, HEX_REG_SA1);
    print_reg(f, env, HEX_REG_LC1);
    print_reg(f, env, HEX_REG_M0);
    print_reg(f, env, HEX_REG_M1);
    print_reg(f, env, HEX_REG_USR);
    print_reg(f, env, HEX_REG_P3_0_ALIASED);
    print_reg(f, env, HEX_REG_GP);
    print_reg(f, env, HEX_REG_UGP);
    print_reg(f, env, HEX_REG_PC);
#ifdef CONFIG_USER_ONLY
    /*
     * Not modelled in user mode, print junk to minimize the diff's
     * with LLDB output
     */
    qemu_fprintf(f, "  cause = 0x000000db\n");
    qemu_fprintf(f, "  badva = 0x00000000\n");
    qemu_fprintf(f, "  cs0 = 0x00000000\n");
    qemu_fprintf(f, "  cs1 = 0x00000000\n");
#else
    print_t_sreg(f, env, HEX_SREG_BADVA);
    print_reg(f, env, HEX_REG_CS0);
    print_reg(f, env, HEX_REG_CS1);
#endif
    qemu_fprintf(f, "}\n");

    if (flags & CPU_DUMP_FPU) {
        qemu_fprintf(f, "Vector Registers = {\n");
        for (int i = 0; i < NUM_VREGS; i++) {
            print_vreg(f, env, i, true);
        }
        for (int i = 0; i < NUM_QREGS; i++) {
            print_qreg(f, env, i, true);
        }
        qemu_fprintf(f, "}\n");
    }
}

static void hexagon_dump_state(CPUState *cs, FILE *f, int flags)
{
    hexagon_dump(cpu_env(cs), f, flags);
}

void hexagon_debug(CPUHexagonState *env)
{
    hexagon_dump(env, stdout, CPU_DUMP_FPU);
}

static void hexagon_cpu_set_pc(CPUState *cs, vaddr value)
{
    cpu_env(cs)->gpr[HEX_REG_PC] = value;
}

static vaddr hexagon_cpu_get_pc(CPUState *cs)
{
    return cpu_env(cs)->gpr[HEX_REG_PC];
}

static TCGTBCPUState hexagon_get_tb_cpu_state(CPUState *cs)
{
    CPUHexagonState *env = cpu_env(cs);
    vaddr pc = env->gpr[HEX_REG_PC];
    uint32_t hex_flags = 0;

    if (pc == env->gpr[HEX_REG_SA0]) {
        hex_flags = FIELD_DP32(hex_flags, TB_FLAGS, IS_TIGHT_LOOP, 1);
    }
    if (pc & PCALIGN_MASK) {
        hexagon_raise_exception_err(env, HEX_CAUSE_PC_NOT_ALIGNED, 0);
    }

#ifndef CONFIG_USER_ONLY
    hex_flags = FIELD_DP32(hex_flags, TB_FLAGS, MMU_INDEX,
                           cpu_mmu_index(env_cpu(env), false));
    hex_flags = FIELD_DP32(hex_flags, TB_FLAGS, PCYCLE_ENABLED, 1);
#else
    hex_flags = FIELD_DP32(hex_flags, TB_FLAGS, MMU_INDEX, MMU_USER_IDX);
#endif

    return (TCGTBCPUState){ .pc = pc, .flags = hex_flags };
}

static void hexagon_cpu_synchronize_from_tb(CPUState *cs,
                                            const TranslationBlock *tb)
{
    tcg_debug_assert(!tcg_cflags_has(cs, CF_PCREL));
    cpu_env(cs)->gpr[HEX_REG_PC] = tb->pc;
}

static void hexagon_restore_state_to_opc(CPUState *cs,
                                         const TranslationBlock *tb,
                                         const uint64_t *data)
{
    cpu_env(cs)->gpr[HEX_REG_PC] = data[0];
}


static void hexagon_cpu_reset_hold(Object *obj, ResetType type)
{
    CPUState *cs = CPU(obj);
    HexagonCPUClass *mcc = HEXAGON_CPU_GET_CLASS(obj);
    CPUHexagonState *env = cpu_env(cs);
#ifndef CONFIG_USER_ONLY
    HexagonCPU *cpu = HEXAGON_CPU(cs);
#endif

    if (mcc->parent_phases.hold) {
        mcc->parent_phases.hold(obj, type);
    }

    set_default_nan_mode(1, &env->fp_status);
    set_float_detect_tininess(float_tininess_before_rounding, &env->fp_status);
    /* Default NaN value: sign bit set, all frac bits set */
    set_float_default_nan_pattern(0b11111111, &env->fp_status);
#ifndef CONFIG_USER_ONLY
    memset(env->t_sreg, 0, sizeof(uint32_t) * NUM_SREGS);
    memset(env->greg, 0, sizeof(uint32_t) * NUM_GREGS);
    env->wait_next_pc = 0;
    env->tlb_lock_state = HEX_LOCK_UNLOCKED;
    env->k0_lock_state = HEX_LOCK_UNLOCKED;
    env->tlb_lock_count = 0;
    env->k0_lock_count = 0;
    env->next_PC = 0;

    env->t_sreg[HEX_SREG_HTID] = cpu->htid;
    env->threadId = cpu->htid;
#endif
    env->cause_code = HEX_EVENT_NONE;
}

static void hexagon_cpu_disas_set_info(const CPUState *cs,
                                       disassemble_info *info)
{
    const HexagonCPU *cpu = HEXAGON_CPU(cs);
    info->print_insn = print_insn_hexagon;
    info->endian = BFD_ENDIAN_LITTLE;
    info->target_info = HEXAGON_CPU_GET_CLASS(cpu)->hex_def;
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

    gdb_register_coprocessor(cs, hexagon_hvx_gdb_read_register,
                             hexagon_hvx_gdb_write_register,
                             gdb_find_static_feature("hexagon-hvx.xml"));

#ifndef CONFIG_USER_ONLY
    if (!HEXAGON_CPU(dev)->tlb) {
        error_setg(errp, "hexagon cpu requires 'tlb' link property to be set");
        return;
    }
#endif

    qemu_init_vcpu(cs);

    cpu_reset(cs);
    mcc->parent_realize(dev, errp);
}

static int hexagon_cpu_mmu_index(CPUState *cs, bool ifetch)
{
    return MMU_USER_IDX;
}

static void hexagon_cpu_init(Object *obj)
{
}

static const TCGCPUOps hexagon_tcg_ops = {
    /* MTTCG not yet supported: require strict ordering */
    .guest_default_memory_order = TCG_MO_ALL,
    .mttcg_supported = false,
    .initialize = hexagon_translate_init,
    .translate_code = hexagon_translate_code,
    .get_tb_cpu_state = hexagon_get_tb_cpu_state,
    .synchronize_from_tb = hexagon_cpu_synchronize_from_tb,
    .restore_state_to_opc = hexagon_restore_state_to_opc,
    .mmu_index = hexagon_cpu_mmu_index,
};

static void hexagon_cpu_class_init(ObjectClass *c, const void *data)
{
    HexagonCPUClass *mcc = HEXAGON_CPU_CLASS(c);
    CPUClass *cc = CPU_CLASS(c);
    DeviceClass *dc = DEVICE_CLASS(c);
    ResettableClass *rc = RESETTABLE_CLASS(c);

    device_class_set_parent_realize(dc, hexagon_cpu_realize,
                                    &mcc->parent_realize);

    device_class_set_props(dc, hexagon_cpu_properties);
    resettable_class_set_parent_phases(rc, NULL, hexagon_cpu_reset_hold, NULL,
                                       &mcc->parent_phases);

    cc->class_by_name = hexagon_cpu_class_by_name;
    cc->dump_state = hexagon_dump_state;
    cc->set_pc = hexagon_cpu_set_pc;
    cc->get_pc = hexagon_cpu_get_pc;
    cc->gdb_read_register = hexagon_gdb_read_register;
    cc->gdb_write_register = hexagon_gdb_write_register;
    cc->gdb_stop_before_watchpoint = true;
    cc->gdb_core_xml_file = "hexagon-core.xml";
    cc->disas_set_info = hexagon_cpu_disas_set_info;
#ifndef CONFIG_USER_ONLY
    dc->vmsd = &vmstate_hexagon_cpu;
#endif
    cc->tcg_ops = &hexagon_tcg_ops;
}

static void hexagon_cpu_class_base_init(ObjectClass *c, const void *data)
{
    HexagonCPUClass *mcc = HEXAGON_CPU_CLASS(c);
    /* Make sure all CPU models define a HexagonCPUDef */
    g_assert(!object_class_is_abstract(c) && data != NULL);
    mcc->hex_def = data;
}

#define DEFINE_CPU(type_name, version)         \
    {                                          \
        .name = type_name,                     \
        .parent = TYPE_HEXAGON_CPU,            \
        .class_data = &(const HexagonCPUDef) { \
            .hex_version = version,            \
        }                                      \
    }

static const TypeInfo hexagon_cpu_type_infos[] = {
    {
        .name = TYPE_HEXAGON_CPU,
        .parent = TYPE_CPU,
        .instance_size = sizeof(HexagonCPU),
        .instance_align = __alignof(HexagonCPU),
        .instance_init = hexagon_cpu_init,
        .abstract = true,
        .class_size = sizeof(HexagonCPUClass),
        .class_init = hexagon_cpu_class_init,
        .class_base_init = hexagon_cpu_class_base_init,
    },
    DEFINE_CPU(TYPE_HEXAGON_CPU_V5,               HEX_VER_V5),
    DEFINE_CPU(TYPE_HEXAGON_CPU_V55,              HEX_VER_V55),
    DEFINE_CPU(TYPE_HEXAGON_CPU_V60,              HEX_VER_V60),
    DEFINE_CPU(TYPE_HEXAGON_CPU_V61,              HEX_VER_V61),
    DEFINE_CPU(TYPE_HEXAGON_CPU_V62,              HEX_VER_V62),
    DEFINE_CPU(TYPE_HEXAGON_CPU_V65,              HEX_VER_V65),
    DEFINE_CPU(TYPE_HEXAGON_CPU_V66,              HEX_VER_V66),
    DEFINE_CPU(TYPE_HEXAGON_CPU_V67,              HEX_VER_V67),
    DEFINE_CPU(TYPE_HEXAGON_CPU_V68,              HEX_VER_V68),
    DEFINE_CPU(TYPE_HEXAGON_CPU_V69,              HEX_VER_V69),
    DEFINE_CPU(TYPE_HEXAGON_CPU_V71,              HEX_VER_V71),
    DEFINE_CPU(TYPE_HEXAGON_CPU_V73,              HEX_VER_V73),
};

DEFINE_TYPES(hexagon_cpu_type_infos)
