/*
 *  Xilinx MicroBlaze emulation for qemu: main translation routines.
 *
 *  Copyright (c) 2009 Edgar E. Iglesias.
 *  Copyright (c) 2009-2012 PetaLogix Qld Pty Ltd.
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
#include "disas/disas.h"
#include "exec/exec-all.h"
#include "tcg/tcg-op.h"
#include "exec/helper-proto.h"
#include "microblaze-decode.h"
#include "exec/cpu_ldst.h"
#include "exec/helper-gen.h"
#include "exec/translator.h"
#include "qemu/qemu-print.h"

#include "trace-tcg.h"
#include "exec/log.h"

#define EXTRACT_FIELD(src, start, end) \
            (((src) >> start) & ((1 << (end - start + 1)) - 1))

/* is_jmp field values */
#define DISAS_JUMP    DISAS_TARGET_0 /* only pc was modified dynamically */
#define DISAS_UPDATE  DISAS_TARGET_1 /* cpu state was modified dynamically */

static TCGv_i32 cpu_R[32];
static TCGv_i32 cpu_pc;
static TCGv_i32 cpu_msr;
static TCGv_i32 cpu_msr_c;
static TCGv_i32 cpu_imm;
static TCGv_i32 cpu_btaken;
static TCGv_i32 cpu_btarget;
static TCGv_i32 cpu_iflags;
static TCGv cpu_res_addr;
static TCGv_i32 cpu_res_val;

#include "exec/gen-icount.h"

/* This is the state at translation time.  */
typedef struct DisasContext {
    DisasContextBase base;
    MicroBlazeCPU *cpu;

    /* TCG op of the current insn_start.  */
    TCGOp *insn_start;

    TCGv_i32 r0;
    bool r0_set;

    /* Decoder.  */
    int type_b;
    uint32_t ir;
    uint32_t ext_imm;
    uint8_t opcode;
    uint8_t rd, ra, rb;
    uint16_t imm;

    unsigned int cpustate_changed;
    unsigned int tb_flags;
    unsigned int tb_flags_to_set;
    int mem_index;

#define JMP_NOJMP     0
#define JMP_DIRECT    1
#define JMP_DIRECT_CC 2
#define JMP_INDIRECT  3
    unsigned int jmp;
    uint32_t jmp_pc;

    int abort_at_next_insn;
} DisasContext;

static int typeb_imm(DisasContext *dc, int x)
{
    if (dc->tb_flags & IMM_FLAG) {
        return deposit32(dc->ext_imm, 0, 16, x);
    }
    return x;
}

/* Include the auto-generated decoder.  */
#include "decode-insns.c.inc"

static void t_sync_flags(DisasContext *dc)
{
    /* Synch the tb dependent flags between translator and runtime.  */
    if ((dc->tb_flags ^ dc->base.tb->flags) & ~MSR_TB_MASK) {
        tcg_gen_movi_i32(cpu_iflags, dc->tb_flags & ~MSR_TB_MASK);
    }
}

static inline void sync_jmpstate(DisasContext *dc)
{
    if (dc->jmp == JMP_DIRECT || dc->jmp == JMP_DIRECT_CC) {
        if (dc->jmp == JMP_DIRECT) {
            tcg_gen_movi_i32(cpu_btaken, 1);
        }
        dc->jmp = JMP_INDIRECT;
        tcg_gen_movi_i32(cpu_btarget, dc->jmp_pc);
    }
}

static void gen_raise_exception(DisasContext *dc, uint32_t index)
{
    TCGv_i32 tmp = tcg_const_i32(index);

    gen_helper_raise_exception(cpu_env, tmp);
    tcg_temp_free_i32(tmp);
    dc->base.is_jmp = DISAS_NORETURN;
}

static void gen_raise_exception_sync(DisasContext *dc, uint32_t index)
{
    t_sync_flags(dc);
    tcg_gen_movi_i32(cpu_pc, dc->base.pc_next);
    gen_raise_exception(dc, index);
}

static void gen_raise_hw_excp(DisasContext *dc, uint32_t esr_ec)
{
    TCGv_i32 tmp = tcg_const_i32(esr_ec);
    tcg_gen_st_i32(tmp, cpu_env, offsetof(CPUMBState, esr));
    tcg_temp_free_i32(tmp);

    gen_raise_exception_sync(dc, EXCP_HW_EXCP);
}

static inline bool use_goto_tb(DisasContext *dc, target_ulong dest)
{
#ifndef CONFIG_USER_ONLY
    return (dc->base.pc_first & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK);
#else
    return true;
#endif
}

static void gen_goto_tb(DisasContext *dc, int n, target_ulong dest)
{
    if (dc->base.singlestep_enabled) {
        TCGv_i32 tmp = tcg_const_i32(EXCP_DEBUG);
        tcg_gen_movi_i32(cpu_pc, dest);
        gen_helper_raise_exception(cpu_env, tmp);
        tcg_temp_free_i32(tmp);
    } else if (use_goto_tb(dc, dest)) {
        tcg_gen_goto_tb(n);
        tcg_gen_movi_i32(cpu_pc, dest);
        tcg_gen_exit_tb(dc->base.tb, n);
    } else {
        tcg_gen_movi_i32(cpu_pc, dest);
        tcg_gen_exit_tb(NULL, 0);
    }
    dc->base.is_jmp = DISAS_NORETURN;
}

/*
 * Returns true if the insn an illegal operation.
 * If exceptions are enabled, an exception is raised.
 */
static bool trap_illegal(DisasContext *dc, bool cond)
{
    if (cond && (dc->tb_flags & MSR_EE)
        && dc->cpu->cfg.illegal_opcode_exception) {
        gen_raise_hw_excp(dc, ESR_EC_ILLEGAL_OP);
    }
    return cond;
}

/*
 * Returns true if the insn is illegal in userspace.
 * If exceptions are enabled, an exception is raised.
 */
static bool trap_userspace(DisasContext *dc, bool cond)
{
    bool cond_user = cond && dc->mem_index == MMU_USER_IDX;

    if (cond_user && (dc->tb_flags & MSR_EE)) {
        gen_raise_hw_excp(dc, ESR_EC_PRIVINSN);
    }
    return cond_user;
}

static int32_t dec_alu_typeb_imm(DisasContext *dc)
{
    tcg_debug_assert(dc->type_b);
    return typeb_imm(dc, (int16_t)dc->imm);
}

static inline TCGv_i32 *dec_alu_op_b(DisasContext *dc)
{
    if (dc->type_b) {
        tcg_gen_movi_i32(cpu_imm, dec_alu_typeb_imm(dc));
        return &cpu_imm;
    }
    return &cpu_R[dc->rb];
}

static TCGv_i32 reg_for_read(DisasContext *dc, int reg)
{
    if (likely(reg != 0)) {
        return cpu_R[reg];
    }
    if (!dc->r0_set) {
        if (dc->r0 == NULL) {
            dc->r0 = tcg_temp_new_i32();
        }
        tcg_gen_movi_i32(dc->r0, 0);
        dc->r0_set = true;
    }
    return dc->r0;
}

static TCGv_i32 reg_for_write(DisasContext *dc, int reg)
{
    if (likely(reg != 0)) {
        return cpu_R[reg];
    }
    if (dc->r0 == NULL) {
        dc->r0 = tcg_temp_new_i32();
    }
    return dc->r0;
}

static bool do_typea(DisasContext *dc, arg_typea *arg, bool side_effects,
                     void (*fn)(TCGv_i32, TCGv_i32, TCGv_i32))
{
    TCGv_i32 rd, ra, rb;

    if (arg->rd == 0 && !side_effects) {
        return true;
    }

    rd = reg_for_write(dc, arg->rd);
    ra = reg_for_read(dc, arg->ra);
    rb = reg_for_read(dc, arg->rb);
    fn(rd, ra, rb);
    return true;
}

static bool do_typea0(DisasContext *dc, arg_typea0 *arg, bool side_effects,
                      void (*fn)(TCGv_i32, TCGv_i32))
{
    TCGv_i32 rd, ra;

    if (arg->rd == 0 && !side_effects) {
        return true;
    }

    rd = reg_for_write(dc, arg->rd);
    ra = reg_for_read(dc, arg->ra);
    fn(rd, ra);
    return true;
}

static bool do_typeb_imm(DisasContext *dc, arg_typeb *arg, bool side_effects,
                         void (*fni)(TCGv_i32, TCGv_i32, int32_t))
{
    TCGv_i32 rd, ra;

    if (arg->rd == 0 && !side_effects) {
        return true;
    }

    rd = reg_for_write(dc, arg->rd);
    ra = reg_for_read(dc, arg->ra);
    fni(rd, ra, arg->imm);
    return true;
}

