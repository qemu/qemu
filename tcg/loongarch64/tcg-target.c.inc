/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2021 WANG Xuerui <git@xen0n.name>
 *
 * Based on tcg/riscv/tcg-target.c.inc
 *
 * Copyright (c) 2018 SiFive, Inc
 * Copyright (c) 2008-2009 Arnaud Patard <arnaud.patard@rtp-net.org>
 * Copyright (c) 2009 Aurelien Jarno <aurelien@aurel32.net>
 * Copyright (c) 2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifdef CONFIG_DEBUG_TCG
static const char * const tcg_target_reg_names[TCG_TARGET_NB_REGS] = {
    "zero",
    "ra",
    "tp",
    "sp",
    "a0",
    "a1",
    "a2",
    "a3",
    "a4",
    "a5",
    "a6",
    "a7",
    "t0",
    "t1",
    "t2",
    "t3",
    "t4",
    "t5",
    "t6",
    "t7",
    "t8",
    "r21", /* reserved in the LP64* ABI, hence no ABI name */
    "s9",
    "s0",
    "s1",
    "s2",
    "s3",
    "s4",
    "s5",
    "s6",
    "s7",
    "s8"
};
#endif

static const int tcg_target_reg_alloc_order[] = {
    /* Registers preserved across calls */
    /* TCG_REG_S0 reserved for TCG_AREG0 */
    TCG_REG_S1,
    TCG_REG_S2,
    TCG_REG_S3,
    TCG_REG_S4,
    TCG_REG_S5,
    TCG_REG_S6,
    TCG_REG_S7,
    TCG_REG_S8,
    TCG_REG_S9,

    /* Registers (potentially) clobbered across calls */
    TCG_REG_T0,
    TCG_REG_T1,
    TCG_REG_T2,
    TCG_REG_T3,
    TCG_REG_T4,
    TCG_REG_T5,
    TCG_REG_T6,
    TCG_REG_T7,
    TCG_REG_T8,

    /* Argument registers, opposite order of allocation.  */
    TCG_REG_A7,
    TCG_REG_A6,
    TCG_REG_A5,
    TCG_REG_A4,
    TCG_REG_A3,
    TCG_REG_A2,
    TCG_REG_A1,
    TCG_REG_A0,
};

static const int tcg_target_call_iarg_regs[] = {
    TCG_REG_A0,
    TCG_REG_A1,
    TCG_REG_A2,
    TCG_REG_A3,
    TCG_REG_A4,
    TCG_REG_A5,
    TCG_REG_A6,
    TCG_REG_A7,
};

static const int tcg_target_call_oarg_regs[] = {
    TCG_REG_A0,
    TCG_REG_A1,
};

#define TCG_CT_CONST_ZERO  0x100
#define TCG_CT_CONST_S12   0x200
#define TCG_CT_CONST_N12   0x400
#define TCG_CT_CONST_U12   0x800
#define TCG_CT_CONST_C12   0x1000
#define TCG_CT_CONST_WSZ   0x2000

#define ALL_GENERAL_REGS      MAKE_64BIT_MASK(0, 32)
/*
 * For softmmu, we need to avoid conflicts with the first 5
 * argument registers to call the helper.  Some of these are
 * also used for the tlb lookup.
 */
#ifdef CONFIG_SOFTMMU
#define SOFTMMU_RESERVE_REGS  MAKE_64BIT_MASK(TCG_REG_A0, 5)
#else
#define SOFTMMU_RESERVE_REGS  0
#endif


static inline tcg_target_long sextreg(tcg_target_long val, int pos, int len)
{
    return sextract64(val, pos, len);
}

