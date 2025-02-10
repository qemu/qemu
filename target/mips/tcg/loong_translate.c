/*
 * MIPS Loongson 64-bit translation routines
 *
 *  Copyright (c) 2004-2005 Jocelyn Mayer
 *  Copyright (c) 2006 Marius Groeger (FPU operations)
 *  Copyright (c) 2006 Thiemo Seufer (MIPS32R2 support)
 *  Copyright (c) 2011 Richard Henderson <rth@twiddle.net>
 *  Copyright (c) 2021 Philippe Mathieu-Daudé
 *
 * This code is licensed under the GNU GPLv2 and later.
 */

#include "qemu/osdep.h"
#include "translate.h"

/* Include the auto-generated decoder.  */
#include "decode-godson2.c.inc"
#include "decode-loong-ext.c.inc"

/*
 * Word or double-word Fixed-point instructions.
 * ---------------------------------------------
 *
 * Fixed-point multiplies and divisions write only
 * one result into general-purpose registers.
 */

static bool gen_lext_DIV_G(DisasContext *s, int rd, int rs, int rt,
                           bool is_double)
{
    TCGv t0, t1;
    TCGLabel *l1, *l2, *l3;

    if (rd == 0) {
        /* Treat as NOP. */
        return true;
    }

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    l1 = gen_new_label();
    l2 = gen_new_label();
    l3 = gen_new_label();

    gen_load_gpr(t0, rs);
    gen_load_gpr(t1, rt);

    if (!is_double) {
        tcg_gen_ext32s_tl(t0, t0);
        tcg_gen_ext32s_tl(t1, t1);
    }
    tcg_gen_brcondi_tl(TCG_COND_NE, t1, 0, l1);
    tcg_gen_movi_tl(cpu_gpr[rd], 0);
    tcg_gen_br(l3);
    gen_set_label(l1);

    tcg_gen_brcondi_tl(TCG_COND_NE, t0, is_double ? LLONG_MIN : INT_MIN, l2);
    tcg_gen_brcondi_tl(TCG_COND_NE, t1, -1LL, l2);
    tcg_gen_mov_tl(cpu_gpr[rd], t0);

    tcg_gen_br(l3);
    gen_set_label(l2);
    tcg_gen_div_tl(cpu_gpr[rd], t0, t1);
    if (!is_double) {
        tcg_gen_ext32s_tl(cpu_gpr[rd], cpu_gpr[rd]);
    }
    gen_set_label(l3);

    return true;
}

static bool trans_DIV_G(DisasContext *s, arg_muldiv *a)
{
    return gen_lext_DIV_G(s, a->rd, a->rs, a->rt, false);
}

static bool trans_DDIV_G(DisasContext *s, arg_muldiv *a)
{
    return gen_lext_DIV_G(s, a->rd, a->rs, a->rt, true);
}

static bool gen_lext_DIVU_G(DisasContext *s, int rd, int rs, int rt,
                            bool is_double)
{
    TCGv t0, t1;
    TCGLabel *l1, *l2;

    if (rd == 0) {
        /* Treat as NOP. */
        return true;
    }

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    l1 = gen_new_label();
    l2 = gen_new_label();

    gen_load_gpr(t0, rs);
    gen_load_gpr(t1, rt);

    if (!is_double) {
        tcg_gen_ext32u_tl(t0, t0);
        tcg_gen_ext32u_tl(t1, t1);
    }
    tcg_gen_brcondi_tl(TCG_COND_NE, t1, 0, l1);
    tcg_gen_movi_tl(cpu_gpr[rd], 0);

    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_divu_tl(cpu_gpr[rd], t0, t1);
    if (!is_double) {
        tcg_gen_ext32s_tl(cpu_gpr[rd], cpu_gpr[rd]);
    }
    gen_set_label(l2);

    return true;
}

static bool trans_DIVU_G(DisasContext *s, arg_muldiv *a)
{
    return gen_lext_DIVU_G(s, a->rd, a->rs, a->rt, false);
}

static bool trans_DDIVU_G(DisasContext *s, arg_muldiv *a)
{
    return gen_lext_DIVU_G(s, a->rd, a->rs, a->rt, true);
}

