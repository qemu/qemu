%{
/*
 *  Copyright(c) 2019-2023 rev.ng Labs Srl. All Rights Reserved.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "idef-parser.h"
#include "parser-helpers.h"
#include "idef-parser.tab.h"
#include "idef-parser.yy.h"

/* Uncomment this to disable yyasserts */
/* #define NDEBUG */

#define ERR_LINE_CONTEXT 40

%}

%lex-param {void *scanner}
%parse-param {void *scanner}
%parse-param {Context *c}

%define parse.error verbose
%define parse.lac full
%define api.pure full

%locations

%union {
    GString *string;
    HexValue rvalue;
    HexSat sat;
    HexCast cast;
    HexExtract extract;
    HexMpy mpy;
    HexSignedness signedness;
    int index;
}

/* Tokens */
%start input

%expect 1

%token IN INAME VAR
%token ABS CROUND ROUND CIRCADD COUNTONES INC DEC ANDA ORA XORA PLUSPLUS ASL
%token ASR LSR EQ NEQ LTE GTE MIN MAX ANDL FOR ICIRC IF MUN FSCR FCHK SXT
%token ZXT CONSTEXT LOCNT BREV SIGN LOAD STORE PC LPCFG
%token LOAD_CANCEL STORE_CANCEL CANCEL IDENTITY ROTL INSBITS SETBITS EXTRANGE
%token CAST4_8U FAIL CARRY_FROM_ADD ADDSAT64 LSBNEW
%token TYPE_SIZE_T TYPE_INT TYPE_SIGNED TYPE_UNSIGNED TYPE_LONG

%token <rvalue> REG IMM PRED
%token <index> ELSE
%token <mpy> MPY
%token <sat> SAT
%token <cast> CAST DEPOSIT SETHALF
%token <extract> EXTRACT
%type <string> INAME
%type <rvalue> rvalue lvalue VAR assign_statement var var_decl var_type
%type <rvalue> FAIL
%type <rvalue> TYPE_SIGNED TYPE_UNSIGNED TYPE_INT TYPE_LONG TYPE_SIZE_T
%type <index> if_stmt IF
%type <signedness> SIGN

/* Operator Precedences */
%left MIN MAX
%left '('
%left ','
%left '='
%right CIRCADD
%right INC DEC ANDA ORA XORA
%left '?' ':'
%left ANDL
%left '|'
%left '^' ANDOR
%left '&'
%left EQ NEQ
%left '<' '>' LTE GTE
%left ASL ASR LSR
%right ABS
%left '-' '+'
%left '*' '/' '%' MPY
%right '~' '!'
%left '['
%right CAST
%right LOCNT BREV

/* Bison Grammar */
%%

/* Input file containing the description of each hexagon instruction */
input : instructions
      {
          /* Suppress warning about unused yynerrs */
          (void) yynerrs;
          YYACCEPT;
      }
      ;

instructions : instruction instructions
             | %empty
             ;

instruction : INAME
              {
                  gen_inst(c, $1);
              }
              arguments
              {
                  EMIT_SIG(c, ")");
                  EMIT_HEAD(c, "{\n");
              }
              code
              {
                  gen_inst_code(c, &@1);
              }
            | error /* Recover gracefully after instruction compilation error */
              {
                  free_instruction(c);
              }
            ;

arguments : '(' ')'
          | '(' argument_list ')';

argument_list : argument_decl ',' argument_list
              | argument_decl
              ;

var : VAR
      {
          track_string(c, $1.var.name);
          $$ = $1;
      }
    ;

/*
 * Here the integer types are defined from valid combinations of
 * `signed`, `unsigned`, `int`, and `long` tokens. The `signed`
 * and `unsigned` tokens are here assumed to always be placed
 * first in the type declaration, which is not the case in
 * normal C. Similarly, `int` is assumed to always be placed
 * last in the type.
 */
