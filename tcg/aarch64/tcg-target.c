/*
 * Initial TCG Implementation for aarch64
 *
 * Copyright (c) 2013 Huawei Technologies Duesseldorf GmbH
 * Written by Claudio Fontana
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.
 *
 * See the COPYING file in the top-level directory for details.
 */

#include "qemu/bitops.h"

#ifndef NDEBUG
static const char * const tcg_target_reg_names[TCG_TARGET_NB_REGS] = {
    "%x0", "%x1", "%x2", "%x3", "%x4", "%x5", "%x6", "%x7",
    "%x8", "%x9", "%x10", "%x11", "%x12", "%x13", "%x14", "%x15",
    "%x16", "%x17", "%x18", "%x19", "%x20", "%x21", "%x22", "%x23",
    "%x24", "%x25", "%x26", "%x27", "%x28",
    "%fp", /* frame pointer */
    "%lr", /* link register */
    "%sp",  /* stack pointer */
};
#endif /* NDEBUG */

#ifdef TARGET_WORDS_BIGENDIAN
 #define TCG_LDST_BSWAP 1
#else
 #define TCG_LDST_BSWAP 0
#endif

static const int tcg_target_reg_alloc_order[] = {
    TCG_REG_X20, TCG_REG_X21, TCG_REG_X22, TCG_REG_X23,
    TCG_REG_X24, TCG_REG_X25, TCG_REG_X26, TCG_REG_X27,
    TCG_REG_X28, /* we will reserve this for GUEST_BASE if configured */

    TCG_REG_X9, TCG_REG_X10, TCG_REG_X11, TCG_REG_X12,
    TCG_REG_X13, TCG_REG_X14, TCG_REG_X15,
    TCG_REG_X16, TCG_REG_X17,

    TCG_REG_X18, TCG_REG_X19, /* will not use these, see tcg_target_init */

    TCG_REG_X0, TCG_REG_X1, TCG_REG_X2, TCG_REG_X3,
    TCG_REG_X4, TCG_REG_X5, TCG_REG_X6, TCG_REG_X7,

    TCG_REG_X8, /* will not use, see tcg_target_init */
};

static const int tcg_target_call_iarg_regs[8] = {
    TCG_REG_X0, TCG_REG_X1, TCG_REG_X2, TCG_REG_X3,
    TCG_REG_X4, TCG_REG_X5, TCG_REG_X6, TCG_REG_X7
};
static const int tcg_target_call_oarg_regs[1] = {
    TCG_REG_X0
};

#define TCG_REG_TMP TCG_REG_X8

#ifndef CONFIG_SOFTMMU
# if defined(CONFIG_USE_GUEST_BASE)
# define TCG_REG_GUEST_BASE TCG_REG_X28
# else
# define TCG_REG_GUEST_BASE TCG_REG_XZR
# endif
#endif

static inline void reloc_pc26(void *code_ptr, tcg_target_long target)
{
    tcg_target_long offset; uint32_t insn;
    offset = (target - (tcg_target_long)code_ptr) / 4;
    /* read instruction, mask away previous PC_REL26 parameter contents,
       set the proper offset, then write back the instruction. */
    insn = *(uint32_t *)code_ptr;
    insn = deposit32(insn, 0, 26, offset);
    *(uint32_t *)code_ptr = insn;
}

static inline void reloc_pc19(void *code_ptr, tcg_target_long target)
{
    tcg_target_long offset; uint32_t insn;
    offset = (target - (tcg_target_long)code_ptr) / 4;
    /* read instruction, mask away previous PC_REL19 parameter contents,
       set the proper offset, then write back the instruction. */
    insn = *(uint32_t *)code_ptr;
    insn = deposit32(insn, 5, 19, offset);
    *(uint32_t *)code_ptr = insn;
}

static inline void patch_reloc(uint8_t *code_ptr, int type,
                               tcg_target_long value, tcg_target_long addend)
{
    value += addend;

    switch (type) {
    case R_AARCH64_JUMP26:
    case R_AARCH64_CALL26:
        reloc_pc26(code_ptr, value);
        break;
    case R_AARCH64_CONDBR19:
        reloc_pc19(code_ptr, value);
        break;

    default:
        tcg_abort();
    }
}

/* parse target specific constraints */
static int target_parse_constraint(TCGArgConstraint *ct,
                                   const char **pct_str)
{
    const char *ct_str = *pct_str;

    switch (ct_str[0]) {
    case 'r':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, (1ULL << TCG_TARGET_NB_REGS) - 1);
        break;
    case 'l': /* qemu_ld / qemu_st address, data_reg */
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, (1ULL << TCG_TARGET_NB_REGS) - 1);
#ifdef CONFIG_SOFTMMU
        /* x0 and x1 will be overwritten when reading the tlb entry,
           and x2, and x3 for helper args, better to avoid using them. */
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_X0);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_X1);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_X2);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_X3);
#endif
        break;
    default:
        return -1;
    }

    ct_str++;
    *pct_str = ct_str;
    return 0;
}

static inline int tcg_target_const_match(tcg_target_long val,
                                         const TCGArgConstraint *arg_ct)
{
    int ct = arg_ct->ct;

    if (ct & TCG_CT_CONST) {
        return 1;
    }

    return 0;
}

enum aarch64_cond_code {
    COND_EQ = 0x0,
    COND_NE = 0x1,
    COND_CS = 0x2,     /* Unsigned greater or equal */
    COND_HS = COND_CS, /* ALIAS greater or equal */
    COND_CC = 0x3,     /* Unsigned less than */
    COND_LO = COND_CC, /* ALIAS Lower */
    COND_MI = 0x4,     /* Negative */
    COND_PL = 0x5,     /* Zero or greater */
    COND_VS = 0x6,     /* Overflow */
    COND_VC = 0x7,     /* No overflow */
    COND_HI = 0x8,     /* Unsigned greater than */
    COND_LS = 0x9,     /* Unsigned less or equal */
    COND_GE = 0xa,
    COND_LT = 0xb,
    COND_GT = 0xc,
    COND_LE = 0xd,
    COND_AL = 0xe,
    COND_NV = 0xf, /* behaves like COND_AL here */
};

static const enum aarch64_cond_code tcg_cond_to_aarch64[] = {
    [TCG_COND_EQ] = COND_EQ,
    [TCG_COND_NE] = COND_NE,
    [TCG_COND_LT] = COND_LT,
    [TCG_COND_GE] = COND_GE,
    [TCG_COND_LE] = COND_LE,
    [TCG_COND_GT] = COND_GT,
    /* unsigned */
    [TCG_COND_LTU] = COND_LO,
    [TCG_COND_GTU] = COND_HI,
    [TCG_COND_GEU] = COND_HS,
    [TCG_COND_LEU] = COND_LS,
};

/* opcodes for LDR / STR instructions with base + simm9 addressing */
enum aarch64_ldst_op_data { /* size of the data moved */
    LDST_8 = 0x38,
    LDST_16 = 0x78,
    LDST_32 = 0xb8,
    LDST_64 = 0xf8,
};
enum aarch64_ldst_op_type { /* type of operation */
    LDST_ST = 0x0,    /* store */
    LDST_LD = 0x4,    /* load */
    LDST_LD_S_X = 0x8,  /* load and sign-extend into Xt */
    LDST_LD_S_W = 0xc,  /* load and sign-extend into Wt */
};

