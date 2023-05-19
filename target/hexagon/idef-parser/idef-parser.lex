%option noyywrap noinput nounput
%option 8bit reentrant bison-bridge
%option warn nodefault
%option bison-locations

%{
/*
 *  Copyright(c) 2019-2023 rev.ng Labs Srl. All Rights Reserved.
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

#include <string.h>
#include <stdbool.h>

#include "hex_regs.h"

#include "idef-parser.h"
#include "idef-parser.tab.h"

/* Keep track of scanner position for error message printout */
#define YY_USER_ACTION yylloc->first_column = yylloc->last_column; \
    for (int i = 0; yytext[i] != '\0'; i++) {   \
        yylloc->last_column++;                  \
    }

/* Global Error Counter */
int error_count;

%}

/* Definitions */
DIGIT                    [0-9]
LOWER_ID                 [a-z]
UPPER_ID                 [A-Z]
ID                       LOWER_ID|UPPER_ID
INST_NAME                [A-Z]+[0-9]_([A-Za-z]|[0-9]|_)+
HEX_DIGIT                [0-9a-fA-F]
REG_ID_32                e|s|d|t|u|v|x|y
REG_ID_64                ee|ss|dd|tt|uu|vv|xx|yy
SYS_ID_32                s|d
SYS_ID_64                ss|dd
PRED_ID                  d|s|t|u|v|e|x|x
IMM_ID                   r|s|S|u|U
VAR_ID                   [a-zA-Z_][a-zA-Z0-9_]*
SIGN_ID                  s|u
STRING_LIT               \"(\\.|[^"\\])*\"

/* Tokens */
%%

[ \t\f\v]+                { /* Ignore whitespaces. */ }
[\n\r]+                   { /* Ignore newlines. */ }
^#.*$                     { /* Ignore linemarkers. */ }

{INST_NAME}               { yylval->string = g_string_new(yytext);
                            return INAME; }
"fFLOAT"                 |
"fUNFLOAT"               |
"fDOUBLE"                |
"fUNDOUBLE"              |
"0.0"                    |
"0x1.0p52"               |
"0x1.0p-52"              { return FAIL; }
"in"                     { return IN; }
"R"{REG_ID_32}"V" {
                           yylval->rvalue.type = REGISTER_ARG;
                           yylval->rvalue.reg.type = GENERAL_PURPOSE;
                           yylval->rvalue.reg.id = yytext[1];
                           yylval->rvalue.reg.bit_width = 32;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.is_dotnew = false;
                           yylval->rvalue.signedness = SIGNED;
                           return REG; }
"R"{REG_ID_64}"V" {
                           yylval->rvalue.type = REGISTER_ARG;
                           yylval->rvalue.reg.type = GENERAL_PURPOSE;
                           yylval->rvalue.reg.id = yytext[1];
                           yylval->rvalue.reg.bit_width = 64;
                           yylval->rvalue.bit_width = 64;
                           yylval->rvalue.is_dotnew = false;
                           yylval->rvalue.signedness = SIGNED;
                           return REG; }
"MuV" {
                           yylval->rvalue.type = REGISTER_ARG;
                           yylval->rvalue.reg.type = MODIFIER;
                           yylval->rvalue.reg.id = 'u';
                           yylval->rvalue.reg.bit_width = 32;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.signedness = SIGNED;
                           return REG; }
"C"{REG_ID_32}"V" {
                           yylval->rvalue.type = REGISTER_ARG;
                           yylval->rvalue.reg.type = CONTROL;
                           yylval->rvalue.reg.id = yytext[1];
                           yylval->rvalue.reg.bit_width = 32;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.is_dotnew = false;
                           yylval->rvalue.signedness = SIGNED;
                           return REG; }
"C"{REG_ID_64}"V" {
                           yylval->rvalue.type = REGISTER_ARG;
                           yylval->rvalue.reg.type = CONTROL;
                           yylval->rvalue.reg.id = yytext[1];
                           yylval->rvalue.reg.bit_width = 64;
                           yylval->rvalue.bit_width = 64;
                           yylval->rvalue.is_dotnew = false;
                           yylval->rvalue.signedness = SIGNED;
                           return REG; }
{IMM_ID}"iV" {
                           yylval->rvalue.type = IMMEDIATE;
                           yylval->rvalue.signedness = SIGNED;
                           yylval->rvalue.imm.type = VARIABLE;
                           yylval->rvalue.imm.id = yytext[0];
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.is_dotnew = false;
                           return IMM; }
