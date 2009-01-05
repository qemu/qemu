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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "cpu.h"
#include "exec-all.h"
#include "disas.h"
#include "host-utils.h"
#include "tcg-op.h"
#include "qemu-common.h"

#include "helper.h"
#define GEN_HELPER 1
#include "helper.h"

/* #define DO_SINGLE_STEP */
#define ALPHA_DEBUG_DISAS
/* #define DO_TB_FLUSH */

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
static TCGv_ptr cpu_env;
static TCGv cpu_ir[31];
static TCGv cpu_fir[31];
static TCGv cpu_pc;
static TCGv cpu_lock;

/* register names */
static char cpu_reg_names[10*4+21*5 + 10*5+21*6];

#include "gen-icount.h"

static void alpha_translate_init(void)
{
    int i;
    char *p;
    static int done_init = 0;

    if (done_init)
        return;

    cpu_env = tcg_global_reg_new_ptr(TCG_AREG0, "env");

    p = cpu_reg_names;
    for (i = 0; i < 31; i++) {
        sprintf(p, "ir%d", i);
        cpu_ir[i] = tcg_global_mem_new_i64(TCG_AREG0,
                                           offsetof(CPUState, ir[i]), p);
        p += (i < 10) ? 4 : 5;

        sprintf(p, "fir%d", i);
        cpu_fir[i] = tcg_global_mem_new_i64(TCG_AREG0,
                                            offsetof(CPUState, fir[i]), p);
        p += (i < 10) ? 5 : 6;
    }

    cpu_pc = tcg_global_mem_new_i64(TCG_AREG0,
                                    offsetof(CPUState, pc), "pc");

    cpu_lock = tcg_global_mem_new_i64(TCG_AREG0,
                                      offsetof(CPUState, lock), "lock");

    /* register helpers */
#define GEN_HELPER 2
#include "helper.h"

    done_init = 1;
}

static always_inline void gen_excp (DisasContext *ctx,
                                    int exception, int error_code)
{
    TCGv_i32 tmp1, tmp2;

    tcg_gen_movi_i64(cpu_pc, ctx->pc);
    tmp1 = tcg_const_i32(exception);
    tmp2 = tcg_const_i32(error_code);
    gen_helper_excp(tmp1, tmp2);
    tcg_temp_free_i32(tmp2);
    tcg_temp_free_i32(tmp1);
}

static always_inline void gen_invalid (DisasContext *ctx)
{
    gen_excp(ctx, EXCP_OPCDEC, 0);
}

static always_inline void gen_qemu_ldf (TCGv t0, TCGv t1, int flags)
{
    TCGv tmp = tcg_temp_new();
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    tcg_gen_qemu_ld32u(tmp, t1, flags);
    tcg_gen_trunc_i64_i32(tmp32, tmp);
    gen_helper_memory_to_f(t0, tmp32);
    tcg_temp_free_i32(tmp32);
    tcg_temp_free(tmp);
}

static always_inline void gen_qemu_ldg (TCGv t0, TCGv t1, int flags)
{
    TCGv tmp = tcg_temp_new();
    tcg_gen_qemu_ld64(tmp, t1, flags);
    gen_helper_memory_to_g(t0, tmp);
    tcg_temp_free(tmp);
}

static always_inline void gen_qemu_lds (TCGv t0, TCGv t1, int flags)
{
    TCGv tmp = tcg_temp_new();
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    tcg_gen_qemu_ld32u(tmp, t1, flags);
    tcg_gen_trunc_i64_i32(tmp32, tmp);
    gen_helper_memory_to_s(t0, tmp32);
    tcg_temp_free_i32(tmp32);
    tcg_temp_free(tmp);
}

static always_inline void gen_qemu_ldl_l (TCGv t0, TCGv t1, int flags)
{
    tcg_gen_mov_i64(cpu_lock, t1);
    tcg_gen_qemu_ld32s(t0, t1, flags);
}

static always_inline void gen_qemu_ldq_l (TCGv t0, TCGv t1, int flags)
{
    tcg_gen_mov_i64(cpu_lock, t1);
    tcg_gen_qemu_ld64(t0, t1, flags);
}

static always_inline void gen_load_mem (DisasContext *ctx,
                                        void (*tcg_gen_qemu_load)(TCGv t0, TCGv t1, int flags),
                                        int ra, int rb, int32_t disp16,
                                        int fp, int clear)
{
    TCGv addr;

    if (unlikely(ra == 31))
        return;

    addr = tcg_temp_new();
    if (rb != 31) {
        tcg_gen_addi_i64(addr, cpu_ir[rb], disp16);
        if (clear)
            tcg_gen_andi_i64(addr, addr, ~0x7);
    } else {
        if (clear)
            disp16 &= ~0x7;
        tcg_gen_movi_i64(addr, disp16);
    }
    if (fp)
        tcg_gen_qemu_load(cpu_fir[ra], addr, ctx->mem_idx);
    else
        tcg_gen_qemu_load(cpu_ir[ra], addr, ctx->mem_idx);
    tcg_temp_free(addr);
}

static always_inline void gen_qemu_stf (TCGv t0, TCGv t1, int flags)
{
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    TCGv tmp = tcg_temp_new();
    gen_helper_f_to_memory(tmp32, t0);
    tcg_gen_extu_i32_i64(tmp, tmp32);
    tcg_gen_qemu_st32(tmp, t1, flags);
    tcg_temp_free(tmp);
    tcg_temp_free_i32(tmp32);
}

static always_inline void gen_qemu_stg (TCGv t0, TCGv t1, int flags)
{
    TCGv tmp = tcg_temp_new();
    gen_helper_g_to_memory(tmp, t0);
    tcg_gen_qemu_st64(tmp, t1, flags);
    tcg_temp_free(tmp);
}

static always_inline void gen_qemu_sts (TCGv t0, TCGv t1, int flags)
{
    TCGv_i32 tmp32 = tcg_temp_new_i32();
    TCGv tmp = tcg_temp_new();
    gen_helper_s_to_memory(tmp32, t0);
    tcg_gen_extu_i32_i64(tmp, tmp32);
    tcg_gen_qemu_st32(tmp, t1, flags);
    tcg_temp_free(tmp);
    tcg_temp_free_i32(tmp32);
}

static always_inline void gen_qemu_stl_c (TCGv t0, TCGv t1, int flags)
{
    int l1, l2;

    l1 = gen_new_label();
    l2 = gen_new_label();
    tcg_gen_brcond_i64(TCG_COND_NE, cpu_lock, t1, l1);
    tcg_gen_qemu_st32(t0, t1, flags);
    tcg_gen_movi_i64(t0, 1);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_movi_i64(t0, 0);
    gen_set_label(l2);
    tcg_gen_movi_i64(cpu_lock, -1);
}

static always_inline void gen_qemu_stq_c (TCGv t0, TCGv t1, int flags)
{
    int l1, l2;

    l1 = gen_new_label();
    l2 = gen_new_label();
    tcg_gen_brcond_i64(TCG_COND_NE, cpu_lock, t1, l1);
    tcg_gen_qemu_st64(t0, t1, flags);
    tcg_gen_movi_i64(t0, 1);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_movi_i64(t0, 0);
    gen_set_label(l2);
    tcg_gen_movi_i64(cpu_lock, -1);
}

static always_inline void gen_store_mem (DisasContext *ctx,
                                         void (*tcg_gen_qemu_store)(TCGv t0, TCGv t1, int flags),
                                         int ra, int rb, int32_t disp16,
                                         int fp, int clear, int local)
{
    TCGv addr;
    if (local)
        addr = tcg_temp_local_new();
    else
        addr = tcg_temp_new();
    if (rb != 31) {
        tcg_gen_addi_i64(addr, cpu_ir[rb], disp16);
        if (clear)
            tcg_gen_andi_i64(addr, addr, ~0x7);
    } else {
        if (clear)
            disp16 &= ~0x7;
        tcg_gen_movi_i64(addr, disp16);
    }
    if (ra != 31) {
        if (fp)
            tcg_gen_qemu_store(cpu_fir[ra], addr, ctx->mem_idx);
        else
            tcg_gen_qemu_store(cpu_ir[ra], addr, ctx->mem_idx);
    } else {
        TCGv zero;
        if (local)
            zero = tcg_const_local_i64(0);
        else
            zero = tcg_const_i64(0);
        tcg_gen_qemu_store(zero, addr, ctx->mem_idx);
        tcg_temp_free(zero);
    }
    tcg_temp_free(addr);
}