static bool do_typeb_val(DisasContext *dc, arg_typeb *arg, bool side_effects,
                         void (*fn)(TCGv_i32, TCGv_i32, TCGv_i32))
{
    TCGv_i32 rd, ra, imm;

    if (arg->rd == 0 && !side_effects) {
        return true;
    }

    rd = reg_for_write(dc, arg->rd);
    ra = reg_for_read(dc, arg->ra);
    imm = tcg_const_i32(arg->imm);

    fn(rd, ra, imm);

    tcg_temp_free_i32(imm);
    return true;
}

#define DO_TYPEA(NAME, SE, FN) \
    static bool trans_##NAME(DisasContext *dc, arg_typea *a) \
    { return do_typea(dc, a, SE, FN); }

#define DO_TYPEA_CFG(NAME, CFG, SE, FN) \
    static bool trans_##NAME(DisasContext *dc, arg_typea *a) \
    { return dc->cpu->cfg.CFG && do_typea(dc, a, SE, FN); }

#define DO_TYPEA0(NAME, SE, FN) \
    static bool trans_##NAME(DisasContext *dc, arg_typea0 *a) \
    { return do_typea0(dc, a, SE, FN); }

#define DO_TYPEA0_CFG(NAME, CFG, SE, FN) \
    static bool trans_##NAME(DisasContext *dc, arg_typea0 *a) \
    { return dc->cpu->cfg.CFG && do_typea0(dc, a, SE, FN); }

#define DO_TYPEBI(NAME, SE, FNI) \
    static bool trans_##NAME(DisasContext *dc, arg_typeb *a) \
    { return do_typeb_imm(dc, a, SE, FNI); }

#define DO_TYPEBI_CFG(NAME, CFG, SE, FNI) \
    static bool trans_##NAME(DisasContext *dc, arg_typeb *a) \
    { return dc->cpu->cfg.CFG && do_typeb_imm(dc, a, SE, FNI); }

#define DO_TYPEBV(NAME, SE, FN) \
    static bool trans_##NAME(DisasContext *dc, arg_typeb *a) \
    { return do_typeb_val(dc, a, SE, FN); }

#define ENV_WRAPPER2(NAME, HELPER) \
    static void NAME(TCGv_i32 out, TCGv_i32 ina) \
    { HELPER(out, cpu_env, ina); }

#define ENV_WRAPPER3(NAME, HELPER) \
    static void NAME(TCGv_i32 out, TCGv_i32 ina, TCGv_i32 inb) \
    { HELPER(out, cpu_env, ina, inb); }

/* No input carry, but output carry. */
static void gen_add(TCGv_i32 out, TCGv_i32 ina, TCGv_i32 inb)
{
    TCGv_i32 zero = tcg_const_i32(0);

    tcg_gen_add2_i32(out, cpu_msr_c, ina, zero, inb, zero);

    tcg_temp_free_i32(zero);
}

/* Input and output carry. */
static void gen_addc(TCGv_i32 out, TCGv_i32 ina, TCGv_i32 inb)
{
    TCGv_i32 zero = tcg_const_i32(0);
    TCGv_i32 tmp = tcg_temp_new_i32();

    tcg_gen_add2_i32(tmp, cpu_msr_c, ina, zero, cpu_msr_c, zero);
    tcg_gen_add2_i32(out, cpu_msr_c, tmp, cpu_msr_c, inb, zero);

    tcg_temp_free_i32(tmp);
    tcg_temp_free_i32(zero);
}

/* Input carry, but no output carry. */
static void gen_addkc(TCGv_i32 out, TCGv_i32 ina, TCGv_i32 inb)
{
    tcg_gen_add_i32(out, ina, inb);
    tcg_gen_add_i32(out, out, cpu_msr_c);
}

DO_TYPEA(add, true, gen_add)
DO_TYPEA(addc, true, gen_addc)
DO_TYPEA(addk, false, tcg_gen_add_i32)
DO_TYPEA(addkc, true, gen_addkc)

DO_TYPEBV(addi, true, gen_add)
DO_TYPEBV(addic, true, gen_addc)
DO_TYPEBI(addik, false, tcg_gen_addi_i32)
DO_TYPEBV(addikc, true, gen_addkc)

static void gen_andni(TCGv_i32 out, TCGv_i32 ina, int32_t imm)
{
    tcg_gen_andi_i32(out, ina, ~imm);
}

DO_TYPEA(and, false, tcg_gen_and_i32)
DO_TYPEBI(andi, false, tcg_gen_andi_i32)
DO_TYPEA(andn, false, tcg_gen_andc_i32)
DO_TYPEBI(andni, false, gen_andni)

static void gen_bsra(TCGv_i32 out, TCGv_i32 ina, TCGv_i32 inb)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_andi_i32(tmp, inb, 31);
    tcg_gen_sar_i32(out, ina, tmp);
    tcg_temp_free_i32(tmp);
}

static void gen_bsrl(TCGv_i32 out, TCGv_i32 ina, TCGv_i32 inb)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_andi_i32(tmp, inb, 31);
    tcg_gen_shr_i32(out, ina, tmp);
    tcg_temp_free_i32(tmp);
}

static void gen_bsll(TCGv_i32 out, TCGv_i32 ina, TCGv_i32 inb)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_andi_i32(tmp, inb, 31);
    tcg_gen_shl_i32(out, ina, tmp);
    tcg_temp_free_i32(tmp);
}

static void gen_bsefi(TCGv_i32 out, TCGv_i32 ina, int32_t imm)
{
    /* Note that decodetree has extracted and reassembled imm_w/imm_s. */
    int imm_w = extract32(imm, 5, 5);
    int imm_s = extract32(imm, 0, 5);

    if (imm_w + imm_s > 32 || imm_w == 0) {
        /* These inputs have an undefined behavior.  */
        qemu_log_mask(LOG_GUEST_ERROR, "bsefi: Bad input w=%d s=%d\n",
                      imm_w, imm_s);
    } else {
        tcg_gen_extract_i32(out, ina, imm_s, imm_w);
    }
}

static void gen_bsifi(TCGv_i32 out, TCGv_i32 ina, int32_t imm)
{
    /* Note that decodetree has extracted and reassembled imm_w/imm_s. */
    int imm_w = extract32(imm, 5, 5);
    int imm_s = extract32(imm, 0, 5);
    int width = imm_w - imm_s + 1;

    if (imm_w < imm_s) {
        /* These inputs have an undefined behavior.  */
        qemu_log_mask(LOG_GUEST_ERROR, "bsifi: Bad input w=%d s=%d\n",
                      imm_w, imm_s);
    } else {
        tcg_gen_deposit_i32(out, out, ina, imm_s, width);
    }
}

DO_TYPEA_CFG(bsra, use_barrel, false, gen_bsra)
DO_TYPEA_CFG(bsrl, use_barrel, false, gen_bsrl)
DO_TYPEA_CFG(bsll, use_barrel, false, gen_bsll)

DO_TYPEBI_CFG(bsrai, use_barrel, false, tcg_gen_sari_i32)
DO_TYPEBI_CFG(bsrli, use_barrel, false, tcg_gen_shri_i32)
DO_TYPEBI_CFG(bslli, use_barrel, false, tcg_gen_shli_i32)

DO_TYPEBI_CFG(bsefi, use_barrel, false, gen_bsefi)
DO_TYPEBI_CFG(bsifi, use_barrel, false, gen_bsifi)

static void gen_clz(TCGv_i32 out, TCGv_i32 ina)
{
    tcg_gen_clzi_i32(out, ina, 32);
}

DO_TYPEA0_CFG(clz, use_pcmp_instr, false, gen_clz)

static void gen_cmp(TCGv_i32 out, TCGv_i32 ina, TCGv_i32 inb)
{
    TCGv_i32 lt = tcg_temp_new_i32();

    tcg_gen_setcond_i32(TCG_COND_LT, lt, inb, ina);
    tcg_gen_sub_i32(out, inb, ina);
    tcg_gen_deposit_i32(out, out, lt, 31, 1);
    tcg_temp_free_i32(lt);
}

static void gen_cmpu(TCGv_i32 out, TCGv_i32 ina, TCGv_i32 inb)
{
    TCGv_i32 lt = tcg_temp_new_i32();

    tcg_gen_setcond_i32(TCG_COND_LTU, lt, inb, ina);
    tcg_gen_sub_i32(out, inb, ina);
    tcg_gen_deposit_i32(out, out, lt, 31, 1);
    tcg_temp_free_i32(lt);
}

DO_TYPEA(cmp, false, gen_cmp)
DO_TYPEA(cmpu, false, gen_cmpu)

ENV_WRAPPER3(gen_fadd, gen_helper_fadd)
ENV_WRAPPER3(gen_frsub, gen_helper_frsub)
ENV_WRAPPER3(gen_fmul, gen_helper_fmul)
ENV_WRAPPER3(gen_fdiv, gen_helper_fdiv)
ENV_WRAPPER3(gen_fcmp_un, gen_helper_fcmp_un)
ENV_WRAPPER3(gen_fcmp_lt, gen_helper_fcmp_lt)
ENV_WRAPPER3(gen_fcmp_eq, gen_helper_fcmp_eq)
ENV_WRAPPER3(gen_fcmp_le, gen_helper_fcmp_le)
ENV_WRAPPER3(gen_fcmp_gt, gen_helper_fcmp_gt)
ENV_WRAPPER3(gen_fcmp_ne, gen_helper_fcmp_ne)
ENV_WRAPPER3(gen_fcmp_ge, gen_helper_fcmp_ge)

