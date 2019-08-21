/*
 * Altera Nios II emulation for qemu: main translation routines.
 *
 * Copyright (C) 2016 Marek Vasut <marex@denx.de>
 * Copyright (C) 2012 Chris Wulff <crwulff@gmail.com>
 * Copyright (C) 2010 Tobias Klauser <tklauser@distanz.ch>
 *  (Portions of this file that were originally from nios2sim-ng.)
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
#include "tcg-op.h"
#include "exec/exec-all.h"
#include "disas/disas.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "exec/log.h"
#include "exec/cpu_ldst.h"
#include "exec/translator.h"
#include "qemu/qemu-print.h"

/* is_jmp field values */
#define DISAS_JUMP    DISAS_TARGET_0 /* only pc was modified dynamically */
#define DISAS_UPDATE  DISAS_TARGET_1 /* cpu state was modified dynamically */
#define DISAS_TB_JUMP DISAS_TARGET_2 /* only pc was modified statically */

#define INSTRUCTION_FLG(func, flags) { (func), (flags) }
#define INSTRUCTION(func)                  \
        INSTRUCTION_FLG(func, 0)
#define INSTRUCTION_NOP()                  \
        INSTRUCTION_FLG(nop, 0)
#define INSTRUCTION_UNIMPLEMENTED()        \
        INSTRUCTION_FLG(gen_excp, EXCP_UNIMPL)
#define INSTRUCTION_ILLEGAL()              \
        INSTRUCTION_FLG(gen_excp, EXCP_ILLEGAL)

/* Special R-Type instruction opcode */
#define INSN_R_TYPE 0x3A

/* I-Type instruction parsing */
#define I_TYPE(instr, code)                \
    struct {                               \
        uint8_t op;                        \
        union {                            \
            uint16_t u;                    \
            int16_t s;                     \
        } imm16;                           \
        uint8_t b;                         \
        uint8_t a;                         \
    } (instr) = {                          \
        .op    = extract32((code), 0, 6),  \
        .imm16.u = extract32((code), 6, 16), \
        .b     = extract32((code), 22, 5), \
        .a     = extract32((code), 27, 5), \
    }

/* R-Type instruction parsing */
#define R_TYPE(instr, code)                \
    struct {                               \
        uint8_t op;                        \
        uint8_t imm5;                      \
        uint8_t opx;                       \
        uint8_t c;                         \
        uint8_t b;                         \
        uint8_t a;                         \
    } (instr) = {                          \
        .op    = extract32((code), 0, 6),  \
        .imm5  = extract32((code), 6, 5),  \
        .opx   = extract32((code), 11, 6), \
        .c     = extract32((code), 17, 5), \
        .b     = extract32((code), 22, 5), \
        .a     = extract32((code), 27, 5), \
    }

/* J-Type instruction parsing */
#define J_TYPE(instr, code)                \
    struct {                               \
        uint8_t op;                        \
        uint32_t imm26;                    \
    } (instr) = {                          \
        .op    = extract32((code), 0, 6),  \
        .imm26 = extract32((code), 6, 26), \
    }

typedef struct DisasContext {
    TCGv_ptr          cpu_env;
    TCGv             *cpu_R;
    TCGv_i32          zero;
    int               is_jmp;
    target_ulong      pc;
    TranslationBlock *tb;
    int               mem_idx;
    bool              singlestep_enabled;
} DisasContext;

typedef struct Nios2Instruction {
    void     (*handler)(DisasContext *dc, uint32_t code, uint32_t flags);
    uint32_t  flags;
} Nios2Instruction;

static uint8_t get_opcode(uint32_t code)
{
    I_TYPE(instr, code);
    return instr.op;
}

static uint8_t get_opxcode(uint32_t code)
{
    R_TYPE(instr, code);
    return instr.opx;
}

static TCGv load_zero(DisasContext *dc)
{
    if (!dc->zero) {
        dc->zero = tcg_const_i32(0);
    }
    return dc->zero;
}

static TCGv load_gpr(DisasContext *dc, uint8_t reg)
{
    if (likely(reg != R_ZERO)) {
        return dc->cpu_R[reg];
    } else {
        return load_zero(dc);
    }
}

static void t_gen_helper_raise_exception(DisasContext *dc,
                                         uint32_t index)
{
    TCGv_i32 tmp = tcg_const_i32(index);

    tcg_gen_movi_tl(dc->cpu_R[R_PC], dc->pc);
    gen_helper_raise_exception(dc->cpu_env, tmp);
    tcg_temp_free_i32(tmp);
    dc->is_jmp = DISAS_UPDATE;
}

