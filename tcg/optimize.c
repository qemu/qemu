/*
 * Optimizations for Tiny Code Generator for QEMU
 *
 * Copyright (c) 2010 Samsung Electronics.
 * Contributed by Kirill Batuzov <batuzovk@ispras.ru>
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

#include "qemu/osdep.h"
#include "tcg/tcg-op.h"
#include "tcg-internal.h"

#define CASE_OP_32_64(x)                        \
        glue(glue(case INDEX_op_, x), _i32):    \
        glue(glue(case INDEX_op_, x), _i64)

#define CASE_OP_32_64_VEC(x)                    \
        glue(glue(case INDEX_op_, x), _i32):    \
        glue(glue(case INDEX_op_, x), _i64):    \
        glue(glue(case INDEX_op_, x), _vec)

typedef struct TempOptInfo {
    bool is_const;
    TCGTemp *prev_copy;
    TCGTemp *next_copy;
    uint64_t val;
    uint64_t z_mask;  /* mask bit is 0 if and only if value bit is 0 */
} TempOptInfo;

typedef struct OptContext {
    TCGContext *tcg;
    TCGOp *prev_mb;
    TCGTempSet temps_used;

    /* In flight values from optimization. */
    uint64_t z_mask;
} OptContext;

static inline TempOptInfo *ts_info(TCGTemp *ts)
{
    return ts->state_ptr;
}

static inline TempOptInfo *arg_info(TCGArg arg)
{
    return ts_info(arg_temp(arg));
}

static inline bool ts_is_const(TCGTemp *ts)
{
    return ts_info(ts)->is_const;
}

static inline bool arg_is_const(TCGArg arg)
{
    return ts_is_const(arg_temp(arg));
}

static inline bool ts_is_copy(TCGTemp *ts)
{
    return ts_info(ts)->next_copy != ts;
}

/* Reset TEMP's state, possibly removing the temp for the list of copies.  */
static void reset_ts(TCGTemp *ts)
{
    TempOptInfo *ti = ts_info(ts);
    TempOptInfo *pi = ts_info(ti->prev_copy);
    TempOptInfo *ni = ts_info(ti->next_copy);

    ni->prev_copy = ti->prev_copy;
    pi->next_copy = ti->next_copy;
    ti->next_copy = ts;
    ti->prev_copy = ts;
    ti->is_const = false;
    ti->z_mask = -1;
}

static void reset_temp(TCGArg arg)
{
    reset_ts(arg_temp(arg));
}

/* Initialize and activate a temporary.  */
static void init_ts_info(OptContext *ctx, TCGTemp *ts)
{
    size_t idx = temp_idx(ts);
    TempOptInfo *ti;

    if (test_bit(idx, ctx->temps_used.l)) {
        return;
    }
    set_bit(idx, ctx->temps_used.l);

    ti = ts->state_ptr;
    if (ti == NULL) {
        ti = tcg_malloc(sizeof(TempOptInfo));
        ts->state_ptr = ti;
    }

    ti->next_copy = ts;
    ti->prev_copy = ts;
    if (ts->kind == TEMP_CONST) {
        ti->is_const = true;
        ti->val = ts->val;
        ti->z_mask = ts->val;
        if (TCG_TARGET_REG_BITS > 32 && ts->type == TCG_TYPE_I32) {
            /* High bits of a 32-bit quantity are garbage.  */
            ti->z_mask |= ~0xffffffffull;
        }
    } else {
        ti->is_const = false;
        ti->z_mask = -1;
    }
}

static TCGTemp *find_better_copy(TCGContext *s, TCGTemp *ts)
{
    TCGTemp *i, *g, *l;

    /* If this is already readonly, we can't do better. */
    if (temp_readonly(ts)) {
        return ts;
    }

    g = l = NULL;
    for (i = ts_info(ts)->next_copy; i != ts; i = ts_info(i)->next_copy) {
        if (temp_readonly(i)) {
            return i;
        } else if (i->kind > ts->kind) {
            if (i->kind == TEMP_GLOBAL) {
                g = i;
            } else if (i->kind == TEMP_LOCAL) {
                l = i;
            }
        }
    }

    /* If we didn't find a better representation, return the same temp. */
    return g ? g : l ? l : ts;
}

static bool ts_are_copies(TCGTemp *ts1, TCGTemp *ts2)
{
    TCGTemp *i;

    if (ts1 == ts2) {
        return true;
    }

    if (!ts_is_copy(ts1) || !ts_is_copy(ts2)) {
        return false;
    }

    for (i = ts_info(ts1)->next_copy; i != ts1; i = ts_info(i)->next_copy) {
        if (i == ts2) {
            return true;
        }
    }

    return false;
}

static bool args_are_copies(TCGArg arg1, TCGArg arg2)
{
    return ts_are_copies(arg_temp(arg1), arg_temp(arg2));
}

static bool tcg_opt_gen_mov(OptContext *ctx, TCGOp *op, TCGArg dst, TCGArg src)
{
    TCGTemp *dst_ts = arg_temp(dst);
    TCGTemp *src_ts = arg_temp(src);
    const TCGOpDef *def;
    TempOptInfo *di;
    TempOptInfo *si;
    uint64_t z_mask;
    TCGOpcode new_op;

    if (ts_are_copies(dst_ts, src_ts)) {
        tcg_op_remove(ctx->tcg, op);
        return true;
    }

    reset_ts(dst_ts);
    di = ts_info(dst_ts);
    si = ts_info(src_ts);
    def = &tcg_op_defs[op->opc];
    if (def->flags & TCG_OPF_VECTOR) {
        new_op = INDEX_op_mov_vec;
    } else if (def->flags & TCG_OPF_64BIT) {
        new_op = INDEX_op_mov_i64;
    } else {
        new_op = INDEX_op_mov_i32;
    }
    op->opc = new_op;
    /* TCGOP_VECL and TCGOP_VECE remain unchanged.  */
    op->args[0] = dst;
    op->args[1] = src;

    z_mask = si->z_mask;
    if (TCG_TARGET_REG_BITS > 32 && new_op == INDEX_op_mov_i32) {
        /* High bits of the destination are now garbage.  */
        z_mask |= ~0xffffffffull;
    }
    di->z_mask = z_mask;

    if (src_ts->type == dst_ts->type) {
        TempOptInfo *ni = ts_info(si->next_copy);

        di->next_copy = si->next_copy;
        di->prev_copy = src_ts;
        ni->prev_copy = dst_ts;
        si->next_copy = dst_ts;
        di->is_const = si->is_const;
        di->val = si->val;
    }
    return true;
}

static bool tcg_opt_gen_movi(OptContext *ctx, TCGOp *op,
                             TCGArg dst, uint64_t val)
{
    const TCGOpDef *def = &tcg_op_defs[op->opc];
    TCGType type;
    TCGTemp *tv;

    if (def->flags & TCG_OPF_VECTOR) {
        type = TCGOP_VECL(op) + TCG_TYPE_V64;
    } else if (def->flags & TCG_OPF_64BIT) {
        type = TCG_TYPE_I64;
    } else {
        type = TCG_TYPE_I32;
    }

    /* Convert movi to mov with constant temp. */
    tv = tcg_constant_internal(type, val);
    init_ts_info(ctx, tv);
    return tcg_opt_gen_mov(ctx, op, dst, temp_arg(tv));
}

