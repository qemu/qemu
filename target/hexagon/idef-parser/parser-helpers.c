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

#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "idef-parser.h"
#include "parser-helpers.h"
#include "idef-parser.tab.h"
#include "idef-parser.yy.h"

void yyerror(YYLTYPE *locp,
             yyscan_t scanner __attribute__((unused)),
             Context *c,
             const char *s)
{
    const char *code_ptr = c->input_buffer;

    fprintf(stderr, "WARNING (%s): '%s'\n", c->inst.name->str, s);

    fprintf(stderr, "Problematic range: ");
    for (int i = locp->first_column; i < locp->last_column; i++) {
        if (code_ptr[i] != '\n') {
            fprintf(stderr, "%c", code_ptr[i]);
        }
    }
    fprintf(stderr, "\n");

    for (unsigned i = 0;
         i < 80 &&
         code_ptr[locp->first_column - 10 + i] != '\0' &&
         code_ptr[locp->first_column - 10 + i] != '\n';
         i++) {
        fprintf(stderr, "%c", code_ptr[locp->first_column - 10 + i]);
    }
    fprintf(stderr, "\n");
    for (unsigned i = 0; i < 9; i++) {
        fprintf(stderr, " ");
    }
    fprintf(stderr, "^");
    for (int i = 0; i < (locp->last_column - locp->first_column) - 1; i++) {
        fprintf(stderr, "~");
    }
    fprintf(stderr, "\n");
    c->inst.error_count++;
}

bool is_direct_predicate(HexValue *value)
{
    return value->pred.id >= '0' && value->pred.id <= '3';
}

bool is_inside_ternary(Context *c)
{
    return c->ternary->len > 0;
}

/* Print functions */
void str_print(Context *c, YYLTYPE *locp, const char *string)
{
    (void) locp;
    EMIT(c, "%s", string);
}

void uint8_print(Context *c, YYLTYPE *locp, uint8_t *num)
{
    (void) locp;
    EMIT(c, "%u", *num);
}

void uint64_print(Context *c, YYLTYPE *locp, uint64_t *num)
{
    (void) locp;
    EMIT(c, "%" PRIu64, *num);
}

void int_print(Context *c, YYLTYPE *locp, int *num)
{
    (void) locp;
    EMIT(c, "%d", *num);
}

void uint_print(Context *c, YYLTYPE *locp, unsigned *num)
{
    (void) locp;
    EMIT(c, "%u", *num);
}

void tmp_print(Context *c, YYLTYPE *locp, HexTmp *tmp)
{
    (void) locp;
    EMIT(c, "tmp_%d", tmp->index);
}

void pred_print(Context *c, YYLTYPE *locp, HexPred *pred, bool is_dotnew)
{
    (void) locp;
    char suffix = is_dotnew ? 'N' : 'V';
    EMIT(c, "P%c%c", pred->id, suffix);
}

void reg_compose(Context *c, YYLTYPE *locp, HexReg *reg, char reg_id[5])
{
    memset(reg_id, 0, 5 * sizeof(char));
    switch (reg->type) {
    case GENERAL_PURPOSE:
        reg_id[0] = 'R';
        break;
    case CONTROL:
        reg_id[0] = 'C';
        break;
    case MODIFIER:
        reg_id[0] = 'M';
        break;
    case DOTNEW:
        reg_id[0] = 'N';
        reg_id[1] = reg->id;
        reg_id[2] = 'N';
        return;
    }
    switch (reg->bit_width) {
    case 32:
        reg_id[1] = reg->id;
        reg_id[2] = 'V';
        break;
    case 64:
        reg_id[1] = reg->id;
        reg_id[2] = reg->id;
        reg_id[3] = 'V';
        break;
    default:
        yyassert(c, locp, false, "Unhandled register bit width!\n");
    }
}

static void reg_arg_print(Context *c, YYLTYPE *locp, HexReg *reg)
{
    char reg_id[5];
    reg_compose(c, locp, reg, reg_id);
    EMIT(c, "%s", reg_id);
}

void reg_print(Context *c, YYLTYPE *locp, HexReg *reg)
{
    (void) locp;
    EMIT(c, "hex_gpr[%u]", reg->id);
}

void imm_print(Context *c, YYLTYPE *locp, HexImm *imm)
{
    switch (imm->type) {
    case I:
        EMIT(c, "i");
        break;
    case VARIABLE:
        EMIT(c, "%ciV", imm->id);
        break;
    case VALUE:
        EMIT(c, "((int64_t) %" PRIu64 "ULL)", (int64_t) imm->value);
        break;
    case QEMU_TMP:
        EMIT(c, "qemu_tmp_%" PRIu64, imm->index);
        break;
    case IMM_PC:
        EMIT(c, "ctx->base.pc_next");
        break;
    case IMM_NPC:
        EMIT(c, "ctx->npc");
        break;
    case IMM_CONSTEXT:
        EMIT(c, "insn->extension_valid");
        break;
    default:
        yyassert(c, locp, false, "Cannot print this expression!");
    }
}

void var_print(Context *c, YYLTYPE *locp, HexVar *var)
{
    (void) locp;
    EMIT(c, "%s", var->name->str);
}

void rvalue_print(Context *c, YYLTYPE *locp, void *pointer)
{
  HexValue *rvalue = (HexValue *) pointer;
  switch (rvalue->type) {
  case REGISTER:
      reg_print(c, locp, &rvalue->reg);
      break;
  case REGISTER_ARG:
      reg_arg_print(c, locp, &rvalue->reg);
      break;
  case TEMP:
      tmp_print(c, locp, &rvalue->tmp);
      break;
  case IMMEDIATE:
      imm_print(c, locp, &rvalue->imm);
      break;
  case VARID:
      var_print(c, locp, &rvalue->var);
      break;
  case PREDICATE:
      pred_print(c, locp, &rvalue->pred, rvalue->is_dotnew);
      break;
  default:
      yyassert(c, locp, false, "Cannot print this expression!");
  }
}

void out_assert(Context *c, YYLTYPE *locp,
                void *dummy __attribute__((unused)))
{
    yyassert(c, locp, false, "Unhandled print type!");
}

/* Copy output code buffer */
void commit(Context *c)
{
    /* Emit instruction pseudocode */
    EMIT_SIG(c, "\n" START_COMMENT " ");
    for (char *x = c->inst.code_begin; x < c->inst.code_end; x++) {
        EMIT_SIG(c, "%c", *x);
    }
    EMIT_SIG(c, " " END_COMMENT "\n");

    /* Commit instruction code to output file */
    fwrite(c->signature_str->str, sizeof(char), c->signature_str->len,
           c->output_file);
    fwrite(c->header_str->str, sizeof(char), c->header_str->len,
           c->output_file);
    fwrite(c->out_str->str, sizeof(char), c->out_str->len,
           c->output_file);

    fwrite(c->signature_str->str, sizeof(char), c->signature_str->len,
           c->defines_file);
    fprintf(c->defines_file, ";\n");
}

static void gen_c_int_type(Context *c, YYLTYPE *locp, unsigned bit_width,
                           HexSignedness signedness)
{
    const char *signstr = (signedness == UNSIGNED) ? "u" : "";
    OUT(c, locp, signstr, "int", &bit_width, "_t");
}

static HexValue gen_constant(Context *c,
                             YYLTYPE *locp,
                             const char *value,
                             unsigned bit_width,
                             HexSignedness signedness)
{
    HexValue rvalue;
    assert(bit_width == 32 || bit_width == 64);
    memset(&rvalue, 0, sizeof(HexValue));
    rvalue.type = TEMP;
    rvalue.bit_width = bit_width;
    rvalue.signedness = signedness;
    rvalue.is_dotnew = false;
    rvalue.tmp.index = c->inst.tmp_count;
    OUT(c, locp, "TCGv_i", &bit_width, " tmp_", &c->inst.tmp_count,
        " = tcg_constant_i", &bit_width, "(", value, ");\n");
    c->inst.tmp_count++;
    return rvalue;
}

/* Temporary values creation */
HexValue gen_tmp(Context *c,
                 YYLTYPE *locp,
                 unsigned bit_width,
                 HexSignedness signedness)
{
    HexValue rvalue;
    assert(bit_width == 32 || bit_width == 64);
    memset(&rvalue, 0, sizeof(HexValue));
    rvalue.type = TEMP;
    rvalue.bit_width = bit_width;
    rvalue.signedness = signedness;
    rvalue.is_dotnew = false;
    rvalue.tmp.index = c->inst.tmp_count;
    OUT(c, locp, "TCGv_i", &bit_width, " tmp_", &c->inst.tmp_count,
        " = tcg_temp_new_i", &bit_width, "();\n");
    c->inst.tmp_count++;
    return rvalue;
}

HexValue gen_tmp_value(Context *c,
                       YYLTYPE *locp,
                       const char *value,
                       unsigned bit_width,
                       HexSignedness signedness)
{
    HexValue rvalue;
    assert(bit_width == 32 || bit_width == 64);
    memset(&rvalue, 0, sizeof(HexValue));
    rvalue.type = TEMP;
    rvalue.bit_width = bit_width;
    rvalue.signedness = signedness;
    rvalue.is_dotnew = false;
    rvalue.tmp.index = c->inst.tmp_count;
    OUT(c, locp, "TCGv_i", &bit_width, " tmp_", &c->inst.tmp_count,
        " = tcg_const_i", &bit_width, "(", value, ");\n");
    c->inst.tmp_count++;
    return rvalue;
}

static HexValue gen_tmp_value_from_imm(Context *c,
                                       YYLTYPE *locp,
                                       HexValue *value)
{
    HexValue rvalue;
    assert(value->type == IMMEDIATE);
    memset(&rvalue, 0, sizeof(HexValue));
    rvalue.type = TEMP;
    rvalue.bit_width = value->bit_width;
    rvalue.signedness = value->signedness;
    rvalue.is_dotnew = false;
    rvalue.tmp.index = c->inst.tmp_count;
    /*
     * Here we output the call to `tcg_const_i<width>` in
     * order to create the temporary value. Note, that we
     * add a cast
     *
     *   `tcg_const_i<width>`((int<width>_t) ...)`
     *
     * This cast is required to avoid implicit integer
     * conversion warnings since all immediates are
     * output as `((int64_t) 123ULL)`, even if the
     * integer is 32-bit.
     */
    OUT(c, locp, "TCGv_i", &rvalue.bit_width, " tmp_", &c->inst.tmp_count);
    OUT(c, locp, " = tcg_const_i", &rvalue.bit_width,
        "((int", &rvalue.bit_width, "_t) (", value, "));\n");

    c->inst.tmp_count++;
    return rvalue;
}

