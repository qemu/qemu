/*
 * Renesas RX Disassembler
 *
 * Copyright (c) 2019 Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "disas/dis-asm.h"
#include "qemu/bitops.h"
#include "cpu.h"

typedef struct DisasContext {
    disassemble_info *dis;
    uint32_t addr;
    uint32_t pc;
    uint8_t len;
    uint8_t bytes[8];
} DisasContext;


static uint32_t decode_load_bytes(DisasContext *ctx, uint32_t insn,
                                  int i, int n)
{
    uint32_t addr = ctx->addr;

    g_assert(ctx->len == i);
    g_assert(n <= ARRAY_SIZE(ctx->bytes));

    while (++i <= n) {
        ctx->dis->read_memory_func(addr++, &ctx->bytes[i - 1], 1, ctx->dis);
        insn |= ctx->bytes[i - 1] << (32 - i * 8);
    }
    ctx->addr = addr;
    ctx->len = n;

    return insn;
}

static int32_t li(DisasContext *ctx, int sz)
{
    uint32_t addr = ctx->addr;
    uintptr_t len = ctx->len;

    switch (sz) {
    case 1:
        g_assert(len + 1 <= ARRAY_SIZE(ctx->bytes));
        ctx->addr += 1;
        ctx->len += 1;
        ctx->dis->read_memory_func(addr, ctx->bytes + len, 1, ctx->dis);
        return (int8_t)ctx->bytes[len];
    case 2:
        g_assert(len + 2 <= ARRAY_SIZE(ctx->bytes));
        ctx->addr += 2;
        ctx->len += 2;
        ctx->dis->read_memory_func(addr, ctx->bytes + len, 2, ctx->dis);
        return ldsw_le_p(ctx->bytes + len);
    case 3:
        g_assert(len + 3 <= ARRAY_SIZE(ctx->bytes));
        ctx->addr += 3;
        ctx->len += 3;
        ctx->dis->read_memory_func(addr, ctx->bytes + len, 3, ctx->dis);
        return (int8_t)ctx->bytes[len + 2] << 16 | lduw_le_p(ctx->bytes + len);
    case 0:
        g_assert(len + 4 <= ARRAY_SIZE(ctx->bytes));
        ctx->addr += 4;
        ctx->len += 4;
        ctx->dis->read_memory_func(addr, ctx->bytes + len, 4, ctx->dis);
        return ldl_le_p(ctx->bytes + len);
    default:
        g_assert_not_reached();
    }
}

static int bdsp_s(DisasContext *ctx, int d)
{
    /*
     * 0 -> 8
     * 1 -> 9
     * 2 -> 10
     * 3 -> 3
     * :
     * 7 -> 7
     */
    if (d < 3) {
        d += 8;
    }
    return d;
}

/* Include the auto-generated decoder.  */
#include "decode-insns.c.inc"

static void dump_bytes(DisasContext *ctx)
{
    int i, len = ctx->len;

    for (i = 0; i < len; ++i) {
        ctx->dis->fprintf_func(ctx->dis->stream, "%02x ", ctx->bytes[i]);
    }
    ctx->dis->fprintf_func(ctx->dis->stream, "%*c", (8 - i) * 3, '\t');
}

#define prt(...) \
    do {                                                        \
        dump_bytes(ctx);                                        \
        ctx->dis->fprintf_func(ctx->dis->stream, __VA_ARGS__);  \
    } while (0)

#define RX_MEMORY_BYTE 0
#define RX_MEMORY_WORD 1
#define RX_MEMORY_LONG 2

#define RX_IM_BYTE 0
#define RX_IM_WORD 1
#define RX_IM_LONG 2
#define RX_IM_UWORD 3

static const char size[] = {'b', 'w', 'l'};
static const char cond[][4] = {
    "eq", "ne", "c", "nc", "gtu", "leu", "pz", "n",
    "ge", "lt", "gt", "le", "o", "no", "ra", "f"
};
static const char psw[] = {
    'c', 'z', 's', 'o', 0, 0, 0, 0,
    'i', 'u', 0, 0, 0, 0, 0, 0,
};

static void rx_index_addr(DisasContext *ctx, char out[8], int ld, int mi)
{
    uint32_t addr = ctx->addr;
    uintptr_t len = ctx->len;
    uint16_t dsp;

    switch (ld) {
    case 0:
        /* No index; return empty string.  */
        out[0] = '\0';
        return;
    case 1:
        g_assert(len + 1 <= ARRAY_SIZE(ctx->bytes));
        ctx->addr += 1;
        ctx->len += 1;
        ctx->dis->read_memory_func(addr, ctx->bytes + len, 1, ctx->dis);
        dsp = ctx->bytes[len];
        break;
    case 2:
        g_assert(len + 2 <= ARRAY_SIZE(ctx->bytes));
        ctx->addr += 2;
        ctx->len += 2;
        ctx->dis->read_memory_func(addr, ctx->bytes + len, 2, ctx->dis);
        dsp = lduw_le_p(ctx->bytes + len);
        break;
    default:
        g_assert_not_reached();
    }

    sprintf(out, "%u", dsp << (mi < 3 ? mi : 4 - mi));
}

static void prt_ldmi(DisasContext *ctx, const char *insn,
                     int ld, int mi, int rs, int rd)
{
    static const char sizes[][4] = {".b", ".w", ".l", ".uw", ".ub"};
    char dsp[8];

    if (ld < 3) {
        rx_index_addr(ctx, dsp, ld, mi);
        prt("%s\t%s[r%d]%s, r%d", insn, dsp, rs, sizes[mi], rd);
    } else {
        prt("%s\tr%d, r%d", insn, rs, rd);
    }
}