static uint64_t do_constant_folding_2(TCGOpcode op, uint64_t x, uint64_t y)
{
    uint64_t l64, h64;

    switch (op) {
    CASE_OP_32_64(add):
        return x + y;

    CASE_OP_32_64(sub):
        return x - y;

    CASE_OP_32_64(mul):
        return x * y;

    CASE_OP_32_64(and):
        return x & y;

    CASE_OP_32_64(or):
        return x | y;

    CASE_OP_32_64(xor):
        return x ^ y;

    case INDEX_op_shl_i32:
        return (uint32_t)x << (y & 31);

    case INDEX_op_shl_i64:
        return (uint64_t)x << (y & 63);

    case INDEX_op_shr_i32:
        return (uint32_t)x >> (y & 31);

    case INDEX_op_shr_i64:
        return (uint64_t)x >> (y & 63);

    case INDEX_op_sar_i32:
        return (int32_t)x >> (y & 31);

    case INDEX_op_sar_i64:
        return (int64_t)x >> (y & 63);

    case INDEX_op_rotr_i32:
        return ror32(x, y & 31);

    case INDEX_op_rotr_i64:
        return ror64(x, y & 63);

    case INDEX_op_rotl_i32:
        return rol32(x, y & 31);

    case INDEX_op_rotl_i64:
        return rol64(x, y & 63);

    CASE_OP_32_64(not):
        return ~x;

    CASE_OP_32_64(neg):
        return -x;

    CASE_OP_32_64(andc):
        return x & ~y;

    CASE_OP_32_64(orc):
        return x | ~y;

    CASE_OP_32_64(eqv):
        return ~(x ^ y);

    CASE_OP_32_64(nand):
        return ~(x & y);

    CASE_OP_32_64(nor):
        return ~(x | y);

    case INDEX_op_clz_i32:
        return (uint32_t)x ? clz32(x) : y;

    case INDEX_op_clz_i64:
        return x ? clz64(x) : y;

    case INDEX_op_ctz_i32:
        return (uint32_t)x ? ctz32(x) : y;

    case INDEX_op_ctz_i64:
        return x ? ctz64(x) : y;

    case INDEX_op_ctpop_i32:
        return ctpop32(x);

    case INDEX_op_ctpop_i64:
        return ctpop64(x);

    CASE_OP_32_64(ext8s):
        return (int8_t)x;

    CASE_OP_32_64(ext16s):
        return (int16_t)x;

    CASE_OP_32_64(ext8u):
        return (uint8_t)x;

    CASE_OP_32_64(ext16u):
        return (uint16_t)x;

    CASE_OP_32_64(bswap16):
        x = bswap16(x);
        return y & TCG_BSWAP_OS ? (int16_t)x : x;

    CASE_OP_32_64(bswap32):
        x = bswap32(x);
        return y & TCG_BSWAP_OS ? (int32_t)x : x;

    case INDEX_op_bswap64_i64:
        return bswap64(x);

    case INDEX_op_ext_i32_i64:
    case INDEX_op_ext32s_i64:
        return (int32_t)x;

    case INDEX_op_extu_i32_i64:
    case INDEX_op_extrl_i64_i32:
    case INDEX_op_ext32u_i64:
        return (uint32_t)x;

    case INDEX_op_extrh_i64_i32:
        return (uint64_t)x >> 32;

    case INDEX_op_muluh_i32:
        return ((uint64_t)(uint32_t)x * (uint32_t)y) >> 32;
    case INDEX_op_mulsh_i32:
        return ((int64_t)(int32_t)x * (int32_t)y) >> 32;

    case INDEX_op_muluh_i64:
        mulu64(&l64, &h64, x, y);
        return h64;
    case INDEX_op_mulsh_i64:
        muls64(&l64, &h64, x, y);
        return h64;

    case INDEX_op_div_i32:
        /* Avoid crashing on divide by zero, otherwise undefined.  */
        return (int32_t)x / ((int32_t)y ? : 1);
    case INDEX_op_divu_i32:
        return (uint32_t)x / ((uint32_t)y ? : 1);
    case INDEX_op_div_i64:
        return (int64_t)x / ((int64_t)y ? : 1);
    case INDEX_op_divu_i64:
        return (uint64_t)x / ((uint64_t)y ? : 1);

    case INDEX_op_rem_i32:
        return (int32_t)x % ((int32_t)y ? : 1);
    case INDEX_op_remu_i32:
        return (uint32_t)x % ((uint32_t)y ? : 1);
    case INDEX_op_rem_i64:
        return (int64_t)x % ((int64_t)y ? : 1);
    case INDEX_op_remu_i64:
        return (uint64_t)x % ((uint64_t)y ? : 1);

    default:
        fprintf(stderr,
                "Unrecognized operation %d in do_constant_folding.\n", op);
        tcg_abort();
    }
}

static uint64_t do_constant_folding(TCGOpcode op, uint64_t x, uint64_t y)
{
    const TCGOpDef *def = &tcg_op_defs[op];
    uint64_t res = do_constant_folding_2(op, x, y);
    if (!(def->flags & TCG_OPF_64BIT)) {
        res = (int32_t)res;
    }
    return res;
}

static bool do_constant_folding_cond_32(uint32_t x, uint32_t y, TCGCond c)
{
    switch (c) {
    case TCG_COND_EQ:
        return x == y;
    case TCG_COND_NE:
        return x != y;
    case TCG_COND_LT:
        return (int32_t)x < (int32_t)y;
    case TCG_COND_GE:
        return (int32_t)x >= (int32_t)y;
    case TCG_COND_LE:
        return (int32_t)x <= (int32_t)y;
    case TCG_COND_GT:
        return (int32_t)x > (int32_t)y;
    case TCG_COND_LTU:
        return x < y;
    case TCG_COND_GEU:
        return x >= y;
    case TCG_COND_LEU:
        return x <= y;
    case TCG_COND_GTU:
        return x > y;
    default:
        tcg_abort();
    }
}

static bool do_constant_folding_cond_64(uint64_t x, uint64_t y, TCGCond c)
{
    switch (c) {
    case TCG_COND_EQ:
        return x == y;
    case TCG_COND_NE:
        return x != y;
    case TCG_COND_LT:
        return (int64_t)x < (int64_t)y;
    case TCG_COND_GE:
        return (int64_t)x >= (int64_t)y;
    case TCG_COND_LE:
        return (int64_t)x <= (int64_t)y;
    case TCG_COND_GT:
        return (int64_t)x > (int64_t)y;
    case TCG_COND_LTU:
        return x < y;
    case TCG_COND_GEU:
        return x >= y;
    case TCG_COND_LEU:
        return x <= y;
    case TCG_COND_GTU:
        return x > y;
    default:
        tcg_abort();
    }
}

static bool do_constant_folding_cond_eq(TCGCond c)
{
    switch (c) {
    case TCG_COND_GT:
    case TCG_COND_LTU:
    case TCG_COND_LT:
    case TCG_COND_GTU:
    case TCG_COND_NE:
        return 0;
    case TCG_COND_GE:
    case TCG_COND_GEU:
    case TCG_COND_LE:
    case TCG_COND_LEU:
    case TCG_COND_EQ:
        return 1;
    default:
        tcg_abort();
    }
}

/*
 * Return -1 if the condition can't be simplified,
 * and the result of the condition (0 or 1) if it can.
 */