DO_TYPEA_CFG(fadd, use_fpu, true, gen_fadd)
DO_TYPEA_CFG(frsub, use_fpu, true, gen_frsub)
DO_TYPEA_CFG(fmul, use_fpu, true, gen_fmul)
DO_TYPEA_CFG(fdiv, use_fpu, true, gen_fdiv)
DO_TYPEA_CFG(fcmp_un, use_fpu, true, gen_fcmp_un)
DO_TYPEA_CFG(fcmp_lt, use_fpu, true, gen_fcmp_lt)
DO_TYPEA_CFG(fcmp_eq, use_fpu, true, gen_fcmp_eq)
DO_TYPEA_CFG(fcmp_le, use_fpu, true, gen_fcmp_le)
DO_TYPEA_CFG(fcmp_gt, use_fpu, true, gen_fcmp_gt)
DO_TYPEA_CFG(fcmp_ne, use_fpu, true, gen_fcmp_ne)
DO_TYPEA_CFG(fcmp_ge, use_fpu, true, gen_fcmp_ge)

ENV_WRAPPER2(gen_flt, gen_helper_flt)
ENV_WRAPPER2(gen_fint, gen_helper_fint)
ENV_WRAPPER2(gen_fsqrt, gen_helper_fsqrt)

DO_TYPEA0_CFG(flt, use_fpu >= 2, true, gen_flt)
DO_TYPEA0_CFG(fint, use_fpu >= 2, true, gen_fint)
DO_TYPEA0_CFG(fsqrt, use_fpu >= 2, true, gen_fsqrt)

/* Does not use ENV_WRAPPER3, because arguments are swapped as well. */
static void gen_idiv(TCGv_i32 out, TCGv_i32 ina, TCGv_i32 inb)
{
    gen_helper_divs(out, cpu_env, inb, ina);
}

static void gen_idivu(TCGv_i32 out, TCGv_i32 ina, TCGv_i32 inb)
{
    gen_helper_divu(out, cpu_env, inb, ina);
}

DO_TYPEA_CFG(idiv, use_div, true, gen_idiv)
DO_TYPEA_CFG(idivu, use_div, true, gen_idivu)

static bool trans_imm(DisasContext *dc, arg_imm *arg)
{
    dc->ext_imm = arg->imm << 16;
    tcg_gen_movi_i32(cpu_imm, dc->ext_imm);
    dc->tb_flags_to_set = IMM_FLAG;
    return true;
}

static void gen_mulh(TCGv_i32 out, TCGv_i32 ina, TCGv_i32 inb)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_muls2_i32(tmp, out, ina, inb);
    tcg_temp_free_i32(tmp);
}

static void gen_mulhu(TCGv_i32 out, TCGv_i32 ina, TCGv_i32 inb)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_mulu2_i32(tmp, out, ina, inb);
    tcg_temp_free_i32(tmp);
}

static void gen_mulhsu(TCGv_i32 out, TCGv_i32 ina, TCGv_i32 inb)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_mulsu2_i32(tmp, out, ina, inb);
    tcg_temp_free_i32(tmp);
}

DO_TYPEA_CFG(mul, use_hw_mul, false, tcg_gen_mul_i32)
DO_TYPEA_CFG(mulh, use_hw_mul >= 2, false, gen_mulh)
DO_TYPEA_CFG(mulhu, use_hw_mul >= 2, false, gen_mulhu)
DO_TYPEA_CFG(mulhsu, use_hw_mul >= 2, false, gen_mulhsu)
DO_TYPEBI_CFG(muli, use_hw_mul, false, tcg_gen_muli_i32)

DO_TYPEA(or, false, tcg_gen_or_i32)
DO_TYPEBI(ori, false, tcg_gen_ori_i32)

static void gen_pcmpeq(TCGv_i32 out, TCGv_i32 ina, TCGv_i32 inb)
{
    tcg_gen_setcond_i32(TCG_COND_EQ, out, ina, inb);
}

static void gen_pcmpne(TCGv_i32 out, TCGv_i32 ina, TCGv_i32 inb)
{
    tcg_gen_setcond_i32(TCG_COND_NE, out, ina, inb);
}

DO_TYPEA_CFG(pcmpbf, use_pcmp_instr, false, gen_helper_pcmpbf)
DO_TYPEA_CFG(pcmpeq, use_pcmp_instr, false, gen_pcmpeq)
DO_TYPEA_CFG(pcmpne, use_pcmp_instr, false, gen_pcmpne)

/* No input carry, but output carry. */
static void gen_rsub(TCGv_i32 out, TCGv_i32 ina, TCGv_i32 inb)
{
    tcg_gen_setcond_i32(TCG_COND_GEU, cpu_msr_c, inb, ina);
    tcg_gen_sub_i32(out, inb, ina);
}

/* Input and output carry. */
static void gen_rsubc(TCGv_i32 out, TCGv_i32 ina, TCGv_i32 inb)
{
    TCGv_i32 zero = tcg_const_i32(0);
    TCGv_i32 tmp = tcg_temp_new_i32();

    tcg_gen_not_i32(tmp, ina);
    tcg_gen_add2_i32(tmp, cpu_msr_c, tmp, zero, cpu_msr_c, zero);
    tcg_gen_add2_i32(out, cpu_msr_c, tmp, cpu_msr_c, inb, zero);

    tcg_temp_free_i32(zero);
    tcg_temp_free_i32(tmp);
}

/* No input or output carry. */
static void gen_rsubk(TCGv_i32 out, TCGv_i32 ina, TCGv_i32 inb)
{
    tcg_gen_sub_i32(out, inb, ina);
}

/* Input carry, no output carry. */
static void gen_rsubkc(TCGv_i32 out, TCGv_i32 ina, TCGv_i32 inb)
{
    TCGv_i32 nota = tcg_temp_new_i32();

    tcg_gen_not_i32(nota, ina);
    tcg_gen_add_i32(out, inb, nota);
    tcg_gen_add_i32(out, out, cpu_msr_c);

    tcg_temp_free_i32(nota);
}

DO_TYPEA(rsub, true, gen_rsub)
DO_TYPEA(rsubc, true, gen_rsubc)
DO_TYPEA(rsubk, false, gen_rsubk)
DO_TYPEA(rsubkc, true, gen_rsubkc)

DO_TYPEBV(rsubi, true, gen_rsub)
DO_TYPEBV(rsubic, true, gen_rsubc)
DO_TYPEBV(rsubik, false, gen_rsubk)
DO_TYPEBV(rsubikc, true, gen_rsubkc)

DO_TYPEA0(sext8, false, tcg_gen_ext8s_i32)
DO_TYPEA0(sext16, false, tcg_gen_ext16s_i32)

static void gen_sra(TCGv_i32 out, TCGv_i32 ina)
{
    tcg_gen_andi_i32(cpu_msr_c, ina, 1);
    tcg_gen_sari_i32(out, ina, 1);
}

static void gen_src(TCGv_i32 out, TCGv_i32 ina)
{
    TCGv_i32 tmp = tcg_temp_new_i32();

    tcg_gen_mov_i32(tmp, cpu_msr_c);
    tcg_gen_andi_i32(cpu_msr_c, ina, 1);
    tcg_gen_extract2_i32(out, ina, tmp, 1);

    tcg_temp_free_i32(tmp);
}

static void gen_srl(TCGv_i32 out, TCGv_i32 ina)
{
    tcg_gen_andi_i32(cpu_msr_c, ina, 1);
    tcg_gen_shri_i32(out, ina, 1);
}

DO_TYPEA0(sra, false, gen_sra)
DO_TYPEA0(src, false, gen_src)
DO_TYPEA0(srl, false, gen_srl)

static void gen_swaph(TCGv_i32 out, TCGv_i32 ina)
{
    tcg_gen_rotri_i32(out, ina, 16);
}

DO_TYPEA0(swapb, false, tcg_gen_bswap32_i32)
DO_TYPEA0(swaph, false, gen_swaph)

static bool trans_wdic(DisasContext *dc, arg_wdic *a)
{
    /* Cache operations are nops: only check for supervisor mode.  */
    trap_userspace(dc, true);
    return true;
}

DO_TYPEA(xor, false, tcg_gen_xor_i32)
DO_TYPEBI(xori, false, tcg_gen_xori_i32)

