/*
 * QEMU Motorola 68k CPU
 *
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
#include "cpu.h"
#include "migration/vmstate.h"
#include "fpu/softfloat.h"

static void m68k_cpu_set_pc(CPUState *cs, vaddr value)
{
    M68kCPU *cpu = M68K_CPU(cs);

    cpu->env.pc = value;
}

static vaddr m68k_cpu_get_pc(CPUState *cs)
{
    M68kCPU *cpu = M68K_CPU(cs);

    return cpu->env.pc;
}

static void m68k_restore_state_to_opc(CPUState *cs,
                                      const TranslationBlock *tb,
                                      const uint64_t *data)
{
    M68kCPU *cpu = M68K_CPU(cs);
    int cc_op = data[1];

    cpu->env.pc = data[0];
    if (cc_op != CC_OP_DYNAMIC) {
        cpu->env.cc_op = cc_op;
    }
}

#ifndef CONFIG_USER_ONLY
static bool m68k_cpu_has_work(CPUState *cs)
{
    return cs->interrupt_request & CPU_INTERRUPT_HARD;
}
#endif /* !CONFIG_USER_ONLY */

static int m68k_cpu_mmu_index(CPUState *cs, bool ifetch)
{
    return cpu_env(cs)->sr & SR_S ? MMU_KERNEL_IDX : MMU_USER_IDX;
}

static void m68k_set_feature(CPUM68KState *env, int feature)
{
    env->features |= BIT_ULL(feature);
}

static void m68k_unset_feature(CPUM68KState *env, int feature)
{
    env->features &= ~BIT_ULL(feature);
}

static void m68k_cpu_reset_hold(Object *obj, ResetType type)
{
    CPUState *cs = CPU(obj);
    M68kCPUClass *mcc = M68K_CPU_GET_CLASS(obj);
    CPUM68KState *env = cpu_env(cs);
    floatx80 nan;
    int i;

    if (mcc->parent_phases.hold) {
        mcc->parent_phases.hold(obj, type);
    }

    memset(env, 0, offsetof(CPUM68KState, end_reset_fields));
#ifdef CONFIG_USER_ONLY
    cpu_m68k_set_sr(env, 0);
#else
    cpu_m68k_set_sr(env, SR_S | SR_I);
#endif
    /*
     * M68000 FAMILY PROGRAMMER'S REFERENCE MANUAL
     * 3.4 FLOATING-POINT INSTRUCTION DETAILS
     * If either operand, but not both operands, of an operation is a
     * nonsignaling NaN, then that NaN is returned as the result. If both
     * operands are nonsignaling NaNs, then the destination operand
     * nonsignaling NaN is returned as the result.
     * If either operand to an operation is a signaling NaN (SNaN), then the
     * SNaN bit is set in the FPSR EXC byte. If the SNaN exception enable bit
     * is set in the FPCR ENABLE byte, then the exception is taken and the
     * destination is not modified. If the SNaN exception enable bit is not
     * set, setting the SNaN bit in the operand to a one converts the SNaN to
     * a nonsignaling NaN. The operation then continues as described in the
     * preceding paragraph for nonsignaling NaNs.
     */
    set_float_2nan_prop_rule(float_2nan_prop_ab, &env->fp_status);
    /* Default NaN: sign bit clear, all frac bits set */
    set_float_default_nan_pattern(0b01111111, &env->fp_status);
    /*
     * m68k-specific floatx80 behaviour:
     *  * default Infinity values have a zero Integer bit
     *  * input Infinities may have the Integer bit either 0 or 1
     *  * pseudo-denormals supported for input and output
     *  * don't raise Invalid for pseudo-NaN/pseudo-Inf/Unnormal
     *
     * With m68k, the explicit integer bit can be zero in the case of:
     * - zeros                (exp == 0, mantissa == 0)
     * - denormalized numbers (exp == 0, mantissa != 0)
     * - unnormalized numbers (exp != 0, exp < 0x7FFF)
     * - infinities           (exp == 0x7FFF, mantissa == 0)
     * - not-a-numbers        (exp == 0x7FFF, mantissa != 0)
     *
     * For infinities and NaNs, the explicit integer bit can be either one or
     * zero.
     *
     * The IEEE 754 standard does not define a zero integer bit. Such a number
     * is an unnormalized number. Hardware does not directly support
     * denormalized and unnormalized numbers, but implicitly supports them by
     * trapping them as unimplemented data types, allowing efficient conversion
     * in software.
     *
     * See "M68000 FAMILY PROGRAMMER’S REFERENCE MANUAL",
     *     "1.6 FLOATING-POINT DATA TYPES"
     *
     * Note though that QEMU's fp emulation does directly handle both
     * denormal and unnormal values, and does not trap to guest software.
     */
    set_floatx80_behaviour(floatx80_default_inf_int_bit_is_zero |
                           floatx80_pseudo_inf_valid |
                           floatx80_pseudo_nan_valid |
                           floatx80_unnormal_valid |
                           floatx80_pseudo_denormal_valid,
                           &env->fp_status);

    nan = floatx80_default_nan(&env->fp_status);
    for (i = 0; i < 8; i++) {
        env->fregs[i].d = nan;
    }
    cpu_m68k_set_fpcr(env, 0);
    env->fpsr = 0;

    /* TODO: We should set PC from the interrupt vector.  */
    env->pc = 0;
}