static int do_constant_folding_cond(TCGOpcode op, TCGArg x,
                                    TCGArg y, TCGCond c)
{
    uint64_t xv = arg_info(x)->val;
    uint64_t yv = arg_info(y)->val;

    if (arg_is_const(x) && arg_is_const(y)) {
        const TCGOpDef *def = &tcg_op_defs[op];
        tcg_debug_assert(!(def->flags & TCG_OPF_VECTOR));
        if (def->flags & TCG_OPF_64BIT) {
            return do_constant_folding_cond_64(xv, yv, c);
        } else {
            return do_constant_folding_cond_32(xv, yv, c);
        }
    } else if (args_are_copies(x, y)) {
        return do_constant_folding_cond_eq(c);
    } else if (arg_is_const(y) && yv == 0) {
        switch (c) {
        case TCG_COND_LTU:
            return 0;
        case TCG_COND_GEU:
            return 1;
        default:
            return -1;
        }
    }
    return -1;
}

/*
 * Return -1 if the condition can't be simplified,
 * and the result of the condition (0 or 1) if it can.
 */
static int do_constant_folding_cond2(TCGArg *p1, TCGArg *p2, TCGCond c)
{
    TCGArg al = p1[0], ah = p1[1];
    TCGArg bl = p2[0], bh = p2[1];

    if (arg_is_const(bl) && arg_is_const(bh)) {
        tcg_target_ulong blv = arg_info(bl)->val;
        tcg_target_ulong bhv = arg_info(bh)->val;
        uint64_t b = deposit64(blv, 32, 32, bhv);

        if (arg_is_const(al) && arg_is_const(ah)) {
            tcg_target_ulong alv = arg_info(al)->val;
            tcg_target_ulong ahv = arg_info(ah)->val;
            uint64_t a = deposit64(alv, 32, 32, ahv);
            return do_constant_folding_cond_64(a, b, c);
        }
        if (b == 0) {
            switch (c) {
            case TCG_COND_LTU:
                return 0;
            case TCG_COND_GEU:
                return 1;
            default:
                break;
            }
        }
    }
    if (args_are_copies(al, bl) && args_are_copies(ah, bh)) {
        return do_constant_folding_cond_eq(c);
    }
    return -1;
}

static bool swap_commutative(TCGArg dest, TCGArg *p1, TCGArg *p2)
{
    TCGArg a1 = *p1, a2 = *p2;
    int sum = 0;
    sum += arg_is_const(a1);
    sum -= arg_is_const(a2);

    /* Prefer the constant in second argument, and then the form
       op a, a, b, which is better handled on non-RISC hosts. */
    if (sum > 0 || (sum == 0 && dest == a2)) {
        *p1 = a2;
        *p2 = a1;
        return true;
    }
    return false;
}

static bool swap_commutative2(TCGArg *p1, TCGArg *p2)
{
    int sum = 0;
    sum += arg_is_const(p1[0]);
    sum += arg_is_const(p1[1]);
    sum -= arg_is_const(p2[0]);
    sum -= arg_is_const(p2[1]);
    if (sum > 0) {
        TCGArg t;
        t = p1[0], p1[0] = p2[0], p2[0] = t;
        t = p1[1], p1[1] = p2[1], p2[1] = t;
        return true;
    }
    return false;
}

static void init_arguments(OptContext *ctx, TCGOp *op, int nb_args)
{
    for (int i = 0; i < nb_args; i++) {
        TCGTemp *ts = arg_temp(op->args[i]);
        if (ts) {
            init_ts_info(ctx, ts);
        }
    }
}

static void copy_propagate(OptContext *ctx, TCGOp *op,
                           int nb_oargs, int nb_iargs)
{
    TCGContext *s = ctx->tcg;

    for (int i = nb_oargs; i < nb_oargs + nb_iargs; i++) {
        TCGTemp *ts = arg_temp(op->args[i]);
        if (ts && ts_is_copy(ts)) {
            op->args[i] = temp_arg(find_better_copy(s, ts));
        }
    }
}

static void finish_folding(OptContext *ctx, TCGOp *op)
{
    const TCGOpDef *def = &tcg_op_defs[op->opc];
    int i, nb_oargs;

    /*
     * For an opcode that ends a BB, reset all temp data.
     * We do no cross-BB optimization.
     */
    if (def->flags & TCG_OPF_BB_END) {
        memset(&ctx->temps_used, 0, sizeof(ctx->temps_used));
        ctx->prev_mb = NULL;
        return;
    }

    nb_oargs = def->nb_oargs;
    for (i = 0; i < nb_oargs; i++) {
        reset_temp(op->args[i]);
        /*
         * Save the corresponding known-zero bits mask for the
         * first output argument (only one supported so far).
         */
        if (i == 0) {
            arg_info(op->args[i])->z_mask = ctx->z_mask;
        }
    }
}

/*
 * The fold_* functions return true when processing is complete,
 * usually by folding the operation to a constant or to a copy,
 * and calling tcg_opt_gen_{mov,movi}.  They may do other things,
 * like collect information about the value produced, for use in
 * optimizing a subsequent operation.
 *
 * These first fold_* functions are all helpers, used by other
 * folders for more specific operations.
 */

static bool fold_const1(OptContext *ctx, TCGOp *op)
{
    if (arg_is_const(op->args[1])) {
        uint64_t t;

        t = arg_info(op->args[1])->val;
        t = do_constant_folding(op->opc, t, 0);
        return tcg_opt_gen_movi(ctx, op, op->args[0], t);
    }
    return false;
}

static bool fold_const2(OptContext *ctx, TCGOp *op)
{
    if (arg_is_const(op->args[1]) && arg_is_const(op->args[2])) {
        uint64_t t1 = arg_info(op->args[1])->val;
        uint64_t t2 = arg_info(op->args[2])->val;

        t1 = do_constant_folding(op->opc, t1, t2);
        return tcg_opt_gen_movi(ctx, op, op->args[0], t1);
    }
    return false;
}

/* If the binary operation has both arguments equal, fold to @i. */
static bool fold_xx_to_i(OptContext *ctx, TCGOp *op, uint64_t i)
{
    if (args_are_copies(op->args[1], op->args[2])) {
        return tcg_opt_gen_movi(ctx, op, op->args[0], i);
    }
    return false;
}

/*
 * These outermost fold_<op> functions are sorted alphabetically.
 */

static bool fold_add(OptContext *ctx, TCGOp *op)
{
    return fold_const2(ctx, op);
}

static bool fold_addsub2_i32(OptContext *ctx, TCGOp *op, bool add)
{
    if (arg_is_const(op->args[2]) && arg_is_const(op->args[3]) &&
        arg_is_const(op->args[4]) && arg_is_const(op->args[5])) {
        uint32_t al = arg_info(op->args[2])->val;
        uint32_t ah = arg_info(op->args[3])->val;
        uint32_t bl = arg_info(op->args[4])->val;
        uint32_t bh = arg_info(op->args[5])->val;
        uint64_t a = ((uint64_t)ah << 32) | al;
        uint64_t b = ((uint64_t)bh << 32) | bl;
        TCGArg rl, rh;
        TCGOp *op2 = tcg_op_insert_before(ctx->tcg, op, INDEX_op_mov_i32);

        if (add) {
            a += b;
        } else {
            a -= b;
        }

        rl = op->args[0];
        rh = op->args[1];
        tcg_opt_gen_movi(ctx, op, rl, (int32_t)a);
        tcg_opt_gen_movi(ctx, op2, rh, (int32_t)(a >> 32));
        return true;
    }
    return false;
}

static bool fold_add2_i32(OptContext *ctx, TCGOp *op)
{
    return fold_addsub2_i32(ctx, op, true);
}

static bool fold_and(OptContext *ctx, TCGOp *op)
{
    return fold_const2(ctx, op);
}

