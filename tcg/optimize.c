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
#include "qemu/int128.h"
#include "qemu/interval-tree.h"
#include "tcg/tcg-op-common.h"
#include "tcg-internal.h"
#include "tcg-has.h"

#define CASE_OP_32_64(x)                        \
        glue(glue(case INDEX_op_, x), _i32):    \
        glue(glue(case INDEX_op_, x), _i64)

#define CASE_OP_32_64_VEC(x)                    \
        glue(glue(case INDEX_op_, x), _i32):    \
        glue(glue(case INDEX_op_, x), _i64):    \
        glue(glue(case INDEX_op_, x), _vec)

typedef struct MemCopyInfo {
    IntervalTreeNode itree;
    QSIMPLEQ_ENTRY (MemCopyInfo) next;
    TCGTemp *ts;
    TCGType type;
} MemCopyInfo;

typedef struct TempOptInfo {
    bool is_const;
    TCGTemp *prev_copy;
    TCGTemp *next_copy;
    QSIMPLEQ_HEAD(, MemCopyInfo) mem_copy;
    uint64_t val;
    uint64_t z_mask;  /* mask bit is 0 if and only if value bit is 0 */
    uint64_t s_mask;  /* mask bit is 1 if value bit matches msb */
} TempOptInfo;

typedef struct OptContext {
    TCGContext *tcg;
    TCGOp *prev_mb;
    TCGTempSet temps_used;

    IntervalTreeRoot mem_copy;
    QSIMPLEQ_HEAD(, MemCopyInfo) mem_free;

    /* In flight values from optimization. */
    TCGType type;
} OptContext;

static inline TempOptInfo *ts_info(TCGTemp *ts)
{
    return ts->state_ptr;
}

static inline TempOptInfo *arg_info(TCGArg arg)
{
    return ts_info(arg_temp(arg));
}

static inline bool ti_is_const(TempOptInfo *ti)
{
    return ti->is_const;
}

static inline uint64_t ti_const_val(TempOptInfo *ti)
{
    return ti->val;
}

static inline bool ti_is_const_val(TempOptInfo *ti, uint64_t val)
{
    return ti_is_const(ti) && ti_const_val(ti) == val;
}

static inline bool ts_is_const(TCGTemp *ts)
{
    return ti_is_const(ts_info(ts));
}

static inline bool ts_is_const_val(TCGTemp *ts, uint64_t val)
{
    return ti_is_const_val(ts_info(ts), val);
}

static inline bool arg_is_const(TCGArg arg)
{
    return ts_is_const(arg_temp(arg));
}

static inline bool arg_is_const_val(TCGArg arg, uint64_t val)
{
    return ts_is_const_val(arg_temp(arg), val);
}

static inline bool ts_is_copy(TCGTemp *ts)
{
    return ts_info(ts)->next_copy != ts;
}

static TCGTemp *cmp_better_copy(TCGTemp *a, TCGTemp *b)
{
    return a->kind < b->kind ? b : a;
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
    QSIMPLEQ_INIT(&ti->mem_copy);
    if (ts->kind == TEMP_CONST) {
        ti->is_const = true;
        ti->val = ts->val;
        ti->z_mask = ts->val;
        ti->s_mask = INT64_MIN >> clrsb64(ts->val);
    } else {
        ti->is_const = false;
        ti->z_mask = -1;
        ti->s_mask = 0;
    }
}

static MemCopyInfo *mem_copy_first(OptContext *ctx, intptr_t s, intptr_t l)
{
    IntervalTreeNode *r = interval_tree_iter_first(&ctx->mem_copy, s, l);
    return r ? container_of(r, MemCopyInfo, itree) : NULL;
}

static MemCopyInfo *mem_copy_next(MemCopyInfo *mem, intptr_t s, intptr_t l)
{
    IntervalTreeNode *r = interval_tree_iter_next(&mem->itree, s, l);
    return r ? container_of(r, MemCopyInfo, itree) : NULL;
}

static void remove_mem_copy(OptContext *ctx, MemCopyInfo *mc)
{
    TCGTemp *ts = mc->ts;
    TempOptInfo *ti = ts_info(ts);

    interval_tree_remove(&mc->itree, &ctx->mem_copy);
    QSIMPLEQ_REMOVE(&ti->mem_copy, mc, MemCopyInfo, next);
    QSIMPLEQ_INSERT_TAIL(&ctx->mem_free, mc, next);
}

static void remove_mem_copy_in(OptContext *ctx, intptr_t s, intptr_t l)
{
    while (true) {
        MemCopyInfo *mc = mem_copy_first(ctx, s, l);
        if (!mc) {
            break;
        }
        remove_mem_copy(ctx, mc);
    }
}

static void remove_mem_copy_all(OptContext *ctx)
{
    remove_mem_copy_in(ctx, 0, -1);
    tcg_debug_assert(interval_tree_is_empty(&ctx->mem_copy));
}

static TCGTemp *find_better_copy(TCGTemp *ts)
{
    TCGTemp *i, *ret;

    /* If this is already readonly, we can't do better. */
    if (temp_readonly(ts)) {
        return ts;
    }

    ret = ts;
    for (i = ts_info(ts)->next_copy; i != ts; i = ts_info(i)->next_copy) {
        ret = cmp_better_copy(ret, i);
    }
    return ret;
}

static void move_mem_copies(TCGTemp *dst_ts, TCGTemp *src_ts)
{
    TempOptInfo *si = ts_info(src_ts);
    TempOptInfo *di = ts_info(dst_ts);
    MemCopyInfo *mc;

    QSIMPLEQ_FOREACH(mc, &si->mem_copy, next) {
        tcg_debug_assert(mc->ts == src_ts);
        mc->ts = dst_ts;
    }
    QSIMPLEQ_CONCAT(&di->mem_copy, &si->mem_copy);
}

/* Reset TEMP's state, possibly removing the temp for the list of copies.  */
static void reset_ts(OptContext *ctx, TCGTemp *ts)
{
    TempOptInfo *ti = ts_info(ts);
    TCGTemp *pts = ti->prev_copy;
    TCGTemp *nts = ti->next_copy;
    TempOptInfo *pi = ts_info(pts);
    TempOptInfo *ni = ts_info(nts);

    ni->prev_copy = ti->prev_copy;
    pi->next_copy = ti->next_copy;
    ti->next_copy = ts;
    ti->prev_copy = ts;
    ti->is_const = false;
    ti->z_mask = -1;
    ti->s_mask = 0;

    if (!QSIMPLEQ_EMPTY(&ti->mem_copy)) {
        if (ts == nts) {
            /* Last temp copy being removed, the mem copies die. */
            MemCopyInfo *mc;
            QSIMPLEQ_FOREACH(mc, &ti->mem_copy, next) {
                interval_tree_remove(&mc->itree, &ctx->mem_copy);
            }
            QSIMPLEQ_CONCAT(&ctx->mem_free, &ti->mem_copy);
        } else {
            move_mem_copies(find_better_copy(nts), ts);
        }
    }
}

static void reset_temp(OptContext *ctx, TCGArg arg)
{
    reset_ts(ctx, arg_temp(arg));
}

static void record_mem_copy(OptContext *ctx, TCGType type,
                            TCGTemp *ts, intptr_t start, intptr_t last)
{
    MemCopyInfo *mc;
    TempOptInfo *ti;

    mc = QSIMPLEQ_FIRST(&ctx->mem_free);
    if (mc) {
        QSIMPLEQ_REMOVE_HEAD(&ctx->mem_free, next);
    } else {
        mc = tcg_malloc(sizeof(*mc));
    }

    memset(mc, 0, sizeof(*mc));
    mc->itree.start = start;
    mc->itree.last = last;
    mc->type = type;
    interval_tree_insert(&mc->itree, &ctx->mem_copy);

    ts = find_better_copy(ts);
    ti = ts_info(ts);
    mc->ts = ts;
    QSIMPLEQ_INSERT_TAIL(&ti->mem_copy, mc, next);
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

static TCGTemp *find_mem_copy_for(OptContext *ctx, TCGType type, intptr_t s)
{
    MemCopyInfo *mc;

    for (mc = mem_copy_first(ctx, s, s); mc; mc = mem_copy_next(mc, s, s)) {
        if (mc->itree.start == s && mc->type == type) {
            return find_better_copy(mc->ts);
        }
    }
    return NULL;
}

static TCGArg arg_new_constant(OptContext *ctx, uint64_t val)
{
    TCGType type = ctx->type;
    TCGTemp *ts;

    if (type == TCG_TYPE_I32) {
        val = (int32_t)val;
    }

    ts = tcg_constant_internal(type, val);
    init_ts_info(ctx, ts);

    return temp_arg(ts);
}

static TCGArg arg_new_temp(OptContext *ctx)
{
    TCGTemp *ts = tcg_temp_new_internal(ctx->type, TEMP_EBB);
    init_ts_info(ctx, ts);
    return temp_arg(ts);
}

static bool tcg_opt_gen_mov(OptContext *ctx, TCGOp *op, TCGArg dst, TCGArg src)
{
    TCGTemp *dst_ts = arg_temp(dst);
    TCGTemp *src_ts = arg_temp(src);
    TempOptInfo *di;
    TempOptInfo *si;
    TCGOpcode new_op;

    if (ts_are_copies(dst_ts, src_ts)) {
        tcg_op_remove(ctx->tcg, op);
        return true;
    }

    reset_ts(ctx, dst_ts);
    di = ts_info(dst_ts);
    si = ts_info(src_ts);

    switch (ctx->type) {
    case TCG_TYPE_I32:
        new_op = INDEX_op_mov_i32;
        break;
    case TCG_TYPE_I64:
        new_op = INDEX_op_mov_i64;
        break;
    case TCG_TYPE_V64:
    case TCG_TYPE_V128:
    case TCG_TYPE_V256:
        /* TCGOP_TYPE and TCGOP_VECE remain unchanged.  */
        new_op = INDEX_op_mov_vec;
        break;
    default:
        g_assert_not_reached();
    }
    op->opc = new_op;
    op->args[0] = dst;
    op->args[1] = src;

    di->z_mask = si->z_mask;
    di->s_mask = si->s_mask;

    if (src_ts->type == dst_ts->type) {
        TempOptInfo *ni = ts_info(si->next_copy);

        di->next_copy = si->next_copy;
        di->prev_copy = src_ts;
        ni->prev_copy = dst_ts;
        si->next_copy = dst_ts;
        di->is_const = si->is_const;
        di->val = si->val;

        if (!QSIMPLEQ_EMPTY(&si->mem_copy)
            && cmp_better_copy(src_ts, dst_ts) == dst_ts) {
            move_mem_copies(dst_ts, src_ts);
        }
    }
    return true;
}

static bool tcg_opt_gen_movi(OptContext *ctx, TCGOp *op,
                             TCGArg dst, uint64_t val)
{
    /* Convert movi to mov with constant temp. */
    return tcg_opt_gen_mov(ctx, op, dst, arg_new_constant(ctx, val));
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

    CASE_OP_32_64_VEC(and):
        return x & y;

    CASE_OP_32_64_VEC(or):
        return x | y;

    CASE_OP_32_64_VEC(xor):
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

    CASE_OP_32_64_VEC(not):
        return ~x;

    CASE_OP_32_64(neg):
        return -x;

    CASE_OP_32_64_VEC(andc):
        return x & ~y;

    CASE_OP_32_64_VEC(orc):
        return x | ~y;

    CASE_OP_32_64_VEC(eqv):
        return ~(x ^ y);

    CASE_OP_32_64_VEC(nand):
        return ~(x & y);

    CASE_OP_32_64_VEC(nor):
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
        g_assert_not_reached();
    }
}

