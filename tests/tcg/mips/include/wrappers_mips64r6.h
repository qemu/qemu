/*
 *  Header file for wrappers around MIPS64R6 instructions assembler
 *  invocations
 *
 *  Copyright (C) 2019  Wave Computing, Inc.
 *  Copyright (C) 2019  Aleksandar Markovic <amarkovic@wavecomp.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#ifndef WRAPPERS_MIPS64R6_H
#define WRAPPERS_MIPS64R6_H

#include <string.h>

#define DO_MIPS64R6__RD__RS(suffix, mnemonic)                          \
static inline void do_mips64r6_##suffix(const void *input,             \
                                        void *output)                  \
{                                                                      \
   __asm__ volatile (                                                  \
      "ld $t1, 0(%0)\n\t"                                              \
      #mnemonic " $t0, $t1\n\t"                                        \
      "sd $t0, 0(%1)\n\t"                                              \
      :                                                                \
      : "r" (input), "r" (output)                                      \
      : "t0", "t1", "memory"                                           \
   );                                                                  \
}

DO_MIPS64R6__RD__RS(CLO, clo)
DO_MIPS64R6__RD__RS(CLZ, clz)
DO_MIPS64R6__RD__RS(DCLO, dclo)
DO_MIPS64R6__RD__RS(DCLZ, dclz)

DO_MIPS64R6__RD__RS(BITSWAP, bitswap)
DO_MIPS64R6__RD__RS(DBITSWAP, dbitswap)


#define DO_MIPS64R6__RD__RS_RT(suffix, mnemonic)                       \
static inline void do_mips64r6_##suffix(const void *input1,            \
                                        const void *input2,            \
                                        void *output)                  \
{                                                                      \
   __asm__ volatile (                                                  \
      "ld $t1, 0(%0)\n\t"                                              \
      "ld $t2, 0(%1)\n\t"                                              \
      #mnemonic " $t0, $t1, $t2\n\t"                                   \
      "sd $t0, 0(%2)\n\t"                                              \
      :                                                                \
      : "r" (input1), "r" (input2), "r" (output)                       \
      : "t0", "t1", "memory"                                           \
   );                                                                  \
}

DO_MIPS64R6__RD__RS_RT(SLLV, sllv)
DO_MIPS64R6__RD__RS_RT(SRLV, srlv)
DO_MIPS64R6__RD__RS_RT(SRAV, srav)
DO_MIPS64R6__RD__RS_RT(DSLLV, dsllv)
DO_MIPS64R6__RD__RS_RT(DSRLV, dsrlv)
DO_MIPS64R6__RD__RS_RT(DSRAV, dsrav)

DO_MIPS64R6__RD__RS_RT(MUL, mul)
DO_MIPS64R6__RD__RS_RT(MUH, muh)
DO_MIPS64R6__RD__RS_RT(MULU, mulu)
DO_MIPS64R6__RD__RS_RT(MUHU, muhu)
DO_MIPS64R6__RD__RS_RT(DMUL, dmul)
DO_MIPS64R6__RD__RS_RT(DMUH, dmuh)
DO_MIPS64R6__RD__RS_RT(DMULU, dmulu)
DO_MIPS64R6__RD__RS_RT(DMUHU, dmuhu)


#define DO_MIPS64R6__RT__RS_RT(suffix, mnemonic)                       \
static inline void do_mips64r6_##suffix(const void *input1,            \
                                        const void *input2,            \
                                        void *output)                  \
{                                                                      \
    if (strncmp(#mnemonic, "crc32", 5) == 0)                           \
        __asm__ volatile (                                             \
           ".set crc\n\t"                                              \
        );                                                             \
                                                                       \
   __asm__ volatile (                                                  \
      "ld $t1, 0(%0)\n\t"                                              \
      "ld $t2, 0(%1)\n\t"                                              \
      #mnemonic " $t2, $t1, $t2\n\t"                                   \
      "sd $t2, 0(%2)\n\t"                                              \
      :                                                                \
      : "r" (input1), "r" (input2), "r" (output)                       \
      : "t0", "t1", "t2", "memory"                                     \
   );                                                                  \
}

DO_MIPS64R6__RT__RS_RT(CRC32B, crc32b)
DO_MIPS64R6__RT__RS_RT(CRC32H, crc32h)
DO_MIPS64R6__RT__RS_RT(CRC32W, crc32w)
DO_MIPS64R6__RT__RS_RT(CRC32D, crc32d)

DO_MIPS64R6__RT__RS_RT(CRC32CB, crc32cb)
DO_MIPS64R6__RT__RS_RT(CRC32CH, crc32ch)
DO_MIPS64R6__RT__RS_RT(CRC32CW, crc32cw)
DO_MIPS64R6__RT__RS_RT(CRC32CD, crc32cd)

#endif