type_int : TYPE_INT
         | TYPE_SIGNED
         | TYPE_SIGNED TYPE_INT;
type_uint : TYPE_UNSIGNED
          | TYPE_UNSIGNED TYPE_INT;
type_ulonglong : TYPE_UNSIGNED TYPE_LONG TYPE_LONG
               | TYPE_UNSIGNED TYPE_LONG TYPE_LONG TYPE_INT;

/*
 * Here the various valid int types defined above specify
 * their `signedness` and `bit_width`. The LP64 convention
 * is assumed where longs are 64-bit, long longs are then
 * assumed to also be 64-bit.
 */
var_type : TYPE_SIZE_T
           {
              yyassert(c, &@1, $1.bit_width <= 64,
                       "Variables with size > 64-bit are not supported!");
              $$ = $1;
           }
         | type_int
           {
              $$.signedness = SIGNED;
              $$.bit_width  = 32;
           }
         | type_uint
           {
              $$.signedness = UNSIGNED;
              $$.bit_width  = 32;
           }
         | type_ulonglong
           {
              $$.signedness = UNSIGNED;
              $$.bit_width  = 64;
           }
         ;

/* Rule to capture declarations of VARs */
var_decl : var_type IMM
           {
              /*
               * Rule to capture "int i;" declarations since "i" is special
               * and assumed to be always be IMM. Moreover, "i" is only
               * assumed to be used in for-loops.
               *
               * Therefore we want to NOP these declarations.
               */
              yyassert(c, &@2, $2.imm.type == I,
                       "Variable declaration with immedaties only allowed"
                       " for the loop induction variable \"i\"");
              $$ = $2;
           }
         | var_type var
           {
              /*
               * Allocate new variable, this checks that it hasn't already
               * been declared.
               */
              gen_varid_allocate(c, &@1, &$2, $1.bit_width, $1.signedness);
              /* Copy var for variable name */
              $$ = $2;
              /* Copy type info from var_type */
              $$.signedness = $1.signedness;
              $$.bit_width  = $1.bit_width;
           }
         ;

/* Return the modified registers list */
code : '{' statements '}'
       {
           c->inst.code_begin = c->input_buffer + @2.first_column - 1;
           c->inst.code_end = c->input_buffer + @2.last_column - 1;
       }
     | '{'
       {
           /* Nop */
       }
       '}'
     ;

argument_decl : REG
                {
                    emit_arg(c, &@1, &$1);
                    /* Enqueue register into initialization list */
                    g_array_append_val(c->inst.init_list, $1);
                }
              | PRED
                {
                    emit_arg(c, &@1, &$1);
                    /* Enqueue predicate into initialization list */
                    g_array_append_val(c->inst.init_list, $1);
                }
              | IN REG
                {
                    emit_arg(c, &@2, &$2);
                }
              | IN PRED
                {
                    emit_arg(c, &@2, &$2);
                }
              | IMM
                {
                    EMIT_SIG(c, ", int %ciV", $1.imm.id);
                }
              ;

code_block : '{' statements '}'
           | '{' '}'
           ;

/* A list of one or more statements */
statements : statements statement
           | statement
           ;

/* Statements can be assignment (rvalue ';'), control or memory statements */
statement : control_statement
          | var_decl ';'
          | rvalue ';'
          | code_block
          | ';'
          ;