static bool fold_andc(OptContext *ctx, TCGOp *op)
{
    if (fold_const2(ctx, op) ||
        fold_xx_to_i(ctx, op, 0)) {
        return true;
    }
    return false;
}

static bool fold_brcond(OptContext *ctx, TCGOp *op)
{
    TCGCond cond = op->args[2];
    int i = do_constant_folding_cond(op->opc, op->args[0], op->args[1], cond);

    if (i == 0) {
        tcg_op_remove(ctx->tcg, op);
        return true;
    }
    if (i > 0) {
        op->opc = INDEX_op_br;
        op->args[0] = op->args[3];
    }
    return false;
}

static bool fold_brcond2(OptContext *ctx, TCGOp *op)
{
    TCGCond cond = op->args[4];
    int i = do_constant_folding_cond2(&op->args[0], &op->args[2], cond);
    TCGArg label = op->args[5];
    int inv = 0;

    if (i >= 0) {
        goto do_brcond_const;
    }

    switch (cond) {
    case TCG_COND_LT:
    case TCG_COND_GE:
        /*
         * Simplify LT/GE comparisons vs zero to a single compare
         * vs the high word of the input.
         */
        if (arg_is_const(op->args[2]) && arg_info(op->args[2])->val == 0 &&
            arg_is_const(op->args[3]) && arg_info(op->args[3])->val == 0) {
            goto do_brcond_high;
        }
        break;

    case TCG_COND_NE:
        inv = 1;
        QEMU_FALLTHROUGH;
    case TCG_COND_EQ:
        /*
         * Simplify EQ/NE comparisons where one of the pairs
         * can be simplified.
         */
        i = do_constant_folding_cond(INDEX_op_brcond_i32, op->args[0],
                                     op->args[2], cond);
        switch (i ^ inv) {
        case 0:
            goto do_brcond_const;
        case 1:
            goto do_brcond_high;
        }

        i = do_constant_folding_cond(INDEX_op_brcond_i32, op->args[1],
                                     op->args[3], cond);
        switch (i ^ inv) {
        case 0:
            goto do_brcond_const;
        case 1:
            op->opc = INDEX_op_brcond_i32;
            op->args[1] = op->args[2];
            op->args[2] = cond;
            op->args[3] = label;
            break;
        }
        break;

    default:
        break;

    do_brcond_high:
        op->opc = INDEX_op_brcond_i32;
        op->args[0] = op->args[1];
        op->args[1] = op->args[3];
        op->args[2] = cond;
        op->args[3] = label;
        break;

    do_brcond_const:
        if (i == 0) {
            tcg_op_remove(ctx->tcg, op);
            return true;
        }
        op->opc = INDEX_op_br;
        op->args[0] = label;
        break;
    }
    return false;
}

static bool fold_bswap(OptContext *ctx, TCGOp *op)
{
    if (arg_is_const(op->args[1])) {
        uint64_t t = arg_info(op->args[1])->val;

        t = do_constant_folding(op->opc, t, op->args[2]);
        return tcg_opt_gen_movi(ctx, op, op->args[0], t);
    }
    return false;
}

static bool fold_call(OptContext *ctx, TCGOp *op)
{
    TCGContext *s = ctx->tcg;
    int nb_oargs = TCGOP_CALLO(op);
    int nb_iargs = TCGOP_CALLI(op);
    int flags, i;

    init_arguments(ctx, op, nb_oargs + nb_iargs);
    copy_propagate(ctx, op, nb_oargs, nb_iargs);

    /* If the function reads or writes globals, reset temp data. */
    flags = tcg_call_flags(op);
    if (!(flags & (TCG_CALL_NO_READ_GLOBALS | TCG_CALL_NO_WRITE_GLOBALS))) {
        int nb_globals = s->nb_globals;

        for (i = 0; i < nb_globals; i++) {
            if (test_bit(i, ctx->temps_used.l)) {
                reset_ts(&ctx->tcg->temps[i]);
            }
        }
    }

    /* Reset temp data for outputs. */
    for (i = 0; i < nb_oargs; i++) {
        reset_temp(op->args[i]);
    }

    /* Stop optimizing MB across calls. */
    ctx->prev_mb = NULL;
    return true;
}

static bool fold_count_zeros(OptContext *ctx, TCGOp *op)
{
    if (arg_is_const(op->args[1])) {
        uint64_t t = arg_info(op->args[1])->val;

        if (t != 0) {
            t = do_constant_folding(op->opc, t, 0);
            return tcg_opt_gen_movi(ctx, op, op->args[0], t);
        }
        return tcg_opt_gen_mov(ctx, op, op->args[0], op->args[2]);
    }
    return false;
}

static bool fold_ctpop(OptContext *ctx, TCGOp *op)
{
    return fold_const1(ctx, op);
}

static bool fold_deposit(OptContext *ctx, TCGOp *op)
{
    if (arg_is_const(op->args[1]) && arg_is_const(op->args[2])) {
        uint64_t t1 = arg_info(op->args[1])->val;
        uint64_t t2 = arg_info(op->args[2])->val;

        t1 = deposit64(t1, op->args[3], op->args[4], t2);
        return tcg_opt_gen_movi(ctx, op, op->args[0], t1);
    }
    return false;
}

static bool fold_divide(OptContext *ctx, TCGOp *op)
{
    return fold_const2(ctx, op);
}

static bool fold_dup(OptContext *ctx, TCGOp *op)
{
    if (arg_is_const(op->args[1])) {
        uint64_t t = arg_info(op->args[1])->val;
        t = dup_const(TCGOP_VECE(op), t);
        return tcg_opt_gen_movi(ctx, op, op->args[0], t);
    }
    return false;
}

static bool fold_dup2(OptContext *ctx, TCGOp *op)
{
    if (arg_is_const(op->args[1]) && arg_is_const(op->args[2])) {
        uint64_t t = deposit64(arg_info(op->args[1])->val, 32, 32,
                               arg_info(op->args[2])->val);
        return tcg_opt_gen_movi(ctx, op, op->args[0], t);
    }

    if (args_are_copies(op->args[1], op->args[2])) {
        op->opc = INDEX_op_dup_vec;
        TCGOP_VECE(op) = MO_32;
    }
    return false;
}

static bool fold_eqv(OptContext *ctx, TCGOp *op)
{
    return fold_const2(ctx, op);
}

static bool fold_extract(OptContext *ctx, TCGOp *op)
{
    if (arg_is_const(op->args[1])) {
        uint64_t t;

        t = arg_info(op->args[1])->val;
        t = extract64(t, op->args[2], op->args[3]);
        return tcg_opt_gen_movi(ctx, op, op->args[0], t);
    }
    return false;
}

static bool fold_extract2(OptContext *ctx, TCGOp *op)
{
    if (arg_is_const(op->args[1]) && arg_is_const(op->args[2])) {
        uint64_t v1 = arg_info(op->args[1])->val;
        uint64_t v2 = arg_info(op->args[2])->val;
        int shr = op->args[3];

        if (op->opc == INDEX_op_extract2_i64) {
            v1 >>= shr;
            v2 <<= 64 - shr;
        } else {
            v1 = (uint32_t)v1 >> shr;
            v2 = (int32_t)v2 << (32 - shr);
        }
        return tcg_opt_gen_movi(ctx, op, op->args[0], v1 | v2);
    }
    return false;
}

static bool fold_exts(OptContext *ctx, TCGOp *op)
{
    return fold_const1(ctx, op);
}

static bool fold_extu(OptContext *ctx, TCGOp *op)
{
    return fold_const1(ctx, op);
}