static void prt_ir(DisasContext *ctx, const char *insn, int imm, int rd)
{
    if (imm < 0x100) {
        prt("%s\t#%d, r%d", insn, imm, rd);
    } else {
        prt("%s\t#0x%08x, r%d", insn, imm, rd);
    }
}

/* mov.[bwl] rs,dsp:[rd] */
static bool trans_MOV_rm(DisasContext *ctx, arg_MOV_rm *a)
{
    if (a->dsp > 0) {
        prt("mov.%c\tr%d,%d[r%d]",
            size[a->sz], a->rs, a->dsp << a->sz, a->rd);
    } else {
        prt("mov.%c\tr%d,[r%d]",
            size[a->sz], a->rs, a->rd);
    }
    return true;
}

/* mov.[bwl] dsp:[rs],rd */
static bool trans_MOV_mr(DisasContext *ctx, arg_MOV_mr *a)
{
    if (a->dsp > 0) {
        prt("mov.%c\t%d[r%d], r%d",
            size[a->sz], a->dsp << a->sz, a->rs, a->rd);
    } else {
        prt("mov.%c\t[r%d], r%d",
            size[a->sz], a->rs, a->rd);
    }
    return true;
}

/* mov.l #uimm4,rd */
/* mov.l #uimm8,rd */
/* mov.l #imm,rd */
static bool trans_MOV_ir(DisasContext *ctx, arg_MOV_ir *a)
{
    prt_ir(ctx, "mov.l", a->imm, a->rd);
    return true;
}

/* mov.[bwl] #uimm8,dsp:[rd] */
/* mov #imm, dsp:[rd] */
static bool trans_MOV_im(DisasContext *ctx, arg_MOV_im *a)
{
    if (a->dsp > 0) {
        prt("mov.%c\t#%d,%d[r%d]",
            size[a->sz], a->imm, a->dsp << a->sz, a->rd);
    } else {
        prt("mov.%c\t#%d,[r%d]",
            size[a->sz], a->imm, a->rd);
    }
    return true;
}

/* mov.[bwl] [ri,rb],rd */
static bool trans_MOV_ar(DisasContext *ctx, arg_MOV_ar *a)
{
    prt("mov.%c\t[r%d,r%d], r%d", size[a->sz], a->ri, a->rb, a->rd);
    return true;
}

/* mov.[bwl] rd,[ri,rb] */
static bool trans_MOV_ra(DisasContext *ctx, arg_MOV_ra *a)
{
    prt("mov.%c\tr%d, [r%d, r%d]", size[a->sz], a->rs, a->ri, a->rb);
    return true;
}


/* mov.[bwl] dsp:[rs],dsp:[rd] */
/* mov.[bwl] rs,dsp:[rd] */
/* mov.[bwl] dsp:[rs],rd */
/* mov.[bwl] rs,rd */
static bool trans_MOV_mm(DisasContext *ctx, arg_MOV_mm *a)
{
    char dspd[8], dsps[8], szc = size[a->sz];

    if (a->lds == 3 && a->ldd == 3) {
        /* mov.[bwl] rs,rd */
        prt("mov.%c\tr%d, r%d", szc, a->rs, a->rd);
    } else if (a->lds == 3) {
        rx_index_addr(ctx, dspd, a->ldd, a->sz);
        prt("mov.%c\tr%d, %s[r%d]", szc, a->rs, dspd, a->rd);
    } else if (a->ldd == 3) {
        rx_index_addr(ctx, dsps, a->lds, a->sz);
        prt("mov.%c\t%s[r%d], r%d", szc, dsps, a->rs, a->rd);
    } else {
        rx_index_addr(ctx, dsps, a->lds, a->sz);
        rx_index_addr(ctx, dspd, a->ldd, a->sz);
        prt("mov.%c\t%s[r%d], %s[r%d]", szc, dsps, a->rs, dspd, a->rd);
    }
    return true;
}

/* mov.[bwl] rs,[rd+] */
/* mov.[bwl] rs,[-rd] */
static bool trans_MOV_rp(DisasContext *ctx, arg_MOV_rp *a)
{
    if (a->ad) {
        prt("mov.%c\tr%d, [-r%d]", size[a->sz], a->rs, a->rd);
    } else {
        prt("mov.%c\tr%d, [r%d+]", size[a->sz], a->rs, a->rd);
    }
    return true;
}

/* mov.[bwl] [rd+],rs */
/* mov.[bwl] [-rd],rs */
static bool trans_MOV_pr(DisasContext *ctx, arg_MOV_pr *a)
{
    if (a->ad) {
        prt("mov.%c\t[-r%d], r%d", size[a->sz], a->rd, a->rs);
    } else {
        prt("mov.%c\t[r%d+], r%d", size[a->sz], a->rd, a->rs);
    }
    return true;
}

/* movu.[bw] dsp5:[rs],rd */
static bool trans_MOVU_mr(DisasContext *ctx, arg_MOVU_mr *a)
{
    if (a->dsp > 0) {
        prt("movu.%c\t%d[r%d], r%d", size[a->sz],
            a->dsp << a->sz, a->rs, a->rd);
    } else {
        prt("movu.%c\t[r%d], r%d", size[a->sz], a->rs, a->rd);
    }
    return true;
}

/* movu.[bw] rs,rd */
static bool trans_MOVU_rr(DisasContext *ctx, arg_MOVU_rr *a)
{
    prt("movu.%c\tr%d, r%d", size[a->sz], a->rs, a->rd);
    return true;
}

/* movu.[bw] [ri,rb],rd */
static bool trans_MOVU_ar(DisasContext *ctx, arg_MOVU_ar *a)
{
    prt("mov.%c\t[r%d,r%d], r%d", size[a->sz], a->ri, a->rb, a->rd);
    return true;
}