static TCGv compute_ldst_addr_typea(DisasContext *dc, int ra, int rb)
{
    TCGv ret = tcg_temp_new();

    /* If any of the regs is r0, set t to the value of the other reg.  */
    if (ra && rb) {
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_add_i32(tmp, cpu_R[ra], cpu_R[rb]);
        tcg_gen_extu_i32_tl(ret, tmp);
        tcg_temp_free_i32(tmp);
    } else if (ra) {
        tcg_gen_extu_i32_tl(ret, cpu_R[ra]);
    } else if (rb) {
        tcg_gen_extu_i32_tl(ret, cpu_R[rb]);
    } else {
        tcg_gen_movi_tl(ret, 0);
    }

    if ((ra == 1 || rb == 1) && dc->cpu->cfg.stackprot) {
        gen_helper_stackprot(cpu_env, ret);
    }
    return ret;
}

static TCGv compute_ldst_addr_typeb(DisasContext *dc, int ra, int imm)
{
    TCGv ret = tcg_temp_new();

    /* If any of the regs is r0, set t to the value of the other reg.  */
    if (ra) {
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_addi_i32(tmp, cpu_R[ra], imm);
        tcg_gen_extu_i32_tl(ret, tmp);
        tcg_temp_free_i32(tmp);
    } else {
        tcg_gen_movi_tl(ret, (uint32_t)imm);
    }

    if (ra == 1 && dc->cpu->cfg.stackprot) {
        gen_helper_stackprot(cpu_env, ret);
    }
    return ret;
}

static TCGv compute_ldst_addr_ea(DisasContext *dc, int ra, int rb)
{
    int addr_size = dc->cpu->cfg.addr_size;
    TCGv ret = tcg_temp_new();

    if (addr_size == 32 || ra == 0) {
        if (rb) {
            tcg_gen_extu_i32_tl(ret, cpu_R[rb]);
        } else {
            tcg_gen_movi_tl(ret, 0);
        }
    } else {
        if (rb) {
            tcg_gen_concat_i32_i64(ret, cpu_R[rb], cpu_R[ra]);
        } else {
            tcg_gen_extu_i32_tl(ret, cpu_R[ra]);
            tcg_gen_shli_tl(ret, ret, 32);
        }
        if (addr_size < 64) {
            /* Mask off out of range bits.  */
            tcg_gen_andi_i64(ret, ret, MAKE_64BIT_MASK(0, addr_size));
        }
    }
    return ret;
}

static void record_unaligned_ess(DisasContext *dc, int rd,
                                 MemOp size, bool store)
{
    uint32_t iflags = tcg_get_insn_start_param(dc->insn_start, 1);

    iflags |= ESR_ESS_FLAG;
    iflags |= rd << 5;
    iflags |= store * ESR_S;
    iflags |= (size == MO_32) * ESR_W;

    tcg_set_insn_start_param(dc->insn_start, 1, iflags);
}

static bool do_load(DisasContext *dc, int rd, TCGv addr, MemOp mop,
                    int mem_index, bool rev)
{
    MemOp size = mop & MO_SIZE;

    /*
     * When doing reverse accesses we need to do two things.
     *
     * 1. Reverse the address wrt endianness.
     * 2. Byteswap the data lanes on the way back into the CPU core.
     */
    if (rev) {
        if (size > MO_8) {
            mop ^= MO_BSWAP;
        }
        if (size < MO_32) {
            tcg_gen_xori_tl(addr, addr, 3 - size);
        }
    }

    sync_jmpstate(dc);

    if (size > MO_8 &&
        (dc->tb_flags & MSR_EE) &&
        dc->cpu->cfg.unaligned_exceptions) {
        record_unaligned_ess(dc, rd, size, false);
        mop |= MO_ALIGN;
    }

    tcg_gen_qemu_ld_i32(reg_for_write(dc, rd), addr, mem_index, mop);

    tcg_temp_free(addr);
    return true;
}

static bool trans_lbu(DisasContext *dc, arg_typea *arg)
{
    TCGv addr = compute_ldst_addr_typea(dc, arg->ra, arg->rb);
    return do_load(dc, arg->rd, addr, MO_UB, dc->mem_index, false);
}

static bool trans_lbur(DisasContext *dc, arg_typea *arg)
{
    TCGv addr = compute_ldst_addr_typea(dc, arg->ra, arg->rb);
    return do_load(dc, arg->rd, addr, MO_UB, dc->mem_index, true);
}

static bool trans_lbuea(DisasContext *dc, arg_typea *arg)
{
    if (trap_userspace(dc, true)) {
        return true;
    }
    TCGv addr = compute_ldst_addr_ea(dc, arg->ra, arg->rb);
    return do_load(dc, arg->rd, addr, MO_UB, MMU_NOMMU_IDX, false);
}

static bool trans_lbui(DisasContext *dc, arg_typeb *arg)
{
    TCGv addr = compute_ldst_addr_typeb(dc, arg->ra, arg->imm);
    return do_load(dc, arg->rd, addr, MO_UB, dc->mem_index, false);
}

static bool trans_lhu(DisasContext *dc, arg_typea *arg)
{
    TCGv addr = compute_ldst_addr_typea(dc, arg->ra, arg->rb);
    return do_load(dc, arg->rd, addr, MO_TEUW, dc->mem_index, false);
}

static bool trans_lhur(DisasContext *dc, arg_typea *arg)
{
    TCGv addr = compute_ldst_addr_typea(dc, arg->ra, arg->rb);
    return do_load(dc, arg->rd, addr, MO_TEUW, dc->mem_index, true);
}

static bool trans_lhuea(DisasContext *dc, arg_typea *arg)
{
    if (trap_userspace(dc, true)) {
        return true;
    }
    TCGv addr = compute_ldst_addr_ea(dc, arg->ra, arg->rb);
    return do_load(dc, arg->rd, addr, MO_TEUW, MMU_NOMMU_IDX, false);
}

static bool trans_lhui(DisasContext *dc, arg_typeb *arg)
{
    TCGv addr = compute_ldst_addr_typeb(dc, arg->ra, arg->imm);
    return do_load(dc, arg->rd, addr, MO_TEUW, dc->mem_index, false);
}

static bool trans_lw(DisasContext *dc, arg_typea *arg)
{
    TCGv addr = compute_ldst_addr_typea(dc, arg->ra, arg->rb);
    return do_load(dc, arg->rd, addr, MO_TEUL, dc->mem_index, false);
}

static bool trans_lwr(DisasContext *dc, arg_typea *arg)
{
    TCGv addr = compute_ldst_addr_typea(dc, arg->ra, arg->rb);
    return do_load(dc, arg->rd, addr, MO_TEUL, dc->mem_index, true);
}

static bool trans_lwea(DisasContext *dc, arg_typea *arg)
{
    if (trap_userspace(dc, true)) {
        return true;
    }
    TCGv addr = compute_ldst_addr_ea(dc, arg->ra, arg->rb);
    return do_load(dc, arg->rd, addr, MO_TEUL, MMU_NOMMU_IDX, false);
}

static bool trans_lwi(DisasContext *dc, arg_typeb *arg)
{
    TCGv addr = compute_ldst_addr_typeb(dc, arg->ra, arg->imm);
    return do_load(dc, arg->rd, addr, MO_TEUL, dc->mem_index, false);
}

static bool trans_lwx(DisasContext *dc, arg_typea *arg)
{
    TCGv addr = compute_ldst_addr_typea(dc, arg->ra, arg->rb);

    /* lwx does not throw unaligned access errors, so force alignment */
    tcg_gen_andi_tl(addr, addr, ~3);

    sync_jmpstate(dc);

    tcg_gen_qemu_ld_i32(cpu_res_val, addr, dc->mem_index, MO_TEUL);
    tcg_gen_mov_tl(cpu_res_addr, addr);
    tcg_temp_free(addr);

    if (arg->rd) {
        tcg_gen_mov_i32(cpu_R[arg->rd], cpu_res_val);
    }

    /* No support for AXI exclusive so always clear C */
    tcg_gen_movi_i32(cpu_msr_c, 0);
    return true;
}

static bool do_store(DisasContext *dc, int rd, TCGv addr, MemOp mop,
                     int mem_index, bool rev)
{
    MemOp size = mop & MO_SIZE;

    /*
     * When doing reverse accesses we need to do two things.
     *
     * 1. Reverse the address wrt endianness.
     * 2. Byteswap the data lanes on the way back into the CPU core.
     */
    if (rev) {
        if (size > MO_8) {
            mop ^= MO_BSWAP;
        }
        if (size < MO_32) {
            tcg_gen_xori_tl(addr, addr, 3 - size);
        }
    }

    sync_jmpstate(dc);

    if (size > MO_8 &&
        (dc->tb_flags & MSR_EE) &&
        dc->cpu->cfg.unaligned_exceptions) {
        record_unaligned_ess(dc, rd, size, true);
        mop |= MO_ALIGN;
    }

    tcg_gen_qemu_st_i32(reg_for_read(dc, rd), addr, mem_index, mop);

    tcg_temp_free(addr);
    return true;
}

