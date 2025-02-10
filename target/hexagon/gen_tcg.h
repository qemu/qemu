/*
 *  Copyright(c) 2019-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

#ifndef HEXAGON_GEN_TCG_H
#define HEXAGON_GEN_TCG_H

/*
 * Here is a primer to understand the tag names for load/store instructions
 *
 * Data types
 *      b        signed byte                       r0 = memb(r2+#0)
 *     ub        unsigned byte                     r0 = memub(r2+#0)
 *      h        signed half word (16 bits)        r0 = memh(r2+#0)
 *     uh        unsigned half word                r0 = memuh(r2+#0)
 *      i        integer (32 bits)                 r0 = memw(r2+#0)
 *      d        double word (64 bits)             r1:0 = memd(r2+#0)
 *
 * Addressing modes
 *     _io       indirect with offset              r0 = memw(r1+#4)
 *     _ur       absolute with register offset     r0 = memw(r1<<#4+##variable)
 *     _rr       indirect with register offset     r0 = memw(r1+r4<<#2)
 *     gp        global pointer relative           r0 = memw(gp+#200)
 *     _sp       stack pointer relative            r0 = memw(r29+#12)
 *     _ap       absolute set                      r0 = memw(r1=##variable)
 *     _pr       post increment register           r0 = memw(r1++m1)
 *     _pbr      post increment bit reverse        r0 = memw(r1++m1:brev)
 *     _pi       post increment immediate          r0 = memb(r1++#1)
 *     _pci      post increment circular immediate r0 = memw(r1++#4:circ(m0))
 *     _pcr      post increment circular register  r0 = memw(r1++I:circ(m0))
 */

/* Macros for complex addressing modes */
#define GET_EA_ap \
    do { \
        fEA_IMM(UiV); \
        tcg_gen_movi_tl(ReV, UiV); \
    } while (0)
#define GET_EA_pr \
    do { \
        fEA_REG(RxV); \
        fPM_M(RxV, MuV); \
    } while (0)
#define GET_EA_pbr \
    do { \
        gen_helper_fbrev(EA, RxV); \
        tcg_gen_add_tl(RxV, RxV, MuV); \
    } while (0)
#define GET_EA_pi \
    do { \
        fEA_REG(RxV); \
        fPM_I(RxV, siV); \
    } while (0)
#define GET_EA_pci \
    do { \
        TCGv tcgv_siV = tcg_constant_tl(siV); \
        tcg_gen_mov_tl(EA, RxV); \
        gen_helper_fcircadd(RxV, RxV, tcgv_siV, MuV, CS); \
    } while (0)
#define GET_EA_pcr(SHIFT) \
    do { \
        TCGv ireg = tcg_temp_new(); \
        tcg_gen_mov_tl(EA, RxV); \
        gen_read_ireg(ireg, MuV, (SHIFT)); \
        gen_helper_fcircadd(RxV, RxV, ireg, MuV, CS); \
    } while (0)

/* Instructions with multiple definitions */
#define fGEN_TCG_LOAD_AP(RES, SIZE, SIGN) \
    do { \
        fMUST_IMMEXT(UiV); \
        fEA_IMM(UiV); \
        fLOAD(1, SIZE, SIGN, EA, RES); \
        tcg_gen_movi_tl(ReV, UiV); \
    } while (0)

#define fGEN_TCG_L4_loadrub_ap(SHORTCODE) \
    fGEN_TCG_LOAD_AP(RdV, 1, u)
#define fGEN_TCG_L4_loadrb_ap(SHORTCODE) \
    fGEN_TCG_LOAD_AP(RdV, 1, s)
#define fGEN_TCG_L4_loadruh_ap(SHORTCODE) \
    fGEN_TCG_LOAD_AP(RdV, 2, u)
#define fGEN_TCG_L4_loadrh_ap(SHORTCODE) \
    fGEN_TCG_LOAD_AP(RdV, 2, s)
#define fGEN_TCG_L4_loadri_ap(SHORTCODE) \
    fGEN_TCG_LOAD_AP(RdV, 4, u)
#define fGEN_TCG_L4_loadrd_ap(SHORTCODE) \
    fGEN_TCG_LOAD_AP(RddV, 8, u)

#define fGEN_TCG_L2_loadrub_pci(SHORTCODE)    SHORTCODE
#define fGEN_TCG_L2_loadrb_pci(SHORTCODE)     SHORTCODE
#define fGEN_TCG_L2_loadruh_pci(SHORTCODE)    SHORTCODE
#define fGEN_TCG_L2_loadrh_pci(SHORTCODE)     SHORTCODE
#define fGEN_TCG_L2_loadri_pci(SHORTCODE)     SHORTCODE
#define fGEN_TCG_L2_loadrd_pci(SHORTCODE)     SHORTCODE

#define fGEN_TCG_LOAD_pcr(SHIFT, LOAD) \
    do { \
        TCGv ireg = tcg_temp_new(); \
        tcg_gen_mov_tl(EA, RxV); \
        gen_read_ireg(ireg, MuV, SHIFT); \
        gen_helper_fcircadd(RxV, RxV, ireg, MuV, CS); \
        LOAD; \
    } while (0)

#define fGEN_TCG_L2_loadrub_pcr(SHORTCODE) \
      fGEN_TCG_LOAD_pcr(0, fLOAD(1, 1, u, EA, RdV))
#define fGEN_TCG_L2_loadrb_pcr(SHORTCODE) \
      fGEN_TCG_LOAD_pcr(0, fLOAD(1, 1, s, EA, RdV))
#define fGEN_TCG_L2_loadruh_pcr(SHORTCODE) \
      fGEN_TCG_LOAD_pcr(1, fLOAD(1, 2, u, EA, RdV))
#define fGEN_TCG_L2_loadrh_pcr(SHORTCODE) \
      fGEN_TCG_LOAD_pcr(1, fLOAD(1, 2, s, EA, RdV))
#define fGEN_TCG_L2_loadri_pcr(SHORTCODE) \
      fGEN_TCG_LOAD_pcr(2, fLOAD(1, 4, u, EA, RdV))
#define fGEN_TCG_L2_loadrd_pcr(SHORTCODE) \
      fGEN_TCG_LOAD_pcr(3, fLOAD(1, 8, u, EA, RddV))

#define fGEN_TCG_L2_loadrub_pr(SHORTCODE)      SHORTCODE
#define fGEN_TCG_L2_loadrub_pbr(SHORTCODE)     SHORTCODE
#define fGEN_TCG_L2_loadrub_pi(SHORTCODE)      SHORTCODE
#define fGEN_TCG_L2_loadrb_pr(SHORTCODE)       SHORTCODE
#define fGEN_TCG_L2_loadrb_pbr(SHORTCODE)      SHORTCODE
#define fGEN_TCG_L2_loadrb_pi(SHORTCODE)       SHORTCODE
#define fGEN_TCG_L2_loadruh_pr(SHORTCODE)      SHORTCODE
#define fGEN_TCG_L2_loadruh_pbr(SHORTCODE)     SHORTCODE
#define fGEN_TCG_L2_loadruh_pi(SHORTCODE)      SHORTCODE
#define fGEN_TCG_L2_loadrh_pr(SHORTCODE)       SHORTCODE
#define fGEN_TCG_L2_loadrh_pbr(SHORTCODE)      SHORTCODE
#define fGEN_TCG_L2_loadrh_pi(SHORTCODE)       SHORTCODE
#define fGEN_TCG_L2_loadri_pr(SHORTCODE)       SHORTCODE
#define fGEN_TCG_L2_loadri_pbr(SHORTCODE)      SHORTCODE
#define fGEN_TCG_L2_loadri_pi(SHORTCODE)       SHORTCODE
#define fGEN_TCG_L2_loadrd_pr(SHORTCODE)       SHORTCODE
#define fGEN_TCG_L2_loadrd_pbr(SHORTCODE)      SHORTCODE
#define fGEN_TCG_L2_loadrd_pi(SHORTCODE)       SHORTCODE

/*
 * These instructions load 2 bytes and places them in
 * two halves of the destination register.
 * The GET_EA macro determines the addressing mode.
 * The SIGN argument determines whether to zero-extend or
 * sign-extend.
 */
#define fGEN_TCG_loadbXw2(GET_EA, SIGN) \
    do { \
        TCGv tmp = tcg_temp_new(); \
        TCGv byte = tcg_temp_new(); \
        GET_EA; \
        fLOAD(1, 2, u, EA, tmp); \
        tcg_gen_movi_tl(RdV, 0); \
        for (int i = 0; i < 2; i++) { \
            gen_set_half(i, RdV, gen_get_byte(byte, i, tmp, (SIGN))); \
        } \
    } while (0)

#define fGEN_TCG_L2_loadbzw2_io(SHORTCODE) \
    fGEN_TCG_loadbXw2(fEA_RI(RsV, siV), false)
#define fGEN_TCG_L4_loadbzw2_ur(SHORTCODE) \
    fGEN_TCG_loadbXw2(fEA_IRs(UiV, RtV, uiV), false)
#define fGEN_TCG_L2_loadbsw2_io(SHORTCODE) \
    fGEN_TCG_loadbXw2(fEA_RI(RsV, siV), true)
#define fGEN_TCG_L4_loadbsw2_ur(SHORTCODE) \
    fGEN_TCG_loadbXw2(fEA_IRs(UiV, RtV, uiV), true)
#define fGEN_TCG_L4_loadbzw2_ap(SHORTCODE) \
    fGEN_TCG_loadbXw2(GET_EA_ap, false)
#define fGEN_TCG_L2_loadbzw2_pr(SHORTCODE) \
    fGEN_TCG_loadbXw2(GET_EA_pr, false)
#define fGEN_TCG_L2_loadbzw2_pbr(SHORTCODE) \
    fGEN_TCG_loadbXw2(GET_EA_pbr, false)
#define fGEN_TCG_L2_loadbzw2_pi(SHORTCODE) \
    fGEN_TCG_loadbXw2(GET_EA_pi, false)
#define fGEN_TCG_L4_loadbsw2_ap(SHORTCODE) \
    fGEN_TCG_loadbXw2(GET_EA_ap, true)
#define fGEN_TCG_L2_loadbsw2_pr(SHORTCODE) \
    fGEN_TCG_loadbXw2(GET_EA_pr, true)
#define fGEN_TCG_L2_loadbsw2_pbr(SHORTCODE) \
    fGEN_TCG_loadbXw2(GET_EA_pbr, true)
#define fGEN_TCG_L2_loadbsw2_pi(SHORTCODE) \
    fGEN_TCG_loadbXw2(GET_EA_pi, true)
#define fGEN_TCG_L2_loadbzw2_pci(SHORTCODE) \
    fGEN_TCG_loadbXw2(GET_EA_pci, false)
#define fGEN_TCG_L2_loadbsw2_pci(SHORTCODE) \
    fGEN_TCG_loadbXw2(GET_EA_pci, true)
#define fGEN_TCG_L2_loadbzw2_pcr(SHORTCODE) \
    fGEN_TCG_loadbXw2(GET_EA_pcr(1), false)
#define fGEN_TCG_L2_loadbsw2_pcr(SHORTCODE) \
    fGEN_TCG_loadbXw2(GET_EA_pcr(1), true)