HexValue gen_imm_value(Context *c __attribute__((unused)),
                       YYLTYPE *locp,
                       int value,
                       unsigned bit_width,
                       HexSignedness signedness)
{
    (void) locp;
    HexValue rvalue;
    assert(bit_width == 32 || bit_width == 64);
    memset(&rvalue, 0, sizeof(HexValue));
    rvalue.type = IMMEDIATE;
    rvalue.bit_width = bit_width;
    rvalue.signedness = signedness;
    rvalue.is_dotnew = false;
    rvalue.imm.type = VALUE;
    rvalue.imm.value = value;
    return rvalue;
}

HexValue gen_imm_qemu_tmp(Context *c, YYLTYPE *locp, unsigned bit_width,
                          HexSignedness signedness)
{
    (void) locp;
    HexValue rvalue;
    assert(bit_width == 32 || bit_width == 64);
    memset(&rvalue, 0, sizeof(HexValue));
    rvalue.type = IMMEDIATE;
    rvalue.is_dotnew = false;
    rvalue.bit_width = bit_width;
    rvalue.signedness = signedness;
    rvalue.imm.type = QEMU_TMP;
    rvalue.imm.index = c->inst.qemu_tmp_count++;
    return rvalue;
}

HexValue rvalue_materialize(Context *c, YYLTYPE *locp, HexValue *rvalue)
{
    if (rvalue->type == IMMEDIATE) {
        HexValue res = gen_tmp_value_from_imm(c, locp, rvalue);
        return res;
    }
    return *rvalue;
}

HexValue gen_rvalue_extend(Context *c, YYLTYPE *locp, HexValue *rvalue)
{
    assert_signedness(c, locp, rvalue->signedness);
    if (rvalue->bit_width > 32) {
        return *rvalue;
    }

    if (rvalue->type == IMMEDIATE) {
        HexValue res = gen_imm_qemu_tmp(c, locp, 64, rvalue->signedness);
        bool is_unsigned = (rvalue->signedness == UNSIGNED);
        const char *sign_suffix = is_unsigned ? "u" : "";
        gen_c_int_type(c, locp, 64, rvalue->signedness);
        OUT(c, locp, " ", &res, " = ");
        OUT(c, locp, "(", sign_suffix, "int64_t) ");
        OUT(c, locp, "(", sign_suffix, "int32_t) ");
        OUT(c, locp, rvalue, ";\n");
        return res;
    } else {
        HexValue res = gen_tmp(c, locp, 64, rvalue->signedness);
        bool is_unsigned = (rvalue->signedness == UNSIGNED);
        const char *sign_suffix = is_unsigned ? "u" : "";
        OUT(c, locp, "tcg_gen_ext", sign_suffix,
            "_i32_i64(", &res, ", ", rvalue, ");\n");
        return res;
    }
}

HexValue gen_rvalue_truncate(Context *c, YYLTYPE *locp, HexValue *rvalue)
{
    if (rvalue->type == IMMEDIATE) {
        HexValue res = *rvalue;
        res.bit_width = 32;
        return res;
    } else {
        if (rvalue->bit_width == 64) {
            HexValue res = gen_tmp(c, locp, 32, rvalue->signedness);
            OUT(c, locp, "tcg_gen_trunc_i64_tl(", &res, ", ", rvalue, ");\n");
            return res;
        }
    }
    return *rvalue;
}

/*
 * Attempts to lookup the `Var` struct associated with the given `varid`.
 * The `dst` argument is populated with the found name, bit_width, and
 * signedness, given that `dst` is non-NULL. Returns true if the lookup
 * succeeded and false otherwise.
 */
static bool try_find_variable(Context *c, YYLTYPE *locp,
                              HexValue *dst,
                              HexValue *varid)
{
    yyassert(c, locp, varid, "varid to lookup is NULL");
    yyassert(c, locp, varid->type == VARID,
             "Can only lookup variables by varid");
    for (unsigned i = 0; i < c->inst.allocated->len; i++) {
        Var *curr = &g_array_index(c->inst.allocated, Var, i);
        if (g_string_equal(varid->var.name, curr->name)) {
            if (dst) {
                dst->var.name = curr->name;
                dst->bit_width = curr->bit_width;
                dst->signedness = curr->signedness;
            }
            return true;
        }
    }
    return false;
}

/* Calls `try_find_variable` and asserts succcess. */
static void find_variable(Context *c, YYLTYPE *locp,
                          HexValue *dst,
                          HexValue *varid)
{
    bool found = try_find_variable(c, locp, dst, varid);
    yyassert(c, locp, found, "Use of undeclared variable!\n");
}

/* Handle signedness, if both unsigned -> result is unsigned, else signed */
static inline HexSignedness bin_op_signedness(Context *c, YYLTYPE *locp,
                                              HexSignedness sign1,
                                              HexSignedness sign2)
{
    assert_signedness(c, locp, sign1);
    assert_signedness(c, locp, sign2);
    return (sign1 == UNSIGNED && sign2 == UNSIGNED) ? UNSIGNED : SIGNED;
}

void gen_varid_allocate(Context *c,
                        YYLTYPE *locp,
                        HexValue *varid,
                        unsigned bit_width,
                        HexSignedness signedness)
{
    const char *bit_suffix = (bit_width == 64) ? "i64" : "i32";
    bool found = try_find_variable(c, locp, NULL, varid);
    Var new_var;

    memset(&new_var, 0, sizeof(Var));

    yyassert(c, locp, !found, "Redeclaration of variables not allowed!");
    assert_signedness(c, locp, signedness);

    /* `varid` only carries name information */
    new_var.name = varid->var.name;
    new_var.bit_width = bit_width;
    new_var.signedness = signedness;

    EMIT_HEAD(c, "TCGv_%s %s", bit_suffix, varid->var.name->str);
    EMIT_HEAD(c, " = tcg_temp_new_%s();\n", bit_suffix);
    g_array_append_val(c->inst.allocated, new_var);
}

enum OpTypes {
    IMM_IMM = 0,
    IMM_REG = 1,
    REG_IMM = 2,
    REG_REG = 3,
};

HexValue gen_bin_cmp(Context *c,
                     YYLTYPE *locp,
                     TCGCond type,
                     HexValue *op1,
                     HexValue *op2)
{
    HexValue op1_m = *op1;
    HexValue op2_m = *op2;
    enum OpTypes op_types = (op1_m.type != IMMEDIATE) << 1
                            | (op2_m.type != IMMEDIATE);

    bool op_is64bit = op1_m.bit_width == 64 || op2_m.bit_width == 64;
    const char *bit_suffix = op_is64bit ? "i64" : "i32";
    unsigned bit_width = (op_is64bit) ? 64 : 32;
    HexValue res = gen_tmp(c, locp, bit_width, UNSIGNED);

    /* Extend to 64-bits, if required */
    if (op_is64bit) {
        op1_m = gen_rvalue_extend(c, locp, &op1_m);
        op2_m = gen_rvalue_extend(c, locp, &op2_m);
    }

    switch (op_types) {
    case IMM_IMM:
    case IMM_REG:
        yyassert(c, locp, false, "Binary comparisons between IMM op IMM and"
                                 "IMM op REG not handled!");
        break;
    case REG_IMM:
        OUT(c, locp, "tcg_gen_setcondi_", bit_suffix, "(");
        OUT(c, locp, cond_to_str(type), ", ", &res, ", ", &op1_m, ", ", &op2_m,
            ");\n");
        break;
    case REG_REG:
        OUT(c, locp, "tcg_gen_setcond_", bit_suffix, "(");
        OUT(c, locp, cond_to_str(type), ", ", &res, ", ", &op1_m, ", ", &op2_m,
            ");\n");
        break;
    default:
        fprintf(stderr, "Error in evalutating immediateness!");
        abort();
    }
    return res;
}

static void gen_simple_op(Context *c, YYLTYPE *locp, unsigned bit_width,
                          const char *bit_suffix, HexValue *res,
                          enum OpTypes op_types,
                          HexValue *op1,
                          HexValue *op2,
                          const char *imm_imm,
                          const char *imm_reg,
                          const char *reg_imm,
                          const char *reg_reg)
{
    switch (op_types) {
    case IMM_IMM: {
        HexSignedness signedness = bin_op_signedness(c, locp,
                                                     op1->signedness,
                                                     op2->signedness);
        gen_c_int_type(c, locp, bit_width, signedness);
        OUT(c, locp, " ", res,
            " = ", op1, imm_imm, op2, ";\n");
    } break;
    case IMM_REG:
        OUT(c, locp, imm_reg, bit_suffix,
            "(", res, ", ", op2, ", ", op1, ");\n");
        break;
    case REG_IMM:
        OUT(c, locp, reg_imm, bit_suffix,
            "(", res, ", ", op1, ", ", op2, ");\n");
        break;
    case REG_REG:
        OUT(c, locp, reg_reg, bit_suffix,
            "(", res, ", ", op1, ", ", op2, ");\n");
        break;
    }
}

static void gen_sub_op(Context *c, YYLTYPE *locp, unsigned bit_width,
                       const char *bit_suffix, HexValue *res,
                       enum OpTypes op_types, HexValue *op1,
                       HexValue *op2)
{
    switch (op_types) {
    case IMM_IMM: {
        HexSignedness signedness = bin_op_signedness(c, locp,
                                                     op1->signedness,
                                                     op2->signedness);
        gen_c_int_type(c, locp, bit_width, signedness);
        OUT(c, locp, " ", res,
            " = ", op1, " - ", op2, ";\n");
    } break;
    case IMM_REG: {
        OUT(c, locp, "tcg_gen_subfi_", bit_suffix,
            "(", res, ", ", op1, ", ", op2, ");\n");
    } break;
    case REG_IMM: {
        OUT(c, locp, "tcg_gen_subi_", bit_suffix,
            "(", res, ", ", op1, ", ", op2, ");\n");
    } break;
    case REG_REG: {
        OUT(c, locp, "tcg_gen_sub_", bit_suffix,
            "(", res, ", ", op1, ", ", op2, ");\n");
    } break;
    }
}