"P"{PRED_ID}"V" {
                           yylval->rvalue.type = PREDICATE;
                           yylval->rvalue.pred.id = yytext[1];
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.is_dotnew = false;
                           yylval->rvalue.signedness = SIGNED;
                           return PRED; }
"P"{PRED_ID}"N" {
                           yylval->rvalue.type = PREDICATE;
                           yylval->rvalue.pred.id = yytext[1];
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.is_dotnew = true;
                           yylval->rvalue.signedness = SIGNED;
                           return PRED; }
"+="                     { return INC; }
"-="                     { return DEC; }
"++"                     { return PLUSPLUS; }
"&="                     { return ANDA; }
"|="                     { return ORA; }
"^="                     { return XORA; }
"<<"                     { return ASL; }
">>"                     { return ASR; }
">>>"                    { return LSR; }
"=="                     { return EQ; }
"!="                     { return NEQ; }
"<="                     { return LTE; }
">="                     { return GTE; }
"&&"                     { return ANDL; }
"else"                   { return ELSE; }
"for"                    { return FOR; }
"fREAD_IREG"             { return ICIRC; }
"if"                     { return IF; }
"fFRAME_SCRAMBLE"        |
"fFRAME_UNSCRAMBLE"      { return FSCR; }
"fFRAMECHECK"            { return FCHK; }
"Constant_extended"      { return CONSTEXT; }
"fCL1_"{DIGIT}           { return LOCNT; }
"fbrev"                  { return BREV; }
"fSXTN"                  { return SXT; }
"fZXTN"                  { return ZXT; }
"fDF_MAX"                |
"fSF_MAX"                |
"fMAX"                   { return MAX; }
"fDF_MIN"                |
"fSF_MIN"                |
"fMIN"                   { return MIN; }
"fABS"                   { return ABS; }
"fRNDN"                  { return ROUND; }
"fCRND"                  { return CROUND; }
"fCRNDN"                 { return CROUND; }
"fPM_CIRI"               { return CIRCADD; }
"fPM_CIRR"               { return CIRCADD; }
"fCOUNTONES_"{DIGIT}     { return COUNTONES; }
"fSATN"                  { yylval->sat.signedness = SIGNED;
                           return SAT; }
"fSATUN"                 { yylval->sat.signedness = UNSIGNED;
                           return SAT; }
"fCONSTLL"               { yylval->cast.bit_width = 64;
                           yylval->cast.signedness = SIGNED;
                           return CAST; }
"fSE32_64"               { yylval->cast.bit_width = 64;
                           yylval->cast.signedness = SIGNED;
                           return CAST; }
"fCAST4_4u"              { yylval->cast.bit_width = 32;
                           yylval->cast.signedness = UNSIGNED;
                           return CAST; }
"fCAST4_8s"              { yylval->cast.bit_width = 64;
                           yylval->cast.signedness = SIGNED;
                           return CAST; }
"fCAST4_8u"              { return CAST4_8U; }
"fCAST4u"                { yylval->cast.bit_width = 32;
                           yylval->cast.signedness = UNSIGNED;
                           return CAST; }
"fNEWREG"                |
"fCAST4_4s"              |
"fCAST4s"                { yylval->cast.bit_width = 32;
                           yylval->cast.signedness = SIGNED;
                           return CAST; }
"fCAST8_8u"              { yylval->cast.bit_width = 64;
                           yylval->cast.signedness = UNSIGNED;
                           return CAST; }
"fCAST8u"                { yylval->cast.bit_width = 64;
                           yylval->cast.signedness = UNSIGNED;
                           return CAST; }
"fCAST8_8s"              |
"fCAST8s"                { yylval->cast.bit_width = 64;
                           yylval->cast.signedness = SIGNED;
                           return CAST; }