/* test if a constant matches the constraint */
static bool tcg_target_const_match(int64_t val, TCGType type, int ct)
{
    if (ct & TCG_CT_CONST) {
        return true;
    }
    if ((ct & TCG_CT_CONST_ZERO) && val == 0) {
        return true;
    }
    if ((ct & TCG_CT_CONST_S12) && val == sextreg(val, 0, 12)) {
        return true;
    }
    if ((ct & TCG_CT_CONST_N12) && -val == sextreg(-val, 0, 12)) {
        return true;
    }
    if ((ct & TCG_CT_CONST_U12) && val >= 0 && val <= 0xfff) {
        return true;
    }
    if ((ct & TCG_CT_CONST_C12) && ~val >= 0 && ~val <= 0xfff) {
        return true;
    }
    if ((ct & TCG_CT_CONST_WSZ) && val == (type == TCG_TYPE_I32 ? 32 : 64)) {
        return true;
    }
    return false;
}

/*
 * Relocations
 */

/*
 * Relocation records defined in LoongArch ELF psABI v1.00 is way too
 * complicated; a whopping stack machine is needed to stuff the fields, at
 * the very least one SOP_PUSH and one SOP_POP (of the correct format) are
 * needed.
 *
 * Hence, define our own simpler relocation types. Numbers are chosen as to
 * not collide with potential future additions to the true ELF relocation
 * type enum.
 */

/* Field Sk16, shifted right by 2; suitable for conditional jumps */
#define R_LOONGARCH_BR_SK16     256
/* Field Sd10k16, shifted right by 2; suitable for B and BL */
#define R_LOONGARCH_BR_SD10K16  257

static bool reloc_br_sk16(tcg_insn_unit *src_rw, const tcg_insn_unit *target)
{
    const tcg_insn_unit *src_rx = tcg_splitwx_to_rx(src_rw);
    intptr_t offset = (intptr_t)target - (intptr_t)src_rx;

    tcg_debug_assert((offset & 3) == 0);
    offset >>= 2;
    if (offset == sextreg(offset, 0, 16)) {
        *src_rw = deposit64(*src_rw, 10, 16, offset);
        return true;
    }

    return false;
}

static bool reloc_br_sd10k16(tcg_insn_unit *src_rw,
                             const tcg_insn_unit *target)
{
    const tcg_insn_unit *src_rx = tcg_splitwx_to_rx(src_rw);
    intptr_t offset = (intptr_t)target - (intptr_t)src_rx;

    tcg_debug_assert((offset & 3) == 0);
    offset >>= 2;
    if (offset == sextreg(offset, 0, 26)) {
        *src_rw = deposit64(*src_rw, 0, 10, offset >> 16); /* slot d10 */
        *src_rw = deposit64(*src_rw, 10, 16, offset); /* slot k16 */
        return true;
    }

    return false;
}

static bool patch_reloc(tcg_insn_unit *code_ptr, int type,
                        intptr_t value, intptr_t addend)
{
    tcg_debug_assert(addend == 0);
    switch (type) {
    case R_LOONGARCH_BR_SK16:
        return reloc_br_sk16(code_ptr, (tcg_insn_unit *)value);
    case R_LOONGARCH_BR_SD10K16:
        return reloc_br_sd10k16(code_ptr, (tcg_insn_unit *)value);
    default:
        g_assert_not_reached();
    }
}

#include "tcg-insn-defs.c.inc"

/*
 * TCG intrinsics
 */

static void tcg_out_mb(TCGContext *s, TCGArg a0)
{
    /* Baseline LoongArch only has the full barrier, unfortunately.  */
    tcg_out_opc_dbar(s, 0);
}

static bool tcg_out_mov(TCGContext *s, TCGType type, TCGReg ret, TCGReg arg)
{
    if (ret == arg) {
        return true;
    }
    switch (type) {
    case TCG_TYPE_I32:
    case TCG_TYPE_I64:
        /*
         * Conventional register-register move used in LoongArch is
         * `or dst, src, zero`.
         */
        tcg_out_opc_or(s, ret, arg, TCG_REG_ZERO);
        break;
    default:
        g_assert_not_reached();
    }
    return true;
}

static bool imm_part_needs_loading(bool high_bits_are_ones,
                                   tcg_target_long part)
{
    if (high_bits_are_ones) {
        return part != -1;
    } else {
        return part != 0;
    }
}