enum aarch64_arith_opc {
    ARITH_AND = 0x0a,
    ARITH_ADD = 0x0b,
    ARITH_OR = 0x2a,
    ARITH_ADDS = 0x2b,
    ARITH_XOR = 0x4a,
    ARITH_SUB = 0x4b,
    ARITH_ANDS = 0x6a,
    ARITH_SUBS = 0x6b,
};

enum aarch64_srr_opc {
    SRR_SHL = 0x0,
    SRR_SHR = 0x4,
    SRR_SAR = 0x8,
    SRR_ROR = 0xc
};

static inline enum aarch64_ldst_op_data
aarch64_ldst_get_data(TCGOpcode tcg_op)
{
    switch (tcg_op) {
    case INDEX_op_ld8u_i32:
    case INDEX_op_ld8s_i32:
    case INDEX_op_ld8u_i64:
    case INDEX_op_ld8s_i64:
    case INDEX_op_st8_i32:
    case INDEX_op_st8_i64:
        return LDST_8;

    case INDEX_op_ld16u_i32:
    case INDEX_op_ld16s_i32:
    case INDEX_op_ld16u_i64:
    case INDEX_op_ld16s_i64:
    case INDEX_op_st16_i32:
    case INDEX_op_st16_i64:
        return LDST_16;

    case INDEX_op_ld_i32:
    case INDEX_op_st_i32:
    case INDEX_op_ld32u_i64:
    case INDEX_op_ld32s_i64:
    case INDEX_op_st32_i64:
        return LDST_32;

    case INDEX_op_ld_i64:
    case INDEX_op_st_i64:
        return LDST_64;

    default:
        tcg_abort();
    }
}

static inline enum aarch64_ldst_op_type
aarch64_ldst_get_type(TCGOpcode tcg_op)
{
    switch (tcg_op) {
    case INDEX_op_st8_i32:
    case INDEX_op_st16_i32:
    case INDEX_op_st8_i64:
    case INDEX_op_st16_i64:
    case INDEX_op_st_i32:
    case INDEX_op_st32_i64:
    case INDEX_op_st_i64:
        return LDST_ST;

    case INDEX_op_ld8u_i32:
    case INDEX_op_ld16u_i32:
    case INDEX_op_ld8u_i64:
    case INDEX_op_ld16u_i64:
    case INDEX_op_ld_i32:
    case INDEX_op_ld32u_i64:
    case INDEX_op_ld_i64:
        return LDST_LD;

    case INDEX_op_ld8s_i32:
    case INDEX_op_ld16s_i32:
        return LDST_LD_S_W;

    case INDEX_op_ld8s_i64:
    case INDEX_op_ld16s_i64:
    case INDEX_op_ld32s_i64:
        return LDST_LD_S_X;

    default:
        tcg_abort();
    }
}

static inline uint32_t tcg_in32(TCGContext *s)
{
    uint32_t v = *(uint32_t *)s->code_ptr;
    return v;
}

static inline void tcg_out_ldst_9(TCGContext *s,
                                  enum aarch64_ldst_op_data op_data,
                                  enum aarch64_ldst_op_type op_type,
                                  TCGReg rd, TCGReg rn, tcg_target_long offset)
{
    /* use LDUR with BASE register with 9bit signed unscaled offset */
    unsigned int mod, off;

    if (offset < 0) {
        off = (256 + offset);
        mod = 0x1;
    } else {
        off = offset;
        mod = 0x0;
    }

    mod |= op_type;
    tcg_out32(s, op_data << 24 | mod << 20 | off << 12 | rn << 5 | rd);
}

static inline void tcg_out_movr(TCGContext *s, int ext, TCGReg rd, TCGReg src)
{
    /* register to register move using MOV (shifted register with no shift) */
    /* using MOV 0x2a0003e0 | (shift).. */
    unsigned int base = ext ? 0xaa0003e0 : 0x2a0003e0;
    tcg_out32(s, base | src << 16 | rd);
}

static inline void tcg_out_movi_aux(TCGContext *s,
                                    TCGReg rd, uint64_t value)
{
    uint32_t half, base, shift, movk = 0;
    /* construct halfwords of the immediate with MOVZ/MOVK with LSL */
    /* using MOVZ 0x52800000 | extended reg.. */
    base = (value > 0xffffffff) ? 0xd2800000 : 0x52800000;
    /* count trailing zeros in 16 bit steps, mapping 64 to 0. Emit the
       first MOVZ with the half-word immediate skipping the zeros, with a shift
       (LSL) equal to this number. Then morph all next instructions into MOVKs.
       Zero the processed half-word in the value, continue until empty.
       We build the final result 16bits at a time with up to 4 instructions,
       but do not emit instructions for 16bit zero holes. */
    do {
        shift = ctz64(value) & (63 & -16);
        half = (value >> shift) & 0xffff;
        tcg_out32(s, base | movk | shift << 17 | half << 5 | rd);
        movk = 0x20000000; /* morph next MOVZs into MOVKs */
        value &= ~(0xffffUL << shift);
    } while (value);
}

static inline void tcg_out_movi(TCGContext *s, TCGType type,
                                TCGReg rd, tcg_target_long value)
{
    if (type == TCG_TYPE_I64) {
        tcg_out_movi_aux(s, rd, value);
    } else {
        tcg_out_movi_aux(s, rd, value & 0xffffffff);
    }
}

static inline void tcg_out_ldst_r(TCGContext *s,
                                  enum aarch64_ldst_op_data op_data,
                                  enum aarch64_ldst_op_type op_type,
                                  TCGReg rd, TCGReg base, TCGReg regoff)
{
    /* load from memory to register using base + 64bit register offset */
    /* using f.e. STR Wt, [Xn, Xm] 0xb8600800|(regoff << 16)|(base << 5)|rd */
    /* the 0x6000 is for the "no extend field" */
    tcg_out32(s, 0x00206800
              | op_data << 24 | op_type << 20 | regoff << 16 | base << 5 | rd);
}

/* solve the whole ldst problem */
static inline void tcg_out_ldst(TCGContext *s, enum aarch64_ldst_op_data data,
                                enum aarch64_ldst_op_type type,
                                TCGReg rd, TCGReg rn, tcg_target_long offset)
{
    if (offset >= -256 && offset < 256) {
        tcg_out_ldst_9(s, data, type, rd, rn, offset);
    } else {
        tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_TMP, offset);
        tcg_out_ldst_r(s, data, type, rd, rn, TCG_REG_TMP);
    }
}

/* mov alias implemented with add immediate, useful to move to/from SP */
static inline void tcg_out_movr_sp(TCGContext *s, int ext, TCGReg rd, TCGReg rn)
{
    /* using ADD 0x11000000 | (ext) | rn << 5 | rd */
    unsigned int base = ext ? 0x91000000 : 0x11000000;
    tcg_out32(s, base | rn << 5 | rd);
}

static inline void tcg_out_mov(TCGContext *s,
                               TCGType type, TCGReg ret, TCGReg arg)
{
    if (ret != arg) {
        tcg_out_movr(s, type == TCG_TYPE_I64, ret, arg);
    }
}

