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

#include "cpu.h"
#include "internals.h"
#include "disas/disas.h"
#include "exec/exec-all.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-op-gvec.h"
#include "qemu/log.h"
#include "qemu/bitops.h"
#include "arm_ldst.h"
#include "semihosting/semihost.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "exec/log.h"
#include "cpregs.h"


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

#include "translate.h"
#include "translate-a32.h"

/* These are TCG temporaries used only by the legacy iwMMXt decoder */
static TCGv_i64 cpu_V0, cpu_V1, cpu_M0;
/* These are TCG globals which alias CPUARMState fields */
static TCGv_i32 cpu_R[16];
TCGv_i32 cpu_CF, cpu_NF, cpu_VF, cpu_ZF;
TCGv_i64 cpu_exclusive_addr;
TCGv_i64 cpu_exclusive_val;

#include "exec/gen-icount.h"

static const char * const regnames[] =
    { "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
      "r8", "r9", "r10", "r11", "r12", "r13", "r14", "pc" };


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

uint64_t asimd_imm_const(uint32_t imm, int cmode, int op)
{
    /* Expand the encoded constant as per AdvSIMDExpandImm pseudocode */
    switch (cmode) {
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
        if (op) {
            /*
             * This and cmode == 15 op == 1 are the only cases where
             * the top and bottom 32 bits of the encoded constant differ.
             */
            uint64_t imm64 = 0;
            int n;

            for (n = 0; n < 8; n++) {
                if (imm & (1 << n)) {
                    imm64 |= (0xffULL << (n * 8));
                }
            }
            return imm64;
        }
        imm |= (imm << 8) | (imm << 16) | (imm << 24);
        break;
    case 15:
        if (op) {
            /* Reserved encoding for AArch32; valid for AArch64 */
            uint64_t imm64 = (uint64_t)(imm & 0x3f) << 48;
            if (imm & 0x80) {
                imm64 |= 0x8000000000000000ULL;
            }
            if (imm & 0x40) {
                imm64 |= 0x3fc0000000000000ULL;
            } else {
                imm64 |= 0x4000000000000000ULL;
            }
            return imm64;
        }
        imm = ((imm & 0x80) << 24) | ((imm & 0x3f) << 19)
            | ((imm & 0x40) ? (0x1f << 25) : (1 << 30));
        break;
    }
    if (op) {
        imm = ~imm;
    }
    return dup_const(MO_32, imm);
}