/*
 * These instructions load 4 bytes and places them in
 * four halves of the destination register pair.
 * The GET_EA macro determines the addressing mode.
 * The SIGN argument determines whether to zero-extend or
 * sign-extend.
 */
#define fGEN_TCG_loadbXw4(GET_EA, SIGN) \
    do { \
        TCGv tmp = tcg_temp_new(); \
        TCGv byte = tcg_temp_new(); \
        GET_EA; \
        fLOAD(1, 4, u, EA, tmp);  \
        tcg_gen_movi_i64(RddV, 0); \
        for (int i = 0; i < 4; i++) { \
            gen_set_half_i64(i, RddV, gen_get_byte(byte, i, tmp, (SIGN)));  \
        }  \
    } while (0)

#define fGEN_TCG_L2_loadbzw4_io(SHORTCODE) \
    fGEN_TCG_loadbXw4(fEA_RI(RsV, siV), false)
#define fGEN_TCG_L4_loadbzw4_ur(SHORTCODE) \
    fGEN_TCG_loadbXw4(fEA_IRs(UiV, RtV, uiV), false)
#define fGEN_TCG_L2_loadbsw4_io(SHORTCODE) \
    fGEN_TCG_loadbXw4(fEA_RI(RsV, siV), true)
#define fGEN_TCG_L4_loadbsw4_ur(SHORTCODE) \
    fGEN_TCG_loadbXw4(fEA_IRs(UiV, RtV, uiV), true)
#define fGEN_TCG_L2_loadbzw4_pci(SHORTCODE) \
    fGEN_TCG_loadbXw4(GET_EA_pci, false)
#define fGEN_TCG_L2_loadbsw4_pci(SHORTCODE) \
    fGEN_TCG_loadbXw4(GET_EA_pci, true)
#define fGEN_TCG_L2_loadbzw4_pcr(SHORTCODE) \
    fGEN_TCG_loadbXw4(GET_EA_pcr(2), false)
#define fGEN_TCG_L2_loadbsw4_pcr(SHORTCODE) \
    fGEN_TCG_loadbXw4(GET_EA_pcr(2), true)
#define fGEN_TCG_L4_loadbzw4_ap(SHORTCODE) \
    fGEN_TCG_loadbXw4(GET_EA_ap, false)
#define fGEN_TCG_L2_loadbzw4_pr(SHORTCODE) \
    fGEN_TCG_loadbXw4(GET_EA_pr, false)
#define fGEN_TCG_L2_loadbzw4_pbr(SHORTCODE) \
    fGEN_TCG_loadbXw4(GET_EA_pbr, false)
#define fGEN_TCG_L2_loadbzw4_pi(SHORTCODE) \
    fGEN_TCG_loadbXw4(GET_EA_pi, false)
#define fGEN_TCG_L4_loadbsw4_ap(SHORTCODE) \
    fGEN_TCG_loadbXw4(GET_EA_ap, true)
#define fGEN_TCG_L2_loadbsw4_pr(SHORTCODE) \
    fGEN_TCG_loadbXw4(GET_EA_pr, true)
#define fGEN_TCG_L2_loadbsw4_pbr(SHORTCODE) \
    fGEN_TCG_loadbXw4(GET_EA_pbr, true)
#define fGEN_TCG_L2_loadbsw4_pi(SHORTCODE) \
    fGEN_TCG_loadbXw4(GET_EA_pi, true)

/*
 * These instructions load a half word, shift the destination right by 16 bits
 * and place the loaded value in the high half word of the destination pair.
 * The GET_EA macro determines the addressing mode.
 */
#define fGEN_TCG_loadalignh(GET_EA) \
    do { \
        TCGv tmp = tcg_temp_new(); \
        TCGv_i64 tmp_i64 = tcg_temp_new_i64(); \
        GET_EA;  \
        fLOAD(1, 2, u, EA, tmp);  \
        tcg_gen_extu_i32_i64(tmp_i64, tmp); \
        tcg_gen_shri_i64(RyyV, RyyV, 16); \
        tcg_gen_deposit_i64(RyyV, RyyV, tmp_i64, 48, 16); \
    } while (0)

#define fGEN_TCG_L4_loadalignh_ur(SHORTCODE) \
    fGEN_TCG_loadalignh(fEA_IRs(UiV, RtV, uiV))
#define fGEN_TCG_L2_loadalignh_io(SHORTCODE) \
    fGEN_TCG_loadalignh(fEA_RI(RsV, siV))
#define fGEN_TCG_L2_loadalignh_pci(SHORTCODE) \
    fGEN_TCG_loadalignh(GET_EA_pci)
#define fGEN_TCG_L2_loadalignh_pcr(SHORTCODE) \
    fGEN_TCG_loadalignh(GET_EA_pcr(1))
#define fGEN_TCG_L4_loadalignh_ap(SHORTCODE) \
    fGEN_TCG_loadalignh(GET_EA_ap)
#define fGEN_TCG_L2_loadalignh_pr(SHORTCODE) \
    fGEN_TCG_loadalignh(GET_EA_pr)
#define fGEN_TCG_L2_loadalignh_pbr(SHORTCODE) \
    fGEN_TCG_loadalignh(GET_EA_pbr)
#define fGEN_TCG_L2_loadalignh_pi(SHORTCODE) \
    fGEN_TCG_loadalignh(GET_EA_pi)

/* Same as above, but loads a byte instead of half word */
#define fGEN_TCG_loadalignb(GET_EA) \
    do { \
        TCGv tmp = tcg_temp_new(); \
        TCGv_i64 tmp_i64 = tcg_temp_new_i64(); \
        GET_EA;  \
        fLOAD(1, 1, u, EA, tmp);  \
        tcg_gen_extu_i32_i64(tmp_i64, tmp); \
        tcg_gen_shri_i64(RyyV, RyyV, 8); \
        tcg_gen_deposit_i64(RyyV, RyyV, tmp_i64, 56, 8); \
    } while (0)

#define fGEN_TCG_L2_loadalignb_io(SHORTCODE) \
    fGEN_TCG_loadalignb(fEA_RI(RsV, siV))
#define fGEN_TCG_L4_loadalignb_ur(SHORTCODE) \
    fGEN_TCG_loadalignb(fEA_IRs(UiV, RtV, uiV))
#define fGEN_TCG_L2_loadalignb_pci(SHORTCODE) \
    fGEN_TCG_loadalignb(GET_EA_pci)
#define fGEN_TCG_L2_loadalignb_pcr(SHORTCODE) \
    fGEN_TCG_loadalignb(GET_EA_pcr(0))
#define fGEN_TCG_L4_loadalignb_ap(SHORTCODE) \
    fGEN_TCG_loadalignb(GET_EA_ap)
#define fGEN_TCG_L2_loadalignb_pr(SHORTCODE) \
    fGEN_TCG_loadalignb(GET_EA_pr)
#define fGEN_TCG_L2_loadalignb_pbr(SHORTCODE) \
    fGEN_TCG_loadalignb(GET_EA_pbr)
#define fGEN_TCG_L2_loadalignb_pi(SHORTCODE) \
    fGEN_TCG_loadalignb(GET_EA_pi)

/*
 * Predicated loads
 * Here is a primer to understand the tag names
 *
 * Predicate used
 *      t        true "old" value                  if (p0) r0 = memb(r2+#0)
 *      f        false "old" value                 if (!p0) r0 = memb(r2+#0)
 *      tnew     true "new" value                  if (p0.new) r0 = memb(r2+#0)
 *      fnew     false "new" value                 if (!p0.new) r0 = memb(r2+#0)
 */
#define fGEN_TCG_PRED_LOAD(GET_EA, PRED, SIZE, SIGN) \
    do { \
        TCGv LSB = tcg_temp_new(); \
        TCGLabel *label = gen_new_label(); \
        tcg_gen_movi_tl(EA, 0); \
        PRED;  \
        CHECK_NOSHUF_PRED(GET_EA, SIZE, LSB); \
        tcg_gen_brcondi_tl(TCG_COND_EQ, LSB, 0, label); \
        fLOAD(1, SIZE, SIGN, EA, RdV); \
        gen_set_label(label); \
    } while (0)

#define fGEN_TCG_L2_ploadrubt_pi(SHORTCODE) \
    fGEN_TCG_PRED_LOAD(GET_EA_pi, fLSBOLD(PtV), 1, u)
#define fGEN_TCG_L2_ploadrubf_pi(SHORTCODE) \
    fGEN_TCG_PRED_LOAD(GET_EA_pi, fLSBOLDNOT(PtV), 1, u)
#define fGEN_TCG_L2_ploadrubtnew_pi(SHORTCODE) \
    fGEN_TCG_PRED_LOAD(GET_EA_pi, fLSBNEW(PtN), 1, u)
#define fGEN_TCG_L2_ploadrubfnew_pi(SHORTCODE) \
    fGEN_TCG_PRED_LOAD(GET_EA_pi, fLSBNEWNOT(PtN), 1, u)
#define fGEN_TCG_L2_ploadrbt_pi(SHORTCODE) \
    fGEN_TCG_PRED_LOAD(GET_EA_pi, fLSBOLD(PtV), 1, s)
#define fGEN_TCG_L2_ploadrbf_pi(SHORTCODE) \
    fGEN_TCG_PRED_LOAD(GET_EA_pi, fLSBOLDNOT(PtV), 1, s)
#define fGEN_TCG_L2_ploadrbtnew_pi(SHORTCODE) \
    fGEN_TCG_PRED_LOAD(GET_EA_pi, fLSBNEW(PtN), 1, s)
#define fGEN_TCG_L2_ploadrbfnew_pi(SHORTCODE) \
    fGEN_TCG_PRED_LOAD({ fEA_REG(RxV); fPM_I(RxV, siV); }, \
                       fLSBNEWNOT(PtN), 1, s)

#define fGEN_TCG_L2_ploadruht_pi(SHORTCODE) \
    fGEN_TCG_PRED_LOAD(GET_EA_pi, fLSBOLD(PtV), 2, u)
#define fGEN_TCG_L2_ploadruhf_pi(SHORTCODE) \
    fGEN_TCG_PRED_LOAD(GET_EA_pi, fLSBOLDNOT(PtV), 2, u)
#define fGEN_TCG_L2_ploadruhtnew_pi(SHORTCODE) \
    fGEN_TCG_PRED_LOAD(GET_EA_pi, fLSBNEW(PtN), 2, u)
#define fGEN_TCG_L2_ploadruhfnew_pi(SHORTCODE) \
    fGEN_TCG_PRED_LOAD(GET_EA_pi, fLSBNEWNOT(PtN), 2, u)
#define fGEN_TCG_L2_ploadrht_pi(SHORTCODE) \
    fGEN_TCG_PRED_LOAD(GET_EA_pi, fLSBOLD(PtV), 2, s)
#define fGEN_TCG_L2_ploadrhf_pi(SHORTCODE) \
    fGEN_TCG_PRED_LOAD(GET_EA_pi, fLSBOLDNOT(PtV), 2, s)
#define fGEN_TCG_L2_ploadrhtnew_pi(SHORTCODE) \
    fGEN_TCG_PRED_LOAD(GET_EA_pi, fLSBNEW(PtN), 2, s)