static bool trans_sb(DisasContext *dc, arg_typea *arg)
{
    TCGv addr = compute_ldst_addr_typea(dc, arg->ra, arg->rb);
    return do_store(dc, arg->rd, addr, MO_UB, dc->mem_index, false);
}

static bool trans_sbr(DisasContext *dc, arg_typea *arg)
{
    TCGv addr = compute_ldst_addr_typea(dc, arg->ra, arg->rb);
    return do_store(dc, arg->rd, addr, MO_UB, dc->mem_index, true);
}

static bool trans_sbea(DisasContext *dc, arg_typea *arg)
{
    if (trap_userspace(dc, true)) {
        return true;
    }
    TCGv addr = compute_ldst_addr_ea(dc, arg->ra, arg->rb);
    return do_store(dc, arg->rd, addr, MO_UB, MMU_NOMMU_IDX, false);
}

static bool trans_sbi(DisasContext *dc, arg_typeb *arg)
{
    TCGv addr = compute_ldst_addr_typeb(dc, arg->ra, arg->imm);
    return do_store(dc, arg->rd, addr, MO_UB, dc->mem_index, false);
}

static bool trans_sh(DisasContext *dc, arg_typea *arg)
{
    TCGv addr = compute_ldst_addr_typea(dc, arg->ra, arg->rb);
    return do_store(dc, arg->rd, addr, MO_TEUW, dc->mem_index, false);
}

static bool trans_shr(DisasContext *dc, arg_typea *arg)
{
    TCGv addr = compute_ldst_addr_typea(dc, arg->ra, arg->rb);
    return do_store(dc, arg->rd, addr, MO_TEUW, dc->mem_index, true);
}

static bool trans_shea(DisasContext *dc, arg_typea *arg)
{
    if (trap_userspace(dc, true)) {
        return true;
    }
    TCGv addr = compute_ldst_addr_ea(dc, arg->ra, arg->rb);
    return do_store(dc, arg->rd, addr, MO_TEUW, MMU_NOMMU_IDX, false);
}

static bool trans_shi(DisasContext *dc, arg_typeb *arg)
{
    TCGv addr = compute_ldst_addr_typeb(dc, arg->ra, arg->imm);
    return do_store(dc, arg->rd, addr, MO_TEUW, dc->mem_index, false);
}

static bool trans_sw(DisasContext *dc, arg_typea *arg)
{
    TCGv addr = compute_ldst_addr_typea(dc, arg->ra, arg->rb);
    return do_store(dc, arg->rd, addr, MO_TEUL, dc->mem_index, false);
}

static bool trans_swr(DisasContext *dc, arg_typea *arg)
{
    TCGv addr = compute_ldst_addr_typea(dc, arg->ra, arg->rb);
    return do_store(dc, arg->rd, addr, MO_TEUL, dc->mem_index, true);
}

static bool trans_swea(DisasContext *dc, arg_typea *arg)
{
    if (trap_userspace(dc, true)) {
        return true;
    }
    TCGv addr = compute_ldst_addr_ea(dc, arg->ra, arg->rb);
    return do_store(dc, arg->rd, addr, MO_TEUL, MMU_NOMMU_IDX, false);
}

static bool trans_swi(DisasContext *dc, arg_typeb *arg)
{
    TCGv addr = compute_ldst_addr_typeb(dc, arg->ra, arg->imm);
    return do_store(dc, arg->rd, addr, MO_TEUL, dc->mem_index, false);
}

static bool trans_swx(DisasContext *dc, arg_typea *arg)
{
    TCGv addr = compute_ldst_addr_typea(dc, arg->ra, arg->rb);
    TCGLabel *swx_done = gen_new_label();
    TCGLabel *swx_fail = gen_new_label();
    TCGv_i32 tval;

    sync_jmpstate(dc);

    /* swx does not throw unaligned access errors, so force alignment */
    tcg_gen_andi_tl(addr, addr, ~3);

    /*
     * Compare the address vs the one we used during lwx.
     * On mismatch, the operation fails.  On match, addr dies at the
     * branch, but we know we can use the equal version in the global.
     * In either case, addr is no longer needed.
     */
    tcg_gen_brcond_tl(TCG_COND_NE, cpu_res_addr, addr, swx_fail);
    tcg_temp_free(addr);

    /*
     * Compare the value loaded during lwx with current contents of
     * the reserved location.
     */
    tval = tcg_temp_new_i32();

    tcg_gen_atomic_cmpxchg_i32(tval, cpu_res_addr, cpu_res_val,
                               reg_for_write(dc, arg->rd),
                               dc->mem_index, MO_TEUL);

    tcg_gen_brcond_i32(TCG_COND_NE, cpu_res_val, tval, swx_fail);
    tcg_temp_free_i32(tval);

    /* Success */
    tcg_gen_movi_i32(cpu_msr_c, 0);
    tcg_gen_br(swx_done);

    /* Failure */
    gen_set_label(swx_fail);
    tcg_gen_movi_i32(cpu_msr_c, 1);

    gen_set_label(swx_done);

    /*
     * Prevent the saved address from working again without another ldx.
     * Akin to the pseudocode setting reservation = 0.
     */
    tcg_gen_movi_tl(cpu_res_addr, -1);
    return true;
}

static bool trans_brk(DisasContext *dc, arg_typea_br *arg)
{
    if (trap_userspace(dc, true)) {
        return true;
    }
    tcg_gen_mov_i32(cpu_pc, reg_for_read(dc, arg->rb));
    if (arg->rd) {
        tcg_gen_movi_i32(cpu_R[arg->rd], dc->base.pc_next);
    }
    tcg_gen_ori_i32(cpu_msr, cpu_msr, MSR_BIP);
    tcg_gen_movi_tl(cpu_res_addr, -1);

    dc->base.is_jmp = DISAS_UPDATE;
    return true;
}

static bool trans_brki(DisasContext *dc, arg_typeb_br *arg)
{
    uint32_t imm = arg->imm;

    if (trap_userspace(dc, imm != 0x8 && imm != 0x18)) {
        return true;
    }
    tcg_gen_movi_i32(cpu_pc, imm);
    if (arg->rd) {
        tcg_gen_movi_i32(cpu_R[arg->rd], dc->base.pc_next);
    }
    tcg_gen_movi_tl(cpu_res_addr, -1);

#ifdef CONFIG_USER_ONLY
    switch (imm) {
    case 0x8:  /* syscall trap */
        gen_raise_exception_sync(dc, EXCP_SYSCALL);
        break;
    case 0x18: /* debug trap */
        gen_raise_exception_sync(dc, EXCP_DEBUG);
        break;
    default:   /* eliminated with trap_userspace check */
        g_assert_not_reached();
    }
#else
    uint32_t msr_to_set = 0;

    if (imm != 0x18) {
        msr_to_set |= MSR_BIP;
    }
    if (imm == 0x8 || imm == 0x18) {
        /* MSR_UM and MSR_VM are in tb_flags, so we know their value. */
        msr_to_set |= (dc->tb_flags & (MSR_UM | MSR_VM)) << 1;
        tcg_gen_andi_i32(cpu_msr, cpu_msr,
                         ~(MSR_VMS | MSR_UMS | MSR_VM | MSR_UM));
    }
    tcg_gen_ori_i32(cpu_msr, cpu_msr, msr_to_set);
    dc->base.is_jmp = DISAS_UPDATE;
#endif

    return true;
}

static bool trans_zero(DisasContext *dc, arg_zero *arg)
{
    /* If opcode_0_illegal, trap.  */
    if (dc->cpu->cfg.opcode_0_illegal) {
        trap_illegal(dc, true);
        return true;
    }
    /*
     * Otherwise, this is "add r0, r0, r0".
     * Continue to trans_add so that MSR[C] gets cleared.
     */
    return false;
}

static void msr_read(DisasContext *dc, TCGv_i32 d)
{
    TCGv_i32 t;

    /* Replicate the cpu_msr_c boolean into the proper bit and the copy. */
    t = tcg_temp_new_i32();
    tcg_gen_muli_i32(t, cpu_msr_c, MSR_C | MSR_CC);
    tcg_gen_or_i32(d, cpu_msr, t);
    tcg_temp_free_i32(t);
}

static void msr_write(DisasContext *dc, TCGv_i32 v)
{
    dc->cpustate_changed = 1;

    /* Install MSR_C.  */
    tcg_gen_extract_i32(cpu_msr_c, v, 2, 1);

    /* Clear MSR_C and MSR_CC; MSR_PVR is not writable, and is always clear. */
    tcg_gen_andi_i32(cpu_msr, v, ~(MSR_C | MSR_CC | MSR_PVR));
}