static bool use_goto_tb(DisasContext *dc, uint32_t dest)
{
    if (unlikely(dc->singlestep_enabled)) {
        return false;
    }

#ifndef CONFIG_USER_ONLY
    return (dc->tb->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK);
#else
    return true;
#endif
}

static void gen_goto_tb(DisasContext *dc, int n, uint32_t dest)
{
    TranslationBlock *tb = dc->tb;

    if (use_goto_tb(dc, dest)) {
        tcg_gen_goto_tb(n);
        tcg_gen_movi_tl(dc->cpu_R[R_PC], dest);
        tcg_gen_exit_tb(tb, n);
    } else {
        tcg_gen_movi_tl(dc->cpu_R[R_PC], dest);
        tcg_gen_exit_tb(NULL, 0);
    }
}

static void gen_excp(DisasContext *dc, uint32_t code, uint32_t flags)
{
    t_gen_helper_raise_exception(dc, flags);
}

static void gen_check_supervisor(DisasContext *dc)
{
    if (dc->tb->flags & CR_STATUS_U) {
        /* CPU in user mode, privileged instruction called, stop. */
        t_gen_helper_raise_exception(dc, EXCP_SUPERI);
    }
}

/*
 * Used as a placeholder for all instructions which do not have
 * an effect on the simulator (e.g. flush, sync)
 */
static void nop(DisasContext *dc, uint32_t code, uint32_t flags)
{
    /* Nothing to do here */
}

/*
 * J-Type instructions
 */
static void jmpi(DisasContext *dc, uint32_t code, uint32_t flags)
{
    J_TYPE(instr, code);
    gen_goto_tb(dc, 0, (dc->pc & 0xF0000000) | (instr.imm26 << 2));
    dc->is_jmp = DISAS_TB_JUMP;
}

static void call(DisasContext *dc, uint32_t code, uint32_t flags)
{
    tcg_gen_movi_tl(dc->cpu_R[R_RA], dc->pc + 4);
    jmpi(dc, code, flags);
}

/*
 * I-Type instructions
 */
/* Load instructions */
static void gen_ldx(DisasContext *dc, uint32_t code, uint32_t flags)
{
    I_TYPE(instr, code);

    TCGv addr = tcg_temp_new();
    TCGv data;

    /*
     * WARNING: Loads into R_ZERO are ignored, but we must generate the
     *          memory access itself to emulate the CPU precisely. Load
     *          from a protected page to R_ZERO will cause SIGSEGV on
     *          the Nios2 CPU.
     */
    if (likely(instr.b != R_ZERO)) {
        data = dc->cpu_R[instr.b];
    } else {
        data = tcg_temp_new();
    }

    tcg_gen_addi_tl(addr, load_gpr(dc, instr.a), instr.imm16.s);
    tcg_gen_qemu_ld_tl(data, addr, dc->mem_idx, flags);

    if (unlikely(instr.b == R_ZERO)) {
        tcg_temp_free(data);
    }

    tcg_temp_free(addr);
}

/* Store instructions */
static void gen_stx(DisasContext *dc, uint32_t code, uint32_t flags)
{
    I_TYPE(instr, code);
    TCGv val = load_gpr(dc, instr.b);

    TCGv addr = tcg_temp_new();
    tcg_gen_addi_tl(addr, load_gpr(dc, instr.a), instr.imm16.s);
    tcg_gen_qemu_st_tl(val, addr, dc->mem_idx, flags);
    tcg_temp_free(addr);
}

/* Branch instructions */
static void br(DisasContext *dc, uint32_t code, uint32_t flags)
{
    I_TYPE(instr, code);

    gen_goto_tb(dc, 0, dc->pc + 4 + (instr.imm16.s & -4));
    dc->is_jmp = DISAS_TB_JUMP;
}

static void gen_bxx(DisasContext *dc, uint32_t code, uint32_t flags)
{
    I_TYPE(instr, code);

    TCGLabel *l1 = gen_new_label();
    tcg_gen_brcond_tl(flags, dc->cpu_R[instr.a], dc->cpu_R[instr.b], l1);
    gen_goto_tb(dc, 0, dc->pc + 4);
    gen_set_label(l1);
    gen_goto_tb(dc, 1, dc->pc + 4 + (instr.imm16.s & -4));
    dc->is_jmp = DISAS_TB_JUMP;
}