/* Generate a label used for skipping this instruction */
void arm_gen_condlabel(DisasContext *s)
{
    if (!s->condjmp) {
        s->condlabel = gen_disas_label(s);
        s->condjmp = 1;
    }
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

/*
 * Store var into env + offset to a member with size bytes.
 * Free var after use.
 */
void store_cpu_offset(TCGv_i32 var, int offset, int size)
{
    switch (size) {
    case 1:
        tcg_gen_st8_i32(var, cpu_env, offset);
        break;
    case 4:
        tcg_gen_st_i32(var, cpu_env, offset);
        break;
    default:
        g_assert_not_reached();
    }
    tcg_temp_free_i32(var);
}

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
    case ARMMMUIdx_E3:
    case ARMMMUIdx_E2:        /* this one is UNPREDICTABLE */
    case ARMMMUIdx_E10_0:
    case ARMMMUIdx_E10_1:
    case ARMMMUIdx_E10_1_PAN:
        return arm_to_core_mmu_idx(ARMMMUIdx_E10_0);
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

/* The pc_curr difference for an architectural jump. */
static target_long jmp_diff(DisasContext *s, target_long diff)
{
    return diff + (s->thumb ? 4 : 8);
}

static void gen_pc_plus_diff(DisasContext *s, TCGv_i32 var, target_long diff)
{
    assert(s->pc_save != -1);
    if (tb_cflags(s->base.tb) & CF_PCREL) {
        tcg_gen_addi_i32(var, cpu_R[15], (s->pc_curr - s->pc_save) + diff);
    } else {
        tcg_gen_movi_i32(var, s->pc_curr + diff);
    }
}

/* Set a variable to the value of a CPU register.  */
void load_reg_var(DisasContext *s, TCGv_i32 var, int reg)
{
    if (reg == 15) {
        gen_pc_plus_diff(s, var, jmp_diff(s, 0));
    } else {
        tcg_gen_mov_i32(var, cpu_R[reg]);
    }
}

/*
 * Create a new temp, REG + OFS, except PC is ALIGN(PC, 4).
 * This is used for load/store for which use of PC implies (literal),
 * or ADD that implies ADR.
 */
TCGv_i32 add_reg_for_lit(DisasContext *s, int reg, int ofs)
{
    TCGv_i32 tmp = tcg_temp_new_i32();

    if (reg == 15) {
        /*
         * This address is computed from an aligned PC:
         * subtract off the low bits.
         */
        gen_pc_plus_diff(s, tmp, jmp_diff(s, ofs - (s->pc_curr & 3)));
    } else {
        tcg_gen_addi_i32(tmp, cpu_R[reg], ofs);
    }
    return tmp;
}

/* Set a CPU register.  The source must be a temporary and will be
   marked as dead.  */
void store_reg(DisasContext *s, int reg, TCGv_i32 var)
{
    if (reg == 15) {
        /* In Thumb mode, we must ignore bit 0.
         * In ARM mode, for ARMv4 and ARMv5, it is UNPREDICTABLE if bits [1:0]
         * are not 0b00, but for ARMv6 and above, we must ignore bits [1:0].
         * We choose to ignore [1:0] in ARM mode for all architecture versions.
         */
        tcg_gen_andi_i32(var, var, s->thumb ? ~1 : ~3);
        s->base.is_jmp = DISAS_JUMP;
        s->pc_save = -1;
    } else if (reg == 13 && arm_dc_feature(s, ARM_FEATURE_M)) {
        /* For M-profile SP bits [1:0] are always zero */
        tcg_gen_andi_i32(var, var, ~3);
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

void gen_set_cpsr(TCGv_i32 var, uint32_t mask)
{
    gen_helper_cpsr_write(cpu_env, var, tcg_constant_i32(mask));
}

static void gen_rebuild_hflags(DisasContext *s, bool new_el)
{
    bool m_profile = arm_dc_feature(s, ARM_FEATURE_M);

    if (new_el) {
        if (m_profile) {
            gen_helper_rebuild_hflags_m32_newel(cpu_env);
        } else {
            gen_helper_rebuild_hflags_a32_newel(cpu_env);
        }
    } else {
        TCGv_i32 tcg_el = tcg_constant_i32(s->current_el);
        if (m_profile) {
            gen_helper_rebuild_hflags_m32(cpu_env, tcg_el);
        } else {
            gen_helper_rebuild_hflags_a32(cpu_env, tcg_el);
        }
    }
}

static void gen_exception_internal(int excp)
{
    assert(excp_is_internal(excp));
    gen_helper_exception_internal(cpu_env, tcg_constant_i32(excp));
}

static void gen_singlestep_exception(DisasContext *s)
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

void clear_eci_state(DisasContext *s)
{
    /*
     * Clear any ECI/ICI state: used when a load multiple/store
     * multiple insn executes.
     */
    if (s->eci) {
        store_cpu_field_constant(0, condexec_bits);
        s->eci = 0;
    }
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
void gen_rev16(TCGv_i32 dest, TCGv_i32 var)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    TCGv_i32 mask = tcg_constant_i32(0x00ff00ff);
    tcg_gen_shri_i32(tmp, var, 8);
    tcg_gen_and_i32(tmp, tmp, mask);
    tcg_gen_and_i32(var, var, mask);
    tcg_gen_shli_i32(var, var, 8);
    tcg_gen_or_i32(dest, var, tmp);
    tcg_temp_free_i32(tmp);
}

/* Byteswap low halfword and sign extend.  */
static void gen_revsh(TCGv_i32 dest, TCGv_i32 var)
{
    tcg_gen_bswap16_i32(var, var, TCG_BSWAP_OS);
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
    TCGv_i32 tmpd = tcg_temp_new_i32();                               \
    TCGv_i32 tmp1 = tcg_temp_new_i32();                               \
    TCGv_i32 zero = tcg_constant_i32(0);                              \
    tcg_gen_andi_i32(tmp1, t1, 0x1f);                                 \
    tcg_gen_##name##_i32(tmpd, t0, tmp1);                             \
    tcg_gen_andi_i32(tmp1, t1, 0xe0);                                 \
    tcg_gen_movcond_i32(TCG_COND_NE, dest, tmp1, zero, zero, tmpd);   \
    tcg_temp_free_i32(tmpd);                                          \
    tcg_temp_free_i32(tmp1);                                          \
}
GEN_SHIFT(shl)
GEN_SHIFT(shr)
#undef GEN_SHIFT

static void gen_sar(TCGv_i32 dest, TCGv_i32 t0, TCGv_i32 t1)
{
    TCGv_i32 tmp1 = tcg_temp_new_i32();

    tcg_gen_andi_i32(tmp1, t1, 0xff);
    tcg_gen_umin_i32(tmp1, tmp1, tcg_constant_i32(31));
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
        tcg_gen_xor_i32(value, cpu_VF, cpu_NF);
        break;

    case 12: /* gt: !Z && N == V */
    case 13: /* le: Z || N != V */
        cond = TCG_COND_NE;
        value = tcg_temp_new_i32();
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
}

void gen_set_condexec(DisasContext *s)
{
    if (s->condexec_mask) {
        uint32_t val = (s->condexec_cond << 4) | (s->condexec_mask >> 1);

        store_cpu_field_constant(val, condexec_bits);
    }
}

void gen_update_pc(DisasContext *s, target_long diff)
{
    gen_pc_plus_diff(s, cpu_R[15], diff);
    s->pc_save = s->pc_curr + diff;
}

/* Set PC and Thumb state from var.  var is marked as dead.  */
static inline void gen_bx(DisasContext *s, TCGv_i32 var)
{
    s->base.is_jmp = DISAS_JUMP;
    tcg_gen_andi_i32(cpu_R[15], var, ~1);
    tcg_gen_andi_i32(var, var, 1);
    store_cpu_field(var, thumb);
    s->pc_save = -1;
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
    DisasLabel excret_label = gen_disas_label(s);
    uint32_t min_magic;

    if (arm_dc_feature(s, ARM_FEATURE_M_SECURITY)) {
        /* Covers FNC_RETURN and EXC_RETURN magic */
        min_magic = FNC_RETURN_MIN_MAGIC;
    } else {
        /* EXC_RETURN magic only */
        min_magic = EXC_RETURN_MIN_MAGIC;
    }

    /* Is the new PC value in the magic range indicating exception return? */
    tcg_gen_brcondi_i32(TCG_COND_GEU, cpu_R[15], min_magic, excret_label.label);
    /* No: end the TB as we would for a DISAS_JMP */
    if (s->ss_active) {
        gen_singlestep_exception(s);
    } else {
        tcg_gen_exit_tb(NULL, 0);
    }
    set_disas_label(s, excret_label);
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
     *  - we don't need to do gen_update_pc() because the bxns helper will
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
    gen_update_pc(s, curr_insn_len(s));
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

MemOp pow2_align(unsigned i)
{
    static const MemOp mop_align[] = {
        0, MO_ALIGN_2, MO_ALIGN_4, MO_ALIGN_8, MO_ALIGN_16,
        /*
         * FIXME: TARGET_PAGE_BITS_MIN affects TLB_FLAGS_MASK such
         * that 256-bit alignment (MO_ALIGN_32) cannot be supported:
         * see get_alignment_bits(). Enforce only 128-bit alignment for now.
         */
        MO_ALIGN_16
    };
    g_assert(i < ARRAY_SIZE(mop_align));
    return mop_align[i];
}

/*
 * Abstractions of "generate code to do a guest load/store for
 * AArch32", where a vaddr is always 32 bits (and is zero
 * extended if we're a 64 bit core) and  data is also
 * 32 bits unless specifically doing a 64 bit access.
 * These functions work like tcg_gen_qemu_{ld,st}* except
 * that the address argument is TCGv_i32 rather than TCGv.
 */

static TCGv gen_aa32_addr(DisasContext *s, TCGv_i32 a32, MemOp op)
{
    TCGv addr = tcg_temp_new();
    tcg_gen_extu_i32_tl(addr, a32);

    /* Not needed for user-mode BE32, where we use MO_BE instead.  */
    if (!IS_USER_ONLY && s->sctlr_b && (op & MO_SIZE) < MO_32) {
        tcg_gen_xori_tl(addr, addr, 4 - (1 << (op & MO_SIZE)));
    }
    return addr;
}

/*
 * Internal routines are used for NEON cases where the endianness
 * and/or alignment has already been taken into account and manipulated.
 */
void gen_aa32_ld_internal_i32(DisasContext *s, TCGv_i32 val,
                              TCGv_i32 a32, int index, MemOp opc)
{
    TCGv addr = gen_aa32_addr(s, a32, opc);
    tcg_gen_qemu_ld_i32(val, addr, index, opc);
    tcg_temp_free(addr);
}

void gen_aa32_st_internal_i32(DisasContext *s, TCGv_i32 val,
                              TCGv_i32 a32, int index, MemOp opc)
{
    TCGv addr = gen_aa32_addr(s, a32, opc);
    tcg_gen_qemu_st_i32(val, addr, index, opc);
    tcg_temp_free(addr);
}

void gen_aa32_ld_internal_i64(DisasContext *s, TCGv_i64 val,
                              TCGv_i32 a32, int index, MemOp opc)
{
    TCGv addr = gen_aa32_addr(s, a32, opc);

    tcg_gen_qemu_ld_i64(val, addr, index, opc);

    /* Not needed for user-mode BE32, where we use MO_BE instead.  */
    if (!IS_USER_ONLY && s->sctlr_b && (opc & MO_SIZE) == MO_64) {
        tcg_gen_rotri_i64(val, val, 32);
    }
    tcg_temp_free(addr);
}

void gen_aa32_st_internal_i64(DisasContext *s, TCGv_i64 val,
                              TCGv_i32 a32, int index, MemOp opc)
{
    TCGv addr = gen_aa32_addr(s, a32, opc);

    /* Not needed for user-mode BE32, where we use MO_BE instead.  */
    if (!IS_USER_ONLY && s->sctlr_b && (opc & MO_SIZE) == MO_64) {
        TCGv_i64 tmp = tcg_temp_new_i64();
        tcg_gen_rotri_i64(tmp, val, 32);
        tcg_gen_qemu_st_i64(tmp, addr, index, opc);
        tcg_temp_free_i64(tmp);
    } else {
        tcg_gen_qemu_st_i64(val, addr, index, opc);
    }
    tcg_temp_free(addr);
}

void gen_aa32_ld_i32(DisasContext *s, TCGv_i32 val, TCGv_i32 a32,
                     int index, MemOp opc)
{
    gen_aa32_ld_internal_i32(s, val, a32, index, finalize_memop(s, opc));
}

void gen_aa32_st_i32(DisasContext *s, TCGv_i32 val, TCGv_i32 a32,
                     int index, MemOp opc)
{
    gen_aa32_st_internal_i32(s, val, a32, index, finalize_memop(s, opc));
}

void gen_aa32_ld_i64(DisasContext *s, TCGv_i64 val, TCGv_i32 a32,
                     int index, MemOp opc)
{
    gen_aa32_ld_internal_i64(s, val, a32, index, finalize_memop(s, opc));
}

void gen_aa32_st_i64(DisasContext *s, TCGv_i64 val, TCGv_i32 a32,
                     int index, MemOp opc)
{
    gen_aa32_st_internal_i64(s, val, a32, index, finalize_memop(s, opc));
}

#define DO_GEN_LD(SUFF, OPC)                                            \
    static inline void gen_aa32_ld##SUFF(DisasContext *s, TCGv_i32 val, \
                                         TCGv_i32 a32, int index)       \
    {                                                                   \
        gen_aa32_ld_i32(s, val, a32, index, OPC);                       \
    }

#define DO_GEN_ST(SUFF, OPC)                                            \
    static inline void gen_aa32_st##SUFF(DisasContext *s, TCGv_i32 val, \
                                         TCGv_i32 a32, int index)       \
    {                                                                   \
        gen_aa32_st_i32(s, val, a32, index, OPC);                       \
    }

static inline void gen_hvc(DisasContext *s, int imm16)
{
    /* The pre HVC helper handles cases when HVC gets trapped
     * as an undefined insn by runtime configuration (ie before
     * the insn really executes).
     */
    gen_update_pc(s, 0);
    gen_helper_pre_hvc(cpu_env);
    /* Otherwise we will treat this as a real exception which
     * happens after execution of the insn. (The distinction matters
     * for the PC value reported to the exception handler and also
     * for single stepping.)
     */
    s->svc_imm = imm16;
    gen_update_pc(s, curr_insn_len(s));
    s->base.is_jmp = DISAS_HVC;
}

static inline void gen_smc(DisasContext *s)
{
    /* As with HVC, we may take an exception either before or after
     * the insn executes.
     */
    gen_update_pc(s, 0);
    gen_helper_pre_smc(cpu_env, tcg_constant_i32(syn_aa32_smc()));
    gen_update_pc(s, curr_insn_len(s));
    s->base.is_jmp = DISAS_SMC;
}

static void gen_exception_internal_insn(DisasContext *s, int excp)
{
    gen_set_condexec(s);
    gen_update_pc(s, 0);
    gen_exception_internal(excp);
    s->base.is_jmp = DISAS_NORETURN;
}

static void gen_exception_el_v(int excp, uint32_t syndrome, TCGv_i32 tcg_el)
{
    gen_helper_exception_with_syndrome_el(cpu_env, tcg_constant_i32(excp),
                                          tcg_constant_i32(syndrome), tcg_el);
}

static void gen_exception_el(int excp, uint32_t syndrome, uint32_t target_el)
{
    gen_exception_el_v(excp, syndrome, tcg_constant_i32(target_el));
}

static void gen_exception(int excp, uint32_t syndrome)
{
    gen_helper_exception_with_syndrome(cpu_env, tcg_constant_i32(excp),
                                       tcg_constant_i32(syndrome));
}

static void gen_exception_insn_el_v(DisasContext *s, target_long pc_diff,
                                    int excp, uint32_t syn, TCGv_i32 tcg_el)
{
    if (s->aarch64) {
        gen_a64_update_pc(s, pc_diff);
    } else {
        gen_set_condexec(s);
        gen_update_pc(s, pc_diff);
    }
    gen_exception_el_v(excp, syn, tcg_el);
    s->base.is_jmp = DISAS_NORETURN;
}

void gen_exception_insn_el(DisasContext *s, target_long pc_diff, int excp,
                           uint32_t syn, uint32_t target_el)
{
    gen_exception_insn_el_v(s, pc_diff, excp, syn,
                            tcg_constant_i32(target_el));
}

void gen_exception_insn(DisasContext *s, target_long pc_diff,
                        int excp, uint32_t syn)
{
    if (s->aarch64) {
        gen_a64_update_pc(s, pc_diff);
    } else {
        gen_set_condexec(s);
        gen_update_pc(s, pc_diff);
    }
    gen_exception(excp, syn);
    s->base.is_jmp = DISAS_NORETURN;
}

static void gen_exception_bkpt_insn(DisasContext *s, uint32_t syn)
{
    gen_set_condexec(s);
    gen_update_pc(s, 0);
    gen_helper_exception_bkpt_insn(cpu_env, tcg_constant_i32(syn));
    s->base.is_jmp = DISAS_NORETURN;
}

void unallocated_encoding(DisasContext *s)
{
    /* Unallocated and reserved encodings are uncategorized */
    gen_exception_insn(s, 0, EXCP_UDEF, syn_uncategorized());
}

/* Force a TB lookup after an instruction that changes the CPU state.  */
void gen_lookup_tb(DisasContext *s)
{
    gen_pc_plus_diff(s, cpu_R[15], curr_insn_len(s));
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
    if (semihosting_enabled(s->current_el == 0) &&
        (imm == (s->thumb ? 0x3c : 0xf000))) {
        gen_exception_internal_insn(s, EXCP_SEMIHOST);
        return;
    }

    unallocated_encoding(s);
}

/*
 * Return the offset of a "full" NEON Dreg.
 */
long neon_full_reg_offset(unsigned reg)
{
    return offsetof(CPUARMState, vfp.zregs[reg >> 1].d[reg & 1]);
}

/*
 * Return the offset of a 2**SIZE piece of a NEON register, at index ELE,
 * where 0 is the least significant end of the register.
 */
long neon_element_offset(int reg, int element, MemOp memop)
{
    int element_size = 1 << (memop & MO_SIZE);
    int ofs = element * element_size;
#if HOST_BIG_ENDIAN
    /*
     * Calculate the offset assuming fully little-endian,
     * then XOR to account for the order of the 8-byte units.
     */
    if (element_size < 8) {
        ofs ^= 8 - element_size;
    }
#endif
    return neon_full_reg_offset(reg) + ofs;
}

/* Return the offset of a VFP Dreg (dp = true) or VFP Sreg (dp = false). */
long vfp_reg_offset(bool dp, unsigned reg)
{
    if (dp) {
        return neon_element_offset(reg, 0, MO_64);
    } else {
        return neon_element_offset(reg >> 1, reg & 1, MO_32);
    }
}

void read_neon_element32(TCGv_i32 dest, int reg, int ele, MemOp memop)
{
    long off = neon_element_offset(reg, ele, memop);

    switch (memop) {
    case MO_SB:
        tcg_gen_ld8s_i32(dest, cpu_env, off);
        break;
    case MO_UB:
        tcg_gen_ld8u_i32(dest, cpu_env, off);
        break;
    case MO_SW:
        tcg_gen_ld16s_i32(dest, cpu_env, off);
        break;
    case MO_UW:
        tcg_gen_ld16u_i32(dest, cpu_env, off);
        break;
    case MO_UL:
    case MO_SL:
        tcg_gen_ld_i32(dest, cpu_env, off);
        break;
    default:
        g_assert_not_reached();
    }
}

void read_neon_element64(TCGv_i64 dest, int reg, int ele, MemOp memop)
{
    long off = neon_element_offset(reg, ele, memop);

    switch (memop) {
    case MO_SL:
        tcg_gen_ld32s_i64(dest, cpu_env, off);
        break;
    case MO_UL:
        tcg_gen_ld32u_i64(dest, cpu_env, off);
        break;
    case MO_UQ:
        tcg_gen_ld_i64(dest, cpu_env, off);
        break;
    default:
        g_assert_not_reached();
    }
}

void write_neon_element32(TCGv_i32 src, int reg, int ele, MemOp memop)
{
    long off = neon_element_offset(reg, ele, memop);

    switch (memop) {
    case MO_8:
        tcg_gen_st8_i32(src, cpu_env, off);
        break;
    case MO_16:
        tcg_gen_st16_i32(src, cpu_env, off);
        break;
    case MO_32:
        tcg_gen_st_i32(src, cpu_env, off);
        break;
    default:
        g_assert_not_reached();
    }
}

void write_neon_element64(TCGv_i64 src, int reg, int ele, MemOp memop)
{
    long off = neon_element_offset(reg, ele, memop);

    switch (memop) {
    case MO_32:
        tcg_gen_st32_i64(src, cpu_env, off);
        break;
    case MO_64:
        tcg_gen_st_i64(src, cpu_env, off);
        break;
    default:
        g_assert_not_reached();
    }
}

#define ARM_CP_RW_BIT   (1 << 20)

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
            tmp2 = tcg_constant_i32(0xff);
            tmp3 = tcg_constant_i32((insn & 7) << 3);
            break;
        case 1:
            tmp2 = tcg_constant_i32(0xffff);
            tmp3 = tcg_constant_i32((insn & 3) << 4);
            break;
        case 2:
            tmp2 = tcg_constant_i32(0xffffffff);
            tmp3 = tcg_constant_i32((insn & 1) << 5);
            break;
        default:
            g_assert_not_reached();
        }
        gen_helper_iwmmxt_insr(cpu_M0, cpu_M0, tmp, tmp2, tmp3);
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
        iwmmxt_load_reg(cpu_V1, rd1);
        gen_helper_iwmmxt_align(cpu_M0, cpu_M0, cpu_V1,
                                tcg_constant_i32((insn >> 20) & 3));
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
        tmp = tcg_constant_i32(((insn >> 16) & 0xf0) | (insn & 0x0f));
        gen_helper_iwmmxt_shufh(cpu_M0, cpu_env, cpu_M0, tmp);
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

static void gen_goto_ptr(void)
{
    tcg_gen_lookup_and_goto_ptr();
}

/* This will end the TB but doesn't guarantee we'll return to
 * cpu_loop_exec. Any live exit_requests will be processed as we
 * enter the next TB.
 */
static void gen_goto_tb(DisasContext *s, int n, target_long diff)
{
    if (translator_use_goto_tb(&s->base, s->pc_curr + diff)) {
        /*
         * For pcrel, the pc must always be up-to-date on entry to
         * the linked TB, so that it can use simple additions for all
         * further adjustments.  For !pcrel, the linked TB is compiled
         * to know its full virtual address, so we can delay the
         * update to pc to the unlinked path.  A long chain of links
         * can thus avoid many updates to the PC.
         */
        if (tb_cflags(s->base.tb) & CF_PCREL) {
            gen_update_pc(s, diff);
            tcg_gen_goto_tb(n);
        } else {
            tcg_gen_goto_tb(n);
            gen_update_pc(s, diff);
        }
        tcg_gen_exit_tb(s->base.tb, n);
    } else {
        gen_update_pc(s, diff);
        gen_goto_ptr();
    }
    s->base.is_jmp = DISAS_NORETURN;
}

/* Jump, specifying which TB number to use if we gen_goto_tb() */
static void gen_jmp_tb(DisasContext *s, target_long diff, int tbno)
{
    if (unlikely(s->ss_active)) {
        /* An indirect jump so that we still trigger the debug exception.  */
        gen_update_pc(s, diff);
        s->base.is_jmp = DISAS_JUMP;
        return;
    }
    switch (s->base.is_jmp) {
    case DISAS_NEXT:
    case DISAS_TOO_MANY:
    case DISAS_NORETURN:
        /*
         * The normal case: just go to the destination TB.
         * NB: NORETURN happens if we generate code like
         *    gen_brcondi(l);
         *    gen_jmp();
         *    gen_set_label(l);
         *    gen_jmp();
         * on the second call to gen_jmp().
         */
        gen_goto_tb(s, tbno, diff);
        break;
    case DISAS_UPDATE_NOCHAIN:
    case DISAS_UPDATE_EXIT:
        /*
         * We already decided we're leaving the TB for some other reason.
         * Avoid using goto_tb so we really do exit back to the main loop
         * and don't chain to another TB.
         */
        gen_update_pc(s, diff);
        gen_goto_ptr();
        s->base.is_jmp = DISAS_NORETURN;
        break;
    default:
        /*
         * We shouldn't be emitting code for a jump and also have
         * is_jmp set to one of the special cases like DISAS_SWI.
         */
        g_assert_not_reached();
    }
}

static inline void gen_jmp(DisasContext *s, target_long diff)
{
    gen_jmp_tb(s, diff, 0);
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
             * then accesses to Mon registers trap to Secure EL2, if it exists,
             * otherwise EL3.
             */
            TCGv_i32 tcg_el;

            if (arm_dc_feature(s, ARM_FEATURE_AARCH64) &&
                dc_isar_feature(aa64_sel2, s)) {
                /* Target EL is EL<3 minus SCR_EL3.EEL2> */
                tcg_el = load_cpu_field(cp15.scr_el3);
                tcg_gen_sextract_i32(tcg_el, tcg_el, ctz32(SCR_EEL2), 1);
                tcg_gen_addi_i32(tcg_el, tcg_el, 3);
            } else {
                tcg_el = tcg_constant_i32(3);
            }

            gen_exception_insn_el_v(s, 0, EXCP_UDEF,
                                    syn_uncategorized(), tcg_el);
            tcg_temp_free_i32(tcg_el);
            return false;
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
    gen_exception_insn(s, 0, EXCP_UDEF, syn_uncategorized());
    return false;
}

static void gen_msr_banked(DisasContext *s, int r, int sysm, int rn)
{
    TCGv_i32 tcg_reg;
    int tgtmode = 0, regno = 0;

    if (!msr_banked_access_decode(s, r, sysm, rn, &tgtmode, &regno)) {
        return;
    }

    /* Sync state because msr_banked() can raise exceptions */
    gen_set_condexec(s);
    gen_update_pc(s, 0);
    tcg_reg = load_reg(s, rn);
    gen_helper_msr_banked(cpu_env, tcg_reg,
                          tcg_constant_i32(tgtmode),
                          tcg_constant_i32(regno));
    tcg_temp_free_i32(tcg_reg);
    s->base.is_jmp = DISAS_UPDATE_EXIT;
}

static void gen_mrs_banked(DisasContext *s, int r, int sysm, int rn)
{
    TCGv_i32 tcg_reg;
    int tgtmode = 0, regno = 0;

    if (!msr_banked_access_decode(s, r, sysm, rn, &tgtmode, &regno)) {
        return;
    }

    /* Sync state because mrs_banked() can raise exceptions */
    gen_set_condexec(s);
    gen_update_pc(s, 0);
    tcg_reg = tcg_temp_new_i32();
    gen_helper_mrs_banked(tcg_reg, cpu_env,
                          tcg_constant_i32(tgtmode),
                          tcg_constant_i32(regno));
    store_reg(s, rn, tcg_reg);
    s->base.is_jmp = DISAS_UPDATE_EXIT;
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

static void gen_gvec_fn3_qc(uint32_t rd_ofs, uint32_t rn_ofs, uint32_t rm_ofs,
                            uint32_t opr_sz, uint32_t max_sz,
                            gen_helper_gvec_3_ptr *fn)
{
    TCGv_ptr qc_ptr = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(qc_ptr, cpu_env, offsetof(CPUARMState, vfp.qc));
    tcg_gen_gvec_3_ptr(rd_ofs, rn_ofs, rm_ofs, qc_ptr,
                       opr_sz, max_sz, 0, fn);
    tcg_temp_free_ptr(qc_ptr);
}

void gen_gvec_sqrdmlah_qc(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                          uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static gen_helper_gvec_3_ptr * const fns[2] = {
        gen_helper_gvec_qrdmlah_s16, gen_helper_gvec_qrdmlah_s32
    };
    tcg_debug_assert(vece >= 1 && vece <= 2);
    gen_gvec_fn3_qc(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, fns[vece - 1]);
}

void gen_gvec_sqrdmlsh_qc(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                          uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static gen_helper_gvec_3_ptr * const fns[2] = {
        gen_helper_gvec_qrdmlsh_s16, gen_helper_gvec_qrdmlsh_s32
    };
    tcg_debug_assert(vece >= 1 && vece <= 2);
    gen_gvec_fn3_qc(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, fns[vece - 1]);
}

#define GEN_CMP0(NAME, COND)                                            \
    static void gen_##NAME##0_i32(TCGv_i32 d, TCGv_i32 a)               \
    {                                                                   \
        tcg_gen_setcondi_i32(COND, d, a, 0);                            \
        tcg_gen_neg_i32(d, d);                                          \
    }                                                                   \
    static void gen_##NAME##0_i64(TCGv_i64 d, TCGv_i64 a)               \
    {                                                                   \
        tcg_gen_setcondi_i64(COND, d, a, 0);                            \
        tcg_gen_neg_i64(d, d);                                          \
    }                                                                   \
    static void gen_##NAME##0_vec(unsigned vece, TCGv_vec d, TCGv_vec a) \
    {                                                                   \
        TCGv_vec zero = tcg_constant_vec_matching(d, vece, 0);          \
        tcg_gen_cmp_vec(COND, vece, d, a, zero);                        \
    }                                                                   \
    void gen_gvec_##NAME##0(unsigned vece, uint32_t d, uint32_t m,      \
                            uint32_t opr_sz, uint32_t max_sz)           \
    {                                                                   \
        const GVecGen2 op[4] = {                                        \
            { .fno = gen_helper_gvec_##NAME##0_b,                       \
              .fniv = gen_##NAME##0_vec,                                \
              .opt_opc = vecop_list_cmp,                                \
              .vece = MO_8 },                                           \
            { .fno = gen_helper_gvec_##NAME##0_h,                       \
              .fniv = gen_##NAME##0_vec,                                \
              .opt_opc = vecop_list_cmp,                                \
              .vece = MO_16 },                                          \
            { .fni4 = gen_##NAME##0_i32,                                \
              .fniv = gen_##NAME##0_vec,                                \
              .opt_opc = vecop_list_cmp,                                \
              .vece = MO_32 },                                          \
            { .fni8 = gen_##NAME##0_i64,                                \
              .fniv = gen_##NAME##0_vec,                                \
              .opt_opc = vecop_list_cmp,                                \
              .prefer_i64 = TCG_TARGET_REG_BITS == 64,                  \
              .vece = MO_64 },                                          \
        };                                                              \
        tcg_gen_gvec_2(d, m, opr_sz, max_sz, &op[vece]);                \
    }

static const TCGOpcode vecop_list_cmp[] = {
    INDEX_op_cmp_vec, 0
};

GEN_CMP0(ceq, TCG_COND_EQ)
GEN_CMP0(cle, TCG_COND_LE)
GEN_CMP0(cge, TCG_COND_GE)
GEN_CMP0(clt, TCG_COND_LT)
GEN_CMP0(cgt, TCG_COND_GT)

#undef GEN_CMP0

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

void gen_gvec_ssra(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                   int64_t shift, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_sari_vec, INDEX_op_add_vec, 0
    };
    static const GVecGen2i ops[4] = {
        { .fni8 = gen_ssra8_i64,
          .fniv = gen_ssra_vec,
          .fno = gen_helper_gvec_ssra_b,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fni8 = gen_ssra16_i64,
          .fniv = gen_ssra_vec,
          .fno = gen_helper_gvec_ssra_h,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fni4 = gen_ssra32_i32,
          .fniv = gen_ssra_vec,
          .fno = gen_helper_gvec_ssra_s,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_32 },
        { .fni8 = gen_ssra64_i64,
          .fniv = gen_ssra_vec,
          .fno = gen_helper_gvec_ssra_b,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_64 },
    };

    /* tszimm encoding produces immediates in the range [1..esize]. */
    tcg_debug_assert(shift > 0);
    tcg_debug_assert(shift <= (8 << vece));

    /*
     * Shifts larger than the element size are architecturally valid.
     * Signed results in all sign bits.
     */
    shift = MIN(shift, (8 << vece) - 1);
    tcg_gen_gvec_2i(rd_ofs, rm_ofs, opr_sz, max_sz, shift, &ops[vece]);
}

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

