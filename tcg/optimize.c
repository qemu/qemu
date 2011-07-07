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

static TCGArg *tcg_constant_folding(TCGContext *s, uint16_t *tcg_opc_ptr,
                                    TCGArg *args, TCGOpDef *tcg_op_defs)
{
    int i, nb_ops, op_index, op, nb_temps, nb_globals;
    const TCGOpDef *def;
    TCGArg *gen_args;

    nb_temps = s->nb_temps;
    nb_globals = s->nb_globals;

    nb_ops = tcg_opc_ptr - gen_opc_buf;
    gen_args = args;
    for (op_index = 0; op_index < nb_ops; op_index++) {
        op = gen_opc_buf[op_index];
        def = &tcg_op_defs[op];
        switch (op) {
        case INDEX_op_call:
            i = (args[0] >> 16) + (args[0] & 0xffff) + 3;
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
            for (i = 0; i < def->nb_args; i++) {
                *gen_args = *args;
                args++;
                gen_args++;
            }
            break;
        default:
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