/* Comparison instructions */
#define gen_i_cmpxx(fname, op3)                                              \
static void (fname)(DisasContext *dc, uint32_t code, uint32_t flags)         \
{                                                                            \
    I_TYPE(instr, (code));                                                   \
    tcg_gen_setcondi_tl(flags, (dc)->cpu_R[instr.b], (dc)->cpu_R[instr.a],   \
                        (op3));                                              \
}

gen_i_cmpxx(gen_cmpxxsi, instr.imm16.s)
gen_i_cmpxx(gen_cmpxxui, instr.imm16.u)

/* Math/logic instructions */
#define gen_i_math_logic(fname, insn, resimm, op3)                          \
static void (fname)(DisasContext *dc, uint32_t code, uint32_t flags)        \
{                                                                           \
    I_TYPE(instr, (code));                                                  \
    if (unlikely(instr.b == R_ZERO)) { /* Store to R_ZERO is ignored */     \
        return;                                                             \
    } else if (instr.a == R_ZERO) { /* MOVxI optimizations */               \
        tcg_gen_movi_tl(dc->cpu_R[instr.b], (resimm) ? (op3) : 0);          \
    } else {                                                                \
        tcg_gen_##insn##_tl((dc)->cpu_R[instr.b], (dc)->cpu_R[instr.a],     \
                            (op3));                                         \
    }                                                                       \
}

gen_i_math_logic(addi,  addi, 1, instr.imm16.s)
gen_i_math_logic(muli,  muli, 0, instr.imm16.s)

gen_i_math_logic(andi,  andi, 0, instr.imm16.u)
gen_i_math_logic(ori,   ori,  1, instr.imm16.u)
gen_i_math_logic(xori,  xori, 1, instr.imm16.u)

gen_i_math_logic(andhi, andi, 0, instr.imm16.u << 16)
gen_i_math_logic(orhi , ori,  1, instr.imm16.u << 16)
gen_i_math_logic(xorhi, xori, 1, instr.imm16.u << 16)

/* Prototype only, defined below */
static void handle_r_type_instr(DisasContext *dc, uint32_t code,
                                uint32_t flags);

static const Nios2Instruction i_type_instructions[] = {
    INSTRUCTION(call),                                /* call */
    INSTRUCTION(jmpi),                                /* jmpi */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_FLG(gen_ldx, MO_UB),                  /* ldbu */
    INSTRUCTION(addi),                                /* addi */
    INSTRUCTION_FLG(gen_stx, MO_UB),                  /* stb */
    INSTRUCTION(br),                                  /* br */
    INSTRUCTION_FLG(gen_ldx, MO_SB),                  /* ldb */
    INSTRUCTION_FLG(gen_cmpxxsi, TCG_COND_GE),        /* cmpgei */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_FLG(gen_ldx, MO_UW),                  /* ldhu */
    INSTRUCTION(andi),                                /* andi */
    INSTRUCTION_FLG(gen_stx, MO_UW),                  /* sth */
    INSTRUCTION_FLG(gen_bxx, TCG_COND_GE),            /* bge */
    INSTRUCTION_FLG(gen_ldx, MO_SW),                  /* ldh */
    INSTRUCTION_FLG(gen_cmpxxsi, TCG_COND_LT),        /* cmplti */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_NOP(),                                /* initda */
    INSTRUCTION(ori),                                 /* ori */
    INSTRUCTION_FLG(gen_stx, MO_UL),                  /* stw */
    INSTRUCTION_FLG(gen_bxx, TCG_COND_LT),            /* blt */
    INSTRUCTION_FLG(gen_ldx, MO_UL),                  /* ldw */
    INSTRUCTION_FLG(gen_cmpxxsi, TCG_COND_NE),        /* cmpnei */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_NOP(),                                /* flushda */
    INSTRUCTION(xori),                                /* xori */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_FLG(gen_bxx, TCG_COND_NE),            /* bne */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_FLG(gen_cmpxxsi, TCG_COND_EQ),        /* cmpeqi */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_FLG(gen_ldx, MO_UB),                  /* ldbuio */
    INSTRUCTION(muli),                                /* muli */
    INSTRUCTION_FLG(gen_stx, MO_UB),                  /* stbio */
    INSTRUCTION_FLG(gen_bxx, TCG_COND_EQ),            /* beq */
    INSTRUCTION_FLG(gen_ldx, MO_SB),                  /* ldbio */
    INSTRUCTION_FLG(gen_cmpxxui, TCG_COND_GEU),       /* cmpgeui */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_FLG(gen_ldx, MO_UW),                  /* ldhuio */
    INSTRUCTION(andhi),                               /* andhi */
    INSTRUCTION_FLG(gen_stx, MO_UW),                  /* sthio */
    INSTRUCTION_FLG(gen_bxx, TCG_COND_GEU),           /* bgeu */
    INSTRUCTION_FLG(gen_ldx, MO_SW),                  /* ldhio */
    INSTRUCTION_FLG(gen_cmpxxui, TCG_COND_LTU),       /* cmpltui */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_UNIMPLEMENTED(),                      /* custom */
    INSTRUCTION_NOP(),                                /* initd */
    INSTRUCTION(orhi),                                /* orhi */
    INSTRUCTION_FLG(gen_stx, MO_SL),                  /* stwio */
    INSTRUCTION_FLG(gen_bxx, TCG_COND_LTU),           /* bltu */
    INSTRUCTION_FLG(gen_ldx, MO_UL),                  /* ldwio */
    INSTRUCTION_UNIMPLEMENTED(),                      /* rdprs */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_FLG(handle_r_type_instr, 0),          /* R-Type */
    INSTRUCTION_NOP(),                                /* flushd */
    INSTRUCTION(xorhi),                               /* xorhi */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_ILLEGAL(),
};