#define fGEN_TCG_L2_ploadrhfnew_pi(SHORTCODE) \
    fGEN_TCG_PRED_LOAD(GET_EA_pi, fLSBNEWNOT(PtN), 2, s)

#define fGEN_TCG_L2_ploadrit_pi(SHORTCODE) \
    fGEN_TCG_PRED_LOAD(GET_EA_pi, fLSBOLD(PtV), 4, u)
#define fGEN_TCG_L2_ploadrif_pi(SHORTCODE) \
    fGEN_TCG_PRED_LOAD(GET_EA_pi, fLSBOLDNOT(PtV), 4, u)
#define fGEN_TCG_L2_ploadritnew_pi(SHORTCODE) \
    fGEN_TCG_PRED_LOAD(GET_EA_pi, fLSBNEW(PtN), 4, u)
#define fGEN_TCG_L2_ploadrifnew_pi(SHORTCODE) \
    fGEN_TCG_PRED_LOAD(GET_EA_pi, fLSBNEWNOT(PtN), 4, u)

/* Predicated loads into a register pair */
#define fGEN_TCG_PRED_LOAD_PAIR(GET_EA, PRED) \
    do { \
        TCGv LSB = tcg_temp_new(); \
        TCGLabel *label = gen_new_label(); \
        tcg_gen_movi_tl(EA, 0); \
        PRED;  \
        CHECK_NOSHUF_PRED(GET_EA, 8, LSB); \
        tcg_gen_brcondi_tl(TCG_COND_EQ, LSB, 0, label); \
        fLOAD(1, 8, u, EA, RddV); \
        gen_set_label(label); \
    } while (0)

#define fGEN_TCG_L2_ploadrdt_pi(SHORTCODE) \
    fGEN_TCG_PRED_LOAD_PAIR(GET_EA_pi, fLSBOLD(PtV))
#define fGEN_TCG_L2_ploadrdf_pi(SHORTCODE) \
    fGEN_TCG_PRED_LOAD_PAIR(GET_EA_pi, fLSBOLDNOT(PtV))
#define fGEN_TCG_L2_ploadrdtnew_pi(SHORTCODE) \
    fGEN_TCG_PRED_LOAD_PAIR(GET_EA_pi, fLSBNEW(PtN))
#define fGEN_TCG_L2_ploadrdfnew_pi(SHORTCODE) \
    fGEN_TCG_PRED_LOAD_PAIR(GET_EA_pi, fLSBNEWNOT(PtN))

/* load-locked and store-locked */
#define fGEN_TCG_L2_loadw_locked(SHORTCODE) \
    SHORTCODE
#define fGEN_TCG_L4_loadd_locked(SHORTCODE) \
    SHORTCODE
#define fGEN_TCG_S2_storew_locked(SHORTCODE) \
    SHORTCODE
#define fGEN_TCG_S4_stored_locked(SHORTCODE) \
    SHORTCODE

#define fGEN_TCG_STORE(SHORTCODE) \
    do { \
        TCGv HALF G_GNUC_UNUSED = tcg_temp_new(); \
        TCGv BYTE G_GNUC_UNUSED = tcg_temp_new(); \
        SHORTCODE; \
    } while (0)

#define fGEN_TCG_STORE_pcr(SHIFT, STORE) \
    do { \
        TCGv ireg = tcg_temp_new(); \
        TCGv HALF G_GNUC_UNUSED = tcg_temp_new(); \
        TCGv BYTE G_GNUC_UNUSED = tcg_temp_new(); \
        tcg_gen_mov_tl(EA, RxV); \
        gen_read_ireg(ireg, MuV, SHIFT); \
        gen_helper_fcircadd(RxV, RxV, ireg, MuV, CS); \
        STORE; \
    } while (0)

#define fGEN_TCG_S2_storerb_pbr(SHORTCODE) \
    fGEN_TCG_STORE(SHORTCODE)
#define fGEN_TCG_S2_storerb_pci(SHORTCODE) \
    fGEN_TCG_STORE(SHORTCODE)
#define fGEN_TCG_S2_storerb_pcr(SHORTCODE) \
    fGEN_TCG_STORE_pcr(0, fSTORE(1, 1, EA, fGETBYTE(0, RtV)))

#define fGEN_TCG_S2_storerh_pbr(SHORTCODE) \
    fGEN_TCG_STORE(SHORTCODE)
#define fGEN_TCG_S2_storerh_pci(SHORTCODE) \
    fGEN_TCG_STORE(SHORTCODE)
#define fGEN_TCG_S2_storerh_pcr(SHORTCODE) \
    fGEN_TCG_STORE_pcr(1, fSTORE(1, 2, EA, fGETHALF(0, RtV)))

#define fGEN_TCG_S2_storerf_pbr(SHORTCODE) \
    fGEN_TCG_STORE(SHORTCODE)
#define fGEN_TCG_S2_storerf_pci(SHORTCODE) \
    fGEN_TCG_STORE(SHORTCODE)
#define fGEN_TCG_S2_storerf_pcr(SHORTCODE) \
    fGEN_TCG_STORE_pcr(1, fSTORE(1, 2, EA, fGETHALF(1, RtV)))

#define fGEN_TCG_S2_storeri_pbr(SHORTCODE) \
    fGEN_TCG_STORE(SHORTCODE)
#define fGEN_TCG_S2_storeri_pci(SHORTCODE) \
    fGEN_TCG_STORE(SHORTCODE)
#define fGEN_TCG_S2_storeri_pcr(SHORTCODE) \
    fGEN_TCG_STORE_pcr(2, fSTORE(1, 4, EA, RtV))

#define fGEN_TCG_S2_storerd_pbr(SHORTCODE) \
    fGEN_TCG_STORE(SHORTCODE)
#define fGEN_TCG_S2_storerd_pci(SHORTCODE) \
    fGEN_TCG_STORE(SHORTCODE)
#define fGEN_TCG_S2_storerd_pcr(SHORTCODE) \
    fGEN_TCG_STORE_pcr(3, fSTORE(1, 8, EA, RttV))

#define fGEN_TCG_S2_storerbnew_pbr(SHORTCODE) \
    fGEN_TCG_STORE(SHORTCODE)
#define fGEN_TCG_S2_storerbnew_pci(SHORTCODE) \
    fGEN_TCG_STORE(SHORTCODE)
#define fGEN_TCG_S2_storerbnew_pcr(SHORTCODE) \
    fGEN_TCG_STORE_pcr(0, fSTORE(1, 1, EA, fGETBYTE(0, NtN)))

#define fGEN_TCG_S2_storerhnew_pbr(SHORTCODE) \
    fGEN_TCG_STORE(SHORTCODE)
#define fGEN_TCG_S2_storerhnew_pci(SHORTCODE) \
    fGEN_TCG_STORE(SHORTCODE)
#define fGEN_TCG_S2_storerhnew_pcr(SHORTCODE) \
    fGEN_TCG_STORE_pcr(1, fSTORE(1, 2, EA, fGETHALF(0, NtN)))

#define fGEN_TCG_S2_storerinew_pbr(SHORTCODE) \
    fGEN_TCG_STORE(SHORTCODE)
#define fGEN_TCG_S2_storerinew_pci(SHORTCODE) \
    fGEN_TCG_STORE(SHORTCODE)
#define fGEN_TCG_S2_storerinew_pcr(SHORTCODE) \
    fGEN_TCG_STORE_pcr(2, fSTORE(1, 4, EA, NtN))

/* dczeroa clears the 32 byte cache line at the address given */
#define fGEN_TCG_Y2_dczeroa(SHORTCODE) SHORTCODE

/* In linux-user mode, these are not modelled, suppress compiler warning */
#define fGEN_TCG_Y2_dcinva(SHORTCODE) \
    do { RsV = RsV; } while (0)
#define fGEN_TCG_Y2_dccleaninva(SHORTCODE) \
    do { RsV = RsV; } while (0)
#define fGEN_TCG_Y2_dccleana(SHORTCODE) \
    do { RsV = RsV; } while (0)
#define fGEN_TCG_Y2_icinva(SHORTCODE) \
    do { RsV = RsV; } while (0)

/*
 * allocframe(#uiV)
 *     RxV == r29
 */
#define fGEN_TCG_S2_allocframe(SHORTCODE) \
    gen_allocframe(ctx, RxV, uiV)

/* sub-instruction version (no RxV, so handle it manually) */
#define fGEN_TCG_SS2_allocframe(SHORTCODE) \
    do { \
        TCGv r29 = tcg_temp_new(); \
        tcg_gen_mov_tl(r29, hex_gpr[HEX_REG_SP]); \
        gen_allocframe(ctx, r29, uiV); \
        gen_log_reg_write(ctx, HEX_REG_SP, r29); \
    } while (0)

/*
 * Rdd32 = deallocframe(Rs32):raw
 *     RddV == r31:30
 *     RsV  == r30
 */
#define fGEN_TCG_L2_deallocframe(SHORTCODE) \
    gen_deallocframe(ctx, RddV, RsV)

/* sub-instruction version (no RddV/RsV, so handle it manually) */
#define fGEN_TCG_SL2_deallocframe(SHORTCODE) \
    do { \
        TCGv_i64 r31_30 = tcg_temp_new_i64(); \
        gen_deallocframe(ctx, r31_30, hex_gpr[HEX_REG_FP]); \
        gen_log_reg_write_pair(ctx, HEX_REG_FP, r31_30); \
    } while (0)

/*
 * dealloc_return
 * Assembler mapped to
 * r31:30 = dealloc_return(r30):raw
 */
#define fGEN_TCG_L4_return(SHORTCODE) \
    gen_return(ctx, RddV, RsV)

/*
 * sub-instruction version (no RddV, so handle it manually)
 */
#define fGEN_TCG_SL2_return(SHORTCODE) \
    do { \
        TCGv_i64 RddV = get_result_gpr_pair(ctx, HEX_REG_FP); \
        gen_return(ctx, RddV, hex_gpr[HEX_REG_FP]); \
        gen_log_reg_write_pair(ctx, HEX_REG_FP, RddV); \
    } while (0)

/*
 * Conditional returns follow this naming convention
 *     _t                 predicate true
 *     _f                 predicate false
 *     _tnew_pt           predicate.new true predict taken
 *     _fnew_pt           predicate.new false predict taken
 *     _tnew_pnt          predicate.new true predict not taken
 *     _fnew_pnt          predicate.new false predict not taken
 * Predictions are not modelled in QEMU
 *
 * Example:
 *     if (p1) r31:30 = dealloc_return(r30):raw
 */
#define fGEN_TCG_L4_return_t(SHORTCODE) \
    gen_cond_return(ctx, RddV, RsV, PvV, TCG_COND_EQ);
#define fGEN_TCG_L4_return_f(SHORTCODE) \
    gen_cond_return(ctx, RddV, RsV, PvV, TCG_COND_NE)
#define fGEN_TCG_L4_return_tnew_pt(SHORTCODE) \
    gen_cond_return(ctx, RddV, RsV, PvN, TCG_COND_EQ)
#define fGEN_TCG_L4_return_fnew_pt(SHORTCODE) \
    gen_cond_return(ctx, RddV, RsV, PvN, TCG_COND_NE)