static void gen_asl_op(Context *c, YYLTYPE *locp, unsigned bit_width,
                       bool op_is64bit, const char *bit_suffix,
                       HexValue *res, enum OpTypes op_types,
                       HexValue *op1, HexValue *op2)
{
    HexValue op1_m = *op1;
    HexValue op2_m = *op2;
    switch (op_types) {
    case IMM_IMM: {
        HexSignedness signedness = bin_op_signedness(c, locp,
                                                     op1->signedness,
                                                     op2->signedness);
        gen_c_int_type(c, locp, bit_width, signedness);
        OUT(c, locp, " ", res,
            " = ", op1, " << ", op2, ";\n");
    } break;
    case REG_IMM: {
        OUT(c, locp, "if (", op2, " >= ", &bit_width, ") {\n");
        OUT(c, locp, "tcg_gen_movi_", bit_suffix, "(", res, ", 0);\n");
        OUT(c, locp, "} else {\n");
        OUT(c, locp, "tcg_gen_shli_", bit_suffix,
                "(", res, ", ", op1, ", ", op2, ");\n");
        OUT(c, locp, "}\n");
    } break;
    case IMM_REG:
        op1_m.bit_width = bit_width;
        op1_m = rvalue_materialize(c, locp, &op1_m);
        /* fallthrough */
    case REG_REG: {
        OUT(c, locp, "tcg_gen_shl_", bit_suffix,
            "(", res, ", ", &op1_m, ", ", op2, ");\n");
    } break;
    }
    if (op_types == IMM_REG || op_types == REG_REG) {
        /*
         * Handle left shift by 64/32 which hexagon-sim expects to clear out
         * register
         */
        HexValue zero = gen_constant(c, locp, "0", bit_width, UNSIGNED);
        HexValue edge = gen_imm_value(c, locp, bit_width, bit_width, UNSIGNED);
        edge = rvalue_materialize(c, locp, &edge);
        if (op_is64bit) {
            op2_m = gen_rvalue_extend(c, locp, &op2_m);
        }
        op1_m = rvalue_materialize(c, locp, &op1_m);
        op2_m = rvalue_materialize(c, locp, &op2_m);
        OUT(c, locp, "tcg_gen_movcond_i", &bit_width);
        OUT(c, locp, "(TCG_COND_GEU, ", res, ", ", &op2_m, ", ", &edge);
        OUT(c, locp, ", ", &zero, ", ", res, ");\n");
    }
}

static void gen_asr_op(Context *c, YYLTYPE *locp, unsigned bit_width,
                       bool op_is64bit, const char *bit_suffix,
                       HexValue *res, enum OpTypes op_types,
                       HexValue *op1, HexValue *op2)
{
    HexValue op1_m = *op1;
    HexValue op2_m = *op2;
    switch (op_types) {
    case IMM_IMM:
    case IMM_REG:
        yyassert(c, locp, false, "ASR between IMM op IMM, and IMM op REG"
                                 " not handled!");
        break;
    case REG_IMM: {
        HexSignedness signedness = bin_op_signedness(c, locp,
                                                     op1->signedness,
                                                     op2->signedness);
        OUT(c, locp, "{\n");
        gen_c_int_type(c, locp, bit_width, signedness);
        OUT(c, locp, " shift = ", op2, ";\n");
        OUT(c, locp, "if (", op2, " >= ", &bit_width, ") {\n");
        OUT(c, locp, "    shift = ", &bit_width, " - 1;\n");
        OUT(c, locp, "}\n");
        OUT(c, locp, "tcg_gen_sari_", bit_suffix,
            "(", res, ", ", op1, ", shift);\n}\n");
    } break;
    case REG_REG:
        OUT(c, locp, "tcg_gen_sar_", bit_suffix,
            "(", res, ", ", &op1_m, ", ", op2, ");\n");
        break;
    }
    if (op_types == REG_REG) {
        /* Handle right shift by values >= bit_width */
        const char *offset = op_is64bit ? "63" : "31";
        HexValue tmp = gen_tmp(c, locp, bit_width, SIGNED);
        HexValue zero = gen_constant(c, locp, "0", bit_width, SIGNED);
        HexValue edge = gen_imm_value(c, locp, bit_width, bit_width, UNSIGNED);

        edge = rvalue_materialize(c, locp, &edge);
        if (op_is64bit) {
            op2_m = gen_rvalue_extend(c, locp, &op2_m);
        }
        op1_m = rvalue_materialize(c, locp, &op1_m);
        op2_m = rvalue_materialize(c, locp, &op2_m);

        OUT(c, locp, "tcg_gen_extract_", bit_suffix, "(",
            &tmp, ", ", &op1_m, ", ", offset, ", 1);\n");
        OUT(c, locp, "tcg_gen_sub_", bit_suffix, "(",
            &tmp, ", ", &zero, ", ", &tmp, ");\n");
        OUT(c, locp, "tcg_gen_movcond_i", &bit_width);
        OUT(c, locp, "(TCG_COND_GEU, ", res, ", ", &op2_m, ", ", &edge);
        OUT(c, locp, ", ", &tmp, ", ", res, ");\n");
    }
}

static void gen_lsr_op(Context *c, YYLTYPE *locp, unsigned bit_width,
                       bool op_is64bit, const char *bit_suffix,
                       HexValue *res, enum OpTypes op_types,
                       HexValue *op1, HexValue *op2)
{
    HexValue op1_m = *op1;
    HexValue op2_m = *op2;
    switch (op_types) {
    case IMM_IMM:
    case IMM_REG:
        yyassert(c, locp, false, "LSR between IMM op IMM, and IMM op REG"
                                 " not handled!");
        break;
    case REG_IMM:
        OUT(c, locp, "if (", op2, " >= ", &bit_width, ") {\n");
        OUT(c, locp, "tcg_gen_movi_", bit_suffix, "(", res, ", 0);\n");
        OUT(c, locp, "} else {\n");
        OUT(c, locp, "tcg_gen_shri_", bit_suffix,
            "(", res, ", ", op1, ", ", op2, ");\n");
        OUT(c, locp, "}\n");
        break;
    case REG_REG:
        OUT(c, locp, "tcg_gen_shr_", bit_suffix,
            "(", res, ", ", &op1_m, ", ", op2, ");\n");
        break;
    }
    if (op_types == REG_REG) {
        /* Handle right shift by values >= bit_width */
        HexValue zero = gen_constant(c, locp, "0", bit_width, UNSIGNED);
        HexValue edge = gen_imm_value(c, locp, bit_width, bit_width, UNSIGNED);
        edge = rvalue_materialize(c, locp, &edge);
        if (op_is64bit) {
            op2_m = gen_rvalue_extend(c, locp, &op2_m);
        }
        op1_m = rvalue_materialize(c, locp, &op1_m);
        op2_m = rvalue_materialize(c, locp, &op2_m);
        OUT(c, locp, "tcg_gen_movcond_i", &bit_width);
        OUT(c, locp, "(TCG_COND_GEU, ", res, ", ", &op2_m, ", ", &edge);
        OUT(c, locp, ", ", &zero, ", ", res, ");\n");
    }
}

/*
 * Note: This implementation of logical `and` does not mirror that in C.
 * We do not short-circuit logical expressions!
 */
static void gen_andl_op(Context *c, YYLTYPE *locp, unsigned bit_width,
                        const char *bit_suffix, HexValue *res,
                        enum OpTypes op_types, HexValue *op1,
                        HexValue *op2)
{
    (void) bit_width;
    HexValue tmp1, tmp2;
    HexValue zero = gen_constant(c, locp, "0", 32, UNSIGNED);
    memset(&tmp1, 0, sizeof(HexValue));
    memset(&tmp2, 0, sizeof(HexValue));
    switch (op_types) {
    case IMM_IMM:
    case IMM_REG:
    case REG_IMM:
        yyassert(c, locp, false, "ANDL between IMM op IMM, IMM op REG, and"
                                 " REG op IMM, not handled!");
        break;
    case REG_REG:
        tmp1 = gen_bin_cmp(c, locp, TCG_COND_NE, op1, &zero);
        tmp2 = gen_bin_cmp(c, locp, TCG_COND_NE, op2, &zero);
        OUT(c, locp, "tcg_gen_and_", bit_suffix,
            "(", res, ", ", &tmp1, ", ", &tmp2, ");\n");
        break;
    }
}

static void gen_minmax_op(Context *c, YYLTYPE *locp, unsigned bit_width,
                          HexValue *res, enum OpTypes op_types,
                          HexValue *op1, HexValue *op2, bool minmax)
{
    const char *mm;
    HexValue op1_m = *op1;
    HexValue op2_m = *op2;
    bool is_unsigned;

    assert_signedness(c, locp, res->signedness);
    is_unsigned = res->signedness == UNSIGNED;

    if (minmax) {
        /* Max */
        mm = is_unsigned ? "tcg_gen_umax" : "tcg_gen_smax";
    } else {
        /* Min */
        mm = is_unsigned ? "tcg_gen_umin" : "tcg_gen_smin";
    }
    switch (op_types) {
    case IMM_IMM:
        yyassert(c, locp, false, "MINMAX between IMM op IMM, not handled!");
        break;
    case IMM_REG:
        op1_m.bit_width = bit_width;
        op1_m = rvalue_materialize(c, locp, &op1_m);
        OUT(c, locp, mm, "_i", &bit_width, "(");
        OUT(c, locp, res, ", ", &op1_m, ", ", op2, ");\n");
        break;
    case REG_IMM:
        op2_m.bit_width = bit_width;
        op2_m = rvalue_materialize(c, locp, &op2_m);
        /* Fallthrough */
    case REG_REG:
        OUT(c, locp, mm, "_i", &bit_width, "(");
        OUT(c, locp, res, ", ", op1, ", ", &op2_m, ");\n");
        break;
    }
}