static uint64_t do_constant_folding(TCGOpcode op, TCGType type,
                                    uint64_t x, uint64_t y)
{
    uint64_t res = do_constant_folding_2(op, x, y);
    if (type == TCG_TYPE_I32) {
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
    case TCG_COND_TSTEQ:
        return (x & y) == 0;
    case TCG_COND_TSTNE:
        return (x & y) != 0;
    case TCG_COND_ALWAYS:
    case TCG_COND_NEVER:
        break;
    }
    g_assert_not_reached();
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
    case TCG_COND_TSTEQ:
        return (x & y) == 0;
    case TCG_COND_TSTNE:
        return (x & y) != 0;
    case TCG_COND_ALWAYS:
    case TCG_COND_NEVER:
        break;
    }
    g_assert_not_reached();
}

static int do_constant_folding_cond_eq(TCGCond c)
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
    case TCG_COND_TSTEQ:
    case TCG_COND_TSTNE:
        return -1;
    case TCG_COND_ALWAYS:
    case TCG_COND_NEVER:
        break;
    }
    g_assert_not_reached();
}

/*
 * Return -1 if the condition can't be simplified,
 * and the result of the condition (0 or 1) if it can.
 */
static int do_constant_folding_cond(TCGType type, TCGArg x,
                                    TCGArg y, TCGCond c)
{
    if (arg_is_const(x) && arg_is_const(y)) {
        uint64_t xv = arg_info(x)->val;
        uint64_t yv = arg_info(y)->val;

        switch (type) {
        case TCG_TYPE_I32:
            return do_constant_folding_cond_32(xv, yv, c);
        case TCG_TYPE_I64:
            return do_constant_folding_cond_64(xv, yv, c);
        default:
            /* Only scalar comparisons are optimizable */
            return -1;
        }
    } else if (args_are_copies(x, y)) {
        return do_constant_folding_cond_eq(c);
    } else if (arg_is_const_val(y, 0)) {
        switch (c) {
        case TCG_COND_LTU:
        case TCG_COND_TSTNE:
            return 0;
        case TCG_COND_GEU:
        case TCG_COND_TSTEQ:
            return 1;
        default:
            return -1;
        }
    }
    return -1;
}

/**
 * swap_commutative:
 * @dest: TCGArg of the destination argument, or NO_DEST.
 * @p1: first paired argument
 * @p2: second paired argument
 *
 * If *@p1 is a constant and *@p2 is not, swap.
 * If *@p2 matches @dest, swap.
 * Return true if a swap was performed.
 */

#define NO_DEST  temp_arg(NULL)

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

/*
 * Return -1 if the condition can't be simplified,
 * and the result of the condition (0 or 1) if it can.
 */
static int do_constant_folding_cond1(OptContext *ctx, TCGOp *op, TCGArg dest,
                                     TCGArg *p1, TCGArg *p2, TCGArg *pcond)
{
    TCGCond cond;
    bool swap;
    int r;

    swap = swap_commutative(dest, p1, p2);
    cond = *pcond;
    if (swap) {
        *pcond = cond = tcg_swap_cond(cond);
    }

    r = do_constant_folding_cond(ctx->type, *p1, *p2, cond);
    if (r >= 0) {
        return r;
    }
    if (!is_tst_cond(cond)) {
        return -1;
    }

    /*
     * TSTNE x,x -> NE x,0
     * TSTNE x,-1 -> NE x,0
     */
    if (args_are_copies(*p1, *p2) || arg_is_const_val(*p2, -1)) {
        *p2 = arg_new_constant(ctx, 0);
        *pcond = tcg_tst_eqne_cond(cond);
        return -1;
    }

    /* TSTNE x,sign -> LT x,0 */
    if (arg_is_const_val(*p2, (ctx->type == TCG_TYPE_I32
                               ? INT32_MIN : INT64_MIN))) {
        *p2 = arg_new_constant(ctx, 0);
        *pcond = tcg_tst_ltge_cond(cond);
        return -1;
    }

    /* Expand to AND with a temporary if no backend support. */
    if (!TCG_TARGET_HAS_tst) {
        TCGOpcode and_opc = (ctx->type == TCG_TYPE_I32
                             ? INDEX_op_and_i32 : INDEX_op_and_i64);
        TCGOp *op2 = tcg_op_insert_before(ctx->tcg, op, and_opc, 3);
        TCGArg tmp = arg_new_temp(ctx);

        op2->args[0] = tmp;
        op2->args[1] = *p1;
        op2->args[2] = *p2;

        *p1 = tmp;
        *p2 = arg_new_constant(ctx, 0);
        *pcond = tcg_tst_eqne_cond(cond);
    }
    return -1;
}

static int do_constant_folding_cond2(OptContext *ctx, TCGOp *op, TCGArg *args)
{
    TCGArg al, ah, bl, bh;
    TCGCond c;
    bool swap;
    int r;

    swap = swap_commutative2(args, args + 2);
    c = args[4];
    if (swap) {
        args[4] = c = tcg_swap_cond(c);
    }

    al = args[0];
    ah = args[1];
    bl = args[2];
    bh = args[3];

    if (arg_is_const(bl) && arg_is_const(bh)) {
        tcg_target_ulong blv = arg_info(bl)->val;
        tcg_target_ulong bhv = arg_info(bh)->val;
        uint64_t b = deposit64(blv, 32, 32, bhv);

        if (arg_is_const(al) && arg_is_const(ah)) {
            tcg_target_ulong alv = arg_info(al)->val;
            tcg_target_ulong ahv = arg_info(ah)->val;
            uint64_t a = deposit64(alv, 32, 32, ahv);

            r = do_constant_folding_cond_64(a, b, c);
            if (r >= 0) {
                return r;
            }
        }

        if (b == 0) {
            switch (c) {
            case TCG_COND_LTU:
            case TCG_COND_TSTNE:
                return 0;
            case TCG_COND_GEU:
            case TCG_COND_TSTEQ:
                return 1;
            default:
                break;
            }
        }

        /* TSTNE x,-1 -> NE x,0 */
        if (b == -1 && is_tst_cond(c)) {
            args[3] = args[2] = arg_new_constant(ctx, 0);
            args[4] = tcg_tst_eqne_cond(c);
            return -1;
        }

        /* TSTNE x,sign -> LT x,0 */
        if (b == INT64_MIN && is_tst_cond(c)) {
            /* bl must be 0, so copy that to bh */
            args[3] = bl;
            args[4] = tcg_tst_ltge_cond(c);
            return -1;
        }
    }

    if (args_are_copies(al, bl) && args_are_copies(ah, bh)) {
        r = do_constant_folding_cond_eq(c);
        if (r >= 0) {
            return r;
        }

        /* TSTNE x,x -> NE x,0 */
        if (is_tst_cond(c)) {
            args[3] = args[2] = arg_new_constant(ctx, 0);
            args[4] = tcg_tst_eqne_cond(c);
            return -1;
        }
    }

    /* Expand to AND with a temporary if no backend support. */
    if (!TCG_TARGET_HAS_tst && is_tst_cond(c)) {
        TCGOp *op1 = tcg_op_insert_before(ctx->tcg, op, INDEX_op_and_i32, 3);
        TCGOp *op2 = tcg_op_insert_before(ctx->tcg, op, INDEX_op_and_i32, 3);
        TCGArg t1 = arg_new_temp(ctx);
        TCGArg t2 = arg_new_temp(ctx);

        op1->args[0] = t1;
        op1->args[1] = al;
        op1->args[2] = bl;
        op2->args[0] = t2;
        op2->args[1] = ah;
        op2->args[2] = bh;

        args[0] = t1;
        args[1] = t2;
        args[3] = args[2] = arg_new_constant(ctx, 0);
        args[4] = tcg_tst_eqne_cond(c);
    }
    return -1;
}

static void init_arguments(OptContext *ctx, TCGOp *op, int nb_args)
{
    for (int i = 0; i < nb_args; i++) {
        TCGTemp *ts = arg_temp(op->args[i]);
        init_ts_info(ctx, ts);
    }
}

static void copy_propagate(OptContext *ctx, TCGOp *op,
                           int nb_oargs, int nb_iargs)
{
    for (int i = nb_oargs; i < nb_oargs + nb_iargs; i++) {
        TCGTemp *ts = arg_temp(op->args[i]);
        if (ts_is_copy(ts)) {
            op->args[i] = temp_arg(find_better_copy(ts));
        }
    }
}