#define fGEN_TCG_L4_return_tnew_pnt(SHORTCODE) \
    gen_cond_return(ctx, RddV, RsV, PvN, TCG_COND_EQ)
#define fGEN_TCG_L4_return_fnew_pnt(SHORTCODE) \
    gen_cond_return(ctx, RddV, RsV, PvN, TCG_COND_NE)

#define fGEN_TCG_SL2_return_t(SHORTCODE) \
    gen_cond_return_subinsn(ctx, TCG_COND_EQ, hex_pred[0])
#define fGEN_TCG_SL2_return_f(SHORTCODE) \
    gen_cond_return_subinsn(ctx, TCG_COND_NE, hex_pred[0])
#define fGEN_TCG_SL2_return_tnew(SHORTCODE) \
    gen_cond_return_subinsn(ctx, TCG_COND_EQ, ctx->new_pred_value[0])
#define fGEN_TCG_SL2_return_fnew(SHORTCODE) \
    gen_cond_return_subinsn(ctx, TCG_COND_NE, ctx->new_pred_value[0])

/*
 * Mathematical operations with more than one definition require
 * special handling
 */
#define fGEN_TCG_A5_ACS(SHORTCODE) \
    do { \
        gen_helper_vacsh_pred(PeV, tcg_env, RxxV, RssV, RttV); \
        gen_helper_vacsh_val(RxxV, tcg_env, RxxV, RssV, RttV, \
                             tcg_constant_tl(ctx->need_commit)); \
    } while (0)

#define fGEN_TCG_S2_cabacdecbin(SHORTCODE) \
    do { \
        TCGv p0 = tcg_temp_new(); \
        gen_helper_cabacdecbin_pred(p0, RssV, RttV); \
        gen_helper_cabacdecbin_val(RddV, RssV, RttV); \
        gen_log_pred_write(ctx, 0, p0); \
    } while (0)

/*
 * Approximate reciprocal
 * r3,p1 = sfrecipa(r0, r1)
 *
 * The helper packs the 2 32-bit results into a 64-bit value,
 * so unpack them into the proper results.
 */
#define fGEN_TCG_F2_sfrecipa(SHORTCODE) \
    do { \
        TCGv_i64 tmp = tcg_temp_new_i64(); \
        gen_helper_sfrecipa(tmp, tcg_env, RsV, RtV);  \
        tcg_gen_extrh_i64_i32(RdV, tmp); \
        tcg_gen_extrl_i64_i32(PeV, tmp); \
    } while (0)

/*
 * Approximation of the reciprocal square root
 * r1,p0 = sfinvsqrta(r0)
 *
 * The helper packs the 2 32-bit results into a 64-bit value,
 * so unpack them into the proper results.
 */
#define fGEN_TCG_F2_sfinvsqrta(SHORTCODE) \
    do { \
        TCGv_i64 tmp = tcg_temp_new_i64(); \
        gen_helper_sfinvsqrta(tmp, tcg_env, RsV); \
        tcg_gen_extrh_i64_i32(RdV, tmp); \
        tcg_gen_extrl_i64_i32(PeV, tmp); \
    } while (0)

/*
 * Add or subtract with carry.
 * Predicate register is used as an extra input and output.
 * r5:4 = add(r1:0, r3:2, p1):carry
 */
#define fGEN_TCG_A4_addp_c(SHORTCODE) \
    do { \
        TCGv_i64 carry = tcg_temp_new_i64(); \
        TCGv_i64 zero = tcg_constant_i64(0); \
        tcg_gen_extu_i32_i64(carry, PxV); \
        tcg_gen_andi_i64(carry, carry, 1); \
        tcg_gen_add2_i64(RddV, carry, RssV, zero, carry, zero); \
        tcg_gen_add2_i64(RddV, carry, RddV, carry, RttV, zero); \
        tcg_gen_extrl_i64_i32(PxV, carry); \
        gen_8bitsof(PxV, PxV); \
    } while (0)

/* r5:4 = sub(r1:0, r3:2, p1):carry */
#define fGEN_TCG_A4_subp_c(SHORTCODE) \
    do { \
        TCGv_i64 carry = tcg_temp_new_i64(); \
        TCGv_i64 zero = tcg_constant_i64(0); \
        TCGv_i64 not_RttV = tcg_temp_new_i64(); \
        tcg_gen_extu_i32_i64(carry, PxV); \
        tcg_gen_andi_i64(carry, carry, 1); \
        tcg_gen_not_i64(not_RttV, RttV); \
        tcg_gen_add2_i64(RddV, carry, RssV, zero, carry, zero); \
        tcg_gen_add2_i64(RddV, carry, RddV, carry, not_RttV, zero); \
        tcg_gen_extrl_i64_i32(PxV, carry); \
        gen_8bitsof(PxV, PxV); \
    } while (0)

/*
 * Compare each of the 8 unsigned bytes
 * The minimum is placed in each byte of the destination.
 * Each bit of the predicate is set true if the bit from the first operand
 * is greater than the bit from the second operand.
 * r5:4,p1 = vminub(r1:0, r3:2)
 */
#define fGEN_TCG_A6_vminub_RdP(SHORTCODE) \
    do { \
        TCGv left = tcg_temp_new(); \
        TCGv right = tcg_temp_new(); \
        TCGv tmp = tcg_temp_new(); \
        tcg_gen_movi_tl(PeV, 0); \
        tcg_gen_movi_i64(RddV, 0); \
        for (int i = 0; i < 8; i++) { \
            gen_get_byte_i64(left, i, RttV, false); \
            gen_get_byte_i64(right, i, RssV, false); \
            tcg_gen_setcond_tl(TCG_COND_GT, tmp, left, right); \
            tcg_gen_deposit_tl(PeV, PeV, tmp, i, 1); \
            tcg_gen_umin_tl(tmp, left, right); \
            gen_set_byte_i64(i, RddV, tmp); \
        } \
    } while (0)

#define fGEN_TCG_J2_call(SHORTCODE) \
    gen_call(ctx, riV)
#define fGEN_TCG_J2_callr(SHORTCODE) \
    gen_callr(ctx, RsV)
#define fGEN_TCG_J2_callrh(SHORTCODE) \
    gen_callr(ctx, RsV)

#define fGEN_TCG_J2_callt(SHORTCODE) \
    gen_cond_call(ctx, PuV, TCG_COND_EQ, riV)
#define fGEN_TCG_J2_callf(SHORTCODE) \
    gen_cond_call(ctx, PuV, TCG_COND_NE, riV)
#define fGEN_TCG_J2_callrt(SHORTCODE) \
    gen_cond_callr(ctx, TCG_COND_EQ, PuV, RsV)
#define fGEN_TCG_J2_callrf(SHORTCODE) \
    gen_cond_callr(ctx, TCG_COND_NE, PuV, RsV)

#define fGEN_TCG_J2_loop0r(SHORTCODE) \
    gen_loop0r(ctx, RsV, riV)
#define fGEN_TCG_J2_loop1r(SHORTCODE) \
    gen_loop1r(ctx, RsV, riV)
#define fGEN_TCG_J2_loop0i(SHORTCODE) \
    gen_loop0i(ctx, UiV, riV)
#define fGEN_TCG_J2_loop1i(SHORTCODE) \
    gen_loop1i(ctx, UiV, riV)
#define fGEN_TCG_J2_ploop1sr(SHORTCODE) \
    gen_ploopNsr(ctx, 1, RsV, riV)
#define fGEN_TCG_J2_ploop1si(SHORTCODE) \
    gen_ploopNsi(ctx, 1, UiV, riV)
#define fGEN_TCG_J2_ploop2sr(SHORTCODE) \
    gen_ploopNsr(ctx, 2, RsV, riV)
#define fGEN_TCG_J2_ploop2si(SHORTCODE) \
    gen_ploopNsi(ctx, 2, UiV, riV)
#define fGEN_TCG_J2_ploop3sr(SHORTCODE) \
    gen_ploopNsr(ctx, 3, RsV, riV)
#define fGEN_TCG_J2_ploop3si(SHORTCODE) \
    gen_ploopNsi(ctx, 3, UiV, riV)

#define fGEN_TCG_J2_endloop0(SHORTCODE) \
    gen_endloop0(ctx)
#define fGEN_TCG_J2_endloop1(SHORTCODE) \
    gen_endloop1(ctx)
#define fGEN_TCG_J2_endloop01(SHORTCODE) \
    gen_endloop01(ctx)

/*
 * Compound compare and jump instructions
 * Here is a primer to understand the tag names
 *
 * Comparison
 *      cmpeqi   compare equal to an immediate
 *      cmpgti   compare greater than an immediate
 *      cmpgtiu  compare greater than an unsigned immediate
 *      cmpeqn1  compare equal to negative 1
 *      cmpgtn1  compare greater than negative 1
 *      cmpeq    compare equal (two registers)
 *      cmpgtu   compare greater than unsigned (two registers)
 *      tstbit0  test bit zero
 *
 * Condition
 *      tp0      p0 is true     p0 = cmp.eq(r0,#5); if (p0.new) jump:nt address
 *      fp0      p0 is false    p0 = cmp.eq(r0,#5); if (!p0.new) jump:nt address
 *      tp1      p1 is true     p1 = cmp.eq(r0,#5); if (p1.new) jump:nt address
 *      fp1      p1 is false    p1 = cmp.eq(r0,#5); if (!p1.new) jump:nt address
 *
 * Prediction (not modelled in qemu)
 *      _nt      not taken
 *      _t       taken
 */
#define fGEN_TCG_J4_cmpeq_tp0_jump_t(SHORTCODE) \
    gen_cmpnd_cmp_jmp_t(ctx, 0, TCG_COND_EQ, RsV, RtV, riV)
#define fGEN_TCG_J4_cmpeq_tp0_jump_nt(SHORTCODE) \
    gen_cmpnd_cmp_jmp_t(ctx, 0, TCG_COND_EQ, RsV, RtV, riV)
#define fGEN_TCG_J4_cmpeq_fp0_jump_t(SHORTCODE) \
    gen_cmpnd_cmp_jmp_f(ctx, 0, TCG_COND_EQ, RsV, RtV, riV)
#define fGEN_TCG_J4_cmpeq_fp0_jump_nt(SHORTCODE) \
    gen_cmpnd_cmp_jmp_f(ctx, 0, TCG_COND_EQ, RsV, RtV, riV)
#define fGEN_TCG_J4_cmpeq_tp1_jump_t(SHORTCODE) \
    gen_cmpnd_cmp_jmp_t(ctx, 1, TCG_COND_EQ, RsV, RtV, riV)
#define fGEN_TCG_J4_cmpeq_tp1_jump_nt(SHORTCODE) \
    gen_cmpnd_cmp_jmp_t(ctx, 1, TCG_COND_EQ, RsV, RtV, riV)
#define fGEN_TCG_J4_cmpeq_fp1_jump_t(SHORTCODE) \
    gen_cmpnd_cmp_jmp_f(ctx, 1, TCG_COND_EQ, RsV, RtV, riV)
#define fGEN_TCG_J4_cmpeq_fp1_jump_nt(SHORTCODE) \
    gen_cmpnd_cmp_jmp_f(ctx, 1, TCG_COND_EQ, RsV, RtV, riV)

