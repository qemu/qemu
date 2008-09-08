/*
 *  Alpha emulation cpu translation for qemu.
 *
 *  Copyright (c) 2007 Jocelyn Mayer
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "cpu.h"
#include "exec-all.h"
#include "disas.h"
#include "helper.h"
#include "tcg-op.h"
#include "qemu-common.h"

#define DO_SINGLE_STEP
#define GENERATE_NOP
#define ALPHA_DEBUG_DISAS
#define DO_TB_FLUSH

typedef struct DisasContext DisasContext;
struct DisasContext {
    uint64_t pc;
    int mem_idx;
#if !defined (CONFIG_USER_ONLY)
    int pal_mode;
#endif
    uint32_t amask;
};

/* global register indexes */
static TCGv cpu_env;
static TCGv cpu_ir[31];
static TCGv cpu_pc;

/* dyngen register indexes */
static TCGv cpu_T[3];

/* register names */
static char cpu_reg_names[5*31];

#include "gen-icount.h"

static void alpha_translate_init(void)
{
    int i;
    char *p;
    static int done_init = 0;

    if (done_init)
        return;

    cpu_env = tcg_global_reg_new(TCG_TYPE_PTR, TCG_AREG0, "env");

#if TARGET_LONG_BITS > HOST_LONG_BITS
    cpu_T[0] = tcg_global_mem_new(TCG_TYPE_I64, TCG_AREG0,
                                  offsetof(CPUState, t0), "T0");
    cpu_T[1] = tcg_global_mem_new(TCG_TYPE_I64, TCG_AREG0,
                                  offsetof(CPUState, t1), "T1");
    cpu_T[2] = tcg_global_mem_new(TCG_TYPE_I64, TCG_AREG0,
                                  offsetof(CPUState, t2), "T2");
#else
    cpu_T[0] = tcg_global_reg_new(TCG_TYPE_I64, TCG_AREG1, "T0");
    cpu_T[1] = tcg_global_reg_new(TCG_TYPE_I64, TCG_AREG2, "T1");
    cpu_T[2] = tcg_global_reg_new(TCG_TYPE_I64, TCG_AREG3, "T2");
#endif

    p = cpu_reg_names;
    for (i = 0; i < 31; i++) {
        sprintf(p, "ir%d", i);
        cpu_ir[i] = tcg_global_mem_new(TCG_TYPE_I64, TCG_AREG0,
                                       offsetof(CPUState, ir[i]), p);
        p += 4;
    }

    cpu_pc = tcg_global_mem_new(TCG_TYPE_I64, TCG_AREG0,
                                offsetof(CPUState, pc), "pc");

    /* register helpers */
#undef DEF_HELPER
#define DEF_HELPER(ret, name, params) tcg_register_helper(name, #name);
#include "helper.h"

    done_init = 1;
}

static always_inline void gen_op_nop (void)
{
#if defined(GENERATE_NOP)
    gen_op_no_op();
#endif
}

#define GEN32(func, NAME) \
static GenOpFunc *NAME ## _table [32] = {                                     \
NAME ## 0, NAME ## 1, NAME ## 2, NAME ## 3,                                   \
NAME ## 4, NAME ## 5, NAME ## 6, NAME ## 7,                                   \
NAME ## 8, NAME ## 9, NAME ## 10, NAME ## 11,                                 \
NAME ## 12, NAME ## 13, NAME ## 14, NAME ## 15,                               \
NAME ## 16, NAME ## 17, NAME ## 18, NAME ## 19,                               \
NAME ## 20, NAME ## 21, NAME ## 22, NAME ## 23,                               \
NAME ## 24, NAME ## 25, NAME ## 26, NAME ## 27,                               \
NAME ## 28, NAME ## 29, NAME ## 30, NAME ## 31,                               \
};                                                                            \
static always_inline void func (int n)                                        \
{                                                                             \
    NAME ## _table[n]();                                                      \
}

/* IR moves */
/* Special hacks for ir31 */
#define gen_op_cmov_ir31 gen_op_nop
GEN32(gen_op_cmov_ir, gen_op_cmov_ir);

/* FIR moves */
/* Special hacks for fir31 */
#define gen_op_load_FT0_fir31 gen_op_reset_FT0
#define gen_op_load_FT1_fir31 gen_op_reset_FT1
#define gen_op_load_FT2_fir31 gen_op_reset_FT2
#define gen_op_store_FT0_fir31 gen_op_nop
#define gen_op_store_FT1_fir31 gen_op_nop
#define gen_op_store_FT2_fir31 gen_op_nop
#define gen_op_cmov_fir31 gen_op_nop
GEN32(gen_op_load_FT0_fir, gen_op_load_FT0_fir);
GEN32(gen_op_load_FT1_fir, gen_op_load_FT1_fir);
GEN32(gen_op_load_FT2_fir, gen_op_load_FT2_fir);
GEN32(gen_op_store_FT0_fir, gen_op_store_FT0_fir);
GEN32(gen_op_store_FT1_fir, gen_op_store_FT1_fir);
GEN32(gen_op_store_FT2_fir, gen_op_store_FT2_fir);
GEN32(gen_op_cmov_fir, gen_op_cmov_fir);

static always_inline void gen_load_fir (DisasContext *ctx, int firn, int Tn)
{
    switch (Tn) {
    case 0:
        gen_op_load_FT0_fir(firn);
        break;
    case 1:
        gen_op_load_FT1_fir(firn);
        break;
    case 2:
        gen_op_load_FT2_fir(firn);
        break;
    }
}

static always_inline void gen_store_fir (DisasContext *ctx, int firn, int Tn)
{
    switch (Tn) {
    case 0:
        gen_op_store_FT0_fir(firn);
        break;
    case 1:
        gen_op_store_FT1_fir(firn);
        break;
    case 2:
        gen_op_store_FT2_fir(firn);
        break;
    }
}

/* Memory moves */
#if defined(CONFIG_USER_ONLY)
#define OP_LD_TABLE(width)                                                    \
static GenOpFunc *gen_op_ld##width[] = {                                      \
    &gen_op_ld##width##_raw,                                                  \
}
#define OP_ST_TABLE(width)                                                    \
static GenOpFunc *gen_op_st##width[] = {                                      \
    &gen_op_st##width##_raw,                                                  \
}
#else
#define OP_LD_TABLE(width)                                                    \
static GenOpFunc *gen_op_ld##width[] = {                                      \
    &gen_op_ld##width##_kernel,                                               \
    &gen_op_ld##width##_executive,                                            \
    &gen_op_ld##width##_supervisor,                                           \
    &gen_op_ld##width##_user,                                                 \
}
#define OP_ST_TABLE(width)                                                    \
static GenOpFunc *gen_op_st##width[] = {                                      \
    &gen_op_st##width##_kernel,                                               \
    &gen_op_st##width##_executive,                                            \
    &gen_op_st##width##_supervisor,                                           \
    &gen_op_st##width##_user,                                                 \
}
#endif