static void finish_bb(OptContext *ctx)
{
    /* We only optimize memory barriers across basic blocks. */
    ctx->prev_mb = NULL;
}

static void finish_ebb(OptContext *ctx)
{
    finish_bb(ctx);
    /* We only optimize across extended basic blocks. */
    memset(&ctx->temps_used, 0, sizeof(ctx->temps_used));
    remove_mem_copy_all(ctx);
}

static bool finish_folding(OptContext *ctx, TCGOp *op)
{
    const TCGOpDef *def = &tcg_op_defs[op->opc];
    int i, nb_oargs;

    nb_oargs = def->nb_oargs;
    for (i = 0; i < nb_oargs; i++) {
        TCGTemp *ts = arg_temp(op->args[i]);
        reset_ts(ctx, ts);
    }
    return true;
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
        t = do_constant_folding(op->opc, ctx->type, t, 0);
        return tcg_opt_gen_movi(ctx, op, op->args[0], t);
    }
    return false;
}

static bool fold_const2(OptContext *ctx, TCGOp *op)
{
    if (arg_is_const(op->args[1]) && arg_is_const(op->args[2])) {
        uint64_t t1 = arg_info(op->args[1])->val;
        uint64_t t2 = arg_info(op->args[2])->val;

        t1 = do_constant_folding(op->opc, ctx->type, t1, t2);
        return tcg_opt_gen_movi(ctx, op, op->args[0], t1);
    }
    return false;
}

static bool fold_commutative(OptContext *ctx, TCGOp *op)
{
    swap_commutative(op->args[0], &op->args[1], &op->args[2]);
    return false;
}

static bool fold_const2_commutative(OptContext *ctx, TCGOp *op)
{
    swap_commutative(op->args[0], &op->args[1], &op->args[2]);
    return fold_const2(ctx, op);
}

/*
 * Record "zero" and "sign" masks for the single output of @op.
 * See TempOptInfo definition of z_mask and s_mask.
 * If z_mask allows, fold the output to constant zero.
 * The passed s_mask may be augmented by z_mask.
 */
static bool fold_masks_zs(OptContext *ctx, TCGOp *op,
                          uint64_t z_mask, int64_t s_mask)
{
    const TCGOpDef *def = &tcg_op_defs[op->opc];
    TCGTemp *ts;
    TempOptInfo *ti;
    int rep;

    /* Only single-output opcodes are supported here. */
    tcg_debug_assert(def->nb_oargs == 1);

    /*
     * 32-bit ops generate 32-bit results, which for the purpose of
     * simplifying tcg are sign-extended.  Certainly that's how we
     * represent our constants elsewhere.  Note that the bits will
     * be reset properly for a 64-bit value when encountering the
     * type changing opcodes.
     */
    if (ctx->type == TCG_TYPE_I32) {
        z_mask = (int32_t)z_mask;
        s_mask |= INT32_MIN;
    }

    if (z_mask == 0) {
        return tcg_opt_gen_movi(ctx, op, op->args[0], 0);
    }

    ts = arg_temp(op->args[0]);
    reset_ts(ctx, ts);

    ti = ts_info(ts);
    ti->z_mask = z_mask;

    /* Canonicalize s_mask and incorporate data from z_mask. */
    rep = clz64(~s_mask);
    rep = MAX(rep, clz64(z_mask));
    rep = MAX(rep - 1, 0);
    ti->s_mask = INT64_MIN >> rep;

    return true;
}

static bool fold_masks_z(OptContext *ctx, TCGOp *op, uint64_t z_mask)
{
    return fold_masks_zs(ctx, op, z_mask, 0);
}

static bool fold_masks_s(OptContext *ctx, TCGOp *op, uint64_t s_mask)
{
    return fold_masks_zs(ctx, op, -1, s_mask);
}

/*
 * An "affected" mask bit is 0 if and only if the result is identical
 * to the first input.  Thus if the entire mask is 0, the operation
 * is equivalent to a copy.
 */
static bool fold_affected_mask(OptContext *ctx, TCGOp *op, uint64_t a_mask)
{
    if (ctx->type == TCG_TYPE_I32) {
        a_mask = (uint32_t)a_mask;
    }
    if (a_mask == 0) {
        return tcg_opt_gen_mov(ctx, op, op->args[0], op->args[1]);
    }
    return false;
}

/*
 * Convert @op to NOT, if NOT is supported by the host.
 * Return true f the conversion is successful, which will still
 * indicate that the processing is complete.
 */
static bool fold_not(OptContext *ctx, TCGOp *op);
static bool fold_to_not(OptContext *ctx, TCGOp *op, int idx)
{
    TCGOpcode not_op;
    bool have_not;

    switch (ctx->type) {
    case TCG_TYPE_I32:
        not_op = INDEX_op_not_i32;
        have_not = TCG_TARGET_HAS_not_i32;
        break;
    case TCG_TYPE_I64:
        not_op = INDEX_op_not_i64;
        have_not = TCG_TARGET_HAS_not_i64;
        break;
    case TCG_TYPE_V64:
    case TCG_TYPE_V128:
    case TCG_TYPE_V256:
        not_op = INDEX_op_not_vec;
        have_not = TCG_TARGET_HAS_not_vec;
        break;
    default:
        g_assert_not_reached();
    }
    if (have_not) {
        op->opc = not_op;
        op->args[1] = op->args[idx];
        return fold_not(ctx, op);
    }
    return false;
}

/* If the binary operation has first argument @i, fold to @i. */
static bool fold_ix_to_i(OptContext *ctx, TCGOp *op, uint64_t i)
{
    if (arg_is_const_val(op->args[1], i)) {
        return tcg_opt_gen_movi(ctx, op, op->args[0], i);
    }
    return false;
}

/* If the binary operation has first argument @i, fold to NOT. */
static bool fold_ix_to_not(OptContext *ctx, TCGOp *op, uint64_t i)
{
    if (arg_is_const_val(op->args[1], i)) {
        return fold_to_not(ctx, op, 2);
    }
    return false;
}

/* If the binary operation has second argument @i, fold to @i. */
static bool fold_xi_to_i(OptContext *ctx, TCGOp *op, uint64_t i)
{
    if (arg_is_const_val(op->args[2], i)) {
        return tcg_opt_gen_movi(ctx, op, op->args[0], i);
    }
    return false;
}

/* If the binary operation has second argument @i, fold to identity. */
static bool fold_xi_to_x(OptContext *ctx, TCGOp *op, uint64_t i)
{
    if (arg_is_const_val(op->args[2], i)) {
        return tcg_opt_gen_mov(ctx, op, op->args[0], op->args[1]);
    }
    return false;
}