/*
 * R-Type instructions
 */
/*
 * status <- estatus
 * PC <- ea
 */
static void eret(DisasContext *dc, uint32_t code, uint32_t flags)
{
    tcg_gen_mov_tl(dc->cpu_R[CR_STATUS], dc->cpu_R[CR_ESTATUS]);
    tcg_gen_mov_tl(dc->cpu_R[R_PC], dc->cpu_R[R_EA]);

    dc->is_jmp = DISAS_JUMP;
}

/* PC <- ra */
static void ret(DisasContext *dc, uint32_t code, uint32_t flags)
{
    tcg_gen_mov_tl(dc->cpu_R[R_PC], dc->cpu_R[R_RA]);

    dc->is_jmp = DISAS_JUMP;
}

/* PC <- ba */
static void bret(DisasContext *dc, uint32_t code, uint32_t flags)
{
    tcg_gen_mov_tl(dc->cpu_R[R_PC], dc->cpu_R[R_BA]);

    dc->is_jmp = DISAS_JUMP;
}

/* PC <- rA */
static void jmp(DisasContext *dc, uint32_t code, uint32_t flags)
{
    R_TYPE(instr, code);

    tcg_gen_mov_tl(dc->cpu_R[R_PC], load_gpr(dc, instr.a));

    dc->is_jmp = DISAS_JUMP;
}

/* rC <- PC + 4 */
static void nextpc(DisasContext *dc, uint32_t code, uint32_t flags)
{
    R_TYPE(instr, code);

    if (likely(instr.c != R_ZERO)) {
        tcg_gen_movi_tl(dc->cpu_R[instr.c], dc->pc + 4);
    }
}

/*
 * ra <- PC + 4
 * PC <- rA
 */
static void callr(DisasContext *dc, uint32_t code, uint32_t flags)
{
    R_TYPE(instr, code);

    tcg_gen_mov_tl(dc->cpu_R[R_PC], load_gpr(dc, instr.a));
    tcg_gen_movi_tl(dc->cpu_R[R_RA], dc->pc + 4);

    dc->is_jmp = DISAS_JUMP;
}

/* rC <- ctlN */
static void rdctl(DisasContext *dc, uint32_t code, uint32_t flags)
{
    R_TYPE(instr, code);

    gen_check_supervisor(dc);

    switch (instr.imm5 + CR_BASE) {
    case CR_PTEADDR:
    case CR_TLBACC:
    case CR_TLBMISC:
    {
#if !defined(CONFIG_USER_ONLY)
        if (likely(instr.c != R_ZERO)) {
            tcg_gen_mov_tl(dc->cpu_R[instr.c], dc->cpu_R[instr.imm5 + CR_BASE]);
#ifdef DEBUG_MMU
            TCGv_i32 tmp = tcg_const_i32(instr.imm5 + CR_BASE);
            gen_helper_mmu_read_debug(dc->cpu_R[instr.c], dc->cpu_env, tmp);
            tcg_temp_free_i32(tmp);
#endif
        }
#endif
        break;
    }

    default:
        if (likely(instr.c != R_ZERO)) {
            tcg_gen_mov_tl(dc->cpu_R[instr.c], dc->cpu_R[instr.imm5 + CR_BASE]);
        }
        break;
    }
}