void gen_gvec_usra(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                   int64_t shift, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_add_vec, 0
    };
    static const GVecGen2i ops[4] = {
        { .fni8 = gen_usra8_i64,
          .fniv = gen_usra_vec,
          .fno = gen_helper_gvec_usra_b,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_8, },
        { .fni8 = gen_usra16_i64,
          .fniv = gen_usra_vec,
          .fno = gen_helper_gvec_usra_h,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_16, },
        { .fni4 = gen_usra32_i32,
          .fniv = gen_usra_vec,
          .fno = gen_helper_gvec_usra_s,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_32, },
        { .fni8 = gen_usra64_i64,
          .fniv = gen_usra_vec,
          .fno = gen_helper_gvec_usra_d,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_64, },
    };

    /* tszimm encoding produces immediates in the range [1..esize]. */
    tcg_debug_assert(shift > 0);
    tcg_debug_assert(shift <= (8 << vece));

    /*
     * Shifts larger than the element size are architecturally valid.
     * Unsigned results in all zeros as input to accumulate: nop.
     */
    if (shift < (8 << vece)) {
        tcg_gen_gvec_2i(rd_ofs, rm_ofs, opr_sz, max_sz, shift, &ops[vece]);
    } else {
        /* Nop, but we do need to clear the tail. */
        tcg_gen_gvec_mov(vece, rd_ofs, rd_ofs, opr_sz, max_sz);
    }
}

/*
 * Shift one less than the requested amount, and the low bit is
 * the rounding bit.  For the 8 and 16-bit operations, because we
 * mask the low bit, we can perform a normal integer shift instead
 * of a vector shift.
 */
static void gen_srshr8_i64(TCGv_i64 d, TCGv_i64 a, int64_t sh)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_shri_i64(t, a, sh - 1);
    tcg_gen_andi_i64(t, t, dup_const(MO_8, 1));
    tcg_gen_vec_sar8i_i64(d, a, sh);
    tcg_gen_vec_add8_i64(d, d, t);
    tcg_temp_free_i64(t);
}

static void gen_srshr16_i64(TCGv_i64 d, TCGv_i64 a, int64_t sh)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_shri_i64(t, a, sh - 1);
    tcg_gen_andi_i64(t, t, dup_const(MO_16, 1));
    tcg_gen_vec_sar16i_i64(d, a, sh);
    tcg_gen_vec_add16_i64(d, d, t);
    tcg_temp_free_i64(t);
}

static void gen_srshr32_i32(TCGv_i32 d, TCGv_i32 a, int32_t sh)
{
    TCGv_i32 t;

    /* Handle shift by the input size for the benefit of trans_SRSHR_ri */
    if (sh == 32) {
        tcg_gen_movi_i32(d, 0);
        return;
    }
    t = tcg_temp_new_i32();
    tcg_gen_extract_i32(t, a, sh - 1, 1);
    tcg_gen_sari_i32(d, a, sh);
    tcg_gen_add_i32(d, d, t);
    tcg_temp_free_i32(t);
}

static void gen_srshr64_i64(TCGv_i64 d, TCGv_i64 a, int64_t sh)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_extract_i64(t, a, sh - 1, 1);
    tcg_gen_sari_i64(d, a, sh);
    tcg_gen_add_i64(d, d, t);
    tcg_temp_free_i64(t);
}

static void gen_srshr_vec(unsigned vece, TCGv_vec d, TCGv_vec a, int64_t sh)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);
    TCGv_vec ones = tcg_temp_new_vec_matching(d);

    tcg_gen_shri_vec(vece, t, a, sh - 1);
    tcg_gen_dupi_vec(vece, ones, 1);
    tcg_gen_and_vec(vece, t, t, ones);
    tcg_gen_sari_vec(vece, d, a, sh);
    tcg_gen_add_vec(vece, d, d, t);

    tcg_temp_free_vec(t);
    tcg_temp_free_vec(ones);
}

void gen_gvec_srshr(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                    int64_t shift, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_sari_vec, INDEX_op_add_vec, 0
    };
    static const GVecGen2i ops[4] = {
        { .fni8 = gen_srshr8_i64,
          .fniv = gen_srshr_vec,
          .fno = gen_helper_gvec_srshr_b,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fni8 = gen_srshr16_i64,
          .fniv = gen_srshr_vec,
          .fno = gen_helper_gvec_srshr_h,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fni4 = gen_srshr32_i32,
          .fniv = gen_srshr_vec,
          .fno = gen_helper_gvec_srshr_s,
          .opt_opc = vecop_list,
          .vece = MO_32 },
        { .fni8 = gen_srshr64_i64,
          .fniv = gen_srshr_vec,
          .fno = gen_helper_gvec_srshr_d,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .opt_opc = vecop_list,
          .vece = MO_64 },
    };

    /* tszimm encoding produces immediates in the range [1..esize] */
    tcg_debug_assert(shift > 0);
    tcg_debug_assert(shift <= (8 << vece));

    if (shift == (8 << vece)) {
        /*
         * Shifts larger than the element size are architecturally valid.
         * Signed results in all sign bits.  With rounding, this produces
         *   (-1 + 1) >> 1 == 0, or (0 + 1) >> 1 == 0.
         * I.e. always zero.
         */
        tcg_gen_gvec_dup_imm(vece, rd_ofs, opr_sz, max_sz, 0);
    } else {
        tcg_gen_gvec_2i(rd_ofs, rm_ofs, opr_sz, max_sz, shift, &ops[vece]);
    }
}

static void gen_srsra8_i64(TCGv_i64 d, TCGv_i64 a, int64_t sh)
{
    TCGv_i64 t = tcg_temp_new_i64();

    gen_srshr8_i64(t, a, sh);
    tcg_gen_vec_add8_i64(d, d, t);
    tcg_temp_free_i64(t);
}

static void gen_srsra16_i64(TCGv_i64 d, TCGv_i64 a, int64_t sh)
{
    TCGv_i64 t = tcg_temp_new_i64();

    gen_srshr16_i64(t, a, sh);
    tcg_gen_vec_add16_i64(d, d, t);
    tcg_temp_free_i64(t);
}

static void gen_srsra32_i32(TCGv_i32 d, TCGv_i32 a, int32_t sh)
{
    TCGv_i32 t = tcg_temp_new_i32();

    gen_srshr32_i32(t, a, sh);
    tcg_gen_add_i32(d, d, t);
    tcg_temp_free_i32(t);
}

static void gen_srsra64_i64(TCGv_i64 d, TCGv_i64 a, int64_t sh)
{
    TCGv_i64 t = tcg_temp_new_i64();

    gen_srshr64_i64(t, a, sh);
    tcg_gen_add_i64(d, d, t);
    tcg_temp_free_i64(t);
}

static void gen_srsra_vec(unsigned vece, TCGv_vec d, TCGv_vec a, int64_t sh)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);

    gen_srshr_vec(vece, t, a, sh);
    tcg_gen_add_vec(vece, d, d, t);
    tcg_temp_free_vec(t);
}

void gen_gvec_srsra(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                    int64_t shift, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_sari_vec, INDEX_op_add_vec, 0
    };
    static const GVecGen2i ops[4] = {
        { .fni8 = gen_srsra8_i64,
          .fniv = gen_srsra_vec,
          .fno = gen_helper_gvec_srsra_b,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_8 },
        { .fni8 = gen_srsra16_i64,
          .fniv = gen_srsra_vec,
          .fno = gen_helper_gvec_srsra_h,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_16 },
        { .fni4 = gen_srsra32_i32,
          .fniv = gen_srsra_vec,
          .fno = gen_helper_gvec_srsra_s,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_32 },
        { .fni8 = gen_srsra64_i64,
          .fniv = gen_srsra_vec,
          .fno = gen_helper_gvec_srsra_d,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_64 },
    };

    /* tszimm encoding produces immediates in the range [1..esize] */
    tcg_debug_assert(shift > 0);
    tcg_debug_assert(shift <= (8 << vece));

    /*
     * Shifts larger than the element size are architecturally valid.
     * Signed results in all sign bits.  With rounding, this produces
     *   (-1 + 1) >> 1 == 0, or (0 + 1) >> 1 == 0.
     * I.e. always zero.  With accumulation, this leaves D unchanged.
     */
    if (shift == (8 << vece)) {
        /* Nop, but we do need to clear the tail. */
        tcg_gen_gvec_mov(vece, rd_ofs, rd_ofs, opr_sz, max_sz);
    } else {
        tcg_gen_gvec_2i(rd_ofs, rm_ofs, opr_sz, max_sz, shift, &ops[vece]);
    }
}

static void gen_urshr8_i64(TCGv_i64 d, TCGv_i64 a, int64_t sh)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_shri_i64(t, a, sh - 1);
    tcg_gen_andi_i64(t, t, dup_const(MO_8, 1));
    tcg_gen_vec_shr8i_i64(d, a, sh);
    tcg_gen_vec_add8_i64(d, d, t);
    tcg_temp_free_i64(t);
}

static void gen_urshr16_i64(TCGv_i64 d, TCGv_i64 a, int64_t sh)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_shri_i64(t, a, sh - 1);
    tcg_gen_andi_i64(t, t, dup_const(MO_16, 1));
    tcg_gen_vec_shr16i_i64(d, a, sh);
    tcg_gen_vec_add16_i64(d, d, t);
    tcg_temp_free_i64(t);
}

static void gen_urshr32_i32(TCGv_i32 d, TCGv_i32 a, int32_t sh)
{
    TCGv_i32 t;

    /* Handle shift by the input size for the benefit of trans_URSHR_ri */
    if (sh == 32) {
        tcg_gen_extract_i32(d, a, sh - 1, 1);
        return;
    }
    t = tcg_temp_new_i32();
    tcg_gen_extract_i32(t, a, sh - 1, 1);
    tcg_gen_shri_i32(d, a, sh);
    tcg_gen_add_i32(d, d, t);
    tcg_temp_free_i32(t);
}

static void gen_urshr64_i64(TCGv_i64 d, TCGv_i64 a, int64_t sh)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_extract_i64(t, a, sh - 1, 1);
    tcg_gen_shri_i64(d, a, sh);
    tcg_gen_add_i64(d, d, t);
    tcg_temp_free_i64(t);
}

static void gen_urshr_vec(unsigned vece, TCGv_vec d, TCGv_vec a, int64_t shift)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);
    TCGv_vec ones = tcg_temp_new_vec_matching(d);

    tcg_gen_shri_vec(vece, t, a, shift - 1);
    tcg_gen_dupi_vec(vece, ones, 1);
    tcg_gen_and_vec(vece, t, t, ones);
    tcg_gen_shri_vec(vece, d, a, shift);
    tcg_gen_add_vec(vece, d, d, t);

    tcg_temp_free_vec(t);
    tcg_temp_free_vec(ones);
}

void gen_gvec_urshr(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                    int64_t shift, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_add_vec, 0
    };
    static const GVecGen2i ops[4] = {
        { .fni8 = gen_urshr8_i64,
          .fniv = gen_urshr_vec,
          .fno = gen_helper_gvec_urshr_b,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fni8 = gen_urshr16_i64,
          .fniv = gen_urshr_vec,
          .fno = gen_helper_gvec_urshr_h,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fni4 = gen_urshr32_i32,
          .fniv = gen_urshr_vec,
          .fno = gen_helper_gvec_urshr_s,
          .opt_opc = vecop_list,
          .vece = MO_32 },
        { .fni8 = gen_urshr64_i64,
          .fniv = gen_urshr_vec,
          .fno = gen_helper_gvec_urshr_d,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .opt_opc = vecop_list,
          .vece = MO_64 },
    };

    /* tszimm encoding produces immediates in the range [1..esize] */
    tcg_debug_assert(shift > 0);
    tcg_debug_assert(shift <= (8 << vece));

    if (shift == (8 << vece)) {
        /*
         * Shifts larger than the element size are architecturally valid.
         * Unsigned results in zero.  With rounding, this produces a
         * copy of the most significant bit.
         */
        tcg_gen_gvec_shri(vece, rd_ofs, rm_ofs, shift - 1, opr_sz, max_sz);
    } else {
        tcg_gen_gvec_2i(rd_ofs, rm_ofs, opr_sz, max_sz, shift, &ops[vece]);
    }
}

static void gen_ursra8_i64(TCGv_i64 d, TCGv_i64 a, int64_t sh)
{
    TCGv_i64 t = tcg_temp_new_i64();

    if (sh == 8) {
        tcg_gen_vec_shr8i_i64(t, a, 7);
    } else {
        gen_urshr8_i64(t, a, sh);
    }
    tcg_gen_vec_add8_i64(d, d, t);
    tcg_temp_free_i64(t);
}

static void gen_ursra16_i64(TCGv_i64 d, TCGv_i64 a, int64_t sh)
{
    TCGv_i64 t = tcg_temp_new_i64();

    if (sh == 16) {
        tcg_gen_vec_shr16i_i64(t, a, 15);
    } else {
        gen_urshr16_i64(t, a, sh);
    }
    tcg_gen_vec_add16_i64(d, d, t);
    tcg_temp_free_i64(t);
}

static void gen_ursra32_i32(TCGv_i32 d, TCGv_i32 a, int32_t sh)
{
    TCGv_i32 t = tcg_temp_new_i32();

    if (sh == 32) {
        tcg_gen_shri_i32(t, a, 31);
    } else {
        gen_urshr32_i32(t, a, sh);
    }
    tcg_gen_add_i32(d, d, t);
    tcg_temp_free_i32(t);
}

static void gen_ursra64_i64(TCGv_i64 d, TCGv_i64 a, int64_t sh)
{
    TCGv_i64 t = tcg_temp_new_i64();

    if (sh == 64) {
        tcg_gen_shri_i64(t, a, 63);
    } else {
        gen_urshr64_i64(t, a, sh);
    }
    tcg_gen_add_i64(d, d, t);
    tcg_temp_free_i64(t);
}

static void gen_ursra_vec(unsigned vece, TCGv_vec d, TCGv_vec a, int64_t sh)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);

    if (sh == (8 << vece)) {
        tcg_gen_shri_vec(vece, t, a, sh - 1);
    } else {
        gen_urshr_vec(vece, t, a, sh);
    }
    tcg_gen_add_vec(vece, d, d, t);
    tcg_temp_free_vec(t);
}

void gen_gvec_ursra(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                    int64_t shift, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_add_vec, 0
    };
    static const GVecGen2i ops[4] = {
        { .fni8 = gen_ursra8_i64,
          .fniv = gen_ursra_vec,
          .fno = gen_helper_gvec_ursra_b,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_8 },
        { .fni8 = gen_ursra16_i64,
          .fniv = gen_ursra_vec,
          .fno = gen_helper_gvec_ursra_h,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_16 },
        { .fni4 = gen_ursra32_i32,
          .fniv = gen_ursra_vec,
          .fno = gen_helper_gvec_ursra_s,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_32 },
        { .fni8 = gen_ursra64_i64,
          .fniv = gen_ursra_vec,
          .fno = gen_helper_gvec_ursra_d,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_64 },
    };

    /* tszimm encoding produces immediates in the range [1..esize] */
    tcg_debug_assert(shift > 0);
    tcg_debug_assert(shift <= (8 << vece));

    tcg_gen_gvec_2i(rd_ofs, rm_ofs, opr_sz, max_sz, shift, &ops[vece]);
}

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
    TCGv_vec t = tcg_temp_new_vec_matching(d);
    TCGv_vec m = tcg_temp_new_vec_matching(d);

    tcg_gen_dupi_vec(vece, m, MAKE_64BIT_MASK((8 << vece) - sh, sh));
    tcg_gen_shri_vec(vece, t, a, sh);
    tcg_gen_and_vec(vece, d, d, m);
    tcg_gen_or_vec(vece, d, d, t);

    tcg_temp_free_vec(t);
    tcg_temp_free_vec(m);
}

void gen_gvec_sri(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                  int64_t shift, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = { INDEX_op_shri_vec, 0 };
    const GVecGen2i ops[4] = {
        { .fni8 = gen_shr8_ins_i64,
          .fniv = gen_shr_ins_vec,
          .fno = gen_helper_gvec_sri_b,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fni8 = gen_shr16_ins_i64,
          .fniv = gen_shr_ins_vec,
          .fno = gen_helper_gvec_sri_h,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fni4 = gen_shr32_ins_i32,
          .fniv = gen_shr_ins_vec,
          .fno = gen_helper_gvec_sri_s,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_32 },
        { .fni8 = gen_shr64_ins_i64,
          .fniv = gen_shr_ins_vec,
          .fno = gen_helper_gvec_sri_d,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_64 },
    };

    /* tszimm encoding produces immediates in the range [1..esize]. */
    tcg_debug_assert(shift > 0);
    tcg_debug_assert(shift <= (8 << vece));

    /* Shift of esize leaves destination unchanged. */
    if (shift < (8 << vece)) {
        tcg_gen_gvec_2i(rd_ofs, rm_ofs, opr_sz, max_sz, shift, &ops[vece]);
    } else {
        /* Nop, but we do need to clear the tail. */
        tcg_gen_gvec_mov(vece, rd_ofs, rd_ofs, opr_sz, max_sz);
    }
}

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
    TCGv_vec t = tcg_temp_new_vec_matching(d);
    TCGv_vec m = tcg_temp_new_vec_matching(d);

    tcg_gen_shli_vec(vece, t, a, sh);
    tcg_gen_dupi_vec(vece, m, MAKE_64BIT_MASK(0, sh));
    tcg_gen_and_vec(vece, d, d, m);
    tcg_gen_or_vec(vece, d, d, t);

    tcg_temp_free_vec(t);
    tcg_temp_free_vec(m);
}