assign_statement : lvalue '=' rvalue
                   {
                       @1.last_column = @3.last_column;
                       gen_assign(c, &@1, &$1, &$3);
                       $$ = $1;
                   }
                 | var_decl '=' rvalue
                   {
                       @1.last_column = @3.last_column;
                       gen_assign(c, &@1, &$1, &$3);
                       $$ = $1;
                   }
                 | lvalue INC rvalue
                   {
                       @1.last_column = @3.last_column;
                       HexValue tmp = gen_bin_op(c, &@1, ADD_OP, &$1, &$3);
                       gen_assign(c, &@1, &$1, &tmp);
                       $$ = $1;
                   }
                 | lvalue DEC rvalue
                   {
                       @1.last_column = @3.last_column;
                       HexValue tmp = gen_bin_op(c, &@1, SUB_OP, &$1, &$3);
                       gen_assign(c, &@1, &$1, &tmp);
                       $$ = $1;
                   }
                 | lvalue ANDA rvalue
                   {
                       @1.last_column = @3.last_column;
                       HexValue tmp = gen_bin_op(c, &@1, ANDB_OP, &$1, &$3);
                       gen_assign(c, &@1, &$1, &tmp);
                       $$ = $1;
                   }
                 | lvalue ORA rvalue
                   {
                       @1.last_column = @3.last_column;
                       HexValue tmp = gen_bin_op(c, &@1, ORB_OP, &$1, &$3);
                       gen_assign(c, &@1, &$1, &tmp);
                       $$ = $1;
                   }
                 | lvalue XORA rvalue
                   {
                       @1.last_column = @3.last_column;
                       HexValue tmp = gen_bin_op(c, &@1, XORB_OP, &$1, &$3);
                       gen_assign(c, &@1, &$1, &tmp);
                       $$ = $1;
                   }
                 | PRED '=' rvalue
                   {
                       @1.last_column = @3.last_column;
                       gen_pred_assign(c, &@1, &$1, &$3);
                   }
                 | IMM '=' rvalue
                   {
                       @1.last_column = @3.last_column;
                       yyassert(c, &@1, $3.type == IMMEDIATE,
                                "Cannot assign non-immediate to immediate!");
                       yyassert(c, &@1, $1.imm.type == VARIABLE,
                                "Cannot assign to non-variable!");
                       /* Assign to the function argument */
                       OUT(c, &@1, &$1, " = ", &$3, ";\n");
                       $$ = $1;
                   }
                 | LOAD '(' IMM ',' IMM ',' SIGN ',' var ',' lvalue ')'
                   {
                       @1.last_column = @12.last_column;
                       yyassert(c, &@1, !is_inside_ternary(c),
                                "Assignment side-effect not modeled!");
                       yyassert(c, &@1, $3.imm.value == 1,
                                "LOAD of arrays not supported!");
                       gen_load(c, &@1, &$5, $7, &$9, &$11);
                   }
                 | STORE '(' IMM ',' IMM ',' var ',' rvalue ')'
                   /* Store primitive */
                   {
                       @1.last_column = @10.last_column;
                       yyassert(c, &@1, !is_inside_ternary(c),
                                "Assignment side-effect not modeled!");
                       yyassert(c, &@1, $3.imm.value == 1,
                                "STORE of arrays not supported!");
                       gen_store(c, &@1, &$5, &$7, &$9);
                   }
                 | LPCFG '=' rvalue
                   {
                       @1.last_column = @3.last_column;
                       yyassert(c, &@1, !is_inside_ternary(c),
                                "Assignment side-effect not modeled!");
                       $3 = gen_rvalue_truncate(c, &@1, &$3);
                       $3 = rvalue_materialize(c, &@1, &$3);
                       OUT(c, &@1, "gen_set_usr_field(ctx, USR_LPCFG, ", &$3, ");\n");
                   }
                 | DEPOSIT '(' rvalue ',' rvalue ',' rvalue ')'
                   {
                       @1.last_column = @8.last_column;
                       yyassert(c, &@1, !is_inside_ternary(c),
                                "Assignment side-effect not modeled!");
                       gen_deposit_op(c, &@1, &$5, &$7, &$3, &$1);
                   }
                 | SETHALF '(' rvalue ',' lvalue ',' rvalue ')'
                   {
                       @1.last_column = @8.last_column;
                       yyassert(c, &@1, !is_inside_ternary(c),
                                "Assignment side-effect not modeled!");
                       gen_sethalf(c, &@1, &$1, &$3, &$5, &$7);
                   }
                 | SETBITS '(' rvalue ',' rvalue ',' rvalue ',' rvalue ')'
                   {
                       @1.last_column = @10.last_column;
                       yyassert(c, &@1, !is_inside_ternary(c),
                                "Assignment side-effect not modeled!");
                       gen_setbits(c, &@1, &$3, &$5, &$7, &$9);
                   }
                 | INSBITS '(' lvalue ',' rvalue ',' rvalue ',' rvalue ')'
                   {
                       @1.last_column = @10.last_column;
                       yyassert(c, &@1, !is_inside_ternary(c),
                                "Assignment side-effect not modeled!");
                       gen_rdeposit_op(c, &@1, &$3, &$9, &$7, &$5);
                   }
                 | IDENTITY '(' rvalue ')'
                   {
                       @1.last_column = @4.last_column;
                       $$ = $3;
                   }
                 ;

