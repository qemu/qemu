/*
 *  UniCore32 translation
 *
 * Copyright (C) 2010-2012 Guan Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"

#include "cpu.h"
#include "disas/disas.h"
#include "exec/exec-all.h"
#include "tcg-op.h"
#include "qemu/log.h"
#include "exec/cpu_ldst.h"
#include "exec/translator.h"
#include "qemu/qemu-print.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"

#include "trace-tcg.h"
#include "exec/log.h"


/* internal defines */
typedef struct DisasContext {
    target_ulong pc;
    int is_jmp;
    /* Nonzero if this instruction has been conditionally skipped.  */
    int condjmp;
    /* The label that will be jumped to when the instruction is skipped.  */
    TCGLabel *condlabel;
    struct TranslationBlock *tb;
    int singlestep_enabled;
#ifndef CONFIG_USER_ONLY
    int user;
#endif
} DisasContext;

#ifndef CONFIG_USER_ONLY
#define IS_USER(s)      (s->user)
#else
#define IS_USER(s)      1
#endif

/* is_jmp field values */
#define DISAS_JUMP    DISAS_TARGET_0 /* only pc was modified dynamically */
#define DISAS_UPDATE  DISAS_TARGET_1 /* cpu state was modified dynamically */
#define DISAS_TB_JUMP DISAS_TARGET_2 /* only pc was modified statically */
/* These instructions trap after executing, so defer them until after the
   conditional executions state has been updated.  */
#define DISAS_SYSCALL DISAS_TARGET_3

static TCGv_i32 cpu_R[32];

/* FIXME:  These should be removed.  */
static TCGv cpu_F0s, cpu_F1s;
static TCGv_i64 cpu_F0d, cpu_F1d;

#include "exec/gen-icount.h"

static const char *regnames[] = {
      "r00", "r01", "r02", "r03", "r04", "r05", "r06", "r07",
      "r08", "r09", "r10", "r11", "r12", "r13", "r14", "r15",
      "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
      "r24", "r25", "r26", "r27", "r28", "r29", "r30", "pc" };

/* initialize TCG globals.  */
void uc32_translate_init(void)
{
    int i;

    for (i = 0; i < 32; i++) {
        cpu_R[i] = tcg_global_mem_new_i32(cpu_env,
                                offsetof(CPUUniCore32State, regs[i]), regnames[i]);
    }
}

static int num_temps;

/* Allocate a temporary variable.  */
static TCGv_i32 new_tmp(void)
{
    num_temps++;
    return tcg_temp_new_i32();
}

/* Release a temporary variable.  */
static void dead_tmp(TCGv tmp)
{
    tcg_temp_free(tmp);
    num_temps--;
}

static inline TCGv load_cpu_offset(int offset)
{
    TCGv tmp = new_tmp();
    tcg_gen_ld_i32(tmp, cpu_env, offset);
    return tmp;
}

#define load_cpu_field(name) load_cpu_offset(offsetof(CPUUniCore32State, name))

static inline void store_cpu_offset(TCGv var, int offset)
{
    tcg_gen_st_i32(var, cpu_env, offset);
    dead_tmp(var);
}

#define store_cpu_field(var, name) \
    store_cpu_offset(var, offsetof(CPUUniCore32State, name))

/* Set a variable to the value of a CPU register.  */
static void load_reg_var(DisasContext *s, TCGv var, int reg)
{
    if (reg == 31) {
        uint32_t addr;
        /* normaly, since we updated PC */
        addr = (long)s->pc;
        tcg_gen_movi_i32(var, addr);
    } else {
        tcg_gen_mov_i32(var, cpu_R[reg]);
    }
}

/* Create a new temporary and set it to the value of a CPU register.  */
static inline TCGv load_reg(DisasContext *s, int reg)
{
    TCGv tmp = new_tmp();
    load_reg_var(s, tmp, reg);
    return tmp;
}

/* Set a CPU register.  The source must be a temporary and will be
   marked as dead.  */
static void store_reg(DisasContext *s, int reg, TCGv var)
{
    if (reg == 31) {
        tcg_gen_andi_i32(var, var, ~3);
        s->is_jmp = DISAS_JUMP;
    }
    tcg_gen_mov_i32(cpu_R[reg], var);
    dead_tmp(var);
}

/* Value extensions.  */
#define gen_uxtb(var)           tcg_gen_ext8u_i32(var, var)
#define gen_uxth(var)           tcg_gen_ext16u_i32(var, var)
#define gen_sxtb(var)           tcg_gen_ext8s_i32(var, var)
#define gen_sxth(var)           tcg_gen_ext16s_i32(var, var)

#define UCOP_REG_M              (((insn) >>  0) & 0x1f)
#define UCOP_REG_N              (((insn) >> 19) & 0x1f)
#define UCOP_REG_D              (((insn) >> 14) & 0x1f)
#define UCOP_REG_S              (((insn) >>  9) & 0x1f)
#define UCOP_REG_LO             (((insn) >> 14) & 0x1f)
#define UCOP_REG_HI             (((insn) >>  9) & 0x1f)
#define UCOP_SH_OP              (((insn) >>  6) & 0x03)
#define UCOP_SH_IM              (((insn) >>  9) & 0x1f)
#define UCOP_OPCODES            (((insn) >> 25) & 0x0f)
#define UCOP_IMM_9              (((insn) >>  0) & 0x1ff)
#define UCOP_IMM10              (((insn) >>  0) & 0x3ff)
#define UCOP_IMM14              (((insn) >>  0) & 0x3fff)
#define UCOP_COND               (((insn) >> 25) & 0x0f)
#define UCOP_CMOV_COND          (((insn) >> 19) & 0x0f)
#define UCOP_CPNUM              (((insn) >> 10) & 0x0f)
#define UCOP_UCF64_FMT          (((insn) >> 24) & 0x03)
#define UCOP_UCF64_FUNC         (((insn) >>  6) & 0x0f)
#define UCOP_UCF64_COND         (((insn) >>  6) & 0x0f)

#define UCOP_SET(i)             ((insn) & (1 << (i)))
#define UCOP_SET_P              UCOP_SET(28)
#define UCOP_SET_U              UCOP_SET(27)
#define UCOP_SET_B              UCOP_SET(26)
#define UCOP_SET_W              UCOP_SET(25)
#define UCOP_SET_L              UCOP_SET(24)
#define UCOP_SET_S              UCOP_SET(24)

#define ILLEGAL         cpu_abort(env_cpu(env),                         \
                        "Illegal UniCore32 instruction %x at line %d!", \
                        insn, __LINE__)

#ifndef CONFIG_USER_ONLY
static void disas_cp0_insn(CPUUniCore32State *env, DisasContext *s,
        uint32_t insn)
{
    TCGv tmp, tmp2, tmp3;
    if ((insn & 0xfe000000) == 0xe0000000) {
        tmp2 = new_tmp();
        tmp3 = new_tmp();
        tcg_gen_movi_i32(tmp2, UCOP_REG_N);
        tcg_gen_movi_i32(tmp3, UCOP_IMM10);
        if (UCOP_SET_L) {
            tmp = new_tmp();
            gen_helper_cp0_get(tmp, cpu_env, tmp2, tmp3);
            store_reg(s, UCOP_REG_D, tmp);
        } else {
            tmp = load_reg(s, UCOP_REG_D);
            gen_helper_cp0_set(cpu_env, tmp, tmp2, tmp3);
            dead_tmp(tmp);
        }
        dead_tmp(tmp2);
        dead_tmp(tmp3);
        return;
    }
    ILLEGAL;
}

static void disas_ocd_insn(CPUUniCore32State *env, DisasContext *s,
        uint32_t insn)
{
    TCGv tmp;

    if ((insn & 0xff003fff) == 0xe1000400) {
        /*
         * movc rd, pp.nn, #imm9
         *      rd: UCOP_REG_D
         *      nn: UCOP_REG_N (must be 0)
         *      imm9: 0
         */
        if (UCOP_REG_N == 0) {
            tmp = new_tmp();
            tcg_gen_movi_i32(tmp, 0);
            store_reg(s, UCOP_REG_D, tmp);
            return;
        } else {
            ILLEGAL;
        }
    }
    if ((insn & 0xff003fff) == 0xe0000401) {
        /*
         * movc pp.nn, rn, #imm9
         *      rn: UCOP_REG_D
         *      nn: UCOP_REG_N (must be 1)
         *      imm9: 1
         */
        if (UCOP_REG_N == 1) {
            tmp = load_reg(s, UCOP_REG_D);
            gen_helper_cp1_putc(tmp);
            dead_tmp(tmp);
            return;
        } else {
            ILLEGAL;
        }
    }
    ILLEGAL;
}
#endif

