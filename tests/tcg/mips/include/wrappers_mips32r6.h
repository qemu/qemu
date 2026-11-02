/*
 * Wrappers for MIPS32R6 instructions operating on 32-bit values
 * (32-bit counterpart of wrappers_mips64r6.h).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WRAPPERS_MIPS32R6_H
#define WRAPPERS_MIPS32R6_H

#include <stdint.h>

#define DO_MIPS32R6__RD__RS(suffix, mnemonic)                          \
static inline void do_mips32r6_##suffix(const uint32_t *input,         \
                                        uint32_t *output)              \
{                                                                      \
   __asm__ volatile (                                                  \
      "lw $t1, 0(%0)\n\t"                                              \
      #mnemonic " $t0, $t1\n\t"                                        \
      "sw $t0, 0(%1)\n\t"                                              \
      :                                                                \
      : "r" (input), "r" (output)                                      \
      : "t0", "t1", "memory"                                           \
   );                                                                  \
}

#define DO_MIPS32R6__RD__RS_RT(suffix, mnemonic)                       \
static inline void do_mips32r6_##suffix(const uint32_t *input1,        \
                                        const uint32_t *input2,        \
                                        uint32_t *output)              \
{                                                                      \
   __asm__ volatile (                                                  \
      "lw $t1, 0(%0)\n\t"                                              \
      "lw $t2, 0(%1)\n\t"                                              \
      #mnemonic " $t0, $t1, $t2\n\t"                                   \
      "sw $t0, 0(%2)\n\t"                                              \
      :                                                                \
      : "r" (input1), "r" (input2), "r" (output)                       \
      : "t0", "t1", "t2", "memory"                                     \
   );                                                                  \
}

DO_MIPS32R6__RD__RS(CLO, clo)
DO_MIPS32R6__RD__RS(CLZ, clz)
DO_MIPS32R6__RD__RS(BITSWAP, bitswap)

DO_MIPS32R6__RD__RS_RT(SLLV, sllv)
DO_MIPS32R6__RD__RS_RT(SRLV, srlv)
DO_MIPS32R6__RD__RS_RT(SRAV, srav)

/* LSA rd, rs, rt, sa: rd = (rs << sa) + rt, with sa in 1..4. */
#define DO_MIPS32R6__LSA(suffix, sa)                                   \
static inline void do_mips32r6_##suffix(const uint32_t *input1,        \
                                        const uint32_t *input2,        \
                                        uint32_t *output)              \
{                                                                      \
   __asm__ volatile (                                                  \
      "lw $t1, 0(%0)\n\t"                                              \
      "lw $t2, 0(%1)\n\t"                                              \
      "lsa $t0, $t1, $t2, " #sa "\n\t"                                 \
      "sw $t0, 0(%2)\n\t"                                              \
      :                                                                \
      : "r" (input1), "r" (input2), "r" (output)                       \
      : "t0", "t1", "t2", "memory"                                     \
   );                                                                  \
}

DO_MIPS32R6__LSA(LSA1, 1)
DO_MIPS32R6__LSA(LSA2, 2)
DO_MIPS32R6__LSA(LSA3, 3)
DO_MIPS32R6__LSA(LSA4, 4)

DO_MIPS32R6__RD__RS_RT(MUL, mul)
DO_MIPS32R6__RD__RS_RT(MUH, muh)
DO_MIPS32R6__RD__RS_RT(MULU, mulu)
DO_MIPS32R6__RD__RS_RT(MUHU, muhu)

DO_MIPS32R6__RD__RS_RT(DIV, div)
DO_MIPS32R6__RD__RS_RT(MOD, mod)
DO_MIPS32R6__RD__RS_RT(DIVU, divu)
DO_MIPS32R6__RD__RS_RT(MODU, modu)

DO_MIPS32R6__RD__RS_RT(AND, and)
DO_MIPS32R6__RD__RS_RT(NOR, nor)
DO_MIPS32R6__RD__RS_RT(OR, or)
DO_MIPS32R6__RD__RS_RT(XOR, xor)

DO_MIPS32R6__RD__RS_RT(SELEQZ, seleqz)
DO_MIPS32R6__RD__RS_RT(SELNEZ, selnez)

#endif