static inline void tcg_out_ld(TCGContext *s, TCGType type, TCGReg arg,
                              TCGReg arg1, tcg_target_long arg2)
{
    tcg_out_ldst(s, (type == TCG_TYPE_I64) ? LDST_64 : LDST_32, LDST_LD,
                 arg, arg1, arg2);
}

static inline void tcg_out_st(TCGContext *s, TCGType type, TCGReg arg,
                              TCGReg arg1, tcg_target_long arg2)
{
    tcg_out_ldst(s, (type == TCG_TYPE_I64) ? LDST_64 : LDST_32, LDST_ST,
                 arg, arg1, arg2);
}

static inline void tcg_out_arith(TCGContext *s, enum aarch64_arith_opc opc,
                                 int ext, TCGReg rd, TCGReg rn, TCGReg rm,
                                 int shift_imm)
{
    /* Using shifted register arithmetic operations */
    /* if extended register operation (64bit) just OR with 0x80 << 24 */
    unsigned int shift, base = ext ? (0x80 | opc) << 24 : opc << 24;
    if (shift_imm == 0) {
        shift = 0;
    } else if (shift_imm > 0) {
        shift = shift_imm << 10 | 1 << 22;
    } else /* (shift_imm < 0) */ {
        shift = (-shift_imm) << 10;
    }
    tcg_out32(s, base | rm << 16 | shift | rn << 5 | rd);
}

static inline void tcg_out_mul(TCGContext *s, int ext,
                               TCGReg rd, TCGReg rn, TCGReg rm)
{
    /* Using MADD 0x1b000000 with Ra = wzr alias MUL 0x1b007c00 */
    unsigned int base = ext ? 0x9b007c00 : 0x1b007c00;
    tcg_out32(s, base | rm << 16 | rn << 5 | rd);
}

static inline void tcg_out_shiftrot_reg(TCGContext *s,
                                        enum aarch64_srr_opc opc, int ext,
                                        TCGReg rd, TCGReg rn, TCGReg rm)
{
    /* using 2-source data processing instructions 0x1ac02000 */
    unsigned int base = ext ? 0x9ac02000 : 0x1ac02000;
    tcg_out32(s, base | rm << 16 | opc << 8 | rn << 5 | rd);
}

static inline void tcg_out_ubfm(TCGContext *s, int ext, TCGReg rd, TCGReg rn,
                                unsigned int a, unsigned int b)
{
    /* Using UBFM 0x53000000 Wd, Wn, a, b */
    unsigned int base = ext ? 0xd3400000 : 0x53000000;
    tcg_out32(s, base | a << 16 | b << 10 | rn << 5 | rd);
}

static inline void tcg_out_sbfm(TCGContext *s, int ext, TCGReg rd, TCGReg rn,
                                unsigned int a, unsigned int b)
{
    /* Using SBFM 0x13000000 Wd, Wn, a, b */
    unsigned int base = ext ? 0x93400000 : 0x13000000;
    tcg_out32(s, base | a << 16 | b << 10 | rn << 5 | rd);
}

static inline void tcg_out_extr(TCGContext *s, int ext, TCGReg rd,
                                TCGReg rn, TCGReg rm, unsigned int a)
{
    /* Using EXTR 0x13800000 Wd, Wn, Wm, a */
    unsigned int base = ext ? 0x93c00000 : 0x13800000;
    tcg_out32(s, base | rm << 16 | a << 10 | rn << 5 | rd);
}

static inline void tcg_out_shl(TCGContext *s, int ext,
                               TCGReg rd, TCGReg rn, unsigned int m)
{
    int bits, max;
    bits = ext ? 64 : 32;
    max = bits - 1;
    tcg_out_ubfm(s, ext, rd, rn, bits - (m & max), max - (m & max));
}

static inline void tcg_out_shr(TCGContext *s, int ext,
                               TCGReg rd, TCGReg rn, unsigned int m)
{
    int max = ext ? 63 : 31;
    tcg_out_ubfm(s, ext, rd, rn, m & max, max);
}

static inline void tcg_out_sar(TCGContext *s, int ext,
                               TCGReg rd, TCGReg rn, unsigned int m)
{
    int max = ext ? 63 : 31;
    tcg_out_sbfm(s, ext, rd, rn, m & max, max);
}

static inline void tcg_out_rotr(TCGContext *s, int ext,
                                TCGReg rd, TCGReg rn, unsigned int m)
{
    int max = ext ? 63 : 31;
    tcg_out_extr(s, ext, rd, rn, rn, m & max);
}

static inline void tcg_out_rotl(TCGContext *s, int ext,
                                TCGReg rd, TCGReg rn, unsigned int m)
{
    int bits, max;
    bits = ext ? 64 : 32;
    max = bits - 1;
    tcg_out_extr(s, ext, rd, rn, rn, bits - (m & max));
}

static inline void tcg_out_cmp(TCGContext *s, int ext, TCGReg rn, TCGReg rm,
                               int shift_imm)
{
    /* Using CMP alias SUBS wzr, Wn, Wm */
    tcg_out_arith(s, ARITH_SUBS, ext, TCG_REG_XZR, rn, rm, shift_imm);
}

static inline void tcg_out_cset(TCGContext *s, int ext, TCGReg rd, TCGCond c)
{
    /* Using CSET alias of CSINC 0x1a800400 Xd, XZR, XZR, invert(cond) */
    unsigned int base = ext ? 0x9a9f07e0 : 0x1a9f07e0;
    tcg_out32(s, base | tcg_cond_to_aarch64[tcg_invert_cond(c)] << 12 | rd);
}

static inline void tcg_out_goto(TCGContext *s, tcg_target_long target)
{
    tcg_target_long offset;
    offset = (target - (tcg_target_long)s->code_ptr) / 4;

    if (offset < -0x02000000 || offset >= 0x02000000) {
        /* out of 26bit range */
        tcg_abort();
    }

    tcg_out32(s, 0x14000000 | (offset & 0x03ffffff));
}

static inline void tcg_out_goto_noaddr(TCGContext *s)
{
    /* We pay attention here to not modify the branch target by
       reading from the buffer. This ensure that caches and memory are
       kept coherent during retranslation.
       Mask away possible garbage in the high bits for the first translation,
       while keeping the offset bits for retranslation. */
    uint32_t insn;
    insn = (tcg_in32(s) & 0x03ffffff) | 0x14000000;
    tcg_out32(s, insn);
}

static inline void tcg_out_goto_cond_noaddr(TCGContext *s, TCGCond c)
{
    /* see comments in tcg_out_goto_noaddr */
    uint32_t insn;
    insn = tcg_in32(s) & (0x07ffff << 5);
    insn |= 0x54000000 | tcg_cond_to_aarch64[c];
    tcg_out32(s, insn);
}

static inline void tcg_out_goto_cond(TCGContext *s, TCGCond c,
                                     tcg_target_long target)
{
    tcg_target_long offset;
    offset = (target - (tcg_target_long)s->code_ptr) / 4;

    if (offset < -0x40000 || offset >= 0x40000) {
        /* out of 19bit range */
        tcg_abort();
    }

    offset &= 0x7ffff;
    tcg_out32(s, 0x54000000 | tcg_cond_to_aarch64[c] | offset << 5);
}