"fGETBIT"                { yylval->extract.bit_width = 1;
                           yylval->extract.storage_bit_width = 1;
                           yylval->extract.signedness = UNSIGNED;
                           return EXTRACT; }
"fGETBYTE"               { yylval->extract.bit_width = 8;
                           yylval->extract.storage_bit_width = 8;
                           yylval->extract.signedness = SIGNED;
                           return EXTRACT; }
"fGETUBYTE"              { yylval->extract.bit_width = 8;
                           yylval->extract.storage_bit_width = 8;
                           yylval->extract.signedness = UNSIGNED;
                           return EXTRACT; }
"fGETHALF"               { yylval->extract.bit_width = 16;
                           yylval->extract.storage_bit_width = 16;
                           yylval->extract.signedness = SIGNED;
                           return EXTRACT; }
"fGETUHALF"              { yylval->extract.bit_width = 16;
                           yylval->extract.storage_bit_width = 16;
                           yylval->extract.signedness = UNSIGNED;
                           return EXTRACT; }
"fGETWORD"               { yylval->extract.bit_width = 32;
                           yylval->extract.storage_bit_width = 64;
                           yylval->extract.signedness = SIGNED;
                           return EXTRACT; }
"fGETUWORD"              { yylval->extract.bit_width = 32;
                           yylval->extract.storage_bit_width = 64;
                           yylval->extract.signedness = UNSIGNED;
                           return EXTRACT; }
"fEXTRACTU_RANGE"        { return EXTRANGE; }
"fSETBIT"                { yylval->cast.bit_width = 1;
                           yylval->cast.signedness = SIGNED;
                           return DEPOSIT; }
"fSETBYTE"               { yylval->cast.bit_width = 8;
                           yylval->cast.signedness = SIGNED;
                           return DEPOSIT; }
"fSETHALF"               { yylval->cast.bit_width = 16;
                           yylval->cast.signedness = SIGNED;
                           return SETHALF; }
"fSETWORD"               { yylval->cast.bit_width = 32;
                           yylval->cast.signedness = SIGNED;
                           return DEPOSIT; }
"fINSERT_BITS"           { return INSBITS; }
"fSETBITS"               { return SETBITS; }
"fMPY16UU"               { yylval->mpy.first_bit_width = 16;
                           yylval->mpy.second_bit_width = 16;
                           yylval->mpy.first_signedness = UNSIGNED;
                           yylval->mpy.second_signedness = UNSIGNED;
                           return MPY; }
"fMPY16SU"               { yylval->mpy.first_bit_width = 16;
                           yylval->mpy.second_bit_width = 16;
                           yylval->mpy.first_signedness = SIGNED;
                           yylval->mpy.second_signedness = UNSIGNED;
                           return MPY; }
"fMPY16SS"               { yylval->mpy.first_bit_width = 16;
                           yylval->mpy.second_bit_width = 16;
                           yylval->mpy.first_signedness = SIGNED;
                           yylval->mpy.second_signedness = SIGNED;
                           return MPY; }
"fMPY32UU"               { yylval->mpy.first_bit_width = 32;
                           yylval->mpy.second_bit_width = 32;
                           yylval->mpy.first_signedness = UNSIGNED;
                           yylval->mpy.second_signedness = UNSIGNED;
                           return MPY; }
"fMPY32SU"               { yylval->mpy.first_bit_width = 32;
                           yylval->mpy.second_bit_width = 32;
                           yylval->mpy.first_signedness = SIGNED;
                           yylval->mpy.second_signedness = UNSIGNED;
                           return MPY; }
"fSFMPY"                 |
"fMPY32SS"               { yylval->mpy.first_bit_width = 32;
                           yylval->mpy.second_bit_width = 32;
                           yylval->mpy.first_signedness = SIGNED;
                           yylval->mpy.second_signedness = SIGNED;
                           return MPY; }
"fMPY3216SS"             { yylval->mpy.first_bit_width = 32;
                           yylval->mpy.second_bit_width = 16;
                           yylval->mpy.first_signedness = SIGNED;
                           yylval->mpy.second_signedness = SIGNED;
                           return MPY; }