void gen_gvec_sli(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                  int64_t shift, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = { INDEX_op_shli_vec, 0 };
    const GVecGen2i ops[4] = {
        { .fni8 = gen_shl8_ins_i64,
          .fniv = gen_shl_ins_vec,
          .fno = gen_helper_gvec_sli_b,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fni8 = gen_shl16_ins_i64,
          .fniv = gen_shl_ins_vec,
          .fno = gen_helper_gvec_sli_h,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fni4 = gen_shl32_ins_i32,
          .fniv = gen_shl_ins_vec,
          .fno = gen_helper_gvec_sli_s,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_32 },
        { .fni8 = gen_shl64_ins_i64,
          .fniv = gen_shl_ins_vec,
          .fno = gen_helper_gvec_sli_d,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_64 },
    };

    /* tszimm encoding produces immediates in the range [0..esize-1]. */
    tcg_debug_assert(shift >= 0);
    tcg_debug_assert(shift < (8 << vece));

    if (shift == 0) {
        tcg_gen_gvec_mov(vece, rd_ofs, rm_ofs, opr_sz, max_sz);
    } else {
        tcg_gen_gvec_2i(rd_ofs, rm_ofs, opr_sz, max_sz, shift, &ops[vece]);
    }
}

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
void gen_gvec_mla(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                  uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_mul_vec, INDEX_op_add_vec, 0
    };
    static const GVecGen3 ops[4] = {
        { .fni4 = gen_mla8_i32,
          .fniv = gen_mla_vec,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fni4 = gen_mla16_i32,
          .fniv = gen_mla_vec,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fni4 = gen_mla32_i32,
          .fniv = gen_mla_vec,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_32 },
        { .fni8 = gen_mla64_i64,
          .fniv = gen_mla_vec,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_64 },
    };
    tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, &ops[vece]);
}

void gen_gvec_mls(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                  uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_mul_vec, INDEX_op_sub_vec, 0
    };
    static const GVecGen3 ops[4] = {
        { .fni4 = gen_mls8_i32,
          .fniv = gen_mls_vec,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fni4 = gen_mls16_i32,
          .fniv = gen_mls_vec,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fni4 = gen_mls32_i32,
          .fniv = gen_mls_vec,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_32 },
        { .fni8 = gen_mls64_i64,
          .fniv = gen_mls_vec,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_64 },
    };
    tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, &ops[vece]);
}

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

void gen_gvec_cmtst(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = { INDEX_op_cmp_vec, 0 };
    static const GVecGen3 ops[4] = {
        { .fni4 = gen_helper_neon_tst_u8,
          .fniv = gen_cmtst_vec,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fni4 = gen_helper_neon_tst_u16,
          .fniv = gen_cmtst_vec,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fni4 = gen_cmtst_i32,
          .fniv = gen_cmtst_vec,
          .opt_opc = vecop_list,
          .vece = MO_32 },
        { .fni8 = gen_cmtst_i64,
          .fniv = gen_cmtst_vec,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .opt_opc = vecop_list,
          .vece = MO_64 },
    };
    tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, &ops[vece]);
}

void gen_ushl_i32(TCGv_i32 dst, TCGv_i32 src, TCGv_i32 shift)
{
    TCGv_i32 lval = tcg_temp_new_i32();
    TCGv_i32 rval = tcg_temp_new_i32();
    TCGv_i32 lsh = tcg_temp_new_i32();
    TCGv_i32 rsh = tcg_temp_new_i32();
    TCGv_i32 zero = tcg_constant_i32(0);
    TCGv_i32 max = tcg_constant_i32(32);

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
}

void gen_ushl_i64(TCGv_i64 dst, TCGv_i64 src, TCGv_i64 shift)
{
    TCGv_i64 lval = tcg_temp_new_i64();
    TCGv_i64 rval = tcg_temp_new_i64();
    TCGv_i64 lsh = tcg_temp_new_i64();
    TCGv_i64 rsh = tcg_temp_new_i64();
    TCGv_i64 zero = tcg_constant_i64(0);
    TCGv_i64 max = tcg_constant_i64(64);

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

void gen_gvec_ushl(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                   uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_neg_vec, INDEX_op_shlv_vec,
        INDEX_op_shrv_vec, INDEX_op_cmp_vec, 0
    };
    static const GVecGen3 ops[4] = {
        { .fniv = gen_ushl_vec,
          .fno = gen_helper_gvec_ushl_b,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fniv = gen_ushl_vec,
          .fno = gen_helper_gvec_ushl_h,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fni4 = gen_ushl_i32,
          .fniv = gen_ushl_vec,
          .opt_opc = vecop_list,
          .vece = MO_32 },
        { .fni8 = gen_ushl_i64,
          .fniv = gen_ushl_vec,
          .opt_opc = vecop_list,
          .vece = MO_64 },
    };
    tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, &ops[vece]);
}

void gen_sshl_i32(TCGv_i32 dst, TCGv_i32 src, TCGv_i32 shift)
{
    TCGv_i32 lval = tcg_temp_new_i32();
    TCGv_i32 rval = tcg_temp_new_i32();
    TCGv_i32 lsh = tcg_temp_new_i32();
    TCGv_i32 rsh = tcg_temp_new_i32();
    TCGv_i32 zero = tcg_constant_i32(0);
    TCGv_i32 max = tcg_constant_i32(31);

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
}

void gen_sshl_i64(TCGv_i64 dst, TCGv_i64 src, TCGv_i64 shift)
{
    TCGv_i64 lval = tcg_temp_new_i64();
    TCGv_i64 rval = tcg_temp_new_i64();
    TCGv_i64 lsh = tcg_temp_new_i64();
    TCGv_i64 rsh = tcg_temp_new_i64();
    TCGv_i64 zero = tcg_constant_i64(0);
    TCGv_i64 max = tcg_constant_i64(63);

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

void gen_gvec_sshl(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                   uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_neg_vec, INDEX_op_umin_vec, INDEX_op_shlv_vec,
        INDEX_op_sarv_vec, INDEX_op_cmp_vec, INDEX_op_cmpsel_vec, 0
    };
    static const GVecGen3 ops[4] = {
        { .fniv = gen_sshl_vec,
          .fno = gen_helper_gvec_sshl_b,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fniv = gen_sshl_vec,
          .fno = gen_helper_gvec_sshl_h,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fni4 = gen_sshl_i32,
          .fniv = gen_sshl_vec,
          .opt_opc = vecop_list,
          .vece = MO_32 },
        { .fni8 = gen_sshl_i64,
          .fniv = gen_sshl_vec,
          .opt_opc = vecop_list,
          .vece = MO_64 },
    };
    tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, &ops[vece]);
}

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

void gen_gvec_uqadd_qc(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                       uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_usadd_vec, INDEX_op_cmp_vec, INDEX_op_add_vec, 0
    };
    static const GVecGen4 ops[4] = {
        { .fniv = gen_uqadd_vec,
          .fno = gen_helper_gvec_uqadd_b,
          .write_aofs = true,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fniv = gen_uqadd_vec,
          .fno = gen_helper_gvec_uqadd_h,
          .write_aofs = true,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fniv = gen_uqadd_vec,
          .fno = gen_helper_gvec_uqadd_s,
          .write_aofs = true,
          .opt_opc = vecop_list,
          .vece = MO_32 },
        { .fniv = gen_uqadd_vec,
          .fno = gen_helper_gvec_uqadd_d,
          .write_aofs = true,
          .opt_opc = vecop_list,
          .vece = MO_64 },
    };
    tcg_gen_gvec_4(rd_ofs, offsetof(CPUARMState, vfp.qc),
                   rn_ofs, rm_ofs, opr_sz, max_sz, &ops[vece]);
}

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

void gen_gvec_sqadd_qc(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                       uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_ssadd_vec, INDEX_op_cmp_vec, INDEX_op_add_vec, 0
    };
    static const GVecGen4 ops[4] = {
        { .fniv = gen_sqadd_vec,
          .fno = gen_helper_gvec_sqadd_b,
          .opt_opc = vecop_list,
          .write_aofs = true,
          .vece = MO_8 },
        { .fniv = gen_sqadd_vec,
          .fno = gen_helper_gvec_sqadd_h,
          .opt_opc = vecop_list,
          .write_aofs = true,
          .vece = MO_16 },
        { .fniv = gen_sqadd_vec,
          .fno = gen_helper_gvec_sqadd_s,
          .opt_opc = vecop_list,
          .write_aofs = true,
          .vece = MO_32 },
        { .fniv = gen_sqadd_vec,
          .fno = gen_helper_gvec_sqadd_d,
          .opt_opc = vecop_list,
          .write_aofs = true,
          .vece = MO_64 },
    };
    tcg_gen_gvec_4(rd_ofs, offsetof(CPUARMState, vfp.qc),
                   rn_ofs, rm_ofs, opr_sz, max_sz, &ops[vece]);
}

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

void gen_gvec_uqsub_qc(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                       uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_ussub_vec, INDEX_op_cmp_vec, INDEX_op_sub_vec, 0
    };
    static const GVecGen4 ops[4] = {
        { .fniv = gen_uqsub_vec,
          .fno = gen_helper_gvec_uqsub_b,
          .opt_opc = vecop_list,
          .write_aofs = true,
          .vece = MO_8 },
        { .fniv = gen_uqsub_vec,
          .fno = gen_helper_gvec_uqsub_h,
          .opt_opc = vecop_list,
          .write_aofs = true,
          .vece = MO_16 },
        { .fniv = gen_uqsub_vec,
          .fno = gen_helper_gvec_uqsub_s,
          .opt_opc = vecop_list,
          .write_aofs = true,
          .vece = MO_32 },
        { .fniv = gen_uqsub_vec,
          .fno = gen_helper_gvec_uqsub_d,
          .opt_opc = vecop_list,
          .write_aofs = true,
          .vece = MO_64 },
    };
    tcg_gen_gvec_4(rd_ofs, offsetof(CPUARMState, vfp.qc),
                   rn_ofs, rm_ofs, opr_sz, max_sz, &ops[vece]);
}

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

void gen_gvec_sqsub_qc(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                       uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_sssub_vec, INDEX_op_cmp_vec, INDEX_op_sub_vec, 0
    };
    static const GVecGen4 ops[4] = {
        { .fniv = gen_sqsub_vec,
          .fno = gen_helper_gvec_sqsub_b,
          .opt_opc = vecop_list,
          .write_aofs = true,
          .vece = MO_8 },
        { .fniv = gen_sqsub_vec,
          .fno = gen_helper_gvec_sqsub_h,
          .opt_opc = vecop_list,
          .write_aofs = true,
          .vece = MO_16 },
        { .fniv = gen_sqsub_vec,
          .fno = gen_helper_gvec_sqsub_s,
          .opt_opc = vecop_list,
          .write_aofs = true,
          .vece = MO_32 },
        { .fniv = gen_sqsub_vec,
          .fno = gen_helper_gvec_sqsub_d,
          .opt_opc = vecop_list,
          .write_aofs = true,
          .vece = MO_64 },
    };
    tcg_gen_gvec_4(rd_ofs, offsetof(CPUARMState, vfp.qc),
                   rn_ofs, rm_ofs, opr_sz, max_sz, &ops[vece]);
}

static void gen_sabd_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_sub_i32(t, a, b);
    tcg_gen_sub_i32(d, b, a);
    tcg_gen_movcond_i32(TCG_COND_LT, d, a, b, d, t);
    tcg_temp_free_i32(t);
}

static void gen_sabd_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_sub_i64(t, a, b);
    tcg_gen_sub_i64(d, b, a);
    tcg_gen_movcond_i64(TCG_COND_LT, d, a, b, d, t);
    tcg_temp_free_i64(t);
}

static void gen_sabd_vec(unsigned vece, TCGv_vec d, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);

    tcg_gen_smin_vec(vece, t, a, b);
    tcg_gen_smax_vec(vece, d, a, b);
    tcg_gen_sub_vec(vece, d, d, t);
    tcg_temp_free_vec(t);
}

void gen_gvec_sabd(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                   uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_sub_vec, INDEX_op_smin_vec, INDEX_op_smax_vec, 0
    };
    static const GVecGen3 ops[4] = {
        { .fniv = gen_sabd_vec,
          .fno = gen_helper_gvec_sabd_b,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fniv = gen_sabd_vec,
          .fno = gen_helper_gvec_sabd_h,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fni4 = gen_sabd_i32,
          .fniv = gen_sabd_vec,
          .fno = gen_helper_gvec_sabd_s,
          .opt_opc = vecop_list,
          .vece = MO_32 },
        { .fni8 = gen_sabd_i64,
          .fniv = gen_sabd_vec,
          .fno = gen_helper_gvec_sabd_d,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .opt_opc = vecop_list,
          .vece = MO_64 },
    };
    tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, &ops[vece]);
}

static void gen_uabd_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_sub_i32(t, a, b);
    tcg_gen_sub_i32(d, b, a);
    tcg_gen_movcond_i32(TCG_COND_LTU, d, a, b, d, t);
    tcg_temp_free_i32(t);
}

static void gen_uabd_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_sub_i64(t, a, b);
    tcg_gen_sub_i64(d, b, a);
    tcg_gen_movcond_i64(TCG_COND_LTU, d, a, b, d, t);
    tcg_temp_free_i64(t);
}

static void gen_uabd_vec(unsigned vece, TCGv_vec d, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);

    tcg_gen_umin_vec(vece, t, a, b);
    tcg_gen_umax_vec(vece, d, a, b);
    tcg_gen_sub_vec(vece, d, d, t);
    tcg_temp_free_vec(t);
}

void gen_gvec_uabd(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                   uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_sub_vec, INDEX_op_umin_vec, INDEX_op_umax_vec, 0
    };
    static const GVecGen3 ops[4] = {
        { .fniv = gen_uabd_vec,
          .fno = gen_helper_gvec_uabd_b,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fniv = gen_uabd_vec,
          .fno = gen_helper_gvec_uabd_h,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fni4 = gen_uabd_i32,
          .fniv = gen_uabd_vec,
          .fno = gen_helper_gvec_uabd_s,
          .opt_opc = vecop_list,
          .vece = MO_32 },
        { .fni8 = gen_uabd_i64,
          .fniv = gen_uabd_vec,
          .fno = gen_helper_gvec_uabd_d,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .opt_opc = vecop_list,
          .vece = MO_64 },
    };
    tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, &ops[vece]);
}

static void gen_saba_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t = tcg_temp_new_i32();
    gen_sabd_i32(t, a, b);
    tcg_gen_add_i32(d, d, t);
    tcg_temp_free_i32(t);
}

static void gen_saba_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t = tcg_temp_new_i64();
    gen_sabd_i64(t, a, b);
    tcg_gen_add_i64(d, d, t);
    tcg_temp_free_i64(t);
}

static void gen_saba_vec(unsigned vece, TCGv_vec d, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);
    gen_sabd_vec(vece, t, a, b);
    tcg_gen_add_vec(vece, d, d, t);
    tcg_temp_free_vec(t);
}

void gen_gvec_saba(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                   uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_sub_vec, INDEX_op_add_vec,
        INDEX_op_smin_vec, INDEX_op_smax_vec, 0
    };
    static const GVecGen3 ops[4] = {
        { .fniv = gen_saba_vec,
          .fno = gen_helper_gvec_saba_b,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_8 },
        { .fniv = gen_saba_vec,
          .fno = gen_helper_gvec_saba_h,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_16 },
        { .fni4 = gen_saba_i32,
          .fniv = gen_saba_vec,
          .fno = gen_helper_gvec_saba_s,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_32 },
        { .fni8 = gen_saba_i64,
          .fniv = gen_saba_vec,
          .fno = gen_helper_gvec_saba_d,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_64 },
    };
    tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, &ops[vece]);
}

static void gen_uaba_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t = tcg_temp_new_i32();
    gen_uabd_i32(t, a, b);
    tcg_gen_add_i32(d, d, t);
    tcg_temp_free_i32(t);
}

static void gen_uaba_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t = tcg_temp_new_i64();
    gen_uabd_i64(t, a, b);
    tcg_gen_add_i64(d, d, t);
    tcg_temp_free_i64(t);
}

static void gen_uaba_vec(unsigned vece, TCGv_vec d, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);
    gen_uabd_vec(vece, t, a, b);
    tcg_gen_add_vec(vece, d, d, t);
    tcg_temp_free_vec(t);
}

void gen_gvec_uaba(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                   uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_sub_vec, INDEX_op_add_vec,
        INDEX_op_umin_vec, INDEX_op_umax_vec, 0
    };
    static const GVecGen3 ops[4] = {
        { .fniv = gen_uaba_vec,
          .fno = gen_helper_gvec_uaba_b,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_8 },
        { .fniv = gen_uaba_vec,
          .fno = gen_helper_gvec_uaba_h,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_16 },
        { .fni4 = gen_uaba_i32,
          .fniv = gen_uaba_vec,
          .fno = gen_helper_gvec_uaba_s,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_32 },
        { .fni8 = gen_uaba_i64,
          .fniv = gen_uaba_vec,
          .fno = gen_helper_gvec_uaba_d,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_64 },
    };
    tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, &ops[vece]);
}