static inline void gen_set_asr(TCGv var, uint32_t mask)
{
    TCGv tmp_mask = tcg_const_i32(mask);
    gen_helper_asr_write(cpu_env, var, tmp_mask);
    tcg_temp_free_i32(tmp_mask);
}
/* Set NZCV flags from the high 4 bits of var.  */
#define gen_set_nzcv(var) gen_set_asr(var, ASR_NZCV)

static void gen_exception(int excp)
{
    TCGv tmp = new_tmp();
    tcg_gen_movi_i32(tmp, excp);
    gen_helper_exception(cpu_env, tmp);
    dead_tmp(tmp);
}

#define gen_set_CF(var) tcg_gen_st_i32(var, cpu_env, offsetof(CPUUniCore32State, CF))

/* Set CF to the top bit of var.  */
static void gen_set_CF_bit31(TCGv var)
{
    TCGv tmp = new_tmp();
    tcg_gen_shri_i32(tmp, var, 31);
    gen_set_CF(tmp);
    dead_tmp(tmp);
}

/* Set N and Z flags from var.  */
static inline void gen_logic_CC(TCGv var)
{
    tcg_gen_st_i32(var, cpu_env, offsetof(CPUUniCore32State, NF));
    tcg_gen_st_i32(var, cpu_env, offsetof(CPUUniCore32State, ZF));
}

/* dest = T0 + T1 + CF. */
static void gen_add_carry(TCGv dest, TCGv t0, TCGv t1)
{
    TCGv tmp;
    tcg_gen_add_i32(dest, t0, t1);
    tmp = load_cpu_field(CF);
    tcg_gen_add_i32(dest, dest, tmp);
    dead_tmp(tmp);
}

/* dest = T0 - T1 + CF - 1.  */
static void gen_sub_carry(TCGv dest, TCGv t0, TCGv t1)
{
    TCGv tmp;
    tcg_gen_sub_i32(dest, t0, t1);
    tmp = load_cpu_field(CF);
    tcg_gen_add_i32(dest, dest, tmp);
    tcg_gen_subi_i32(dest, dest, 1);
    dead_tmp(tmp);
}

static void shifter_out_im(TCGv var, int shift)
{
    TCGv tmp = new_tmp();
    if (shift == 0) {
        tcg_gen_andi_i32(tmp, var, 1);
    } else {
        tcg_gen_shri_i32(tmp, var, shift);
        if (shift != 31) {
            tcg_gen_andi_i32(tmp, tmp, 1);
        }
    }
    gen_set_CF(tmp);
    dead_tmp(tmp);
}

/* Shift by immediate.  Includes special handling for shift == 0.  */
static inline void gen_uc32_shift_im(TCGv var, int shiftop, int shift,
        int flags)
{
    switch (shiftop) {
    case 0: /* LSL */
        if (shift != 0) {
            if (flags) {
                shifter_out_im(var, 32 - shift);
            }
            tcg_gen_shli_i32(var, var, shift);
        }
        break;
    case 1: /* LSR */
        if (shift == 0) {
            if (flags) {
                tcg_gen_shri_i32(var, var, 31);
                gen_set_CF(var);
            }
            tcg_gen_movi_i32(var, 0);
        } else {
            if (flags) {
                shifter_out_im(var, shift - 1);
            }
            tcg_gen_shri_i32(var, var, shift);
        }
        break;
    case 2: /* ASR */
        if (shift == 0) {
            shift = 32;
        }
        if (flags) {
            shifter_out_im(var, shift - 1);
        }
        if (shift == 32) {
            shift = 31;
        }
        tcg_gen_sari_i32(var, var, shift);
        break;
    case 3: /* ROR/RRX */
        if (shift != 0) {
            if (flags) {
                shifter_out_im(var, shift - 1);
            }
            tcg_gen_rotri_i32(var, var, shift); break;
        } else {
            TCGv tmp = load_cpu_field(CF);
            if (flags) {
                shifter_out_im(var, 0);
            }
            tcg_gen_shri_i32(var, var, 1);
            tcg_gen_shli_i32(tmp, tmp, 31);
            tcg_gen_or_i32(var, var, tmp);
            dead_tmp(tmp);
        }
    }
};

static inline void gen_uc32_shift_reg(TCGv var, int shiftop,
                                     TCGv shift, int flags)
{
    if (flags) {
        switch (shiftop) {
        case 0:
            gen_helper_shl_cc(var, cpu_env, var, shift);
            break;
        case 1:
            gen_helper_shr_cc(var, cpu_env, var, shift);
            break;
        case 2:
            gen_helper_sar_cc(var, cpu_env, var, shift);
            break;
        case 3:
            gen_helper_ror_cc(var, cpu_env, var, shift);
            break;
        }
    } else {
        switch (shiftop) {
        case 0:
            gen_helper_shl(var, var, shift);
            break;
        case 1:
            gen_helper_shr(var, var, shift);
            break;
        case 2:
            gen_helper_sar(var, var, shift);
            break;
        case 3:
            tcg_gen_andi_i32(shift, shift, 0x1f);
            tcg_gen_rotr_i32(var, var, shift);
            break;
        }
    }
    dead_tmp(shift);
}

static void gen_test_cc(int cc, TCGLabel *label)
{
    TCGv tmp;
    TCGv tmp2;
    TCGLabel *inv;

    switch (cc) {
    case 0: /* eq: Z */
        tmp = load_cpu_field(ZF);
        tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, label);
        break;
    case 1: /* ne: !Z */
        tmp = load_cpu_field(ZF);
        tcg_gen_brcondi_i32(TCG_COND_NE, tmp, 0, label);
        break;
    case 2: /* cs: C */
        tmp = load_cpu_field(CF);
        tcg_gen_brcondi_i32(TCG_COND_NE, tmp, 0, label);
        break;
    case 3: /* cc: !C */
        tmp = load_cpu_field(CF);
        tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, label);
        break;
    case 4: /* mi: N */
        tmp = load_cpu_field(NF);
        tcg_gen_brcondi_i32(TCG_COND_LT, tmp, 0, label);
        break;
    case 5: /* pl: !N */
        tmp = load_cpu_field(NF);
        tcg_gen_brcondi_i32(TCG_COND_GE, tmp, 0, label);
        break;
    case 6: /* vs: V */
        tmp = load_cpu_field(VF);
        tcg_gen_brcondi_i32(TCG_COND_LT, tmp, 0, label);
        break;
    case 7: /* vc: !V */
        tmp = load_cpu_field(VF);
        tcg_gen_brcondi_i32(TCG_COND_GE, tmp, 0, label);
        break;
    case 8: /* hi: C && !Z */
        inv = gen_new_label();
        tmp = load_cpu_field(CF);
        tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, inv);
        dead_tmp(tmp);
        tmp = load_cpu_field(ZF);
        tcg_gen_brcondi_i32(TCG_COND_NE, tmp, 0, label);
        gen_set_label(inv);
        break;
    case 9: /* ls: !C || Z */
        tmp = load_cpu_field(CF);
        tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, label);
        dead_tmp(tmp);
        tmp = load_cpu_field(ZF);
        tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, label);
        break;
    case 10: /* ge: N == V -> N ^ V == 0 */
        tmp = load_cpu_field(VF);
        tmp2 = load_cpu_field(NF);
        tcg_gen_xor_i32(tmp, tmp, tmp2);
        dead_tmp(tmp2);
        tcg_gen_brcondi_i32(TCG_COND_GE, tmp, 0, label);
        break;
    case 11: /* lt: N != V -> N ^ V != 0 */
        tmp = load_cpu_field(VF);
        tmp2 = load_cpu_field(NF);
        tcg_gen_xor_i32(tmp, tmp, tmp2);
        dead_tmp(tmp2);
        tcg_gen_brcondi_i32(TCG_COND_LT, tmp, 0, label);
        break;
    case 12: /* gt: !Z && N == V */
        inv = gen_new_label();
        tmp = load_cpu_field(ZF);
        tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, inv);
        dead_tmp(tmp);
        tmp = load_cpu_field(VF);
        tmp2 = load_cpu_field(NF);
        tcg_gen_xor_i32(tmp, tmp, tmp2);
        dead_tmp(tmp2);
        tcg_gen_brcondi_i32(TCG_COND_GE, tmp, 0, label);
        gen_set_label(inv);
        break;
    case 13: /* le: Z || N != V */
        tmp = load_cpu_field(ZF);
        tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, label);
        dead_tmp(tmp);
        tmp = load_cpu_field(VF);
        tmp2 = load_cpu_field(NF);
        tcg_gen_xor_i32(tmp, tmp, tmp2);
        dead_tmp(tmp2);
        tcg_gen_brcondi_i32(TCG_COND_LT, tmp, 0, label);
        break;
    default:
        fprintf(stderr, "Bad condition code 0x%x\n", cc);
        abort();
    }
    dead_tmp(tmp);
}