/* movu.[bw] [rs+],rd */
/* movu.[bw] [-rs],rd */
static bool trans_MOVU_pr(DisasContext *ctx, arg_MOVU_pr *a)
{
    if (a->ad) {
        prt("movu.%c\t[-r%d], r%d", size[a->sz], a->rd, a->rs);
    } else {
        prt("movu.%c\t[r%d+], r%d", size[a->sz], a->rd, a->rs);
    }
    return true;
}

/* pop rd */
static bool trans_POP(DisasContext *ctx, arg_POP *a)
{
    prt("pop\tr%d", a->rd);
    return true;
}

/* popc rx */
static bool trans_POPC(DisasContext *ctx, arg_POPC *a)
{
    prt("pop\tr%s", rx_crname(a->cr));
    return true;
}

/* popm rd-rd2 */
static bool trans_POPM(DisasContext *ctx, arg_POPM *a)
{
    prt("popm\tr%d-r%d", a->rd, a->rd2);
    return true;
}

/* push rs */
static bool trans_PUSH_r(DisasContext *ctx, arg_PUSH_r *a)
{
    prt("push\tr%d", a->rs);
    return true;
}

/* push dsp[rs] */
static bool trans_PUSH_m(DisasContext *ctx, arg_PUSH_m *a)
{
    char dsp[8];

    rx_index_addr(ctx, dsp, a->ld, a->sz);
    prt("push\t%s[r%d]", dsp, a->rs);
    return true;
}

/* pushc rx */
static bool trans_PUSHC(DisasContext *ctx, arg_PUSHC *a)
{
    prt("push\t%s", rx_crname(a->cr));
    return true;
}

/* pushm rs-rs2*/
static bool trans_PUSHM(DisasContext *ctx, arg_PUSHM *a)
{
    prt("pushm\tr%d-r%d", a->rs, a->rs2);
    return true;
}

/* xchg rs,rd */
static bool trans_XCHG_rr(DisasContext *ctx, arg_XCHG_rr *a)
{
    prt("xchg\tr%d, r%d", a->rs, a->rd);
    return true;
}
/* xchg dsp[rs].<mi>,rd */
static bool trans_XCHG_mr(DisasContext *ctx, arg_XCHG_mr *a)
{
    prt_ldmi(ctx, "xchg", a->ld, a->mi, a->rs, a->rd);
    return true;
}

/* stz #imm,rd */
static bool trans_STZ(DisasContext *ctx, arg_STZ *a)
{
    prt_ir(ctx, "stz", a->imm, a->rd);
    return true;
}

/* stnz #imm,rd */
static bool trans_STNZ(DisasContext *ctx, arg_STNZ *a)
{
    prt_ir(ctx, "stnz", a->imm, a->rd);
    return true;
}

/* rtsd #imm */
static bool trans_RTSD_i(DisasContext *ctx, arg_RTSD_i *a)
{
    prt("rtsd\t#%d", a->imm << 2);
    return true;
}

/* rtsd #imm, rd-rd2 */
static bool trans_RTSD_irr(DisasContext *ctx, arg_RTSD_irr *a)
{
    prt("rtsd\t#%d, r%d - r%d", a->imm << 2, a->rd, a->rd2);
    return true;
}

/* and #uimm:4, rd */
/* and #imm, rd */
static bool trans_AND_ir(DisasContext *ctx, arg_AND_ir *a)
{
    prt_ir(ctx, "and", a->imm, a->rd);
    return true;
}

/* and dsp[rs], rd */
/* and rs,rd */
static bool trans_AND_mr(DisasContext *ctx, arg_AND_mr *a)
{
    prt_ldmi(ctx, "and", a->ld, a->mi, a->rs, a->rd);
    return true;
}

/* and rs,rs2,rd */
static bool trans_AND_rrr(DisasContext *ctx, arg_AND_rrr *a)
{
    prt("and\tr%d,r%d, r%d", a->rs, a->rs2, a->rd);
    return true;
}

/* or #uimm:4, rd */
/* or #imm, rd */
static bool trans_OR_ir(DisasContext *ctx, arg_OR_ir *a)
{
    prt_ir(ctx, "or", a->imm, a->rd);
    return true;
}

/* or dsp[rs], rd */
/* or rs,rd */
static bool trans_OR_mr(DisasContext *ctx, arg_OR_mr *a)
{
    prt_ldmi(ctx, "or", a->ld, a->mi, a->rs, a->rd);
    return true;
}

/* or rs,rs2,rd */
static bool trans_OR_rrr(DisasContext *ctx, arg_OR_rrr *a)
{
    prt("or\tr%d, r%d, r%d", a->rs, a->rs2, a->rd);
    return true;
}

/* xor #imm, rd */
static bool trans_XOR_ir(DisasContext *ctx, arg_XOR_ir *a)
{
    prt_ir(ctx, "xor", a->imm, a->rd);
    return true;
}

/* xor dsp[rs], rd */
/* xor rs,rd */
static bool trans_XOR_mr(DisasContext *ctx, arg_XOR_mr *a)
{
    prt_ldmi(ctx, "xor", a->ld, a->mi, a->rs, a->rd);
    return true;
}

/* tst #imm, rd */
static bool trans_TST_ir(DisasContext *ctx, arg_TST_ir *a)
{
    prt_ir(ctx, "tst", a->imm, a->rd);
    return true;
}

/* tst dsp[rs], rd */
/* tst rs, rd */
static bool trans_TST_mr(DisasContext *ctx, arg_TST_mr *a)
{
    prt_ldmi(ctx, "tst", a->ld, a->mi, a->rs, a->rd);
    return true;
}