static inline void tcg_out_callr(TCGContext *s, TCGReg reg)
{
    tcg_out32(s, 0xd63f0000 | reg << 5);
}

static inline void tcg_out_gotor(TCGContext *s, TCGReg reg)
{
    tcg_out32(s, 0xd61f0000 | reg << 5);
}

static inline void tcg_out_call(TCGContext *s, tcg_target_long target)
{
    tcg_target_long offset;

    offset = (target - (tcg_target_long)s->code_ptr) / 4;

    if (offset < -0x02000000 || offset >= 0x02000000) { /* out of 26bit rng */
        tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_TMP, target);
        tcg_out_callr(s, TCG_REG_TMP);
    } else {
        tcg_out32(s, 0x94000000 | (offset & 0x03ffffff));
    }
}

/* encode a logical immediate, mapping user parameter
   M=set bits pattern length to S=M-1 */
static inline unsigned int
aarch64_limm(unsigned int m, unsigned int r)
{
    assert(m > 0);
    return r << 16 | (m - 1) << 10;
}

/* test a register against an immediate bit pattern made of
   M set bits rotated right by R.
   Examples:
   to test a 32/64 reg against 0x00000007, pass M = 3,  R = 0.
   to test a 32/64 reg against 0x000000ff, pass M = 8,  R = 0.
   to test a 32bit reg against 0xff000000, pass M = 8,  R = 8.
   to test a 32bit reg against 0xff0000ff, pass M = 16, R = 8.
 */
static inline void tcg_out_tst(TCGContext *s, int ext, TCGReg rn,
                               unsigned int m, unsigned int r)
{
    /* using TST alias of ANDS XZR, Xn,#bimm64 0x7200001f */
    unsigned int base = ext ? 0xf240001f : 0x7200001f;
    tcg_out32(s, base | aarch64_limm(m, r) | rn << 5);
}

/* and a register with a bit pattern, similarly to TST, no flags change */
static inline void tcg_out_andi(TCGContext *s, int ext, TCGReg rd, TCGReg rn,
                                unsigned int m, unsigned int r)
{
    /* using AND 0x12000000 */
    unsigned int base = ext ? 0x92400000 : 0x12000000;
    tcg_out32(s, base | aarch64_limm(m, r) | rn << 5 | rd);
}

static inline void tcg_out_ret(TCGContext *s)
{
    /* emit RET { LR } */
    tcg_out32(s, 0xd65f03c0);
}

void aarch64_tb_set_jmp_target(uintptr_t jmp_addr, uintptr_t addr)
{
    tcg_target_long target, offset;
    target = (tcg_target_long)addr;
    offset = (target - (tcg_target_long)jmp_addr) / 4;

    if (offset < -0x02000000 || offset >= 0x02000000) {
        /* out of 26bit range */
        tcg_abort();
    }

    patch_reloc((uint8_t *)jmp_addr, R_AARCH64_JUMP26, target, 0);
    flush_icache_range(jmp_addr, jmp_addr + 4);
}

static inline void tcg_out_goto_label(TCGContext *s, int label_index)
{
    TCGLabel *l = &s->labels[label_index];

    if (!l->has_value) {
        tcg_out_reloc(s, s->code_ptr, R_AARCH64_JUMP26, label_index, 0);
        tcg_out_goto_noaddr(s);
    } else {
        tcg_out_goto(s, l->u.value);
    }
}

static inline void tcg_out_goto_label_cond(TCGContext *s,
                                           TCGCond c, int label_index)
{
    TCGLabel *l = &s->labels[label_index];

    if (!l->has_value) {
        tcg_out_reloc(s, s->code_ptr, R_AARCH64_CONDBR19, label_index, 0);
        tcg_out_goto_cond_noaddr(s, c);
    } else {
        tcg_out_goto_cond(s, c, l->u.value);
    }
}

static inline void tcg_out_rev(TCGContext *s, int ext, TCGReg rd, TCGReg rm)
{
    /* using REV 0x5ac00800 */
    unsigned int base = ext ? 0xdac00c00 : 0x5ac00800;
    tcg_out32(s, base | rm << 5 | rd);
}

static inline void tcg_out_rev16(TCGContext *s, int ext, TCGReg rd, TCGReg rm)
{
    /* using REV16 0x5ac00400 */
    unsigned int base = ext ? 0xdac00400 : 0x5ac00400;
    tcg_out32(s, base | rm << 5 | rd);
}

static inline void tcg_out_sxt(TCGContext *s, int ext, int s_bits,
                               TCGReg rd, TCGReg rn)
{
    /* using ALIASes SXTB 0x13001c00, SXTH 0x13003c00, SXTW 0x93407c00
       of SBFM Xd, Xn, #0, #7|15|31 */
    int bits = 8 * (1 << s_bits) - 1;
    tcg_out_sbfm(s, ext, rd, rn, 0, bits);
}

static inline void tcg_out_uxt(TCGContext *s, int s_bits,
                               TCGReg rd, TCGReg rn)
{
    /* using ALIASes UXTB 0x53001c00, UXTH 0x53003c00
       of UBFM Wd, Wn, #0, #7|15 */
    int bits = 8 * (1 << s_bits) - 1;
    tcg_out_ubfm(s, 0, rd, rn, 0, bits);
}

#ifdef CONFIG_SOFTMMU
#include "exec/softmmu_defs.h"

/* helper signature: helper_ld_mmu(CPUState *env, target_ulong addr,
   int mmu_idx) */
static const void * const qemu_ld_helpers[4] = {
    helper_ldb_mmu,
    helper_ldw_mmu,
    helper_ldl_mmu,
    helper_ldq_mmu,
};

/* helper signature: helper_st_mmu(CPUState *env, target_ulong addr,
   uintxx_t val, int mmu_idx) */
static const void * const qemu_st_helpers[4] = {
    helper_stb_mmu,
    helper_stw_mmu,
    helper_stl_mmu,
    helper_stq_mmu,
};

#else /* !CONFIG_SOFTMMU */

static void tcg_out_qemu_ld_direct(TCGContext *s, int opc, TCGReg data_r,
                                   TCGReg addr_r, TCGReg off_r)
{
    switch (opc) {
    case 0:
        tcg_out_ldst_r(s, LDST_8, LDST_LD, data_r, addr_r, off_r);
        break;
    case 0 | 4:
        tcg_out_ldst_r(s, LDST_8, LDST_LD_S_X, data_r, addr_r, off_r);
        break;
    case 1:
        tcg_out_ldst_r(s, LDST_16, LDST_LD, data_r, addr_r, off_r);
        if (TCG_LDST_BSWAP) {
            tcg_out_rev16(s, 0, data_r, data_r);
        }
        break;
    case 1 | 4:
        if (TCG_LDST_BSWAP) {
            tcg_out_ldst_r(s, LDST_16, LDST_LD, data_r, addr_r, off_r);
            tcg_out_rev16(s, 0, data_r, data_r);
            tcg_out_sxt(s, 1, 1, data_r, data_r);
        } else {
            tcg_out_ldst_r(s, LDST_16, LDST_LD_S_X, data_r, addr_r, off_r);
        }
        break;
    case 2:
        tcg_out_ldst_r(s, LDST_32, LDST_LD, data_r, addr_r, off_r);
        if (TCG_LDST_BSWAP) {
            tcg_out_rev(s, 0, data_r, data_r);
        }
        break;
    case 2 | 4:
        if (TCG_LDST_BSWAP) {
            tcg_out_ldst_r(s, LDST_32, LDST_LD, data_r, addr_r, off_r);
            tcg_out_rev(s, 0, data_r, data_r);
            tcg_out_sxt(s, 1, 2, data_r, data_r);
        } else {
            tcg_out_ldst_r(s, LDST_32, LDST_LD_S_X, data_r, addr_r, off_r);
        }
        break;
    case 3:
        tcg_out_ldst_r(s, LDST_64, LDST_LD, data_r, addr_r, off_r);
        if (TCG_LDST_BSWAP) {
            tcg_out_rev(s, 1, data_r, data_r);
        }
        break;
    default:
        tcg_abort();
    }
}