static const uint8_t table_logic_cc[16] = {
    1, /* and */    1, /* xor */    0, /* sub */    0, /* rsb */
    0, /* add */    0, /* adc */    0, /* sbc */    0, /* rsc */
    1, /* andl */   1, /* xorl */   0, /* cmp */    0, /* cmn */
    1, /* orr */    1, /* mov */    1, /* bic */    1, /* mvn */
};

/* Set PC state from an immediate address.  */
static inline void gen_bx_im(DisasContext *s, uint32_t addr)
{
    s->is_jmp = DISAS_UPDATE;
    tcg_gen_movi_i32(cpu_R[31], addr & ~3);
}

/* Set PC state from var.  var is marked as dead.  */
static inline void gen_bx(DisasContext *s, TCGv var)
{
    s->is_jmp = DISAS_UPDATE;
    tcg_gen_andi_i32(cpu_R[31], var, ~3);
    dead_tmp(var);
}

static inline void store_reg_bx(DisasContext *s, int reg, TCGv var)
{
    store_reg(s, reg, var);
}

static inline TCGv gen_ld8s(TCGv addr, int index)
{
    TCGv tmp = new_tmp();
    tcg_gen_qemu_ld8s(tmp, addr, index);
    return tmp;
}

static inline TCGv gen_ld8u(TCGv addr, int index)
{
    TCGv tmp = new_tmp();
    tcg_gen_qemu_ld8u(tmp, addr, index);
    return tmp;
}

static inline TCGv gen_ld16s(TCGv addr, int index)
{
    TCGv tmp = new_tmp();
    tcg_gen_qemu_ld16s(tmp, addr, index);
    return tmp;
}

static inline TCGv gen_ld16u(TCGv addr, int index)
{
    TCGv tmp = new_tmp();
    tcg_gen_qemu_ld16u(tmp, addr, index);
    return tmp;
}

static inline TCGv gen_ld32(TCGv addr, int index)
{
    TCGv tmp = new_tmp();
    tcg_gen_qemu_ld32u(tmp, addr, index);
    return tmp;
}

static inline void gen_st8(TCGv val, TCGv addr, int index)
{
    tcg_gen_qemu_st8(val, addr, index);
    dead_tmp(val);
}

static inline void gen_st16(TCGv val, TCGv addr, int index)
{
    tcg_gen_qemu_st16(val, addr, index);
    dead_tmp(val);
}

static inline void gen_st32(TCGv val, TCGv addr, int index)
{
    tcg_gen_qemu_st32(val, addr, index);
    dead_tmp(val);
}

static inline void gen_set_pc_im(uint32_t val)
{
    tcg_gen_movi_i32(cpu_R[31], val);
}

/* Force a TB lookup after an instruction that changes the CPU state.  */
static inline void gen_lookup_tb(DisasContext *s)
{
    tcg_gen_movi_i32(cpu_R[31], s->pc & ~1);
    s->is_jmp = DISAS_UPDATE;
}

static inline void gen_add_data_offset(DisasContext *s, unsigned int insn,
        TCGv var)
{
    int val;
    TCGv offset;

    if (UCOP_SET(29)) {
        /* immediate */
        val = UCOP_IMM14;
        if (!UCOP_SET_U) {
            val = -val;
        }
        if (val != 0) {
            tcg_gen_addi_i32(var, var, val);
        }
    } else {
        /* shift/register */
        offset = load_reg(s, UCOP_REG_M);
        gen_uc32_shift_im(offset, UCOP_SH_OP, UCOP_SH_IM, 0);
        if (!UCOP_SET_U) {
            tcg_gen_sub_i32(var, var, offset);
        } else {
            tcg_gen_add_i32(var, var, offset);
        }
        dead_tmp(offset);
    }
}

static inline void gen_add_datah_offset(DisasContext *s, unsigned int insn,
        TCGv var)
{
    int val;
    TCGv offset;

    if (UCOP_SET(26)) {
        /* immediate */
        val = (insn & 0x1f) | ((insn >> 4) & 0x3e0);
        if (!UCOP_SET_U) {
            val = -val;
        }
        if (val != 0) {
            tcg_gen_addi_i32(var, var, val);
        }
    } else {
        /* register */
        offset = load_reg(s, UCOP_REG_M);
        if (!UCOP_SET_U) {
            tcg_gen_sub_i32(var, var, offset);
        } else {
            tcg_gen_add_i32(var, var, offset);
        }
        dead_tmp(offset);
    }
}

static inline long ucf64_reg_offset(int reg)
{
    if (reg & 1) {
        return offsetof(CPUUniCore32State, ucf64.regs[reg >> 1])
          + offsetof(CPU_DoubleU, l.upper);
    } else {
        return offsetof(CPUUniCore32State, ucf64.regs[reg >> 1])
          + offsetof(CPU_DoubleU, l.lower);
    }
}

#define ucf64_gen_ld32(reg)      load_cpu_offset(ucf64_reg_offset(reg))
#define ucf64_gen_st32(var, reg) store_cpu_offset(var, ucf64_reg_offset(reg))

/* UniCore-F64 single load/store I_offset */
static void do_ucf64_ldst_i(CPUUniCore32State *env, DisasContext *s, uint32_t insn)
{
    int offset;
    TCGv tmp;
    TCGv addr;

    addr = load_reg(s, UCOP_REG_N);
    if (!UCOP_SET_P && !UCOP_SET_W) {
        ILLEGAL;
    }

    if (UCOP_SET_P) {
        offset = UCOP_IMM10 << 2;
        if (!UCOP_SET_U) {
            offset = -offset;
        }
        if (offset != 0) {
            tcg_gen_addi_i32(addr, addr, offset);
        }
    }

    if (UCOP_SET_L) { /* load */
        tmp = gen_ld32(addr, IS_USER(s));
        ucf64_gen_st32(tmp, UCOP_REG_D);
    } else { /* store */
        tmp = ucf64_gen_ld32(UCOP_REG_D);
        gen_st32(tmp, addr, IS_USER(s));
    }

    if (!UCOP_SET_P) {
        offset = UCOP_IMM10 << 2;
        if (!UCOP_SET_U) {
            offset = -offset;
        }
        if (offset != 0) {
            tcg_gen_addi_i32(addr, addr, offset);
        }
    }
    if (UCOP_SET_W) {
        store_reg(s, UCOP_REG_N, addr);
    } else {
        dead_tmp(addr);
    }
}

/* UniCore-F64 load/store multiple words */
static void do_ucf64_ldst_m(CPUUniCore32State *env, DisasContext *s, uint32_t insn)
{
    unsigned int i;
    int j, n, freg;
    TCGv tmp;
    TCGv addr;

    if (UCOP_REG_D != 0) {
        ILLEGAL;
    }
    if (UCOP_REG_N == 31) {
        ILLEGAL;
    }
    if ((insn << 24) == 0) {
        ILLEGAL;
    }

    addr = load_reg(s, UCOP_REG_N);

    n = 0;
    for (i = 0; i < 8; i++) {
        if (UCOP_SET(i)) {
            n++;
        }
    }

    if (UCOP_SET_U) {
        if (UCOP_SET_P) { /* pre increment */
            tcg_gen_addi_i32(addr, addr, 4);
        } /* unnecessary to do anything when post increment */
    } else {
        if (UCOP_SET_P) { /* pre decrement */
            tcg_gen_addi_i32(addr, addr, -(n * 4));
        } else { /* post decrement */
            if (n != 1) {
                tcg_gen_addi_i32(addr, addr, -((n - 1) * 4));
            }
        }
    }

    freg = ((insn >> 8) & 3) << 3; /* freg should be 0, 8, 16, 24 */

    for (i = 0, j = 0; i < 8; i++, freg++) {
        if (!UCOP_SET(i)) {
            continue;
        }

        if (UCOP_SET_L) { /* load */
            tmp = gen_ld32(addr, IS_USER(s));
            ucf64_gen_st32(tmp, freg);
        } else { /* store */
            tmp = ucf64_gen_ld32(freg);
            gen_st32(tmp, addr, IS_USER(s));
        }

        j++;
        /* unnecessary to add after the last transfer */
        if (j != n) {
            tcg_gen_addi_i32(addr, addr, 4);
        }
    }

    if (UCOP_SET_W) { /* write back */
        if (UCOP_SET_U) {
            if (!UCOP_SET_P) { /* post increment */
                tcg_gen_addi_i32(addr, addr, 4);
            } /* unnecessary to do anything when pre increment */
        } else {
            if (UCOP_SET_P) {
                /* pre decrement */
                if (n != 1) {
                    tcg_gen_addi_i32(addr, addr, -((n - 1) * 4));
                }
            } else {
                /* post decrement */
                tcg_gen_addi_i32(addr, addr, -(n * 4));
            }
        }
        store_reg(s, UCOP_REG_N, addr);
    } else {
        dead_tmp(addr);
    }
}