static void dec_msr(DisasContext *dc)
{
    CPUState *cs = CPU(dc->cpu);
    TCGv_i32 t0, t1;
    unsigned int sr, rn;
    bool to, clrset, extended = false;

    sr = extract32(dc->imm, 0, 14);
    to = extract32(dc->imm, 14, 1);
    clrset = extract32(dc->imm, 15, 1) == 0;
    dc->type_b = 1;
    if (to) {
        dc->cpustate_changed = 1;
    }

    /* Extended MSRs are only available if addr_size > 32.  */
    if (dc->cpu->cfg.addr_size > 32) {
        /* The E-bit is encoded differently for To/From MSR.  */
        static const unsigned int e_bit[] = { 19, 24 };

        extended = extract32(dc->imm, e_bit[to], 1);
    }

    /* msrclr and msrset.  */
    if (clrset) {
        bool clr = extract32(dc->ir, 16, 1);

        if (!dc->cpu->cfg.use_msr_instr) {
            /* nop??? */
            return;
        }

        if (trap_userspace(dc, dc->imm != 4 && dc->imm != 0)) {
            return;
        }

        if (dc->rd)
            msr_read(dc, cpu_R[dc->rd]);

        t0 = tcg_temp_new_i32();
        t1 = tcg_temp_new_i32();
        msr_read(dc, t0);
        tcg_gen_mov_i32(t1, *(dec_alu_op_b(dc)));

        if (clr) {
            tcg_gen_not_i32(t1, t1);
            tcg_gen_and_i32(t0, t0, t1);
        } else
            tcg_gen_or_i32(t0, t0, t1);
        msr_write(dc, t0);
        tcg_temp_free_i32(t0);
        tcg_temp_free_i32(t1);
        tcg_gen_movi_i32(cpu_pc, dc->base.pc_next + 4);
        dc->base.is_jmp = DISAS_UPDATE;
        return;
    }

    if (trap_userspace(dc, to)) {
        return;
    }

#if !defined(CONFIG_USER_ONLY)
    /* Catch read/writes to the mmu block.  */
    if ((sr & ~0xff) == 0x1000) {
        TCGv_i32 tmp_ext = tcg_const_i32(extended);
        TCGv_i32 tmp_sr;

        sr &= 7;
        tmp_sr = tcg_const_i32(sr);
        if (to) {
            gen_helper_mmu_write(cpu_env, tmp_ext, tmp_sr, cpu_R[dc->ra]);
        } else {
            gen_helper_mmu_read(cpu_R[dc->rd], cpu_env, tmp_ext, tmp_sr);
        }
        tcg_temp_free_i32(tmp_sr);
        tcg_temp_free_i32(tmp_ext);
        return;
    }
#endif

    if (to) {
        switch (sr) {
            case SR_PC:
                break;
            case SR_MSR:
                msr_write(dc, cpu_R[dc->ra]);
                break;
            case SR_EAR:
                {
                    TCGv_i64 t64 = tcg_temp_new_i64();
                    tcg_gen_extu_i32_i64(t64, cpu_R[dc->ra]);
                    tcg_gen_st_i64(t64, cpu_env, offsetof(CPUMBState, ear));
                    tcg_temp_free_i64(t64);
                }
                break;
            case SR_ESR:
                tcg_gen_st_i32(cpu_R[dc->ra],
                               cpu_env, offsetof(CPUMBState, esr));
                break;
            case SR_FSR:
                tcg_gen_st_i32(cpu_R[dc->ra],
                               cpu_env, offsetof(CPUMBState, fsr));
                break;
            case SR_BTR:
                tcg_gen_st_i32(cpu_R[dc->ra],
                               cpu_env, offsetof(CPUMBState, btr));
                break;
            case SR_EDR:
                tcg_gen_st_i32(cpu_R[dc->ra],
                               cpu_env, offsetof(CPUMBState, edr));
                break;
            case 0x800:
                tcg_gen_st_i32(cpu_R[dc->ra],
                               cpu_env, offsetof(CPUMBState, slr));
                break;
            case 0x802:
                tcg_gen_st_i32(cpu_R[dc->ra],
                               cpu_env, offsetof(CPUMBState, shr));
                break;
            default:
                cpu_abort(CPU(dc->cpu), "unknown mts reg %x\n", sr);
                break;
        }
    } else {
        switch (sr) {
            case SR_PC:
                tcg_gen_movi_i32(cpu_R[dc->rd], dc->base.pc_next);
                break;
            case SR_MSR:
                msr_read(dc, cpu_R[dc->rd]);
                break;
            case SR_EAR:
                {
                    TCGv_i64 t64 = tcg_temp_new_i64();
                    tcg_gen_ld_i64(t64, cpu_env, offsetof(CPUMBState, ear));
                    if (extended) {
                        tcg_gen_extrh_i64_i32(cpu_R[dc->rd], t64);
                    } else {
                        tcg_gen_extrl_i64_i32(cpu_R[dc->rd], t64);
                    }
                    tcg_temp_free_i64(t64);
                }
                break;
            case SR_ESR:
                tcg_gen_ld_i32(cpu_R[dc->rd],
                               cpu_env, offsetof(CPUMBState, esr));
                break;
            case SR_FSR:
                tcg_gen_ld_i32(cpu_R[dc->rd],
                               cpu_env, offsetof(CPUMBState, fsr));
                break;
            case SR_BTR:
                tcg_gen_ld_i32(cpu_R[dc->rd],
                               cpu_env, offsetof(CPUMBState, btr));
                break;
            case SR_EDR:
                tcg_gen_ld_i32(cpu_R[dc->rd],
                               cpu_env, offsetof(CPUMBState, edr));
                break;
            case 0x800:
                tcg_gen_ld_i32(cpu_R[dc->rd],
                               cpu_env, offsetof(CPUMBState, slr));
                break;
            case 0x802:
                tcg_gen_ld_i32(cpu_R[dc->rd],
                               cpu_env, offsetof(CPUMBState, shr));
                break;
            case 0x2000 ... 0x200c:
                rn = sr & 0xf;
                tcg_gen_ld_i32(cpu_R[dc->rd],
                              cpu_env, offsetof(CPUMBState, pvr.regs[rn]));
                break;
            default:
                cpu_abort(cs, "unknown mfs reg %x\n", sr);
                break;
        }
    }

    if (dc->rd == 0) {
        tcg_gen_movi_i32(cpu_R[0], 0);
    }
}

static inline void eval_cc(DisasContext *dc, unsigned int cc,
                           TCGv_i32 d, TCGv_i32 a)
{
    static const int mb_to_tcg_cc[] = {
        [CC_EQ] = TCG_COND_EQ,
        [CC_NE] = TCG_COND_NE,
        [CC_LT] = TCG_COND_LT,
        [CC_LE] = TCG_COND_LE,
        [CC_GE] = TCG_COND_GE,
        [CC_GT] = TCG_COND_GT,
    };

    switch (cc) {
    case CC_EQ:
    case CC_NE:
    case CC_LT:
    case CC_LE:
    case CC_GE:
    case CC_GT:
        tcg_gen_setcondi_i32(mb_to_tcg_cc[cc], d, a, 0);
        break;
    default:
        cpu_abort(CPU(dc->cpu), "Unknown condition code %x.\n", cc);
        break;
    }
}

static void eval_cond_jmp(DisasContext *dc, TCGv_i32 pc_true, TCGv_i32 pc_false)
{
    TCGv_i32 zero = tcg_const_i32(0);

    tcg_gen_movcond_i32(TCG_COND_NE, cpu_pc,
                        cpu_btaken, zero,
                        pc_true, pc_false);

    tcg_temp_free_i32(zero);
}

static void dec_setup_dslot(DisasContext *dc)
{
    dc->tb_flags_to_set |= D_FLAG;
    if (dc->type_b && (dc->tb_flags & IMM_FLAG)) {
        dc->tb_flags_to_set |= BIMM_FLAG;
    }
}

static void dec_bcc(DisasContext *dc)
{
    unsigned int cc;
    unsigned int dslot;

    cc = EXTRACT_FIELD(dc->ir, 21, 23);
    dslot = dc->ir & (1 << 25);

    if (dslot) {
        dec_setup_dslot(dc);
    }

    if (dc->type_b) {
        dc->jmp = JMP_DIRECT_CC;
        dc->jmp_pc = dc->base.pc_next + dec_alu_typeb_imm(dc);
        tcg_gen_movi_i32(cpu_btarget, dc->jmp_pc);
    } else {
        dc->jmp = JMP_INDIRECT;
        tcg_gen_addi_i32(cpu_btarget, cpu_R[dc->rb], dc->base.pc_next);
    }
    eval_cc(dc, cc, cpu_btaken, cpu_R[dc->ra]);
}