"fMPY3216SU"             { yylval->mpy.first_bit_width = 32;
                           yylval->mpy.second_bit_width = 16;
                           yylval->mpy.first_signedness = SIGNED;
                           yylval->mpy.second_signedness = UNSIGNED;
                           return MPY; }
"fNEWREG_ST"             |
"fIMMEXT"                |
"fMUST_IMMEXT"           |
"fPASS"                  |
"fECHO"                  { return IDENTITY; }
"(size8u_t)"             { yylval->cast.bit_width = 64;
                           yylval->cast.signedness = UNSIGNED;
                           return CAST; }
"(unsigned int)"         { yylval->cast.bit_width = 32;
                           yylval->cast.signedness = UNSIGNED;
                           return CAST; }
"fREAD_PC()"             { return PC; }
"USR.LPCFG"              { return LPCFG; }
"LOAD_CANCEL(EA)"        { return LOAD_CANCEL; }
"STORE_CANCEL(EA)"       { return STORE_CANCEL; }
"CANCEL"                 { return CANCEL; }
"N"{LOWER_ID}"N"         { yylval->rvalue.type = REGISTER_ARG;
                           yylval->rvalue.reg.type = DOTNEW;
                           yylval->rvalue.reg.id = yytext[1];
                           yylval->rvalue.reg.bit_width = 32;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.signedness = UNSIGNED;
                           return REG; }
"fREAD_SP()"             |
"SP"                     { yylval->rvalue.type = REGISTER;
                           yylval->rvalue.reg.type = GENERAL_PURPOSE;
                           yylval->rvalue.reg.id = HEX_REG_SP;
                           yylval->rvalue.reg.bit_width = 32;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.signedness = UNSIGNED;
                           return REG; }
"fREAD_FP()"             |
"FP"                     { yylval->rvalue.type = REGISTER;
                           yylval->rvalue.reg.type = GENERAL_PURPOSE;
                           yylval->rvalue.reg.id = HEX_REG_FP;
                           yylval->rvalue.reg.bit_width = 32;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.signedness = UNSIGNED;
                           return REG; }
"fREAD_LR()"             |
"LR"                     { yylval->rvalue.type = REGISTER;
                           yylval->rvalue.reg.type = GENERAL_PURPOSE;
                           yylval->rvalue.reg.id = HEX_REG_LR;
                           yylval->rvalue.reg.bit_width = 32;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.signedness = UNSIGNED;
                           return REG; }
"fREAD_GP()"             |
"GP"                     { yylval->rvalue.type = REGISTER;
                           yylval->rvalue.reg.type = CONTROL;
                           yylval->rvalue.reg.id = HEX_REG_GP;
                           yylval->rvalue.reg.bit_width = 32;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.signedness = UNSIGNED;
                           return REG; }
"LC"[01]                 { yylval->rvalue.type = REGISTER;
                           yylval->rvalue.reg.type = CONTROL;
                           yylval->rvalue.reg.id = HEX_REG_LC0
                                                 + (yytext[2] - '0') * 2;
                           yylval->rvalue.reg.bit_width = 32;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.signedness = UNSIGNED;
                           return REG; }
"SA"[01]                 { yylval->rvalue.type = REGISTER;
                           yylval->rvalue.reg.type = CONTROL;
                           yylval->rvalue.reg.id = HEX_REG_SA0
                                                 + (yytext[2] - '0') * 2;
                           yylval->rvalue.reg.bit_width = 32;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.signedness = UNSIGNED;
                           return REG; }
"fREAD_P0()"             { yylval->rvalue.type = PREDICATE;
                           yylval->rvalue.pred.id = '0';
                           yylval->rvalue.bit_width = 32;
                           return PRED; }
[pP]{DIGIT}              { yylval->rvalue.type = PREDICATE;
                           yylval->rvalue.pred.id = yytext[1];
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.is_dotnew = false;
                           return PRED; }
[pP]{DIGIT}[nN]          { yylval->rvalue.type = PREDICATE;
                           yylval->rvalue.pred.id = yytext[1];
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.is_dotnew = true;
                           return PRED; }
"fLSBNEW"                { return LSBNEW; }
"N"                      { yylval->rvalue.type = IMMEDIATE;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.imm.type = VARIABLE;
                           yylval->rvalue.imm.id = 'N';
                           return IMM; }
