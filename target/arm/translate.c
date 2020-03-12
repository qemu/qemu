/*
 *  ARM translation
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *  Copyright (c) 2005-2007 CodeSourcery
 *  Copyright (c) 2007 OpenedHand, Ltd.
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
#include "internals.h"
#include "disas/disas.h"
#include "exec/exec-all.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-op-gvec.h"
#include "qemu/log.h"
#include "qemu/bitops.h"
#include "arm_ldst.h"
#include "hw/semihosting/semihost.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"

#include "trace-tcg.h"
#include "exec/log.h"


#define ENABLE_ARCH_4T    arm_dc_feature(s, ARM_FEATURE_V4T)
#define ENABLE_ARCH_5     arm_dc_feature(s, ARM_FEATURE_V5)
/* currently all emulated v5 cores are also v5TE, so don't bother */
#define ENABLE_ARCH_5TE   arm_dc_feature(s, ARM_FEATURE_V5)
#define ENABLE_ARCH_5J    dc_isar_feature(aa32_jazelle, s)
#define ENABLE_ARCH_6     arm_dc_feature(s, ARM_FEATURE_V6)
#define ENABLE_ARCH_6K    arm_dc_feature(s, ARM_FEATURE_V6K)
#define ENABLE_ARCH_6T2   arm_dc_feature(s, ARM_FEATURE_THUMB2)
#define ENABLE_ARCH_7     arm_dc_feature(s, ARM_FEATURE_V7)
#define ENABLE_ARCH_8     arm_dc_feature(s, ARM_FEATURE_V8)

#define ARCH(x) do { if (!ENABLE_ARCH_##x) goto illegal_op; } while(0)

#include "translate.h"

#if defined(CONFIG_USER_ONLY)
#define IS_USER(s) 1
#else
#define IS_USER(s) (s->user)
#endif

/* We reuse the same 64-bit temporaries for efficiency.  */
static TCGv_i64 cpu_V0, cpu_V1, cpu_M0;
static TCGv_i32 cpu_R[16];
TCGv_i32 cpu_CF, cpu_NF, cpu_VF, cpu_ZF;
TCGv_i64 cpu_exclusive_addr;
TCGv_i64 cpu_exclusive_val;

#include "exec/gen-icount.h"

static const char * const regnames[] =
    { "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
      "r8", "r9", "r10", "r11", "r12", "r13", "r14", "pc" };

/* Function prototypes for gen_ functions calling Neon helpers.  */
typedef void NeonGenThreeOpEnvFn(TCGv_i32, TCGv_env, TCGv_i32,
                                 TCGv_i32, TCGv_i32);
/* Function prototypes for gen_ functions for fix point conversions */
typedef void VFPGenFixPointFn(TCGv_i32, TCGv_i32, TCGv_i32, TCGv_ptr);

/* initialize TCG globals.  */
void arm_translate_init(void)
{
    int i;

    for (i = 0; i < 16; i++) {
        cpu_R[i] = tcg_global_mem_new_i32(cpu_env,
                                          offsetof(CPUARMState, regs[i]),
                                          regnames[i]);
    }
    cpu_CF = tcg_global_mem_new_i32(cpu_env, offsetof(CPUARMState, CF), "CF");
    cpu_NF = tcg_global_mem_new_i32(cpu_env, offsetof(CPUARMState, NF), "NF");
    cpu_VF = tcg_global_mem_new_i32(cpu_env, offsetof(CPUARMState, VF), "VF");
    cpu_ZF = tcg_global_mem_new_i32(cpu_env, offsetof(CPUARMState, ZF), "ZF");

    cpu_exclusive_addr = tcg_global_mem_new_i64(cpu_env,
        offsetof(CPUARMState, exclusive_addr), "exclusive_addr");
    cpu_exclusive_val = tcg_global_mem_new_i64(cpu_env,
        offsetof(CPUARMState, exclusive_val), "exclusive_val");

    a64_translate_init();
}

/* Flags for the disas_set_da_iss info argument:
 * lower bits hold the Rt register number, higher bits are flags.
 */
typedef enum ISSInfo {
    ISSNone = 0,
    ISSRegMask = 0x1f,
    ISSInvalid = (1 << 5),
    ISSIsAcqRel = (1 << 6),
    ISSIsWrite = (1 << 7),
    ISSIs16Bit = (1 << 8),
} ISSInfo;

/* Save the syndrome information for a Data Abort */
static void disas_set_da_iss(DisasContext *s, MemOp memop, ISSInfo issinfo)
{
    uint32_t syn;
    int sas = memop & MO_SIZE;
    bool sse = memop & MO_SIGN;
    bool is_acqrel = issinfo & ISSIsAcqRel;
    bool is_write = issinfo & ISSIsWrite;
    bool is_16bit = issinfo & ISSIs16Bit;
    int srt = issinfo & ISSRegMask;

    if (issinfo & ISSInvalid) {
        /* Some callsites want to conditionally provide ISS info,
         * eg "only if this was not a writeback"
         */
        return;
    }

    if (srt == 15) {
        /* For AArch32, insns where the src/dest is R15 never generate
         * ISS information. Catching that here saves checking at all
         * the call sites.
         */
        return;
    }

    syn = syn_data_abort_with_iss(0, sas, sse, srt, 0, is_acqrel,
                                  0, 0, 0, is_write, 0, is_16bit);
    disas_set_insn_syndrome(s, syn);
}

static inline int get_a32_user_mem_index(DisasContext *s)
{
    /* Return the core mmu_idx to use for A32/T32 "unprivileged load/store"
     * insns:
     *  if PL2, UNPREDICTABLE (we choose to implement as if PL0)
     *  otherwise, access as if at PL0.
     */
    switch (s->mmu_idx) {
    case ARMMMUIdx_E2:        /* this one is UNPREDICTABLE */
    case ARMMMUIdx_E10_0:
    case ARMMMUIdx_E10_1:
    case ARMMMUIdx_E10_1_PAN:
        return arm_to_core_mmu_idx(ARMMMUIdx_E10_0);
    case ARMMMUIdx_SE3:
    case ARMMMUIdx_SE10_0:
    case ARMMMUIdx_SE10_1:
    case ARMMMUIdx_SE10_1_PAN:
        return arm_to_core_mmu_idx(ARMMMUIdx_SE10_0);
    case ARMMMUIdx_MUser:
    case ARMMMUIdx_MPriv:
        return arm_to_core_mmu_idx(ARMMMUIdx_MUser);
    case ARMMMUIdx_MUserNegPri:
    case ARMMMUIdx_MPrivNegPri:
        return arm_to_core_mmu_idx(ARMMMUIdx_MUserNegPri);
    case ARMMMUIdx_MSUser:
    case ARMMMUIdx_MSPriv:
        return arm_to_core_mmu_idx(ARMMMUIdx_MSUser);
    case ARMMMUIdx_MSUserNegPri:
    case ARMMMUIdx_MSPrivNegPri:
        return arm_to_core_mmu_idx(ARMMMUIdx_MSUserNegPri);
    default:
        g_assert_not_reached();
    }
}

static inline TCGv_i32 load_cpu_offset(int offset)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_ld_i32(tmp, cpu_env, offset);
    return tmp;
}

#define load_cpu_field(name) load_cpu_offset(offsetof(CPUARMState, name))

static inline void store_cpu_offset(TCGv_i32 var, int offset)
{
    tcg_gen_st_i32(var, cpu_env, offset);
    tcg_temp_free_i32(var);
}

#define store_cpu_field(var, name) \
    store_cpu_offset(var, offsetof(CPUARMState, name))

/* The architectural value of PC.  */
static uint32_t read_pc(DisasContext *s)
{
    return s->pc_curr + (s->thumb ? 4 : 8);
}

/* Set a variable to the value of a CPU register.  */
static void load_reg_var(DisasContext *s, TCGv_i32 var, int reg)
{
    if (reg == 15) {
        tcg_gen_movi_i32(var, read_pc(s));
    } else {
        tcg_gen_mov_i32(var, cpu_R[reg]);
    }
}

/* Create a new temporary and set it to the value of a CPU register.  */
static inline TCGv_i32 load_reg(DisasContext *s, int reg)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    load_reg_var(s, tmp, reg);
    return tmp;
}

/*
 * Create a new temp, REG + OFS, except PC is ALIGN(PC, 4).
 * This is used for load/store for which use of PC implies (literal),
 * or ADD that implies ADR.
 */
static TCGv_i32 add_reg_for_lit(DisasContext *s, int reg, int ofs)
{
    TCGv_i32 tmp = tcg_temp_new_i32();

    if (reg == 15) {
        tcg_gen_movi_i32(tmp, (read_pc(s) & ~3) + ofs);
    } else {
        tcg_gen_addi_i32(tmp, cpu_R[reg], ofs);
    }
    return tmp;
}

/* Set a CPU register.  The source must be a temporary and will be
   marked as dead.  */
static void store_reg(DisasContext *s, int reg, TCGv_i32 var)
{
    if (reg == 15) {
        /* In Thumb mode, we must ignore bit 0.
         * In ARM mode, for ARMv4 and ARMv5, it is UNPREDICTABLE if bits [1:0]
         * are not 0b00, but for ARMv6 and above, we must ignore bits [1:0].
         * We choose to ignore [1:0] in ARM mode for all architecture versions.
         */
        tcg_gen_andi_i32(var, var, s->thumb ? ~1 : ~3);
        s->base.is_jmp = DISAS_JUMP;
    }
    tcg_gen_mov_i32(cpu_R[reg], var);
    tcg_temp_free_i32(var);
}

/*
 * Variant of store_reg which applies v8M stack-limit checks before updating
 * SP. If the check fails this will result in an exception being taken.
 * We disable the stack checks for CONFIG_USER_ONLY because we have
 * no idea what the stack limits should be in that case.
 * If stack checking is not being done this just acts like store_reg().
 */
static void store_sp_checked(DisasContext *s, TCGv_i32 var)
{
#ifndef CONFIG_USER_ONLY
    if (s->v8m_stackcheck) {
        gen_helper_v8m_stackcheck(cpu_env, var);
    }
#endif
    store_reg(s, 13, var);
}

/* Value extensions.  */
#define gen_uxtb(var) tcg_gen_ext8u_i32(var, var)
#define gen_uxth(var) tcg_gen_ext16u_i32(var, var)
#define gen_sxtb(var) tcg_gen_ext8s_i32(var, var)
#define gen_sxth(var) tcg_gen_ext16s_i32(var, var)

#define gen_sxtb16(var) gen_helper_sxtb16(var, var)
#define gen_uxtb16(var) gen_helper_uxtb16(var, var)


static inline void gen_set_cpsr(TCGv_i32 var, uint32_t mask)
{
    TCGv_i32 tmp_mask = tcg_const_i32(mask);
    gen_helper_cpsr_write(cpu_env, var, tmp_mask);
    tcg_temp_free_i32(tmp_mask);
}
/* Set NZCV flags from the high 4 bits of var.  */
#define gen_set_nzcv(var) gen_set_cpsr(var, CPSR_NZCV)

static void gen_exception_internal(int excp)
{
    TCGv_i32 tcg_excp = tcg_const_i32(excp);

    assert(excp_is_internal(excp));
    gen_helper_exception_internal(cpu_env, tcg_excp);
    tcg_temp_free_i32(tcg_excp);
}

static void gen_step_complete_exception(DisasContext *s)
{
    /* We just completed step of an insn. Move from Active-not-pending
     * to Active-pending, and then also take the swstep exception.
     * This corresponds to making the (IMPDEF) choice to prioritize
     * swstep exceptions over asynchronous exceptions taken to an exception
     * level where debug is disabled. This choice has the advantage that
     * we do not need to maintain internal state corresponding to the
     * ISV/EX syndrome bits between completion of the step and generation
     * of the exception, and our syndrome information is always correct.
     */
    gen_ss_advance(s);
    gen_swstep_exception(s, 1, s->is_ldex);
    s->base.is_jmp = DISAS_NORETURN;
}

static void gen_singlestep_exception(DisasContext *s)
{
    /* Generate the right kind of exception for singlestep, which is
     * either the architectural singlestep or EXCP_DEBUG for QEMU's
     * gdb singlestepping.
     */
    if (s->ss_active) {
        gen_step_complete_exception(s);
    } else {
        gen_exception_internal(EXCP_DEBUG);
    }
}

static inline bool is_singlestepping(DisasContext *s)
{
    /* Return true if we are singlestepping either because of
     * architectural singlestep or QEMU gdbstub singlestep. This does
     * not include the command line '-singlestep' mode which is rather
     * misnamed as it only means "one instruction per TB" and doesn't
     * affect the code we generate.
     */
    return s->base.singlestep_enabled || s->ss_active;
}

static void gen_smul_dual(TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 tmp1 = tcg_temp_new_i32();
    TCGv_i32 tmp2 = tcg_temp_new_i32();
    tcg_gen_ext16s_i32(tmp1, a);
    tcg_gen_ext16s_i32(tmp2, b);
    tcg_gen_mul_i32(tmp1, tmp1, tmp2);
    tcg_temp_free_i32(tmp2);
    tcg_gen_sari_i32(a, a, 16);
    tcg_gen_sari_i32(b, b, 16);
    tcg_gen_mul_i32(b, b, a);
    tcg_gen_mov_i32(a, tmp1);
    tcg_temp_free_i32(tmp1);
}

/* Byteswap each halfword.  */
static void gen_rev16(TCGv_i32 dest, TCGv_i32 var)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    TCGv_i32 mask = tcg_const_i32(0x00ff00ff);
    tcg_gen_shri_i32(tmp, var, 8);
    tcg_gen_and_i32(tmp, tmp, mask);
    tcg_gen_and_i32(var, var, mask);
    tcg_gen_shli_i32(var, var, 8);
    tcg_gen_or_i32(dest, var, tmp);
    tcg_temp_free_i32(mask);
    tcg_temp_free_i32(tmp);
}

/* Byteswap low halfword and sign extend.  */
static void gen_revsh(TCGv_i32 dest, TCGv_i32 var)
{
    tcg_gen_ext16u_i32(var, var);
    tcg_gen_bswap16_i32(var, var);
    tcg_gen_ext16s_i32(dest, var);
}

/* 32x32->64 multiply.  Marks inputs as dead.  */
static TCGv_i64 gen_mulu_i64_i32(TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();
    TCGv_i64 ret;

    tcg_gen_mulu2_i32(lo, hi, a, b);
    tcg_temp_free_i32(a);
    tcg_temp_free_i32(b);

    ret = tcg_temp_new_i64();
    tcg_gen_concat_i32_i64(ret, lo, hi);
    tcg_temp_free_i32(lo);
    tcg_temp_free_i32(hi);

    return ret;
}

static TCGv_i64 gen_muls_i64_i32(TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();
    TCGv_i64 ret;

    tcg_gen_muls2_i32(lo, hi, a, b);
    tcg_temp_free_i32(a);
    tcg_temp_free_i32(b);

    ret = tcg_temp_new_i64();
    tcg_gen_concat_i32_i64(ret, lo, hi);
    tcg_temp_free_i32(lo);
    tcg_temp_free_i32(hi);

    return ret;
}

/* Swap low and high halfwords.  */
static void gen_swap_half(TCGv_i32 var)
{
    tcg_gen_rotri_i32(var, var, 16);
}

/* Dual 16-bit add.  Result placed in t0 and t1 is marked as dead.
    tmp = (t0 ^ t1) & 0x8000;
    t0 &= ~0x8000;
    t1 &= ~0x8000;
    t0 = (t0 + t1) ^ tmp;
 */

static void gen_add16(TCGv_i32 dest, TCGv_i32 t0, TCGv_i32 t1)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_xor_i32(tmp, t0, t1);
    tcg_gen_andi_i32(tmp, tmp, 0x8000);
    tcg_gen_andi_i32(t0, t0, ~0x8000);
    tcg_gen_andi_i32(t1, t1, ~0x8000);
    tcg_gen_add_i32(t0, t0, t1);
    tcg_gen_xor_i32(dest, t0, tmp);
    tcg_temp_free_i32(tmp);
}

/* Set N and Z flags from var.  */
static inline void gen_logic_CC(TCGv_i32 var)
{
    tcg_gen_mov_i32(cpu_NF, var);
    tcg_gen_mov_i32(cpu_ZF, var);
}

/* dest = T0 + T1 + CF. */
static void gen_add_carry(TCGv_i32 dest, TCGv_i32 t0, TCGv_i32 t1)
{
    tcg_gen_add_i32(dest, t0, t1);
    tcg_gen_add_i32(dest, dest, cpu_CF);
}

/* dest = T0 - T1 + CF - 1.  */
static void gen_sub_carry(TCGv_i32 dest, TCGv_i32 t0, TCGv_i32 t1)
{
    tcg_gen_sub_i32(dest, t0, t1);
    tcg_gen_add_i32(dest, dest, cpu_CF);
    tcg_gen_subi_i32(dest, dest, 1);
}

/* dest = T0 + T1. Compute C, N, V and Z flags */
static void gen_add_CC(TCGv_i32 dest, TCGv_i32 t0, TCGv_i32 t1)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_movi_i32(tmp, 0);
    tcg_gen_add2_i32(cpu_NF, cpu_CF, t0, tmp, t1, tmp);
    tcg_gen_mov_i32(cpu_ZF, cpu_NF);
    tcg_gen_xor_i32(cpu_VF, cpu_NF, t0);
    tcg_gen_xor_i32(tmp, t0, t1);
    tcg_gen_andc_i32(cpu_VF, cpu_VF, tmp);
    tcg_temp_free_i32(tmp);
    tcg_gen_mov_i32(dest, cpu_NF);
}

/* dest = T0 + T1 + CF.  Compute C, N, V and Z flags */
static void gen_adc_CC(TCGv_i32 dest, TCGv_i32 t0, TCGv_i32 t1)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    if (TCG_TARGET_HAS_add2_i32) {
        tcg_gen_movi_i32(tmp, 0);
        tcg_gen_add2_i32(cpu_NF, cpu_CF, t0, tmp, cpu_CF, tmp);
        tcg_gen_add2_i32(cpu_NF, cpu_CF, cpu_NF, cpu_CF, t1, tmp);
    } else {
        TCGv_i64 q0 = tcg_temp_new_i64();
        TCGv_i64 q1 = tcg_temp_new_i64();
        tcg_gen_extu_i32_i64(q0, t0);
        tcg_gen_extu_i32_i64(q1, t1);
        tcg_gen_add_i64(q0, q0, q1);
        tcg_gen_extu_i32_i64(q1, cpu_CF);
        tcg_gen_add_i64(q0, q0, q1);
        tcg_gen_extr_i64_i32(cpu_NF, cpu_CF, q0);
        tcg_temp_free_i64(q0);
        tcg_temp_free_i64(q1);
    }
    tcg_gen_mov_i32(cpu_ZF, cpu_NF);
    tcg_gen_xor_i32(cpu_VF, cpu_NF, t0);
    tcg_gen_xor_i32(tmp, t0, t1);
    tcg_gen_andc_i32(cpu_VF, cpu_VF, tmp);
    tcg_temp_free_i32(tmp);
    tcg_gen_mov_i32(dest, cpu_NF);
}

/* dest = T0 - T1. Compute C, N, V and Z flags */
static void gen_sub_CC(TCGv_i32 dest, TCGv_i32 t0, TCGv_i32 t1)
{
    TCGv_i32 tmp;
    tcg_gen_sub_i32(cpu_NF, t0, t1);
    tcg_gen_mov_i32(cpu_ZF, cpu_NF);
    tcg_gen_setcond_i32(TCG_COND_GEU, cpu_CF, t0, t1);
    tcg_gen_xor_i32(cpu_VF, cpu_NF, t0);
    tmp = tcg_temp_new_i32();
    tcg_gen_xor_i32(tmp, t0, t1);
    tcg_gen_and_i32(cpu_VF, cpu_VF, tmp);
    tcg_temp_free_i32(tmp);
    tcg_gen_mov_i32(dest, cpu_NF);
}

/* dest = T0 + ~T1 + CF.  Compute C, N, V and Z flags */
static void gen_sbc_CC(TCGv_i32 dest, TCGv_i32 t0, TCGv_i32 t1)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_not_i32(tmp, t1);
    gen_adc_CC(dest, t0, tmp);
    tcg_temp_free_i32(tmp);
}

#define GEN_SHIFT(name)                                               \
static void gen_##name(TCGv_i32 dest, TCGv_i32 t0, TCGv_i32 t1)       \
{                                                                     \
    TCGv_i32 tmp1, tmp2, tmp3;                                        \
    tmp1 = tcg_temp_new_i32();                                        \
    tcg_gen_andi_i32(tmp1, t1, 0xff);                                 \
    tmp2 = tcg_const_i32(0);                                          \
    tmp3 = tcg_const_i32(0x1f);                                       \
    tcg_gen_movcond_i32(TCG_COND_GTU, tmp2, tmp1, tmp3, tmp2, t0);    \
    tcg_temp_free_i32(tmp3);                                          \
    tcg_gen_andi_i32(tmp1, tmp1, 0x1f);                               \
    tcg_gen_##name##_i32(dest, tmp2, tmp1);                           \
    tcg_temp_free_i32(tmp2);                                          \
    tcg_temp_free_i32(tmp1);                                          \
}
GEN_SHIFT(shl)
GEN_SHIFT(shr)
#undef GEN_SHIFT

static void gen_sar(TCGv_i32 dest, TCGv_i32 t0, TCGv_i32 t1)
{
    TCGv_i32 tmp1, tmp2;
    tmp1 = tcg_temp_new_i32();
    tcg_gen_andi_i32(tmp1, t1, 0xff);
    tmp2 = tcg_const_i32(0x1f);
    tcg_gen_movcond_i32(TCG_COND_GTU, tmp1, tmp1, tmp2, tmp2, tmp1);
    tcg_temp_free_i32(tmp2);
    tcg_gen_sar_i32(dest, t0, tmp1);
    tcg_temp_free_i32(tmp1);
}

static void shifter_out_im(TCGv_i32 var, int shift)
{
    tcg_gen_extract_i32(cpu_CF, var, shift, 1);
}

/* Shift by immediate.  Includes special handling for shift == 0.  */
static inline void gen_arm_shift_im(TCGv_i32 var, int shiftop,
                                    int shift, int flags)
{
    switch (shiftop) {
    case 0: /* LSL */
        if (shift != 0) {
            if (flags)
                shifter_out_im(var, 32 - shift);
            tcg_gen_shli_i32(var, var, shift);
        }
        break;
    case 1: /* LSR */
        if (shift == 0) {
            if (flags) {
                tcg_gen_shri_i32(cpu_CF, var, 31);
            }
            tcg_gen_movi_i32(var, 0);
        } else {
            if (flags)
                shifter_out_im(var, shift - 1);
            tcg_gen_shri_i32(var, var, shift);
        }
        break;
    case 2: /* ASR */
        if (shift == 0)
            shift = 32;
        if (flags)
            shifter_out_im(var, shift - 1);
        if (shift == 32)
          shift = 31;
        tcg_gen_sari_i32(var, var, shift);
        break;
    case 3: /* ROR/RRX */
        if (shift != 0) {
            if (flags)
                shifter_out_im(var, shift - 1);
            tcg_gen_rotri_i32(var, var, shift); break;
        } else {
            TCGv_i32 tmp = tcg_temp_new_i32();
            tcg_gen_shli_i32(tmp, cpu_CF, 31);
            if (flags)
                shifter_out_im(var, 0);
            tcg_gen_shri_i32(var, var, 1);
            tcg_gen_or_i32(var, var, tmp);
            tcg_temp_free_i32(tmp);
        }
    }
};

static inline void gen_arm_shift_reg(TCGv_i32 var, int shiftop,
                                     TCGv_i32 shift, int flags)
{
    if (flags) {
        switch (shiftop) {
        case 0: gen_helper_shl_cc(var, cpu_env, var, shift); break;
        case 1: gen_helper_shr_cc(var, cpu_env, var, shift); break;
        case 2: gen_helper_sar_cc(var, cpu_env, var, shift); break;
        case 3: gen_helper_ror_cc(var, cpu_env, var, shift); break;
        }
    } else {
        switch (shiftop) {
        case 0:
            gen_shl(var, var, shift);
            break;
        case 1:
            gen_shr(var, var, shift);
            break;
        case 2:
            gen_sar(var, var, shift);
            break;
        case 3: tcg_gen_andi_i32(shift, shift, 0x1f);
                tcg_gen_rotr_i32(var, var, shift); break;
        }
    }
    tcg_temp_free_i32(shift);
}

/*
 * Generate a conditional based on ARM condition code cc.
 * This is common between ARM and Aarch64 targets.
 */
void arm_test_cc(DisasCompare *cmp, int cc)
{
    TCGv_i32 value;
    TCGCond cond;
    bool global = true;

    switch (cc) {
    case 0: /* eq: Z */
    case 1: /* ne: !Z */
        cond = TCG_COND_EQ;
        value = cpu_ZF;
        break;

    case 2: /* cs: C */
    case 3: /* cc: !C */
        cond = TCG_COND_NE;
        value = cpu_CF;
        break;

    case 4: /* mi: N */
    case 5: /* pl: !N */
        cond = TCG_COND_LT;
        value = cpu_NF;
        break;

    case 6: /* vs: V */
    case 7: /* vc: !V */
        cond = TCG_COND_LT;
        value = cpu_VF;
        break;

    case 8: /* hi: C && !Z */
    case 9: /* ls: !C || Z -> !(C && !Z) */
        cond = TCG_COND_NE;
        value = tcg_temp_new_i32();
        global = false;
        /* CF is 1 for C, so -CF is an all-bits-set mask for C;
           ZF is non-zero for !Z; so AND the two subexpressions.  */
        tcg_gen_neg_i32(value, cpu_CF);
        tcg_gen_and_i32(value, value, cpu_ZF);
        break;

    case 10: /* ge: N == V -> N ^ V == 0 */
    case 11: /* lt: N != V -> N ^ V != 0 */
        /* Since we're only interested in the sign bit, == 0 is >= 0.  */
        cond = TCG_COND_GE;
        value = tcg_temp_new_i32();
        global = false;
        tcg_gen_xor_i32(value, cpu_VF, cpu_NF);
        break;

    case 12: /* gt: !Z && N == V */
    case 13: /* le: Z || N != V */
        cond = TCG_COND_NE;
        value = tcg_temp_new_i32();
        global = false;
        /* (N == V) is equal to the sign bit of ~(NF ^ VF).  Propagate
         * the sign bit then AND with ZF to yield the result.  */
        tcg_gen_xor_i32(value, cpu_VF, cpu_NF);
        tcg_gen_sari_i32(value, value, 31);
        tcg_gen_andc_i32(value, cpu_ZF, value);
        break;

    case 14: /* always */
    case 15: /* always */
        /* Use the ALWAYS condition, which will fold early.
         * It doesn't matter what we use for the value.  */
        cond = TCG_COND_ALWAYS;
        value = cpu_ZF;
        goto no_invert;

    default:
        fprintf(stderr, "Bad condition code 0x%x\n", cc);
        abort();
    }

    if (cc & 1) {
        cond = tcg_invert_cond(cond);
    }

 no_invert:
    cmp->cond = cond;
    cmp->value = value;
    cmp->value_global = global;
}

void arm_free_cc(DisasCompare *cmp)
{
    if (!cmp->value_global) {
        tcg_temp_free_i32(cmp->value);
    }
}

void arm_jump_cc(DisasCompare *cmp, TCGLabel *label)
{
    tcg_gen_brcondi_i32(cmp->cond, cmp->value, 0, label);
}

void arm_gen_test_cc(int cc, TCGLabel *label)
{
    DisasCompare cmp;
    arm_test_cc(&cmp, cc);
    arm_jump_cc(&cmp, label);
    arm_free_cc(&cmp);
}

static inline void gen_set_condexec(DisasContext *s)
{
    if (s->condexec_mask) {
        uint32_t val = (s->condexec_cond << 4) | (s->condexec_mask >> 1);
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_movi_i32(tmp, val);
        store_cpu_field(tmp, condexec_bits);
    }
}

static inline void gen_set_pc_im(DisasContext *s, target_ulong val)
{
    tcg_gen_movi_i32(cpu_R[15], val);
}

/* Set PC and Thumb state from var.  var is marked as dead.  */
static inline void gen_bx(DisasContext *s, TCGv_i32 var)
{
    s->base.is_jmp = DISAS_JUMP;
    tcg_gen_andi_i32(cpu_R[15], var, ~1);
    tcg_gen_andi_i32(var, var, 1);
    store_cpu_field(var, thumb);
}

/*
 * Set PC and Thumb state from var. var is marked as dead.
 * For M-profile CPUs, include logic to detect exception-return
 * branches and handle them. This is needed for Thumb POP/LDM to PC, LDR to PC,
 * and BX reg, and no others, and happens only for code in Handler mode.
 * The Security Extension also requires us to check for the FNC_RETURN
 * which signals a function return from non-secure state; this can happen
 * in both Handler and Thread mode.
 * To avoid having to do multiple comparisons in inline generated code,
 * we make the check we do here loose, so it will match for EXC_RETURN
 * in Thread mode. For system emulation do_v7m_exception_exit() checks
 * for these spurious cases and returns without doing anything (giving
 * the same behaviour as for a branch to a non-magic address).
 *
 * In linux-user mode it is unclear what the right behaviour for an
 * attempted FNC_RETURN should be, because in real hardware this will go
 * directly to Secure code (ie not the Linux kernel) which will then treat
 * the error in any way it chooses. For QEMU we opt to make the FNC_RETURN
 * attempt behave the way it would on a CPU without the security extension,
 * which is to say "like a normal branch". That means we can simply treat
 * all branches as normal with no magic address behaviour.
 */
static inline void gen_bx_excret(DisasContext *s, TCGv_i32 var)
{
    /* Generate the same code here as for a simple bx, but flag via
     * s->base.is_jmp that we need to do the rest of the work later.
     */
    gen_bx(s, var);
#ifndef CONFIG_USER_ONLY
    if (arm_dc_feature(s, ARM_FEATURE_M_SECURITY) ||
        (s->v7m_handler_mode && arm_dc_feature(s, ARM_FEATURE_M))) {
        s->base.is_jmp = DISAS_BX_EXCRET;
    }
#endif
}

static inline void gen_bx_excret_final_code(DisasContext *s)
{
    /* Generate the code to finish possible exception return and end the TB */
    TCGLabel *excret_label = gen_new_label();
    uint32_t min_magic;

    if (arm_dc_feature(s, ARM_FEATURE_M_SECURITY)) {
        /* Covers FNC_RETURN and EXC_RETURN magic */
        min_magic = FNC_RETURN_MIN_MAGIC;
    } else {
        /* EXC_RETURN magic only */
        min_magic = EXC_RETURN_MIN_MAGIC;
    }

    /* Is the new PC value in the magic range indicating exception return? */
    tcg_gen_brcondi_i32(TCG_COND_GEU, cpu_R[15], min_magic, excret_label);
    /* No: end the TB as we would for a DISAS_JMP */
    if (is_singlestepping(s)) {
        gen_singlestep_exception(s);
    } else {
        tcg_gen_exit_tb(NULL, 0);
    }
    gen_set_label(excret_label);
    /* Yes: this is an exception return.
     * At this point in runtime env->regs[15] and env->thumb will hold
     * the exception-return magic number, which do_v7m_exception_exit()
     * will read. Nothing else will be able to see those values because
     * the cpu-exec main loop guarantees that we will always go straight
     * from raising the exception to the exception-handling code.
     *
     * gen_ss_advance(s) does nothing on M profile currently but
     * calling it is conceptually the right thing as we have executed
     * this instruction (compare SWI, HVC, SMC handling).
     */
    gen_ss_advance(s);
    gen_exception_internal(EXCP_EXCEPTION_EXIT);
}

static inline void gen_bxns(DisasContext *s, int rm)
{
    TCGv_i32 var = load_reg(s, rm);

    /* The bxns helper may raise an EXCEPTION_EXIT exception, so in theory
     * we need to sync state before calling it, but:
     *  - we don't need to do gen_set_pc_im() because the bxns helper will
     *    always set the PC itself
     *  - we don't need to do gen_set_condexec() because BXNS is UNPREDICTABLE
     *    unless it's outside an IT block or the last insn in an IT block,
     *    so we know that condexec == 0 (already set at the top of the TB)
     *    is correct in the non-UNPREDICTABLE cases, and we can choose
     *    "zeroes the IT bits" as our UNPREDICTABLE behaviour otherwise.
     */
    gen_helper_v7m_bxns(cpu_env, var);
    tcg_temp_free_i32(var);
    s->base.is_jmp = DISAS_EXIT;
}

static inline void gen_blxns(DisasContext *s, int rm)
{
    TCGv_i32 var = load_reg(s, rm);

    /* We don't need to sync condexec state, for the same reason as bxns.
     * We do however need to set the PC, because the blxns helper reads it.
     * The blxns helper may throw an exception.
     */
    gen_set_pc_im(s, s->base.pc_next);
    gen_helper_v7m_blxns(cpu_env, var);
    tcg_temp_free_i32(var);
    s->base.is_jmp = DISAS_EXIT;
}

/* Variant of store_reg which uses branch&exchange logic when storing
   to r15 in ARM architecture v7 and above. The source must be a temporary
   and will be marked as dead. */
static inline void store_reg_bx(DisasContext *s, int reg, TCGv_i32 var)
{
    if (reg == 15 && ENABLE_ARCH_7) {
        gen_bx(s, var);
    } else {
        store_reg(s, reg, var);
    }
}

/* Variant of store_reg which uses branch&exchange logic when storing
 * to r15 in ARM architecture v5T and above. This is used for storing
 * the results of a LDR/LDM/POP into r15, and corresponds to the cases
 * in the ARM ARM which use the LoadWritePC() pseudocode function. */
static inline void store_reg_from_load(DisasContext *s, int reg, TCGv_i32 var)
{
    if (reg == 15 && ENABLE_ARCH_5) {
        gen_bx_excret(s, var);
    } else {
        store_reg(s, reg, var);
    }
}

#ifdef CONFIG_USER_ONLY
#define IS_USER_ONLY 1
#else
#define IS_USER_ONLY 0
#endif

/* Abstractions of "generate code to do a guest load/store for
 * AArch32", where a vaddr is always 32 bits (and is zero
 * extended if we're a 64 bit core) and  data is also
 * 32 bits unless specifically doing a 64 bit access.
 * These functions work like tcg_gen_qemu_{ld,st}* except
 * that the address argument is TCGv_i32 rather than TCGv.
 */

static inline TCGv gen_aa32_addr(DisasContext *s, TCGv_i32 a32, MemOp op)
{
    TCGv addr = tcg_temp_new();
    tcg_gen_extu_i32_tl(addr, a32);

    /* Not needed for user-mode BE32, where we use MO_BE instead.  */
    if (!IS_USER_ONLY && s->sctlr_b && (op & MO_SIZE) < MO_32) {
        tcg_gen_xori_tl(addr, addr, 4 - (1 << (op & MO_SIZE)));
    }
    return addr;
}

static void gen_aa32_ld_i32(DisasContext *s, TCGv_i32 val, TCGv_i32 a32,
                            int index, MemOp opc)
{
    TCGv addr;

    if (arm_dc_feature(s, ARM_FEATURE_M) &&
        !arm_dc_feature(s, ARM_FEATURE_M_MAIN)) {
        opc |= MO_ALIGN;
    }

    addr = gen_aa32_addr(s, a32, opc);
    tcg_gen_qemu_ld_i32(val, addr, index, opc);
    tcg_temp_free(addr);
}

static void gen_aa32_st_i32(DisasContext *s, TCGv_i32 val, TCGv_i32 a32,
                            int index, MemOp opc)
{
    TCGv addr;

    if (arm_dc_feature(s, ARM_FEATURE_M) &&
        !arm_dc_feature(s, ARM_FEATURE_M_MAIN)) {
        opc |= MO_ALIGN;
    }

    addr = gen_aa32_addr(s, a32, opc);
    tcg_gen_qemu_st_i32(val, addr, index, opc);
    tcg_temp_free(addr);
}

#define DO_GEN_LD(SUFF, OPC)                                             \
static inline void gen_aa32_ld##SUFF(DisasContext *s, TCGv_i32 val,      \
                                     TCGv_i32 a32, int index)            \
{                                                                        \
    gen_aa32_ld_i32(s, val, a32, index, OPC | s->be_data);               \
}

#define DO_GEN_ST(SUFF, OPC)                                             \
static inline void gen_aa32_st##SUFF(DisasContext *s, TCGv_i32 val,      \
                                     TCGv_i32 a32, int index)            \
{                                                                        \
    gen_aa32_st_i32(s, val, a32, index, OPC | s->be_data);               \
}

static inline void gen_aa32_frob64(DisasContext *s, TCGv_i64 val)
{
    /* Not needed for user-mode BE32, where we use MO_BE instead.  */
    if (!IS_USER_ONLY && s->sctlr_b) {
        tcg_gen_rotri_i64(val, val, 32);
    }
}

static void gen_aa32_ld_i64(DisasContext *s, TCGv_i64 val, TCGv_i32 a32,
                            int index, MemOp opc)
{
    TCGv addr = gen_aa32_addr(s, a32, opc);
    tcg_gen_qemu_ld_i64(val, addr, index, opc);
    gen_aa32_frob64(s, val);
    tcg_temp_free(addr);
}

static inline void gen_aa32_ld64(DisasContext *s, TCGv_i64 val,
                                 TCGv_i32 a32, int index)
{
    gen_aa32_ld_i64(s, val, a32, index, MO_Q | s->be_data);
}

static void gen_aa32_st_i64(DisasContext *s, TCGv_i64 val, TCGv_i32 a32,
                            int index, MemOp opc)
{
    TCGv addr = gen_aa32_addr(s, a32, opc);

    /* Not needed for user-mode BE32, where we use MO_BE instead.  */
    if (!IS_USER_ONLY && s->sctlr_b) {
        TCGv_i64 tmp = tcg_temp_new_i64();
        tcg_gen_rotri_i64(tmp, val, 32);
        tcg_gen_qemu_st_i64(tmp, addr, index, opc);
        tcg_temp_free_i64(tmp);
    } else {
        tcg_gen_qemu_st_i64(val, addr, index, opc);
    }
    tcg_temp_free(addr);
}

static inline void gen_aa32_st64(DisasContext *s, TCGv_i64 val,
                                 TCGv_i32 a32, int index)
{
    gen_aa32_st_i64(s, val, a32, index, MO_Q | s->be_data);
}

DO_GEN_LD(8u, MO_UB)
DO_GEN_LD(16u, MO_UW)
DO_GEN_LD(32u, MO_UL)
DO_GEN_ST(8, MO_UB)
DO_GEN_ST(16, MO_UW)
DO_GEN_ST(32, MO_UL)

static inline void gen_hvc(DisasContext *s, int imm16)
{
    /* The pre HVC helper handles cases when HVC gets trapped
     * as an undefined insn by runtime configuration (ie before
     * the insn really executes).
     */
    gen_set_pc_im(s, s->pc_curr);
    gen_helper_pre_hvc(cpu_env);
    /* Otherwise we will treat this as a real exception which
     * happens after execution of the insn. (The distinction matters
     * for the PC value reported to the exception handler and also
     * for single stepping.)
     */
    s->svc_imm = imm16;
    gen_set_pc_im(s, s->base.pc_next);
    s->base.is_jmp = DISAS_HVC;
}

static inline void gen_smc(DisasContext *s)
{
    /* As with HVC, we may take an exception either before or after
     * the insn executes.
     */
    TCGv_i32 tmp;

    gen_set_pc_im(s, s->pc_curr);
    tmp = tcg_const_i32(syn_aa32_smc());
    gen_helper_pre_smc(cpu_env, tmp);
    tcg_temp_free_i32(tmp);
    gen_set_pc_im(s, s->base.pc_next);
    s->base.is_jmp = DISAS_SMC;
}

static void gen_exception_internal_insn(DisasContext *s, uint32_t pc, int excp)
{
    gen_set_condexec(s);
    gen_set_pc_im(s, pc);
    gen_exception_internal(excp);
    s->base.is_jmp = DISAS_NORETURN;
}

static void gen_exception_insn(DisasContext *s, uint32_t pc, int excp,
                               int syn, uint32_t target_el)
{
    gen_set_condexec(s);
    gen_set_pc_im(s, pc);
    gen_exception(excp, syn, target_el);
    s->base.is_jmp = DISAS_NORETURN;
}

static void gen_exception_bkpt_insn(DisasContext *s, uint32_t syn)
{
    TCGv_i32 tcg_syn;

    gen_set_condexec(s);
    gen_set_pc_im(s, s->pc_curr);
    tcg_syn = tcg_const_i32(syn);
    gen_helper_exception_bkpt_insn(cpu_env, tcg_syn);
    tcg_temp_free_i32(tcg_syn);
    s->base.is_jmp = DISAS_NORETURN;
}

static void unallocated_encoding(DisasContext *s)
{
    /* Unallocated and reserved encodings are uncategorized */
    gen_exception_insn(s, s->pc_curr, EXCP_UDEF, syn_uncategorized(),
                       default_exception_el(s));
}

/* Force a TB lookup after an instruction that changes the CPU state.  */
static inline void gen_lookup_tb(DisasContext *s)
{
    tcg_gen_movi_i32(cpu_R[15], s->base.pc_next);
    s->base.is_jmp = DISAS_EXIT;
}

static inline void gen_hlt(DisasContext *s, int imm)
{
    /* HLT. This has two purposes.
     * Architecturally, it is an external halting debug instruction.
     * Since QEMU doesn't implement external debug, we treat this as
     * it is required for halting debug disabled: it will UNDEF.
     * Secondly, "HLT 0x3C" is a T32 semihosting trap instruction,
     * and "HLT 0xF000" is an A32 semihosting syscall. These traps
     * must trigger semihosting even for ARMv7 and earlier, where
     * HLT was an undefined encoding.
     * In system mode, we don't allow userspace access to
     * semihosting, to provide some semblance of security
     * (and for consistency with our 32-bit semihosting).
     */
    if (semihosting_enabled() &&
#ifndef CONFIG_USER_ONLY
        s->current_el != 0 &&
#endif
        (imm == (s->thumb ? 0x3c : 0xf000))) {
        gen_exception_internal_insn(s, s->pc_curr, EXCP_SEMIHOST);
        return;
    }

    unallocated_encoding(s);
}

static TCGv_ptr get_fpstatus_ptr(int neon)
{
    TCGv_ptr statusptr = tcg_temp_new_ptr();
    int offset;
    if (neon) {
        offset = offsetof(CPUARMState, vfp.standard_fp_status);
    } else {
        offset = offsetof(CPUARMState, vfp.fp_status);
    }
    tcg_gen_addi_ptr(statusptr, cpu_env, offset);
    return statusptr;
}

static inline long vfp_reg_offset(bool dp, unsigned reg)
{
    if (dp) {
        return offsetof(CPUARMState, vfp.zregs[reg >> 1].d[reg & 1]);
    } else {
        long ofs = offsetof(CPUARMState, vfp.zregs[reg >> 2].d[(reg >> 1) & 1]);
        if (reg & 1) {
            ofs += offsetof(CPU_DoubleU, l.upper);
        } else {
            ofs += offsetof(CPU_DoubleU, l.lower);
        }
        return ofs;
    }
}

/* Return the offset of a 32-bit piece of a NEON register.
   zero is the least significant end of the register.  */
static inline long
neon_reg_offset (int reg, int n)
{
    int sreg;
    sreg = reg * 2 + n;
    return vfp_reg_offset(0, sreg);
}

/* Return the offset of a 2**SIZE piece of a NEON register, at index ELE,
 * where 0 is the least significant end of the register.
 */
static inline long
neon_element_offset(int reg, int element, MemOp size)
{
    int element_size = 1 << size;
    int ofs = element * element_size;
#ifdef HOST_WORDS_BIGENDIAN
    /* Calculate the offset assuming fully little-endian,
     * then XOR to account for the order of the 8-byte units.
     */
    if (element_size < 8) {
        ofs ^= 8 - element_size;
    }
#endif
    return neon_reg_offset(reg, 0) + ofs;
}

static TCGv_i32 neon_load_reg(int reg, int pass)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_ld_i32(tmp, cpu_env, neon_reg_offset(reg, pass));
    return tmp;
}

static void neon_load_element(TCGv_i32 var, int reg, int ele, MemOp mop)
{
    long offset = neon_element_offset(reg, ele, mop & MO_SIZE);

    switch (mop) {
    case MO_UB:
        tcg_gen_ld8u_i32(var, cpu_env, offset);
        break;
    case MO_UW:
        tcg_gen_ld16u_i32(var, cpu_env, offset);
        break;
    case MO_UL:
        tcg_gen_ld_i32(var, cpu_env, offset);
        break;
    default:
        g_assert_not_reached();
    }
}

static void neon_load_element64(TCGv_i64 var, int reg, int ele, MemOp mop)
{
    long offset = neon_element_offset(reg, ele, mop & MO_SIZE);

    switch (mop) {
    case MO_UB:
        tcg_gen_ld8u_i64(var, cpu_env, offset);
        break;
    case MO_UW:
        tcg_gen_ld16u_i64(var, cpu_env, offset);
        break;
    case MO_UL:
        tcg_gen_ld32u_i64(var, cpu_env, offset);
        break;
    case MO_Q:
        tcg_gen_ld_i64(var, cpu_env, offset);
        break;
    default:
        g_assert_not_reached();
    }
}

static void neon_store_reg(int reg, int pass, TCGv_i32 var)
{
    tcg_gen_st_i32(var, cpu_env, neon_reg_offset(reg, pass));
    tcg_temp_free_i32(var);
}

static void neon_store_element(int reg, int ele, MemOp size, TCGv_i32 var)
{
    long offset = neon_element_offset(reg, ele, size);

    switch (size) {
    case MO_8:
        tcg_gen_st8_i32(var, cpu_env, offset);
        break;
    case MO_16:
        tcg_gen_st16_i32(var, cpu_env, offset);
        break;
    case MO_32:
        tcg_gen_st_i32(var, cpu_env, offset);
        break;
    default:
        g_assert_not_reached();
    }
}

static void neon_store_element64(int reg, int ele, MemOp size, TCGv_i64 var)
{
    long offset = neon_element_offset(reg, ele, size);

    switch (size) {
    case MO_8:
        tcg_gen_st8_i64(var, cpu_env, offset);
        break;
    case MO_16:
        tcg_gen_st16_i64(var, cpu_env, offset);
        break;
    case MO_32:
        tcg_gen_st32_i64(var, cpu_env, offset);
        break;
    case MO_64:
        tcg_gen_st_i64(var, cpu_env, offset);
        break;
    default:
        g_assert_not_reached();
    }
}

static inline void neon_load_reg64(TCGv_i64 var, int reg)
{
    tcg_gen_ld_i64(var, cpu_env, vfp_reg_offset(1, reg));
}

static inline void neon_store_reg64(TCGv_i64 var, int reg)
{
    tcg_gen_st_i64(var, cpu_env, vfp_reg_offset(1, reg));
}

static inline void neon_load_reg32(TCGv_i32 var, int reg)
{
    tcg_gen_ld_i32(var, cpu_env, vfp_reg_offset(false, reg));
}

static inline void neon_store_reg32(TCGv_i32 var, int reg)
{
    tcg_gen_st_i32(var, cpu_env, vfp_reg_offset(false, reg));
}

static TCGv_ptr vfp_reg_ptr(bool dp, int reg)
{
    TCGv_ptr ret = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(ret, cpu_env, vfp_reg_offset(dp, reg));
    return ret;
}

#define ARM_CP_RW_BIT   (1 << 20)

/* Include the VFP decoder */
#include "translate-vfp.inc.c"

static inline void iwmmxt_load_reg(TCGv_i64 var, int reg)
{
    tcg_gen_ld_i64(var, cpu_env, offsetof(CPUARMState, iwmmxt.regs[reg]));
}

static inline void iwmmxt_store_reg(TCGv_i64 var, int reg)
{
    tcg_gen_st_i64(var, cpu_env, offsetof(CPUARMState, iwmmxt.regs[reg]));
}

static inline TCGv_i32 iwmmxt_load_creg(int reg)
{
    TCGv_i32 var = tcg_temp_new_i32();
    tcg_gen_ld_i32(var, cpu_env, offsetof(CPUARMState, iwmmxt.cregs[reg]));
    return var;
}

static inline void iwmmxt_store_creg(int reg, TCGv_i32 var)
{
    tcg_gen_st_i32(var, cpu_env, offsetof(CPUARMState, iwmmxt.cregs[reg]));
    tcg_temp_free_i32(var);
}

static inline void gen_op_iwmmxt_movq_wRn_M0(int rn)
{
    iwmmxt_store_reg(cpu_M0, rn);
}

static inline void gen_op_iwmmxt_movq_M0_wRn(int rn)
{
    iwmmxt_load_reg(cpu_M0, rn);
}

static inline void gen_op_iwmmxt_orq_M0_wRn(int rn)
{
    iwmmxt_load_reg(cpu_V1, rn);
    tcg_gen_or_i64(cpu_M0, cpu_M0, cpu_V1);
}

static inline void gen_op_iwmmxt_andq_M0_wRn(int rn)
{
    iwmmxt_load_reg(cpu_V1, rn);
    tcg_gen_and_i64(cpu_M0, cpu_M0, cpu_V1);
}

static inline void gen_op_iwmmxt_xorq_M0_wRn(int rn)
{
    iwmmxt_load_reg(cpu_V1, rn);
    tcg_gen_xor_i64(cpu_M0, cpu_M0, cpu_V1);
}

#define IWMMXT_OP(name) \
static inline void gen_op_iwmmxt_##name##_M0_wRn(int rn) \
{ \
    iwmmxt_load_reg(cpu_V1, rn); \
    gen_helper_iwmmxt_##name(cpu_M0, cpu_M0, cpu_V1); \
}

#define IWMMXT_OP_ENV(name) \
static inline void gen_op_iwmmxt_##name##_M0_wRn(int rn) \
{ \
    iwmmxt_load_reg(cpu_V1, rn); \
    gen_helper_iwmmxt_##name(cpu_M0, cpu_env, cpu_M0, cpu_V1); \
}

#define IWMMXT_OP_ENV_SIZE(name) \
IWMMXT_OP_ENV(name##b) \
IWMMXT_OP_ENV(name##w) \
IWMMXT_OP_ENV(name##l)

#define IWMMXT_OP_ENV1(name) \
static inline void gen_op_iwmmxt_##name##_M0(void) \
{ \
    gen_helper_iwmmxt_##name(cpu_M0, cpu_env, cpu_M0); \
}

IWMMXT_OP(maddsq)
IWMMXT_OP(madduq)
IWMMXT_OP(sadb)
IWMMXT_OP(sadw)
IWMMXT_OP(mulslw)
IWMMXT_OP(mulshw)
IWMMXT_OP(mululw)
IWMMXT_OP(muluhw)
IWMMXT_OP(macsw)
IWMMXT_OP(macuw)

IWMMXT_OP_ENV_SIZE(unpackl)
IWMMXT_OP_ENV_SIZE(unpackh)

IWMMXT_OP_ENV1(unpacklub)
IWMMXT_OP_ENV1(unpackluw)
IWMMXT_OP_ENV1(unpacklul)
IWMMXT_OP_ENV1(unpackhub)
IWMMXT_OP_ENV1(unpackhuw)
IWMMXT_OP_ENV1(unpackhul)
IWMMXT_OP_ENV1(unpacklsb)
IWMMXT_OP_ENV1(unpacklsw)
IWMMXT_OP_ENV1(unpacklsl)
IWMMXT_OP_ENV1(unpackhsb)
IWMMXT_OP_ENV1(unpackhsw)
IWMMXT_OP_ENV1(unpackhsl)

IWMMXT_OP_ENV_SIZE(cmpeq)
IWMMXT_OP_ENV_SIZE(cmpgtu)
IWMMXT_OP_ENV_SIZE(cmpgts)

IWMMXT_OP_ENV_SIZE(mins)
IWMMXT_OP_ENV_SIZE(minu)
IWMMXT_OP_ENV_SIZE(maxs)
IWMMXT_OP_ENV_SIZE(maxu)

IWMMXT_OP_ENV_SIZE(subn)
IWMMXT_OP_ENV_SIZE(addn)
IWMMXT_OP_ENV_SIZE(subu)
IWMMXT_OP_ENV_SIZE(addu)
IWMMXT_OP_ENV_SIZE(subs)
IWMMXT_OP_ENV_SIZE(adds)

IWMMXT_OP_ENV(avgb0)
IWMMXT_OP_ENV(avgb1)
IWMMXT_OP_ENV(avgw0)
IWMMXT_OP_ENV(avgw1)

IWMMXT_OP_ENV(packuw)
IWMMXT_OP_ENV(packul)
IWMMXT_OP_ENV(packuq)
IWMMXT_OP_ENV(packsw)
IWMMXT_OP_ENV(packsl)
IWMMXT_OP_ENV(packsq)

static void gen_op_iwmmxt_set_mup(void)
{
    TCGv_i32 tmp;
    tmp = load_cpu_field(iwmmxt.cregs[ARM_IWMMXT_wCon]);
    tcg_gen_ori_i32(tmp, tmp, 2);
    store_cpu_field(tmp, iwmmxt.cregs[ARM_IWMMXT_wCon]);
}

static void gen_op_iwmmxt_set_cup(void)
{
    TCGv_i32 tmp;
    tmp = load_cpu_field(iwmmxt.cregs[ARM_IWMMXT_wCon]);
    tcg_gen_ori_i32(tmp, tmp, 1);
    store_cpu_field(tmp, iwmmxt.cregs[ARM_IWMMXT_wCon]);
}

static void gen_op_iwmmxt_setpsr_nz(void)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    gen_helper_iwmmxt_setpsr_nz(tmp, cpu_M0);
    store_cpu_field(tmp, iwmmxt.cregs[ARM_IWMMXT_wCASF]);
}

static inline void gen_op_iwmmxt_addl_M0_wRn(int rn)
{
    iwmmxt_load_reg(cpu_V1, rn);
    tcg_gen_ext32u_i64(cpu_V1, cpu_V1);
    tcg_gen_add_i64(cpu_M0, cpu_M0, cpu_V1);
}

static inline int gen_iwmmxt_address(DisasContext *s, uint32_t insn,
                                     TCGv_i32 dest)
{
    int rd;
    uint32_t offset;
    TCGv_i32 tmp;

    rd = (insn >> 16) & 0xf;
    tmp = load_reg(s, rd);

    offset = (insn & 0xff) << ((insn >> 7) & 2);
    if (insn & (1 << 24)) {
        /* Pre indexed */
        if (insn & (1 << 23))
            tcg_gen_addi_i32(tmp, tmp, offset);
        else
            tcg_gen_addi_i32(tmp, tmp, -offset);
        tcg_gen_mov_i32(dest, tmp);
        if (insn & (1 << 21))
            store_reg(s, rd, tmp);
        else
            tcg_temp_free_i32(tmp);
    } else if (insn & (1 << 21)) {
        /* Post indexed */
        tcg_gen_mov_i32(dest, tmp);
        if (insn & (1 << 23))
            tcg_gen_addi_i32(tmp, tmp, offset);
        else
            tcg_gen_addi_i32(tmp, tmp, -offset);
        store_reg(s, rd, tmp);
    } else if (!(insn & (1 << 23)))
        return 1;
    return 0;
}

static inline int gen_iwmmxt_shift(uint32_t insn, uint32_t mask, TCGv_i32 dest)
{
    int rd = (insn >> 0) & 0xf;
    TCGv_i32 tmp;

    if (insn & (1 << 8)) {
        if (rd < ARM_IWMMXT_wCGR0 || rd > ARM_IWMMXT_wCGR3) {
            return 1;
        } else {
            tmp = iwmmxt_load_creg(rd);
        }
    } else {
        tmp = tcg_temp_new_i32();
        iwmmxt_load_reg(cpu_V0, rd);
        tcg_gen_extrl_i64_i32(tmp, cpu_V0);
    }
    tcg_gen_andi_i32(tmp, tmp, mask);
    tcg_gen_mov_i32(dest, tmp);
    tcg_temp_free_i32(tmp);
    return 0;
}

/* Disassemble an iwMMXt instruction.  Returns nonzero if an error occurred
   (ie. an undefined instruction).  */
static int disas_iwmmxt_insn(DisasContext *s, uint32_t insn)
{
    int rd, wrd;
    int rdhi, rdlo, rd0, rd1, i;
    TCGv_i32 addr;
    TCGv_i32 tmp, tmp2, tmp3;

    if ((insn & 0x0e000e00) == 0x0c000000) {
        if ((insn & 0x0fe00ff0) == 0x0c400000) {
            wrd = insn & 0xf;
            rdlo = (insn >> 12) & 0xf;
            rdhi = (insn >> 16) & 0xf;
            if (insn & ARM_CP_RW_BIT) {                         /* TMRRC */
                iwmmxt_load_reg(cpu_V0, wrd);
                tcg_gen_extrl_i64_i32(cpu_R[rdlo], cpu_V0);
                tcg_gen_extrh_i64_i32(cpu_R[rdhi], cpu_V0);
            } else {                                    /* TMCRR */
                tcg_gen_concat_i32_i64(cpu_V0, cpu_R[rdlo], cpu_R[rdhi]);
                iwmmxt_store_reg(cpu_V0, wrd);
                gen_op_iwmmxt_set_mup();
            }
            return 0;
        }

        wrd = (insn >> 12) & 0xf;
        addr = tcg_temp_new_i32();
        if (gen_iwmmxt_address(s, insn, addr)) {
            tcg_temp_free_i32(addr);
            return 1;
        }
        if (insn & ARM_CP_RW_BIT) {
            if ((insn >> 28) == 0xf) {                  /* WLDRW wCx */
                tmp = tcg_temp_new_i32();
                gen_aa32_ld32u(s, tmp, addr, get_mem_index(s));
                iwmmxt_store_creg(wrd, tmp);
            } else {
                i = 1;
                if (insn & (1 << 8)) {
                    if (insn & (1 << 22)) {             /* WLDRD */
                        gen_aa32_ld64(s, cpu_M0, addr, get_mem_index(s));
                        i = 0;
                    } else {                            /* WLDRW wRd */
                        tmp = tcg_temp_new_i32();
                        gen_aa32_ld32u(s, tmp, addr, get_mem_index(s));
                    }
                } else {
                    tmp = tcg_temp_new_i32();
                    if (insn & (1 << 22)) {             /* WLDRH */
                        gen_aa32_ld16u(s, tmp, addr, get_mem_index(s));
                    } else {                            /* WLDRB */
                        gen_aa32_ld8u(s, tmp, addr, get_mem_index(s));
                    }
                }
                if (i) {
                    tcg_gen_extu_i32_i64(cpu_M0, tmp);
                    tcg_temp_free_i32(tmp);
                }
                gen_op_iwmmxt_movq_wRn_M0(wrd);
            }
        } else {
            if ((insn >> 28) == 0xf) {                  /* WSTRW wCx */
                tmp = iwmmxt_load_creg(wrd);
                gen_aa32_st32(s, tmp, addr, get_mem_index(s));
            } else {
                gen_op_iwmmxt_movq_M0_wRn(wrd);
                tmp = tcg_temp_new_i32();
                if (insn & (1 << 8)) {
                    if (insn & (1 << 22)) {             /* WSTRD */
                        gen_aa32_st64(s, cpu_M0, addr, get_mem_index(s));
                    } else {                            /* WSTRW wRd */
                        tcg_gen_extrl_i64_i32(tmp, cpu_M0);
                        gen_aa32_st32(s, tmp, addr, get_mem_index(s));
                    }
                } else {
                    if (insn & (1 << 22)) {             /* WSTRH */
                        tcg_gen_extrl_i64_i32(tmp, cpu_M0);
                        gen_aa32_st16(s, tmp, addr, get_mem_index(s));
                    } else {                            /* WSTRB */
                        tcg_gen_extrl_i64_i32(tmp, cpu_M0);
                        gen_aa32_st8(s, tmp, addr, get_mem_index(s));
                    }
                }
            }
            tcg_temp_free_i32(tmp);
        }
        tcg_temp_free_i32(addr);
        return 0;
    }

    if ((insn & 0x0f000000) != 0x0e000000)
        return 1;

    switch (((insn >> 12) & 0xf00) | ((insn >> 4) & 0xff)) {
    case 0x000:                                                 /* WOR */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 0) & 0xf;
        rd1 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        gen_op_iwmmxt_orq_M0_wRn(rd1);
        gen_op_iwmmxt_setpsr_nz();
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x011:                                                 /* TMCR */
        if (insn & 0xf)
            return 1;
        rd = (insn >> 12) & 0xf;
        wrd = (insn >> 16) & 0xf;
        switch (wrd) {
        case ARM_IWMMXT_wCID:
        case ARM_IWMMXT_wCASF:
            break;
        case ARM_IWMMXT_wCon:
            gen_op_iwmmxt_set_cup();
            /* Fall through.  */
        case ARM_IWMMXT_wCSSF:
            tmp = iwmmxt_load_creg(wrd);
            tmp2 = load_reg(s, rd);
            tcg_gen_andc_i32(tmp, tmp, tmp2);
            tcg_temp_free_i32(tmp2);
            iwmmxt_store_creg(wrd, tmp);
            break;
        case ARM_IWMMXT_wCGR0:
        case ARM_IWMMXT_wCGR1:
        case ARM_IWMMXT_wCGR2:
        case ARM_IWMMXT_wCGR3:
            gen_op_iwmmxt_set_cup();
            tmp = load_reg(s, rd);
            iwmmxt_store_creg(wrd, tmp);
            break;
        default:
            return 1;
        }
        break;
    case 0x100:                                                 /* WXOR */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 0) & 0xf;
        rd1 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        gen_op_iwmmxt_xorq_M0_wRn(rd1);
        gen_op_iwmmxt_setpsr_nz();
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x111:                                                 /* TMRC */
        if (insn & 0xf)
            return 1;
        rd = (insn >> 12) & 0xf;
        wrd = (insn >> 16) & 0xf;
        tmp = iwmmxt_load_creg(wrd);
        store_reg(s, rd, tmp);
        break;
    case 0x300:                                                 /* WANDN */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 0) & 0xf;
        rd1 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        tcg_gen_neg_i64(cpu_M0, cpu_M0);
        gen_op_iwmmxt_andq_M0_wRn(rd1);
        gen_op_iwmmxt_setpsr_nz();
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x200:                                                 /* WAND */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 0) & 0xf;
        rd1 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        gen_op_iwmmxt_andq_M0_wRn(rd1);
        gen_op_iwmmxt_setpsr_nz();
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x810: case 0xa10:                             /* WMADD */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 0) & 0xf;
        rd1 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        if (insn & (1 << 21))
            gen_op_iwmmxt_maddsq_M0_wRn(rd1);
        else
            gen_op_iwmmxt_madduq_M0_wRn(rd1);
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x10e: case 0x50e: case 0x90e: case 0xd0e:     /* WUNPCKIL */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 22) & 3) {
        case 0:
            gen_op_iwmmxt_unpacklb_M0_wRn(rd1);
            break;
        case 1:
            gen_op_iwmmxt_unpacklw_M0_wRn(rd1);
            break;
        case 2:
            gen_op_iwmmxt_unpackll_M0_wRn(rd1);
            break;
        case 3:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x10c: case 0x50c: case 0x90c: case 0xd0c:     /* WUNPCKIH */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 22) & 3) {
        case 0:
            gen_op_iwmmxt_unpackhb_M0_wRn(rd1);
            break;
        case 1:
            gen_op_iwmmxt_unpackhw_M0_wRn(rd1);
            break;
        case 2:
            gen_op_iwmmxt_unpackhl_M0_wRn(rd1);
            break;
        case 3:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x012: case 0x112: case 0x412: case 0x512:     /* WSAD */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        if (insn & (1 << 22))
            gen_op_iwmmxt_sadw_M0_wRn(rd1);
        else
            gen_op_iwmmxt_sadb_M0_wRn(rd1);
        if (!(insn & (1 << 20)))
            gen_op_iwmmxt_addl_M0_wRn(wrd);
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x010: case 0x110: case 0x210: case 0x310:     /* WMUL */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        if (insn & (1 << 21)) {
            if (insn & (1 << 20))
                gen_op_iwmmxt_mulshw_M0_wRn(rd1);
            else
                gen_op_iwmmxt_mulslw_M0_wRn(rd1);
        } else {
            if (insn & (1 << 20))
                gen_op_iwmmxt_muluhw_M0_wRn(rd1);
            else
                gen_op_iwmmxt_mululw_M0_wRn(rd1);
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x410: case 0x510: case 0x610: case 0x710:     /* WMAC */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        if (insn & (1 << 21))
            gen_op_iwmmxt_macsw_M0_wRn(rd1);
        else
            gen_op_iwmmxt_macuw_M0_wRn(rd1);
        if (!(insn & (1 << 20))) {
            iwmmxt_load_reg(cpu_V1, wrd);
            tcg_gen_add_i64(cpu_M0, cpu_M0, cpu_V1);
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x006: case 0x406: case 0x806: case 0xc06:     /* WCMPEQ */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 22) & 3) {
        case 0:
            gen_op_iwmmxt_cmpeqb_M0_wRn(rd1);
            break;
        case 1:
            gen_op_iwmmxt_cmpeqw_M0_wRn(rd1);
            break;
        case 2:
            gen_op_iwmmxt_cmpeql_M0_wRn(rd1);
            break;
        case 3:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x800: case 0x900: case 0xc00: case 0xd00:     /* WAVG2 */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        if (insn & (1 << 22)) {
            if (insn & (1 << 20))
                gen_op_iwmmxt_avgw1_M0_wRn(rd1);
            else
                gen_op_iwmmxt_avgw0_M0_wRn(rd1);
        } else {
            if (insn & (1 << 20))
                gen_op_iwmmxt_avgb1_M0_wRn(rd1);
            else
                gen_op_iwmmxt_avgb0_M0_wRn(rd1);
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x802: case 0x902: case 0xa02: case 0xb02:     /* WALIGNR */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        tmp = iwmmxt_load_creg(ARM_IWMMXT_wCGR0 + ((insn >> 20) & 3));
        tcg_gen_andi_i32(tmp, tmp, 7);
        iwmmxt_load_reg(cpu_V1, rd1);
        gen_helper_iwmmxt_align(cpu_M0, cpu_M0, cpu_V1, tmp);
        tcg_temp_free_i32(tmp);
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x601: case 0x605: case 0x609: case 0x60d:     /* TINSR */
        if (((insn >> 6) & 3) == 3)
            return 1;
        rd = (insn >> 12) & 0xf;
        wrd = (insn >> 16) & 0xf;
        tmp = load_reg(s, rd);
        gen_op_iwmmxt_movq_M0_wRn(wrd);
        switch ((insn >> 6) & 3) {
        case 0:
            tmp2 = tcg_const_i32(0xff);
            tmp3 = tcg_const_i32((insn & 7) << 3);
            break;
        case 1:
            tmp2 = tcg_const_i32(0xffff);
            tmp3 = tcg_const_i32((insn & 3) << 4);
            break;
        case 2:
            tmp2 = tcg_const_i32(0xffffffff);
            tmp3 = tcg_const_i32((insn & 1) << 5);
            break;
        default:
            tmp2 = NULL;
            tmp3 = NULL;
        }
        gen_helper_iwmmxt_insr(cpu_M0, cpu_M0, tmp, tmp2, tmp3);
        tcg_temp_free_i32(tmp3);
        tcg_temp_free_i32(tmp2);
        tcg_temp_free_i32(tmp);
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x107: case 0x507: case 0x907: case 0xd07:     /* TEXTRM */
        rd = (insn >> 12) & 0xf;
        wrd = (insn >> 16) & 0xf;
        if (rd == 15 || ((insn >> 22) & 3) == 3)
            return 1;
        gen_op_iwmmxt_movq_M0_wRn(wrd);
        tmp = tcg_temp_new_i32();
        switch ((insn >> 22) & 3) {
        case 0:
            tcg_gen_shri_i64(cpu_M0, cpu_M0, (insn & 7) << 3);
            tcg_gen_extrl_i64_i32(tmp, cpu_M0);
            if (insn & 8) {
                tcg_gen_ext8s_i32(tmp, tmp);
            } else {
                tcg_gen_andi_i32(tmp, tmp, 0xff);
            }
            break;
        case 1:
            tcg_gen_shri_i64(cpu_M0, cpu_M0, (insn & 3) << 4);
            tcg_gen_extrl_i64_i32(tmp, cpu_M0);
            if (insn & 8) {
                tcg_gen_ext16s_i32(tmp, tmp);
            } else {
                tcg_gen_andi_i32(tmp, tmp, 0xffff);
            }
            break;
        case 2:
            tcg_gen_shri_i64(cpu_M0, cpu_M0, (insn & 1) << 5);
            tcg_gen_extrl_i64_i32(tmp, cpu_M0);
            break;
        }
        store_reg(s, rd, tmp);
        break;
    case 0x117: case 0x517: case 0x917: case 0xd17:     /* TEXTRC */
        if ((insn & 0x000ff008) != 0x0003f000 || ((insn >> 22) & 3) == 3)
            return 1;
        tmp = iwmmxt_load_creg(ARM_IWMMXT_wCASF);
        switch ((insn >> 22) & 3) {
        case 0:
            tcg_gen_shri_i32(tmp, tmp, ((insn & 7) << 2) + 0);
            break;
        case 1:
            tcg_gen_shri_i32(tmp, tmp, ((insn & 3) << 3) + 4);
            break;
        case 2:
            tcg_gen_shri_i32(tmp, tmp, ((insn & 1) << 4) + 12);
            break;
        }
        tcg_gen_shli_i32(tmp, tmp, 28);
        gen_set_nzcv(tmp);
        tcg_temp_free_i32(tmp);
        break;
    case 0x401: case 0x405: case 0x409: case 0x40d:     /* TBCST */
        if (((insn >> 6) & 3) == 3)
            return 1;
        rd = (insn >> 12) & 0xf;
        wrd = (insn >> 16) & 0xf;
        tmp = load_reg(s, rd);
        switch ((insn >> 6) & 3) {
        case 0:
            gen_helper_iwmmxt_bcstb(cpu_M0, tmp);
            break;
        case 1:
            gen_helper_iwmmxt_bcstw(cpu_M0, tmp);
            break;
        case 2:
            gen_helper_iwmmxt_bcstl(cpu_M0, tmp);
            break;
        }
        tcg_temp_free_i32(tmp);
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x113: case 0x513: case 0x913: case 0xd13:     /* TANDC */
        if ((insn & 0x000ff00f) != 0x0003f000 || ((insn >> 22) & 3) == 3)
            return 1;
        tmp = iwmmxt_load_creg(ARM_IWMMXT_wCASF);
        tmp2 = tcg_temp_new_i32();
        tcg_gen_mov_i32(tmp2, tmp);
        switch ((insn >> 22) & 3) {
        case 0:
            for (i = 0; i < 7; i ++) {
                tcg_gen_shli_i32(tmp2, tmp2, 4);
                tcg_gen_and_i32(tmp, tmp, tmp2);
            }
            break;
        case 1:
            for (i = 0; i < 3; i ++) {
                tcg_gen_shli_i32(tmp2, tmp2, 8);
                tcg_gen_and_i32(tmp, tmp, tmp2);
            }
            break;
        case 2:
            tcg_gen_shli_i32(tmp2, tmp2, 16);
            tcg_gen_and_i32(tmp, tmp, tmp2);
            break;
        }
        gen_set_nzcv(tmp);
        tcg_temp_free_i32(tmp2);
        tcg_temp_free_i32(tmp);
        break;
    case 0x01c: case 0x41c: case 0x81c: case 0xc1c:     /* WACC */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 22) & 3) {
        case 0:
            gen_helper_iwmmxt_addcb(cpu_M0, cpu_M0);
            break;
        case 1:
            gen_helper_iwmmxt_addcw(cpu_M0, cpu_M0);
            break;
        case 2:
            gen_helper_iwmmxt_addcl(cpu_M0, cpu_M0);
            break;
        case 3:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x115: case 0x515: case 0x915: case 0xd15:     /* TORC */
        if ((insn & 0x000ff00f) != 0x0003f000 || ((insn >> 22) & 3) == 3)
            return 1;
        tmp = iwmmxt_load_creg(ARM_IWMMXT_wCASF);
        tmp2 = tcg_temp_new_i32();
        tcg_gen_mov_i32(tmp2, tmp);
        switch ((insn >> 22) & 3) {
        case 0:
            for (i = 0; i < 7; i ++) {
                tcg_gen_shli_i32(tmp2, tmp2, 4);
                tcg_gen_or_i32(tmp, tmp, tmp2);
            }
            break;
        case 1:
            for (i = 0; i < 3; i ++) {
                tcg_gen_shli_i32(tmp2, tmp2, 8);
                tcg_gen_or_i32(tmp, tmp, tmp2);
            }
            break;
        case 2:
            tcg_gen_shli_i32(tmp2, tmp2, 16);
            tcg_gen_or_i32(tmp, tmp, tmp2);
            break;
        }
        gen_set_nzcv(tmp);
        tcg_temp_free_i32(tmp2);
        tcg_temp_free_i32(tmp);
        break;
    case 0x103: case 0x503: case 0x903: case 0xd03:     /* TMOVMSK */
        rd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        if ((insn & 0xf) != 0 || ((insn >> 22) & 3) == 3)
            return 1;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        tmp = tcg_temp_new_i32();
        switch ((insn >> 22) & 3) {
        case 0:
            gen_helper_iwmmxt_msbb(tmp, cpu_M0);
            break;
        case 1:
            gen_helper_iwmmxt_msbw(tmp, cpu_M0);
            break;
        case 2:
            gen_helper_iwmmxt_msbl(tmp, cpu_M0);
            break;
        }
        store_reg(s, rd, tmp);
        break;
    case 0x106: case 0x306: case 0x506: case 0x706:     /* WCMPGT */
    case 0x906: case 0xb06: case 0xd06: case 0xf06:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 22) & 3) {
        case 0:
            if (insn & (1 << 21))
                gen_op_iwmmxt_cmpgtsb_M0_wRn(rd1);
            else
                gen_op_iwmmxt_cmpgtub_M0_wRn(rd1);
            break;
        case 1:
            if (insn & (1 << 21))
                gen_op_iwmmxt_cmpgtsw_M0_wRn(rd1);
            else
                gen_op_iwmmxt_cmpgtuw_M0_wRn(rd1);
            break;
        case 2:
            if (insn & (1 << 21))
                gen_op_iwmmxt_cmpgtsl_M0_wRn(rd1);
            else
                gen_op_iwmmxt_cmpgtul_M0_wRn(rd1);
            break;
        case 3:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x00e: case 0x20e: case 0x40e: case 0x60e:     /* WUNPCKEL */
    case 0x80e: case 0xa0e: case 0xc0e: case 0xe0e:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 22) & 3) {
        case 0:
            if (insn & (1 << 21))
                gen_op_iwmmxt_unpacklsb_M0();
            else
                gen_op_iwmmxt_unpacklub_M0();
            break;
        case 1:
            if (insn & (1 << 21))
                gen_op_iwmmxt_unpacklsw_M0();
            else
                gen_op_iwmmxt_unpackluw_M0();
            break;
        case 2:
            if (insn & (1 << 21))
                gen_op_iwmmxt_unpacklsl_M0();
            else
                gen_op_iwmmxt_unpacklul_M0();
            break;
        case 3:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x00c: case 0x20c: case 0x40c: case 0x60c:     /* WUNPCKEH */
    case 0x80c: case 0xa0c: case 0xc0c: case 0xe0c:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 22) & 3) {
        case 0:
            if (insn & (1 << 21))
                gen_op_iwmmxt_unpackhsb_M0();
            else
                gen_op_iwmmxt_unpackhub_M0();
            break;
        case 1:
            if (insn & (1 << 21))
                gen_op_iwmmxt_unpackhsw_M0();
            else
                gen_op_iwmmxt_unpackhuw_M0();
            break;
        case 2:
            if (insn & (1 << 21))
                gen_op_iwmmxt_unpackhsl_M0();
            else
                gen_op_iwmmxt_unpackhul_M0();
            break;
        case 3:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x204: case 0x604: case 0xa04: case 0xe04:     /* WSRL */
    case 0x214: case 0x614: case 0xa14: case 0xe14:
        if (((insn >> 22) & 3) == 0)
            return 1;
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        tmp = tcg_temp_new_i32();
        if (gen_iwmmxt_shift(insn, 0xff, tmp)) {
            tcg_temp_free_i32(tmp);
            return 1;
        }
        switch ((insn >> 22) & 3) {
        case 1:
            gen_helper_iwmmxt_srlw(cpu_M0, cpu_env, cpu_M0, tmp);
            break;
        case 2:
            gen_helper_iwmmxt_srll(cpu_M0, cpu_env, cpu_M0, tmp);
            break;
        case 3:
            gen_helper_iwmmxt_srlq(cpu_M0, cpu_env, cpu_M0, tmp);
            break;
        }
        tcg_temp_free_i32(tmp);
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x004: case 0x404: case 0x804: case 0xc04:     /* WSRA */
    case 0x014: case 0x414: case 0x814: case 0xc14:
        if (((insn >> 22) & 3) == 0)
            return 1;
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        tmp = tcg_temp_new_i32();
        if (gen_iwmmxt_shift(insn, 0xff, tmp)) {
            tcg_temp_free_i32(tmp);
            return 1;
        }
        switch ((insn >> 22) & 3) {
        case 1:
            gen_helper_iwmmxt_sraw(cpu_M0, cpu_env, cpu_M0, tmp);
            break;
        case 2:
            gen_helper_iwmmxt_sral(cpu_M0, cpu_env, cpu_M0, tmp);
            break;
        case 3:
            gen_helper_iwmmxt_sraq(cpu_M0, cpu_env, cpu_M0, tmp);
            break;
        }
        tcg_temp_free_i32(tmp);
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x104: case 0x504: case 0x904: case 0xd04:     /* WSLL */
    case 0x114: case 0x514: case 0x914: case 0xd14:
        if (((insn >> 22) & 3) == 0)
            return 1;
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        tmp = tcg_temp_new_i32();
        if (gen_iwmmxt_shift(insn, 0xff, tmp)) {
            tcg_temp_free_i32(tmp);
            return 1;
        }
        switch ((insn >> 22) & 3) {
        case 1:
            gen_helper_iwmmxt_sllw(cpu_M0, cpu_env, cpu_M0, tmp);
            break;
        case 2:
            gen_helper_iwmmxt_slll(cpu_M0, cpu_env, cpu_M0, tmp);
            break;
        case 3:
            gen_helper_iwmmxt_sllq(cpu_M0, cpu_env, cpu_M0, tmp);
            break;
        }
        tcg_temp_free_i32(tmp);
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x304: case 0x704: case 0xb04: case 0xf04:     /* WROR */
    case 0x314: case 0x714: case 0xb14: case 0xf14:
        if (((insn >> 22) & 3) == 0)
            return 1;
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        tmp = tcg_temp_new_i32();
        switch ((insn >> 22) & 3) {
        case 1:
            if (gen_iwmmxt_shift(insn, 0xf, tmp)) {
                tcg_temp_free_i32(tmp);
                return 1;
            }
            gen_helper_iwmmxt_rorw(cpu_M0, cpu_env, cpu_M0, tmp);
            break;
        case 2:
            if (gen_iwmmxt_shift(insn, 0x1f, tmp)) {
                tcg_temp_free_i32(tmp);
                return 1;
            }
            gen_helper_iwmmxt_rorl(cpu_M0, cpu_env, cpu_M0, tmp);
            break;
        case 3:
            if (gen_iwmmxt_shift(insn, 0x3f, tmp)) {
                tcg_temp_free_i32(tmp);
                return 1;
            }
            gen_helper_iwmmxt_rorq(cpu_M0, cpu_env, cpu_M0, tmp);
            break;
        }
        tcg_temp_free_i32(tmp);
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x116: case 0x316: case 0x516: case 0x716:     /* WMIN */
    case 0x916: case 0xb16: case 0xd16: case 0xf16:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 22) & 3) {
        case 0:
            if (insn & (1 << 21))
                gen_op_iwmmxt_minsb_M0_wRn(rd1);
            else
                gen_op_iwmmxt_minub_M0_wRn(rd1);
            break;
        case 1:
            if (insn & (1 << 21))
                gen_op_iwmmxt_minsw_M0_wRn(rd1);
            else
                gen_op_iwmmxt_minuw_M0_wRn(rd1);
            break;
        case 2:
            if (insn & (1 << 21))
                gen_op_iwmmxt_minsl_M0_wRn(rd1);
            else
                gen_op_iwmmxt_minul_M0_wRn(rd1);
            break;
        case 3:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x016: case 0x216: case 0x416: case 0x616:     /* WMAX */
    case 0x816: case 0xa16: case 0xc16: case 0xe16:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 22) & 3) {
        case 0:
            if (insn & (1 << 21))
                gen_op_iwmmxt_maxsb_M0_wRn(rd1);
            else
                gen_op_iwmmxt_maxub_M0_wRn(rd1);
            break;
        case 1:
            if (insn & (1 << 21))
                gen_op_iwmmxt_maxsw_M0_wRn(rd1);
            else
                gen_op_iwmmxt_maxuw_M0_wRn(rd1);
            break;
        case 2:
            if (insn & (1 << 21))
                gen_op_iwmmxt_maxsl_M0_wRn(rd1);
            else
                gen_op_iwmmxt_maxul_M0_wRn(rd1);
            break;
        case 3:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x002: case 0x102: case 0x202: case 0x302:     /* WALIGNI */
    case 0x402: case 0x502: case 0x602: case 0x702:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        tmp = tcg_const_i32((insn >> 20) & 3);
        iwmmxt_load_reg(cpu_V1, rd1);
        gen_helper_iwmmxt_align(cpu_M0, cpu_M0, cpu_V1, tmp);
        tcg_temp_free_i32(tmp);
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x01a: case 0x11a: case 0x21a: case 0x31a:     /* WSUB */
    case 0x41a: case 0x51a: case 0x61a: case 0x71a:
    case 0x81a: case 0x91a: case 0xa1a: case 0xb1a:
    case 0xc1a: case 0xd1a: case 0xe1a: case 0xf1a:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 20) & 0xf) {
        case 0x0:
            gen_op_iwmmxt_subnb_M0_wRn(rd1);
            break;
        case 0x1:
            gen_op_iwmmxt_subub_M0_wRn(rd1);
            break;
        case 0x3:
            gen_op_iwmmxt_subsb_M0_wRn(rd1);
            break;
        case 0x4:
            gen_op_iwmmxt_subnw_M0_wRn(rd1);
            break;
        case 0x5:
            gen_op_iwmmxt_subuw_M0_wRn(rd1);
            break;
        case 0x7:
            gen_op_iwmmxt_subsw_M0_wRn(rd1);
            break;
        case 0x8:
            gen_op_iwmmxt_subnl_M0_wRn(rd1);
            break;
        case 0x9:
            gen_op_iwmmxt_subul_M0_wRn(rd1);
            break;
        case 0xb:
            gen_op_iwmmxt_subsl_M0_wRn(rd1);
            break;
        default:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x01e: case 0x11e: case 0x21e: case 0x31e:     /* WSHUFH */
    case 0x41e: case 0x51e: case 0x61e: case 0x71e:
    case 0x81e: case 0x91e: case 0xa1e: case 0xb1e:
    case 0xc1e: case 0xd1e: case 0xe1e: case 0xf1e:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        tmp = tcg_const_i32(((insn >> 16) & 0xf0) | (insn & 0x0f));
        gen_helper_iwmmxt_shufh(cpu_M0, cpu_env, cpu_M0, tmp);
        tcg_temp_free_i32(tmp);
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x018: case 0x118: case 0x218: case 0x318:     /* WADD */
    case 0x418: case 0x518: case 0x618: case 0x718:
    case 0x818: case 0x918: case 0xa18: case 0xb18:
    case 0xc18: case 0xd18: case 0xe18: case 0xf18:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 20) & 0xf) {
        case 0x0:
            gen_op_iwmmxt_addnb_M0_wRn(rd1);
            break;
        case 0x1:
            gen_op_iwmmxt_addub_M0_wRn(rd1);
            break;
        case 0x3:
            gen_op_iwmmxt_addsb_M0_wRn(rd1);
            break;
        case 0x4:
            gen_op_iwmmxt_addnw_M0_wRn(rd1);
            break;
        case 0x5:
            gen_op_iwmmxt_adduw_M0_wRn(rd1);
            break;
        case 0x7:
            gen_op_iwmmxt_addsw_M0_wRn(rd1);
            break;
        case 0x8:
            gen_op_iwmmxt_addnl_M0_wRn(rd1);
            break;
        case 0x9:
            gen_op_iwmmxt_addul_M0_wRn(rd1);
            break;
        case 0xb:
            gen_op_iwmmxt_addsl_M0_wRn(rd1);
            break;
        default:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x008: case 0x108: case 0x208: case 0x308:     /* WPACK */
    case 0x408: case 0x508: case 0x608: case 0x708:
    case 0x808: case 0x908: case 0xa08: case 0xb08:
    case 0xc08: case 0xd08: case 0xe08: case 0xf08:
        if (!(insn & (1 << 20)) || ((insn >> 22) & 3) == 0)
            return 1;
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 22) & 3) {
        case 1:
            if (insn & (1 << 21))
                gen_op_iwmmxt_packsw_M0_wRn(rd1);
            else
                gen_op_iwmmxt_packuw_M0_wRn(rd1);
            break;
        case 2:
            if (insn & (1 << 21))
                gen_op_iwmmxt_packsl_M0_wRn(rd1);
            else
                gen_op_iwmmxt_packul_M0_wRn(rd1);
            break;
        case 3:
            if (insn & (1 << 21))
                gen_op_iwmmxt_packsq_M0_wRn(rd1);
            else
                gen_op_iwmmxt_packuq_M0_wRn(rd1);
            break;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x201: case 0x203: case 0x205: case 0x207:
    case 0x209: case 0x20b: case 0x20d: case 0x20f:
    case 0x211: case 0x213: case 0x215: case 0x217:
    case 0x219: case 0x21b: case 0x21d: case 0x21f:
        wrd = (insn >> 5) & 0xf;
        rd0 = (insn >> 12) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        if (rd0 == 0xf || rd1 == 0xf)
            return 1;
        gen_op_iwmmxt_movq_M0_wRn(wrd);
        tmp = load_reg(s, rd0);
        tmp2 = load_reg(s, rd1);
        switch ((insn >> 16) & 0xf) {
        case 0x0:                                       /* TMIA */
            gen_helper_iwmmxt_muladdsl(cpu_M0, cpu_M0, tmp, tmp2);
            break;
        case 0x8:                                       /* TMIAPH */
            gen_helper_iwmmxt_muladdsw(cpu_M0, cpu_M0, tmp, tmp2);
            break;
        case 0xc: case 0xd: case 0xe: case 0xf:                 /* TMIAxy */
            if (insn & (1 << 16))
                tcg_gen_shri_i32(tmp, tmp, 16);
            if (insn & (1 << 17))
                tcg_gen_shri_i32(tmp2, tmp2, 16);
            gen_helper_iwmmxt_muladdswl(cpu_M0, cpu_M0, tmp, tmp2);
            break;
        default:
            tcg_temp_free_i32(tmp2);
            tcg_temp_free_i32(tmp);
            return 1;
        }
        tcg_temp_free_i32(tmp2);
        tcg_temp_free_i32(tmp);
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    default:
        return 1;
    }

    return 0;
}

/* Disassemble an XScale DSP instruction.  Returns nonzero if an error occurred
   (ie. an undefined instruction).  */
static int disas_dsp_insn(DisasContext *s, uint32_t insn)
{
    int acc, rd0, rd1, rdhi, rdlo;
    TCGv_i32 tmp, tmp2;

    if ((insn & 0x0ff00f10) == 0x0e200010) {
        /* Multiply with Internal Accumulate Format */
        rd0 = (insn >> 12) & 0xf;
        rd1 = insn & 0xf;
        acc = (insn >> 5) & 7;

        if (acc != 0)
            return 1;

        tmp = load_reg(s, rd0);
        tmp2 = load_reg(s, rd1);
        switch ((insn >> 16) & 0xf) {
        case 0x0:                                       /* MIA */
            gen_helper_iwmmxt_muladdsl(cpu_M0, cpu_M0, tmp, tmp2);
            break;
        case 0x8:                                       /* MIAPH */
            gen_helper_iwmmxt_muladdsw(cpu_M0, cpu_M0, tmp, tmp2);
            break;
        case 0xc:                                       /* MIABB */
        case 0xd:                                       /* MIABT */
        case 0xe:                                       /* MIATB */
        case 0xf:                                       /* MIATT */
            if (insn & (1 << 16))
                tcg_gen_shri_i32(tmp, tmp, 16);
            if (insn & (1 << 17))
                tcg_gen_shri_i32(tmp2, tmp2, 16);
            gen_helper_iwmmxt_muladdswl(cpu_M0, cpu_M0, tmp, tmp2);
            break;
        default:
            return 1;
        }
        tcg_temp_free_i32(tmp2);
        tcg_temp_free_i32(tmp);

        gen_op_iwmmxt_movq_wRn_M0(acc);
        return 0;
    }

    if ((insn & 0x0fe00ff8) == 0x0c400000) {
        /* Internal Accumulator Access Format */
        rdhi = (insn >> 16) & 0xf;
        rdlo = (insn >> 12) & 0xf;
        acc = insn & 7;

        if (acc != 0)
            return 1;

        if (insn & ARM_CP_RW_BIT) {                     /* MRA */
            iwmmxt_load_reg(cpu_V0, acc);
            tcg_gen_extrl_i64_i32(cpu_R[rdlo], cpu_V0);
            tcg_gen_extrh_i64_i32(cpu_R[rdhi], cpu_V0);
            tcg_gen_andi_i32(cpu_R[rdhi], cpu_R[rdhi], (1 << (40 - 32)) - 1);
        } else {                                        /* MAR */
            tcg_gen_concat_i32_i64(cpu_V0, cpu_R[rdlo], cpu_R[rdhi]);
            iwmmxt_store_reg(cpu_V0, acc);
        }
        return 0;
    }

    return 1;
}

#define VFP_REG_SHR(x, n) (((n) > 0) ? (x) >> (n) : (x) << -(n))
#define VFP_SREG(insn, bigbit, smallbit) \
  ((VFP_REG_SHR(insn, bigbit - 1) & 0x1e) | (((insn) >> (smallbit)) & 1))
#define VFP_DREG(reg, insn, bigbit, smallbit) do { \
    if (dc_isar_feature(aa32_simd_r32, s)) { \
        reg = (((insn) >> (bigbit)) & 0x0f) \
              | (((insn) >> ((smallbit) - 4)) & 0x10); \
    } else { \
        if (insn & (1 << (smallbit))) \
            return 1; \
        reg = ((insn) >> (bigbit)) & 0x0f; \
    }} while (0)

#define VFP_SREG_D(insn) VFP_SREG(insn, 12, 22)
#define VFP_DREG_D(reg, insn) VFP_DREG(reg, insn, 12, 22)
#define VFP_SREG_N(insn) VFP_SREG(insn, 16,  7)
#define VFP_DREG_N(reg, insn) VFP_DREG(reg, insn, 16,  7)
#define VFP_SREG_M(insn) VFP_SREG(insn,  0,  5)
#define VFP_DREG_M(reg, insn) VFP_DREG(reg, insn,  0,  5)

static void gen_neon_dup_low16(TCGv_i32 var)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_ext16u_i32(var, var);
    tcg_gen_shli_i32(tmp, var, 16);
    tcg_gen_or_i32(var, var, tmp);
    tcg_temp_free_i32(tmp);
}

static void gen_neon_dup_high16(TCGv_i32 var)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_andi_i32(var, var, 0xffff0000);
    tcg_gen_shri_i32(tmp, var, 16);
    tcg_gen_or_i32(var, var, tmp);
    tcg_temp_free_i32(tmp);
}

static inline bool use_goto_tb(DisasContext *s, target_ulong dest)
{
#ifndef CONFIG_USER_ONLY
    return (s->base.tb->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK) ||
           ((s->base.pc_next - 1) & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK);
#else
    return true;
#endif
}

static void gen_goto_ptr(void)
{
    tcg_gen_lookup_and_goto_ptr();
}

/* This will end the TB but doesn't guarantee we'll return to
 * cpu_loop_exec. Any live exit_requests will be processed as we
 * enter the next TB.
 */
static void gen_goto_tb(DisasContext *s, int n, target_ulong dest)
{
    if (use_goto_tb(s, dest)) {
        tcg_gen_goto_tb(n);
        gen_set_pc_im(s, dest);
        tcg_gen_exit_tb(s->base.tb, n);
    } else {
        gen_set_pc_im(s, dest);
        gen_goto_ptr();
    }
    s->base.is_jmp = DISAS_NORETURN;
}

static inline void gen_jmp (DisasContext *s, uint32_t dest)
{
    if (unlikely(is_singlestepping(s))) {
        /* An indirect jump so that we still trigger the debug exception.  */
        gen_set_pc_im(s, dest);
        s->base.is_jmp = DISAS_JUMP;
    } else {
        gen_goto_tb(s, 0, dest);
    }
}

static inline void gen_mulxy(TCGv_i32 t0, TCGv_i32 t1, int x, int y)
{
    if (x)
        tcg_gen_sari_i32(t0, t0, 16);
    else
        gen_sxth(t0);
    if (y)
        tcg_gen_sari_i32(t1, t1, 16);
    else
        gen_sxth(t1);
    tcg_gen_mul_i32(t0, t0, t1);
}

/* Return the mask of PSR bits set by a MSR instruction.  */
static uint32_t msr_mask(DisasContext *s, int flags, int spsr)
{
    uint32_t mask = 0;

    if (flags & (1 << 0)) {
        mask |= 0xff;
    }
    if (flags & (1 << 1)) {
        mask |= 0xff00;
    }
    if (flags & (1 << 2)) {
        mask |= 0xff0000;
    }
    if (flags & (1 << 3)) {
        mask |= 0xff000000;
    }

    /* Mask out undefined and reserved bits.  */
    mask &= aarch32_cpsr_valid_mask(s->features, s->isar);

    /* Mask out execution state.  */
    if (!spsr) {
        mask &= ~CPSR_EXEC;
    }

    /* Mask out privileged bits.  */
    if (IS_USER(s)) {
        mask &= CPSR_USER;
    }
    return mask;
}

/* Returns nonzero if access to the PSR is not permitted. Marks t0 as dead. */
static int gen_set_psr(DisasContext *s, uint32_t mask, int spsr, TCGv_i32 t0)
{
    TCGv_i32 tmp;
    if (spsr) {
        /* ??? This is also undefined in system mode.  */
        if (IS_USER(s))
            return 1;

        tmp = load_cpu_field(spsr);
        tcg_gen_andi_i32(tmp, tmp, ~mask);
        tcg_gen_andi_i32(t0, t0, mask);
        tcg_gen_or_i32(tmp, tmp, t0);
        store_cpu_field(tmp, spsr);
    } else {
        gen_set_cpsr(t0, mask);
    }
    tcg_temp_free_i32(t0);
    gen_lookup_tb(s);
    return 0;
}

/* Returns nonzero if access to the PSR is not permitted.  */
static int gen_set_psr_im(DisasContext *s, uint32_t mask, int spsr, uint32_t val)
{
    TCGv_i32 tmp;
    tmp = tcg_temp_new_i32();
    tcg_gen_movi_i32(tmp, val);
    return gen_set_psr(s, mask, spsr, tmp);
}

static bool msr_banked_access_decode(DisasContext *s, int r, int sysm, int rn,
                                     int *tgtmode, int *regno)
{
    /* Decode the r and sysm fields of MSR/MRS banked accesses into
     * the target mode and register number, and identify the various
     * unpredictable cases.
     * MSR (banked) and MRS (banked) are CONSTRAINED UNPREDICTABLE if:
     *  + executed in user mode
     *  + using R15 as the src/dest register
     *  + accessing an unimplemented register
     *  + accessing a register that's inaccessible at current PL/security state*
     *  + accessing a register that you could access with a different insn
     * We choose to UNDEF in all these cases.
     * Since we don't know which of the various AArch32 modes we are in
     * we have to defer some checks to runtime.
     * Accesses to Monitor mode registers from Secure EL1 (which implies
     * that EL3 is AArch64) must trap to EL3.
     *
     * If the access checks fail this function will emit code to take
     * an exception and return false. Otherwise it will return true,
     * and set *tgtmode and *regno appropriately.
     */
    int exc_target = default_exception_el(s);

    /* These instructions are present only in ARMv8, or in ARMv7 with the
     * Virtualization Extensions.
     */
    if (!arm_dc_feature(s, ARM_FEATURE_V8) &&
        !arm_dc_feature(s, ARM_FEATURE_EL2)) {
        goto undef;
    }

    if (IS_USER(s) || rn == 15) {
        goto undef;
    }

    /* The table in the v8 ARM ARM section F5.2.3 describes the encoding
     * of registers into (r, sysm).
     */
    if (r) {
        /* SPSRs for other modes */
        switch (sysm) {
        case 0xe: /* SPSR_fiq */
            *tgtmode = ARM_CPU_MODE_FIQ;
            break;
        case 0x10: /* SPSR_irq */
            *tgtmode = ARM_CPU_MODE_IRQ;
            break;
        case 0x12: /* SPSR_svc */
            *tgtmode = ARM_CPU_MODE_SVC;
            break;
        case 0x14: /* SPSR_abt */
            *tgtmode = ARM_CPU_MODE_ABT;
            break;
        case 0x16: /* SPSR_und */
            *tgtmode = ARM_CPU_MODE_UND;
            break;
        case 0x1c: /* SPSR_mon */
            *tgtmode = ARM_CPU_MODE_MON;
            break;
        case 0x1e: /* SPSR_hyp */
            *tgtmode = ARM_CPU_MODE_HYP;
            break;
        default: /* unallocated */
            goto undef;
        }
        /* We arbitrarily assign SPSR a register number of 16. */
        *regno = 16;
    } else {
        /* general purpose registers for other modes */
        switch (sysm) {
        case 0x0 ... 0x6:   /* 0b00xxx : r8_usr ... r14_usr */
            *tgtmode = ARM_CPU_MODE_USR;
            *regno = sysm + 8;
            break;
        case 0x8 ... 0xe:   /* 0b01xxx : r8_fiq ... r14_fiq */
            *tgtmode = ARM_CPU_MODE_FIQ;
            *regno = sysm;
            break;
        case 0x10 ... 0x11: /* 0b1000x : r14_irq, r13_irq */
            *tgtmode = ARM_CPU_MODE_IRQ;
            *regno = sysm & 1 ? 13 : 14;
            break;
        case 0x12 ... 0x13: /* 0b1001x : r14_svc, r13_svc */
            *tgtmode = ARM_CPU_MODE_SVC;
            *regno = sysm & 1 ? 13 : 14;
            break;
        case 0x14 ... 0x15: /* 0b1010x : r14_abt, r13_abt */
            *tgtmode = ARM_CPU_MODE_ABT;
            *regno = sysm & 1 ? 13 : 14;
            break;
        case 0x16 ... 0x17: /* 0b1011x : r14_und, r13_und */
            *tgtmode = ARM_CPU_MODE_UND;
            *regno = sysm & 1 ? 13 : 14;
            break;
        case 0x1c ... 0x1d: /* 0b1110x : r14_mon, r13_mon */
            *tgtmode = ARM_CPU_MODE_MON;
            *regno = sysm & 1 ? 13 : 14;
            break;
        case 0x1e ... 0x1f: /* 0b1111x : elr_hyp, r13_hyp */
            *tgtmode = ARM_CPU_MODE_HYP;
            /* Arbitrarily pick 17 for ELR_Hyp (which is not a banked LR!) */
            *regno = sysm & 1 ? 13 : 17;
            break;
        default: /* unallocated */
            goto undef;
        }
    }

    /* Catch the 'accessing inaccessible register' cases we can detect
     * at translate time.
     */
    switch (*tgtmode) {
    case ARM_CPU_MODE_MON:
        if (!arm_dc_feature(s, ARM_FEATURE_EL3) || s->ns) {
            goto undef;
        }
        if (s->current_el == 1) {
            /* If we're in Secure EL1 (which implies that EL3 is AArch64)
             * then accesses to Mon registers trap to EL3
             */
            exc_target = 3;
            goto undef;
        }
        break;
    case ARM_CPU_MODE_HYP:
        /*
         * SPSR_hyp and r13_hyp can only be accessed from Monitor mode
         * (and so we can forbid accesses from EL2 or below). elr_hyp
         * can be accessed also from Hyp mode, so forbid accesses from
         * EL0 or EL1.
         */
        if (!arm_dc_feature(s, ARM_FEATURE_EL2) || s->current_el < 2 ||
            (s->current_el < 3 && *regno != 17)) {
            goto undef;
        }
        break;
    default:
        break;
    }

    return true;

undef:
    /* If we get here then some access check did not pass */
    gen_exception_insn(s, s->pc_curr, EXCP_UDEF,
                       syn_uncategorized(), exc_target);
    return false;
}

static void gen_msr_banked(DisasContext *s, int r, int sysm, int rn)
{
    TCGv_i32 tcg_reg, tcg_tgtmode, tcg_regno;
    int tgtmode = 0, regno = 0;

    if (!msr_banked_access_decode(s, r, sysm, rn, &tgtmode, &regno)) {
        return;
    }

    /* Sync state because msr_banked() can raise exceptions */
    gen_set_condexec(s);
    gen_set_pc_im(s, s->pc_curr);
    tcg_reg = load_reg(s, rn);
    tcg_tgtmode = tcg_const_i32(tgtmode);
    tcg_regno = tcg_const_i32(regno);
    gen_helper_msr_banked(cpu_env, tcg_reg, tcg_tgtmode, tcg_regno);
    tcg_temp_free_i32(tcg_tgtmode);
    tcg_temp_free_i32(tcg_regno);
    tcg_temp_free_i32(tcg_reg);
    s->base.is_jmp = DISAS_UPDATE;
}

static void gen_mrs_banked(DisasContext *s, int r, int sysm, int rn)
{
    TCGv_i32 tcg_reg, tcg_tgtmode, tcg_regno;
    int tgtmode = 0, regno = 0;

    if (!msr_banked_access_decode(s, r, sysm, rn, &tgtmode, &regno)) {
        return;
    }

    /* Sync state because mrs_banked() can raise exceptions */
    gen_set_condexec(s);
    gen_set_pc_im(s, s->pc_curr);
    tcg_reg = tcg_temp_new_i32();
    tcg_tgtmode = tcg_const_i32(tgtmode);
    tcg_regno = tcg_const_i32(regno);
    gen_helper_mrs_banked(tcg_reg, cpu_env, tcg_tgtmode, tcg_regno);
    tcg_temp_free_i32(tcg_tgtmode);
    tcg_temp_free_i32(tcg_regno);
    store_reg(s, rn, tcg_reg);
    s->base.is_jmp = DISAS_UPDATE;
}

/* Store value to PC as for an exception return (ie don't
 * mask bits). The subsequent call to gen_helper_cpsr_write_eret()
 * will do the masking based on the new value of the Thumb bit.
 */
static void store_pc_exc_ret(DisasContext *s, TCGv_i32 pc)
{
    tcg_gen_mov_i32(cpu_R[15], pc);
    tcg_temp_free_i32(pc);
}

/* Generate a v6 exception return.  Marks both values as dead.  */
static void gen_rfe(DisasContext *s, TCGv_i32 pc, TCGv_i32 cpsr)
{
    store_pc_exc_ret(s, pc);
    /* The cpsr_write_eret helper will mask the low bits of PC
     * appropriately depending on the new Thumb bit, so it must
     * be called after storing the new PC.
     */
    if (tb_cflags(s->base.tb) & CF_USE_ICOUNT) {
        gen_io_start();
    }
    gen_helper_cpsr_write_eret(cpu_env, cpsr);
    tcg_temp_free_i32(cpsr);
    /* Must exit loop to check un-masked IRQs */
    s->base.is_jmp = DISAS_EXIT;
}

/* Generate an old-style exception return. Marks pc as dead. */
static void gen_exception_return(DisasContext *s, TCGv_i32 pc)
{
    gen_rfe(s, pc, load_cpu_field(spsr));
}

#define CPU_V001 cpu_V0, cpu_V0, cpu_V1

static inline void gen_neon_add(int size, TCGv_i32 t0, TCGv_i32 t1)
{
    switch (size) {
    case 0: gen_helper_neon_add_u8(t0, t0, t1); break;
    case 1: gen_helper_neon_add_u16(t0, t0, t1); break;
    case 2: tcg_gen_add_i32(t0, t0, t1); break;
    default: abort();
    }
}

static inline void gen_neon_rsb(int size, TCGv_i32 t0, TCGv_i32 t1)
{
    switch (size) {
    case 0: gen_helper_neon_sub_u8(t0, t1, t0); break;
    case 1: gen_helper_neon_sub_u16(t0, t1, t0); break;
    case 2: tcg_gen_sub_i32(t0, t1, t0); break;
    default: return;
    }
}

/* 32-bit pairwise ops end up the same as the elementwise versions.  */
#define gen_helper_neon_pmax_s32  tcg_gen_smax_i32
#define gen_helper_neon_pmax_u32  tcg_gen_umax_i32
#define gen_helper_neon_pmin_s32  tcg_gen_smin_i32
#define gen_helper_neon_pmin_u32  tcg_gen_umin_i32

#define GEN_NEON_INTEGER_OP_ENV(name) do { \
    switch ((size << 1) | u) { \
    case 0: \
        gen_helper_neon_##name##_s8(tmp, cpu_env, tmp, tmp2); \
        break; \
    case 1: \
        gen_helper_neon_##name##_u8(tmp, cpu_env, tmp, tmp2); \
        break; \
    case 2: \
        gen_helper_neon_##name##_s16(tmp, cpu_env, tmp, tmp2); \
        break; \
    case 3: \
        gen_helper_neon_##name##_u16(tmp, cpu_env, tmp, tmp2); \
        break; \
    case 4: \
        gen_helper_neon_##name##_s32(tmp, cpu_env, tmp, tmp2); \
        break; \
    case 5: \
        gen_helper_neon_##name##_u32(tmp, cpu_env, tmp, tmp2); \
        break; \
    default: return 1; \
    }} while (0)

#define GEN_NEON_INTEGER_OP(name) do { \
    switch ((size << 1) | u) { \
    case 0: \
        gen_helper_neon_##name##_s8(tmp, tmp, tmp2); \
        break; \
    case 1: \
        gen_helper_neon_##name##_u8(tmp, tmp, tmp2); \
        break; \
    case 2: \
        gen_helper_neon_##name##_s16(tmp, tmp, tmp2); \
        break; \
    case 3: \
        gen_helper_neon_##name##_u16(tmp, tmp, tmp2); \
        break; \
    case 4: \
        gen_helper_neon_##name##_s32(tmp, tmp, tmp2); \
        break; \
    case 5: \
        gen_helper_neon_##name##_u32(tmp, tmp, tmp2); \
        break; \
    default: return 1; \
    }} while (0)

static TCGv_i32 neon_load_scratch(int scratch)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_ld_i32(tmp, cpu_env, offsetof(CPUARMState, vfp.scratch[scratch]));
    return tmp;
}

static void neon_store_scratch(int scratch, TCGv_i32 var)
{
    tcg_gen_st_i32(var, cpu_env, offsetof(CPUARMState, vfp.scratch[scratch]));
    tcg_temp_free_i32(var);
}

static inline TCGv_i32 neon_get_scalar(int size, int reg)
{
    TCGv_i32 tmp;
    if (size == 1) {
        tmp = neon_load_reg(reg & 7, reg >> 4);
        if (reg & 8) {
            gen_neon_dup_high16(tmp);
        } else {
            gen_neon_dup_low16(tmp);
        }
    } else {
        tmp = neon_load_reg(reg & 15, reg >> 4);
    }
    return tmp;
}

static int gen_neon_unzip(int rd, int rm, int size, int q)
{
    TCGv_ptr pd, pm;
    
    if (!q && size == 2) {
        return 1;
    }
    pd = vfp_reg_ptr(true, rd);
    pm = vfp_reg_ptr(true, rm);
    if (q) {
        switch (size) {
        case 0:
            gen_helper_neon_qunzip8(pd, pm);
            break;
        case 1:
            gen_helper_neon_qunzip16(pd, pm);
            break;
        case 2:
            gen_helper_neon_qunzip32(pd, pm);
            break;
        default:
            abort();
        }
    } else {
        switch (size) {
        case 0:
            gen_helper_neon_unzip8(pd, pm);
            break;
        case 1:
            gen_helper_neon_unzip16(pd, pm);
            break;
        default:
            abort();
        }
    }
    tcg_temp_free_ptr(pd);
    tcg_temp_free_ptr(pm);
    return 0;
}

static int gen_neon_zip(int rd, int rm, int size, int q)
{
    TCGv_ptr pd, pm;

    if (!q && size == 2) {
        return 1;
    }
    pd = vfp_reg_ptr(true, rd);
    pm = vfp_reg_ptr(true, rm);
    if (q) {
        switch (size) {
        case 0:
            gen_helper_neon_qzip8(pd, pm);
            break;
        case 1:
            gen_helper_neon_qzip16(pd, pm);
            break;
        case 2:
            gen_helper_neon_qzip32(pd, pm);
            break;
        default:
            abort();
        }
    } else {
        switch (size) {
        case 0:
            gen_helper_neon_zip8(pd, pm);
            break;
        case 1:
            gen_helper_neon_zip16(pd, pm);
            break;
        default:
            abort();
        }
    }
    tcg_temp_free_ptr(pd);
    tcg_temp_free_ptr(pm);
    return 0;
}

static void gen_neon_trn_u8(TCGv_i32 t0, TCGv_i32 t1)
{
    TCGv_i32 rd, tmp;

    rd = tcg_temp_new_i32();
    tmp = tcg_temp_new_i32();

    tcg_gen_shli_i32(rd, t0, 8);
    tcg_gen_andi_i32(rd, rd, 0xff00ff00);
    tcg_gen_andi_i32(tmp, t1, 0x00ff00ff);
    tcg_gen_or_i32(rd, rd, tmp);

    tcg_gen_shri_i32(t1, t1, 8);
    tcg_gen_andi_i32(t1, t1, 0x00ff00ff);
    tcg_gen_andi_i32(tmp, t0, 0xff00ff00);
    tcg_gen_or_i32(t1, t1, tmp);
    tcg_gen_mov_i32(t0, rd);

    tcg_temp_free_i32(tmp);
    tcg_temp_free_i32(rd);
}

static void gen_neon_trn_u16(TCGv_i32 t0, TCGv_i32 t1)
{
    TCGv_i32 rd, tmp;

    rd = tcg_temp_new_i32();
    tmp = tcg_temp_new_i32();

    tcg_gen_shli_i32(rd, t0, 16);
    tcg_gen_andi_i32(tmp, t1, 0xffff);
    tcg_gen_or_i32(rd, rd, tmp);
    tcg_gen_shri_i32(t1, t1, 16);
    tcg_gen_andi_i32(tmp, t0, 0xffff0000);
    tcg_gen_or_i32(t1, t1, tmp);
    tcg_gen_mov_i32(t0, rd);

    tcg_temp_free_i32(tmp);
    tcg_temp_free_i32(rd);
}


static struct {
    int nregs;
    int interleave;
    int spacing;
} const neon_ls_element_type[11] = {
    {1, 4, 1},
    {1, 4, 2},
    {4, 1, 1},
    {2, 2, 2},
    {1, 3, 1},
    {1, 3, 2},
    {3, 1, 1},
    {1, 1, 1},
    {1, 2, 1},
    {1, 2, 2},
    {2, 1, 1}
};

/* Translate a NEON load/store element instruction.  Return nonzero if the
   instruction is invalid.  */
static int disas_neon_ls_insn(DisasContext *s, uint32_t insn)
{
    int rd, rn, rm;
    int op;
    int nregs;
    int interleave;
    int spacing;
    int stride;
    int size;
    int reg;
    int load;
    int n;
    int vec_size;
    int mmu_idx;
    MemOp endian;
    TCGv_i32 addr;
    TCGv_i32 tmp;
    TCGv_i32 tmp2;
    TCGv_i64 tmp64;

    /* FIXME: this access check should not take precedence over UNDEF
     * for invalid encodings; we will generate incorrect syndrome information
     * for attempts to execute invalid vfp/neon encodings with FP disabled.
     */
    if (s->fp_excp_el) {
        gen_exception_insn(s, s->pc_curr, EXCP_UDEF,
                           syn_simd_access_trap(1, 0xe, false), s->fp_excp_el);
        return 0;
    }

    if (!s->vfp_enabled)
      return 1;
    VFP_DREG_D(rd, insn);
    rn = (insn >> 16) & 0xf;
    rm = insn & 0xf;
    load = (insn & (1 << 21)) != 0;
    endian = s->be_data;
    mmu_idx = get_mem_index(s);
    if ((insn & (1 << 23)) == 0) {
        /* Load store all elements.  */
        op = (insn >> 8) & 0xf;
        size = (insn >> 6) & 3;
        if (op > 10)
            return 1;
        /* Catch UNDEF cases for bad values of align field */
        switch (op & 0xc) {
        case 4:
            if (((insn >> 5) & 1) == 1) {
                return 1;
            }
            break;
        case 8:
            if (((insn >> 4) & 3) == 3) {
                return 1;
            }
            break;
        default:
            break;
        }
        nregs = neon_ls_element_type[op].nregs;
        interleave = neon_ls_element_type[op].interleave;
        spacing = neon_ls_element_type[op].spacing;
        if (size == 3 && (interleave | spacing) != 1) {
            return 1;
        }
        /* For our purposes, bytes are always little-endian.  */
        if (size == 0) {
            endian = MO_LE;
        }
        /* Consecutive little-endian elements from a single register
         * can be promoted to a larger little-endian operation.
         */
        if (interleave == 1 && endian == MO_LE) {
            size = 3;
        }
        tmp64 = tcg_temp_new_i64();
        addr = tcg_temp_new_i32();
        tmp2 = tcg_const_i32(1 << size);
        load_reg_var(s, addr, rn);
        for (reg = 0; reg < nregs; reg++) {
            for (n = 0; n < 8 >> size; n++) {
                int xs;
                for (xs = 0; xs < interleave; xs++) {
                    int tt = rd + reg + spacing * xs;

                    if (load) {
                        gen_aa32_ld_i64(s, tmp64, addr, mmu_idx, endian | size);
                        neon_store_element64(tt, n, size, tmp64);
                    } else {
                        neon_load_element64(tmp64, tt, n, size);
                        gen_aa32_st_i64(s, tmp64, addr, mmu_idx, endian | size);
                    }
                    tcg_gen_add_i32(addr, addr, tmp2);
                }
            }
        }
        tcg_temp_free_i32(addr);
        tcg_temp_free_i32(tmp2);
        tcg_temp_free_i64(tmp64);
        stride = nregs * interleave * 8;
    } else {
        size = (insn >> 10) & 3;
        if (size == 3) {
            /* Load single element to all lanes.  */
            int a = (insn >> 4) & 1;
            if (!load) {
                return 1;
            }
            size = (insn >> 6) & 3;
            nregs = ((insn >> 8) & 3) + 1;

            if (size == 3) {
                if (nregs != 4 || a == 0) {
                    return 1;
                }
                /* For VLD4 size==3 a == 1 means 32 bits at 16 byte alignment */
                size = 2;
            }
            if (nregs == 1 && a == 1 && size == 0) {
                return 1;
            }
            if (nregs == 3 && a == 1) {
                return 1;
            }
            addr = tcg_temp_new_i32();
            load_reg_var(s, addr, rn);

            /* VLD1 to all lanes: bit 5 indicates how many Dregs to write.
             * VLD2/3/4 to all lanes: bit 5 indicates register stride.
             */
            stride = (insn & (1 << 5)) ? 2 : 1;
            vec_size = nregs == 1 ? stride * 8 : 8;

            tmp = tcg_temp_new_i32();
            for (reg = 0; reg < nregs; reg++) {
                gen_aa32_ld_i32(s, tmp, addr, get_mem_index(s),
                                s->be_data | size);
                if ((rd & 1) && vec_size == 16) {
                    /* We cannot write 16 bytes at once because the
                     * destination is unaligned.
                     */
                    tcg_gen_gvec_dup_i32(size, neon_reg_offset(rd, 0),
                                         8, 8, tmp);
                    tcg_gen_gvec_mov(0, neon_reg_offset(rd + 1, 0),
                                     neon_reg_offset(rd, 0), 8, 8);
                } else {
                    tcg_gen_gvec_dup_i32(size, neon_reg_offset(rd, 0),
                                         vec_size, vec_size, tmp);
                }
                tcg_gen_addi_i32(addr, addr, 1 << size);
                rd += stride;
            }
            tcg_temp_free_i32(tmp);
            tcg_temp_free_i32(addr);
            stride = (1 << size) * nregs;
        } else {
            /* Single element.  */
            int idx = (insn >> 4) & 0xf;
            int reg_idx;
            switch (size) {
            case 0:
                reg_idx = (insn >> 5) & 7;
                stride = 1;
                break;
            case 1:
                reg_idx = (insn >> 6) & 3;
                stride = (insn & (1 << 5)) ? 2 : 1;
                break;
            case 2:
                reg_idx = (insn >> 7) & 1;
                stride = (insn & (1 << 6)) ? 2 : 1;
                break;
            default:
                abort();
            }
            nregs = ((insn >> 8) & 3) + 1;
            /* Catch the UNDEF cases. This is unavoidably a bit messy. */
            switch (nregs) {
            case 1:
                if (((idx & (1 << size)) != 0) ||
                    (size == 2 && ((idx & 3) == 1 || (idx & 3) == 2))) {
                    return 1;
                }
                break;
            case 3:
                if ((idx & 1) != 0) {
                    return 1;
                }
                /* fall through */
            case 2:
                if (size == 2 && (idx & 2) != 0) {
                    return 1;
                }
                break;
            case 4:
                if ((size == 2) && ((idx & 3) == 3)) {
                    return 1;
                }
                break;
            default:
                abort();
            }
            if ((rd + stride * (nregs - 1)) > 31) {
                /* Attempts to write off the end of the register file
                 * are UNPREDICTABLE; we choose to UNDEF because otherwise
                 * the neon_load_reg() would write off the end of the array.
                 */
                return 1;
            }
            tmp = tcg_temp_new_i32();
            addr = tcg_temp_new_i32();
            load_reg_var(s, addr, rn);
            for (reg = 0; reg < nregs; reg++) {
                if (load) {
                    gen_aa32_ld_i32(s, tmp, addr, get_mem_index(s),
                                    s->be_data | size);
                    neon_store_element(rd, reg_idx, size, tmp);
                } else { /* Store */
                    neon_load_element(tmp, rd, reg_idx, size);
                    gen_aa32_st_i32(s, tmp, addr, get_mem_index(s),
                                    s->be_data | size);
                }
                rd += stride;
                tcg_gen_addi_i32(addr, addr, 1 << size);
            }
            tcg_temp_free_i32(addr);
            tcg_temp_free_i32(tmp);
            stride = nregs * (1 << size);
        }
    }
    if (rm != 15) {
        TCGv_i32 base;

        base = load_reg(s, rn);
        if (rm == 13) {
            tcg_gen_addi_i32(base, base, stride);
        } else {
            TCGv_i32 index;
            index = load_reg(s, rm);
            tcg_gen_add_i32(base, base, index);
            tcg_temp_free_i32(index);
        }
        store_reg(s, rn, base);
    }
    return 0;
}

static inline void gen_neon_narrow(int size, TCGv_i32 dest, TCGv_i64 src)
{
    switch (size) {
    case 0: gen_helper_neon_narrow_u8(dest, src); break;
    case 1: gen_helper_neon_narrow_u16(dest, src); break;
    case 2: tcg_gen_extrl_i64_i32(dest, src); break;
    default: abort();
    }
}

static inline void gen_neon_narrow_sats(int size, TCGv_i32 dest, TCGv_i64 src)
{
    switch (size) {
    case 0: gen_helper_neon_narrow_sat_s8(dest, cpu_env, src); break;
    case 1: gen_helper_neon_narrow_sat_s16(dest, cpu_env, src); break;
    case 2: gen_helper_neon_narrow_sat_s32(dest, cpu_env, src); break;
    default: abort();
    }
}

static inline void gen_neon_narrow_satu(int size, TCGv_i32 dest, TCGv_i64 src)
{
    switch (size) {
    case 0: gen_helper_neon_narrow_sat_u8(dest, cpu_env, src); break;
    case 1: gen_helper_neon_narrow_sat_u16(dest, cpu_env, src); break;
    case 2: gen_helper_neon_narrow_sat_u32(dest, cpu_env, src); break;
    default: abort();
    }
}

static inline void gen_neon_unarrow_sats(int size, TCGv_i32 dest, TCGv_i64 src)
{
    switch (size) {
    case 0: gen_helper_neon_unarrow_sat8(dest, cpu_env, src); break;
    case 1: gen_helper_neon_unarrow_sat16(dest, cpu_env, src); break;
    case 2: gen_helper_neon_unarrow_sat32(dest, cpu_env, src); break;
    default: abort();
    }
}

static inline void gen_neon_shift_narrow(int size, TCGv_i32 var, TCGv_i32 shift,
                                         int q, int u)
{
    if (q) {
        if (u) {
            switch (size) {
            case 1: gen_helper_neon_rshl_u16(var, var, shift); break;
            case 2: gen_helper_neon_rshl_u32(var, var, shift); break;
            default: abort();
            }
        } else {
            switch (size) {
            case 1: gen_helper_neon_rshl_s16(var, var, shift); break;
            case 2: gen_helper_neon_rshl_s32(var, var, shift); break;
            default: abort();
            }
        }
    } else {
        if (u) {
            switch (size) {
            case 1: gen_helper_neon_shl_u16(var, var, shift); break;
            case 2: gen_ushl_i32(var, var, shift); break;
            default: abort();
            }
        } else {
            switch (size) {
            case 1: gen_helper_neon_shl_s16(var, var, shift); break;
            case 2: gen_sshl_i32(var, var, shift); break;
            default: abort();
            }
        }
    }
}

static inline void gen_neon_widen(TCGv_i64 dest, TCGv_i32 src, int size, int u)
{
    if (u) {
        switch (size) {
        case 0: gen_helper_neon_widen_u8(dest, src); break;
        case 1: gen_helper_neon_widen_u16(dest, src); break;
        case 2: tcg_gen_extu_i32_i64(dest, src); break;
        default: abort();
        }
    } else {
        switch (size) {
        case 0: gen_helper_neon_widen_s8(dest, src); break;
        case 1: gen_helper_neon_widen_s16(dest, src); break;
        case 2: tcg_gen_ext_i32_i64(dest, src); break;
        default: abort();
        }
    }
    tcg_temp_free_i32(src);
}

static inline void gen_neon_addl(int size)
{
    switch (size) {
    case 0: gen_helper_neon_addl_u16(CPU_V001); break;
    case 1: gen_helper_neon_addl_u32(CPU_V001); break;
    case 2: tcg_gen_add_i64(CPU_V001); break;
    default: abort();
    }
}

static inline void gen_neon_subl(int size)
{
    switch (size) {
    case 0: gen_helper_neon_subl_u16(CPU_V001); break;
    case 1: gen_helper_neon_subl_u32(CPU_V001); break;
    case 2: tcg_gen_sub_i64(CPU_V001); break;
    default: abort();
    }
}

static inline void gen_neon_negl(TCGv_i64 var, int size)
{
    switch (size) {
    case 0: gen_helper_neon_negl_u16(var, var); break;
    case 1: gen_helper_neon_negl_u32(var, var); break;
    case 2:
        tcg_gen_neg_i64(var, var);
        break;
    default: abort();
    }
}

static inline void gen_neon_addl_saturate(TCGv_i64 op0, TCGv_i64 op1, int size)
{
    switch (size) {
    case 1: gen_helper_neon_addl_saturate_s32(op0, cpu_env, op0, op1); break;
    case 2: gen_helper_neon_addl_saturate_s64(op0, cpu_env, op0, op1); break;
    default: abort();
    }
}

static inline void gen_neon_mull(TCGv_i64 dest, TCGv_i32 a, TCGv_i32 b,
                                 int size, int u)
{
    TCGv_i64 tmp;

    switch ((size << 1) | u) {
    case 0: gen_helper_neon_mull_s8(dest, a, b); break;
    case 1: gen_helper_neon_mull_u8(dest, a, b); break;
    case 2: gen_helper_neon_mull_s16(dest, a, b); break;
    case 3: gen_helper_neon_mull_u16(dest, a, b); break;
    case 4:
        tmp = gen_muls_i64_i32(a, b);
        tcg_gen_mov_i64(dest, tmp);
        tcg_temp_free_i64(tmp);
        break;
    case 5:
        tmp = gen_mulu_i64_i32(a, b);
        tcg_gen_mov_i64(dest, tmp);
        tcg_temp_free_i64(tmp);
        break;
    default: abort();
    }

    /* gen_helper_neon_mull_[su]{8|16} do not free their parameters.
       Don't forget to clean them now.  */
    if (size < 2) {
        tcg_temp_free_i32(a);
        tcg_temp_free_i32(b);
    }
}

static void gen_neon_narrow_op(int op, int u, int size,
                               TCGv_i32 dest, TCGv_i64 src)
{
    if (op) {
        if (u) {
            gen_neon_unarrow_sats(size, dest, src);
        } else {
            gen_neon_narrow(size, dest, src);
        }
    } else {
        if (u) {
            gen_neon_narrow_satu(size, dest, src);
        } else {
            gen_neon_narrow_sats(size, dest, src);
        }
    }
}

/* Symbolic constants for op fields for Neon 3-register same-length.
 * The values correspond to bits [11:8,4]; see the ARM ARM DDI0406B
 * table A7-9.
 */
#define NEON_3R_VHADD 0
#define NEON_3R_VQADD 1
#define NEON_3R_VRHADD 2
#define NEON_3R_LOGIC 3 /* VAND,VBIC,VORR,VMOV,VORN,VEOR,VBIF,VBIT,VBSL */
#define NEON_3R_VHSUB 4
#define NEON_3R_VQSUB 5
#define NEON_3R_VCGT 6
#define NEON_3R_VCGE 7
#define NEON_3R_VSHL 8
#define NEON_3R_VQSHL 9
#define NEON_3R_VRSHL 10
#define NEON_3R_VQRSHL 11
#define NEON_3R_VMAX 12
#define NEON_3R_VMIN 13
#define NEON_3R_VABD 14
#define NEON_3R_VABA 15
#define NEON_3R_VADD_VSUB 16
#define NEON_3R_VTST_VCEQ 17
#define NEON_3R_VML 18 /* VMLA, VMLS */
#define NEON_3R_VMUL 19
#define NEON_3R_VPMAX 20
#define NEON_3R_VPMIN 21
#define NEON_3R_VQDMULH_VQRDMULH 22
#define NEON_3R_VPADD_VQRDMLAH 23
#define NEON_3R_SHA 24 /* SHA1C,SHA1P,SHA1M,SHA1SU0,SHA256H{2},SHA256SU1 */
#define NEON_3R_VFM_VQRDMLSH 25 /* VFMA, VFMS, VQRDMLSH */
#define NEON_3R_FLOAT_ARITH 26 /* float VADD, VSUB, VPADD, VABD */
#define NEON_3R_FLOAT_MULTIPLY 27 /* float VMLA, VMLS, VMUL */
#define NEON_3R_FLOAT_CMP 28 /* float VCEQ, VCGE, VCGT */
#define NEON_3R_FLOAT_ACMP 29 /* float VACGE, VACGT, VACLE, VACLT */
#define NEON_3R_FLOAT_MINMAX 30 /* float VMIN, VMAX */
#define NEON_3R_FLOAT_MISC 31 /* float VRECPS, VRSQRTS, VMAXNM/MINNM */

static const uint8_t neon_3r_sizes[] = {
    [NEON_3R_VHADD] = 0x7,
    [NEON_3R_VQADD] = 0xf,
    [NEON_3R_VRHADD] = 0x7,
    [NEON_3R_LOGIC] = 0xf, /* size field encodes op type */
    [NEON_3R_VHSUB] = 0x7,
    [NEON_3R_VQSUB] = 0xf,
    [NEON_3R_VCGT] = 0x7,
    [NEON_3R_VCGE] = 0x7,
    [NEON_3R_VSHL] = 0xf,
    [NEON_3R_VQSHL] = 0xf,
    [NEON_3R_VRSHL] = 0xf,
    [NEON_3R_VQRSHL] = 0xf,
    [NEON_3R_VMAX] = 0x7,
    [NEON_3R_VMIN] = 0x7,
    [NEON_3R_VABD] = 0x7,
    [NEON_3R_VABA] = 0x7,
    [NEON_3R_VADD_VSUB] = 0xf,
    [NEON_3R_VTST_VCEQ] = 0x7,
    [NEON_3R_VML] = 0x7,
    [NEON_3R_VMUL] = 0x7,
    [NEON_3R_VPMAX] = 0x7,
    [NEON_3R_VPMIN] = 0x7,
    [NEON_3R_VQDMULH_VQRDMULH] = 0x6,
    [NEON_3R_VPADD_VQRDMLAH] = 0x7,
    [NEON_3R_SHA] = 0xf, /* size field encodes op type */
    [NEON_3R_VFM_VQRDMLSH] = 0x7, /* For VFM, size bit 1 encodes op */
    [NEON_3R_FLOAT_ARITH] = 0x5, /* size bit 1 encodes op */
    [NEON_3R_FLOAT_MULTIPLY] = 0x5, /* size bit 1 encodes op */
    [NEON_3R_FLOAT_CMP] = 0x5, /* size bit 1 encodes op */
    [NEON_3R_FLOAT_ACMP] = 0x5, /* size bit 1 encodes op */
    [NEON_3R_FLOAT_MINMAX] = 0x5, /* size bit 1 encodes op */
    [NEON_3R_FLOAT_MISC] = 0x5, /* size bit 1 encodes op */
};

/* Symbolic constants for op fields for Neon 2-register miscellaneous.
 * The values correspond to bits [17:16,10:7]; see the ARM ARM DDI0406B
 * table A7-13.
 */
#define NEON_2RM_VREV64 0
#define NEON_2RM_VREV32 1
#define NEON_2RM_VREV16 2
#define NEON_2RM_VPADDL 4
#define NEON_2RM_VPADDL_U 5
#define NEON_2RM_AESE 6 /* Includes AESD */
#define NEON_2RM_AESMC 7 /* Includes AESIMC */
#define NEON_2RM_VCLS 8
#define NEON_2RM_VCLZ 9
#define NEON_2RM_VCNT 10
#define NEON_2RM_VMVN 11
#define NEON_2RM_VPADAL 12
#define NEON_2RM_VPADAL_U 13
#define NEON_2RM_VQABS 14
#define NEON_2RM_VQNEG 15
#define NEON_2RM_VCGT0 16
#define NEON_2RM_VCGE0 17
#define NEON_2RM_VCEQ0 18
#define NEON_2RM_VCLE0 19
#define NEON_2RM_VCLT0 20
#define NEON_2RM_SHA1H 21
#define NEON_2RM_VABS 22
#define NEON_2RM_VNEG 23
#define NEON_2RM_VCGT0_F 24
#define NEON_2RM_VCGE0_F 25
#define NEON_2RM_VCEQ0_F 26
#define NEON_2RM_VCLE0_F 27
#define NEON_2RM_VCLT0_F 28
#define NEON_2RM_VABS_F 30
#define NEON_2RM_VNEG_F 31
#define NEON_2RM_VSWP 32
#define NEON_2RM_VTRN 33
#define NEON_2RM_VUZP 34
#define NEON_2RM_VZIP 35
#define NEON_2RM_VMOVN 36 /* Includes VQMOVN, VQMOVUN */
#define NEON_2RM_VQMOVN 37 /* Includes VQMOVUN */
#define NEON_2RM_VSHLL 38
#define NEON_2RM_SHA1SU1 39 /* Includes SHA256SU0 */
#define NEON_2RM_VRINTN 40
#define NEON_2RM_VRINTX 41
#define NEON_2RM_VRINTA 42
#define NEON_2RM_VRINTZ 43
#define NEON_2RM_VCVT_F16_F32 44
#define NEON_2RM_VRINTM 45
#define NEON_2RM_VCVT_F32_F16 46
#define NEON_2RM_VRINTP 47
#define NEON_2RM_VCVTAU 48
#define NEON_2RM_VCVTAS 49
#define NEON_2RM_VCVTNU 50
#define NEON_2RM_VCVTNS 51
#define NEON_2RM_VCVTPU 52
#define NEON_2RM_VCVTPS 53
#define NEON_2RM_VCVTMU 54
#define NEON_2RM_VCVTMS 55
#define NEON_2RM_VRECPE 56
#define NEON_2RM_VRSQRTE 57
#define NEON_2RM_VRECPE_F 58
#define NEON_2RM_VRSQRTE_F 59
#define NEON_2RM_VCVT_FS 60
#define NEON_2RM_VCVT_FU 61
#define NEON_2RM_VCVT_SF 62
#define NEON_2RM_VCVT_UF 63

static bool neon_2rm_is_v8_op(int op)
{
    /* Return true if this neon 2reg-misc op is ARMv8 and up */
    switch (op) {
    case NEON_2RM_VRINTN:
    case NEON_2RM_VRINTA:
    case NEON_2RM_VRINTM:
    case NEON_2RM_VRINTP:
    case NEON_2RM_VRINTZ:
    case NEON_2RM_VRINTX:
    case NEON_2RM_VCVTAU:
    case NEON_2RM_VCVTAS:
    case NEON_2RM_VCVTNU:
    case NEON_2RM_VCVTNS:
    case NEON_2RM_VCVTPU:
    case NEON_2RM_VCVTPS:
    case NEON_2RM_VCVTMU:
    case NEON_2RM_VCVTMS:
        return true;
    default:
        return false;
    }
}

/* Each entry in this array has bit n set if the insn allows
 * size value n (otherwise it will UNDEF). Since unallocated
 * op values will have no bits set they always UNDEF.
 */
static const uint8_t neon_2rm_sizes[] = {
    [NEON_2RM_VREV64] = 0x7,
    [NEON_2RM_VREV32] = 0x3,
    [NEON_2RM_VREV16] = 0x1,
    [NEON_2RM_VPADDL] = 0x7,
    [NEON_2RM_VPADDL_U] = 0x7,
    [NEON_2RM_AESE] = 0x1,
    [NEON_2RM_AESMC] = 0x1,
    [NEON_2RM_VCLS] = 0x7,
    [NEON_2RM_VCLZ] = 0x7,
    [NEON_2RM_VCNT] = 0x1,
    [NEON_2RM_VMVN] = 0x1,
    [NEON_2RM_VPADAL] = 0x7,
    [NEON_2RM_VPADAL_U] = 0x7,
    [NEON_2RM_VQABS] = 0x7,
    [NEON_2RM_VQNEG] = 0x7,
    [NEON_2RM_VCGT0] = 0x7,
    [NEON_2RM_VCGE0] = 0x7,
    [NEON_2RM_VCEQ0] = 0x7,
    [NEON_2RM_VCLE0] = 0x7,
    [NEON_2RM_VCLT0] = 0x7,
    [NEON_2RM_SHA1H] = 0x4,
    [NEON_2RM_VABS] = 0x7,
    [NEON_2RM_VNEG] = 0x7,
    [NEON_2RM_VCGT0_F] = 0x4,
    [NEON_2RM_VCGE0_F] = 0x4,
    [NEON_2RM_VCEQ0_F] = 0x4,
    [NEON_2RM_VCLE0_F] = 0x4,
    [NEON_2RM_VCLT0_F] = 0x4,
    [NEON_2RM_VABS_F] = 0x4,
    [NEON_2RM_VNEG_F] = 0x4,
    [NEON_2RM_VSWP] = 0x1,
    [NEON_2RM_VTRN] = 0x7,
    [NEON_2RM_VUZP] = 0x7,
    [NEON_2RM_VZIP] = 0x7,
    [NEON_2RM_VMOVN] = 0x7,
    [NEON_2RM_VQMOVN] = 0x7,
    [NEON_2RM_VSHLL] = 0x7,
    [NEON_2RM_SHA1SU1] = 0x4,
    [NEON_2RM_VRINTN] = 0x4,
    [NEON_2RM_VRINTX] = 0x4,
    [NEON_2RM_VRINTA] = 0x4,
    [NEON_2RM_VRINTZ] = 0x4,
    [NEON_2RM_VCVT_F16_F32] = 0x2,
    [NEON_2RM_VRINTM] = 0x4,
    [NEON_2RM_VCVT_F32_F16] = 0x2,
    [NEON_2RM_VRINTP] = 0x4,
    [NEON_2RM_VCVTAU] = 0x4,
    [NEON_2RM_VCVTAS] = 0x4,
    [NEON_2RM_VCVTNU] = 0x4,
    [NEON_2RM_VCVTNS] = 0x4,
    [NEON_2RM_VCVTPU] = 0x4,
    [NEON_2RM_VCVTPS] = 0x4,
    [NEON_2RM_VCVTMU] = 0x4,
    [NEON_2RM_VCVTMS] = 0x4,
    [NEON_2RM_VRECPE] = 0x4,
    [NEON_2RM_VRSQRTE] = 0x4,
    [NEON_2RM_VRECPE_F] = 0x4,
    [NEON_2RM_VRSQRTE_F] = 0x4,
    [NEON_2RM_VCVT_FS] = 0x4,
    [NEON_2RM_VCVT_FU] = 0x4,
    [NEON_2RM_VCVT_SF] = 0x4,
    [NEON_2RM_VCVT_UF] = 0x4,
};


/* Expand v8.1 simd helper.  */
static int do_v81_helper(DisasContext *s, gen_helper_gvec_3_ptr *fn,
                         int q, int rd, int rn, int rm)
{
    if (dc_isar_feature(aa32_rdm, s)) {
        int opr_sz = (1 + q) * 8;
        tcg_gen_gvec_3_ptr(vfp_reg_offset(1, rd),
                           vfp_reg_offset(1, rn),
                           vfp_reg_offset(1, rm), cpu_env,
                           opr_sz, opr_sz, 0, fn);
        return 0;
    }
    return 1;
}

static void gen_ssra8_i64(TCGv_i64 d, TCGv_i64 a, int64_t shift)
{
    tcg_gen_vec_sar8i_i64(a, a, shift);
    tcg_gen_vec_add8_i64(d, d, a);
}

static void gen_ssra16_i64(TCGv_i64 d, TCGv_i64 a, int64_t shift)
{
    tcg_gen_vec_sar16i_i64(a, a, shift);
    tcg_gen_vec_add16_i64(d, d, a);
}

static void gen_ssra32_i32(TCGv_i32 d, TCGv_i32 a, int32_t shift)
{
    tcg_gen_sari_i32(a, a, shift);
    tcg_gen_add_i32(d, d, a);
}

static void gen_ssra64_i64(TCGv_i64 d, TCGv_i64 a, int64_t shift)
{
    tcg_gen_sari_i64(a, a, shift);
    tcg_gen_add_i64(d, d, a);
}

static void gen_ssra_vec(unsigned vece, TCGv_vec d, TCGv_vec a, int64_t sh)
{
    tcg_gen_sari_vec(vece, a, a, sh);
    tcg_gen_add_vec(vece, d, d, a);
}

static const TCGOpcode vecop_list_ssra[] = {
    INDEX_op_sari_vec, INDEX_op_add_vec, 0
};

const GVecGen2i ssra_op[4] = {
    { .fni8 = gen_ssra8_i64,
      .fniv = gen_ssra_vec,
      .load_dest = true,
      .opt_opc = vecop_list_ssra,
      .vece = MO_8 },
    { .fni8 = gen_ssra16_i64,
      .fniv = gen_ssra_vec,
      .load_dest = true,
      .opt_opc = vecop_list_ssra,
      .vece = MO_16 },
    { .fni4 = gen_ssra32_i32,
      .fniv = gen_ssra_vec,
      .load_dest = true,
      .opt_opc = vecop_list_ssra,
      .vece = MO_32 },
    { .fni8 = gen_ssra64_i64,
      .fniv = gen_ssra_vec,
      .prefer_i64 = TCG_TARGET_REG_BITS == 64,
      .opt_opc = vecop_list_ssra,
      .load_dest = true,
      .vece = MO_64 },
};

static void gen_usra8_i64(TCGv_i64 d, TCGv_i64 a, int64_t shift)
{
    tcg_gen_vec_shr8i_i64(a, a, shift);
    tcg_gen_vec_add8_i64(d, d, a);
}

static void gen_usra16_i64(TCGv_i64 d, TCGv_i64 a, int64_t shift)
{
    tcg_gen_vec_shr16i_i64(a, a, shift);
    tcg_gen_vec_add16_i64(d, d, a);
}

static void gen_usra32_i32(TCGv_i32 d, TCGv_i32 a, int32_t shift)
{
    tcg_gen_shri_i32(a, a, shift);
    tcg_gen_add_i32(d, d, a);
}

static void gen_usra64_i64(TCGv_i64 d, TCGv_i64 a, int64_t shift)
{
    tcg_gen_shri_i64(a, a, shift);
    tcg_gen_add_i64(d, d, a);
}

static void gen_usra_vec(unsigned vece, TCGv_vec d, TCGv_vec a, int64_t sh)
{
    tcg_gen_shri_vec(vece, a, a, sh);
    tcg_gen_add_vec(vece, d, d, a);
}

static const TCGOpcode vecop_list_usra[] = {
    INDEX_op_shri_vec, INDEX_op_add_vec, 0
};

const GVecGen2i usra_op[4] = {
    { .fni8 = gen_usra8_i64,
      .fniv = gen_usra_vec,
      .load_dest = true,
      .opt_opc = vecop_list_usra,
      .vece = MO_8, },
    { .fni8 = gen_usra16_i64,
      .fniv = gen_usra_vec,
      .load_dest = true,
      .opt_opc = vecop_list_usra,
      .vece = MO_16, },
    { .fni4 = gen_usra32_i32,
      .fniv = gen_usra_vec,
      .load_dest = true,
      .opt_opc = vecop_list_usra,
      .vece = MO_32, },
    { .fni8 = gen_usra64_i64,
      .fniv = gen_usra_vec,
      .prefer_i64 = TCG_TARGET_REG_BITS == 64,
      .load_dest = true,
      .opt_opc = vecop_list_usra,
      .vece = MO_64, },
};

static void gen_shr8_ins_i64(TCGv_i64 d, TCGv_i64 a, int64_t shift)
{
    uint64_t mask = dup_const(MO_8, 0xff >> shift);
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_shri_i64(t, a, shift);
    tcg_gen_andi_i64(t, t, mask);
    tcg_gen_andi_i64(d, d, ~mask);
    tcg_gen_or_i64(d, d, t);
    tcg_temp_free_i64(t);
}

static void gen_shr16_ins_i64(TCGv_i64 d, TCGv_i64 a, int64_t shift)
{
    uint64_t mask = dup_const(MO_16, 0xffff >> shift);
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_shri_i64(t, a, shift);
    tcg_gen_andi_i64(t, t, mask);
    tcg_gen_andi_i64(d, d, ~mask);
    tcg_gen_or_i64(d, d, t);
    tcg_temp_free_i64(t);
}

static void gen_shr32_ins_i32(TCGv_i32 d, TCGv_i32 a, int32_t shift)
{
    tcg_gen_shri_i32(a, a, shift);
    tcg_gen_deposit_i32(d, d, a, 0, 32 - shift);
}

static void gen_shr64_ins_i64(TCGv_i64 d, TCGv_i64 a, int64_t shift)
{
    tcg_gen_shri_i64(a, a, shift);
    tcg_gen_deposit_i64(d, d, a, 0, 64 - shift);
}

static void gen_shr_ins_vec(unsigned vece, TCGv_vec d, TCGv_vec a, int64_t sh)
{
    if (sh == 0) {
        tcg_gen_mov_vec(d, a);
    } else {
        TCGv_vec t = tcg_temp_new_vec_matching(d);
        TCGv_vec m = tcg_temp_new_vec_matching(d);

        tcg_gen_dupi_vec(vece, m, MAKE_64BIT_MASK((8 << vece) - sh, sh));
        tcg_gen_shri_vec(vece, t, a, sh);
        tcg_gen_and_vec(vece, d, d, m);
        tcg_gen_or_vec(vece, d, d, t);

        tcg_temp_free_vec(t);
        tcg_temp_free_vec(m);
    }
}

static const TCGOpcode vecop_list_sri[] = { INDEX_op_shri_vec, 0 };

const GVecGen2i sri_op[4] = {
    { .fni8 = gen_shr8_ins_i64,
      .fniv = gen_shr_ins_vec,
      .load_dest = true,
      .opt_opc = vecop_list_sri,
      .vece = MO_8 },
    { .fni8 = gen_shr16_ins_i64,
      .fniv = gen_shr_ins_vec,
      .load_dest = true,
      .opt_opc = vecop_list_sri,
      .vece = MO_16 },
    { .fni4 = gen_shr32_ins_i32,
      .fniv = gen_shr_ins_vec,
      .load_dest = true,
      .opt_opc = vecop_list_sri,
      .vece = MO_32 },
    { .fni8 = gen_shr64_ins_i64,
      .fniv = gen_shr_ins_vec,
      .prefer_i64 = TCG_TARGET_REG_BITS == 64,
      .load_dest = true,
      .opt_opc = vecop_list_sri,
      .vece = MO_64 },
};

static void gen_shl8_ins_i64(TCGv_i64 d, TCGv_i64 a, int64_t shift)
{
    uint64_t mask = dup_const(MO_8, 0xff << shift);
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_shli_i64(t, a, shift);
    tcg_gen_andi_i64(t, t, mask);
    tcg_gen_andi_i64(d, d, ~mask);
    tcg_gen_or_i64(d, d, t);
    tcg_temp_free_i64(t);
}

static void gen_shl16_ins_i64(TCGv_i64 d, TCGv_i64 a, int64_t shift)
{
    uint64_t mask = dup_const(MO_16, 0xffff << shift);
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_shli_i64(t, a, shift);
    tcg_gen_andi_i64(t, t, mask);
    tcg_gen_andi_i64(d, d, ~mask);
    tcg_gen_or_i64(d, d, t);
    tcg_temp_free_i64(t);
}

static void gen_shl32_ins_i32(TCGv_i32 d, TCGv_i32 a, int32_t shift)
{
    tcg_gen_deposit_i32(d, d, a, shift, 32 - shift);
}

static void gen_shl64_ins_i64(TCGv_i64 d, TCGv_i64 a, int64_t shift)
{
    tcg_gen_deposit_i64(d, d, a, shift, 64 - shift);
}

static void gen_shl_ins_vec(unsigned vece, TCGv_vec d, TCGv_vec a, int64_t sh)
{
    if (sh == 0) {
        tcg_gen_mov_vec(d, a);
    } else {
        TCGv_vec t = tcg_temp_new_vec_matching(d);
        TCGv_vec m = tcg_temp_new_vec_matching(d);

        tcg_gen_dupi_vec(vece, m, MAKE_64BIT_MASK(0, sh));
        tcg_gen_shli_vec(vece, t, a, sh);
        tcg_gen_and_vec(vece, d, d, m);
        tcg_gen_or_vec(vece, d, d, t);

        tcg_temp_free_vec(t);
        tcg_temp_free_vec(m);
    }
}

static const TCGOpcode vecop_list_sli[] = { INDEX_op_shli_vec, 0 };

const GVecGen2i sli_op[4] = {
    { .fni8 = gen_shl8_ins_i64,
      .fniv = gen_shl_ins_vec,
      .load_dest = true,
      .opt_opc = vecop_list_sli,
      .vece = MO_8 },
    { .fni8 = gen_shl16_ins_i64,
      .fniv = gen_shl_ins_vec,
      .load_dest = true,
      .opt_opc = vecop_list_sli,
      .vece = MO_16 },
    { .fni4 = gen_shl32_ins_i32,
      .fniv = gen_shl_ins_vec,
      .load_dest = true,
      .opt_opc = vecop_list_sli,
      .vece = MO_32 },
    { .fni8 = gen_shl64_ins_i64,
      .fniv = gen_shl_ins_vec,
      .prefer_i64 = TCG_TARGET_REG_BITS == 64,
      .load_dest = true,
      .opt_opc = vecop_list_sli,
      .vece = MO_64 },
};

static void gen_mla8_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b)
{
    gen_helper_neon_mul_u8(a, a, b);
    gen_helper_neon_add_u8(d, d, a);
}

static void gen_mls8_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b)
{
    gen_helper_neon_mul_u8(a, a, b);
    gen_helper_neon_sub_u8(d, d, a);
}

static void gen_mla16_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b)
{
    gen_helper_neon_mul_u16(a, a, b);
    gen_helper_neon_add_u16(d, d, a);
}

static void gen_mls16_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b)
{
    gen_helper_neon_mul_u16(a, a, b);
    gen_helper_neon_sub_u16(d, d, a);
}

static void gen_mla32_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b)
{
    tcg_gen_mul_i32(a, a, b);
    tcg_gen_add_i32(d, d, a);
}

static void gen_mls32_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b)
{
    tcg_gen_mul_i32(a, a, b);
    tcg_gen_sub_i32(d, d, a);
}

static void gen_mla64_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    tcg_gen_mul_i64(a, a, b);
    tcg_gen_add_i64(d, d, a);
}

static void gen_mls64_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    tcg_gen_mul_i64(a, a, b);
    tcg_gen_sub_i64(d, d, a);
}

static void gen_mla_vec(unsigned vece, TCGv_vec d, TCGv_vec a, TCGv_vec b)
{
    tcg_gen_mul_vec(vece, a, a, b);
    tcg_gen_add_vec(vece, d, d, a);
}

static void gen_mls_vec(unsigned vece, TCGv_vec d, TCGv_vec a, TCGv_vec b)
{
    tcg_gen_mul_vec(vece, a, a, b);
    tcg_gen_sub_vec(vece, d, d, a);
}

/* Note that while NEON does not support VMLA and VMLS as 64-bit ops,
 * these tables are shared with AArch64 which does support them.
 */

static const TCGOpcode vecop_list_mla[] = {
    INDEX_op_mul_vec, INDEX_op_add_vec, 0
};

static const TCGOpcode vecop_list_mls[] = {
    INDEX_op_mul_vec, INDEX_op_sub_vec, 0
};

const GVecGen3 mla_op[4] = {
    { .fni4 = gen_mla8_i32,
      .fniv = gen_mla_vec,
      .load_dest = true,
      .opt_opc = vecop_list_mla,
      .vece = MO_8 },
    { .fni4 = gen_mla16_i32,
      .fniv = gen_mla_vec,
      .load_dest = true,
      .opt_opc = vecop_list_mla,
      .vece = MO_16 },
    { .fni4 = gen_mla32_i32,
      .fniv = gen_mla_vec,
      .load_dest = true,
      .opt_opc = vecop_list_mla,
      .vece = MO_32 },
    { .fni8 = gen_mla64_i64,
      .fniv = gen_mla_vec,
      .prefer_i64 = TCG_TARGET_REG_BITS == 64,
      .load_dest = true,
      .opt_opc = vecop_list_mla,
      .vece = MO_64 },
};

const GVecGen3 mls_op[4] = {
    { .fni4 = gen_mls8_i32,
      .fniv = gen_mls_vec,
      .load_dest = true,
      .opt_opc = vecop_list_mls,
      .vece = MO_8 },
    { .fni4 = gen_mls16_i32,
      .fniv = gen_mls_vec,
      .load_dest = true,
      .opt_opc = vecop_list_mls,
      .vece = MO_16 },
    { .fni4 = gen_mls32_i32,
      .fniv = gen_mls_vec,
      .load_dest = true,
      .opt_opc = vecop_list_mls,
      .vece = MO_32 },
    { .fni8 = gen_mls64_i64,
      .fniv = gen_mls_vec,
      .prefer_i64 = TCG_TARGET_REG_BITS == 64,
      .load_dest = true,
      .opt_opc = vecop_list_mls,
      .vece = MO_64 },
};

/* CMTST : test is "if (X & Y != 0)". */
static void gen_cmtst_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b)
{
    tcg_gen_and_i32(d, a, b);
    tcg_gen_setcondi_i32(TCG_COND_NE, d, d, 0);
    tcg_gen_neg_i32(d, d);
}

void gen_cmtst_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    tcg_gen_and_i64(d, a, b);
    tcg_gen_setcondi_i64(TCG_COND_NE, d, d, 0);
    tcg_gen_neg_i64(d, d);
}

static void gen_cmtst_vec(unsigned vece, TCGv_vec d, TCGv_vec a, TCGv_vec b)
{
    tcg_gen_and_vec(vece, d, a, b);
    tcg_gen_dupi_vec(vece, a, 0);
    tcg_gen_cmp_vec(TCG_COND_NE, vece, d, d, a);
}

static const TCGOpcode vecop_list_cmtst[] = { INDEX_op_cmp_vec, 0 };

const GVecGen3 cmtst_op[4] = {
    { .fni4 = gen_helper_neon_tst_u8,
      .fniv = gen_cmtst_vec,
      .opt_opc = vecop_list_cmtst,
      .vece = MO_8 },
    { .fni4 = gen_helper_neon_tst_u16,
      .fniv = gen_cmtst_vec,
      .opt_opc = vecop_list_cmtst,
      .vece = MO_16 },
    { .fni4 = gen_cmtst_i32,
      .fniv = gen_cmtst_vec,
      .opt_opc = vecop_list_cmtst,
      .vece = MO_32 },
    { .fni8 = gen_cmtst_i64,
      .fniv = gen_cmtst_vec,
      .prefer_i64 = TCG_TARGET_REG_BITS == 64,
      .opt_opc = vecop_list_cmtst,
      .vece = MO_64 },
};

void gen_ushl_i32(TCGv_i32 dst, TCGv_i32 src, TCGv_i32 shift)
{
    TCGv_i32 lval = tcg_temp_new_i32();
    TCGv_i32 rval = tcg_temp_new_i32();
    TCGv_i32 lsh = tcg_temp_new_i32();
    TCGv_i32 rsh = tcg_temp_new_i32();
    TCGv_i32 zero = tcg_const_i32(0);
    TCGv_i32 max = tcg_const_i32(32);

    /*
     * Rely on the TCG guarantee that out of range shifts produce
     * unspecified results, not undefined behaviour (i.e. no trap).
     * Discard out-of-range results after the fact.
     */
    tcg_gen_ext8s_i32(lsh, shift);
    tcg_gen_neg_i32(rsh, lsh);
    tcg_gen_shl_i32(lval, src, lsh);
    tcg_gen_shr_i32(rval, src, rsh);
    tcg_gen_movcond_i32(TCG_COND_LTU, dst, lsh, max, lval, zero);
    tcg_gen_movcond_i32(TCG_COND_LTU, dst, rsh, max, rval, dst);

    tcg_temp_free_i32(lval);
    tcg_temp_free_i32(rval);
    tcg_temp_free_i32(lsh);
    tcg_temp_free_i32(rsh);
    tcg_temp_free_i32(zero);
    tcg_temp_free_i32(max);
}

void gen_ushl_i64(TCGv_i64 dst, TCGv_i64 src, TCGv_i64 shift)
{
    TCGv_i64 lval = tcg_temp_new_i64();
    TCGv_i64 rval = tcg_temp_new_i64();
    TCGv_i64 lsh = tcg_temp_new_i64();
    TCGv_i64 rsh = tcg_temp_new_i64();
    TCGv_i64 zero = tcg_const_i64(0);
    TCGv_i64 max = tcg_const_i64(64);

    /*
     * Rely on the TCG guarantee that out of range shifts produce
     * unspecified results, not undefined behaviour (i.e. no trap).
     * Discard out-of-range results after the fact.
     */
    tcg_gen_ext8s_i64(lsh, shift);
    tcg_gen_neg_i64(rsh, lsh);
    tcg_gen_shl_i64(lval, src, lsh);
    tcg_gen_shr_i64(rval, src, rsh);
    tcg_gen_movcond_i64(TCG_COND_LTU, dst, lsh, max, lval, zero);
    tcg_gen_movcond_i64(TCG_COND_LTU, dst, rsh, max, rval, dst);

    tcg_temp_free_i64(lval);
    tcg_temp_free_i64(rval);
    tcg_temp_free_i64(lsh);
    tcg_temp_free_i64(rsh);
    tcg_temp_free_i64(zero);
    tcg_temp_free_i64(max);
}

static void gen_ushl_vec(unsigned vece, TCGv_vec dst,
                         TCGv_vec src, TCGv_vec shift)
{
    TCGv_vec lval = tcg_temp_new_vec_matching(dst);
    TCGv_vec rval = tcg_temp_new_vec_matching(dst);
    TCGv_vec lsh = tcg_temp_new_vec_matching(dst);
    TCGv_vec rsh = tcg_temp_new_vec_matching(dst);
    TCGv_vec msk, max;

    tcg_gen_neg_vec(vece, rsh, shift);
    if (vece == MO_8) {
        tcg_gen_mov_vec(lsh, shift);
    } else {
        msk = tcg_temp_new_vec_matching(dst);
        tcg_gen_dupi_vec(vece, msk, 0xff);
        tcg_gen_and_vec(vece, lsh, shift, msk);
        tcg_gen_and_vec(vece, rsh, rsh, msk);
        tcg_temp_free_vec(msk);
    }

    /*
     * Rely on the TCG guarantee that out of range shifts produce
     * unspecified results, not undefined behaviour (i.e. no trap).
     * Discard out-of-range results after the fact.
     */
    tcg_gen_shlv_vec(vece, lval, src, lsh);
    tcg_gen_shrv_vec(vece, rval, src, rsh);

    max = tcg_temp_new_vec_matching(dst);
    tcg_gen_dupi_vec(vece, max, 8 << vece);

    /*
     * The choice of LT (signed) and GEU (unsigned) are biased toward
     * the instructions of the x86_64 host.  For MO_8, the whole byte
     * is significant so we must use an unsigned compare; otherwise we
     * have already masked to a byte and so a signed compare works.
     * Other tcg hosts have a full set of comparisons and do not care.
     */
    if (vece == MO_8) {
        tcg_gen_cmp_vec(TCG_COND_GEU, vece, lsh, lsh, max);
        tcg_gen_cmp_vec(TCG_COND_GEU, vece, rsh, rsh, max);
        tcg_gen_andc_vec(vece, lval, lval, lsh);
        tcg_gen_andc_vec(vece, rval, rval, rsh);
    } else {
        tcg_gen_cmp_vec(TCG_COND_LT, vece, lsh, lsh, max);
        tcg_gen_cmp_vec(TCG_COND_LT, vece, rsh, rsh, max);
        tcg_gen_and_vec(vece, lval, lval, lsh);
        tcg_gen_and_vec(vece, rval, rval, rsh);
    }
    tcg_gen_or_vec(vece, dst, lval, rval);

    tcg_temp_free_vec(max);
    tcg_temp_free_vec(lval);
    tcg_temp_free_vec(rval);
    tcg_temp_free_vec(lsh);
    tcg_temp_free_vec(rsh);
}

static const TCGOpcode ushl_list[] = {
    INDEX_op_neg_vec, INDEX_op_shlv_vec,
    INDEX_op_shrv_vec, INDEX_op_cmp_vec, 0
};

const GVecGen3 ushl_op[4] = {
    { .fniv = gen_ushl_vec,
      .fno = gen_helper_gvec_ushl_b,
      .opt_opc = ushl_list,
      .vece = MO_8 },
    { .fniv = gen_ushl_vec,
      .fno = gen_helper_gvec_ushl_h,
      .opt_opc = ushl_list,
      .vece = MO_16 },
    { .fni4 = gen_ushl_i32,
      .fniv = gen_ushl_vec,
      .opt_opc = ushl_list,
      .vece = MO_32 },
    { .fni8 = gen_ushl_i64,
      .fniv = gen_ushl_vec,
      .opt_opc = ushl_list,
      .vece = MO_64 },
};

void gen_sshl_i32(TCGv_i32 dst, TCGv_i32 src, TCGv_i32 shift)
{
    TCGv_i32 lval = tcg_temp_new_i32();
    TCGv_i32 rval = tcg_temp_new_i32();
    TCGv_i32 lsh = tcg_temp_new_i32();
    TCGv_i32 rsh = tcg_temp_new_i32();
    TCGv_i32 zero = tcg_const_i32(0);
    TCGv_i32 max = tcg_const_i32(31);

    /*
     * Rely on the TCG guarantee that out of range shifts produce
     * unspecified results, not undefined behaviour (i.e. no trap).
     * Discard out-of-range results after the fact.
     */
    tcg_gen_ext8s_i32(lsh, shift);
    tcg_gen_neg_i32(rsh, lsh);
    tcg_gen_shl_i32(lval, src, lsh);
    tcg_gen_umin_i32(rsh, rsh, max);
    tcg_gen_sar_i32(rval, src, rsh);
    tcg_gen_movcond_i32(TCG_COND_LEU, lval, lsh, max, lval, zero);
    tcg_gen_movcond_i32(TCG_COND_LT, dst, lsh, zero, rval, lval);

    tcg_temp_free_i32(lval);
    tcg_temp_free_i32(rval);
    tcg_temp_free_i32(lsh);
    tcg_temp_free_i32(rsh);
    tcg_temp_free_i32(zero);
    tcg_temp_free_i32(max);
}

void gen_sshl_i64(TCGv_i64 dst, TCGv_i64 src, TCGv_i64 shift)
{
    TCGv_i64 lval = tcg_temp_new_i64();
    TCGv_i64 rval = tcg_temp_new_i64();
    TCGv_i64 lsh = tcg_temp_new_i64();
    TCGv_i64 rsh = tcg_temp_new_i64();
    TCGv_i64 zero = tcg_const_i64(0);
    TCGv_i64 max = tcg_const_i64(63);

    /*
     * Rely on the TCG guarantee that out of range shifts produce
     * unspecified results, not undefined behaviour (i.e. no trap).
     * Discard out-of-range results after the fact.
     */
    tcg_gen_ext8s_i64(lsh, shift);
    tcg_gen_neg_i64(rsh, lsh);
    tcg_gen_shl_i64(lval, src, lsh);
    tcg_gen_umin_i64(rsh, rsh, max);
    tcg_gen_sar_i64(rval, src, rsh);
    tcg_gen_movcond_i64(TCG_COND_LEU, lval, lsh, max, lval, zero);
    tcg_gen_movcond_i64(TCG_COND_LT, dst, lsh, zero, rval, lval);

    tcg_temp_free_i64(lval);
    tcg_temp_free_i64(rval);
    tcg_temp_free_i64(lsh);
    tcg_temp_free_i64(rsh);
    tcg_temp_free_i64(zero);
    tcg_temp_free_i64(max);
}

static void gen_sshl_vec(unsigned vece, TCGv_vec dst,
                         TCGv_vec src, TCGv_vec shift)
{
    TCGv_vec lval = tcg_temp_new_vec_matching(dst);
    TCGv_vec rval = tcg_temp_new_vec_matching(dst);
    TCGv_vec lsh = tcg_temp_new_vec_matching(dst);
    TCGv_vec rsh = tcg_temp_new_vec_matching(dst);
    TCGv_vec tmp = tcg_temp_new_vec_matching(dst);

    /*
     * Rely on the TCG guarantee that out of range shifts produce
     * unspecified results, not undefined behaviour (i.e. no trap).
     * Discard out-of-range results after the fact.
     */
    tcg_gen_neg_vec(vece, rsh, shift);
    if (vece == MO_8) {
        tcg_gen_mov_vec(lsh, shift);
    } else {
        tcg_gen_dupi_vec(vece, tmp, 0xff);
        tcg_gen_and_vec(vece, lsh, shift, tmp);
        tcg_gen_and_vec(vece, rsh, rsh, tmp);
    }

    /* Bound rsh so out of bound right shift gets -1.  */
    tcg_gen_dupi_vec(vece, tmp, (8 << vece) - 1);
    tcg_gen_umin_vec(vece, rsh, rsh, tmp);
    tcg_gen_cmp_vec(TCG_COND_GT, vece, tmp, lsh, tmp);

    tcg_gen_shlv_vec(vece, lval, src, lsh);
    tcg_gen_sarv_vec(vece, rval, src, rsh);

    /* Select in-bound left shift.  */
    tcg_gen_andc_vec(vece, lval, lval, tmp);

    /* Select between left and right shift.  */
    if (vece == MO_8) {
        tcg_gen_dupi_vec(vece, tmp, 0);
        tcg_gen_cmpsel_vec(TCG_COND_LT, vece, dst, lsh, tmp, rval, lval);
    } else {
        tcg_gen_dupi_vec(vece, tmp, 0x80);
        tcg_gen_cmpsel_vec(TCG_COND_LT, vece, dst, lsh, tmp, lval, rval);
    }

    tcg_temp_free_vec(lval);
    tcg_temp_free_vec(rval);
    tcg_temp_free_vec(lsh);
    tcg_temp_free_vec(rsh);
    tcg_temp_free_vec(tmp);
}

static const TCGOpcode sshl_list[] = {
    INDEX_op_neg_vec, INDEX_op_umin_vec, INDEX_op_shlv_vec,
    INDEX_op_sarv_vec, INDEX_op_cmp_vec, INDEX_op_cmpsel_vec, 0
};

const GVecGen3 sshl_op[4] = {
    { .fniv = gen_sshl_vec,
      .fno = gen_helper_gvec_sshl_b,
      .opt_opc = sshl_list,
      .vece = MO_8 },
    { .fniv = gen_sshl_vec,
      .fno = gen_helper_gvec_sshl_h,
      .opt_opc = sshl_list,
      .vece = MO_16 },
    { .fni4 = gen_sshl_i32,
      .fniv = gen_sshl_vec,
      .opt_opc = sshl_list,
      .vece = MO_32 },
    { .fni8 = gen_sshl_i64,
      .fniv = gen_sshl_vec,
      .opt_opc = sshl_list,
      .vece = MO_64 },
};

static void gen_uqadd_vec(unsigned vece, TCGv_vec t, TCGv_vec sat,
                          TCGv_vec a, TCGv_vec b)
{
    TCGv_vec x = tcg_temp_new_vec_matching(t);
    tcg_gen_add_vec(vece, x, a, b);
    tcg_gen_usadd_vec(vece, t, a, b);
    tcg_gen_cmp_vec(TCG_COND_NE, vece, x, x, t);
    tcg_gen_or_vec(vece, sat, sat, x);
    tcg_temp_free_vec(x);
}

static const TCGOpcode vecop_list_uqadd[] = {
    INDEX_op_usadd_vec, INDEX_op_cmp_vec, INDEX_op_add_vec, 0
};

const GVecGen4 uqadd_op[4] = {
    { .fniv = gen_uqadd_vec,
      .fno = gen_helper_gvec_uqadd_b,
      .write_aofs = true,
      .opt_opc = vecop_list_uqadd,
      .vece = MO_8 },
    { .fniv = gen_uqadd_vec,
      .fno = gen_helper_gvec_uqadd_h,
      .write_aofs = true,
      .opt_opc = vecop_list_uqadd,
      .vece = MO_16 },
    { .fniv = gen_uqadd_vec,
      .fno = gen_helper_gvec_uqadd_s,
      .write_aofs = true,
      .opt_opc = vecop_list_uqadd,
      .vece = MO_32 },
    { .fniv = gen_uqadd_vec,
      .fno = gen_helper_gvec_uqadd_d,
      .write_aofs = true,
      .opt_opc = vecop_list_uqadd,
      .vece = MO_64 },
};

static void gen_sqadd_vec(unsigned vece, TCGv_vec t, TCGv_vec sat,
                          TCGv_vec a, TCGv_vec b)
{
    TCGv_vec x = tcg_temp_new_vec_matching(t);
    tcg_gen_add_vec(vece, x, a, b);
    tcg_gen_ssadd_vec(vece, t, a, b);
    tcg_gen_cmp_vec(TCG_COND_NE, vece, x, x, t);
    tcg_gen_or_vec(vece, sat, sat, x);
    tcg_temp_free_vec(x);
}

static const TCGOpcode vecop_list_sqadd[] = {
    INDEX_op_ssadd_vec, INDEX_op_cmp_vec, INDEX_op_add_vec, 0
};

const GVecGen4 sqadd_op[4] = {
    { .fniv = gen_sqadd_vec,
      .fno = gen_helper_gvec_sqadd_b,
      .opt_opc = vecop_list_sqadd,
      .write_aofs = true,
      .vece = MO_8 },
    { .fniv = gen_sqadd_vec,
      .fno = gen_helper_gvec_sqadd_h,
      .opt_opc = vecop_list_sqadd,
      .write_aofs = true,
      .vece = MO_16 },
    { .fniv = gen_sqadd_vec,
      .fno = gen_helper_gvec_sqadd_s,
      .opt_opc = vecop_list_sqadd,
      .write_aofs = true,
      .vece = MO_32 },
    { .fniv = gen_sqadd_vec,
      .fno = gen_helper_gvec_sqadd_d,
      .opt_opc = vecop_list_sqadd,
      .write_aofs = true,
      .vece = MO_64 },
};

static void gen_uqsub_vec(unsigned vece, TCGv_vec t, TCGv_vec sat,
                          TCGv_vec a, TCGv_vec b)
{
    TCGv_vec x = tcg_temp_new_vec_matching(t);
    tcg_gen_sub_vec(vece, x, a, b);
    tcg_gen_ussub_vec(vece, t, a, b);
    tcg_gen_cmp_vec(TCG_COND_NE, vece, x, x, t);
    tcg_gen_or_vec(vece, sat, sat, x);
    tcg_temp_free_vec(x);
}

static const TCGOpcode vecop_list_uqsub[] = {
    INDEX_op_ussub_vec, INDEX_op_cmp_vec, INDEX_op_sub_vec, 0
};

const GVecGen4 uqsub_op[4] = {
    { .fniv = gen_uqsub_vec,
      .fno = gen_helper_gvec_uqsub_b,
      .opt_opc = vecop_list_uqsub,
      .write_aofs = true,
      .vece = MO_8 },
    { .fniv = gen_uqsub_vec,
      .fno = gen_helper_gvec_uqsub_h,
      .opt_opc = vecop_list_uqsub,
      .write_aofs = true,
      .vece = MO_16 },
    { .fniv = gen_uqsub_vec,
      .fno = gen_helper_gvec_uqsub_s,
      .opt_opc = vecop_list_uqsub,
      .write_aofs = true,
      .vece = MO_32 },
    { .fniv = gen_uqsub_vec,
      .fno = gen_helper_gvec_uqsub_d,
      .opt_opc = vecop_list_uqsub,
      .write_aofs = true,
      .vece = MO_64 },
};

static void gen_sqsub_vec(unsigned vece, TCGv_vec t, TCGv_vec sat,
                          TCGv_vec a, TCGv_vec b)
{
    TCGv_vec x = tcg_temp_new_vec_matching(t);
    tcg_gen_sub_vec(vece, x, a, b);
    tcg_gen_sssub_vec(vece, t, a, b);
    tcg_gen_cmp_vec(TCG_COND_NE, vece, x, x, t);
    tcg_gen_or_vec(vece, sat, sat, x);
    tcg_temp_free_vec(x);
}

static const TCGOpcode vecop_list_sqsub[] = {
    INDEX_op_sssub_vec, INDEX_op_cmp_vec, INDEX_op_sub_vec, 0
};

const GVecGen4 sqsub_op[4] = {
    { .fniv = gen_sqsub_vec,
      .fno = gen_helper_gvec_sqsub_b,
      .opt_opc = vecop_list_sqsub,
      .write_aofs = true,
      .vece = MO_8 },
    { .fniv = gen_sqsub_vec,
      .fno = gen_helper_gvec_sqsub_h,
      .opt_opc = vecop_list_sqsub,
      .write_aofs = true,
      .vece = MO_16 },
    { .fniv = gen_sqsub_vec,
      .fno = gen_helper_gvec_sqsub_s,
      .opt_opc = vecop_list_sqsub,
      .write_aofs = true,
      .vece = MO_32 },
    { .fniv = gen_sqsub_vec,
      .fno = gen_helper_gvec_sqsub_d,
      .opt_opc = vecop_list_sqsub,
      .write_aofs = true,
      .vece = MO_64 },
};

/* Translate a NEON data processing instruction.  Return nonzero if the
   instruction is invalid.
   We process data in a mixture of 32-bit and 64-bit chunks.
   Mostly we use 32-bit chunks so we can use normal scalar instructions.  */

static int disas_neon_data_insn(DisasContext *s, uint32_t insn)
{
    int op;
    int q;
    int rd, rn, rm, rd_ofs, rn_ofs, rm_ofs;
    int size;
    int shift;
    int pass;
    int count;
    int pairwise;
    int u;
    int vec_size;
    uint32_t imm;
    TCGv_i32 tmp, tmp2, tmp3, tmp4, tmp5;
    TCGv_ptr ptr1, ptr2, ptr3;
    TCGv_i64 tmp64;

    /* FIXME: this access check should not take precedence over UNDEF
     * for invalid encodings; we will generate incorrect syndrome information
     * for attempts to execute invalid vfp/neon encodings with FP disabled.
     */
    if (s->fp_excp_el) {
        gen_exception_insn(s, s->pc_curr, EXCP_UDEF,
                           syn_simd_access_trap(1, 0xe, false), s->fp_excp_el);
        return 0;
    }

    if (!s->vfp_enabled)
      return 1;
    q = (insn & (1 << 6)) != 0;
    u = (insn >> 24) & 1;
    VFP_DREG_D(rd, insn);
    VFP_DREG_N(rn, insn);
    VFP_DREG_M(rm, insn);
    size = (insn >> 20) & 3;
    vec_size = q ? 16 : 8;
    rd_ofs = neon_reg_offset(rd, 0);
    rn_ofs = neon_reg_offset(rn, 0);
    rm_ofs = neon_reg_offset(rm, 0);

    if ((insn & (1 << 23)) == 0) {
        /* Three register same length.  */
        op = ((insn >> 7) & 0x1e) | ((insn >> 4) & 1);
        /* Catch invalid op and bad size combinations: UNDEF */
        if ((neon_3r_sizes[op] & (1 << size)) == 0) {
            return 1;
        }
        /* All insns of this form UNDEF for either this condition or the
         * superset of cases "Q==1"; we catch the latter later.
         */
        if (q && ((rd | rn | rm) & 1)) {
            return 1;
        }
        switch (op) {
        case NEON_3R_SHA:
            /* The SHA-1/SHA-256 3-register instructions require special
             * treatment here, as their size field is overloaded as an
             * op type selector, and they all consume their input in a
             * single pass.
             */
            if (!q) {
                return 1;
            }
            if (!u) { /* SHA-1 */
                if (!dc_isar_feature(aa32_sha1, s)) {
                    return 1;
                }
                ptr1 = vfp_reg_ptr(true, rd);
                ptr2 = vfp_reg_ptr(true, rn);
                ptr3 = vfp_reg_ptr(true, rm);
                tmp4 = tcg_const_i32(size);
                gen_helper_crypto_sha1_3reg(ptr1, ptr2, ptr3, tmp4);
                tcg_temp_free_i32(tmp4);
            } else { /* SHA-256 */
                if (!dc_isar_feature(aa32_sha2, s) || size == 3) {
                    return 1;
                }
                ptr1 = vfp_reg_ptr(true, rd);
                ptr2 = vfp_reg_ptr(true, rn);
                ptr3 = vfp_reg_ptr(true, rm);
                switch (size) {
                case 0:
                    gen_helper_crypto_sha256h(ptr1, ptr2, ptr3);
                    break;
                case 1:
                    gen_helper_crypto_sha256h2(ptr1, ptr2, ptr3);
                    break;
                case 2:
                    gen_helper_crypto_sha256su1(ptr1, ptr2, ptr3);
                    break;
                }
            }
            tcg_temp_free_ptr(ptr1);
            tcg_temp_free_ptr(ptr2);
            tcg_temp_free_ptr(ptr3);
            return 0;

        case NEON_3R_VPADD_VQRDMLAH:
            if (!u) {
                break;  /* VPADD */
            }
            /* VQRDMLAH */
            switch (size) {
            case 1:
                return do_v81_helper(s, gen_helper_gvec_qrdmlah_s16,
                                     q, rd, rn, rm);
            case 2:
                return do_v81_helper(s, gen_helper_gvec_qrdmlah_s32,
                                     q, rd, rn, rm);
            }
            return 1;

        case NEON_3R_VFM_VQRDMLSH:
            if (!u) {
                /* VFM, VFMS */
                if (size == 1) {
                    return 1;
                }
                break;
            }
            /* VQRDMLSH */
            switch (size) {
            case 1:
                return do_v81_helper(s, gen_helper_gvec_qrdmlsh_s16,
                                     q, rd, rn, rm);
            case 2:
                return do_v81_helper(s, gen_helper_gvec_qrdmlsh_s32,
                                     q, rd, rn, rm);
            }
            return 1;

        case NEON_3R_LOGIC: /* Logic ops.  */
            switch ((u << 2) | size) {
            case 0: /* VAND */
                tcg_gen_gvec_and(0, rd_ofs, rn_ofs, rm_ofs,
                                 vec_size, vec_size);
                break;
            case 1: /* VBIC */
                tcg_gen_gvec_andc(0, rd_ofs, rn_ofs, rm_ofs,
                                  vec_size, vec_size);
                break;
            case 2: /* VORR */
                tcg_gen_gvec_or(0, rd_ofs, rn_ofs, rm_ofs,
                                vec_size, vec_size);
                break;
            case 3: /* VORN */
                tcg_gen_gvec_orc(0, rd_ofs, rn_ofs, rm_ofs,
                                 vec_size, vec_size);
                break;
            case 4: /* VEOR */
                tcg_gen_gvec_xor(0, rd_ofs, rn_ofs, rm_ofs,
                                 vec_size, vec_size);
                break;
            case 5: /* VBSL */
                tcg_gen_gvec_bitsel(MO_8, rd_ofs, rd_ofs, rn_ofs, rm_ofs,
                                    vec_size, vec_size);
                break;
            case 6: /* VBIT */
                tcg_gen_gvec_bitsel(MO_8, rd_ofs, rm_ofs, rn_ofs, rd_ofs,
                                    vec_size, vec_size);
                break;
            case 7: /* VBIF */
                tcg_gen_gvec_bitsel(MO_8, rd_ofs, rm_ofs, rd_ofs, rn_ofs,
                                    vec_size, vec_size);
                break;
            }
            return 0;

        case NEON_3R_VADD_VSUB:
            if (u) {
                tcg_gen_gvec_sub(size, rd_ofs, rn_ofs, rm_ofs,
                                 vec_size, vec_size);
            } else {
                tcg_gen_gvec_add(size, rd_ofs, rn_ofs, rm_ofs,
                                 vec_size, vec_size);
            }
            return 0;

        case NEON_3R_VQADD:
            tcg_gen_gvec_4(rd_ofs, offsetof(CPUARMState, vfp.qc),
                           rn_ofs, rm_ofs, vec_size, vec_size,
                           (u ? uqadd_op : sqadd_op) + size);
            return 0;

        case NEON_3R_VQSUB:
            tcg_gen_gvec_4(rd_ofs, offsetof(CPUARMState, vfp.qc),
                           rn_ofs, rm_ofs, vec_size, vec_size,
                           (u ? uqsub_op : sqsub_op) + size);
            return 0;

        case NEON_3R_VMUL: /* VMUL */
            if (u) {
                /* Polynomial case allows only P8.  */
                if (size != 0) {
                    return 1;
                }
                tcg_gen_gvec_3_ool(rd_ofs, rn_ofs, rm_ofs, vec_size, vec_size,
                                   0, gen_helper_gvec_pmul_b);
            } else {
                tcg_gen_gvec_mul(size, rd_ofs, rn_ofs, rm_ofs,
                                 vec_size, vec_size);
            }
            return 0;

        case NEON_3R_VML: /* VMLA, VMLS */
            tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, vec_size, vec_size,
                           u ? &mls_op[size] : &mla_op[size]);
            return 0;

        case NEON_3R_VTST_VCEQ:
            if (u) { /* VCEQ */
                tcg_gen_gvec_cmp(TCG_COND_EQ, size, rd_ofs, rn_ofs, rm_ofs,
                                 vec_size, vec_size);
            } else { /* VTST */
                tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs,
                               vec_size, vec_size, &cmtst_op[size]);
            }
            return 0;

        case NEON_3R_VCGT:
            tcg_gen_gvec_cmp(u ? TCG_COND_GTU : TCG_COND_GT, size,
                             rd_ofs, rn_ofs, rm_ofs, vec_size, vec_size);
            return 0;

        case NEON_3R_VCGE:
            tcg_gen_gvec_cmp(u ? TCG_COND_GEU : TCG_COND_GE, size,
                             rd_ofs, rn_ofs, rm_ofs, vec_size, vec_size);
            return 0;

        case NEON_3R_VMAX:
            if (u) {
                tcg_gen_gvec_umax(size, rd_ofs, rn_ofs, rm_ofs,
                                  vec_size, vec_size);
            } else {
                tcg_gen_gvec_smax(size, rd_ofs, rn_ofs, rm_ofs,
                                  vec_size, vec_size);
            }
            return 0;
        case NEON_3R_VMIN:
            if (u) {
                tcg_gen_gvec_umin(size, rd_ofs, rn_ofs, rm_ofs,
                                  vec_size, vec_size);
            } else {
                tcg_gen_gvec_smin(size, rd_ofs, rn_ofs, rm_ofs,
                                  vec_size, vec_size);
            }
            return 0;

        case NEON_3R_VSHL:
            /* Note the operation is vshl vd,vm,vn */
            tcg_gen_gvec_3(rd_ofs, rm_ofs, rn_ofs, vec_size, vec_size,
                           u ? &ushl_op[size] : &sshl_op[size]);
            return 0;
        }

        if (size == 3) {
            /* 64-bit element instructions. */
            for (pass = 0; pass < (q ? 2 : 1); pass++) {
                neon_load_reg64(cpu_V0, rn + pass);
                neon_load_reg64(cpu_V1, rm + pass);
                switch (op) {
                case NEON_3R_VQSHL:
                    if (u) {
                        gen_helper_neon_qshl_u64(cpu_V0, cpu_env,
                                                 cpu_V1, cpu_V0);
                    } else {
                        gen_helper_neon_qshl_s64(cpu_V0, cpu_env,
                                                 cpu_V1, cpu_V0);
                    }
                    break;
                case NEON_3R_VRSHL:
                    if (u) {
                        gen_helper_neon_rshl_u64(cpu_V0, cpu_V1, cpu_V0);
                    } else {
                        gen_helper_neon_rshl_s64(cpu_V0, cpu_V1, cpu_V0);
                    }
                    break;
                case NEON_3R_VQRSHL:
                    if (u) {
                        gen_helper_neon_qrshl_u64(cpu_V0, cpu_env,
                                                  cpu_V1, cpu_V0);
                    } else {
                        gen_helper_neon_qrshl_s64(cpu_V0, cpu_env,
                                                  cpu_V1, cpu_V0);
                    }
                    break;
                default:
                    abort();
                }
                neon_store_reg64(cpu_V0, rd + pass);
            }
            return 0;
        }
        pairwise = 0;
        switch (op) {
        case NEON_3R_VQSHL:
        case NEON_3R_VRSHL:
        case NEON_3R_VQRSHL:
            {
                int rtmp;
                /* Shift instruction operands are reversed.  */
                rtmp = rn;
                rn = rm;
                rm = rtmp;
            }
            break;
        case NEON_3R_VPADD_VQRDMLAH:
        case NEON_3R_VPMAX:
        case NEON_3R_VPMIN:
            pairwise = 1;
            break;
        case NEON_3R_FLOAT_ARITH:
            pairwise = (u && size < 2); /* if VPADD (float) */
            break;
        case NEON_3R_FLOAT_MINMAX:
            pairwise = u; /* if VPMIN/VPMAX (float) */
            break;
        case NEON_3R_FLOAT_CMP:
            if (!u && size) {
                /* no encoding for U=0 C=1x */
                return 1;
            }
            break;
        case NEON_3R_FLOAT_ACMP:
            if (!u) {
                return 1;
            }
            break;
        case NEON_3R_FLOAT_MISC:
            /* VMAXNM/VMINNM in ARMv8 */
            if (u && !arm_dc_feature(s, ARM_FEATURE_V8)) {
                return 1;
            }
            break;
        case NEON_3R_VFM_VQRDMLSH:
            if (!dc_isar_feature(aa32_simdfmac, s)) {
                return 1;
            }
            break;
        default:
            break;
        }

        if (pairwise && q) {
            /* All the pairwise insns UNDEF if Q is set */
            return 1;
        }

        for (pass = 0; pass < (q ? 4 : 2); pass++) {

        if (pairwise) {
            /* Pairwise.  */
            if (pass < 1) {
                tmp = neon_load_reg(rn, 0);
                tmp2 = neon_load_reg(rn, 1);
            } else {
                tmp = neon_load_reg(rm, 0);
                tmp2 = neon_load_reg(rm, 1);
            }
        } else {
            /* Elementwise.  */
            tmp = neon_load_reg(rn, pass);
            tmp2 = neon_load_reg(rm, pass);
        }
        switch (op) {
        case NEON_3R_VHADD:
            GEN_NEON_INTEGER_OP(hadd);
            break;
        case NEON_3R_VRHADD:
            GEN_NEON_INTEGER_OP(rhadd);
            break;
        case NEON_3R_VHSUB:
            GEN_NEON_INTEGER_OP(hsub);
            break;
        case NEON_3R_VQSHL:
            GEN_NEON_INTEGER_OP_ENV(qshl);
            break;
        case NEON_3R_VRSHL:
            GEN_NEON_INTEGER_OP(rshl);
            break;
        case NEON_3R_VQRSHL:
            GEN_NEON_INTEGER_OP_ENV(qrshl);
            break;
        case NEON_3R_VABD:
            GEN_NEON_INTEGER_OP(abd);
            break;
        case NEON_3R_VABA:
            GEN_NEON_INTEGER_OP(abd);
            tcg_temp_free_i32(tmp2);
            tmp2 = neon_load_reg(rd, pass);
            gen_neon_add(size, tmp, tmp2);
            break;
        case NEON_3R_VPMAX:
            GEN_NEON_INTEGER_OP(pmax);
            break;
        case NEON_3R_VPMIN:
            GEN_NEON_INTEGER_OP(pmin);
            break;
        case NEON_3R_VQDMULH_VQRDMULH: /* Multiply high.  */
            if (!u) { /* VQDMULH */
                switch (size) {
                case 1:
                    gen_helper_neon_qdmulh_s16(tmp, cpu_env, tmp, tmp2);
                    break;
                case 2:
                    gen_helper_neon_qdmulh_s32(tmp, cpu_env, tmp, tmp2);
                    break;
                default: abort();
                }
            } else { /* VQRDMULH */
                switch (size) {
                case 1:
                    gen_helper_neon_qrdmulh_s16(tmp, cpu_env, tmp, tmp2);
                    break;
                case 2:
                    gen_helper_neon_qrdmulh_s32(tmp, cpu_env, tmp, tmp2);
                    break;
                default: abort();
                }
            }
            break;
        case NEON_3R_VPADD_VQRDMLAH:
            switch (size) {
            case 0: gen_helper_neon_padd_u8(tmp, tmp, tmp2); break;
            case 1: gen_helper_neon_padd_u16(tmp, tmp, tmp2); break;
            case 2: tcg_gen_add_i32(tmp, tmp, tmp2); break;
            default: abort();
            }
            break;
        case NEON_3R_FLOAT_ARITH: /* Floating point arithmetic. */
        {
            TCGv_ptr fpstatus = get_fpstatus_ptr(1);
            switch ((u << 2) | size) {
            case 0: /* VADD */
            case 4: /* VPADD */
                gen_helper_vfp_adds(tmp, tmp, tmp2, fpstatus);
                break;
            case 2: /* VSUB */
                gen_helper_vfp_subs(tmp, tmp, tmp2, fpstatus);
                break;
            case 6: /* VABD */
                gen_helper_neon_abd_f32(tmp, tmp, tmp2, fpstatus);
                break;
            default:
                abort();
            }
            tcg_temp_free_ptr(fpstatus);
            break;
        }
        case NEON_3R_FLOAT_MULTIPLY:
        {
            TCGv_ptr fpstatus = get_fpstatus_ptr(1);
            gen_helper_vfp_muls(tmp, tmp, tmp2, fpstatus);
            if (!u) {
                tcg_temp_free_i32(tmp2);
                tmp2 = neon_load_reg(rd, pass);
                if (size == 0) {
                    gen_helper_vfp_adds(tmp, tmp, tmp2, fpstatus);
                } else {
                    gen_helper_vfp_subs(tmp, tmp2, tmp, fpstatus);
                }
            }
            tcg_temp_free_ptr(fpstatus);
            break;
        }
        case NEON_3R_FLOAT_CMP:
        {
            TCGv_ptr fpstatus = get_fpstatus_ptr(1);
            if (!u) {
                gen_helper_neon_ceq_f32(tmp, tmp, tmp2, fpstatus);
            } else {
                if (size == 0) {
                    gen_helper_neon_cge_f32(tmp, tmp, tmp2, fpstatus);
                } else {
                    gen_helper_neon_cgt_f32(tmp, tmp, tmp2, fpstatus);
                }
            }
            tcg_temp_free_ptr(fpstatus);
            break;
        }
        case NEON_3R_FLOAT_ACMP:
        {
            TCGv_ptr fpstatus = get_fpstatus_ptr(1);
            if (size == 0) {
                gen_helper_neon_acge_f32(tmp, tmp, tmp2, fpstatus);
            } else {
                gen_helper_neon_acgt_f32(tmp, tmp, tmp2, fpstatus);
            }
            tcg_temp_free_ptr(fpstatus);
            break;
        }
        case NEON_3R_FLOAT_MINMAX:
        {
            TCGv_ptr fpstatus = get_fpstatus_ptr(1);
            if (size == 0) {
                gen_helper_vfp_maxs(tmp, tmp, tmp2, fpstatus);
            } else {
                gen_helper_vfp_mins(tmp, tmp, tmp2, fpstatus);
            }
            tcg_temp_free_ptr(fpstatus);
            break;
        }
        case NEON_3R_FLOAT_MISC:
            if (u) {
                /* VMAXNM/VMINNM */
                TCGv_ptr fpstatus = get_fpstatus_ptr(1);
                if (size == 0) {
                    gen_helper_vfp_maxnums(tmp, tmp, tmp2, fpstatus);
                } else {
                    gen_helper_vfp_minnums(tmp, tmp, tmp2, fpstatus);
                }
                tcg_temp_free_ptr(fpstatus);
            } else {
                if (size == 0) {
                    gen_helper_recps_f32(tmp, tmp, tmp2, cpu_env);
                } else {
                    gen_helper_rsqrts_f32(tmp, tmp, tmp2, cpu_env);
              }
            }
            break;
        case NEON_3R_VFM_VQRDMLSH:
        {
            /* VFMA, VFMS: fused multiply-add */
            TCGv_ptr fpstatus = get_fpstatus_ptr(1);
            TCGv_i32 tmp3 = neon_load_reg(rd, pass);
            if (size) {
                /* VFMS */
                gen_helper_vfp_negs(tmp, tmp);
            }
            gen_helper_vfp_muladds(tmp, tmp, tmp2, tmp3, fpstatus);
            tcg_temp_free_i32(tmp3);
            tcg_temp_free_ptr(fpstatus);
            break;
        }
        default:
            abort();
        }
        tcg_temp_free_i32(tmp2);

        /* Save the result.  For elementwise operations we can put it
           straight into the destination register.  For pairwise operations
           we have to be careful to avoid clobbering the source operands.  */
        if (pairwise && rd == rm) {
            neon_store_scratch(pass, tmp);
        } else {
            neon_store_reg(rd, pass, tmp);
        }

        } /* for pass */
        if (pairwise && rd == rm) {
            for (pass = 0; pass < (q ? 4 : 2); pass++) {
                tmp = neon_load_scratch(pass);
                neon_store_reg(rd, pass, tmp);
            }
        }
        /* End of 3 register same size operations.  */
    } else if (insn & (1 << 4)) {
        if ((insn & 0x00380080) != 0) {
            /* Two registers and shift.  */
            op = (insn >> 8) & 0xf;
            if (insn & (1 << 7)) {
                /* 64-bit shift. */
                if (op > 7) {
                    return 1;
                }
                size = 3;
            } else {
                size = 2;
                while ((insn & (1 << (size + 19))) == 0)
                    size--;
            }
            shift = (insn >> 16) & ((1 << (3 + size)) - 1);
            if (op < 8) {
                /* Shift by immediate:
                   VSHR, VSRA, VRSHR, VRSRA, VSRI, VSHL, VQSHL, VQSHLU.  */
                if (q && ((rd | rm) & 1)) {
                    return 1;
                }
                if (!u && (op == 4 || op == 6)) {
                    return 1;
                }
                /* Right shifts are encoded as N - shift, where N is the
                   element size in bits.  */
                if (op <= 4) {
                    shift = shift - (1 << (size + 3));
                }

                switch (op) {
                case 0:  /* VSHR */
                    /* Right shift comes here negative.  */
                    shift = -shift;
                    /* Shifts larger than the element size are architecturally
                     * valid.  Unsigned results in all zeros; signed results
                     * in all sign bits.
                     */
                    if (!u) {
                        tcg_gen_gvec_sari(size, rd_ofs, rm_ofs,
                                          MIN(shift, (8 << size) - 1),
                                          vec_size, vec_size);
                    } else if (shift >= 8 << size) {
                        tcg_gen_gvec_dup8i(rd_ofs, vec_size, vec_size, 0);
                    } else {
                        tcg_gen_gvec_shri(size, rd_ofs, rm_ofs, shift,
                                          vec_size, vec_size);
                    }
                    return 0;

                case 1:  /* VSRA */
                    /* Right shift comes here negative.  */
                    shift = -shift;
                    /* Shifts larger than the element size are architecturally
                     * valid.  Unsigned results in all zeros; signed results
                     * in all sign bits.
                     */
                    if (!u) {
                        tcg_gen_gvec_2i(rd_ofs, rm_ofs, vec_size, vec_size,
                                        MIN(shift, (8 << size) - 1),
                                        &ssra_op[size]);
                    } else if (shift >= 8 << size) {
                        /* rd += 0 */
                    } else {
                        tcg_gen_gvec_2i(rd_ofs, rm_ofs, vec_size, vec_size,
                                        shift, &usra_op[size]);
                    }
                    return 0;

                case 4: /* VSRI */
                    if (!u) {
                        return 1;
                    }
                    /* Right shift comes here negative.  */
                    shift = -shift;
                    /* Shift out of range leaves destination unchanged.  */
                    if (shift < 8 << size) {
                        tcg_gen_gvec_2i(rd_ofs, rm_ofs, vec_size, vec_size,
                                        shift, &sri_op[size]);
                    }
                    return 0;

                case 5: /* VSHL, VSLI */
                    if (u) { /* VSLI */
                        /* Shift out of range leaves destination unchanged.  */
                        if (shift < 8 << size) {
                            tcg_gen_gvec_2i(rd_ofs, rm_ofs, vec_size,
                                            vec_size, shift, &sli_op[size]);
                        }
                    } else { /* VSHL */
                        /* Shifts larger than the element size are
                         * architecturally valid and results in zero.
                         */
                        if (shift >= 8 << size) {
                            tcg_gen_gvec_dup8i(rd_ofs, vec_size, vec_size, 0);
                        } else {
                            tcg_gen_gvec_shli(size, rd_ofs, rm_ofs, shift,
                                              vec_size, vec_size);
                        }
                    }
                    return 0;
                }

                if (size == 3) {
                    count = q + 1;
                } else {
                    count = q ? 4: 2;
                }

                /* To avoid excessive duplication of ops we implement shift
                 * by immediate using the variable shift operations.
                  */
                imm = dup_const(size, shift);

                for (pass = 0; pass < count; pass++) {
                    if (size == 3) {
                        neon_load_reg64(cpu_V0, rm + pass);
                        tcg_gen_movi_i64(cpu_V1, imm);
                        switch (op) {
                        case 2: /* VRSHR */
                        case 3: /* VRSRA */
                            if (u)
                                gen_helper_neon_rshl_u64(cpu_V0, cpu_V0, cpu_V1);
                            else
                                gen_helper_neon_rshl_s64(cpu_V0, cpu_V0, cpu_V1);
                            break;
                        case 6: /* VQSHLU */
                            gen_helper_neon_qshlu_s64(cpu_V0, cpu_env,
                                                      cpu_V0, cpu_V1);
                            break;
                        case 7: /* VQSHL */
                            if (u) {
                                gen_helper_neon_qshl_u64(cpu_V0, cpu_env,
                                                         cpu_V0, cpu_V1);
                            } else {
                                gen_helper_neon_qshl_s64(cpu_V0, cpu_env,
                                                         cpu_V0, cpu_V1);
                            }
                            break;
                        default:
                            g_assert_not_reached();
                        }
                        if (op == 3) {
                            /* Accumulate.  */
                            neon_load_reg64(cpu_V1, rd + pass);
                            tcg_gen_add_i64(cpu_V0, cpu_V0, cpu_V1);
                        }
                        neon_store_reg64(cpu_V0, rd + pass);
                    } else { /* size < 3 */
                        /* Operands in T0 and T1.  */
                        tmp = neon_load_reg(rm, pass);
                        tmp2 = tcg_temp_new_i32();
                        tcg_gen_movi_i32(tmp2, imm);
                        switch (op) {
                        case 2: /* VRSHR */
                        case 3: /* VRSRA */
                            GEN_NEON_INTEGER_OP(rshl);
                            break;
                        case 6: /* VQSHLU */
                            switch (size) {
                            case 0:
                                gen_helper_neon_qshlu_s8(tmp, cpu_env,
                                                         tmp, tmp2);
                                break;
                            case 1:
                                gen_helper_neon_qshlu_s16(tmp, cpu_env,
                                                          tmp, tmp2);
                                break;
                            case 2:
                                gen_helper_neon_qshlu_s32(tmp, cpu_env,
                                                          tmp, tmp2);
                                break;
                            default:
                                abort();
                            }
                            break;
                        case 7: /* VQSHL */
                            GEN_NEON_INTEGER_OP_ENV(qshl);
                            break;
                        default:
                            g_assert_not_reached();
                        }
                        tcg_temp_free_i32(tmp2);

                        if (op == 3) {
                            /* Accumulate.  */
                            tmp2 = neon_load_reg(rd, pass);
                            gen_neon_add(size, tmp, tmp2);
                            tcg_temp_free_i32(tmp2);
                        }
                        neon_store_reg(rd, pass, tmp);
                    }
                } /* for pass */
            } else if (op < 10) {
                /* Shift by immediate and narrow:
                   VSHRN, VRSHRN, VQSHRN, VQRSHRN.  */
                int input_unsigned = (op == 8) ? !u : u;
                if (rm & 1) {
                    return 1;
                }
                shift = shift - (1 << (size + 3));
                size++;
                if (size == 3) {
                    tmp64 = tcg_const_i64(shift);
                    neon_load_reg64(cpu_V0, rm);
                    neon_load_reg64(cpu_V1, rm + 1);
                    for (pass = 0; pass < 2; pass++) {
                        TCGv_i64 in;
                        if (pass == 0) {
                            in = cpu_V0;
                        } else {
                            in = cpu_V1;
                        }
                        if (q) {
                            if (input_unsigned) {
                                gen_helper_neon_rshl_u64(cpu_V0, in, tmp64);
                            } else {
                                gen_helper_neon_rshl_s64(cpu_V0, in, tmp64);
                            }
                        } else {
                            if (input_unsigned) {
                                gen_ushl_i64(cpu_V0, in, tmp64);
                            } else {
                                gen_sshl_i64(cpu_V0, in, tmp64);
                            }
                        }
                        tmp = tcg_temp_new_i32();
                        gen_neon_narrow_op(op == 8, u, size - 1, tmp, cpu_V0);
                        neon_store_reg(rd, pass, tmp);
                    } /* for pass */
                    tcg_temp_free_i64(tmp64);
                } else {
                    if (size == 1) {
                        imm = (uint16_t)shift;
                        imm |= imm << 16;
                    } else {
                        /* size == 2 */
                        imm = (uint32_t)shift;
                    }
                    tmp2 = tcg_const_i32(imm);
                    tmp4 = neon_load_reg(rm + 1, 0);
                    tmp5 = neon_load_reg(rm + 1, 1);
                    for (pass = 0; pass < 2; pass++) {
                        if (pass == 0) {
                            tmp = neon_load_reg(rm, 0);
                        } else {
                            tmp = tmp4;
                        }
                        gen_neon_shift_narrow(size, tmp, tmp2, q,
                                              input_unsigned);
                        if (pass == 0) {
                            tmp3 = neon_load_reg(rm, 1);
                        } else {
                            tmp3 = tmp5;
                        }
                        gen_neon_shift_narrow(size, tmp3, tmp2, q,
                                              input_unsigned);
                        tcg_gen_concat_i32_i64(cpu_V0, tmp, tmp3);
                        tcg_temp_free_i32(tmp);
                        tcg_temp_free_i32(tmp3);
                        tmp = tcg_temp_new_i32();
                        gen_neon_narrow_op(op == 8, u, size - 1, tmp, cpu_V0);
                        neon_store_reg(rd, pass, tmp);
                    } /* for pass */
                    tcg_temp_free_i32(tmp2);
                }
            } else if (op == 10) {
                /* VSHLL, VMOVL */
                if (q || (rd & 1)) {
                    return 1;
                }
                tmp = neon_load_reg(rm, 0);
                tmp2 = neon_load_reg(rm, 1);
                for (pass = 0; pass < 2; pass++) {
                    if (pass == 1)
                        tmp = tmp2;

                    gen_neon_widen(cpu_V0, tmp, size, u);

                    if (shift != 0) {
                        /* The shift is less than the width of the source
                           type, so we can just shift the whole register.  */
                        tcg_gen_shli_i64(cpu_V0, cpu_V0, shift);
                        /* Widen the result of shift: we need to clear
                         * the potential overflow bits resulting from
                         * left bits of the narrow input appearing as
                         * right bits of left the neighbour narrow
                         * input.  */
                        if (size < 2 || !u) {
                            uint64_t imm64;
                            if (size == 0) {
                                imm = (0xffu >> (8 - shift));
                                imm |= imm << 16;
                            } else if (size == 1) {
                                imm = 0xffff >> (16 - shift);
                            } else {
                                /* size == 2 */
                                imm = 0xffffffff >> (32 - shift);
                            }
                            if (size < 2) {
                                imm64 = imm | (((uint64_t)imm) << 32);
                            } else {
                                imm64 = imm;
                            }
                            tcg_gen_andi_i64(cpu_V0, cpu_V0, ~imm64);
                        }
                    }
                    neon_store_reg64(cpu_V0, rd + pass);
                }
            } else if (op >= 14) {
                /* VCVT fixed-point.  */
                TCGv_ptr fpst;
                TCGv_i32 shiftv;
                VFPGenFixPointFn *fn;

                if (!(insn & (1 << 21)) || (q && ((rd | rm) & 1))) {
                    return 1;
                }

                if (!(op & 1)) {
                    if (u) {
                        fn = gen_helper_vfp_ultos;
                    } else {
                        fn = gen_helper_vfp_sltos;
                    }
                } else {
                    if (u) {
                        fn = gen_helper_vfp_touls_round_to_zero;
                    } else {
                        fn = gen_helper_vfp_tosls_round_to_zero;
                    }
                }

                /* We have already masked out the must-be-1 top bit of imm6,
                 * hence this 32-shift where the ARM ARM has 64-imm6.
                 */
                shift = 32 - shift;
                fpst = get_fpstatus_ptr(1);
                shiftv = tcg_const_i32(shift);
                for (pass = 0; pass < (q ? 4 : 2); pass++) {
                    TCGv_i32 tmpf = neon_load_reg(rm, pass);
                    fn(tmpf, tmpf, shiftv, fpst);
                    neon_store_reg(rd, pass, tmpf);
                }
                tcg_temp_free_ptr(fpst);
                tcg_temp_free_i32(shiftv);
            } else {
                return 1;
            }
        } else { /* (insn & 0x00380080) == 0 */
            int invert, reg_ofs, vec_size;

            if (q && (rd & 1)) {
                return 1;
            }

            op = (insn >> 8) & 0xf;
            /* One register and immediate.  */
            imm = (u << 7) | ((insn >> 12) & 0x70) | (insn & 0xf);
            invert = (insn & (1 << 5)) != 0;
            /* Note that op = 2,3,4,5,6,7,10,11,12,13 imm=0 is UNPREDICTABLE.
             * We choose to not special-case this and will behave as if a
             * valid constant encoding of 0 had been given.
             */
            switch (op) {
            case 0: case 1:
                /* no-op */
                break;
            case 2: case 3:
                imm <<= 8;
                break;
            case 4: case 5:
                imm <<= 16;
                break;
            case 6: case 7:
                imm <<= 24;
                break;
            case 8: case 9:
                imm |= imm << 16;
                break;
            case 10: case 11:
                imm = (imm << 8) | (imm << 24);
                break;
            case 12:
                imm = (imm << 8) | 0xff;
                break;
            case 13:
                imm = (imm << 16) | 0xffff;
                break;
            case 14:
                imm |= (imm << 8) | (imm << 16) | (imm << 24);
                if (invert) {
                    imm = ~imm;
                }
                break;
            case 15:
                if (invert) {
                    return 1;
                }
                imm = ((imm & 0x80) << 24) | ((imm & 0x3f) << 19)
                      | ((imm & 0x40) ? (0x1f << 25) : (1 << 30));
                break;
            }
            if (invert) {
                imm = ~imm;
            }

            reg_ofs = neon_reg_offset(rd, 0);
            vec_size = q ? 16 : 8;

            if (op & 1 && op < 12) {
                if (invert) {
                    /* The immediate value has already been inverted,
                     * so BIC becomes AND.
                     */
                    tcg_gen_gvec_andi(MO_32, reg_ofs, reg_ofs, imm,
                                      vec_size, vec_size);
                } else {
                    tcg_gen_gvec_ori(MO_32, reg_ofs, reg_ofs, imm,
                                     vec_size, vec_size);
                }
            } else {
                /* VMOV, VMVN.  */
                if (op == 14 && invert) {
                    TCGv_i64 t64 = tcg_temp_new_i64();

                    for (pass = 0; pass <= q; ++pass) {
                        uint64_t val = 0;
                        int n;

                        for (n = 0; n < 8; n++) {
                            if (imm & (1 << (n + pass * 8))) {
                                val |= 0xffull << (n * 8);
                            }
                        }
                        tcg_gen_movi_i64(t64, val);
                        neon_store_reg64(t64, rd + pass);
                    }
                    tcg_temp_free_i64(t64);
                } else {
                    tcg_gen_gvec_dup32i(reg_ofs, vec_size, vec_size, imm);
                }
            }
        }
    } else { /* (insn & 0x00800010 == 0x00800000) */
        if (size != 3) {
            op = (insn >> 8) & 0xf;
            if ((insn & (1 << 6)) == 0) {
                /* Three registers of different lengths.  */
                int src1_wide;
                int src2_wide;
                int prewiden;
                /* undefreq: bit 0 : UNDEF if size == 0
                 *           bit 1 : UNDEF if size == 1
                 *           bit 2 : UNDEF if size == 2
                 *           bit 3 : UNDEF if U == 1
                 * Note that [2:0] set implies 'always UNDEF'
                 */
                int undefreq;
                /* prewiden, src1_wide, src2_wide, undefreq */
                static const int neon_3reg_wide[16][4] = {
                    {1, 0, 0, 0}, /* VADDL */
                    {1, 1, 0, 0}, /* VADDW */
                    {1, 0, 0, 0}, /* VSUBL */
                    {1, 1, 0, 0}, /* VSUBW */
                    {0, 1, 1, 0}, /* VADDHN */
                    {0, 0, 0, 0}, /* VABAL */
                    {0, 1, 1, 0}, /* VSUBHN */
                    {0, 0, 0, 0}, /* VABDL */
                    {0, 0, 0, 0}, /* VMLAL */
                    {0, 0, 0, 9}, /* VQDMLAL */
                    {0, 0, 0, 0}, /* VMLSL */
                    {0, 0, 0, 9}, /* VQDMLSL */
                    {0, 0, 0, 0}, /* Integer VMULL */
                    {0, 0, 0, 1}, /* VQDMULL */
                    {0, 0, 0, 0xa}, /* Polynomial VMULL */
                    {0, 0, 0, 7}, /* Reserved: always UNDEF */
                };

                prewiden = neon_3reg_wide[op][0];
                src1_wide = neon_3reg_wide[op][1];
                src2_wide = neon_3reg_wide[op][2];
                undefreq = neon_3reg_wide[op][3];

                if ((undefreq & (1 << size)) ||
                    ((undefreq & 8) && u)) {
                    return 1;
                }
                if ((src1_wide && (rn & 1)) ||
                    (src2_wide && (rm & 1)) ||
                    (!src2_wide && (rd & 1))) {
                    return 1;
                }

                /* Handle polynomial VMULL in a single pass.  */
                if (op == 14) {
                    if (size == 0) {
                        /* VMULL.P8 */
                        tcg_gen_gvec_3_ool(rd_ofs, rn_ofs, rm_ofs, 16, 16,
                                           0, gen_helper_neon_pmull_h);
                    } else {
                        /* VMULL.P64 */
                        if (!dc_isar_feature(aa32_pmull, s)) {
                            return 1;
                        }
                        tcg_gen_gvec_3_ool(rd_ofs, rn_ofs, rm_ofs, 16, 16,
                                           0, gen_helper_gvec_pmull_q);
                    }
                    return 0;
                }

                /* Avoid overlapping operands.  Wide source operands are
                   always aligned so will never overlap with wide
                   destinations in problematic ways.  */
                if (rd == rm && !src2_wide) {
                    tmp = neon_load_reg(rm, 1);
                    neon_store_scratch(2, tmp);
                } else if (rd == rn && !src1_wide) {
                    tmp = neon_load_reg(rn, 1);
                    neon_store_scratch(2, tmp);
                }
                tmp3 = NULL;
                for (pass = 0; pass < 2; pass++) {
                    if (src1_wide) {
                        neon_load_reg64(cpu_V0, rn + pass);
                        tmp = NULL;
                    } else {
                        if (pass == 1 && rd == rn) {
                            tmp = neon_load_scratch(2);
                        } else {
                            tmp = neon_load_reg(rn, pass);
                        }
                        if (prewiden) {
                            gen_neon_widen(cpu_V0, tmp, size, u);
                        }
                    }
                    if (src2_wide) {
                        neon_load_reg64(cpu_V1, rm + pass);
                        tmp2 = NULL;
                    } else {
                        if (pass == 1 && rd == rm) {
                            tmp2 = neon_load_scratch(2);
                        } else {
                            tmp2 = neon_load_reg(rm, pass);
                        }
                        if (prewiden) {
                            gen_neon_widen(cpu_V1, tmp2, size, u);
                        }
                    }
                    switch (op) {
                    case 0: case 1: case 4: /* VADDL, VADDW, VADDHN, VRADDHN */
                        gen_neon_addl(size);
                        break;
                    case 2: case 3: case 6: /* VSUBL, VSUBW, VSUBHN, VRSUBHN */
                        gen_neon_subl(size);
                        break;
                    case 5: case 7: /* VABAL, VABDL */
                        switch ((size << 1) | u) {
                        case 0:
                            gen_helper_neon_abdl_s16(cpu_V0, tmp, tmp2);
                            break;
                        case 1:
                            gen_helper_neon_abdl_u16(cpu_V0, tmp, tmp2);
                            break;
                        case 2:
                            gen_helper_neon_abdl_s32(cpu_V0, tmp, tmp2);
                            break;
                        case 3:
                            gen_helper_neon_abdl_u32(cpu_V0, tmp, tmp2);
                            break;
                        case 4:
                            gen_helper_neon_abdl_s64(cpu_V0, tmp, tmp2);
                            break;
                        case 5:
                            gen_helper_neon_abdl_u64(cpu_V0, tmp, tmp2);
                            break;
                        default: abort();
                        }
                        tcg_temp_free_i32(tmp2);
                        tcg_temp_free_i32(tmp);
                        break;
                    case 8: case 9: case 10: case 11: case 12: case 13:
                        /* VMLAL, VQDMLAL, VMLSL, VQDMLSL, VMULL, VQDMULL */
                        gen_neon_mull(cpu_V0, tmp, tmp2, size, u);
                        break;
                    default: /* 15 is RESERVED: caught earlier  */
                        abort();
                    }
                    if (op == 13) {
                        /* VQDMULL */
                        gen_neon_addl_saturate(cpu_V0, cpu_V0, size);
                        neon_store_reg64(cpu_V0, rd + pass);
                    } else if (op == 5 || (op >= 8 && op <= 11)) {
                        /* Accumulate.  */
                        neon_load_reg64(cpu_V1, rd + pass);
                        switch (op) {
                        case 10: /* VMLSL */
                            gen_neon_negl(cpu_V0, size);
                            /* Fall through */
                        case 5: case 8: /* VABAL, VMLAL */
                            gen_neon_addl(size);
                            break;
                        case 9: case 11: /* VQDMLAL, VQDMLSL */
                            gen_neon_addl_saturate(cpu_V0, cpu_V0, size);
                            if (op == 11) {
                                gen_neon_negl(cpu_V0, size);
                            }
                            gen_neon_addl_saturate(cpu_V0, cpu_V1, size);
                            break;
                        default:
                            abort();
                        }
                        neon_store_reg64(cpu_V0, rd + pass);
                    } else if (op == 4 || op == 6) {
                        /* Narrowing operation.  */
                        tmp = tcg_temp_new_i32();
                        if (!u) {
                            switch (size) {
                            case 0:
                                gen_helper_neon_narrow_high_u8(tmp, cpu_V0);
                                break;
                            case 1:
                                gen_helper_neon_narrow_high_u16(tmp, cpu_V0);
                                break;
                            case 2:
                                tcg_gen_extrh_i64_i32(tmp, cpu_V0);
                                break;
                            default: abort();
                            }
                        } else {
                            switch (size) {
                            case 0:
                                gen_helper_neon_narrow_round_high_u8(tmp, cpu_V0);
                                break;
                            case 1:
                                gen_helper_neon_narrow_round_high_u16(tmp, cpu_V0);
                                break;
                            case 2:
                                tcg_gen_addi_i64(cpu_V0, cpu_V0, 1u << 31);
                                tcg_gen_extrh_i64_i32(tmp, cpu_V0);
                                break;
                            default: abort();
                            }
                        }
                        if (pass == 0) {
                            tmp3 = tmp;
                        } else {
                            neon_store_reg(rd, 0, tmp3);
                            neon_store_reg(rd, 1, tmp);
                        }
                    } else {
                        /* Write back the result.  */
                        neon_store_reg64(cpu_V0, rd + pass);
                    }
                }
            } else {
                /* Two registers and a scalar. NB that for ops of this form
                 * the ARM ARM labels bit 24 as Q, but it is in our variable
                 * 'u', not 'q'.
                 */
                if (size == 0) {
                    return 1;
                }
                switch (op) {
                case 1: /* Float VMLA scalar */
                case 5: /* Floating point VMLS scalar */
                case 9: /* Floating point VMUL scalar */
                    if (size == 1) {
                        return 1;
                    }
                    /* fall through */
                case 0: /* Integer VMLA scalar */
                case 4: /* Integer VMLS scalar */
                case 8: /* Integer VMUL scalar */
                case 12: /* VQDMULH scalar */
                case 13: /* VQRDMULH scalar */
                    if (u && ((rd | rn) & 1)) {
                        return 1;
                    }
                    tmp = neon_get_scalar(size, rm);
                    neon_store_scratch(0, tmp);
                    for (pass = 0; pass < (u ? 4 : 2); pass++) {
                        tmp = neon_load_scratch(0);
                        tmp2 = neon_load_reg(rn, pass);
                        if (op == 12) {
                            if (size == 1) {
                                gen_helper_neon_qdmulh_s16(tmp, cpu_env, tmp, tmp2);
                            } else {
                                gen_helper_neon_qdmulh_s32(tmp, cpu_env, tmp, tmp2);
                            }
                        } else if (op == 13) {
                            if (size == 1) {
                                gen_helper_neon_qrdmulh_s16(tmp, cpu_env, tmp, tmp2);
                            } else {
                                gen_helper_neon_qrdmulh_s32(tmp, cpu_env, tmp, tmp2);
                            }
                        } else if (op & 1) {
                            TCGv_ptr fpstatus = get_fpstatus_ptr(1);
                            gen_helper_vfp_muls(tmp, tmp, tmp2, fpstatus);
                            tcg_temp_free_ptr(fpstatus);
                        } else {
                            switch (size) {
                            case 0: gen_helper_neon_mul_u8(tmp, tmp, tmp2); break;
                            case 1: gen_helper_neon_mul_u16(tmp, tmp, tmp2); break;
                            case 2: tcg_gen_mul_i32(tmp, tmp, tmp2); break;
                            default: abort();
                            }
                        }
                        tcg_temp_free_i32(tmp2);
                        if (op < 8) {
                            /* Accumulate.  */
                            tmp2 = neon_load_reg(rd, pass);
                            switch (op) {
                            case 0:
                                gen_neon_add(size, tmp, tmp2);
                                break;
                            case 1:
                            {
                                TCGv_ptr fpstatus = get_fpstatus_ptr(1);
                                gen_helper_vfp_adds(tmp, tmp, tmp2, fpstatus);
                                tcg_temp_free_ptr(fpstatus);
                                break;
                            }
                            case 4:
                                gen_neon_rsb(size, tmp, tmp2);
                                break;
                            case 5:
                            {
                                TCGv_ptr fpstatus = get_fpstatus_ptr(1);
                                gen_helper_vfp_subs(tmp, tmp2, tmp, fpstatus);
                                tcg_temp_free_ptr(fpstatus);
                                break;
                            }
                            default:
                                abort();
                            }
                            tcg_temp_free_i32(tmp2);
                        }
                        neon_store_reg(rd, pass, tmp);
                    }
                    break;
                case 3: /* VQDMLAL scalar */
                case 7: /* VQDMLSL scalar */
                case 11: /* VQDMULL scalar */
                    if (u == 1) {
                        return 1;
                    }
                    /* fall through */
                case 2: /* VMLAL sclar */
                case 6: /* VMLSL scalar */
                case 10: /* VMULL scalar */
                    if (rd & 1) {
                        return 1;
                    }
                    tmp2 = neon_get_scalar(size, rm);
                    /* We need a copy of tmp2 because gen_neon_mull
                     * deletes it during pass 0.  */
                    tmp4 = tcg_temp_new_i32();
                    tcg_gen_mov_i32(tmp4, tmp2);
                    tmp3 = neon_load_reg(rn, 1);

                    for (pass = 0; pass < 2; pass++) {
                        if (pass == 0) {
                            tmp = neon_load_reg(rn, 0);
                        } else {
                            tmp = tmp3;
                            tmp2 = tmp4;
                        }
                        gen_neon_mull(cpu_V0, tmp, tmp2, size, u);
                        if (op != 11) {
                            neon_load_reg64(cpu_V1, rd + pass);
                        }
                        switch (op) {
                        case 6:
                            gen_neon_negl(cpu_V0, size);
                            /* Fall through */
                        case 2:
                            gen_neon_addl(size);
                            break;
                        case 3: case 7:
                            gen_neon_addl_saturate(cpu_V0, cpu_V0, size);
                            if (op == 7) {
                                gen_neon_negl(cpu_V0, size);
                            }
                            gen_neon_addl_saturate(cpu_V0, cpu_V1, size);
                            break;
                        case 10:
                            /* no-op */
                            break;
                        case 11:
                            gen_neon_addl_saturate(cpu_V0, cpu_V0, size);
                            break;
                        default:
                            abort();
                        }
                        neon_store_reg64(cpu_V0, rd + pass);
                    }
                    break;
                case 14: /* VQRDMLAH scalar */
                case 15: /* VQRDMLSH scalar */
                    {
                        NeonGenThreeOpEnvFn *fn;

                        if (!dc_isar_feature(aa32_rdm, s)) {
                            return 1;
                        }
                        if (u && ((rd | rn) & 1)) {
                            return 1;
                        }
                        if (op == 14) {
                            if (size == 1) {
                                fn = gen_helper_neon_qrdmlah_s16;
                            } else {
                                fn = gen_helper_neon_qrdmlah_s32;
                            }
                        } else {
                            if (size == 1) {
                                fn = gen_helper_neon_qrdmlsh_s16;
                            } else {
                                fn = gen_helper_neon_qrdmlsh_s32;
                            }
                        }

                        tmp2 = neon_get_scalar(size, rm);
                        for (pass = 0; pass < (u ? 4 : 2); pass++) {
                            tmp = neon_load_reg(rn, pass);
                            tmp3 = neon_load_reg(rd, pass);
                            fn(tmp, cpu_env, tmp, tmp2, tmp3);
                            tcg_temp_free_i32(tmp3);
                            neon_store_reg(rd, pass, tmp);
                        }
                        tcg_temp_free_i32(tmp2);
                    }
                    break;
                default:
                    g_assert_not_reached();
                }
            }
        } else { /* size == 3 */
            if (!u) {
                /* Extract.  */
                imm = (insn >> 8) & 0xf;

                if (imm > 7 && !q)
                    return 1;

                if (q && ((rd | rn | rm) & 1)) {
                    return 1;
                }

                if (imm == 0) {
                    neon_load_reg64(cpu_V0, rn);
                    if (q) {
                        neon_load_reg64(cpu_V1, rn + 1);
                    }
                } else if (imm == 8) {
                    neon_load_reg64(cpu_V0, rn + 1);
                    if (q) {
                        neon_load_reg64(cpu_V1, rm);
                    }
                } else if (q) {
                    tmp64 = tcg_temp_new_i64();
                    if (imm < 8) {
                        neon_load_reg64(cpu_V0, rn);
                        neon_load_reg64(tmp64, rn + 1);
                    } else {
                        neon_load_reg64(cpu_V0, rn + 1);
                        neon_load_reg64(tmp64, rm);
                    }
                    tcg_gen_shri_i64(cpu_V0, cpu_V0, (imm & 7) * 8);
                    tcg_gen_shli_i64(cpu_V1, tmp64, 64 - ((imm & 7) * 8));
                    tcg_gen_or_i64(cpu_V0, cpu_V0, cpu_V1);
                    if (imm < 8) {
                        neon_load_reg64(cpu_V1, rm);
                    } else {
                        neon_load_reg64(cpu_V1, rm + 1);
                        imm -= 8;
                    }
                    tcg_gen_shli_i64(cpu_V1, cpu_V1, 64 - (imm * 8));
                    tcg_gen_shri_i64(tmp64, tmp64, imm * 8);
                    tcg_gen_or_i64(cpu_V1, cpu_V1, tmp64);
                    tcg_temp_free_i64(tmp64);
                } else {
                    /* BUGFIX */
                    neon_load_reg64(cpu_V0, rn);
                    tcg_gen_shri_i64(cpu_V0, cpu_V0, imm * 8);
                    neon_load_reg64(cpu_V1, rm);
                    tcg_gen_shli_i64(cpu_V1, cpu_V1, 64 - (imm * 8));
                    tcg_gen_or_i64(cpu_V0, cpu_V0, cpu_V1);
                }
                neon_store_reg64(cpu_V0, rd);
                if (q) {
                    neon_store_reg64(cpu_V1, rd + 1);
                }
            } else if ((insn & (1 << 11)) == 0) {
                /* Two register misc.  */
                op = ((insn >> 12) & 0x30) | ((insn >> 7) & 0xf);
                size = (insn >> 18) & 3;
                /* UNDEF for unknown op values and bad op-size combinations */
                if ((neon_2rm_sizes[op] & (1 << size)) == 0) {
                    return 1;
                }
                if (neon_2rm_is_v8_op(op) &&
                    !arm_dc_feature(s, ARM_FEATURE_V8)) {
                    return 1;
                }
                if ((op != NEON_2RM_VMOVN && op != NEON_2RM_VQMOVN) &&
                    q && ((rm | rd) & 1)) {
                    return 1;
                }
                switch (op) {
                case NEON_2RM_VREV64:
                    for (pass = 0; pass < (q ? 2 : 1); pass++) {
                        tmp = neon_load_reg(rm, pass * 2);
                        tmp2 = neon_load_reg(rm, pass * 2 + 1);
                        switch (size) {
                        case 0: tcg_gen_bswap32_i32(tmp, tmp); break;
                        case 1: gen_swap_half(tmp); break;
                        case 2: /* no-op */ break;
                        default: abort();
                        }
                        neon_store_reg(rd, pass * 2 + 1, tmp);
                        if (size == 2) {
                            neon_store_reg(rd, pass * 2, tmp2);
                        } else {
                            switch (size) {
                            case 0: tcg_gen_bswap32_i32(tmp2, tmp2); break;
                            case 1: gen_swap_half(tmp2); break;
                            default: abort();
                            }
                            neon_store_reg(rd, pass * 2, tmp2);
                        }
                    }
                    break;
                case NEON_2RM_VPADDL: case NEON_2RM_VPADDL_U:
                case NEON_2RM_VPADAL: case NEON_2RM_VPADAL_U:
                    for (pass = 0; pass < q + 1; pass++) {
                        tmp = neon_load_reg(rm, pass * 2);
                        gen_neon_widen(cpu_V0, tmp, size, op & 1);
                        tmp = neon_load_reg(rm, pass * 2 + 1);
                        gen_neon_widen(cpu_V1, tmp, size, op & 1);
                        switch (size) {
                        case 0: gen_helper_neon_paddl_u16(CPU_V001); break;
                        case 1: gen_helper_neon_paddl_u32(CPU_V001); break;
                        case 2: tcg_gen_add_i64(CPU_V001); break;
                        default: abort();
                        }
                        if (op >= NEON_2RM_VPADAL) {
                            /* Accumulate.  */
                            neon_load_reg64(cpu_V1, rd + pass);
                            gen_neon_addl(size);
                        }
                        neon_store_reg64(cpu_V0, rd + pass);
                    }
                    break;
                case NEON_2RM_VTRN:
                    if (size == 2) {
                        int n;
                        for (n = 0; n < (q ? 4 : 2); n += 2) {
                            tmp = neon_load_reg(rm, n);
                            tmp2 = neon_load_reg(rd, n + 1);
                            neon_store_reg(rm, n, tmp2);
                            neon_store_reg(rd, n + 1, tmp);
                        }
                    } else {
                        goto elementwise;
                    }
                    break;
                case NEON_2RM_VUZP:
                    if (gen_neon_unzip(rd, rm, size, q)) {
                        return 1;
                    }
                    break;
                case NEON_2RM_VZIP:
                    if (gen_neon_zip(rd, rm, size, q)) {
                        return 1;
                    }
                    break;
                case NEON_2RM_VMOVN: case NEON_2RM_VQMOVN:
                    /* also VQMOVUN; op field and mnemonics don't line up */
                    if (rm & 1) {
                        return 1;
                    }
                    tmp2 = NULL;
                    for (pass = 0; pass < 2; pass++) {
                        neon_load_reg64(cpu_V0, rm + pass);
                        tmp = tcg_temp_new_i32();
                        gen_neon_narrow_op(op == NEON_2RM_VMOVN, q, size,
                                           tmp, cpu_V0);
                        if (pass == 0) {
                            tmp2 = tmp;
                        } else {
                            neon_store_reg(rd, 0, tmp2);
                            neon_store_reg(rd, 1, tmp);
                        }
                    }
                    break;
                case NEON_2RM_VSHLL:
                    if (q || (rd & 1)) {
                        return 1;
                    }
                    tmp = neon_load_reg(rm, 0);
                    tmp2 = neon_load_reg(rm, 1);
                    for (pass = 0; pass < 2; pass++) {
                        if (pass == 1)
                            tmp = tmp2;
                        gen_neon_widen(cpu_V0, tmp, size, 1);
                        tcg_gen_shli_i64(cpu_V0, cpu_V0, 8 << size);
                        neon_store_reg64(cpu_V0, rd + pass);
                    }
                    break;
                case NEON_2RM_VCVT_F16_F32:
                {
                    TCGv_ptr fpst;
                    TCGv_i32 ahp;

                    if (!dc_isar_feature(aa32_fp16_spconv, s) ||
                        q || (rm & 1)) {
                        return 1;
                    }
                    fpst = get_fpstatus_ptr(true);
                    ahp = get_ahp_flag();
                    tmp = neon_load_reg(rm, 0);
                    gen_helper_vfp_fcvt_f32_to_f16(tmp, tmp, fpst, ahp);
                    tmp2 = neon_load_reg(rm, 1);
                    gen_helper_vfp_fcvt_f32_to_f16(tmp2, tmp2, fpst, ahp);
                    tcg_gen_shli_i32(tmp2, tmp2, 16);
                    tcg_gen_or_i32(tmp2, tmp2, tmp);
                    tcg_temp_free_i32(tmp);
                    tmp = neon_load_reg(rm, 2);
                    gen_helper_vfp_fcvt_f32_to_f16(tmp, tmp, fpst, ahp);
                    tmp3 = neon_load_reg(rm, 3);
                    neon_store_reg(rd, 0, tmp2);
                    gen_helper_vfp_fcvt_f32_to_f16(tmp3, tmp3, fpst, ahp);
                    tcg_gen_shli_i32(tmp3, tmp3, 16);
                    tcg_gen_or_i32(tmp3, tmp3, tmp);
                    neon_store_reg(rd, 1, tmp3);
                    tcg_temp_free_i32(tmp);
                    tcg_temp_free_i32(ahp);
                    tcg_temp_free_ptr(fpst);
                    break;
                }
                case NEON_2RM_VCVT_F32_F16:
                {
                    TCGv_ptr fpst;
                    TCGv_i32 ahp;
                    if (!dc_isar_feature(aa32_fp16_spconv, s) ||
                        q || (rd & 1)) {
                        return 1;
                    }
                    fpst = get_fpstatus_ptr(true);
                    ahp = get_ahp_flag();
                    tmp3 = tcg_temp_new_i32();
                    tmp = neon_load_reg(rm, 0);
                    tmp2 = neon_load_reg(rm, 1);
                    tcg_gen_ext16u_i32(tmp3, tmp);
                    gen_helper_vfp_fcvt_f16_to_f32(tmp3, tmp3, fpst, ahp);
                    neon_store_reg(rd, 0, tmp3);
                    tcg_gen_shri_i32(tmp, tmp, 16);
                    gen_helper_vfp_fcvt_f16_to_f32(tmp, tmp, fpst, ahp);
                    neon_store_reg(rd, 1, tmp);
                    tmp3 = tcg_temp_new_i32();
                    tcg_gen_ext16u_i32(tmp3, tmp2);
                    gen_helper_vfp_fcvt_f16_to_f32(tmp3, tmp3, fpst, ahp);
                    neon_store_reg(rd, 2, tmp3);
                    tcg_gen_shri_i32(tmp2, tmp2, 16);
                    gen_helper_vfp_fcvt_f16_to_f32(tmp2, tmp2, fpst, ahp);
                    neon_store_reg(rd, 3, tmp2);
                    tcg_temp_free_i32(ahp);
                    tcg_temp_free_ptr(fpst);
                    break;
                }
                case NEON_2RM_AESE: case NEON_2RM_AESMC:
                    if (!dc_isar_feature(aa32_aes, s) || ((rm | rd) & 1)) {
                        return 1;
                    }
                    ptr1 = vfp_reg_ptr(true, rd);
                    ptr2 = vfp_reg_ptr(true, rm);

                     /* Bit 6 is the lowest opcode bit; it distinguishes between
                      * encryption (AESE/AESMC) and decryption (AESD/AESIMC)
                      */
                    tmp3 = tcg_const_i32(extract32(insn, 6, 1));

                    if (op == NEON_2RM_AESE) {
                        gen_helper_crypto_aese(ptr1, ptr2, tmp3);
                    } else {
                        gen_helper_crypto_aesmc(ptr1, ptr2, tmp3);
                    }
                    tcg_temp_free_ptr(ptr1);
                    tcg_temp_free_ptr(ptr2);
                    tcg_temp_free_i32(tmp3);
                    break;
                case NEON_2RM_SHA1H:
                    if (!dc_isar_feature(aa32_sha1, s) || ((rm | rd) & 1)) {
                        return 1;
                    }
                    ptr1 = vfp_reg_ptr(true, rd);
                    ptr2 = vfp_reg_ptr(true, rm);

                    gen_helper_crypto_sha1h(ptr1, ptr2);

                    tcg_temp_free_ptr(ptr1);
                    tcg_temp_free_ptr(ptr2);
                    break;
                case NEON_2RM_SHA1SU1:
                    if ((rm | rd) & 1) {
                            return 1;
                    }
                    /* bit 6 (q): set -> SHA256SU0, cleared -> SHA1SU1 */
                    if (q) {
                        if (!dc_isar_feature(aa32_sha2, s)) {
                            return 1;
                        }
                    } else if (!dc_isar_feature(aa32_sha1, s)) {
                        return 1;
                    }
                    ptr1 = vfp_reg_ptr(true, rd);
                    ptr2 = vfp_reg_ptr(true, rm);
                    if (q) {
                        gen_helper_crypto_sha256su0(ptr1, ptr2);
                    } else {
                        gen_helper_crypto_sha1su1(ptr1, ptr2);
                    }
                    tcg_temp_free_ptr(ptr1);
                    tcg_temp_free_ptr(ptr2);
                    break;

                case NEON_2RM_VMVN:
                    tcg_gen_gvec_not(0, rd_ofs, rm_ofs, vec_size, vec_size);
                    break;
                case NEON_2RM_VNEG:
                    tcg_gen_gvec_neg(size, rd_ofs, rm_ofs, vec_size, vec_size);
                    break;
                case NEON_2RM_VABS:
                    tcg_gen_gvec_abs(size, rd_ofs, rm_ofs, vec_size, vec_size);
                    break;

                default:
                elementwise:
                    for (pass = 0; pass < (q ? 4 : 2); pass++) {
                        tmp = neon_load_reg(rm, pass);
                        switch (op) {
                        case NEON_2RM_VREV32:
                            switch (size) {
                            case 0: tcg_gen_bswap32_i32(tmp, tmp); break;
                            case 1: gen_swap_half(tmp); break;
                            default: abort();
                            }
                            break;
                        case NEON_2RM_VREV16:
                            gen_rev16(tmp, tmp);
                            break;
                        case NEON_2RM_VCLS:
                            switch (size) {
                            case 0: gen_helper_neon_cls_s8(tmp, tmp); break;
                            case 1: gen_helper_neon_cls_s16(tmp, tmp); break;
                            case 2: gen_helper_neon_cls_s32(tmp, tmp); break;
                            default: abort();
                            }
                            break;
                        case NEON_2RM_VCLZ:
                            switch (size) {
                            case 0: gen_helper_neon_clz_u8(tmp, tmp); break;
                            case 1: gen_helper_neon_clz_u16(tmp, tmp); break;
                            case 2: tcg_gen_clzi_i32(tmp, tmp, 32); break;
                            default: abort();
                            }
                            break;
                        case NEON_2RM_VCNT:
                            gen_helper_neon_cnt_u8(tmp, tmp);
                            break;
                        case NEON_2RM_VQABS:
                            switch (size) {
                            case 0:
                                gen_helper_neon_qabs_s8(tmp, cpu_env, tmp);
                                break;
                            case 1:
                                gen_helper_neon_qabs_s16(tmp, cpu_env, tmp);
                                break;
                            case 2:
                                gen_helper_neon_qabs_s32(tmp, cpu_env, tmp);
                                break;
                            default: abort();
                            }
                            break;
                        case NEON_2RM_VQNEG:
                            switch (size) {
                            case 0:
                                gen_helper_neon_qneg_s8(tmp, cpu_env, tmp);
                                break;
                            case 1:
                                gen_helper_neon_qneg_s16(tmp, cpu_env, tmp);
                                break;
                            case 2:
                                gen_helper_neon_qneg_s32(tmp, cpu_env, tmp);
                                break;
                            default: abort();
                            }
                            break;
                        case NEON_2RM_VCGT0: case NEON_2RM_VCLE0:
                            tmp2 = tcg_const_i32(0);
                            switch(size) {
                            case 0: gen_helper_neon_cgt_s8(tmp, tmp, tmp2); break;
                            case 1: gen_helper_neon_cgt_s16(tmp, tmp, tmp2); break;
                            case 2: gen_helper_neon_cgt_s32(tmp, tmp, tmp2); break;
                            default: abort();
                            }
                            tcg_temp_free_i32(tmp2);
                            if (op == NEON_2RM_VCLE0) {
                                tcg_gen_not_i32(tmp, tmp);
                            }
                            break;
                        case NEON_2RM_VCGE0: case NEON_2RM_VCLT0:
                            tmp2 = tcg_const_i32(0);
                            switch(size) {
                            case 0: gen_helper_neon_cge_s8(tmp, tmp, tmp2); break;
                            case 1: gen_helper_neon_cge_s16(tmp, tmp, tmp2); break;
                            case 2: gen_helper_neon_cge_s32(tmp, tmp, tmp2); break;
                            default: abort();
                            }
                            tcg_temp_free_i32(tmp2);
                            if (op == NEON_2RM_VCLT0) {
                                tcg_gen_not_i32(tmp, tmp);
                            }
                            break;
                        case NEON_2RM_VCEQ0:
                            tmp2 = tcg_const_i32(0);
                            switch(size) {
                            case 0: gen_helper_neon_ceq_u8(tmp, tmp, tmp2); break;
                            case 1: gen_helper_neon_ceq_u16(tmp, tmp, tmp2); break;
                            case 2: gen_helper_neon_ceq_u32(tmp, tmp, tmp2); break;
                            default: abort();
                            }
                            tcg_temp_free_i32(tmp2);
                            break;
                        case NEON_2RM_VCGT0_F:
                        {
                            TCGv_ptr fpstatus = get_fpstatus_ptr(1);
                            tmp2 = tcg_const_i32(0);
                            gen_helper_neon_cgt_f32(tmp, tmp, tmp2, fpstatus);
                            tcg_temp_free_i32(tmp2);
                            tcg_temp_free_ptr(fpstatus);
                            break;
                        }
                        case NEON_2RM_VCGE0_F:
                        {
                            TCGv_ptr fpstatus = get_fpstatus_ptr(1);
                            tmp2 = tcg_const_i32(0);
                            gen_helper_neon_cge_f32(tmp, tmp, tmp2, fpstatus);
                            tcg_temp_free_i32(tmp2);
                            tcg_temp_free_ptr(fpstatus);
                            break;
                        }
                        case NEON_2RM_VCEQ0_F:
                        {
                            TCGv_ptr fpstatus = get_fpstatus_ptr(1);
                            tmp2 = tcg_const_i32(0);
                            gen_helper_neon_ceq_f32(tmp, tmp, tmp2, fpstatus);
                            tcg_temp_free_i32(tmp2);
                            tcg_temp_free_ptr(fpstatus);
                            break;
                        }
                        case NEON_2RM_VCLE0_F:
                        {
                            TCGv_ptr fpstatus = get_fpstatus_ptr(1);
                            tmp2 = tcg_const_i32(0);
                            gen_helper_neon_cge_f32(tmp, tmp2, tmp, fpstatus);
                            tcg_temp_free_i32(tmp2);
                            tcg_temp_free_ptr(fpstatus);
                            break;
                        }
                        case NEON_2RM_VCLT0_F:
                        {
                            TCGv_ptr fpstatus = get_fpstatus_ptr(1);
                            tmp2 = tcg_const_i32(0);
                            gen_helper_neon_cgt_f32(tmp, tmp2, tmp, fpstatus);
                            tcg_temp_free_i32(tmp2);
                            tcg_temp_free_ptr(fpstatus);
                            break;
                        }
                        case NEON_2RM_VABS_F:
                            gen_helper_vfp_abss(tmp, tmp);
                            break;
                        case NEON_2RM_VNEG_F:
                            gen_helper_vfp_negs(tmp, tmp);
                            break;
                        case NEON_2RM_VSWP:
                            tmp2 = neon_load_reg(rd, pass);
                            neon_store_reg(rm, pass, tmp2);
                            break;
                        case NEON_2RM_VTRN:
                            tmp2 = neon_load_reg(rd, pass);
                            switch (size) {
                            case 0: gen_neon_trn_u8(tmp, tmp2); break;
                            case 1: gen_neon_trn_u16(tmp, tmp2); break;
                            default: abort();
                            }
                            neon_store_reg(rm, pass, tmp2);
                            break;
                        case NEON_2RM_VRINTN:
                        case NEON_2RM_VRINTA:
                        case NEON_2RM_VRINTM:
                        case NEON_2RM_VRINTP:
                        case NEON_2RM_VRINTZ:
                        {
                            TCGv_i32 tcg_rmode;
                            TCGv_ptr fpstatus = get_fpstatus_ptr(1);
                            int rmode;

                            if (op == NEON_2RM_VRINTZ) {
                                rmode = FPROUNDING_ZERO;
                            } else {
                                rmode = fp_decode_rm[((op & 0x6) >> 1) ^ 1];
                            }

                            tcg_rmode = tcg_const_i32(arm_rmode_to_sf(rmode));
                            gen_helper_set_neon_rmode(tcg_rmode, tcg_rmode,
                                                      cpu_env);
                            gen_helper_rints(tmp, tmp, fpstatus);
                            gen_helper_set_neon_rmode(tcg_rmode, tcg_rmode,
                                                      cpu_env);
                            tcg_temp_free_ptr(fpstatus);
                            tcg_temp_free_i32(tcg_rmode);
                            break;
                        }
                        case NEON_2RM_VRINTX:
                        {
                            TCGv_ptr fpstatus = get_fpstatus_ptr(1);
                            gen_helper_rints_exact(tmp, tmp, fpstatus);
                            tcg_temp_free_ptr(fpstatus);
                            break;
                        }
                        case NEON_2RM_VCVTAU:
                        case NEON_2RM_VCVTAS:
                        case NEON_2RM_VCVTNU:
                        case NEON_2RM_VCVTNS:
                        case NEON_2RM_VCVTPU:
                        case NEON_2RM_VCVTPS:
                        case NEON_2RM_VCVTMU:
                        case NEON_2RM_VCVTMS:
                        {
                            bool is_signed = !extract32(insn, 7, 1);
                            TCGv_ptr fpst = get_fpstatus_ptr(1);
                            TCGv_i32 tcg_rmode, tcg_shift;
                            int rmode = fp_decode_rm[extract32(insn, 8, 2)];

                            tcg_shift = tcg_const_i32(0);
                            tcg_rmode = tcg_const_i32(arm_rmode_to_sf(rmode));
                            gen_helper_set_neon_rmode(tcg_rmode, tcg_rmode,
                                                      cpu_env);

                            if (is_signed) {
                                gen_helper_vfp_tosls(tmp, tmp,
                                                     tcg_shift, fpst);
                            } else {
                                gen_helper_vfp_touls(tmp, tmp,
                                                     tcg_shift, fpst);
                            }

                            gen_helper_set_neon_rmode(tcg_rmode, tcg_rmode,
                                                      cpu_env);
                            tcg_temp_free_i32(tcg_rmode);
                            tcg_temp_free_i32(tcg_shift);
                            tcg_temp_free_ptr(fpst);
                            break;
                        }
                        case NEON_2RM_VRECPE:
                        {
                            TCGv_ptr fpstatus = get_fpstatus_ptr(1);
                            gen_helper_recpe_u32(tmp, tmp, fpstatus);
                            tcg_temp_free_ptr(fpstatus);
                            break;
                        }
                        case NEON_2RM_VRSQRTE:
                        {
                            TCGv_ptr fpstatus = get_fpstatus_ptr(1);
                            gen_helper_rsqrte_u32(tmp, tmp, fpstatus);
                            tcg_temp_free_ptr(fpstatus);
                            break;
                        }
                        case NEON_2RM_VRECPE_F:
                        {
                            TCGv_ptr fpstatus = get_fpstatus_ptr(1);
                            gen_helper_recpe_f32(tmp, tmp, fpstatus);
                            tcg_temp_free_ptr(fpstatus);
                            break;
                        }
                        case NEON_2RM_VRSQRTE_F:
                        {
                            TCGv_ptr fpstatus = get_fpstatus_ptr(1);
                            gen_helper_rsqrte_f32(tmp, tmp, fpstatus);
                            tcg_temp_free_ptr(fpstatus);
                            break;
                        }
                        case NEON_2RM_VCVT_FS: /* VCVT.F32.S32 */
                        {
                            TCGv_ptr fpstatus = get_fpstatus_ptr(1);
                            gen_helper_vfp_sitos(tmp, tmp, fpstatus);
                            tcg_temp_free_ptr(fpstatus);
                            break;
                        }
                        case NEON_2RM_VCVT_FU: /* VCVT.F32.U32 */
                        {
                            TCGv_ptr fpstatus = get_fpstatus_ptr(1);
                            gen_helper_vfp_uitos(tmp, tmp, fpstatus);
                            tcg_temp_free_ptr(fpstatus);
                            break;
                        }
                        case NEON_2RM_VCVT_SF: /* VCVT.S32.F32 */
                        {
                            TCGv_ptr fpstatus = get_fpstatus_ptr(1);
                            gen_helper_vfp_tosizs(tmp, tmp, fpstatus);
                            tcg_temp_free_ptr(fpstatus);
                            break;
                        }
                        case NEON_2RM_VCVT_UF: /* VCVT.U32.F32 */
                        {
                            TCGv_ptr fpstatus = get_fpstatus_ptr(1);
                            gen_helper_vfp_touizs(tmp, tmp, fpstatus);
                            tcg_temp_free_ptr(fpstatus);
                            break;
                        }
                        default:
                            /* Reserved op values were caught by the
                             * neon_2rm_sizes[] check earlier.
                             */
                            abort();
                        }
                        neon_store_reg(rd, pass, tmp);
                    }
                    break;
                }
            } else if ((insn & (1 << 10)) == 0) {
                /* VTBL, VTBX.  */
                int n = ((insn >> 8) & 3) + 1;
                if ((rn + n) > 32) {
                    /* This is UNPREDICTABLE; we choose to UNDEF to avoid the
                     * helper function running off the end of the register file.
                     */
                    return 1;
                }
                n <<= 3;
                if (insn & (1 << 6)) {
                    tmp = neon_load_reg(rd, 0);
                } else {
                    tmp = tcg_temp_new_i32();
                    tcg_gen_movi_i32(tmp, 0);
                }
                tmp2 = neon_load_reg(rm, 0);
                ptr1 = vfp_reg_ptr(true, rn);
                tmp5 = tcg_const_i32(n);
                gen_helper_neon_tbl(tmp2, tmp2, tmp, ptr1, tmp5);
                tcg_temp_free_i32(tmp);
                if (insn & (1 << 6)) {
                    tmp = neon_load_reg(rd, 1);
                } else {
                    tmp = tcg_temp_new_i32();
                    tcg_gen_movi_i32(tmp, 0);
                }
                tmp3 = neon_load_reg(rm, 1);
                gen_helper_neon_tbl(tmp3, tmp3, tmp, ptr1, tmp5);
                tcg_temp_free_i32(tmp5);
                tcg_temp_free_ptr(ptr1);
                neon_store_reg(rd, 0, tmp2);
                neon_store_reg(rd, 1, tmp3);
                tcg_temp_free_i32(tmp);
            } else if ((insn & 0x380) == 0) {
                /* VDUP */
                int element;
                MemOp size;

                if ((insn & (7 << 16)) == 0 || (q && (rd & 1))) {
                    return 1;
                }
                if (insn & (1 << 16)) {
                    size = MO_8;
                    element = (insn >> 17) & 7;
                } else if (insn & (1 << 17)) {
                    size = MO_16;
                    element = (insn >> 18) & 3;
                } else {
                    size = MO_32;
                    element = (insn >> 19) & 1;
                }
                tcg_gen_gvec_dup_mem(size, neon_reg_offset(rd, 0),
                                     neon_element_offset(rm, element, size),
                                     q ? 16 : 8, q ? 16 : 8);
            } else {
                return 1;
            }
        }
    }
    return 0;
}

/* Advanced SIMD three registers of the same length extension.
 *  31           25    23  22    20   16   12  11   10   9    8        3     0
 * +---------------+-----+---+-----+----+----+---+----+---+----+---------+----+
 * | 1 1 1 1 1 1 0 | op1 | D | op2 | Vn | Vd | 1 | o3 | 0 | o4 | N Q M U | Vm |
 * +---------------+-----+---+-----+----+----+---+----+---+----+---------+----+
 */
static int disas_neon_insn_3same_ext(DisasContext *s, uint32_t insn)
{
    gen_helper_gvec_3 *fn_gvec = NULL;
    gen_helper_gvec_3_ptr *fn_gvec_ptr = NULL;
    int rd, rn, rm, opr_sz;
    int data = 0;
    int off_rn, off_rm;
    bool is_long = false, q = extract32(insn, 6, 1);
    bool ptr_is_env = false;

    if ((insn & 0xfe200f10) == 0xfc200800) {
        /* VCMLA -- 1111 110R R.1S .... .... 1000 ...0 .... */
        int size = extract32(insn, 20, 1);
        data = extract32(insn, 23, 2); /* rot */
        if (!dc_isar_feature(aa32_vcma, s)
            || (!size && !dc_isar_feature(aa32_fp16_arith, s))) {
            return 1;
        }
        fn_gvec_ptr = size ? gen_helper_gvec_fcmlas : gen_helper_gvec_fcmlah;
    } else if ((insn & 0xfea00f10) == 0xfc800800) {
        /* VCADD -- 1111 110R 1.0S .... .... 1000 ...0 .... */
        int size = extract32(insn, 20, 1);
        data = extract32(insn, 24, 1); /* rot */
        if (!dc_isar_feature(aa32_vcma, s)
            || (!size && !dc_isar_feature(aa32_fp16_arith, s))) {
            return 1;
        }
        fn_gvec_ptr = size ? gen_helper_gvec_fcadds : gen_helper_gvec_fcaddh;
    } else if ((insn & 0xfeb00f00) == 0xfc200d00) {
        /* V[US]DOT -- 1111 1100 0.10 .... .... 1101 .Q.U .... */
        bool u = extract32(insn, 4, 1);
        if (!dc_isar_feature(aa32_dp, s)) {
            return 1;
        }
        fn_gvec = u ? gen_helper_gvec_udot_b : gen_helper_gvec_sdot_b;
    } else if ((insn & 0xff300f10) == 0xfc200810) {
        /* VFM[AS]L -- 1111 1100 S.10 .... .... 1000 .Q.1 .... */
        int is_s = extract32(insn, 23, 1);
        if (!dc_isar_feature(aa32_fhm, s)) {
            return 1;
        }
        is_long = true;
        data = is_s; /* is_2 == 0 */
        fn_gvec_ptr = gen_helper_gvec_fmlal_a32;
        ptr_is_env = true;
    } else {
        return 1;
    }

    VFP_DREG_D(rd, insn);
    if (rd & q) {
        return 1;
    }
    if (q || !is_long) {
        VFP_DREG_N(rn, insn);
        VFP_DREG_M(rm, insn);
        if ((rn | rm) & q & !is_long) {
            return 1;
        }
        off_rn = vfp_reg_offset(1, rn);
        off_rm = vfp_reg_offset(1, rm);
    } else {
        rn = VFP_SREG_N(insn);
        rm = VFP_SREG_M(insn);
        off_rn = vfp_reg_offset(0, rn);
        off_rm = vfp_reg_offset(0, rm);
    }

    if (s->fp_excp_el) {
        gen_exception_insn(s, s->pc_curr, EXCP_UDEF,
                           syn_simd_access_trap(1, 0xe, false), s->fp_excp_el);
        return 0;
    }
    if (!s->vfp_enabled) {
        return 1;
    }

    opr_sz = (1 + q) * 8;
    if (fn_gvec_ptr) {
        TCGv_ptr ptr;
        if (ptr_is_env) {
            ptr = cpu_env;
        } else {
            ptr = get_fpstatus_ptr(1);
        }
        tcg_gen_gvec_3_ptr(vfp_reg_offset(1, rd), off_rn, off_rm, ptr,
                           opr_sz, opr_sz, data, fn_gvec_ptr);
        if (!ptr_is_env) {
            tcg_temp_free_ptr(ptr);
        }
    } else {
        tcg_gen_gvec_3_ool(vfp_reg_offset(1, rd), off_rn, off_rm,
                           opr_sz, opr_sz, data, fn_gvec);
    }
    return 0;
}

/* Advanced SIMD two registers and a scalar extension.
 *  31             24   23  22   20   16   12  11   10   9    8        3     0
 * +-----------------+----+---+----+----+----+---+----+---+----+---------+----+
 * | 1 1 1 1 1 1 1 0 | o1 | D | o2 | Vn | Vd | 1 | o3 | 0 | o4 | N Q M U | Vm |
 * +-----------------+----+---+----+----+----+---+----+---+----+---------+----+
 *
 */

static int disas_neon_insn_2reg_scalar_ext(DisasContext *s, uint32_t insn)
{
    gen_helper_gvec_3 *fn_gvec = NULL;
    gen_helper_gvec_3_ptr *fn_gvec_ptr = NULL;
    int rd, rn, rm, opr_sz, data;
    int off_rn, off_rm;
    bool is_long = false, q = extract32(insn, 6, 1);
    bool ptr_is_env = false;

    if ((insn & 0xff000f10) == 0xfe000800) {
        /* VCMLA (indexed) -- 1111 1110 S.RR .... .... 1000 ...0 .... */
        int rot = extract32(insn, 20, 2);
        int size = extract32(insn, 23, 1);
        int index;

        if (!dc_isar_feature(aa32_vcma, s)) {
            return 1;
        }
        if (size == 0) {
            if (!dc_isar_feature(aa32_fp16_arith, s)) {
                return 1;
            }
            /* For fp16, rm is just Vm, and index is M.  */
            rm = extract32(insn, 0, 4);
            index = extract32(insn, 5, 1);
        } else {
            /* For fp32, rm is the usual M:Vm, and index is 0.  */
            VFP_DREG_M(rm, insn);
            index = 0;
        }
        data = (index << 2) | rot;
        fn_gvec_ptr = (size ? gen_helper_gvec_fcmlas_idx
                       : gen_helper_gvec_fcmlah_idx);
    } else if ((insn & 0xffb00f00) == 0xfe200d00) {
        /* V[US]DOT -- 1111 1110 0.10 .... .... 1101 .Q.U .... */
        int u = extract32(insn, 4, 1);

        if (!dc_isar_feature(aa32_dp, s)) {
            return 1;
        }
        fn_gvec = u ? gen_helper_gvec_udot_idx_b : gen_helper_gvec_sdot_idx_b;
        /* rm is just Vm, and index is M.  */
        data = extract32(insn, 5, 1); /* index */
        rm = extract32(insn, 0, 4);
    } else if ((insn & 0xffa00f10) == 0xfe000810) {
        /* VFM[AS]L -- 1111 1110 0.0S .... .... 1000 .Q.1 .... */
        int is_s = extract32(insn, 20, 1);
        int vm20 = extract32(insn, 0, 3);
        int vm3 = extract32(insn, 3, 1);
        int m = extract32(insn, 5, 1);
        int index;

        if (!dc_isar_feature(aa32_fhm, s)) {
            return 1;
        }
        if (q) {
            rm = vm20;
            index = m * 2 + vm3;
        } else {
            rm = vm20 * 2 + m;
            index = vm3;
        }
        is_long = true;
        data = (index << 2) | is_s; /* is_2 == 0 */
        fn_gvec_ptr = gen_helper_gvec_fmlal_idx_a32;
        ptr_is_env = true;
    } else {
        return 1;
    }

    VFP_DREG_D(rd, insn);
    if (rd & q) {
        return 1;
    }
    if (q || !is_long) {
        VFP_DREG_N(rn, insn);
        if (rn & q & !is_long) {
            return 1;
        }
        off_rn = vfp_reg_offset(1, rn);
        off_rm = vfp_reg_offset(1, rm);
    } else {
        rn = VFP_SREG_N(insn);
        off_rn = vfp_reg_offset(0, rn);
        off_rm = vfp_reg_offset(0, rm);
    }
    if (s->fp_excp_el) {
        gen_exception_insn(s, s->pc_curr, EXCP_UDEF,
                           syn_simd_access_trap(1, 0xe, false), s->fp_excp_el);
        return 0;
    }
    if (!s->vfp_enabled) {
        return 1;
    }

    opr_sz = (1 + q) * 8;
    if (fn_gvec_ptr) {
        TCGv_ptr ptr;
        if (ptr_is_env) {
            ptr = cpu_env;
        } else {
            ptr = get_fpstatus_ptr(1);
        }
        tcg_gen_gvec_3_ptr(vfp_reg_offset(1, rd), off_rn, off_rm, ptr,
                           opr_sz, opr_sz, data, fn_gvec_ptr);
        if (!ptr_is_env) {
            tcg_temp_free_ptr(ptr);
        }
    } else {
        tcg_gen_gvec_3_ool(vfp_reg_offset(1, rd), off_rn, off_rm,
                           opr_sz, opr_sz, data, fn_gvec);
    }
    return 0;
}

static int disas_coproc_insn(DisasContext *s, uint32_t insn)
{
    int cpnum, is64, crn, crm, opc1, opc2, isread, rt, rt2;
    const ARMCPRegInfo *ri;

    cpnum = (insn >> 8) & 0xf;

    /* First check for coprocessor space used for XScale/iwMMXt insns */
    if (arm_dc_feature(s, ARM_FEATURE_XSCALE) && (cpnum < 2)) {
        if (extract32(s->c15_cpar, cpnum, 1) == 0) {
            return 1;
        }
        if (arm_dc_feature(s, ARM_FEATURE_IWMMXT)) {
            return disas_iwmmxt_insn(s, insn);
        } else if (arm_dc_feature(s, ARM_FEATURE_XSCALE)) {
            return disas_dsp_insn(s, insn);
        }
        return 1;
    }

    /* Otherwise treat as a generic register access */
    is64 = (insn & (1 << 25)) == 0;
    if (!is64 && ((insn & (1 << 4)) == 0)) {
        /* cdp */
        return 1;
    }

    crm = insn & 0xf;
    if (is64) {
        crn = 0;
        opc1 = (insn >> 4) & 0xf;
        opc2 = 0;
        rt2 = (insn >> 16) & 0xf;
    } else {
        crn = (insn >> 16) & 0xf;
        opc1 = (insn >> 21) & 7;
        opc2 = (insn >> 5) & 7;
        rt2 = 0;
    }
    isread = (insn >> 20) & 1;
    rt = (insn >> 12) & 0xf;

    ri = get_arm_cp_reginfo(s->cp_regs,
            ENCODE_CP_REG(cpnum, is64, s->ns, crn, crm, opc1, opc2));
    if (ri) {
        bool need_exit_tb;

        /* Check access permissions */
        if (!cp_access_ok(s->current_el, ri, isread)) {
            return 1;
        }

        if (s->hstr_active || ri->accessfn ||
            (arm_dc_feature(s, ARM_FEATURE_XSCALE) && cpnum < 14)) {
            /* Emit code to perform further access permissions checks at
             * runtime; this may result in an exception.
             * Note that on XScale all cp0..c13 registers do an access check
             * call in order to handle c15_cpar.
             */
            TCGv_ptr tmpptr;
            TCGv_i32 tcg_syn, tcg_isread;
            uint32_t syndrome;

            /* Note that since we are an implementation which takes an
             * exception on a trapped conditional instruction only if the
             * instruction passes its condition code check, we can take
             * advantage of the clause in the ARM ARM that allows us to set
             * the COND field in the instruction to 0xE in all cases.
             * We could fish the actual condition out of the insn (ARM)
             * or the condexec bits (Thumb) but it isn't necessary.
             */
            switch (cpnum) {
            case 14:
                if (is64) {
                    syndrome = syn_cp14_rrt_trap(1, 0xe, opc1, crm, rt, rt2,
                                                 isread, false);
                } else {
                    syndrome = syn_cp14_rt_trap(1, 0xe, opc1, opc2, crn, crm,
                                                rt, isread, false);
                }
                break;
            case 15:
                if (is64) {
                    syndrome = syn_cp15_rrt_trap(1, 0xe, opc1, crm, rt, rt2,
                                                 isread, false);
                } else {
                    syndrome = syn_cp15_rt_trap(1, 0xe, opc1, opc2, crn, crm,
                                                rt, isread, false);
                }
                break;
            default:
                /* ARMv8 defines that only coprocessors 14 and 15 exist,
                 * so this can only happen if this is an ARMv7 or earlier CPU,
                 * in which case the syndrome information won't actually be
                 * guest visible.
                 */
                assert(!arm_dc_feature(s, ARM_FEATURE_V8));
                syndrome = syn_uncategorized();
                break;
            }

            gen_set_condexec(s);
            gen_set_pc_im(s, s->pc_curr);
            tmpptr = tcg_const_ptr(ri);
            tcg_syn = tcg_const_i32(syndrome);
            tcg_isread = tcg_const_i32(isread);
            gen_helper_access_check_cp_reg(cpu_env, tmpptr, tcg_syn,
                                           tcg_isread);
            tcg_temp_free_ptr(tmpptr);
            tcg_temp_free_i32(tcg_syn);
            tcg_temp_free_i32(tcg_isread);
        } else if (ri->type & ARM_CP_RAISES_EXC) {
            /*
             * The readfn or writefn might raise an exception;
             * synchronize the CPU state in case it does.
             */
            gen_set_condexec(s);
            gen_set_pc_im(s, s->pc_curr);
        }

        /* Handle special cases first */
        switch (ri->type & ~(ARM_CP_FLAG_MASK & ~ARM_CP_SPECIAL)) {
        case ARM_CP_NOP:
            return 0;
        case ARM_CP_WFI:
            if (isread) {
                return 1;
            }
            gen_set_pc_im(s, s->base.pc_next);
            s->base.is_jmp = DISAS_WFI;
            return 0;
        default:
            break;
        }

        if ((tb_cflags(s->base.tb) & CF_USE_ICOUNT) && (ri->type & ARM_CP_IO)) {
            gen_io_start();
        }

        if (isread) {
            /* Read */
            if (is64) {
                TCGv_i64 tmp64;
                TCGv_i32 tmp;
                if (ri->type & ARM_CP_CONST) {
                    tmp64 = tcg_const_i64(ri->resetvalue);
                } else if (ri->readfn) {
                    TCGv_ptr tmpptr;
                    tmp64 = tcg_temp_new_i64();
                    tmpptr = tcg_const_ptr(ri);
                    gen_helper_get_cp_reg64(tmp64, cpu_env, tmpptr);
                    tcg_temp_free_ptr(tmpptr);
                } else {
                    tmp64 = tcg_temp_new_i64();
                    tcg_gen_ld_i64(tmp64, cpu_env, ri->fieldoffset);
                }
                tmp = tcg_temp_new_i32();
                tcg_gen_extrl_i64_i32(tmp, tmp64);
                store_reg(s, rt, tmp);
                tmp = tcg_temp_new_i32();
                tcg_gen_extrh_i64_i32(tmp, tmp64);
                tcg_temp_free_i64(tmp64);
                store_reg(s, rt2, tmp);
            } else {
                TCGv_i32 tmp;
                if (ri->type & ARM_CP_CONST) {
                    tmp = tcg_const_i32(ri->resetvalue);
                } else if (ri->readfn) {
                    TCGv_ptr tmpptr;
                    tmp = tcg_temp_new_i32();
                    tmpptr = tcg_const_ptr(ri);
                    gen_helper_get_cp_reg(tmp, cpu_env, tmpptr);
                    tcg_temp_free_ptr(tmpptr);
                } else {
                    tmp = load_cpu_offset(ri->fieldoffset);
                }
                if (rt == 15) {
                    /* Destination register of r15 for 32 bit loads sets
                     * the condition codes from the high 4 bits of the value
                     */
                    gen_set_nzcv(tmp);
                    tcg_temp_free_i32(tmp);
                } else {
                    store_reg(s, rt, tmp);
                }
            }
        } else {
            /* Write */
            if (ri->type & ARM_CP_CONST) {
                /* If not forbidden by access permissions, treat as WI */
                return 0;
            }

            if (is64) {
                TCGv_i32 tmplo, tmphi;
                TCGv_i64 tmp64 = tcg_temp_new_i64();
                tmplo = load_reg(s, rt);
                tmphi = load_reg(s, rt2);
                tcg_gen_concat_i32_i64(tmp64, tmplo, tmphi);
                tcg_temp_free_i32(tmplo);
                tcg_temp_free_i32(tmphi);
                if (ri->writefn) {
                    TCGv_ptr tmpptr = tcg_const_ptr(ri);
                    gen_helper_set_cp_reg64(cpu_env, tmpptr, tmp64);
                    tcg_temp_free_ptr(tmpptr);
                } else {
                    tcg_gen_st_i64(tmp64, cpu_env, ri->fieldoffset);
                }
                tcg_temp_free_i64(tmp64);
            } else {
                if (ri->writefn) {
                    TCGv_i32 tmp;
                    TCGv_ptr tmpptr;
                    tmp = load_reg(s, rt);
                    tmpptr = tcg_const_ptr(ri);
                    gen_helper_set_cp_reg(cpu_env, tmpptr, tmp);
                    tcg_temp_free_ptr(tmpptr);
                    tcg_temp_free_i32(tmp);
                } else {
                    TCGv_i32 tmp = load_reg(s, rt);
                    store_cpu_offset(tmp, ri->fieldoffset);
                }
            }
        }

        /* I/O operations must end the TB here (whether read or write) */
        need_exit_tb = ((tb_cflags(s->base.tb) & CF_USE_ICOUNT) &&
                        (ri->type & ARM_CP_IO));

        if (!isread && !(ri->type & ARM_CP_SUPPRESS_TB_END)) {
            /*
             * A write to any coprocessor register that ends a TB
             * must rebuild the hflags for the next TB.
             */
            TCGv_i32 tcg_el = tcg_const_i32(s->current_el);
            if (arm_dc_feature(s, ARM_FEATURE_M)) {
                gen_helper_rebuild_hflags_m32(cpu_env, tcg_el);
            } else {
                if (ri->type & ARM_CP_NEWEL) {
                    gen_helper_rebuild_hflags_a32_newel(cpu_env);
                } else {
                    gen_helper_rebuild_hflags_a32(cpu_env, tcg_el);
                }
            }
            tcg_temp_free_i32(tcg_el);
            /*
             * We default to ending the TB on a coprocessor register write,
             * but allow this to be suppressed by the register definition
             * (usually only necessary to work around guest bugs).
             */
            need_exit_tb = true;
        }
        if (need_exit_tb) {
            gen_lookup_tb(s);
        }

        return 0;
    }

    /* Unknown register; this might be a guest error or a QEMU
     * unimplemented feature.
     */
    if (is64) {
        qemu_log_mask(LOG_UNIMP, "%s access to unsupported AArch32 "
                      "64 bit system register cp:%d opc1: %d crm:%d "
                      "(%s)\n",
                      isread ? "read" : "write", cpnum, opc1, crm,
                      s->ns ? "non-secure" : "secure");
    } else {
        qemu_log_mask(LOG_UNIMP, "%s access to unsupported AArch32 "
                      "system register cp:%d opc1:%d crn:%d crm:%d opc2:%d "
                      "(%s)\n",
                      isread ? "read" : "write", cpnum, opc1, crn, crm, opc2,
                      s->ns ? "non-secure" : "secure");
    }

    return 1;
}


/* Store a 64-bit value to a register pair.  Clobbers val.  */
static void gen_storeq_reg(DisasContext *s, int rlow, int rhigh, TCGv_i64 val)
{
    TCGv_i32 tmp;
    tmp = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(tmp, val);
    store_reg(s, rlow, tmp);
    tmp = tcg_temp_new_i32();
    tcg_gen_extrh_i64_i32(tmp, val);
    store_reg(s, rhigh, tmp);
}

/* load and add a 64-bit value from a register pair.  */
static void gen_addq(DisasContext *s, TCGv_i64 val, int rlow, int rhigh)
{
    TCGv_i64 tmp;
    TCGv_i32 tmpl;
    TCGv_i32 tmph;

    /* Load 64-bit value rd:rn.  */
    tmpl = load_reg(s, rlow);
    tmph = load_reg(s, rhigh);
    tmp = tcg_temp_new_i64();
    tcg_gen_concat_i32_i64(tmp, tmpl, tmph);
    tcg_temp_free_i32(tmpl);
    tcg_temp_free_i32(tmph);
    tcg_gen_add_i64(val, val, tmp);
    tcg_temp_free_i64(tmp);
}

/* Set N and Z flags from hi|lo.  */
static void gen_logicq_cc(TCGv_i32 lo, TCGv_i32 hi)
{
    tcg_gen_mov_i32(cpu_NF, hi);
    tcg_gen_or_i32(cpu_ZF, lo, hi);
}

/* Load/Store exclusive instructions are implemented by remembering
   the value/address loaded, and seeing if these are the same
   when the store is performed.  This should be sufficient to implement
   the architecturally mandated semantics, and avoids having to monitor
   regular stores.  The compare vs the remembered value is done during
   the cmpxchg operation, but we must compare the addresses manually.  */
static void gen_load_exclusive(DisasContext *s, int rt, int rt2,
                               TCGv_i32 addr, int size)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    MemOp opc = size | MO_ALIGN | s->be_data;

    s->is_ldex = true;

    if (size == 3) {
        TCGv_i32 tmp2 = tcg_temp_new_i32();
        TCGv_i64 t64 = tcg_temp_new_i64();

        /* For AArch32, architecturally the 32-bit word at the lowest
         * address is always Rt and the one at addr+4 is Rt2, even if
         * the CPU is big-endian. That means we don't want to do a
         * gen_aa32_ld_i64(), which invokes gen_aa32_frob64() as if
         * for an architecturally 64-bit access, but instead do a
         * 64-bit access using MO_BE if appropriate and then split
         * the two halves.
         * This only makes a difference for BE32 user-mode, where
         * frob64() must not flip the two halves of the 64-bit data
         * but this code must treat BE32 user-mode like BE32 system.
         */
        TCGv taddr = gen_aa32_addr(s, addr, opc);

        tcg_gen_qemu_ld_i64(t64, taddr, get_mem_index(s), opc);
        tcg_temp_free(taddr);
        tcg_gen_mov_i64(cpu_exclusive_val, t64);
        if (s->be_data == MO_BE) {
            tcg_gen_extr_i64_i32(tmp2, tmp, t64);
        } else {
            tcg_gen_extr_i64_i32(tmp, tmp2, t64);
        }
        tcg_temp_free_i64(t64);

        store_reg(s, rt2, tmp2);
    } else {
        gen_aa32_ld_i32(s, tmp, addr, get_mem_index(s), opc);
        tcg_gen_extu_i32_i64(cpu_exclusive_val, tmp);
    }

    store_reg(s, rt, tmp);
    tcg_gen_extu_i32_i64(cpu_exclusive_addr, addr);
}

static void gen_clrex(DisasContext *s)
{
    tcg_gen_movi_i64(cpu_exclusive_addr, -1);
}

static void gen_store_exclusive(DisasContext *s, int rd, int rt, int rt2,
                                TCGv_i32 addr, int size)
{
    TCGv_i32 t0, t1, t2;
    TCGv_i64 extaddr;
    TCGv taddr;
    TCGLabel *done_label;
    TCGLabel *fail_label;
    MemOp opc = size | MO_ALIGN | s->be_data;

    /* if (env->exclusive_addr == addr && env->exclusive_val == [addr]) {
         [addr] = {Rt};
         {Rd} = 0;
       } else {
         {Rd} = 1;
       } */
    fail_label = gen_new_label();
    done_label = gen_new_label();
    extaddr = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(extaddr, addr);
    tcg_gen_brcond_i64(TCG_COND_NE, extaddr, cpu_exclusive_addr, fail_label);
    tcg_temp_free_i64(extaddr);

    taddr = gen_aa32_addr(s, addr, opc);
    t0 = tcg_temp_new_i32();
    t1 = load_reg(s, rt);
    if (size == 3) {
        TCGv_i64 o64 = tcg_temp_new_i64();
        TCGv_i64 n64 = tcg_temp_new_i64();

        t2 = load_reg(s, rt2);
        /* For AArch32, architecturally the 32-bit word at the lowest
         * address is always Rt and the one at addr+4 is Rt2, even if
         * the CPU is big-endian. Since we're going to treat this as a
         * single 64-bit BE store, we need to put the two halves in the
         * opposite order for BE to LE, so that they end up in the right
         * places.
         * We don't want gen_aa32_frob64() because that does the wrong
         * thing for BE32 usermode.
         */
        if (s->be_data == MO_BE) {
            tcg_gen_concat_i32_i64(n64, t2, t1);
        } else {
            tcg_gen_concat_i32_i64(n64, t1, t2);
        }
        tcg_temp_free_i32(t2);

        tcg_gen_atomic_cmpxchg_i64(o64, taddr, cpu_exclusive_val, n64,
                                   get_mem_index(s), opc);
        tcg_temp_free_i64(n64);

        tcg_gen_setcond_i64(TCG_COND_NE, o64, o64, cpu_exclusive_val);
        tcg_gen_extrl_i64_i32(t0, o64);

        tcg_temp_free_i64(o64);
    } else {
        t2 = tcg_temp_new_i32();
        tcg_gen_extrl_i64_i32(t2, cpu_exclusive_val);
        tcg_gen_atomic_cmpxchg_i32(t0, taddr, t2, t1, get_mem_index(s), opc);
        tcg_gen_setcond_i32(TCG_COND_NE, t0, t0, t2);
        tcg_temp_free_i32(t2);
    }
    tcg_temp_free_i32(t1);
    tcg_temp_free(taddr);
    tcg_gen_mov_i32(cpu_R[rd], t0);
    tcg_temp_free_i32(t0);
    tcg_gen_br(done_label);

    gen_set_label(fail_label);
    tcg_gen_movi_i32(cpu_R[rd], 1);
    gen_set_label(done_label);
    tcg_gen_movi_i64(cpu_exclusive_addr, -1);
}

/* gen_srs:
 * @env: CPUARMState
 * @s: DisasContext
 * @mode: mode field from insn (which stack to store to)
 * @amode: addressing mode (DA/IA/DB/IB), encoded as per P,U bits in ARM insn
 * @writeback: true if writeback bit set
 *
 * Generate code for the SRS (Store Return State) insn.
 */
static void gen_srs(DisasContext *s,
                    uint32_t mode, uint32_t amode, bool writeback)
{
    int32_t offset;
    TCGv_i32 addr, tmp;
    bool undef = false;

    /* SRS is:
     * - trapped to EL3 if EL3 is AArch64 and we are at Secure EL1
     *   and specified mode is monitor mode
     * - UNDEFINED in Hyp mode
     * - UNPREDICTABLE in User or System mode
     * - UNPREDICTABLE if the specified mode is:
     * -- not implemented
     * -- not a valid mode number
     * -- a mode that's at a higher exception level
     * -- Monitor, if we are Non-secure
     * For the UNPREDICTABLE cases we choose to UNDEF.
     */
    if (s->current_el == 1 && !s->ns && mode == ARM_CPU_MODE_MON) {
        gen_exception_insn(s, s->pc_curr, EXCP_UDEF, syn_uncategorized(), 3);
        return;
    }

    if (s->current_el == 0 || s->current_el == 2) {
        undef = true;
    }

    switch (mode) {
    case ARM_CPU_MODE_USR:
    case ARM_CPU_MODE_FIQ:
    case ARM_CPU_MODE_IRQ:
    case ARM_CPU_MODE_SVC:
    case ARM_CPU_MODE_ABT:
    case ARM_CPU_MODE_UND:
    case ARM_CPU_MODE_SYS:
        break;
    case ARM_CPU_MODE_HYP:
        if (s->current_el == 1 || !arm_dc_feature(s, ARM_FEATURE_EL2)) {
            undef = true;
        }
        break;
    case ARM_CPU_MODE_MON:
        /* No need to check specifically for "are we non-secure" because
         * we've already made EL0 UNDEF and handled the trap for S-EL1;
         * so if this isn't EL3 then we must be non-secure.
         */
        if (s->current_el != 3) {
            undef = true;
        }
        break;
    default:
        undef = true;
    }

    if (undef) {
        unallocated_encoding(s);
        return;
    }

    addr = tcg_temp_new_i32();
    tmp = tcg_const_i32(mode);
    /* get_r13_banked() will raise an exception if called from System mode */
    gen_set_condexec(s);
    gen_set_pc_im(s, s->pc_curr);
    gen_helper_get_r13_banked(addr, cpu_env, tmp);
    tcg_temp_free_i32(tmp);
    switch (amode) {
    case 0: /* DA */
        offset = -4;
        break;
    case 1: /* IA */
        offset = 0;
        break;
    case 2: /* DB */
        offset = -8;
        break;
    case 3: /* IB */
        offset = 4;
        break;
    default:
        abort();
    }
    tcg_gen_addi_i32(addr, addr, offset);
    tmp = load_reg(s, 14);
    gen_aa32_st32(s, tmp, addr, get_mem_index(s));
    tcg_temp_free_i32(tmp);
    tmp = load_cpu_field(spsr);
    tcg_gen_addi_i32(addr, addr, 4);
    gen_aa32_st32(s, tmp, addr, get_mem_index(s));
    tcg_temp_free_i32(tmp);
    if (writeback) {
        switch (amode) {
        case 0:
            offset = -8;
            break;
        case 1:
            offset = 4;
            break;
        case 2:
            offset = -4;
            break;
        case 3:
            offset = 0;
            break;
        default:
            abort();
        }
        tcg_gen_addi_i32(addr, addr, offset);
        tmp = tcg_const_i32(mode);
        gen_helper_set_r13_banked(cpu_env, tmp, addr);
        tcg_temp_free_i32(tmp);
    }
    tcg_temp_free_i32(addr);
    s->base.is_jmp = DISAS_UPDATE;
}

/* Generate a label used for skipping this instruction */
static void arm_gen_condlabel(DisasContext *s)
{
    if (!s->condjmp) {
        s->condlabel = gen_new_label();
        s->condjmp = 1;
    }
}

/* Skip this instruction if the ARM condition is false */
static void arm_skip_unless(DisasContext *s, uint32_t cond)
{
    arm_gen_condlabel(s);
    arm_gen_test_cc(cond ^ 1, s->condlabel);
}


/*
 * Constant expanders for the decoders.
 */

static int negate(DisasContext *s, int x)
{
    return -x;
}

static int plus_2(DisasContext *s, int x)
{
    return x + 2;
}

static int times_2(DisasContext *s, int x)
{
    return x * 2;
}

static int times_4(DisasContext *s, int x)
{
    return x * 4;
}

/* Return only the rotation part of T32ExpandImm.  */
static int t32_expandimm_rot(DisasContext *s, int x)
{
    return x & 0xc00 ? extract32(x, 7, 5) : 0;
}

/* Return the unrotated immediate from T32ExpandImm.  */
static int t32_expandimm_imm(DisasContext *s, int x)
{
    int imm = extract32(x, 0, 8);

    switch (extract32(x, 8, 4)) {
    case 0: /* XY */
        /* Nothing to do.  */
        break;
    case 1: /* 00XY00XY */
        imm *= 0x00010001;
        break;
    case 2: /* XY00XY00 */
        imm *= 0x01000100;
        break;
    case 3: /* XYXYXYXY */
        imm *= 0x01010101;
        break;
    default:
        /* Rotated constant.  */
        imm |= 0x80;
        break;
    }
    return imm;
}

static int t32_branch24(DisasContext *s, int x)
{
    /* Convert J1:J2 at x[22:21] to I2:I1, which involves I=J^~S.  */
    x ^= !(x < 0) * (3 << 21);
    /* Append the final zero.  */
    return x << 1;
}

static int t16_setflags(DisasContext *s)
{
    return s->condexec_mask == 0;
}

static int t16_push_list(DisasContext *s, int x)
{
    return (x & 0xff) | (x & 0x100) << (14 - 8);
}

static int t16_pop_list(DisasContext *s, int x)
{
    return (x & 0xff) | (x & 0x100) << (15 - 8);
}

/*
 * Include the generated decoders.
 */

#include "decode-a32.inc.c"
#include "decode-a32-uncond.inc.c"
#include "decode-t32.inc.c"
#include "decode-t16.inc.c"

/* Helpers to swap operands for reverse-subtract.  */
static void gen_rsb(TCGv_i32 dst, TCGv_i32 a, TCGv_i32 b)
{
    tcg_gen_sub_i32(dst, b, a);
}

static void gen_rsb_CC(TCGv_i32 dst, TCGv_i32 a, TCGv_i32 b)
{
    gen_sub_CC(dst, b, a);
}

static void gen_rsc(TCGv_i32 dest, TCGv_i32 a, TCGv_i32 b)
{
    gen_sub_carry(dest, b, a);
}

static void gen_rsc_CC(TCGv_i32 dest, TCGv_i32 a, TCGv_i32 b)
{
    gen_sbc_CC(dest, b, a);
}

/*
 * Helpers for the data processing routines.
 *
 * After the computation store the results back.
 * This may be suppressed altogether (STREG_NONE), require a runtime
 * check against the stack limits (STREG_SP_CHECK), or generate an
 * exception return.  Oh, or store into a register.
 *
 * Always return true, indicating success for a trans_* function.
 */
typedef enum {
   STREG_NONE,
   STREG_NORMAL,
   STREG_SP_CHECK,
   STREG_EXC_RET,
} StoreRegKind;

static bool store_reg_kind(DisasContext *s, int rd,
                            TCGv_i32 val, StoreRegKind kind)
{
    switch (kind) {
    case STREG_NONE:
        tcg_temp_free_i32(val);
        return true;
    case STREG_NORMAL:
        /* See ALUWritePC: Interworking only from a32 mode. */
        if (s->thumb) {
            store_reg(s, rd, val);
        } else {
            store_reg_bx(s, rd, val);
        }
        return true;
    case STREG_SP_CHECK:
        store_sp_checked(s, val);
        return true;
    case STREG_EXC_RET:
        gen_exception_return(s, val);
        return true;
    }
    g_assert_not_reached();
}

/*
 * Data Processing (register)
 *
 * Operate, with set flags, one register source,
 * one immediate shifted register source, and a destination.
 */
static bool op_s_rrr_shi(DisasContext *s, arg_s_rrr_shi *a,
                         void (*gen)(TCGv_i32, TCGv_i32, TCGv_i32),
                         int logic_cc, StoreRegKind kind)
{
    TCGv_i32 tmp1, tmp2;

    tmp2 = load_reg(s, a->rm);
    gen_arm_shift_im(tmp2, a->shty, a->shim, logic_cc);
    tmp1 = load_reg(s, a->rn);

    gen(tmp1, tmp1, tmp2);
    tcg_temp_free_i32(tmp2);

    if (logic_cc) {
        gen_logic_CC(tmp1);
    }
    return store_reg_kind(s, a->rd, tmp1, kind);
}

static bool op_s_rxr_shi(DisasContext *s, arg_s_rrr_shi *a,
                         void (*gen)(TCGv_i32, TCGv_i32),
                         int logic_cc, StoreRegKind kind)
{
    TCGv_i32 tmp;

    tmp = load_reg(s, a->rm);
    gen_arm_shift_im(tmp, a->shty, a->shim, logic_cc);

    gen(tmp, tmp);
    if (logic_cc) {
        gen_logic_CC(tmp);
    }
    return store_reg_kind(s, a->rd, tmp, kind);
}

/*
 * Data-processing (register-shifted register)
 *
 * Operate, with set flags, one register source,
 * one register shifted register source, and a destination.
 */
static bool op_s_rrr_shr(DisasContext *s, arg_s_rrr_shr *a,
                         void (*gen)(TCGv_i32, TCGv_i32, TCGv_i32),
                         int logic_cc, StoreRegKind kind)
{
    TCGv_i32 tmp1, tmp2;

    tmp1 = load_reg(s, a->rs);
    tmp2 = load_reg(s, a->rm);
    gen_arm_shift_reg(tmp2, a->shty, tmp1, logic_cc);
    tmp1 = load_reg(s, a->rn);

    gen(tmp1, tmp1, tmp2);
    tcg_temp_free_i32(tmp2);

    if (logic_cc) {
        gen_logic_CC(tmp1);
    }
    return store_reg_kind(s, a->rd, tmp1, kind);
}

static bool op_s_rxr_shr(DisasContext *s, arg_s_rrr_shr *a,
                         void (*gen)(TCGv_i32, TCGv_i32),
                         int logic_cc, StoreRegKind kind)
{
    TCGv_i32 tmp1, tmp2;

    tmp1 = load_reg(s, a->rs);
    tmp2 = load_reg(s, a->rm);
    gen_arm_shift_reg(tmp2, a->shty, tmp1, logic_cc);

    gen(tmp2, tmp2);
    if (logic_cc) {
        gen_logic_CC(tmp2);
    }
    return store_reg_kind(s, a->rd, tmp2, kind);
}

/*
 * Data-processing (immediate)
 *
 * Operate, with set flags, one register source,
 * one rotated immediate, and a destination.
 *
 * Note that logic_cc && a->rot setting CF based on the msb of the
 * immediate is the reason why we must pass in the unrotated form
 * of the immediate.
 */
static bool op_s_rri_rot(DisasContext *s, arg_s_rri_rot *a,
                         void (*gen)(TCGv_i32, TCGv_i32, TCGv_i32),
                         int logic_cc, StoreRegKind kind)
{
    TCGv_i32 tmp1, tmp2;
    uint32_t imm;

    imm = ror32(a->imm, a->rot);
    if (logic_cc && a->rot) {
        tcg_gen_movi_i32(cpu_CF, imm >> 31);
    }
    tmp2 = tcg_const_i32(imm);
    tmp1 = load_reg(s, a->rn);

    gen(tmp1, tmp1, tmp2);
    tcg_temp_free_i32(tmp2);

    if (logic_cc) {
        gen_logic_CC(tmp1);
    }
    return store_reg_kind(s, a->rd, tmp1, kind);
}

static bool op_s_rxi_rot(DisasContext *s, arg_s_rri_rot *a,
                         void (*gen)(TCGv_i32, TCGv_i32),
                         int logic_cc, StoreRegKind kind)
{
    TCGv_i32 tmp;
    uint32_t imm;

    imm = ror32(a->imm, a->rot);
    if (logic_cc && a->rot) {
        tcg_gen_movi_i32(cpu_CF, imm >> 31);
    }
    tmp = tcg_const_i32(imm);

    gen(tmp, tmp);
    if (logic_cc) {
        gen_logic_CC(tmp);
    }
    return store_reg_kind(s, a->rd, tmp, kind);
}

#define DO_ANY3(NAME, OP, L, K)                                         \
    static bool trans_##NAME##_rrri(DisasContext *s, arg_s_rrr_shi *a)  \
    { StoreRegKind k = (K); return op_s_rrr_shi(s, a, OP, L, k); }      \
    static bool trans_##NAME##_rrrr(DisasContext *s, arg_s_rrr_shr *a)  \
    { StoreRegKind k = (K); return op_s_rrr_shr(s, a, OP, L, k); }      \
    static bool trans_##NAME##_rri(DisasContext *s, arg_s_rri_rot *a)   \
    { StoreRegKind k = (K); return op_s_rri_rot(s, a, OP, L, k); }

#define DO_ANY2(NAME, OP, L, K)                                         \
    static bool trans_##NAME##_rxri(DisasContext *s, arg_s_rrr_shi *a)  \
    { StoreRegKind k = (K); return op_s_rxr_shi(s, a, OP, L, k); }      \
    static bool trans_##NAME##_rxrr(DisasContext *s, arg_s_rrr_shr *a)  \
    { StoreRegKind k = (K); return op_s_rxr_shr(s, a, OP, L, k); }      \
    static bool trans_##NAME##_rxi(DisasContext *s, arg_s_rri_rot *a)   \
    { StoreRegKind k = (K); return op_s_rxi_rot(s, a, OP, L, k); }

#define DO_CMP2(NAME, OP, L)                                            \
    static bool trans_##NAME##_xrri(DisasContext *s, arg_s_rrr_shi *a)  \
    { return op_s_rrr_shi(s, a, OP, L, STREG_NONE); }                   \
    static bool trans_##NAME##_xrrr(DisasContext *s, arg_s_rrr_shr *a)  \
    { return op_s_rrr_shr(s, a, OP, L, STREG_NONE); }                   \
    static bool trans_##NAME##_xri(DisasContext *s, arg_s_rri_rot *a)   \
    { return op_s_rri_rot(s, a, OP, L, STREG_NONE); }

DO_ANY3(AND, tcg_gen_and_i32, a->s, STREG_NORMAL)
DO_ANY3(EOR, tcg_gen_xor_i32, a->s, STREG_NORMAL)
DO_ANY3(ORR, tcg_gen_or_i32, a->s, STREG_NORMAL)
DO_ANY3(BIC, tcg_gen_andc_i32, a->s, STREG_NORMAL)

DO_ANY3(RSB, a->s ? gen_rsb_CC : gen_rsb, false, STREG_NORMAL)
DO_ANY3(ADC, a->s ? gen_adc_CC : gen_add_carry, false, STREG_NORMAL)
DO_ANY3(SBC, a->s ? gen_sbc_CC : gen_sub_carry, false, STREG_NORMAL)
DO_ANY3(RSC, a->s ? gen_rsc_CC : gen_rsc, false, STREG_NORMAL)

DO_CMP2(TST, tcg_gen_and_i32, true)
DO_CMP2(TEQ, tcg_gen_xor_i32, true)
DO_CMP2(CMN, gen_add_CC, false)
DO_CMP2(CMP, gen_sub_CC, false)

DO_ANY3(ADD, a->s ? gen_add_CC : tcg_gen_add_i32, false,
        a->rd == 13 && a->rn == 13 ? STREG_SP_CHECK : STREG_NORMAL)

/*
 * Note for the computation of StoreRegKind we return out of the
 * middle of the functions that are expanded by DO_ANY3, and that
 * we modify a->s via that parameter before it is used by OP.
 */
DO_ANY3(SUB, a->s ? gen_sub_CC : tcg_gen_sub_i32, false,
        ({
            StoreRegKind ret = STREG_NORMAL;
            if (a->rd == 15 && a->s) {
                /*
                 * See ALUExceptionReturn:
                 * In User mode, UNPREDICTABLE; we choose UNDEF.
                 * In Hyp mode, UNDEFINED.
                 */
                if (IS_USER(s) || s->current_el == 2) {
                    unallocated_encoding(s);
                    return true;
                }
                /* There is no writeback of nzcv to PSTATE.  */
                a->s = 0;
                ret = STREG_EXC_RET;
            } else if (a->rd == 13 && a->rn == 13) {
                ret = STREG_SP_CHECK;
            }
            ret;
        }))

DO_ANY2(MOV, tcg_gen_mov_i32, a->s,
        ({
            StoreRegKind ret = STREG_NORMAL;
            if (a->rd == 15 && a->s) {
                /*
                 * See ALUExceptionReturn:
                 * In User mode, UNPREDICTABLE; we choose UNDEF.
                 * In Hyp mode, UNDEFINED.
                 */
                if (IS_USER(s) || s->current_el == 2) {
                    unallocated_encoding(s);
                    return true;
                }
                /* There is no writeback of nzcv to PSTATE.  */
                a->s = 0;
                ret = STREG_EXC_RET;
            } else if (a->rd == 13) {
                ret = STREG_SP_CHECK;
            }
            ret;
        }))

DO_ANY2(MVN, tcg_gen_not_i32, a->s, STREG_NORMAL)

/*
 * ORN is only available with T32, so there is no register-shifted-register
 * form of the insn.  Using the DO_ANY3 macro would create an unused function.
 */
static bool trans_ORN_rrri(DisasContext *s, arg_s_rrr_shi *a)
{
    return op_s_rrr_shi(s, a, tcg_gen_orc_i32, a->s, STREG_NORMAL);
}

static bool trans_ORN_rri(DisasContext *s, arg_s_rri_rot *a)
{
    return op_s_rri_rot(s, a, tcg_gen_orc_i32, a->s, STREG_NORMAL);
}

#undef DO_ANY3
#undef DO_ANY2
#undef DO_CMP2

static bool trans_ADR(DisasContext *s, arg_ri *a)
{
    store_reg_bx(s, a->rd, add_reg_for_lit(s, 15, a->imm));
    return true;
}

static bool trans_MOVW(DisasContext *s, arg_MOVW *a)
{
    TCGv_i32 tmp;

    if (!ENABLE_ARCH_6T2) {
        return false;
    }

    tmp = tcg_const_i32(a->imm);
    store_reg(s, a->rd, tmp);
    return true;
}

static bool trans_MOVT(DisasContext *s, arg_MOVW *a)
{
    TCGv_i32 tmp;

    if (!ENABLE_ARCH_6T2) {
        return false;
    }

    tmp = load_reg(s, a->rd);
    tcg_gen_ext16u_i32(tmp, tmp);
    tcg_gen_ori_i32(tmp, tmp, a->imm << 16);
    store_reg(s, a->rd, tmp);
    return true;
}

/*
 * Multiply and multiply accumulate
 */

static bool op_mla(DisasContext *s, arg_s_rrrr *a, bool add)
{
    TCGv_i32 t1, t2;

    t1 = load_reg(s, a->rn);
    t2 = load_reg(s, a->rm);
    tcg_gen_mul_i32(t1, t1, t2);
    tcg_temp_free_i32(t2);
    if (add) {
        t2 = load_reg(s, a->ra);
        tcg_gen_add_i32(t1, t1, t2);
        tcg_temp_free_i32(t2);
    }
    if (a->s) {
        gen_logic_CC(t1);
    }
    store_reg(s, a->rd, t1);
    return true;
}

static bool trans_MUL(DisasContext *s, arg_MUL *a)
{
    return op_mla(s, a, false);
}

static bool trans_MLA(DisasContext *s, arg_MLA *a)
{
    return op_mla(s, a, true);
}

static bool trans_MLS(DisasContext *s, arg_MLS *a)
{
    TCGv_i32 t1, t2;

    if (!ENABLE_ARCH_6T2) {
        return false;
    }
    t1 = load_reg(s, a->rn);
    t2 = load_reg(s, a->rm);
    tcg_gen_mul_i32(t1, t1, t2);
    tcg_temp_free_i32(t2);
    t2 = load_reg(s, a->ra);
    tcg_gen_sub_i32(t1, t2, t1);
    tcg_temp_free_i32(t2);
    store_reg(s, a->rd, t1);
    return true;
}

static bool op_mlal(DisasContext *s, arg_s_rrrr *a, bool uns, bool add)
{
    TCGv_i32 t0, t1, t2, t3;

    t0 = load_reg(s, a->rm);
    t1 = load_reg(s, a->rn);
    if (uns) {
        tcg_gen_mulu2_i32(t0, t1, t0, t1);
    } else {
        tcg_gen_muls2_i32(t0, t1, t0, t1);
    }
    if (add) {
        t2 = load_reg(s, a->ra);
        t3 = load_reg(s, a->rd);
        tcg_gen_add2_i32(t0, t1, t0, t1, t2, t3);
        tcg_temp_free_i32(t2);
        tcg_temp_free_i32(t3);
    }
    if (a->s) {
        gen_logicq_cc(t0, t1);
    }
    store_reg(s, a->ra, t0);
    store_reg(s, a->rd, t1);
    return true;
}

static bool trans_UMULL(DisasContext *s, arg_UMULL *a)
{
    return op_mlal(s, a, true, false);
}

static bool trans_SMULL(DisasContext *s, arg_SMULL *a)
{
    return op_mlal(s, a, false, false);
}

static bool trans_UMLAL(DisasContext *s, arg_UMLAL *a)
{
    return op_mlal(s, a, true, true);
}

static bool trans_SMLAL(DisasContext *s, arg_SMLAL *a)
{
    return op_mlal(s, a, false, true);
}

static bool trans_UMAAL(DisasContext *s, arg_UMAAL *a)
{
    TCGv_i32 t0, t1, t2, zero;

    if (s->thumb
        ? !arm_dc_feature(s, ARM_FEATURE_THUMB_DSP)
        : !ENABLE_ARCH_6) {
        return false;
    }

    t0 = load_reg(s, a->rm);
    t1 = load_reg(s, a->rn);
    tcg_gen_mulu2_i32(t0, t1, t0, t1);
    zero = tcg_const_i32(0);
    t2 = load_reg(s, a->ra);
    tcg_gen_add2_i32(t0, t1, t0, t1, t2, zero);
    tcg_temp_free_i32(t2);
    t2 = load_reg(s, a->rd);
    tcg_gen_add2_i32(t0, t1, t0, t1, t2, zero);
    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(zero);
    store_reg(s, a->ra, t0);
    store_reg(s, a->rd, t1);
    return true;
}

/*
 * Saturating addition and subtraction
 */

static bool op_qaddsub(DisasContext *s, arg_rrr *a, bool add, bool doub)
{
    TCGv_i32 t0, t1;

    if (s->thumb
        ? !arm_dc_feature(s, ARM_FEATURE_THUMB_DSP)
        : !ENABLE_ARCH_5TE) {
        return false;
    }

    t0 = load_reg(s, a->rm);
    t1 = load_reg(s, a->rn);
    if (doub) {
        gen_helper_add_saturate(t1, cpu_env, t1, t1);
    }
    if (add) {
        gen_helper_add_saturate(t0, cpu_env, t0, t1);
    } else {
        gen_helper_sub_saturate(t0, cpu_env, t0, t1);
    }
    tcg_temp_free_i32(t1);
    store_reg(s, a->rd, t0);
    return true;
}

#define DO_QADDSUB(NAME, ADD, DOUB) \
static bool trans_##NAME(DisasContext *s, arg_rrr *a)    \
{                                                        \
    return op_qaddsub(s, a, ADD, DOUB);                  \
}

DO_QADDSUB(QADD, true, false)
DO_QADDSUB(QSUB, false, false)
DO_QADDSUB(QDADD, true, true)
DO_QADDSUB(QDSUB, false, true)

#undef DO_QADDSUB

/*
 * Halfword multiply and multiply accumulate
 */

static bool op_smlaxxx(DisasContext *s, arg_rrrr *a,
                       int add_long, bool nt, bool mt)
{
    TCGv_i32 t0, t1, tl, th;

    if (s->thumb
        ? !arm_dc_feature(s, ARM_FEATURE_THUMB_DSP)
        : !ENABLE_ARCH_5TE) {
        return false;
    }

    t0 = load_reg(s, a->rn);
    t1 = load_reg(s, a->rm);
    gen_mulxy(t0, t1, nt, mt);
    tcg_temp_free_i32(t1);

    switch (add_long) {
    case 0:
        store_reg(s, a->rd, t0);
        break;
    case 1:
        t1 = load_reg(s, a->ra);
        gen_helper_add_setq(t0, cpu_env, t0, t1);
        tcg_temp_free_i32(t1);
        store_reg(s, a->rd, t0);
        break;
    case 2:
        tl = load_reg(s, a->ra);
        th = load_reg(s, a->rd);
        /* Sign-extend the 32-bit product to 64 bits.  */
        t1 = tcg_temp_new_i32();
        tcg_gen_sari_i32(t1, t0, 31);
        tcg_gen_add2_i32(tl, th, tl, th, t0, t1);
        tcg_temp_free_i32(t0);
        tcg_temp_free_i32(t1);
        store_reg(s, a->ra, tl);
        store_reg(s, a->rd, th);
        break;
    default:
        g_assert_not_reached();
    }
    return true;
}

#define DO_SMLAX(NAME, add, nt, mt) \
static bool trans_##NAME(DisasContext *s, arg_rrrr *a)     \
{                                                          \
    return op_smlaxxx(s, a, add, nt, mt);                  \
}

DO_SMLAX(SMULBB, 0, 0, 0)
DO_SMLAX(SMULBT, 0, 0, 1)
DO_SMLAX(SMULTB, 0, 1, 0)
DO_SMLAX(SMULTT, 0, 1, 1)

DO_SMLAX(SMLABB, 1, 0, 0)
DO_SMLAX(SMLABT, 1, 0, 1)
DO_SMLAX(SMLATB, 1, 1, 0)
DO_SMLAX(SMLATT, 1, 1, 1)

DO_SMLAX(SMLALBB, 2, 0, 0)
DO_SMLAX(SMLALBT, 2, 0, 1)
DO_SMLAX(SMLALTB, 2, 1, 0)
DO_SMLAX(SMLALTT, 2, 1, 1)

#undef DO_SMLAX

static bool op_smlawx(DisasContext *s, arg_rrrr *a, bool add, bool mt)
{
    TCGv_i32 t0, t1;

    if (!ENABLE_ARCH_5TE) {
        return false;
    }

    t0 = load_reg(s, a->rn);
    t1 = load_reg(s, a->rm);
    /*
     * Since the nominal result is product<47:16>, shift the 16-bit
     * input up by 16 bits, so that the result is at product<63:32>.
     */
    if (mt) {
        tcg_gen_andi_i32(t1, t1, 0xffff0000);
    } else {
        tcg_gen_shli_i32(t1, t1, 16);
    }
    tcg_gen_muls2_i32(t0, t1, t0, t1);
    tcg_temp_free_i32(t0);
    if (add) {
        t0 = load_reg(s, a->ra);
        gen_helper_add_setq(t1, cpu_env, t1, t0);
        tcg_temp_free_i32(t0);
    }
    store_reg(s, a->rd, t1);
    return true;
}

#define DO_SMLAWX(NAME, add, mt) \
static bool trans_##NAME(DisasContext *s, arg_rrrr *a)     \
{                                                          \
    return op_smlawx(s, a, add, mt);                       \
}

DO_SMLAWX(SMULWB, 0, 0)
DO_SMLAWX(SMULWT, 0, 1)
DO_SMLAWX(SMLAWB, 1, 0)
DO_SMLAWX(SMLAWT, 1, 1)

#undef DO_SMLAWX

/*
 * MSR (immediate) and hints
 */

static bool trans_YIELD(DisasContext *s, arg_YIELD *a)
{
    /*
     * When running single-threaded TCG code, use the helper to ensure that
     * the next round-robin scheduled vCPU gets a crack.  When running in
     * MTTCG we don't generate jumps to the helper as it won't affect the
     * scheduling of other vCPUs.
     */
    if (!(tb_cflags(s->base.tb) & CF_PARALLEL)) {
        gen_set_pc_im(s, s->base.pc_next);
        s->base.is_jmp = DISAS_YIELD;
    }
    return true;
}

static bool trans_WFE(DisasContext *s, arg_WFE *a)
{
    /*
     * When running single-threaded TCG code, use the helper to ensure that
     * the next round-robin scheduled vCPU gets a crack.  In MTTCG mode we
     * just skip this instruction.  Currently the SEV/SEVL instructions,
     * which are *one* of many ways to wake the CPU from WFE, are not
     * implemented so we can't sleep like WFI does.
     */
    if (!(tb_cflags(s->base.tb) & CF_PARALLEL)) {
        gen_set_pc_im(s, s->base.pc_next);
        s->base.is_jmp = DISAS_WFE;
    }
    return true;
}

static bool trans_WFI(DisasContext *s, arg_WFI *a)
{
    /* For WFI, halt the vCPU until an IRQ. */
    gen_set_pc_im(s, s->base.pc_next);
    s->base.is_jmp = DISAS_WFI;
    return true;
}

static bool trans_NOP(DisasContext *s, arg_NOP *a)
{
    return true;
}

static bool trans_MSR_imm(DisasContext *s, arg_MSR_imm *a)
{
    uint32_t val = ror32(a->imm, a->rot * 2);
    uint32_t mask = msr_mask(s, a->mask, a->r);

    if (gen_set_psr_im(s, mask, a->r, val)) {
        unallocated_encoding(s);
    }
    return true;
}

/*
 * Cyclic Redundancy Check
 */

static bool op_crc32(DisasContext *s, arg_rrr *a, bool c, MemOp sz)
{
    TCGv_i32 t1, t2, t3;

    if (!dc_isar_feature(aa32_crc32, s)) {
        return false;
    }

    t1 = load_reg(s, a->rn);
    t2 = load_reg(s, a->rm);
    switch (sz) {
    case MO_8:
        gen_uxtb(t2);
        break;
    case MO_16:
        gen_uxth(t2);
        break;
    case MO_32:
        break;
    default:
        g_assert_not_reached();
    }
    t3 = tcg_const_i32(1 << sz);
    if (c) {
        gen_helper_crc32c(t1, t1, t2, t3);
    } else {
        gen_helper_crc32(t1, t1, t2, t3);
    }
    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t3);
    store_reg(s, a->rd, t1);
    return true;
}

#define DO_CRC32(NAME, c, sz) \
static bool trans_##NAME(DisasContext *s, arg_rrr *a)  \
    { return op_crc32(s, a, c, sz); }

DO_CRC32(CRC32B, false, MO_8)
DO_CRC32(CRC32H, false, MO_16)
DO_CRC32(CRC32W, false, MO_32)
DO_CRC32(CRC32CB, true, MO_8)
DO_CRC32(CRC32CH, true, MO_16)
DO_CRC32(CRC32CW, true, MO_32)

#undef DO_CRC32

/*
 * Miscellaneous instructions
 */

static bool trans_MRS_bank(DisasContext *s, arg_MRS_bank *a)
{
    if (arm_dc_feature(s, ARM_FEATURE_M)) {
        return false;
    }
    gen_mrs_banked(s, a->r, a->sysm, a->rd);
    return true;
}

static bool trans_MSR_bank(DisasContext *s, arg_MSR_bank *a)
{
    if (arm_dc_feature(s, ARM_FEATURE_M)) {
        return false;
    }
    gen_msr_banked(s, a->r, a->sysm, a->rn);
    return true;
}

static bool trans_MRS_reg(DisasContext *s, arg_MRS_reg *a)
{
    TCGv_i32 tmp;

    if (arm_dc_feature(s, ARM_FEATURE_M)) {
        return false;
    }
    if (a->r) {
        if (IS_USER(s)) {
            unallocated_encoding(s);
            return true;
        }
        tmp = load_cpu_field(spsr);
    } else {
        tmp = tcg_temp_new_i32();
        gen_helper_cpsr_read(tmp, cpu_env);
    }
    store_reg(s, a->rd, tmp);
    return true;
}

static bool trans_MSR_reg(DisasContext *s, arg_MSR_reg *a)
{
    TCGv_i32 tmp;
    uint32_t mask = msr_mask(s, a->mask, a->r);

    if (arm_dc_feature(s, ARM_FEATURE_M)) {
        return false;
    }
    tmp = load_reg(s, a->rn);
    if (gen_set_psr(s, mask, a->r, tmp)) {
        unallocated_encoding(s);
    }
    return true;
}

static bool trans_MRS_v7m(DisasContext *s, arg_MRS_v7m *a)
{
    TCGv_i32 tmp;

    if (!arm_dc_feature(s, ARM_FEATURE_M)) {
        return false;
    }
    tmp = tcg_const_i32(a->sysm);
    gen_helper_v7m_mrs(tmp, cpu_env, tmp);
    store_reg(s, a->rd, tmp);
    return true;
}

static bool trans_MSR_v7m(DisasContext *s, arg_MSR_v7m *a)
{
    TCGv_i32 addr, reg;

    if (!arm_dc_feature(s, ARM_FEATURE_M)) {
        return false;
    }
    addr = tcg_const_i32((a->mask << 10) | a->sysm);
    reg = load_reg(s, a->rn);
    gen_helper_v7m_msr(cpu_env, addr, reg);
    tcg_temp_free_i32(addr);
    tcg_temp_free_i32(reg);
    /* If we wrote to CONTROL, the EL might have changed */
    gen_helper_rebuild_hflags_m32_newel(cpu_env);
    gen_lookup_tb(s);
    return true;
}

static bool trans_BX(DisasContext *s, arg_BX *a)
{
    if (!ENABLE_ARCH_4T) {
        return false;
    }
    gen_bx_excret(s, load_reg(s, a->rm));
    return true;
}

static bool trans_BXJ(DisasContext *s, arg_BXJ *a)
{
    if (!ENABLE_ARCH_5J || arm_dc_feature(s, ARM_FEATURE_M)) {
        return false;
    }
    /* Trivial implementation equivalent to bx.  */
    gen_bx(s, load_reg(s, a->rm));
    return true;
}

static bool trans_BLX_r(DisasContext *s, arg_BLX_r *a)
{
    TCGv_i32 tmp;

    if (!ENABLE_ARCH_5) {
        return false;
    }
    tmp = load_reg(s, a->rm);
    tcg_gen_movi_i32(cpu_R[14], s->base.pc_next | s->thumb);
    gen_bx(s, tmp);
    return true;
}

/*
 * BXNS/BLXNS: only exist for v8M with the security extensions,
 * and always UNDEF if NonSecure.  We don't implement these in
 * the user-only mode either (in theory you can use them from
 * Secure User mode but they are too tied in to system emulation).
 */
static bool trans_BXNS(DisasContext *s, arg_BXNS *a)
{
    if (!s->v8m_secure || IS_USER_ONLY) {
        unallocated_encoding(s);
    } else {
        gen_bxns(s, a->rm);
    }
    return true;
}

static bool trans_BLXNS(DisasContext *s, arg_BLXNS *a)
{
    if (!s->v8m_secure || IS_USER_ONLY) {
        unallocated_encoding(s);
    } else {
        gen_blxns(s, a->rm);
    }
    return true;
}

static bool trans_CLZ(DisasContext *s, arg_CLZ *a)
{
    TCGv_i32 tmp;

    if (!ENABLE_ARCH_5) {
        return false;
    }
    tmp = load_reg(s, a->rm);
    tcg_gen_clzi_i32(tmp, tmp, 32);
    store_reg(s, a->rd, tmp);
    return true;
}

static bool trans_ERET(DisasContext *s, arg_ERET *a)
{
    TCGv_i32 tmp;

    if (!arm_dc_feature(s, ARM_FEATURE_V7VE)) {
        return false;
    }
    if (IS_USER(s)) {
        unallocated_encoding(s);
        return true;
    }
    if (s->current_el == 2) {
        /* ERET from Hyp uses ELR_Hyp, not LR */
        tmp = load_cpu_field(elr_el[2]);
    } else {
        tmp = load_reg(s, 14);
    }
    gen_exception_return(s, tmp);
    return true;
}

static bool trans_HLT(DisasContext *s, arg_HLT *a)
{
    gen_hlt(s, a->imm);
    return true;
}

static bool trans_BKPT(DisasContext *s, arg_BKPT *a)
{
    if (!ENABLE_ARCH_5) {
        return false;
    }
    if (arm_dc_feature(s, ARM_FEATURE_M) &&
        semihosting_enabled() &&
#ifndef CONFIG_USER_ONLY
        !IS_USER(s) &&
#endif
        (a->imm == 0xab)) {
        gen_exception_internal_insn(s, s->pc_curr, EXCP_SEMIHOST);
    } else {
        gen_exception_bkpt_insn(s, syn_aa32_bkpt(a->imm, false));
    }
    return true;
}

static bool trans_HVC(DisasContext *s, arg_HVC *a)
{
    if (!ENABLE_ARCH_7 || arm_dc_feature(s, ARM_FEATURE_M)) {
        return false;
    }
    if (IS_USER(s)) {
        unallocated_encoding(s);
    } else {
        gen_hvc(s, a->imm);
    }
    return true;
}

static bool trans_SMC(DisasContext *s, arg_SMC *a)
{
    if (!ENABLE_ARCH_6K || arm_dc_feature(s, ARM_FEATURE_M)) {
        return false;
    }
    if (IS_USER(s)) {
        unallocated_encoding(s);
    } else {
        gen_smc(s);
    }
    return true;
}

static bool trans_SG(DisasContext *s, arg_SG *a)
{
    if (!arm_dc_feature(s, ARM_FEATURE_M) ||
        !arm_dc_feature(s, ARM_FEATURE_V8)) {
        return false;
    }
    /*
     * SG (v8M only)
     * The bulk of the behaviour for this instruction is implemented
     * in v7m_handle_execute_nsc(), which deals with the insn when
     * it is executed by a CPU in non-secure state from memory
     * which is Secure & NonSecure-Callable.
     * Here we only need to handle the remaining cases:
     *  * in NS memory (including the "security extension not
     *    implemented" case) : NOP
     *  * in S memory but CPU already secure (clear IT bits)
     * We know that the attribute for the memory this insn is
     * in must match the current CPU state, because otherwise
     * get_phys_addr_pmsav8 would have generated an exception.
     */
    if (s->v8m_secure) {
        /* Like the IT insn, we don't need to generate any code */
        s->condexec_cond = 0;
        s->condexec_mask = 0;
    }
    return true;
}

static bool trans_TT(DisasContext *s, arg_TT *a)
{
    TCGv_i32 addr, tmp;

    if (!arm_dc_feature(s, ARM_FEATURE_M) ||
        !arm_dc_feature(s, ARM_FEATURE_V8)) {
        return false;
    }
    if (a->rd == 13 || a->rd == 15 || a->rn == 15) {
        /* We UNDEF for these UNPREDICTABLE cases */
        unallocated_encoding(s);
        return true;
    }
    if (a->A && !s->v8m_secure) {
        /* This case is UNDEFINED.  */
        unallocated_encoding(s);
        return true;
    }

    addr = load_reg(s, a->rn);
    tmp = tcg_const_i32((a->A << 1) | a->T);
    gen_helper_v7m_tt(tmp, cpu_env, addr, tmp);
    tcg_temp_free_i32(addr);
    store_reg(s, a->rd, tmp);
    return true;
}

/*
 * Load/store register index
 */

static ISSInfo make_issinfo(DisasContext *s, int rd, bool p, bool w)
{
    ISSInfo ret;

    /* ISS not valid if writeback */
    if (p && !w) {
        ret = rd;
        if (s->base.pc_next - s->pc_curr == 2) {
            ret |= ISSIs16Bit;
        }
    } else {
        ret = ISSInvalid;
    }
    return ret;
}

static TCGv_i32 op_addr_rr_pre(DisasContext *s, arg_ldst_rr *a)
{
    TCGv_i32 addr = load_reg(s, a->rn);

    if (s->v8m_stackcheck && a->rn == 13 && a->w) {
        gen_helper_v8m_stackcheck(cpu_env, addr);
    }

    if (a->p) {
        TCGv_i32 ofs = load_reg(s, a->rm);
        gen_arm_shift_im(ofs, a->shtype, a->shimm, 0);
        if (a->u) {
            tcg_gen_add_i32(addr, addr, ofs);
        } else {
            tcg_gen_sub_i32(addr, addr, ofs);
        }
        tcg_temp_free_i32(ofs);
    }
    return addr;
}

static void op_addr_rr_post(DisasContext *s, arg_ldst_rr *a,
                            TCGv_i32 addr, int address_offset)
{
    if (!a->p) {
        TCGv_i32 ofs = load_reg(s, a->rm);
        gen_arm_shift_im(ofs, a->shtype, a->shimm, 0);
        if (a->u) {
            tcg_gen_add_i32(addr, addr, ofs);
        } else {
            tcg_gen_sub_i32(addr, addr, ofs);
        }
        tcg_temp_free_i32(ofs);
    } else if (!a->w) {
        tcg_temp_free_i32(addr);
        return;
    }
    tcg_gen_addi_i32(addr, addr, address_offset);
    store_reg(s, a->rn, addr);
}

static bool op_load_rr(DisasContext *s, arg_ldst_rr *a,
                       MemOp mop, int mem_idx)
{
    ISSInfo issinfo = make_issinfo(s, a->rt, a->p, a->w);
    TCGv_i32 addr, tmp;

    addr = op_addr_rr_pre(s, a);

    tmp = tcg_temp_new_i32();
    gen_aa32_ld_i32(s, tmp, addr, mem_idx, mop | s->be_data);
    disas_set_da_iss(s, mop, issinfo);

    /*
     * Perform base writeback before the loaded value to
     * ensure correct behavior with overlapping index registers.
     */
    op_addr_rr_post(s, a, addr, 0);
    store_reg_from_load(s, a->rt, tmp);
    return true;
}

static bool op_store_rr(DisasContext *s, arg_ldst_rr *a,
                        MemOp mop, int mem_idx)
{
    ISSInfo issinfo = make_issinfo(s, a->rt, a->p, a->w) | ISSIsWrite;
    TCGv_i32 addr, tmp;

    addr = op_addr_rr_pre(s, a);

    tmp = load_reg(s, a->rt);
    gen_aa32_st_i32(s, tmp, addr, mem_idx, mop | s->be_data);
    disas_set_da_iss(s, mop, issinfo);
    tcg_temp_free_i32(tmp);

    op_addr_rr_post(s, a, addr, 0);
    return true;
}

static bool trans_LDRD_rr(DisasContext *s, arg_ldst_rr *a)
{
    int mem_idx = get_mem_index(s);
    TCGv_i32 addr, tmp;

    if (!ENABLE_ARCH_5TE) {
        return false;
    }
    if (a->rt & 1) {
        unallocated_encoding(s);
        return true;
    }
    addr = op_addr_rr_pre(s, a);

    tmp = tcg_temp_new_i32();
    gen_aa32_ld_i32(s, tmp, addr, mem_idx, MO_UL | s->be_data);
    store_reg(s, a->rt, tmp);

    tcg_gen_addi_i32(addr, addr, 4);

    tmp = tcg_temp_new_i32();
    gen_aa32_ld_i32(s, tmp, addr, mem_idx, MO_UL | s->be_data);
    store_reg(s, a->rt + 1, tmp);

    /* LDRD w/ base writeback is undefined if the registers overlap.  */
    op_addr_rr_post(s, a, addr, -4);
    return true;
}

static bool trans_STRD_rr(DisasContext *s, arg_ldst_rr *a)
{
    int mem_idx = get_mem_index(s);
    TCGv_i32 addr, tmp;

    if (!ENABLE_ARCH_5TE) {
        return false;
    }
    if (a->rt & 1) {
        unallocated_encoding(s);
        return true;
    }
    addr = op_addr_rr_pre(s, a);

    tmp = load_reg(s, a->rt);
    gen_aa32_st_i32(s, tmp, addr, mem_idx, MO_UL | s->be_data);
    tcg_temp_free_i32(tmp);

    tcg_gen_addi_i32(addr, addr, 4);

    tmp = load_reg(s, a->rt + 1);
    gen_aa32_st_i32(s, tmp, addr, mem_idx, MO_UL | s->be_data);
    tcg_temp_free_i32(tmp);

    op_addr_rr_post(s, a, addr, -4);
    return true;
}

/*
 * Load/store immediate index
 */

static TCGv_i32 op_addr_ri_pre(DisasContext *s, arg_ldst_ri *a)
{
    int ofs = a->imm;

    if (!a->u) {
        ofs = -ofs;
    }

    if (s->v8m_stackcheck && a->rn == 13 && a->w) {
        /*
         * Stackcheck. Here we know 'addr' is the current SP;
         * U is set if we're moving SP up, else down. It is
         * UNKNOWN whether the limit check triggers when SP starts
         * below the limit and ends up above it; we chose to do so.
         */
        if (!a->u) {
            TCGv_i32 newsp = tcg_temp_new_i32();
            tcg_gen_addi_i32(newsp, cpu_R[13], ofs);
            gen_helper_v8m_stackcheck(cpu_env, newsp);
            tcg_temp_free_i32(newsp);
        } else {
            gen_helper_v8m_stackcheck(cpu_env, cpu_R[13]);
        }
    }

    return add_reg_for_lit(s, a->rn, a->p ? ofs : 0);
}

static void op_addr_ri_post(DisasContext *s, arg_ldst_ri *a,
                            TCGv_i32 addr, int address_offset)
{
    if (!a->p) {
        if (a->u) {
            address_offset += a->imm;
        } else {
            address_offset -= a->imm;
        }
    } else if (!a->w) {
        tcg_temp_free_i32(addr);
        return;
    }
    tcg_gen_addi_i32(addr, addr, address_offset);
    store_reg(s, a->rn, addr);
}

static bool op_load_ri(DisasContext *s, arg_ldst_ri *a,
                       MemOp mop, int mem_idx)
{
    ISSInfo issinfo = make_issinfo(s, a->rt, a->p, a->w);
    TCGv_i32 addr, tmp;

    addr = op_addr_ri_pre(s, a);

    tmp = tcg_temp_new_i32();
    gen_aa32_ld_i32(s, tmp, addr, mem_idx, mop | s->be_data);
    disas_set_da_iss(s, mop, issinfo);

    /*
     * Perform base writeback before the loaded value to
     * ensure correct behavior with overlapping index registers.
     */
    op_addr_ri_post(s, a, addr, 0);
    store_reg_from_load(s, a->rt, tmp);
    return true;
}

static bool op_store_ri(DisasContext *s, arg_ldst_ri *a,
                        MemOp mop, int mem_idx)
{
    ISSInfo issinfo = make_issinfo(s, a->rt, a->p, a->w) | ISSIsWrite;
    TCGv_i32 addr, tmp;

    addr = op_addr_ri_pre(s, a);

    tmp = load_reg(s, a->rt);
    gen_aa32_st_i32(s, tmp, addr, mem_idx, mop | s->be_data);
    disas_set_da_iss(s, mop, issinfo);
    tcg_temp_free_i32(tmp);

    op_addr_ri_post(s, a, addr, 0);
    return true;
}

static bool op_ldrd_ri(DisasContext *s, arg_ldst_ri *a, int rt2)
{
    int mem_idx = get_mem_index(s);
    TCGv_i32 addr, tmp;

    addr = op_addr_ri_pre(s, a);

    tmp = tcg_temp_new_i32();
    gen_aa32_ld_i32(s, tmp, addr, mem_idx, MO_UL | s->be_data);
    store_reg(s, a->rt, tmp);

    tcg_gen_addi_i32(addr, addr, 4);

    tmp = tcg_temp_new_i32();
    gen_aa32_ld_i32(s, tmp, addr, mem_idx, MO_UL | s->be_data);
    store_reg(s, rt2, tmp);

    /* LDRD w/ base writeback is undefined if the registers overlap.  */
    op_addr_ri_post(s, a, addr, -4);
    return true;
}

static bool trans_LDRD_ri_a32(DisasContext *s, arg_ldst_ri *a)
{
    if (!ENABLE_ARCH_5TE || (a->rt & 1)) {
        return false;
    }
    return op_ldrd_ri(s, a, a->rt + 1);
}

static bool trans_LDRD_ri_t32(DisasContext *s, arg_ldst_ri2 *a)
{
    arg_ldst_ri b = {
        .u = a->u, .w = a->w, .p = a->p,
        .rn = a->rn, .rt = a->rt, .imm = a->imm
    };
    return op_ldrd_ri(s, &b, a->rt2);
}

static bool op_strd_ri(DisasContext *s, arg_ldst_ri *a, int rt2)
{
    int mem_idx = get_mem_index(s);
    TCGv_i32 addr, tmp;

    addr = op_addr_ri_pre(s, a);

    tmp = load_reg(s, a->rt);
    gen_aa32_st_i32(s, tmp, addr, mem_idx, MO_UL | s->be_data);
    tcg_temp_free_i32(tmp);

    tcg_gen_addi_i32(addr, addr, 4);

    tmp = load_reg(s, rt2);
    gen_aa32_st_i32(s, tmp, addr, mem_idx, MO_UL | s->be_data);
    tcg_temp_free_i32(tmp);

    op_addr_ri_post(s, a, addr, -4);
    return true;
}

static bool trans_STRD_ri_a32(DisasContext *s, arg_ldst_ri *a)
{
    if (!ENABLE_ARCH_5TE || (a->rt & 1)) {
        return false;
    }
    return op_strd_ri(s, a, a->rt + 1);
}

static bool trans_STRD_ri_t32(DisasContext *s, arg_ldst_ri2 *a)
{
    arg_ldst_ri b = {
        .u = a->u, .w = a->w, .p = a->p,
        .rn = a->rn, .rt = a->rt, .imm = a->imm
    };
    return op_strd_ri(s, &b, a->rt2);
}

#define DO_LDST(NAME, WHICH, MEMOP) \
static bool trans_##NAME##_ri(DisasContext *s, arg_ldst_ri *a)        \
{                                                                     \
    return op_##WHICH##_ri(s, a, MEMOP, get_mem_index(s));            \
}                                                                     \
static bool trans_##NAME##T_ri(DisasContext *s, arg_ldst_ri *a)       \
{                                                                     \
    return op_##WHICH##_ri(s, a, MEMOP, get_a32_user_mem_index(s));   \
}                                                                     \
static bool trans_##NAME##_rr(DisasContext *s, arg_ldst_rr *a)        \
{                                                                     \
    return op_##WHICH##_rr(s, a, MEMOP, get_mem_index(s));            \
}                                                                     \
static bool trans_##NAME##T_rr(DisasContext *s, arg_ldst_rr *a)       \
{                                                                     \
    return op_##WHICH##_rr(s, a, MEMOP, get_a32_user_mem_index(s));   \
}

DO_LDST(LDR, load, MO_UL)
DO_LDST(LDRB, load, MO_UB)
DO_LDST(LDRH, load, MO_UW)
DO_LDST(LDRSB, load, MO_SB)
DO_LDST(LDRSH, load, MO_SW)

DO_LDST(STR, store, MO_UL)
DO_LDST(STRB, store, MO_UB)
DO_LDST(STRH, store, MO_UW)

#undef DO_LDST

/*
 * Synchronization primitives
 */

static bool op_swp(DisasContext *s, arg_SWP *a, MemOp opc)
{
    TCGv_i32 addr, tmp;
    TCGv taddr;

    opc |= s->be_data;
    addr = load_reg(s, a->rn);
    taddr = gen_aa32_addr(s, addr, opc);
    tcg_temp_free_i32(addr);

    tmp = load_reg(s, a->rt2);
    tcg_gen_atomic_xchg_i32(tmp, taddr, tmp, get_mem_index(s), opc);
    tcg_temp_free(taddr);

    store_reg(s, a->rt, tmp);
    return true;
}

static bool trans_SWP(DisasContext *s, arg_SWP *a)
{
    return op_swp(s, a, MO_UL | MO_ALIGN);
}

static bool trans_SWPB(DisasContext *s, arg_SWP *a)
{
    return op_swp(s, a, MO_UB);
}

/*
 * Load/Store Exclusive and Load-Acquire/Store-Release
 */

static bool op_strex(DisasContext *s, arg_STREX *a, MemOp mop, bool rel)
{
    TCGv_i32 addr;
    /* Some cases stopped being UNPREDICTABLE in v8A (but not v8M) */
    bool v8a = ENABLE_ARCH_8 && !arm_dc_feature(s, ARM_FEATURE_M);

    /* We UNDEF for these UNPREDICTABLE cases.  */
    if (a->rd == 15 || a->rn == 15 || a->rt == 15
        || a->rd == a->rn || a->rd == a->rt
        || (!v8a && s->thumb && (a->rd == 13 || a->rt == 13))
        || (mop == MO_64
            && (a->rt2 == 15
                || a->rd == a->rt2
                || (!v8a && s->thumb && a->rt2 == 13)))) {
        unallocated_encoding(s);
        return true;
    }

    if (rel) {
        tcg_gen_mb(TCG_MO_ALL | TCG_BAR_STRL);
    }

    addr = tcg_temp_local_new_i32();
    load_reg_var(s, addr, a->rn);
    tcg_gen_addi_i32(addr, addr, a->imm);

    gen_store_exclusive(s, a->rd, a->rt, a->rt2, addr, mop);
    tcg_temp_free_i32(addr);
    return true;
}

static bool trans_STREX(DisasContext *s, arg_STREX *a)
{
    if (!ENABLE_ARCH_6) {
        return false;
    }
    return op_strex(s, a, MO_32, false);
}

static bool trans_STREXD_a32(DisasContext *s, arg_STREX *a)
{
    if (!ENABLE_ARCH_6K) {
        return false;
    }
    /* We UNDEF for these UNPREDICTABLE cases.  */
    if (a->rt & 1) {
        unallocated_encoding(s);
        return true;
    }
    a->rt2 = a->rt + 1;
    return op_strex(s, a, MO_64, false);
}

static bool trans_STREXD_t32(DisasContext *s, arg_STREX *a)
{
    return op_strex(s, a, MO_64, false);
}

static bool trans_STREXB(DisasContext *s, arg_STREX *a)
{
    if (s->thumb ? !ENABLE_ARCH_7 : !ENABLE_ARCH_6K) {
        return false;
    }
    return op_strex(s, a, MO_8, false);
}

static bool trans_STREXH(DisasContext *s, arg_STREX *a)
{
    if (s->thumb ? !ENABLE_ARCH_7 : !ENABLE_ARCH_6K) {
        return false;
    }
    return op_strex(s, a, MO_16, false);
}

static bool trans_STLEX(DisasContext *s, arg_STREX *a)
{
    if (!ENABLE_ARCH_8) {
        return false;
    }
    return op_strex(s, a, MO_32, true);
}

static bool trans_STLEXD_a32(DisasContext *s, arg_STREX *a)
{
    if (!ENABLE_ARCH_8) {
        return false;
    }
    /* We UNDEF for these UNPREDICTABLE cases.  */
    if (a->rt & 1) {
        unallocated_encoding(s);
        return true;
    }
    a->rt2 = a->rt + 1;
    return op_strex(s, a, MO_64, true);
}

static bool trans_STLEXD_t32(DisasContext *s, arg_STREX *a)
{
    if (!ENABLE_ARCH_8) {
        return false;
    }
    return op_strex(s, a, MO_64, true);
}

static bool trans_STLEXB(DisasContext *s, arg_STREX *a)
{
    if (!ENABLE_ARCH_8) {
        return false;
    }
    return op_strex(s, a, MO_8, true);
}

static bool trans_STLEXH(DisasContext *s, arg_STREX *a)
{
    if (!ENABLE_ARCH_8) {
        return false;
    }
    return op_strex(s, a, MO_16, true);
}

static bool op_stl(DisasContext *s, arg_STL *a, MemOp mop)
{
    TCGv_i32 addr, tmp;

    if (!ENABLE_ARCH_8) {
        return false;
    }
    /* We UNDEF for these UNPREDICTABLE cases.  */
    if (a->rn == 15 || a->rt == 15) {
        unallocated_encoding(s);
        return true;
    }

    addr = load_reg(s, a->rn);
    tmp = load_reg(s, a->rt);
    tcg_gen_mb(TCG_MO_ALL | TCG_BAR_STRL);
    gen_aa32_st_i32(s, tmp, addr, get_mem_index(s), mop | s->be_data);
    disas_set_da_iss(s, mop, a->rt | ISSIsAcqRel | ISSIsWrite);

    tcg_temp_free_i32(tmp);
    tcg_temp_free_i32(addr);
    return true;
}

static bool trans_STL(DisasContext *s, arg_STL *a)
{
    return op_stl(s, a, MO_UL);
}

static bool trans_STLB(DisasContext *s, arg_STL *a)
{
    return op_stl(s, a, MO_UB);
}

static bool trans_STLH(DisasContext *s, arg_STL *a)
{
    return op_stl(s, a, MO_UW);
}

static bool op_ldrex(DisasContext *s, arg_LDREX *a, MemOp mop, bool acq)
{
    TCGv_i32 addr;
    /* Some cases stopped being UNPREDICTABLE in v8A (but not v8M) */
    bool v8a = ENABLE_ARCH_8 && !arm_dc_feature(s, ARM_FEATURE_M);

    /* We UNDEF for these UNPREDICTABLE cases.  */
    if (a->rn == 15 || a->rt == 15
        || (!v8a && s->thumb && a->rt == 13)
        || (mop == MO_64
            && (a->rt2 == 15 || a->rt == a->rt2
                || (!v8a && s->thumb && a->rt2 == 13)))) {
        unallocated_encoding(s);
        return true;
    }

    addr = tcg_temp_local_new_i32();
    load_reg_var(s, addr, a->rn);
    tcg_gen_addi_i32(addr, addr, a->imm);

    gen_load_exclusive(s, a->rt, a->rt2, addr, mop);
    tcg_temp_free_i32(addr);

    if (acq) {
        tcg_gen_mb(TCG_MO_ALL | TCG_BAR_LDAQ);
    }
    return true;
}

static bool trans_LDREX(DisasContext *s, arg_LDREX *a)
{
    if (!ENABLE_ARCH_6) {
        return false;
    }
    return op_ldrex(s, a, MO_32, false);
}

static bool trans_LDREXD_a32(DisasContext *s, arg_LDREX *a)
{
    if (!ENABLE_ARCH_6K) {
        return false;
    }
    /* We UNDEF for these UNPREDICTABLE cases.  */
    if (a->rt & 1) {
        unallocated_encoding(s);
        return true;
    }
    a->rt2 = a->rt + 1;
    return op_ldrex(s, a, MO_64, false);
}

static bool trans_LDREXD_t32(DisasContext *s, arg_LDREX *a)
{
    return op_ldrex(s, a, MO_64, false);
}

static bool trans_LDREXB(DisasContext *s, arg_LDREX *a)
{
    if (s->thumb ? !ENABLE_ARCH_7 : !ENABLE_ARCH_6K) {
        return false;
    }
    return op_ldrex(s, a, MO_8, false);
}

static bool trans_LDREXH(DisasContext *s, arg_LDREX *a)
{
    if (s->thumb ? !ENABLE_ARCH_7 : !ENABLE_ARCH_6K) {
        return false;
    }
    return op_ldrex(s, a, MO_16, false);
}

static bool trans_LDAEX(DisasContext *s, arg_LDREX *a)
{
    if (!ENABLE_ARCH_8) {
        return false;
    }
    return op_ldrex(s, a, MO_32, true);
}

static bool trans_LDAEXD_a32(DisasContext *s, arg_LDREX *a)
{
    if (!ENABLE_ARCH_8) {
        return false;
    }
    /* We UNDEF for these UNPREDICTABLE cases.  */
    if (a->rt & 1) {
        unallocated_encoding(s);
        return true;
    }
    a->rt2 = a->rt + 1;
    return op_ldrex(s, a, MO_64, true);
}

static bool trans_LDAEXD_t32(DisasContext *s, arg_LDREX *a)
{
    if (!ENABLE_ARCH_8) {
        return false;
    }
    return op_ldrex(s, a, MO_64, true);
}

static bool trans_LDAEXB(DisasContext *s, arg_LDREX *a)
{
    if (!ENABLE_ARCH_8) {
        return false;
    }
    return op_ldrex(s, a, MO_8, true);
}

static bool trans_LDAEXH(DisasContext *s, arg_LDREX *a)
{
    if (!ENABLE_ARCH_8) {
        return false;
    }
    return op_ldrex(s, a, MO_16, true);
}

static bool op_lda(DisasContext *s, arg_LDA *a, MemOp mop)
{
    TCGv_i32 addr, tmp;

    if (!ENABLE_ARCH_8) {
        return false;
    }
    /* We UNDEF for these UNPREDICTABLE cases.  */
    if (a->rn == 15 || a->rt == 15) {
        unallocated_encoding(s);
        return true;
    }

    addr = load_reg(s, a->rn);
    tmp = tcg_temp_new_i32();
    gen_aa32_ld_i32(s, tmp, addr, get_mem_index(s), mop | s->be_data);
    disas_set_da_iss(s, mop, a->rt | ISSIsAcqRel);
    tcg_temp_free_i32(addr);

    store_reg(s, a->rt, tmp);
    tcg_gen_mb(TCG_MO_ALL | TCG_BAR_STRL);
    return true;
}

static bool trans_LDA(DisasContext *s, arg_LDA *a)
{
    return op_lda(s, a, MO_UL);
}

static bool trans_LDAB(DisasContext *s, arg_LDA *a)
{
    return op_lda(s, a, MO_UB);
}

static bool trans_LDAH(DisasContext *s, arg_LDA *a)
{
    return op_lda(s, a, MO_UW);
}

/*
 * Media instructions
 */

static bool trans_USADA8(DisasContext *s, arg_USADA8 *a)
{
    TCGv_i32 t1, t2;

    if (!ENABLE_ARCH_6) {
        return false;
    }

    t1 = load_reg(s, a->rn);
    t2 = load_reg(s, a->rm);
    gen_helper_usad8(t1, t1, t2);
    tcg_temp_free_i32(t2);
    if (a->ra != 15) {
        t2 = load_reg(s, a->ra);
        tcg_gen_add_i32(t1, t1, t2);
        tcg_temp_free_i32(t2);
    }
    store_reg(s, a->rd, t1);
    return true;
}

static bool op_bfx(DisasContext *s, arg_UBFX *a, bool u)
{
    TCGv_i32 tmp;
    int width = a->widthm1 + 1;
    int shift = a->lsb;

    if (!ENABLE_ARCH_6T2) {
        return false;
    }
    if (shift + width > 32) {
        /* UNPREDICTABLE; we choose to UNDEF */
        unallocated_encoding(s);
        return true;
    }

    tmp = load_reg(s, a->rn);
    if (u) {
        tcg_gen_extract_i32(tmp, tmp, shift, width);
    } else {
        tcg_gen_sextract_i32(tmp, tmp, shift, width);
    }
    store_reg(s, a->rd, tmp);
    return true;
}

static bool trans_SBFX(DisasContext *s, arg_SBFX *a)
{
    return op_bfx(s, a, false);
}

static bool trans_UBFX(DisasContext *s, arg_UBFX *a)
{
    return op_bfx(s, a, true);
}

static bool trans_BFCI(DisasContext *s, arg_BFCI *a)
{
    TCGv_i32 tmp;
    int msb = a->msb, lsb = a->lsb;
    int width;

    if (!ENABLE_ARCH_6T2) {
        return false;
    }
    if (msb < lsb) {
        /* UNPREDICTABLE; we choose to UNDEF */
        unallocated_encoding(s);
        return true;
    }

    width = msb + 1 - lsb;
    if (a->rn == 15) {
        /* BFC */
        tmp = tcg_const_i32(0);
    } else {
        /* BFI */
        tmp = load_reg(s, a->rn);
    }
    if (width != 32) {
        TCGv_i32 tmp2 = load_reg(s, a->rd);
        tcg_gen_deposit_i32(tmp, tmp2, tmp, lsb, width);
        tcg_temp_free_i32(tmp2);
    }
    store_reg(s, a->rd, tmp);
    return true;
}

static bool trans_UDF(DisasContext *s, arg_UDF *a)
{
    unallocated_encoding(s);
    return true;
}

/*
 * Parallel addition and subtraction
 */

static bool op_par_addsub(DisasContext *s, arg_rrr *a,
                          void (*gen)(TCGv_i32, TCGv_i32, TCGv_i32))
{
    TCGv_i32 t0, t1;

    if (s->thumb
        ? !arm_dc_feature(s, ARM_FEATURE_THUMB_DSP)
        : !ENABLE_ARCH_6) {
        return false;
    }

    t0 = load_reg(s, a->rn);
    t1 = load_reg(s, a->rm);

    gen(t0, t0, t1);

    tcg_temp_free_i32(t1);
    store_reg(s, a->rd, t0);
    return true;
}

static bool op_par_addsub_ge(DisasContext *s, arg_rrr *a,
                             void (*gen)(TCGv_i32, TCGv_i32,
                                         TCGv_i32, TCGv_ptr))
{
    TCGv_i32 t0, t1;
    TCGv_ptr ge;

    if (s->thumb
        ? !arm_dc_feature(s, ARM_FEATURE_THUMB_DSP)
        : !ENABLE_ARCH_6) {
        return false;
    }

    t0 = load_reg(s, a->rn);
    t1 = load_reg(s, a->rm);

    ge = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(ge, cpu_env, offsetof(CPUARMState, GE));
    gen(t0, t0, t1, ge);

    tcg_temp_free_ptr(ge);
    tcg_temp_free_i32(t1);
    store_reg(s, a->rd, t0);
    return true;
}

#define DO_PAR_ADDSUB(NAME, helper) \
static bool trans_##NAME(DisasContext *s, arg_rrr *a)   \
{                                                       \
    return op_par_addsub(s, a, helper);                 \
}

#define DO_PAR_ADDSUB_GE(NAME, helper) \
static bool trans_##NAME(DisasContext *s, arg_rrr *a)   \
{                                                       \
    return op_par_addsub_ge(s, a, helper);              \
}

DO_PAR_ADDSUB_GE(SADD16, gen_helper_sadd16)
DO_PAR_ADDSUB_GE(SASX, gen_helper_saddsubx)
DO_PAR_ADDSUB_GE(SSAX, gen_helper_ssubaddx)
DO_PAR_ADDSUB_GE(SSUB16, gen_helper_ssub16)
DO_PAR_ADDSUB_GE(SADD8, gen_helper_sadd8)
DO_PAR_ADDSUB_GE(SSUB8, gen_helper_ssub8)

DO_PAR_ADDSUB_GE(UADD16, gen_helper_uadd16)
DO_PAR_ADDSUB_GE(UASX, gen_helper_uaddsubx)
DO_PAR_ADDSUB_GE(USAX, gen_helper_usubaddx)
DO_PAR_ADDSUB_GE(USUB16, gen_helper_usub16)
DO_PAR_ADDSUB_GE(UADD8, gen_helper_uadd8)
DO_PAR_ADDSUB_GE(USUB8, gen_helper_usub8)

DO_PAR_ADDSUB(QADD16, gen_helper_qadd16)
DO_PAR_ADDSUB(QASX, gen_helper_qaddsubx)
DO_PAR_ADDSUB(QSAX, gen_helper_qsubaddx)
DO_PAR_ADDSUB(QSUB16, gen_helper_qsub16)
DO_PAR_ADDSUB(QADD8, gen_helper_qadd8)
DO_PAR_ADDSUB(QSUB8, gen_helper_qsub8)

DO_PAR_ADDSUB(UQADD16, gen_helper_uqadd16)
DO_PAR_ADDSUB(UQASX, gen_helper_uqaddsubx)
DO_PAR_ADDSUB(UQSAX, gen_helper_uqsubaddx)
DO_PAR_ADDSUB(UQSUB16, gen_helper_uqsub16)
DO_PAR_ADDSUB(UQADD8, gen_helper_uqadd8)
DO_PAR_ADDSUB(UQSUB8, gen_helper_uqsub8)

DO_PAR_ADDSUB(SHADD16, gen_helper_shadd16)
DO_PAR_ADDSUB(SHASX, gen_helper_shaddsubx)
DO_PAR_ADDSUB(SHSAX, gen_helper_shsubaddx)
DO_PAR_ADDSUB(SHSUB16, gen_helper_shsub16)
DO_PAR_ADDSUB(SHADD8, gen_helper_shadd8)
DO_PAR_ADDSUB(SHSUB8, gen_helper_shsub8)

DO_PAR_ADDSUB(UHADD16, gen_helper_uhadd16)
DO_PAR_ADDSUB(UHASX, gen_helper_uhaddsubx)
DO_PAR_ADDSUB(UHSAX, gen_helper_uhsubaddx)
DO_PAR_ADDSUB(UHSUB16, gen_helper_uhsub16)
DO_PAR_ADDSUB(UHADD8, gen_helper_uhadd8)
DO_PAR_ADDSUB(UHSUB8, gen_helper_uhsub8)

#undef DO_PAR_ADDSUB
#undef DO_PAR_ADDSUB_GE

/*
 * Packing, unpacking, saturation, and reversal
 */

static bool trans_PKH(DisasContext *s, arg_PKH *a)
{
    TCGv_i32 tn, tm;
    int shift = a->imm;

    if (s->thumb
        ? !arm_dc_feature(s, ARM_FEATURE_THUMB_DSP)
        : !ENABLE_ARCH_6) {
        return false;
    }

    tn = load_reg(s, a->rn);
    tm = load_reg(s, a->rm);
    if (a->tb) {
        /* PKHTB */
        if (shift == 0) {
            shift = 31;
        }
        tcg_gen_sari_i32(tm, tm, shift);
        tcg_gen_deposit_i32(tn, tn, tm, 0, 16);
    } else {
        /* PKHBT */
        tcg_gen_shli_i32(tm, tm, shift);
        tcg_gen_deposit_i32(tn, tm, tn, 0, 16);
    }
    tcg_temp_free_i32(tm);
    store_reg(s, a->rd, tn);
    return true;
}

static bool op_sat(DisasContext *s, arg_sat *a,
                   void (*gen)(TCGv_i32, TCGv_env, TCGv_i32, TCGv_i32))
{
    TCGv_i32 tmp, satimm;
    int shift = a->imm;

    if (!ENABLE_ARCH_6) {
        return false;
    }

    tmp = load_reg(s, a->rn);
    if (a->sh) {
        tcg_gen_sari_i32(tmp, tmp, shift ? shift : 31);
    } else {
        tcg_gen_shli_i32(tmp, tmp, shift);
    }

    satimm = tcg_const_i32(a->satimm);
    gen(tmp, cpu_env, tmp, satimm);
    tcg_temp_free_i32(satimm);

    store_reg(s, a->rd, tmp);
    return true;
}

static bool trans_SSAT(DisasContext *s, arg_sat *a)
{
    return op_sat(s, a, gen_helper_ssat);
}

static bool trans_USAT(DisasContext *s, arg_sat *a)
{
    return op_sat(s, a, gen_helper_usat);
}

static bool trans_SSAT16(DisasContext *s, arg_sat *a)
{
    if (s->thumb && !arm_dc_feature(s, ARM_FEATURE_THUMB_DSP)) {
        return false;
    }
    return op_sat(s, a, gen_helper_ssat16);
}

static bool trans_USAT16(DisasContext *s, arg_sat *a)
{
    if (s->thumb && !arm_dc_feature(s, ARM_FEATURE_THUMB_DSP)) {
        return false;
    }
    return op_sat(s, a, gen_helper_usat16);
}

static bool op_xta(DisasContext *s, arg_rrr_rot *a,
                   void (*gen_extract)(TCGv_i32, TCGv_i32),
                   void (*gen_add)(TCGv_i32, TCGv_i32, TCGv_i32))
{
    TCGv_i32 tmp;

    if (!ENABLE_ARCH_6) {
        return false;
    }

    tmp = load_reg(s, a->rm);
    /*
     * TODO: In many cases we could do a shift instead of a rotate.
     * Combined with a simple extend, that becomes an extract.
     */
    tcg_gen_rotri_i32(tmp, tmp, a->rot * 8);
    gen_extract(tmp, tmp);

    if (a->rn != 15) {
        TCGv_i32 tmp2 = load_reg(s, a->rn);
        gen_add(tmp, tmp, tmp2);
        tcg_temp_free_i32(tmp2);
    }
    store_reg(s, a->rd, tmp);
    return true;
}

static bool trans_SXTAB(DisasContext *s, arg_rrr_rot *a)
{
    return op_xta(s, a, tcg_gen_ext8s_i32, tcg_gen_add_i32);
}

static bool trans_SXTAH(DisasContext *s, arg_rrr_rot *a)
{
    return op_xta(s, a, tcg_gen_ext16s_i32, tcg_gen_add_i32);
}

static bool trans_SXTAB16(DisasContext *s, arg_rrr_rot *a)
{
    if (s->thumb && !arm_dc_feature(s, ARM_FEATURE_THUMB_DSP)) {
        return false;
    }
    return op_xta(s, a, gen_helper_sxtb16, gen_add16);
}

static bool trans_UXTAB(DisasContext *s, arg_rrr_rot *a)
{
    return op_xta(s, a, tcg_gen_ext8u_i32, tcg_gen_add_i32);
}

static bool trans_UXTAH(DisasContext *s, arg_rrr_rot *a)
{
    return op_xta(s, a, tcg_gen_ext16u_i32, tcg_gen_add_i32);
}

static bool trans_UXTAB16(DisasContext *s, arg_rrr_rot *a)
{
    if (s->thumb && !arm_dc_feature(s, ARM_FEATURE_THUMB_DSP)) {
        return false;
    }
    return op_xta(s, a, gen_helper_uxtb16, gen_add16);
}

static bool trans_SEL(DisasContext *s, arg_rrr *a)
{
    TCGv_i32 t1, t2, t3;

    if (s->thumb
        ? !arm_dc_feature(s, ARM_FEATURE_THUMB_DSP)
        : !ENABLE_ARCH_6) {
        return false;
    }

    t1 = load_reg(s, a->rn);
    t2 = load_reg(s, a->rm);
    t3 = tcg_temp_new_i32();
    tcg_gen_ld_i32(t3, cpu_env, offsetof(CPUARMState, GE));
    gen_helper_sel_flags(t1, t3, t1, t2);
    tcg_temp_free_i32(t3);
    tcg_temp_free_i32(t2);
    store_reg(s, a->rd, t1);
    return true;
}

static bool op_rr(DisasContext *s, arg_rr *a,
                  void (*gen)(TCGv_i32, TCGv_i32))
{
    TCGv_i32 tmp;

    tmp = load_reg(s, a->rm);
    gen(tmp, tmp);
    store_reg(s, a->rd, tmp);
    return true;
}

static bool trans_REV(DisasContext *s, arg_rr *a)
{
    if (!ENABLE_ARCH_6) {
        return false;
    }
    return op_rr(s, a, tcg_gen_bswap32_i32);
}

static bool trans_REV16(DisasContext *s, arg_rr *a)
{
    if (!ENABLE_ARCH_6) {
        return false;
    }
    return op_rr(s, a, gen_rev16);
}

static bool trans_REVSH(DisasContext *s, arg_rr *a)
{
    if (!ENABLE_ARCH_6) {
        return false;
    }
    return op_rr(s, a, gen_revsh);
}

static bool trans_RBIT(DisasContext *s, arg_rr *a)
{
    if (!ENABLE_ARCH_6T2) {
        return false;
    }
    return op_rr(s, a, gen_helper_rbit);
}

/*
 * Signed multiply, signed and unsigned divide
 */

static bool op_smlad(DisasContext *s, arg_rrrr *a, bool m_swap, bool sub)
{
    TCGv_i32 t1, t2;

    if (!ENABLE_ARCH_6) {
        return false;
    }

    t1 = load_reg(s, a->rn);
    t2 = load_reg(s, a->rm);
    if (m_swap) {
        gen_swap_half(t2);
    }
    gen_smul_dual(t1, t2);

    if (sub) {
        /* This subtraction cannot overflow. */
        tcg_gen_sub_i32(t1, t1, t2);
    } else {
        /*
         * This addition cannot overflow 32 bits; however it may
         * overflow considered as a signed operation, in which case
         * we must set the Q flag.
         */
        gen_helper_add_setq(t1, cpu_env, t1, t2);
    }
    tcg_temp_free_i32(t2);

    if (a->ra != 15) {
        t2 = load_reg(s, a->ra);
        gen_helper_add_setq(t1, cpu_env, t1, t2);
        tcg_temp_free_i32(t2);
    }
    store_reg(s, a->rd, t1);
    return true;
}

static bool trans_SMLAD(DisasContext *s, arg_rrrr *a)
{
    return op_smlad(s, a, false, false);
}

static bool trans_SMLADX(DisasContext *s, arg_rrrr *a)
{
    return op_smlad(s, a, true, false);
}

static bool trans_SMLSD(DisasContext *s, arg_rrrr *a)
{
    return op_smlad(s, a, false, true);
}

static bool trans_SMLSDX(DisasContext *s, arg_rrrr *a)
{
    return op_smlad(s, a, true, true);
}

static bool op_smlald(DisasContext *s, arg_rrrr *a, bool m_swap, bool sub)
{
    TCGv_i32 t1, t2;
    TCGv_i64 l1, l2;

    if (!ENABLE_ARCH_6) {
        return false;
    }

    t1 = load_reg(s, a->rn);
    t2 = load_reg(s, a->rm);
    if (m_swap) {
        gen_swap_half(t2);
    }
    gen_smul_dual(t1, t2);

    l1 = tcg_temp_new_i64();
    l2 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(l1, t1);
    tcg_gen_ext_i32_i64(l2, t2);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);

    if (sub) {
        tcg_gen_sub_i64(l1, l1, l2);
    } else {
        tcg_gen_add_i64(l1, l1, l2);
    }
    tcg_temp_free_i64(l2);

    gen_addq(s, l1, a->ra, a->rd);
    gen_storeq_reg(s, a->ra, a->rd, l1);
    tcg_temp_free_i64(l1);
    return true;
}

static bool trans_SMLALD(DisasContext *s, arg_rrrr *a)
{
    return op_smlald(s, a, false, false);
}

static bool trans_SMLALDX(DisasContext *s, arg_rrrr *a)
{
    return op_smlald(s, a, true, false);
}

static bool trans_SMLSLD(DisasContext *s, arg_rrrr *a)
{
    return op_smlald(s, a, false, true);
}

static bool trans_SMLSLDX(DisasContext *s, arg_rrrr *a)
{
    return op_smlald(s, a, true, true);
}

static bool op_smmla(DisasContext *s, arg_rrrr *a, bool round, bool sub)
{
    TCGv_i32 t1, t2;

    if (s->thumb
        ? !arm_dc_feature(s, ARM_FEATURE_THUMB_DSP)
        : !ENABLE_ARCH_6) {
        return false;
    }

    t1 = load_reg(s, a->rn);
    t2 = load_reg(s, a->rm);
    tcg_gen_muls2_i32(t2, t1, t1, t2);

    if (a->ra != 15) {
        TCGv_i32 t3 = load_reg(s, a->ra);
        if (sub) {
            /*
             * For SMMLS, we need a 64-bit subtract.  Borrow caused by
             * a non-zero multiplicand lowpart, and the correct result
             * lowpart for rounding.
             */
            TCGv_i32 zero = tcg_const_i32(0);
            tcg_gen_sub2_i32(t2, t1, zero, t3, t2, t1);
            tcg_temp_free_i32(zero);
        } else {
            tcg_gen_add_i32(t1, t1, t3);
        }
        tcg_temp_free_i32(t3);
    }
    if (round) {
        /*
         * Adding 0x80000000 to the 64-bit quantity means that we have
         * carry in to the high word when the low word has the msb set.
         */
        tcg_gen_shri_i32(t2, t2, 31);
        tcg_gen_add_i32(t1, t1, t2);
    }
    tcg_temp_free_i32(t2);
    store_reg(s, a->rd, t1);
    return true;
}

static bool trans_SMMLA(DisasContext *s, arg_rrrr *a)
{
    return op_smmla(s, a, false, false);
}

static bool trans_SMMLAR(DisasContext *s, arg_rrrr *a)
{
    return op_smmla(s, a, true, false);
}

static bool trans_SMMLS(DisasContext *s, arg_rrrr *a)
{
    return op_smmla(s, a, false, true);
}

static bool trans_SMMLSR(DisasContext *s, arg_rrrr *a)
{
    return op_smmla(s, a, true, true);
}

static bool op_div(DisasContext *s, arg_rrr *a, bool u)
{
    TCGv_i32 t1, t2;

    if (s->thumb
        ? !dc_isar_feature(aa32_thumb_div, s)
        : !dc_isar_feature(aa32_arm_div, s)) {
        return false;
    }

    t1 = load_reg(s, a->rn);
    t2 = load_reg(s, a->rm);
    if (u) {
        gen_helper_udiv(t1, t1, t2);
    } else {
        gen_helper_sdiv(t1, t1, t2);
    }
    tcg_temp_free_i32(t2);
    store_reg(s, a->rd, t1);
    return true;
}

static bool trans_SDIV(DisasContext *s, arg_rrr *a)
{
    return op_div(s, a, false);
}

static bool trans_UDIV(DisasContext *s, arg_rrr *a)
{
    return op_div(s, a, true);
}

/*
 * Block data transfer
 */

static TCGv_i32 op_addr_block_pre(DisasContext *s, arg_ldst_block *a, int n)
{
    TCGv_i32 addr = load_reg(s, a->rn);

    if (a->b) {
        if (a->i) {
            /* pre increment */
            tcg_gen_addi_i32(addr, addr, 4);
        } else {
            /* pre decrement */
            tcg_gen_addi_i32(addr, addr, -(n * 4));
        }
    } else if (!a->i && n != 1) {
        /* post decrement */
        tcg_gen_addi_i32(addr, addr, -((n - 1) * 4));
    }

    if (s->v8m_stackcheck && a->rn == 13 && a->w) {
        /*
         * If the writeback is incrementing SP rather than
         * decrementing it, and the initial SP is below the
         * stack limit but the final written-back SP would
         * be above, then then we must not perform any memory
         * accesses, but it is IMPDEF whether we generate
         * an exception. We choose to do so in this case.
         * At this point 'addr' is the lowest address, so
         * either the original SP (if incrementing) or our
         * final SP (if decrementing), so that's what we check.
         */
        gen_helper_v8m_stackcheck(cpu_env, addr);
    }

    return addr;
}

static void op_addr_block_post(DisasContext *s, arg_ldst_block *a,
                               TCGv_i32 addr, int n)
{
    if (a->w) {
        /* write back */
        if (!a->b) {
            if (a->i) {
                /* post increment */
                tcg_gen_addi_i32(addr, addr, 4);
            } else {
                /* post decrement */
                tcg_gen_addi_i32(addr, addr, -(n * 4));
            }
        } else if (!a->i && n != 1) {
            /* pre decrement */
            tcg_gen_addi_i32(addr, addr, -((n - 1) * 4));
        }
        store_reg(s, a->rn, addr);
    } else {
        tcg_temp_free_i32(addr);
    }
}

static bool op_stm(DisasContext *s, arg_ldst_block *a, int min_n)
{
    int i, j, n, list, mem_idx;
    bool user = a->u;
    TCGv_i32 addr, tmp, tmp2;

    if (user) {
        /* STM (user) */
        if (IS_USER(s)) {
            /* Only usable in supervisor mode.  */
            unallocated_encoding(s);
            return true;
        }
    }

    list = a->list;
    n = ctpop16(list);
    if (n < min_n || a->rn == 15) {
        unallocated_encoding(s);
        return true;
    }

    addr = op_addr_block_pre(s, a, n);
    mem_idx = get_mem_index(s);

    for (i = j = 0; i < 16; i++) {
        if (!(list & (1 << i))) {
            continue;
        }

        if (user && i != 15) {
            tmp = tcg_temp_new_i32();
            tmp2 = tcg_const_i32(i);
            gen_helper_get_user_reg(tmp, cpu_env, tmp2);
            tcg_temp_free_i32(tmp2);
        } else {
            tmp = load_reg(s, i);
        }
        gen_aa32_st32(s, tmp, addr, mem_idx);
        tcg_temp_free_i32(tmp);

        /* No need to add after the last transfer.  */
        if (++j != n) {
            tcg_gen_addi_i32(addr, addr, 4);
        }
    }

    op_addr_block_post(s, a, addr, n);
    return true;
}

static bool trans_STM(DisasContext *s, arg_ldst_block *a)
{
    /* BitCount(list) < 1 is UNPREDICTABLE */
    return op_stm(s, a, 1);
}

static bool trans_STM_t32(DisasContext *s, arg_ldst_block *a)
{
    /* Writeback register in register list is UNPREDICTABLE for T32.  */
    if (a->w && (a->list & (1 << a->rn))) {
        unallocated_encoding(s);
        return true;
    }
    /* BitCount(list) < 2 is UNPREDICTABLE */
    return op_stm(s, a, 2);
}

static bool do_ldm(DisasContext *s, arg_ldst_block *a, int min_n)
{
    int i, j, n, list, mem_idx;
    bool loaded_base;
    bool user = a->u;
    bool exc_return = false;
    TCGv_i32 addr, tmp, tmp2, loaded_var;

    if (user) {
        /* LDM (user), LDM (exception return) */
        if (IS_USER(s)) {
            /* Only usable in supervisor mode.  */
            unallocated_encoding(s);
            return true;
        }
        if (extract32(a->list, 15, 1)) {
            exc_return = true;
            user = false;
        } else {
            /* LDM (user) does not allow writeback.  */
            if (a->w) {
                unallocated_encoding(s);
                return true;
            }
        }
    }

    list = a->list;
    n = ctpop16(list);
    if (n < min_n || a->rn == 15) {
        unallocated_encoding(s);
        return true;
    }

    addr = op_addr_block_pre(s, a, n);
    mem_idx = get_mem_index(s);
    loaded_base = false;
    loaded_var = NULL;

    for (i = j = 0; i < 16; i++) {
        if (!(list & (1 << i))) {
            continue;
        }

        tmp = tcg_temp_new_i32();
        gen_aa32_ld32u(s, tmp, addr, mem_idx);
        if (user) {
            tmp2 = tcg_const_i32(i);
            gen_helper_set_user_reg(cpu_env, tmp2, tmp);
            tcg_temp_free_i32(tmp2);
            tcg_temp_free_i32(tmp);
        } else if (i == a->rn) {
            loaded_var = tmp;
            loaded_base = true;
        } else if (i == 15 && exc_return) {
            store_pc_exc_ret(s, tmp);
        } else {
            store_reg_from_load(s, i, tmp);
        }

        /* No need to add after the last transfer.  */
        if (++j != n) {
            tcg_gen_addi_i32(addr, addr, 4);
        }
    }

    op_addr_block_post(s, a, addr, n);

    if (loaded_base) {
        /* Note that we reject base == pc above.  */
        store_reg(s, a->rn, loaded_var);
    }

    if (exc_return) {
        /* Restore CPSR from SPSR.  */
        tmp = load_cpu_field(spsr);
        if (tb_cflags(s->base.tb) & CF_USE_ICOUNT) {
            gen_io_start();
        }
        gen_helper_cpsr_write_eret(cpu_env, tmp);
        if (tb_cflags(s->base.tb) & CF_USE_ICOUNT) {
            gen_io_end();
        }
        tcg_temp_free_i32(tmp);
        /* Must exit loop to check un-masked IRQs */
        s->base.is_jmp = DISAS_EXIT;
    }
    return true;
}

static bool trans_LDM_a32(DisasContext *s, arg_ldst_block *a)
{
    /*
     * Writeback register in register list is UNPREDICTABLE
     * for ArchVersion() >= 7.  Prior to v7, A32 would write
     * an UNKNOWN value to the base register.
     */
    if (ENABLE_ARCH_7 && a->w && (a->list & (1 << a->rn))) {
        unallocated_encoding(s);
        return true;
    }
    /* BitCount(list) < 1 is UNPREDICTABLE */
    return do_ldm(s, a, 1);
}

static bool trans_LDM_t32(DisasContext *s, arg_ldst_block *a)
{
    /* Writeback register in register list is UNPREDICTABLE for T32. */
    if (a->w && (a->list & (1 << a->rn))) {
        unallocated_encoding(s);
        return true;
    }
    /* BitCount(list) < 2 is UNPREDICTABLE */
    return do_ldm(s, a, 2);
}

static bool trans_LDM_t16(DisasContext *s, arg_ldst_block *a)
{
    /* Writeback is conditional on the base register not being loaded.  */
    a->w = !(a->list & (1 << a->rn));
    /* BitCount(list) < 1 is UNPREDICTABLE */
    return do_ldm(s, a, 1);
}

/*
 * Branch, branch with link
 */

static bool trans_B(DisasContext *s, arg_i *a)
{
    gen_jmp(s, read_pc(s) + a->imm);
    return true;
}

static bool trans_B_cond_thumb(DisasContext *s, arg_ci *a)
{
    /* This has cond from encoding, required to be outside IT block.  */
    if (a->cond >= 0xe) {
        return false;
    }
    if (s->condexec_mask) {
        unallocated_encoding(s);
        return true;
    }
    arm_skip_unless(s, a->cond);
    gen_jmp(s, read_pc(s) + a->imm);
    return true;
}

static bool trans_BL(DisasContext *s, arg_i *a)
{
    tcg_gen_movi_i32(cpu_R[14], s->base.pc_next | s->thumb);
    gen_jmp(s, read_pc(s) + a->imm);
    return true;
}

static bool trans_BLX_i(DisasContext *s, arg_BLX_i *a)
{
    TCGv_i32 tmp;

    /* For A32, ARCH(5) is checked near the start of the uncond block. */
    if (s->thumb && (a->imm & 2)) {
        return false;
    }
    tcg_gen_movi_i32(cpu_R[14], s->base.pc_next | s->thumb);
    tmp = tcg_const_i32(!s->thumb);
    store_cpu_field(tmp, thumb);
    gen_jmp(s, (read_pc(s) & ~3) + a->imm);
    return true;
}

static bool trans_BL_BLX_prefix(DisasContext *s, arg_BL_BLX_prefix *a)
{
    assert(!arm_dc_feature(s, ARM_FEATURE_THUMB2));
    tcg_gen_movi_i32(cpu_R[14], read_pc(s) + (a->imm << 12));
    return true;
}

static bool trans_BL_suffix(DisasContext *s, arg_BL_suffix *a)
{
    TCGv_i32 tmp = tcg_temp_new_i32();

    assert(!arm_dc_feature(s, ARM_FEATURE_THUMB2));
    tcg_gen_addi_i32(tmp, cpu_R[14], (a->imm << 1) | 1);
    tcg_gen_movi_i32(cpu_R[14], s->base.pc_next | 1);
    gen_bx(s, tmp);
    return true;
}

static bool trans_BLX_suffix(DisasContext *s, arg_BLX_suffix *a)
{
    TCGv_i32 tmp;

    assert(!arm_dc_feature(s, ARM_FEATURE_THUMB2));
    if (!ENABLE_ARCH_5) {
        return false;
    }
    tmp = tcg_temp_new_i32();
    tcg_gen_addi_i32(tmp, cpu_R[14], a->imm << 1);
    tcg_gen_andi_i32(tmp, tmp, 0xfffffffc);
    tcg_gen_movi_i32(cpu_R[14], s->base.pc_next | 1);
    gen_bx(s, tmp);
    return true;
}

static bool op_tbranch(DisasContext *s, arg_tbranch *a, bool half)
{
    TCGv_i32 addr, tmp;

    tmp = load_reg(s, a->rm);
    if (half) {
        tcg_gen_add_i32(tmp, tmp, tmp);
    }
    addr = load_reg(s, a->rn);
    tcg_gen_add_i32(addr, addr, tmp);

    gen_aa32_ld_i32(s, tmp, addr, get_mem_index(s),
                    half ? MO_UW | s->be_data : MO_UB);
    tcg_temp_free_i32(addr);

    tcg_gen_add_i32(tmp, tmp, tmp);
    tcg_gen_addi_i32(tmp, tmp, read_pc(s));
    store_reg(s, 15, tmp);
    return true;
}

static bool trans_TBB(DisasContext *s, arg_tbranch *a)
{
    return op_tbranch(s, a, false);
}

static bool trans_TBH(DisasContext *s, arg_tbranch *a)
{
    return op_tbranch(s, a, true);
}

static bool trans_CBZ(DisasContext *s, arg_CBZ *a)
{
    TCGv_i32 tmp = load_reg(s, a->rn);

    arm_gen_condlabel(s);
    tcg_gen_brcondi_i32(a->nz ? TCG_COND_EQ : TCG_COND_NE,
                        tmp, 0, s->condlabel);
    tcg_temp_free_i32(tmp);
    gen_jmp(s, read_pc(s) + a->imm);
    return true;
}

/*
 * Supervisor call - both T32 & A32 come here so we need to check
 * which mode we are in when checking for semihosting.
 */

static bool trans_SVC(DisasContext *s, arg_SVC *a)
{
    const uint32_t semihost_imm = s->thumb ? 0xab : 0x123456;

    if (!arm_dc_feature(s, ARM_FEATURE_M) && semihosting_enabled() &&
#ifndef CONFIG_USER_ONLY
        !IS_USER(s) &&
#endif
        (a->imm == semihost_imm)) {
        gen_exception_internal_insn(s, s->pc_curr, EXCP_SEMIHOST);
    } else {
        gen_set_pc_im(s, s->base.pc_next);
        s->svc_imm = a->imm;
        s->base.is_jmp = DISAS_SWI;
    }
    return true;
}

/*
 * Unconditional system instructions
 */

static bool trans_RFE(DisasContext *s, arg_RFE *a)
{
    static const int8_t pre_offset[4] = {
        /* DA */ -4, /* IA */ 0, /* DB */ -8, /* IB */ 4
    };
    static const int8_t post_offset[4] = {
        /* DA */ -8, /* IA */ 4, /* DB */ -4, /* IB */ 0
    };
    TCGv_i32 addr, t1, t2;

    if (!ENABLE_ARCH_6 || arm_dc_feature(s, ARM_FEATURE_M)) {
        return false;
    }
    if (IS_USER(s)) {
        unallocated_encoding(s);
        return true;
    }

    addr = load_reg(s, a->rn);
    tcg_gen_addi_i32(addr, addr, pre_offset[a->pu]);

    /* Load PC into tmp and CPSR into tmp2.  */
    t1 = tcg_temp_new_i32();
    gen_aa32_ld32u(s, t1, addr, get_mem_index(s));
    tcg_gen_addi_i32(addr, addr, 4);
    t2 = tcg_temp_new_i32();
    gen_aa32_ld32u(s, t2, addr, get_mem_index(s));

    if (a->w) {
        /* Base writeback.  */
        tcg_gen_addi_i32(addr, addr, post_offset[a->pu]);
        store_reg(s, a->rn, addr);
    } else {
        tcg_temp_free_i32(addr);
    }
    gen_rfe(s, t1, t2);
    return true;
}

static bool trans_SRS(DisasContext *s, arg_SRS *a)
{
    if (!ENABLE_ARCH_6 || arm_dc_feature(s, ARM_FEATURE_M)) {
        return false;
    }
    gen_srs(s, a->mode, a->pu, a->w);
    return true;
}

static bool trans_CPS(DisasContext *s, arg_CPS *a)
{
    uint32_t mask, val;

    if (!ENABLE_ARCH_6 || arm_dc_feature(s, ARM_FEATURE_M)) {
        return false;
    }
    if (IS_USER(s)) {
        /* Implemented as NOP in user mode.  */
        return true;
    }
    /* TODO: There are quite a lot of UNPREDICTABLE argument combinations. */

    mask = val = 0;
    if (a->imod & 2) {
        if (a->A) {
            mask |= CPSR_A;
        }
        if (a->I) {
            mask |= CPSR_I;
        }
        if (a->F) {
            mask |= CPSR_F;
        }
        if (a->imod & 1) {
            val |= mask;
        }
    }
    if (a->M) {
        mask |= CPSR_M;
        val |= a->mode;
    }
    if (mask) {
        gen_set_psr_im(s, mask, 0, val);
    }
    return true;
}

static bool trans_CPS_v7m(DisasContext *s, arg_CPS_v7m *a)
{
    TCGv_i32 tmp, addr, el;

    if (!arm_dc_feature(s, ARM_FEATURE_M)) {
        return false;
    }
    if (IS_USER(s)) {
        /* Implemented as NOP in user mode.  */
        return true;
    }

    tmp = tcg_const_i32(a->im);
    /* FAULTMASK */
    if (a->F) {
        addr = tcg_const_i32(19);
        gen_helper_v7m_msr(cpu_env, addr, tmp);
        tcg_temp_free_i32(addr);
    }
    /* PRIMASK */
    if (a->I) {
        addr = tcg_const_i32(16);
        gen_helper_v7m_msr(cpu_env, addr, tmp);
        tcg_temp_free_i32(addr);
    }
    el = tcg_const_i32(s->current_el);
    gen_helper_rebuild_hflags_m32(cpu_env, el);
    tcg_temp_free_i32(el);
    tcg_temp_free_i32(tmp);
    gen_lookup_tb(s);
    return true;
}

/*
 * Clear-Exclusive, Barriers
 */

static bool trans_CLREX(DisasContext *s, arg_CLREX *a)
{
    if (s->thumb
        ? !ENABLE_ARCH_7 && !arm_dc_feature(s, ARM_FEATURE_M)
        : !ENABLE_ARCH_6K) {
        return false;
    }
    gen_clrex(s);
    return true;
}

static bool trans_DSB(DisasContext *s, arg_DSB *a)
{
    if (!ENABLE_ARCH_7 && !arm_dc_feature(s, ARM_FEATURE_M)) {
        return false;
    }
    tcg_gen_mb(TCG_MO_ALL | TCG_BAR_SC);
    return true;
}

static bool trans_DMB(DisasContext *s, arg_DMB *a)
{
    return trans_DSB(s, NULL);
}

static bool trans_ISB(DisasContext *s, arg_ISB *a)
{
    if (!ENABLE_ARCH_7 && !arm_dc_feature(s, ARM_FEATURE_M)) {
        return false;
    }
    /*
     * We need to break the TB after this insn to execute
     * self-modifying code correctly and also to take
     * any pending interrupts immediately.
     */
    gen_goto_tb(s, 0, s->base.pc_next);
    return true;
}

static bool trans_SB(DisasContext *s, arg_SB *a)
{
    if (!dc_isar_feature(aa32_sb, s)) {
        return false;
    }
    /*
     * TODO: There is no speculation barrier opcode
     * for TCG; MB and end the TB instead.
     */
    tcg_gen_mb(TCG_MO_ALL | TCG_BAR_SC);
    gen_goto_tb(s, 0, s->base.pc_next);
    return true;
}

static bool trans_SETEND(DisasContext *s, arg_SETEND *a)
{
    if (!ENABLE_ARCH_6) {
        return false;
    }
    if (a->E != (s->be_data == MO_BE)) {
        gen_helper_setend(cpu_env);
        s->base.is_jmp = DISAS_UPDATE;
    }
    return true;
}

/*
 * Preload instructions
 * All are nops, contingent on the appropriate arch level.
 */

static bool trans_PLD(DisasContext *s, arg_PLD *a)
{
    return ENABLE_ARCH_5TE;
}

static bool trans_PLDW(DisasContext *s, arg_PLD *a)
{
    return arm_dc_feature(s, ARM_FEATURE_V7MP);
}

static bool trans_PLI(DisasContext *s, arg_PLD *a)
{
    return ENABLE_ARCH_7;
}

/*
 * If-then
 */

static bool trans_IT(DisasContext *s, arg_IT *a)
{
    int cond_mask = a->cond_mask;

    /*
     * No actual code generated for this insn, just setup state.
     *
     * Combinations of firstcond and mask which set up an 0b1111
     * condition are UNPREDICTABLE; we take the CONSTRAINED
     * UNPREDICTABLE choice to treat 0b1111 the same as 0b1110,
     * i.e. both meaning "execute always".
     */
    s->condexec_cond = (cond_mask >> 4) & 0xe;
    s->condexec_mask = cond_mask & 0x1f;
    return true;
}

/*
 * Legacy decoder.
 */

static void disas_arm_insn(DisasContext *s, unsigned int insn)
{
    unsigned int cond = insn >> 28;

    /* M variants do not implement ARM mode; this must raise the INVSTATE
     * UsageFault exception.
     */
    if (arm_dc_feature(s, ARM_FEATURE_M)) {
        gen_exception_insn(s, s->pc_curr, EXCP_INVSTATE, syn_uncategorized(),
                           default_exception_el(s));
        return;
    }

    if (cond == 0xf) {
        /* In ARMv3 and v4 the NV condition is UNPREDICTABLE; we
         * choose to UNDEF. In ARMv5 and above the space is used
         * for miscellaneous unconditional instructions.
         */
        ARCH(5);

        /* Unconditional instructions.  */
        /* TODO: Perhaps merge these into one decodetree output file.  */
        if (disas_a32_uncond(s, insn) ||
            disas_vfp_uncond(s, insn)) {
            return;
        }
        /* fall back to legacy decoder */

        if (((insn >> 25) & 7) == 1) {
            /* NEON Data processing.  */
            if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
                goto illegal_op;
            }

            if (disas_neon_data_insn(s, insn)) {
                goto illegal_op;
            }
            return;
        }
        if ((insn & 0x0f100000) == 0x04000000) {
            /* NEON load/store.  */
            if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
                goto illegal_op;
            }

            if (disas_neon_ls_insn(s, insn)) {
                goto illegal_op;
            }
            return;
        }
        if ((insn & 0x0e000f00) == 0x0c000100) {
            if (arm_dc_feature(s, ARM_FEATURE_IWMMXT)) {
                /* iWMMXt register transfer.  */
                if (extract32(s->c15_cpar, 1, 1)) {
                    if (!disas_iwmmxt_insn(s, insn)) {
                        return;
                    }
                }
            }
        } else if ((insn & 0x0e000a00) == 0x0c000800
                   && arm_dc_feature(s, ARM_FEATURE_V8)) {
            if (disas_neon_insn_3same_ext(s, insn)) {
                goto illegal_op;
            }
            return;
        } else if ((insn & 0x0f000a00) == 0x0e000800
                   && arm_dc_feature(s, ARM_FEATURE_V8)) {
            if (disas_neon_insn_2reg_scalar_ext(s, insn)) {
                goto illegal_op;
            }
            return;
        }
        goto illegal_op;
    }
    if (cond != 0xe) {
        /* if not always execute, we generate a conditional jump to
           next instruction */
        arm_skip_unless(s, cond);
    }

    /* TODO: Perhaps merge these into one decodetree output file.  */
    if (disas_a32(s, insn) ||
        disas_vfp(s, insn)) {
        return;
    }
    /* fall back to legacy decoder */

    switch ((insn >> 24) & 0xf) {
    case 0xc:
    case 0xd:
    case 0xe:
        if (((insn >> 8) & 0xe) == 10) {
            /* VFP, but failed disas_vfp.  */
            goto illegal_op;
        }
        if (disas_coproc_insn(s, insn)) {
            /* Coprocessor.  */
            goto illegal_op;
        }
        break;
    default:
    illegal_op:
        unallocated_encoding(s);
        break;
    }
}

static bool thumb_insn_is_16bit(DisasContext *s, uint32_t pc, uint32_t insn)
{
    /*
     * Return true if this is a 16 bit instruction. We must be precise
     * about this (matching the decode).
     */
    if ((insn >> 11) < 0x1d) {
        /* Definitely a 16-bit instruction */
        return true;
    }

    /* Top five bits 0b11101 / 0b11110 / 0b11111 : this is the
     * first half of a 32-bit Thumb insn. Thumb-1 cores might
     * end up actually treating this as two 16-bit insns, though,
     * if it's half of a bl/blx pair that might span a page boundary.
     */
    if (arm_dc_feature(s, ARM_FEATURE_THUMB2) ||
        arm_dc_feature(s, ARM_FEATURE_M)) {
        /* Thumb2 cores (including all M profile ones) always treat
         * 32-bit insns as 32-bit.
         */
        return false;
    }

    if ((insn >> 11) == 0x1e && pc - s->page_start < TARGET_PAGE_SIZE - 3) {
        /* 0b1111_0xxx_xxxx_xxxx : BL/BLX prefix, and the suffix
         * is not on the next page; we merge this into a 32-bit
         * insn.
         */
        return false;
    }
    /* 0b1110_1xxx_xxxx_xxxx : BLX suffix (or UNDEF);
     * 0b1111_1xxx_xxxx_xxxx : BL suffix;
     * 0b1111_0xxx_xxxx_xxxx : BL/BLX prefix on the end of a page
     *  -- handle as single 16 bit insn
     */
    return true;
}

/* Translate a 32-bit thumb instruction. */
static void disas_thumb2_insn(DisasContext *s, uint32_t insn)
{
    /*
     * ARMv6-M supports a limited subset of Thumb2 instructions.
     * Other Thumb1 architectures allow only 32-bit
     * combined BL/BLX prefix and suffix.
     */
    if (arm_dc_feature(s, ARM_FEATURE_M) &&
        !arm_dc_feature(s, ARM_FEATURE_V7)) {
        int i;
        bool found = false;
        static const uint32_t armv6m_insn[] = {0xf3808000 /* msr */,
                                               0xf3b08040 /* dsb */,
                                               0xf3b08050 /* dmb */,
                                               0xf3b08060 /* isb */,
                                               0xf3e08000 /* mrs */,
                                               0xf000d000 /* bl */};
        static const uint32_t armv6m_mask[] = {0xffe0d000,
                                               0xfff0d0f0,
                                               0xfff0d0f0,
                                               0xfff0d0f0,
                                               0xffe0d000,
                                               0xf800d000};

        for (i = 0; i < ARRAY_SIZE(armv6m_insn); i++) {
            if ((insn & armv6m_mask[i]) == armv6m_insn[i]) {
                found = true;
                break;
            }
        }
        if (!found) {
            goto illegal_op;
        }
    } else if ((insn & 0xf800e800) != 0xf000e800)  {
        ARCH(6T2);
    }

    /*
     * TODO: Perhaps merge these into one decodetree output file.
     * Note disas_vfp is written for a32 with cond field in the
     * top nibble.  The t32 encoding requires 0xe in the top nibble.
     */
    if (disas_t32(s, insn) ||
        disas_vfp_uncond(s, insn) ||
        ((insn >> 28) == 0xe && disas_vfp(s, insn))) {
        return;
    }
    /* fall back to legacy decoder */

    switch ((insn >> 25) & 0xf) {
    case 0: case 1: case 2: case 3:
        /* 16-bit instructions.  Should never happen.  */
        abort();
    case 6: case 7: case 14: case 15:
        /* Coprocessor.  */
        if (arm_dc_feature(s, ARM_FEATURE_M)) {
            /* 0b111x_11xx_xxxx_xxxx_xxxx_xxxx_xxxx_xxxx */
            if (extract32(insn, 24, 2) == 3) {
                goto illegal_op; /* op0 = 0b11 : unallocated */
            }

            if (((insn >> 8) & 0xe) == 10 &&
                dc_isar_feature(aa32_fpsp_v2, s)) {
                /* FP, and the CPU supports it */
                goto illegal_op;
            } else {
                /* All other insns: NOCP */
                gen_exception_insn(s, s->pc_curr, EXCP_NOCP,
                                   syn_uncategorized(),
                                   default_exception_el(s));
            }
            break;
        }
        if ((insn & 0xfe000a00) == 0xfc000800
            && arm_dc_feature(s, ARM_FEATURE_V8)) {
            /* The Thumb2 and ARM encodings are identical.  */
            if (disas_neon_insn_3same_ext(s, insn)) {
                goto illegal_op;
            }
        } else if ((insn & 0xff000a00) == 0xfe000800
                   && arm_dc_feature(s, ARM_FEATURE_V8)) {
            /* The Thumb2 and ARM encodings are identical.  */
            if (disas_neon_insn_2reg_scalar_ext(s, insn)) {
                goto illegal_op;
            }
        } else if (((insn >> 24) & 3) == 3) {
            /* Translate into the equivalent ARM encoding.  */
            insn = (insn & 0xe2ffffff) | ((insn & (1 << 28)) >> 4) | (1 << 28);
            if (disas_neon_data_insn(s, insn)) {
                goto illegal_op;
            }
        } else if (((insn >> 8) & 0xe) == 10) {
            /* VFP, but failed disas_vfp.  */
            goto illegal_op;
        } else {
            if (insn & (1 << 28))
                goto illegal_op;
            if (disas_coproc_insn(s, insn)) {
                goto illegal_op;
            }
        }
        break;
    case 12:
        if ((insn & 0x01100000) == 0x01000000) {
            if (disas_neon_ls_insn(s, insn)) {
                goto illegal_op;
            }
            break;
        }
        goto illegal_op;
    default:
    illegal_op:
        unallocated_encoding(s);
    }
}

static void disas_thumb_insn(DisasContext *s, uint32_t insn)
{
    if (!disas_t16(s, insn)) {
        unallocated_encoding(s);
    }
}

static bool insn_crosses_page(CPUARMState *env, DisasContext *s)
{
    /* Return true if the insn at dc->base.pc_next might cross a page boundary.
     * (False positives are OK, false negatives are not.)
     * We know this is a Thumb insn, and our caller ensures we are
     * only called if dc->base.pc_next is less than 4 bytes from the page
     * boundary, so we cross the page if the first 16 bits indicate
     * that this is a 32 bit insn.
     */
    uint16_t insn = arm_lduw_code(env, s->base.pc_next, s->sctlr_b);

    return !thumb_insn_is_16bit(s, s->base.pc_next, insn);
}

static void arm_tr_init_disas_context(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    CPUARMState *env = cs->env_ptr;
    ARMCPU *cpu = env_archcpu(env);
    uint32_t tb_flags = dc->base.tb->flags;
    uint32_t condexec, core_mmu_idx;

    dc->isar = &cpu->isar;
    dc->condjmp = 0;

    dc->aarch64 = 0;
    /* If we are coming from secure EL0 in a system with a 32-bit EL3, then
     * there is no secure EL1, so we route exceptions to EL3.
     */
    dc->secure_routed_to_el3 = arm_feature(env, ARM_FEATURE_EL3) &&
                               !arm_el_is_aa64(env, 3);
    dc->thumb = FIELD_EX32(tb_flags, TBFLAG_AM32, THUMB);
    dc->be_data = FIELD_EX32(tb_flags, TBFLAG_ANY, BE_DATA) ? MO_BE : MO_LE;
    condexec = FIELD_EX32(tb_flags, TBFLAG_AM32, CONDEXEC);
    dc->condexec_mask = (condexec & 0xf) << 1;
    dc->condexec_cond = condexec >> 4;

    core_mmu_idx = FIELD_EX32(tb_flags, TBFLAG_ANY, MMUIDX);
    dc->mmu_idx = core_to_arm_mmu_idx(env, core_mmu_idx);
    dc->current_el = arm_mmu_idx_to_el(dc->mmu_idx);
#if !defined(CONFIG_USER_ONLY)
    dc->user = (dc->current_el == 0);
#endif
    dc->fp_excp_el = FIELD_EX32(tb_flags, TBFLAG_ANY, FPEXC_EL);

    if (arm_feature(env, ARM_FEATURE_M)) {
        dc->vfp_enabled = 1;
        dc->be_data = MO_TE;
        dc->v7m_handler_mode = FIELD_EX32(tb_flags, TBFLAG_M32, HANDLER);
        dc->v8m_secure = arm_feature(env, ARM_FEATURE_M_SECURITY) &&
            regime_is_secure(env, dc->mmu_idx);
        dc->v8m_stackcheck = FIELD_EX32(tb_flags, TBFLAG_M32, STACKCHECK);
        dc->v8m_fpccr_s_wrong =
            FIELD_EX32(tb_flags, TBFLAG_M32, FPCCR_S_WRONG);
        dc->v7m_new_fp_ctxt_needed =
            FIELD_EX32(tb_flags, TBFLAG_M32, NEW_FP_CTXT_NEEDED);
        dc->v7m_lspact = FIELD_EX32(tb_flags, TBFLAG_M32, LSPACT);
    } else {
        dc->be_data =
            FIELD_EX32(tb_flags, TBFLAG_ANY, BE_DATA) ? MO_BE : MO_LE;
        dc->debug_target_el =
            FIELD_EX32(tb_flags, TBFLAG_ANY, DEBUG_TARGET_EL);
        dc->sctlr_b = FIELD_EX32(tb_flags, TBFLAG_A32, SCTLR_B);
        dc->hstr_active = FIELD_EX32(tb_flags, TBFLAG_A32, HSTR_ACTIVE);
        dc->ns = FIELD_EX32(tb_flags, TBFLAG_A32, NS);
        dc->vfp_enabled = FIELD_EX32(tb_flags, TBFLAG_A32, VFPEN);
        if (arm_feature(env, ARM_FEATURE_XSCALE)) {
            dc->c15_cpar = FIELD_EX32(tb_flags, TBFLAG_A32, XSCALE_CPAR);
        } else {
            dc->vec_len = FIELD_EX32(tb_flags, TBFLAG_A32, VECLEN);
            dc->vec_stride = FIELD_EX32(tb_flags, TBFLAG_A32, VECSTRIDE);
        }
    }
    dc->cp_regs = cpu->cp_regs;
    dc->features = env->features;

    /* Single step state. The code-generation logic here is:
     *  SS_ACTIVE == 0:
     *   generate code with no special handling for single-stepping (except
     *   that anything that can make us go to SS_ACTIVE == 1 must end the TB;
     *   this happens anyway because those changes are all system register or
     *   PSTATE writes).
     *  SS_ACTIVE == 1, PSTATE.SS == 1: (active-not-pending)
     *   emit code for one insn
     *   emit code to clear PSTATE.SS
     *   emit code to generate software step exception for completed step
     *   end TB (as usual for having generated an exception)
     *  SS_ACTIVE == 1, PSTATE.SS == 0: (active-pending)
     *   emit code to generate a software step exception
     *   end the TB
     */
    dc->ss_active = FIELD_EX32(tb_flags, TBFLAG_ANY, SS_ACTIVE);
    dc->pstate_ss = FIELD_EX32(tb_flags, TBFLAG_ANY, PSTATE_SS);
    dc->is_ldex = false;

    dc->page_start = dc->base.pc_first & TARGET_PAGE_MASK;

    /* If architectural single step active, limit to 1.  */
    if (is_singlestepping(dc)) {
        dc->base.max_insns = 1;
    }

    /* ARM is a fixed-length ISA.  Bound the number of insns to execute
       to those left on the page.  */
    if (!dc->thumb) {
        int bound = -(dc->base.pc_first | TARGET_PAGE_MASK) / 4;
        dc->base.max_insns = MIN(dc->base.max_insns, bound);
    }

    cpu_V0 = tcg_temp_new_i64();
    cpu_V1 = tcg_temp_new_i64();
    /* FIXME: cpu_M0 can probably be the same as cpu_V0.  */
    cpu_M0 = tcg_temp_new_i64();
}

static void arm_tr_tb_start(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    /* A note on handling of the condexec (IT) bits:
     *
     * We want to avoid the overhead of having to write the updated condexec
     * bits back to the CPUARMState for every instruction in an IT block. So:
     * (1) if the condexec bits are not already zero then we write
     * zero back into the CPUARMState now. This avoids complications trying
     * to do it at the end of the block. (For example if we don't do this
     * it's hard to identify whether we can safely skip writing condexec
     * at the end of the TB, which we definitely want to do for the case
     * where a TB doesn't do anything with the IT state at all.)
     * (2) if we are going to leave the TB then we call gen_set_condexec()
     * which will write the correct value into CPUARMState if zero is wrong.
     * This is done both for leaving the TB at the end, and for leaving
     * it because of an exception we know will happen, which is done in
     * gen_exception_insn(). The latter is necessary because we need to
     * leave the TB with the PC/IT state just prior to execution of the
     * instruction which caused the exception.
     * (3) if we leave the TB unexpectedly (eg a data abort on a load)
     * then the CPUARMState will be wrong and we need to reset it.
     * This is handled in the same way as restoration of the
     * PC in these situations; we save the value of the condexec bits
     * for each PC via tcg_gen_insn_start(), and restore_state_to_opc()
     * then uses this to restore them after an exception.
     *
     * Note that there are no instructions which can read the condexec
     * bits, and none which can write non-static values to them, so
     * we don't need to care about whether CPUARMState is correct in the
     * middle of a TB.
     */

    /* Reset the conditional execution bits immediately. This avoids
       complications trying to do it at the end of the block.  */
    if (dc->condexec_mask || dc->condexec_cond) {
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_movi_i32(tmp, 0);
        store_cpu_field(tmp, condexec_bits);
    }
}

static void arm_tr_insn_start(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    tcg_gen_insn_start(dc->base.pc_next,
                       (dc->condexec_cond << 4) | (dc->condexec_mask >> 1),
                       0);
    dc->insn_start = tcg_last_op();
}

static bool arm_tr_breakpoint_check(DisasContextBase *dcbase, CPUState *cpu,
                                    const CPUBreakpoint *bp)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    if (bp->flags & BP_CPU) {
        gen_set_condexec(dc);
        gen_set_pc_im(dc, dc->base.pc_next);
        gen_helper_check_breakpoints(cpu_env);
        /* End the TB early; it's likely not going to be executed */
        dc->base.is_jmp = DISAS_TOO_MANY;
    } else {
        gen_exception_internal_insn(dc, dc->base.pc_next, EXCP_DEBUG);
        /* The address covered by the breakpoint must be
           included in [tb->pc, tb->pc + tb->size) in order
           to for it to be properly cleared -- thus we
           increment the PC here so that the logic setting
           tb->size below does the right thing.  */
        /* TODO: Advance PC by correct instruction length to
         * avoid disassembler error messages */
        dc->base.pc_next += 2;
        dc->base.is_jmp = DISAS_NORETURN;
    }

    return true;
}

static bool arm_pre_translate_insn(DisasContext *dc)
{
#ifdef CONFIG_USER_ONLY
    /* Intercept jump to the magic kernel page.  */
    if (dc->base.pc_next >= 0xffff0000) {
        /* We always get here via a jump, so know we are not in a
           conditional execution block.  */
        gen_exception_internal(EXCP_KERNEL_TRAP);
        dc->base.is_jmp = DISAS_NORETURN;
        return true;
    }
#endif

    if (dc->ss_active && !dc->pstate_ss) {
        /* Singlestep state is Active-pending.
         * If we're in this state at the start of a TB then either
         *  a) we just took an exception to an EL which is being debugged
         *     and this is the first insn in the exception handler
         *  b) debug exceptions were masked and we just unmasked them
         *     without changing EL (eg by clearing PSTATE.D)
         * In either case we're going to take a swstep exception in the
         * "did not step an insn" case, and so the syndrome ISV and EX
         * bits should be zero.
         */
        assert(dc->base.num_insns == 1);
        gen_swstep_exception(dc, 0, 0);
        dc->base.is_jmp = DISAS_NORETURN;
        return true;
    }

    return false;
}

static void arm_post_translate_insn(DisasContext *dc)
{
    if (dc->condjmp && !dc->base.is_jmp) {
        gen_set_label(dc->condlabel);
        dc->condjmp = 0;
    }
    translator_loop_temp_check(&dc->base);
}

static void arm_tr_translate_insn(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    CPUARMState *env = cpu->env_ptr;
    unsigned int insn;

    if (arm_pre_translate_insn(dc)) {
        return;
    }

    dc->pc_curr = dc->base.pc_next;
    insn = arm_ldl_code(env, dc->base.pc_next, dc->sctlr_b);
    dc->insn = insn;
    dc->base.pc_next += 4;
    disas_arm_insn(dc, insn);

    arm_post_translate_insn(dc);

    /* ARM is a fixed-length ISA.  We performed the cross-page check
       in init_disas_context by adjusting max_insns.  */
}

static bool thumb_insn_is_unconditional(DisasContext *s, uint32_t insn)
{
    /* Return true if this Thumb insn is always unconditional,
     * even inside an IT block. This is true of only a very few
     * instructions: BKPT, HLT, and SG.
     *
     * A larger class of instructions are UNPREDICTABLE if used
     * inside an IT block; we do not need to detect those here, because
     * what we do by default (perform the cc check and update the IT
     * bits state machine) is a permitted CONSTRAINED UNPREDICTABLE
     * choice for those situations.
     *
     * insn is either a 16-bit or a 32-bit instruction; the two are
     * distinguishable because for the 16-bit case the top 16 bits
     * are zeroes, and that isn't a valid 32-bit encoding.
     */
    if ((insn & 0xffffff00) == 0xbe00) {
        /* BKPT */
        return true;
    }

    if ((insn & 0xffffffc0) == 0xba80 && arm_dc_feature(s, ARM_FEATURE_V8) &&
        !arm_dc_feature(s, ARM_FEATURE_M)) {
        /* HLT: v8A only. This is unconditional even when it is going to
         * UNDEF; see the v8A ARM ARM DDI0487B.a H3.3.
         * For v7 cores this was a plain old undefined encoding and so
         * honours its cc check. (We might be using the encoding as
         * a semihosting trap, but we don't change the cc check behaviour
         * on that account, because a debugger connected to a real v7A
         * core and emulating semihosting traps by catching the UNDEF
         * exception would also only see cases where the cc check passed.
         * No guest code should be trying to do a HLT semihosting trap
         * in an IT block anyway.
         */
        return true;
    }

    if (insn == 0xe97fe97f && arm_dc_feature(s, ARM_FEATURE_V8) &&
        arm_dc_feature(s, ARM_FEATURE_M)) {
        /* SG: v8M only */
        return true;
    }

    return false;
}

static void thumb_tr_translate_insn(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    CPUARMState *env = cpu->env_ptr;
    uint32_t insn;
    bool is_16bit;

    if (arm_pre_translate_insn(dc)) {
        return;
    }

    dc->pc_curr = dc->base.pc_next;
    insn = arm_lduw_code(env, dc->base.pc_next, dc->sctlr_b);
    is_16bit = thumb_insn_is_16bit(dc, dc->base.pc_next, insn);
    dc->base.pc_next += 2;
    if (!is_16bit) {
        uint32_t insn2 = arm_lduw_code(env, dc->base.pc_next, dc->sctlr_b);

        insn = insn << 16 | insn2;
        dc->base.pc_next += 2;
    }
    dc->insn = insn;

    if (dc->condexec_mask && !thumb_insn_is_unconditional(dc, insn)) {
        uint32_t cond = dc->condexec_cond;

        /*
         * Conditionally skip the insn. Note that both 0xe and 0xf mean
         * "always"; 0xf is not "never".
         */
        if (cond < 0x0e) {
            arm_skip_unless(dc, cond);
        }
    }

    if (is_16bit) {
        disas_thumb_insn(dc, insn);
    } else {
        disas_thumb2_insn(dc, insn);
    }

    /* Advance the Thumb condexec condition.  */
    if (dc->condexec_mask) {
        dc->condexec_cond = ((dc->condexec_cond & 0xe) |
                             ((dc->condexec_mask >> 4) & 1));
        dc->condexec_mask = (dc->condexec_mask << 1) & 0x1f;
        if (dc->condexec_mask == 0) {
            dc->condexec_cond = 0;
        }
    }

    arm_post_translate_insn(dc);

    /* Thumb is a variable-length ISA.  Stop translation when the next insn
     * will touch a new page.  This ensures that prefetch aborts occur at
     * the right place.
     *
     * We want to stop the TB if the next insn starts in a new page,
     * or if it spans between this page and the next. This means that
     * if we're looking at the last halfword in the page we need to
     * see if it's a 16-bit Thumb insn (which will fit in this TB)
     * or a 32-bit Thumb insn (which won't).
     * This is to avoid generating a silly TB with a single 16-bit insn
     * in it at the end of this page (which would execute correctly
     * but isn't very efficient).
     */
    if (dc->base.is_jmp == DISAS_NEXT
        && (dc->base.pc_next - dc->page_start >= TARGET_PAGE_SIZE
            || (dc->base.pc_next - dc->page_start >= TARGET_PAGE_SIZE - 3
                && insn_crosses_page(env, dc)))) {
        dc->base.is_jmp = DISAS_TOO_MANY;
    }
}

static void arm_tr_tb_stop(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    if (tb_cflags(dc->base.tb) & CF_LAST_IO && dc->condjmp) {
        /* FIXME: This can theoretically happen with self-modifying code. */
        cpu_abort(cpu, "IO on conditional branch instruction");
    }

    /* At this stage dc->condjmp will only be set when the skipped
       instruction was a conditional branch or trap, and the PC has
       already been written.  */
    gen_set_condexec(dc);
    if (dc->base.is_jmp == DISAS_BX_EXCRET) {
        /* Exception return branches need some special case code at the
         * end of the TB, which is complex enough that it has to
         * handle the single-step vs not and the condition-failed
         * insn codepath itself.
         */
        gen_bx_excret_final_code(dc);
    } else if (unlikely(is_singlestepping(dc))) {
        /* Unconditional and "condition passed" instruction codepath. */
        switch (dc->base.is_jmp) {
        case DISAS_SWI:
            gen_ss_advance(dc);
            gen_exception(EXCP_SWI, syn_aa32_svc(dc->svc_imm, dc->thumb),
                          default_exception_el(dc));
            break;
        case DISAS_HVC:
            gen_ss_advance(dc);
            gen_exception(EXCP_HVC, syn_aa32_hvc(dc->svc_imm), 2);
            break;
        case DISAS_SMC:
            gen_ss_advance(dc);
            gen_exception(EXCP_SMC, syn_aa32_smc(), 3);
            break;
        case DISAS_NEXT:
        case DISAS_TOO_MANY:
        case DISAS_UPDATE:
            gen_set_pc_im(dc, dc->base.pc_next);
            /* fall through */
        default:
            /* FIXME: Single stepping a WFI insn will not halt the CPU. */
            gen_singlestep_exception(dc);
            break;
        case DISAS_NORETURN:
            break;
        }
    } else {
        /* While branches must always occur at the end of an IT block,
           there are a few other things that can cause us to terminate
           the TB in the middle of an IT block:
            - Exception generating instructions (bkpt, swi, undefined).
            - Page boundaries.
            - Hardware watchpoints.
           Hardware breakpoints have already been handled and skip this code.
         */
        switch(dc->base.is_jmp) {
        case DISAS_NEXT:
        case DISAS_TOO_MANY:
            gen_goto_tb(dc, 1, dc->base.pc_next);
            break;
        case DISAS_JUMP:
            gen_goto_ptr();
            break;
        case DISAS_UPDATE:
            gen_set_pc_im(dc, dc->base.pc_next);
            /* fall through */
        default:
            /* indicate that the hash table must be used to find the next TB */
            tcg_gen_exit_tb(NULL, 0);
            break;
        case DISAS_NORETURN:
            /* nothing more to generate */
            break;
        case DISAS_WFI:
        {
            TCGv_i32 tmp = tcg_const_i32((dc->thumb &&
                                          !(dc->insn & (1U << 31))) ? 2 : 4);

            gen_helper_wfi(cpu_env, tmp);
            tcg_temp_free_i32(tmp);
            /* The helper doesn't necessarily throw an exception, but we
             * must go back to the main loop to check for interrupts anyway.
             */
            tcg_gen_exit_tb(NULL, 0);
            break;
        }
        case DISAS_WFE:
            gen_helper_wfe(cpu_env);
            break;
        case DISAS_YIELD:
            gen_helper_yield(cpu_env);
            break;
        case DISAS_SWI:
            gen_exception(EXCP_SWI, syn_aa32_svc(dc->svc_imm, dc->thumb),
                          default_exception_el(dc));
            break;
        case DISAS_HVC:
            gen_exception(EXCP_HVC, syn_aa32_hvc(dc->svc_imm), 2);
            break;
        case DISAS_SMC:
            gen_exception(EXCP_SMC, syn_aa32_smc(), 3);
            break;
        }
    }

    if (dc->condjmp) {
        /* "Condition failed" instruction codepath for the branch/trap insn */
        gen_set_label(dc->condlabel);
        gen_set_condexec(dc);
        if (unlikely(is_singlestepping(dc))) {
            gen_set_pc_im(dc, dc->base.pc_next);
            gen_singlestep_exception(dc);
        } else {
            gen_goto_tb(dc, 1, dc->base.pc_next);
        }
    }
}

static void arm_tr_disas_log(const DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    qemu_log("IN: %s\n", lookup_symbol(dc->base.pc_first));
    log_target_disas(cpu, dc->base.pc_first, dc->base.tb->size);
}

static const TranslatorOps arm_translator_ops = {
    .init_disas_context = arm_tr_init_disas_context,
    .tb_start           = arm_tr_tb_start,
    .insn_start         = arm_tr_insn_start,
    .breakpoint_check   = arm_tr_breakpoint_check,
    .translate_insn     = arm_tr_translate_insn,
    .tb_stop            = arm_tr_tb_stop,
    .disas_log          = arm_tr_disas_log,
};

static const TranslatorOps thumb_translator_ops = {
    .init_disas_context = arm_tr_init_disas_context,
    .tb_start           = arm_tr_tb_start,
    .insn_start         = arm_tr_insn_start,
    .breakpoint_check   = arm_tr_breakpoint_check,
    .translate_insn     = thumb_tr_translate_insn,
    .tb_stop            = arm_tr_tb_stop,
    .disas_log          = arm_tr_disas_log,
};

/* generate intermediate code for basic block 'tb'.  */
void gen_intermediate_code(CPUState *cpu, TranslationBlock *tb, int max_insns)
{
    DisasContext dc = { };
    const TranslatorOps *ops = &arm_translator_ops;

    if (FIELD_EX32(tb->flags, TBFLAG_AM32, THUMB)) {
        ops = &thumb_translator_ops;
    }
#ifdef TARGET_AARCH64
    if (FIELD_EX32(tb->flags, TBFLAG_ANY, AARCH64_STATE)) {
        ops = &aarch64_translator_ops;
    }
#endif

    translator_loop(ops, &dc.base, cpu, tb, max_insns);
}

void restore_state_to_opc(CPUARMState *env, TranslationBlock *tb,
                          target_ulong *data)
{
    if (is_a64(env)) {
        env->pc = data[0];
        env->condexec_bits = 0;
        env->exception.syndrome = data[2] << ARM_INSN_START_WORD2_SHIFT;
    } else {
        env->regs[15] = data[0];
        env->condexec_bits = data[1];
        env->exception.syndrome = data[2] << ARM_INSN_START_WORD2_SHIFT;
    }
}