static void dec_br(DisasContext *dc)
{
    unsigned int dslot, link, abs, mbar;
    uint32_t add_pc;

    dslot = dc->ir & (1 << 20);
    abs = dc->ir & (1 << 19);
    link = dc->ir & (1 << 18);

    /* Memory barrier.  */
    mbar = (dc->ir >> 16) & 31;
    if (mbar == 2 && dc->imm == 4) {
        uint16_t mbar_imm = dc->rd;

        /* Data access memory barrier.  */
        if ((mbar_imm & 2) == 0) {
            tcg_gen_mb(TCG_BAR_SC | TCG_MO_ALL);
        }

        /* mbar IMM & 16 decodes to sleep.  */
        if (mbar_imm & 16) {
            TCGv_i32 tmp_1;

            if (trap_userspace(dc, true)) {
                /* Sleep is a privileged instruction.  */
                return;
            }

            t_sync_flags(dc);

            tmp_1 = tcg_const_i32(1);
            tcg_gen_st_i32(tmp_1, cpu_env,
                           -offsetof(MicroBlazeCPU, env)
                           +offsetof(CPUState, halted));
            tcg_temp_free_i32(tmp_1);

            tcg_gen_movi_i32(cpu_pc, dc->base.pc_next + 4);

            gen_raise_exception(dc, EXCP_HLT);
            return;
        }
        /* Break the TB.  */
        dc->cpustate_changed = 1;
        return;
    }

    if (dslot) {
        dec_setup_dslot(dc);
    }
    if (link && dc->rd) {
        tcg_gen_movi_i32(cpu_R[dc->rd], dc->base.pc_next);
    }

    add_pc = abs ? 0 : dc->base.pc_next;
    if (dc->type_b) {
        dc->jmp = JMP_DIRECT;
        dc->jmp_pc = add_pc + dec_alu_typeb_imm(dc);
        tcg_gen_movi_i32(cpu_btarget, dc->jmp_pc);
    } else {
        dc->jmp = JMP_INDIRECT;
        tcg_gen_addi_i32(cpu_btarget, cpu_R[dc->rb], add_pc);
    }
    tcg_gen_movi_i32(cpu_btaken, 1);
}

static inline void do_rti(DisasContext *dc)
{
    TCGv_i32 t0, t1;
    t0 = tcg_temp_new_i32();
    t1 = tcg_temp_new_i32();
    tcg_gen_mov_i32(t1, cpu_msr);
    tcg_gen_shri_i32(t0, t1, 1);
    tcg_gen_ori_i32(t1, t1, MSR_IE);
    tcg_gen_andi_i32(t0, t0, (MSR_VM | MSR_UM));

    tcg_gen_andi_i32(t1, t1, ~(MSR_VM | MSR_UM));
    tcg_gen_or_i32(t1, t1, t0);
    msr_write(dc, t1);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t0);
    dc->tb_flags &= ~DRTI_FLAG;
}

static inline void do_rtb(DisasContext *dc)
{
    TCGv_i32 t0, t1;
    t0 = tcg_temp_new_i32();
    t1 = tcg_temp_new_i32();
    tcg_gen_mov_i32(t1, cpu_msr);
    tcg_gen_andi_i32(t1, t1, ~MSR_BIP);
    tcg_gen_shri_i32(t0, t1, 1);
    tcg_gen_andi_i32(t0, t0, (MSR_VM | MSR_UM));

    tcg_gen_andi_i32(t1, t1, ~(MSR_VM | MSR_UM));
    tcg_gen_or_i32(t1, t1, t0);
    msr_write(dc, t1);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t0);
    dc->tb_flags &= ~DRTB_FLAG;
}

static inline void do_rte(DisasContext *dc)
{
    TCGv_i32 t0, t1;
    t0 = tcg_temp_new_i32();
    t1 = tcg_temp_new_i32();

    tcg_gen_mov_i32(t1, cpu_msr);
    tcg_gen_ori_i32(t1, t1, MSR_EE);
    tcg_gen_andi_i32(t1, t1, ~MSR_EIP);
    tcg_gen_shri_i32(t0, t1, 1);
    tcg_gen_andi_i32(t0, t0, (MSR_VM | MSR_UM));

    tcg_gen_andi_i32(t1, t1, ~(MSR_VM | MSR_UM));
    tcg_gen_or_i32(t1, t1, t0);
    msr_write(dc, t1);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t0);
    dc->tb_flags &= ~DRTE_FLAG;
}

static void dec_rts(DisasContext *dc)
{
    unsigned int b_bit, i_bit, e_bit;

    i_bit = dc->ir & (1 << 21);
    b_bit = dc->ir & (1 << 22);
    e_bit = dc->ir & (1 << 23);

    if (trap_userspace(dc, i_bit || b_bit || e_bit)) {
        return;
    }

    dec_setup_dslot(dc);

    if (i_bit) {
        dc->tb_flags |= DRTI_FLAG;
    } else if (b_bit) {
        dc->tb_flags |= DRTB_FLAG;
    } else if (e_bit) {
        dc->tb_flags |= DRTE_FLAG;
    }

    dc->jmp = JMP_INDIRECT;
    tcg_gen_movi_i32(cpu_btaken, 1);
    tcg_gen_add_i32(cpu_btarget, cpu_R[dc->ra], *dec_alu_op_b(dc));
}

static void dec_null(DisasContext *dc)
{
    if (trap_illegal(dc, true)) {
        return;
    }
    qemu_log_mask(LOG_GUEST_ERROR, "unknown insn pc=%x opc=%x\n",
                  (uint32_t)dc->base.pc_next, dc->opcode);
    dc->abort_at_next_insn = 1;
}

/* Insns connected to FSL or AXI stream attached devices.  */
static void dec_stream(DisasContext *dc)
{
    TCGv_i32 t_id, t_ctrl;
    int ctrl;

    if (trap_userspace(dc, true)) {
        return;
    }

    t_id = tcg_temp_new_i32();
    if (dc->type_b) {
        tcg_gen_movi_i32(t_id, dc->imm & 0xf);
        ctrl = dc->imm >> 10;
    } else {
        tcg_gen_andi_i32(t_id, cpu_R[dc->rb], 0xf);
        ctrl = dc->imm >> 5;
    }

    t_ctrl = tcg_const_i32(ctrl);

    if (dc->rd == 0) {
        gen_helper_put(t_id, t_ctrl, cpu_R[dc->ra]);
    } else {
        gen_helper_get(cpu_R[dc->rd], t_id, t_ctrl);
    }
    tcg_temp_free_i32(t_id);
    tcg_temp_free_i32(t_ctrl);
}

static struct decoder_info {
    struct {
        uint32_t bits;
        uint32_t mask;
    };
    void (*dec)(DisasContext *dc);
} decinfo[] = {
    {DEC_BR, dec_br},
    {DEC_BCC, dec_bcc},
    {DEC_RTS, dec_rts},
    {DEC_MSR, dec_msr},
    {DEC_STREAM, dec_stream},
    {{0, 0}, dec_null}
};

static void old_decode(DisasContext *dc, uint32_t ir)
{
    int i;

    dc->ir = ir;

    /* bit 2 seems to indicate insn type.  */
    dc->type_b = ir & (1 << 29);

    dc->opcode = EXTRACT_FIELD(ir, 26, 31);
    dc->rd = EXTRACT_FIELD(ir, 21, 25);
    dc->ra = EXTRACT_FIELD(ir, 16, 20);
    dc->rb = EXTRACT_FIELD(ir, 11, 15);
    dc->imm = EXTRACT_FIELD(ir, 0, 15);

    /* Large switch for all insns.  */
    for (i = 0; i < ARRAY_SIZE(decinfo); i++) {
        if ((dc->opcode & decinfo[i].mask) == decinfo[i].bits) {
            decinfo[i].dec(dc);
            break;
        }
    }
}

static void mb_tr_init_disas_context(DisasContextBase *dcb, CPUState *cs)
{
    DisasContext *dc = container_of(dcb, DisasContext, base);
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(cs);
    int bound;

    dc->cpu = cpu;
    dc->tb_flags = dc->base.tb->flags;
    dc->jmp = dc->tb_flags & D_FLAG ? JMP_INDIRECT : JMP_NOJMP;
    dc->cpustate_changed = 0;
    dc->abort_at_next_insn = 0;
    dc->ext_imm = dc->base.tb->cs_base;
    dc->r0 = NULL;
    dc->r0_set = false;
    dc->mem_index = cpu_mmu_index(&cpu->env, false);

    bound = -(dc->base.pc_first | TARGET_PAGE_MASK) / 4;
    dc->base.max_insns = MIN(dc->base.max_insns, bound);
}

static void mb_tr_tb_start(DisasContextBase *dcb, CPUState *cs)
{
}

static void mb_tr_insn_start(DisasContextBase *dcb, CPUState *cs)
{
    DisasContext *dc = container_of(dcb, DisasContext, base);

    tcg_gen_insn_start(dc->base.pc_next, dc->tb_flags & ~MSR_TB_MASK);
    dc->insn_start = tcg_last_op();
}

