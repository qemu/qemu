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

#include "config.h"

#include <stdlib.h>
#include <stdio.h>

#include "qemu-common.h"
#include "tcg-op.h"

#define CASE_OP_32_64(x)                        \
        glue(glue(case INDEX_op_, x), _i32):    \
        glue(glue(case INDEX_op_, x), _i64)

typedef enum {
    TCG_TEMP_UNDEF = 0,
    TCG_TEMP_CONST,
    TCG_TEMP_COPY,
} tcg_temp_state;

struct tcg_temp_info {
    tcg_temp_state state;
    uint16_t prev_copy;
    uint16_t next_copy;
    tcg_target_ulong val;
    tcg_target_ulong mask;
};

static struct tcg_temp_info temps[TCG_MAX_TEMPS];

/* Reset TEMP's state to TCG_TEMP_UNDEF.  If TEMP only had one copy, remove
   the copy flag from the left temp.  */
static void reset_temp(TCGArg temp)
{
    if (temps[temp].state == TCG_TEMP_COPY) {
        if (temps[temp].prev_copy == temps[temp].next_copy) {
            temps[temps[temp].next_copy].state = TCG_TEMP_UNDEF;
        } else {
            temps[temps[temp].next_copy].prev_copy = temps[temp].prev_copy;
            temps[temps[temp].prev_copy].next_copy = temps[temp].next_copy;
        }
    }
    temps[temp].state = TCG_TEMP_UNDEF;
    temps[temp].mask = -1;
}

/* Reset all temporaries, given that there are NB_TEMPS of them.  */
static void reset_all_temps(int nb_temps)
{
    int i;
    for (i = 0; i < nb_temps; i++) {
        temps[i].state = TCG_TEMP_UNDEF;
        temps[i].mask = -1;
    }
}

static int op_bits(TCGOpcode op)
{
    const TCGOpDef *def = &tcg_op_defs[op];
    return def->flags & TCG_OPF_64BIT ? 64 : 32;
}

static TCGOpcode op_to_mov(TCGOpcode op)
{
    switch (op_bits(op)) {
    case 32:
        return INDEX_op_mov_i32;
    case 64:
        return INDEX_op_mov_i64;
    default:
        fprintf(stderr, "op_to_mov: unexpected return value of "
                "function op_bits.\n");
        tcg_abort();
    }
}

static TCGOpcode op_to_movi(TCGOpcode op)
{
    switch (op_bits(op)) {
    case 32:
        return INDEX_op_movi_i32;
    case 64:
        return INDEX_op_movi_i64;
    default:
        fprintf(stderr, "op_to_movi: unexpected return value of "
                "function op_bits.\n");
        tcg_abort();
    }
}

static TCGArg find_better_copy(TCGContext *s, TCGArg temp)
{
    TCGArg i;

    /* If this is already a global, we can't do better. */
    if (temp < s->nb_globals) {
        return temp;
    }

    /* Search for a global first. */
    for (i = temps[temp].next_copy ; i != temp ; i = temps[i].next_copy) {
        if (i < s->nb_globals) {
            return i;
        }
    }

    /* If it is a temp, search for a temp local. */
    if (!s->temps[temp].temp_local) {
        for (i = temps[temp].next_copy ; i != temp ; i = temps[i].next_copy) {
            if (s->temps[i].temp_local) {
                return i;
            }
        }
    }

    /* Failure to find a better representation, return the same temp. */
    return temp;
}

static bool temps_are_copies(TCGArg arg1, TCGArg arg2)
{
    TCGArg i;

    if (arg1 == arg2) {
        return true;
    }

    if (temps[arg1].state != TCG_TEMP_COPY
        || temps[arg2].state != TCG_TEMP_COPY) {
        return false;
    }

    for (i = temps[arg1].next_copy ; i != arg1 ; i = temps[i].next_copy) {
        if (i == arg2) {
            return true;
        }
    }

    return false;
}

static void tcg_opt_gen_mov(TCGContext *s, int op_index, TCGArg *gen_args,
                            TCGOpcode old_op, TCGArg dst, TCGArg src)
{
    TCGOpcode new_op = op_to_mov(old_op);
    tcg_target_ulong mask;

    s->gen_opc_buf[op_index] = new_op;

    reset_temp(dst);
    mask = temps[src].mask;
    if (TCG_TARGET_REG_BITS > 32 && new_op == INDEX_op_mov_i32) {
        /* High bits of the destination are now garbage.  */
        mask |= ~0xffffffffull;
    }
    temps[dst].mask = mask;

    assert(temps[src].state != TCG_TEMP_CONST);

    if (s->temps[src].type == s->temps[dst].type) {
        if (temps[src].state != TCG_TEMP_COPY) {
            temps[src].state = TCG_TEMP_COPY;
            temps[src].next_copy = src;
            temps[src].prev_copy = src;
        }
        temps[dst].state = TCG_TEMP_COPY;
        temps[dst].next_copy = temps[src].next_copy;
        temps[dst].prev_copy = src;
        temps[temps[dst].next_copy].prev_copy = dst;
        temps[src].next_copy = dst;
    }

    gen_args[0] = dst;
    gen_args[1] = src;
}