static void tcg_out_qemu_st_direct(TCGContext *s, int opc, TCGReg data_r,
                                   TCGReg addr_r, TCGReg off_r)
{
    switch (opc) {
    case 0:
        tcg_out_ldst_r(s, LDST_8, LDST_ST, data_r, addr_r, off_r);
        break;
    case 1:
        if (TCG_LDST_BSWAP) {
            tcg_out_rev16(s, 0, TCG_REG_TMP, data_r);
            tcg_out_ldst_r(s, LDST_16, LDST_ST, TCG_REG_TMP, addr_r, off_r);
        } else {
            tcg_out_ldst_r(s, LDST_16, LDST_ST, data_r, addr_r, off_r);
        }
        break;
    case 2:
        if (TCG_LDST_BSWAP) {
            tcg_out_rev(s, 0, TCG_REG_TMP, data_r);
            tcg_out_ldst_r(s, LDST_32, LDST_ST, TCG_REG_TMP, addr_r, off_r);
        } else {
            tcg_out_ldst_r(s, LDST_32, LDST_ST, data_r, addr_r, off_r);
        }
        break;
    case 3:
        if (TCG_LDST_BSWAP) {
            tcg_out_rev(s, 1, TCG_REG_TMP, data_r);
            tcg_out_ldst_r(s, LDST_64, LDST_ST, TCG_REG_TMP, addr_r, off_r);
        } else {
            tcg_out_ldst_r(s, LDST_64, LDST_ST, data_r, addr_r, off_r);
        }
        break;
    default:
        tcg_abort();
    }
}
#endif /* CONFIG_SOFTMMU */

static void tcg_out_qemu_ld(TCGContext *s, const TCGArg *args, int opc)
{
    TCGReg addr_reg, data_reg;
#ifdef CONFIG_SOFTMMU
    int mem_index, s_bits;
#endif
    data_reg = args[0];
    addr_reg = args[1];

#ifdef CONFIG_SOFTMMU
    mem_index = args[2];
    s_bits = opc & 3;

    /* TODO: insert TLB lookup here */

    /* all arguments passed via registers */
    tcg_out_movr(s, 1, TCG_REG_X0, TCG_AREG0);
    tcg_out_movr(s, (TARGET_LONG_BITS == 64), TCG_REG_X1, addr_reg);
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_X2, mem_index);
    tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_TMP,
                 (tcg_target_long)qemu_ld_helpers[s_bits]);
    tcg_out_callr(s, TCG_REG_TMP);

    if (opc & 0x04) { /* sign extend */
        tcg_out_sxt(s, 1, s_bits, data_reg, TCG_REG_X0);
    } else {
        tcg_out_movr(s, 1, data_reg, TCG_REG_X0);
    }

#else /* !CONFIG_SOFTMMU */
    tcg_out_qemu_ld_direct(s, opc, data_reg, addr_reg,
                           GUEST_BASE ? TCG_REG_GUEST_BASE : TCG_REG_XZR);
#endif /* CONFIG_SOFTMMU */
}

static void tcg_out_qemu_st(TCGContext *s, const TCGArg *args, int opc)
{
    TCGReg addr_reg, data_reg;
#ifdef CONFIG_SOFTMMU
    int mem_index, s_bits;
#endif
    data_reg = args[0];
    addr_reg = args[1];

#ifdef CONFIG_SOFTMMU
    mem_index = args[2];
    s_bits = opc & 3;

    /* TODO: insert TLB lookup here */

    /* all arguments passed via registers */
    tcg_out_movr(s, 1, TCG_REG_X0, TCG_AREG0);
    tcg_out_movr(s, (TARGET_LONG_BITS == 64), TCG_REG_X1, addr_reg);
    tcg_out_movr(s, 1, TCG_REG_X2, data_reg);
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_X3, mem_index);
    tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_TMP,
                 (tcg_target_long)qemu_st_helpers[s_bits]);
    tcg_out_callr(s, TCG_REG_TMP);

#else /* !CONFIG_SOFTMMU */
    tcg_out_qemu_st_direct(s, opc, data_reg, addr_reg,
                           GUEST_BASE ? TCG_REG_GUEST_BASE : TCG_REG_XZR);
#endif /* CONFIG_SOFTMMU */
}

static uint8_t *tb_ret_addr;

/* callee stack use example:
   stp     x29, x30, [sp,#-32]!
   mov     x29, sp
   stp     x1, x2, [sp,#16]
   ...
   ldp     x1, x2, [sp,#16]
   ldp     x29, x30, [sp],#32
   ret
*/

/* push r1 and r2, and alloc stack space for a total of
   alloc_n elements (1 element=16 bytes, must be between 1 and 31. */
static inline void tcg_out_push_pair(TCGContext *s, TCGReg addr,
                                     TCGReg r1, TCGReg r2, int alloc_n)
{
    /* using indexed scaled simm7 STP 0x28800000 | (ext) | 0x01000000 (pre-idx)
       | alloc_n * (-1) << 16 | r2 << 10 | addr << 5 | r1 */
    assert(alloc_n > 0 && alloc_n < 0x20);
    alloc_n = (-alloc_n) & 0x3f;
    tcg_out32(s, 0xa9800000 | alloc_n << 16 | r2 << 10 | addr << 5 | r1);
}

/* dealloc stack space for a total of alloc_n elements and pop r1, r2.  */
static inline void tcg_out_pop_pair(TCGContext *s, TCGReg addr,
                                    TCGReg r1, TCGReg r2, int alloc_n)
{
    /* using indexed scaled simm7 LDP 0x28c00000 | (ext) | nothing (post-idx)
       | alloc_n << 16 | r2 << 10 | addr << 5 | r1 */
    assert(alloc_n > 0 && alloc_n < 0x20);
    tcg_out32(s, 0xa8c00000 | alloc_n << 16 | r2 << 10 | addr << 5 | r1);
}

static inline void tcg_out_store_pair(TCGContext *s, TCGReg addr,
                                      TCGReg r1, TCGReg r2, int idx)
{
    /* using register pair offset simm7 STP 0x29000000 | (ext)
       | idx << 16 | r2 << 10 | addr << 5 | r1 */
    assert(idx > 0 && idx < 0x20);
    tcg_out32(s, 0xa9000000 | idx << 16 | r2 << 10 | addr << 5 | r1);
}