/* Code generation functions */
HexValue gen_bin_op(Context *c,
                    YYLTYPE *locp,
                    OpType type,
                    HexValue *op1,
                    HexValue *op2)
{
    /* Replicate operands to avoid side effects */
    HexValue op1_m = *op1;
    HexValue op2_m = *op2;
    enum OpTypes op_types;
    bool op_is64bit;
    HexSignedness signedness;
    unsigned bit_width;
    const char *bit_suffix;
    HexValue res;

    memset(&res, 0, sizeof(HexValue));

    /*
     * If the operands are VARID's we need to look up the
     * type information.
     */
    if (op1_m.type == VARID) {
        find_variable(c, locp, &op1_m, &op1_m);
    }
    if (op2_m.type == VARID) {
        find_variable(c, locp, &op2_m, &op2_m);
    }

    op_types = (op1_m.type != IMMEDIATE) << 1
               | (op2_m.type != IMMEDIATE);
    op_is64bit = op1_m.bit_width == 64 || op2_m.bit_width == 64;
    /* Shift greater than 32 are 64 bits wide */

    if (type == ASL_OP && op2_m.type == IMMEDIATE &&
        op2_m.imm.type == VALUE && op2_m.imm.value >= 32) {
        op_is64bit = true;
    }

    bit_width = (op_is64bit) ? 64 : 32;
    bit_suffix = op_is64bit ? "i64" : "i32";

    /* Extend to 64-bits, if required */
    if (op_is64bit) {
        op1_m = gen_rvalue_extend(c, locp, &op1_m);
        op2_m = gen_rvalue_extend(c, locp, &op2_m);
    }

    signedness = bin_op_signedness(c, locp, op1_m.signedness, op2_m.signedness);
    if (op_types != IMM_IMM) {
        res = gen_tmp(c, locp, bit_width, signedness);
    } else {
        res = gen_imm_qemu_tmp(c, locp, bit_width, signedness);
    }

    switch (type) {
    case ADD_OP:
        gen_simple_op(c, locp, bit_width, bit_suffix, &res,
                      op_types, &op1_m, &op2_m,
                      " + ",
                      "tcg_gen_addi_",
                      "tcg_gen_addi_",
                      "tcg_gen_add_");
        break;
    case SUB_OP:
        gen_sub_op(c, locp, bit_width, bit_suffix, &res, op_types,
                   &op1_m, &op2_m);
        break;
    case MUL_OP:
        gen_simple_op(c, locp, bit_width, bit_suffix, &res,
                      op_types, &op1_m, &op2_m,
                      " * ",
                      "tcg_gen_muli_",
                      "tcg_gen_muli_",
                      "tcg_gen_mul_");
        break;
    case ASL_OP:
        gen_asl_op(c, locp, bit_width, op_is64bit, bit_suffix, &res, op_types,
                   &op1_m, &op2_m);
        break;
    case ASR_OP:
        gen_asr_op(c, locp, bit_width, op_is64bit, bit_suffix, &res, op_types,
                   &op1_m, &op2_m);
        break;
    case LSR_OP:
        gen_lsr_op(c, locp, bit_width, op_is64bit, bit_suffix, &res, op_types,
                   &op1_m, &op2_m);
        break;
    case ANDB_OP:
        gen_simple_op(c, locp, bit_width, bit_suffix, &res,
                      op_types, &op1_m, &op2_m,
                      " & ",
                      "tcg_gen_andi_",
                      "tcg_gen_andi_",
                      "tcg_gen_and_");
        break;
    case ORB_OP:
        gen_simple_op(c, locp, bit_width, bit_suffix, &res,
                      op_types, &op1_m, &op2_m,
                      " | ",
                      "tcg_gen_ori_",
                      "tcg_gen_ori_",
                      "tcg_gen_or_");
        break;
    case XORB_OP:
        gen_simple_op(c, locp, bit_width, bit_suffix, &res,
                      op_types, &op1_m, &op2_m,
                      " ^ ",
                      "tcg_gen_xori_",
                      "tcg_gen_xori_",
                      "tcg_gen_xor_");
        break;
    case ANDL_OP:
        gen_andl_op(c, locp, bit_width, bit_suffix, &res, op_types, &op1_m,
                    &op2_m);
        break;
    case MINI_OP:
        gen_minmax_op(c, locp, bit_width, &res, op_types, &op1_m, &op2_m,
                      false);
        break;
    case MAXI_OP:
        gen_minmax_op(c, locp, bit_width, &res, op_types, &op1_m, &op2_m, true);
        break;
    }
    return res;
}

HexValue gen_cast_op(Context *c,
                     YYLTYPE *locp,
                     HexValue *src,
                     unsigned target_width,
                     HexSignedness signedness)
{
    assert_signedness(c, locp, src->signedness);
    if (src->bit_width == target_width) {
        return *src;
    } else if (src->type == IMMEDIATE) {
        HexValue res = *src;
        res.bit_width = target_width;
        res.signedness = signedness;
        return res;
    } else {
        HexValue res = gen_tmp(c, locp, target_width, signedness);
        /* Truncate */
        if (src->bit_width > target_width) {
            OUT(c, locp, "tcg_gen_trunc_i64_tl(", &res, ", ", src, ");\n");
        } else {
            assert_signedness(c, locp, src->signedness);
            if (src->signedness == UNSIGNED) {
                /* Extend unsigned */
                OUT(c, locp, "tcg_gen_extu_i32_i64(",
                    &res, ", ", src, ");\n");
            } else {
                /* Extend signed */
                OUT(c, locp, "tcg_gen_ext_i32_i64(",
                    &res, ", ", src, ");\n");
            }
        }
        return res;
    }
}


/*
 * Implements an extension when the `src_width` is an immediate.
 * If the `value` to extend is also an immediate we use `extract/sextract`
 * from QEMU `bitops.h`. If `value` is a TCGv then we rely on
 * `tcg_gen_extract/tcg_gen_sextract`.
 */
static HexValue gen_extend_imm_width_op(Context *c,
                                        YYLTYPE *locp,
                                        HexValue *src_width,
                                        unsigned dst_width,
                                        HexValue *value,
                                        HexSignedness signedness)
{
    /*
     * If the source width is not an immediate value, we need to guard
     * our extend op with if statements to handle the case where
     * `src_width_m` is 0.
     */
    const char *sign_prefix;
    bool need_guarding;

    assert_signedness(c, locp, signedness);
    assert(dst_width == 64 || dst_width == 32);
    assert(src_width->type == IMMEDIATE);

    sign_prefix = (signedness == UNSIGNED) ? "" : "s";
    need_guarding = (src_width->imm.type != VALUE);

    if (src_width->imm.type == VALUE &&
        src_width->imm.value == 0) {
        /*
         * We can bail out early if the source width is known to be zero
         * at translation time.
         */
        return gen_imm_value(c, locp, 0, dst_width, signedness);
    }

    if (value->type == IMMEDIATE) {
        /*
         * If both the value and source width are immediates,
         * we can perform the extension at translation time
         * using QEMUs bitops.
         */
        HexValue res = gen_imm_qemu_tmp(c, locp, dst_width, signedness);
        gen_c_int_type(c, locp, dst_width, signedness);
        OUT(c, locp, " ", &res, " = 0;\n");
        if (need_guarding) {
            OUT(c, locp, "if (", src_width, " != 0) {\n");
        }
        OUT(c, locp, &res, " = ", sign_prefix, "extract", &dst_width);
        OUT(c, locp, "(", value, ", 0, ", src_width, ");\n");
        if (need_guarding) {
            OUT(c, locp, "}\n");
        }
        return res;
    } else {
        /*
         * If the source width is an immediate and the value to
         * extend is a TCGv, then use tcg_gen_extract/tcg_gen_sextract
         */
        HexValue res = gen_tmp(c, locp, dst_width, signedness);

        /*
         * If the width is an immediate value we know it is non-zero
         * at this point, otherwise we need an if-statement
         */
        if (need_guarding) {
            OUT(c, locp, "if (", src_width, " != 0) {\n");
        }
        OUT(c, locp, "tcg_gen_", sign_prefix, "extract_i", &dst_width);
        OUT(c, locp, "(", &res, ", ", value, ", 0, ", src_width,
            ");\n");
        if (need_guarding) {
            OUT(c, locp, "} else {\n");
            OUT(c, locp, "tcg_gen_movi_i", &dst_width, "(", &res,
                ", 0);\n");
            OUT(c, locp, "}\n");
        }
        return res;
    }
}

/*
 * Implements an extension when the `src_width` is given by
 * a TCGv. Here we need to reimplement the behaviour of
 * `tcg_gen_extract` and the like using shifts and masks.
 */
static HexValue gen_extend_tcg_width_op(Context *c,
                                        YYLTYPE *locp,
                                        HexValue *src_width,
                                        unsigned dst_width,
                                        HexValue *value,
                                        HexSignedness signedness)
{
    HexValue src_width_m = rvalue_materialize(c, locp, src_width);
    HexValue zero = gen_constant(c, locp, "0", dst_width, UNSIGNED);
    HexValue shift = gen_tmp(c, locp, dst_width, UNSIGNED);
    HexValue res;

    assert_signedness(c, locp, signedness);
    assert(dst_width == 64 || dst_width == 32);
    assert(src_width->type != IMMEDIATE);

    res = gen_tmp(c, locp, dst_width, signedness);

    OUT(c, locp, "tcg_gen_subfi_i", &dst_width);
    OUT(c, locp, "(", &shift, ", ", &dst_width, ", ", &src_width_m, ");\n");
    if (signedness == UNSIGNED) {
        const char *mask_str = (dst_width == 32)
            ? "0xffffffff"
            : "0xffffffffffffffff";
        HexValue mask = gen_tmp_value(c, locp, mask_str,
                                     dst_width, UNSIGNED);
        OUT(c, locp, "tcg_gen_shr_i", &dst_width, "(",
            &mask, ", ", &mask, ", ", &shift, ");\n");
        OUT(c, locp, "tcg_gen_and_i", &dst_width, "(",
            &res, ", ", value, ", ", &mask, ");\n");
    } else {
        OUT(c, locp, "tcg_gen_shl_i", &dst_width, "(",
            &res, ", ", value, ", ", &shift, ");\n");
        OUT(c, locp, "tcg_gen_sar_i", &dst_width, "(",
            &res, ", ", &res, ", ", &shift, ");\n");
    }
    OUT(c, locp, "tcg_gen_movcond_i", &dst_width, "(TCG_COND_EQ, ", &res,
        ", ");
    OUT(c, locp, &src_width_m, ", ", &zero, ", ", &zero, ", ", &res,
        ");\n");

    return res;
}

HexValue gen_extend_op(Context *c,
                       YYLTYPE *locp,
                       HexValue *src_width,
                       unsigned dst_width,
                       HexValue *value,
                       HexSignedness signedness)
{
    unsigned bit_width = (dst_width = 64) ? 64 : 32;
    HexValue value_m = *value;
    HexValue src_width_m = *src_width;

    assert_signedness(c, locp, signedness);
    yyassert(c, locp, value_m.bit_width <= bit_width &&
                      src_width_m.bit_width <= bit_width,
                      "Extending to a size smaller than the current size"
                      " makes no sense");

    if (value_m.bit_width < bit_width) {
        value_m = gen_rvalue_extend(c, locp, &value_m);
    }

    if (src_width_m.bit_width < bit_width) {
        src_width_m = gen_rvalue_extend(c, locp, &src_width_m);
    }

    if (src_width_m.type == IMMEDIATE) {
        return gen_extend_imm_width_op(c, locp, &src_width_m, bit_width,
                                       &value_m, signedness);
    } else {
        return gen_extend_tcg_width_op(c, locp, &src_width_m, bit_width,
                                       &value_m, signedness);
    }
}