/* ctlN <- rA */
static void wrctl(DisasContext *dc, uint32_t code, uint32_t flags)
{
    R_TYPE(instr, code);

    gen_check_supervisor(dc);

    switch (instr.imm5 + CR_BASE) {
    case CR_PTEADDR:
    case CR_TLBACC:
    case CR_TLBMISC:
    {
#if !defined(CONFIG_USER_ONLY)
        TCGv_i32 tmp = tcg_const_i32(instr.imm5 + CR_BASE);
        gen_helper_mmu_write(dc->cpu_env, tmp, load_gpr(dc, instr.a));
        tcg_temp_free_i32(tmp);
#endif
        break;
    }

    default:
        tcg_gen_mov_tl(dc->cpu_R[instr.imm5 + CR_BASE], load_gpr(dc, instr.a));
        break;
    }

    /* If interrupts were enabled using WRCTL, trigger them. */
#if !defined(CONFIG_USER_ONLY)
    if ((instr.imm5 + CR_BASE) == CR_STATUS) {
        gen_helper_check_interrupts(dc->cpu_env);
    }
#endif
}

/* Comparison instructions */
static void gen_cmpxx(DisasContext *dc, uint32_t code, uint32_t flags)
{
    R_TYPE(instr, code);
    if (likely(instr.c != R_ZERO)) {
        tcg_gen_setcond_tl(flags, dc->cpu_R[instr.c], dc->cpu_R[instr.a],
                           dc->cpu_R[instr.b]);
    }
}

/* Math/logic instructions */
#define gen_r_math_logic(fname, insn, op3)                                 \
static void (fname)(DisasContext *dc, uint32_t code, uint32_t flags)       \
{                                                                          \
    R_TYPE(instr, (code));                                                 \
    if (likely(instr.c != R_ZERO)) {                                       \
        tcg_gen_##insn((dc)->cpu_R[instr.c], load_gpr((dc), instr.a),      \
                       (op3));                                             \
    }                                                                      \
}

gen_r_math_logic(add,  add_tl,   load_gpr(dc, instr.b))
gen_r_math_logic(sub,  sub_tl,   load_gpr(dc, instr.b))
gen_r_math_logic(mul,  mul_tl,   load_gpr(dc, instr.b))

gen_r_math_logic(and,  and_tl,   load_gpr(dc, instr.b))
gen_r_math_logic(or,   or_tl,    load_gpr(dc, instr.b))
gen_r_math_logic(xor,  xor_tl,   load_gpr(dc, instr.b))
gen_r_math_logic(nor,  nor_tl,   load_gpr(dc, instr.b))

gen_r_math_logic(srai, sari_tl,  instr.imm5)
gen_r_math_logic(srli, shri_tl,  instr.imm5)
gen_r_math_logic(slli, shli_tl,  instr.imm5)
gen_r_math_logic(roli, rotli_tl, instr.imm5)

#define gen_r_mul(fname, insn)                                         \
static void (fname)(DisasContext *dc, uint32_t code, uint32_t flags)   \
{                                                                      \
    R_TYPE(instr, (code));                                             \
    if (likely(instr.c != R_ZERO)) {                                   \
        TCGv t0 = tcg_temp_new();                                      \
        tcg_gen_##insn(t0, dc->cpu_R[instr.c],                         \
                       load_gpr(dc, instr.a), load_gpr(dc, instr.b)); \
        tcg_temp_free(t0);                                             \
    }                                                                  \
}

gen_r_mul(mulxss, muls2_tl)
gen_r_mul(mulxuu, mulu2_tl)
gen_r_mul(mulxsu, mulsu2_tl)

#define gen_r_shift_s(fname, insn)                                         \
static void (fname)(DisasContext *dc, uint32_t code, uint32_t flags)       \
{                                                                          \
    R_TYPE(instr, (code));                                                 \
    if (likely(instr.c != R_ZERO)) {                                       \
        TCGv t0 = tcg_temp_new();                                          \
        tcg_gen_andi_tl(t0, load_gpr((dc), instr.b), 31);                  \
        tcg_gen_##insn((dc)->cpu_R[instr.c], load_gpr((dc), instr.a), t0); \
        tcg_temp_free(t0);                                                 \
    }                                                                      \
}