/* Loads a 32-bit immediate into rd, sign-extended.  */
static void tcg_out_movi_i32(TCGContext *s, TCGReg rd, int32_t val)
{
    tcg_target_long lo = sextreg(val, 0, 12);
    tcg_target_long hi12 = sextreg(val, 12, 20);

    /* Single-instruction cases.  */
    if (lo == val) {
        /* val fits in simm12: addi.w rd, zero, val */
        tcg_out_opc_addi_w(s, rd, TCG_REG_ZERO, val);
        return;
    }
    if (0x800 <= val && val <= 0xfff) {
        /* val fits in uimm12: ori rd, zero, val */
        tcg_out_opc_ori(s, rd, TCG_REG_ZERO, val);
        return;
    }

    /* High bits must be set; load with lu12i.w + optional ori.  */
    tcg_out_opc_lu12i_w(s, rd, hi12);
    if (lo != 0) {
        tcg_out_opc_ori(s, rd, rd, lo & 0xfff);
    }
}

static void tcg_out_movi(TCGContext *s, TCGType type, TCGReg rd,
                         tcg_target_long val)
{
    /*
     * LoongArch conventionally loads 64-bit immediates in at most 4 steps,
     * with dedicated instructions for filling the respective bitfields
     * below:
     *
     *        6                   5                   4               3
     *  3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2
     * +-----------------------+---------------------------------------+...
     * |          hi52         |                  hi32                 |
     * +-----------------------+---------------------------------------+...
     *       3                   2                   1
     *     1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
     * ...+-------------------------------------+-------------------------+
     *    |                 hi12                |            lo           |
     * ...+-------------------------------------+-------------------------+
     *
     * Check if val belong to one of the several fast cases, before falling
     * back to the slow path.
     */

    intptr_t pc_offset;
    tcg_target_long val_lo, val_hi, pc_hi, offset_hi;
    tcg_target_long hi32, hi52;
    bool rd_high_bits_are_ones;

    /* Value fits in signed i32.  */
    if (type == TCG_TYPE_I32 || val == (int32_t)val) {
        tcg_out_movi_i32(s, rd, val);
        return;
    }

    /* PC-relative cases.  */
    pc_offset = tcg_pcrel_diff(s, (void *)val);
    if (pc_offset == sextreg(pc_offset, 0, 22) && (pc_offset & 3) == 0) {
        /* Single pcaddu2i.  */
        tcg_out_opc_pcaddu2i(s, rd, pc_offset >> 2);
        return;
    }

    if (pc_offset == (int32_t)pc_offset) {
        /* Offset within 32 bits; load with pcalau12i + ori.  */
        val_lo = sextreg(val, 0, 12);
        val_hi = val >> 12;
        pc_hi = (val - pc_offset) >> 12;
        offset_hi = val_hi - pc_hi;

        tcg_debug_assert(offset_hi == sextreg(offset_hi, 0, 20));
        tcg_out_opc_pcalau12i(s, rd, offset_hi);
        if (val_lo != 0) {
            tcg_out_opc_ori(s, rd, rd, val_lo & 0xfff);
        }
        return;
    }

    hi32 = sextreg(val, 32, 20);
    hi52 = sextreg(val, 52, 12);

    /* Single cu52i.d case.  */
    if (ctz64(val) >= 52) {
        tcg_out_opc_cu52i_d(s, rd, TCG_REG_ZERO, hi52);
        return;
    }

    /* Slow path.  Initialize the low 32 bits, then concat high bits.  */
    tcg_out_movi_i32(s, rd, val);
    rd_high_bits_are_ones = (int32_t)val < 0;

    if (imm_part_needs_loading(rd_high_bits_are_ones, hi32)) {
        tcg_out_opc_cu32i_d(s, rd, hi32);
        rd_high_bits_are_ones = hi32 < 0;
    }

    if (imm_part_needs_loading(rd_high_bits_are_ones, hi52)) {
        tcg_out_opc_cu52i_d(s, rd, rd, hi52);
    }
}