control_statement : frame_check
                  | cancel_statement
                  | if_statement
                  | for_statement
                  ;

frame_check : FCHK '(' rvalue ',' rvalue ')' ';'
            ;

cancel_statement : LOAD_CANCEL
                   {
                       gen_load_cancel(c, &@1);
                   }
                 | STORE_CANCEL
                   {
                       gen_cancel(c, &@1);
                   }
                 | CANCEL
                 ;

if_statement : if_stmt
               {
                   /* Fix else label */
                   OUT(c, &@1, "gen_set_label(if_label_", &$1, ");\n");
               }
             | if_stmt ELSE
               {
                   @1.last_column = @2.last_column;
                   $2 = gen_if_else(c, &@1, $1);
               }
               statement
               {
                   OUT(c, &@1, "gen_set_label(if_label_", &$2, ");\n");
               }
             ;

for_statement : FOR '(' IMM '=' IMM ';' IMM '<' IMM ';' IMM PLUSPLUS ')'
                {
                    yyassert(c, &@3,
                             $3.imm.type == I &&
                             $7.imm.type == I &&
                             $11.imm.type == I,
                             "Loop induction variable must be \"i\"");
                    @1.last_column = @13.last_column;
                    OUT(c, &@1, "for (int ", &$3, " = ", &$5, "; ",
                        &$7, " < ", &$9);
                    OUT(c, &@1, "; ", &$11, "++) {\n");
                }
                code_block
                {
                    OUT(c, &@1, "}\n");
                }
              ;

if_stmt : IF '(' rvalue ')'
          {
              @1.last_column = @3.last_column;
              $1 = gen_if_cond(c, &@1, &$3);
          }
          statement
          {
              $$ = $1;
          }
        ;