gen_r_shift_s(sra, sar_tl)
gen_r_shift_s(srl, shr_tl)
gen_r_shift_s(sll, shl_tl)
gen_r_shift_s(rol, rotl_tl)
gen_r_shift_s(ror, rotr_tl)

static void divs(DisasContext *dc, uint32_t code, uint32_t flags)
{
    R_TYPE(instr, (code));

    /* Stores into R_ZERO are ignored */
    if (unlikely(instr.c == R_ZERO)) {
        return;
    }

    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    TCGv t3 = tcg_temp_new();

    tcg_gen_ext32s_tl(t0, load_gpr(dc, instr.a));
    tcg_gen_ext32s_tl(t1, load_gpr(dc, instr.b));
    tcg_gen_setcondi_tl(TCG_COND_EQ, t2, t0, INT_MIN);
    tcg_gen_setcondi_tl(TCG_COND_EQ, t3, t1, -1);
    tcg_gen_and_tl(t2, t2, t3);
    tcg_gen_setcondi_tl(TCG_COND_EQ, t3, t1, 0);
    tcg_gen_or_tl(t2, t2, t3);
    tcg_gen_movi_tl(t3, 0);
    tcg_gen_movcond_tl(TCG_COND_NE, t1, t2, t3, t2, t1);
    tcg_gen_div_tl(dc->cpu_R[instr.c], t0, t1);
    tcg_gen_ext32s_tl(dc->cpu_R[instr.c], dc->cpu_R[instr.c]);

    tcg_temp_free(t3);
    tcg_temp_free(t2);
    tcg_temp_free(t1);
    tcg_temp_free(t0);
}

static void divu(DisasContext *dc, uint32_t code, uint32_t flags)
{
    R_TYPE(instr, (code));

    /* Stores into R_ZERO are ignored */
    if (unlikely(instr.c == R_ZERO)) {
        return;
    }

    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_const_tl(0);
    TCGv t3 = tcg_const_tl(1);

    tcg_gen_ext32u_tl(t0, load_gpr(dc, instr.a));
    tcg_gen_ext32u_tl(t1, load_gpr(dc, instr.b));
    tcg_gen_movcond_tl(TCG_COND_EQ, t1, t1, t2, t3, t1);
    tcg_gen_divu_tl(dc->cpu_R[instr.c], t0, t1);
    tcg_gen_ext32s_tl(dc->cpu_R[instr.c], dc->cpu_R[instr.c]);

    tcg_temp_free(t3);
    tcg_temp_free(t2);
    tcg_temp_free(t1);
    tcg_temp_free(t0);
}

static const Nios2Instruction r_type_instructions[] = {
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION(eret),                                /* eret */
    INSTRUCTION(roli),                                /* roli */
    INSTRUCTION(rol),                                 /* rol */
    INSTRUCTION_NOP(),                                /* flushp */
    INSTRUCTION(ret),                                 /* ret */
    INSTRUCTION(nor),                                 /* nor */
    INSTRUCTION(mulxuu),                              /* mulxuu */
    INSTRUCTION_FLG(gen_cmpxx, TCG_COND_GE),          /* cmpge */
    INSTRUCTION(bret),                                /* bret */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION(ror),                                 /* ror */
    INSTRUCTION_NOP(),                                /* flushi */
    INSTRUCTION(jmp),                                 /* jmp */
    INSTRUCTION(and),                                 /* and */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_FLG(gen_cmpxx, TCG_COND_LT),          /* cmplt */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION(slli),                                /* slli */
    INSTRUCTION(sll),                                 /* sll */
    INSTRUCTION_UNIMPLEMENTED(),                      /* wrprs */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION(or),                                  /* or */
    INSTRUCTION(mulxsu),                              /* mulxsu */
    INSTRUCTION_FLG(gen_cmpxx, TCG_COND_NE),          /* cmpne */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION(srli),                                /* srli */
    INSTRUCTION(srl),                                 /* srl */
    INSTRUCTION(nextpc),                              /* nextpc */
    INSTRUCTION(callr),                               /* callr */
    INSTRUCTION(xor),                                 /* xor */
    INSTRUCTION(mulxss),                              /* mulxss */
    INSTRUCTION_FLG(gen_cmpxx, TCG_COND_EQ),          /* cmpeq */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION(divu),                                /* divu */
    INSTRUCTION(divs),                                /* div */
    INSTRUCTION(rdctl),                               /* rdctl */
    INSTRUCTION(mul),                                 /* mul */
    INSTRUCTION_FLG(gen_cmpxx, TCG_COND_GEU),         /* cmpgeu */
    INSTRUCTION_NOP(),                                /* initi */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_FLG(gen_excp, EXCP_TRAP),             /* trap */
    INSTRUCTION(wrctl),                               /* wrctl */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_FLG(gen_cmpxx, TCG_COND_LTU),         /* cmpltu */
    INSTRUCTION(add),                                 /* add */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_FLG(gen_excp, EXCP_BREAK),            /* break */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION(nop),                                 /* nop */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION(sub),                                 /* sub */
    INSTRUCTION(srai),                                /* srai */
    INSTRUCTION(sra),                                 /* sra */
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_ILLEGAL(),
    INSTRUCTION_ILLEGAL(),
};