#define GEN_LD(width)                                                         \
OP_LD_TABLE(width);                                                           \
static always_inline void gen_ld##width (DisasContext *ctx)                   \
{                                                                             \
    (*gen_op_ld##width[ctx->mem_idx])();                                      \
}

#define GEN_ST(width)                                                         \
OP_ST_TABLE(width);                                                           \
static always_inline void gen_st##width (DisasContext *ctx)                   \
{                                                                             \
    (*gen_op_st##width[ctx->mem_idx])();                                      \
}

GEN_LD(bu);
GEN_ST(b);
GEN_LD(wu);
GEN_ST(w);
GEN_LD(l);
GEN_ST(l);
GEN_LD(q);
GEN_ST(q);
GEN_LD(q_u);
GEN_ST(q_u);
GEN_LD(l_l);
GEN_ST(l_c);
GEN_LD(q_l);
GEN_ST(q_c);

#if 0 /* currently unused */
GEN_LD(f);
GEN_ST(f);
GEN_LD(g);
GEN_ST(g);
#endif /* 0 */
GEN_LD(s);
GEN_ST(s);
GEN_LD(t);
GEN_ST(t);

static always_inline void _gen_op_bcond (DisasContext *ctx)
{
#if 0 // Qemu does not know how to do this...
    gen_op_bcond(ctx->pc);
#else
    gen_op_bcond(ctx->pc >> 32, ctx->pc);
#endif
}

static always_inline void gen_excp (DisasContext *ctx,
                                    int exception, int error_code)
{
    tcg_gen_movi_i64(cpu_pc, ctx->pc);
    gen_op_excp(exception, error_code);
}

static always_inline void gen_invalid (DisasContext *ctx)
{
    gen_excp(ctx, EXCP_OPCDEC, 0);
}

static always_inline void gen_load_mem (DisasContext *ctx,
                                        void (*gen_load_op)(DisasContext *ctx),
                                        int ra, int rb, int32_t disp16,
                                        int clear)
{
    if (ra == 31 && disp16 == 0) {
        /* UNOP */
        gen_op_nop();
    } else {
        if (rb != 31)
            tcg_gen_addi_i64(cpu_T[0], cpu_ir[rb], disp16);
        else
            tcg_gen_movi_i64(cpu_T[0], disp16);
        if (clear)
            tcg_gen_andi_i64(cpu_T[0], cpu_T[0], ~0x7);
        (*gen_load_op)(ctx);
        if (ra != 31)
            tcg_gen_mov_i64(cpu_ir[ra], cpu_T[1]);
    }
}

static always_inline void gen_store_mem (DisasContext *ctx,
                                         void (*gen_store_op)(DisasContext *ctx),
                                         int ra, int rb, int32_t disp16,
                                         int clear)
{
    if (rb != 31)
        tcg_gen_addi_i64(cpu_T[0], cpu_ir[rb], disp16);
    else
        tcg_gen_movi_i64(cpu_T[0], disp16);
    if (clear)
        tcg_gen_andi_i64(cpu_T[0], cpu_T[0], ~0x7);
    if (ra != 31)
        tcg_gen_mov_i64(cpu_T[1], cpu_ir[ra]);
    else
        tcg_gen_movi_i64(cpu_T[1], 0);
    (*gen_store_op)(ctx);
}

static always_inline void gen_load_fmem (DisasContext *ctx,
                                         void (*gen_load_fop)(DisasContext *ctx),
                                         int ra, int rb, int32_t disp16)
{
    if (rb != 31)
        tcg_gen_addi_i64(cpu_T[0], cpu_ir[rb], disp16);
    else
        tcg_gen_movi_i64(cpu_T[0], disp16);
    (*gen_load_fop)(ctx);
    gen_store_fir(ctx, ra, 1);
}

static always_inline void gen_store_fmem (DisasContext *ctx,
                                          void (*gen_store_fop)(DisasContext *ctx),
                                          int ra, int rb, int32_t disp16)
{
    if (rb != 31)
        tcg_gen_addi_i64(cpu_T[0], cpu_ir[rb], disp16);
    else
        tcg_gen_movi_i64(cpu_T[0], disp16);
    gen_load_fir(ctx, ra, 1);
    (*gen_store_fop)(ctx);
}

static always_inline void gen_bcond (DisasContext *ctx,
                                     void (*gen_test_op)(void),
                                     int ra, int32_t disp16)
{
    tcg_gen_movi_i64(cpu_T[1], ctx->pc + (int64_t)(disp16 << 2));
    if (ra != 31)
        tcg_gen_mov_i64(cpu_T[0], cpu_ir[ra]);
    else
        tcg_gen_movi_i64(cpu_T[0], 0);
    (*gen_test_op)();
    _gen_op_bcond(ctx);
}

static always_inline void gen_fbcond (DisasContext *ctx,
                                      void (*gen_test_op)(void),
                                      int ra, int32_t disp16)
{
    tcg_gen_movi_i64(cpu_T[1], ctx->pc + (int64_t)(disp16 << 2));
    gen_load_fir(ctx, ra, 0);
    (*gen_test_op)();
    _gen_op_bcond(ctx);
}

static always_inline void gen_arith2 (DisasContext *ctx,
                                      void (*gen_arith_op)(void),
                                      int rb, int rc, int islit, int8_t lit)
{
    if (islit)
        tcg_gen_movi_i64(cpu_T[0], lit);
    else if (rb != 31)
        tcg_gen_mov_i64(cpu_T[0], cpu_ir[rb]);
    else
        tcg_gen_movi_i64(cpu_T[0], 0);
    (*gen_arith_op)();
    if (rc != 31)
        tcg_gen_mov_i64(cpu_ir[rc], cpu_T[0]);
}

static always_inline void gen_arith3 (DisasContext *ctx,
                                      void (*gen_arith_op)(void),
                                      int ra, int rb, int rc,
                                      int islit, int8_t lit)
{
    if (ra != 31)
        tcg_gen_mov_i64(cpu_T[0], cpu_ir[ra]);
    else
        tcg_gen_movi_i64(cpu_T[0], 0);
    if (islit)
        tcg_gen_movi_i64(cpu_T[1], lit);
    else if (rb != 31)
        tcg_gen_mov_i64(cpu_T[1], cpu_ir[rb]);
    else
        tcg_gen_movi_i64(cpu_T[1], 0);
    (*gen_arith_op)();
    if (rc != 31)
        tcg_gen_mov_i64(cpu_ir[rc], cpu_T[0]);
}

static always_inline void gen_cmov (DisasContext *ctx,
                                    void (*gen_test_op)(void),
                                    int ra, int rb, int rc,
                                    int islit, int8_t lit)
{
    if (ra != 31)
        tcg_gen_mov_i64(cpu_T[0], cpu_ir[ra]);
    else
        tcg_gen_movi_i64(cpu_T[0], 0);
    if (islit)
        tcg_gen_movi_i64(cpu_T[1], lit);
    else if (rb != 31)
        tcg_gen_mov_i64(cpu_T[1], cpu_ir[rb]);
    else
        tcg_gen_movi_i64(cpu_T[1], 0);
    (*gen_test_op)();
    gen_op_cmov_ir(rc);
}

static always_inline void gen_farith2 (DisasContext *ctx,
                                       void (*gen_arith_fop)(void),
                                       int rb, int rc)
{
    gen_load_fir(ctx, rb, 0);
    (*gen_arith_fop)();
    gen_store_fir(ctx, rc, 0);
}

static always_inline void gen_farith3 (DisasContext *ctx,
                                       void (*gen_arith_fop)(void),
                                       int ra, int rb, int rc)
{
    gen_load_fir(ctx, ra, 0);
    gen_load_fir(ctx, rb, 1);
    (*gen_arith_fop)();
    gen_store_fir(ctx, rc, 0);
}

static always_inline void gen_fcmov (DisasContext *ctx,
                                     void (*gen_test_fop)(void),
                                     int ra, int rb, int rc)
{
    gen_load_fir(ctx, ra, 0);
    gen_load_fir(ctx, rb, 1);
    (*gen_test_fop)();
    gen_op_cmov_fir(rc);
}

static always_inline void gen_fti (DisasContext *ctx,
                                   void (*gen_move_fop)(void),
                                   int ra, int rc)
{
    gen_load_fir(ctx, rc, 0);
    (*gen_move_fop)();
    if (ra != 31)
        tcg_gen_mov_i64(cpu_ir[ra], cpu_T[0]);
}

static always_inline void gen_itf (DisasContext *ctx,
                                   void (*gen_move_fop)(void),
                                   int ra, int rc)
{
    if (ra != 31)
        tcg_gen_mov_i64(cpu_T[0], cpu_ir[ra]);
    else
        tcg_gen_movi_i64(cpu_T[0], 0);
    (*gen_move_fop)();
    gen_store_fir(ctx, rc, 0);
}

static always_inline void gen_s4addl (void)
{
    tcg_gen_shli_i64(cpu_T[0], cpu_T[0], 2);
    gen_op_addl();
}

static always_inline void gen_s4subl (void)
{
    tcg_gen_shli_i64(cpu_T[0], cpu_T[0], 2);
    gen_op_subl();
}

static always_inline void gen_s8addl (void)
{
    tcg_gen_shli_i64(cpu_T[0], cpu_T[0], 3);
    gen_op_addl();
}

static always_inline void gen_s8subl (void)
{
    tcg_gen_shli_i64(cpu_T[0], cpu_T[0], 3);
    gen_op_subl();
}

static always_inline void gen_s4addq (void)
{
    tcg_gen_shli_i64(cpu_T[0], cpu_T[0], 2);
    tcg_gen_add_i64(cpu_T[0], cpu_T[0], cpu_T[1]);
}

static always_inline void gen_s4subq (void)
{
    tcg_gen_shli_i64(cpu_T[0], cpu_T[0], 2);
    tcg_gen_sub_i64(cpu_T[0], cpu_T[0], cpu_T[1]);
}

static always_inline void gen_s8addq (void)
{
    tcg_gen_shli_i64(cpu_T[0], cpu_T[0], 3);
    tcg_gen_add_i64(cpu_T[0], cpu_T[0], cpu_T[1]);
}

static always_inline void gen_s8subq (void)
{
    tcg_gen_shli_i64(cpu_T[0], cpu_T[0], 3);
    tcg_gen_sub_i64(cpu_T[0], cpu_T[0], cpu_T[1]);
}

static always_inline void gen_amask (void)
{
    gen_op_load_amask();
    gen_op_bic();
}

static always_inline int translate_one (DisasContext *ctx, uint32_t insn)
{
    uint32_t palcode;
    int32_t disp21, disp16, disp12;
    uint16_t fn11, fn16;
    uint8_t opc, ra, rb, rc, sbz, fpfn, fn7, fn2, islit;
    int8_t lit;
    int ret;

    /* Decode all instruction fields */
    opc = insn >> 26;
    ra = (insn >> 21) & 0x1F;
    rb = (insn >> 16) & 0x1F;
    rc = insn & 0x1F;
    sbz = (insn >> 13) & 0x07;
    islit = (insn >> 12) & 1;
    lit = (insn >> 13) & 0xFF;
    palcode = insn & 0x03FFFFFF;
    disp21 = ((int32_t)((insn & 0x001FFFFF) << 11)) >> 11;
    disp16 = (int16_t)(insn & 0x0000FFFF);
    disp12 = (int32_t)((insn & 0x00000FFF) << 20) >> 20;
    fn16 = insn & 0x0000FFFF;
    fn11 = (insn >> 5) & 0x000007FF;
    fpfn = fn11 & 0x3F;
    fn7 = (insn >> 5) & 0x0000007F;
    fn2 = (insn >> 5) & 0x00000003;
    ret = 0;
#if defined ALPHA_DEBUG_DISAS
    if (logfile != NULL) {
        fprintf(logfile, "opc %02x ra %d rb %d rc %d disp16 %04x\n",
                opc, ra, rb, rc, disp16);
    }
#endif
    switch (opc) {
    case 0x00:
        /* CALL_PAL */
        if (palcode >= 0x80 && palcode < 0xC0) {
            /* Unprivileged PAL call */
            gen_excp(ctx, EXCP_CALL_PAL + ((palcode & 0x1F) << 6), 0);
#if !defined (CONFIG_USER_ONLY)
        } else if (palcode < 0x40) {
            /* Privileged PAL code */
            if (ctx->mem_idx & 1)
                goto invalid_opc;
            else
                gen_excp(ctx, EXCP_CALL_PALP + ((palcode & 0x1F) << 6), 0);
#endif
        } else {
            /* Invalid PAL call */
            goto invalid_opc;
        }
        ret = 3;
        break;
    case 0x01:
        /* OPC01 */
        goto invalid_opc;
    case 0x02:
        /* OPC02 */
        goto invalid_opc;
    case 0x03:
        /* OPC03 */
        goto invalid_opc;
    case 0x04:
        /* OPC04 */
        goto invalid_opc;
    case 0x05:
        /* OPC05 */
        goto invalid_opc;
    case 0x06:
        /* OPC06 */
        goto invalid_opc;
    case 0x07:
        /* OPC07 */
        goto invalid_opc;
    case 0x08:
        /* LDA */
        if (ra != 31) {
            if (rb != 31)
                tcg_gen_addi_i64(cpu_ir[ra], cpu_ir[rb], disp16);
            else
                tcg_gen_movi_i64(cpu_ir[ra], disp16);
        }
        break;
    case 0x09:
        /* LDAH */
        if (ra != 31) {
            if (rb != 31)
                tcg_gen_addi_i64(cpu_ir[ra], cpu_ir[rb], disp16 << 16);
            else
                tcg_gen_movi_i64(cpu_ir[ra], disp16 << 16);
        }
        break;
    case 0x0A:
        /* LDBU */
        if (!(ctx->amask & AMASK_BWX))
            goto invalid_opc;
        gen_load_mem(ctx, &gen_ldbu, ra, rb, disp16, 0);
        break;
    case 0x0B:
        /* LDQ_U */
        gen_load_mem(ctx, &gen_ldq_u, ra, rb, disp16, 1);
        break;
    case 0x0C:
        /* LDWU */
        if (!(ctx->amask & AMASK_BWX))
            goto invalid_opc;
        gen_load_mem(ctx, &gen_ldwu, ra, rb, disp16, 0);
        break;
    case 0x0D:
        /* STW */
        if (!(ctx->amask & AMASK_BWX))
            goto invalid_opc;
        gen_store_mem(ctx, &gen_stw, ra, rb, disp16, 0);
        break;
    case 0x0E:
        /* STB */
        if (!(ctx->amask & AMASK_BWX))
            goto invalid_opc;
        gen_store_mem(ctx, &gen_stb, ra, rb, disp16, 0);
        break;
    case 0x0F:
        /* STQ_U */
        gen_store_mem(ctx, &gen_stq_u, ra, rb, disp16, 1);
        break;
    case 0x10:
        switch (fn7) {
        case 0x00:
            /* ADDL */
            gen_arith3(ctx, &gen_op_addl, ra, rb, rc, islit, lit);
            break;
        case 0x02:
            /* S4ADDL */
            gen_arith3(ctx, &gen_s4addl, ra, rb, rc, islit, lit);
            break;
        case 0x09:
            /* SUBL */
            gen_arith3(ctx, &gen_op_subl, ra, rb, rc, islit, lit);
            break;
        case 0x0B:
            /* S4SUBL */
            gen_arith3(ctx, &gen_s4subl, ra, rb, rc, islit, lit);
            break;
        case 0x0F:
            /* CMPBGE */
            gen_arith3(ctx, &gen_op_cmpbge, ra, rb, rc, islit, lit);
            break;
        case 0x12:
            /* S8ADDL */
            gen_arith3(ctx, &gen_s8addl, ra, rb, rc, islit, lit);
            break;
        case 0x1B:
            /* S8SUBL */
            gen_arith3(ctx, &gen_s8subl, ra, rb, rc, islit, lit);
            break;
        case 0x1D:
            /* CMPULT */
            gen_arith3(ctx, &gen_op_cmpult, ra, rb, rc, islit, lit);
            break;
        case 0x20:
            /* ADDQ */
            gen_arith3(ctx, &gen_op_addq, ra, rb, rc, islit, lit);
            break;
        case 0x22:
            /* S4ADDQ */
            gen_arith3(ctx, &gen_s4addq, ra, rb, rc, islit, lit);
            break;
        case 0x29:
            /* SUBQ */
            gen_arith3(ctx, &gen_op_subq, ra, rb, rc, islit, lit);
            break;
        case 0x2B:
            /* S4SUBQ */
            gen_arith3(ctx, &gen_s4subq, ra, rb, rc, islit, lit);
            break;
        case 0x2D:
            /* CMPEQ */
            gen_arith3(ctx, &gen_op_cmpeq, ra, rb, rc, islit, lit);
            break;
        case 0x32:
            /* S8ADDQ */
            gen_arith3(ctx, &gen_s8addq, ra, rb, rc, islit, lit);
            break;
        case 0x3B:
            /* S8SUBQ */
            gen_arith3(ctx, &gen_s8subq, ra, rb, rc, islit, lit);
            break;
        case 0x3D:
            /* CMPULE */
            gen_arith3(ctx, &gen_op_cmpule, ra, rb, rc, islit, lit);
            break;
        case 0x40:
            /* ADDL/V */
            gen_arith3(ctx, &gen_op_addlv, ra, rb, rc, islit, lit);
            break;
        case 0x49:
            /* SUBL/V */
            gen_arith3(ctx, &gen_op_sublv, ra, rb, rc, islit, lit);
            break;
        case 0x4D:
            /* CMPLT */
            gen_arith3(ctx, &gen_op_cmplt, ra, rb, rc, islit, lit);
            break;
        case 0x60:
            /* ADDQ/V */
            gen_arith3(ctx, &gen_op_addqv, ra, rb, rc, islit, lit);
            break;
        case 0x69:
            /* SUBQ/V */
            gen_arith3(ctx, &gen_op_subqv, ra, rb, rc, islit, lit);
            break;
        case 0x6D:
            /* CMPLE */
            gen_arith3(ctx, &gen_op_cmple, ra, rb, rc, islit, lit);
            break;
        default:
            goto invalid_opc;
        }
        break;
    case 0x11:
        switch (fn7) {
        case 0x00:
            /* AND */
            gen_arith3(ctx, &gen_op_and, ra, rb, rc, islit, lit);
            break;
        case 0x08:
            /* BIC */
            gen_arith3(ctx, &gen_op_bic, ra, rb, rc, islit, lit);
            break;
        case 0x14:
            /* CMOVLBS */
            gen_cmov(ctx, &gen_op_cmplbs, ra, rb, rc, islit, lit);
            break;
        case 0x16:
            /* CMOVLBC */
            gen_cmov(ctx, &gen_op_cmplbc, ra, rb, rc, islit, lit);
            break;
        case 0x20:
            /* BIS */
            if (ra == rb || ra == 31 || rb == 31) {
                if (ra == 31 && rc == 31) {
                    /* NOP */
                    gen_op_nop();
                } else {
                    /* MOV */
                    if (rc != 31) {
                        if (rb != 31)
                            tcg_gen_mov_i64(cpu_ir[rc], cpu_ir[rb]);
                        else
                            tcg_gen_movi_i64(cpu_ir[rc], 0);
                    }
                }
            } else {
                gen_arith3(ctx, &gen_op_bis, ra, rb, rc, islit, lit);
            }
            break;
        case 0x24:
            /* CMOVEQ */
            gen_cmov(ctx, &gen_op_cmpeqz, ra, rb, rc, islit, lit);
            break;
        case 0x26:
            /* CMOVNE */
            gen_cmov(ctx, &gen_op_cmpnez, ra, rb, rc, islit, lit);
            break;
        case 0x28:
            /* ORNOT */
            gen_arith3(ctx, &gen_op_ornot, ra, rb, rc, islit, lit);
            break;
        case 0x40:
            /* XOR */
            gen_arith3(ctx, &gen_op_xor, ra, rb, rc, islit, lit);
            break;
        case 0x44:
            /* CMOVLT */
            gen_cmov(ctx, &gen_op_cmpltz, ra, rb, rc, islit, lit);
            break;
        case 0x46:
            /* CMOVGE */
            gen_cmov(ctx, &gen_op_cmpgez, ra, rb, rc, islit, lit);
            break;
        case 0x48:
            /* EQV */
            gen_arith3(ctx, &gen_op_eqv, ra, rb, rc, islit, lit);
            break;
        case 0x61:
            /* AMASK */
            gen_arith2(ctx, &gen_amask, rb, rc, islit, lit);
            break;
        case 0x64:
            /* CMOVLE */
            gen_cmov(ctx, &gen_op_cmplez, ra, rb, rc, islit, lit);
            break;
        case 0x66:
            /* CMOVGT */
            gen_cmov(ctx, &gen_op_cmpgtz, ra, rb, rc, islit, lit);
            break;
        case 0x6C:
            /* IMPLVER */
            gen_op_load_implver();
            if (rc != 31)
                tcg_gen_mov_i64(cpu_ir[rc], cpu_T[0]);
            break;
        default:
            goto invalid_opc;
        }
        break;
    case 0x12:
        switch (fn7) {
        case 0x02:
            /* MSKBL */
            gen_arith3(ctx, &gen_op_mskbl, ra, rb, rc, islit, lit);
            break;
        case 0x06:
            /* EXTBL */
            gen_arith3(ctx, &gen_op_extbl, ra, rb, rc, islit, lit);
            break;
        case 0x0B:
            /* INSBL */
            gen_arith3(ctx, &gen_op_insbl, ra, rb, rc, islit, lit);
            break;
        case 0x12:
            /* MSKWL */
            gen_arith3(ctx, &gen_op_mskwl, ra, rb, rc, islit, lit);
            break;
        case 0x16:
            /* EXTWL */
            gen_arith3(ctx, &gen_op_extwl, ra, rb, rc, islit, lit);
            break;
        case 0x1B:
            /* INSWL */
            gen_arith3(ctx, &gen_op_inswl, ra, rb, rc, islit, lit);
            break;
        case 0x22:
            /* MSKLL */
            gen_arith3(ctx, &gen_op_mskll, ra, rb, rc, islit, lit);
            break;
        case 0x26:
            /* EXTLL */
            gen_arith3(ctx, &gen_op_extll, ra, rb, rc, islit, lit);
            break;
        case 0x2B:
            /* INSLL */
            gen_arith3(ctx, &gen_op_insll, ra, rb, rc, islit, lit);
            break;
        case 0x30:
            /* ZAP */
            gen_arith3(ctx, &gen_op_zap, ra, rb, rc, islit, lit);
            break;
        case 0x31:
            /* ZAPNOT */
            gen_arith3(ctx, &gen_op_zapnot, ra, rb, rc, islit, lit);
            break;
        case 0x32:
            /* MSKQL */
            gen_arith3(ctx, &gen_op_mskql, ra, rb, rc, islit, lit);
            break;
        case 0x34:
            /* SRL */
            gen_arith3(ctx, &gen_op_srl, ra, rb, rc, islit, lit);
            break;
        case 0x36:
            /* EXTQL */
            gen_arith3(ctx, &gen_op_extql, ra, rb, rc, islit, lit);
            break;
        case 0x39:
            /* SLL */
            gen_arith3(ctx, &gen_op_sll, ra, rb, rc, islit, lit);
            break;
        case 0x3B:
            /* INSQL */
            gen_arith3(ctx, &gen_op_insql, ra, rb, rc, islit, lit);
            break;
        case 0x3C:
            /* SRA */
            gen_arith3(ctx, &gen_op_sra, ra, rb, rc, islit, lit);
            break;
        case 0x52:
            /* MSKWH */
            gen_arith3(ctx, &gen_op_mskwh, ra, rb, rc, islit, lit);
            break;
        case 0x57:
            /* INSWH */
            gen_arith3(ctx, &gen_op_inswh, ra, rb, rc, islit, lit);
            break;
        case 0x5A:
            /* EXTWH */
            gen_arith3(ctx, &gen_op_extwh, ra, rb, rc, islit, lit);
            break;
        case 0x62:
            /* MSKLH */
            gen_arith3(ctx, &gen_op_msklh, ra, rb, rc, islit, lit);
            break;
        case 0x67:
            /* INSLH */
            gen_arith3(ctx, &gen_op_inslh, ra, rb, rc, islit, lit);
            break;
        case 0x6A:
            /* EXTLH */
            gen_arith3(ctx, &gen_op_extlh, ra, rb, rc, islit, lit);
            break;
        case 0x72:
            /* MSKQH */
            gen_arith3(ctx, &gen_op_mskqh, ra, rb, rc, islit, lit);
            break;
        case 0x77:
            /* INSQH */
            gen_arith3(ctx, &gen_op_insqh, ra, rb, rc, islit, lit);
            break;
        case 0x7A:
            /* EXTQH */
            gen_arith3(ctx, &gen_op_extqh, ra, rb, rc, islit, lit);
            break;
        default:
            goto invalid_opc;
        }
        break;
    case 0x13:
        switch (fn7) {
        case 0x00:
            /* MULL */
            gen_arith3(ctx, &gen_op_mull, ra, rb, rc, islit, lit);
            break;
        case 0x20:
            /* MULQ */
            gen_arith3(ctx, &gen_op_mulq, ra, rb, rc, islit, lit);
            break;
        case 0x30:
            /* UMULH */
            gen_arith3(ctx, &gen_op_umulh, ra, rb, rc, islit, lit);
            break;
        case 0x40:
            /* MULL/V */
            gen_arith3(ctx, &gen_op_mullv, ra, rb, rc, islit, lit);
            break;
        case 0x60:
            /* MULQ/V */
            gen_arith3(ctx, &gen_op_mulqv, ra, rb, rc, islit, lit);
            break;
        default:
            goto invalid_opc;
        }
        break;
    case 0x14:
        switch (fpfn) { /* f11 & 0x3F */
        case 0x04:
            /* ITOFS */
            if (!(ctx->amask & AMASK_FIX))
                goto invalid_opc;
            gen_itf(ctx, &gen_op_itofs, ra, rc);
            break;
        case 0x0A:
            /* SQRTF */
            if (!(ctx->amask & AMASK_FIX))
                goto invalid_opc;
            gen_farith2(ctx, &gen_op_sqrtf, rb, rc);
            break;
        case 0x0B:
            /* SQRTS */
            if (!(ctx->amask & AMASK_FIX))
                goto invalid_opc;
            gen_farith2(ctx, &gen_op_sqrts, rb, rc);
            break;
        case 0x14:
            /* ITOFF */
            if (!(ctx->amask & AMASK_FIX))
                goto invalid_opc;
#if 0 // TODO
            gen_itf(ctx, &gen_op_itoff, ra, rc);
#else
            goto invalid_opc;
#endif
            break;
        case 0x24:
            /* ITOFT */
            if (!(ctx->amask & AMASK_FIX))
                goto invalid_opc;
            gen_itf(ctx, &gen_op_itoft, ra, rc);
            break;
        case 0x2A:
            /* SQRTG */
            if (!(ctx->amask & AMASK_FIX))
                goto invalid_opc;
            gen_farith2(ctx, &gen_op_sqrtg, rb, rc);
            break;
        case 0x02B:
            /* SQRTT */
            if (!(ctx->amask & AMASK_FIX))
                goto invalid_opc;
            gen_farith2(ctx, &gen_op_sqrtt, rb, rc);
            break;
        default:
            goto invalid_opc;
        }
        break;
    case 0x15:
        /* VAX floating point */
        /* XXX: rounding mode and trap are ignored (!) */
        switch (fpfn) { /* f11 & 0x3F */
        case 0x00:
            /* ADDF */
            gen_farith3(ctx, &gen_op_addf, ra, rb, rc);
            break;
        case 0x01:
            /* SUBF */
            gen_farith3(ctx, &gen_op_subf, ra, rb, rc);
            break;
        case 0x02:
            /* MULF */
            gen_farith3(ctx, &gen_op_mulf, ra, rb, rc);
            break;
        case 0x03:
            /* DIVF */
            gen_farith3(ctx, &gen_op_divf, ra, rb, rc);
            break;
        case 0x1E:
            /* CVTDG */
#if 0 // TODO
            gen_farith2(ctx, &gen_op_cvtdg, rb, rc);
#else
            goto invalid_opc;
#endif
            break;
        case 0x20:
            /* ADDG */
            gen_farith3(ctx, &gen_op_addg, ra, rb, rc);
            break;
        case 0x21:
            /* SUBG */
            gen_farith3(ctx, &gen_op_subg, ra, rb, rc);
            break;
        case 0x22:
            /* MULG */
            gen_farith3(ctx, &gen_op_mulg, ra, rb, rc);
            break;
        case 0x23:
            /* DIVG */
            gen_farith3(ctx, &gen_op_divg, ra, rb, rc);
            break;
        case 0x25:
            /* CMPGEQ */
            gen_farith3(ctx, &gen_op_cmpgeq, ra, rb, rc);
            break;
        case 0x26:
            /* CMPGLT */
            gen_farith3(ctx, &gen_op_cmpglt, ra, rb, rc);
            break;
        case 0x27:
            /* CMPGLE */
            gen_farith3(ctx, &gen_op_cmpgle, ra, rb, rc);
            break;
        case 0x2C:
            /* CVTGF */
            gen_farith2(ctx, &gen_op_cvtgf, rb, rc);
            break;
        case 0x2D:
            /* CVTGD */
#if 0 // TODO
            gen_farith2(ctx, &gen_op_cvtgd, rb, rc);
#else
            goto invalid_opc;
#endif
            break;
        case 0x2F:
            /* CVTGQ */
            gen_farith2(ctx, &gen_op_cvtgq, rb, rc);
            break;
        case 0x3C:
            /* CVTQF */
            gen_farith2(ctx, &gen_op_cvtqf, rb, rc);
            break;
        case 0x3E:
            /* CVTQG */
            gen_farith2(ctx, &gen_op_cvtqg, rb, rc);
            break;
        default:
            goto invalid_opc;
        }
        break;
    case 0x16:
        /* IEEE floating-point */
        /* XXX: rounding mode and traps are ignored (!) */
        switch (fpfn) { /* f11 & 0x3F */
        case 0x00:
            /* ADDS */
            gen_farith3(ctx, &gen_op_adds, ra, rb, rc);
            break;
        case 0x01:
            /* SUBS */
            gen_farith3(ctx, &gen_op_subs, ra, rb, rc);
            break;
        case 0x02:
            /* MULS */
            gen_farith3(ctx, &gen_op_muls, ra, rb, rc);
            break;
        case 0x03:
            /* DIVS */
            gen_farith3(ctx, &gen_op_divs, ra, rb, rc);
            break;
        case 0x20:
            /* ADDT */
            gen_farith3(ctx, &gen_op_addt, ra, rb, rc);
            break;
        case 0x21:
            /* SUBT */
            gen_farith3(ctx, &gen_op_subt, ra, rb, rc);
            break;
        case 0x22:
            /* MULT */
            gen_farith3(ctx, &gen_op_mult, ra, rb, rc);
            break;
        case 0x23:
            /* DIVT */
            gen_farith3(ctx, &gen_op_divt, ra, rb, rc);
            break;
        case 0x24:
            /* CMPTUN */
            gen_farith3(ctx, &gen_op_cmptun, ra, rb, rc);
            break;
        case 0x25:
            /* CMPTEQ */
            gen_farith3(ctx, &gen_op_cmpteq, ra, rb, rc);
            break;
        case 0x26:
            /* CMPTLT */
            gen_farith3(ctx, &gen_op_cmptlt, ra, rb, rc);
            break;
        case 0x27:
            /* CMPTLE */
            gen_farith3(ctx, &gen_op_cmptle, ra, rb, rc);
            break;
        case 0x2C:
            /* XXX: incorrect */
            if (fn11 == 0x2AC) {
                /* CVTST */
                gen_farith2(ctx, &gen_op_cvtst, rb, rc);
            } else {
                /* CVTTS */
                gen_farith2(ctx, &gen_op_cvtts, rb, rc);
            }
            break;
        case 0x2F:
            /* CVTTQ */
            gen_farith2(ctx, &gen_op_cvttq, rb, rc);
            break;
        case 0x3C:
            /* CVTQS */
            gen_farith2(ctx, &gen_op_cvtqs, rb, rc);
            break;
        case 0x3E:
            /* CVTQT */
            gen_farith2(ctx, &gen_op_cvtqt, rb, rc);
            break;
        default:
            goto invalid_opc;
        }
        break;
    case 0x17:
        switch (fn11) {
        case 0x010:
            /* CVTLQ */
            gen_farith2(ctx, &gen_op_cvtlq, rb, rc);
            break;
        case 0x020:
            /* CPYS */
            if (ra == rb) {
                if (ra == 31 && rc == 31) {
                    /* FNOP */
                    gen_op_nop();
                } else {
                    /* FMOV */
                    gen_load_fir(ctx, rb, 0);
                    gen_store_fir(ctx, rc, 0);
                }
            } else {
                gen_farith3(ctx, &gen_op_cpys, ra, rb, rc);
            }
            break;
        case 0x021:
            /* CPYSN */
            gen_farith2(ctx, &gen_op_cpysn, rb, rc);
            break;
        case 0x022:
            /* CPYSE */
            gen_farith2(ctx, &gen_op_cpyse, rb, rc);
            break;
        case 0x024:
            /* MT_FPCR */
            gen_load_fir(ctx, ra, 0);
            gen_op_store_fpcr();
            break;
        case 0x025:
            /* MF_FPCR */
            gen_op_load_fpcr();
            gen_store_fir(ctx, ra, 0);
            break;
        case 0x02A:
            /* FCMOVEQ */
            gen_fcmov(ctx, &gen_op_cmpfeq, ra, rb, rc);
            break;
        case 0x02B:
            /* FCMOVNE */
            gen_fcmov(ctx, &gen_op_cmpfne, ra, rb, rc);
            break;
        case 0x02C:
            /* FCMOVLT */
            gen_fcmov(ctx, &gen_op_cmpflt, ra, rb, rc);
            break;
        case 0x02D:
            /* FCMOVGE */
            gen_fcmov(ctx, &gen_op_cmpfge, ra, rb, rc);
            break;
        case 0x02E:
            /* FCMOVLE */
            gen_fcmov(ctx, &gen_op_cmpfle, ra, rb, rc);
            break;
        case 0x02F:
            /* FCMOVGT */
            gen_fcmov(ctx, &gen_op_cmpfgt, ra, rb, rc);
            break;
        case 0x030:
            /* CVTQL */
            gen_farith2(ctx, &gen_op_cvtql, rb, rc);
            break;
        case 0x130:
            /* CVTQL/V */
            gen_farith2(ctx, &gen_op_cvtqlv, rb, rc);
            break;
        case 0x530:
            /* CVTQL/SV */
            gen_farith2(ctx, &gen_op_cvtqlsv, rb, rc);
            break;
        default:
            goto invalid_opc;
        }
        break;
    case 0x18:
        switch ((uint16_t)disp16) {
        case 0x0000:
            /* TRAPB */
            /* No-op. Just exit from the current tb */
            ret = 2;
            break;
        case 0x0400:
            /* EXCB */
            /* No-op. Just exit from the current tb */
            ret = 2;
            break;
        case 0x4000:
            /* MB */
            /* No-op */
            break;
        case 0x4400:
            /* WMB */
            /* No-op */
            break;
        case 0x8000:
            /* FETCH */
            /* No-op */
            break;
        case 0xA000:
            /* FETCH_M */
            /* No-op */
            break;
        case 0xC000:
            /* RPCC */
            gen_op_load_pcc();
            if (ra != 31)
                tcg_gen_mov_i64(cpu_ir[ra], cpu_T[0]);
            break;
        case 0xE000:
            /* RC */
            gen_op_load_irf();
            if (ra != 31)
                tcg_gen_mov_i64(cpu_ir[ra], cpu_T[0]);
            gen_op_clear_irf();
            break;
        case 0xE800:
            /* ECB */
            /* XXX: TODO: evict tb cache at address rb */
#if 0
            ret = 2;
#else
            goto invalid_opc;
#endif
            break;
        case 0xF000:
            /* RS */
            gen_op_load_irf();
            if (ra != 31)
                tcg_gen_mov_i64(cpu_ir[ra], cpu_T[0]);
            gen_op_set_irf();
            break;
        case 0xF800:
            /* WH64 */
            /* No-op */
            break;
        default:
            goto invalid_opc;
        }
        break;
    case 0x19:
        /* HW_MFPR (PALcode) */
#if defined (CONFIG_USER_ONLY)
        goto invalid_opc;
#else
        if (!ctx->pal_mode)
            goto invalid_opc;
        gen_op_mfpr(insn & 0xFF);
        if (ra != 31)
            tcg_gen_mov_i64(cpu_ir[ra], cpu_T[0]);
        break;
#endif
    case 0x1A:
        if (ra != 31)
            tcg_gen_movi_i64(cpu_ir[ra], ctx->pc);
        if (rb != 31)
            tcg_gen_andi_i64(cpu_pc, cpu_ir[rb], ~3);
        else
            tcg_gen_movi_i64(cpu_pc, 0);
        /* Those four jumps only differ by the branch prediction hint */
        switch (fn2) {
        case 0x0:
            /* JMP */
            break;
        case 0x1:
            /* JSR */
            break;
        case 0x2:
            /* RET */
            break;
        case 0x3:
            /* JSR_COROUTINE */
            break;
        }
        ret = 1;
        break;
    case 0x1B:
        /* HW_LD (PALcode) */
#if defined (CONFIG_USER_ONLY)
        goto invalid_opc;
#else
        if (!ctx->pal_mode)
            goto invalid_opc;
        if (rb != 31)
            tcg_gen_mov_i64(cpu_T[0], cpu_ir[rb]);
        else
            tcg_gen_movi_i64(cpu_T[0], 0);
        tcg_gen_movi_i64(cpu_T[1], disp12);
        tcg_gen_add_i64(cpu_T[0], cpu_T[0], cpu_T[1]);
        switch ((insn >> 12) & 0xF) {
        case 0x0:
            /* Longword physical access */
            gen_op_ldl_raw();
            break;
        case 0x1:
            /* Quadword physical access */
            gen_op_ldq_raw();
            break;
        case 0x2:
            /* Longword physical access with lock */
            gen_op_ldl_l_raw();
            break;
        case 0x3:
            /* Quadword physical access with lock */
            gen_op_ldq_l_raw();
            break;
        case 0x4:
            /* Longword virtual PTE fetch */
            gen_op_ldl_kernel();
            break;
        case 0x5:
            /* Quadword virtual PTE fetch */
            gen_op_ldq_kernel();
            break;
        case 0x6:
            /* Invalid */
            goto invalid_opc;
        case 0x7:
            /* Invalid */
            goto invalid_opc;
        case 0x8:
            /* Longword virtual access */
            gen_op_ld_phys_to_virt();
            gen_op_ldl_raw();
            break;
        case 0x9:
            /* Quadword virtual access */
            gen_op_ld_phys_to_virt();
            gen_op_ldq_raw();
            break;
        case 0xA:
            /* Longword virtual access with protection check */
            gen_ldl(ctx);
            break;
        case 0xB:
            /* Quadword virtual access with protection check */
            gen_ldq(ctx);
            break;
        case 0xC:
            /* Longword virtual access with altenate access mode */
            gen_op_set_alt_mode();
            gen_op_ld_phys_to_virt();
            gen_op_ldl_raw();
            gen_op_restore_mode();
            break;
        case 0xD:
            /* Quadword virtual access with altenate access mode */
            gen_op_set_alt_mode();
            gen_op_ld_phys_to_virt();
            gen_op_ldq_raw();
            gen_op_restore_mode();
            break;
        case 0xE:
            /* Longword virtual access with alternate access mode and
             * protection checks
             */
            gen_op_set_alt_mode();
            gen_op_ldl_data();
            gen_op_restore_mode();
            break;
        case 0xF:
            /* Quadword virtual access with alternate access mode and
             * protection checks
             */
            gen_op_set_alt_mode();
            gen_op_ldq_data();
            gen_op_restore_mode();
            break;
        }
        if (ra != 31)
            tcg_gen_mov_i64(cpu_ir[ra], cpu_T[1]);
        break;
#endif
    case 0x1C:
        switch (fn7) {
        case 0x00:
            /* SEXTB */
            if (!(ctx->amask & AMASK_BWX))
                goto invalid_opc;
            gen_arith2(ctx, &gen_op_sextb, rb, rc, islit, lit);
            break;
        case 0x01:
            /* SEXTW */
            if (!(ctx->amask & AMASK_BWX))
                goto invalid_opc;
            gen_arith2(ctx, &gen_op_sextw, rb, rc, islit, lit);
            break;
        case 0x30:
            /* CTPOP */
            if (!(ctx->amask & AMASK_CIX))
                goto invalid_opc;
            gen_arith2(ctx, &gen_op_ctpop, rb, rc, 0, 0);
            break;
        case 0x31:
            /* PERR */
            if (!(ctx->amask & AMASK_MVI))
                goto invalid_opc;
            /* XXX: TODO */
            goto invalid_opc;
            break;
        case 0x32:
            /* CTLZ */
            if (!(ctx->amask & AMASK_CIX))
                goto invalid_opc;
            gen_arith2(ctx, &gen_op_ctlz, rb, rc, 0, 0);
            break;
        case 0x33:
            /* CTTZ */
            if (!(ctx->amask & AMASK_CIX))
                goto invalid_opc;
            gen_arith2(ctx, &gen_op_cttz, rb, rc, 0, 0);
            break;
        case 0x34:
            /* UNPKBW */
            if (!(ctx->amask & AMASK_MVI))
                goto invalid_opc;
            /* XXX: TODO */
            goto invalid_opc;
            break;
        case 0x35:
            /* UNPKWL */
            if (!(ctx->amask & AMASK_MVI))
                goto invalid_opc;
            /* XXX: TODO */
            goto invalid_opc;
            break;
        case 0x36:
            /* PKWB */
            if (!(ctx->amask & AMASK_MVI))
                goto invalid_opc;
            /* XXX: TODO */
            goto invalid_opc;
            break;
        case 0x37:
            /* PKLB */
            if (!(ctx->amask & AMASK_MVI))
                goto invalid_opc;
            /* XXX: TODO */
            goto invalid_opc;
            break;
        case 0x38:
            /* MINSB8 */
            if (!(ctx->amask & AMASK_MVI))
                goto invalid_opc;
            /* XXX: TODO */
            goto invalid_opc;
            break;
        case 0x39:
            /* MINSW4 */
            if (!(ctx->amask & AMASK_MVI))
                goto invalid_opc;
            /* XXX: TODO */
            goto invalid_opc;
            break;
        case 0x3A:
            /* MINUB8 */
            if (!(ctx->amask & AMASK_MVI))
                goto invalid_opc;
            /* XXX: TODO */
            goto invalid_opc;
            break;
        case 0x3B:
            /* MINUW4 */
            if (!(ctx->amask & AMASK_MVI))
                goto invalid_opc;
            /* XXX: TODO */
            goto invalid_opc;
            break;
        case 0x3C:
            /* MAXUB8 */
            if (!(ctx->amask & AMASK_MVI))
                goto invalid_opc;
            /* XXX: TODO */
            goto invalid_opc;
            break;
        case 0x3D:
            /* MAXUW4 */
            if (!(ctx->amask & AMASK_MVI))
                goto invalid_opc;
            /* XXX: TODO */
            goto invalid_opc;
            break;
        case 0x3E:
            /* MAXSB8 */
            if (!(ctx->amask & AMASK_MVI))
                goto invalid_opc;
            /* XXX: TODO */
            goto invalid_opc;
            break;
        case 0x3F:
            /* MAXSW4 */
            if (!(ctx->amask & AMASK_MVI))
                goto invalid_opc;
            /* XXX: TODO */
            goto invalid_opc;
            break;
        case 0x70:
            /* FTOIT */
            if (!(ctx->amask & AMASK_FIX))
                goto invalid_opc;
            gen_fti(ctx, &gen_op_ftoit, ra, rb);
            break;
        case 0x78:
            /* FTOIS */
            if (!(ctx->amask & AMASK_FIX))
                goto invalid_opc;
            gen_fti(ctx, &gen_op_ftois, ra, rb);
            break;
        default:
            goto invalid_opc;
        }
        break;
    case 0x1D:
        /* HW_MTPR (PALcode) */
#if defined (CONFIG_USER_ONLY)
        goto invalid_opc;
#else
        if (!ctx->pal_mode)
            goto invalid_opc;
        if (ra != 31)
            tcg_gen_mov_i64(cpu_T[0], cpu_ir[ra]);
        else
            tcg_gen_movi_i64(cpu_T[0], 0);
        gen_op_mtpr(insn & 0xFF);
        ret = 2;
        break;
#endif
    case 0x1E:
        /* HW_REI (PALcode) */
#if defined (CONFIG_USER_ONLY)
        goto invalid_opc;
#else
        if (!ctx->pal_mode)
            goto invalid_opc;
        if (rb == 31) {
            /* "Old" alpha */
            gen_op_hw_rei();
        } else {
            if (ra != 31)
                tcg_gen_mov_i64(cpu_T[0], cpu_ir[rb]);
            else
                tcg_gen_movi_i64(cpu_T[0], 0);
            tcg_gen_movi_i64(cpu_T[1], (((int64_t)insn << 51) >> 51));
            tcg_gen_add_i64(cpu_T[0], cpu_T[0], cpu_T[1]);
            gen_op_hw_ret();
        }
        ret = 2;
        break;
#endif
    case 0x1F:
        /* HW_ST (PALcode) */
#if defined (CONFIG_USER_ONLY)
        goto invalid_opc;
#else
        if (!ctx->pal_mode)
            goto invalid_opc;
        if (ra != 31)
            tcg_gen_addi_i64(cpu_T[0], cpu_ir[rb], disp12);
        else
            tcg_gen_movi_i64(cpu_T[0], disp12);
        if (ra != 31)
            tcg_gen_mov_i64(cpu_T[1], cpu_ir[ra]);
        else
            tcg_gen_movi_i64(cpu_T[1], 0);
        switch ((insn >> 12) & 0xF) {
        case 0x0:
            /* Longword physical access */
            gen_op_stl_raw();
            break;
        case 0x1:
            /* Quadword physical access */
            gen_op_stq_raw();
            break;
        case 0x2:
            /* Longword physical access with lock */
            gen_op_stl_c_raw();
            break;
        case 0x3:
            /* Quadword physical access with lock */
            gen_op_stq_c_raw();
            break;
        case 0x4:
            /* Longword virtual access */
            gen_op_st_phys_to_virt();
            gen_op_stl_raw();
            break;
        case 0x5:
            /* Quadword virtual access */
            gen_op_st_phys_to_virt();
            gen_op_stq_raw();
            break;
        case 0x6:
            /* Invalid */
            goto invalid_opc;
        case 0x7:
            /* Invalid */
            goto invalid_opc;
        case 0x8:
            /* Invalid */
            goto invalid_opc;
        case 0x9:
            /* Invalid */
            goto invalid_opc;
        case 0xA:
            /* Invalid */
            goto invalid_opc;
        case 0xB:
            /* Invalid */
            goto invalid_opc;
        case 0xC:
            /* Longword virtual access with alternate access mode */
            gen_op_set_alt_mode();
            gen_op_st_phys_to_virt();
            gen_op_ldl_raw();
            gen_op_restore_mode();
            break;
        case 0xD:
            /* Quadword virtual access with alternate access mode */
            gen_op_set_alt_mode();
            gen_op_st_phys_to_virt();
            gen_op_ldq_raw();
            gen_op_restore_mode();
            break;
        case 0xE:
            /* Invalid */
            goto invalid_opc;
        case 0xF:
            /* Invalid */
            goto invalid_opc;
        }
        ret = 2;
        break;
#endif
    case 0x20:
        /* LDF */
#if 0 // TODO
        gen_load_fmem(ctx, &gen_ldf, ra, rb, disp16);
#else
        goto invalid_opc;
#endif
        break;
    case 0x21:
        /* LDG */
#if 0 // TODO
        gen_load_fmem(ctx, &gen_ldg, ra, rb, disp16);
#else
        goto invalid_opc;
#endif
        break;
    case 0x22:
        /* LDS */
        gen_load_fmem(ctx, &gen_lds, ra, rb, disp16);
        break;
    case 0x23:
        /* LDT */
        gen_load_fmem(ctx, &gen_ldt, ra, rb, disp16);
        break;
    case 0x24:
        /* STF */
#if 0 // TODO
        gen_store_fmem(ctx, &gen_stf, ra, rb, disp16);
#else
        goto invalid_opc;
#endif
        break;
    case 0x25:
        /* STG */
#if 0 // TODO
        gen_store_fmem(ctx, &gen_stg, ra, rb, disp16);
#else
        goto invalid_opc;
#endif
        break;
    case 0x26:
        /* STS */
        gen_store_fmem(ctx, &gen_sts, ra, rb, disp16);
        break;
    case 0x27:
        /* STT */
        gen_store_fmem(ctx, &gen_stt, ra, rb, disp16);
        break;
    case 0x28:
        /* LDL */
        gen_load_mem(ctx, &gen_ldl, ra, rb, disp16, 0);
        break;
    case 0x29:
        /* LDQ */
        gen_load_mem(ctx, &gen_ldq, ra, rb, disp16, 0);
        break;
    case 0x2A:
        /* LDL_L */
        gen_load_mem(ctx, &gen_ldl_l, ra, rb, disp16, 0);
        break;
    case 0x2B:
        /* LDQ_L */
        gen_load_mem(ctx, &gen_ldq_l, ra, rb, disp16, 0);
        break;
    case 0x2C:
        /* STL */
        gen_store_mem(ctx, &gen_stl, ra, rb, disp16, 0);
        break;
    case 0x2D:
        /* STQ */
        gen_store_mem(ctx, &gen_stq, ra, rb, disp16, 0);
        break;
    case 0x2E:
        /* STL_C */
        gen_store_mem(ctx, &gen_stl_c, ra, rb, disp16, 0);
        break;
    case 0x2F:
        /* STQ_C */
        gen_store_mem(ctx, &gen_stq_c, ra, rb, disp16, 0);
        break;
    case 0x30:
        /* BR */
        if (ra != 31)
            tcg_gen_movi_i64(cpu_ir[ra], ctx->pc);
        tcg_gen_movi_i64(cpu_pc, ctx->pc + (int64_t)(disp21 << 2));
        ret = 1;
        break;
    case 0x31:
        /* FBEQ */
        gen_fbcond(ctx, &gen_op_cmpfeq, ra, disp16);
        ret = 1;
        break;
    case 0x32:
        /* FBLT */
        gen_fbcond(ctx, &gen_op_cmpflt, ra, disp16);
        ret = 1;
        break;
    case 0x33:
        /* FBLE */
        gen_fbcond(ctx, &gen_op_cmpfle, ra, disp16);
        ret = 1;
        break;
    case 0x34:
        /* BSR */
        if (ra != 31)
            tcg_gen_movi_i64(cpu_ir[ra], ctx->pc);
        tcg_gen_movi_i64(cpu_pc, ctx->pc + (int64_t)(disp21 << 2));
        ret = 1;
        break;
    case 0x35:
        /* FBNE */
        gen_fbcond(ctx, &gen_op_cmpfne, ra, disp16);
        ret = 1;
        break;
    case 0x36:
        /* FBGE */
        gen_fbcond(ctx, &gen_op_cmpfge, ra, disp16);
        ret = 1;
        break;
    case 0x37:
        /* FBGT */
        gen_fbcond(ctx, &gen_op_cmpfgt, ra, disp16);
        ret = 1;
        break;
    case 0x38:
        /* BLBC */
        gen_bcond(ctx, &gen_op_cmplbc, ra, disp16);
        ret = 1;
        break;
    case 0x39:
        /* BEQ */
        gen_bcond(ctx, &gen_op_cmpeqz, ra, disp16);
        ret = 1;
        break;
    case 0x3A:
        /* BLT */
        gen_bcond(ctx, &gen_op_cmpltz, ra, disp16);
        ret = 1;
        break;
    case 0x3B:
        /* BLE */
        gen_bcond(ctx, &gen_op_cmplez, ra, disp16);
        ret = 1;
        break;
    case 0x3C:
        /* BLBS */
        gen_bcond(ctx, &gen_op_cmplbs, ra, disp16);
        ret = 1;
        break;
    case 0x3D:
        /* BNE */
        gen_bcond(ctx, &gen_op_cmpnez, ra, disp16);
        ret = 1;
        break;
    case 0x3E:
        /* BGE */
        gen_bcond(ctx, &gen_op_cmpgez, ra, disp16);
        ret = 1;
        break;
    case 0x3F:
        /* BGT */
        gen_bcond(ctx, &gen_op_cmpgtz, ra, disp16);
        ret = 1;
        break;
    invalid_opc:
        gen_invalid(ctx);
        ret = 3;
        break;
    }

    return ret;
}

static always_inline void gen_intermediate_code_internal (CPUState *env,
                                                          TranslationBlock *tb,
                                                          int search_pc)
{
#if defined ALPHA_DEBUG_DISAS
    static int insn_count;
#endif
    DisasContext ctx, *ctxp = &ctx;
    target_ulong pc_start;
    uint32_t insn;
    uint16_t *gen_opc_end;
    int j, lj = -1;
    int ret;
    int num_insns;
    int max_insns;

    pc_start = tb->pc;
    gen_opc_end = gen_opc_buf + OPC_MAX_SIZE;
    ctx.pc = pc_start;
    ctx.amask = env->amask;
#if defined (CONFIG_USER_ONLY)
    ctx.mem_idx = 0;
#else
    ctx.mem_idx = ((env->ps >> 3) & 3);
    ctx.pal_mode = env->ipr[IPR_EXC_ADDR] & 1;
#endif
    num_insns = 0;
    max_insns = tb->cflags & CF_COUNT_MASK;
    if (max_insns == 0)
        max_insns = CF_COUNT_MASK;

    gen_icount_start();
    for (ret = 0; ret == 0;) {
        if (env->nb_breakpoints > 0) {
            for(j = 0; j < env->nb_breakpoints; j++) {
                if (env->breakpoints[j] == ctx.pc) {
                    gen_excp(&ctx, EXCP_DEBUG, 0);
                    break;
                }
            }
        }
        if (search_pc) {
            j = gen_opc_ptr - gen_opc_buf;
            if (lj < j) {
                lj++;
                while (lj < j)
                    gen_opc_instr_start[lj++] = 0;
                gen_opc_pc[lj] = ctx.pc;
                gen_opc_instr_start[lj] = 1;
                gen_opc_icount[lj] = num_insns;
            }
        }
        if (num_insns + 1 == max_insns && (tb->cflags & CF_LAST_IO))
            gen_io_start();
#if defined ALPHA_DEBUG_DISAS
        insn_count++;
        if (logfile != NULL) {
            fprintf(logfile, "pc " TARGET_FMT_lx " mem_idx %d\n",
                    ctx.pc, ctx.mem_idx);
        }
#endif
        insn = ldl_code(ctx.pc);
#if defined ALPHA_DEBUG_DISAS
        insn_count++;
        if (logfile != NULL) {
            fprintf(logfile, "opcode %08x %d\n", insn, insn_count);
        }
#endif
        num_insns++;
        ctx.pc += 4;
        ret = translate_one(ctxp, insn);
        if (ret != 0)
            break;
        /* if we reach a page boundary or are single stepping, stop
         * generation
         */
        if (((ctx.pc & (TARGET_PAGE_SIZE - 1)) == 0) ||
            (env->singlestep_enabled) ||
            num_insns >= max_insns) {
            break;
        }
#if defined (DO_SINGLE_STEP)
        break;
#endif
    }
    if (ret != 1 && ret != 3) {
        tcg_gen_movi_i64(cpu_pc, ctx.pc);
    }
#if defined (DO_TB_FLUSH)
    tcg_gen_helper_0_0(helper_tb_flush);
#endif
    if (tb->cflags & CF_LAST_IO)
        gen_io_end();
    /* Generate the return instruction */
    tcg_gen_exit_tb(0);
    gen_icount_end(tb, num_insns);
    *gen_opc_ptr = INDEX_op_end;
    if (search_pc) {
        j = gen_opc_ptr - gen_opc_buf;
        lj++;
        while (lj <= j)
            gen_opc_instr_start[lj++] = 0;
    } else {
        tb->size = ctx.pc - pc_start;
        tb->icount = num_insns;
    }
#if defined ALPHA_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_CPU) {
        cpu_dump_state(env, logfile, fprintf, 0);
    }
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "IN: %s\n", lookup_symbol(pc_start));
	target_disas(logfile, pc_start, ctx.pc - pc_start, 1);
        fprintf(logfile, "\n");
    }
