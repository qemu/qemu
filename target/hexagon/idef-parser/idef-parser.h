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

#ifndef IDEF_PARSER_H
#define IDEF_PARSER_H

#include <inttypes.h>
#include <stdio.h>
#include <stdbool.h>
#include <glib.h>

#define TCGV_NAME_SIZE 7
#define MAX_WRITTEN_REGS 32
#define OFFSET_STR_LEN 32
#define ALLOC_LIST_LEN 32
#define ALLOC_NAME_SIZE 32
#define INIT_LIST_LEN 32
#define OUT_BUF_LEN (1024 * 1024)
#define SIGNATURE_BUF_LEN (128 * 1024)
#define HEADER_BUF_LEN (128 * 1024)

/* Variadic macros to wrap the buffer printing functions */
#define EMIT(c, ...)                                                           \
    do {                                                                       \
        g_string_append_printf((c)->out_str, __VA_ARGS__);                     \
    } while (0)

#define EMIT_SIG(c, ...)                                                       \
    do {                                                                       \
        g_string_append_printf((c)->signature_str, __VA_ARGS__);               \
    } while (0)

#define EMIT_HEAD(c, ...)                                                      \
    do {                                                                       \
        g_string_append_printf((c)->header_str, __VA_ARGS__);                  \
    } while (0)

/**
 * Type of register, assigned to the HexReg.type field
 */
typedef enum { GENERAL_PURPOSE, CONTROL, MODIFIER, DOTNEW } HexRegType;

typedef enum { UNKNOWN_SIGNEDNESS, SIGNED, UNSIGNED } HexSignedness;

/**
 * Semantic record of the REG tokens, identifying registers
 */
typedef struct HexReg {
    uint8_t id;                 /**< Identifier of the register               */
    HexRegType type;            /**< Type of the register                     */
    unsigned bit_width;         /**< Bit width of the reg, 32 or 64 bits      */
} HexReg;

/**
 * Data structure, identifying a TCGv temporary value
 */
typedef struct HexTmp {
    unsigned index;             /**< Index of the TCGv temporary value        */
} HexTmp;

/**
 * Enum of the possible immediate, an immediate is a value which is known
 * at tinycode generation time, e.g. an integer value, not a TCGv
 */
enum ImmUnionTag {
    I,
    VARIABLE,
    VALUE,
    QEMU_TMP,
    IMM_PC,
    IMM_CONSTEXT,
};

/**
 * Semantic record of the IMM token, identifying an immediate constant
 */
typedef struct HexImm {
    union {
        char id;                /**< Identifier, used when type is VARIABLE   */
        uint64_t value;         /**< Immediate value, used when type is VALUE */
        uint64_t index;         /**< Index, used when type is QEMU_TMP        */
    };
    enum ImmUnionTag type;      /**< Type of the immediate                    */
} HexImm;

/**
 * Semantic record of the PRED token, identifying a predicate
 */
typedef struct HexPred {
    char id;                    /**< Identifier of the predicate              */
} HexPred;

/**
 * Semantic record of the SAT token, identifying the saturate operator
 * Note: All saturates are assumed to implicitly set overflow.
 */
typedef struct HexSat {
    HexSignedness signedness;   /**< Signedness of the sat. op.               */
} HexSat;

/**
 * Semantic record of the CAST token, identifying the cast operator
 */
typedef struct HexCast {
    unsigned bit_width;         /**< Bit width of the cast operator           */
    HexSignedness signedness;   /**< Unsigned flag for the cast operator      */
} HexCast;

/**
 * Semantic record of the EXTRACT token, identifying the cast operator
 */
typedef struct HexExtract {
    unsigned bit_width;         /**< Bit width of the extract operator        */
    unsigned storage_bit_width; /**< Actual bit width of the extract operator */
    HexSignedness signedness;   /**< Unsigned flag for the extract operator   */
} HexExtract;

/**
 * Semantic record of the MPY token, identifying the fMPY multiplication
 * operator
 */
typedef struct HexMpy {
    unsigned first_bit_width;        /**< Bit width of 1st operand of fMPY    */
    unsigned second_bit_width;       /**< Bit width of 2nd operand of fMPY    */
    HexSignedness first_signedness;  /**< Signedness of 1st operand of fMPY   */
    HexSignedness second_signedness; /**< Signedness of 2nd operand of fMPY   */
} HexMpy;