/* UniCore-F64 mrc/mcr */
static void do_ucf64_trans(CPUUniCore32State *env, DisasContext *s, uint32_t insn)
{
    TCGv tmp;

    if ((insn & 0xfe0003ff) == 0xe2000000) {
        /* control register */
        if ((UCOP_REG_N != UC32_UCF64_FPSCR) || (UCOP_REG_D == 31)) {
            ILLEGAL;
        }
        if (UCOP_SET(24)) {
            /* CFF */
            tmp = new_tmp();
            gen_helper_ucf64_get_fpscr(tmp, cpu_env);
            store_reg(s, UCOP_REG_D, tmp);
        } else {
            /* CTF */
            tmp = load_reg(s, UCOP_REG_D);
            gen_helper_ucf64_set_fpscr(cpu_env, tmp);
            dead_tmp(tmp);
            gen_lookup_tb(s);
        }
        return;
    }
    if ((insn & 0xfe0003ff) == 0xe0000000) {
        /* general register */
        if (UCOP_REG_D == 31) {
            ILLEGAL;
        }
        if (UCOP_SET(24)) { /* MFF */
            tmp = ucf64_gen_ld32(UCOP_REG_N);
            store_reg(s, UCOP_REG_D, tmp);
        } else { /* MTF */
            tmp = load_reg(s, UCOP_REG_D);
            ucf64_gen_st32(tmp, UCOP_REG_N);
        }
        return;
    }
    if ((insn & 0xfb000000) == 0xe9000000) {
        /* MFFC */
        if (UCOP_REG_D != 31) {
            ILLEGAL;
        }
        if (UCOP_UCF64_COND & 0x8) {
            ILLEGAL;
        }

        tmp = new_tmp();
        tcg_gen_movi_i32(tmp, UCOP_UCF64_COND);
        if (UCOP_SET(26)) {
            tcg_gen_ld_i64(cpu_F0d, cpu_env, ucf64_reg_offset(UCOP_REG_N));
            tcg_gen_ld_i64(cpu_F1d, cpu_env, ucf64_reg_offset(UCOP_REG_M));
            gen_helper_ucf64_cmpd(cpu_F0d, cpu_F1d, tmp, cpu_env);
        } else {
            tcg_gen_ld_i32(cpu_F0s, cpu_env, ucf64_reg_offset(UCOP_REG_N));
            tcg_gen_ld_i32(cpu_F1s, cpu_env, ucf64_reg_offset(UCOP_REG_M));
            gen_helper_ucf64_cmps(cpu_F0s, cpu_F1s, tmp, cpu_env);
        }
        dead_tmp(tmp);
        return;
    }
    ILLEGAL;
}

/* UniCore-F64 convert instructions */
static void do_ucf64_fcvt(CPUUniCore32State *env, DisasContext *s, uint32_t insn)
{
    if (UCOP_UCF64_FMT == 3) {
        ILLEGAL;
    }
    if (UCOP_REG_N != 0) {
        ILLEGAL;
    }
    switch (UCOP_UCF64_FUNC) {
    case 0: /* cvt.s */
        switch (UCOP_UCF64_FMT) {
        case 1 /* d */:
            tcg_gen_ld_i64(cpu_F0d, cpu_env, ucf64_reg_offset(UCOP_REG_M));
            gen_helper_ucf64_df2sf(cpu_F0s, cpu_F0d, cpu_env);
            tcg_gen_st_i32(cpu_F0s, cpu_env, ucf64_reg_offset(UCOP_REG_D));
            break;
        case 2 /* w */:
            tcg_gen_ld_i32(cpu_F0s, cpu_env, ucf64_reg_offset(UCOP_REG_M));
            gen_helper_ucf64_si2sf(cpu_F0s, cpu_F0s, cpu_env);
            tcg_gen_st_i32(cpu_F0s, cpu_env, ucf64_reg_offset(UCOP_REG_D));
            break;
        default /* s */:
            ILLEGAL;
            break;
        }
        break;
    case 1: /* cvt.d */
        switch (UCOP_UCF64_FMT) {
        case 0 /* s */:
            tcg_gen_ld_i32(cpu_F0s, cpu_env, ucf64_reg_offset(UCOP_REG_M));
            gen_helper_ucf64_sf2df(cpu_F0d, cpu_F0s, cpu_env);
            tcg_gen_st_i64(cpu_F0d, cpu_env, ucf64_reg_offset(UCOP_REG_D));
            break;
        case 2 /* w */:
            tcg_gen_ld_i32(cpu_F0s, cpu_env, ucf64_reg_offset(UCOP_REG_M));
            gen_helper_ucf64_si2df(cpu_F0d, cpu_F0s, cpu_env);
            tcg_gen_st_i64(cpu_F0d, cpu_env, ucf64_reg_offset(UCOP_REG_D));
            break;
        default /* d */:
            ILLEGAL;
            break;
        }
        break;
    case 4: /* cvt.w */
        switch (UCOP_UCF64_FMT) {
        case 0 /* s */:
            tcg_gen_ld_i32(cpu_F0s, cpu_env, ucf64_reg_offset(UCOP_REG_M));
            gen_helper_ucf64_sf2si(cpu_F0s, cpu_F0s, cpu_env);
            tcg_gen_st_i32(cpu_F0s, cpu_env, ucf64_reg_offset(UCOP_REG_D));
            break;
        case 1 /* d */:
            tcg_gen_ld_i64(cpu_F0d, cpu_env, ucf64_reg_offset(UCOP_REG_M));
            gen_helper_ucf64_df2si(cpu_F0s, cpu_F0d, cpu_env);
            tcg_gen_st_i32(cpu_F0s, cpu_env, ucf64_reg_offset(UCOP_REG_D));
            break;
    default /* w */:
            ILLEGAL;
            break;
        }
        break;
    default:
        ILLEGAL;
    }
}

/* UniCore-F64 compare instructions */
static void do_ucf64_fcmp(CPUUniCore32State *env, DisasContext *s, uint32_t insn)
{
    if (UCOP_SET(25)) {
        ILLEGAL;
    }
    if (UCOP_REG_D != 0) {
        ILLEGAL;
    }

    ILLEGAL; /* TODO */
    if (UCOP_SET(24)) {
        tcg_gen_ld_i64(cpu_F0d, cpu_env, ucf64_reg_offset(UCOP_REG_N));
        tcg_gen_ld_i64(cpu_F1d, cpu_env, ucf64_reg_offset(UCOP_REG_M));
        /* gen_helper_ucf64_cmpd(cpu_F0d, cpu_F1d, cpu_env); */
    } else {
        tcg_gen_ld_i32(cpu_F0s, cpu_env, ucf64_reg_offset(UCOP_REG_N));
        tcg_gen_ld_i32(cpu_F1s, cpu_env, ucf64_reg_offset(UCOP_REG_M));
        /* gen_helper_ucf64_cmps(cpu_F0s, cpu_F1s, cpu_env); */
    }
}

#define gen_helper_ucf64_movs(x, y)      do { } while (0)
#define gen_helper_ucf64_movd(x, y)      do { } while (0)

#define UCF64_OP1(name)    do {                           \
        if (UCOP_REG_N != 0) {                            \
            ILLEGAL;                                      \
        }                                                 \
        switch (UCOP_UCF64_FMT) {                         \
        case 0 /* s */:                                   \
            tcg_gen_ld_i32(cpu_F0s, cpu_env,              \
                           ucf64_reg_offset(UCOP_REG_M)); \
            gen_helper_ucf64_##name##s(cpu_F0s, cpu_F0s); \
            tcg_gen_st_i32(cpu_F0s, cpu_env,              \
                           ucf64_reg_offset(UCOP_REG_D)); \
            break;                                        \
        case 1 /* d */:                                   \
            tcg_gen_ld_i64(cpu_F0d, cpu_env,              \
                           ucf64_reg_offset(UCOP_REG_M)); \
            gen_helper_ucf64_##name##d(cpu_F0d, cpu_F0d); \
            tcg_gen_st_i64(cpu_F0d, cpu_env,              \
                           ucf64_reg_offset(UCOP_REG_D)); \
            break;                                        \
        case 2 /* w */:                                   \
            ILLEGAL;                                      \
            break;                                        \
        }                                                 \
    } while (0)