#define fGEN_TCG_J4_cmpgt_tp0_jump_t(SHORTCODE) \
    gen_cmpnd_cmp_jmp_t(ctx, 0, TCG_COND_GT, RsV, RtV, riV)
#define fGEN_TCG_J4_cmpgt_tp0_jump_nt(SHORTCODE) \
    gen_cmpnd_cmp_jmp_t(ctx, 0, TCG_COND_GT, RsV, RtV, riV)
#define fGEN_TCG_J4_cmpgt_fp0_jump_t(SHORTCODE) \
    gen_cmpnd_cmp_jmp_f(ctx, 0, TCG_COND_GT, RsV, RtV, riV)
#define fGEN_TCG_J4_cmpgt_fp0_jump_nt(SHORTCODE) \
    gen_cmpnd_cmp_jmp_f(ctx, 0, TCG_COND_GT, RsV, RtV, riV)
#define fGEN_TCG_J4_cmpgt_tp1_jump_t(SHORTCODE) \
    gen_cmpnd_cmp_jmp_t(ctx, 1, TCG_COND_GT, RsV, RtV, riV)
#define fGEN_TCG_J4_cmpgt_tp1_jump_nt(SHORTCODE) \
    gen_cmpnd_cmp_jmp_t(ctx, 1, TCG_COND_GT, RsV, RtV, riV)
#define fGEN_TCG_J4_cmpgt_fp1_jump_t(SHORTCODE) \
    gen_cmpnd_cmp_jmp_f(ctx, 1, TCG_COND_GT, RsV, RtV, riV)
#define fGEN_TCG_J4_cmpgt_fp1_jump_nt(SHORTCODE) \
    gen_cmpnd_cmp_jmp_f(ctx, 1, TCG_COND_GT, RsV, RtV, riV)

#define fGEN_TCG_J4_cmpgtu_tp0_jump_t(SHORTCODE) \
    gen_cmpnd_cmp_jmp_t(ctx, 0, TCG_COND_GTU, RsV, RtV, riV)
#define fGEN_TCG_J4_cmpgtu_tp0_jump_nt(SHORTCODE) \
    gen_cmpnd_cmp_jmp_t(ctx, 0, TCG_COND_GTU, RsV, RtV, riV)
#define fGEN_TCG_J4_cmpgtu_fp0_jump_t(SHORTCODE) \
    gen_cmpnd_cmp_jmp_f(ctx, 0, TCG_COND_GTU, RsV, RtV, riV)
#define fGEN_TCG_J4_cmpgtu_fp0_jump_nt(SHORTCODE) \
    gen_cmpnd_cmp_jmp_f(ctx, 0, TCG_COND_GTU, RsV, RtV, riV)
#define fGEN_TCG_J4_cmpgtu_tp1_jump_t(SHORTCODE) \
    gen_cmpnd_cmp_jmp_t(ctx, 1, TCG_COND_GTU, RsV, RtV, riV)
#define fGEN_TCG_J4_cmpgtu_tp1_jump_nt(SHORTCODE) \
    gen_cmpnd_cmp_jmp_t(ctx, 1, TCG_COND_GTU, RsV, RtV, riV)
#define fGEN_TCG_J4_cmpgtu_fp1_jump_t(SHORTCODE) \
    gen_cmpnd_cmp_jmp_f(ctx, 1, TCG_COND_GTU, RsV, RtV, riV)
#define fGEN_TCG_J4_cmpgtu_fp1_jump_nt(SHORTCODE) \
    gen_cmpnd_cmp_jmp_f(ctx, 1, TCG_COND_GTU, RsV, RtV, riV)

#define fGEN_TCG_J4_cmpeqi_tp0_jump_t(SHORTCODE) \
    gen_cmpnd_cmpi_jmp_t(ctx, 0, TCG_COND_EQ, RsV, UiV, riV)
#define fGEN_TCG_J4_cmpeqi_tp0_jump_nt(SHORTCODE) \
    gen_cmpnd_cmpi_jmp_t(ctx, 0, TCG_COND_EQ, RsV, UiV, riV)
#define fGEN_TCG_J4_cmpeqi_fp0_jump_t(SHORTCODE) \
    gen_cmpnd_cmpi_jmp_f(ctx, 0, TCG_COND_EQ, RsV, UiV, riV)
#define fGEN_TCG_J4_cmpeqi_fp0_jump_nt(SHORTCODE) \
    gen_cmpnd_cmpi_jmp_f(ctx, 0, TCG_COND_EQ, RsV, UiV, riV)
#define fGEN_TCG_J4_cmpeqi_tp1_jump_t(SHORTCODE) \
    gen_cmpnd_cmpi_jmp_t(ctx, 1, TCG_COND_EQ, RsV, UiV, riV)
#define fGEN_TCG_J4_cmpeqi_tp1_jump_nt(SHORTCODE) \
    gen_cmpnd_cmpi_jmp_t(ctx, 1, TCG_COND_EQ, RsV, UiV, riV)
#define fGEN_TCG_J4_cmpeqi_fp1_jump_t(SHORTCODE) \
    gen_cmpnd_cmpi_jmp_f(ctx, 1, TCG_COND_EQ, RsV, UiV, riV)
#define fGEN_TCG_J4_cmpeqi_fp1_jump_nt(SHORTCODE) \
    gen_cmpnd_cmpi_jmp_f(ctx, 1, TCG_COND_EQ, RsV, UiV, riV)

#define fGEN_TCG_J4_cmpgti_tp0_jump_t(SHORTCODE) \
    gen_cmpnd_cmpi_jmp_t(ctx, 0, TCG_COND_GT, RsV, UiV, riV)
#define fGEN_TCG_J4_cmpgti_tp0_jump_nt(SHORTCODE) \
    gen_cmpnd_cmpi_jmp_t(ctx, 0, TCG_COND_GT, RsV, UiV, riV)
#define fGEN_TCG_J4_cmpgti_fp0_jump_t(SHORTCODE) \
    gen_cmpnd_cmpi_jmp_f(ctx, 0, TCG_COND_GT, RsV, UiV, riV)
#define fGEN_TCG_J4_cmpgti_fp0_jump_nt(SHORTCODE) \
    gen_cmpnd_cmpi_jmp_f(ctx, 0, TCG_COND_GT, RsV, UiV, riV)
#define fGEN_TCG_J4_cmpgti_tp1_jump_t(SHORTCODE) \
    gen_cmpnd_cmpi_jmp_t(ctx, 1, TCG_COND_GT, RsV, UiV, riV)
#define fGEN_TCG_J4_cmpgti_tp1_jump_nt(SHORTCODE) \
    gen_cmpnd_cmpi_jmp_t(ctx, 1, TCG_COND_GT, RsV, UiV, riV)
#define fGEN_TCG_J4_cmpgti_fp1_jump_t(SHORTCODE) \
    gen_cmpnd_cmpi_jmp_f(ctx, 1, TCG_COND_GT, RsV, UiV, riV)
#define fGEN_TCG_J4_cmpgti_fp1_jump_nt(SHORTCODE) \
    gen_cmpnd_cmpi_jmp_f(ctx, 1, TCG_COND_GT, RsV, UiV, riV)

#define fGEN_TCG_J4_cmpgtui_tp0_jump_t(SHORTCODE) \
    gen_cmpnd_cmpi_jmp_t(ctx, 0, TCG_COND_GTU, RsV, UiV, riV)
#define fGEN_TCG_J4_cmpgtui_tp0_jump_nt(SHORTCODE) \
    gen_cmpnd_cmpi_jmp_t(ctx, 0, TCG_COND_GTU, RsV, UiV, riV)
#define fGEN_TCG_J4_cmpgtui_fp0_jump_t(SHORTCODE) \
    gen_cmpnd_cmpi_jmp_f(ctx, 0, TCG_COND_GTU, RsV, UiV, riV)
#define fGEN_TCG_J4_cmpgtui_fp0_jump_nt(SHORTCODE) \
    gen_cmpnd_cmpi_jmp_f(ctx, 0, TCG_COND_GTU, RsV, UiV, riV)
#define fGEN_TCG_J4_cmpgtui_tp1_jump_t(SHORTCODE) \
    gen_cmpnd_cmpi_jmp_t(ctx, 1, TCG_COND_GTU, RsV, UiV, riV)
#define fGEN_TCG_J4_cmpgtui_tp1_jump_nt(SHORTCODE) \
    gen_cmpnd_cmpi_jmp_t(ctx, 1, TCG_COND_GTU, RsV, UiV, riV)
#define fGEN_TCG_J4_cmpgtui_fp1_jump_t(SHORTCODE) \
    gen_cmpnd_cmpi_jmp_f(ctx, 1, TCG_COND_GTU, RsV, UiV, riV)
#define fGEN_TCG_J4_cmpgtui_fp1_jump_nt(SHORTCODE) \
    gen_cmpnd_cmpi_jmp_f(ctx, 1, TCG_COND_GTU, RsV, UiV, riV)

#define fGEN_TCG_J4_cmpeqn1_tp0_jump_t(SHORTCODE) \
    gen_cmpnd_cmp_n1_jmp_t(ctx, 0, TCG_COND_EQ, RsV, riV)
#define fGEN_TCG_J4_cmpeqn1_tp0_jump_nt(SHORTCODE) \
    gen_cmpnd_cmp_n1_jmp_t(ctx, 0, TCG_COND_EQ, RsV, riV)
#define fGEN_TCG_J4_cmpeqn1_fp0_jump_t(SHORTCODE) \
    gen_cmpnd_cmp_n1_jmp_f(ctx, 0, TCG_COND_EQ, RsV, riV)
#define fGEN_TCG_J4_cmpeqn1_fp0_jump_nt(SHORTCODE) \
    gen_cmpnd_cmp_n1_jmp_f(ctx, 0, TCG_COND_EQ, RsV, riV)
#define fGEN_TCG_J4_cmpeqn1_tp1_jump_t(SHORTCODE) \
    gen_cmpnd_cmp_n1_jmp_t(ctx, 1, TCG_COND_EQ, RsV, riV)
#define fGEN_TCG_J4_cmpeqn1_tp1_jump_nt(SHORTCODE) \
    gen_cmpnd_cmp_n1_jmp_t(ctx, 1, TCG_COND_EQ, RsV, riV)
#define fGEN_TCG_J4_cmpeqn1_fp1_jump_t(SHORTCODE) \
    gen_cmpnd_cmp_n1_jmp_f(ctx, 1, TCG_COND_EQ, RsV, riV)
#define fGEN_TCG_J4_cmpeqn1_fp1_jump_nt(SHORTCODE) \
    gen_cmpnd_cmp_n1_jmp_f(ctx, 1, TCG_COND_EQ, RsV, riV)

#define fGEN_TCG_J4_cmpgtn1_tp0_jump_t(SHORTCODE) \
    gen_cmpnd_cmp_n1_jmp_t(ctx, 0, TCG_COND_GT, RsV, riV)
#define fGEN_TCG_J4_cmpgtn1_tp0_jump_nt(SHORTCODE) \
    gen_cmpnd_cmp_n1_jmp_t(ctx, 0, TCG_COND_GT, RsV, riV)