/*
 * Implements `rdeposit` for the special case where `width`
 * is of TCGv type. In this case we need to reimplement the behaviour
 * of `tcg_gen_deposit*` using binary operations and masks/shifts.
 *
 * Note: this is the only type of `rdeposit` that occurs, meaning the
 * `width` is _NEVER_ of IMMEDIATE type.
 */
void gen_rdeposit_op(Context *c,
                     YYLTYPE *locp,
                     HexValue *dst,
                     HexValue *value,
                     HexValue *begin,
                     HexValue *width)
{
    /*
     * Otherwise if the width is not known, we fallback on reimplementing
     * desposit in TCG.
     */
    HexValue begin_m = *begin;
    HexValue value_m = *value;
    HexValue width_m = *width;
    const char *mask_str = (dst->bit_width == 32)
        ? "0xffffffffUL"
        : "0xffffffffffffffffUL";
    HexValue mask = gen_constant(c, locp, mask_str, dst->bit_width,
                                 UNSIGNED);
    const char *dst_width_str = (dst->bit_width == 32) ? "32" : "64";
    HexValue k64 = gen_constant(c, locp, dst_width_str, dst->bit_width,
                                UNSIGNED);
    HexValue res;
    HexValue zero;

    assert(dst->bit_width >= value->bit_width);
    assert(begin->type == IMMEDIATE && begin->imm.type == VALUE);
    assert(dst->type == REGISTER_ARG);

    yyassert(c, locp, width->type != IMMEDIATE,
             "Immediate index to rdeposit not handled!");

    yyassert(c, locp, value_m.bit_width == dst->bit_width &&
                      begin_m.bit_width == dst->bit_width &&
                      width_m.bit_width == dst->bit_width,
                      "Extension/truncation should be taken care of"
                      " before rdeposit!");

    width_m = rvalue_materialize(c, locp, &width_m);

    /*
     * mask = 0xffffffffffffffff >> (64 - width)
     * mask = mask << begin
     * value = (value << begin) & mask
     * res = dst & ~mask
     * res = res | value
     * dst = (width != 0) ? res : dst
     */
    k64 = gen_bin_op(c, locp, SUB_OP, &k64, &width_m);
    mask = gen_bin_op(c, locp, LSR_OP, &mask, &k64);
    mask = gen_bin_op(c, locp, ASL_OP, &mask, &begin_m);
    value_m = gen_bin_op(c, locp, ASL_OP, &value_m, &begin_m);
    value_m = gen_bin_op(c, locp, ANDB_OP, &value_m, &mask);

    OUT(c, locp, "tcg_gen_not_i", &dst->bit_width, "(", &mask, ", ",
        &mask, ");\n");
    res = gen_bin_op(c, locp, ANDB_OP, dst, &mask);
    res = gen_bin_op(c, locp, ORB_OP, &res, &value_m);

    /*
     * We don't need to truncate `res` here, since all operations involved use
     * the same bit width.
     */

    /* If the width is zero, then return the identity dst = dst */
    zero = gen_constant(c, locp, "0", res.bit_width, UNSIGNED);
    OUT(c, locp, "tcg_gen_movcond_i", &res.bit_width, "(TCG_COND_NE, ",
        dst);
    OUT(c, locp, ", ", &width_m, ", ", &zero, ", ", &res, ", ", dst,
        ");\n");
}

void gen_deposit_op(Context *c,
                    YYLTYPE *locp,
                    HexValue *dst,
                    HexValue *value,
                    HexValue *index,
                    HexCast *cast)
{
    HexValue value_m = *value;
    unsigned bit_width = (dst->bit_width == 64) ? 64 : 32;
    unsigned width = cast->bit_width;

    yyassert(c, locp, index->type == IMMEDIATE,
             "Deposit index must be immediate!\n");

    /*
     * Using tcg_gen_deposit_i**(dst, dst, ...) requires dst to be
     * initialized.
     */
    gen_inst_init_args(c, locp);

    /* If the destination value is 32, truncate the value, otherwise extend */
    if (dst->bit_width != value->bit_width) {
        if (bit_width == 32) {
            value_m = gen_rvalue_truncate(c, locp, &value_m);
        } else {
            value_m = gen_rvalue_extend(c, locp, &value_m);
        }
    }
    value_m = rvalue_materialize(c, locp, &value_m);
    OUT(c, locp, "tcg_gen_deposit_i", &bit_width, "(", dst, ", ", dst, ", ");
    OUT(c, locp, &value_m, ", ", index, " * ", &width, ", ", &width, ");\n");
}

HexValue gen_rextract_op(Context *c,
                         YYLTYPE *locp,
                         HexValue *src,
                         unsigned begin,
                         unsigned width)
{
    unsigned bit_width = (src->bit_width == 64) ? 64 : 32;
    HexValue res = gen_tmp(c, locp, bit_width, UNSIGNED);
    OUT(c, locp, "tcg_gen_extract_i", &bit_width, "(", &res);
    OUT(c, locp, ", ", src, ", ", &begin, ", ", &width, ");\n");
    return res;
}

HexValue gen_extract_op(Context *c,
                        YYLTYPE *locp,
                        HexValue *src,
                        HexValue *index,
                        HexExtract *extract)
{
    unsigned bit_width = (src->bit_width == 64) ? 64 : 32;
    unsigned width = extract->bit_width;
    const char *sign_prefix;
    HexValue res;

    yyassert(c, locp, index->type == IMMEDIATE,
             "Extract index must be immediate!\n");
    assert_signedness(c, locp, extract->signedness);

    sign_prefix = (extract->signedness == UNSIGNED) ? "" : "s";
    res = gen_tmp(c, locp, bit_width, extract->signedness);

    OUT(c, locp, "tcg_gen_", sign_prefix, "extract_i", &bit_width,
        "(", &res, ", ", src);
    OUT(c, locp, ", ", index, " * ", &width, ", ", &width, ");\n");

    /* Some extract operations have bit_width != storage_bit_width */
    if (extract->storage_bit_width > bit_width) {
        HexValue tmp = gen_tmp(c, locp, extract->storage_bit_width,
                               extract->signedness);
        const char *sign_suffix = (extract->signedness == UNSIGNED) ? "u" : "";
        OUT(c, locp, "tcg_gen_ext", sign_suffix, "_i32_i64(",
            &tmp, ", ", &res, ");\n");
        res = tmp;
    }
    return res;
}

void gen_write_reg(Context *c, YYLTYPE *locp, HexValue *reg, HexValue *value)
{
    HexValue value_m = *value;
    yyassert(c, locp, reg->type == REGISTER, "reg must be a register!");
    value_m = gen_rvalue_truncate(c, locp, &value_m);
    value_m = rvalue_materialize(c, locp, &value_m);
    OUT(c,
        locp,
        "gen_log_reg_write(", &reg->reg.id, ", ",
        &value_m, ");\n");
    OUT(c,
        locp,
        "ctx_log_reg_write(ctx, ", &reg->reg.id,
        ");\n");
}

void gen_assign(Context *c,
                YYLTYPE *locp,
                HexValue *dst,
                HexValue *value)
{
    HexValue value_m = *value;
    unsigned bit_width;

    yyassert(c, locp, !is_inside_ternary(c),
             "Assign in ternary not allowed!");

    if (dst->type == REGISTER) {
        gen_write_reg(c, locp, dst, &value_m);
        return;
    }

    if (dst->type == VARID) {
        find_variable(c, locp, dst, dst);
    }
    bit_width = dst->bit_width == 64 ? 64 : 32;

    if (bit_width != value_m.bit_width) {
        if (bit_width == 64) {
            value_m = gen_rvalue_extend(c, locp, &value_m);
        } else {
            value_m = gen_rvalue_truncate(c, locp, &value_m);
        }
    }

    const char *imm_suffix = (value_m.type == IMMEDIATE) ? "i" : "";
    OUT(c, locp, "tcg_gen_mov", imm_suffix, "_i", &bit_width,
        "(", dst, ", ", &value_m, ");\n");
}

HexValue gen_convround(Context *c,
                       YYLTYPE *locp,
                       HexValue *src)
{
    HexValue src_m = *src;
    unsigned bit_width = src_m.bit_width;
    const char *size = (bit_width == 32) ? "32" : "64";
    HexValue res = gen_tmp(c, locp, bit_width, src->signedness);
    HexValue mask = gen_constant(c, locp, "0x3", bit_width, UNSIGNED);
    HexValue one = gen_constant(c, locp, "1", bit_width, UNSIGNED);
    HexValue and;
    HexValue src_p1;

    and = gen_bin_op(c, locp, ANDB_OP, &src_m, &mask);
    src_p1 = gen_bin_op(c, locp, ADD_OP, &src_m, &one);

    OUT(c, locp, "tcg_gen_movcond_i", size, "(TCG_COND_EQ, ", &res);
    OUT(c, locp, ", ", &and, ", ", &mask, ", ");
    OUT(c, locp, &src_p1, ", ", &src_m, ");\n");

    return res;
}

static HexValue gen_convround_n_b(Context *c,
                                  YYLTYPE *locp,
                                  HexValue *a,
                                  HexValue *n)
{
    HexValue one = gen_constant(c, locp, "1", 32, UNSIGNED);
    HexValue res = gen_tmp(c, locp, 64, UNSIGNED);
    HexValue tmp = gen_tmp(c, locp, 32, UNSIGNED);
    HexValue tmp_64 = gen_tmp(c, locp, 64, UNSIGNED);

    assert(n->type != IMMEDIATE);
    OUT(c, locp, "tcg_gen_ext_i32_i64(", &res, ", ", a, ");\n");
    OUT(c, locp, "tcg_gen_shl_i32(", &tmp);
    OUT(c, locp, ", ", &one, ", ", n, ");\n");
    OUT(c, locp, "tcg_gen_and_i32(", &tmp);
    OUT(c, locp, ", ", &tmp, ", ", a, ");\n");
    OUT(c, locp, "tcg_gen_shri_i32(", &tmp);
    OUT(c, locp, ", ", &tmp, ", 1);\n");
    OUT(c, locp, "tcg_gen_ext_i32_i64(", &tmp_64, ", ", &tmp, ");\n");
    OUT(c, locp, "tcg_gen_add_i64(", &res);
    OUT(c, locp, ", ", &res, ", ", &tmp_64, ");\n");

    return res;
}