static inline void tcg_out_load_pair(TCGContext *s, TCGReg addr,
                                     TCGReg r1, TCGReg r2, int idx)
{
    /* using register pair offset simm7 LDP 0x29400000 | (ext)
       | idx << 16 | r2 << 10 | addr << 5 | r1 */
    assert(idx > 0 && idx < 0x20);
    tcg_out32(s, 0xa9400000 | idx << 16 | r2 << 10 | addr << 5 | r1);
}

static void tcg_out_op(TCGContext *s, TCGOpcode opc,
                       const TCGArg *args, const int *const_args)
{
    /* ext will be set in the switch below, which will fall through to the
       common code. It triggers the use of extended regs where appropriate. */
    int ext = 0;

    switch (opc) {
    case INDEX_op_exit_tb:
        tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_X0, args[0]);
        tcg_out_goto(s, (tcg_target_long)tb_ret_addr);
        break;

    case INDEX_op_goto_tb:
#ifndef USE_DIRECT_JUMP
#error "USE_DIRECT_JUMP required for aarch64"
#endif
        assert(s->tb_jmp_offset != NULL); /* consistency for USE_DIRECT_JUMP */
        s->tb_jmp_offset[args[0]] = s->code_ptr - s->code_buf;
        /* actual branch destination will be patched by
           aarch64_tb_set_jmp_target later, beware retranslation. */
        tcg_out_goto_noaddr(s);
        s->tb_next_offset[args[0]] = s->code_ptr - s->code_buf;
        break;

    case INDEX_op_call:
        if (const_args[0]) {
            tcg_out_call(s, args[0]);
        } else {
            tcg_out_callr(s, args[0]);
        }
        break;

    case INDEX_op_br:
        tcg_out_goto_label(s, args[0]);
        break;

    case INDEX_op_ld_i32:
    case INDEX_op_ld_i64:
    case INDEX_op_st_i32:
    case INDEX_op_st_i64:
    case INDEX_op_ld8u_i32:
    case INDEX_op_ld8s_i32:
    case INDEX_op_ld16u_i32:
    case INDEX_op_ld16s_i32:
    case INDEX_op_ld8u_i64:
    case INDEX_op_ld8s_i64:
    case INDEX_op_ld16u_i64:
    case INDEX_op_ld16s_i64:
    case INDEX_op_ld32u_i64:
    case INDEX_op_ld32s_i64:
    case INDEX_op_st8_i32:
    case INDEX_op_st8_i64:
    case INDEX_op_st16_i32:
    case INDEX_op_st16_i64:
    case INDEX_op_st32_i64:
        tcg_out_ldst(s, aarch64_ldst_get_data(opc), aarch64_ldst_get_type(opc),
                     args[0], args[1], args[2]);
        break;

    case INDEX_op_mov_i64:
        ext = 1; /* fall through */
    case INDEX_op_mov_i32:
        tcg_out_movr(s, ext, args[0], args[1]);
        break;

    case INDEX_op_movi_i64:
        tcg_out_movi(s, TCG_TYPE_I64, args[0], args[1]);
        break;
    case INDEX_op_movi_i32:
        tcg_out_movi(s, TCG_TYPE_I32, args[0], args[1]);
        break;

    case INDEX_op_add_i64:
        ext = 1; /* fall through */
    case INDEX_op_add_i32:
        tcg_out_arith(s, ARITH_ADD, ext, args[0], args[1], args[2], 0);
        break;

    case INDEX_op_sub_i64:
        ext = 1; /* fall through */
    case INDEX_op_sub_i32:
        tcg_out_arith(s, ARITH_SUB, ext, args[0], args[1], args[2], 0);
        break;

    case INDEX_op_and_i64:
        ext = 1; /* fall through */
    case INDEX_op_and_i32:
        tcg_out_arith(s, ARITH_AND, ext, args[0], args[1], args[2], 0);
        break;

    case INDEX_op_or_i64:
        ext = 1; /* fall through */
    case INDEX_op_or_i32:
        tcg_out_arith(s, ARITH_OR, ext, args[0], args[1], args[2], 0);
        break;

    case INDEX_op_xor_i64:
        ext = 1; /* fall through */
    case INDEX_op_xor_i32:
        tcg_out_arith(s, ARITH_XOR, ext, args[0], args[1], args[2], 0);
        break;

    case INDEX_op_mul_i64:
        ext = 1; /* fall through */
    case INDEX_op_mul_i32:
        tcg_out_mul(s, ext, args[0], args[1], args[2]);
        break;

    case INDEX_op_shl_i64:
        ext = 1; /* fall through */
    case INDEX_op_shl_i32:
        if (const_args[2]) {    /* LSL / UBFM Wd, Wn, (32 - m) */
            tcg_out_shl(s, ext, args[0], args[1], args[2]);
        } else {                /* LSL / LSLV */
            tcg_out_shiftrot_reg(s, SRR_SHL, ext, args[0], args[1], args[2]);
        }
        break;

    case INDEX_op_shr_i64:
        ext = 1; /* fall through */
    case INDEX_op_shr_i32:
        if (const_args[2]) {    /* LSR / UBFM Wd, Wn, m, 31 */
            tcg_out_shr(s, ext, args[0], args[1], args[2]);
        } else {                /* LSR / LSRV */
            tcg_out_shiftrot_reg(s, SRR_SHR, ext, args[0], args[1], args[2]);
        }
        break;

    case INDEX_op_sar_i64:
        ext = 1; /* fall through */
    case INDEX_op_sar_i32:
        if (const_args[2]) {    /* ASR / SBFM Wd, Wn, m, 31 */
            tcg_out_sar(s, ext, args[0], args[1], args[2]);
        } else {                /* ASR / ASRV */
            tcg_out_shiftrot_reg(s, SRR_SAR, ext, args[0], args[1], args[2]);
        }
        break;

    case INDEX_op_rotr_i64:
        ext = 1; /* fall through */
    case INDEX_op_rotr_i32:
        if (const_args[2]) {    /* ROR / EXTR Wd, Wm, Wm, m */
            tcg_out_rotr(s, ext, args[0], args[1], args[2]);
        } else {                /* ROR / RORV */
            tcg_out_shiftrot_reg(s, SRR_ROR, ext, args[0], args[1], args[2]);
        }
        break;

    case INDEX_op_rotl_i64:
        ext = 1; /* fall through */
    case INDEX_op_rotl_i32:     /* same as rotate right by (32 - m) */
        if (const_args[2]) {    /* ROR / EXTR Wd, Wm, Wm, 32 - m */
            tcg_out_rotl(s, ext, args[0], args[1], args[2]);
        } else {
            tcg_out_arith(s, ARITH_SUB, 0,
                          TCG_REG_TMP, TCG_REG_XZR, args[2], 0);
            tcg_out_shiftrot_reg(s, SRR_ROR, ext,
                                 args[0], args[1], TCG_REG_TMP);
        }
        break;

    case INDEX_op_brcond_i64:
        ext = 1; /* fall through */
    case INDEX_op_brcond_i32: /* CMP 0, 1, cond(2), label 3 */
        tcg_out_cmp(s, ext, args[0], args[1], 0);
        tcg_out_goto_label_cond(s, args[2], args[3]);
        break;

    case INDEX_op_setcond_i64:
        ext = 1; /* fall through */
    case INDEX_op_setcond_i32:
        tcg_out_cmp(s, ext, args[1], args[2], 0);
        tcg_out_cset(s, 0, args[0], args[3]);
        break;

    case INDEX_op_qemu_ld8u:
        tcg_out_qemu_ld(s, args, 0 | 0);
        break;
    case INDEX_op_qemu_ld8s:
        tcg_out_qemu_ld(s, args, 4 | 0);
        break;
    case INDEX_op_qemu_ld16u:
        tcg_out_qemu_ld(s, args, 0 | 1);
        break;
    case INDEX_op_qemu_ld16s:
        tcg_out_qemu_ld(s, args, 4 | 1);
        break;
    case INDEX_op_qemu_ld32u:
        tcg_out_qemu_ld(s, args, 0 | 2);
        break;
    case INDEX_op_qemu_ld32s:
        tcg_out_qemu_ld(s, args, 4 | 2);
        break;
    case INDEX_op_qemu_ld32:
        tcg_out_qemu_ld(s, args, 0 | 2);
        break;
    case INDEX_op_qemu_ld64:
        tcg_out_qemu_ld(s, args, 0 | 3);
        break;
    case INDEX_op_qemu_st8:
        tcg_out_qemu_st(s, args, 0);
        break;
    case INDEX_op_qemu_st16:
        tcg_out_qemu_st(s, args, 1);
        break;
    case INDEX_op_qemu_st32:
        tcg_out_qemu_st(s, args, 2);
        break;
    case INDEX_op_qemu_st64:
        tcg_out_qemu_st(s, args, 3);
        break;

    case INDEX_op_bswap64_i64:
        ext = 1; /* fall through */
    case INDEX_op_bswap32_i64:
    case INDEX_op_bswap32_i32:
        tcg_out_rev(s, ext, args[0], args[1]);
        break;
    case INDEX_op_bswap16_i64:
    case INDEX_op_bswap16_i32:
        tcg_out_rev16(s, 0, args[0], args[1]);
        break;

    case INDEX_op_ext8s_i64:
        ext = 1; /* fall through */
    case INDEX_op_ext8s_i32:
        tcg_out_sxt(s, ext, 0, args[0], args[1]);
        break;
    case INDEX_op_ext16s_i64:
        ext = 1; /* fall through */
    case INDEX_op_ext16s_i32:
        tcg_out_sxt(s, ext, 1, args[0], args[1]);
        break;
    case INDEX_op_ext32s_i64:
        tcg_out_sxt(s, 1, 2, args[0], args[1]);
        break;
    case INDEX_op_ext8u_i64:
    case INDEX_op_ext8u_i32:
        tcg_out_uxt(s, 0, args[0], args[1]);
        break;
    case INDEX_op_ext16u_i64:
    case INDEX_op_ext16u_i32:
        tcg_out_uxt(s, 1, args[0], args[1]);
        break;
    case INDEX_op_ext32u_i64:
        tcg_out_movr(s, 0, args[0], args[1]);
        break;

    default:
        tcg_abort(); /* opcode not implemented */
    }
}

