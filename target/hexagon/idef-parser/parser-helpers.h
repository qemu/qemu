/*
 *  Copyright(c) 2019-2022 rev.ng Labs Srl. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PARSER_HELPERS_H
#define PARSER_HELPERS_H

#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tcg/tcg-cond.h"

#include "idef-parser.tab.h"
#include "idef-parser.yy.h"
#include "idef-parser.h"

/* Decomment this to disable yyasserts */
/* #define NDEBUG */

#define ERR_LINE_CONTEXT 40

#define START_COMMENT "/" "*"
#define END_COMMENT "*" "/"

void yyerror(YYLTYPE *locp,
             yyscan_t scanner __attribute__((unused)),
             Context *c,
             const char *s);

#ifndef NDEBUG
#define yyassert(context, locp, condition, msg)              \
    if (!(condition)) {                                      \
        yyerror(locp, (context)->scanner, (context), (msg)); \
    }
#endif

bool is_direct_predicate(HexValue *value);

bool is_inside_ternary(Context *c);

/**
 * Print functions
 */

void str_print(Context *c, YYLTYPE *locp, const char *string);

void uint8_print(Context *c, YYLTYPE *locp, uint8_t *num);

void uint64_print(Context *c, YYLTYPE *locp, uint64_t *num);

void int_print(Context *c, YYLTYPE *locp, int *num);

void uint_print(Context *c, YYLTYPE *locp, unsigned *num);

void tmp_print(Context *c, YYLTYPE *locp, HexTmp *tmp);

void pred_print(Context *c, YYLTYPE *locp, HexPred *pred, bool is_dotnew);

void reg_compose(Context *c, YYLTYPE *locp, HexReg *reg, char reg_id[5]);

void reg_print(Context *c, YYLTYPE *locp, HexReg *reg);

void imm_print(Context *c, YYLTYPE *locp, HexImm *imm);

void var_print(Context *c, YYLTYPE *locp, HexVar *var);

void rvalue_print(Context *c, YYLTYPE *locp, void *pointer);

void out_assert(Context *c, YYLTYPE *locp, void *dummy);

/**
 * Copies output code buffer into stdout
 */
void commit(Context *c);

#define OUT_IMPL(c, locp, x)                    \
    _Generic(*(x),                              \
        char:      str_print,                   \
        uint8_t:   uint8_print,                 \
        uint64_t:  uint64_print,                \
        int:       int_print,                   \
        unsigned:  uint_print,                  \
        HexValue:  rvalue_print,                \
        default:   out_assert                   \
    )(c, locp, x);

/* FOREACH macro */
#define FE_1(c, locp, WHAT, X) WHAT(c, locp, X)
#define FE_2(c, locp, WHAT, X, ...) \
    WHAT(c, locp, X)FE_1(c, locp, WHAT, __VA_ARGS__)
#define FE_3(c, locp, WHAT, X, ...) \
    WHAT(c, locp, X)FE_2(c, locp, WHAT, __VA_ARGS__)
#define FE_4(c, locp, WHAT, X, ...) \
    WHAT(c, locp, X)FE_3(c, locp, WHAT, __VA_ARGS__)
#define FE_5(c, locp, WHAT, X, ...) \
    WHAT(c, locp, X)FE_4(c, locp, WHAT, __VA_ARGS__)
#define FE_6(c, locp, WHAT, X, ...) \
    WHAT(c, locp, X)FE_5(c, locp, WHAT, __VA_ARGS__)
#define FE_7(c, locp, WHAT, X, ...) \
    WHAT(c, locp, X)FE_6(c, locp, WHAT, __VA_ARGS__)
#define FE_8(c, locp, WHAT, X, ...) \
    WHAT(c, locp, X)FE_7(c, locp, WHAT, __VA_ARGS__)
#define FE_9(c, locp, WHAT, X, ...) \
    WHAT(c, locp, X)FE_8(c, locp, WHAT, __VA_ARGS__)
/* repeat as needed */

#define GET_MACRO(_1, _2, _3, _4, _5, _6, _7, _8, _9, NAME, ...) NAME