/* not rd */
/* not rs, rd */
static bool trans_NOT_rr(DisasContext *ctx, arg_NOT_rr *a)
{
    if (a->rs != a->rd) {
        prt("not\tr%d, r%d", a->rs, a->rd);
    } else {
        prt("not\tr%d", a->rs);
    }
    return true;
}

/* neg rd */
/* neg rs, rd */
static bool trans_NEG_rr(DisasContext *ctx, arg_NEG_rr *a)
{
    if (a->rs != a->rd) {
        prt("neg\tr%d, r%d", a->rs, a->rd);
    } else {
        prt("neg\tr%d", a->rs);
    }
    return true;
}

/* adc #imm, rd */
static bool trans_ADC_ir(DisasContext *ctx, arg_ADC_ir *a)
{
    prt_ir(ctx, "adc", a->imm, a->rd);
    return true;
}

/* adc rs, rd */
static bool trans_ADC_rr(DisasContext *ctx, arg_ADC_rr *a)
{
    prt("adc\tr%d, r%d", a->rs, a->rd);
    return true;
}

/* adc dsp[rs], rd */
static bool trans_ADC_mr(DisasContext *ctx, arg_ADC_mr *a)
{
    char dsp[8];

    rx_index_addr(ctx, dsp, a->ld, 2);
    prt("adc\t%s[r%d], r%d", dsp, a->rs, a->rd);
    return true;
}

/* add #uimm4, rd */
/* add #imm, rs, rd */
static bool trans_ADD_irr(DisasContext *ctx, arg_ADD_irr *a)
{
    if (a->imm < 0x10 && a->rs2 == a->rd) {
        prt("add\t#%d, r%d", a->imm, a->rd);
    } else {
        prt("add\t#0x%08x, r%d, r%d", a->imm, a->rs2, a->rd);
    }
    return true;
}

/* add rs, rd */
/* add dsp[rs], rd */
static bool trans_ADD_mr(DisasContext *ctx, arg_ADD_mr *a)
{
    prt_ldmi(ctx, "add", a->ld, a->mi, a->rs, a->rd);
    return true;
}

/* add rs, rs2, rd */
static bool trans_ADD_rrr(DisasContext *ctx, arg_ADD_rrr *a)
{
    prt("add\tr%d, r%d, r%d", a->rs, a->rs2, a->rd);
    return true;
}

/* cmp #imm4, rd */
/* cmp #imm8, rd */
/* cmp #imm, rs2 */
static bool trans_CMP_ir(DisasContext *ctx, arg_CMP_ir *a)
{
    prt_ir(ctx, "cmp", a->imm, a->rs2);
    return true;
}

/* cmp rs, rs2 */
/* cmp dsp[rs], rs2 */
static bool trans_CMP_mr(DisasContext *ctx, arg_CMP_mr *a)
{
    prt_ldmi(ctx, "cmp", a->ld, a->mi, a->rs, a->rd);
    return true;
}

/* sub #imm4, rd */
static bool trans_SUB_ir(DisasContext *ctx, arg_SUB_ir *a)
{
    prt("sub\t#%d, r%d", a->imm, a->rd);
    return true;
}

/* sub rs, rd */
/* sub dsp[rs], rd */
static bool trans_SUB_mr(DisasContext *ctx, arg_SUB_mr *a)
{
    prt_ldmi(ctx, "sub", a->ld, a->mi, a->rs, a->rd);
    return true;
}

/* sub rs, rs2, rd */
static bool trans_SUB_rrr(DisasContext *ctx, arg_SUB_rrr *a)
{
    prt("sub\tr%d, r%d, r%d", a->rs, a->rs2, a->rd);
    return true;
}

/* sbb rs, rd */
static bool trans_SBB_rr(DisasContext *ctx, arg_SBB_rr *a)
{
    prt("sbb\tr%d, r%d", a->rs, a->rd);
    return true;
}

/* sbb dsp[rs], rd */
static bool trans_SBB_mr(DisasContext *ctx, arg_SBB_mr *a)
{
    prt_ldmi(ctx, "sbb", a->ld, RX_IM_LONG, a->rs, a->rd);
    return true;
}

/* abs rd */
/* abs rs, rd */
static bool trans_ABS_rr(DisasContext *ctx, arg_ABS_rr *a)
{
    if (a->rs != a->rd) {
        prt("abs\tr%d, r%d", a->rs, a->rd);
    } else {
        prt("abs\tr%d", a->rs);
    }
    return true;
}

/* max #imm, rd */
static bool trans_MAX_ir(DisasContext *ctx, arg_MAX_ir *a)
{
    prt_ir(ctx, "max", a->imm, a->rd);
    return true;
}

/* max rs, rd */
/* max dsp[rs], rd */
static bool trans_MAX_mr(DisasContext *ctx, arg_MAX_mr *a)
{
    prt_ldmi(ctx, "max", a->ld, a->mi, a->rs, a->rd);
    return true;
}

/* min #imm, rd */
static bool trans_MIN_ir(DisasContext *ctx, arg_MIN_ir *a)
{
    prt_ir(ctx, "min", a->imm, a->rd);
    return true;
}

/* min rs, rd */
/* min dsp[rs], rd */
static bool trans_MIN_mr(DisasContext *ctx, arg_MIN_mr *a)
{
    prt_ldmi(ctx, "min", a->ld, a->mi, a->rs, a->rd);
    return true;
}

/* mul #uimm4, rd */
/* mul #imm, rd */
static bool trans_MUL_ir(DisasContext *ctx, arg_MUL_ir *a)
{
    prt_ir(ctx, "mul", a->imm, a->rd);
    return true;
}

/* mul rs, rd */
/* mul dsp[rs], rd */
static bool trans_MUL_mr(DisasContext *ctx, arg_MUL_mr *a)
{
    prt_ldmi(ctx, "mul", a->ld, a->mi, a->rs, a->rd);
    return true;
}