static always_inline void gen_bcond (DisasContext *ctx,
                                     TCGCond cond,
                                     int ra, int32_t disp16, int mask)
{
    int l1, l2;

    l1 = gen_new_label();
    l2 = gen_new_label();
    if (likely(ra != 31)) {
        if (mask) {
            TCGv tmp = tcg_temp_new();
            tcg_gen_andi_i64(tmp, cpu_ir[ra], 1);
            tcg_gen_brcondi_i64(cond, tmp, 0, l1);
            tcg_temp_free(tmp);
        } else
            tcg_gen_brcondi_i64(cond, cpu_ir[ra], 0, l1);
    } else {
        /* Very uncommon case - Do not bother to optimize.  */
        TCGv tmp = tcg_const_i64(0);
        tcg_gen_brcondi_i64(cond, tmp, 0, l1);
        tcg_temp_free(tmp);
    }
    tcg_gen_movi_i64(cpu_pc, ctx->pc);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_movi_i64(cpu_pc, ctx->pc + (int64_t)(disp16 << 2));
    gen_set_label(l2);
}

static always_inline void gen_fbcond (DisasContext *ctx, int opc,
                                      int ra, int32_t disp16)
{
    int l1, l2;
    TCGv tmp;
    TCGv src;

    l1 = gen_new_label();
    l2 = gen_new_label();
    if (ra != 31) {
        tmp = tcg_temp_new();
        src = cpu_fir[ra];
    } else  {
        tmp = tcg_const_i64(0);
        src = tmp;
    }
    switch (opc) {
    case 0x31: /* FBEQ */
        gen_helper_cmpfeq(tmp, src);
        break;
    case 0x32: /* FBLT */
        gen_helper_cmpflt(tmp, src);
        break;
    case 0x33: /* FBLE */
        gen_helper_cmpfle(tmp, src);
        break;
    case 0x35: /* FBNE */
        gen_helper_cmpfne(tmp, src);
        break;
    case 0x36: /* FBGE */
        gen_helper_cmpfge(tmp, src);
        break;
    case 0x37: /* FBGT */
        gen_helper_cmpfgt(tmp, src);
        break;
    default:
        abort();
    }
    tcg_gen_brcondi_i64(TCG_COND_NE, tmp, 0, l1);
    tcg_gen_movi_i64(cpu_pc, ctx->pc);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_movi_i64(cpu_pc, ctx->pc + (int64_t)(disp16 << 2));
    gen_set_label(l2);
}

static always_inline void gen_cmov (TCGCond inv_cond,
                                    int ra, int rb, int rc,
                                    int islit, uint8_t lit, int mask)
{
    int l1;

    if (unlikely(rc == 31))
        return;

    l1 = gen_new_label();

    if (ra != 31) {
        if (mask) {
            TCGv tmp = tcg_temp_new();
            tcg_gen_andi_i64(tmp, cpu_ir[ra], 1);
            tcg_gen_brcondi_i64(inv_cond, tmp, 0, l1);
            tcg_temp_free(tmp);
        } else
            tcg_gen_brcondi_i64(inv_cond, cpu_ir[ra], 0, l1);
    } else {
        /* Very uncommon case - Do not bother to optimize.  */
        TCGv tmp = tcg_const_i64(0);
        tcg_gen_brcondi_i64(inv_cond, tmp, 0, l1);
        tcg_temp_free(tmp);
    }

    if (islit)
        tcg_gen_movi_i64(cpu_ir[rc], lit);
    else
        tcg_gen_mov_i64(cpu_ir[rc], cpu_ir[rb]);
    gen_set_label(l1);
}

#define FARITH2(name)                                       \
static always_inline void glue(gen_f, name)(int rb, int rc) \
{                                                           \
    if (unlikely(rc == 31))                                 \
      return;                                               \
                                                            \
    if (rb != 31)                                           \
        gen_helper_ ## name (cpu_fir[rc], cpu_fir[rb]);    \
    else {                                                  \
        TCGv tmp = tcg_const_i64(0);                        \
        gen_helper_ ## name (cpu_fir[rc], tmp);            \
        tcg_temp_free(tmp);                                 \
    }                                                       \
}
FARITH2(sqrts)
FARITH2(sqrtf)
FARITH2(sqrtg)
FARITH2(sqrtt)
FARITH2(cvtgf)
FARITH2(cvtgq)
FARITH2(cvtqf)
FARITH2(cvtqg)
FARITH2(cvtst)
FARITH2(cvtts)
FARITH2(cvttq)
FARITH2(cvtqs)
FARITH2(cvtqt)
FARITH2(cvtlq)
FARITH2(cvtql)
FARITH2(cvtqlv)
FARITH2(cvtqlsv)

#define FARITH3(name)                                                     \
static always_inline void glue(gen_f, name) (int ra, int rb, int rc)      \
{                                                                         \
    if (unlikely(rc == 31))                                               \
        return;                                                           \
                                                                          \
    if (ra != 31) {                                                       \
        if (rb != 31)                                                     \
            gen_helper_ ## name (cpu_fir[rc], cpu_fir[ra], cpu_fir[rb]);  \
        else {                                                            \
            TCGv tmp = tcg_const_i64(0);                                  \
            gen_helper_ ## name (cpu_fir[rc], cpu_fir[ra], tmp);          \
            tcg_temp_free(tmp);                                           \
        }                                                                 \
    } else {                                                              \
        TCGv tmp = tcg_const_i64(0);                                      \
        if (rb != 31)                                                     \
            gen_helper_ ## name (cpu_fir[rc], tmp, cpu_fir[rb]);          \
        else                                                              \
            gen_helper_ ## name (cpu_fir[rc], tmp, tmp);                   \
        tcg_temp_free(tmp);                                               \
    }                                                                     \
}

FARITH3(addf)
FARITH3(subf)
FARITH3(mulf)
FARITH3(divf)
FARITH3(addg)
FARITH3(subg)
FARITH3(mulg)
FARITH3(divg)
FARITH3(cmpgeq)
FARITH3(cmpglt)
FARITH3(cmpgle)
FARITH3(adds)
FARITH3(subs)
FARITH3(muls)
FARITH3(divs)
FARITH3(addt)
FARITH3(subt)
FARITH3(mult)
FARITH3(divt)
FARITH3(cmptun)
FARITH3(cmpteq)
FARITH3(cmptlt)
FARITH3(cmptle)
FARITH3(cpys)
FARITH3(cpysn)
FARITH3(cpyse)

#define FCMOV(name)                                                   \
static always_inline void glue(gen_f, name) (int ra, int rb, int rc)  \
{                                                                     \
    int l1;                                                           \
    TCGv tmp;                                                         \
                                                                      \
    if (unlikely(rc == 31))                                           \
        return;                                                       \
                                                                      \
    l1 = gen_new_label();                                             \
    tmp = tcg_temp_new();                                 \
    if (ra != 31) {                                                   \
        tmp = tcg_temp_new();                             \
        gen_helper_ ## name (tmp, cpu_fir[ra]);                       \
    } else  {                                                         \
        tmp = tcg_const_i64(0);                                       \
        gen_helper_ ## name (tmp, tmp);                               \
    }                                                                 \
    tcg_gen_brcondi_i64(TCG_COND_EQ, tmp, 0, l1);                     \
    if (rb != 31)                                                     \
        tcg_gen_mov_i64(cpu_fir[rc], cpu_fir[ra]);                    \
    else                                                              \
        tcg_gen_movi_i64(cpu_fir[rc], 0);                             \
    gen_set_label(l1);                                                \
}
FCMOV(cmpfeq)
FCMOV(cmpfne)
FCMOV(cmpflt)
FCMOV(cmpfge)
FCMOV(cmpfle)
FCMOV(cmpfgt)