#define FOR_EACH(c, locp, action, ...)          \
  do {                                          \
    GET_MACRO(__VA_ARGS__,                      \
              FE_9,                             \
              FE_8,                             \
              FE_7,                             \
              FE_6,                             \
              FE_5,                             \
              FE_4,                             \
              FE_3,                             \
              FE_2,                             \
              FE_1)(c, locp, action,            \
                    __VA_ARGS__)                \
  } while (0)

#define OUT(c, locp, ...) FOR_EACH((c), (locp), OUT_IMPL, __VA_ARGS__)

const char *cmp_swap(Context *c, YYLTYPE *locp, const char *type);

/**
 * Temporary values creation
 */

HexValue gen_tmp(Context *c,
                 YYLTYPE *locp,
                 unsigned bit_width,
                 HexSignedness signedness);

HexValue gen_tmp_value(Context *c,
                       YYLTYPE *locp,
                       const char *value,
                       unsigned bit_width,
                       HexSignedness signedness);

HexValue gen_imm_value(Context *c __attribute__((unused)),
                       YYLTYPE *locp,
                       int value,
                       unsigned bit_width,
                       HexSignedness signedness);

HexValue gen_imm_qemu_tmp(Context *c, YYLTYPE *locp, unsigned bit_width,
                          HexSignedness signedness);

void gen_rvalue_free(Context *c, YYLTYPE *locp, HexValue *rvalue);

HexValue rvalue_materialize(Context *c, YYLTYPE *locp, HexValue *rvalue);

HexValue gen_rvalue_extend(Context *c, YYLTYPE *locp, HexValue *rvalue);

HexValue gen_rvalue_truncate(Context *c, YYLTYPE *locp, HexValue *rvalue);

void gen_varid_allocate(Context *c,
                        YYLTYPE *locp,
                        HexValue *varid,
                        unsigned bit_width,
                        HexSignedness signedness);

/**
 * Code generation functions
 */

HexValue gen_bin_cmp(Context *c,
                     YYLTYPE *locp,
                     TCGCond type,
                     HexValue *op1,
                     HexValue *op2);

HexValue gen_bin_op(Context *c,
                    YYLTYPE *locp,
                    OpType type,
                    HexValue *op1,
                    HexValue *op2);

HexValue gen_cast_op(Context *c,
                     YYLTYPE *locp,
                     HexValue *src,
                     unsigned target_width,
                     HexSignedness signedness);

/**
 * gen_extend_op extends a region of src_width_ptr bits stored in a
 * value_ptr to the size of dst_width. Note: src_width_ptr is a
 * HexValue * to handle the special case where it is unknown at
 * translation time.
 */
HexValue gen_extend_op(Context *c,
                       YYLTYPE *locp,
                       HexValue *src_width,
                       unsigned dst_width,
                       HexValue *value,
                       HexSignedness signedness);

void gen_rdeposit_op(Context *c,
                     YYLTYPE *locp,
                     HexValue *dst,
                     HexValue *value,
                     HexValue *begin,
                     HexValue *width);

void gen_deposit_op(Context *c,
                    YYLTYPE *locp,
                    HexValue *dst,
                    HexValue *value,
                    HexValue *index,
                    HexCast *cast);

HexValue gen_rextract_op(Context *c,
                         YYLTYPE *locp,
                         HexValue *src,
                         unsigned begin,
                         unsigned width);

HexValue gen_extract_op(Context *c,
                        YYLTYPE *locp,
                        HexValue *src,
                        HexValue *index,
                        HexExtract *extract);

HexValue gen_read_reg(Context *c, YYLTYPE *locp, HexValue *reg);

void gen_write_reg(Context *c, YYLTYPE *locp, HexValue *reg, HexValue *value);

void gen_assign(Context *c,
                YYLTYPE *locp,
                HexValue *dst,
                HexValue *value);

HexValue gen_convround(Context *c,
                       YYLTYPE *locp,
                       HexValue *src);

HexValue gen_round(Context *c,
                   YYLTYPE *locp,
                   HexValue *src,
                   HexValue *position);