/* mul rs, rs2, rd */
static bool trans_MUL_rrr(DisasContext *ctx, arg_MUL_rrr *a)
{
    prt("mul\tr%d,r%d,r%d", a->rs, a->rs2, a->rd);
    return true;
}

/* emul #imm, rd */
static bool trans_EMUL_ir(DisasContext *ctx, arg_EMUL_ir *a)
{
    prt_ir(ctx, "emul", a->imm, a->rd);
    return true;
}

/* emul rs, rd */
/* emul dsp[rs], rd */
static bool trans_EMUL_mr(DisasContext *ctx, arg_EMUL_mr *a)
{
    prt_ldmi(ctx, "emul", a->ld, a->mi, a->rs, a->rd);
    return true;
}

/* emulu #imm, rd */
static bool trans_EMULU_ir(DisasContext *ctx, arg_EMULU_ir *a)
{
    prt_ir(ctx, "emulu", a->imm, a->rd);
    return true;
}

/* emulu rs, rd */
/* emulu dsp[rs], rd */
static bool trans_EMULU_mr(DisasContext *ctx, arg_EMULU_mr *a)
{
    prt_ldmi(ctx, "emulu", a->ld, a->mi, a->rs, a->rd);
    return true;
}

/* div #imm, rd */
static bool trans_DIV_ir(DisasContext *ctx, arg_DIV_ir *a)
{
    prt_ir(ctx, "div", a->imm, a->rd);
    return true;
}

/* div rs, rd */
/* div dsp[rs], rd */
static bool trans_DIV_mr(DisasContext *ctx, arg_DIV_mr *a)
{
    prt_ldmi(ctx, "div", a->ld, a->mi, a->rs, a->rd);
    return true;
}

/* divu #imm, rd */
static bool trans_DIVU_ir(DisasContext *ctx, arg_DIVU_ir *a)
{
    prt_ir(ctx, "divu", a->imm, a->rd);
    return true;
}

/* divu rs, rd */
/* divu dsp[rs], rd */
static bool trans_DIVU_mr(DisasContext *ctx, arg_DIVU_mr *a)
{
    prt_ldmi(ctx, "divu", a->ld, a->mi, a->rs, a->rd);
    return true;
}


/* shll #imm:5, rd */
/* shll #imm:5, rs, rd */
static bool trans_SHLL_irr(DisasContext *ctx, arg_SHLL_irr *a)
{
    if (a->rs2 != a->rd) {
        prt("shll\t#%d, r%d, r%d", a->imm, a->rs2, a->rd);
    } else {
        prt("shll\t#%d, r%d", a->imm, a->rd);
    }
    return true;
}

/* shll rs, rd */
static bool trans_SHLL_rr(DisasContext *ctx, arg_SHLL_rr *a)
{
    prt("shll\tr%d, r%d", a->rs, a->rd);
    return true;
}

/* shar #imm:5, rd */
/* shar #imm:5, rs, rd */
static bool trans_SHAR_irr(DisasContext *ctx, arg_SHAR_irr *a)
{
    if (a->rs2 != a->rd) {
        prt("shar\t#%d, r%d, r%d", a->imm, a->rs2, a->rd);
    } else {
        prt("shar\t#%d, r%d", a->imm, a->rd);
    }
    return true;
}

/* shar rs, rd */
static bool trans_SHAR_rr(DisasContext *ctx, arg_SHAR_rr *a)
{
    prt("shar\tr%d, r%d", a->rs, a->rd);
    return true;
}

/* shlr #imm:5, rd */
/* shlr #imm:5, rs, rd */
static bool trans_SHLR_irr(DisasContext *ctx, arg_SHLR_irr *a)
{
    if (a->rs2 != a->rd) {
        prt("shlr\t#%d, r%d, r%d", a->imm, a->rs2, a->rd);
    } else {
        prt("shlr\t#%d, r%d", a->imm, a->rd);
    }
    return true;
}

/* shlr rs, rd */
static bool trans_SHLR_rr(DisasContext *ctx, arg_SHLR_rr *a)
{
    prt("shlr\tr%d, r%d", a->rs, a->rd);
    return true;
}

/* rolc rd */
static bool trans_ROLC(DisasContext *ctx, arg_ROLC *a)
{
    prt("rorc\tr%d", a->rd);
    return true;
}

/* rorc rd */
static bool trans_RORC(DisasContext *ctx, arg_RORC *a)
{
    prt("rorc\tr%d", a->rd);
    return true;
}

/* rotl #imm, rd */
static bool trans_ROTL_ir(DisasContext *ctx, arg_ROTL_ir *a)
{
    prt("rotl\t#%d, r%d", a->imm, a->rd);
    return true;
}

/* rotl rs, rd */
static bool trans_ROTL_rr(DisasContext *ctx, arg_ROTL_rr *a)
{
    prt("rotl\tr%d, r%d", a->rs, a->rd);
    return true;
}

/* rotr #imm, rd */
static bool trans_ROTR_ir(DisasContext *ctx, arg_ROTR_ir *a)
{
    prt("rotr\t#%d, r%d", a->imm, a->rd);
    return true;
}

/* rotr rs, rd */
static bool trans_ROTR_rr(DisasContext *ctx, arg_ROTR_rr *a)
{
    prt("rotr\tr%d, r%d", a->rs, a->rd);
    return true;
}

/* revl rs, rd */
static bool trans_REVL(DisasContext *ctx, arg_REVL *a)
{
    prt("revl\tr%d, r%d", a->rs, a->rd);
    return true;
}

/* revw rs, rd */
static bool trans_REVW(DisasContext *ctx, arg_REVW *a)
{
    prt("revw\tr%d, r%d", a->rs, a->rd);
    return true;
}