static void m68k_cpu_disas_set_info(CPUState *s, disassemble_info *info)
{
    info->print_insn = print_insn_m68k;
    info->endian = BFD_ENDIAN_BIG;
    info->mach = 0;
}

/* CPU models */

static ObjectClass *m68k_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *typename;

    typename = g_strdup_printf(M68K_CPU_TYPE_NAME("%s"), cpu_model);
    oc = object_class_by_name(typename);
    g_free(typename);

    return oc;
}

static void m5206_cpu_initfn(Object *obj)
{
    CPUM68KState *env = cpu_env(CPU(obj));

    m68k_set_feature(env, M68K_FEATURE_CF_ISA_A);
    m68k_set_feature(env, M68K_FEATURE_MOVEFROMSR_PRIV);
}

/* Base feature set, including isns. for m68k family */
static void m68000_cpu_initfn(Object *obj)
{
    CPUM68KState *env = cpu_env(CPU(obj));

    m68k_set_feature(env, M68K_FEATURE_M68K);
    m68k_set_feature(env, M68K_FEATURE_USP);
    m68k_set_feature(env, M68K_FEATURE_WORD_INDEX);
    m68k_set_feature(env, M68K_FEATURE_MOVEP);
}

/*
 * Adds BKPT, MOVE-from-SR *now priv instr, and MOVEC, MOVES, RTD,
 *      format+vector in exception frame.
 */
static void m68010_cpu_initfn(Object *obj)
{
    CPUM68KState *env = cpu_env(CPU(obj));

    m68000_cpu_initfn(obj);
    m68k_set_feature(env, M68K_FEATURE_M68010);
    m68k_set_feature(env, M68K_FEATURE_RTD);
    m68k_set_feature(env, M68K_FEATURE_BKPT);
    m68k_set_feature(env, M68K_FEATURE_MOVEC);
    m68k_set_feature(env, M68K_FEATURE_MOVEFROMSR_PRIV);
    m68k_set_feature(env, M68K_FEATURE_EXCEPTION_FORMAT_VEC);
}

/*
 * Adds BFCHG, BFCLR, BFEXTS, BFEXTU, BFFFO, BFINS, BFSET, BFTST, CAS, CAS2,
 *      CHK2, CMP2, DIVSL, DIVUL, EXTB, PACK, TRAPcc, UNPK.
 *
 * 68020/30 only:
 *      CALLM, cpBcc, cpDBcc, cpGEN, cpRESTORE, cpSAVE, cpScc, cpTRAPcc
 */
static void m68020_cpu_initfn(Object *obj)
{
    CPUM68KState *env = cpu_env(CPU(obj));

    m68010_cpu_initfn(obj);
    m68k_unset_feature(env, M68K_FEATURE_M68010);
    m68k_set_feature(env, M68K_FEATURE_M68020);
    m68k_set_feature(env, M68K_FEATURE_QUAD_MULDIV);
    m68k_set_feature(env, M68K_FEATURE_BRAL);
    m68k_set_feature(env, M68K_FEATURE_BCCL);
    m68k_set_feature(env, M68K_FEATURE_BITFIELD);
    m68k_set_feature(env, M68K_FEATURE_EXT_FULL);
    m68k_set_feature(env, M68K_FEATURE_SCALED_INDEX);
    m68k_set_feature(env, M68K_FEATURE_LONG_MULDIV);
    m68k_set_feature(env, M68K_FEATURE_FPU);
    m68k_set_feature(env, M68K_FEATURE_CAS);
    m68k_set_feature(env, M68K_FEATURE_CHK2);
    m68k_set_feature(env, M68K_FEATURE_MSP);
    m68k_set_feature(env, M68K_FEATURE_UNALIGNED_DATA);
    m68k_set_feature(env, M68K_FEATURE_TRAPCC);
}

