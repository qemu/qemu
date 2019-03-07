/*
 *  Header file for wrappers around MSA instructions assembler invocations
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

#define DO_MSA__WD__WD(suffix, mnemonic)                               \
static inline void do_msa_##suffix(void *input, void *output)          \
{                                                                      \
   __asm__ volatile (                                                  \
      "move $t0, %0\n\t"                                               \
      "ld.d $w11, 0($t0)\n\t"                                          \
      #mnemonic " $w10, $w10\n\t"                                      \
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

#define DO_MSA__WD__WD_WT(suffix, mnemonic)                            \
static inline void do_msa_##suffix(void *input1, void *input2,         \
                                   void *output)                       \
{                                                                      \
   __asm__ volatile (                                                  \
      "move $t0, %0\n\t"                                               \
      "ld.d $w11, 0($t0)\n\t"                                          \
      "move $t0, %1\n\t"                                               \
      "ld.d $w12, 0($t0)\n\t"                                          \
      #mnemonic " $w10, $w10, $w12\n\t"                                \
      "move $t0, %2\n\t"                                               \
      "st.d $w10, 0($t0)\n\t"                                          \
      :                                                                \
      : "r" (input1), "r" (input2), "r" (output)                       \
      : "t0", "memory"                                                 \
   );                                                                  \
}

#define DO_MSA__WD__WS_WD(suffix, mnemonic)                            \
static inline void do_msa_##suffix(void *input1, void *input2,         \
                                   void *output)                       \
{                                                                      \
   __asm__ volatile (                                                  \
      "move $t0, %0\n\t"                                               \
      "ld.d $w11, 0($t0)\n\t"                                          \
      "move $t0, %1\n\t"                                               \
      "ld.d $w12, 0($t0)\n\t"                                          \
      #mnemonic " $w10, $w11, $w10\n\t"                                \
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

DO_MSA__WD__WS_WT(MAX_A_B, max_a.b)
DO_MSA__WD__WS_WT(MAX_A_H, max_a.h)
DO_MSA__WD__WS_WT(MAX_A_W, max_a.w)
DO_MSA__WD__WS_WT(MAX_A_D, max_a.d)

DO_MSA__WD__WS_WT(MIN_A_B, min_a.b)
DO_MSA__WD__WS_WT(MIN_A_H, min_a.h)
DO_MSA__WD__WS_WT(MIN_A_W, min_a.w)
DO_MSA__WD__WS_WT(MIN_A_D, min_a.d)

DO_MSA__WD__WS_WT(MAX_S_B, max_s.b)
DO_MSA__WD__WS_WT(MAX_S_H, max_s.h)
DO_MSA__WD__WS_WT(MAX_S_W, max_s.w)
DO_MSA__WD__WS_WT(MAX_S_D, max_s.d)

DO_MSA__WD__WS_WT(MIN_S_B, min_s.b)
DO_MSA__WD__WS_WT(MIN_S_H, min_s.h)
DO_MSA__WD__WS_WT(MIN_S_W, min_s.w)
DO_MSA__WD__WS_WT(MIN_S_D, min_s.d)

DO_MSA__WD__WS_WT(MAX_U_B, max_u.b)
DO_MSA__WD__WS_WT(MAX_U_H, max_u.h)
DO_MSA__WD__WS_WT(MAX_U_W, max_u.w)
DO_MSA__WD__WS_WT(MAX_U_D, max_u.d)

DO_MSA__WD__WS_WT(MIN_U_B, min_u.b)
DO_MSA__WD__WS_WT(MIN_U_H, min_u.h)
DO_MSA__WD__WS_WT(MIN_U_W, min_u.w)
DO_MSA__WD__WS_WT(MIN_U_D, min_u.d)

DO_MSA__WD__WS_WT(BCLR_B, bclr.b)
DO_MSA__WD__WS_WT(BCLR_H, bclr.h)
DO_MSA__WD__WS_WT(BCLR_W, bclr.w)
DO_MSA__WD__WS_WT(BCLR_D, bclr.d)

DO_MSA__WD__WS_WT(BSET_B, bset.b)
DO_MSA__WD__WS_WT(BSET_H, bset.h)
DO_MSA__WD__WS_WT(BSET_W, bset.w)
DO_MSA__WD__WS_WT(BSET_D, bset.d)

DO_MSA__WD__WS_WT(BNEG_B, bneg.b)
DO_MSA__WD__WS_WT(BNEG_H, bneg.h)
DO_MSA__WD__WS_WT(BNEG_W, bneg.w)
DO_MSA__WD__WS_WT(BNEG_D, bneg.d)

DO_MSA__WD__WS_WT(PCKEV_B, pckev.b)
DO_MSA__WD__WS_WT(PCKEV_H, pckev.h)
DO_MSA__WD__WS_WT(PCKEV_W, pckev.w)
DO_MSA__WD__WS_WT(PCKEV_D, pckev.d)

DO_MSA__WD__WS_WT(PCKOD_B, pckod.b)
DO_MSA__WD__WS_WT(PCKOD_H, pckod.h)
DO_MSA__WD__WS_WT(PCKOD_W, pckod.w)
DO_MSA__WD__WS_WT(PCKOD_D, pckod.d)

DO_MSA__WD__WS_WT(VSHF_B, vshf.b)
DO_MSA__WD__WS_WT(VSHF_H, vshf.h)
DO_MSA__WD__WS_WT(VSHF_W, vshf.w)
DO_MSA__WD__WS_WT(VSHF_D, vshf.d)

DO_MSA__WD__WS_WT(SLL_B, sll.b)
DO_MSA__WD__WS_WT(SLL_H, sll.h)
DO_MSA__WD__WS_WT(SLL_W, sll.w)
DO_MSA__WD__WS_WT(SLL_D, sll.d)

DO_MSA__WD__WS_WT(SRA_B, sra.b)
DO_MSA__WD__WS_WT(SRA_H, sra.h)
DO_MSA__WD__WS_WT(SRA_W, sra.w)
DO_MSA__WD__WS_WT(SRA_D, sra.d)

DO_MSA__WD__WS_WT(SRAR_B, srar.b)
DO_MSA__WD__WS_WT(SRAR_H, srar.h)
DO_MSA__WD__WS_WT(SRAR_W, srar.w)
DO_MSA__WD__WS_WT(SRAR_D, srar.d)

DO_MSA__WD__WS_WT(SRL_B, srl.b)
DO_MSA__WD__WS_WT(SRL_H, srl.h)
DO_MSA__WD__WS_WT(SRL_W, srl.w)
DO_MSA__WD__WS_WT(SRL_D, srl.d)

DO_MSA__WD__WS_WT(SRLR_B, srlr.b)
DO_MSA__WD__WS_WT(SRLR_H, srlr.h)
DO_MSA__WD__WS_WT(SRLR_W, srlr.w)
DO_MSA__WD__WS_WT(SRLR_D, srlr.d)

DO_MSA__WD__WS_WT(BMNZ_V, bmnz.v)
DO_MSA__WD__WS_WT(BMZ_V, bmz.v)

DO_MSA__WD__WS_WT(FMAX_W, fmax.w)
DO_MSA__WD__WS_WT(FMAX_D, fmax.d)

DO_MSA__WD__WS_WT(FMAX_A_W, fmax_a.w)
DO_MSA__WD__WS_WT(FMAX_A_D, fmax_a.d)

DO_MSA__WD__WS_WT(FMIN_W, fmin.w)
DO_MSA__WD__WS_WT(FMIN_D, fmin.d)

DO_MSA__WD__WS_WT(FMIN_A_W, fmin_a.w)
DO_MSA__WD__WS_WT(FMIN_A_D, fmin_a.d)


#endif