/* conditional branch helper */
static void rx_bcnd_main(DisasContext *ctx, int cd, int len, int dst)
{
    static const char sz[] = {'s', 'b', 'w', 'a'};
    prt("b%s.%c\t%08x", cond[cd], sz[len - 1], ctx->pc + dst);
}

/* beq dsp:3 / bne dsp:3 */
/* beq dsp:8 / bne dsp:8 */
/* bc dsp:8 / bnc dsp:8 */
/* bgtu dsp:8 / bleu dsp:8 */
/* bpz dsp:8 / bn dsp:8 */
/* bge dsp:8 / blt dsp:8 */
/* bgt dsp:8 / ble dsp:8 */
/* bo dsp:8 / bno dsp:8 */
/* beq dsp:16 / bne dsp:16 */
static bool trans_BCnd(DisasContext *ctx, arg_BCnd *a)
{
    rx_bcnd_main(ctx, a->cd, a->sz, a->dsp);
    return true;
}

/* bra dsp:3 */
/* bra dsp:8 */
/* bra dsp:16 */
/* bra dsp:24 */
static bool trans_BRA(DisasContext *ctx, arg_BRA *a)
{
    rx_bcnd_main(ctx, 14, a->sz, a->dsp);
    return true;
}

/* bra rs */
static bool trans_BRA_l(DisasContext *ctx, arg_BRA_l *a)
{
    prt("bra.l\tr%d", a->rd);
    return true;
}

/* jmp rs */
static bool trans_JMP(DisasContext *ctx, arg_JMP *a)
{
    prt("jmp\tr%d", a->rs);
    return true;
}

/* jsr rs */
static bool trans_JSR(DisasContext *ctx, arg_JSR *a)
{
    prt("jsr\tr%d", a->rs);
    return true;
}

/* bsr dsp:16 */
/* bsr dsp:24 */
static bool trans_BSR(DisasContext *ctx, arg_BSR *a)
{
    static const char sz[] = {'w', 'a'};
    prt("bsr.%c\t%08x", sz[a->sz - 3], ctx->pc + a->dsp);
    return true;
}

/* bsr rs */
static bool trans_BSR_l(DisasContext *ctx, arg_BSR_l *a)
{
    prt("bsr.l\tr%d", a->rd);
    return true;
}

/* rts */
static bool trans_RTS(DisasContext *ctx, arg_RTS *a)
{
    prt("rts");
    return true;
}

/* nop */
static bool trans_NOP(DisasContext *ctx, arg_NOP *a)
{
    prt("nop");
    return true;
}

/* scmpu */
static bool trans_SCMPU(DisasContext *ctx, arg_SCMPU *a)
{
    prt("scmpu");
    return true;
}

/* smovu */
static bool trans_SMOVU(DisasContext *ctx, arg_SMOVU *a)
{
    prt("smovu");
    return true;
}

/* smovf */
static bool trans_SMOVF(DisasContext *ctx, arg_SMOVF *a)
{
    prt("smovf");
    return true;
}

/* smovb */
static bool trans_SMOVB(DisasContext *ctx, arg_SMOVB *a)
{
    prt("smovb");
    return true;
}

/* suntile */
static bool trans_SUNTIL(DisasContext *ctx, arg_SUNTIL *a)
{
    prt("suntil.%c", size[a->sz]);
    return true;
}

/* swhile */
static bool trans_SWHILE(DisasContext *ctx, arg_SWHILE *a)
{
    prt("swhile.%c", size[a->sz]);
    return true;
}
/* sstr */
static bool trans_SSTR(DisasContext *ctx, arg_SSTR *a)
{
    prt("sstr.%c", size[a->sz]);
    return true;
}

/* rmpa */
static bool trans_RMPA(DisasContext *ctx, arg_RMPA *a)
{
    prt("rmpa.%c", size[a->sz]);
    return true;
}

/* mulhi rs,rs2 */
static bool trans_MULHI(DisasContext *ctx, arg_MULHI *a)
{
    prt("mulhi\tr%d,r%d", a->rs, a->rs2);
    return true;
}

/* mullo rs,rs2 */
static bool trans_MULLO(DisasContext *ctx, arg_MULLO *a)
{
    prt("mullo\tr%d, r%d", a->rs, a->rs2);
    return true;
}

/* machi rs,rs2 */
static bool trans_MACHI(DisasContext *ctx, arg_MACHI *a)
{
    prt("machi\tr%d, r%d", a->rs, a->rs2);
    return true;
}

/* maclo rs,rs2 */
static bool trans_MACLO(DisasContext *ctx, arg_MACLO *a)
{
    prt("maclo\tr%d, r%d", a->rs, a->rs2);
    return true;
}

/* mvfachi rd */
static bool trans_MVFACHI(DisasContext *ctx, arg_MVFACHI *a)
{
    prt("mvfachi\tr%d", a->rd);
    return true;
}

/* mvfacmi rd */
static bool trans_MVFACMI(DisasContext *ctx, arg_MVFACMI *a)
{
    prt("mvfacmi\tr%d", a->rd);
    return true;
}

/* mvtachi rs */
static bool trans_MVTACHI(DisasContext *ctx, arg_MVTACHI *a)
{
    prt("mvtachi\tr%d", a->rs);
    return true;
}

/* mvtaclo rs */
static bool trans_MVTACLO(DisasContext *ctx, arg_MVTACLO *a)
{
    prt("mvtaclo\tr%d", a->rs);
    return true;
}

/* racw #imm */
static bool trans_RACW(DisasContext *ctx, arg_RACW *a)
{
    prt("racw\t#%d", a->imm + 1);
    return true;
}

