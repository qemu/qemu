/*
 *  Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
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
        TCGv tcgv_siV = tcg_const_tl(siV); \
        tcg_gen_mov_tl(EA, RxV); \
        gen_helper_fcircadd(RxV, RxV, tcgv_siV, MuV, \
                            hex_gpr[HEX_REG_CS0 + MuN]); \
        tcg_temp_free(tcgv_siV); \
    } while (0)
#define GET_EA_pcr(SHIFT) \
    do { \
        TCGv ireg = tcg_temp_new(); \
        tcg_gen_mov_tl(EA, RxV); \
        gen_read_ireg(ireg, MuV, (SHIFT)); \
        gen_helper_fcircadd(RxV, RxV, ireg, MuV, hex_gpr[HEX_REG_CS0 + MuN]); \
        tcg_temp_free(ireg); \
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
        gen_helper_fcircadd(RxV, RxV, ireg, MuV, hex_gpr[HEX_REG_CS0 + MuN]); \
        LOAD; \
        tcg_temp_free(ireg); \
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
        tcg_temp_free(tmp); \
        tcg_temp_free(byte); \
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
        tcg_temp_free(tmp); \
        tcg_temp_free(byte); \
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
        tcg_temp_free(tmp); \
        tcg_temp_free_i64(tmp_i64); \
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
        tcg_temp_free(tmp); \
        tcg_temp_free_i64(tmp_i64); \
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
        TCGv LSB = tcg_temp_local_new(); \
        TCGLabel *label = gen_new_label(); \
        GET_EA; \
        PRED;  \
        PRED_LOAD_CANCEL(LSB, EA); \
        tcg_gen_movi_tl(RdV, 0); \
        tcg_gen_brcondi_tl(TCG_COND_EQ, LSB, 0, label); \
            fLOAD(1, SIZE, SIGN, EA, RdV); \
        gen_set_label(label); \
        tcg_temp_free(LSB); \
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
        TCGv LSB = tcg_temp_local_new(); \
        TCGLabel *label = gen_new_label(); \
        GET_EA; \
        PRED;  \
        PRED_LOAD_CANCEL(LSB, EA); \
        tcg_gen_movi_i64(RddV, 0); \
        tcg_gen_brcondi_tl(TCG_COND_EQ, LSB, 0, label); \
            fLOAD(1, 8, u, EA, RddV); \
        gen_set_label(label); \
        tcg_temp_free(LSB); \
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
        TCGv HALF = tcg_temp_new(); \
        TCGv BYTE = tcg_temp_new(); \
        SHORTCODE; \
        tcg_temp_free(HALF); \
        tcg_temp_free(BYTE); \
    } while (0)

#define fGEN_TCG_STORE_pcr(SHIFT, STORE) \
    do { \
        TCGv ireg = tcg_temp_new(); \
        TCGv HALF = tcg_temp_new(); \
        TCGv BYTE = tcg_temp_new(); \
        tcg_gen_mov_tl(EA, RxV); \
        gen_read_ireg(ireg, MuV, SHIFT); \
        gen_helper_fcircadd(RxV, RxV, ireg, MuV, hex_gpr[HEX_REG_CS0 + MuN]); \
        STORE; \
        tcg_temp_free(ireg); \
        tcg_temp_free(HALF); \
        tcg_temp_free(BYTE); \
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

/*
 * Mathematical operations with more than one definition require
 * special handling
 */
#define fGEN_TCG_A5_ACS(SHORTCODE) \
    do { \
        gen_helper_vacsh_pred(PeV, cpu_env, RxxV, RssV, RttV); \
        gen_helper_vacsh_val(RxxV, cpu_env, RxxV, RssV, RttV); \
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
        gen_helper_sfrecipa(tmp, cpu_env, RsV, RtV);  \
        tcg_gen_extrh_i64_i32(RdV, tmp); \
        tcg_gen_extrl_i64_i32(PeV, tmp); \
        tcg_temp_free_i64(tmp); \
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
        gen_helper_sfinvsqrta(tmp, cpu_env, RsV); \
        tcg_gen_extrh_i64_i32(RdV, tmp); \
        tcg_gen_extrl_i64_i32(PeV, tmp); \
        tcg_temp_free_i64(tmp); \
    } while (0)

/*
 * Add or subtract with carry.
 * Predicate register is used as an extra input and output.
 * r5:4 = add(r1:0, r3:2, p1):carry
 */
#define fGEN_TCG_A4_addp_c(SHORTCODE) \
    do { \
        TCGv_i64 carry = tcg_temp_new_i64(); \
        TCGv_i64 zero = tcg_const_i64(0); \
        tcg_gen_extu_i32_i64(carry, PxV); \
        tcg_gen_andi_i64(carry, carry, 1); \
        tcg_gen_add2_i64(RddV, carry, RssV, zero, carry, zero); \
        tcg_gen_add2_i64(RddV, carry, RddV, carry, RttV, zero); \
        tcg_gen_extrl_i64_i32(PxV, carry); \
        gen_8bitsof(PxV, PxV); \
        tcg_temp_free_i64(carry); \
        tcg_temp_free_i64(zero); \
    } while (0)