static bool gen_lext_MOD_G(DisasContext *s, int rd, int rs, int rt,
                           bool is_double)
{
    TCGv t0, t1;
    TCGLabel *l1, *l2, *l3;

    if (rd == 0) {
        /* Treat as NOP. */
        return true;
    }

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    l1 = gen_new_label();
    l2 = gen_new_label();
    l3 = gen_new_label();

    gen_load_gpr(t0, rs);
    gen_load_gpr(t1, rt);

    if (!is_double) {
        tcg_gen_ext32u_tl(t0, t0);
        tcg_gen_ext32u_tl(t1, t1);
    }
    tcg_gen_brcondi_tl(TCG_COND_EQ, t1, 0, l1);
    tcg_gen_brcondi_tl(TCG_COND_NE, t0, is_double ? LLONG_MIN : INT_MIN, l2);
    tcg_gen_brcondi_tl(TCG_COND_NE, t1, -1LL, l2);
    gen_set_label(l1);
    tcg_gen_movi_tl(cpu_gpr[rd], 0);
    tcg_gen_br(l3);
    gen_set_label(l2);
    tcg_gen_rem_tl(cpu_gpr[rd], t0, t1);
    if (!is_double) {
        tcg_gen_ext32s_tl(cpu_gpr[rd], cpu_gpr[rd]);
    }
    gen_set_label(l3);

    return true;
}

static bool trans_MOD_G(DisasContext *s, arg_muldiv *a)
{
    return gen_lext_MOD_G(s, a->rd, a->rs, a->rt, false);
}

static bool trans_DMOD_G(DisasContext *s, arg_muldiv *a)
{
    return gen_lext_MOD_G(s, a->rd, a->rs, a->rt, true);
}

static bool gen_lext_MODU_G(DisasContext *s, int rd, int rs, int rt,
                            bool is_double)
{
    TCGv t0, t1;
    TCGLabel *l1, *l2;

    if (rd == 0) {
        /* Treat as NOP. */
        return true;
    }

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    l1 = gen_new_label();
    l2 = gen_new_label();

    gen_load_gpr(t0, rs);
    gen_load_gpr(t1, rt);

    if (!is_double) {
        tcg_gen_ext32u_tl(t0, t0);
        tcg_gen_ext32u_tl(t1, t1);
    }
    tcg_gen_brcondi_tl(TCG_COND_NE, t1, 0, l1);
    tcg_gen_movi_tl(cpu_gpr[rd], 0);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_remu_tl(cpu_gpr[rd], t0, t1);
    if (!is_double) {
        tcg_gen_ext32s_tl(cpu_gpr[rd], cpu_gpr[rd]);
    }
    gen_set_label(l2);

    return true;
}

static bool trans_MODU_G(DisasContext *s, arg_muldiv *a)
{
    return gen_lext_MODU_G(s, a->rd, a->rs, a->rt, false);
}

static bool trans_DMODU_G(DisasContext *s, arg_muldiv *a)
{
    return gen_lext_MODU_G(s, a->rd, a->rs, a->rt, true);
}

static bool gen_lext_MULT_G(DisasContext *s, int rd, int rs, int rt,
                            bool is_double)
{
    TCGv t0, t1;

    if (rd == 0) {
        /* Treat as NOP. */
        return true;
    }

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();

    gen_load_gpr(t0, rs);
    gen_load_gpr(t1, rt);

    tcg_gen_mul_tl(cpu_gpr[rd], t0, t1);
    if (!is_double) {
        tcg_gen_ext32s_tl(cpu_gpr[rd], cpu_gpr[rd]);
    }

    return true;
}

static bool trans_MULTu_G(DisasContext *s, arg_muldiv *a)
{
    return gen_lext_MULT_G(s, a->rd, a->rs, a->rt, false);
}

static bool trans_DMULTu_G(DisasContext *s, arg_muldiv *a)
{
    return gen_lext_MULT_G(s, a->rd, a->rs, a->rt, true);
}

bool decode_ext_loongson(DisasContext *ctx, uint32_t insn)
{
    if (!decode_64bit_enabled(ctx)) {
        return false;
    }
    if ((ctx->insn_flags & INSN_LOONGSON2E) && decode_godson2(ctx, ctx->opcode)) {
        return true;
    }
    if ((ctx->insn_flags & ASE_LEXT) && decode_loong_ext(ctx, ctx->opcode)) {
        return true;
    }
    return false;
}