/* sat rd */
static bool trans_SAT(DisasContext *ctx, arg_SAT *a)
{
    prt("sat\tr%d", a->rd);
    return true;
}

/* satr */
static bool trans_SATR(DisasContext *ctx, arg_SATR *a)
{
    prt("satr");
    return true;
}

/* fadd #imm, rd */
static bool trans_FADD_ir(DisasContext *ctx, arg_FADD_ir *a)
{
    prt("fadd\t#%d,r%d", li(ctx, 0), a->rd);
    return true;
}

/* fadd dsp[rs], rd */
/* fadd rs, rd */
static bool trans_FADD_mr(DisasContext *ctx, arg_FADD_mr *a)
{
    prt_ldmi(ctx, "fadd", a->ld, RX_IM_LONG, a->rs, a->rd);
    return true;
}

/* fcmp #imm, rd */
static bool trans_FCMP_ir(DisasContext *ctx, arg_FCMP_ir *a)
{
    prt("fadd\t#%d,r%d", li(ctx, 0), a->rd);
    return true;
}

/* fcmp dsp[rs], rd */
/* fcmp rs, rd */
static bool trans_FCMP_mr(DisasContext *ctx, arg_FCMP_mr *a)
{
    prt_ldmi(ctx, "fcmp", a->ld, RX_IM_LONG, a->rs, a->rd);
    return true;
}

/* fsub #imm, rd */
static bool trans_FSUB_ir(DisasContext *ctx, arg_FSUB_ir *a)
{
    prt("fsub\t#%d,r%d", li(ctx, 0), a->rd);
    return true;
}

/* fsub dsp[rs], rd */
/* fsub rs, rd */
static bool trans_FSUB_mr(DisasContext *ctx, arg_FSUB_mr *a)
{
    prt_ldmi(ctx, "fsub", a->ld, RX_IM_LONG, a->rs, a->rd);
    return true;
}

/* ftoi dsp[rs], rd */
/* ftoi rs, rd */
static bool trans_FTOI(DisasContext *ctx, arg_FTOI *a)
{
    prt_ldmi(ctx, "ftoi", a->ld, RX_IM_LONG, a->rs, a->rd);
    return true;
}

/* fmul #imm, rd */
static bool trans_FMUL_ir(DisasContext *ctx, arg_FMUL_ir *a)
{
    prt("fmul\t#%d,r%d", li(ctx, 0), a->rd);
    return true;
}

/* fmul dsp[rs], rd */
/* fmul rs, rd */
static bool trans_FMUL_mr(DisasContext *ctx, arg_FMUL_mr *a)
{
    prt_ldmi(ctx, "fmul", a->ld, RX_IM_LONG, a->rs, a->rd);
    return true;
}

/* fdiv #imm, rd */
static bool trans_FDIV_ir(DisasContext *ctx, arg_FDIV_ir *a)
{
    prt("fdiv\t#%d,r%d", li(ctx, 0), a->rd);
    return true;
}

/* fdiv dsp[rs], rd */
/* fdiv rs, rd */
static bool trans_FDIV_mr(DisasContext *ctx, arg_FDIV_mr *a)
{
    prt_ldmi(ctx, "fdiv", a->ld, RX_IM_LONG, a->rs, a->rd);
    return true;
}

/* round dsp[rs], rd */
/* round rs, rd */
static bool trans_ROUND(DisasContext *ctx, arg_ROUND *a)
{
    prt_ldmi(ctx, "round", a->ld, RX_IM_LONG, a->rs, a->rd);
    return true;
}

/* itof rs, rd */
/* itof dsp[rs], rd */
static bool trans_ITOF(DisasContext *ctx, arg_ITOF *a)
{
    prt_ldmi(ctx, "itof", a->ld, RX_IM_LONG, a->rs, a->rd);
    return true;
}