/*
 * Adds: PFLUSH (*5)
 * 68030 Only: PFLUSHA (*5), PLOAD (*5), PMOVE
 * 68030/40 Only: PTEST
 *
 * NOTES:
 *  5. Not valid on MC68EC030
 */
static void m68030_cpu_initfn(Object *obj)
{
    CPUM68KState *env = cpu_env(CPU(obj));

    m68020_cpu_initfn(obj);
    m68k_unset_feature(env, M68K_FEATURE_M68020);
    m68k_set_feature(env, M68K_FEATURE_M68030);
}

/*
 * Adds: CINV, CPUSH
 * Adds all with Note *2: FABS, FSABS, FDABS, FADD, FSADD, FDADD, FBcc, FCMP,
 *                        FDBcc, FDIV, FSDIV, FDDIV, FMOVE, FSMOVE, FDMOVE,
 *                        FMOVEM, FMUL, FSMUL, FDMUL, FNEG, FSNEG, FDNEG, FNOP,
 *                        FRESTORE, FSAVE, FScc, FSQRT, FSSQRT, FDSQRT, FSUB,
 *                        FSSUB, FDSUB, FTRAPcc, FTST
 *
 * Adds with Notes *2, and *3: FACOS, FASIN, FATAN, FATANH, FCOS, FCOSH, FETOX,
 *                             FETOXM, FGETEXP, FGETMAN, FINT, FINTRZ, FLOG10,
 *                             FLOG2, FLOGN, FLOGNP1, FMOD, FMOVECR, FREM,
 *                             FSCALE, FSGLDIV, FSGLMUL, FSIN, FSINCOS, FSINH,
 *                             FTAN, FTANH, FTENTOX, FTWOTOX
 * NOTES:
 * 2. Not applicable to the MC68EC040, MC68LC040, MC68EC060, and MC68LC060.
 * 3. These are software-supported instructions on the MC68040 and MC68060.
 */
static void m68040_cpu_initfn(Object *obj)
{
    CPUM68KState *env = cpu_env(CPU(obj));

    m68030_cpu_initfn(obj);
    m68k_unset_feature(env, M68K_FEATURE_M68030);
    m68k_set_feature(env, M68K_FEATURE_M68040);
}

/*
 * Adds: PLPA
 * Adds all with Note *2: CAS, CAS2, MULS, MULU, CHK2, CMP2, DIVS, DIVU
 * All Fxxxx instructions are as per m68040 with exception to; FMOVEM NOTE3
 *
 * Does NOT implement MOVEP
 *
 * NOTES:
 * 2. Not applicable to the MC68EC040, MC68LC040, MC68EC060, and MC68LC060.
 * 3. These are software-supported instructions on the MC68040 and MC68060.
 */
static void m68060_cpu_initfn(Object *obj)
{
    CPUM68KState *env = cpu_env(CPU(obj));

    m68040_cpu_initfn(obj);
    m68k_unset_feature(env, M68K_FEATURE_M68040);
    m68k_set_feature(env, M68K_FEATURE_M68060);
    m68k_unset_feature(env, M68K_FEATURE_MOVEP);

    /* Implemented as a software feature */
    m68k_unset_feature(env, M68K_FEATURE_QUAD_MULDIV);
}

static void m5208_cpu_initfn(Object *obj)
{
    CPUM68KState *env = cpu_env(CPU(obj));

    m68k_set_feature(env, M68K_FEATURE_CF_ISA_A);
    m68k_set_feature(env, M68K_FEATURE_CF_ISA_APLUSC);
    m68k_set_feature(env, M68K_FEATURE_BRAL);
    m68k_set_feature(env, M68K_FEATURE_CF_EMAC);
    m68k_set_feature(env, M68K_FEATURE_USP);
    m68k_set_feature(env, M68K_FEATURE_MOVEFROMSR_PRIV);
}