/* EXTWH, EXTWH, EXTLH, EXTQH */
static always_inline void gen_ext_h(void (*tcg_gen_ext_i64)(TCGv t0, TCGv t1),
                                    int ra, int rb, int rc,
                                    int islit, uint8_t lit)
{
    if (unlikely(rc == 31))
        return;

    if (ra != 31) {
        if (islit) {
            if (lit != 0)
                tcg_gen_shli_i64(cpu_ir[rc], cpu_ir[ra], 64 - ((lit & 7) * 8));
            else
                tcg_gen_mov_i64(cpu_ir[rc], cpu_ir[ra]);
        } else {
            TCGv tmp1, tmp2;
            tmp1 = tcg_temp_new();
            tcg_gen_andi_i64(tmp1, cpu_ir[rb], 7);
            tcg_gen_shli_i64(tmp1, tmp1, 3);
            tmp2 = tcg_const_i64(64);
            tcg_gen_sub_i64(tmp1, tmp2, tmp1);
            tcg_temp_free(tmp2);
            tcg_gen_shl_i64(cpu_ir[rc], cpu_ir[ra], tmp1);
            tcg_temp_free(tmp1);
        }
        if (tcg_gen_ext_i64)
            tcg_gen_ext_i64(cpu_ir[rc], cpu_ir[rc]);
    } else
        tcg_gen_movi_i64(cpu_ir[rc], 0);
}

/* EXTBL, EXTWL, EXTWL, EXTLL, EXTQL */
static always_inline void gen_ext_l(void (*tcg_gen_ext_i64)(TCGv t0, TCGv t1),
                                    int ra, int rb, int rc,
                                    int islit, uint8_t lit)
{
    if (unlikely(rc == 31))
        return;

    if (ra != 31) {
        if (islit) {
                tcg_gen_shri_i64(cpu_ir[rc], cpu_ir[ra], (lit & 7) * 8);
        } else {
            TCGv tmp = tcg_temp_new();
            tcg_gen_andi_i64(tmp, cpu_ir[rb], 7);
            tcg_gen_shli_i64(tmp, tmp, 3);
            tcg_gen_shr_i64(cpu_ir[rc], cpu_ir[ra], tmp);
            tcg_temp_free(tmp);
        }
        if (tcg_gen_ext_i64)
            tcg_gen_ext_i64(cpu_ir[rc], cpu_ir[rc]);
    } else
        tcg_gen_movi_i64(cpu_ir[rc], 0);
}

/* Code to call arith3 helpers */
#define ARITH3(name)                                                  \
static always_inline void glue(gen_, name) (int ra, int rb, int rc,   \
                                            int islit, uint8_t lit)   \
{                                                                     \
    if (unlikely(rc == 31))                                           \
        return;                                                       \
                                                                      \
    if (ra != 31) {                                                   \
        if (islit) {                                                  \
            TCGv tmp = tcg_const_i64(lit);                            \
            gen_helper_ ## name(cpu_ir[rc], cpu_ir[ra], tmp);         \
            tcg_temp_free(tmp);                                       \
        } else                                                        \
            gen_helper_ ## name (cpu_ir[rc], cpu_ir[ra], cpu_ir[rb]); \
    } else {                                                          \
        TCGv tmp1 = tcg_const_i64(0);                                 \
        if (islit) {                                                  \
            TCGv tmp2 = tcg_const_i64(lit);                           \
            gen_helper_ ## name (cpu_ir[rc], tmp1, tmp2);             \
            tcg_temp_free(tmp2);                                      \
        } else                                                        \
            gen_helper_ ## name (cpu_ir[rc], tmp1, cpu_ir[rb]);       \
        tcg_temp_free(tmp1);                                          \
    }                                                                 \
}
ARITH3(cmpbge)
ARITH3(addlv)
ARITH3(sublv)
ARITH3(addqv)
ARITH3(subqv)
ARITH3(mskbl)
ARITH3(insbl)
ARITH3(mskwl)
ARITH3(inswl)
ARITH3(mskll)
ARITH3(insll)
ARITH3(zap)
ARITH3(zapnot)
ARITH3(mskql)
ARITH3(insql)
ARITH3(mskwh)
ARITH3(inswh)
ARITH3(msklh)
ARITH3(inslh)
ARITH3(mskqh)
ARITH3(insqh)
ARITH3(umulh)
ARITH3(mullv)
ARITH3(mulqv)

static always_inline void gen_cmp(TCGCond cond,
                                  int ra, int rb, int rc,
                                  int islit, uint8_t lit)
{
    int l1, l2;
    TCGv tmp;

    if (unlikely(rc == 31))
    return;

    l1 = gen_new_label();
    l2 = gen_new_label();

    if (ra != 31) {
        tmp = tcg_temp_new();
        tcg_gen_mov_i64(tmp, cpu_ir[ra]);
    } else
        tmp = tcg_const_i64(0);
    if (islit)
        tcg_gen_brcondi_i64(cond, tmp, lit, l1);
    else
        tcg_gen_brcond_i64(cond, tmp, cpu_ir[rb], l1);

    tcg_gen_movi_i64(cpu_ir[rc], 0);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_movi_i64(cpu_ir[rc], 1);
    gen_set_label(l2);
}