static void tcg_opt_gen_movi(TCGContext *s, int op_index, TCGArg *gen_args,
                             TCGOpcode old_op, TCGArg dst, TCGArg val)
{
    TCGOpcode new_op = op_to_movi(old_op);
    tcg_target_ulong mask;

    s->gen_opc_buf[op_index] = new_op;

    reset_temp(dst);
    temps[dst].state = TCG_TEMP_CONST;
    temps[dst].val = val;
    mask = val;
    if (TCG_TARGET_REG_BITS > 32 && new_op == INDEX_op_mov_i32) {
        /* High bits of the destination are now garbage.  */
        mask |= ~0xffffffffull;
    }
    temps[dst].mask = mask;

    gen_args[0] = dst;
    gen_args[1] = val;
}

static TCGArg do_constant_folding_2(TCGOpcode op, TCGArg x, TCGArg y)
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

    case INDEX_op_trunc_shr_i32:
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

    CASE_OP_32_64(ext8s):
        return (int8_t)x;

    CASE_OP_32_64(ext16s):
        return (int16_t)x;

    CASE_OP_32_64(ext8u):
        return (uint8_t)x;

    CASE_OP_32_64(ext16u):
        return (uint16_t)x;

    case INDEX_op_ext32s_i64:
        return (int32_t)x;

    case INDEX_op_ext32u_i64:
        return (uint32_t)x;

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