/* If the binary operation has second argument @i, fold to NOT. */
static bool fold_xi_to_not(OptContext *ctx, TCGOp *op, uint64_t i)
{
    if (arg_is_const_val(op->args[2], i)) {
        return fold_to_not(ctx, op, 1);
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

/* If the binary operation has both arguments equal, fold to identity. */
static bool fold_xx_to_x(OptContext *ctx, TCGOp *op)
{
    if (args_are_copies(op->args[1], op->args[2])) {
        return tcg_opt_gen_mov(ctx, op, op->args[0], op->args[1]);
    }
    return false;
}

/*
 * These outermost fold_<op> functions are sorted alphabetically.
 *
 * The ordering of the transformations should be:
 *   1) those that produce a constant
 *   2) those that produce a copy
 *   3) those that produce information about the result value.
 */

static bool fold_or(OptContext *ctx, TCGOp *op);
static bool fold_orc(OptContext *ctx, TCGOp *op);
static bool fold_xor(OptContext *ctx, TCGOp *op);

static bool fold_add(OptContext *ctx, TCGOp *op)
{
    if (fold_const2_commutative(ctx, op) ||
        fold_xi_to_x(ctx, op, 0)) {
        return true;
    }
    return finish_folding(ctx, op);
}

/* We cannot as yet do_constant_folding with vectors. */
static bool fold_add_vec(OptContext *ctx, TCGOp *op)
{
    if (fold_commutative(ctx, op) ||
        fold_xi_to_x(ctx, op, 0)) {
        return true;
    }
    return finish_folding(ctx, op);
}

static bool fold_addsub2(OptContext *ctx, TCGOp *op, bool add)
{
    bool a_const = arg_is_const(op->args[2]) && arg_is_const(op->args[3]);
    bool b_const = arg_is_const(op->args[4]) && arg_is_const(op->args[5]);

    if (a_const && b_const) {
        uint64_t al = arg_info(op->args[2])->val;
        uint64_t ah = arg_info(op->args[3])->val;
        uint64_t bl = arg_info(op->args[4])->val;
        uint64_t bh = arg_info(op->args[5])->val;
        TCGArg rl, rh;
        TCGOp *op2;

        if (ctx->type == TCG_TYPE_I32) {
            uint64_t a = deposit64(al, 32, 32, ah);
            uint64_t b = deposit64(bl, 32, 32, bh);

            if (add) {
                a += b;
            } else {
                a -= b;
            }

            al = sextract64(a, 0, 32);
            ah = sextract64(a, 32, 32);
        } else {
            Int128 a = int128_make128(al, ah);
            Int128 b = int128_make128(bl, bh);

            if (add) {
                a = int128_add(a, b);
            } else {
                a = int128_sub(a, b);
            }

            al = int128_getlo(a);
            ah = int128_gethi(a);
        }

        rl = op->args[0];
        rh = op->args[1];

        /* The proper opcode is supplied by tcg_opt_gen_mov. */
        op2 = tcg_op_insert_before(ctx->tcg, op, 0, 2);

        tcg_opt_gen_movi(ctx, op, rl, al);
        tcg_opt_gen_movi(ctx, op2, rh, ah);
        return true;
    }

    /* Fold sub2 r,x,i to add2 r,x,-i */
    if (!add && b_const) {
        uint64_t bl = arg_info(op->args[4])->val;
        uint64_t bh = arg_info(op->args[5])->val;

        /* Negate the two parts without assembling and disassembling. */
        bl = -bl;
        bh = ~bh + !bl;

        op->opc = (ctx->type == TCG_TYPE_I32
                   ? INDEX_op_add2_i32 : INDEX_op_add2_i64);
        op->args[4] = arg_new_constant(ctx, bl);
        op->args[5] = arg_new_constant(ctx, bh);
    }
    return finish_folding(ctx, op);
}

static bool fold_add2(OptContext *ctx, TCGOp *op)
{
    /* Note that the high and low parts may be independently swapped. */
    swap_commutative(op->args[0], &op->args[2], &op->args[4]);
    swap_commutative(op->args[1], &op->args[3], &op->args[5]);

    return fold_addsub2(ctx, op, true);
}

static bool fold_and(OptContext *ctx, TCGOp *op)
{
    uint64_t z1, z2, z_mask, s_mask;
    TempOptInfo *t1, *t2;

    if (fold_const2_commutative(ctx, op) ||
        fold_xi_to_i(ctx, op, 0) ||
        fold_xi_to_x(ctx, op, -1) ||
        fold_xx_to_x(ctx, op)) {
        return true;
    }

    t1 = arg_info(op->args[1]);
    t2 = arg_info(op->args[2]);
    z1 = t1->z_mask;
    z2 = t2->z_mask;

    /*
     * Known-zeros does not imply known-ones.  Therefore unless
     * arg2 is constant, we can't infer affected bits from it.
     */
    if (ti_is_const(t2) && fold_affected_mask(ctx, op, z1 & ~z2)) {
        return true;
    }

    z_mask = z1 & z2;

    /*
     * Sign repetitions are perforce all identical, whether they are 1 or 0.
     * Bitwise operations preserve the relative quantity of the repetitions.
     */
    s_mask = t1->s_mask & t2->s_mask;

    return fold_masks_zs(ctx, op, z_mask, s_mask);
}

static bool fold_andc(OptContext *ctx, TCGOp *op)
{
    uint64_t z_mask, s_mask;
    TempOptInfo *t1, *t2;

    if (fold_const2(ctx, op) ||
        fold_xx_to_i(ctx, op, 0) ||
        fold_xi_to_x(ctx, op, 0) ||
        fold_ix_to_not(ctx, op, -1)) {
        return true;
    }

    t1 = arg_info(op->args[1]);
    t2 = arg_info(op->args[2]);
    z_mask = t1->z_mask;

    /*
     * Known-zeros does not imply known-ones.  Therefore unless
     * arg2 is constant, we can't infer anything from it.
     */
    if (ti_is_const(t2)) {
        uint64_t v2 = ti_const_val(t2);
        if (fold_affected_mask(ctx, op, z_mask & v2)) {
            return true;
        }
        z_mask &= ~v2;
    }

    s_mask = t1->s_mask & t2->s_mask;
    return fold_masks_zs(ctx, op, z_mask, s_mask);
}

static bool fold_bitsel_vec(OptContext *ctx, TCGOp *op)
{
    /* If true and false values are the same, eliminate the cmp. */
    if (args_are_copies(op->args[2], op->args[3])) {
        return tcg_opt_gen_mov(ctx, op, op->args[0], op->args[2]);
    }

    if (arg_is_const(op->args[2]) && arg_is_const(op->args[3])) {
        uint64_t tv = arg_info(op->args[2])->val;
        uint64_t fv = arg_info(op->args[3])->val;

        if (tv == -1 && fv == 0) {
            return tcg_opt_gen_mov(ctx, op, op->args[0], op->args[1]);
        }
        if (tv == 0 && fv == -1) {
            if (TCG_TARGET_HAS_not_vec) {
                op->opc = INDEX_op_not_vec;
                return fold_not(ctx, op);
            } else {
                op->opc = INDEX_op_xor_vec;
                op->args[2] = arg_new_constant(ctx, -1);
                return fold_xor(ctx, op);
            }
        }
    }
    if (arg_is_const(op->args[2])) {
        uint64_t tv = arg_info(op->args[2])->val;
        if (tv == -1) {
            op->opc = INDEX_op_or_vec;
            op->args[2] = op->args[3];
            return fold_or(ctx, op);
        }
        if (tv == 0 && TCG_TARGET_HAS_andc_vec) {
            op->opc = INDEX_op_andc_vec;
            op->args[2] = op->args[1];
            op->args[1] = op->args[3];
            return fold_andc(ctx, op);
        }
    }
    if (arg_is_const(op->args[3])) {
        uint64_t fv = arg_info(op->args[3])->val;
        if (fv == 0) {
            op->opc = INDEX_op_and_vec;
            return fold_and(ctx, op);
        }
        if (fv == -1 && TCG_TARGET_HAS_orc_vec) {
            op->opc = INDEX_op_orc_vec;
            op->args[2] = op->args[1];
            op->args[1] = op->args[3];
            return fold_orc(ctx, op);
        }
    }
    return finish_folding(ctx, op);
}

static bool fold_brcond(OptContext *ctx, TCGOp *op)
{
    int i = do_constant_folding_cond1(ctx, op, NO_DEST, &op->args[0],
                                      &op->args[1], &op->args[2]);
    if (i == 0) {
        tcg_op_remove(ctx->tcg, op);
        return true;
    }
    if (i > 0) {
        op->opc = INDEX_op_br;
        op->args[0] = op->args[3];
        finish_ebb(ctx);
    } else {
        finish_bb(ctx);
    }
    return true;
}

static bool fold_brcond2(OptContext *ctx, TCGOp *op)
{
    TCGCond cond;
    TCGArg label;
    int i, inv = 0;

    i = do_constant_folding_cond2(ctx, op, &op->args[0]);
    cond = op->args[4];
    label = op->args[5];
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
        if (arg_is_const_val(op->args[2], 0) &&
            arg_is_const_val(op->args[3], 0)) {
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
        i = do_constant_folding_cond(TCG_TYPE_I32, op->args[0],
                                     op->args[2], cond);
        switch (i ^ inv) {
        case 0:
            goto do_brcond_const;
        case 1:
            goto do_brcond_high;
        }

        i = do_constant_folding_cond(TCG_TYPE_I32, op->args[1],
                                     op->args[3], cond);
        switch (i ^ inv) {
        case 0:
            goto do_brcond_const;
        case 1:
            goto do_brcond_low;
        }
        break;

    case TCG_COND_TSTEQ:
    case TCG_COND_TSTNE:
        if (arg_is_const_val(op->args[2], 0)) {
            goto do_brcond_high;
        }
        if (arg_is_const_val(op->args[3], 0)) {
            goto do_brcond_low;
        }
        break;

    default:
        break;

    do_brcond_low:
        op->opc = INDEX_op_brcond_i32;
        op->args[1] = op->args[2];
        op->args[2] = cond;
        op->args[3] = label;
        return fold_brcond(ctx, op);

    do_brcond_high:
        op->opc = INDEX_op_brcond_i32;
        op->args[0] = op->args[1];
        op->args[1] = op->args[3];
        op->args[2] = cond;
        op->args[3] = label;
        return fold_brcond(ctx, op);

    do_brcond_const:
        if (i == 0) {
            tcg_op_remove(ctx->tcg, op);
            return true;
        }
        op->opc = INDEX_op_br;
        op->args[0] = label;
        finish_ebb(ctx);
        return true;
    }

    finish_bb(ctx);
    return true;
}

static bool fold_bswap(OptContext *ctx, TCGOp *op)
{
    uint64_t z_mask, s_mask, sign;
    TempOptInfo *t1 = arg_info(op->args[1]);

    if (ti_is_const(t1)) {
        return tcg_opt_gen_movi(ctx, op, op->args[0],
                                do_constant_folding(op->opc, ctx->type,
                                                    ti_const_val(t1),
                                                    op->args[2]));
    }

    z_mask = t1->z_mask;
    switch (op->opc) {
    case INDEX_op_bswap16_i32:
    case INDEX_op_bswap16_i64:
        z_mask = bswap16(z_mask);
        sign = INT16_MIN;
        break;
    case INDEX_op_bswap32_i32:
    case INDEX_op_bswap32_i64:
        z_mask = bswap32(z_mask);
        sign = INT32_MIN;
        break;
    case INDEX_op_bswap64_i64:
        z_mask = bswap64(z_mask);
        sign = INT64_MIN;
        break;
    default:
        g_assert_not_reached();
    }

    s_mask = 0;
    switch (op->args[2] & (TCG_BSWAP_OZ | TCG_BSWAP_OS)) {
    case TCG_BSWAP_OZ:
        break;
    case TCG_BSWAP_OS:
        /* If the sign bit may be 1, force all the bits above to 1. */
        if (z_mask & sign) {
            z_mask |= sign;
        }
        /* The value and therefore s_mask is explicitly sign-extended. */
        s_mask = sign;
        break;
    default:
        /* The high bits are undefined: force all bits above the sign to 1. */
        z_mask |= sign << 1;
        break;
    }

    return fold_masks_zs(ctx, op, z_mask, s_mask);
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
                reset_ts(ctx, &ctx->tcg->temps[i]);
            }
        }
    }

    /* If the function has side effects, reset mem data. */
    if (!(flags & TCG_CALL_NO_SIDE_EFFECTS)) {
        remove_mem_copy_all(ctx);
    }

    /* Reset temp data for outputs. */
    for (i = 0; i < nb_oargs; i++) {
        reset_temp(ctx, op->args[i]);
    }

    /* Stop optimizing MB across calls. */
    ctx->prev_mb = NULL;
    return true;
}

static bool fold_cmp_vec(OptContext *ctx, TCGOp *op)
{
    /* Canonicalize the comparison to put immediate second. */
    if (swap_commutative(NO_DEST, &op->args[1], &op->args[2])) {
        op->args[3] = tcg_swap_cond(op->args[3]);
    }
    return finish_folding(ctx, op);
}