static void tcg_out_ext8u(TCGContext *s, TCGReg ret, TCGReg arg)
{
    tcg_out_opc_andi(s, ret, arg, 0xff);
}

static void tcg_out_ext16u(TCGContext *s, TCGReg ret, TCGReg arg)
{
    tcg_out_opc_bstrpick_w(s, ret, arg, 0, 15);
}

static void tcg_out_ext32u(TCGContext *s, TCGReg ret, TCGReg arg)
{
    tcg_out_opc_bstrpick_d(s, ret, arg, 0, 31);
}

static void tcg_out_ext8s(TCGContext *s, TCGReg ret, TCGReg arg)
{
    tcg_out_opc_sext_b(s, ret, arg);
}

static void tcg_out_ext16s(TCGContext *s, TCGReg ret, TCGReg arg)
{
    tcg_out_opc_sext_h(s, ret, arg);
}

static void tcg_out_ext32s(TCGContext *s, TCGReg ret, TCGReg arg)
{
    tcg_out_opc_addi_w(s, ret, arg, 0);
}

static void tcg_out_clzctz(TCGContext *s, LoongArchInsn opc,
                           TCGReg a0, TCGReg a1, TCGReg a2,
                           bool c2, bool is_32bit)
{
    if (c2) {
        /*
         * Fast path: semantics already satisfied due to constraint and
         * insn behavior, single instruction is enough.
         */
        tcg_debug_assert(a2 == (is_32bit ? 32 : 64));
        /* all clz/ctz insns belong to DJ-format */
        tcg_out32(s, encode_dj_insn(opc, a0, a1));
        return;
    }

    tcg_out32(s, encode_dj_insn(opc, TCG_REG_TMP0, a1));
    /* a0 = a1 ? REG_TMP0 : a2 */
    tcg_out_opc_maskeqz(s, TCG_REG_TMP0, TCG_REG_TMP0, a1);
    tcg_out_opc_masknez(s, a0, a2, a1);
    tcg_out_opc_or(s, a0, TCG_REG_TMP0, a0);
}

/*
 * Branch helpers
 */

static const struct {
    LoongArchInsn op;
    bool swap;
} tcg_brcond_to_loongarch[] = {
    [TCG_COND_EQ] =  { OPC_BEQ,  false },
    [TCG_COND_NE] =  { OPC_BNE,  false },
    [TCG_COND_LT] =  { OPC_BGT,  true  },
    [TCG_COND_GE] =  { OPC_BLE,  true  },
    [TCG_COND_LE] =  { OPC_BLE,  false },
    [TCG_COND_GT] =  { OPC_BGT,  false },
    [TCG_COND_LTU] = { OPC_BGTU, true  },
    [TCG_COND_GEU] = { OPC_BLEU, true  },
    [TCG_COND_LEU] = { OPC_BLEU, false },
    [TCG_COND_GTU] = { OPC_BGTU, false }
};

static void tcg_out_brcond(TCGContext *s, TCGCond cond, TCGReg arg1,
                           TCGReg arg2, TCGLabel *l)
{
    LoongArchInsn op = tcg_brcond_to_loongarch[cond].op;

    tcg_debug_assert(op != 0);

    if (tcg_brcond_to_loongarch[cond].swap) {
        TCGReg t = arg1;
        arg1 = arg2;
        arg2 = t;
    }

    /* all conditional branch insns belong to DJSk16-format */
    tcg_out_reloc(s, s->code_ptr, R_LOONGARCH_BR_SK16, l, 0);
    tcg_out32(s, encode_djsk16_insn(op, arg1, arg2, 0));
}

/*
 * Entry-points
 */