"i"                      { yylval->rvalue.type = IMMEDIATE;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.signedness = SIGNED;
                           yylval->rvalue.imm.type = I;
                           return IMM; }
{SIGN_ID}                { if (yytext[0] == 'u') {
                               yylval->signedness = UNSIGNED;
                           } else {
                               yylval->signedness = SIGNED;
                           }
                           return SIGN;
                         }
"0x"{HEX_DIGIT}+         { uint64_t value = strtoull(yytext, NULL, 0);
                           yylval->rvalue.type = IMMEDIATE;
                           yylval->rvalue.imm.type = VALUE;
                           yylval->rvalue.imm.value = value;
                           if (value <= INT_MAX) {
                               yylval->rvalue.bit_width = sizeof(int) * 8;
                               yylval->rvalue.signedness = SIGNED;
                           } else if (value <= UINT_MAX) {
                               yylval->rvalue.bit_width = sizeof(unsigned int) * 8;
                               yylval->rvalue.signedness = UNSIGNED;
                           } else if (value <= LONG_MAX) {
                               yylval->rvalue.bit_width = sizeof(long) * 8;
                               yylval->rvalue.signedness = SIGNED;
                           } else if (value <= ULONG_MAX) {
                               yylval->rvalue.bit_width = sizeof(unsigned long) * 8;
                               yylval->rvalue.signedness = UNSIGNED;
                           } else {
                               g_assert_not_reached();
                           }
                           return IMM; }
{DIGIT}+                 { int64_t value = strtoll(yytext, NULL, 0);
                           yylval->rvalue.type = IMMEDIATE;
                           yylval->rvalue.imm.type = VALUE;
                           yylval->rvalue.imm.value = value;
                           if (value >= INT_MIN && value <= INT_MAX) {
                               yylval->rvalue.bit_width = sizeof(int) * 8;
                               yylval->rvalue.signedness = SIGNED;
                           } else if (value >= LONG_MIN && value <= LONG_MAX) {
                               yylval->rvalue.bit_width = sizeof(long) * 8;
                               yylval->rvalue.signedness = SIGNED;
                           } else {
                              g_assert_not_reached();
                           }
                           return IMM; }
"0x"{HEX_DIGIT}+"ULL"    |
{DIGIT}+"ULL"            { yylval->rvalue.type = IMMEDIATE;
                           yylval->rvalue.bit_width = 64;
                           yylval->rvalue.signedness = UNSIGNED;
                           yylval->rvalue.imm.type = VALUE;
                           yylval->rvalue.imm.value = strtoull(yytext, NULL, 0);
                           return IMM; }
"fLOAD"                  { return LOAD; }
"fSTORE"                 { return STORE; }
"fROTL"                  { return ROTL; }
"fCARRY_FROM_ADD"        { return CARRY_FROM_ADD; }
"fADDSAT64"              { return ADDSAT64; }
"size"[1248][us]"_t"     { /* Handles "size_t" variants of int types */
                           const unsigned int bits_per_byte = 8;
                           const unsigned int bytes = yytext[4] - '0';
                           yylval->rvalue.bit_width = bits_per_byte * bytes;
                           if (yytext[5] == 'u') {
                               yylval->rvalue.signedness = UNSIGNED;
                           } else {
                               yylval->rvalue.signedness = SIGNED;
                           }
                           return TYPE_SIZE_T; }
"unsigned"               { return TYPE_UNSIGNED; }
"long"                   { return TYPE_LONG; }
"int"                    { return TYPE_INT; }
"const"                  { /* Emit no token */ }
{VAR_ID}                 { /* Variable name, we adopt the C names convention */
                           yylval->rvalue.type = VARID;
                           yylval->rvalue.var.name = g_string_new(yytext);
                           /* Default to an unknown signedness and 0 width. */
                           yylval->rvalue.bit_width = 0;
                           yylval->rvalue.signedness = UNKNOWN_SIGNEDNESS;
                           return VAR; }
"fatal("{STRING_LIT}")"  { /* Emit no token */ }
"fHINTJR(RsV)"           { /* Emit no token */ }
.                        { return yytext[0]; }

%%