/**
 * Semantic record of the VARID token, identifying declared variables
 * of the input language
 */
typedef struct HexVar {
    GString *name;              /**< Name of the VARID variable               */
} HexVar;

/**
 * Data structure uniquely identifying a declared VARID variable, used for
 * keeping track of declared variable, so that any variable is declared only
 * once, and its properties are propagated through all the subsequent instances
 * of that variable
 */
typedef struct Var {
    GString *name;              /**< Name of the VARID variable               */
    uint8_t bit_width;          /**< Bit width of the VARID variable          */
    HexSignedness signedness;   /**< Unsigned flag for the VARID var          */
} Var;

/**
 * Enum of the possible rvalue types, used in the HexValue.type field
 */
typedef enum RvalueUnionTag {
    REGISTER, REGISTER_ARG, TEMP, IMMEDIATE, PREDICATE, VARID
} RvalueUnionTag;

/**
 * Semantic record of the rvalue token, identifying any numeric value,
 * immediate or register based. The rvalue tokens are combined together
 * through the use of several operators, to encode expressions
 */
typedef struct HexValue {
    union {
        HexReg reg;             /**< rvalue of register type                  */
        HexTmp tmp;             /**< rvalue of temporary type                 */
        HexImm imm;             /**< rvalue of immediate type                 */
        HexPred pred;           /**< rvalue of predicate type                 */
        HexVar var;             /**< rvalue of declared variable type         */
    };
    RvalueUnionTag type;        /**< Type of the rvalue                       */
    unsigned bit_width;         /**< Bit width of the rvalue                  */
    HexSignedness signedness;   /**< Unsigned flag for the rvalue             */
    bool is_dotnew;             /**< rvalue of predicate type is dotnew?      */
} HexValue;

/**
 * State of ternary operator
 */
typedef enum TernaryState { IN_LEFT, IN_RIGHT } TernaryState;

/**
 * Data structure used to handle side effects inside ternary operators
 */
typedef struct Ternary {
    TernaryState state;
    HexValue cond;
} Ternary;

/**
 * Operator type, used for referencing the correct operator when calling the
 * gen_bin_op() function, which in turn will generate the correct code to
 * execute the operation between the two rvalues
 */
typedef enum OpType {
    ADD_OP, SUB_OP, MUL_OP, ASL_OP, ASR_OP, LSR_OP, ANDB_OP, ORB_OP,
    XORB_OP, ANDL_OP, MINI_OP, MAXI_OP
} OpType;

/**
 * Data structure including instruction specific information, to be cleared
 * out after the compilation of each instruction
 */
typedef struct Inst {
    GString *name;              /**< Name of the compiled instruction         */
    char *code_begin;           /**< Beginning of instruction input code      */
    char *code_end;             /**< End of instruction input code            */
    unsigned tmp_count;         /**< Index of the last declared TCGv temp     */
    unsigned qemu_tmp_count;    /**< Index of the last declared int temp      */
    unsigned if_count;          /**< Index of the last declared if label      */
    unsigned error_count;       /**< Number of generated errors               */
    GArray *allocated;          /**< Allocated declaredVARID vars             */
    GArray *init_list;          /**< List of initialized registers            */
    GArray *strings;            /**< Strings allocated by the instruction     */
} Inst;

/**
 * Data structure representing the whole translation context, which in a
 * reentrant flex/bison parser just like ours is passed between the scanner
 * and the parser, holding all the necessary information to perform the
 * parsing, this data structure survives between the compilation of different
 * instructions
 */
typedef struct Context {
    void *scanner;              /**< Reentrant parser state pointer           */
    char *input_buffer;         /**< Buffer containing the input code         */
    GString *out_str;           /**< String containing the output code        */
    GString *signature_str;     /**< String containing the signatures code    */
    GString *header_str;        /**< String containing the header code        */
    FILE *defines_file;         /**< FILE * of the generated header           */
    FILE *output_file;          /**< FILE * of the C output file              */
    FILE *enabled_file;         /**< FILE * of the list of enabled inst       */
    GArray *ternary;            /**< Array to track nesting of ternary ops    */
    unsigned total_insn;        /**< Number of instructions in input file     */
    unsigned implemented_insn;  /**< Instruction compiled without errors      */
    Inst inst;                  /**< Parsing data of the current inst         */
} Context;

#endif /* IDEF_PARSER_H */