static void tcg_out_op(TCGContext *s, TCGOpcode opc,
                       const TCGArg args[TCG_MAX_OP_ARGS],
                       const int const_args[TCG_MAX_OP_ARGS])
{
    TCGArg a0 = args[0];
    TCGArg a1 = args[1];
    TCGArg a2 = args[2];
    int c2 = const_args[2];

    switch (opc) {
    case INDEX_op_mb:
        tcg_out_mb(s, a0);
        break;

    case INDEX_op_goto_ptr:
        tcg_out_opc_jirl(s, TCG_REG_ZERO, a0, 0);
        break;

    case INDEX_op_br:
        tcg_out_reloc(s, s->code_ptr, R_LOONGARCH_BR_SD10K16, arg_label(a0),
                      0);
        tcg_out_opc_b(s, 0);
        break;

    case INDEX_op_brcond_i32:
    case INDEX_op_brcond_i64:
        tcg_out_brcond(s, a2, a0, a1, arg_label(args[3]));
        break;

    case INDEX_op_ext8s_i32:
    case INDEX_op_ext8s_i64:
        tcg_out_ext8s(s, a0, a1);
        break;

    case INDEX_op_ext8u_i32:
    case INDEX_op_ext8u_i64:
        tcg_out_ext8u(s, a0, a1);
        break;

    case INDEX_op_ext16s_i32:
    case INDEX_op_ext16s_i64:
        tcg_out_ext16s(s, a0, a1);
        break;

    case INDEX_op_ext16u_i32:
    case INDEX_op_ext16u_i64:
        tcg_out_ext16u(s, a0, a1);
        break;

    case INDEX_op_ext32u_i64:
    case INDEX_op_extu_i32_i64:
        tcg_out_ext32u(s, a0, a1);
        break;

    case INDEX_op_ext32s_i64:
    case INDEX_op_extrl_i64_i32:
    case INDEX_op_ext_i32_i64:
        tcg_out_ext32s(s, a0, a1);
        break;

    case INDEX_op_extrh_i64_i32:
        tcg_out_opc_srai_d(s, a0, a1, 32);
        break;

    case INDEX_op_not_i32:
    case INDEX_op_not_i64:
        tcg_out_opc_nor(s, a0, a1, TCG_REG_ZERO);
        break;

    case INDEX_op_nor_i32:
    case INDEX_op_nor_i64:
        if (c2) {
            tcg_out_opc_ori(s, a0, a1, a2);
            tcg_out_opc_nor(s, a0, a0, TCG_REG_ZERO);
        } else {
            tcg_out_opc_nor(s, a0, a1, a2);
        }
        break;

    case INDEX_op_andc_i32:
    case INDEX_op_andc_i64:
        if (c2) {
            /* guaranteed to fit due to constraint */
            tcg_out_opc_andi(s, a0, a1, ~a2);
        } else {
            tcg_out_opc_andn(s, a0, a1, a2);
        }
        break;

    case INDEX_op_orc_i32:
    case INDEX_op_orc_i64:
        if (c2) {
            /* guaranteed to fit due to constraint */
            tcg_out_opc_ori(s, a0, a1, ~a2);
        } else {
            tcg_out_opc_orn(s, a0, a1, a2);
        }
        break;

    case INDEX_op_and_i32:
    case INDEX_op_and_i64:
        if (c2) {
            tcg_out_opc_andi(s, a0, a1, a2);
        } else {
            tcg_out_opc_and(s, a0, a1, a2);
        }
        break;

    case INDEX_op_or_i32:
    case INDEX_op_or_i64:
        if (c2) {
            tcg_out_opc_ori(s, a0, a1, a2);
        } else {
            tcg_out_opc_or(s, a0, a1, a2);
        }
        break;

    case INDEX_op_xor_i32:
    case INDEX_op_xor_i64:
        if (c2) {
            tcg_out_opc_xori(s, a0, a1, a2);
        } else {
            tcg_out_opc_xor(s, a0, a1, a2);
        }
        break;

    case INDEX_op_extract_i32:
        tcg_out_opc_bstrpick_w(s, a0, a1, a2, a2 + args[3] - 1);
        break;
    case INDEX_op_extract_i64:
        tcg_out_opc_bstrpick_d(s, a0, a1, a2, a2 + args[3] - 1);
        break;

    case INDEX_op_deposit_i32:
        tcg_out_opc_bstrins_w(s, a0, a2, args[3], args[3] + args[4] - 1);
        break;
    case INDEX_op_deposit_i64:
        tcg_out_opc_bstrins_d(s, a0, a2, args[3], args[3] + args[4] - 1);
        break;

    case INDEX_op_bswap16_i32:
    case INDEX_op_bswap16_i64:
        tcg_out_opc_revb_2h(s, a0, a1);
        if (a2 & TCG_BSWAP_OS) {
            tcg_out_ext16s(s, a0, a0);
        } else if ((a2 & (TCG_BSWAP_IZ | TCG_BSWAP_OZ)) == TCG_BSWAP_OZ) {
            tcg_out_ext16u(s, a0, a0);
        }
        break;

    case INDEX_op_bswap32_i32:
        /* All 32-bit values are computed sign-extended in the register.  */
        a2 = TCG_BSWAP_OS;
        /* fallthrough */
    case INDEX_op_bswap32_i64:
        tcg_out_opc_revb_2w(s, a0, a1);
        if (a2 & TCG_BSWAP_OS) {
            tcg_out_ext32s(s, a0, a0);
        } else if ((a2 & (TCG_BSWAP_IZ | TCG_BSWAP_OZ)) == TCG_BSWAP_OZ) {
            tcg_out_ext32u(s, a0, a0);
        }
        break;

    case INDEX_op_bswap64_i64:
        tcg_out_opc_revb_d(s, a0, a1);
        break;

    case INDEX_op_clz_i32:
        tcg_out_clzctz(s, OPC_CLZ_W, a0, a1, a2, c2, true);
        break;
    case INDEX_op_clz_i64:
        tcg_out_clzctz(s, OPC_CLZ_D, a0, a1, a2, c2, false);
        break;

    case INDEX_op_ctz_i32:
        tcg_out_clzctz(s, OPC_CTZ_W, a0, a1, a2, c2, true);
        break;
    case INDEX_op_ctz_i64:
        tcg_out_clzctz(s, OPC_CTZ_D, a0, a1, a2, c2, false);
        break;

    case INDEX_op_shl_i32:
        if (c2) {
            tcg_out_opc_slli_w(s, a0, a1, a2 & 0x1f);
        } else {
            tcg_out_opc_sll_w(s, a0, a1, a2);
        }
        break;
    case INDEX_op_shl_i64:
        if (c2) {
            tcg_out_opc_slli_d(s, a0, a1, a2 & 0x3f);
        } else {
            tcg_out_opc_sll_d(s, a0, a1, a2);
        }
        break;

    case INDEX_op_shr_i32:
        if (c2) {
            tcg_out_opc_srli_w(s, a0, a1, a2 & 0x1f);
        } else {
            tcg_out_opc_srl_w(s, a0, a1, a2);
        }
        break;
    case INDEX_op_shr_i64:
        if (c2) {
            tcg_out_opc_srli_d(s, a0, a1, a2 & 0x3f);
        } else {
            tcg_out_opc_srl_d(s, a0, a1, a2);
        }
        break;

    case INDEX_op_sar_i32:
        if (c2) {
            tcg_out_opc_srai_w(s, a0, a1, a2 & 0x1f);
        } else {
            tcg_out_opc_sra_w(s, a0, a1, a2);
        }
        break;
    case INDEX_op_sar_i64:
        if (c2) {
            tcg_out_opc_srai_d(s, a0, a1, a2 & 0x3f);
        } else {
            tcg_out_opc_sra_d(s, a0, a1, a2);
        }
        break;

    case INDEX_op_rotl_i32:
        /* transform into equivalent rotr/rotri */
        if (c2) {
            tcg_out_opc_rotri_w(s, a0, a1, (32 - a2) & 0x1f);
        } else {
            tcg_out_opc_sub_w(s, TCG_REG_TMP0, TCG_REG_ZERO, a2);
            tcg_out_opc_rotr_w(s, a0, a1, TCG_REG_TMP0);
        }
        break;
    case INDEX_op_rotl_i64:
        /* transform into equivalent rotr/rotri */
        if (c2) {
            tcg_out_opc_rotri_d(s, a0, a1, (64 - a2) & 0x3f);
        } else {
            tcg_out_opc_sub_w(s, TCG_REG_TMP0, TCG_REG_ZERO, a2);
            tcg_out_opc_rotr_d(s, a0, a1, TCG_REG_TMP0);
        }
        break;

    case INDEX_op_rotr_i32:
        if (c2) {
            tcg_out_opc_rotri_w(s, a0, a1, a2 & 0x1f);
        } else {
            tcg_out_opc_rotr_w(s, a0, a1, a2);
        }
        break;
    case INDEX_op_rotr_i64:
        if (c2) {
            tcg_out_opc_rotri_d(s, a0, a1, a2 & 0x3f);
        } else {
            tcg_out_opc_rotr_d(s, a0, a1, a2);
        }
        break;

    case INDEX_op_add_i32:
        if (c2) {
            tcg_out_opc_addi_w(s, a0, a1, a2);
        } else {
            tcg_out_opc_add_w(s, a0, a1, a2);
        }
        break;
    case INDEX_op_add_i64:
        if (c2) {
            tcg_out_opc_addi_d(s, a0, a1, a2);
        } else {
            tcg_out_opc_add_d(s, a0, a1, a2);
        }
        break;

    case INDEX_op_sub_i32:
        if (c2) {
            tcg_out_opc_addi_w(s, a0, a1, -a2);
        } else {
            tcg_out_opc_sub_w(s, a0, a1, a2);
        }
        break;
    case INDEX_op_sub_i64:
        if (c2) {
            tcg_out_opc_addi_d(s, a0, a1, -a2);
        } else {
            tcg_out_opc_sub_d(s, a0, a1, a2);
        }
        break;

    case INDEX_op_mul_i32:
        tcg_out_opc_mul_w(s, a0, a1, a2);
        break;
    case INDEX_op_mul_i64:
        tcg_out_opc_mul_d(s, a0, a1, a2);
        break;

    case INDEX_op_mulsh_i32:
        tcg_out_opc_mulh_w(s, a0, a1, a2);
        break;
    case INDEX_op_mulsh_i64:
        tcg_out_opc_mulh_d(s, a0, a1, a2);
        break;

    case INDEX_op_muluh_i32:
        tcg_out_opc_mulh_wu(s, a0, a1, a2);
        break;
    case INDEX_op_muluh_i64:
        tcg_out_opc_mulh_du(s, a0, a1, a2);
        break;

    case INDEX_op_div_i32:
        tcg_out_opc_div_w(s, a0, a1, a2);
        break;
    case INDEX_op_div_i64:
        tcg_out_opc_div_d(s, a0, a1, a2);
        break;

    case INDEX_op_divu_i32:
        tcg_out_opc_div_wu(s, a0, a1, a2);
        break;
    case INDEX_op_divu_i64:
        tcg_out_opc_div_du(s, a0, a1, a2);
        break;

    case INDEX_op_rem_i32:
        tcg_out_opc_mod_w(s, a0, a1, a2);
        break;
    case INDEX_op_rem_i64:
        tcg_out_opc_mod_d(s, a0, a1, a2);
        break;

    case INDEX_op_remu_i32:
        tcg_out_opc_mod_wu(s, a0, a1, a2);
        break;
    case INDEX_op_remu_i64:
        tcg_out_opc_mod_du(s, a0, a1, a2);
        break;

    case INDEX_op_mov_i32:  /* Always emitted via tcg_out_mov.  */
    case INDEX_op_mov_i64:
    default:
        g_assert_not_reached();
    }
}