static always_inline int translate_one (DisasContext *ctx, uint32_t insn)
{
    uint32_t palcode;
    int32_t disp21, disp16, disp12;
    uint16_t fn11, fn16;
    uint8_t opc, ra, rb, rc, sbz, fpfn, fn7, fn2, islit;
    uint8_t lit;
    int ret;

    /* Decode all instruction fields */
    opc = insn >> 26;
    ra = (insn >> 21) & 0x1F;
    rb = (insn >> 16) & 0x1F;
    rc = insn & 0x1F;
    sbz = (insn >> 13) & 0x07;
    islit = (insn >> 12) & 1;
    if (rb == 31 && !islit) {
        islit = 1;
        lit = 0;
    } else
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
        if (likely(ra != 31)) {
            if (rb != 31)
                tcg_gen_addi_i64(cpu_ir[ra], cpu_ir[rb], disp16);
            else
                tcg_gen_movi_i64(cpu_ir[ra], disp16);
        }
        break;
    case 0x09:
        /* LDAH */
        if (likely(ra != 31)) {
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
        gen_load_mem(ctx, &tcg_gen_qemu_ld8u, ra, rb, disp16, 0, 0);
        break;
    case 0x0B:
        /* LDQ_U */
        gen_load_mem(ctx, &tcg_gen_qemu_ld64, ra, rb, disp16, 0, 1);
        break;
    case 0x0C:
        /* LDWU */
        if (!(ctx->amask & AMASK_BWX))
            goto invalid_opc;
        gen_load_mem(ctx, &tcg_gen_qemu_ld16u, ra, rb, disp16, 0, 1);
        break;
    case 0x0D:
        /* STW */
        gen_store_mem(ctx, &tcg_gen_qemu_st16, ra, rb, disp16, 0, 0, 0);
        break;
    case 0x0E:
        /* STB */
        gen_store_mem(ctx, &tcg_gen_qemu_st8, ra, rb, disp16, 0, 0, 0);
        break;
    case 0x0F:
        /* STQ_U */
        gen_store_mem(ctx, &tcg_gen_qemu_st64, ra, rb, disp16, 0, 1, 0);
        break;
    case 0x10:
        switch (fn7) {
        case 0x00:
            /* ADDL */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    if (islit) {
                        tcg_gen_addi_i64(cpu_ir[rc], cpu_ir[ra], lit);
                        tcg_gen_ext32s_i64(cpu_ir[rc], cpu_ir[rc]);
                    } else {
                        tcg_gen_add_i64(cpu_ir[rc], cpu_ir[ra], cpu_ir[rb]);
                        tcg_gen_ext32s_i64(cpu_ir[rc], cpu_ir[rc]);
                    }
                } else {
                    if (islit)
                        tcg_gen_movi_i64(cpu_ir[rc], lit);
                    else
                        tcg_gen_ext32s_i64(cpu_ir[rc], cpu_ir[rb]);
                }
            }
            break;
        case 0x02:
            /* S4ADDL */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    TCGv tmp = tcg_temp_new();
                    tcg_gen_shli_i64(tmp, cpu_ir[ra], 2);
                    if (islit)
                        tcg_gen_addi_i64(tmp, tmp, lit);
                    else
                        tcg_gen_add_i64(tmp, tmp, cpu_ir[rb]);
                    tcg_gen_ext32s_i64(cpu_ir[rc], tmp);
                    tcg_temp_free(tmp);
                } else {
                    if (islit)
                        tcg_gen_movi_i64(cpu_ir[rc], lit);
                    else
                        tcg_gen_ext32s_i64(cpu_ir[rc], cpu_ir[rb]);
                }
            }
            break;
        case 0x09:
            /* SUBL */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    if (islit)
                        tcg_gen_subi_i64(cpu_ir[rc], cpu_ir[ra], lit);
                    else
                        tcg_gen_sub_i64(cpu_ir[rc], cpu_ir[ra], cpu_ir[rb]);
                    tcg_gen_ext32s_i64(cpu_ir[rc], cpu_ir[rc]);
                } else {
                    if (islit)
                        tcg_gen_movi_i64(cpu_ir[rc], -lit);
                    else {
                        tcg_gen_neg_i64(cpu_ir[rc], cpu_ir[rb]);
                        tcg_gen_ext32s_i64(cpu_ir[rc], cpu_ir[rc]);
                }
            }
            break;
        case 0x0B:
            /* S4SUBL */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    TCGv tmp = tcg_temp_new();
                    tcg_gen_shli_i64(tmp, cpu_ir[ra], 2);
                    if (islit)
                        tcg_gen_subi_i64(tmp, tmp, lit);
                    else
                        tcg_gen_sub_i64(tmp, tmp, cpu_ir[rb]);
                    tcg_gen_ext32s_i64(cpu_ir[rc], tmp);
                    tcg_temp_free(tmp);
                } else {
                    if (islit)
                        tcg_gen_movi_i64(cpu_ir[rc], -lit);
                    else {
                        tcg_gen_neg_i64(cpu_ir[rc], cpu_ir[rb]);
                        tcg_gen_ext32s_i64(cpu_ir[rc], cpu_ir[rc]);
                    }
                }
            }
            break;
        case 0x0F:
            /* CMPBGE */
            gen_cmpbge(ra, rb, rc, islit, lit);
            break;
        case 0x12:
            /* S8ADDL */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    TCGv tmp = tcg_temp_new();
                    tcg_gen_shli_i64(tmp, cpu_ir[ra], 3);
                    if (islit)
                        tcg_gen_addi_i64(tmp, tmp, lit);
                    else
                        tcg_gen_add_i64(tmp, tmp, cpu_ir[rb]);
                    tcg_gen_ext32s_i64(cpu_ir[rc], tmp);
                    tcg_temp_free(tmp);
                } else {
                    if (islit)
                        tcg_gen_movi_i64(cpu_ir[rc], lit);
                    else
                        tcg_gen_ext32s_i64(cpu_ir[rc], cpu_ir[rb]);
                }
            }
            break;
        case 0x1B:
            /* S8SUBL */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    TCGv tmp = tcg_temp_new();
                    tcg_gen_shli_i64(tmp, cpu_ir[ra], 3);
                    if (islit)
                        tcg_gen_subi_i64(tmp, tmp, lit);
                    else
                       tcg_gen_sub_i64(tmp, tmp, cpu_ir[rb]);
                    tcg_gen_ext32s_i64(cpu_ir[rc], tmp);
                    tcg_temp_free(tmp);
                } else {
                    if (islit)
                        tcg_gen_movi_i64(cpu_ir[rc], -lit);
                    else
                        tcg_gen_neg_i64(cpu_ir[rc], cpu_ir[rb]);
                        tcg_gen_ext32s_i64(cpu_ir[rc], cpu_ir[rc]);
                    }
                }
            }
            break;
        case 0x1D:
            /* CMPULT */
            gen_cmp(TCG_COND_LTU, ra, rb, rc, islit, lit);
            break;
        case 0x20:
            /* ADDQ */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    if (islit)
                        tcg_gen_addi_i64(cpu_ir[rc], cpu_ir[ra], lit);
                    else
                        tcg_gen_add_i64(cpu_ir[rc], cpu_ir[ra], cpu_ir[rb]);
                } else {
                    if (islit)
                        tcg_gen_movi_i64(cpu_ir[rc], lit);
                    else
                        tcg_gen_mov_i64(cpu_ir[rc], cpu_ir[rb]);
                }
            }
            break;
        case 0x22:
            /* S4ADDQ */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    TCGv tmp = tcg_temp_new();
                    tcg_gen_shli_i64(tmp, cpu_ir[ra], 2);
                    if (islit)
                        tcg_gen_addi_i64(cpu_ir[rc], tmp, lit);
                    else
                        tcg_gen_add_i64(cpu_ir[rc], tmp, cpu_ir[rb]);
                    tcg_temp_free(tmp);
                } else {
                    if (islit)
                        tcg_gen_movi_i64(cpu_ir[rc], lit);
                    else
                        tcg_gen_mov_i64(cpu_ir[rc], cpu_ir[rb]);
                }
            }
            break;
        case 0x29:
            /* SUBQ */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    if (islit)
                        tcg_gen_subi_i64(cpu_ir[rc], cpu_ir[ra], lit);
                    else
                        tcg_gen_sub_i64(cpu_ir[rc], cpu_ir[ra], cpu_ir[rb]);
                } else {
                    if (islit)
                        tcg_gen_movi_i64(cpu_ir[rc], -lit);
                    else
                        tcg_gen_neg_i64(cpu_ir[rc], cpu_ir[rb]);
                }
            }
            break;
        case 0x2B:
            /* S4SUBQ */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    TCGv tmp = tcg_temp_new();
                    tcg_gen_shli_i64(tmp, cpu_ir[ra], 2);
                    if (islit)
                        tcg_gen_subi_i64(cpu_ir[rc], tmp, lit);
                    else
                        tcg_gen_sub_i64(cpu_ir[rc], tmp, cpu_ir[rb]);
                    tcg_temp_free(tmp);
                } else {
                    if (islit)
                        tcg_gen_movi_i64(cpu_ir[rc], -lit);
                    else
                        tcg_gen_neg_i64(cpu_ir[rc], cpu_ir[rb]);
                }
            }
            break;
        case 0x2D:
            /* CMPEQ */
            gen_cmp(TCG_COND_EQ, ra, rb, rc, islit, lit);
            break;
        case 0x32:
            /* S8ADDQ */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    TCGv tmp = tcg_temp_new();
                    tcg_gen_shli_i64(tmp, cpu_ir[ra], 3);
                    if (islit)
                        tcg_gen_addi_i64(cpu_ir[rc], tmp, lit);
                    else
                        tcg_gen_add_i64(cpu_ir[rc], tmp, cpu_ir[rb]);
                    tcg_temp_free(tmp);
                } else {
                    if (islit)
                        tcg_gen_movi_i64(cpu_ir[rc], lit);
                    else
                        tcg_gen_mov_i64(cpu_ir[rc], cpu_ir[rb]);
                }
            }
            break;
        case 0x3B:
            /* S8SUBQ */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    TCGv tmp = tcg_temp_new();
                    tcg_gen_shli_i64(tmp, cpu_ir[ra], 3);
                    if (islit)
                        tcg_gen_subi_i64(cpu_ir[rc], tmp, lit);
                    else
                        tcg_gen_sub_i64(cpu_ir[rc], tmp, cpu_ir[rb]);
                    tcg_temp_free(tmp);
                } else {
                    if (islit)
                        tcg_gen_movi_i64(cpu_ir[rc], -lit);
                    else
                        tcg_gen_neg_i64(cpu_ir[rc], cpu_ir[rb]);
                }
            }
            break;
        case 0x3D:
            /* CMPULE */
            gen_cmp(TCG_COND_LEU, ra, rb, rc, islit, lit);
            break;
        case 0x40:
            /* ADDL/V */
            gen_addlv(ra, rb, rc, islit, lit);
            break;
        case 0x49:
            /* SUBL/V */
            gen_sublv(ra, rb, rc, islit, lit);
            break;
        case 0x4D:
            /* CMPLT */
            gen_cmp(TCG_COND_LT, ra, rb, rc, islit, lit);
            break;
        case 0x60:
            /* ADDQ/V */
            gen_addqv(ra, rb, rc, islit, lit);
            break;
        case 0x69:
            /* SUBQ/V */
            gen_subqv(ra, rb, rc, islit, lit);
            break;
        case 0x6D:
            /* CMPLE */
            gen_cmp(TCG_COND_LE, ra, rb, rc, islit, lit);
            break;
        default:
            goto invalid_opc;
        }
        break;
    case 0x11:
        switch (fn7) {
        case 0x00:
            /* AND */
            if (likely(rc != 31)) {
                if (ra == 31)
                    tcg_gen_movi_i64(cpu_ir[rc], 0);
                else if (islit)
                    tcg_gen_andi_i64(cpu_ir[rc], cpu_ir[ra], lit);
                else
                    tcg_gen_and_i64(cpu_ir[rc], cpu_ir[ra], cpu_ir[rb]);
            }
            break;
        case 0x08:
            /* BIC */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    if (islit)
                        tcg_gen_andi_i64(cpu_ir[rc], cpu_ir[ra], ~lit);
                    else
                        tcg_gen_andc_i64(cpu_ir[rc], cpu_ir[ra], cpu_ir[rb]);
                } else
                    tcg_gen_movi_i64(cpu_ir[rc], 0);
            }
            break;
        case 0x14:
            /* CMOVLBS */
            gen_cmov(TCG_COND_EQ, ra, rb, rc, islit, lit, 1);
            break;
        case 0x16:
            /* CMOVLBC */
            gen_cmov(TCG_COND_NE, ra, rb, rc, islit, lit, 1);
            break;
        case 0x20:
            /* BIS */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    if (islit)
                        tcg_gen_ori_i64(cpu_ir[rc], cpu_ir[ra], lit);
                    else
                        tcg_gen_or_i64(cpu_ir[rc], cpu_ir[ra], cpu_ir[rb]);
                } else {
                    if (islit)
                        tcg_gen_movi_i64(cpu_ir[rc], lit);
                    else
                        tcg_gen_mov_i64(cpu_ir[rc], cpu_ir[rb]);
                }
            }
            break;
        case 0x24:
            /* CMOVEQ */
            gen_cmov(TCG_COND_NE, ra, rb, rc, islit, lit, 0);
            break;
        case 0x26:
            /* CMOVNE */
            gen_cmov(TCG_COND_EQ, ra, rb, rc, islit, lit, 0);
            break;
        case 0x28:
            /* ORNOT */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    if (islit)
                        tcg_gen_ori_i64(cpu_ir[rc], cpu_ir[ra], ~lit);
                    else
                        tcg_gen_orc_i64(cpu_ir[rc], cpu_ir[ra], cpu_ir[rb]);
                } else {
                    if (islit)
                        tcg_gen_movi_i64(cpu_ir[rc], ~lit);
                    else
                        tcg_gen_not_i64(cpu_ir[rc], cpu_ir[rb]);
                }
            }
            break;
        case 0x40:
            /* XOR */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    if (islit)
                        tcg_gen_xori_i64(cpu_ir[rc], cpu_ir[ra], lit);
                    else
                        tcg_gen_xor_i64(cpu_ir[rc], cpu_ir[ra], cpu_ir[rb]);
                } else {
                    if (islit)
                        tcg_gen_movi_i64(cpu_ir[rc], lit);
                    else
                        tcg_gen_mov_i64(cpu_ir[rc], cpu_ir[rb]);
                }
            }
            break;
        case 0x44:
            /* CMOVLT */
            gen_cmov(TCG_COND_GE, ra, rb, rc, islit, lit, 0);
            break;
        case 0x46:
            /* CMOVGE */
            gen_cmov(TCG_COND_LT, ra, rb, rc, islit, lit, 0);
            break;
        case 0x48:
            /* EQV */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    if (islit)
                        tcg_gen_xori_i64(cpu_ir[rc], cpu_ir[ra], ~lit);
                    else
                        tcg_gen_eqv_i64(cpu_ir[rc], cpu_ir[ra], cpu_ir[rb]);
                } else {
                    if (islit)
                        tcg_gen_movi_i64(cpu_ir[rc], ~lit);
                    else
                        tcg_gen_not_i64(cpu_ir[rc], cpu_ir[rb]);
                }
            }
            break;
        case 0x61:
            /* AMASK */
            if (likely(rc != 31)) {
                if (islit)
                    tcg_gen_movi_i64(cpu_ir[rc], helper_amask(lit));
                else
                    gen_helper_amask(cpu_ir[rc], cpu_ir[rb]);
            }
            break;
        case 0x64:
            /* CMOVLE */
            gen_cmov(TCG_COND_GT, ra, rb, rc, islit, lit, 0);
            break;
        case 0x66:
            /* CMOVGT */
            gen_cmov(TCG_COND_LE, ra, rb, rc, islit, lit, 0);
            break;
        case 0x6C:
            /* IMPLVER */
            if (rc != 31)
                gen_helper_load_implver(cpu_ir[rc]);
            break;
        default:
            goto invalid_opc;
        }
        break;
    case 0x12:
        switch (fn7) {
        case 0x02:
            /* MSKBL */
            gen_mskbl(ra, rb, rc, islit, lit);
            break;
        case 0x06:
            /* EXTBL */
            gen_ext_l(&tcg_gen_ext8u_i64, ra, rb, rc, islit, lit);
            break;
        case 0x0B:
            /* INSBL */
            gen_insbl(ra, rb, rc, islit, lit);
            break;
        case 0x12:
            /* MSKWL */
            gen_mskwl(ra, rb, rc, islit, lit);
            break;
        case 0x16:
            /* EXTWL */
            gen_ext_l(&tcg_gen_ext16u_i64, ra, rb, rc, islit, lit);
            break;
        case 0x1B:
            /* INSWL */
            gen_inswl(ra, rb, rc, islit, lit);
            break;
        case 0x22:
            /* MSKLL */
            gen_mskll(ra, rb, rc, islit, lit);
            break;
        case 0x26:
            /* EXTLL */
            gen_ext_l(&tcg_gen_ext32u_i64, ra, rb, rc, islit, lit);
            break;
        case 0x2B:
            /* INSLL */
            gen_insll(ra, rb, rc, islit, lit);
            break;
        case 0x30:
            /* ZAP */
            gen_zap(ra, rb, rc, islit, lit);
            break;
        case 0x31:
            /* ZAPNOT */
            gen_zapnot(ra, rb, rc, islit, lit);
            break;
        case 0x32:
            /* MSKQL */
            gen_mskql(ra, rb, rc, islit, lit);
            break;
        case 0x34:
            /* SRL */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    if (islit)
                        tcg_gen_shri_i64(cpu_ir[rc], cpu_ir[ra], lit & 0x3f);
                    else {
                        TCGv shift = tcg_temp_new();
                        tcg_gen_andi_i64(shift, cpu_ir[rb], 0x3f);
                        tcg_gen_shr_i64(cpu_ir[rc], cpu_ir[ra], shift);
                        tcg_temp_free(shift);
                    }
                } else
                    tcg_gen_movi_i64(cpu_ir[rc], 0);
            }
            break;
        case 0x36:
            /* EXTQL */
            gen_ext_l(NULL, ra, rb, rc, islit, lit);
            break;
        case 0x39:
            /* SLL */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    if (islit)
                        tcg_gen_shli_i64(cpu_ir[rc], cpu_ir[ra], lit & 0x3f);
                    else {
                        TCGv shift = tcg_temp_new();
                        tcg_gen_andi_i64(shift, cpu_ir[rb], 0x3f);
                        tcg_gen_shl_i64(cpu_ir[rc], cpu_ir[ra], shift);
                        tcg_temp_free(shift);
                    }
                } else
                    tcg_gen_movi_i64(cpu_ir[rc], 0);
            }
            break;
        case 0x3B:
            /* INSQL */
            gen_insql(ra, rb, rc, islit, lit);
            break;
        case 0x3C:
            /* SRA */
            if (likely(rc != 31)) {
                if (ra != 31) {
                    if (islit)
                        tcg_gen_sari_i64(cpu_ir[rc], cpu_ir[ra], lit & 0x3f);
                    else {
                        TCGv shift = tcg_temp_new();
                        tcg_gen_andi_i64(shift, cpu_ir[rb], 0x3f);
                        tcg_gen_sar_i64(cpu_ir[rc], cpu_ir[ra], shift);
                        tcg_temp_free(shift);
                    }
                } else
                    tcg_gen_movi_i64(cpu_ir[rc], 0);
            }
            break;
        case 0x52:
            /* MSKWH */
            gen_mskwh(ra, rb, rc, islit, lit);
            break;
        case 0x57:
            /* INSWH */
            gen_inswh(ra, rb, rc, islit, lit);
            break;
        case 0x5A:
            /* EXTWH */
            gen_ext_h(&tcg_gen_ext16u_i64, ra, rb, rc, islit, lit);
            break;
        case 0x62:
            /* MSKLH */
            gen_msklh(ra, rb, rc, islit, lit);
            break;
        case 0x67:
            /* INSLH */
            gen_inslh(ra, rb, rc, islit, lit);
            break;
        case 0x6A:
            /* EXTLH */
            gen_ext_h(&tcg_gen_ext16u_i64, ra, rb, rc, islit, lit);
            break;
        case 0x72:
            /* MSKQH */
            gen_mskqh(ra, rb, rc, islit, lit);
            break;
        case 0x77:
            /* INSQH */
            gen_insqh(ra, rb, rc, islit, lit);
            break;
        case 0x7A:
            /* EXTQH */
            gen_ext_h(NULL, ra, rb, rc, islit, lit);
            break;
        default:
            goto invalid_opc;
        }
        break;
    case 0x13:
        switch (fn7) {
        case 0x00:
            /* MULL */
            if (likely(rc != 31)) {
                if (ra == 31)
                    tcg_gen_movi_i64(cpu_ir[rc], 0);
                else {
                    if (islit)
                        tcg_gen_muli_i64(cpu_ir[rc], cpu_ir[ra], lit);
                    else
                        tcg_gen_mul_i64(cpu_ir[rc], cpu_ir[ra], cpu_ir[rb]);
                    tcg_gen_ext32s_i64(cpu_ir[rc], cpu_ir[rc]);
                }
            }
            break;
        case 0x20:
            /* MULQ */
            if (likely(rc != 31)) {
                if (ra == 31)
                    tcg_gen_movi_i64(cpu_ir[rc], 0);
                else if (islit)
                    tcg_gen_muli_i64(cpu_ir[rc], cpu_ir[ra], lit);
                else
                    tcg_gen_mul_i64(cpu_ir[rc], cpu_ir[ra], cpu_ir[rb]);
            }
            break;
        case 0x30:
            /* UMULH */
            gen_umulh(ra, rb, rc, islit, lit);
            break;
        case 0x40:
            /* MULL/V */
            gen_mullv(ra, rb, rc, islit, lit);
            break;
        case 0x60:
            /* MULQ/V */
            gen_mulqv(ra, rb, rc, islit, lit);
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
            if (likely(rc != 31)) {
                if (ra != 31) {
                    TCGv_i32 tmp = tcg_temp_new_i32();
                    tcg_gen_trunc_i64_i32(tmp, cpu_ir[ra]);
                    gen_helper_memory_to_s(cpu_fir[rc], tmp);
                    tcg_temp_free_i32(tmp);
                } else
                    tcg_gen_movi_i64(cpu_fir[rc], 0);
            }
            break;
        case 0x0A:
            /* SQRTF */
            if (!(ctx->amask & AMASK_FIX))
                goto invalid_opc;
            gen_fsqrtf(rb, rc);
            break;
        case 0x0B:
            /* SQRTS */
            if (!(ctx->amask & AMASK_FIX))
                goto invalid_opc;
            gen_fsqrts(rb, rc);
            break;
        case 0x14:
            /* ITOFF */
            if (!(ctx->amask & AMASK_FIX))
                goto invalid_opc;
            if (likely(rc != 31)) {
                if (ra != 31) {
                    TCGv_i32 tmp = tcg_temp_new_i32();
                    tcg_gen_trunc_i64_i32(tmp, cpu_ir[ra]);
                    gen_helper_memory_to_f(cpu_fir[rc], tmp);
                    tcg_temp_free_i32(tmp);
                } else
                    tcg_gen_movi_i64(cpu_fir[rc], 0);
            }
            break;
        case 0x24:
            /* ITOFT */
            if (!(ctx->amask & AMASK_FIX))
                goto invalid_opc;
            if (likely(rc != 31)) {
                if (ra != 31)
                    tcg_gen_mov_i64(cpu_fir[rc], cpu_ir[ra]);
                else
                    tcg_gen_movi_i64(cpu_fir[rc], 0);
            }
            break;
        case 0x2A:
            /* SQRTG */
            if (!(ctx->amask & AMASK_FIX))
                goto invalid_opc;
            gen_fsqrtg(rb, rc);
            break;
        case 0x02B:
            /* SQRTT */
            if (!(ctx->amask & AMASK_FIX))
                goto invalid_opc;
            gen_fsqrtt(rb, rc);
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
            gen_faddf(ra, rb, rc);
            break;
        case 0x01:
            /* SUBF */
            gen_fsubf(ra, rb, rc);
            break;
        case 0x02:
            /* MULF */
            gen_fmulf(ra, rb, rc);
            break;
        case 0x03:
            /* DIVF */
            gen_fdivf(ra, rb, rc);
            break;
        case 0x1E:
            /* CVTDG */
#if 0 // TODO
            gen_fcvtdg(rb, rc);
#else
            goto invalid_opc;
#endif
            break;
        case 0x20:
            /* ADDG */
            gen_faddg(ra, rb, rc);
            break;
        case 0x21:
            /* SUBG */
            gen_fsubg(ra, rb, rc);
            break;
        case 0x22:
            /* MULG */
            gen_fmulg(ra, rb, rc);
            break;
        case 0x23:
            /* DIVG */
            gen_fdivg(ra, rb, rc);
            break;
        case 0x25:
            /* CMPGEQ */
            gen_fcmpgeq(ra, rb, rc);
            break;
        case 0x26:
            /* CMPGLT */
            gen_fcmpglt(ra, rb, rc);
            break;
        case 0x27:
            /* CMPGLE */
            gen_fcmpgle(ra, rb, rc);
            break;
        case 0x2C:
            /* CVTGF */
            gen_fcvtgf(rb, rc);
            break;
        case 0x2D:
            /* CVTGD */
#if 0 // TODO
            gen_fcvtgd(rb, rc);
#else
            goto invalid_opc;
#endif
            break;
        case 0x2F:
            /* CVTGQ */
            gen_fcvtgq(rb, rc);
            break;
        case 0x3C:
            /* CVTQF */
            gen_fcvtqf(rb, rc);
            break;
        case 0x3E:
            /* CVTQG */
            gen_fcvtqg(rb, rc);
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
            gen_fadds(ra, rb, rc);
            break;
        case 0x01:
            /* SUBS */
            gen_fsubs(ra, rb, rc);
            break;
        case 0x02:
            /* MULS */
            gen_fmuls(ra, rb, rc);
            break;
        case 0x03:
            /* DIVS */
            gen_fdivs(ra, rb, rc);
            break;
        case 0x20:
            /* ADDT */
            gen_faddt(ra, rb, rc);
            break;
        case 0x21:
            /* SUBT */
            gen_fsubt(ra, rb, rc);
            break;
        case 0x22:
            /* MULT */
            gen_fmult(ra, rb, rc);
            break;
        case 0x23:
            /* DIVT */
            gen_fdivt(ra, rb, rc);
            break;
        case 0x24:
            /* CMPTUN */
            gen_fcmptun(ra, rb, rc);
            break;
        case 0x25:
            /* CMPTEQ */
            gen_fcmpteq(ra, rb, rc);
            break;
        case 0x26:
            /* CMPTLT */
            gen_fcmptlt(ra, rb, rc);
            break;
        case 0x27:
            /* CMPTLE */
            gen_fcmptle(ra, rb, rc);
            break;
        case 0x2C:
            /* XXX: incorrect */
            if (fn11 == 0x2AC || fn11 == 0x6AC) {
                /* CVTST */
                gen_fcvtst(rb, rc);
            } else {
                /* CVTTS */
                gen_fcvtts(rb, rc);
            }
            break;
        case 0x2F:
            /* CVTTQ */
            gen_fcvttq(rb, rc);
            break;
        case 0x3C:
            /* CVTQS */
            gen_fcvtqs(rb, rc);
            break;
        case 0x3E:
            /* CVTQT */
            gen_fcvtqt(rb, rc);
            break;
        default:
            goto invalid_opc;
        }
        break;
    case 0x17:
        switch (fn11) {
        case 0x010:
            /* CVTLQ */
            gen_fcvtlq(rb, rc);
            break;
        case 0x020:
            if (likely(rc != 31)) {
                if (ra == rb)
                    /* FMOV */
                    tcg_gen_mov_i64(cpu_fir[rc], cpu_fir[ra]);
                else
                    /* CPYS */
                    gen_fcpys(ra, rb, rc);
            }
            break;
        case 0x021:
            /* CPYSN */
            gen_fcpysn(ra, rb, rc);
            break;
        case 0x022:
            /* CPYSE */
            gen_fcpyse(ra, rb, rc);
            break;
        case 0x024:
            /* MT_FPCR */
            if (likely(ra != 31))
                gen_helper_store_fpcr(cpu_fir[ra]);
            else {
                TCGv tmp = tcg_const_i64(0);
                gen_helper_store_fpcr(tmp);
                tcg_temp_free(tmp);
            }
            break;
        case 0x025:
            /* MF_FPCR */
            if (likely(ra != 31))
                gen_helper_load_fpcr(cpu_fir[ra]);
            break;
        case 0x02A:
            /* FCMOVEQ */
            gen_fcmpfeq(ra, rb, rc);
            break;
        case 0x02B:
            /* FCMOVNE */
            gen_fcmpfne(ra, rb, rc);
            break;
        case 0x02C:
            /* FCMOVLT */
            gen_fcmpflt(ra, rb, rc);
            break;
        case 0x02D:
            /* FCMOVGE */
            gen_fcmpfge(ra, rb, rc);
            break;
        case 0x02E:
            /* FCMOVLE */
            gen_fcmpfle(ra, rb, rc);
            break;
        case 0x02F:
            /* FCMOVGT */
            gen_fcmpfgt(ra, rb, rc);
            break;
        case 0x030:
            /* CVTQL */
            gen_fcvtql(rb, rc);
            break;
        case 0x130:
            /* CVTQL/V */
            gen_fcvtqlv(rb, rc);
            break;
        case 0x530:
            /* CVTQL/SV */
            gen_fcvtqlsv(rb, rc);
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
            if (ra != 31)
                gen_helper_load_pcc(cpu_ir[ra]);
            break;
        case 0xE000:
            /* RC */
            if (ra != 31)
                gen_helper_rc(cpu_ir[ra]);
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
            if (ra != 31)
                gen_helper_rs(cpu_ir[ra]);
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
        if (ra != 31) {
            TCGv tmp = tcg_const_i32(insn & 0xFF);
            gen_helper_mfpr(cpu_ir[ra], tmp, cpu_ir[ra]);
            tcg_temp_free(tmp);
        }
        break;
#endif
    case 0x1A:
        if (rb != 31)
            tcg_gen_andi_i64(cpu_pc, cpu_ir[rb], ~3);
        else
            tcg_gen_movi_i64(cpu_pc, 0);
        if (ra != 31)
            tcg_gen_movi_i64(cpu_ir[ra], ctx->pc);
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
        if (ra != 31) {
            TCGv addr = tcg_temp_new();
            if (rb != 31)
                tcg_gen_addi_i64(addr, cpu_ir[rb], disp12);
            else
                tcg_gen_movi_i64(addr, disp12);
            switch ((insn >> 12) & 0xF) {
            case 0x0:
                /* Longword physical access */
                gen_helper_ldl_raw(cpu_ir[ra], addr);
                break;
            case 0x1:
                /* Quadword physical access */
                gen_helper_ldq_raw(cpu_ir[ra], addr);
                break;
            case 0x2:
                /* Longword physical access with lock */
                gen_helper_ldl_l_raw(cpu_ir[ra], addr);
                break;
            case 0x3:
                /* Quadword physical access with lock */
                gen_helper_ldq_l_raw(cpu_ir[ra], addr);
                break;
            case 0x4:
                /* Longword virtual PTE fetch */
                gen_helper_ldl_kernel(cpu_ir[ra], addr);
                break;
            case 0x5:
                /* Quadword virtual PTE fetch */
                gen_helper_ldq_kernel(cpu_ir[ra], addr);
                break;
            case 0x6:
                /* Incpu_ir[ra]id */
                goto incpu_ir[ra]id_opc;
            case 0x7:
                /* Incpu_ir[ra]id */
                goto incpu_ir[ra]id_opc;
            case 0x8:
                /* Longword virtual access */
                gen_helper_st_virt_to_phys(addr, addr);
                gen_helper_ldl_raw(cpu_ir[ra], addr);
                break;
            case 0x9:
                /* Quadword virtual access */
                gen_helper_st_virt_to_phys(addr, addr);
                gen_helper_ldq_raw(cpu_ir[ra], addr);
                break;
            case 0xA:
                /* Longword virtual access with protection check */
                tcg_gen_qemu_ld32s(cpu_ir[ra], addr, ctx->flags);
                break;
            case 0xB:
                /* Quadword virtual access with protection check */
                tcg_gen_qemu_ld64(cpu_ir[ra], addr, ctx->flags);
                break;
            case 0xC:
                /* Longword virtual access with altenate access mode */
                gen_helper_set_alt_mode();
                gen_helper_st_virt_to_phys(addr, addr);
                gen_helper_ldl_raw(cpu_ir[ra], addr);
                gen_helper_restore_mode();
                break;
            case 0xD:
                /* Quadword virtual access with altenate access mode */
                gen_helper_set_alt_mode();
                gen_helper_st_virt_to_phys(addr, addr);
                gen_helper_ldq_raw(cpu_ir[ra], addr);
                gen_helper_restore_mode();
                break;
            case 0xE:
                /* Longword virtual access with alternate access mode and
                 * protection checks
                 */
                gen_helper_set_alt_mode();
                gen_helper_ldl_data(cpu_ir[ra], addr);
                gen_helper_restore_mode();
                break;
            case 0xF:
                /* Quadword virtual access with alternate access mode and
                 * protection checks
                 */
                gen_helper_set_alt_mode();
                gen_helper_ldq_data(cpu_ir[ra], addr);
                gen_helper_restore_mode();
                break;
            }
            tcg_temp_free(addr);
        }
        break;
#endif
    case 0x1C:
        switch (fn7) {
        case 0x00:
            /* SEXTB */
            if (!(ctx->amask & AMASK_BWX))
                goto invalid_opc;
            if (likely(rc != 31)) {
                if (islit)
                    tcg_gen_movi_i64(cpu_ir[rc], (int64_t)((int8_t)lit));
                else
                    tcg_gen_ext8s_i64(cpu_ir[rc], cpu_ir[rb]);
            }
            break;
        case 0x01:
            /* SEXTW */
            if (!(ctx->amask & AMASK_BWX))
                goto invalid_opc;
            if (likely(rc != 31)) {
                if (islit)
                    tcg_gen_movi_i64(cpu_ir[rc], (int64_t)((int16_t)lit));
                else
                    tcg_gen_ext16s_i64(cpu_ir[rc], cpu_ir[rb]);
            }
            break;
        case 0x30:
            /* CTPOP */
            if (!(ctx->amask & AMASK_CIX))
                goto invalid_opc;
            if (likely(rc != 31)) {
                if (islit)
                    tcg_gen_movi_i64(cpu_ir[rc], ctpop64(lit));
                else
                    gen_helper_ctpop(cpu_ir[rc], cpu_ir[rb]);
            }
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
            if (likely(rc != 31)) {
                if (islit)
                    tcg_gen_movi_i64(cpu_ir[rc], clz64(lit));
                else
                    gen_helper_ctlz(cpu_ir[rc], cpu_ir[rb]);
            }
            break;
        case 0x33:
            /* CTTZ */
            if (!(ctx->amask & AMASK_CIX))
                goto invalid_opc;
            if (likely(rc != 31)) {
                if (islit)
                    tcg_gen_movi_i64(cpu_ir[rc], ctz64(lit));
                else
                    gen_helper_cttz(cpu_ir[rc], cpu_ir[rb]);
            }
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
            if (likely(rc != 31)) {
                if (ra != 31)
                    tcg_gen_mov_i64(cpu_ir[rc], cpu_fir[ra]);
                else
                    tcg_gen_movi_i64(cpu_ir[rc], 0);
            }
            break;
        case 0x78:
            /* FTOIS */
            if (!(ctx->amask & AMASK_FIX))
                goto invalid_opc;
            if (rc != 31) {
                TCGv_i32 tmp1 = tcg_temp_new_i32();
                if (ra != 31)
                    gen_helper_s_to_memory(tmp1, cpu_fir[ra]);
                else {
                    TCGv tmp2 = tcg_const_i64(0);
                    gen_helper_s_to_memory(tmp1, tmp2);
                    tcg_temp_free(tmp2);
                }
                tcg_gen_ext_i32_i64(cpu_ir[rc], tmp1);
                tcg_temp_free_i32(tmp1);
            }
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
        else {
            TCGv tmp1 = tcg_const_i32(insn & 0xFF);
            if (ra != 31)
                gen_helper_mtpr(tmp1, cpu_ir[ra]);
            else {
                TCGv tmp2 = tcg_const_i64(0);
                gen_helper_mtpr(tmp1, tmp2);
                tcg_temp_free(tmp2);
            }
            tcg_temp_free(tmp1);
            ret = 2;
        }
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
            gen_helper_hw_rei();
        } else {
            TCGv tmp;

            if (ra != 31) {
                tmp = tcg_temp_new();
                tcg_gen_addi_i64(tmp, cpu_ir[rb], (((int64_t)insn << 51) >> 51));
            } else
                tmp = tcg_const_i64(((int64_t)insn << 51) >> 51);
            gen_helper_hw_ret(tmp);
            tcg_temp_free(tmp);
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
        else {
            TCGv addr, val;
            addr = tcg_temp_new();
            if (rb != 31)
                tcg_gen_addi_i64(addr, cpu_ir[rb], disp12);
            else
                tcg_gen_movi_i64(addr, disp12);
            if (ra != 31)
                val = cpu_ir[ra];
            else {
                val = tcg_temp_new();
                tcg_gen_movi_i64(val, 0);
            }
            switch ((insn >> 12) & 0xF) {
            case 0x0:
                /* Longword physical access */
                gen_helper_stl_raw(val, addr);
                break;
            case 0x1:
                /* Quadword physical access */
                gen_helper_stq_raw(val, addr);
                break;
            case 0x2:
                /* Longword physical access with lock */
                gen_helper_stl_c_raw(val, val, addr);
                break;
            case 0x3:
                /* Quadword physical access with lock */
                gen_helper_stq_c_raw(val, val, addr);
                break;
            case 0x4:
                /* Longword virtual access */
                gen_helper_st_virt_to_phys(addr, addr);
                gen_helper_stl_raw(val, addr);
                break;
            case 0x5:
                /* Quadword virtual access */
                gen_helper_st_virt_to_phys(addr, addr);
                gen_helper_stq_raw(val, addr);
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
                gen_helper_set_alt_mode();
                gen_helper_st_virt_to_phys(addr, addr);
                gen_helper_stl_raw(val, addr);
                gen_helper_restore_mode();
                break;
            case 0xD:
                /* Quadword virtual access with alternate access mode */
                gen_helper_set_alt_mode();
                gen_helper_st_virt_to_phys(addr, addr);
                gen_helper_stl_raw(val, addr);
                gen_helper_restore_mode();
                break;
            case 0xE:
                /* Invalid */
                goto invalid_opc;
            case 0xF:
                /* Invalid */
                goto invalid_opc;
            }
            if (ra != 31)
                tcg_temp_free(val);
            tcg_temp_free(addr);
        }
        ret = 2;
        break;
#endif
    case 0x20:
        /* LDF */
        gen_load_mem(ctx, &gen_qemu_ldf, ra, rb, disp16, 1, 0);
        break;
    case 0x21:
        /* LDG */
        gen_load_mem(ctx, &gen_qemu_ldg, ra, rb, disp16, 1, 0);
        break;
    case 0x22:
        /* LDS */
        gen_load_mem(ctx, &gen_qemu_lds, ra, rb, disp16, 1, 0);
        break;
    case 0x23:
        /* LDT */
        gen_load_mem(ctx, &tcg_gen_qemu_ld64, ra, rb, disp16, 1, 0);
        break;
    case 0x24:
        /* STF */
        gen_store_mem(ctx, &gen_qemu_stf, ra, rb, disp16, 1, 0, 0);
        break;
    case 0x25:
        /* STG */
        gen_store_mem(ctx, &gen_qemu_stg, ra, rb, disp16, 1, 0, 0);
        break;
    case 0x26:
        /* STS */
        gen_store_mem(ctx, &gen_qemu_sts, ra, rb, disp16, 1, 0, 0);
        break;
    case 0x27:
        /* STT */
        gen_store_mem(ctx, &tcg_gen_qemu_st64, ra, rb, disp16, 1, 0, 0);
        break;
    case 0x28:
        /* LDL */
        gen_load_mem(ctx, &tcg_gen_qemu_ld32s, ra, rb, disp16, 0, 0);
        break;
    case 0x29:
        /* LDQ */
        gen_load_mem(ctx, &tcg_gen_qemu_ld64, ra, rb, disp16, 0, 0);
        break;
    case 0x2A:
        /* LDL_L */
        gen_load_mem(ctx, &gen_qemu_ldl_l, ra, rb, disp16, 0, 0);
        break;
    case 0x2B:
        /* LDQ_L */
        gen_load_mem(ctx, &gen_qemu_ldq_l, ra, rb, disp16, 0, 0);
        break;
    case 0x2C:
        /* STL */
        gen_store_mem(ctx, &tcg_gen_qemu_st32, ra, rb, disp16, 0, 0, 0);
        break;
    case 0x2D:
        /* STQ */
        gen_store_mem(ctx, &tcg_gen_qemu_st64, ra, rb, disp16, 0, 0, 0);
        break;
    case 0x2E:
        /* STL_C */
        gen_store_mem(ctx, &gen_qemu_stl_c, ra, rb, disp16, 0, 0, 1);
        break;
    case 0x2F:
        /* STQ_C */
        gen_store_mem(ctx, &gen_qemu_stq_c, ra, rb, disp16, 0, 0, 1);
        break;
    case 0x30:
        /* BR */
        if (ra != 31)
            tcg_gen_movi_i64(cpu_ir[ra], ctx->pc);
        tcg_gen_movi_i64(cpu_pc, ctx->pc + (int64_t)(disp21 << 2));
        ret = 1;
        break;
    case 0x31: /* FBEQ */
    case 0x32: /* FBLT */
    case 0x33: /* FBLE */
        gen_fbcond(ctx, opc, ra, disp16);
        ret = 1;
        break;
    case 0x34:
        /* BSR */
        if (ra != 31)
            tcg_gen_movi_i64(cpu_ir[ra], ctx->pc);
        tcg_gen_movi_i64(cpu_pc, ctx->pc + (int64_t)(disp21 << 2));
        ret = 1;
        break;
    case 0x35: /* FBNE */
    case 0x36: /* FBGE */
    case 0x37: /* FBGT */
        gen_fbcond(ctx, opc, ra, disp16);
        ret = 1;
        break;
    case 0x38:
        /* BLBC */
        gen_bcond(ctx, TCG_COND_EQ, ra, disp16, 1);
        ret = 1;
        break;
    case 0x39:
        /* BEQ */
        gen_bcond(ctx, TCG_COND_EQ, ra, disp16, 0);
        ret = 1;
        break;
    case 0x3A:
        /* BLT */
        gen_bcond(ctx, TCG_COND_LT, ra, disp16, 0);
        ret = 1;
        break;
    case 0x3B:
        /* BLE */
        gen_bcond(ctx, TCG_COND_LE, ra, disp16, 0);
        ret = 1;
        break;
    case 0x3C:
        /* BLBS */
        gen_bcond(ctx, TCG_COND_NE, ra, disp16, 1);
        ret = 1;
        break;
    case 0x3D:
        /* BNE */
        gen_bcond(ctx, TCG_COND_NE, ra, disp16, 0);
        ret = 1;
        break;
    case 0x3E:
        /* BGE */
        gen_bcond(ctx, TCG_COND_GE, ra, disp16, 0);
        ret = 1;
        break;
    case 0x3F:
        /* BGT */
        gen_bcond(ctx, TCG_COND_GT, ra, disp16, 0);
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
    CPUBreakpoint *bp;
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
        if (unlikely(!TAILQ_EMPTY(&env->breakpoints))) {
            TAILQ_FOREACH(bp, &env->breakpoints, entry) {
                if (bp->pc == ctx.pc) {
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
            num_insns >= max_insns) {
            break;
        }

        if (env->singlestep_enabled) {
            gen_excp(&ctx, EXCP_DEBUG, 0);
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
    gen_helper_tb_flush();
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