static void cfv4e_cpu_initfn(Object *obj)
{
    CPUM68KState *env = cpu_env(CPU(obj));

    m68k_set_feature(env, M68K_FEATURE_CF_ISA_A);
    m68k_set_feature(env, M68K_FEATURE_CF_ISA_B);
    m68k_set_feature(env, M68K_FEATURE_BRAL);
    m68k_set_feature(env, M68K_FEATURE_CF_FPU);
    m68k_set_feature(env, M68K_FEATURE_CF_EMAC);
    m68k_set_feature(env, M68K_FEATURE_USP);
    m68k_set_feature(env, M68K_FEATURE_MOVEFROMSR_PRIV);
}

static void any_cpu_initfn(Object *obj)
{
    CPUM68KState *env = cpu_env(CPU(obj));

    m68k_set_feature(env, M68K_FEATURE_CF_ISA_A);
    m68k_set_feature(env, M68K_FEATURE_CF_ISA_B);
    m68k_set_feature(env, M68K_FEATURE_CF_ISA_APLUSC);
    m68k_set_feature(env, M68K_FEATURE_BRAL);
    m68k_set_feature(env, M68K_FEATURE_CF_FPU);
    /*
     * MAC and EMAC are mututally exclusive, so pick EMAC.
     * It's mostly backwards compatible.
     */
    m68k_set_feature(env, M68K_FEATURE_CF_EMAC);
    m68k_set_feature(env, M68K_FEATURE_CF_EMAC_B);
    m68k_set_feature(env, M68K_FEATURE_USP);
    m68k_set_feature(env, M68K_FEATURE_EXT_FULL);
    m68k_set_feature(env, M68K_FEATURE_WORD_INDEX);
    m68k_set_feature(env, M68K_FEATURE_MOVEFROMSR_PRIV);
}

static void m68k_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    M68kCPU *cpu = M68K_CPU(dev);
    M68kCPUClass *mcc = M68K_CPU_GET_CLASS(dev);
    Error *local_err = NULL;

    register_m68k_insns(&cpu->env);

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    m68k_cpu_init_gdb(cpu);

    cpu_reset(cs);
    qemu_init_vcpu(cs);

    mcc->parent_realize(dev, errp);
}

#if !defined(CONFIG_USER_ONLY)
static bool fpu_needed(void *opaque)
{
    M68kCPU *s = opaque;

    return m68k_feature(&s->env, M68K_FEATURE_CF_FPU) ||
           m68k_feature(&s->env, M68K_FEATURE_FPU);
}

typedef struct m68k_FPReg_tmp {
    FPReg *parent;
    uint64_t tmp_mant;
    uint16_t tmp_exp;
} m68k_FPReg_tmp;

static void cpu_get_fp80(uint64_t *pmant, uint16_t *pexp, floatx80 f)
{
    CPU_LDoubleU temp;

    temp.d = f;
    *pmant = temp.l.lower;
    *pexp = temp.l.upper;
}

static floatx80 cpu_set_fp80(uint64_t mant, uint16_t upper)
{
    CPU_LDoubleU temp;

    temp.l.upper = upper;
    temp.l.lower = mant;
    return temp.d;
}

static int freg_pre_save(void *opaque)
{
    m68k_FPReg_tmp *tmp = opaque;

    cpu_get_fp80(&tmp->tmp_mant, &tmp->tmp_exp, tmp->parent->d);

    return 0;
}

static int freg_post_load(void *opaque, int version)
{
    m68k_FPReg_tmp *tmp = opaque;

    tmp->parent->d = cpu_set_fp80(tmp->tmp_mant, tmp->tmp_exp);

    return 0;
}

static const VMStateDescription vmstate_freg_tmp = {
    .name = "freg_tmp",
    .post_load = freg_post_load,
    .pre_save  = freg_pre_save,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(tmp_mant, m68k_FPReg_tmp),
        VMSTATE_UINT16(tmp_exp, m68k_FPReg_tmp),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_freg = {
    .name = "freg",
    .fields = (const VMStateField[]) {
        VMSTATE_WITH_TMP(FPReg, m68k_FPReg_tmp, vmstate_freg_tmp),
        VMSTATE_END_OF_LIST()
    }
};

static int fpu_pre_save(void *opaque)
{
    M68kCPU *s = opaque;

    s->env.fpsr = cpu_m68k_get_fpsr(&s->env);
    return 0;
}

static int fpu_post_load(void *opaque, int version)
{
    M68kCPU *s = opaque;

    cpu_m68k_set_fpsr(&s->env, s->env.fpsr);
    return 0;
}

const VMStateDescription vmmstate_fpu = {
    .name = "cpu/fpu",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = fpu_needed,
    .pre_save = fpu_pre_save,
    .post_load = fpu_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(env.fpcr, M68kCPU),
        VMSTATE_UINT32(env.fpsr, M68kCPU),
        VMSTATE_STRUCT_ARRAY(env.fregs, M68kCPU, 8, 0, vmstate_freg, FPReg),
        VMSTATE_STRUCT(env.fp_result, M68kCPU, 0, vmstate_freg, FPReg),
        VMSTATE_END_OF_LIST()
    }
};