static bool fold_cmpsel_vec(OptContext *ctx, TCGOp *op)
{
    /* If true and false values are the same, eliminate the cmp. */
    if (args_are_copies(op->args[3], op->args[4])) {
        return tcg_opt_gen_mov(ctx, op, op->args[0], op->args[3]);
    }

    /* Canonicalize the comparison to put immediate second. */
    if (swap_commutative(NO_DEST, &op->args[1], &op->args[2])) {
        op->args[5] = tcg_swap_cond(op->args[5]);
    }
    /*
     * Canonicalize the "false" input reg to match the destination,
     * so that the tcg backend can implement "move if true".
     */
    if (swap_commutative(op->args[0], &op->args[4], &op->args[3])) {
        op->args[5] = tcg_invert_cond(op->args[5]);
    }
    return finish_folding(ctx, op);
}

static bool fold_count_zeros(OptContext *ctx, TCGOp *op)
{
    uint64_t z_mask, s_mask;
    TempOptInfo *t1 = arg_info(op->args[1]);
    TempOptInfo *t2 = arg_info(op->args[2]);

    if (ti_is_const(t1)) {
        uint64_t t = ti_const_val(t1);

        if (t != 0) {
            t = do_constant_folding(op->opc, ctx->type, t, 0);
            return tcg_opt_gen_movi(ctx, op, op->args[0], t);
        }
        return tcg_opt_gen_mov(ctx, op, op->args[0], op->args[2]);
    }

    switch (ctx->type) {
    case TCG_TYPE_I32:
        z_mask = 31;
        break;
    case TCG_TYPE_I64:
        z_mask = 63;
        break;
    default:
        g_assert_not_reached();
    }
    s_mask = ~z_mask;
    z_mask |= t2->z_mask;
    s_mask &= t2->s_mask;

    return fold_masks_zs(ctx, op, z_mask, s_mask);
}

static bool fold_ctpop(OptContext *ctx, TCGOp *op)
{
    uint64_t z_mask;

    if (fold_const1(ctx, op)) {
        return true;
    }

    switch (ctx->type) {
    case TCG_TYPE_I32:
        z_mask = 32 | 31;
        break;
    case TCG_TYPE_I64:
        z_mask = 64 | 63;
        break;
    default:
        g_assert_not_reached();
    }
    return fold_masks_z(ctx, op, z_mask);
}

static bool fold_deposit(OptContext *ctx, TCGOp *op)
{
    TempOptInfo *t1 = arg_info(op->args[1]);
    TempOptInfo *t2 = arg_info(op->args[2]);
    int ofs = op->args[3];
    int len = op->args[4];
    int width;
    TCGOpcode and_opc;
    uint64_t z_mask, s_mask;

    if (ti_is_const(t1) && ti_is_const(t2)) {
        return tcg_opt_gen_movi(ctx, op, op->args[0],
                                deposit64(ti_const_val(t1), ofs, len,
                                          ti_const_val(t2)));
    }

    switch (ctx->type) {
    case TCG_TYPE_I32:
        and_opc = INDEX_op_and_i32;
        width = 32;
        break;
    case TCG_TYPE_I64:
        and_opc = INDEX_op_and_i64;
        width = 64;
        break;
    default:
        g_assert_not_reached();
    }

    /* Inserting a value into zero at offset 0. */
    if (ti_is_const_val(t1, 0) && ofs == 0) {
        uint64_t mask = MAKE_64BIT_MASK(0, len);

        op->opc = and_opc;
        op->args[1] = op->args[2];
        op->args[2] = arg_new_constant(ctx, mask);
        return fold_and(ctx, op);
    }

    /* Inserting zero into a value. */
    if (ti_is_const_val(t2, 0)) {
        uint64_t mask = deposit64(-1, ofs, len, 0);

        op->opc = and_opc;
        op->args[2] = arg_new_constant(ctx, mask);
        return fold_and(ctx, op);
    }

    /* The s_mask from the top portion of the deposit is still valid. */
    if (ofs + len == width) {
        s_mask = t2->s_mask << ofs;
    } else {
        s_mask = t1->s_mask & ~MAKE_64BIT_MASK(0, ofs + len);
    }

    z_mask = deposit64(t1->z_mask, ofs, len, t2->z_mask);
    return fold_masks_zs(ctx, op, z_mask, s_mask);
}

static bool fold_divide(OptContext *ctx, TCGOp *op)
{
    if (fold_const2(ctx, op) ||
        fold_xi_to_x(ctx, op, 1)) {
        return true;
    }
    return finish_folding(ctx, op);
}

static bool fold_dup(OptContext *ctx, TCGOp *op)
{
    if (arg_is_const(op->args[1])) {
        uint64_t t = arg_info(op->args[1])->val;
        t = dup_const(TCGOP_VECE(op), t);
        return tcg_opt_gen_movi(ctx, op, op->args[0], t);
    }
    return finish_folding(ctx, op);
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
    return finish_folding(ctx, op);
}

static bool fold_eqv(OptContext *ctx, TCGOp *op)
{
    uint64_t s_mask;

    if (fold_const2_commutative(ctx, op) ||
        fold_xi_to_x(ctx, op, -1) ||
        fold_xi_to_not(ctx, op, 0)) {
        return true;
    }

    s_mask = arg_info(op->args[1])->s_mask
           & arg_info(op->args[2])->s_mask;
    return fold_masks_s(ctx, op, s_mask);
}

static bool fold_extract(OptContext *ctx, TCGOp *op)
{
    uint64_t z_mask_old, z_mask;
    TempOptInfo *t1 = arg_info(op->args[1]);
    int pos = op->args[2];
    int len = op->args[3];

    if (ti_is_const(t1)) {
        return tcg_opt_gen_movi(ctx, op, op->args[0],
                                extract64(ti_const_val(t1), pos, len));
    }

    z_mask_old = t1->z_mask;
    z_mask = extract64(z_mask_old, pos, len);
    if (pos == 0 && fold_affected_mask(ctx, op, z_mask_old ^ z_mask)) {
        return true;
    }

    return fold_masks_z(ctx, op, z_mask);
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
            v2 = (uint64_t)((int32_t)v2 << (32 - shr));
        }
        return tcg_opt_gen_movi(ctx, op, op->args[0], v1 | v2);
    }
    return finish_folding(ctx, op);
}

static bool fold_exts(OptContext *ctx, TCGOp *op)
{
    uint64_t s_mask_old, s_mask, z_mask;
    bool type_change = false;
    TempOptInfo *t1;

    if (fold_const1(ctx, op)) {
        return true;
    }

    t1 = arg_info(op->args[1]);
    z_mask = t1->z_mask;
    s_mask = t1->s_mask;
    s_mask_old = s_mask;

    switch (op->opc) {
    CASE_OP_32_64(ext8s):
        s_mask |= INT8_MIN;
        z_mask = (int8_t)z_mask;
        break;
    CASE_OP_32_64(ext16s):
        s_mask |= INT16_MIN;
        z_mask = (int16_t)z_mask;
        break;
    case INDEX_op_ext_i32_i64:
        type_change = true;
        QEMU_FALLTHROUGH;
    case INDEX_op_ext32s_i64:
        s_mask |= INT32_MIN;
        z_mask = (int32_t)z_mask;
        break;
    default:
        g_assert_not_reached();
    }

    if (!type_change && fold_affected_mask(ctx, op, s_mask & ~s_mask_old)) {
        return true;
    }

    return fold_masks_zs(ctx, op, z_mask, s_mask);
}

static bool fold_extu(OptContext *ctx, TCGOp *op)
{
    uint64_t z_mask_old, z_mask;
    bool type_change = false;

    if (fold_const1(ctx, op)) {
        return true;
    }

    z_mask_old = z_mask = arg_info(op->args[1])->z_mask;

    switch (op->opc) {
    CASE_OP_32_64(ext8u):
        z_mask = (uint8_t)z_mask;
        break;
    CASE_OP_32_64(ext16u):
        z_mask = (uint16_t)z_mask;
        break;
    case INDEX_op_extrl_i64_i32:
    case INDEX_op_extu_i32_i64:
        type_change = true;
        QEMU_FALLTHROUGH;
    case INDEX_op_ext32u_i64:
        z_mask = (uint32_t)z_mask;
        break;
    case INDEX_op_extrh_i64_i32:
        type_change = true;
        z_mask >>= 32;
        break;
    default:
        g_assert_not_reached();
    }

    if (!type_change && fold_affected_mask(ctx, op, z_mask_old ^ z_mask)) {
        return true;
    }

    return fold_masks_z(ctx, op, z_mask);
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
    uint64_t z_mask, s_mask;
    TempOptInfo *tt, *ft;
    int i;

    /* If true and false values are the same, eliminate the cmp. */
    if (args_are_copies(op->args[3], op->args[4])) {
        return tcg_opt_gen_mov(ctx, op, op->args[0], op->args[3]);
    }

    /*
     * Canonicalize the "false" input reg to match the destination reg so
     * that the tcg backend can implement a "move if true" operation.
     */
    if (swap_commutative(op->args[0], &op->args[4], &op->args[3])) {
        op->args[5] = tcg_invert_cond(op->args[5]);
    }

    i = do_constant_folding_cond1(ctx, op, NO_DEST, &op->args[1],
                                  &op->args[2], &op->args[5]);
    if (i >= 0) {
        return tcg_opt_gen_mov(ctx, op, op->args[0], op->args[4 - i]);
    }

    tt = arg_info(op->args[3]);
    ft = arg_info(op->args[4]);
    z_mask = tt->z_mask | ft->z_mask;
    s_mask = tt->s_mask & ft->s_mask;

    if (ti_is_const(tt) && ti_is_const(ft)) {
        uint64_t tv = ti_const_val(tt);
        uint64_t fv = ti_const_val(ft);
        TCGOpcode opc, negopc = 0;
        TCGCond cond = op->args[5];

        switch (ctx->type) {
        case TCG_TYPE_I32:
            opc = INDEX_op_setcond_i32;
            if (TCG_TARGET_HAS_negsetcond_i32) {
                negopc = INDEX_op_negsetcond_i32;
            }
            tv = (int32_t)tv;
            fv = (int32_t)fv;
            break;
        case TCG_TYPE_I64:
            opc = INDEX_op_setcond_i64;
            if (TCG_TARGET_HAS_negsetcond_i64) {
                negopc = INDEX_op_negsetcond_i64;
            }
            break;
        default:
            g_assert_not_reached();
        }

        if (tv == 1 && fv == 0) {
            op->opc = opc;
            op->args[3] = cond;
        } else if (fv == 1 && tv == 0) {
            op->opc = opc;
            op->args[3] = tcg_invert_cond(cond);
        } else if (negopc) {
            if (tv == -1 && fv == 0) {
                op->opc = negopc;
                op->args[3] = cond;
            } else if (fv == -1 && tv == 0) {
                op->opc = negopc;
                op->args[3] = tcg_invert_cond(cond);
            }
        }
    }

    return fold_masks_zs(ctx, op, z_mask, s_mask);
}

