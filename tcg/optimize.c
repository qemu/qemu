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

#if TCG_TARGET_REG_BITS == 64
#define CASE_OP_32_64(x)                        \
        glue(glue(case INDEX_op_, x), _i32):    \
        glue(glue(case INDEX_op_, x), _i64)
#else
#define CASE_OP_32_64(x)                        \
        glue(glue(case INDEX_op_, x), _i32)
#endif

typedef enum {
    TCG_TEMP_UNDEF = 0,
    TCG_TEMP_CONST,
    TCG_TEMP_COPY,
    TCG_TEMP_HAS_COPY,
    TCG_TEMP_ANY
} tcg_temp_state;

struct tcg_temp_info {
    tcg_temp_state state;
    uint16_t prev_copy;
    uint16_t next_copy;
    tcg_target_ulong val;
};

static struct tcg_temp_info temps[TCG_MAX_TEMPS];

/* Reset TEMP's state to TCG_TEMP_ANY.  If TEMP was a representative of some
   class of equivalent temp's, a new representative should be chosen in this
   class. */
static void reset_temp(TCGArg temp, int nb_temps, int nb_globals)
{
    int i;
    TCGArg new_base = (TCGArg)-1;
    if (temps[temp].state == TCG_TEMP_HAS_COPY) {
        for (i = temps[temp].next_copy; i != temp; i = temps[i].next_copy) {
            if (i >= nb_globals) {
                temps[i].state = TCG_TEMP_HAS_COPY;
                new_base = i;
                break;
            }
        }
        for (i = temps[temp].next_copy; i != temp; i = temps[i].next_copy) {
            if (new_base == (TCGArg)-1) {
                temps[i].state = TCG_TEMP_ANY;
            } else {
                temps[i].val = new_base;
            }
        }
        temps[temps[temp].next_copy].prev_copy = temps[temp].prev_copy;
        temps[temps[temp].prev_copy].next_copy = temps[temp].next_copy;
    } else if (temps[temp].state == TCG_TEMP_COPY) {
        temps[temps[temp].next_copy].prev_copy = temps[temp].prev_copy;
        temps[temps[temp].prev_copy].next_copy = temps[temp].next_copy;
        new_base = temps[temp].val;
    }
    temps[temp].state = TCG_TEMP_ANY;
    if (new_base != (TCGArg)-1 && temps[new_base].next_copy == new_base) {
        temps[new_base].state = TCG_TEMP_ANY;
    }
}

static int op_bits(int op)
{
    switch (op) {
    case INDEX_op_mov_i32:
    case INDEX_op_add_i32:
    case INDEX_op_sub_i32:
    case INDEX_op_mul_i32:
    case INDEX_op_and_i32:
    case INDEX_op_or_i32:
    case INDEX_op_xor_i32:
        return 32;
#if TCG_TARGET_REG_BITS == 64
    case INDEX_op_mov_i64:
    case INDEX_op_add_i64:
    case INDEX_op_sub_i64:
    case INDEX_op_mul_i64:
    case INDEX_op_and_i64:
    case INDEX_op_or_i64:
    case INDEX_op_xor_i64:
        return 64;
#endif
    default:
        fprintf(stderr, "Unrecognized operation %d in op_bits.\n", op);
        tcg_abort();
    }
}

static int op_to_movi(int op)
{
    switch (op_bits(op)) {
    case 32:
        return INDEX_op_movi_i32;
#if TCG_TARGET_REG_BITS == 64
    case 64:
        return INDEX_op_movi_i64;
#endif
    default:
        fprintf(stderr, "op_to_movi: unexpected return value of "
                "function op_bits.\n");
        tcg_abort();
    }
}

static void tcg_opt_gen_mov(TCGArg *gen_args, TCGArg dst, TCGArg src,
                            int nb_temps, int nb_globals)
{
        reset_temp(dst, nb_temps, nb_globals);
        assert(temps[src].state != TCG_TEMP_COPY);
        if (src >= nb_globals) {
            assert(temps[src].state != TCG_TEMP_CONST);
            if (temps[src].state != TCG_TEMP_HAS_COPY) {
                temps[src].state = TCG_TEMP_HAS_COPY;
                temps[src].next_copy = src;
                temps[src].prev_copy = src;
            }
            temps[dst].state = TCG_TEMP_COPY;
            temps[dst].val = src;
            temps[dst].next_copy = temps[src].next_copy;
            temps[dst].prev_copy = src;
            temps[temps[dst].next_copy].prev_copy = dst;
            temps[src].next_copy = dst;
        }
        gen_args[0] = dst;
        gen_args[1] = src;
}