#define BOP_IM(name, reg)                                       \
    do {                                                        \
        char dsp[8];                                            \
        rx_index_addr(ctx, dsp, a->ld, RX_MEMORY_BYTE);         \
        prt("b%s\t#%d, %s[r%d]", #name, a->imm, dsp, reg);      \
        return true;                                            \
    } while (0)

#define BOP_RM(name)                                            \
    do {                                                        \
        char dsp[8];                                            \
        rx_index_addr(ctx, dsp, a->ld, RX_MEMORY_BYTE);         \
        prt("b%s\tr%d, %s[r%d]", #name, a->rd, dsp, a->rs);     \
        return true;                                            \
    } while (0)

/* bset #imm, dsp[rd] */
static bool trans_BSET_im(DisasContext *ctx, arg_BSET_im *a)
{
    BOP_IM(bset, a->rs);
}

/* bset rs, dsp[rd] */
static bool trans_BSET_rm(DisasContext *ctx, arg_BSET_rm *a)
{
    BOP_RM(set);
}

/* bset rs, rd */
static bool trans_BSET_rr(DisasContext *ctx, arg_BSET_rr *a)
{
    prt("bset\tr%d,r%d", a->rs, a->rd);
    return true;
}

/* bset #imm, rd */
static bool trans_BSET_ir(DisasContext *ctx, arg_BSET_ir *a)
{
    prt("bset\t#%d, r%d", a->imm, a->rd);
    return true;
}

/* bclr #imm, dsp[rd] */
static bool trans_BCLR_im(DisasContext *ctx, arg_BCLR_im *a)
{
    BOP_IM(clr, a->rs);
}

/* bclr rs, dsp[rd] */
static bool trans_BCLR_rm(DisasContext *ctx, arg_BCLR_rm *a)
{
    BOP_RM(clr);
}

/* bclr rs, rd */
static bool trans_BCLR_rr(DisasContext *ctx, arg_BCLR_rr *a)
{
    prt("bclr\tr%d, r%d", a->rs, a->rd);
    return true;
}

/* bclr #imm, rd */
static bool trans_BCLR_ir(DisasContext *ctx, arg_BCLR_ir *a)
{
    prt("bclr\t#%d,r%d", a->imm, a->rd);
    return true;
}

/* btst #imm, dsp[rd] */
static bool trans_BTST_im(DisasContext *ctx, arg_BTST_im *a)
{
    BOP_IM(tst, a->rs);
}

/* btst rs, dsp[rd] */
static bool trans_BTST_rm(DisasContext *ctx, arg_BTST_rm *a)
{
    BOP_RM(tst);
}

/* btst rs, rd */
static bool trans_BTST_rr(DisasContext *ctx, arg_BTST_rr *a)
{
    prt("btst\tr%d, r%d", a->rs, a->rd);
    return true;
}

/* btst #imm, rd */
static bool trans_BTST_ir(DisasContext *ctx, arg_BTST_ir *a)
{
    prt("btst\t#%d, r%d", a->imm, a->rd);
    return true;
}

/* bnot rs, dsp[rd] */
static bool trans_BNOT_rm(DisasContext *ctx, arg_BNOT_rm *a)
{
    BOP_RM(not);
}

/* bnot rs, rd */
static bool trans_BNOT_rr(DisasContext *ctx, arg_BNOT_rr *a)
{
    prt("bnot\tr%d, r%d", a->rs, a->rd);
    return true;
}

/* bnot #imm, dsp[rd] */
static bool trans_BNOT_im(DisasContext *ctx, arg_BNOT_im *a)
{
    BOP_IM(not, a->rs);
}

/* bnot #imm, rd */
static bool trans_BNOT_ir(DisasContext *ctx, arg_BNOT_ir *a)
{
    prt("bnot\t#%d, r%d", a->imm, a->rd);
    return true;
}

/* bmcond #imm, dsp[rd] */
static bool trans_BMCnd_im(DisasContext *ctx, arg_BMCnd_im *a)
{
    char dsp[8];

    rx_index_addr(ctx, dsp, a->ld, RX_MEMORY_BYTE);
    prt("bm%s\t#%d, %s[r%d]", cond[a->cd], a->imm, dsp, a->rd);
    return true;
}

/* bmcond #imm, rd */
static bool trans_BMCnd_ir(DisasContext *ctx, arg_BMCnd_ir *a)
{
    prt("bm%s\t#%d, r%d", cond[a->cd], a->imm, a->rd);
    return true;
}

/* clrpsw psw */
static bool trans_CLRPSW(DisasContext *ctx, arg_CLRPSW *a)
{
    prt("clrpsw\t%c", psw[a->cb]);
    return true;
}

/* setpsw psw */
static bool trans_SETPSW(DisasContext *ctx, arg_SETPSW *a)
{
    prt("setpsw\t%c", psw[a->cb]);
    return true;
}

/* mvtipl #imm */
static bool trans_MVTIPL(DisasContext *ctx, arg_MVTIPL *a)
{
    prt("movtipl\t#%d", a->imm);
    return true;
}

/* mvtc #imm, rd */
static bool trans_MVTC_i(DisasContext *ctx, arg_MVTC_i *a)
{
    prt("mvtc\t#0x%08x, %s", a->imm, rx_crname(a->cr));
    return true;
}

/* mvtc rs, rd */
static bool trans_MVTC_r(DisasContext *ctx, arg_MVTC_r *a)
{
    prt("mvtc\tr%d, %s", a->rs, rx_crname(a->cr));
    return true;
}

/* mvfc rs, rd */
static bool trans_MVFC(DisasContext *ctx, arg_MVFC *a)
{
    prt("mvfc\t%s, r%d", rx_crname(a->cr), a->rd);
    return true;
}

/* rtfi */
static bool trans_RTFI(DisasContext *ctx, arg_RTFI *a)
{
    prt("rtfi");
    return true;
}

/* rte */
static bool trans_RTE(DisasContext *ctx, arg_RTE *a)
{
    prt("rte");
    return true;
}

/* brk */
static bool trans_BRK(DisasContext *ctx, arg_BRK *a)
{
    prt("brk");
    return true;
}

/* int #imm */
static bool trans_INT(DisasContext *ctx, arg_INT *a)
{
    prt("int\t#%d", a->imm);
    return true;
}

/* wait */
static bool trans_WAIT(DisasContext *ctx, arg_WAIT *a)
{
    prt("wait");
    return true;
}

/* sccnd.[bwl] rd */
/* sccnd.[bwl] dsp:[rd] */
static bool trans_SCCnd(DisasContext *ctx, arg_SCCnd *a)
{
    if (a->ld < 3) {
        char dsp[8];
        rx_index_addr(ctx, dsp, a->sz, a->ld);
        prt("sc%s.%c\t%s[r%d]", cond[a->cd], size[a->sz], dsp, a->rd);
    } else {
        prt("sc%s.%c\tr%d", cond[a->cd], size[a->sz], a->rd);
    }
    return true;
}

int print_insn_rx(bfd_vma addr, disassemble_info *dis)
{
    DisasContext ctx;
    uint32_t insn;
    int i;

    ctx.dis = dis;
    ctx.pc = ctx.addr = addr;
    ctx.len = 0;

    insn = decode_load(&ctx);
    if (!decode(&ctx, insn)) {
        ctx.dis->fprintf_func(ctx.dis->stream, ".byte\t");
        for (i = 0; i < ctx.addr - addr; i++) {
            if (i > 0) {
                ctx.dis->fprintf_func(ctx.dis->stream, ",");
            }
            ctx.dis->fprintf_func(ctx.dis->stream, "0x%02x", insn >> 24);
            insn <<= 8;
        }
    }
    return ctx.addr - addr;
}