static bool fold_mb(OptContext *ctx, TCGOp *op)
{
    /* Eliminate duplicate and redundant fence instructions.  */
    if (ctx->prev_mb) {
        /*
         * Merge two barriers of the same type into one,
         * or a weaker barrier into a stronger one,
         * or two weaker barriers into a stronger one.
         *   mb X; mb Y => mb X|Y
         *   mb; strl => mb; st
         *   ldaq; mb => ld; mb
         *   ldaq; strl => ld; mb; st
         * Other combinations are also merged into a strong
         * barrier.  This is stricter than specified but for
         * the purposes of TCG is better than not optimizing.
         */
        ctx->prev_mb->args[0] |= op->args[0];
        tcg_op_remove(ctx->tcg, op);
    } else {
        ctx->prev_mb = op;
    }
    return true;
}

static bool fold_mov(OptContext *ctx, TCGOp *op)
{
    return tcg_opt_gen_mov(ctx, op, op->args[0], op->args[1]);
}

static bool fold_movcond(OptContext *ctx, TCGOp *op)
{
    TCGOpcode opc = op->opc;
    TCGCond cond = op->args[5];
    int i = do_constant_folding_cond(opc, op->args[1], op->args[2], cond);

    if (i >= 0) {
        return tcg_opt_gen_mov(ctx, op, op->args[0], op->args[4 - i]);
    }

    if (arg_is_const(op->args[3]) && arg_is_const(op->args[4])) {
        uint64_t tv = arg_info(op->args[3])->val;
        uint64_t fv = arg_info(op->args[4])->val;

        opc = (opc == INDEX_op_movcond_i32
               ? INDEX_op_setcond_i32 : INDEX_op_setcond_i64);

        if (tv == 1 && fv == 0) {
            op->opc = opc;
            op->args[3] = cond;
        } else if (fv == 1 && tv == 0) {
            op->opc = opc;
            op->args[3] = tcg_invert_cond(cond);
        }
    }
    return false;
}

static bool fold_mul(OptContext *ctx, TCGOp *op)
{
    return fold_const2(ctx, op);
}

static bool fold_mul_highpart(OptContext *ctx, TCGOp *op)
{
    return fold_const2(ctx, op);
}

static bool fold_mulu2_i32(OptContext *ctx, TCGOp *op)
{
    if (arg_is_const(op->args[2]) && arg_is_const(op->args[3])) {
        uint32_t a = arg_info(op->args[2])->val;
        uint32_t b = arg_info(op->args[3])->val;
        uint64_t r = (uint64_t)a * b;
        TCGArg rl, rh;
        TCGOp *op2 = tcg_op_insert_before(ctx->tcg, op, INDEX_op_mov_i32);

        rl = op->args[0];
        rh = op->args[1];
        tcg_opt_gen_movi(ctx, op, rl, (int32_t)r);
        tcg_opt_gen_movi(ctx, op2, rh, (int32_t)(r >> 32));
        return true;
    }
    return false;
}

static bool fold_nand(OptContext *ctx, TCGOp *op)
{
    return fold_const2(ctx, op);
}

static bool fold_neg(OptContext *ctx, TCGOp *op)
{
    return fold_const1(ctx, op);
}

static bool fold_nor(OptContext *ctx, TCGOp *op)
{
    return fold_const2(ctx, op);
}

static bool fold_not(OptContext *ctx, TCGOp *op)
{
    return fold_const1(ctx, op);
}

static bool fold_or(OptContext *ctx, TCGOp *op)
{
    return fold_const2(ctx, op);
}

static bool fold_orc(OptContext *ctx, TCGOp *op)
{
    return fold_const2(ctx, op);
}

static bool fold_qemu_ld(OptContext *ctx, TCGOp *op)
{
    /* Opcodes that touch guest memory stop the mb optimization.  */
    ctx->prev_mb = NULL;
    return false;
}

static bool fold_qemu_st(OptContext *ctx, TCGOp *op)
{
    /* Opcodes that touch guest memory stop the mb optimization.  */
    ctx->prev_mb = NULL;
    return false;
}

static bool fold_remainder(OptContext *ctx, TCGOp *op)
{
    return fold_const2(ctx, op);
}

static bool fold_setcond(OptContext *ctx, TCGOp *op)
{
    TCGCond cond = op->args[3];
    int i = do_constant_folding_cond(op->opc, op->args[1], op->args[2], cond);

    if (i >= 0) {
        return tcg_opt_gen_movi(ctx, op, op->args[0], i);
    }
    return false;
}

static bool fold_setcond2(OptContext *ctx, TCGOp *op)
{
    TCGCond cond = op->args[5];
    int i = do_constant_folding_cond2(&op->args[1], &op->args[3], cond);
    int inv = 0;

    if (i >= 0) {
        goto do_setcond_const;
    }

    switch (cond) {
    case TCG_COND_LT:
    case TCG_COND_GE:
        /*
         * Simplify LT/GE comparisons vs zero to a single compare
         * vs the high word of the input.
         */
        if (arg_is_const(op->args[3]) && arg_info(op->args[3])->val == 0 &&
            arg_is_const(op->args[4]) && arg_info(op->args[4])->val == 0) {
            goto do_setcond_high;
        }
        break;

    case TCG_COND_NE:
        inv = 1;
        QEMU_FALLTHROUGH;
    case TCG_COND_EQ:
        /*
         * Simplify EQ/NE comparisons where one of the pairs
         * can be simplified.
         */
        i = do_constant_folding_cond(INDEX_op_setcond_i32, op->args[1],
                                     op->args[3], cond);
        switch (i ^ inv) {
        case 0:
            goto do_setcond_const;
        case 1:
            goto do_setcond_high;
        }

        i = do_constant_folding_cond(INDEX_op_setcond_i32, op->args[2],
                                     op->args[4], cond);
        switch (i ^ inv) {
        case 0:
            goto do_setcond_const;
        case 1:
            op->args[2] = op->args[3];
            op->args[3] = cond;
            op->opc = INDEX_op_setcond_i32;
            break;
        }
        break;

    default:
        break;

    do_setcond_high:
        op->args[1] = op->args[2];
        op->args[2] = op->args[4];
        op->args[3] = cond;
        op->opc = INDEX_op_setcond_i32;
        break;
    }
    return false;

 do_setcond_const:
    return tcg_opt_gen_movi(ctx, op, op->args[0], i);
}

static bool fold_sextract(OptContext *ctx, TCGOp *op)
{
    if (arg_is_const(op->args[1])) {
        uint64_t t;

        t = arg_info(op->args[1])->val;
        t = sextract64(t, op->args[2], op->args[3]);
        return tcg_opt_gen_movi(ctx, op, op->args[0], t);
    }
    return false;
}

static bool fold_shift(OptContext *ctx, TCGOp *op)
{
    return fold_const2(ctx, op);
}

static bool fold_sub(OptContext *ctx, TCGOp *op)
{
    if (fold_const2(ctx, op) ||
        fold_xx_to_i(ctx, op, 0)) {
        return true;
    }
    return false;
}

static bool fold_sub2_i32(OptContext *ctx, TCGOp *op)
{
    return fold_addsub2_i32(ctx, op, false);
}

static bool fold_xor(OptContext *ctx, TCGOp *op)
{
    if (fold_const2(ctx, op) ||
        fold_xx_to_i(ctx, op, 0)) {
        return true;
    }
    return false;
}