static bool fold_mul(OptContext *ctx, TCGOp *op)
{
    if (fold_const2(ctx, op) ||
        fold_xi_to_i(ctx, op, 0) ||
        fold_xi_to_x(ctx, op, 1)) {
        return true;
    }
    return finish_folding(ctx, op);
}

static bool fold_mul_highpart(OptContext *ctx, TCGOp *op)
{
    if (fold_const2_commutative(ctx, op) ||
        fold_xi_to_i(ctx, op, 0)) {
        return true;
    }
    return finish_folding(ctx, op);
}

static bool fold_multiply2(OptContext *ctx, TCGOp *op)
{
    swap_commutative(op->args[0], &op->args[2], &op->args[3]);

    if (arg_is_const(op->args[2]) && arg_is_const(op->args[3])) {
        uint64_t a = arg_info(op->args[2])->val;
        uint64_t b = arg_info(op->args[3])->val;
        uint64_t h, l;
        TCGArg rl, rh;
        TCGOp *op2;

        switch (op->opc) {
        case INDEX_op_mulu2_i32:
            l = (uint64_t)(uint32_t)a * (uint32_t)b;
            h = (int32_t)(l >> 32);
            l = (int32_t)l;
            break;
        case INDEX_op_muls2_i32:
            l = (int64_t)(int32_t)a * (int32_t)b;
            h = l >> 32;
            l = (int32_t)l;
            break;
        case INDEX_op_mulu2_i64:
            mulu64(&l, &h, a, b);
            break;
        case INDEX_op_muls2_i64:
            muls64(&l, &h, a, b);
            break;
        default:
            g_assert_not_reached();
        }

        rl = op->args[0];
        rh = op->args[1];

        /* The proper opcode is supplied by tcg_opt_gen_mov. */
        op2 = tcg_op_insert_before(ctx->tcg, op, 0, 2);

        tcg_opt_gen_movi(ctx, op, rl, l);
        tcg_opt_gen_movi(ctx, op2, rh, h);
        return true;
    }
    return finish_folding(ctx, op);
}

static bool fold_nand(OptContext *ctx, TCGOp *op)
{
    uint64_t s_mask;

    if (fold_const2_commutative(ctx, op) ||
        fold_xi_to_not(ctx, op, -1)) {
        return true;
    }

    s_mask = arg_info(op->args[1])->s_mask
           & arg_info(op->args[2])->s_mask;
    return fold_masks_s(ctx, op, s_mask);
}

static bool fold_neg_no_const(OptContext *ctx, TCGOp *op)
{
    /* Set to 1 all bits to the left of the rightmost.  */
    uint64_t z_mask = arg_info(op->args[1])->z_mask;
    z_mask = -(z_mask & -z_mask);

    return fold_masks_z(ctx, op, z_mask);
}

static bool fold_neg(OptContext *ctx, TCGOp *op)
{
    return fold_const1(ctx, op) || fold_neg_no_const(ctx, op);
}

static bool fold_nor(OptContext *ctx, TCGOp *op)
{
    uint64_t s_mask;

    if (fold_const2_commutative(ctx, op) ||
        fold_xi_to_not(ctx, op, 0)) {
        return true;
    }

    s_mask = arg_info(op->args[1])->s_mask
           & arg_info(op->args[2])->s_mask;
    return fold_masks_s(ctx, op, s_mask);
}

static bool fold_not(OptContext *ctx, TCGOp *op)
{
    if (fold_const1(ctx, op)) {
        return true;
    }
    return fold_masks_s(ctx, op, arg_info(op->args[1])->s_mask);
}

static bool fold_or(OptContext *ctx, TCGOp *op)
{
    uint64_t z_mask, s_mask;
    TempOptInfo *t1, *t2;

    if (fold_const2_commutative(ctx, op) ||
        fold_xi_to_x(ctx, op, 0) ||
        fold_xx_to_x(ctx, op)) {
        return true;
    }

    t1 = arg_info(op->args[1]);
    t2 = arg_info(op->args[2]);
    z_mask = t1->z_mask | t2->z_mask;
    s_mask = t1->s_mask & t2->s_mask;
    return fold_masks_zs(ctx, op, z_mask, s_mask);
}

static bool fold_orc(OptContext *ctx, TCGOp *op)
{
    uint64_t s_mask;

    if (fold_const2(ctx, op) ||
        fold_xx_to_i(ctx, op, -1) ||
        fold_xi_to_x(ctx, op, -1) ||
        fold_ix_to_not(ctx, op, 0)) {
        return true;
    }

    s_mask = arg_info(op->args[1])->s_mask
           & arg_info(op->args[2])->s_mask;
    return fold_masks_s(ctx, op, s_mask);
}

static bool fold_qemu_ld_1reg(OptContext *ctx, TCGOp *op)
{
    const TCGOpDef *def = &tcg_op_defs[op->opc];
    MemOpIdx oi = op->args[def->nb_oargs + def->nb_iargs];
    MemOp mop = get_memop(oi);
    int width = 8 * memop_size(mop);
    uint64_t z_mask = -1, s_mask = 0;

    if (width < 64) {
        if (mop & MO_SIGN) {
            s_mask = MAKE_64BIT_MASK(width - 1, 64 - (width - 1));
        } else {
            z_mask = MAKE_64BIT_MASK(0, width);
        }
    }

    /* Opcodes that touch guest memory stop the mb optimization.  */
    ctx->prev_mb = NULL;

    return fold_masks_zs(ctx, op, z_mask, s_mask);
}

static bool fold_qemu_ld_2reg(OptContext *ctx, TCGOp *op)
{
    /* Opcodes that touch guest memory stop the mb optimization.  */
    ctx->prev_mb = NULL;
    return finish_folding(ctx, op);
}

static bool fold_qemu_st(OptContext *ctx, TCGOp *op)
{
    /* Opcodes that touch guest memory stop the mb optimization.  */
    ctx->prev_mb = NULL;
    return true;
}

static bool fold_remainder(OptContext *ctx, TCGOp *op)
{
    if (fold_const2(ctx, op) ||
        fold_xx_to_i(ctx, op, 0)) {
        return true;
    }
    return finish_folding(ctx, op);
}

/* Return 1 if finished, -1 if simplified, 0 if unchanged. */
static int fold_setcond_zmask(OptContext *ctx, TCGOp *op, bool neg)
{
    uint64_t a_zmask, b_val;
    TCGCond cond;

    if (!arg_is_const(op->args[2])) {
        return false;
    }

    a_zmask = arg_info(op->args[1])->z_mask;
    b_val = arg_info(op->args[2])->val;
    cond = op->args[3];

    if (ctx->type == TCG_TYPE_I32) {
        a_zmask = (uint32_t)a_zmask;
        b_val = (uint32_t)b_val;
    }

    /*
     * A with only low bits set vs B with high bits set means that A < B.
     */
    if (a_zmask < b_val) {
        bool inv = false;

        switch (cond) {
        case TCG_COND_NE:
        case TCG_COND_LEU:
        case TCG_COND_LTU:
            inv = true;
            /* fall through */
        case TCG_COND_GTU:
        case TCG_COND_GEU:
        case TCG_COND_EQ:
            return tcg_opt_gen_movi(ctx, op, op->args[0], neg ? -inv : inv);
        default:
            break;
        }
    }

    /*
     * A with only lsb set is already boolean.
     */
    if (a_zmask <= 1) {
        bool convert = false;
        bool inv = false;

        switch (cond) {
        case TCG_COND_EQ:
            inv = true;
            /* fall through */
        case TCG_COND_NE:
            convert = (b_val == 0);
            break;
        case TCG_COND_LTU:
        case TCG_COND_TSTEQ:
            inv = true;
            /* fall through */
        case TCG_COND_GEU:
        case TCG_COND_TSTNE:
            convert = (b_val == 1);
            break;
        default:
            break;
        }
        if (convert) {
            TCGOpcode add_opc, xor_opc, neg_opc;

            if (!inv && !neg) {
                return tcg_opt_gen_mov(ctx, op, op->args[0], op->args[1]);
            }

            switch (ctx->type) {
            case TCG_TYPE_I32:
                add_opc = INDEX_op_add_i32;
                neg_opc = INDEX_op_neg_i32;
                xor_opc = INDEX_op_xor_i32;
                break;
            case TCG_TYPE_I64:
                add_opc = INDEX_op_add_i64;
                neg_opc = INDEX_op_neg_i64;
                xor_opc = INDEX_op_xor_i64;
                break;
            default:
                g_assert_not_reached();
            }

            if (!inv) {
                op->opc = neg_opc;
            } else if (neg) {
                op->opc = add_opc;
                op->args[2] = arg_new_constant(ctx, -1);
            } else {
                op->opc = xor_opc;
                op->args[2] = arg_new_constant(ctx, 1);
            }
            return -1;
        }
    }
    return 0;
}