#define fGEN_TCG_J4_cmpgtn1_fp0_jump_t(SHORTCODE) \
    gen_cmpnd_cmp_n1_jmp_f(ctx, 0, TCG_COND_GT, RsV, riV)
#define fGEN_TCG_J4_cmpgtn1_fp0_jump_nt(SHORTCODE) \
    gen_cmpnd_cmp_n1_jmp_f(ctx, 0, TCG_COND_GT, RsV, riV)
#define fGEN_TCG_J4_cmpgtn1_tp1_jump_t(SHORTCODE) \
    gen_cmpnd_cmp_n1_jmp_t(ctx, 1, TCG_COND_GT, RsV, riV)
#define fGEN_TCG_J4_cmpgtn1_tp1_jump_nt(SHORTCODE) \
    gen_cmpnd_cmp_n1_jmp_t(ctx, 1, TCG_COND_GT, RsV, riV)
#define fGEN_TCG_J4_cmpgtn1_fp1_jump_t(SHORTCODE) \
    gen_cmpnd_cmp_n1_jmp_f(ctx, 1, TCG_COND_GT, RsV, riV)
#define fGEN_TCG_J4_cmpgtn1_fp1_jump_nt(SHORTCODE) \
    gen_cmpnd_cmp_n1_jmp_f(ctx, 1, TCG_COND_GT, RsV, riV)

#define fGEN_TCG_J4_tstbit0_tp0_jump_nt(SHORTCODE) \
    gen_cmpnd_tstbit0_jmp(ctx, 0, RsV, TCG_COND_EQ, riV)
#define fGEN_TCG_J4_tstbit0_tp0_jump_t(SHORTCODE) \
    gen_cmpnd_tstbit0_jmp(ctx, 0, RsV, TCG_COND_EQ, riV)
#define fGEN_TCG_J4_tstbit0_fp0_jump_nt(SHORTCODE) \
    gen_cmpnd_tstbit0_jmp(ctx, 0, RsV, TCG_COND_NE, riV)
#define fGEN_TCG_J4_tstbit0_fp0_jump_t(SHORTCODE) \
    gen_cmpnd_tstbit0_jmp(ctx, 0, RsV, TCG_COND_NE, riV)
#define fGEN_TCG_J4_tstbit0_tp1_jump_nt(SHORTCODE) \
    gen_cmpnd_tstbit0_jmp(ctx, 1, RsV, TCG_COND_EQ, riV)
#define fGEN_TCG_J4_tstbit0_tp1_jump_t(SHORTCODE) \
    gen_cmpnd_tstbit0_jmp(ctx, 1, RsV, TCG_COND_EQ, riV)
#define fGEN_TCG_J4_tstbit0_fp1_jump_nt(SHORTCODE) \
    gen_cmpnd_tstbit0_jmp(ctx, 1, RsV, TCG_COND_NE, riV)
#define fGEN_TCG_J4_tstbit0_fp1_jump_t(SHORTCODE) \
    gen_cmpnd_tstbit0_jmp(ctx, 1, RsV, TCG_COND_NE, riV)

/* p0 = cmp.eq(r0, #7) */
#define fGEN_TCG_SA1_cmpeqi(SHORTCODE) \
    do { \
        TCGv p0 = tcg_temp_new(); \
        gen_comparei(TCG_COND_EQ, p0, RsV, uiV); \
        gen_log_pred_write(ctx, 0, p0); \
    } while (0)

#define fGEN_TCG_J2_jump(SHORTCODE) \
    gen_jump(ctx, riV)
#define fGEN_TCG_J2_jumpr(SHORTCODE) \
    gen_jumpr(ctx, RsV)
#define fGEN_TCG_J2_jumprh(SHORTCODE) \
    gen_jumpr(ctx, RsV)
#define fGEN_TCG_J4_jumpseti(SHORTCODE) \
    do { \
        tcg_gen_movi_tl(RdV, UiV); \
        gen_jump(ctx, riV); \
    } while (0)

#define fGEN_TCG_cond_jumpt(COND) \
    do { \
        TCGv LSB = tcg_temp_new(); \
        COND; \
        gen_cond_jump(ctx, TCG_COND_EQ, LSB, riV); \
    } while (0)
#define fGEN_TCG_cond_jumpf(COND) \
    do { \
        TCGv LSB = tcg_temp_new(); \
        COND; \
        gen_cond_jump(ctx, TCG_COND_NE, LSB, riV); \
    } while (0)

#define fGEN_TCG_J2_jumpt(SHORTCODE) \
    fGEN_TCG_cond_jumpt(fLSBOLD(PuV))
#define fGEN_TCG_J2_jumptpt(SHORTCODE) \
    fGEN_TCG_cond_jumpt(fLSBOLD(PuV))
#define fGEN_TCG_J2_jumpf(SHORTCODE) \
    fGEN_TCG_cond_jumpf(fLSBOLD(PuV))
#define fGEN_TCG_J2_jumpfpt(SHORTCODE) \
    fGEN_TCG_cond_jumpf(fLSBOLD(PuV))
#define fGEN_TCG_J2_jumptnew(SHORTCODE) \
    gen_cond_jump(ctx, TCG_COND_EQ, PuN, riV)
#define fGEN_TCG_J2_jumptnewpt(SHORTCODE) \
    gen_cond_jump(ctx, TCG_COND_EQ, PuN, riV)
#define fGEN_TCG_J2_jumpfnewpt(SHORTCODE) \
    fGEN_TCG_cond_jumpf(fLSBNEW(PuN))
#define fGEN_TCG_J2_jumpfnew(SHORTCODE) \
    fGEN_TCG_cond_jumpf(fLSBNEW(PuN))
#define fGEN_TCG_J2_jumprz(SHORTCODE) \
    fGEN_TCG_cond_jumpt(tcg_gen_setcondi_tl(TCG_COND_NE, LSB, RsV, 0))
#define fGEN_TCG_J2_jumprzpt(SHORTCODE) \
    fGEN_TCG_cond_jumpt(tcg_gen_setcondi_tl(TCG_COND_NE, LSB, RsV, 0))
#define fGEN_TCG_J2_jumprnz(SHORTCODE) \
    fGEN_TCG_cond_jumpt(tcg_gen_setcondi_tl(TCG_COND_EQ, LSB, RsV, 0))
#define fGEN_TCG_J2_jumprnzpt(SHORTCODE) \
    fGEN_TCG_cond_jumpt(tcg_gen_setcondi_tl(TCG_COND_EQ, LSB, RsV, 0))
#define fGEN_TCG_J2_jumprgtez(SHORTCODE) \
    fGEN_TCG_cond_jumpt(tcg_gen_setcondi_tl(TCG_COND_GE, LSB, RsV, 0))
#define fGEN_TCG_J2_jumprgtezpt(SHORTCODE) \
    fGEN_TCG_cond_jumpt(tcg_gen_setcondi_tl(TCG_COND_GE, LSB, RsV, 0))
#define fGEN_TCG_J2_jumprltez(SHORTCODE) \
    fGEN_TCG_cond_jumpt(tcg_gen_setcondi_tl(TCG_COND_LE, LSB, RsV, 0))
#define fGEN_TCG_J2_jumprltezpt(SHORTCODE) \
    fGEN_TCG_cond_jumpt(tcg_gen_setcondi_tl(TCG_COND_LE, LSB, RsV, 0))

#define fGEN_TCG_cond_jumprt(COND) \
    do { \
        TCGv LSB = tcg_temp_new(); \
        COND; \
        gen_cond_jumpr(ctx, RsV, TCG_COND_EQ, LSB); \
    } while (0)
#define fGEN_TCG_cond_jumprf(COND) \
    do { \
        TCGv LSB = tcg_temp_new(); \
        COND; \
        gen_cond_jumpr(ctx, RsV, TCG_COND_NE, LSB); \
    } while (0)

#define fGEN_TCG_J2_jumprt(SHORTCODE) \
    fGEN_TCG_cond_jumprt(fLSBOLD(PuV))
#define fGEN_TCG_J2_jumprtpt(SHORTCODE) \
    fGEN_TCG_cond_jumprt(fLSBOLD(PuV))
#define fGEN_TCG_J2_jumprf(SHORTCODE) \
    fGEN_TCG_cond_jumprf(fLSBOLD(PuV))
#define fGEN_TCG_J2_jumprfpt(SHORTCODE) \
    fGEN_TCG_cond_jumprf(fLSBOLD(PuV))
#define fGEN_TCG_J2_jumprtnew(SHORTCODE) \
    fGEN_TCG_cond_jumprt(fLSBNEW(PuN))
#define fGEN_TCG_J2_jumprtnewpt(SHORTCODE) \
    fGEN_TCG_cond_jumprt(fLSBNEW(PuN))
#define fGEN_TCG_J2_jumprfnew(SHORTCODE) \
    fGEN_TCG_cond_jumprf(fLSBNEW(PuN))
#define fGEN_TCG_J2_jumprfnewpt(SHORTCODE) \
    fGEN_TCG_cond_jumprf(fLSBNEW(PuN))

/*
 * New value compare & jump instructions
 * if ([!]COND(r0.new, r1) jump:t address
 * if ([!]COND(r0.new, #7) jump:t address
 */
#define fGEN_TCG_J4_cmpgt_t_jumpnv_t(SHORTCODE) \
    gen_cmp_jumpnv(ctx, TCG_COND_GT, NsN, RtV, riV)
#define fGEN_TCG_J4_cmpgt_t_jumpnv_nt(SHORTCODE) \
    gen_cmp_jumpnv(ctx, TCG_COND_GT, NsN, RtV, riV)
#define fGEN_TCG_J4_cmpgt_f_jumpnv_t(SHORTCODE) \
    gen_cmp_jumpnv(ctx, TCG_COND_LE, NsN, RtV, riV)
#define fGEN_TCG_J4_cmpgt_f_jumpnv_nt(SHORTCODE) \
    gen_cmp_jumpnv(ctx, TCG_COND_LE, NsN, RtV, riV)

#define fGEN_TCG_J4_cmpeq_t_jumpnv_t(SHORTCODE) \
    gen_cmp_jumpnv(ctx, TCG_COND_EQ, NsN, RtV, riV)
#define fGEN_TCG_J4_cmpeq_t_jumpnv_nt(SHORTCODE) \
    gen_cmp_jumpnv(ctx, TCG_COND_EQ, NsN, RtV, riV)
#define fGEN_TCG_J4_cmpeq_f_jumpnv_t(SHORTCODE) \
    gen_cmp_jumpnv(ctx, TCG_COND_NE, NsN, RtV, riV)
#define fGEN_TCG_J4_cmpeq_f_jumpnv_nt(SHORTCODE) \
    gen_cmp_jumpnv(ctx, TCG_COND_NE, NsN, RtV, riV)

#define fGEN_TCG_J4_cmplt_t_jumpnv_t(SHORTCODE) \
    gen_cmp_jumpnv(ctx, TCG_COND_LT, NsN, RtV, riV)
#define fGEN_TCG_J4_cmplt_t_jumpnv_nt(SHORTCODE) \
    gen_cmp_jumpnv(ctx, TCG_COND_LT, NsN, RtV, riV)