static void do_coproc_insn(DisasContext *s, int cpnum, int is64,
                           int opc1, int crn, int crm, int opc2,
                           bool isread, int rt, int rt2)
{
    uint32_t key = ENCODE_CP_REG(cpnum, is64, s->ns, crn, crm, opc1, opc2);
    const ARMCPRegInfo *ri = get_arm_cp_reginfo(s->cp_regs, key);
    TCGv_ptr tcg_ri = NULL;
    bool need_exit_tb;
    uint32_t syndrome;

    /*
     * Note that since we are an implementation which takes an
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
        /*
         * ARMv8 defines that only coprocessors 14 and 15 exist,
         * so this can only happen if this is an ARMv7 or earlier CPU,
         * in which case the syndrome information won't actually be
         * guest visible.
         */
        assert(!arm_dc_feature(s, ARM_FEATURE_V8));
        syndrome = syn_uncategorized();
        break;
    }

    if (s->hstr_active && cpnum == 15 && s->current_el == 1) {
        /*
         * At EL1, check for a HSTR_EL2 trap, which must take precedence
         * over the UNDEF for "no such register" or the UNDEF for "access
         * permissions forbid this EL1 access". HSTR_EL2 traps from EL0
         * only happen if the cpreg doesn't UNDEF at EL0, so we do those in
         * access_check_cp_reg(), after the checks for whether the access
         * configurably trapped to EL1.
         */
        uint32_t maskbit = is64 ? crm : crn;

        if (maskbit != 4 && maskbit != 14) {
            /* T4 and T14 are RES0 so never cause traps */
            TCGv_i32 t;
            DisasLabel over = gen_disas_label(s);

            t = load_cpu_offset(offsetoflow32(CPUARMState, cp15.hstr_el2));
            tcg_gen_andi_i32(t, t, 1u << maskbit);
            tcg_gen_brcondi_i32(TCG_COND_EQ, t, 0, over.label);
            tcg_temp_free_i32(t);

            gen_exception_insn(s, 0, EXCP_UDEF, syndrome);
            set_disas_label(s, over);
        }
    }

    if (!ri) {
        /*
         * Unknown register; this might be a guest error or a QEMU
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
                          "system register cp:%d opc1:%d crn:%d crm:%d "
                          "opc2:%d (%s)\n",
                          isread ? "read" : "write", cpnum, opc1, crn,
                          crm, opc2, s->ns ? "non-secure" : "secure");
        }
        unallocated_encoding(s);
        return;
    }

    /* Check access permissions */
    if (!cp_access_ok(s->current_el, ri, isread)) {
        unallocated_encoding(s);
        return;
    }

    if ((s->hstr_active && s->current_el == 0) || ri->accessfn ||
        (ri->fgt && s->fgt_active) ||
        (arm_dc_feature(s, ARM_FEATURE_XSCALE) && cpnum < 14)) {
        /*
         * Emit code to perform further access permissions checks at
         * runtime; this may result in an exception.
         * Note that on XScale all cp0..c13 registers do an access check
         * call in order to handle c15_cpar.
         */
        gen_set_condexec(s);
        gen_update_pc(s, 0);
        tcg_ri = tcg_temp_new_ptr();
        gen_helper_access_check_cp_reg(tcg_ri, cpu_env,
                                       tcg_constant_i32(key),
                                       tcg_constant_i32(syndrome),
                                       tcg_constant_i32(isread));
    } else if (ri->type & ARM_CP_RAISES_EXC) {
        /*
         * The readfn or writefn might raise an exception;
         * synchronize the CPU state in case it does.
         */
        gen_set_condexec(s);
        gen_update_pc(s, 0);
    }

    /* Handle special cases first */
    switch (ri->type & ARM_CP_SPECIAL_MASK) {
    case 0:
        break;
    case ARM_CP_NOP:
        goto exit;
    case ARM_CP_WFI:
        if (isread) {
            unallocated_encoding(s);
        } else {
            gen_update_pc(s, curr_insn_len(s));
            s->base.is_jmp = DISAS_WFI;
        }
        goto exit;
    default:
        g_assert_not_reached();
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
                tmp64 = tcg_constant_i64(ri->resetvalue);
            } else if (ri->readfn) {
                if (!tcg_ri) {
                    tcg_ri = gen_lookup_cp_reg(key);
                }
                tmp64 = tcg_temp_new_i64();
                gen_helper_get_cp_reg64(tmp64, cpu_env, tcg_ri);
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
                tmp = tcg_constant_i32(ri->resetvalue);
            } else if (ri->readfn) {
                if (!tcg_ri) {
                    tcg_ri = gen_lookup_cp_reg(key);
                }
                tmp = tcg_temp_new_i32();
                gen_helper_get_cp_reg(tmp, cpu_env, tcg_ri);
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
            goto exit;
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
                if (!tcg_ri) {
                    tcg_ri = gen_lookup_cp_reg(key);
                }
                gen_helper_set_cp_reg64(cpu_env, tcg_ri, tmp64);
            } else {
                tcg_gen_st_i64(tmp64, cpu_env, ri->fieldoffset);
            }
            tcg_temp_free_i64(tmp64);
        } else {
            TCGv_i32 tmp = load_reg(s, rt);
            if (ri->writefn) {
                if (!tcg_ri) {
                    tcg_ri = gen_lookup_cp_reg(key);
                }
                gen_helper_set_cp_reg(cpu_env, tcg_ri, tmp);
                tcg_temp_free_i32(tmp);
            } else {
                store_cpu_offset(tmp, ri->fieldoffset, 4);
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
        gen_rebuild_hflags(s, ri->type & ARM_CP_NEWEL);
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

 exit:
    if (tcg_ri) {
        tcg_temp_free_ptr(tcg_ri);
    }
}

/* Decode XScale DSP or iWMMXt insn (in the copro space, cp=0 or 1) */
static void disas_xscale_insn(DisasContext *s, uint32_t insn)
{
    int cpnum = (insn >> 8) & 0xf;

    if (extract32(s->c15_cpar, cpnum, 1) == 0) {
        unallocated_encoding(s);
    } else if (arm_dc_feature(s, ARM_FEATURE_IWMMXT)) {
        if (disas_iwmmxt_insn(s, insn)) {
            unallocated_encoding(s);
        }
    } else if (arm_dc_feature(s, ARM_FEATURE_XSCALE)) {
        if (disas_dsp_insn(s, insn)) {
            unallocated_encoding(s);
        }
    }
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

        /*
         * For AArch32, architecturally the 32-bit word at the lowest
         * address is always Rt and the one at addr+4 is Rt2, even if
         * the CPU is big-endian. That means we don't want to do a
         * gen_aa32_ld_i64(), which checks SCTLR_B as if for an
         * architecturally 64-bit access, but instead do a 64-bit access
         * using MO_BE if appropriate and then split the two halves.
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

        /*
         * For AArch32, architecturally the 32-bit word at the lowest
         * address is always Rt and the one at addr+4 is Rt2, even if
         * the CPU is big-endian. Since we're going to treat this as a
         * single 64-bit BE store, we need to put the two halves in the
         * opposite order for BE to LE, so that they end up in the right
         * places.  We don't want gen_aa32_st_i64, because that checks
         * SCTLR_B as if for an architectural 64-bit access.
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
        gen_exception_insn_el(s, 0, EXCP_UDEF, syn_uncategorized(), 3);
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
    /* get_r13_banked() will raise an exception if called from System mode */
    gen_set_condexec(s);
    gen_update_pc(s, 0);
    gen_helper_get_r13_banked(addr, cpu_env, tcg_constant_i32(mode));
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
        g_assert_not_reached();
    }
    tcg_gen_addi_i32(addr, addr, offset);
    tmp = load_reg(s, 14);
    gen_aa32_st_i32(s, tmp, addr, get_mem_index(s), MO_UL | MO_ALIGN);
    tcg_temp_free_i32(tmp);
    tmp = load_cpu_field(spsr);
    tcg_gen_addi_i32(addr, addr, 4);
    gen_aa32_st_i32(s, tmp, addr, get_mem_index(s), MO_UL | MO_ALIGN);
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
            g_assert_not_reached();
        }
        tcg_gen_addi_i32(addr, addr, offset);
        gen_helper_set_r13_banked(cpu_env, tcg_constant_i32(mode), addr);
    }
    tcg_temp_free_i32(addr);
    s->base.is_jmp = DISAS_UPDATE_EXIT;
}

/* Skip this instruction if the ARM condition is false */
static void arm_skip_unless(DisasContext *s, uint32_t cond)
{
    arm_gen_condlabel(s);
    arm_gen_test_cc(cond ^ 1, s->condlabel.label);
}


/*
 * Constant expanders used by T16/T32 decode
 */

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

#include "decode-a32.c.inc"
#include "decode-a32-uncond.c.inc"
#include "decode-t32.c.inc"
#include "decode-t16.c.inc"

static bool valid_cp(DisasContext *s, int cp)
{
    /*
     * Return true if this coprocessor field indicates something
     * that's really a possible coprocessor.
     * For v7 and earlier, coprocessors 8..15 were reserved for Arm use,
     * and of those only cp14 and cp15 were used for registers.
     * cp10 and cp11 were used for VFP and Neon, whose decode is
     * dealt with elsewhere. With the advent of fp16, cp9 is also
     * now part of VFP.
     * For v8A and later, the encoding has been tightened so that
     * only cp14 and cp15 are valid, and other values aren't considered
     * to be in the coprocessor-instruction space at all. v8M still
     * permits coprocessors 0..7.
     * For XScale, we must not decode the XScale cp0, cp1 space as
     * a standard coprocessor insn, because we want to fall through to
     * the legacy disas_xscale_insn() decoder after decodetree is done.
     */
    if (arm_dc_feature(s, ARM_FEATURE_XSCALE) && (cp == 0 || cp == 1)) {
        return false;
    }

    if (arm_dc_feature(s, ARM_FEATURE_V8) &&
        !arm_dc_feature(s, ARM_FEATURE_M)) {
        return cp >= 14;
    }
    return cp < 8 || cp >= 14;
}

static bool trans_MCR(DisasContext *s, arg_MCR *a)
{
    if (!valid_cp(s, a->cp)) {
        return false;
    }
    do_coproc_insn(s, a->cp, false, a->opc1, a->crn, a->crm, a->opc2,
                   false, a->rt, 0);
    return true;
}

static bool trans_MRC(DisasContext *s, arg_MRC *a)
{
    if (!valid_cp(s, a->cp)) {
        return false;
    }
    do_coproc_insn(s, a->cp, false, a->opc1, a->crn, a->crm, a->opc2,
                   true, a->rt, 0);
    return true;
}

static bool trans_MCRR(DisasContext *s, arg_MCRR *a)
{
    if (!valid_cp(s, a->cp)) {
        return false;
    }
    do_coproc_insn(s, a->cp, true, a->opc1, 0, a->crm, 0,
                   false, a->rt, a->rt2);
    return true;
}

static bool trans_MRRC(DisasContext *s, arg_MRRC *a)
{
    if (!valid_cp(s, a->cp)) {
        return false;
    }
    do_coproc_insn(s, a->cp, true, a->opc1, 0, a->crm, 0,
                   true, a->rt, a->rt2);
    return true;
}

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
    TCGv_i32 tmp1;
    uint32_t imm;

    imm = ror32(a->imm, a->rot);
    if (logic_cc && a->rot) {
        tcg_gen_movi_i32(cpu_CF, imm >> 31);
    }
    tmp1 = load_reg(s, a->rn);

    gen(tmp1, tmp1, tcg_constant_i32(imm));

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

    tmp = tcg_temp_new_i32();
    gen(tmp, tcg_constant_i32(imm));

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
    if (!ENABLE_ARCH_6T2) {
        return false;
    }

    store_reg(s, a->rd, tcg_constant_i32(a->imm));
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
 * v8.1M MVE wide-shifts
 */
static bool do_mve_shl_ri(DisasContext *s, arg_mve_shl_ri *a,
                          WideShiftImmFn *fn)
{
    TCGv_i64 rda;
    TCGv_i32 rdalo, rdahi;

    if (!arm_dc_feature(s, ARM_FEATURE_V8_1M)) {
        /* Decode falls through to ORR/MOV UNPREDICTABLE handling */
        return false;
    }
    if (a->rdahi == 15) {
        /* These are a different encoding (SQSHL/SRSHR/UQSHL/URSHR) */
        return false;
    }
    if (!dc_isar_feature(aa32_mve, s) ||
        !arm_dc_feature(s, ARM_FEATURE_M_MAIN) ||
        a->rdahi == 13) {
        /* RdaHi == 13 is UNPREDICTABLE; we choose to UNDEF */
        unallocated_encoding(s);
        return true;
    }

    if (a->shim == 0) {
        a->shim = 32;
    }

    rda = tcg_temp_new_i64();
    rdalo = load_reg(s, a->rdalo);
    rdahi = load_reg(s, a->rdahi);
    tcg_gen_concat_i32_i64(rda, rdalo, rdahi);

    fn(rda, rda, a->shim);

    tcg_gen_extrl_i64_i32(rdalo, rda);
    tcg_gen_extrh_i64_i32(rdahi, rda);
    store_reg(s, a->rdalo, rdalo);
    store_reg(s, a->rdahi, rdahi);
    tcg_temp_free_i64(rda);

    return true;
}

static bool trans_ASRL_ri(DisasContext *s, arg_mve_shl_ri *a)
{
    return do_mve_shl_ri(s, a, tcg_gen_sari_i64);
}

static bool trans_LSLL_ri(DisasContext *s, arg_mve_shl_ri *a)
{
    return do_mve_shl_ri(s, a, tcg_gen_shli_i64);
}

static bool trans_LSRL_ri(DisasContext *s, arg_mve_shl_ri *a)
{
    return do_mve_shl_ri(s, a, tcg_gen_shri_i64);
}

static void gen_mve_sqshll(TCGv_i64 r, TCGv_i64 n, int64_t shift)
{
    gen_helper_mve_sqshll(r, cpu_env, n, tcg_constant_i32(shift));
}

static bool trans_SQSHLL_ri(DisasContext *s, arg_mve_shl_ri *a)
{
    return do_mve_shl_ri(s, a, gen_mve_sqshll);
}

static void gen_mve_uqshll(TCGv_i64 r, TCGv_i64 n, int64_t shift)
{
    gen_helper_mve_uqshll(r, cpu_env, n, tcg_constant_i32(shift));
}

static bool trans_UQSHLL_ri(DisasContext *s, arg_mve_shl_ri *a)
{
    return do_mve_shl_ri(s, a, gen_mve_uqshll);
}

static bool trans_SRSHRL_ri(DisasContext *s, arg_mve_shl_ri *a)
{
    return do_mve_shl_ri(s, a, gen_srshr64_i64);
}

static bool trans_URSHRL_ri(DisasContext *s, arg_mve_shl_ri *a)
{
    return do_mve_shl_ri(s, a, gen_urshr64_i64);
}

static bool do_mve_shl_rr(DisasContext *s, arg_mve_shl_rr *a, WideShiftFn *fn)
{
    TCGv_i64 rda;
    TCGv_i32 rdalo, rdahi;

    if (!arm_dc_feature(s, ARM_FEATURE_V8_1M)) {
        /* Decode falls through to ORR/MOV UNPREDICTABLE handling */
        return false;
    }
    if (a->rdahi == 15) {
        /* These are a different encoding (SQSHL/SRSHR/UQSHL/URSHR) */
        return false;
    }
    if (!dc_isar_feature(aa32_mve, s) ||
        !arm_dc_feature(s, ARM_FEATURE_M_MAIN) ||
        a->rdahi == 13 || a->rm == 13 || a->rm == 15 ||
        a->rm == a->rdahi || a->rm == a->rdalo) {
        /* These rdahi/rdalo/rm cases are UNPREDICTABLE; we choose to UNDEF */
        unallocated_encoding(s);
        return true;
    }

    rda = tcg_temp_new_i64();
    rdalo = load_reg(s, a->rdalo);
    rdahi = load_reg(s, a->rdahi);
    tcg_gen_concat_i32_i64(rda, rdalo, rdahi);

    /* The helper takes care of the sign-extension of the low 8 bits of Rm */
    fn(rda, cpu_env, rda, cpu_R[a->rm]);

    tcg_gen_extrl_i64_i32(rdalo, rda);
    tcg_gen_extrh_i64_i32(rdahi, rda);
    store_reg(s, a->rdalo, rdalo);
    store_reg(s, a->rdahi, rdahi);
    tcg_temp_free_i64(rda);

    return true;
}

static bool trans_LSLL_rr(DisasContext *s, arg_mve_shl_rr *a)
{
    return do_mve_shl_rr(s, a, gen_helper_mve_ushll);
}

static bool trans_ASRL_rr(DisasContext *s, arg_mve_shl_rr *a)
{
    return do_mve_shl_rr(s, a, gen_helper_mve_sshrl);
}

static bool trans_UQRSHLL64_rr(DisasContext *s, arg_mve_shl_rr *a)
{
    return do_mve_shl_rr(s, a, gen_helper_mve_uqrshll);
}

static bool trans_SQRSHRL64_rr(DisasContext *s, arg_mve_shl_rr *a)
{
    return do_mve_shl_rr(s, a, gen_helper_mve_sqrshrl);
}

static bool trans_UQRSHLL48_rr(DisasContext *s, arg_mve_shl_rr *a)
{
    return do_mve_shl_rr(s, a, gen_helper_mve_uqrshll48);
}