#endif
}

void gen_intermediate_code (CPUState *env, struct TranslationBlock *tb)
{
    gen_intermediate_code_internal(env, tb, 0);
}

void gen_intermediate_code_pc (CPUState *env, struct TranslationBlock *tb)
{
    gen_intermediate_code_internal(env, tb, 1);
}

CPUAlphaState * cpu_alpha_init (const char *cpu_model)
{
    CPUAlphaState *env;
    uint64_t hwpcb;

    env = qemu_mallocz(sizeof(CPUAlphaState));
    if (!env)
        return NULL;
    cpu_exec_init(env);
    alpha_translate_init();
    tlb_flush(env, 1);
    /* XXX: should not be hardcoded */
    env->implver = IMPLVER_2106x;
    env->ps = 0x1F00;
#if defined (CONFIG_USER_ONLY)
    env->ps |= 1 << 3;
#endif
    pal_init(env);
    /* Initialize IPR */
    hwpcb = env->ipr[IPR_PCBB];
    env->ipr[IPR_ASN] = 0;
    env->ipr[IPR_ASTEN] = 0;
    env->ipr[IPR_ASTSR] = 0;
    env->ipr[IPR_DATFX] = 0;
    /* XXX: fix this */
    //    env->ipr[IPR_ESP] = ldq_raw(hwpcb + 8);
    //    env->ipr[IPR_KSP] = ldq_raw(hwpcb + 0);
    //    env->ipr[IPR_SSP] = ldq_raw(hwpcb + 16);
    //    env->ipr[IPR_USP] = ldq_raw(hwpcb + 24);
    env->ipr[IPR_FEN] = 0;
    env->ipr[IPR_IPL] = 31;
    env->ipr[IPR_MCES] = 0;
    env->ipr[IPR_PERFMON] = 0; /* Implementation specific */
    //    env->ipr[IPR_PTBR] = ldq_raw(hwpcb + 32);
    env->ipr[IPR_SISR] = 0;
    env->ipr[IPR_VIRBND] = -1ULL;

    return env;
}

void gen_pc_load(CPUState *env, TranslationBlock *tb,
                unsigned long searched_pc, int pc_pos, void *puc)
{
    env->pc = gen_opc_pc[pc_pos];
}