static const TCGTargetOpDef aarch64_op_defs[] = {
    { INDEX_op_exit_tb, { } },
    { INDEX_op_goto_tb, { } },
    { INDEX_op_call, { "ri" } },
    { INDEX_op_br, { } },

    { INDEX_op_mov_i32, { "r", "r" } },
    { INDEX_op_mov_i64, { "r", "r" } },

    { INDEX_op_movi_i32, { "r" } },
    { INDEX_op_movi_i64, { "r" } },

    { INDEX_op_ld8u_i32, { "r", "r" } },
    { INDEX_op_ld8s_i32, { "r", "r" } },
    { INDEX_op_ld16u_i32, { "r", "r" } },
    { INDEX_op_ld16s_i32, { "r", "r" } },
    { INDEX_op_ld_i32, { "r", "r" } },
    { INDEX_op_ld8u_i64, { "r", "r" } },
    { INDEX_op_ld8s_i64, { "r", "r" } },
    { INDEX_op_ld16u_i64, { "r", "r" } },
    { INDEX_op_ld16s_i64, { "r", "r" } },
    { INDEX_op_ld32u_i64, { "r", "r" } },
    { INDEX_op_ld32s_i64, { "r", "r" } },
    { INDEX_op_ld_i64, { "r", "r" } },

    { INDEX_op_st8_i32, { "r", "r" } },
    { INDEX_op_st16_i32, { "r", "r" } },
    { INDEX_op_st_i32, { "r", "r" } },
    { INDEX_op_st8_i64, { "r", "r" } },
    { INDEX_op_st16_i64, { "r", "r" } },
    { INDEX_op_st32_i64, { "r", "r" } },
    { INDEX_op_st_i64, { "r", "r" } },

    { INDEX_op_add_i32, { "r", "r", "r" } },
    { INDEX_op_add_i64, { "r", "r", "r" } },
    { INDEX_op_sub_i32, { "r", "r", "r" } },
    { INDEX_op_sub_i64, { "r", "r", "r" } },
    { INDEX_op_mul_i32, { "r", "r", "r" } },
    { INDEX_op_mul_i64, { "r", "r", "r" } },
    { INDEX_op_and_i32, { "r", "r", "r" } },
    { INDEX_op_and_i64, { "r", "r", "r" } },
    { INDEX_op_or_i32, { "r", "r", "r" } },
    { INDEX_op_or_i64, { "r", "r", "r" } },
    { INDEX_op_xor_i32, { "r", "r", "r" } },
    { INDEX_op_xor_i64, { "r", "r", "r" } },

    { INDEX_op_shl_i32, { "r", "r", "ri" } },
    { INDEX_op_shr_i32, { "r", "r", "ri" } },
    { INDEX_op_sar_i32, { "r", "r", "ri" } },
    { INDEX_op_rotl_i32, { "r", "r", "ri" } },
    { INDEX_op_rotr_i32, { "r", "r", "ri" } },
    { INDEX_op_shl_i64, { "r", "r", "ri" } },
    { INDEX_op_shr_i64, { "r", "r", "ri" } },
    { INDEX_op_sar_i64, { "r", "r", "ri" } },
    { INDEX_op_rotl_i64, { "r", "r", "ri" } },
    { INDEX_op_rotr_i64, { "r", "r", "ri" } },

    { INDEX_op_brcond_i32, { "r", "r" } },
    { INDEX_op_setcond_i32, { "r", "r", "r" } },
    { INDEX_op_brcond_i64, { "r", "r" } },
    { INDEX_op_setcond_i64, { "r", "r", "r" } },

    { INDEX_op_qemu_ld8u, { "r", "l" } },
    { INDEX_op_qemu_ld8s, { "r", "l" } },
    { INDEX_op_qemu_ld16u, { "r", "l" } },
    { INDEX_op_qemu_ld16s, { "r", "l" } },
    { INDEX_op_qemu_ld32u, { "r", "l" } },
    { INDEX_op_qemu_ld32s, { "r", "l" } },

    { INDEX_op_qemu_ld32, { "r", "l" } },
    { INDEX_op_qemu_ld64, { "r", "l" } },

    { INDEX_op_qemu_st8, { "l", "l" } },
    { INDEX_op_qemu_st16, { "l", "l" } },
    { INDEX_op_qemu_st32, { "l", "l" } },
    { INDEX_op_qemu_st64, { "l", "l" } },

    { INDEX_op_bswap16_i32, { "r", "r" } },
    { INDEX_op_bswap32_i32, { "r", "r" } },
    { INDEX_op_bswap16_i64, { "r", "r" } },
    { INDEX_op_bswap32_i64, { "r", "r" } },
    { INDEX_op_bswap64_i64, { "r", "r" } },

    { INDEX_op_ext8s_i32, { "r", "r" } },
    { INDEX_op_ext16s_i32, { "r", "r" } },
    { INDEX_op_ext8u_i32, { "r", "r" } },
    { INDEX_op_ext16u_i32, { "r", "r" } },

    { INDEX_op_ext8s_i64, { "r", "r" } },
    { INDEX_op_ext16s_i64, { "r", "r" } },
    { INDEX_op_ext32s_i64, { "r", "r" } },
    { INDEX_op_ext8u_i64, { "r", "r" } },
    { INDEX_op_ext16u_i64, { "r", "r" } },
    { INDEX_op_ext32u_i64, { "r", "r" } },

    { -1 },
};