rvalue : FAIL
         {
             yyassert(c, &@1, false, "Encountered a FAIL token as rvalue.\n");
         }
       | assign_statement
       | REG
         {
             $$ = $1;
         }
       | IMM
         {
             $$ = $1;
         }
       | PRED
         {
             $$ = gen_rvalue_pred(c, &@1, &$1);
         }
       | PC
         {
             /* Read PC from the CR */
             HexValue rvalue;
             memset(&rvalue, 0, sizeof(HexValue));
             rvalue.type = IMMEDIATE;
             rvalue.imm.type = IMM_PC;
             rvalue.bit_width = 32;
             rvalue.signedness = UNSIGNED;
             $$ = rvalue;
         }
       | CONSTEXT
         {
             HexValue rvalue;
             memset(&rvalue, 0, sizeof(HexValue));
             rvalue.type = IMMEDIATE;
             rvalue.imm.type = IMM_CONSTEXT;
             rvalue.signedness = UNSIGNED;
             rvalue.is_dotnew = false;
             $$ = rvalue;
         }
       | var
         {
             $$ = gen_rvalue_var(c, &@1, &$1);
         }
       | MPY '(' rvalue ',' rvalue ')'
         {
             @1.last_column = @6.last_column;
             $$ = gen_rvalue_mpy(c, &@1, &$1, &$3, &$5);
         }
       | rvalue '+' rvalue
         {
             @1.last_column = @3.last_column;
             $$ = gen_bin_op(c, &@1, ADD_OP, &$1, &$3);
         }
       | rvalue '-' rvalue
         {
             @1.last_column = @3.last_column;
             $$ = gen_bin_op(c, &@1, SUB_OP, &$1, &$3);
         }
       | rvalue '*' rvalue
         {
             @1.last_column = @3.last_column;
             $$ = gen_bin_op(c, &@1, MUL_OP, &$1, &$3);
         }
       | rvalue ASL rvalue
         {
             @1.last_column = @3.last_column;
             $$ = gen_bin_op(c, &@1, ASL_OP, &$1, &$3);
         }
       | rvalue ASR rvalue
         {
             @1.last_column = @3.last_column;
             assert_signedness(c, &@1, $1.signedness);
             if ($1.signedness == UNSIGNED) {
                 $$ = gen_bin_op(c, &@1, LSR_OP, &$1, &$3);
             } else if ($1.signedness == SIGNED) {
                 $$ = gen_bin_op(c, &@1, ASR_OP, &$1, &$3);
             }
         }
       | rvalue LSR rvalue
         {
             @1.last_column = @3.last_column;
             $$ = gen_bin_op(c, &@1, LSR_OP, &$1, &$3);
         }
       | rvalue '&' rvalue
         {
             @1.last_column = @3.last_column;
             $$ = gen_bin_op(c, &@1, ANDB_OP, &$1, &$3);
         }
       | rvalue '|' rvalue
         {
             @1.last_column = @3.last_column;
             $$ = gen_bin_op(c, &@1, ORB_OP, &$1, &$3);
         }
       | rvalue '^' rvalue
         {
             @1.last_column = @3.last_column;
             $$ = gen_bin_op(c, &@1, XORB_OP, &$1, &$3);
         }
       | rvalue ANDL rvalue
         {
             @1.last_column = @3.last_column;
             $$ = gen_bin_op(c, &@1, ANDL_OP, &$1, &$3);
         }
       | MIN '(' rvalue ',' rvalue ')'
         {
             @1.last_column = @3.last_column;
             $$ = gen_bin_op(c, &@1, MINI_OP, &$3, &$5);
         }
       | MAX '(' rvalue ',' rvalue ')'
         {
             @1.last_column = @3.last_column;
             $$ = gen_bin_op(c, &@1, MAXI_OP, &$3, &$5);
         }
       | '~' rvalue
         {
             @1.last_column = @2.last_column;
             $$ = gen_rvalue_not(c, &@1, &$2);
         }
       | '!' rvalue
         {
             @1.last_column = @2.last_column;
             $$ = gen_rvalue_notl(c, &@1, &$2);
         }
       | SAT '(' IMM ',' rvalue ')'
         {
             @1.last_column = @6.last_column;
             $$ = gen_rvalue_sat(c, &@1, &$1, &$3, &$5);
         }
       | CAST rvalue
         {
             @1.last_column = @2.last_column;
             $$ = gen_cast_op(c, &@1, &$2, $1.bit_width, $1.signedness);
         }
       | rvalue EQ rvalue
         {
             @1.last_column = @3.last_column;
             $$ = gen_bin_cmp(c, &@1, TCG_COND_EQ, &$1, &$3);
         }
       | rvalue NEQ rvalue
         {
             @1.last_column = @3.last_column;
             $$ = gen_bin_cmp(c, &@1, TCG_COND_NE, &$1, &$3);
         }
       | rvalue '<' rvalue
         {
             @1.last_column = @3.last_column;

             assert_signedness(c, &@1, $1.signedness);
             assert_signedness(c, &@1, $3.signedness);
             if ($1.signedness == UNSIGNED || $3.signedness == UNSIGNED) {
                 $$ = gen_bin_cmp(c, &@1, TCG_COND_LTU, &$1, &$3);
             } else {
                 $$ = gen_bin_cmp(c, &@1, TCG_COND_LT, &$1, &$3);
             }
         }
       | rvalue '>' rvalue
         {
             @1.last_column = @3.last_column;

             assert_signedness(c, &@1, $1.signedness);
             assert_signedness(c, &@1, $3.signedness);
             if ($1.signedness == UNSIGNED || $3.signedness == UNSIGNED) {
                 $$ = gen_bin_cmp(c, &@1, TCG_COND_GTU, &$1, &$3);
             } else {
                 $$ = gen_bin_cmp(c, &@1, TCG_COND_GT, &$1, &$3);
             }
         }
       | rvalue LTE rvalue
         {
             @1.last_column = @3.last_column;

             assert_signedness(c, &@1, $1.signedness);
             assert_signedness(c, &@1, $3.signedness);
             if ($1.signedness == UNSIGNED || $3.signedness == UNSIGNED) {
                 $$ = gen_bin_cmp(c, &@1, TCG_COND_LEU, &$1, &$3);
             } else {
                 $$ = gen_bin_cmp(c, &@1, TCG_COND_LE, &$1, &$3);
             }
         }
       | rvalue GTE rvalue
         {
             @1.last_column = @3.last_column;

             assert_signedness(c, &@1, $1.signedness);
             assert_signedness(c, &@1, $3.signedness);
             if ($1.signedness == UNSIGNED || $3.signedness == UNSIGNED) {
                 $$ = gen_bin_cmp(c, &@1, TCG_COND_GEU, &$1, &$3);
             } else {
                 $$ = gen_bin_cmp(c, &@1, TCG_COND_GE, &$1, &$3);
             }
         }
       | rvalue '?'
         {
             Ternary t = { 0 };
             t.state = IN_LEFT;
             t.cond = $1;
             g_array_append_val(c->ternary, t);
         }
         rvalue ':'
         {
             Ternary *t = &g_array_index(c->ternary, Ternary,
                                         c->ternary->len - 1);
             t->state = IN_RIGHT;
         }
         rvalue
         {
             @1.last_column = @5.last_column;
             $$ = gen_rvalue_ternary(c, &@1, &$1, &$4, &$7);
         }
       | FSCR '(' rvalue ')'
         {
             @1.last_column = @4.last_column;
             $$ = gen_rvalue_fscr(c, &@1, &$3);
         }
       | SXT '(' rvalue ',' IMM ',' rvalue ')'
         {
             @1.last_column = @8.last_column;
             yyassert(c, &@1, $5.type == IMMEDIATE &&
                      $5.imm.type == VALUE,
                      "SXT expects immediate values\n");
             $$ = gen_extend_op(c, &@1, &$3, 64, &$7, SIGNED);
         }
       | ZXT '(' rvalue ',' IMM ',' rvalue ')'
         {
             @1.last_column = @8.last_column;
             yyassert(c, &@1, $5.type == IMMEDIATE &&
                      $5.imm.type == VALUE,
                      "ZXT expects immediate values\n");
             $$ = gen_extend_op(c, &@1, &$3, 64, &$7, UNSIGNED);
         }
       | '(' rvalue ')'
         {
             $$ = $2;
         }
       | ABS rvalue
         {
             @1.last_column = @2.last_column;
             $$ = gen_rvalue_abs(c, &@1, &$2);
         }
       | CROUND '(' rvalue ',' rvalue ')'
         {
             @1.last_column = @6.last_column;
             $$ = gen_convround_n(c, &@1, &$3, &$5);
         }
       | CROUND '(' rvalue ')'
         {
             @1.last_column = @4.last_column;
             $$ = gen_convround(c, &@1, &$3);
         }
       | ROUND '(' rvalue ',' rvalue ')'
         {
             @1.last_column = @6.last_column;
             $$ = gen_round(c, &@1, &$3, &$5);
         }
       | '-' rvalue
         {
             @1.last_column = @2.last_column;
             $$ = gen_rvalue_neg(c, &@1, &$2);
         }
       | ICIRC '(' rvalue ')' ASL IMM
         {
             @1.last_column = @6.last_column;
             $$ = gen_tmp(c, &@1, 32, UNSIGNED);
             OUT(c, &@1, "gen_read_ireg(", &$$, ", ", &$3, ", ", &$6, ");\n");
         }
       | CIRCADD '(' rvalue ',' rvalue ',' rvalue ')'
         {
             @1.last_column = @8.last_column;
             gen_circ_op(c, &@1, &$3, &$5, &$7);
         }
       | LOCNT '(' rvalue ')'
         {
             @1.last_column = @4.last_column;
             /* Leading ones count */
             $$ = gen_locnt_op(c, &@1, &$3);
         }
       | COUNTONES '(' rvalue ')'
         {
             @1.last_column = @4.last_column;
             /* Ones count */
             $$ = gen_ctpop_op(c, &@1, &$3);
         }
       | EXTRACT '(' rvalue ',' rvalue ')'
         {
             @1.last_column = @6.last_column;
             $$ = gen_extract_op(c, &@1, &$5, &$3, &$1);
         }
       | EXTRANGE '(' rvalue ',' rvalue ',' rvalue ')'
         {
             @1.last_column = @8.last_column;
             yyassert(c, &@1, $5.type == IMMEDIATE &&
                      $5.imm.type == VALUE &&
                      $7.type == IMMEDIATE &&
                      $7.imm.type == VALUE,
                      "Range extract needs immediate values!\n");
             $$ = gen_rextract_op(c,
                                  &@1,
                                  &$3,
                                  $7.imm.value,
                                  $5.imm.value - $7.imm.value + 1);
         }
       | CAST4_8U '(' rvalue ')'
         {
             @1.last_column = @4.last_column;
             $$ = gen_rvalue_truncate(c, &@1, &$3);
             $$.signedness = UNSIGNED;
             $$ = rvalue_materialize(c, &@1, &$$);
             $$ = gen_rvalue_extend(c, &@1, &$$);
         }
       | BREV '(' rvalue ')'
         {
             @1.last_column = @4.last_column;
             $$ = gen_rvalue_brev(c, &@1, &$3);
         }
       | ROTL '(' rvalue ',' rvalue ')'
         {
             @1.last_column = @6.last_column;
             $$ = gen_rotl(c, &@1, &$3, &$5);
         }
       | ADDSAT64 '(' rvalue ',' rvalue ',' rvalue ')'
         {
             @1.last_column = @8.last_column;
             gen_addsat64(c, &@1, &$3, &$5, &$7);
         }
       | CARRY_FROM_ADD '(' rvalue ',' rvalue ',' rvalue ')'
         {
             @1.last_column = @8.last_column;
             $$ = gen_carry_from_add(c, &@1, &$3, &$5, &$7);
         }
       | LSBNEW '(' rvalue ')'
         {
             @1.last_column = @4.last_column;
             HexValue one = gen_imm_value(c, &@1, 1, 32, UNSIGNED);
             $$ = gen_bin_op(c, &@1, ANDB_OP, &$3, &one);
         }
       ;