/* r5:4 = sub(r1:0, r3:2, p1):carry */
#define fGEN_TCG_A4_subp_c(SHORTCODE) \
    do { \
        TCGv_i64 carry = tcg_temp_new_i64(); \
        TCGv_i64 zero = tcg_const_i64(0); \
        TCGv_i64 not_RttV = tcg_temp_new_i64(); \
        tcg_gen_extu_i32_i64(carry, PxV); \
        tcg_gen_andi_i64(carry, carry, 1); \
        tcg_gen_not_i64(not_RttV, RttV); \
        tcg_gen_add2_i64(RddV, carry, RssV, zero, carry, zero); \
        tcg_gen_add2_i64(RddV, carry, RddV, carry, not_RttV, zero); \
        tcg_gen_extrl_i64_i32(PxV, carry); \
        gen_8bitsof(PxV, PxV); \
        tcg_temp_free_i64(carry); \
        tcg_temp_free_i64(zero); \
        tcg_temp_free_i64(not_RttV); \
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
        tcg_temp_free(left); \
        tcg_temp_free(right); \
        tcg_temp_free(tmp); \
    } while (0)

/* Floating point */
#define fGEN_TCG_F2_conv_sf2df(SHORTCODE) \
    gen_helper_conv_sf2df(RddV, cpu_env, RsV)
#define fGEN_TCG_F2_conv_df2sf(SHORTCODE) \
    gen_helper_conv_df2sf(RdV, cpu_env, RssV)
#define fGEN_TCG_F2_conv_uw2sf(SHORTCODE) \
    gen_helper_conv_uw2sf(RdV, cpu_env, RsV)
#define fGEN_TCG_F2_conv_uw2df(SHORTCODE) \
    gen_helper_conv_uw2df(RddV, cpu_env, RsV)
#define fGEN_TCG_F2_conv_w2sf(SHORTCODE) \
    gen_helper_conv_w2sf(RdV, cpu_env, RsV)
#define fGEN_TCG_F2_conv_w2df(SHORTCODE) \
    gen_helper_conv_w2df(RddV, cpu_env, RsV)
#define fGEN_TCG_F2_conv_ud2sf(SHORTCODE) \
    gen_helper_conv_ud2sf(RdV, cpu_env, RssV)
#define fGEN_TCG_F2_conv_ud2df(SHORTCODE) \
    gen_helper_conv_ud2df(RddV, cpu_env, RssV)
#define fGEN_TCG_F2_conv_d2sf(SHORTCODE) \
    gen_helper_conv_d2sf(RdV, cpu_env, RssV)
#define fGEN_TCG_F2_conv_d2df(SHORTCODE) \
    gen_helper_conv_d2df(RddV, cpu_env, RssV)
#define fGEN_TCG_F2_conv_sf2uw(SHORTCODE) \
    gen_helper_conv_sf2uw(RdV, cpu_env, RsV)
#define fGEN_TCG_F2_conv_sf2w(SHORTCODE) \
    gen_helper_conv_sf2w(RdV, cpu_env, RsV)
#define fGEN_TCG_F2_conv_sf2ud(SHORTCODE) \
    gen_helper_conv_sf2ud(RddV, cpu_env, RsV)
#define fGEN_TCG_F2_conv_sf2d(SHORTCODE) \
    gen_helper_conv_sf2d(RddV, cpu_env, RsV)
#define fGEN_TCG_F2_conv_df2uw(SHORTCODE) \
    gen_helper_conv_df2uw(RdV, cpu_env, RssV)
#define fGEN_TCG_F2_conv_df2w(SHORTCODE) \
    gen_helper_conv_df2w(RdV, cpu_env, RssV)
#define fGEN_TCG_F2_conv_df2ud(SHORTCODE) \
    gen_helper_conv_df2ud(RddV, cpu_env, RssV)
#define fGEN_TCG_F2_conv_df2d(SHORTCODE) \
    gen_helper_conv_df2d(RddV, cpu_env, RssV)
#define fGEN_TCG_F2_conv_sf2uw_chop(SHORTCODE) \
    gen_helper_conv_sf2uw_chop(RdV, cpu_env, RsV)
#define fGEN_TCG_F2_conv_sf2w_chop(SHORTCODE) \
    gen_helper_conv_sf2w_chop(RdV, cpu_env, RsV)
#define fGEN_TCG_F2_conv_sf2ud_chop(SHORTCODE) \
    gen_helper_conv_sf2ud_chop(RddV, cpu_env, RsV)
#define fGEN_TCG_F2_conv_sf2d_chop(SHORTCODE) \
    gen_helper_conv_sf2d_chop(RddV, cpu_env, RsV)
#define fGEN_TCG_F2_conv_df2uw_chop(SHORTCODE) \
    gen_helper_conv_df2uw_chop(RdV, cpu_env, RssV)
#define fGEN_TCG_F2_conv_df2w_chop(SHORTCODE) \
    gen_helper_conv_df2w_chop(RdV, cpu_env, RssV)