#define fGEN_TCG_J4_cmplt_f_jumpnv_t(SHORTCODE) \
    gen_cmp_jumpnv(ctx, TCG_COND_GE, NsN, RtV, riV)
#define fGEN_TCG_J4_cmplt_f_jumpnv_nt(SHORTCODE) \
    gen_cmp_jumpnv(ctx, TCG_COND_GE, NsN, RtV, riV)

#define fGEN_TCG_J4_cmpeqi_t_jumpnv_t(SHORTCODE) \
    gen_cmpi_jumpnv(ctx, TCG_COND_EQ, NsN, UiV, riV)
#define fGEN_TCG_J4_cmpeqi_t_jumpnv_nt(SHORTCODE) \
    gen_cmpi_jumpnv(ctx, TCG_COND_EQ, NsN, UiV, riV)
#define fGEN_TCG_J4_cmpeqi_f_jumpnv_t(SHORTCODE) \
    gen_cmpi_jumpnv(ctx, TCG_COND_NE, NsN, UiV, riV)
#define fGEN_TCG_J4_cmpeqi_f_jumpnv_nt(SHORTCODE) \
    gen_cmpi_jumpnv(ctx, TCG_COND_NE, NsN, UiV, riV)

#define fGEN_TCG_J4_cmpgti_t_jumpnv_t(SHORTCODE) \
    gen_cmpi_jumpnv(ctx, TCG_COND_GT, NsN, UiV, riV)
#define fGEN_TCG_J4_cmpgti_t_jumpnv_nt(SHORTCODE) \
    gen_cmpi_jumpnv(ctx, TCG_COND_GT, NsN, UiV, riV)
#define fGEN_TCG_J4_cmpgti_f_jumpnv_t(SHORTCODE) \
    gen_cmpi_jumpnv(ctx, TCG_COND_LE, NsN, UiV, riV)
#define fGEN_TCG_J4_cmpgti_f_jumpnv_nt(SHORTCODE) \
    gen_cmpi_jumpnv(ctx, TCG_COND_LE, NsN, UiV, riV)

#define fGEN_TCG_J4_cmpltu_t_jumpnv_t(SHORTCODE) \
    gen_cmp_jumpnv(ctx, TCG_COND_LTU, NsN, RtV, riV)
#define fGEN_TCG_J4_cmpltu_t_jumpnv_nt(SHORTCODE) \
    gen_cmp_jumpnv(ctx, TCG_COND_LTU, NsN, RtV, riV)
#define fGEN_TCG_J4_cmpltu_f_jumpnv_t(SHORTCODE) \
    gen_cmp_jumpnv(ctx, TCG_COND_GEU, NsN, RtV, riV)
#define fGEN_TCG_J4_cmpltu_f_jumpnv_nt(SHORTCODE) \
    gen_cmp_jumpnv(ctx, TCG_COND_GEU, NsN, RtV, riV)

#define fGEN_TCG_J4_cmpgtui_t_jumpnv_t(SHORTCODE) \
    gen_cmpi_jumpnv(ctx, TCG_COND_GTU, NsN, UiV, riV)
#define fGEN_TCG_J4_cmpgtui_t_jumpnv_nt(SHORTCODE) \
    gen_cmpi_jumpnv(ctx, TCG_COND_GTU, NsN, UiV, riV)
#define fGEN_TCG_J4_cmpgtui_f_jumpnv_t(SHORTCODE) \
    gen_cmpi_jumpnv(ctx, TCG_COND_LEU, NsN, UiV, riV)
#define fGEN_TCG_J4_cmpgtui_f_jumpnv_nt(SHORTCODE) \
    gen_cmpi_jumpnv(ctx, TCG_COND_LEU, NsN, UiV, riV)

#define fGEN_TCG_J4_cmpgtu_t_jumpnv_t(SHORTCODE) \
    gen_cmp_jumpnv(ctx, TCG_COND_GTU, NsN, RtV, riV)
#define fGEN_TCG_J4_cmpgtu_t_jumpnv_nt(SHORTCODE) \
    gen_cmp_jumpnv(ctx, TCG_COND_GTU, NsN, RtV, riV)
#define fGEN_TCG_J4_cmpgtu_f_jumpnv_t(SHORTCODE) \
    gen_cmp_jumpnv(ctx, TCG_COND_LEU, NsN, RtV, riV)
#define fGEN_TCG_J4_cmpgtu_f_jumpnv_nt(SHORTCODE) \
    gen_cmp_jumpnv(ctx, TCG_COND_LEU, NsN, RtV, riV)

#define fGEN_TCG_J4_cmpeqn1_t_jumpnv_t(SHORTCODE) \
    gen_cmpi_jumpnv(ctx, TCG_COND_EQ, NsN, -1, riV)
#define fGEN_TCG_J4_cmpeqn1_t_jumpnv_nt(SHORTCODE) \
    gen_cmpi_jumpnv(ctx, TCG_COND_EQ, NsN, -1, riV)
#define fGEN_TCG_J4_cmpeqn1_f_jumpnv_t(SHORTCODE) \
    gen_cmpi_jumpnv(ctx, TCG_COND_NE, NsN, -1, riV)
#define fGEN_TCG_J4_cmpeqn1_f_jumpnv_nt(SHORTCODE) \
    gen_cmpi_jumpnv(ctx, TCG_COND_NE, NsN, -1, riV)

#define fGEN_TCG_J4_cmpgtn1_t_jumpnv_t(SHORTCODE) \
    gen_cmpi_jumpnv(ctx, TCG_COND_GT, NsN, -1, riV)
#define fGEN_TCG_J4_cmpgtn1_t_jumpnv_nt(SHORTCODE) \
    gen_cmpi_jumpnv(ctx, TCG_COND_GT, NsN, -1, riV)
#define fGEN_TCG_J4_cmpgtn1_f_jumpnv_t(SHORTCODE) \
    gen_cmpi_jumpnv(ctx, TCG_COND_LE, NsN, -1, riV)
#define fGEN_TCG_J4_cmpgtn1_f_jumpnv_nt(SHORTCODE) \
    gen_cmpi_jumpnv(ctx, TCG_COND_LE, NsN, -1, riV)

#define fGEN_TCG_J4_tstbit0_t_jumpnv_t(SHORTCODE) \
    gen_testbit0_jumpnv(ctx, NsN, TCG_COND_EQ, riV)
#define fGEN_TCG_J4_tstbit0_t_jumpnv_nt(SHORTCODE) \
    gen_testbit0_jumpnv(ctx, NsN, TCG_COND_EQ, riV)
#define fGEN_TCG_J4_tstbit0_f_jumpnv_t(SHORTCODE) \
    gen_testbit0_jumpnv(ctx, NsN, TCG_COND_NE, riV)
#define fGEN_TCG_J4_tstbit0_f_jumpnv_nt(SHORTCODE) \
    gen_testbit0_jumpnv(ctx, NsN, TCG_COND_NE, riV)

/* r0 = r1 ; jump address */
#define fGEN_TCG_J4_jumpsetr(SHORTCODE) \
    do { \
        tcg_gen_mov_tl(RdV, RsV); \
        gen_jump(ctx, riV); \
    } while (0)

/* if (p0.new) r0 = #0 */
#define fGEN_TCG_SA1_clrtnew(SHORTCODE) \
    do { \
        tcg_gen_movcond_tl(TCG_COND_EQ, RdV, \
                           ctx->new_pred_value[0], tcg_constant_tl(0), \
                           RdV, tcg_constant_tl(0)); \
    } while (0)

/* if (!p0.new) r0 = #0 */
#define fGEN_TCG_SA1_clrfnew(SHORTCODE) \
    do { \
        tcg_gen_movcond_tl(TCG_COND_NE, RdV, \
                           ctx->new_pred_value[0], tcg_constant_tl(0), \
                           RdV, tcg_constant_tl(0)); \
    } while (0)

#define fGEN_TCG_J2_pause(SHORTCODE) \
    do { \
        uiV = uiV; \
        tcg_gen_movi_tl(hex_gpr[HEX_REG_PC], ctx->next_PC); \
    } while (0)

/* r0 = asr(r1, r2):sat */
#define fGEN_TCG_S2_asr_r_r_sat(SHORTCODE) \
    gen_asr_r_r_sat(ctx, RdV, RsV, RtV)

/* r0 = asl(r1, r2):sat */
#define fGEN_TCG_S2_asl_r_r_sat(SHORTCODE) \
    gen_asl_r_r_sat(ctx, RdV, RsV, RtV)

#define fGEN_TCG_SL2_jumpr31(SHORTCODE) \
    gen_jumpr(ctx, hex_gpr[HEX_REG_LR])

#define fGEN_TCG_SL2_jumpr31_t(SHORTCODE) \
    gen_cond_jumpr31(ctx, TCG_COND_EQ, hex_pred[0])
#define fGEN_TCG_SL2_jumpr31_f(SHORTCODE) \
    gen_cond_jumpr31(ctx, TCG_COND_NE, hex_pred[0])

#define fGEN_TCG_SL2_jumpr31_tnew(SHORTCODE) \
    gen_cond_jumpr31(ctx, TCG_COND_EQ, ctx->new_pred_value[0])
#define fGEN_TCG_SL2_jumpr31_fnew(SHORTCODE) \
    gen_cond_jumpr31(ctx, TCG_COND_NE, ctx->new_pred_value[0])

/* Count trailing zeros/ones */
#define fGEN_TCG_S2_ct0(SHORTCODE) \
    do { \
        tcg_gen_ctzi_tl(RdV, RsV, 32); \
    } while (0)
#define fGEN_TCG_S2_ct1(SHORTCODE) \
    do { \
        tcg_gen_not_tl(RdV, RsV); \
        tcg_gen_ctzi_tl(RdV, RdV, 32); \
    } while (0)
#define fGEN_TCG_S2_ct0p(SHORTCODE) \
    do { \
        TCGv_i64 tmp = tcg_temp_new_i64(); \
        tcg_gen_ctzi_i64(tmp, RssV, 64); \
        tcg_gen_extrl_i64_i32(RdV, tmp); \
    } while (0)
#define fGEN_TCG_S2_ct1p(SHORTCODE) \
    do { \
        TCGv_i64 tmp = tcg_temp_new_i64(); \
        tcg_gen_not_i64(tmp, RssV); \
        tcg_gen_ctzi_i64(tmp, tmp, 64); \
        tcg_gen_extrl_i64_i32(RdV, tmp); \
    } while (0)

#define fGEN_TCG_S2_insert(SHORTCODE) \
    do { \
        int width = uiV; \
        int offset = UiV; \
        if (width != 0) { \
            if (offset + width > 32) { \
                width = 32 - offset; \
            } \
            tcg_gen_deposit_tl(RxV, RxV, RsV, offset, width); \
        } \
    } while (0)
#define fGEN_TCG_S2_insert_rp(SHORTCODE) \
    gen_insert_rp(ctx, RxV, RsV, RttV)
#define fGEN_TCG_S2_asr_r_svw_trun(SHORTCODE) \
    gen_asr_r_svw_trun(ctx, RdV, RssV, RtV)
#define fGEN_TCG_A2_swiz(SHORTCODE) \
    tcg_gen_bswap_tl(RdV, RsV)