static HexValue gen_convround_n_c(Context *c,
                                  YYLTYPE *locp,
                                  HexValue *a,
                                  HexValue *n)
{
    HexValue res = gen_tmp(c, locp, 64, UNSIGNED);
    HexValue one = gen_constant(c, locp, "1", 32, UNSIGNED);
    HexValue tmp = gen_tmp(c, locp, 32, UNSIGNED);
    HexValue tmp_64 = gen_tmp(c, locp, 64, UNSIGNED);

    OUT(c, locp, "tcg_gen_ext_i32_i64(", &res, ", ", a, ");\n");
    OUT(c, locp, "tcg_gen_subi_i32(", &tmp);
    OUT(c, locp, ", ", n, ", 1);\n");
    OUT(c, locp, "tcg_gen_shl_i32(", &tmp);
    OUT(c, locp, ", ", &one, ", ", &tmp, ");\n");
    OUT(c, locp, "tcg_gen_ext_i32_i64(", &tmp_64, ", ", &tmp, ");\n");
    OUT(c, locp, "tcg_gen_add_i64(", &res);
    OUT(c, locp, ", ", &res, ", ", &tmp_64, ");\n");

    return res;
}

HexValue gen_convround_n(Context *c,
                         YYLTYPE *locp,
                         HexValue *src,
                         HexValue *pos)
{
    HexValue zero = gen_constant(c, locp, "0", 64, UNSIGNED);
    HexValue l_32 = gen_constant(c, locp, "1", 32, UNSIGNED);
    HexValue cond = gen_tmp(c, locp, 32, UNSIGNED);
    HexValue cond_64 = gen_tmp(c, locp, 64, UNSIGNED);
    HexValue mask = gen_tmp(c, locp, 32, UNSIGNED);
    HexValue n_64 = gen_tmp(c, locp, 64, UNSIGNED);
    HexValue res = gen_tmp(c, locp, 64, UNSIGNED);
    /* If input is 64 bit cast it to 32 */
    HexValue src_casted = gen_cast_op(c, locp, src, 32, src->signedness);
    HexValue pos_casted = gen_cast_op(c, locp, pos, 32, pos->signedness);
    HexValue r1;
    HexValue r2;
    HexValue r3;

    src_casted = rvalue_materialize(c, locp, &src_casted);
    pos_casted = rvalue_materialize(c, locp, &pos_casted);

    /*
     * r1, r2, and r3 represent the results of three different branches.
     *   - r1 picked if pos_casted == 0
     *   - r2 picked if (src_casted & ((1 << (pos_casted - 1)) - 1)) == 0),
     *     that is if bits 0, ..., pos_casted-1 are all 0.
     *   - r3 picked otherwise.
     */
    r1 = gen_rvalue_extend(c, locp, &src_casted);
    r2 = gen_convround_n_b(c, locp, &src_casted, &pos_casted);
    r3 = gen_convround_n_c(c, locp, &src_casted, &pos_casted);

    /*
     * Calculate the condition
     *   (src_casted & ((1 << (pos_casted - 1)) - 1)) == 0),
     * which checks if the bits 0,...,pos-1 are all 0.
     */
    OUT(c, locp, "tcg_gen_sub_i32(", &mask);
    OUT(c, locp, ", ", &pos_casted, ", ", &l_32, ");\n");
    OUT(c, locp, "tcg_gen_shl_i32(", &mask);
    OUT(c, locp, ", ", &l_32, ", ", &mask, ");\n");
    OUT(c, locp, "tcg_gen_sub_i32(", &mask);
    OUT(c, locp, ", ", &mask, ", ", &l_32, ");\n");
    OUT(c, locp, "tcg_gen_and_i32(", &cond);
    OUT(c, locp, ", ", &src_casted, ", ", &mask, ");\n");
    OUT(c, locp, "tcg_gen_extu_i32_i64(", &cond_64, ", ", &cond, ");\n");

    OUT(c, locp, "tcg_gen_ext_i32_i64(", &n_64, ", ", &pos_casted, ");\n");

    /*
     * if the bits 0, ..., pos_casted-1 are all 0, then pick r2 otherwise,
     * pick r3.
     */
    OUT(c, locp, "tcg_gen_movcond_i64");
    OUT(c, locp, "(TCG_COND_EQ, ", &res, ", ", &cond_64, ", ", &zero);
    OUT(c, locp, ", ", &r2, ", ", &r3, ");\n");

    /* Lastly, if the pos_casted == 0, then pick r1 */
    OUT(c, locp, "tcg_gen_movcond_i64");
    OUT(c, locp, "(TCG_COND_EQ, ", &res, ", ", &n_64, ", ", &zero);
    OUT(c, locp, ", ", &r1, ", ", &res, ");\n");

    /* Finally shift back val >>= n */
    OUT(c, locp, "tcg_gen_shr_i64(", &res);
    OUT(c, locp, ", ", &res, ", ", &n_64, ");\n");

    res = gen_rvalue_truncate(c, locp, &res);
    return res;
}

HexValue gen_round(Context *c,
                   YYLTYPE *locp,
                   HexValue *src,
                   HexValue *pos)
{
    HexValue zero = gen_constant(c, locp, "0", 64, UNSIGNED);
    HexValue one = gen_constant(c, locp, "1", 64, UNSIGNED);
    HexValue res;
    HexValue n_m1;
    HexValue shifted;
    HexValue sum;
    HexValue src_width;
    HexValue a;
    HexValue b;

    assert_signedness(c, locp, src->signedness);
    yyassert(c, locp, src->bit_width <= 32,
             "fRNDN not implemented for bit widths > 32!");

    res = gen_tmp(c, locp, 64, src->signedness);

    src_width = gen_imm_value(c, locp, src->bit_width, 32, UNSIGNED);
    a = gen_extend_op(c, locp, &src_width, 64, src, SIGNED);
    a = rvalue_materialize(c, locp, &a);

    src_width = gen_imm_value(c, locp, 5, 32, UNSIGNED);
    b = gen_extend_op(c, locp, &src_width, 64, pos, UNSIGNED);
    b = rvalue_materialize(c, locp, &b);

    n_m1 = gen_bin_op(c, locp, SUB_OP, &b, &one);
    shifted = gen_bin_op(c, locp, ASL_OP, &one, &n_m1);
    sum = gen_bin_op(c, locp, ADD_OP, &shifted, &a);

    OUT(c, locp, "tcg_gen_movcond_i64");
    OUT(c, locp, "(TCG_COND_EQ, ", &res, ", ", &b, ", ", &zero);
    OUT(c, locp, ", ", &a, ", ", &sum, ");\n");

    return res;
}

/* Circular addressing mode with auto-increment */
void gen_circ_op(Context *c,
                 YYLTYPE *locp,
                 HexValue *addr,
                 HexValue *increment,
                 HexValue *modifier)
{
    HexValue cs = gen_tmp(c, locp, 32, UNSIGNED);
    HexValue increment_m = *increment;
    increment_m = rvalue_materialize(c, locp, &increment_m);
    OUT(c, locp, "gen_read_reg(", &cs, ", HEX_REG_CS0 + MuN);\n");
    OUT(c,
        locp,
        "gen_helper_fcircadd(",
        addr,
        ", ",
        addr,
        ", ",
        &increment_m,
        ", ",
        modifier);
    OUT(c, locp, ", ", &cs, ");\n");
}

HexValue gen_locnt_op(Context *c, YYLTYPE *locp, HexValue *src)
{
    const char *bit_suffix = src->bit_width == 64 ? "64" : "32";
    HexValue src_m = *src;
    HexValue res;

    assert_signedness(c, locp, src->signedness);
    res = gen_tmp(c, locp, src->bit_width == 64 ? 64 : 32, src->signedness);
    src_m = rvalue_materialize(c, locp, &src_m);
    OUT(c, locp, "tcg_gen_not_i", bit_suffix, "(",
        &res, ", ", &src_m, ");\n");
    OUT(c, locp, "tcg_gen_clzi_i", bit_suffix, "(", &res, ", ", &res, ", ");
    OUT(c, locp, bit_suffix, ");\n");
    return res;
}

HexValue gen_ctpop_op(Context *c, YYLTYPE *locp, HexValue *src)
{
    const char *bit_suffix = src->bit_width == 64 ? "64" : "32";
    HexValue src_m = *src;
    HexValue res;
    assert_signedness(c, locp, src->signedness);
    res = gen_tmp(c, locp, src->bit_width == 64 ? 64 : 32, src->signedness);
    src_m = rvalue_materialize(c, locp, &src_m);
    OUT(c, locp, "tcg_gen_ctpop_i", bit_suffix,
        "(", &res, ", ", &src_m, ");\n");
    return res;
}

HexValue gen_rotl(Context *c, YYLTYPE *locp, HexValue *src, HexValue *width)
{
    const char *suffix = src->bit_width == 64 ? "i64" : "i32";
    HexValue amount = *width;
    HexValue res;
    assert_signedness(c, locp, src->signedness);
    res = gen_tmp(c, locp, src->bit_width, src->signedness);
    if (amount.bit_width < src->bit_width) {
        amount = gen_rvalue_extend(c, locp, &amount);
    } else {
        amount = gen_rvalue_truncate(c, locp, &amount);
    }
    amount = rvalue_materialize(c, locp, &amount);
    OUT(c, locp, "tcg_gen_rotl_", suffix, "(",
        &res, ", ", src, ", ", &amount, ");\n");

    return res;
}

HexValue gen_carry_from_add(Context *c,
                            YYLTYPE *locp,
                            HexValue *op1,
                            HexValue *op2,
                            HexValue *op3)
{
    HexValue zero = gen_constant(c, locp, "0", 64, UNSIGNED);
    HexValue res = gen_tmp(c, locp, 64, UNSIGNED);
    HexValue cf = gen_tmp(c, locp, 64, UNSIGNED);
    HexValue op1_m = rvalue_materialize(c, locp, op1);
    HexValue op2_m = rvalue_materialize(c, locp, op2);
    HexValue op3_m = rvalue_materialize(c, locp, op3);
    op3_m = gen_rvalue_extend(c, locp, &op3_m);

    OUT(c, locp, "tcg_gen_add2_i64(", &res, ", ", &cf, ", ", &op1_m, ", ",
        &zero);
    OUT(c, locp, ", ", &op3_m, ", ", &zero, ");\n");
    OUT(c, locp, "tcg_gen_add2_i64(", &res, ", ", &cf, ", ", &res, ", ", &cf);
    OUT(c, locp, ", ", &op2_m, ", ", &zero, ");\n");

    return cf;
}