#define fGEN_TCG_F2_conv_df2ud_chop(SHORTCODE) \
    gen_helper_conv_df2ud_chop(RddV, cpu_env, RssV)
#define fGEN_TCG_F2_conv_df2d_chop(SHORTCODE) \
    gen_helper_conv_df2d_chop(RddV, cpu_env, RssV)
#define fGEN_TCG_F2_sfadd(SHORTCODE) \
    gen_helper_sfadd(RdV, cpu_env, RsV, RtV)
#define fGEN_TCG_F2_sfsub(SHORTCODE) \
    gen_helper_sfsub(RdV, cpu_env, RsV, RtV)
#define fGEN_TCG_F2_sfcmpeq(SHORTCODE) \
    gen_helper_sfcmpeq(PdV, cpu_env, RsV, RtV)
#define fGEN_TCG_F2_sfcmpgt(SHORTCODE) \
    gen_helper_sfcmpgt(PdV, cpu_env, RsV, RtV)
#define fGEN_TCG_F2_sfcmpge(SHORTCODE) \
    gen_helper_sfcmpge(PdV, cpu_env, RsV, RtV)
#define fGEN_TCG_F2_sfcmpuo(SHORTCODE) \
    gen_helper_sfcmpuo(PdV, cpu_env, RsV, RtV)
#define fGEN_TCG_F2_sfmax(SHORTCODE) \
    gen_helper_sfmax(RdV, cpu_env, RsV, RtV)
#define fGEN_TCG_F2_sfmin(SHORTCODE) \
    gen_helper_sfmin(RdV, cpu_env, RsV, RtV)
#define fGEN_TCG_F2_sfclass(SHORTCODE) \
    do { \
        TCGv imm = tcg_constant_tl(uiV); \
        gen_helper_sfclass(PdV, cpu_env, RsV, imm); \
    } while (0)
#define fGEN_TCG_F2_sffixupn(SHORTCODE) \
    gen_helper_sffixupn(RdV, cpu_env, RsV, RtV)
#define fGEN_TCG_F2_sffixupd(SHORTCODE) \
    gen_helper_sffixupd(RdV, cpu_env, RsV, RtV)
#define fGEN_TCG_F2_sffixupr(SHORTCODE) \
    gen_helper_sffixupr(RdV, cpu_env, RsV)
#define fGEN_TCG_F2_dfadd(SHORTCODE) \
    gen_helper_dfadd(RddV, cpu_env, RssV, RttV)
#define fGEN_TCG_F2_dfsub(SHORTCODE) \
    gen_helper_dfsub(RddV, cpu_env, RssV, RttV)
#define fGEN_TCG_F2_dfmax(SHORTCODE) \
    gen_helper_dfmax(RddV, cpu_env, RssV, RttV)
#define fGEN_TCG_F2_dfmin(SHORTCODE) \
    gen_helper_dfmin(RddV, cpu_env, RssV, RttV)
#define fGEN_TCG_F2_dfcmpeq(SHORTCODE) \
    gen_helper_dfcmpeq(PdV, cpu_env, RssV, RttV)
#define fGEN_TCG_F2_dfcmpgt(SHORTCODE) \
    gen_helper_dfcmpgt(PdV, cpu_env, RssV, RttV)
#define fGEN_TCG_F2_dfcmpge(SHORTCODE) \
    gen_helper_dfcmpge(PdV, cpu_env, RssV, RttV)
#define fGEN_TCG_F2_dfcmpuo(SHORTCODE) \
    gen_helper_dfcmpuo(PdV, cpu_env, RssV, RttV)
#define fGEN_TCG_F2_dfclass(SHORTCODE) \
    do { \
        TCGv imm = tcg_constant_tl(uiV); \
        gen_helper_dfclass(PdV, cpu_env, RssV, imm); \
    } while (0)
#define fGEN_TCG_F2_sfmpy(SHORTCODE) \
    gen_helper_sfmpy(RdV, cpu_env, RsV, RtV)
#define fGEN_TCG_F2_sffma(SHORTCODE) \
    gen_helper_sffma(RxV, cpu_env, RxV, RsV, RtV)
#define fGEN_TCG_F2_sffma_sc(SHORTCODE) \
    gen_helper_sffma_sc(RxV, cpu_env, RxV, RsV, RtV, PuV)
#define fGEN_TCG_F2_sffms(SHORTCODE) \
    gen_helper_sffms(RxV, cpu_env, RxV, RsV, RtV)
#define fGEN_TCG_F2_sffma_lib(SHORTCODE) \
    gen_helper_sffma_lib(RxV, cpu_env, RxV, RsV, RtV)
#define fGEN_TCG_F2_sffms_lib(SHORTCODE) \
    gen_helper_sffms_lib(RxV, cpu_env, RxV, RsV, RtV)

#define fGEN_TCG_F2_dfmpyfix(SHORTCODE) \
    gen_helper_dfmpyfix(RddV, cpu_env, RssV, RttV)
#define fGEN_TCG_F2_dfmpyhh(SHORTCODE) \
    gen_helper_dfmpyhh(RxxV, cpu_env, RxxV, RssV, RttV)

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

#endif