lvalue : FAIL
         {
             @1.last_column = @1.last_column;
             yyassert(c, &@1, false, "Encountered a FAIL token as lvalue.\n");
         }
       | REG
         {
             $$ = $1;
         }
       | var
         {
             $$ = $1;
         }
       ;

%%

int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr,
                "Semantics: Hexagon ISA to tinycode generator compiler\n\n");
        fprintf(stderr,
                "Usage: ./semantics IDEFS EMITTER_C EMITTER_H "
                "ENABLED_INSTRUCTIONS_LIST\n");
        return 1;
    }

    enum {
        ARG_INDEX_ARGV0 = 0,
        ARG_INDEX_IDEFS,
        ARG_INDEX_EMITTER_C,
        ARG_INDEX_EMITTER_H,
        ARG_INDEX_ENABLED_INSTRUCTIONS_LIST
    };

    FILE *enabled_file = fopen(argv[ARG_INDEX_ENABLED_INSTRUCTIONS_LIST], "w");

    FILE *output_file = fopen(argv[ARG_INDEX_EMITTER_C], "w");
    fputs("#include \"qemu/osdep.h\"\n", output_file);
    fputs("#include \"qemu/log.h\"\n", output_file);
    fputs("#include \"cpu.h\"\n", output_file);
    fputs("#include \"internal.h\"\n", output_file);
    fputs("#include \"tcg/tcg.h\"\n", output_file);
    fputs("#include \"tcg/tcg-op.h\"\n", output_file);
    fputs("#include \"exec/helper-gen.h\"\n", output_file);
    fputs("#include \"insn.h\"\n", output_file);
    fputs("#include \"opcodes.h\"\n", output_file);
    fputs("#include \"translate.h\"\n", output_file);
    fputs("#define QEMU_GENERATE\n", output_file);
    fputs("#include \"genptr.h\"\n", output_file);
    fputs("#include \"macros.h\"\n", output_file);
    fprintf(output_file, "#include \"%s\"\n", argv[ARG_INDEX_EMITTER_H]);

    FILE *defines_file = fopen(argv[ARG_INDEX_EMITTER_H], "w");
    assert(defines_file != NULL);
    fputs("#ifndef HEX_EMITTER_H\n", defines_file);
    fputs("#define HEX_EMITTER_H\n", defines_file);
    fputs("\n", defines_file);
    fputs("#include \"insn.h\"\n\n", defines_file);

    /* Parser input file */
    Context context = { 0 };
    context.defines_file = defines_file;
    context.output_file = output_file;
    context.enabled_file = enabled_file;
    /* Initialize buffers */
    context.out_str = g_string_new(NULL);
    context.signature_str = g_string_new(NULL);
    context.header_str = g_string_new(NULL);
    context.ternary = g_array_new(FALSE, TRUE, sizeof(Ternary));
    /* Read input file */
    FILE *input_file = fopen(argv[ARG_INDEX_IDEFS], "r");
    fseek(input_file, 0L, SEEK_END);
    long input_size = ftell(input_file);
    context.input_buffer = (char *) calloc(input_size + 1, sizeof(char));
    fseek(input_file, 0L, SEEK_SET);
    size_t read_chars = fread(context.input_buffer,
                              sizeof(char),
                              input_size,
                              input_file);
    if (read_chars != (size_t) input_size) {
        fprintf(stderr, "Error: an error occurred while reading input file!\n");
        return -1;
    }
    yylex_init(&context.scanner);
    YY_BUFFER_STATE buffer;
    buffer = yy_scan_string(context.input_buffer, context.scanner);
    /* Start the parsing procedure */
    yyparse(context.scanner, &context);
    if (context.implemented_insn != context.total_insn) {
        fprintf(stderr,
                "Warning: %d/%d meta instructions have been implemented!\n",
                context.implemented_insn,
                context.total_insn);
    }
    fputs("#endif " START_COMMENT " HEX_EMITTER_h " END_COMMENT "\n",
          defines_file);
    /* Cleanup */
    yy_delete_buffer(buffer, context.scanner);
    yylex_destroy(context.scanner);
    free(context.input_buffer);
    g_string_free(context.out_str, TRUE);
    g_string_free(context.signature_str, TRUE);
    g_string_free(context.header_str, TRUE);
    g_array_free(context.ternary, TRUE);
    fclose(output_file);
    fclose(input_file);
    fclose(defines_file);
    fclose(enabled_file);

    return 0;
}
