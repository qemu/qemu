/*
 *  Header file for wrappers around MSA instructions assembler invocations
 *
 *  Copyright (C) 2018  Wave Computing, Inc.
 *  Copyright (C) 2018  Aleksandar Markovic <amarkovic@wavecomp.com>
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

#ifndef WRAPPERS_MSA_H
#define WRAPPERS_MSA_H


#define DO_MSA__WD__WS(suffix, mnemonic)                               \
static inline void do_msa_##suffix(void *input, void *output)          \
{                                                                      \
   __asm__ volatile (                                                  \
      "move $t0, %0\n\t"                                               \
      "ld.d $w11, 0($t0)\n\t"                                          \
      #mnemonic " $w10, $w11\n\t"                                      \
      "move $t0, %1\n\t"                                               \
      "st.d $w10, 0($t0)\n\t"                                          \
      :                                                                \
      : "r" (input), "r" (output)                                      \
      : "t0", "memory"                                                 \
   );                                                                  \
}

DO_MSA__WD__WS(NLOC_B, nloc.b)
DO_MSA__WD__WS(NLOC_H, nloc.h)
DO_MSA__WD__WS(NLOC_W, nloc.w)
DO_MSA__WD__WS(NLOC_D, nloc.d)

DO_MSA__WD__WS(NLZC_B, nlzc.b)
DO_MSA__WD__WS(NLZC_H, nlzc.h)
DO_MSA__WD__WS(NLZC_W, nlzc.w)
DO_MSA__WD__WS(NLZC_D, nlzc.d)

DO_MSA__WD__WS(PCNT_B, pcnt.b)
DO_MSA__WD__WS(PCNT_H, pcnt.h)
DO_MSA__WD__WS(PCNT_W, pcnt.w)
DO_MSA__WD__WS(PCNT_D, pcnt.d)


#define DO_MSA__WD__WS_WT(suffix, mnemonic)                            \
static inline void do_msa_##suffix(void *input1, void *input2,         \
                                   void *output)                       \
{                                                                      \
   __asm__ volatile (                                                  \
      "move $t0, %0\n\t"                                               \
      "ld.d $w11, 0($t0)\n\t"                                          \
      "move $t0, %1\n\t"                                               \
      "ld.d $w12, 0($t0)\n\t"                                          \
      #mnemonic " $w10, $w11, $w12\n\t"                                \
      "move $t0, %2\n\t"                                               \
      "st.d $w10, 0($t0)\n\t"                                          \
      :                                                                \
      : "r" (input1), "r" (input2), "r" (output)                       \
      : "t0", "memory"                                                 \
   );                                                                  \
}

DO_MSA__WD__WS_WT(ILVEV_B, ilvev.b)
DO_MSA__WD__WS_WT(ILVEV_H, ilvev.h)
DO_MSA__WD__WS_WT(ILVEV_W, ilvev.w)
DO_MSA__WD__WS_WT(ILVEV_D, ilvev.d)

DO_MSA__WD__WS_WT(ILVOD_B, ilvod.b)
DO_MSA__WD__WS_WT(ILVOD_H, ilvod.h)
DO_MSA__WD__WS_WT(ILVOD_W, ilvod.w)
DO_MSA__WD__WS_WT(ILVOD_D, ilvod.d)

DO_MSA__WD__WS_WT(ILVL_B, ilvl.b)
DO_MSA__WD__WS_WT(ILVL_H, ilvl.h)
DO_MSA__WD__WS_WT(ILVL_W, ilvl.w)
DO_MSA__WD__WS_WT(ILVL_D, ilvl.d)

DO_MSA__WD__WS_WT(ILVR_B, ilvr.b)
DO_MSA__WD__WS_WT(ILVR_H, ilvr.h)
DO_MSA__WD__WS_WT(ILVR_W, ilvr.w)
DO_MSA__WD__WS_WT(ILVR_D, ilvr.d)

DO_MSA__WD__WS_WT(AND_V, and.v)
DO_MSA__WD__WS_WT(NOR_V, nor.v)
DO_MSA__WD__WS_WT(OR_V, or.v)
DO_MSA__WD__WS_WT(XOR_V, xor.v)

DO_MSA__WD__WS_WT(CEQ_B, ceq.b)
DO_MSA__WD__WS_WT(CEQ_H, ceq.h)
DO_MSA__WD__WS_WT(CEQ_W, ceq.w)
DO_MSA__WD__WS_WT(CEQ_D, ceq.d)

DO_MSA__WD__WS_WT(CLE_S_B, cle_s.b)
DO_MSA__WD__WS_WT(CLE_S_H, cle_s.h)
DO_MSA__WD__WS_WT(CLE_S_W, cle_s.w)
DO_MSA__WD__WS_WT(CLE_S_D, cle_s.d)

DO_MSA__WD__WS_WT(CLE_U_B, cle_u.b)
DO_MSA__WD__WS_WT(CLE_U_H, cle_u.h)
DO_MSA__WD__WS_WT(CLE_U_W, cle_u.w)
DO_MSA__WD__WS_WT(CLE_U_D, cle_u.d)

DO_MSA__WD__WS_WT(CLT_S_B, clt_s.b)
DO_MSA__WD__WS_WT(CLT_S_H, clt_s.h)
DO_MSA__WD__WS_WT(CLT_S_W, clt_s.w)
DO_MSA__WD__WS_WT(CLT_S_D, clt_s.d)

DO_MSA__WD__WS_WT(CLT_U_B, clt_u.b)
DO_MSA__WD__WS_WT(CLT_U_H, clt_u.h)
DO_MSA__WD__WS_WT(CLT_U_W, clt_u.w)
DO_MSA__WD__WS_WT(CLT_U_D, clt_u.d)


#endif