void gen_addsat64(Context *c,
                  YYLTYPE *locp,
                  HexValue *dst,
                  HexValue *op1,
                  HexValue *op2)
{
    HexValue op1_m = rvalue_materialize(c, locp, op1);
    HexValue op2_m = rvalue_materialize(c, locp, op2);
    OUT(c, locp, "gen_add_sat_i64(", dst, ", ", &op1_m, ", ", &op2_m, ");\n");
}

void gen_inst(Context *c, GString *iname)
{
    c->total_insn++;
    c->inst.name = iname;
    c->inst.allocated = g_array_new(FALSE, FALSE, sizeof(Var));
    c->inst.init_list = g_array_new(FALSE, FALSE, sizeof(HexValue));
    c->inst.strings = g_array_new(FALSE, FALSE, sizeof(GString *));
    EMIT_SIG(c, "void emit_%s(DisasContext *ctx, Insn *insn, Packet *pkt",
             c->inst.name->str);
}


/*
 * Initialize declared but uninitialized registers, but only for
 * non-conditional instructions
 */
void gen_inst_init_args(Context *c, YYLTYPE *locp)
{
    if (!c->inst.init_list) {
        return;
    }

    for (unsigned i = 0; i < c->inst.init_list->len; i++) {
        HexValue *val = &g_array_index(c->inst.init_list, HexValue, i);
        if (val->type == REGISTER_ARG) {
            char reg_id[5];
            reg_compose(c, locp, &val->reg, reg_id);
            EMIT_HEAD(c, "tcg_gen_movi_i%u(%s, 0);\n", val->bit_width, reg_id);
        } else if (val->type == PREDICATE) {
            char suffix = val->is_dotnew ? 'N' : 'V';
            EMIT_HEAD(c, "tcg_gen_movi_i%u(P%c%c, 0);\n", val->bit_width,
                      val->pred.id, suffix);
        } else {
            yyassert(c, locp, false, "Invalid arg type!");
        }
    }

    /* Free argument init list once we have initialized everything */
    g_array_free(c->inst.init_list, TRUE);
    c->inst.init_list = NULL;
}

void gen_inst_code(Context *c, YYLTYPE *locp)
{
    if (c->inst.error_count != 0) {
        fprintf(stderr,
                "Parsing of instruction %s generated %d errors!\n",
                c->inst.name->str,
                c->inst.error_count);
    } else {
        c->implemented_insn++;
        fprintf(c->enabled_file, "%s\n", c->inst.name->str);
        emit_footer(c);
        commit(c);
    }
    free_instruction(c);
}

void gen_pred_assign(Context *c, YYLTYPE *locp, HexValue *left_pred,
                     HexValue *right_pred)
{
    char pred_id[2] = {left_pred->pred.id, 0};
    bool is_direct = is_direct_predicate(left_pred);
    HexValue r = rvalue_materialize(c, locp, right_pred);
    r = gen_rvalue_truncate(c, locp, &r);
    yyassert(c, locp, !is_inside_ternary(c),
             "Predicate assign not allowed in ternary!");
    /* Extract predicate TCGv */
    if (is_direct) {
        *left_pred = gen_tmp_value(c, locp, "0", 32, UNSIGNED);
    }
    /* Extract first 8 bits, and store new predicate value */
    OUT(c, locp, "tcg_gen_mov_i32(", left_pred, ", ", &r, ");\n");
    OUT(c, locp, "tcg_gen_andi_i32(", left_pred, ", ", left_pred,
        ", 0xff);\n");
    if (is_direct) {
        OUT(c, locp, "gen_log_pred_write(ctx, ", pred_id, ", ", left_pred,
            ");\n");
        OUT(c, locp, "ctx_log_pred_write(ctx, ", pred_id, ");\n");
    }
}

void gen_cancel(Context *c, YYLTYPE *locp)
{
    OUT(c, locp, "gen_cancel(insn->slot);\n");
}

void gen_load_cancel(Context *c, YYLTYPE *locp)
{
    gen_cancel(c, locp);
    OUT(c, locp, "if (insn->slot == 0 && pkt->pkt_has_store_s1) {\n");
    OUT(c, locp, "ctx->s1_store_processed = false;\n");
    OUT(c, locp, "process_store(ctx, 1);\n");
    OUT(c, locp, "}\n");
}

void gen_load(Context *c, YYLTYPE *locp, HexValue *width,
              HexSignedness signedness, HexValue *ea, HexValue *dst)
{
    char size_suffix[4] = {0};
    const char *sign_suffix;
    /* Memop width is specified in the load macro */
    assert_signedness(c, locp, signedness);
    sign_suffix = (width->imm.value > 4)
                   ? ""
                   : ((signedness == UNSIGNED) ? "u" : "s");
    /* If dst is a variable, assert that is declared and load the type info */
    if (dst->type == VARID) {
        find_variable(c, locp, dst, dst);
    }

    snprintf(size_suffix, 4, "%" PRIu64, width->imm.value * 8);
    /* Lookup the effective address EA */
    find_variable(c, locp, ea, ea);
    OUT(c, locp, "if (insn->slot == 0 && pkt->pkt_has_store_s1) {\n");
    OUT(c, locp, "probe_noshuf_load(", ea, ", ", width, ", ctx->mem_idx);\n");
    OUT(c, locp, "process_store(ctx, 1);\n");
    OUT(c, locp, "}\n");
    OUT(c, locp, "tcg_gen_qemu_ld", size_suffix, sign_suffix);
    OUT(c, locp, "(");
    if (dst->bit_width > width->imm.value * 8) {
        /*
         * Cast to the correct TCG type if necessary, to avoid implict cast
         * warnings. This is needed when the width of the destination var is
         * larger than the size of the requested load.
         */
        OUT(c, locp, "(TCGv) ");
    }
    OUT(c, locp, dst, ", ", ea, ", ctx->mem_idx);\n");
}

void gen_store(Context *c, YYLTYPE *locp, HexValue *width, HexValue *ea,
               HexValue *src)
{
    HexValue src_m = *src;
    /* Memop width is specified in the store macro */
    unsigned mem_width = width->imm.value;
    /* Lookup the effective address EA */
    find_variable(c, locp, ea, ea);
    src_m = rvalue_materialize(c, locp, &src_m);
    OUT(c, locp, "gen_store", &mem_width, "(cpu_env, ", ea, ", ", &src_m);
    OUT(c, locp, ", insn->slot);\n");
}

void gen_sethalf(Context *c, YYLTYPE *locp, HexCast *sh, HexValue *n,
                 HexValue *dst, HexValue *value)
{
    yyassert(c, locp, n->type == IMMEDIATE,
             "Deposit index must be immediate!\n");
    if (dst->type == VARID) {
        find_variable(c, locp, dst, dst);
    }

    gen_deposit_op(c, locp, dst, value, n, sh);
}

void gen_setbits(Context *c, YYLTYPE *locp, HexValue *hi, HexValue *lo,
                 HexValue *dst, HexValue *value)
{
    unsigned len;
    HexValue tmp;

    yyassert(c, locp, hi->type == IMMEDIATE &&
             hi->imm.type == VALUE &&
             lo->type == IMMEDIATE &&
             lo->imm.type == VALUE,
             "Range deposit needs immediate values!\n");

    *value = gen_rvalue_truncate(c, locp, value);
    len = hi->imm.value + 1 - lo->imm.value;
    tmp = gen_tmp(c, locp, 32, value->signedness);
    /* Emit an `and` to ensure `value` is either 0 or 1. */
    OUT(c, locp, "tcg_gen_andi_i32(", &tmp, ", ", value, ", 1);\n");
    /* Use `neg` to map 0 -> 0 and 1 -> 0xffff... */
    OUT(c, locp, "tcg_gen_neg_i32(", &tmp, ", ", &tmp, ");\n");
    OUT(c, locp, "tcg_gen_deposit_i32(", dst, ", ", dst,
        ", ", &tmp, ", ");
    OUT(c, locp, lo, ", ", &len, ");\n");
}

unsigned gen_if_cond(Context *c, YYLTYPE *locp, HexValue *cond)
{
    const char *bit_suffix;
    /* Generate an end label, if false branch to that label */
    OUT(c, locp, "TCGLabel *if_label_", &c->inst.if_count,
        " = gen_new_label();\n");
    *cond = rvalue_materialize(c, locp, cond);
    bit_suffix = (cond->bit_width == 64) ? "i64" : "i32";
    OUT(c, locp, "tcg_gen_brcondi_", bit_suffix, "(TCG_COND_EQ, ", cond,
        ", 0, if_label_", &c->inst.if_count, ");\n");
    return c->inst.if_count++;
}

unsigned gen_if_else(Context *c, YYLTYPE *locp, unsigned index)
{
    unsigned if_index = c->inst.if_count++;
    /* Generate label to jump if else is not verified */
    OUT(c, locp, "TCGLabel *if_label_", &if_index,
        " = gen_new_label();\n");
    /* Jump out of the else statement */
    OUT(c, locp, "tcg_gen_br(if_label_", &if_index, ");\n");
    /* Fix the else label */
    OUT(c, locp, "gen_set_label(if_label_", &index, ");\n");
    return if_index;
}

HexValue gen_rvalue_pred(Context *c, YYLTYPE *locp, HexValue *pred)
{
    /* Predicted instructions need to zero out result args */
    gen_inst_init_args(c, locp);

    if (is_direct_predicate(pred)) {
        bool is_dotnew = pred->is_dotnew;
        char predicate_id[2] = { pred->pred.id, '\0' };
        char *pred_str = (char *) &predicate_id;
        *pred = gen_tmp_value(c, locp, "0", 32, UNSIGNED);
        if (is_dotnew) {
            OUT(c, locp, "tcg_gen_mov_i32(", pred,
                ", hex_new_pred_value[");
            OUT(c, locp, pred_str, "]);\n");
        } else {
            OUT(c, locp, "gen_read_preg(", pred, ", ", pred_str, ");\n");
        }
    }

    return *pred;
}

HexValue gen_rvalue_var(Context *c, YYLTYPE *locp, HexValue *var)
{
    find_variable(c, locp, var, var);
    return *var;
}