static bool cf_spregs_needed(void *opaque)
{
    M68kCPU *s = opaque;

    return m68k_feature(&s->env, M68K_FEATURE_CF_ISA_A);
}

const VMStateDescription vmstate_cf_spregs = {
    .name = "cpu/cf_spregs",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = cf_spregs_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64_ARRAY(env.macc, M68kCPU, 4),
        VMSTATE_UINT32(env.macsr, M68kCPU),
        VMSTATE_UINT32(env.mac_mask, M68kCPU),
        VMSTATE_UINT32(env.rambar0, M68kCPU),
        VMSTATE_UINT32(env.mbar, M68kCPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool cpu_68040_mmu_needed(void *opaque)
{
    M68kCPU *s = opaque;

    return m68k_feature(&s->env, M68K_FEATURE_M68040);
}

const VMStateDescription vmstate_68040_mmu = {
    .name = "cpu/68040_mmu",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = cpu_68040_mmu_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(env.mmu.ar, M68kCPU),
        VMSTATE_UINT32(env.mmu.ssw, M68kCPU),
        VMSTATE_UINT16(env.mmu.tcr, M68kCPU),
        VMSTATE_UINT32(env.mmu.urp, M68kCPU),
        VMSTATE_UINT32(env.mmu.srp, M68kCPU),
        VMSTATE_BOOL(env.mmu.fault, M68kCPU),
        VMSTATE_UINT32_ARRAY(env.mmu.ttr, M68kCPU, 4),
        VMSTATE_UINT32(env.mmu.mmusr, M68kCPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool cpu_68040_spregs_needed(void *opaque)
{
    M68kCPU *s = opaque;

    return m68k_feature(&s->env, M68K_FEATURE_M68040);
}

const VMStateDescription vmstate_68040_spregs = {
    .name = "cpu/68040_spregs",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = cpu_68040_spregs_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(env.vbr, M68kCPU),
        VMSTATE_UINT32(env.cacr, M68kCPU),
        VMSTATE_UINT32(env.sfc, M68kCPU),
        VMSTATE_UINT32(env.dfc, M68kCPU),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_m68k_cpu = {
    .name = "cpu",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(env.dregs, M68kCPU, 8),
        VMSTATE_UINT32_ARRAY(env.aregs, M68kCPU, 8),
        VMSTATE_UINT32(env.pc, M68kCPU),
        VMSTATE_UINT32(env.sr, M68kCPU),
        VMSTATE_INT32(env.current_sp, M68kCPU),
        VMSTATE_UINT32_ARRAY(env.sp, M68kCPU, 3),
        VMSTATE_UINT32(env.cc_op, M68kCPU),
        VMSTATE_UINT32(env.cc_x, M68kCPU),
        VMSTATE_UINT32(env.cc_n, M68kCPU),
        VMSTATE_UINT32(env.cc_v, M68kCPU),
        VMSTATE_UINT32(env.cc_c, M68kCPU),
        VMSTATE_UINT32(env.cc_z, M68kCPU),
        VMSTATE_INT32(env.pending_vector, M68kCPU),
        VMSTATE_INT32(env.pending_level, M68kCPU),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &vmmstate_fpu,
        &vmstate_cf_spregs,
        &vmstate_68040_mmu,
        &vmstate_68040_spregs,
        NULL
    },
};

#include "hw/core/sysemu-cpu-ops.h"

static const struct SysemuCPUOps m68k_sysemu_ops = {
    .has_work = m68k_cpu_has_work,
    .get_phys_page_debug = m68k_cpu_get_phys_page_debug,
};
#endif /* !CONFIG_USER_ONLY */

#include "accel/tcg/cpu-ops.h"

static const TCGCPUOps m68k_tcg_ops = {
    /* MTTCG not yet supported: require strict ordering */
    .guest_default_memory_order = TCG_MO_ALL,
    .mttcg_supported = false,

    .initialize = m68k_tcg_init,
    .translate_code = m68k_translate_code,
    .restore_state_to_opc = m68k_restore_state_to_opc,
    .mmu_index = m68k_cpu_mmu_index,

#ifndef CONFIG_USER_ONLY
    .tlb_fill = m68k_cpu_tlb_fill,
    .cpu_exec_interrupt = m68k_cpu_exec_interrupt,
    .cpu_exec_halt = m68k_cpu_has_work,
    .do_interrupt = m68k_cpu_do_interrupt,
    .do_transaction_failed = m68k_cpu_transaction_failed,
#endif /* !CONFIG_USER_ONLY */
};

static void m68k_cpu_class_init(ObjectClass *c, void *data)
{
    M68kCPUClass *mcc = M68K_CPU_CLASS(c);
    CPUClass *cc = CPU_CLASS(c);
    DeviceClass *dc = DEVICE_CLASS(c);
    ResettableClass *rc = RESETTABLE_CLASS(c);

    device_class_set_parent_realize(dc, m68k_cpu_realizefn,
                                    &mcc->parent_realize);
    resettable_class_set_parent_phases(rc, NULL, m68k_cpu_reset_hold, NULL,
                                       &mcc->parent_phases);

    cc->class_by_name = m68k_cpu_class_by_name;
    cc->dump_state = m68k_cpu_dump_state;
    cc->set_pc = m68k_cpu_set_pc;
    cc->get_pc = m68k_cpu_get_pc;
    cc->gdb_read_register = m68k_cpu_gdb_read_register;
    cc->gdb_write_register = m68k_cpu_gdb_write_register;
#if !defined(CONFIG_USER_ONLY)
    dc->vmsd = &vmstate_m68k_cpu;
    cc->sysemu_ops = &m68k_sysemu_ops;
#endif
    cc->disas_set_info = m68k_cpu_disas_set_info;

    cc->tcg_ops = &m68k_tcg_ops;
}

static void m68k_cpu_class_init_cf_core(ObjectClass *c, void *data)
{
    CPUClass *cc = CPU_CLASS(c);

    cc->gdb_core_xml_file = "cf-core.xml";
}

#define DEFINE_M68K_CPU_TYPE_CF(model)               \
    {                                                \
        .name = M68K_CPU_TYPE_NAME(#model),          \
        .instance_init = model##_cpu_initfn,         \
        .parent = TYPE_M68K_CPU,                     \
        .class_init = m68k_cpu_class_init_cf_core    \
    }

static void m68k_cpu_class_init_m68k_core(ObjectClass *c, void *data)
{
    CPUClass *cc = CPU_CLASS(c);

    cc->gdb_core_xml_file = "m68k-core.xml";
}

#define DEFINE_M68K_CPU_TYPE_M68K(model)             \
    {                                                \
        .name = M68K_CPU_TYPE_NAME(#model),          \
        .instance_init = model##_cpu_initfn,         \
        .parent = TYPE_M68K_CPU,                     \
        .class_init = m68k_cpu_class_init_m68k_core  \
    }

static const TypeInfo m68k_cpus_type_infos[] = {
    { /* base class should be registered first */
        .name = TYPE_M68K_CPU,
        .parent = TYPE_CPU,
        .instance_size = sizeof(M68kCPU),
        .instance_align = __alignof(M68kCPU),
        .abstract = true,
        .class_size = sizeof(M68kCPUClass),
        .class_init = m68k_cpu_class_init,
    },
    DEFINE_M68K_CPU_TYPE_M68K(m68000),
    DEFINE_M68K_CPU_TYPE_M68K(m68010),
    DEFINE_M68K_CPU_TYPE_M68K(m68020),
    DEFINE_M68K_CPU_TYPE_M68K(m68030),
    DEFINE_M68K_CPU_TYPE_M68K(m68040),
    DEFINE_M68K_CPU_TYPE_M68K(m68060),
    DEFINE_M68K_CPU_TYPE_CF(m5206),
    DEFINE_M68K_CPU_TYPE_CF(m5208),
    DEFINE_M68K_CPU_TYPE_CF(cfv4e),
    DEFINE_M68K_CPU_TYPE_CF(any),
};

DEFINE_TYPES(m68k_cpus_type_infos)