#define UCF64_OP2(name)    do {                           \
        switch (UCOP_UCF64_FMT) {                         \
        case 0 /* s */:                                   \
            tcg_gen_ld_i32(cpu_F0s, cpu_env,              \
                           ucf64_reg_offset(UCOP_REG_N)); \
            tcg_gen_ld_i32(cpu_F1s, cpu_env,              \
                           ucf64_reg_offset(UCOP_REG_M)); \
            gen_helper_ucf64_##name##s(cpu_F0s,           \
                           cpu_F0s, cpu_F1s, cpu_env);    \
            tcg_gen_st_i32(cpu_F0s, cpu_env,              \
                           ucf64_reg_offset(UCOP_REG_D)); \
            break;                                        \
        case 1 /* d */:                                   \
            tcg_gen_ld_i64(cpu_F0d, cpu_env,              \
                           ucf64_reg_offset(UCOP_REG_N)); \
            tcg_gen_ld_i64(cpu_F1d, cpu_env,              \
                           ucf64_reg_offset(UCOP_REG_M)); \
            gen_helper_ucf64_##name##d(cpu_F0d,           \
                           cpu_F0d, cpu_F1d, cpu_env);    \
            tcg_gen_st_i64(cpu_F0d, cpu_env,              \
                           ucf64_reg_offset(UCOP_REG_D)); \
            break;                                        \
        case 2 /* w */:                                   \
            ILLEGAL;                                      \
            break;                                        \
        }                                                 \
    } while (0)

/* UniCore-F64 data processing */
static void do_ucf64_datap(CPUUniCore32State *env, DisasContext *s, uint32_t insn)
{
    if (UCOP_UCF64_FMT == 3) {
        ILLEGAL;
    }
    switch (UCOP_UCF64_FUNC) {
    case 0: /* add */
        UCF64_OP2(add);
        break;
    case 1: /* sub */
        UCF64_OP2(sub);
        break;
    case 2: /* mul */
        UCF64_OP2(mul);
        break;
    case 4: /* div */
        UCF64_OP2(div);
        break;
    case 5: /* abs */
        UCF64_OP1(abs);
        break;
    case 6: /* mov */
        UCF64_OP1(mov);
        break;
    case 7: /* neg */
        UCF64_OP1(neg);
        break;
    default:
        ILLEGAL;
    }
}

/* Disassemble an F64 instruction */
static void disas_ucf64_insn(CPUUniCore32State *env, DisasContext *s, uint32_t insn)
{
    if (!UCOP_SET(29)) {
        if (UCOP_SET(26)) {
            do_ucf64_ldst_m(env, s, insn);
        } else {
            do_ucf64_ldst_i(env, s, insn);
        }
    } else {
        if (UCOP_SET(5)) {
            switch ((insn >> 26) & 0x3) {
            case 0:
                do_ucf64_datap(env, s, insn);
                break;
            case 1:
                ILLEGAL;
                break;
            case 2:
                do_ucf64_fcvt(env, s, insn);
                break;
            case 3:
                do_ucf64_fcmp(env, s, insn);
                break;
            }
        } else {
            do_ucf64_trans(env, s, insn);
        }
    }
}

static inline bool use_goto_tb(DisasContext *s, uint32_t dest)
{
#ifndef CONFIG_USER_ONLY
    return (s->tb->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK);
#else
    return true;
#endif
}

static inline void gen_goto_tb(DisasContext *s, int n, uint32_t dest)
{
    if (use_goto_tb(s, dest)) {
        tcg_gen_goto_tb(n);
        gen_set_pc_im(dest);
        tcg_gen_exit_tb(s->tb, n);
    } else {
        gen_set_pc_im(dest);
        tcg_gen_exit_tb(NULL, 0);
    }
}

static inline void gen_jmp(DisasContext *s, uint32_t dest)
{
    if (unlikely(s->singlestep_enabled)) {
        /* An indirect jump so that we still trigger the debug exception.  */
        gen_bx_im(s, dest);
    } else {
        gen_goto_tb(s, 0, dest);
        s->is_jmp = DISAS_TB_JUMP;
    }
}

/* Returns nonzero if access to the PSR is not permitted. Marks t0 as dead. */
static int gen_set_psr(DisasContext *s, uint32_t mask, int bsr, TCGv t0)
{
    TCGv tmp;
    if (bsr) {
        /* ??? This is also undefined in system mode.  */
        if (IS_USER(s)) {
            return 1;
        }

        tmp = load_cpu_field(bsr);
        tcg_gen_andi_i32(tmp, tmp, ~mask);
        tcg_gen_andi_i32(t0, t0, mask);
        tcg_gen_or_i32(tmp, tmp, t0);
        store_cpu_field(tmp, bsr);
    } else {
        gen_set_asr(t0, mask);
    }
    dead_tmp(t0);
    gen_lookup_tb(s);
    return 0;
}

/* Generate an old-style exception return. Marks pc as dead. */
static void gen_exception_return(DisasContext *s, TCGv pc)
{
    TCGv tmp;
    store_reg(s, 31, pc);
    tmp = load_cpu_field(bsr);
    gen_set_asr(tmp, 0xffffffff);
    dead_tmp(tmp);
    s->is_jmp = DISAS_UPDATE;
}

static void disas_coproc_insn(CPUUniCore32State *env, DisasContext *s,
        uint32_t insn)
{
    switch (UCOP_CPNUM) {
#ifndef CONFIG_USER_ONLY
    case 0:
        disas_cp0_insn(env, s, insn);
        break;
    case 1:
        disas_ocd_insn(env, s, insn);
        break;
#endif
    case 2:
        disas_ucf64_insn(env, s, insn);
        break;
    default:
        /* Unknown coprocessor. */
        cpu_abort(env_cpu(env), "Unknown coprocessor!");
    }
}

/* data processing instructions */
static void do_datap(CPUUniCore32State *env, DisasContext *s, uint32_t insn)
{
    TCGv tmp;
    TCGv tmp2;
    int logic_cc;

    if (UCOP_OPCODES == 0x0f || UCOP_OPCODES == 0x0d) {
        if (UCOP_SET(23)) { /* CMOV instructions */
            if ((UCOP_CMOV_COND == 0xe) || (UCOP_CMOV_COND == 0xf)) {
                ILLEGAL;
            }
            /* if not always execute, we generate a conditional jump to
               next instruction */
            s->condlabel = gen_new_label();
            gen_test_cc(UCOP_CMOV_COND ^ 1, s->condlabel);
            s->condjmp = 1;
        }
    }

    logic_cc = table_logic_cc[UCOP_OPCODES] & (UCOP_SET_S >> 24);

    if (UCOP_SET(29)) {
        unsigned int val;
        /* immediate operand */
        val = UCOP_IMM_9;
        if (UCOP_SH_IM) {
            val = (val >> UCOP_SH_IM) | (val << (32 - UCOP_SH_IM));
        }
        tmp2 = new_tmp();
        tcg_gen_movi_i32(tmp2, val);
        if (logic_cc && UCOP_SH_IM) {
            gen_set_CF_bit31(tmp2);
        }
   } else {
        /* register */
        tmp2 = load_reg(s, UCOP_REG_M);
        if (UCOP_SET(5)) {
            tmp = load_reg(s, UCOP_REG_S);
            gen_uc32_shift_reg(tmp2, UCOP_SH_OP, tmp, logic_cc);
        } else {
            gen_uc32_shift_im(tmp2, UCOP_SH_OP, UCOP_SH_IM, logic_cc);
        }
    }

    if (UCOP_OPCODES != 0x0f && UCOP_OPCODES != 0x0d) {
        tmp = load_reg(s, UCOP_REG_N);
    } else {
        tmp = NULL;
    }

    switch (UCOP_OPCODES) {
    case 0x00:
        tcg_gen_and_i32(tmp, tmp, tmp2);
        if (logic_cc) {
            gen_logic_CC(tmp);
        }
        store_reg_bx(s, UCOP_REG_D, tmp);
        break;
    case 0x01:
        tcg_gen_xor_i32(tmp, tmp, tmp2);
        if (logic_cc) {
            gen_logic_CC(tmp);
        }
        store_reg_bx(s, UCOP_REG_D, tmp);
        break;
    case 0x02:
        if (UCOP_SET_S && UCOP_REG_D == 31) {
            /* SUBS r31, ... is used for exception return.  */
            if (IS_USER(s)) {
                ILLEGAL;
            }
            gen_helper_sub_cc(tmp, cpu_env, tmp, tmp2);
            gen_exception_return(s, tmp);
        } else {
            if (UCOP_SET_S) {
                gen_helper_sub_cc(tmp, cpu_env, tmp, tmp2);
            } else {
                tcg_gen_sub_i32(tmp, tmp, tmp2);
            }
            store_reg_bx(s, UCOP_REG_D, tmp);
        }
        break;
    case 0x03:
        if (UCOP_SET_S) {
            gen_helper_sub_cc(tmp, cpu_env, tmp2, tmp);
        } else {
            tcg_gen_sub_i32(tmp, tmp2, tmp);
        }
        store_reg_bx(s, UCOP_REG_D, tmp);
        break;
    case 0x04:
        if (UCOP_SET_S) {
            gen_helper_add_cc(tmp, cpu_env, tmp, tmp2);
        } else {
            tcg_gen_add_i32(tmp, tmp, tmp2);
        }
        store_reg_bx(s, UCOP_REG_D, tmp);
        break;
    case 0x05:
        if (UCOP_SET_S) {
            gen_helper_adc_cc(tmp, cpu_env, tmp, tmp2);
        } else {
            gen_add_carry(tmp, tmp, tmp2);
        }
        store_reg_bx(s, UCOP_REG_D, tmp);
        break;
    case 0x06:
        if (UCOP_SET_S) {
            gen_helper_sbc_cc(tmp, cpu_env, tmp, tmp2);
        } else {
            gen_sub_carry(tmp, tmp, tmp2);
        }
        store_reg_bx(s, UCOP_REG_D, tmp);
        break;
    case 0x07:
        if (UCOP_SET_S) {
            gen_helper_sbc_cc(tmp, cpu_env, tmp2, tmp);
        } else {
            gen_sub_carry(tmp, tmp2, tmp);
        }
        store_reg_bx(s, UCOP_REG_D, tmp);
        break;
    case 0x08:
        if (UCOP_SET_S) {
            tcg_gen_and_i32(tmp, tmp, tmp2);
            gen_logic_CC(tmp);
        }
        dead_tmp(tmp);
        break;
    case 0x09:
        if (UCOP_SET_S) {
            tcg_gen_xor_i32(tmp, tmp, tmp2);
            gen_logic_CC(tmp);
        }
        dead_tmp(tmp);
        break;
    case 0x0a:
        if (UCOP_SET_S) {
            gen_helper_sub_cc(tmp, cpu_env, tmp, tmp2);
        }
        dead_tmp(tmp);
        break;
    case 0x0b:
        if (UCOP_SET_S) {
            gen_helper_add_cc(tmp, cpu_env, tmp, tmp2);
        }
        dead_tmp(tmp);
        break;
    case 0x0c:
        tcg_gen_or_i32(tmp, tmp, tmp2);
        if (logic_cc) {
            gen_logic_CC(tmp);
        }
        store_reg_bx(s, UCOP_REG_D, tmp);
        break;
    case 0x0d:
        if (logic_cc && UCOP_REG_D == 31) {
            /* MOVS r31, ... is used for exception return.  */
            if (IS_USER(s)) {
                ILLEGAL;
            }
            gen_exception_return(s, tmp2);
        } else {
            if (logic_cc) {
                gen_logic_CC(tmp2);
            }
            store_reg_bx(s, UCOP_REG_D, tmp2);
        }
        break;
    case 0x0e:
        tcg_gen_andc_i32(tmp, tmp, tmp2);
        if (logic_cc) {
            gen_logic_CC(tmp);
        }
        store_reg_bx(s, UCOP_REG_D, tmp);
        break;
    default:
    case 0x0f:
        tcg_gen_not_i32(tmp2, tmp2);
        if (logic_cc) {
            gen_logic_CC(tmp2);
        }
        store_reg_bx(s, UCOP_REG_D, tmp2);
        break;
    }
    if (UCOP_OPCODES != 0x0f && UCOP_OPCODES != 0x0d) {
        dead_tmp(tmp2);
    }
}