static TCGConstraintSetIndex tcg_target_op_def(TCGOpcode op)
{
    switch (op) {
    case INDEX_op_goto_ptr:
        return C_O0_I1(r);

    case INDEX_op_brcond_i32:
    case INDEX_op_brcond_i64:
        return C_O0_I2(rZ, rZ);

    case INDEX_op_ext8s_i32:
    case INDEX_op_ext8s_i64:
    case INDEX_op_ext8u_i32:
    case INDEX_op_ext8u_i64:
    case INDEX_op_ext16s_i32:
    case INDEX_op_ext16s_i64:
    case INDEX_op_ext16u_i32:
    case INDEX_op_ext16u_i64:
    case INDEX_op_ext32s_i64:
    case INDEX_op_ext32u_i64:
    case INDEX_op_extu_i32_i64:
    case INDEX_op_extrl_i64_i32:
    case INDEX_op_extrh_i64_i32:
    case INDEX_op_ext_i32_i64:
    case INDEX_op_not_i32:
    case INDEX_op_not_i64:
    case INDEX_op_extract_i32:
    case INDEX_op_extract_i64:
    case INDEX_op_bswap16_i32:
    case INDEX_op_bswap16_i64:
    case INDEX_op_bswap32_i32:
    case INDEX_op_bswap32_i64:
    case INDEX_op_bswap64_i64:
        return C_O1_I1(r, r);

    case INDEX_op_andc_i32:
    case INDEX_op_andc_i64:
    case INDEX_op_orc_i32:
    case INDEX_op_orc_i64:
        /*
         * LoongArch insns for these ops don't have reg-imm forms, but we
         * can express using andi/ori if ~constant satisfies
         * TCG_CT_CONST_U12.
         */
        return C_O1_I2(r, r, rC);

    case INDEX_op_shl_i32:
    case INDEX_op_shl_i64:
    case INDEX_op_shr_i32:
    case INDEX_op_shr_i64:
    case INDEX_op_sar_i32:
    case INDEX_op_sar_i64:
    case INDEX_op_rotl_i32:
    case INDEX_op_rotl_i64:
    case INDEX_op_rotr_i32:
    case INDEX_op_rotr_i64:
        return C_O1_I2(r, r, ri);

    case INDEX_op_add_i32:
    case INDEX_op_add_i64:
        return C_O1_I2(r, r, rI);

    case INDEX_op_and_i32:
    case INDEX_op_and_i64:
    case INDEX_op_nor_i32:
    case INDEX_op_nor_i64:
    case INDEX_op_or_i32:
    case INDEX_op_or_i64:
    case INDEX_op_xor_i32:
    case INDEX_op_xor_i64:
        /* LoongArch reg-imm bitops have their imms ZERO-extended */
        return C_O1_I2(r, r, rU);

    case INDEX_op_clz_i32:
    case INDEX_op_clz_i64:
    case INDEX_op_ctz_i32:
    case INDEX_op_ctz_i64:
        return C_O1_I2(r, r, rW);

    case INDEX_op_deposit_i32:
    case INDEX_op_deposit_i64:
        /* Must deposit into the same register as input */
        return C_O1_I2(r, 0, rZ);

    case INDEX_op_sub_i32:
    case INDEX_op_sub_i64:
        return C_O1_I2(r, rZ, rN);

    case INDEX_op_mul_i32:
    case INDEX_op_mul_i64:
    case INDEX_op_mulsh_i32:
    case INDEX_op_mulsh_i64:
    case INDEX_op_muluh_i32:
    case INDEX_op_muluh_i64:
    case INDEX_op_div_i32:
    case INDEX_op_div_i64:
    case INDEX_op_divu_i32:
    case INDEX_op_divu_i64:
    case INDEX_op_rem_i32:
    case INDEX_op_rem_i64:
    case INDEX_op_remu_i32:
    case INDEX_op_remu_i64:
        return C_O1_I2(r, rZ, rZ);

    default:
        g_assert_not_reached();
    }
}