static TCGArg do_constant_folding(TCGOpcode op, TCGArg x, TCGArg y)
{
    TCGArg res = do_constant_folding_2(op, x, y);
    if (op_bits(op) == 32) {
        res &= 0xffffffff;
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

/* Return 2 if the condition can't be simplified, and the result
   of the condition (0 or 1) if it can */
static TCGArg do_constant_folding_cond(TCGOpcode op, TCGArg x,
                                       TCGArg y, TCGCond c)
{
    if (temps[x].state == TCG_TEMP_CONST && temps[y].state == TCG_TEMP_CONST) {
        switch (op_bits(op)) {
        case 32:
            return do_constant_folding_cond_32(temps[x].val, temps[y].val, c);
        case 64:
            return do_constant_folding_cond_64(temps[x].val, temps[y].val, c);
        default:
            tcg_abort();
        }
    } else if (temps_are_copies(x, y)) {
        return do_constant_folding_cond_eq(c);
    } else if (temps[y].state == TCG_TEMP_CONST && temps[y].val == 0) {
        switch (c) {
        case TCG_COND_LTU:
            return 0;
        case TCG_COND_GEU:
            return 1;
        default:
            return 2;
        }
    } else {
        return 2;
    }
}

/* Return 2 if the condition can't be simplified, and the result
   of the condition (0 or 1) if it can */
static TCGArg do_constant_folding_cond2(TCGArg *p1, TCGArg *p2, TCGCond c)
{
    TCGArg al = p1[0], ah = p1[1];
    TCGArg bl = p2[0], bh = p2[1];

    if (temps[bl].state == TCG_TEMP_CONST
        && temps[bh].state == TCG_TEMP_CONST) {
        uint64_t b = ((uint64_t)temps[bh].val << 32) | (uint32_t)temps[bl].val;

        if (temps[al].state == TCG_TEMP_CONST
            && temps[ah].state == TCG_TEMP_CONST) {
            uint64_t a;
            a = ((uint64_t)temps[ah].val << 32) | (uint32_t)temps[al].val;
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
    if (temps_are_copies(al, bl) && temps_are_copies(ah, bh)) {
        return do_constant_folding_cond_eq(c);
    }
    return 2;
}

static bool swap_commutative(TCGArg dest, TCGArg *p1, TCGArg *p2)
{
    TCGArg a1 = *p1, a2 = *p2;
    int sum = 0;
    sum += temps[a1].state == TCG_TEMP_CONST;
    sum -= temps[a2].state == TCG_TEMP_CONST;

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
    sum += temps[p1[0]].state == TCG_TEMP_CONST;
    sum += temps[p1[1]].state == TCG_TEMP_CONST;
    sum -= temps[p2[0]].state == TCG_TEMP_CONST;
    sum -= temps[p2[1]].state == TCG_TEMP_CONST;
    if (sum > 0) {
        TCGArg t;
        t = p1[0], p1[0] = p2[0], p2[0] = t;
        t = p1[1], p1[1] = p2[1], p2[1] = t;
        return true;
    }
    return false;
}

/* Propagate constants and copies, fold constant expressions. */
static TCGArg *tcg_constant_folding(TCGContext *s, uint16_t *tcg_opc_ptr,
                                    TCGArg *args, TCGOpDef *tcg_op_defs)
{
    int nb_ops, op_index, nb_temps, nb_globals;
    TCGArg *gen_args;

    /* Array VALS has an element for each temp.
       If this temp holds a constant then its value is kept in VALS' element.
       If this temp is a copy of other ones then the other copies are
       available through the doubly linked circular list. */

    nb_temps = s->nb_temps;
    nb_globals = s->nb_globals;
    reset_all_temps(nb_temps);

    nb_ops = tcg_opc_ptr - s->gen_opc_buf;
    gen_args = args;
    for (op_index = 0; op_index < nb_ops; op_index++) {
        TCGOpcode op = s->gen_opc_buf[op_index];
        const TCGOpDef *def = &tcg_op_defs[op];
        tcg_target_ulong mask, partmask, affected;
        int nb_oargs, nb_iargs, nb_args, i;
        TCGArg tmp;

        if (op == INDEX_op_call) {
            *gen_args++ = tmp = *args++;
            nb_oargs = tmp >> 16;
            nb_iargs = tmp & 0xffff;
            nb_args = nb_oargs + nb_iargs + def->nb_cargs;
        } else {
            nb_oargs = def->nb_oargs;
            nb_iargs = def->nb_iargs;
            nb_args = def->nb_args;
        }

        /* Do copy propagation */
        for (i = nb_oargs; i < nb_oargs + nb_iargs; i++) {
            if (temps[args[i]].state == TCG_TEMP_COPY) {
                args[i] = find_better_copy(s, args[i]);
            }
        }

        /* For commutative operations make constant second argument */
        switch (op) {
        CASE_OP_32_64(add):
        CASE_OP_32_64(mul):
        CASE_OP_32_64(and):
        CASE_OP_32_64(or):
        CASE_OP_32_64(xor):
        CASE_OP_32_64(eqv):
        CASE_OP_32_64(nand):
        CASE_OP_32_64(nor):
        CASE_OP_32_64(muluh):
        CASE_OP_32_64(mulsh):
            swap_commutative(args[0], &args[1], &args[2]);
            break;
        CASE_OP_32_64(brcond):
            if (swap_commutative(-1, &args[0], &args[1])) {
                args[2] = tcg_swap_cond(args[2]);
            }
            break;
        CASE_OP_32_64(setcond):
            if (swap_commutative(args[0], &args[1], &args[2])) {
                args[3] = tcg_swap_cond(args[3]);
            }
            break;
        CASE_OP_32_64(movcond):
            if (swap_commutative(-1, &args[1], &args[2])) {
                args[5] = tcg_swap_cond(args[5]);
            }
            /* For movcond, we canonicalize the "false" input reg to match
               the destination reg so that the tcg backend can implement
               a "move if true" operation.  */
            if (swap_commutative(args[0], &args[4], &args[3])) {
                args[5] = tcg_invert_cond(args[5]);
            }
            break;
        CASE_OP_32_64(add2):
            swap_commutative(args[0], &args[2], &args[4]);
            swap_commutative(args[1], &args[3], &args[5]);
            break;
        CASE_OP_32_64(mulu2):
        CASE_OP_32_64(muls2):
            swap_commutative(args[0], &args[2], &args[3]);
            break;
        case INDEX_op_brcond2_i32:
            if (swap_commutative2(&args[0], &args[2])) {
                args[4] = tcg_swap_cond(args[4]);
            }
            break;
        case INDEX_op_setcond2_i32:
            if (swap_commutative2(&args[1], &args[3])) {
                args[5] = tcg_swap_cond(args[5]);
            }
            break;
        default:
            break;
        }

        /* Simplify expressions for "shift/rot r, 0, a => movi r, 0",
           and "sub r, 0, a => neg r, a" case.  */
        switch (op) {
        CASE_OP_32_64(shl):
        CASE_OP_32_64(shr):
        CASE_OP_32_64(sar):
        CASE_OP_32_64(rotl):
        CASE_OP_32_64(rotr):
            if (temps[args[1]].state == TCG_TEMP_CONST
                && temps[args[1]].val == 0) {
                tcg_opt_gen_movi(s, op_index, gen_args, op, args[0], 0);
                args += 3;
                gen_args += 2;
                continue;
            }
            break;
        CASE_OP_32_64(sub):
            {
                TCGOpcode neg_op;
                bool have_neg;

                if (temps[args[2]].state == TCG_TEMP_CONST) {
                    /* Proceed with possible constant folding. */
                    break;
                }
                if (op == INDEX_op_sub_i32) {
                    neg_op = INDEX_op_neg_i32;
                    have_neg = TCG_TARGET_HAS_neg_i32;
                } else {
                    neg_op = INDEX_op_neg_i64;
                    have_neg = TCG_TARGET_HAS_neg_i64;
                }
                if (!have_neg) {
                    break;
                }
                if (temps[args[1]].state == TCG_TEMP_CONST
                    && temps[args[1]].val == 0) {
                    s->gen_opc_buf[op_index] = neg_op;
                    reset_temp(args[0]);
                    gen_args[0] = args[0];
                    gen_args[1] = args[2];
                    args += 3;
                    gen_args += 2;
                    continue;
                }
            }
            break;
        CASE_OP_32_64(xor):
        CASE_OP_32_64(nand):
            if (temps[args[1]].state != TCG_TEMP_CONST
                && temps[args[2]].state == TCG_TEMP_CONST
                && temps[args[2]].val == -1) {
                i = 1;
                goto try_not;
            }
            break;
        CASE_OP_32_64(nor):
            if (temps[args[1]].state != TCG_TEMP_CONST
                && temps[args[2]].state == TCG_TEMP_CONST
                && temps[args[2]].val == 0) {
                i = 1;
                goto try_not;
            }
            break;
        CASE_OP_32_64(andc):
            if (temps[args[2]].state != TCG_TEMP_CONST
                && temps[args[1]].state == TCG_TEMP_CONST
                && temps[args[1]].val == -1) {
                i = 2;
                goto try_not;
            }
            break;
        CASE_OP_32_64(orc):
        CASE_OP_32_64(eqv):
            if (temps[args[2]].state != TCG_TEMP_CONST
                && temps[args[1]].state == TCG_TEMP_CONST
                && temps[args[1]].val == 0) {
                i = 2;
                goto try_not;
            }
            break;
        try_not:
            {
                TCGOpcode not_op;
                bool have_not;

                if (def->flags & TCG_OPF_64BIT) {
                    not_op = INDEX_op_not_i64;
                    have_not = TCG_TARGET_HAS_not_i64;
                } else {
                    not_op = INDEX_op_not_i32;
                    have_not = TCG_TARGET_HAS_not_i32;
                }
                if (!have_not) {
                    break;
                }
                s->gen_opc_buf[op_index] = not_op;
                reset_temp(args[0]);
                gen_args[0] = args[0];
                gen_args[1] = args[i];
                args += 3;
                gen_args += 2;
                continue;
            }
        default:
            break;
        }

        /* Simplify expression for "op r, a, const => mov r, a" cases */
        switch (op) {
        CASE_OP_32_64(add):
        CASE_OP_32_64(sub):
        CASE_OP_32_64(shl):
        CASE_OP_32_64(shr):
        CASE_OP_32_64(sar):
        CASE_OP_32_64(rotl):
        CASE_OP_32_64(rotr):
        CASE_OP_32_64(or):
        CASE_OP_32_64(xor):
        CASE_OP_32_64(andc):
            if (temps[args[1]].state != TCG_TEMP_CONST
                && temps[args[2]].state == TCG_TEMP_CONST
                && temps[args[2]].val == 0) {
                goto do_mov3;
            }
            break;
        CASE_OP_32_64(and):
        CASE_OP_32_64(orc):
        CASE_OP_32_64(eqv):
            if (temps[args[1]].state != TCG_TEMP_CONST
                && temps[args[2]].state == TCG_TEMP_CONST
                && temps[args[2]].val == -1) {
                goto do_mov3;
            }
            break;
        do_mov3:
            if (temps_are_copies(args[0], args[1])) {
                s->gen_opc_buf[op_index] = INDEX_op_nop;
            } else {
                tcg_opt_gen_mov(s, op_index, gen_args, op, args[0], args[1]);
                gen_args += 2;
            }
            args += 3;
            continue;
        default:
            break;
        }

        /* Simplify using known-zero bits. Currently only ops with a single
           output argument is supported. */
        mask = -1;
        affected = -1;
        switch (op) {
        CASE_OP_32_64(ext8s):
            if ((temps[args[1]].mask & 0x80) != 0) {
                break;
            }
        CASE_OP_32_64(ext8u):
            mask = 0xff;
            goto and_const;
        CASE_OP_32_64(ext16s):
            if ((temps[args[1]].mask & 0x8000) != 0) {
                break;
            }
        CASE_OP_32_64(ext16u):
            mask = 0xffff;
            goto and_const;
        case INDEX_op_ext32s_i64:
            if ((temps[args[1]].mask & 0x80000000) != 0) {
                break;
            }
        case INDEX_op_ext32u_i64:
            mask = 0xffffffffU;
            goto and_const;

        CASE_OP_32_64(and):
            mask = temps[args[2]].mask;
            if (temps[args[2]].state == TCG_TEMP_CONST) {
        and_const:
                affected = temps[args[1]].mask & ~mask;
            }
            mask = temps[args[1]].mask & mask;
            break;

        CASE_OP_32_64(andc):
            /* Known-zeros does not imply known-ones.  Therefore unless
               args[2] is constant, we can't infer anything from it.  */
            if (temps[args[2]].state == TCG_TEMP_CONST) {
                mask = ~temps[args[2]].mask;
                goto and_const;
            }
            /* But we certainly know nothing outside args[1] may be set. */
            mask = temps[args[1]].mask;
            break;

        case INDEX_op_sar_i32:
            if (temps[args[2]].state == TCG_TEMP_CONST) {
                tmp = temps[args[2]].val & 31;
                mask = (int32_t)temps[args[1]].mask >> tmp;
            }
            break;
        case INDEX_op_sar_i64:
            if (temps[args[2]].state == TCG_TEMP_CONST) {
                tmp = temps[args[2]].val & 63;
                mask = (int64_t)temps[args[1]].mask >> tmp;
            }
            break;

        case INDEX_op_shr_i32:
            if (temps[args[2]].state == TCG_TEMP_CONST) {
                tmp = temps[args[2]].val & 31;
                mask = (uint32_t)temps[args[1]].mask >> tmp;
            }
            break;
        case INDEX_op_shr_i64:
            if (temps[args[2]].state == TCG_TEMP_CONST) {
                tmp = temps[args[2]].val & 63;
                mask = (uint64_t)temps[args[1]].mask >> tmp;
            }
            break;

        case INDEX_op_trunc_shr_i32:
            mask = (uint64_t)temps[args[1]].mask >> args[2];
            break;

        CASE_OP_32_64(shl):
            if (temps[args[2]].state == TCG_TEMP_CONST) {
                tmp = temps[args[2]].val & (TCG_TARGET_REG_BITS - 1);
                mask = temps[args[1]].mask << tmp;
            }
            break;

        CASE_OP_32_64(neg):
            /* Set to 1 all bits to the left of the rightmost.  */
            mask = -(temps[args[1]].mask & -temps[args[1]].mask);
            break;

        CASE_OP_32_64(deposit):
            mask = deposit64(temps[args[1]].mask, args[3], args[4],
                             temps[args[2]].mask);
            break;

        CASE_OP_32_64(or):
        CASE_OP_32_64(xor):
            mask = temps[args[1]].mask | temps[args[2]].mask;
            break;

        CASE_OP_32_64(setcond):
        case INDEX_op_setcond2_i32:
            mask = 1;
            break;

        CASE_OP_32_64(movcond):
            mask = temps[args[3]].mask | temps[args[4]].mask;
            break;

        CASE_OP_32_64(ld8u):
        case INDEX_op_qemu_ld8u:
            mask = 0xff;
            break;
        CASE_OP_32_64(ld16u):
        case INDEX_op_qemu_ld16u:
            mask = 0xffff;
            break;
        case INDEX_op_ld32u_i64:
#if TCG_TARGET_REG_BITS == 64
        case INDEX_op_qemu_ld32u:
#endif
            mask = 0xffffffffu;
            break;

        CASE_OP_32_64(qemu_ld):
            {
                TCGMemOp mop = args[nb_oargs + nb_iargs];
                if (!(mop & MO_SIGN)) {
                    mask = (2ULL << ((8 << (mop & MO_SIZE)) - 1)) - 1;
                }
            }
            break;

        default:
            break;
        }

        /* 32-bit ops (non 64-bit ops and non load/store ops) generate
           32-bit results.  For the result is zero test below, we can
           ignore high bits, but for further optimizations we need to
           record that the high bits contain garbage.  */
        partmask = mask;
        if (!(def->flags & (TCG_OPF_CALL_CLOBBER | TCG_OPF_64BIT))) {
            mask |= ~(tcg_target_ulong)0xffffffffu;
            partmask &= 0xffffffffu;
            affected &= 0xffffffffu;
        }

        if (partmask == 0) {
            assert(nb_oargs == 1);
            tcg_opt_gen_movi(s, op_index, gen_args, op, args[0], 0);
            args += nb_args;
            gen_args += 2;
            continue;
        }
        if (affected == 0) {
            assert(nb_oargs == 1);
            if (temps_are_copies(args[0], args[1])) {
                s->gen_opc_buf[op_index] = INDEX_op_nop;
            } else if (temps[args[1]].state != TCG_TEMP_CONST) {
                tcg_opt_gen_mov(s, op_index, gen_args, op, args[0], args[1]);
                gen_args += 2;
            } else {
                tcg_opt_gen_movi(s, op_index, gen_args, op,
                                 args[0], temps[args[1]].val);
                gen_args += 2;
            }
            args += nb_args;
            continue;
        }

        /* Simplify expression for "op r, a, 0 => movi r, 0" cases */
        switch (op) {
        CASE_OP_32_64(and):
        CASE_OP_32_64(mul):
        CASE_OP_32_64(muluh):
        CASE_OP_32_64(mulsh):
            if ((temps[args[2]].state == TCG_TEMP_CONST
                && temps[args[2]].val == 0)) {
                tcg_opt_gen_movi(s, op_index, gen_args, op, args[0], 0);
                args += 3;
                gen_args += 2;
                continue;
            }
            break;
        default:
            break;
        }

        /* Simplify expression for "op r, a, a => mov r, a" cases */
        switch (op) {
        CASE_OP_32_64(or):
        CASE_OP_32_64(and):
            if (temps_are_copies(args[1], args[2])) {
                if (temps_are_copies(args[0], args[1])) {
                    s->gen_opc_buf[op_index] = INDEX_op_nop;
                } else {
                    tcg_opt_gen_mov(s, op_index, gen_args, op,
                                    args[0], args[1]);
                    gen_args += 2;
                }
                args += 3;
                continue;
            }
            break;
        default:
            break;
        }

        /* Simplify expression for "op r, a, a => movi r, 0" cases */
        switch (op) {
        CASE_OP_32_64(andc):
        CASE_OP_32_64(sub):
        CASE_OP_32_64(xor):
            if (temps_are_copies(args[1], args[2])) {
                tcg_opt_gen_movi(s, op_index, gen_args, op, args[0], 0);
                gen_args += 2;
                args += 3;
                continue;
            }
            break;
        default:
            break;
        }

        /* Propagate constants through copy operations and do constant
           folding.  Constants will be substituted to arguments by register
           allocator where needed and possible.  Also detect copies. */
        switch (op) {
        CASE_OP_32_64(mov):
            if (temps_are_copies(args[0], args[1])) {
                args += 2;
                s->gen_opc_buf[op_index] = INDEX_op_nop;
                break;
            }
            if (temps[args[1]].state != TCG_TEMP_CONST) {
                tcg_opt_gen_mov(s, op_index, gen_args, op, args[0], args[1]);
                gen_args += 2;
                args += 2;
                break;
            }
            /* Source argument is constant.  Rewrite the operation and
               let movi case handle it. */
            args[1] = temps[args[1]].val;
            /* fallthrough */
        CASE_OP_32_64(movi):
            tcg_opt_gen_movi(s, op_index, gen_args, op, args[0], args[1]);
            gen_args += 2;
            args += 2;
            break;

        CASE_OP_32_64(not):
        CASE_OP_32_64(neg):
        CASE_OP_32_64(ext8s):
        CASE_OP_32_64(ext8u):
        CASE_OP_32_64(ext16s):
        CASE_OP_32_64(ext16u):
        case INDEX_op_ext32s_i64:
        case INDEX_op_ext32u_i64:
            if (temps[args[1]].state == TCG_TEMP_CONST) {
                tmp = do_constant_folding(op, temps[args[1]].val, 0);
                tcg_opt_gen_movi(s, op_index, gen_args, op, args[0], tmp);
                gen_args += 2;
                args += 2;
                break;
            }
            goto do_default;

        case INDEX_op_trunc_shr_i32:
            if (temps[args[1]].state == TCG_TEMP_CONST) {
                tmp = do_constant_folding(op, temps[args[1]].val, args[2]);
                tcg_opt_gen_movi(s, op_index, gen_args, op, args[0], tmp);
                gen_args += 2;
                args += 3;
                break;
            }
            goto do_default;

        CASE_OP_32_64(add):
        CASE_OP_32_64(sub):
        CASE_OP_32_64(mul):
        CASE_OP_32_64(or):
        CASE_OP_32_64(and):
        CASE_OP_32_64(xor):
        CASE_OP_32_64(shl):
        CASE_OP_32_64(shr):
        CASE_OP_32_64(sar):
        CASE_OP_32_64(rotl):
        CASE_OP_32_64(rotr):
        CASE_OP_32_64(andc):
        CASE_OP_32_64(orc):
        CASE_OP_32_64(eqv):
        CASE_OP_32_64(nand):
        CASE_OP_32_64(nor):
        CASE_OP_32_64(muluh):
        CASE_OP_32_64(mulsh):
        CASE_OP_32_64(div):
        CASE_OP_32_64(divu):
        CASE_OP_32_64(rem):
        CASE_OP_32_64(remu):
            if (temps[args[1]].state == TCG_TEMP_CONST
                && temps[args[2]].state == TCG_TEMP_CONST) {
                tmp = do_constant_folding(op, temps[args[1]].val,
                                          temps[args[2]].val);
                tcg_opt_gen_movi(s, op_index, gen_args, op, args[0], tmp);
                gen_args += 2;
                args += 3;
                break;
            }
            goto do_default;

        CASE_OP_32_64(deposit):
            if (temps[args[1]].state == TCG_TEMP_CONST
                && temps[args[2]].state == TCG_TEMP_CONST) {
                tmp = deposit64(temps[args[1]].val, args[3], args[4],
                                temps[args[2]].val);
                tcg_opt_gen_movi(s, op_index, gen_args, op, args[0], tmp);
                gen_args += 2;
                args += 5;
                break;
            }
            goto do_default;

        CASE_OP_32_64(setcond):
            tmp = do_constant_folding_cond(op, args[1], args[2], args[3]);
            if (tmp != 2) {
                tcg_opt_gen_movi(s, op_index, gen_args, op, args[0], tmp);
                gen_args += 2;
                args += 4;
                break;
            }
            goto do_default;

        CASE_OP_32_64(brcond):
            tmp = do_constant_folding_cond(op, args[0], args[1], args[2]);
            if (tmp != 2) {
                if (tmp) {
                    reset_all_temps(nb_temps);
                    s->gen_opc_buf[op_index] = INDEX_op_br;
                    gen_args[0] = args[3];
                    gen_args += 1;
                } else {
                    s->gen_opc_buf[op_index] = INDEX_op_nop;
                }
                args += 4;
                break;
            }
            goto do_default;

        CASE_OP_32_64(movcond):
            tmp = do_constant_folding_cond(op, args[1], args[2], args[5]);
            if (tmp != 2) {
                if (temps_are_copies(args[0], args[4-tmp])) {
                    s->gen_opc_buf[op_index] = INDEX_op_nop;
                } else if (temps[args[4-tmp]].state == TCG_TEMP_CONST) {
                    tcg_opt_gen_movi(s, op_index, gen_args, op,
                                     args[0], temps[args[4-tmp]].val);
                    gen_args += 2;
                } else {
                    tcg_opt_gen_mov(s, op_index, gen_args, op,
                                    args[0], args[4-tmp]);
                    gen_args += 2;
                }
                args += 6;
                break;
            }
            goto do_default;

        case INDEX_op_add2_i32:
        case INDEX_op_sub2_i32:
            if (temps[args[2]].state == TCG_TEMP_CONST
                && temps[args[3]].state == TCG_TEMP_CONST
                && temps[args[4]].state == TCG_TEMP_CONST
                && temps[args[5]].state == TCG_TEMP_CONST) {
                uint32_t al = temps[args[2]].val;
                uint32_t ah = temps[args[3]].val;
                uint32_t bl = temps[args[4]].val;
                uint32_t bh = temps[args[5]].val;
                uint64_t a = ((uint64_t)ah << 32) | al;
                uint64_t b = ((uint64_t)bh << 32) | bl;
                TCGArg rl, rh;

                if (op == INDEX_op_add2_i32) {
                    a += b;
                } else {
                    a -= b;
                }

                /* We emit the extra nop when we emit the add2/sub2.  */
                assert(s->gen_opc_buf[op_index + 1] == INDEX_op_nop);

                rl = args[0];
                rh = args[1];
                tcg_opt_gen_movi(s, op_index, &gen_args[0],
                                 op, rl, (uint32_t)a);
                tcg_opt_gen_movi(s, ++op_index, &gen_args[2],
                                 op, rh, (uint32_t)(a >> 32));
                gen_args += 4;
                args += 6;
                break;
            }
            goto do_default;

        case INDEX_op_mulu2_i32:
            if (temps[args[2]].state == TCG_TEMP_CONST
                && temps[args[3]].state == TCG_TEMP_CONST) {
                uint32_t a = temps[args[2]].val;
                uint32_t b = temps[args[3]].val;
                uint64_t r = (uint64_t)a * b;
                TCGArg rl, rh;

                /* We emit the extra nop when we emit the mulu2.  */
                assert(s->gen_opc_buf[op_index + 1] == INDEX_op_nop);

                rl = args[0];
                rh = args[1];
                tcg_opt_gen_movi(s, op_index, &gen_args[0],
                                 op, rl, (uint32_t)r);
                tcg_opt_gen_movi(s, ++op_index, &gen_args[2],
                                 op, rh, (uint32_t)(r >> 32));
                gen_args += 4;
                args += 4;
                break;
            }
            goto do_default;

        case INDEX_op_brcond2_i32:
            tmp = do_constant_folding_cond2(&args[0], &args[2], args[4]);
            if (tmp != 2) {
                if (tmp) {
            do_brcond_true:
                    reset_all_temps(nb_temps);
                    s->gen_opc_buf[op_index] = INDEX_op_br;
                    gen_args[0] = args[5];
                    gen_args += 1;
                } else {
            do_brcond_false:
                    s->gen_opc_buf[op_index] = INDEX_op_nop;
                }
            } else if ((args[4] == TCG_COND_LT || args[4] == TCG_COND_GE)
                       && temps[args[2]].state == TCG_TEMP_CONST
                       && temps[args[3]].state == TCG_TEMP_CONST
                       && temps[args[2]].val == 0
                       && temps[args[3]].val == 0) {
                /* Simplify LT/GE comparisons vs zero to a single compare
                   vs the high word of the input.  */
            do_brcond_high:
                reset_all_temps(nb_temps);
                s->gen_opc_buf[op_index] = INDEX_op_brcond_i32;
                gen_args[0] = args[1];
                gen_args[1] = args[3];
                gen_args[2] = args[4];
                gen_args[3] = args[5];
                gen_args += 4;
            } else if (args[4] == TCG_COND_EQ) {
                /* Simplify EQ comparisons where one of the pairs
                   can be simplified.  */
                tmp = do_constant_folding_cond(INDEX_op_brcond_i32,
                                               args[0], args[2], TCG_COND_EQ);
                if (tmp == 0) {
                    goto do_brcond_false;
                } else if (tmp == 1) {
                    goto do_brcond_high;
                }
                tmp = do_constant_folding_cond(INDEX_op_brcond_i32,
                                               args[1], args[3], TCG_COND_EQ);
                if (tmp == 0) {
                    goto do_brcond_false;
                } else if (tmp != 1) {
                    goto do_default;
                }
            do_brcond_low:
                reset_all_temps(nb_temps);
                s->gen_opc_buf[op_index] = INDEX_op_brcond_i32;
                gen_args[0] = args[0];
                gen_args[1] = args[2];
                gen_args[2] = args[4];
                gen_args[3] = args[5];
                gen_args += 4;
            } else if (args[4] == TCG_COND_NE) {
                /* Simplify NE comparisons where one of the pairs
                   can be simplified.  */
                tmp = do_constant_folding_cond(INDEX_op_brcond_i32,
                                               args[0], args[2], TCG_COND_NE);
                if (tmp == 0) {
                    goto do_brcond_high;
                } else if (tmp == 1) {
                    goto do_brcond_true;
                }
                tmp = do_constant_folding_cond(INDEX_op_brcond_i32,
                                               args[1], args[3], TCG_COND_NE);
                if (tmp == 0) {
                    goto do_brcond_low;
                } else if (tmp == 1) {
                    goto do_brcond_true;
                }
                goto do_default;
            } else {
                goto do_default;
            }
            args += 6;
            break;

        case INDEX_op_setcond2_i32:
            tmp = do_constant_folding_cond2(&args[1], &args[3], args[5]);
            if (tmp != 2) {
            do_setcond_const:
                tcg_opt_gen_movi(s, op_index, gen_args, op, args[0], tmp);
                gen_args += 2;
            } else if ((args[5] == TCG_COND_LT || args[5] == TCG_COND_GE)
                       && temps[args[3]].state == TCG_TEMP_CONST
                       && temps[args[4]].state == TCG_TEMP_CONST
                       && temps[args[3]].val == 0
                       && temps[args[4]].val == 0) {
                /* Simplify LT/GE comparisons vs zero to a single compare
                   vs the high word of the input.  */
            do_setcond_high:
                s->gen_opc_buf[op_index] = INDEX_op_setcond_i32;
                reset_temp(args[0]);
                temps[args[0]].mask = 1;
                gen_args[0] = args[0];
                gen_args[1] = args[2];
                gen_args[2] = args[4];
                gen_args[3] = args[5];
                gen_args += 4;
            } else if (args[5] == TCG_COND_EQ) {
                /* Simplify EQ comparisons where one of the pairs
                   can be simplified.  */
                tmp = do_constant_folding_cond(INDEX_op_setcond_i32,
                                               args[1], args[3], TCG_COND_EQ);
                if (tmp == 0) {
                    goto do_setcond_const;
                } else if (tmp == 1) {
                    goto do_setcond_high;
                }
                tmp = do_constant_folding_cond(INDEX_op_setcond_i32,
                                               args[2], args[4], TCG_COND_EQ);
                if (tmp == 0) {
                    goto do_setcond_high;
                } else if (tmp != 1) {
                    goto do_default;
                }
            do_setcond_low:
                reset_temp(args[0]);
                temps[args[0]].mask = 1;
                s->gen_opc_buf[op_index] = INDEX_op_setcond_i32;
                gen_args[0] = args[0];
                gen_args[1] = args[1];
                gen_args[2] = args[3];
                gen_args[3] = args[5];
                gen_args += 4;
            } else if (args[5] == TCG_COND_NE) {
                /* Simplify NE comparisons where one of the pairs
                   can be simplified.  */
                tmp = do_constant_folding_cond(INDEX_op_setcond_i32,
                                               args[1], args[3], TCG_COND_NE);
                if (tmp == 0) {
                    goto do_setcond_high;
                } else if (tmp == 1) {
                    goto do_setcond_const;
                }
                tmp = do_constant_folding_cond(INDEX_op_setcond_i32,
                                               args[2], args[4], TCG_COND_NE);
                if (tmp == 0) {
                    goto do_setcond_low;
                } else if (tmp == 1) {
                    goto do_setcond_const;
                }
                goto do_default;
            } else {
                goto do_default;
            }
            args += 6;
            break;

        case INDEX_op_call:
            if (!(args[nb_oargs + nb_iargs + 1]
                  & (TCG_CALL_NO_READ_GLOBALS | TCG_CALL_NO_WRITE_GLOBALS))) {
                for (i = 0; i < nb_globals; i++) {
                    reset_temp(i);
                }
            }
            goto do_reset_output;

        default:
        do_default:
            /* Default case: we know nothing about operation (or were unable
               to compute the operation result) so no propagation is done.
               We trash everything if the operation is the end of a basic
               block, otherwise we only trash the output args.  "mask" is
               the non-zero bits mask for the first output arg.  */
            if (def->flags & TCG_OPF_BB_END) {
                reset_all_temps(nb_temps);
            } else {
        do_reset_output:
                for (i = 0; i < nb_oargs; i++) {
                    reset_temp(args[i]);
                    /* Save the corresponding known-zero bits mask for the
                       first output argument (only one supported so far). */
                    if (i == 0) {
                        temps[args[i]].mask = mask;
                    }
                }
            }
            for (i = 0; i < nb_args; i++) {
                gen_args[i] = args[i];
            }
            args += nb_args;
            gen_args += nb_args;
            break;
        }
    }

    return gen_args;
}

TCGArg *tcg_optimize(TCGContext *s, uint16_t *tcg_opc_ptr,
        TCGArg *args, TCGOpDef *tcg_op_defs)
{
    TCGArg *res;
    res = tcg_constant_folding(s, tcg_opc_ptr, args, tcg_op_defs);
    return res;
}