/* multiply */
static void do_mult(CPUUniCore32State *env, DisasContext *s, uint32_t insn)
{
    TCGv tmp, tmp2, tmp3, tmp4;

    if (UCOP_SET(27)) {
        /* 64 bit mul */
        tmp = load_reg(s, UCOP_REG_M);
        tmp2 = load_reg(s, UCOP_REG_N);
        if (UCOP_SET(26)) {
            tcg_gen_muls2_i32(tmp, tmp2, tmp, tmp2);
        } else {
            tcg_gen_mulu2_i32(tmp, tmp2, tmp, tmp2);
        }
        if (UCOP_SET(25)) { /* mult accumulate */
            tmp3 = load_reg(s, UCOP_REG_LO);
            tmp4 = load_reg(s, UCOP_REG_HI);
            tcg_gen_add2_i32(tmp, tmp2, tmp, tmp2, tmp3, tmp4);
            dead_tmp(tmp3);
            dead_tmp(tmp4);
        }
        store_reg(s, UCOP_REG_LO, tmp);
        store_reg(s, UCOP_REG_HI, tmp2);
    } else {
        /* 32 bit mul */
        tmp = load_reg(s, UCOP_REG_M);
        tmp2 = load_reg(s, UCOP_REG_N);
        tcg_gen_mul_i32(tmp, tmp, tmp2);
        dead_tmp(tmp2);
        if (UCOP_SET(25)) {
            /* Add */
            tmp2 = load_reg(s, UCOP_REG_S);
            tcg_gen_add_i32(tmp, tmp, tmp2);
            dead_tmp(tmp2);
        }
        if (UCOP_SET_S) {
            gen_logic_CC(tmp);
        }
        store_reg(s, UCOP_REG_D, tmp);
    }
}

/* miscellaneous instructions */
static void do_misc(CPUUniCore32State *env, DisasContext *s, uint32_t insn)
{
    unsigned int val;
    TCGv tmp;

    if ((insn & 0xffffffe0) == 0x10ffc120) {
        /* Trivial implementation equivalent to bx.  */
        tmp = load_reg(s, UCOP_REG_M);
        gen_bx(s, tmp);
        return;
    }

    if ((insn & 0xfbffc000) == 0x30ffc000) {
        /* PSR = immediate */
        val = UCOP_IMM_9;
        if (UCOP_SH_IM) {
            val = (val >> UCOP_SH_IM) | (val << (32 - UCOP_SH_IM));
        }
        tmp = new_tmp();
        tcg_gen_movi_i32(tmp, val);
        if (gen_set_psr(s, ~ASR_RESERVED, UCOP_SET_B, tmp)) {
            ILLEGAL;
        }
        return;
    }

    if ((insn & 0xfbffffe0) == 0x12ffc020) {
        /* PSR.flag = reg */
        tmp = load_reg(s, UCOP_REG_M);
        if (gen_set_psr(s, ASR_NZCV, UCOP_SET_B, tmp)) {
            ILLEGAL;
        }
        return;
    }

    if ((insn & 0xfbffffe0) == 0x10ffc020) {
        /* PSR = reg */
        tmp = load_reg(s, UCOP_REG_M);
        if (gen_set_psr(s, ~ASR_RESERVED, UCOP_SET_B, tmp)) {
            ILLEGAL;
        }
        return;
    }

    if ((insn & 0xfbf83fff) == 0x10f80000) {
        /* reg = PSR */
        if (UCOP_SET_B) {
            if (IS_USER(s)) {
                ILLEGAL;
            }
            tmp = load_cpu_field(bsr);
        } else {
            tmp = new_tmp();
            gen_helper_asr_read(tmp, cpu_env);
        }
        store_reg(s, UCOP_REG_D, tmp);
        return;
    }

    if ((insn & 0xfbf83fe0) == 0x12f80120) {
        /* clz */
        tmp = load_reg(s, UCOP_REG_M);
        if (UCOP_SET(26)) {
            /* clo */
            tcg_gen_not_i32(tmp, tmp);
        }
        tcg_gen_clzi_i32(tmp, tmp, 32);
        store_reg(s, UCOP_REG_D, tmp);
        return;
    }

    /* otherwise */
    ILLEGAL;
}

/* load/store I_offset and R_offset */
static void do_ldst_ir(CPUUniCore32State *env, DisasContext *s, uint32_t insn)
{
    unsigned int mmu_idx;
    TCGv tmp;
    TCGv tmp2;

    tmp2 = load_reg(s, UCOP_REG_N);
    mmu_idx = (IS_USER(s) || (!UCOP_SET_P && UCOP_SET_W));

    /* immediate */
    if (UCOP_SET_P) {
        gen_add_data_offset(s, insn, tmp2);
    }

    if (UCOP_SET_L) {
        /* load */
        if (UCOP_SET_B) {
            tmp = gen_ld8u(tmp2, mmu_idx);
        } else {
            tmp = gen_ld32(tmp2, mmu_idx);
        }
    } else {
        /* store */
        tmp = load_reg(s, UCOP_REG_D);
        if (UCOP_SET_B) {
            gen_st8(tmp, tmp2, mmu_idx);
        } else {
            gen_st32(tmp, tmp2, mmu_idx);
        }
    }
    if (!UCOP_SET_P) {
        gen_add_data_offset(s, insn, tmp2);
        store_reg(s, UCOP_REG_N, tmp2);
    } else if (UCOP_SET_W) {
        store_reg(s, UCOP_REG_N, tmp2);
    } else {
        dead_tmp(tmp2);
    }
    if (UCOP_SET_L) {
        /* Complete the load.  */
        if (UCOP_REG_D == 31) {
            gen_bx(s, tmp);
        } else {
            store_reg(s, UCOP_REG_D, tmp);
        }
    }
}