static void fold_setcond_tst_pow2(OptContext *ctx, TCGOp *op, bool neg)
{
    TCGOpcode and_opc, sub_opc, xor_opc, neg_opc, shr_opc;
    TCGOpcode uext_opc = 0, sext_opc = 0;
    TCGCond cond = op->args[3];
    TCGArg ret, src1, src2;
    TCGOp *op2;
    uint64_t val;
    int sh;
    bool inv;

    if (!is_tst_cond(cond) || !arg_is_const(op->args[2])) {
        return;
    }

    src2 = op->args[2];
    val = arg_info(src2)->val;
    if (!is_power_of_2(val)) {
        return;
    }
    sh = ctz64(val);

    switch (ctx->type) {
    case TCG_TYPE_I32:
        and_opc = INDEX_op_and_i32;
        sub_opc = INDEX_op_sub_i32;
        xor_opc = INDEX_op_xor_i32;
        shr_opc = INDEX_op_shr_i32;
        neg_opc = INDEX_op_neg_i32;
        if (TCG_TARGET_extract_valid(TCG_TYPE_I32, sh, 1)) {
            uext_opc = INDEX_op_extract_i32;
        }
        if (TCG_TARGET_sextract_valid(TCG_TYPE_I32, sh, 1)) {
            sext_opc = INDEX_op_sextract_i32;
        }
        break;
    case TCG_TYPE_I64:
        and_opc = INDEX_op_and_i64;
        sub_opc = INDEX_op_sub_i64;
        xor_opc = INDEX_op_xor_i64;
        shr_opc = INDEX_op_shr_i64;
        neg_opc = INDEX_op_neg_i64;
        if (TCG_TARGET_extract_valid(TCG_TYPE_I64, sh, 1)) {
            uext_opc = INDEX_op_extract_i64;
        }
        if (TCG_TARGET_sextract_valid(TCG_TYPE_I64, sh, 1)) {
            sext_opc = INDEX_op_sextract_i64;
        }
        break;
    default:
        g_assert_not_reached();
    }

    ret = op->args[0];
    src1 = op->args[1];
    inv = cond == TCG_COND_TSTEQ;

    if (sh && sext_opc && neg && !inv) {
        op->opc = sext_opc;
        op->args[1] = src1;
        op->args[2] = sh;
        op->args[3] = 1;
        return;
    } else if (sh && uext_opc) {
        op->opc = uext_opc;
        op->args[1] = src1;
        op->args[2] = sh;
        op->args[3] = 1;
    } else {
        if (sh) {
            op2 = tcg_op_insert_before(ctx->tcg, op, shr_opc, 3);
            op2->args[0] = ret;
            op2->args[1] = src1;
            op2->args[2] = arg_new_constant(ctx, sh);
            src1 = ret;
        }
        op->opc = and_opc;
        op->args[1] = src1;
        op->args[2] = arg_new_constant(ctx, 1);
    }

    if (neg && inv) {
        op2 = tcg_op_insert_after(ctx->tcg, op, sub_opc, 3);
        op2->args[0] = ret;
        op2->args[1] = ret;
        op2->args[2] = arg_new_constant(ctx, 1);
    } else if (inv) {
        op2 = tcg_op_insert_after(ctx->tcg, op, xor_opc, 3);
        op2->args[0] = ret;
        op2->args[1] = ret;
        op2->args[2] = arg_new_constant(ctx, 1);
    } else if (neg) {
        op2 = tcg_op_insert_after(ctx->tcg, op, neg_opc, 2);
        op2->args[0] = ret;
        op2->args[1] = ret;
    }
}

static bool fold_setcond(OptContext *ctx, TCGOp *op)
{
    int i = do_constant_folding_cond1(ctx, op, op->args[0], &op->args[1],
                                      &op->args[2], &op->args[3]);
    if (i >= 0) {
        return tcg_opt_gen_movi(ctx, op, op->args[0], i);
    }

    i = fold_setcond_zmask(ctx, op, false);
    if (i > 0) {
        return true;
    }
    if (i == 0) {
        fold_setcond_tst_pow2(ctx, op, false);
    }

    return fold_masks_z(ctx, op, 1);
}

static bool fold_negsetcond(OptContext *ctx, TCGOp *op)
{
    int i = do_constant_folding_cond1(ctx, op, op->args[0], &op->args[1],
                                      &op->args[2], &op->args[3]);
    if (i >= 0) {
        return tcg_opt_gen_movi(ctx, op, op->args[0], -i);
    }

    i = fold_setcond_zmask(ctx, op, true);
    if (i > 0) {
        return true;
    }
    if (i == 0) {
        fold_setcond_tst_pow2(ctx, op, true);
    }

    /* Value is {0,-1} so all bits are repetitions of the sign. */
    return fold_masks_s(ctx, op, -1);
}

static bool fold_setcond2(OptContext *ctx, TCGOp *op)
{
    TCGCond cond;
    int i, inv = 0;

    i = do_constant_folding_cond2(ctx, op, &op->args[1]);
    cond = op->args[5];
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
        if (arg_is_const_val(op->args[3], 0) &&
            arg_is_const_val(op->args[4], 0)) {
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
        i = do_constant_folding_cond(TCG_TYPE_I32, op->args[1],
                                     op->args[3], cond);
        switch (i ^ inv) {
        case 0:
            goto do_setcond_const;
        case 1:
            goto do_setcond_high;
        }

        i = do_constant_folding_cond(TCG_TYPE_I32, op->args[2],
                                     op->args[4], cond);
        switch (i ^ inv) {
        case 0:
            goto do_setcond_const;
        case 1:
            goto do_setcond_low;
        }
        break;

    case TCG_COND_TSTEQ:
    case TCG_COND_TSTNE:
        if (arg_is_const_val(op->args[3], 0)) {
            goto do_setcond_high;
        }
        if (arg_is_const_val(op->args[4], 0)) {
            goto do_setcond_low;
        }
        break;

    default:
        break;

    do_setcond_low:
        op->args[2] = op->args[3];
        op->args[3] = cond;
        op->opc = INDEX_op_setcond_i32;
        return fold_setcond(ctx, op);

    do_setcond_high:
        op->args[1] = op->args[2];
        op->args[2] = op->args[4];
        op->args[3] = cond;
        op->opc = INDEX_op_setcond_i32;
        return fold_setcond(ctx, op);
    }

    return fold_masks_z(ctx, op, 1);

 do_setcond_const:
    return tcg_opt_gen_movi(ctx, op, op->args[0], i);
}

static bool fold_sextract(OptContext *ctx, TCGOp *op)
{
    uint64_t z_mask, s_mask, s_mask_old;
    TempOptInfo *t1 = arg_info(op->args[1]);
    int pos = op->args[2];
    int len = op->args[3];

    if (ti_is_const(t1)) {
        return tcg_opt_gen_movi(ctx, op, op->args[0],
                                sextract64(ti_const_val(t1), pos, len));
    }

    s_mask_old = t1->s_mask;
    s_mask = s_mask_old >> pos;
    s_mask |= -1ull << (len - 1);

    if (pos == 0 && fold_affected_mask(ctx, op, s_mask & ~s_mask_old)) {
        return true;
    }

    z_mask = sextract64(t1->z_mask, pos, len);
    return fold_masks_zs(ctx, op, z_mask, s_mask);
}

static bool fold_shift(OptContext *ctx, TCGOp *op)
{
    uint64_t s_mask, z_mask;
    TempOptInfo *t1, *t2;

    if (fold_const2(ctx, op) ||
        fold_ix_to_i(ctx, op, 0) ||
        fold_xi_to_x(ctx, op, 0)) {
        return true;
    }

    t1 = arg_info(op->args[1]);
    t2 = arg_info(op->args[2]);
    s_mask = t1->s_mask;
    z_mask = t1->z_mask;

    if (ti_is_const(t2)) {
        int sh = ti_const_val(t2);

        z_mask = do_constant_folding(op->opc, ctx->type, z_mask, sh);
        s_mask = do_constant_folding(op->opc, ctx->type, s_mask, sh);

        return fold_masks_zs(ctx, op, z_mask, s_mask);
    }

    switch (op->opc) {
    CASE_OP_32_64(sar):
        /*
         * Arithmetic right shift will not reduce the number of
         * input sign repetitions.
         */
        return fold_masks_s(ctx, op, s_mask);
    CASE_OP_32_64(shr):
        /*
         * If the sign bit is known zero, then logical right shift
         * will not reduce the number of input sign repetitions.
         */
        if (~z_mask & -s_mask) {
            return fold_masks_s(ctx, op, s_mask);
        }
        break;
    default:
        break;
    }

    return finish_folding(ctx, op);
}

static bool fold_sub_to_neg(OptContext *ctx, TCGOp *op)
{
    TCGOpcode neg_op;
    bool have_neg;

    if (!arg_is_const(op->args[1]) || arg_info(op->args[1])->val != 0) {
        return false;
    }

    switch (ctx->type) {
    case TCG_TYPE_I32:
        neg_op = INDEX_op_neg_i32;
        have_neg = true;
        break;
    case TCG_TYPE_I64:
        neg_op = INDEX_op_neg_i64;
        have_neg = true;
        break;
    case TCG_TYPE_V64:
    case TCG_TYPE_V128:
    case TCG_TYPE_V256:
        neg_op = INDEX_op_neg_vec;
        have_neg = (TCG_TARGET_HAS_neg_vec &&
                    tcg_can_emit_vec_op(neg_op, ctx->type, TCGOP_VECE(op)) > 0);
        break;
    default:
        g_assert_not_reached();
    }
    if (have_neg) {
        op->opc = neg_op;
        op->args[1] = op->args[2];
        return fold_neg_no_const(ctx, op);
    }
    return false;
}

/* We cannot as yet do_constant_folding with vectors. */
static bool fold_sub_vec(OptContext *ctx, TCGOp *op)
{
    if (fold_xx_to_i(ctx, op, 0) ||
        fold_xi_to_x(ctx, op, 0) ||
        fold_sub_to_neg(ctx, op)) {
        return true;
    }
    return finish_folding(ctx, op);
}