static void handle_r_type_instr(DisasContext *dc, uint32_t code, uint32_t flags)
{
    uint8_t opx;
    const Nios2Instruction *instr;

    opx = get_opxcode(code);
    if (unlikely(opx >= ARRAY_SIZE(r_type_instructions))) {
        goto illegal_op;
    }

    instr = &r_type_instructions[opx];
    instr->handler(dc, code, instr->flags);

    return;

illegal_op:
    t_gen_helper_raise_exception(dc, EXCP_ILLEGAL);
}

static void handle_instruction(DisasContext *dc, CPUNios2State *env)
{
    uint32_t code;
    uint8_t op;
    const Nios2Instruction *instr;
#if defined(CONFIG_USER_ONLY)
    /* FIXME: Is this needed ? */
    if (dc->pc >= 0x1000 && dc->pc < 0x2000) {
        env->regs[R_PC] = dc->pc;
        t_gen_helper_raise_exception(dc, 0xaa);
        return;
    }
#endif
    code = cpu_ldl_code(env, dc->pc);
    op = get_opcode(code);

    if (unlikely(op >= ARRAY_SIZE(i_type_instructions))) {
        goto illegal_op;
    }

    dc->zero = NULL;

    instr = &i_type_instructions[op];
    instr->handler(dc, code, instr->flags);

    if (dc->zero) {
        tcg_temp_free(dc->zero);
    }

    return;

illegal_op:
    t_gen_helper_raise_exception(dc, EXCP_ILLEGAL);
}

static const char * const regnames[] = {
    "zero",       "at",         "r2",         "r3",
    "r4",         "r5",         "r6",         "r7",
    "r8",         "r9",         "r10",        "r11",
    "r12",        "r13",        "r14",        "r15",
    "r16",        "r17",        "r18",        "r19",
    "r20",        "r21",        "r22",        "r23",
    "et",         "bt",         "gp",         "sp",
    "fp",         "ea",         "ba",         "ra",
    "status",     "estatus",    "bstatus",    "ienable",
    "ipending",   "cpuid",      "reserved0",  "exception",
    "pteaddr",    "tlbacc",     "tlbmisc",    "reserved1",
    "badaddr",    "config",     "mpubase",    "mpuacc",
    "reserved2",  "reserved3",  "reserved4",  "reserved5",
    "reserved6",  "reserved7",  "reserved8",  "reserved9",
    "reserved10", "reserved11", "reserved12", "reserved13",
    "reserved14", "reserved15", "reserved16", "reserved17",
    "rpc"
};

static TCGv cpu_R[NUM_CORE_REGS];

#include "exec/gen-icount.h"

static void gen_exception(DisasContext *dc, uint32_t excp)
{
    TCGv_i32 tmp = tcg_const_i32(excp);

    tcg_gen_movi_tl(cpu_R[R_PC], dc->pc);
    gen_helper_raise_exception(cpu_env, tmp);
    tcg_temp_free_i32(tmp);
    dc->is_jmp = DISAS_UPDATE;
}