/* SWP instruction */
static void do_swap(CPUUniCore32State *env, DisasContext *s, uint32_t insn)
{
    TCGv addr;
    TCGv tmp;
    TCGv tmp2;

    if ((insn & 0xff003fe0) != 0x40000120) {
        ILLEGAL;
    }

    /* ??? This is not really atomic.  However we know
       we never have multiple CPUs running in parallel,
       so it is good enough.  */
    addr = load_reg(s, UCOP_REG_N);
    tmp = load_reg(s, UCOP_REG_M);
    if (UCOP_SET_B) {
        tmp2 = gen_ld8u(addr, IS_USER(s));
        gen_st8(tmp, addr, IS_USER(s));
    } else {
        tmp2 = gen_ld32(addr, IS_USER(s));
        gen_st32(tmp, addr, IS_USER(s));
    }
    dead_tmp(addr);
    store_reg(s, UCOP_REG_D, tmp2);
}

/* load/store hw/sb */
static void do_ldst_hwsb(CPUUniCore32State *env, DisasContext *s, uint32_t insn)
{
    TCGv addr;
    TCGv tmp;

    if (UCOP_SH_OP == 0) {
        do_swap(env, s, insn);
        return;
    }

    addr = load_reg(s, UCOP_REG_N);
    if (UCOP_SET_P) {
        gen_add_datah_offset(s, insn, addr);
    }

    if (UCOP_SET_L) { /* load */
        switch (UCOP_SH_OP) {
        case 1:
            tmp = gen_ld16u(addr, IS_USER(s));
            break;
        case 2:
            tmp = gen_ld8s(addr, IS_USER(s));
            break;
        default: /* see do_swap */
        case 3:
            tmp = gen_ld16s(addr, IS_USER(s));
            break;
        }
    } else { /* store */
        if (UCOP_SH_OP != 1) {
            ILLEGAL;
        }
        tmp = load_reg(s, UCOP_REG_D);
        gen_st16(tmp, addr, IS_USER(s));
    }
    /* Perform base writeback before the loaded value to
       ensure correct behavior with overlapping index registers. */
    if (!UCOP_SET_P) {
        gen_add_datah_offset(s, insn, addr);
        store_reg(s, UCOP_REG_N, addr);
    } else if (UCOP_SET_W) {
        store_reg(s, UCOP_REG_N, addr);
    } else {
        dead_tmp(addr);
    }
    if (UCOP_SET_L) {
        /* Complete the load.  */
        store_reg(s, UCOP_REG_D, tmp);
    }
}

/* load/store multiple words */
static void do_ldst_m(CPUUniCore32State *env, DisasContext *s, uint32_t insn)
{
    unsigned int val, i, mmu_idx;
    int j, n, reg, user, loaded_base;
    TCGv tmp;
    TCGv tmp2;
    TCGv addr;
    TCGv loaded_var;

    if (UCOP_SET(7)) {
        ILLEGAL;
    }
    /* XXX: store correct base if write back */
    user = 0;
    if (UCOP_SET_B) { /* S bit in instruction table */
        if (IS_USER(s)) {
            ILLEGAL; /* only usable in supervisor mode */
        }
        if (UCOP_SET(18) == 0) { /* pc reg */
            user = 1;
        }
    }

    mmu_idx = (IS_USER(s) || (!UCOP_SET_P && UCOP_SET_W));
    addr = load_reg(s, UCOP_REG_N);

    /* compute total size */
    loaded_base = 0;
    loaded_var = NULL;
    n = 0;
    for (i = 0; i < 6; i++) {
        if (UCOP_SET(i)) {
            n++;
        }
    }
    for (i = 9; i < 19; i++) {
        if (UCOP_SET(i)) {
            n++;
        }
    }
    /* XXX: test invalid n == 0 case ? */
    if (UCOP_SET_U) {
        if (UCOP_SET_P) {
            /* pre increment */
            tcg_gen_addi_i32(addr, addr, 4);
        } else {
            /* post increment */
        }
    } else {
        if (UCOP_SET_P) {
            /* pre decrement */
            tcg_gen_addi_i32(addr, addr, -(n * 4));
        } else {
            /* post decrement */
            if (n != 1) {
                tcg_gen_addi_i32(addr, addr, -((n - 1) * 4));
            }
        }
    }

    j = 0;
    reg = UCOP_SET(6) ? 16 : 0;
    for (i = 0; i < 19; i++, reg++) {
        if (i == 6) {
            i = i + 3;
        }
        if (UCOP_SET(i)) {
            if (UCOP_SET_L) { /* load */
                tmp = gen_ld32(addr, mmu_idx);
                if (reg == 31) {
                    gen_bx(s, tmp);
                } else if (user) {
                    tmp2 = tcg_const_i32(reg);
                    gen_helper_set_user_reg(cpu_env, tmp2, tmp);
                    tcg_temp_free_i32(tmp2);
                    dead_tmp(tmp);
                } else if (reg == UCOP_REG_N) {
                    loaded_var = tmp;
                    loaded_base = 1;
                } else {
                    store_reg(s, reg, tmp);
                }
            } else { /* store */
                if (reg == 31) {
                    /* special case: r31 = PC + 4 */
                    val = (long)s->pc;
                    tmp = new_tmp();
                    tcg_gen_movi_i32(tmp, val);
                } else if (user) {
                    tmp = new_tmp();
                    tmp2 = tcg_const_i32(reg);
                    gen_helper_get_user_reg(tmp, cpu_env, tmp2);
                    tcg_temp_free_i32(tmp2);
                } else {
                    tmp = load_reg(s, reg);
                }
                gen_st32(tmp, addr, mmu_idx);
            }
            j++;
            /* no need to add after the last transfer */
            if (j != n) {
                tcg_gen_addi_i32(addr, addr, 4);
            }
        }
    }
    if (UCOP_SET_W) { /* write back */
        if (UCOP_SET_U) {
            if (UCOP_SET_P) {
                /* pre increment */
            } else {
                /* post increment */
                tcg_gen_addi_i32(addr, addr, 4);
            }
        } else {
            if (UCOP_SET_P) {
                /* pre decrement */
                if (n != 1) {
                    tcg_gen_addi_i32(addr, addr, -((n - 1) * 4));
                }
            } else {
                /* post decrement */
                tcg_gen_addi_i32(addr, addr, -(n * 4));
            }
        }
        store_reg(s, UCOP_REG_N, addr);
    } else {
        dead_tmp(addr);
    }
    if (loaded_base) {
        store_reg(s, UCOP_REG_N, loaded_var);
    }
    if (UCOP_SET_B && !user) {
        /* Restore ASR from BSR.  */
        tmp = load_cpu_field(bsr);
        gen_set_asr(tmp, 0xffffffff);
        dead_tmp(tmp);
        s->is_jmp = DISAS_UPDATE;
    }
}

/* branch (and link) */
static void do_branch(CPUUniCore32State *env, DisasContext *s, uint32_t insn)
{
    unsigned int val;
    int32_t offset;
    TCGv tmp;

    if (UCOP_COND == 0xf) {
        ILLEGAL;
    }

    if (UCOP_COND != 0xe) {
        /* if not always execute, we generate a conditional jump to
           next instruction */
        s->condlabel = gen_new_label();
        gen_test_cc(UCOP_COND ^ 1, s->condlabel);
        s->condjmp = 1;
    }

    val = (int32_t)s->pc;
    if (UCOP_SET_L) {
        tmp = new_tmp();
        tcg_gen_movi_i32(tmp, val);
        store_reg(s, 30, tmp);
    }
    offset = (((int32_t)insn << 8) >> 8);
    val += (offset << 2); /* unicore is pc+4 */
    gen_jmp(s, val);
}

static void disas_uc32_insn(CPUUniCore32State *env, DisasContext *s)
{
    unsigned int insn;

    insn = cpu_ldl_code(env, s->pc);
    s->pc += 4;

    /* UniCore instructions class:
     * AAAB BBBC xxxx xxxx xxxx xxxD xxEx xxxx
     * AAA  : see switch case
     * BBBB : opcodes or cond or PUBW
     * C    : S OR L
     * D    : 8
     * E    : 5
     */
    switch (insn >> 29) {
    case 0x0:
        if (UCOP_SET(5) && UCOP_SET(8) && !UCOP_SET(28)) {
            do_mult(env, s, insn);
            break;
        }

        if (UCOP_SET(8)) {
            do_misc(env, s, insn);
            break;
        }
    case 0x1:
        if (((UCOP_OPCODES >> 2) == 2) && !UCOP_SET_S) {
            do_misc(env, s, insn);
            break;
        }
        do_datap(env, s, insn);
        break;

    case 0x2:
        if (UCOP_SET(8) && UCOP_SET(5)) {
            do_ldst_hwsb(env, s, insn);
            break;
        }
        if (UCOP_SET(8) || UCOP_SET(5)) {
            ILLEGAL;
        }
    case 0x3:
        do_ldst_ir(env, s, insn);
        break;

    case 0x4:
        if (UCOP_SET(8)) {
            ILLEGAL; /* extended instructions */
        }
        do_ldst_m(env, s, insn);
        break;
    case 0x5:
        do_branch(env, s, insn);
        break;
    case 0x6:
        /* Coprocessor.  */
        disas_coproc_insn(env, s, insn);
        break;
    case 0x7:
        if (!UCOP_SET(28)) {
            disas_coproc_insn(env, s, insn);
            break;
        }
        if ((insn & 0xff000000) == 0xff000000) { /* syscall */
            gen_set_pc_im(s->pc);
            s->is_jmp = DISAS_SYSCALL;
            break;
        }
        ILLEGAL;
    }
}