static bool fold_sub(OptContext *ctx, TCGOp *op)
{
    if (fold_const2(ctx, op) ||
        fold_xx_to_i(ctx, op, 0) ||
        fold_xi_to_x(ctx, op, 0) ||
        fold_sub_to_neg(ctx, op)) {
        return true;
    }

    /* Fold sub r,x,i to add r,x,-i */
    if (arg_is_const(op->args[2])) {
        uint64_t val = arg_info(op->args[2])->val;

        op->opc = (ctx->type == TCG_TYPE_I32
                   ? INDEX_op_add_i32 : INDEX_op_add_i64);
        op->args[2] = arg_new_constant(ctx, -val);
    }
    return finish_folding(ctx, op);
}

static bool fold_sub2(OptContext *ctx, TCGOp *op)
{
    return fold_addsub2(ctx, op, false);
}

static bool fold_tcg_ld(OptContext *ctx, TCGOp *op)
{
    uint64_t z_mask = -1, s_mask = 0;

    /* We can't do any folding with a load, but we can record bits. */
    switch (op->opc) {
    CASE_OP_32_64(ld8s):
        s_mask = INT8_MIN;
        break;
    CASE_OP_32_64(ld8u):
        z_mask = MAKE_64BIT_MASK(0, 8);
        break;
    CASE_OP_32_64(ld16s):
        s_mask = INT16_MIN;
        break;
    CASE_OP_32_64(ld16u):
        z_mask = MAKE_64BIT_MASK(0, 16);
        break;
    case INDEX_op_ld32s_i64:
        s_mask = INT32_MIN;
        break;
    case INDEX_op_ld32u_i64:
        z_mask = MAKE_64BIT_MASK(0, 32);
        break;
    default:
        g_assert_not_reached();
    }
    return fold_masks_zs(ctx, op, z_mask, s_mask);
}

static bool fold_tcg_ld_memcopy(OptContext *ctx, TCGOp *op)
{
    TCGTemp *dst, *src;
    intptr_t ofs;
    TCGType type;

    if (op->args[1] != tcgv_ptr_arg(tcg_env)) {
        return finish_folding(ctx, op);
    }

    type = ctx->type;
    ofs = op->args[2];
    dst = arg_temp(op->args[0]);
    src = find_mem_copy_for(ctx, type, ofs);
    if (src && src->base_type == type) {
        return tcg_opt_gen_mov(ctx, op, temp_arg(dst), temp_arg(src));
    }

    reset_ts(ctx, dst);
    record_mem_copy(ctx, type, dst, ofs, ofs + tcg_type_size(type) - 1);
    return true;
}

static bool fold_tcg_st(OptContext *ctx, TCGOp *op)
{
    intptr_t ofs = op->args[2];
    intptr_t lm1;

    if (op->args[1] != tcgv_ptr_arg(tcg_env)) {
        remove_mem_copy_all(ctx);
        return true;
    }

    switch (op->opc) {
    CASE_OP_32_64(st8):
        lm1 = 0;
        break;
    CASE_OP_32_64(st16):
        lm1 = 1;
        break;
    case INDEX_op_st32_i64:
    case INDEX_op_st_i32:
        lm1 = 3;
        break;
    case INDEX_op_st_i64:
        lm1 = 7;
        break;
    case INDEX_op_st_vec:
        lm1 = tcg_type_size(ctx->type) - 1;
        break;
    default:
        g_assert_not_reached();
    }
    remove_mem_copy_in(ctx, ofs, ofs + lm1);
    return true;
}

static bool fold_tcg_st_memcopy(OptContext *ctx, TCGOp *op)
{
    TCGTemp *src;
    intptr_t ofs, last;
    TCGType type;

    if (op->args[1] != tcgv_ptr_arg(tcg_env)) {
        return fold_tcg_st(ctx, op);
    }

    src = arg_temp(op->args[0]);
    ofs = op->args[2];
    type = ctx->type;

    /*
     * Eliminate duplicate stores of a constant.
     * This happens frequently when the target ISA zero-extends.
     */
    if (ts_is_const(src)) {
        TCGTemp *prev = find_mem_copy_for(ctx, type, ofs);
        if (src == prev) {
            tcg_op_remove(ctx->tcg, op);
            return true;
        }
    }

    last = ofs + tcg_type_size(type) - 1;
    remove_mem_copy_in(ctx, ofs, last);
    record_mem_copy(ctx, type, src, ofs, last);
    return true;
}

static bool fold_xor(OptContext *ctx, TCGOp *op)
{
    uint64_t z_mask, s_mask;
    TempOptInfo *t1, *t2;

    if (fold_const2_commutative(ctx, op) ||
        fold_xx_to_i(ctx, op, 0) ||
        fold_xi_to_x(ctx, op, 0) ||
        fold_xi_to_not(ctx, op, -1)) {
        return true;
    }

    t1 = arg_info(op->args[1]);
    t2 = arg_info(op->args[2]);
    z_mask = t1->z_mask | t2->z_mask;
    s_mask = t1->s_mask & t2->s_mask;
    return fold_masks_zs(ctx, op, z_mask, s_mask);
}

/* Propagate constants and copies, fold constant expressions. */
void tcg_optimize(TCGContext *s)
{
    int nb_temps, i;
    TCGOp *op, *op_next;
    OptContext ctx = { .tcg = s };

    QSIMPLEQ_INIT(&ctx.mem_free);

    /* Array VALS has an element for each temp.
       If this temp holds a constant then its value is kept in VALS' element.
       If this temp is a copy of other ones then the other copies are
       available through the doubly linked circular list. */

    nb_temps = s->nb_temps;
    for (i = 0; i < nb_temps; ++i) {
        s->temps[i].state_ptr = NULL;
    }

    QTAILQ_FOREACH_SAFE(op, &s->ops, link, op_next) {
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

        /* Pre-compute the type of the operation. */
        ctx.type = TCGOP_TYPE(op);

        /*
         * Process each opcode.
         * Sorted alphabetically by opcode as much as possible.
         */
        switch (opc) {
        CASE_OP_32_64(add):
            done = fold_add(&ctx, op);
            break;
        case INDEX_op_add_vec:
            done = fold_add_vec(&ctx, op);
            break;
        CASE_OP_32_64(add2):
            done = fold_add2(&ctx, op);
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
        CASE_OP_32_64_VEC(eqv):
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
        CASE_OP_32_64(ld8s):
        CASE_OP_32_64(ld8u):
        CASE_OP_32_64(ld16s):
        CASE_OP_32_64(ld16u):
        case INDEX_op_ld32s_i64:
        case INDEX_op_ld32u_i64:
            done = fold_tcg_ld(&ctx, op);
            break;
        case INDEX_op_ld_i32:
        case INDEX_op_ld_i64:
        case INDEX_op_ld_vec:
            done = fold_tcg_ld_memcopy(&ctx, op);
            break;
        CASE_OP_32_64(st8):
        CASE_OP_32_64(st16):
        case INDEX_op_st32_i64:
            done = fold_tcg_st(&ctx, op);
            break;
        case INDEX_op_st_i32:
        case INDEX_op_st_i64:
        case INDEX_op_st_vec:
            done = fold_tcg_st_memcopy(&ctx, op);
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
        CASE_OP_32_64(muls2):
        CASE_OP_32_64(mulu2):
            done = fold_multiply2(&ctx, op);
            break;
        CASE_OP_32_64_VEC(nand):
            done = fold_nand(&ctx, op);
            break;
        CASE_OP_32_64(neg):
            done = fold_neg(&ctx, op);
            break;
        CASE_OP_32_64_VEC(nor):
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
        case INDEX_op_qemu_ld_a32_i32:
        case INDEX_op_qemu_ld_a64_i32:
            done = fold_qemu_ld_1reg(&ctx, op);
            break;
        case INDEX_op_qemu_ld_a32_i64:
        case INDEX_op_qemu_ld_a64_i64:
            if (TCG_TARGET_REG_BITS == 64) {
                done = fold_qemu_ld_1reg(&ctx, op);
                break;
            }
            QEMU_FALLTHROUGH;
        case INDEX_op_qemu_ld_a32_i128:
        case INDEX_op_qemu_ld_a64_i128:
            done = fold_qemu_ld_2reg(&ctx, op);
            break;
        case INDEX_op_qemu_st8_a32_i32:
        case INDEX_op_qemu_st8_a64_i32:
        case INDEX_op_qemu_st_a32_i32:
        case INDEX_op_qemu_st_a64_i32:
        case INDEX_op_qemu_st_a32_i64:
        case INDEX_op_qemu_st_a64_i64:
        case INDEX_op_qemu_st_a32_i128:
        case INDEX_op_qemu_st_a64_i128:
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
        CASE_OP_32_64(negsetcond):
            done = fold_negsetcond(&ctx, op);
            break;
        case INDEX_op_setcond2_i32:
            done = fold_setcond2(&ctx, op);
            break;
        case INDEX_op_cmp_vec:
            done = fold_cmp_vec(&ctx, op);
            break;
        case INDEX_op_cmpsel_vec:
            done = fold_cmpsel_vec(&ctx, op);
            break;
        case INDEX_op_bitsel_vec:
            done = fold_bitsel_vec(&ctx, op);
            break;
        CASE_OP_32_64(sextract):
            done = fold_sextract(&ctx, op);
            break;
        CASE_OP_32_64(sub):
            done = fold_sub(&ctx, op);
            break;
        case INDEX_op_sub_vec:
            done = fold_sub_vec(&ctx, op);
            break;
        CASE_OP_32_64(sub2):
            done = fold_sub2(&ctx, op);
            break;
        CASE_OP_32_64_VEC(xor):
            done = fold_xor(&ctx, op);
            break;
        case INDEX_op_set_label:
        case INDEX_op_br:
        case INDEX_op_exit_tb:
        case INDEX_op_goto_tb:
        case INDEX_op_goto_ptr:
            finish_ebb(&ctx);
            done = true;
            break;
        default:
            done = finish_folding(&ctx, op);
            break;
        }
        tcg_debug_assert(done);
    }
}