static bool trans_SQRSHRL48_rr(DisasContext *s, arg_mve_shl_rr *a)
{
    return do_mve_shl_rr(s, a, gen_helper_mve_sqrshrl48);
}

static bool do_mve_sh_ri(DisasContext *s, arg_mve_sh_ri *a, ShiftImmFn *fn)
{
    if (!arm_dc_feature(s, ARM_FEATURE_V8_1M)) {
        /* Decode falls through to ORR/MOV UNPREDICTABLE handling */
        return false;
    }
    if (!dc_isar_feature(aa32_mve, s) ||
        !arm_dc_feature(s, ARM_FEATURE_M_MAIN) ||
        a->rda == 13 || a->rda == 15) {
        /* These rda cases are UNPREDICTABLE; we choose to UNDEF */
        unallocated_encoding(s);
        return true;
    }

    if (a->shim == 0) {
        a->shim = 32;
    }
    fn(cpu_R[a->rda], cpu_R[a->rda], a->shim);

    return true;
}

static bool trans_URSHR_ri(DisasContext *s, arg_mve_sh_ri *a)
{
    return do_mve_sh_ri(s, a, gen_urshr32_i32);
}

static bool trans_SRSHR_ri(DisasContext *s, arg_mve_sh_ri *a)
{
    return do_mve_sh_ri(s, a, gen_srshr32_i32);
}

static void gen_mve_sqshl(TCGv_i32 r, TCGv_i32 n, int32_t shift)
{
    gen_helper_mve_sqshl(r, cpu_env, n, tcg_constant_i32(shift));
}

static bool trans_SQSHL_ri(DisasContext *s, arg_mve_sh_ri *a)
{
    return do_mve_sh_ri(s, a, gen_mve_sqshl);
}

static void gen_mve_uqshl(TCGv_i32 r, TCGv_i32 n, int32_t shift)
{
    gen_helper_mve_uqshl(r, cpu_env, n, tcg_constant_i32(shift));
}

static bool trans_UQSHL_ri(DisasContext *s, arg_mve_sh_ri *a)
{
    return do_mve_sh_ri(s, a, gen_mve_uqshl);
}

static bool do_mve_sh_rr(DisasContext *s, arg_mve_sh_rr *a, ShiftFn *fn)
{
    if (!arm_dc_feature(s, ARM_FEATURE_V8_1M)) {
        /* Decode falls through to ORR/MOV UNPREDICTABLE handling */
        return false;
    }
    if (!dc_isar_feature(aa32_mve, s) ||
        !arm_dc_feature(s, ARM_FEATURE_M_MAIN) ||
        a->rda == 13 || a->rda == 15 || a->rm == 13 || a->rm == 15 ||
        a->rm == a->rda) {
        /* These rda/rm cases are UNPREDICTABLE; we choose to UNDEF */
        unallocated_encoding(s);
        return true;
    }

    /* The helper takes care of the sign-extension of the low 8 bits of Rm */
    fn(cpu_R[a->rda], cpu_env, cpu_R[a->rda], cpu_R[a->rm]);
    return true;
}

static bool trans_SQRSHR_rr(DisasContext *s, arg_mve_sh_rr *a)
{
    return do_mve_sh_rr(s, a, gen_helper_mve_sqrshr);
}

static bool trans_UQRSHL_rr(DisasContext *s, arg_mve_sh_rr *a)
{
    return do_mve_sh_rr(s, a, gen_helper_mve_uqrshl);
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
    zero = tcg_constant_i32(0);
    t2 = load_reg(s, a->ra);
    tcg_gen_add2_i32(t0, t1, t0, t1, t2, zero);
    tcg_temp_free_i32(t2);
    t2 = load_reg(s, a->rd);
    tcg_gen_add2_i32(t0, t1, t0, t1, t2, zero);
    tcg_temp_free_i32(t2);
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
        gen_update_pc(s, curr_insn_len(s));
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
        gen_update_pc(s, curr_insn_len(s));
        s->base.is_jmp = DISAS_WFE;
    }
    return true;
}

static bool trans_WFI(DisasContext *s, arg_WFI *a)
{
    /* For WFI, halt the vCPU until an IRQ. */
    gen_update_pc(s, curr_insn_len(s));
    s->base.is_jmp = DISAS_WFI;
    return true;
}

static bool trans_ESB(DisasContext *s, arg_ESB *a)
{
    /*
     * For M-profile, minimal-RAS ESB can be a NOP.
     * Without RAS, we must implement this as NOP.
     */
    if (!arm_dc_feature(s, ARM_FEATURE_M) && dc_isar_feature(aa32_ras, s)) {
        /*
         * QEMU does not have a source of physical SErrors,
         * so we are only concerned with virtual SErrors.
         * The pseudocode in the ARM for this case is
         *   if PSTATE.EL IN {EL0, EL1} && EL2Enabled() then
         *      AArch32.vESBOperation();
         * Most of the condition can be evaluated at translation time.
         * Test for EL2 present, and defer test for SEL2 to runtime.
         */
        if (s->current_el <= 1 && arm_dc_feature(s, ARM_FEATURE_EL2)) {
            gen_helper_vesb(cpu_env);
        }
    }
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
    t3 = tcg_constant_i32(1 << sz);
    if (c) {
        gen_helper_crc32c(t1, t1, t2, t3);
    } else {
        gen_helper_crc32(t1, t1, t2, t3);
    }
    tcg_temp_free_i32(t2);
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
    tmp = tcg_temp_new_i32();
    gen_helper_v7m_mrs(tmp, cpu_env, tcg_constant_i32(a->sysm));
    store_reg(s, a->rd, tmp);
    return true;
}