static void tcg_opt_gen_movi(TCGArg *gen_args, TCGArg dst, TCGArg val,
                             int nb_temps, int nb_globals)
{
        reset_temp(dst, nb_temps, nb_globals);
        temps[dst].state = TCG_TEMP_CONST;
        temps[dst].val = val;
        gen_args[0] = dst;
        gen_args[1] = val;
}

static int op_to_mov(int op)
{
    switch (op_bits(op)) {
    case 32:
        return INDEX_op_mov_i32;
#if TCG_TARGET_REG_BITS == 64
    case 64:
        return INDEX_op_mov_i64;
#endif
    default:
        fprintf(stderr, "op_to_mov: unexpected return value of "
                "function op_bits.\n");
        tcg_abort();
    }
}

static TCGArg do_constant_folding_2(int op, TCGArg x, TCGArg y)
{
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

    default:
        fprintf(stderr,
                "Unrecognized operation %d in do_constant_folding.\n", op);
        tcg_abort();
    }
}

static TCGArg do_constant_folding(int op, TCGArg x, TCGArg y)
{
    TCGArg res = do_constant_folding_2(op, x, y);
#if TCG_TARGET_REG_BITS == 64
    if (op_bits(op) == 32) {
        res &= 0xffffffff;
    }
#endif
    return res;
}