/* Floating point */
#define fGEN_TCG_F2_conv_sf2df(SHORTCODE) \
    gen_helper_conv_sf2df(RddV, tcg_env, RsV)
#define fGEN_TCG_F2_conv_df2sf(SHORTCODE) \
    gen_helper_conv_df2sf(RdV, tcg_env, RssV)
#define fGEN_TCG_F2_conv_uw2sf(SHORTCODE) \
    gen_helper_conv_uw2sf(RdV, tcg_env, RsV)
#define fGEN_TCG_F2_conv_uw2df(SHORTCODE) \
    gen_helper_conv_uw2df(RddV, tcg_env, RsV)
#define fGEN_TCG_F2_conv_w2sf(SHORTCODE) \
    gen_helper_conv_w2sf(RdV, tcg_env, RsV)
#define fGEN_TCG_F2_conv_w2df(SHORTCODE) \
    gen_helper_conv_w2df(RddV, tcg_env, RsV)
#define fGEN_TCG_F2_conv_ud2sf(SHORTCODE) \
    gen_helper_conv_ud2sf(RdV, tcg_env, RssV)
#define fGEN_TCG_F2_conv_ud2df(SHORTCODE) \
    gen_helper_conv_ud2df(RddV, tcg_env, RssV)
#define fGEN_TCG_F2_conv_d2sf(SHORTCODE) \
    gen_helper_conv_d2sf(RdV, tcg_env, RssV)
#define fGEN_TCG_F2_conv_d2df(SHORTCODE) \
    gen_helper_conv_d2df(RddV, tcg_env, RssV)
#define fGEN_TCG_F2_conv_sf2uw(SHORTCODE) \
    gen_helper_conv_sf2uw(RdV, tcg_env, RsV)
#define fGEN_TCG_F2_conv_sf2w(SHORTCODE) \
    gen_helper_conv_sf2w(RdV, tcg_env, RsV)
#define fGEN_TCG_F2_conv_sf2ud(SHORTCODE) \
    gen_helper_conv_sf2ud(RddV, tcg_env, RsV)
#define fGEN_TCG_F2_conv_sf2d(SHORTCODE) \
    gen_helper_conv_sf2d(RddV, tcg_env, RsV)
#define fGEN_TCG_F2_conv_df2uw(SHORTCODE) \
    gen_helper_conv_df2uw(RdV, tcg_env, RssV)
#define fGEN_TCG_F2_conv_df2w(SHORTCODE) \
    gen_helper_conv_df2w(RdV, tcg_env, RssV)
#define fGEN_TCG_F2_conv_df2ud(SHORTCODE) \
    gen_helper_conv_df2ud(RddV, tcg_env, RssV)
#define fGEN_TCG_F2_conv_df2d(SHORTCODE) \
    gen_helper_conv_df2d(RddV, tcg_env, RssV)
#define fGEN_TCG_F2_conv_sf2uw_chop(SHORTCODE) \
    gen_helper_conv_sf2uw_chop(RdV, tcg_env, RsV)
#define fGEN_TCG_F2_conv_sf2w_chop(SHORTCODE) \
    gen_helper_conv_sf2w_chop(RdV, tcg_env, RsV)
#define fGEN_TCG_F2_conv_sf2ud_chop(SHORTCODE) \
    gen_helper_conv_sf2ud_chop(RddV, tcg_env, RsV)
#define fGEN_TCG_F2_conv_sf2d_chop(SHORTCODE) \
    gen_helper_conv_sf2d_chop(RddV, tcg_env, RsV)
#define fGEN_TCG_F2_conv_df2uw_chop(SHORTCODE) \
    gen_helper_conv_df2uw_chop(RdV, tcg_env, RssV)
#define fGEN_TCG_F2_conv_df2w_chop(SHORTCODE) \
    gen_helper_conv_df2w_chop(RdV, tcg_env, RssV)
#define fGEN_TCG_F2_conv_df2ud_chop(SHORTCODE) \
    gen_helper_conv_df2ud_chop(RddV, tcg_env, RssV)
#define fGEN_TCG_F2_conv_df2d_chop(SHORTCODE) \
    gen_helper_conv_df2d_chop(RddV, tcg_env, RssV)
#define fGEN_TCG_F2_sfadd(SHORTCODE) \
    gen_helper_sfadd(RdV, tcg_env, RsV, RtV)
#define fGEN_TCG_F2_sfsub(SHORTCODE) \
    gen_helper_sfsub(RdV, tcg_env, RsV, RtV)
#define fGEN_TCG_F2_sfcmpeq(SHORTCODE) \
    gen_helper_sfcmpeq(PdV, tcg_env, RsV, RtV)
#define fGEN_TCG_F2_sfcmpgt(SHORTCODE) \
    gen_helper_sfcmpgt(PdV, tcg_env, RsV, RtV)
#define fGEN_TCG_F2_sfcmpge(SHORTCODE) \
    gen_helper_sfcmpge(PdV, tcg_env, RsV, RtV)
#define fGEN_TCG_F2_sfcmpuo(SHORTCODE) \
    gen_helper_sfcmpuo(PdV, tcg_env, RsV, RtV)
#define fGEN_TCG_F2_sfmax(SHORTCODE) \
    gen_helper_sfmax(RdV, tcg_env, RsV, RtV)
#define fGEN_TCG_F2_sfmin(SHORTCODE) \
    gen_helper_sfmin(RdV, tcg_env, RsV, RtV)
#define fGEN_TCG_F2_sfclass(SHORTCODE) \
    do { \
        TCGv imm = tcg_constant_tl(uiV); \
        gen_helper_sfclass(PdV, tcg_env, RsV, imm); \
    } while (0)
#define fGEN_TCG_F2_sffixupn(SHORTCODE) \
    gen_helper_sffixupn(RdV, tcg_env, RsV, RtV)
#define fGEN_TCG_F2_sffixupd(SHORTCODE) \
    gen_helper_sffixupd(RdV, tcg_env, RsV, RtV)
#define fGEN_TCG_F2_sffixupr(SHORTCODE) \
    gen_helper_sffixupr(RdV, tcg_env, RsV)
#define fGEN_TCG_F2_dfadd(SHORTCODE) \
    gen_helper_dfadd(RddV, tcg_env, RssV, RttV)
#define fGEN_TCG_F2_dfsub(SHORTCODE) \
    gen_helper_dfsub(RddV, tcg_env, RssV, RttV)
#define fGEN_TCG_F2_dfmax(SHORTCODE) \
    gen_helper_dfmax(RddV, tcg_env, RssV, RttV)
#define fGEN_TCG_F2_dfmin(SHORTCODE) \
    gen_helper_dfmin(RddV, tcg_env, RssV, RttV)
#define fGEN_TCG_F2_dfcmpeq(SHORTCODE) \
    gen_helper_dfcmpeq(PdV, tcg_env, RssV, RttV)
#define fGEN_TCG_F2_dfcmpgt(SHORTCODE) \
    gen_helper_dfcmpgt(PdV, tcg_env, RssV, RttV)
#define fGEN_TCG_F2_dfcmpge(SHORTCODE) \
    gen_helper_dfcmpge(PdV, tcg_env, RssV, RttV)
#define fGEN_TCG_F2_dfcmpuo(SHORTCODE) \
    gen_helper_dfcmpuo(PdV, tcg_env, RssV, RttV)
#define fGEN_TCG_F2_dfclass(SHORTCODE) \
    do { \
        TCGv imm = tcg_constant_tl(uiV); \
        gen_helper_dfclass(PdV, tcg_env, RssV, imm); \
    } while (0)
#define fGEN_TCG_F2_sfmpy(SHORTCODE) \
    gen_helper_sfmpy(RdV, tcg_env, RsV, RtV)
#define fGEN_TCG_F2_sffma(SHORTCODE) \
    gen_helper_sffma(RxV, tcg_env, RxV, RsV, RtV)
#define fGEN_TCG_F2_sffma_sc(SHORTCODE) \
    gen_helper_sffma_sc(RxV, tcg_env, RxV, RsV, RtV, PuV)
#define fGEN_TCG_F2_sffms(SHORTCODE) \
    gen_helper_sffms(RxV, tcg_env, RxV, RsV, RtV)
#define fGEN_TCG_F2_sffma_lib(SHORTCODE) \
    gen_helper_sffma_lib(RxV, tcg_env, RxV, RsV, RtV)
#define fGEN_TCG_F2_sffms_lib(SHORTCODE) \
    gen_helper_sffms_lib(RxV, tcg_env, RxV, RsV, RtV)

#define fGEN_TCG_F2_dfmpyfix(SHORTCODE) \
    gen_helper_dfmpyfix(RddV, tcg_env, RssV, RttV)
#define fGEN_TCG_F2_dfmpyhh(SHORTCODE) \
    gen_helper_dfmpyhh(RxxV, tcg_env, RxxV, RssV, RttV)

/* Nothing to do for these in qemu, need to suppress compiler warnings */
#define fGEN_TCG_Y4_l2fetch(SHORTCODE) \
    do { \
        RsV = RsV; \
        RtV = RtV; \
    } while (0)
#define fGEN_TCG_Y5_l2fetch(SHORTCODE) \
    do { \
        RsV = RsV; \
    } while (0)
#define fGEN_TCG_Y2_isync(SHORTCODE) \
    do { } while (0)
#define fGEN_TCG_Y2_barrier(SHORTCODE) \
    do { } while (0)
#define fGEN_TCG_Y2_syncht(SHORTCODE) \
    do { } while (0)
#define fGEN_TCG_Y2_dcfetchbo(SHORTCODE) \
    do { \
        RsV = RsV; \
        uiV = uiV; \
    } while (0)

#define fGEN_TCG_L2_loadw_aq(SHORTCODE)                 SHORTCODE
#define fGEN_TCG_L4_loadd_aq(SHORTCODE)                 SHORTCODE

/* Nothing to do for these in qemu, need to suppress compiler warnings */
#define fGEN_TCG_R6_release_at_vi(SHORTCODE) \
    do { \
        RsV = RsV; \
    } while (0)
#define fGEN_TCG_R6_release_st_vi(SHORTCODE) \
    do { \
        RsV = RsV; \
    } while (0)

#define fGEN_TCG_S2_storew_rl_at_vi(SHORTCODE)          SHORTCODE
#define fGEN_TCG_S4_stored_rl_at_vi(SHORTCODE)          SHORTCODE
#define fGEN_TCG_S2_storew_rl_st_vi(SHORTCODE)          SHORTCODE
#define fGEN_TCG_S4_stored_rl_st_vi(SHORTCODE)          SHORTCODE

#define fGEN_TCG_J2_trap0(SHORTCODE) \
    do { \
        uiV = uiV; \
        tcg_gen_movi_tl(hex_gpr[HEX_REG_PC], ctx->pkt->pc); \
        TCGv excp = tcg_constant_tl(HEX_EVENT_TRAP0); \
        gen_helper_raise_exception(tcg_env, excp); \
    } while (0)
#endif

#define fGEN_TCG_A2_nop(SHORTCODE) do { } while (0)
#define fGEN_TCG_SA1_setin1(SHORTCODE) tcg_gen_movi_tl(RdV, -1)