static void tcg_target_init(TCGContext *s)
{
#if !defined(CONFIG_USER_ONLY)
    /* fail safe */
    if ((1ULL << CPU_TLB_ENTRY_BITS) != sizeof(CPUTLBEntry)) {
        tcg_abort();
    }
#endif
    tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I32], 0, 0xffffffff);
    tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I64], 0, 0xffffffff);

    tcg_regset_set32(tcg_target_call_clobber_regs, 0,
                     (1 << TCG_REG_X0) | (1 << TCG_REG_X1) |
                     (1 << TCG_REG_X2) | (1 << TCG_REG_X3) |
                     (1 << TCG_REG_X4) | (1 << TCG_REG_X5) |
                     (1 << TCG_REG_X6) | (1 << TCG_REG_X7) |
                     (1 << TCG_REG_X8) | (1 << TCG_REG_X9) |
                     (1 << TCG_REG_X10) | (1 << TCG_REG_X11) |
                     (1 << TCG_REG_X12) | (1 << TCG_REG_X13) |
                     (1 << TCG_REG_X14) | (1 << TCG_REG_X15) |
                     (1 << TCG_REG_X16) | (1 << TCG_REG_X17) |
                     (1 << TCG_REG_X18));

    tcg_regset_clear(s->reserved_regs);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_SP);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_FP);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_TMP);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_X18); /* platform register */

    tcg_add_target_add_op_defs(aarch64_op_defs);
}

static inline void tcg_out_addi(TCGContext *s, int ext,
                                TCGReg rd, TCGReg rn, unsigned int aimm)
{
    /* add immediate aimm unsigned 12bit value (we use LSL 0 - no shift) */
    /* using ADD 0x11000000 | (ext) | (aimm << 10) | (rn << 5) | rd */
    unsigned int base = ext ? 0x91000000 : 0x11000000;
    assert(aimm <= 0xfff);
    tcg_out32(s, base | (aimm << 10) | (rn << 5) | rd);
}

static inline void tcg_out_subi(TCGContext *s, int ext,
                                TCGReg rd, TCGReg rn, unsigned int aimm)
{
    /* sub immediate aimm unsigned 12bit value (we use LSL 0 - no shift) */
    /* using SUB 0x51000000 | (ext) | (aimm << 10) | (rn << 5) | rd */
    unsigned int base = ext ? 0xd1000000 : 0x51000000;
    assert(aimm <= 0xfff);
    tcg_out32(s, base | (aimm << 10) | (rn << 5) | rd);
}

static void tcg_target_qemu_prologue(TCGContext *s)
{
    /* NB: frame sizes are in 16 byte stack units! */
    int frame_size_callee_saved, frame_size_tcg_locals;
    TCGReg r;

    /* save pairs             (FP, LR) and (X19, X20) .. (X27, X28) */
    frame_size_callee_saved = (1) + (TCG_REG_X28 - TCG_REG_X19) / 2 + 1;

    /* frame size requirement for TCG local variables */
    frame_size_tcg_locals = TCG_STATIC_CALL_ARGS_SIZE
        + CPU_TEMP_BUF_NLONGS * sizeof(long)
        + (TCG_TARGET_STACK_ALIGN - 1);
    frame_size_tcg_locals &= ~(TCG_TARGET_STACK_ALIGN - 1);
    frame_size_tcg_locals /= TCG_TARGET_STACK_ALIGN;

    /* push (FP, LR) and update sp */
    tcg_out_push_pair(s, TCG_REG_SP,
                      TCG_REG_FP, TCG_REG_LR, frame_size_callee_saved);

    /* FP -> callee_saved */
    tcg_out_movr_sp(s, 1, TCG_REG_FP, TCG_REG_SP);

    /* store callee-preserved regs x19..x28 using FP -> callee_saved */
    for (r = TCG_REG_X19; r <= TCG_REG_X27; r += 2) {
        int idx = (r - TCG_REG_X19) / 2 + 1;
        tcg_out_store_pair(s, TCG_REG_FP, r, r + 1, idx);
    }

    /* make stack space for TCG locals */
    tcg_out_subi(s, 1, TCG_REG_SP, TCG_REG_SP,
                 frame_size_tcg_locals * TCG_TARGET_STACK_ALIGN);
    /* inform TCG about how to find TCG locals with register, offset, size */
    tcg_set_frame(s, TCG_REG_SP, TCG_STATIC_CALL_ARGS_SIZE,
                  CPU_TEMP_BUF_NLONGS * sizeof(long));

#if defined(CONFIG_USE_GUEST_BASE)
    if (GUEST_BASE) {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_GUEST_BASE, GUEST_BASE);
        tcg_regset_set_reg(s->reserved_regs, TCG_REG_GUEST_BASE);
    }
#endif

    tcg_out_mov(s, TCG_TYPE_PTR, TCG_AREG0, tcg_target_call_iarg_regs[0]);
    tcg_out_gotor(s, tcg_target_call_iarg_regs[1]);

    tb_ret_addr = s->code_ptr;

    /* remove TCG locals stack space */
    tcg_out_addi(s, 1, TCG_REG_SP, TCG_REG_SP,
                 frame_size_tcg_locals * TCG_TARGET_STACK_ALIGN);

    /* restore registers x19..x28.
       FP must be preserved, so it still points to callee_saved area */
    for (r = TCG_REG_X19; r <= TCG_REG_X27; r += 2) {
        int idx = (r - TCG_REG_X19) / 2 + 1;
        tcg_out_load_pair(s, TCG_REG_FP, r, r + 1, idx);
    }

    /* pop (FP, LR), restore SP to previous frame, return */
    tcg_out_pop_pair(s, TCG_REG_SP,
                     TCG_REG_FP, TCG_REG_LR, frame_size_callee_saved);
    tcg_out_ret(s);
}