/* Propagate constants and copies, fold constant expressions. */
static TCGArg *tcg_constant_folding(TCGContext *s, uint16_t *tcg_opc_ptr,
                                    TCGArg *args, TCGOpDef *tcg_op_defs)
{
    int i, nb_ops, op_index, op, nb_temps, nb_globals, nb_call_args;
    const TCGOpDef *def;
    TCGArg *gen_args;
    TCGArg tmp;
    /* Array VALS has an element for each temp.
       If this temp holds a constant then its value is kept in VALS' element.
       If this temp is a copy of other ones then this equivalence class'
       representative is kept in VALS' element.
       If this temp is neither copy nor constant then corresponding VALS'
       element is unused. */

    nb_temps = s->nb_temps;
    nb_globals = s->nb_globals;
    memset(temps, 0, nb_temps * sizeof(struct tcg_temp_info));

    nb_ops = tcg_opc_ptr - gen_opc_buf;
    gen_args = args;
    for (op_index = 0; op_index < nb_ops; op_index++) {
        op = gen_opc_buf[op_index];
        def = &tcg_op_defs[op];
        /* Do copy propagation */
        if (!(def->flags & (TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS))) {
            assert(op != INDEX_op_call);
            for (i = def->nb_oargs; i < def->nb_oargs + def->nb_iargs; i++) {
                if (temps[args[i]].state == TCG_TEMP_COPY) {
                    args[i] = temps[args[i]].val;
                }
            }
        }

        /* For commutative operations make constant second argument */
        switch (op) {
        CASE_OP_32_64(add):
        CASE_OP_32_64(mul):
        CASE_OP_32_64(and):
        CASE_OP_32_64(or):
        CASE_OP_32_64(xor):
            if (temps[args[1]].state == TCG_TEMP_CONST) {
                tmp = args[1];
                args[1] = args[2];
                args[2] = tmp;
            }
            break;
        default:
            break;
        }

        /* Simplify expression if possible. */
        switch (op) {
        CASE_OP_32_64(add):
        CASE_OP_32_64(sub):
            if (temps[args[1]].state == TCG_TEMP_CONST) {
                /* Proceed with possible constant folding. */
                break;
            }
            if (temps[args[2]].state == TCG_TEMP_CONST
                && temps[args[2]].val == 0) {
                if ((temps[args[0]].state == TCG_TEMP_COPY
                    && temps[args[0]].val == args[1])
                    || args[0] == args[1]) {
                    args += 3;
                    gen_opc_buf[op_index] = INDEX_op_nop;
                } else {
                    gen_opc_buf[op_index] = op_to_mov(op);
                    tcg_opt_gen_mov(gen_args, args[0], args[1],
                                    nb_temps, nb_globals);
                    gen_args += 2;
                    args += 3;
                }
                continue;
            }
            break;
        CASE_OP_32_64(mul):
            if ((temps[args[2]].state == TCG_TEMP_CONST
                && temps[args[2]].val == 0)) {
                gen_opc_buf[op_index] = op_to_movi(op);
                tcg_opt_gen_movi(gen_args, args[0], 0, nb_temps, nb_globals);
                args += 3;
                gen_args += 2;
                continue;
            }
            break;
        CASE_OP_32_64(or):
        CASE_OP_32_64(and):
            if (args[1] == args[2]) {
                if (args[1] == args[0]) {
                    args += 3;
                    gen_opc_buf[op_index] = INDEX_op_nop;
                } else {
                    gen_opc_buf[op_index] = op_to_mov(op);
                    tcg_opt_gen_mov(gen_args, args[0], args[1], nb_temps,
                                    nb_globals);
                    gen_args += 2;
                    args += 3;
                }
                continue;
            }
            break;
        }

        /* Propagate constants through copy operations and do constant
           folding.  Constants will be substituted to arguments by register
           allocator where needed and possible.  Also detect copies. */
        switch (op) {
        CASE_OP_32_64(mov):
            if ((temps[args[1]].state == TCG_TEMP_COPY
                && temps[args[1]].val == args[0])
                || args[0] == args[1]) {
                args += 2;
                gen_opc_buf[op_index] = INDEX_op_nop;
                break;
            }
            if (temps[args[1]].state != TCG_TEMP_CONST) {
                tcg_opt_gen_mov(gen_args, args[0], args[1],
                                nb_temps, nb_globals);
                gen_args += 2;
                args += 2;
                break;
            }
            /* Source argument is constant.  Rewrite the operation and
               let movi case handle it. */
            op = op_to_movi(op);
            gen_opc_buf[op_index] = op;
            args[1] = temps[args[1]].val;
            /* fallthrough */
        CASE_OP_32_64(movi):
            tcg_opt_gen_movi(gen_args, args[0], args[1], nb_temps, nb_globals);
            gen_args += 2;
            args += 2;
            break;
        CASE_OP_32_64(add):
        CASE_OP_32_64(sub):
        CASE_OP_32_64(mul):
        CASE_OP_32_64(or):
        CASE_OP_32_64(and):
        CASE_OP_32_64(xor):
            if (temps[args[1]].state == TCG_TEMP_CONST
                && temps[args[2]].state == TCG_TEMP_CONST) {
                gen_opc_buf[op_index] = op_to_movi(op);
                tmp = do_constant_folding(op, temps[args[1]].val,
                                          temps[args[2]].val);
                tcg_opt_gen_movi(gen_args, args[0], tmp, nb_temps, nb_globals);
                gen_args += 2;
                args += 3;
                break;
            } else {
                reset_temp(args[0], nb_temps, nb_globals);
                gen_args[0] = args[0];
                gen_args[1] = args[1];
                gen_args[2] = args[2];
                gen_args += 3;
                args += 3;
                break;
            }
        case INDEX_op_call:
            nb_call_args = (args[0] >> 16) + (args[0] & 0xffff);
            if (!(args[nb_call_args + 1] & (TCG_CALL_CONST | TCG_CALL_PURE))) {
                for (i = 0; i < nb_globals; i++) {
                    reset_temp(i, nb_temps, nb_globals);
                }
            }
            for (i = 0; i < (args[0] >> 16); i++) {
                reset_temp(args[i + 1], nb_temps, nb_globals);
            }
            i = nb_call_args + 3;
            while (i) {
                *gen_args = *args;
                args++;
                gen_args++;
                i--;
            }
            break;
        case INDEX_op_set_label:
        case INDEX_op_jmp:
        case INDEX_op_br:
        CASE_OP_32_64(brcond):
            memset(temps, 0, nb_temps * sizeof(struct tcg_temp_info));
            for (i = 0; i < def->nb_args; i++) {
                *gen_args = *args;
                args++;
                gen_args++;
            }
            break;
        default:
            /* Default case: we do know nothing about operation so no
               propagation is done.  We only trash output args.  */
            for (i = 0; i < def->nb_oargs; i++) {
                reset_temp(args[i], nb_temps, nb_globals);
            }
            for (i = 0; i < def->nb_args; i++) {
                gen_args[i] = args[i];
            }
            args += def->nb_args;
            gen_args += def->nb_args;
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
