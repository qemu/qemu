/* SPDX-License-Identifier: MIT */
/* Templated TCGv_i32 and TCGv_i64 operations */

DEF3(add, TCGV, TCGV, TCGV)
DEF3(addi, TCGV, TCGV, TINT)
DEF3(and, TCGV, TCGV, TCGV)
DEF1(discard, TCGV)
DEF2(mov, TCGV, TCGV)
DEF2(movi, TCGV, TINT)
DEF2(neg, TCGV, TCGV)
DEF3(or, TCGV, TCGV, TCGV)
DEF3(shl, TCGV, TCGV, TCGV)
DEF3(sub, TCGV, TCGV, TCGV)
DEF3(subi, TCGV, TCGV, TINT)
DEF3(subfi, TCGV, TINT, TCGV)
DEF3(xor, TCGV, TCGV, TCGV)