/* Propagate constants and copies, fold constant expressions. */
void tcg_optimize(TCGContext *s)
{
    int nb_temps, i;
    TCGOp *op, *op_next;
    OptContext ctx = { .tcg = s };

    /* Array VALS has an element for each temp.
       If this temp holds a constant then its value is kept in VALS' element.
       If this temp is a copy of other ones then the other copies are
       available through the doubly linked circular list. */

    nb_temps = s->nb_temps;
    for (i = 0; i < nb_temps; ++i) {
        s->temps[i].state_ptr = NULL;
    }

    QTAILQ_FOREACH_SAFE(op, &s->ops, link, op_next) {
        uint64_t z_mask, partmask, affected, tmp;
        TCGOpcode opc = op->opc;
        const TCGOpDef *def;
        bool done = false;

        /* Calls are special. */
        if (opc == INDEX_op_call) {
            fold_call(&ctx, op);
            continue;
        }

        def = &tcg_op_defs[opc];
        init_arguments(&ctx, op, def->nb_oargs + def->nb_iargs);
        copy_propagate(&ctx, op, def->nb_oargs, def->nb_iargs);

        /* For commutative operations make constant second argument */
        switch (opc) {
        CASE_OP_32_64_VEC(add):
        CASE_OP_32_64_VEC(mul):
        CASE_OP_32_64_VEC(and):
        CASE_OP_32_64_VEC(or):
        CASE_OP_32_64_VEC(xor):
        CASE_OP_32_64(eqv):
        CASE_OP_32_64(nand):
        CASE_OP_32_64(nor):
        CASE_OP_32_64(muluh):
        CASE_OP_32_64(mulsh):
            swap_commutative(op->args[0], &op->args[1], &op->args[2]);
            break;
        CASE_OP_32_64(brcond):
            if (swap_commutative(-1, &op->args[0], &op->args[1])) {
                op->args[2] = tcg_swap_cond(op->args[2]);
            }
            break;
        CASE_OP_32_64(setcond):
            if (swap_commutative(op->args[0], &op->args[1], &op->args[2])) {
                op->args[3] = tcg_swap_cond(op->args[3]);
            }
            break;
        CASE_OP_32_64(movcond):
            if (swap_commutative(-1, &op->args[1], &op->args[2])) {
                op->args[5] = tcg_swap_cond(op->args[5]);
            }
            /* For movcond, we canonicalize the "false" input reg to match
               the destination reg so that the tcg backend can implement
               a "move if true" operation.  */
            if (swap_commutative(op->args[0], &op->args[4], &op->args[3])) {
                op->args[5] = tcg_invert_cond(op->args[5]);
            }
            break;
        CASE_OP_32_64(add2):
            swap_commutative(op->args[0], &op->args[2], &op->args[4]);
            swap_commutative(op->args[1], &op->args[3], &op->args[5]);
            break;
        CASE_OP_32_64(mulu2):
        CASE_OP_32_64(muls2):
            swap_commutative(op->args[0], &op->args[2], &op->args[3]);
            break;
        case INDEX_op_brcond2_i32:
            if (swap_commutative2(&op->args[0], &op->args[2])) {
                op->args[4] = tcg_swap_cond(op->args[4]);
            }
            break;
        case INDEX_op_setcond2_i32:
            if (swap_commutative2(&op->args[1], &op->args[3])) {
                op->args[5] = tcg_swap_cond(op->args[5]);
            }
            break;
        default:
            break;
        }

        /* Simplify expressions for "shift/rot r, 0, a => movi r, 0",
           and "sub r, 0, a => neg r, a" case.  */
        switch (opc) {
        CASE_OP_32_64(shl):
        CASE_OP_32_64(shr):
        CASE_OP_32_64(sar):
        CASE_OP_32_64(rotl):
        CASE_OP_32_64(rotr):
            if (arg_is_const(op->args[1])
                && arg_info(op->args[1])->val == 0) {
                tcg_opt_gen_movi(&ctx, op, op->args[0], 0);
                continue;
            }
            break;
        CASE_OP_32_64_VEC(sub):
            {
                TCGOpcode neg_op;
                bool have_neg;

                if (arg_is_const(op->args[2])) {
                    /* Proceed with possible constant folding. */
                    break;
                }
                if (opc == INDEX_op_sub_i32) {
                    neg_op = INDEX_op_neg_i32;
                    have_neg = TCG_TARGET_HAS_neg_i32;
                } else if (opc == INDEX_op_sub_i64) {
                    neg_op = INDEX_op_neg_i64;
                    have_neg = TCG_TARGET_HAS_neg_i64;
                } else if (TCG_TARGET_HAS_neg_vec) {
                    TCGType type = TCGOP_VECL(op) + TCG_TYPE_V64;
                    unsigned vece = TCGOP_VECE(op);
                    neg_op = INDEX_op_neg_vec;
                    have_neg = tcg_can_emit_vec_op(neg_op, type, vece) > 0;
                } else {
                    break;
                }
                if (!have_neg) {
                    break;
                }
                if (arg_is_const(op->args[1])
                    && arg_info(op->args[1])->val == 0) {
                    op->opc = neg_op;
                    reset_temp(op->args[0]);
                    op->args[1] = op->args[2];
                    continue;
                }
            }
            break;
        CASE_OP_32_64_VEC(xor):
        CASE_OP_32_64(nand):
            if (!arg_is_const(op->args[1])
                && arg_is_const(op->args[2])
                && arg_info(op->args[2])->val == -1) {
                i = 1;
                goto try_not;
            }
            break;
        CASE_OP_32_64(nor):
            if (!arg_is_const(op->args[1])
                && arg_is_const(op->args[2])
                && arg_info(op->args[2])->val == 0) {
                i = 1;
                goto try_not;
            }
            break;
        CASE_OP_32_64_VEC(andc):
            if (!arg_is_const(op->args[2])
                && arg_is_const(op->args[1])
                && arg_info(op->args[1])->val == -1) {
                i = 2;
                goto try_not;
            }
            break;
        CASE_OP_32_64_VEC(orc):
        CASE_OP_32_64(eqv):
            if (!arg_is_const(op->args[2])
                && arg_is_const(op->args[1])
                && arg_info(op->args[1])->val == 0) {
                i = 2;
                goto try_not;
            }
            break;
        try_not:
            {
                TCGOpcode not_op;
                bool have_not;

                if (def->flags & TCG_OPF_VECTOR) {
                    not_op = INDEX_op_not_vec;
                    have_not = TCG_TARGET_HAS_not_vec;
                } else if (def->flags & TCG_OPF_64BIT) {
                    not_op = INDEX_op_not_i64;
                    have_not = TCG_TARGET_HAS_not_i64;
                } else {
                    not_op = INDEX_op_not_i32;
                    have_not = TCG_TARGET_HAS_not_i32;
                }
                if (!have_not) {
                    break;
                }
                op->opc = not_op;
                reset_temp(op->args[0]);
                op->args[1] = op->args[i];
                continue;
            }
        default:
            break;
        }

        /* Simplify expression for "op r, a, const => mov r, a" cases */
        switch (opc) {
        CASE_OP_32_64_VEC(add):
        CASE_OP_32_64_VEC(sub):
        CASE_OP_32_64_VEC(or):
        CASE_OP_32_64_VEC(xor):
        CASE_OP_32_64_VEC(andc):
        CASE_OP_32_64(shl):
        CASE_OP_32_64(shr):
        CASE_OP_32_64(sar):
        CASE_OP_32_64(rotl):
        CASE_OP_32_64(rotr):
            if (!arg_is_const(op->args[1])
                && arg_is_const(op->args[2])
                && arg_info(op->args[2])->val == 0) {
                tcg_opt_gen_mov(&ctx, op, op->args[0], op->args[1]);
                continue;
            }
            break;
        CASE_OP_32_64_VEC(and):
        CASE_OP_32_64_VEC(orc):
        CASE_OP_32_64(eqv):
            if (!arg_is_const(op->args[1])
                && arg_is_const(op->args[2])
                && arg_info(op->args[2])->val == -1) {
                tcg_opt_gen_mov(&ctx, op, op->args[0], op->args[1]);
                continue;
            }
            break;
        default:
            break;
        }

        /* Simplify using known-zero bits. Currently only ops with a single
           output argument is supported. */
        z_mask = -1;
        affected = -1;
        switch (opc) {
        CASE_OP_32_64(ext8s):
            if ((arg_info(op->args[1])->z_mask & 0x80) != 0) {
                break;
            }
            QEMU_FALLTHROUGH;
        CASE_OP_32_64(ext8u):
            z_mask = 0xff;
            goto and_const;
        CASE_OP_32_64(ext16s):
            if ((arg_info(op->args[1])->z_mask & 0x8000) != 0) {
                break;
            }
            QEMU_FALLTHROUGH;
        CASE_OP_32_64(ext16u):
            z_mask = 0xffff;
            goto and_const;
        case INDEX_op_ext32s_i64:
            if ((arg_info(op->args[1])->z_mask & 0x80000000) != 0) {
                break;
            }
            QEMU_FALLTHROUGH;
        case INDEX_op_ext32u_i64:
            z_mask = 0xffffffffU;
            goto and_const;

        CASE_OP_32_64(and):
            z_mask = arg_info(op->args[2])->z_mask;
            if (arg_is_const(op->args[2])) {
        and_const:
                affected = arg_info(op->args[1])->z_mask & ~z_mask;
            }
            z_mask = arg_info(op->args[1])->z_mask & z_mask;
            break;

        case INDEX_op_ext_i32_i64:
            if ((arg_info(op->args[1])->z_mask & 0x80000000) != 0) {
                break;
            }
            QEMU_FALLTHROUGH;
        case INDEX_op_extu_i32_i64:
            /* We do not compute affected as it is a size changing op.  */
            z_mask = (uint32_t)arg_info(op->args[1])->z_mask;
            break;

        CASE_OP_32_64(andc):
            /* Known-zeros does not imply known-ones.  Therefore unless
               op->args[2] is constant, we can't infer anything from it.  */
            if (arg_is_const(op->args[2])) {
                z_mask = ~arg_info(op->args[2])->z_mask;
                goto and_const;
            }
            /* But we certainly know nothing outside args[1] may be set. */
            z_mask = arg_info(op->args[1])->z_mask;
            break;

        case INDEX_op_sar_i32:
            if (arg_is_const(op->args[2])) {
                tmp = arg_info(op->args[2])->val & 31;
                z_mask = (int32_t)arg_info(op->args[1])->z_mask >> tmp;
            }
            break;
        case INDEX_op_sar_i64:
            if (arg_is_const(op->args[2])) {
                tmp = arg_info(op->args[2])->val & 63;
                z_mask = (int64_t)arg_info(op->args[1])->z_mask >> tmp;
            }
            break;

        case INDEX_op_shr_i32:
            if (arg_is_const(op->args[2])) {
                tmp = arg_info(op->args[2])->val & 31;
                z_mask = (uint32_t)arg_info(op->args[1])->z_mask >> tmp;
            }
            break;
        case INDEX_op_shr_i64:
            if (arg_is_const(op->args[2])) {
                tmp = arg_info(op->args[2])->val & 63;
                z_mask = (uint64_t)arg_info(op->args[1])->z_mask >> tmp;
            }
            break;

        case INDEX_op_extrl_i64_i32:
            z_mask = (uint32_t)arg_info(op->args[1])->z_mask;
            break;
        case INDEX_op_extrh_i64_i32:
            z_mask = (uint64_t)arg_info(op->args[1])->z_mask >> 32;
            break;

        CASE_OP_32_64(shl):
            if (arg_is_const(op->args[2])) {
                tmp = arg_info(op->args[2])->val & (TCG_TARGET_REG_BITS - 1);
                z_mask = arg_info(op->args[1])->z_mask << tmp;
            }
            break;

        CASE_OP_32_64(neg):
            /* Set to 1 all bits to the left of the rightmost.  */
            z_mask = -(arg_info(op->args[1])->z_mask
                       & -arg_info(op->args[1])->z_mask);
            break;

        CASE_OP_32_64(deposit):
            z_mask = deposit64(arg_info(op->args[1])->z_mask,
                               op->args[3], op->args[4],
                               arg_info(op->args[2])->z_mask);
            break;

        CASE_OP_32_64(extract):
            z_mask = extract64(arg_info(op->args[1])->z_mask,
                               op->args[2], op->args[3]);
            if (op->args[2] == 0) {
                affected = arg_info(op->args[1])->z_mask & ~z_mask;
            }
            break;
        CASE_OP_32_64(sextract):
            z_mask = sextract64(arg_info(op->args[1])->z_mask,
                                op->args[2], op->args[3]);
            if (op->args[2] == 0 && (tcg_target_long)z_mask >= 0) {
                affected = arg_info(op->args[1])->z_mask & ~z_mask;
            }
            break;

        CASE_OP_32_64(or):
        CASE_OP_32_64(xor):
            z_mask = arg_info(op->args[1])->z_mask
                   | arg_info(op->args[2])->z_mask;
            break;

        case INDEX_op_clz_i32:
        case INDEX_op_ctz_i32:
            z_mask = arg_info(op->args[2])->z_mask | 31;
            break;

        case INDEX_op_clz_i64:
        case INDEX_op_ctz_i64:
            z_mask = arg_info(op->args[2])->z_mask | 63;
            break;

        case INDEX_op_ctpop_i32:
            z_mask = 32 | 31;
            break;
        case INDEX_op_ctpop_i64:
            z_mask = 64 | 63;
            break;

        CASE_OP_32_64(setcond):
        case INDEX_op_setcond2_i32:
            z_mask = 1;
            break;

        CASE_OP_32_64(movcond):
            z_mask = arg_info(op->args[3])->z_mask
                   | arg_info(op->args[4])->z_mask;
            break;

        CASE_OP_32_64(ld8u):
            z_mask = 0xff;
            break;
        CASE_OP_32_64(ld16u):
            z_mask = 0xffff;
            break;
        case INDEX_op_ld32u_i64:
            z_mask = 0xffffffffu;
            break;

        CASE_OP_32_64(qemu_ld):
            {
                MemOpIdx oi = op->args[def->nb_oargs + def->nb_iargs];
                MemOp mop = get_memop(oi);
                if (!(mop & MO_SIGN)) {
                    z_mask = (2ULL << ((8 << (mop & MO_SIZE)) - 1)) - 1;
                }
            }
            break;

        CASE_OP_32_64(bswap16):
            z_mask = arg_info(op->args[1])->z_mask;
            if (z_mask <= 0xffff) {
                op->args[2] |= TCG_BSWAP_IZ;
            }
            z_mask = bswap16(z_mask);
            switch (op->args[2] & (TCG_BSWAP_OZ | TCG_BSWAP_OS)) {
            case TCG_BSWAP_OZ:
                break;
            case TCG_BSWAP_OS:
                z_mask = (int16_t)z_mask;
                break;
            default: /* undefined high bits */
                z_mask |= MAKE_64BIT_MASK(16, 48);
                break;
            }
            break;

        case INDEX_op_bswap32_i64:
            z_mask = arg_info(op->args[1])->z_mask;
            if (z_mask <= 0xffffffffu) {
                op->args[2] |= TCG_BSWAP_IZ;
            }
            z_mask = bswap32(z_mask);
            switch (op->args[2] & (TCG_BSWAP_OZ | TCG_BSWAP_OS)) {
            case TCG_BSWAP_OZ:
                break;
            case TCG_BSWAP_OS:
                z_mask = (int32_t)z_mask;
                break;
            default: /* undefined high bits */
                z_mask |= MAKE_64BIT_MASK(32, 32);
                break;
            }
            break;

        default:
            break;
        }

        /* 32-bit ops generate 32-bit results.  For the result is zero test
           below, we can ignore high bits, but for further optimizations we
           need to record that the high bits contain garbage.  */
        partmask = z_mask;
        if (!(def->flags & TCG_OPF_64BIT)) {
            z_mask |= ~(tcg_target_ulong)0xffffffffu;
            partmask &= 0xffffffffu;
            affected &= 0xffffffffu;
        }
        ctx.z_mask = z_mask;

        if (partmask == 0) {
            tcg_opt_gen_movi(&ctx, op, op->args[0], 0);
            continue;
        }
        if (affected == 0) {
            tcg_opt_gen_mov(&ctx, op, op->args[0], op->args[1]);
            continue;
        }

        /* Simplify expression for "op r, a, 0 => movi r, 0" cases */
        switch (opc) {
        CASE_OP_32_64_VEC(and):
        CASE_OP_32_64_VEC(mul):
        CASE_OP_32_64(muluh):
        CASE_OP_32_64(mulsh):
            if (arg_is_const(op->args[2])
                && arg_info(op->args[2])->val == 0) {
                tcg_opt_gen_movi(&ctx, op, op->args[0], 0);
                continue;
            }
            break;
        default:
            break;
        }

        /* Simplify expression for "op r, a, a => mov r, a" cases */
        switch (opc) {
        CASE_OP_32_64_VEC(or):
        CASE_OP_32_64_VEC(and):
            if (args_are_copies(op->args[1], op->args[2])) {
                tcg_opt_gen_mov(&ctx, op, op->args[0], op->args[1]);
                continue;
            }
            break;
        default:
            break;
        }

        /*
         * Process each opcode.
         * Sorted alphabetically by opcode as much as possible.
         */
        switch (opc) {
        CASE_OP_32_64_VEC(add):
            done = fold_add(&ctx, op);
            break;
        case INDEX_op_add2_i32:
            done = fold_add2_i32(&ctx, op);
            break;
        CASE_OP_32_64_VEC(and):
            done = fold_and(&ctx, op);
            break;
        CASE_OP_32_64_VEC(andc):
            done = fold_andc(&ctx, op);
            break;
        CASE_OP_32_64(brcond):
            done = fold_brcond(&ctx, op);
            break;
        case INDEX_op_brcond2_i32:
            done = fold_brcond2(&ctx, op);
            break;
        CASE_OP_32_64(bswap16):
        CASE_OP_32_64(bswap32):
        case INDEX_op_bswap64_i64:
            done = fold_bswap(&ctx, op);
            break;
        CASE_OP_32_64(clz):
        CASE_OP_32_64(ctz):
            done = fold_count_zeros(&ctx, op);
            break;
        CASE_OP_32_64(ctpop):
            done = fold_ctpop(&ctx, op);
            break;
        CASE_OP_32_64(deposit):
            done = fold_deposit(&ctx, op);
            break;
        CASE_OP_32_64(div):
        CASE_OP_32_64(divu):
            done = fold_divide(&ctx, op);
            break;
        case INDEX_op_dup_vec:
            done = fold_dup(&ctx, op);
            break;
        case INDEX_op_dup2_vec:
            done = fold_dup2(&ctx, op);
            break;
        CASE_OP_32_64(eqv):
            done = fold_eqv(&ctx, op);
            break;
        CASE_OP_32_64(extract):
            done = fold_extract(&ctx, op);
            break;
        CASE_OP_32_64(extract2):
            done = fold_extract2(&ctx, op);
            break;
        CASE_OP_32_64(ext8s):
        CASE_OP_32_64(ext16s):
        case INDEX_op_ext32s_i64:
        case INDEX_op_ext_i32_i64:
            done = fold_exts(&ctx, op);
            break;
        CASE_OP_32_64(ext8u):
        CASE_OP_32_64(ext16u):
        case INDEX_op_ext32u_i64:
        case INDEX_op_extu_i32_i64:
        case INDEX_op_extrl_i64_i32:
        case INDEX_op_extrh_i64_i32:
            done = fold_extu(&ctx, op);
            break;
        case INDEX_op_mb:
            done = fold_mb(&ctx, op);
            break;
        CASE_OP_32_64_VEC(mov):
            done = fold_mov(&ctx, op);
            break;
        CASE_OP_32_64(movcond):
            done = fold_movcond(&ctx, op);
            break;
        CASE_OP_32_64(mul):
            done = fold_mul(&ctx, op);
            break;
        CASE_OP_32_64(mulsh):
        CASE_OP_32_64(muluh):
            done = fold_mul_highpart(&ctx, op);
            break;
        case INDEX_op_mulu2_i32:
            done = fold_mulu2_i32(&ctx, op);
            break;
        CASE_OP_32_64(nand):
            done = fold_nand(&ctx, op);
            break;
        CASE_OP_32_64(neg):
            done = fold_neg(&ctx, op);
            break;
        CASE_OP_32_64(nor):
            done = fold_nor(&ctx, op);
            break;
        CASE_OP_32_64_VEC(not):
            done = fold_not(&ctx, op);
            break;
        CASE_OP_32_64_VEC(or):
            done = fold_or(&ctx, op);
            break;
        CASE_OP_32_64_VEC(orc):
            done = fold_orc(&ctx, op);
            break;
        case INDEX_op_qemu_ld_i32:
        case INDEX_op_qemu_ld_i64:
            done = fold_qemu_ld(&ctx, op);
            break;
        case INDEX_op_qemu_st_i32:
        case INDEX_op_qemu_st8_i32:
        case INDEX_op_qemu_st_i64:
            done = fold_qemu_st(&ctx, op);
            break;
        CASE_OP_32_64(rem):
        CASE_OP_32_64(remu):
            done = fold_remainder(&ctx, op);
            break;
        CASE_OP_32_64(rotl):
        CASE_OP_32_64(rotr):
        CASE_OP_32_64(sar):
        CASE_OP_32_64(shl):
        CASE_OP_32_64(shr):
            done = fold_shift(&ctx, op);
            break;
        CASE_OP_32_64(setcond):
            done = fold_setcond(&ctx, op);
            break;
        case INDEX_op_setcond2_i32:
            done = fold_setcond2(&ctx, op);
            break;
        CASE_OP_32_64(sextract):
            done = fold_sextract(&ctx, op);
            break;
        CASE_OP_32_64_VEC(sub):
            done = fold_sub(&ctx, op);
            break;
        case INDEX_op_sub2_i32:
            done = fold_sub2_i32(&ctx, op);
            break;
        CASE_OP_32_64_VEC(xor):
            done = fold_xor(&ctx, op);
            break;
        default:
            break;
        }

        if (!done) {
            finish_folding(&ctx, op);
        }
    }
}