static bool trans_MSR_v7m(DisasContext *s, arg_MSR_v7m *a)
{
    TCGv_i32 addr, reg;

    if (!arm_dc_feature(s, ARM_FEATURE_M)) {
        return false;
    }
    addr = tcg_constant_i32((a->mask << 10) | a->sysm);
    reg = load_reg(s, a->rn);
    gen_helper_v7m_msr(cpu_env, addr, reg);
    tcg_temp_free_i32(reg);
    /* If we wrote to CONTROL, the EL might have changed */
    gen_rebuild_hflags(s, true);
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
    /*
     * v7A allows BXJ to be trapped via HSTR.TJDBX. We don't waste a
     * TBFLAGS bit on a basically-never-happens case, so call a helper
     * function to check for the trap and raise the exception if needed
     * (passing it the register number for the syndrome value).
     * v8A doesn't have this HSTR bit.
     */
    if (!arm_dc_feature(s, ARM_FEATURE_V8) &&
        arm_dc_feature(s, ARM_FEATURE_EL2) &&
        s->current_el < 2 && s->ns) {
        gen_helper_check_bxj_trap(cpu_env, tcg_constant_i32(a->rm));
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
    gen_pc_plus_diff(s, cpu_R[14], curr_insn_len(s) | s->thumb);
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
    /* BKPT is OK with ECI set and leaves it untouched */
    s->eci_handled = true;
    if (arm_dc_feature(s, ARM_FEATURE_M) &&
        semihosting_enabled(s->current_el == 0) &&
        (a->imm == 0xab)) {
        gen_exception_internal_insn(s, EXCP_SEMIHOST);
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
    tmp = tcg_temp_new_i32();
    gen_helper_v7m_tt(tmp, cpu_env, addr, tcg_constant_i32((a->A << 1) | a->T));
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
        if (curr_insn_len(s) == 2) {
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
    gen_aa32_ld_i32(s, tmp, addr, mem_idx, mop);
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

    /*
     * In Thumb encodings of stores Rn=1111 is UNDEF; for Arm it
     * is either UNPREDICTABLE or has defined behaviour
     */
    if (s->thumb && a->rn == 15) {
        return false;
    }

    addr = op_addr_rr_pre(s, a);

    tmp = load_reg(s, a->rt);
    gen_aa32_st_i32(s, tmp, addr, mem_idx, mop);
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
    gen_aa32_ld_i32(s, tmp, addr, mem_idx, MO_UL | MO_ALIGN);
    store_reg(s, a->rt, tmp);

    tcg_gen_addi_i32(addr, addr, 4);

    tmp = tcg_temp_new_i32();
    gen_aa32_ld_i32(s, tmp, addr, mem_idx, MO_UL | MO_ALIGN);
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
    gen_aa32_st_i32(s, tmp, addr, mem_idx, MO_UL | MO_ALIGN);
    tcg_temp_free_i32(tmp);

    tcg_gen_addi_i32(addr, addr, 4);

    tmp = load_reg(s, a->rt + 1);
    gen_aa32_st_i32(s, tmp, addr, mem_idx, MO_UL | MO_ALIGN);
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
    gen_aa32_ld_i32(s, tmp, addr, mem_idx, mop);
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

    /*
     * In Thumb encodings of stores Rn=1111 is UNDEF; for Arm it
     * is either UNPREDICTABLE or has defined behaviour
     */
    if (s->thumb && a->rn == 15) {
        return false;
    }

    addr = op_addr_ri_pre(s, a);

    tmp = load_reg(s, a->rt);
    gen_aa32_st_i32(s, tmp, addr, mem_idx, mop);
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
    gen_aa32_ld_i32(s, tmp, addr, mem_idx, MO_UL | MO_ALIGN);
    store_reg(s, a->rt, tmp);

    tcg_gen_addi_i32(addr, addr, 4);

    tmp = tcg_temp_new_i32();
    gen_aa32_ld_i32(s, tmp, addr, mem_idx, MO_UL | MO_ALIGN);
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
    gen_aa32_st_i32(s, tmp, addr, mem_idx, MO_UL | MO_ALIGN);
    tcg_temp_free_i32(tmp);

    tcg_gen_addi_i32(addr, addr, 4);

    tmp = load_reg(s, rt2);
    gen_aa32_st_i32(s, tmp, addr, mem_idx, MO_UL | MO_ALIGN);
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

    addr = tcg_temp_new_i32();
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
    gen_aa32_st_i32(s, tmp, addr, get_mem_index(s), mop | MO_ALIGN);
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

    addr = tcg_temp_new_i32();
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
    gen_aa32_ld_i32(s, tmp, addr, get_mem_index(s), mop | MO_ALIGN);
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
    TCGv_i32 tmp;
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

    gen(tmp, cpu_env, tmp, tcg_constant_i32(a->satimm));

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
        gen_swap_half(t2, t2);
    }
    gen_smul_dual(t1, t2);

    if (sub) {
        /*
         * This subtraction cannot overflow, so we can do a simple
         * 32-bit subtraction and then a possible 32-bit saturating
         * addition of Ra.
         */
        tcg_gen_sub_i32(t1, t1, t2);
        tcg_temp_free_i32(t2);

        if (a->ra != 15) {
            t2 = load_reg(s, a->ra);
            gen_helper_add_setq(t1, cpu_env, t1, t2);
            tcg_temp_free_i32(t2);
        }
    } else if (a->ra == 15) {
        /* Single saturation-checking addition */
        gen_helper_add_setq(t1, cpu_env, t1, t2);
        tcg_temp_free_i32(t2);
    } else {
        /*
         * We need to add the products and Ra together and then
         * determine whether the final result overflowed. Doing
         * this as two separate add-and-check-overflow steps incorrectly
         * sets Q for cases like (-32768 * -32768) + (-32768 * -32768) + -1.
         * Do all the arithmetic at 64-bits and then check for overflow.
         */
        TCGv_i64 p64, q64;
        TCGv_i32 t3, qf, one;

        p64 = tcg_temp_new_i64();
        q64 = tcg_temp_new_i64();
        tcg_gen_ext_i32_i64(p64, t1);
        tcg_gen_ext_i32_i64(q64, t2);
        tcg_gen_add_i64(p64, p64, q64);
        load_reg_var(s, t2, a->ra);
        tcg_gen_ext_i32_i64(q64, t2);
        tcg_gen_add_i64(p64, p64, q64);
        tcg_temp_free_i64(q64);

        tcg_gen_extr_i64_i32(t1, t2, p64);
        tcg_temp_free_i64(p64);
        /*
         * t1 is the low half of the result which goes into Rd.
         * We have overflow and must set Q if the high half (t2)
         * is different from the sign-extension of t1.
         */
        t3 = tcg_temp_new_i32();
        tcg_gen_sari_i32(t3, t1, 31);
        qf = load_cpu_field(QF);
        one = tcg_constant_i32(1);
        tcg_gen_movcond_i32(TCG_COND_NE, qf, t2, t3, one, qf);
        store_cpu_field(qf, QF);
        tcg_temp_free_i32(t3);
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
        gen_swap_half(t2, t2);
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
            tcg_gen_sub2_i32(t2, t1, tcg_constant_i32(0), t3, t2, t1);
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
        gen_helper_udiv(t1, cpu_env, t1, t2);
    } else {
        gen_helper_sdiv(t1, cpu_env, t1, t2);
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
         * be above, then we must not perform any memory
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
    TCGv_i32 addr, tmp;

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

    s->eci_handled = true;

    addr = op_addr_block_pre(s, a, n);
    mem_idx = get_mem_index(s);

    for (i = j = 0; i < 16; i++) {
        if (!(list & (1 << i))) {
            continue;
        }

        if (user && i != 15) {
            tmp = tcg_temp_new_i32();
            gen_helper_get_user_reg(tmp, cpu_env, tcg_constant_i32(i));
        } else {
            tmp = load_reg(s, i);
        }
        gen_aa32_st_i32(s, tmp, addr, mem_idx, MO_UL | MO_ALIGN);
        tcg_temp_free_i32(tmp);

        /* No need to add after the last transfer.  */
        if (++j != n) {
            tcg_gen_addi_i32(addr, addr, 4);
        }
    }

    op_addr_block_post(s, a, addr, n);
    clear_eci_state(s);
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
    TCGv_i32 addr, tmp, loaded_var;

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

    s->eci_handled = true;

    addr = op_addr_block_pre(s, a, n);
    mem_idx = get_mem_index(s);
    loaded_base = false;
    loaded_var = NULL;

    for (i = j = 0; i < 16; i++) {
        if (!(list & (1 << i))) {
            continue;
        }

        tmp = tcg_temp_new_i32();
        gen_aa32_ld_i32(s, tmp, addr, mem_idx, MO_UL | MO_ALIGN);
        if (user) {
            gen_helper_set_user_reg(cpu_env, tcg_constant_i32(i), tmp);
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
        tcg_temp_free_i32(tmp);
        /* Must exit loop to check un-masked IRQs */
        s->base.is_jmp = DISAS_EXIT;
    }
    clear_eci_state(s);
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

static bool trans_CLRM(DisasContext *s, arg_CLRM *a)
{
    int i;
    TCGv_i32 zero;

    if (!dc_isar_feature(aa32_m_sec_state, s)) {
        return false;
    }

    if (extract32(a->list, 13, 1)) {
        return false;
    }

    if (!a->list) {
        /* UNPREDICTABLE; we choose to UNDEF */
        return false;
    }

    s->eci_handled = true;

    zero = tcg_constant_i32(0);
    for (i = 0; i < 15; i++) {
        if (extract32(a->list, i, 1)) {
            /* Clear R[i] */
            tcg_gen_mov_i32(cpu_R[i], zero);
        }
    }
    if (extract32(a->list, 15, 1)) {
        /*
         * Clear APSR (by calling the MSR helper with the same argument
         * as for "MSR APSR_nzcvqg, Rn": mask = 0b1100, SYSM=0)
         */
        gen_helper_v7m_msr(cpu_env, tcg_constant_i32(0xc00), zero);
    }
    clear_eci_state(s);
    return true;
}

/*
 * Branch, branch with link
 */

static bool trans_B(DisasContext *s, arg_i *a)
{
    gen_jmp(s, jmp_diff(s, a->imm));
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
    gen_jmp(s, jmp_diff(s, a->imm));
    return true;
}

static bool trans_BL(DisasContext *s, arg_i *a)
{
    gen_pc_plus_diff(s, cpu_R[14], curr_insn_len(s) | s->thumb);
    gen_jmp(s, jmp_diff(s, a->imm));
    return true;
}

static bool trans_BLX_i(DisasContext *s, arg_BLX_i *a)
{
    /*
     * BLX <imm> would be useless on M-profile; the encoding space
     * is used for other insns from v8.1M onward, and UNDEFs before that.
     */
    if (arm_dc_feature(s, ARM_FEATURE_M)) {
        return false;
    }

    /* For A32, ARM_FEATURE_V5 is checked near the start of the uncond block. */
    if (s->thumb && (a->imm & 2)) {
        return false;
    }
    gen_pc_plus_diff(s, cpu_R[14], curr_insn_len(s) | s->thumb);
    store_cpu_field_constant(!s->thumb, thumb);
    /* This jump is computed from an aligned PC: subtract off the low bits. */
    gen_jmp(s, jmp_diff(s, a->imm - (s->pc_curr & 3)));
    return true;
}

static bool trans_BL_BLX_prefix(DisasContext *s, arg_BL_BLX_prefix *a)
{
    assert(!arm_dc_feature(s, ARM_FEATURE_THUMB2));
    gen_pc_plus_diff(s, cpu_R[14], jmp_diff(s, a->imm << 12));
    return true;
}

static bool trans_BL_suffix(DisasContext *s, arg_BL_suffix *a)
{
    TCGv_i32 tmp = tcg_temp_new_i32();

    assert(!arm_dc_feature(s, ARM_FEATURE_THUMB2));
    tcg_gen_addi_i32(tmp, cpu_R[14], (a->imm << 1) | 1);
    gen_pc_plus_diff(s, cpu_R[14], curr_insn_len(s) | 1);
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
    gen_pc_plus_diff(s, cpu_R[14], curr_insn_len(s) | 1);
    gen_bx(s, tmp);
    return true;
}

static bool trans_BF(DisasContext *s, arg_BF *a)
{
    /*
     * M-profile branch future insns. The architecture permits an
     * implementation to implement these as NOPs (equivalent to
     * discarding the LO_BRANCH_INFO cache immediately), and we
     * take that IMPDEF option because for QEMU a "real" implementation
     * would be complicated and wouldn't execute any faster.
     */
    if (!dc_isar_feature(aa32_lob, s)) {
        return false;
    }
    if (a->boff == 0) {
        /* SEE "Related encodings" (loop insns) */
        return false;
    }
    /* Handle as NOP */
    return true;
}

static bool trans_DLS(DisasContext *s, arg_DLS *a)
{
    /* M-profile low-overhead loop start */
    TCGv_i32 tmp;

    if (!dc_isar_feature(aa32_lob, s)) {
        return false;
    }
    if (a->rn == 13 || a->rn == 15) {
        /*
         * For DLSTP rn == 15 is a related encoding (LCTP); the
         * other cases caught by this condition are all
         * CONSTRAINED UNPREDICTABLE: we choose to UNDEF
         */
        return false;
    }

    if (a->size != 4) {
        /* DLSTP */
        if (!dc_isar_feature(aa32_mve, s)) {
            return false;
        }
        if (!vfp_access_check(s)) {
            return true;
        }
    }

    /* Not a while loop: set LR to the count, and set LTPSIZE for DLSTP */
    tmp = load_reg(s, a->rn);
    store_reg(s, 14, tmp);
    if (a->size != 4) {
        /* DLSTP: set FPSCR.LTPSIZE */
        store_cpu_field(tcg_constant_i32(a->size), v7m.ltpsize);
        s->base.is_jmp = DISAS_UPDATE_NOCHAIN;
    }
    return true;
}

static bool trans_WLS(DisasContext *s, arg_WLS *a)
{
    /* M-profile low-overhead while-loop start */
    TCGv_i32 tmp;
    DisasLabel nextlabel;

    if (!dc_isar_feature(aa32_lob, s)) {
        return false;
    }
    if (a->rn == 13 || a->rn == 15) {
        /*
         * For WLSTP rn == 15 is a related encoding (LE); the
         * other cases caught by this condition are all
         * CONSTRAINED UNPREDICTABLE: we choose to UNDEF
         */
        return false;
    }
    if (s->condexec_mask) {
        /*
         * WLS in an IT block is CONSTRAINED UNPREDICTABLE;
         * we choose to UNDEF, because otherwise our use of
         * gen_goto_tb(1) would clash with the use of TB exit 1
         * in the dc->condjmp condition-failed codepath in
         * arm_tr_tb_stop() and we'd get an assertion.
         */
        return false;
    }
    if (a->size != 4) {
        /* WLSTP */
        if (!dc_isar_feature(aa32_mve, s)) {
            return false;
        }
        /*
         * We need to check that the FPU is enabled here, but mustn't
         * call vfp_access_check() to do that because we don't want to
         * do the lazy state preservation in the "loop count is zero" case.
         * Do the check-and-raise-exception by hand.
         */
        if (s->fp_excp_el) {
            gen_exception_insn_el(s, 0, EXCP_NOCP,
                                  syn_uncategorized(), s->fp_excp_el);
            return true;
        }
    }

    nextlabel = gen_disas_label(s);
    tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_R[a->rn], 0, nextlabel.label);
    tmp = load_reg(s, a->rn);
    store_reg(s, 14, tmp);
    if (a->size != 4) {
        /*
         * WLSTP: set FPSCR.LTPSIZE. This requires that we do the
         * lazy state preservation, new FP context creation, etc,
         * that vfp_access_check() does. We know that the actual
         * access check will succeed (ie it won't generate code that
         * throws an exception) because we did that check by hand earlier.
         */
        bool ok = vfp_access_check(s);
        assert(ok);
        store_cpu_field(tcg_constant_i32(a->size), v7m.ltpsize);
        /*
         * LTPSIZE updated, but MVE_NO_PRED will always be the same thing (0)
         * when we take this upcoming exit from this TB, so gen_jmp_tb() is OK.
         */
    }
    gen_jmp_tb(s, curr_insn_len(s), 1);

    set_disas_label(s, nextlabel);
    gen_jmp(s, jmp_diff(s, a->imm));
    return true;
}

static bool trans_LE(DisasContext *s, arg_LE *a)
{
    /*
     * M-profile low-overhead loop end. The architecture permits an
     * implementation to discard the LO_BRANCH_INFO cache at any time,
     * and we take the IMPDEF option to never set it in the first place
     * (equivalent to always discarding it immediately), because for QEMU
     * a "real" implementation would be complicated and wouldn't execute
     * any faster.
     */
    TCGv_i32 tmp;
    DisasLabel loopend;
    bool fpu_active;

    if (!dc_isar_feature(aa32_lob, s)) {
        return false;
    }
    if (a->f && a->tp) {
        return false;
    }
    if (s->condexec_mask) {
        /*
         * LE in an IT block is CONSTRAINED UNPREDICTABLE;
         * we choose to UNDEF, because otherwise our use of
         * gen_goto_tb(1) would clash with the use of TB exit 1
         * in the dc->condjmp condition-failed codepath in
         * arm_tr_tb_stop() and we'd get an assertion.
         */
        return false;
    }
    if (a->tp) {
        /* LETP */
        if (!dc_isar_feature(aa32_mve, s)) {
            return false;
        }
        if (!vfp_access_check(s)) {
            s->eci_handled = true;
            return true;
        }
    }

    /* LE/LETP is OK with ECI set and leaves it untouched */
    s->eci_handled = true;

    /*
     * With MVE, LTPSIZE might not be 4, and we must emit an INVSTATE
     * UsageFault exception for the LE insn in that case. Note that we
     * are not directly checking FPSCR.LTPSIZE but instead check the
     * pseudocode LTPSIZE() function, which returns 4 if the FPU is
     * not currently active (ie ActiveFPState() returns false). We
     * can identify not-active purely from our TB state flags, as the
     * FPU is active only if:
     *  the FPU is enabled
     *  AND lazy state preservation is not active
     *  AND we do not need a new fp context (this is the ASPEN/FPCA check)
     *
     * Usually we don't need to care about this distinction between
     * LTPSIZE and FPSCR.LTPSIZE, because the code in vfp_access_check()
     * will either take an exception or clear the conditions that make
     * the FPU not active. But LE is an unusual case of a non-FP insn
     * that looks at LTPSIZE.
     */
    fpu_active = !s->fp_excp_el && !s->v7m_lspact && !s->v7m_new_fp_ctxt_needed;

    if (!a->tp && dc_isar_feature(aa32_mve, s) && fpu_active) {
        /* Need to do a runtime check for LTPSIZE != 4 */
        DisasLabel skipexc = gen_disas_label(s);
        tmp = load_cpu_field(v7m.ltpsize);
        tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 4, skipexc.label);
        tcg_temp_free_i32(tmp);
        gen_exception_insn(s, 0, EXCP_INVSTATE, syn_uncategorized());
        set_disas_label(s, skipexc);
    }

    if (a->f) {
        /* Loop-forever: just jump back to the loop start */
        gen_jmp(s, jmp_diff(s, -a->imm));
        return true;
    }

    /*
     * Not loop-forever. If LR <= loop-decrement-value this is the last loop.
     * For LE, we know at this point that LTPSIZE must be 4 and the
     * loop decrement value is 1. For LETP we need to calculate the decrement
     * value from LTPSIZE.
     */
    loopend = gen_disas_label(s);
    if (!a->tp) {
        tcg_gen_brcondi_i32(TCG_COND_LEU, cpu_R[14], 1, loopend.label);
        tcg_gen_addi_i32(cpu_R[14], cpu_R[14], -1);
    } else {
        /*
         * Decrement by 1 << (4 - LTPSIZE). We need to use a TCG local
         * so that decr stays live after the brcondi.
         */
        TCGv_i32 decr = tcg_temp_new_i32();
        TCGv_i32 ltpsize = load_cpu_field(v7m.ltpsize);
        tcg_gen_sub_i32(decr, tcg_constant_i32(4), ltpsize);
        tcg_gen_shl_i32(decr, tcg_constant_i32(1), decr);
        tcg_temp_free_i32(ltpsize);

        tcg_gen_brcond_i32(TCG_COND_LEU, cpu_R[14], decr, loopend.label);

        tcg_gen_sub_i32(cpu_R[14], cpu_R[14], decr);
        tcg_temp_free_i32(decr);
    }
    /* Jump back to the loop start */
    gen_jmp(s, jmp_diff(s, -a->imm));

    set_disas_label(s, loopend);
    if (a->tp) {
        /* Exits from tail-pred loops must reset LTPSIZE to 4 */
        store_cpu_field(tcg_constant_i32(4), v7m.ltpsize);
    }
    /* End TB, continuing to following insn */
    gen_jmp_tb(s, curr_insn_len(s), 1);
    return true;
}

static bool trans_LCTP(DisasContext *s, arg_LCTP *a)
{
    /*
     * M-profile Loop Clear with Tail Predication. Since our implementation
     * doesn't cache branch information, all we need to do is reset
     * FPSCR.LTPSIZE to 4.
     */

    if (!dc_isar_feature(aa32_lob, s) ||
        !dc_isar_feature(aa32_mve, s)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    store_cpu_field_constant(4, v7m.ltpsize);
    return true;
}

static bool trans_VCTP(DisasContext *s, arg_VCTP *a)
{
    /*
     * M-profile Create Vector Tail Predicate. This insn is itself
     * predicated and is subject to beatwise execution.
     */
    TCGv_i32 rn_shifted, masklen;

    if (!dc_isar_feature(aa32_mve, s) || a->rn == 13 || a->rn == 15) {
        return false;
    }

    if (!mve_eci_check(s) || !vfp_access_check(s)) {
        return true;
    }

    /*
     * We pre-calculate the mask length here to avoid having
     * to have multiple helpers specialized for size.
     * We pass the helper "rn <= (1 << (4 - size)) ? (rn << size) : 16".
     */
    rn_shifted = tcg_temp_new_i32();
    masklen = load_reg(s, a->rn);
    tcg_gen_shli_i32(rn_shifted, masklen, a->size);
    tcg_gen_movcond_i32(TCG_COND_LEU, masklen,
                        masklen, tcg_constant_i32(1 << (4 - a->size)),
                        rn_shifted, tcg_constant_i32(16));
    gen_helper_mve_vctp(cpu_env, masklen);
    tcg_temp_free_i32(masklen);
    tcg_temp_free_i32(rn_shifted);
    /* This insn updates predication bits */
    s->base.is_jmp = DISAS_UPDATE_NOCHAIN;
    mve_update_eci(s);
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

    gen_aa32_ld_i32(s, tmp, addr, get_mem_index(s), half ? MO_UW : MO_UB);

    tcg_gen_add_i32(tmp, tmp, tmp);
    gen_pc_plus_diff(s, addr, jmp_diff(s, 0));
    tcg_gen_add_i32(tmp, tmp, addr);
    tcg_temp_free_i32(addr);
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
                        tmp, 0, s->condlabel.label);
    tcg_temp_free_i32(tmp);
    gen_jmp(s, jmp_diff(s, a->imm));
    return true;
}

/*
 * Supervisor call - both T32 & A32 come here so we need to check
 * which mode we are in when checking for semihosting.
 */

static bool trans_SVC(DisasContext *s, arg_SVC *a)
{
    const uint32_t semihost_imm = s->thumb ? 0xab : 0x123456;

    if (!arm_dc_feature(s, ARM_FEATURE_M) &&
        semihosting_enabled(s->current_el == 0) &&
        (a->imm == semihost_imm)) {
        gen_exception_internal_insn(s, EXCP_SEMIHOST);
    } else {
        if (s->fgt_svc) {
            uint32_t syndrome = syn_aa32_svc(a->imm, s->thumb);
            gen_exception_insn_el(s, 0, EXCP_UDEF, syndrome, 2);
        } else {
            gen_update_pc(s, curr_insn_len(s));
            s->svc_imm = a->imm;
            s->base.is_jmp = DISAS_SWI;
        }
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
    gen_aa32_ld_i32(s, t1, addr, get_mem_index(s), MO_UL | MO_ALIGN);
    tcg_gen_addi_i32(addr, addr, 4);
    t2 = tcg_temp_new_i32();
    gen_aa32_ld_i32(s, t2, addr, get_mem_index(s), MO_UL | MO_ALIGN);

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
    TCGv_i32 tmp, addr;

    if (!arm_dc_feature(s, ARM_FEATURE_M)) {
        return false;
    }
    if (IS_USER(s)) {
        /* Implemented as NOP in user mode.  */
        return true;
    }

    tmp = tcg_constant_i32(a->im);
    /* FAULTMASK */
    if (a->F) {
        addr = tcg_constant_i32(19);
        gen_helper_v7m_msr(cpu_env, addr, tmp);
    }
    /* PRIMASK */
    if (a->I) {
        addr = tcg_constant_i32(16);
        gen_helper_v7m_msr(cpu_env, addr, tmp);
    }
    gen_rebuild_hflags(s, false);
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
    s->base.is_jmp = DISAS_TOO_MANY;
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
    s->base.is_jmp = DISAS_TOO_MANY;
    return true;
}

static bool trans_SETEND(DisasContext *s, arg_SETEND *a)
{
    if (!ENABLE_ARCH_6) {
        return false;
    }
    if (a->E != (s->be_data == MO_BE)) {
        gen_helper_setend(cpu_env);
        s->base.is_jmp = DISAS_UPDATE_EXIT;
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

/* v8.1M CSEL/CSINC/CSNEG/CSINV */
static bool trans_CSEL(DisasContext *s, arg_CSEL *a)
{
    TCGv_i32 rn, rm, zero;
    DisasCompare c;

    if (!arm_dc_feature(s, ARM_FEATURE_V8_1M)) {
        return false;
    }

    if (a->rm == 13) {
        /* SEE "Related encodings" (MVE shifts) */
        return false;
    }

    if (a->rd == 13 || a->rd == 15 || a->rn == 13 || a->fcond >= 14) {
        /* CONSTRAINED UNPREDICTABLE: we choose to UNDEF */
        return false;
    }

    /* In this insn input reg fields of 0b1111 mean "zero", not "PC" */
    zero = tcg_constant_i32(0);
    if (a->rn == 15) {
        rn = zero;
    } else {
        rn = load_reg(s, a->rn);
    }
    if (a->rm == 15) {
        rm = zero;
    } else {
        rm = load_reg(s, a->rm);
    }

    switch (a->op) {
    case 0: /* CSEL */
        break;
    case 1: /* CSINC */
        tcg_gen_addi_i32(rm, rm, 1);
        break;
    case 2: /* CSINV */
        tcg_gen_not_i32(rm, rm);
        break;
    case 3: /* CSNEG */
        tcg_gen_neg_i32(rm, rm);
        break;
    default:
        g_assert_not_reached();
    }

    arm_test_cc(&c, a->fcond);
    tcg_gen_movcond_i32(c.cond, rn, c.value, zero, rn, rm);

    store_reg(s, a->rd, rn);
    tcg_temp_free_i32(rm);

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
        gen_exception_insn(s, 0, EXCP_INVSTATE, syn_uncategorized());
        return;
    }

    if (s->pstate_il) {
        /*
         * Illegal execution state. This has priority over BTI
         * exceptions, but comes after instruction abort exceptions.
         */
        gen_exception_insn(s, 0, EXCP_UDEF, syn_illegalstate());
        return;
    }

    if (cond == 0xf) {
        /* In ARMv3 and v4 the NV condition is UNPREDICTABLE; we
         * choose to UNDEF. In ARMv5 and above the space is used
         * for miscellaneous unconditional instructions.
         */
        if (!arm_dc_feature(s, ARM_FEATURE_V5)) {
            unallocated_encoding(s);
            return;
        }

        /* Unconditional instructions.  */
        /* TODO: Perhaps merge these into one decodetree output file.  */
        if (disas_a32_uncond(s, insn) ||
            disas_vfp_uncond(s, insn) ||
            disas_neon_dp(s, insn) ||
            disas_neon_ls(s, insn) ||
            disas_neon_shared(s, insn)) {
            return;
        }
        /* fall back to legacy decoder */

        if ((insn & 0x0e000f00) == 0x0c000100) {
            if (arm_dc_feature(s, ARM_FEATURE_IWMMXT)) {
                /* iWMMXt register transfer.  */
                if (extract32(s->c15_cpar, 1, 1)) {
                    if (!disas_iwmmxt_insn(s, insn)) {
                        return;
                    }
                }
            }
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
    /* TODO: convert xscale/iwmmxt decoder to decodetree ?? */
    if (arm_dc_feature(s, ARM_FEATURE_XSCALE)) {
        if (((insn & 0x0c000e00) == 0x0c000000)
            && ((insn & 0x03000000) != 0x03000000)) {
            /* Coprocessor insn, coprocessor 0 or 1 */
            disas_xscale_insn(s, insn);
            return;
        }
    }

illegal_op:
    unallocated_encoding(s);
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
        if (!arm_dc_feature(s, ARM_FEATURE_THUMB2)) {
            unallocated_encoding(s);
            return;
        }
    }

    if (arm_dc_feature(s, ARM_FEATURE_M)) {
        /*
         * NOCP takes precedence over any UNDEF for (almost) the
         * entire wide range of coprocessor-space encodings, so check
         * for it first before proceeding to actually decode eg VFP
         * insns. This decode also handles the few insns which are
         * in copro space but do not have NOCP checks (eg VLLDM, VLSTM).
         */
        if (disas_m_nocp(s, insn)) {
            return;
        }
    }

    if ((insn & 0xef000000) == 0xef000000) {
        /*
         * T32 encodings 0b111p_1111_qqqq_qqqq_qqqq_qqqq_qqqq_qqqq
         * transform into
         * A32 encodings 0b1111_001p_qqqq_qqqq_qqqq_qqqq_qqqq_qqqq
         */
        uint32_t a32_insn = (insn & 0xe2ffffff) |
            ((insn & (1 << 28)) >> 4) | (1 << 28);

        if (disas_neon_dp(s, a32_insn)) {
            return;
        }
    }

    if ((insn & 0xff100000) == 0xf9000000) {
        /*
         * T32 encodings 0b1111_1001_ppp0_qqqq_qqqq_qqqq_qqqq_qqqq
         * transform into
         * A32 encodings 0b1111_0100_ppp0_qqqq_qqqq_qqqq_qqqq_qqqq
         */
        uint32_t a32_insn = (insn & 0x00ffffff) | 0xf4000000;

        if (disas_neon_ls(s, a32_insn)) {
            return;
        }
    }

    /*
     * TODO: Perhaps merge these into one decodetree output file.
     * Note disas_vfp is written for a32 with cond field in the
     * top nibble.  The t32 encoding requires 0xe in the top nibble.
     */
    if (disas_t32(s, insn) ||
        disas_vfp_uncond(s, insn) ||
        disas_neon_shared(s, insn) ||
        disas_mve(s, insn) ||
        ((insn >> 28) == 0xe && disas_vfp(s, insn))) {
        return;
    }

illegal_op:
    unallocated_encoding(s);
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
    uint16_t insn = arm_lduw_code(env, &s->base, s->base.pc_next, s->sctlr_b);

    return !thumb_insn_is_16bit(s, s->base.pc_next, insn);
}

static void arm_tr_init_disas_context(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    CPUARMState *env = cs->env_ptr;
    ARMCPU *cpu = env_archcpu(env);
    CPUARMTBFlags tb_flags = arm_tbflags_from_tb(dc->base.tb);
    uint32_t condexec, core_mmu_idx;

    dc->isar = &cpu->isar;
    dc->condjmp = 0;
    dc->pc_save = dc->base.pc_first;
    dc->aarch64 = false;
    dc->thumb = EX_TBFLAG_AM32(tb_flags, THUMB);
    dc->be_data = EX_TBFLAG_ANY(tb_flags, BE_DATA) ? MO_BE : MO_LE;
    condexec = EX_TBFLAG_AM32(tb_flags, CONDEXEC);
    /*
     * the CONDEXEC TB flags are CPSR bits [15:10][26:25]. On A-profile this
     * is always the IT bits. On M-profile, some of the reserved encodings
     * of IT are used instead to indicate either ICI or ECI, which
     * indicate partial progress of a restartable insn that was interrupted
     * partway through by an exception:
     *  * if CONDEXEC[3:0] != 0b0000 : CONDEXEC is IT bits
     *  * if CONDEXEC[3:0] == 0b0000 : CONDEXEC is ICI or ECI bits
     * In all cases CONDEXEC == 0 means "not in IT block or restartable
     * insn, behave normally".
     */
    dc->eci = dc->condexec_mask = dc->condexec_cond = 0;
    dc->eci_handled = false;
    if (condexec & 0xf) {
        dc->condexec_mask = (condexec & 0xf) << 1;
        dc->condexec_cond = condexec >> 4;
    } else {
        if (arm_feature(env, ARM_FEATURE_M)) {
            dc->eci = condexec >> 4;
        }
    }

    core_mmu_idx = EX_TBFLAG_ANY(tb_flags, MMUIDX);
    dc->mmu_idx = core_to_arm_mmu_idx(env, core_mmu_idx);
    dc->current_el = arm_mmu_idx_to_el(dc->mmu_idx);
#if !defined(CONFIG_USER_ONLY)
    dc->user = (dc->current_el == 0);
#endif
    dc->fp_excp_el = EX_TBFLAG_ANY(tb_flags, FPEXC_EL);
    dc->align_mem = EX_TBFLAG_ANY(tb_flags, ALIGN_MEM);
    dc->pstate_il = EX_TBFLAG_ANY(tb_flags, PSTATE__IL);
    dc->fgt_active = EX_TBFLAG_ANY(tb_flags, FGT_ACTIVE);
    dc->fgt_svc = EX_TBFLAG_ANY(tb_flags, FGT_SVC);

    if (arm_feature(env, ARM_FEATURE_M)) {
        dc->vfp_enabled = 1;
        dc->be_data = MO_TE;
        dc->v7m_handler_mode = EX_TBFLAG_M32(tb_flags, HANDLER);
        dc->v8m_secure = EX_TBFLAG_M32(tb_flags, SECURE);
        dc->v8m_stackcheck = EX_TBFLAG_M32(tb_flags, STACKCHECK);
        dc->v8m_fpccr_s_wrong = EX_TBFLAG_M32(tb_flags, FPCCR_S_WRONG);
        dc->v7m_new_fp_ctxt_needed =
            EX_TBFLAG_M32(tb_flags, NEW_FP_CTXT_NEEDED);
        dc->v7m_lspact = EX_TBFLAG_M32(tb_flags, LSPACT);
        dc->mve_no_pred = EX_TBFLAG_M32(tb_flags, MVE_NO_PRED);
    } else {
        dc->sctlr_b = EX_TBFLAG_A32(tb_flags, SCTLR__B);
        dc->hstr_active = EX_TBFLAG_A32(tb_flags, HSTR_ACTIVE);
        dc->ns = EX_TBFLAG_A32(tb_flags, NS);
        dc->vfp_enabled = EX_TBFLAG_A32(tb_flags, VFPEN);
        if (arm_feature(env, ARM_FEATURE_XSCALE)) {
            dc->c15_cpar = EX_TBFLAG_A32(tb_flags, XSCALE_CPAR);
        } else {
            dc->vec_len = EX_TBFLAG_A32(tb_flags, VECLEN);
            dc->vec_stride = EX_TBFLAG_A32(tb_flags, VECSTRIDE);
        }
        dc->sme_trap_nonstreaming =
            EX_TBFLAG_A32(tb_flags, SME_TRAP_NONSTREAMING);
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
    dc->ss_active = EX_TBFLAG_ANY(tb_flags, SS_ACTIVE);
    dc->pstate_ss = EX_TBFLAG_ANY(tb_flags, PSTATE__SS);
    dc->is_ldex = false;

    dc->page_start = dc->base.pc_first & TARGET_PAGE_MASK;

    /* If architectural single step active, limit to 1.  */
    if (dc->ss_active) {
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
        store_cpu_field_constant(0, condexec_bits);
    }
}

static void arm_tr_insn_start(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    /*
     * The ECI/ICI bits share PSR bits with the IT bits, so we
     * need to reconstitute the bits from the split-out DisasContext
     * fields here.
     */
    uint32_t condexec_bits;
    target_ulong pc_arg = dc->base.pc_next;

    if (tb_cflags(dcbase->tb) & CF_PCREL) {
        pc_arg &= ~TARGET_PAGE_MASK;
    }
    if (dc->eci) {
        condexec_bits = dc->eci << 4;
    } else {
        condexec_bits = (dc->condexec_cond << 4) | (dc->condexec_mask >> 1);
    }
    tcg_gen_insn_start(pc_arg, condexec_bits, 0);
    dc->insn_start = tcg_last_op();
}

static bool arm_check_kernelpage(DisasContext *dc)
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
    return false;
}

static bool arm_check_ss_active(DisasContext *dc)
{
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
    if (dc->condjmp && dc->base.is_jmp == DISAS_NEXT) {
        if (dc->pc_save != dc->condlabel.pc_save) {
            gen_update_pc(dc, dc->condlabel.pc_save - dc->pc_save);
        }
        gen_set_label(dc->condlabel.label);
        dc->condjmp = 0;
    }
}

static void arm_tr_translate_insn(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    CPUARMState *env = cpu->env_ptr;
    uint32_t pc = dc->base.pc_next;
    unsigned int insn;

    /* Singlestep exceptions have the highest priority. */
    if (arm_check_ss_active(dc)) {
        dc->base.pc_next = pc + 4;
        return;
    }

    if (pc & 3) {
        /*
         * PC alignment fault.  This has priority over the instruction abort
         * that we would receive from a translation fault via arm_ldl_code
         * (or the execution of the kernelpage entrypoint). This should only
         * be possible after an indirect branch, at the start of the TB.
         */
        assert(dc->base.num_insns == 1);
        gen_helper_exception_pc_alignment(cpu_env, tcg_constant_tl(pc));
        dc->base.is_jmp = DISAS_NORETURN;
        dc->base.pc_next = QEMU_ALIGN_UP(pc, 4);
        return;
    }

    if (arm_check_kernelpage(dc)) {
        dc->base.pc_next = pc + 4;
        return;
    }

    dc->pc_curr = pc;
    insn = arm_ldl_code(env, &dc->base, pc, dc->sctlr_b);
    dc->insn = insn;
    dc->base.pc_next = pc + 4;
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
    uint32_t pc = dc->base.pc_next;
    uint32_t insn;
    bool is_16bit;
    /* TCG op to rewind to if this turns out to be an invalid ECI state */
    TCGOp *insn_eci_rewind = NULL;
    target_ulong insn_eci_pc_save = -1;

    /* Misaligned thumb PC is architecturally impossible. */
    assert((dc->base.pc_next & 1) == 0);

    if (arm_check_ss_active(dc) || arm_check_kernelpage(dc)) {
        dc->base.pc_next = pc + 2;
        return;
    }

    dc->pc_curr = pc;
    insn = arm_lduw_code(env, &dc->base, pc, dc->sctlr_b);
    is_16bit = thumb_insn_is_16bit(dc, dc->base.pc_next, insn);
    pc += 2;
    if (!is_16bit) {
        uint32_t insn2 = arm_lduw_code(env, &dc->base, pc, dc->sctlr_b);
        insn = insn << 16 | insn2;
        pc += 2;
    }
    dc->base.pc_next = pc;
    dc->insn = insn;

    if (dc->pstate_il) {
        /*
         * Illegal execution state. This has priority over BTI
         * exceptions, but comes after instruction abort exceptions.
         */
        gen_exception_insn(dc, 0, EXCP_UDEF, syn_illegalstate());
        return;
    }

    if (dc->eci) {
        /*
         * For M-profile continuable instructions, ECI/ICI handling
         * falls into these cases:
         *  - interrupt-continuable instructions
         *     These are the various load/store multiple insns (both
         *     integer and fp). The ICI bits indicate the register
         *     where the load/store can resume. We make the IMPDEF
         *     choice to always do "instruction restart", ie ignore
         *     the ICI value and always execute the ldm/stm from the
         *     start. So all we need to do is zero PSR.ICI if the
         *     insn executes.
         *  - MVE instructions subject to beat-wise execution
         *     Here the ECI bits indicate which beats have already been
         *     executed, and we must honour this. Each insn of this
         *     type will handle it correctly. We will update PSR.ECI
         *     in the helper function for the insn (some ECI values
         *     mean that the following insn also has been partially
         *     executed).
         *  - Special cases which don't advance ECI
         *     The insns LE, LETP and BKPT leave the ECI/ICI state
         *     bits untouched.
         *  - all other insns (the common case)
         *     Non-zero ECI/ICI means an INVSTATE UsageFault.
         *     We place a rewind-marker here. Insns in the previous
         *     three categories will set a flag in the DisasContext.
         *     If the flag isn't set after we call disas_thumb_insn()
         *     or disas_thumb2_insn() then we know we have a "some other
         *     insn" case. We will rewind to the marker (ie throwing away
         *     all the generated code) and instead emit "take exception".
         */
        insn_eci_rewind = tcg_last_op();
        insn_eci_pc_save = dc->pc_save;
    }

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

    if (dc->eci && !dc->eci_handled) {
        /*
         * Insn wasn't valid for ECI/ICI at all: undo what we
         * just generated and instead emit an exception
         */
        tcg_remove_ops_after(insn_eci_rewind);
        dc->pc_save = insn_eci_pc_save;
        dc->condjmp = 0;
        gen_exception_insn(dc, 0, EXCP_INVSTATE, syn_uncategorized());
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
    } else if (unlikely(dc->ss_active)) {
        /* Unconditional and "condition passed" instruction codepath. */
        switch (dc->base.is_jmp) {
        case DISAS_SWI:
            gen_ss_advance(dc);
            gen_exception(EXCP_SWI, syn_aa32_svc(dc->svc_imm, dc->thumb));
            break;
        case DISAS_HVC:
            gen_ss_advance(dc);
            gen_exception_el(EXCP_HVC, syn_aa32_hvc(dc->svc_imm), 2);
            break;
        case DISAS_SMC:
            gen_ss_advance(dc);
            gen_exception_el(EXCP_SMC, syn_aa32_smc(), 3);
            break;
        case DISAS_NEXT:
        case DISAS_TOO_MANY:
        case DISAS_UPDATE_EXIT:
        case DISAS_UPDATE_NOCHAIN:
            gen_update_pc(dc, curr_insn_len(dc));
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
        switch (dc->base.is_jmp) {
        case DISAS_NEXT:
        case DISAS_TOO_MANY:
            gen_goto_tb(dc, 1, curr_insn_len(dc));
            break;
        case DISAS_UPDATE_NOCHAIN:
            gen_update_pc(dc, curr_insn_len(dc));
            /* fall through */
        case DISAS_JUMP:
            gen_goto_ptr();
            break;
        case DISAS_UPDATE_EXIT:
            gen_update_pc(dc, curr_insn_len(dc));
            /* fall through */
        default:
            /* indicate that the hash table must be used to find the next TB */
            tcg_gen_exit_tb(NULL, 0);
            break;
        case DISAS_NORETURN:
            /* nothing more to generate */
            break;
        case DISAS_WFI:
            gen_helper_wfi(cpu_env, tcg_constant_i32(curr_insn_len(dc)));
            /*
             * The helper doesn't necessarily throw an exception, but we
             * must go back to the main loop to check for interrupts anyway.
             */
            tcg_gen_exit_tb(NULL, 0);
            break;
        case DISAS_WFE:
            gen_helper_wfe(cpu_env);
            break;
        case DISAS_YIELD:
            gen_helper_yield(cpu_env);
            break;
        case DISAS_SWI:
            gen_exception(EXCP_SWI, syn_aa32_svc(dc->svc_imm, dc->thumb));
            break;
        case DISAS_HVC:
            gen_exception_el(EXCP_HVC, syn_aa32_hvc(dc->svc_imm), 2);
            break;
        case DISAS_SMC:
            gen_exception_el(EXCP_SMC, syn_aa32_smc(), 3);
            break;
        }
    }

    if (dc->condjmp) {
        /* "Condition failed" instruction codepath for the branch/trap insn */
        set_disas_label(dc, dc->condlabel);
        gen_set_condexec(dc);
        if (unlikely(dc->ss_active)) {
            gen_update_pc(dc, curr_insn_len(dc));
            gen_singlestep_exception(dc);
        } else {
            gen_goto_tb(dc, 1, curr_insn_len(dc));
        }
    }
}

static void arm_tr_disas_log(const DisasContextBase *dcbase,
                             CPUState *cpu, FILE *logfile)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    fprintf(logfile, "IN: %s\n", lookup_symbol(dc->base.pc_first));
    target_disas(logfile, cpu, dc->base.pc_first, dc->base.tb->size);
}

static const TranslatorOps arm_translator_ops = {
    .init_disas_context = arm_tr_init_disas_context,
    .tb_start           = arm_tr_tb_start,
    .insn_start         = arm_tr_insn_start,
    .translate_insn     = arm_tr_translate_insn,
    .tb_stop            = arm_tr_tb_stop,
    .disas_log          = arm_tr_disas_log,
};

static const TranslatorOps thumb_translator_ops = {
    .init_disas_context = arm_tr_init_disas_context,
    .tb_start           = arm_tr_tb_start,
    .insn_start         = arm_tr_insn_start,
    .translate_insn     = thumb_tr_translate_insn,
    .tb_stop            = arm_tr_tb_stop,
    .disas_log          = arm_tr_disas_log,
};

/* generate intermediate code for basic block 'tb'.  */
void gen_intermediate_code(CPUState *cpu, TranslationBlock *tb, int *max_insns,
                           target_ulong pc, void *host_pc)
{
    DisasContext dc = { };
    const TranslatorOps *ops = &arm_translator_ops;
    CPUARMTBFlags tb_flags = arm_tbflags_from_tb(tb);

    if (EX_TBFLAG_AM32(tb_flags, THUMB)) {
        ops = &thumb_translator_ops;
    }
#ifdef TARGET_AARCH64
    if (EX_TBFLAG_ANY(tb_flags, AARCH64_STATE)) {
        ops = &aarch64_translator_ops;
    }
#endif

    translator_loop(cpu, tb, max_insns, pc, host_pc, ops, &dc.base);
}