HexValue gen_rvalue_mpy(Context *c, YYLTYPE *locp, HexMpy *mpy,
                        HexValue *op1, HexValue *op2)
{
    HexValue res;
    memset(&res, 0, sizeof(HexValue));

    assert_signedness(c, locp, mpy->first_signedness);
    assert_signedness(c, locp, mpy->second_signedness);

    *op1 = gen_cast_op(c, locp, op1, mpy->first_bit_width * 2,
                     mpy->first_signedness);
    /* Handle fMPTY3216.. */
    if (mpy->first_bit_width == 32) {
        *op2 = gen_cast_op(c, locp, op2, 64, mpy->second_signedness);
    } else {
        *op2 = gen_cast_op(c, locp, op2, mpy->second_bit_width * 2,
                         mpy->second_signedness);
    }
    res = gen_bin_op(c, locp, MUL_OP, op1, op2);
    /* Handle special cases required by the language */
    if (mpy->first_bit_width == 16 && mpy->second_bit_width == 16) {
        HexValue src_width = gen_imm_value(c, locp, 32, 32, UNSIGNED);
        HexSignedness signedness = bin_op_signedness(c, locp,
                                                     mpy->first_signedness,
                                                     mpy->second_signedness);
        res = gen_extend_op(c, locp, &src_width, 64, &res,
                            signedness);
    }
    return res;
}

static inline HexValue gen_rvalue_simple_unary(Context *c, YYLTYPE *locp,
                                               HexValue *value,
                                               const char *c_code,
                                               const char *tcg_code)
{
    unsigned bit_width = (value->bit_width == 64) ? 64 : 32;
    HexValue res;
    if (value->type == IMMEDIATE) {
        res = gen_imm_qemu_tmp(c, locp, bit_width, value->signedness);
        gen_c_int_type(c, locp, value->bit_width, value->signedness);
        OUT(c, locp, " ", &res, " = ", c_code, "(", value, ");\n");
    } else {
        res = gen_tmp(c, locp, bit_width, value->signedness);
        OUT(c, locp, tcg_code, "_i", &bit_width, "(", &res, ", ", value,
            ");\n");
    }
    return res;
}


HexValue gen_rvalue_not(Context *c, YYLTYPE *locp, HexValue *value)
{
    return gen_rvalue_simple_unary(c, locp, value, "~", "tcg_gen_not");
}

HexValue gen_rvalue_notl(Context *c, YYLTYPE *locp, HexValue *value)
{
    unsigned bit_width = (value->bit_width == 64) ? 64 : 32;
    HexValue res;
    if (value->type == IMMEDIATE) {
        res = gen_imm_qemu_tmp(c, locp, bit_width, value->signedness);
        gen_c_int_type(c, locp, value->bit_width, value->signedness);
        OUT(c, locp, " ", &res, " = !(", value, ");\n");
    } else {
        HexValue zero = gen_constant(c, locp, "0", bit_width, UNSIGNED);
        HexValue one = gen_constant(c, locp, "0xff", bit_width, UNSIGNED);
        res = gen_tmp(c, locp, bit_width, value->signedness);
        OUT(c, locp, "tcg_gen_movcond_i", &bit_width);
        OUT(c, locp, "(TCG_COND_EQ, ", &res, ", ", value, ", ", &zero);
        OUT(c, locp, ", ", &one, ", ", &zero, ");\n");
    }
    return res;
}

HexValue gen_rvalue_sat(Context *c, YYLTYPE *locp, HexSat *sat,
                        HexValue *width, HexValue *value)
{
    const char *unsigned_str;
    const char *bit_suffix = (value->bit_width == 64) ? "i64" : "i32";
    HexValue res;
    HexValue ovfl;
    /*
     * Note: all saturates are assumed to implicitly set overflow.
     * This assumption holds for the instructions currently parsed
     * by idef-parser.
     */
    yyassert(c, locp, width->imm.value < value->bit_width,
             "To compute overflow, source width must be greater than"
             " saturation width!");
    yyassert(c, locp, !is_inside_ternary(c),
             "Saturating from within a ternary is not allowed!");
    assert_signedness(c, locp, sat->signedness);

    unsigned_str = (sat->signedness == UNSIGNED) ? "u" : "";
    res = gen_tmp(c, locp, value->bit_width, sat->signedness);
    ovfl = gen_tmp(c, locp, 32, sat->signedness);
    OUT(c, locp, "gen_sat", unsigned_str, "_", bit_suffix, "_ovfl(");
    OUT(c, locp, &ovfl, ", ", &res, ", ", value, ", ", &width->imm.value,
        ");\n");
    OUT(c, locp, "gen_set_usr_field_if(USR_OVF,", &ovfl, ");\n");

    return res;
}

HexValue gen_rvalue_fscr(Context *c, YYLTYPE *locp, HexValue *value)
{
    HexValue key = gen_tmp(c, locp, 64, UNSIGNED);
    HexValue res = gen_tmp(c, locp, 64, UNSIGNED);
    HexValue frame_key = gen_tmp(c, locp, 32, UNSIGNED);
    *value = gen_rvalue_extend(c, locp, value);
    OUT(c, locp, "gen_read_reg(", &frame_key, ", HEX_REG_FRAMEKEY);\n");
    OUT(c, locp, "tcg_gen_concat_i32_i64(",
        &key, ", ", &frame_key, ", ", &frame_key, ");\n");
    OUT(c, locp, "tcg_gen_xor_i64(", &res, ", ", value, ", ", &key, ");\n");
    return res;
}

HexValue gen_rvalue_abs(Context *c, YYLTYPE *locp, HexValue *value)
{
    return gen_rvalue_simple_unary(c, locp, value, "abs", "tcg_gen_abs");
}

HexValue gen_rvalue_neg(Context *c, YYLTYPE *locp, HexValue *value)
{
    return gen_rvalue_simple_unary(c, locp, value, "-", "tcg_gen_neg");
}

HexValue gen_rvalue_brev(Context *c, YYLTYPE *locp, HexValue *value)
{
    HexValue res;
    yyassert(c, locp, value->bit_width <= 32,
             "fbrev not implemented for 64-bit integers!");
    res = gen_tmp(c, locp, value->bit_width, value->signedness);
    *value = rvalue_materialize(c, locp, value);
    OUT(c, locp, "gen_helper_fbrev(", &res, ", ", value, ");\n");
    return res;
}

HexValue gen_rvalue_ternary(Context *c, YYLTYPE *locp, HexValue *cond,
                            HexValue *true_branch, HexValue *false_branch)
{
    bool is_64bit = (true_branch->bit_width == 64) ||
                    (false_branch->bit_width == 64);
    unsigned bit_width = (is_64bit) ? 64 : 32;
    HexValue zero = gen_constant(c, locp, "0", bit_width, UNSIGNED);
    HexValue res = gen_tmp(c, locp, bit_width, UNSIGNED);

    if (is_64bit) {
        *cond = gen_rvalue_extend(c, locp, cond);
        *true_branch = gen_rvalue_extend(c, locp, true_branch);
        *false_branch = gen_rvalue_extend(c, locp, false_branch);
    } else {
        *cond = gen_rvalue_truncate(c, locp, cond);
    }
    *cond = rvalue_materialize(c, locp, cond);
    *true_branch = rvalue_materialize(c, locp, true_branch);
    *false_branch = rvalue_materialize(c, locp, false_branch);

    OUT(c, locp, "tcg_gen_movcond_i", &bit_width);
    OUT(c, locp, "(TCG_COND_NE, ", &res, ", ", cond, ", ", &zero);
    OUT(c, locp, ", ", true_branch, ", ", false_branch, ");\n");

    assert(c->ternary->len > 0);
    g_array_remove_index(c->ternary, c->ternary->len - 1);

    return res;
}

const char *cond_to_str(TCGCond cond)
{
    switch (cond) {
    case TCG_COND_NEVER:
        return "TCG_COND_NEVER";
    case TCG_COND_ALWAYS:
        return "TCG_COND_ALWAYS";
    case TCG_COND_EQ:
        return "TCG_COND_EQ";
    case TCG_COND_NE:
        return "TCG_COND_NE";
    case TCG_COND_LT:
        return "TCG_COND_LT";
    case TCG_COND_GE:
        return "TCG_COND_GE";
    case TCG_COND_LE:
        return "TCG_COND_LE";
    case TCG_COND_GT:
        return "TCG_COND_GT";
    case TCG_COND_LTU:
        return "TCG_COND_LTU";
    case TCG_COND_GEU:
        return "TCG_COND_GEU";
    case TCG_COND_LEU:
        return "TCG_COND_LEU";
    case TCG_COND_GTU:
        return "TCG_COND_GTU";
    default:
        abort();
    }
}

void emit_arg(Context *c, YYLTYPE *locp, HexValue *arg)
{
    switch (arg->type) {
    case REGISTER_ARG:
        if (arg->reg.type == DOTNEW) {
            EMIT_SIG(c, ", TCGv N%cN", arg->reg.id);
        } else {
            bool is64 = (arg->bit_width == 64);
            const char *type = is64 ? "TCGv_i64" : "TCGv_i32";
            char reg_id[5];
            reg_compose(c, locp, &(arg->reg), reg_id);
            EMIT_SIG(c, ", %s %s", type, reg_id);
            /* MuV register requires also MuN to provide its index */
            if (arg->reg.type == MODIFIER) {
                EMIT_SIG(c, ", int MuN");
            }
        }
        break;
    case PREDICATE:
        {
            char suffix = arg->is_dotnew ? 'N' : 'V';
            EMIT_SIG(c, ", TCGv P%c%c", arg->pred.id, suffix);
        }
        break;
    default:
        {
            fprintf(stderr, "emit_arg got unsupported argument!");
            abort();
        }
    }
}

void emit_footer(Context *c)
{
    EMIT(c, "}\n");
    EMIT(c, "\n");
}

void track_string(Context *c, GString *s)
{
    g_array_append_val(c->inst.strings, s);
}

void free_instruction(Context *c)
{
    assert(!is_inside_ternary(c));
    /* Free the strings */
    g_string_truncate(c->signature_str, 0);
    g_string_truncate(c->out_str, 0);
    g_string_truncate(c->header_str, 0);
    /* Free strings allocated by the instruction */
    for (unsigned i = 0; i < c->inst.strings->len; i++) {
        g_string_free(g_array_index(c->inst.strings, GString*, i), TRUE);
    }
    g_array_free(c->inst.strings, TRUE);
    /* Free INAME token value */
    g_string_free(c->inst.name, TRUE);
    /* Free variables and registers */
    g_array_free(c->inst.allocated, TRUE);
    /* Initialize instruction-specific portion of the context */
    memset(&(c->inst), 0, sizeof(Inst));
}

void assert_signedness(Context *c,
                       YYLTYPE *locp,
                       HexSignedness signedness)
{
    yyassert(c, locp,
             signedness != UNKNOWN_SIGNEDNESS,
             "Unspecified signedness");
}