/* generate intermediate code for basic block 'tb'.  */
void gen_intermediate_code(CPUState *cs, TranslationBlock *tb, int max_insns)
{
    CPUUniCore32State *env = cs->env_ptr;
    DisasContext dc1, *dc = &dc1;
    target_ulong pc_start;
    uint32_t page_start;
    int num_insns;

    /* generate intermediate code */
    num_temps = 0;

    pc_start = tb->pc;

    dc->tb = tb;

    dc->is_jmp = DISAS_NEXT;
    dc->pc = pc_start;
    dc->singlestep_enabled = cs->singlestep_enabled;
    dc->condjmp = 0;
    cpu_F0s = tcg_temp_new_i32();
    cpu_F1s = tcg_temp_new_i32();
    cpu_F0d = tcg_temp_new_i64();
    cpu_F1d = tcg_temp_new_i64();
    page_start = pc_start & TARGET_PAGE_MASK;
    num_insns = 0;

#ifndef CONFIG_USER_ONLY
    if ((env->uncached_asr & ASR_M) == ASR_MODE_USER) {
        dc->user = 1;
    } else {
        dc->user = 0;
    }
#endif

    gen_tb_start(tb);
    do {
        tcg_gen_insn_start(dc->pc);
        num_insns++;

        if (unlikely(cpu_breakpoint_test(cs, dc->pc, BP_ANY))) {
            gen_set_pc_im(dc->pc);
            gen_exception(EXCP_DEBUG);
            dc->is_jmp = DISAS_JUMP;
            /* The address covered by the breakpoint must be included in
               [tb->pc, tb->pc + tb->size) in order to for it to be
               properly cleared -- thus we increment the PC here so that
               the logic setting tb->size below does the right thing.  */
            dc->pc += 4;
            goto done_generating;
        }

        if (num_insns == max_insns && (tb_cflags(tb) & CF_LAST_IO)) {
            gen_io_start();
        }

        disas_uc32_insn(env, dc);

        if (num_temps) {
            fprintf(stderr, "Internal resource leak before %08x\n", dc->pc);
            num_temps = 0;
        }

        if (dc->condjmp && !dc->is_jmp) {
            gen_set_label(dc->condlabel);
            dc->condjmp = 0;
        }
        /* Translation stops when a conditional branch is encountered.
         * Otherwise the subsequent code could get translated several times.
         * Also stop translation when a page boundary is reached.  This
         * ensures prefetch aborts occur at the right place.  */
    } while (!dc->is_jmp && !tcg_op_buf_full() &&
             !cs->singlestep_enabled &&
             !singlestep &&
             dc->pc - page_start < TARGET_PAGE_SIZE &&
             num_insns < max_insns);

    if (tb_cflags(tb) & CF_LAST_IO) {
        if (dc->condjmp) {
            /* FIXME:  This can theoretically happen with self-modifying
               code.  */
            cpu_abort(cs, "IO on conditional branch instruction");
        }
        gen_io_end();
    }

    /* At this stage dc->condjmp will only be set when the skipped
       instruction was a conditional branch or trap, and the PC has
       already been written.  */
    if (unlikely(cs->singlestep_enabled)) {
        /* Make sure the pc is updated, and raise a debug exception.  */
        if (dc->condjmp) {
            if (dc->is_jmp == DISAS_SYSCALL) {
                gen_exception(UC32_EXCP_PRIV);
            } else {
                gen_exception(EXCP_DEBUG);
            }
            gen_set_label(dc->condlabel);
        }
        if (dc->condjmp || !dc->is_jmp) {
            gen_set_pc_im(dc->pc);
            dc->condjmp = 0;
        }
        if (dc->is_jmp == DISAS_SYSCALL && !dc->condjmp) {
            gen_exception(UC32_EXCP_PRIV);
        } else {
            gen_exception(EXCP_DEBUG);
        }
    } else {
        /* While branches must always occur at the end of an IT block,
           there are a few other things that can cause us to terminate
           the TB in the middel of an IT block:
            - Exception generating instructions (bkpt, swi, undefined).
            - Page boundaries.
            - Hardware watchpoints.
           Hardware breakpoints have already been handled and skip this code.
         */
        switch (dc->is_jmp) {
        case DISAS_NEXT:
            gen_goto_tb(dc, 1, dc->pc);
            break;
        default:
        case DISAS_JUMP:
        case DISAS_UPDATE:
            /* indicate that the hash table must be used to find the next TB */
            tcg_gen_exit_tb(NULL, 0);
            break;
        case DISAS_TB_JUMP:
            /* nothing more to generate */
            break;
        case DISAS_SYSCALL:
            gen_exception(UC32_EXCP_PRIV);
            break;
        }
        if (dc->condjmp) {
            gen_set_label(dc->condlabel);
            gen_goto_tb(dc, 1, dc->pc);
            dc->condjmp = 0;
        }
    }

done_generating:
    gen_tb_end(tb, num_insns);

#ifdef DEBUG_DISAS
    if (qemu_loglevel_mask(CPU_LOG_TB_IN_ASM)
        && qemu_log_in_addr_range(pc_start)) {
        qemu_log_lock();
        qemu_log("----------------\n");
        qemu_log("IN: %s\n", lookup_symbol(pc_start));
        log_target_disas(cs, pc_start, dc->pc - pc_start);
        qemu_log("\n");
        qemu_log_unlock();
    }
#endif
    tb->size = dc->pc - pc_start;
    tb->icount = num_insns;
}

static const char *cpu_mode_names[16] = {
    "USER", "REAL", "INTR", "PRIV", "UM14", "UM15", "UM16", "TRAP",
    "UM18", "UM19", "UM1A", "EXTN", "UM1C", "UM1D", "UM1E", "SUSR"
};

#undef UCF64_DUMP_STATE
#ifdef UCF64_DUMP_STATE
static void cpu_dump_state_ucf64(CPUUniCore32State *env, int flags)
{
    int i;
    union {
        uint32_t i;
        float s;
    } s0, s1;
    CPU_DoubleU d;
    /* ??? This assumes float64 and double have the same layout.
       Oh well, it's only debug dumps.  */
    union {
        float64 f64;
        double d;
    } d0;

    for (i = 0; i < 16; i++) {
        d.d = env->ucf64.regs[i];
        s0.i = d.l.lower;
        s1.i = d.l.upper;
        d0.f64 = d.d;
        qemu_fprintf(f, "s%02d=%08x(%8g) s%02d=%08x(%8g)",
                     i * 2, (int)s0.i, s0.s,
                     i * 2 + 1, (int)s1.i, s1.s);
        qemu_fprintf(f, " d%02d=%" PRIx64 "(%8g)\n",
                     i, (uint64_t)d0.f64, d0.d);
    }
    qemu_fprintf(f, "FPSCR: %08x\n", (int)env->ucf64.xregs[UC32_UCF64_FPSCR]);
}
#else
#define cpu_dump_state_ucf64(env, file, pr, flags)      do { } while (0)
#endif

void uc32_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    UniCore32CPU *cpu = UNICORE32_CPU(cs);
    CPUUniCore32State *env = &cpu->env;
    int i;
    uint32_t psr;

    for (i = 0; i < 32; i++) {
        qemu_fprintf(f, "R%02d=%08x", i, env->regs[i]);
        if ((i % 4) == 3) {
            qemu_fprintf(f, "\n");
        } else {
            qemu_fprintf(f, " ");
        }
    }
    psr = cpu_asr_read(env);
    qemu_fprintf(f, "PSR=%08x %c%c%c%c %s\n",
                 psr,
                 psr & (1 << 31) ? 'N' : '-',
                 psr & (1 << 30) ? 'Z' : '-',
                 psr & (1 << 29) ? 'C' : '-',
                 psr & (1 << 28) ? 'V' : '-',
                 cpu_mode_names[psr & 0xf]);

    if (flags & CPU_DUMP_FPU) {
        cpu_dump_state_ucf64(env, f, cpu_fprintf, flags);
    }
}

void restore_state_to_opc(CPUUniCore32State *env, TranslationBlock *tb,
                          target_ulong *data)
{
    env->regs[31] = data[0];
}