HexValue gen_convround_n(Context *c,
                         YYLTYPE *locp,
                         HexValue *src,
                         HexValue *pos);

/**
 * Circular addressing mode with auto-increment
 */
void gen_circ_op(Context *c,
                 YYLTYPE *locp,
                 HexValue *addr,
                 HexValue *increment,
                 HexValue *modifier);

HexValue gen_locnt_op(Context *c, YYLTYPE *locp, HexValue *src);

HexValue gen_ctpop_op(Context *c, YYLTYPE *locp, HexValue *src);

HexValue gen_rotl(Context *c, YYLTYPE *locp, HexValue *src, HexValue *n);

HexValue gen_deinterleave(Context *c, YYLTYPE *locp, HexValue *mixed);

HexValue gen_interleave(Context *c,
                        YYLTYPE *locp,
                        HexValue *odd,
                        HexValue *even);

HexValue gen_carry_from_add(Context *c,
                            YYLTYPE *locp,
                            HexValue *op1,
                            HexValue *op2,
                            HexValue *op3);

void gen_addsat64(Context *c,
                  YYLTYPE *locp,
                  HexValue *dst,
                  HexValue *op1,
                  HexValue *op2);

void gen_inst(Context *c, GString *iname);

void gen_inst_init_args(Context *c, YYLTYPE *locp);

void gen_inst_code(Context *c, YYLTYPE *locp);

void gen_pred_assign(Context *c, YYLTYPE *locp, HexValue *left_pred,
                     HexValue *right_pred);

void gen_cancel(Context *c, YYLTYPE *locp);

void gen_load_cancel(Context *c, YYLTYPE *locp);

void gen_load(Context *c, YYLTYPE *locp, HexValue *size,
              HexSignedness signedness, HexValue *ea, HexValue *dst);

void gen_store(Context *c, YYLTYPE *locp, HexValue *size, HexValue *ea,
               HexValue *src);

void gen_sethalf(Context *c, YYLTYPE *locp, HexCast *sh, HexValue *n,
                 HexValue *dst, HexValue *value);

void gen_setbits(Context *c, YYLTYPE *locp, HexValue *hi, HexValue *lo,
                 HexValue *dst, HexValue *value);

unsigned gen_if_cond(Context *c, YYLTYPE *locp, HexValue *cond);

unsigned gen_if_else(Context *c, YYLTYPE *locp, unsigned index);

HexValue gen_rvalue_pred(Context *c, YYLTYPE *locp, HexValue *pred);

HexValue gen_rvalue_var(Context *c, YYLTYPE *locp, HexValue *var);

HexValue gen_rvalue_mpy(Context *c, YYLTYPE *locp, HexMpy *mpy, HexValue *op1,
                        HexValue *op2);

HexValue gen_rvalue_not(Context *c, YYLTYPE *locp, HexValue *value);

HexValue gen_rvalue_notl(Context *c, YYLTYPE *locp, HexValue *value);

HexValue gen_rvalue_sat(Context *c, YYLTYPE *locp, HexSat *sat, HexValue *n,
                        HexValue *value);

HexValue gen_rvalue_fscr(Context *c, YYLTYPE *locp, HexValue *value);

HexValue gen_rvalue_abs(Context *c, YYLTYPE *locp, HexValue *value);

HexValue gen_rvalue_neg(Context *c, YYLTYPE *locp, HexValue *value);

HexValue gen_rvalue_brev(Context *c, YYLTYPE *locp, HexValue *value);

HexValue gen_rvalue_ternary(Context *c, YYLTYPE *locp, HexValue *cond,
                            HexValue *true_branch, HexValue *false_branch);

const char *cond_to_str(TCGCond cond);

void emit_header(Context *c);

void emit_arg(Context *c, YYLTYPE *locp, HexValue *arg);

void emit_footer(Context *c);

void track_string(Context *c, GString *s);

void free_variables(Context *c, YYLTYPE *locp);

void free_instruction(Context *c);

void assert_signedness(Context *c,
                       YYLTYPE *locp,
                       HexSignedness signedness);

#endif /* PARSER_HELPERS_h */