static bool mb_tr_breakpoint_check(DisasContextBase *dcb, CPUState *cs,
                                   const CPUBreakpoint *bp)
{
    DisasContext *dc = container_of(dcb, DisasContext, base);

    gen_raise_exception_sync(dc, EXCP_DEBUG);

    /*
     * The address covered by the breakpoint must be included in
     * [tb->pc, tb->pc + tb->size) in order to for it to be
     * properly cleared -- thus we increment the PC here so that
     * the logic setting tb->size below does the right thing.
     */
    dc->base.pc_next += 4;
    return true;
}

static void mb_tr_translate_insn(DisasContextBase *dcb, CPUState *cs)
{
    DisasContext *dc = container_of(dcb, DisasContext, base);
    CPUMBState *env = cs->env_ptr;
    uint32_t ir;

    /* TODO: This should raise an exception, not terminate qemu. */
    if (dc->base.pc_next & 3) {
        cpu_abort(cs, "Microblaze: unaligned PC=%x\n",
                  (uint32_t)dc->base.pc_next);
    }

    dc->tb_flags_to_set = 0;

    ir = cpu_ldl_code(env, dc->base.pc_next);
    if (!decode(dc, ir)) {
        old_decode(dc, ir);
    }

    if (dc->r0) {
        tcg_temp_free_i32(dc->r0);
        dc->r0 = NULL;
        dc->r0_set = false;
    }

    /* Discard the imm global when its contents cannot be used. */
    if ((dc->tb_flags & ~dc->tb_flags_to_set) & IMM_FLAG) {
        tcg_gen_discard_i32(cpu_imm);
    }

    dc->tb_flags &= ~(IMM_FLAG | BIMM_FLAG | D_FLAG);
    dc->tb_flags |= dc->tb_flags_to_set;
    dc->base.pc_next += 4;

    if (dc->jmp != JMP_NOJMP && !(dc->tb_flags & D_FLAG)) {
        if (dc->tb_flags & DRTI_FLAG) {
            do_rti(dc);
        }
        if (dc->tb_flags & DRTB_FLAG) {
            do_rtb(dc);
        }
        if (dc->tb_flags & DRTE_FLAG) {
            do_rte(dc);
        }
        dc->base.is_jmp = DISAS_JUMP;
    }

    /* Force an exit if the per-tb cpu state has changed.  */
    if (dc->base.is_jmp == DISAS_NEXT && dc->cpustate_changed) {
        dc->base.is_jmp = DISAS_UPDATE;
        tcg_gen_movi_i32(cpu_pc, dc->base.pc_next);
    }
}

static void mb_tr_tb_stop(DisasContextBase *dcb, CPUState *cs)
{
    DisasContext *dc = container_of(dcb, DisasContext, base);

    assert(!dc->abort_at_next_insn);

    if (dc->base.is_jmp == DISAS_NORETURN) {
        /* We have already exited the TB. */
        return;
    }

    t_sync_flags(dc);
    if (dc->tb_flags & D_FLAG) {
        sync_jmpstate(dc);
        dc->jmp = JMP_NOJMP;
    }

    switch (dc->base.is_jmp) {
    case DISAS_TOO_MANY:
        assert(dc->jmp == JMP_NOJMP);
        gen_goto_tb(dc, 0, dc->base.pc_next);
        return;

    case DISAS_UPDATE:
        assert(dc->jmp == JMP_NOJMP);
        if (unlikely(cs->singlestep_enabled)) {
            gen_raise_exception(dc, EXCP_DEBUG);
        } else {
            tcg_gen_exit_tb(NULL, 0);
        }
        return;

    case DISAS_JUMP:
        switch (dc->jmp) {
        case JMP_INDIRECT:
            {
                TCGv_i32 tmp_pc = tcg_const_i32(dc->base.pc_next);
                eval_cond_jmp(dc, cpu_btarget, tmp_pc);
                tcg_temp_free_i32(tmp_pc);

                if (unlikely(cs->singlestep_enabled)) {
                    gen_raise_exception(dc, EXCP_DEBUG);
                } else {
                    tcg_gen_exit_tb(NULL, 0);
                }
            }
            return;

        case JMP_DIRECT_CC:
            {
                TCGLabel *l1 = gen_new_label();
                tcg_gen_brcondi_i32(TCG_COND_NE, cpu_btaken, 0, l1);
                gen_goto_tb(dc, 1, dc->base.pc_next);
                gen_set_label(l1);
            }
            /* fall through */

        case JMP_DIRECT:
            gen_goto_tb(dc, 0, dc->jmp_pc);
            return;
        }
        /* fall through */

    default:
        g_assert_not_reached();
    }
}

static void mb_tr_disas_log(const DisasContextBase *dcb, CPUState *cs)
{
    qemu_log("IN: %s\n", lookup_symbol(dcb->pc_first));
    log_target_disas(cs, dcb->pc_first, dcb->tb->size);
}

static const TranslatorOps mb_tr_ops = {
    .init_disas_context = mb_tr_init_disas_context,
    .tb_start           = mb_tr_tb_start,
    .insn_start         = mb_tr_insn_start,
    .breakpoint_check   = mb_tr_breakpoint_check,
    .translate_insn     = mb_tr_translate_insn,
    .tb_stop            = mb_tr_tb_stop,
    .disas_log          = mb_tr_disas_log,
};

void gen_intermediate_code(CPUState *cpu, TranslationBlock *tb, int max_insns)
{
    DisasContext dc;
    translator_loop(&mb_tr_ops, &dc.base, cpu, tb, max_insns);
}

void mb_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(cs);
    CPUMBState *env = &cpu->env;
    uint32_t iflags;
    int i;

    qemu_fprintf(f, "pc=0x%08x msr=0x%05x mode=%s(saved=%s) eip=%d ie=%d\n",
                 env->pc, env->msr,
                 (env->msr & MSR_UM) ? "user" : "kernel",
                 (env->msr & MSR_UMS) ? "user" : "kernel",
                 (bool)(env->msr & MSR_EIP),
                 (bool)(env->msr & MSR_IE));

    iflags = env->iflags;
    qemu_fprintf(f, "iflags: 0x%08x", iflags);
    if (iflags & IMM_FLAG) {
        qemu_fprintf(f, " IMM(0x%08x)", env->imm);
    }
    if (iflags & BIMM_FLAG) {
        qemu_fprintf(f, " BIMM");
    }
    if (iflags & D_FLAG) {
        qemu_fprintf(f, " D(btaken=%d btarget=0x%08x)",
                     env->btaken, env->btarget);
    }
    if (iflags & DRTI_FLAG) {
        qemu_fprintf(f, " DRTI");
    }
    if (iflags & DRTE_FLAG) {
        qemu_fprintf(f, " DRTE");
    }
    if (iflags & DRTB_FLAG) {
        qemu_fprintf(f, " DRTB");
    }
    if (iflags & ESR_ESS_FLAG) {
        qemu_fprintf(f, " ESR_ESS(0x%04x)", iflags & ESR_ESS_MASK);
    }

    qemu_fprintf(f, "\nesr=0x%04x fsr=0x%02x btr=0x%08x edr=0x%x\n"
                 "ear=0x%016" PRIx64 " slr=0x%x shr=0x%x\n",
                 env->esr, env->fsr, env->btr, env->edr,
                 env->ear, env->slr, env->shr);

    for (i = 0; i < 12; i++) {
        qemu_fprintf(f, "rpvr%-2d=%08x%c",
                     i, env->pvr.regs[i], i % 4 == 3 ? '\n' : ' ');
    }

    for (i = 0; i < 32; i++) {
        qemu_fprintf(f, "r%2.2d=%08x%c",
                     i, env->regs[i], i % 4 == 3 ? '\n' : ' ');
    }
    qemu_fprintf(f, "\n");
}

void mb_tcg_init(void)
{
#define R(X)  { &cpu_R[X], offsetof(CPUMBState, regs[X]), "r" #X }
#define SP(X) { &cpu_##X, offsetof(CPUMBState, X), #X }

    static const struct {
        TCGv_i32 *var; int ofs; char name[8];
    } i32s[] = {
        R(0),  R(1),  R(2),  R(3),  R(4),  R(5),  R(6),  R(7),
        R(8),  R(9),  R(10), R(11), R(12), R(13), R(14), R(15),
        R(16), R(17), R(18), R(19), R(20), R(21), R(22), R(23),
        R(24), R(25), R(26), R(27), R(28), R(29), R(30), R(31),

        SP(pc),
        SP(msr),
        SP(msr_c),
        SP(imm),
        SP(iflags),
        SP(btaken),
        SP(btarget),
        SP(res_val),
    };

#undef R
#undef SP

    for (int i = 0; i < ARRAY_SIZE(i32s); ++i) {
        *i32s[i].var =
          tcg_global_mem_new_i32(cpu_env, i32s[i].ofs, i32s[i].name);
    }

    cpu_res_addr =
        tcg_global_mem_new(cpu_env, offsetof(CPUMBState, res_addr), "res_addr");
}

void restore_state_to_opc(CPUMBState *env, TranslationBlock *tb,
                          target_ulong *data)
{
    env->pc = data[0];
    env->iflags = data[1];
}