/* generate intermediate code for basic block 'tb'.  */
void gen_intermediate_code(CPUState *cs, TranslationBlock *tb, int max_insns)
{
    CPUNios2State *env = cs->env_ptr;
    DisasContext dc1, *dc = &dc1;
    int num_insns;

    /* Initialize DC */
    dc->cpu_env = cpu_env;
    dc->cpu_R   = cpu_R;
    dc->is_jmp  = DISAS_NEXT;
    dc->pc      = tb->pc;
    dc->tb      = tb;
    dc->mem_idx = cpu_mmu_index(env, false);
    dc->singlestep_enabled = cs->singlestep_enabled;

    /* Set up instruction counts */
    num_insns = 0;
    if (max_insns > 1) {
        int page_insns = (TARGET_PAGE_SIZE - (tb->pc & TARGET_PAGE_MASK)) / 4;
        if (max_insns > page_insns) {
            max_insns = page_insns;
        }
    }

    gen_tb_start(tb);
    do {
        tcg_gen_insn_start(dc->pc);
        num_insns++;

        if (unlikely(cpu_breakpoint_test(cs, dc->pc, BP_ANY))) {
            gen_exception(dc, EXCP_DEBUG);
            /* The address covered by the breakpoint must be included in
               [tb->pc, tb->pc + tb->size) in order to for it to be
               properly cleared -- thus we increment the PC here so that
               the logic setting tb->size below does the right thing.  */
            dc->pc += 4;
            break;
        }

        if (num_insns == max_insns && (tb_cflags(tb) & CF_LAST_IO)) {
            gen_io_start();
        }

        /* Decode an instruction */
        handle_instruction(dc, env);

        dc->pc += 4;

        /* Translation stops when a conditional branch is encountered.
         * Otherwise the subsequent code could get translated several times.
         * Also stop translation when a page boundary is reached.  This
         * ensures prefetch aborts occur at the right place.  */
    } while (!dc->is_jmp &&
             !tcg_op_buf_full() &&
             num_insns < max_insns);

    /* Indicate where the next block should start */
    switch (dc->is_jmp) {
    case DISAS_NEXT:
        /* Save the current PC back into the CPU register */
        tcg_gen_movi_tl(cpu_R[R_PC], dc->pc);
        tcg_gen_exit_tb(NULL, 0);
        break;

    default:
    case DISAS_JUMP:
    case DISAS_UPDATE:
        /* The jump will already have updated the PC register */
        tcg_gen_exit_tb(NULL, 0);
        break;

    case DISAS_TB_JUMP:
        /* nothing more to generate */
        break;
    }

    /* End off the block */
    gen_tb_end(tb, num_insns);

    /* Mark instruction starts for the final generated instruction */
    tb->size = dc->pc - tb->pc;
    tb->icount = num_insns;

#ifdef DEBUG_DISAS
    if (qemu_loglevel_mask(CPU_LOG_TB_IN_ASM)
        && qemu_log_in_addr_range(tb->pc)) {
        qemu_log_lock();
        qemu_log("IN: %s\n", lookup_symbol(tb->pc));
        log_target_disas(cs, tb->pc, dc->pc - tb->pc);
        qemu_log("\n");
        qemu_log_unlock();
    }
#endif
}

void nios2_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    Nios2CPU *cpu = NIOS2_CPU(cs);
    CPUNios2State *env = &cpu->env;
    int i;

    if (!env) {
        return;
    }

    qemu_fprintf(f, "IN: PC=%x %s\n",
                 env->regs[R_PC], lookup_symbol(env->regs[R_PC]));

    for (i = 0; i < NUM_CORE_REGS; i++) {
        qemu_fprintf(f, "%9s=%8.8x ", regnames[i], env->regs[i]);
        if ((i + 1) % 4 == 0) {
            qemu_fprintf(f, "\n");
        }
    }
#if !defined(CONFIG_USER_ONLY)
    qemu_fprintf(f, " mmu write: VPN=%05X PID %02X TLBACC %08X\n",
                 env->mmu.pteaddr_wr & CR_PTEADDR_VPN_MASK,
                 (env->mmu.tlbmisc_wr & CR_TLBMISC_PID_MASK) >> 4,
                 env->mmu.tlbacc_wr);
#endif
    qemu_fprintf(f, "\n\n");
}

void nios2_tcg_init(void)
{
    int i;

    for (i = 0; i < NUM_CORE_REGS; i++) {
        cpu_R[i] = tcg_global_mem_new(cpu_env,
                                      offsetof(CPUNios2State, regs[i]),
                                      regnames[i]);
    }
}

void restore_state_to_opc(CPUNios2State *env, TranslationBlock *tb,
                          target_ulong *data)
{
    env->regs[R_PC] = data[0];
}
