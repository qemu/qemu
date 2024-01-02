/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch vector helper functions.
 *
 * Copyright (c) 2022-2023 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "fpu/softfloat.h"
#include "internals.h"
#include "tcg/tcg.h"
#include "vec.h"
#include "tcg/tcg-gvec-desc.h"

#define DO_ODD_EVEN(NAME, BIT, E1, E2, DO_OP)                        \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc)       \
{                                                                    \
    int i;                                                           \
    VReg *Vd = (VReg *)vd;                                           \
    VReg *Vj = (VReg *)vj;                                           \
    VReg *Vk = (VReg *)vk;                                           \
    typedef __typeof(Vd->E1(0)) TD;                                  \
    int oprsz = simd_oprsz(desc);                                    \
                                                                     \
    for (i = 0; i < oprsz / (BIT / 8); i++) {                        \
        Vd->E1(i) = DO_OP((TD)Vj->E2(2 * i + 1), (TD)Vk->E2(2 * i)); \
    }                                                                \
}

DO_ODD_EVEN(vhaddw_h_b, 16, H, B, DO_ADD)
DO_ODD_EVEN(vhaddw_w_h, 32, W, H, DO_ADD)
DO_ODD_EVEN(vhaddw_d_w, 64, D, W, DO_ADD)

void HELPER(vhaddw_q_d)(void *vd, void *vj, void *vk, uint32_t desc)
{
    int i;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;
    int oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz / 16 ; i++) {
        Vd->Q(i) = int128_add(int128_makes64(Vj->D(2 * i + 1)),
                              int128_makes64(Vk->D(2 * i)));
    }
}

DO_ODD_EVEN(vhsubw_h_b, 16, H, B, DO_SUB)
DO_ODD_EVEN(vhsubw_w_h, 32, W, H, DO_SUB)
DO_ODD_EVEN(vhsubw_d_w, 64, D, W, DO_SUB)

void HELPER(vhsubw_q_d)(void *vd, void *vj, void *vk, uint32_t desc)
{
    int i;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;
    int oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz / 16; i++) {
        Vd->Q(i) = int128_sub(int128_makes64(Vj->D(2 * i + 1)),
                              int128_makes64(Vk->D(2 * i)));
    }
}

DO_ODD_EVEN(vhaddw_hu_bu, 16, UH, UB, DO_ADD)
DO_ODD_EVEN(vhaddw_wu_hu, 32, UW, UH, DO_ADD)
DO_ODD_EVEN(vhaddw_du_wu, 64, UD, UW, DO_ADD)

void HELPER(vhaddw_qu_du)(void *vd, void *vj, void *vk, uint32_t desc)
{
    int i;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;
    int oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz / 16; i ++) {
        Vd->Q(i) = int128_add(int128_make64(Vj->UD(2 * i + 1)),
                              int128_make64(Vk->UD(2 * i)));
    }
}

DO_ODD_EVEN(vhsubw_hu_bu, 16, UH, UB, DO_SUB)
DO_ODD_EVEN(vhsubw_wu_hu, 32, UW, UH, DO_SUB)
DO_ODD_EVEN(vhsubw_du_wu, 64, UD, UW, DO_SUB)

void HELPER(vhsubw_qu_du)(void *vd, void *vj, void *vk, uint32_t desc)
{
    int i;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;
    int oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz / 16; i++) {
        Vd->Q(i) = int128_sub(int128_make64(Vj->UD(2 * i + 1)),
                              int128_make64(Vk->UD(2 * i)));
    }
}

#define DO_EVEN(NAME, BIT, E1, E2, DO_OP)                        \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc)   \
{                                                                \
    int i;                                                       \
    VReg *Vd = (VReg *)vd;                                       \
    VReg *Vj = (VReg *)vj;                                       \
    VReg *Vk = (VReg *)vk;                                       \
    typedef __typeof(Vd->E1(0)) TD;                              \
    int oprsz = simd_oprsz(desc);                                \
                                                                 \
    for (i = 0; i < oprsz / (BIT / 8); i++) {                    \
        Vd->E1(i) = DO_OP((TD)Vj->E2(2 * i) ,(TD)Vk->E2(2 * i)); \
    }                                                            \
}

#define DO_ODD(NAME, BIT, E1, E2, DO_OP)                                 \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc)           \
{                                                                        \
    int i;                                                               \
    VReg *Vd = (VReg *)vd;                                               \
    VReg *Vj = (VReg *)vj;                                               \
    VReg *Vk = (VReg *)vk;                                               \
    typedef __typeof(Vd->E1(0)) TD;                                      \
    int oprsz = simd_oprsz(desc);                                        \
                                                                         \
    for (i = 0; i < oprsz / (BIT / 8); i++) {                            \
        Vd->E1(i) = DO_OP((TD)Vj->E2(2 * i + 1), (TD)Vk->E2(2 * i + 1)); \
    }                                                                    \
}

void HELPER(vaddwev_q_d)(void *vd, void *vj, void *vk, uint32_t desc)
{
    int i;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;
    int oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz / 16; i++) {
        Vd->Q(i) = int128_add(int128_makes64(Vj->D(2 * i)),
                              int128_makes64(Vk->D(2 * i)));
    }
}

DO_EVEN(vaddwev_h_b, 16, H, B, DO_ADD)
DO_EVEN(vaddwev_w_h, 32, W, H, DO_ADD)
DO_EVEN(vaddwev_d_w, 64, D, W, DO_ADD)

void HELPER(vaddwod_q_d)(void *vd, void *vj, void *vk, uint32_t desc)
{
    int i;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;
    int oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz / 16; i++) {
        Vd->Q(i) = int128_add(int128_makes64(Vj->D(2 * i +1)),
                              int128_makes64(Vk->D(2 * i +1)));
    }
}

DO_ODD(vaddwod_h_b, 16, H, B, DO_ADD)
DO_ODD(vaddwod_w_h, 32, W, H, DO_ADD)
DO_ODD(vaddwod_d_w, 64, D, W, DO_ADD)

void HELPER(vsubwev_q_d)(void *vd, void *vj, void *vk, uint32_t desc)
{
    int i;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;
    int oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz / 16; i++) {
        Vd->Q(i) = int128_sub(int128_makes64(Vj->D(2 * i)),
                              int128_makes64(Vk->D(2 * i)));
    }
}

DO_EVEN(vsubwev_h_b, 16, H, B, DO_SUB)
DO_EVEN(vsubwev_w_h, 32, W, H, DO_SUB)
DO_EVEN(vsubwev_d_w, 64, D, W, DO_SUB)

void HELPER(vsubwod_q_d)(void *vd, void *vj, void *vk, uint32_t desc)
{
    int i;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;
    int oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz / 16; i++) {
        Vd->Q(i) = int128_sub(int128_makes64(Vj->D(2 * i + 1)),
                              int128_makes64(Vk->D(2 * i + 1)));
    }
}

DO_ODD(vsubwod_h_b, 16, H, B, DO_SUB)
DO_ODD(vsubwod_w_h, 32, W, H, DO_SUB)
DO_ODD(vsubwod_d_w, 64, D, W, DO_SUB)

void HELPER(vaddwev_q_du)(void *vd, void *vj, void *vk, uint32_t desc)
{
    int i;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;
    int oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz / 16; i++) {
        Vd->Q(i) = int128_add(int128_make64(Vj->UD(2 * i)),
                              int128_make64(Vk->UD(2 * i)));
    }
}

DO_EVEN(vaddwev_h_bu, 16, UH, UB, DO_ADD)
DO_EVEN(vaddwev_w_hu, 32, UW, UH, DO_ADD)
DO_EVEN(vaddwev_d_wu, 64, UD, UW, DO_ADD)

void HELPER(vaddwod_q_du)(void *vd, void *vj, void *vk, uint32_t desc)
{
    int i;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;
    int oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz / 16; i++) {
        Vd->Q(i) = int128_add(int128_make64(Vj->UD(2 * i + 1)),
                              int128_make64(Vk->UD(2 * i + 1)));
    }
}

DO_ODD(vaddwod_h_bu, 16, UH, UB, DO_ADD)
DO_ODD(vaddwod_w_hu, 32, UW, UH, DO_ADD)
DO_ODD(vaddwod_d_wu, 64, UD, UW, DO_ADD)

void HELPER(vsubwev_q_du)(void *vd, void *vj, void *vk, uint32_t desc)
{
    int i;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;
    int oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz / 16; i++) {
        Vd->Q(i) = int128_sub(int128_make64(Vj->UD(2 * i)),
                              int128_make64(Vk->UD(2 * i)));
    }
}

DO_EVEN(vsubwev_h_bu, 16, UH, UB, DO_SUB)
DO_EVEN(vsubwev_w_hu, 32, UW, UH, DO_SUB)
DO_EVEN(vsubwev_d_wu, 64, UD, UW, DO_SUB)

void HELPER(vsubwod_q_du)(void *vd, void *vj, void *vk, uint32_t desc)
{
    int i;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;
    int oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz / 16; i++) {
        Vd->Q(i) = int128_sub(int128_make64(Vj->UD(2 * i + 1)),
                              int128_make64(Vk->UD(2 * i + 1)));
    }
}

DO_ODD(vsubwod_h_bu, 16, UH, UB, DO_SUB)
DO_ODD(vsubwod_w_hu, 32, UW, UH, DO_SUB)
DO_ODD(vsubwod_d_wu, 64, UD, UW, DO_SUB)

#define DO_EVEN_U_S(NAME, BIT, ES1, EU1, ES2, EU2, DO_OP)             \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc)        \
{                                                                     \
    int i;                                                            \
    VReg *Vd = (VReg *)vd;                                            \
    VReg *Vj = (VReg *)vj;                                            \
    VReg *Vk = (VReg *)vk;                                            \
    typedef __typeof(Vd->ES1(0)) TDS;                                 \
    typedef __typeof(Vd->EU1(0)) TDU;                                 \
    int oprsz = simd_oprsz(desc);                                     \
                                                                      \
    for (i = 0; i < oprsz / (BIT / 8); i++) {                         \
        Vd->ES1(i) = DO_OP((TDU)Vj->EU2(2 * i) ,(TDS)Vk->ES2(2 * i)); \
    }                                                                 \
}

#define DO_ODD_U_S(NAME, BIT, ES1, EU1, ES2, EU2, DO_OP)                      \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc)                \
{                                                                             \
    int i;                                                                    \
    VReg *Vd = (VReg *)vd;                                                    \
    VReg *Vj = (VReg *)vj;                                                    \
    VReg *Vk = (VReg *)vk;                                                    \
    typedef __typeof(Vd->ES1(0)) TDS;                                         \
    typedef __typeof(Vd->EU1(0)) TDU;                                         \
    int oprsz = simd_oprsz(desc);                                             \
                                                                              \
    for (i = 0; i < oprsz / (BIT / 8); i++) {                                 \
        Vd->ES1(i) = DO_OP((TDU)Vj->EU2(2 * i + 1), (TDS)Vk->ES2(2 * i + 1)); \
    }                                                                         \
}

void HELPER(vaddwev_q_du_d)(void *vd, void *vj, void *vk, uint32_t desc)
{
    int i;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;
    int oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz / 16; i++) {
        Vd->Q(i) = int128_add(int128_make64(Vj->UD(2 * i)),
                              int128_makes64(Vk->D(2 * i)));
    }
}

DO_EVEN_U_S(vaddwev_h_bu_b, 16, H, UH, B, UB, DO_ADD)
DO_EVEN_U_S(vaddwev_w_hu_h, 32, W, UW, H, UH, DO_ADD)
DO_EVEN_U_S(vaddwev_d_wu_w, 64, D, UD, W, UW, DO_ADD)

void HELPER(vaddwod_q_du_d)(void *vd, void *vj, void *vk, uint32_t desc)
{
    int i;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;
    int oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz / 16; i++) {
        Vd->Q(i) = int128_add(int128_make64(Vj->UD(2 * i + 1)),
                              int128_makes64(Vk->D(2 * i + 1)));
    }
}

DO_ODD_U_S(vaddwod_h_bu_b, 16, H, UH, B, UB, DO_ADD)
DO_ODD_U_S(vaddwod_w_hu_h, 32, W, UW, H, UH, DO_ADD)
DO_ODD_U_S(vaddwod_d_wu_w, 64, D, UD, W, UW, DO_ADD)

#define DO_3OP(NAME, BIT, E, DO_OP)                            \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc) \
{                                                              \
    int i;                                                     \
    VReg *Vd = (VReg *)vd;                                     \
    VReg *Vj = (VReg *)vj;                                     \
    VReg *Vk = (VReg *)vk;                                     \
    int oprsz = simd_oprsz(desc);                              \
                                                               \
    for (i = 0; i < oprsz / (BIT / 8); i++) {                  \
        Vd->E(i) = DO_OP(Vj->E(i), Vk->E(i));                  \
    }                                                          \
}

DO_3OP(vavg_b, 8, B, DO_VAVG)
DO_3OP(vavg_h, 16, H, DO_VAVG)
DO_3OP(vavg_w, 32, W, DO_VAVG)
DO_3OP(vavg_d, 64, D, DO_VAVG)
DO_3OP(vavgr_b, 8, B, DO_VAVGR)
DO_3OP(vavgr_h, 16, H, DO_VAVGR)
DO_3OP(vavgr_w, 32, W, DO_VAVGR)
DO_3OP(vavgr_d, 64, D, DO_VAVGR)
DO_3OP(vavg_bu, 8, UB, DO_VAVG)
DO_3OP(vavg_hu, 16, UH, DO_VAVG)
DO_3OP(vavg_wu, 32, UW, DO_VAVG)
DO_3OP(vavg_du, 64, UD, DO_VAVG)
DO_3OP(vavgr_bu, 8, UB, DO_VAVGR)
DO_3OP(vavgr_hu, 16, UH, DO_VAVGR)
DO_3OP(vavgr_wu, 32, UW, DO_VAVGR)
DO_3OP(vavgr_du, 64, UD, DO_VAVGR)

DO_3OP(vabsd_b, 8, B, DO_VABSD)
DO_3OP(vabsd_h, 16, H, DO_VABSD)
DO_3OP(vabsd_w, 32, W, DO_VABSD)
DO_3OP(vabsd_d, 64, D, DO_VABSD)
DO_3OP(vabsd_bu, 8, UB, DO_VABSD)
DO_3OP(vabsd_hu, 16, UH, DO_VABSD)
DO_3OP(vabsd_wu, 32, UW, DO_VABSD)
DO_3OP(vabsd_du, 64, UD, DO_VABSD)

#define DO_VADDA(NAME, BIT, E)                                 \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc) \
{                                                              \
    int i;                                                     \
    VReg *Vd = (VReg *)vd;                                     \
    VReg *Vj = (VReg *)vj;                                     \
    VReg *Vk = (VReg *)vk;                                     \
    int oprsz = simd_oprsz(desc);                              \
                                                               \
    for (i = 0; i < oprsz / (BIT / 8); i++) {                  \
        Vd->E(i) = DO_VABS(Vj->E(i)) + DO_VABS(Vk->E(i));      \
    }                                                          \
}

DO_VADDA(vadda_b, 8, B)
DO_VADDA(vadda_h, 16, H)
DO_VADDA(vadda_w, 32, W)
DO_VADDA(vadda_d, 64, D)

#define VMINMAXI(NAME, BIT, E, DO_OP)                              \
void HELPER(NAME)(void *vd, void *vj, uint64_t imm, uint32_t desc) \
{                                                                  \
    int i;                                                         \
    VReg *Vd = (VReg *)vd;                                         \
    VReg *Vj = (VReg *)vj;                                         \
    typedef __typeof(Vd->E(0)) TD;                                 \
    int oprsz = simd_oprsz(desc);                                  \
                                                                   \
    for (i = 0; i < oprsz / (BIT / 8); i++) {                      \
        Vd->E(i) = DO_OP(Vj->E(i), (TD)imm);                       \
    }                                                              \
}

VMINMAXI(vmini_b, 8, B, DO_MIN)
VMINMAXI(vmini_h, 16, H, DO_MIN)
VMINMAXI(vmini_w, 32, W, DO_MIN)
VMINMAXI(vmini_d, 64, D, DO_MIN)
VMINMAXI(vmaxi_b, 8, B, DO_MAX)
VMINMAXI(vmaxi_h, 16, H, DO_MAX)
VMINMAXI(vmaxi_w, 32, W, DO_MAX)
VMINMAXI(vmaxi_d, 64, D, DO_MAX)
VMINMAXI(vmini_bu, 8, UB, DO_MIN)
VMINMAXI(vmini_hu, 16, UH, DO_MIN)
VMINMAXI(vmini_wu, 32, UW, DO_MIN)
VMINMAXI(vmini_du, 64, UD, DO_MIN)
VMINMAXI(vmaxi_bu, 8, UB, DO_MAX)
VMINMAXI(vmaxi_hu, 16, UH, DO_MAX)
VMINMAXI(vmaxi_wu, 32, UW, DO_MAX)
VMINMAXI(vmaxi_du, 64, UD, DO_MAX)

#define DO_VMUH(NAME, BIT, E1, E2, DO_OP)                      \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc) \
{                                                              \
    int i;                                                     \
    VReg *Vd = (VReg *)vd;                                     \
    VReg *Vj = (VReg *)vj;                                     \
    VReg *Vk = (VReg *)vk;                                     \
    typedef __typeof(Vd->E1(0)) T;                             \
    int oprsz = simd_oprsz(desc);                              \
                                                               \
    for (i = 0; i < oprsz / (BIT / 8); i++) {                  \
        Vd->E2(i) = ((T)Vj->E2(i)) * ((T)Vk->E2(i)) >> BIT;    \
    }                                                          \
}

void HELPER(vmuh_d)(void *vd, void *vj, void *vk, uint32_t desc)
{
    int i;
    uint64_t l, h;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;
    int oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz / 8; i++) {
        muls64(&l, &h, Vj->D(i), Vk->D(i));
        Vd->D(i) = h;
    }
}

DO_VMUH(vmuh_b, 8, H, B, DO_MUH)
DO_VMUH(vmuh_h, 16, W, H, DO_MUH)
DO_VMUH(vmuh_w, 32, D, W, DO_MUH)

void HELPER(vmuh_du)(void *vd, void *vj, void *vk, uint32_t desc)
{
    int i;
    uint64_t l, h;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;
    int oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz / 8; i++) {
        mulu64(&l, &h, Vj->D(i), Vk->D(i));
        Vd->D(i) = h;
    }
}

DO_VMUH(vmuh_bu, 8, UH, UB, DO_MUH)
DO_VMUH(vmuh_hu, 16, UW, UH, DO_MUH)
DO_VMUH(vmuh_wu, 32, UD, UW, DO_MUH)

DO_EVEN(vmulwev_h_b, 16, H, B, DO_MUL)
DO_EVEN(vmulwev_w_h, 32, W, H, DO_MUL)
DO_EVEN(vmulwev_d_w, 64, D, W, DO_MUL)

DO_ODD(vmulwod_h_b, 16, H, B, DO_MUL)
DO_ODD(vmulwod_w_h, 32, W, H, DO_MUL)
DO_ODD(vmulwod_d_w, 64, D, W, DO_MUL)

DO_EVEN(vmulwev_h_bu, 16, UH, UB, DO_MUL)
DO_EVEN(vmulwev_w_hu, 32, UW, UH, DO_MUL)
DO_EVEN(vmulwev_d_wu, 64, UD, UW, DO_MUL)

DO_ODD(vmulwod_h_bu, 16, UH, UB, DO_MUL)
DO_ODD(vmulwod_w_hu, 32, UW, UH, DO_MUL)
DO_ODD(vmulwod_d_wu, 64, UD, UW, DO_MUL)

DO_EVEN_U_S(vmulwev_h_bu_b, 16, H, UH, B, UB, DO_MUL)
DO_EVEN_U_S(vmulwev_w_hu_h, 32, W, UW, H, UH, DO_MUL)
DO_EVEN_U_S(vmulwev_d_wu_w, 64, D, UD, W, UW, DO_MUL)

DO_ODD_U_S(vmulwod_h_bu_b, 16, H, UH, B, UB, DO_MUL)
DO_ODD_U_S(vmulwod_w_hu_h, 32, W, UW, H, UH, DO_MUL)
DO_ODD_U_S(vmulwod_d_wu_w, 64, D, UD, W, UW, DO_MUL)

#define VMADDSUB(NAME, BIT, E, DO_OP)                          \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc) \
{                                                              \
    int i;                                                     \
    VReg *Vd = (VReg *)vd;                                     \
    VReg *Vj = (VReg *)vj;                                     \
    VReg *Vk = (VReg *)vk;                                     \
    int oprsz = simd_oprsz(desc);                              \
                                                               \
    for (i = 0; i < oprsz / (BIT / 8); i++) {                  \
        Vd->E(i) = DO_OP(Vd->E(i), Vj->E(i) ,Vk->E(i));        \
    }                                                          \
}

VMADDSUB(vmadd_b, 8, B, DO_MADD)
VMADDSUB(vmadd_h, 16, H, DO_MADD)
VMADDSUB(vmadd_w, 32, W, DO_MADD)
VMADDSUB(vmadd_d, 64, D, DO_MADD)
VMADDSUB(vmsub_b, 8, B, DO_MSUB)
VMADDSUB(vmsub_h, 16, H, DO_MSUB)
VMADDSUB(vmsub_w, 32, W, DO_MSUB)
VMADDSUB(vmsub_d, 64, D, DO_MSUB)

#define VMADDWEV(NAME, BIT, E1, E2, DO_OP)                        \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc)    \
{                                                                 \
    int i;                                                        \
    VReg *Vd = (VReg *)vd;                                        \
    VReg *Vj = (VReg *)vj;                                        \
    VReg *Vk = (VReg *)vk;                                        \
    typedef __typeof(Vd->E1(0)) TD;                               \
    int oprsz = simd_oprsz(desc);                                 \
                                                                  \
    for (i = 0; i < oprsz / (BIT / 8); i++) {                     \
        Vd->E1(i) += DO_OP((TD)Vj->E2(2 * i), (TD)Vk->E2(2 * i)); \
    }                                                             \
}

VMADDWEV(vmaddwev_h_b, 16, H, B, DO_MUL)
VMADDWEV(vmaddwev_w_h, 32, W, H, DO_MUL)
VMADDWEV(vmaddwev_d_w, 64, D, W, DO_MUL)
VMADDWEV(vmaddwev_h_bu, 16, UH, UB, DO_MUL)
VMADDWEV(vmaddwev_w_hu, 32, UW, UH, DO_MUL)
VMADDWEV(vmaddwev_d_wu, 64, UD, UW, DO_MUL)

#define VMADDWOD(NAME, BIT, E1, E2, DO_OP)                     \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc) \
{                                                              \
    int i;                                                     \
    VReg *Vd = (VReg *)vd;                                     \
    VReg *Vj = (VReg *)vj;                                     \
    VReg *Vk = (VReg *)vk;                                     \
    typedef __typeof(Vd->E1(0)) TD;                            \
    int oprsz = simd_oprsz(desc);                              \
                                                               \
    for (i = 0; i < oprsz / (BIT / 8); i++) {                  \
        Vd->E1(i) += DO_OP((TD)Vj->E2(2 * i + 1),              \
                           (TD)Vk->E2(2 * i + 1));             \
    }                                                          \
}

VMADDWOD(vmaddwod_h_b, 16, H, B, DO_MUL)
VMADDWOD(vmaddwod_w_h, 32, W, H, DO_MUL)
VMADDWOD(vmaddwod_d_w, 64, D, W, DO_MUL)
VMADDWOD(vmaddwod_h_bu, 16,  UH, UB, DO_MUL)
VMADDWOD(vmaddwod_w_hu, 32,  UW, UH, DO_MUL)
VMADDWOD(vmaddwod_d_wu, 64,  UD, UW, DO_MUL)

#define VMADDWEV_U_S(NAME, BIT, ES1, EU1, ES2, EU2, DO_OP)     \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc) \
{                                                              \
    int i;                                                     \
    VReg *Vd = (VReg *)vd;                                     \
    VReg *Vj = (VReg *)vj;                                     \
    VReg *Vk = (VReg *)vk;                                     \
    typedef __typeof(Vd->ES1(0)) TS1;                          \
    typedef __typeof(Vd->EU1(0)) TU1;                          \
    int oprsz = simd_oprsz(desc);                              \
                                                               \
    for (i = 0; i < oprsz / (BIT / 8); i++) {                  \
        Vd->ES1(i) += DO_OP((TU1)Vj->EU2(2 * i),               \
                            (TS1)Vk->ES2(2 * i));              \
    }                                                          \
}

VMADDWEV_U_S(vmaddwev_h_bu_b, 16, H, UH, B, UB, DO_MUL)
VMADDWEV_U_S(vmaddwev_w_hu_h, 32, W, UW, H, UH, DO_MUL)
VMADDWEV_U_S(vmaddwev_d_wu_w, 64, D, UD, W, UW, DO_MUL)

#define VMADDWOD_U_S(NAME, BIT, ES1, EU1, ES2, EU2, DO_OP)     \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc) \
{                                                              \
    int i;                                                     \
    VReg *Vd = (VReg *)vd;                                     \
    VReg *Vj = (VReg *)vj;                                     \
    VReg *Vk = (VReg *)vk;                                     \
    typedef __typeof(Vd->ES1(0)) TS1;                          \
    typedef __typeof(Vd->EU1(0)) TU1;                          \
    int oprsz = simd_oprsz(desc);                              \
                                                               \
    for (i = 0; i < oprsz / (BIT / 8); i++) {                  \
        Vd->ES1(i) += DO_OP((TU1)Vj->EU2(2 * i + 1),           \
                            (TS1)Vk->ES2(2 * i + 1));          \
    }                                                          \
}

VMADDWOD_U_S(vmaddwod_h_bu_b, 16, H, UH, B, UB, DO_MUL)
VMADDWOD_U_S(vmaddwod_w_hu_h, 32, W, UW, H, UH, DO_MUL)
VMADDWOD_U_S(vmaddwod_d_wu_w, 64, D, UD, W, UW, DO_MUL)

#define VDIV(NAME, BIT, E, DO_OP)                              \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc) \
{                                                              \
    int i;                                                     \
    VReg *Vd = (VReg *)vd;                                     \
    VReg *Vj = (VReg *)vj;                                     \
    VReg *Vk = (VReg *)vk;                                     \
    int oprsz = simd_oprsz(desc);                              \
                                                               \
    for (i = 0; i < oprsz / (BIT / 8); i++) {                  \
        Vd->E(i) = DO_OP(Vj->E(i), Vk->E(i));                  \
    }                                                          \
}

VDIV(vdiv_b, 8, B, DO_DIV)
VDIV(vdiv_h, 16, H, DO_DIV)
VDIV(vdiv_w, 32, W, DO_DIV)
VDIV(vdiv_d, 64, D, DO_DIV)
VDIV(vdiv_bu, 8, UB, DO_DIVU)
VDIV(vdiv_hu, 16, UH, DO_DIVU)
VDIV(vdiv_wu, 32, UW, DO_DIVU)
VDIV(vdiv_du, 64, UD, DO_DIVU)
VDIV(vmod_b, 8, B, DO_REM)
VDIV(vmod_h, 16, H, DO_REM)
VDIV(vmod_w, 32, W, DO_REM)
VDIV(vmod_d, 64, D, DO_REM)
VDIV(vmod_bu, 8, UB, DO_REMU)
VDIV(vmod_hu, 16, UH, DO_REMU)
VDIV(vmod_wu, 32, UW, DO_REMU)
VDIV(vmod_du, 64, UD, DO_REMU)

#define VSAT_S(NAME, BIT, E)                                       \
void HELPER(NAME)(void *vd, void *vj, uint64_t max, uint32_t desc) \
{                                                                  \
    int i;                                                         \
    VReg *Vd = (VReg *)vd;                                         \
    VReg *Vj = (VReg *)vj;                                         \
    typedef __typeof(Vd->E(0)) TD;                                 \
    int oprsz = simd_oprsz(desc);                                  \
                                                                   \
    for (i = 0; i < oprsz / (BIT / 8); i++) {                      \
        Vd->E(i) = Vj->E(i) > (TD)max ? (TD)max :                  \
                   Vj->E(i) < (TD)~max ? (TD)~max: Vj->E(i);       \
    }                                                              \
}

VSAT_S(vsat_b, 8, B)
VSAT_S(vsat_h, 16, H)
VSAT_S(vsat_w, 32, W)
VSAT_S(vsat_d, 64, D)

#define VSAT_U(NAME, BIT, E)                                       \
void HELPER(NAME)(void *vd, void *vj, uint64_t max, uint32_t desc) \
{                                                                  \
    int i;                                                         \
    VReg *Vd = (VReg *)vd;                                         \
    VReg *Vj = (VReg *)vj;                                         \
    typedef __typeof(Vd->E(0)) TD;                                 \
    int oprsz = simd_oprsz(desc);                                  \
                                                                   \
    for (i = 0; i < oprsz / (BIT / 8); i++) {                      \
        Vd->E(i) = Vj->E(i) > (TD)max ? (TD)max : Vj->E(i);        \
    }                                                              \
}

VSAT_U(vsat_bu, 8, UB)
VSAT_U(vsat_hu, 16, UH)
VSAT_U(vsat_wu, 32, UW)
VSAT_U(vsat_du, 64, UD)

#define VEXTH(NAME, BIT, E1, E2)                                 \
void HELPER(NAME)(void *vd, void *vj, uint32_t desc)             \
{                                                                \
    int i, j, ofs;                                               \
    VReg *Vd = (VReg *)vd;                                       \
    VReg *Vj = (VReg *)vj;                                       \
    int oprsz = simd_oprsz(desc);                                \
                                                                 \
    ofs = LSX_LEN / BIT;                                         \
    for (i = 0; i < oprsz / 16; i++) {                           \
        for (j = 0; j < ofs; j++) {                              \
            Vd->E1(j + i * ofs) = Vj->E2(j + ofs + ofs * 2 * i); \
        }                                                        \
    }                                                            \
}

void HELPER(vexth_q_d)(void *vd, void *vj, uint32_t desc)
{
    int i;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    int oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz / 16; i++) {
        Vd->Q(i) = int128_makes64(Vj->D(2 * i + 1));
    }
}

void HELPER(vexth_qu_du)(void *vd, void *vj, uint32_t desc)
{
    int i;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    int oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz / 16; i++) {
        Vd->Q(i) = int128_make64(Vj->UD(2 * i + 1));
    }
}

VEXTH(vexth_h_b, 16, H, B)
VEXTH(vexth_w_h, 32, W, H)
VEXTH(vexth_d_w, 64, D, W)
VEXTH(vexth_hu_bu, 16, UH, UB)
VEXTH(vexth_wu_hu, 32, UW, UH)
VEXTH(vexth_du_wu, 64, UD, UW)

#define VEXT2XV(NAME, BIT, E1, E2)                   \
void HELPER(NAME)(void *vd, void *vj, uint32_t desc) \
{                                                    \
    int i;                                           \
    VReg temp = {};                                  \
    VReg *Vd = (VReg *)vd;                           \
    VReg *Vj = (VReg *)vj;                           \
    int oprsz = simd_oprsz(desc);                    \
                                                     \
    for (i = 0; i < oprsz / (BIT / 8); i++) {        \
        temp.E1(i) = Vj->E2(i);                      \
    }                                                \
    *Vd = temp;                                      \
}

VEXT2XV(vext2xv_h_b, 16, H, B)
VEXT2XV(vext2xv_w_b, 32, W, B)
VEXT2XV(vext2xv_d_b, 64, D, B)
VEXT2XV(vext2xv_w_h, 32, W, H)
VEXT2XV(vext2xv_d_h, 64, D, H)
VEXT2XV(vext2xv_d_w, 64, D, W)
VEXT2XV(vext2xv_hu_bu, 16, UH, UB)
VEXT2XV(vext2xv_wu_bu, 32, UW, UB)
VEXT2XV(vext2xv_du_bu, 64, UD, UB)
VEXT2XV(vext2xv_wu_hu, 32, UW, UH)
VEXT2XV(vext2xv_du_hu, 64, UD, UH)
VEXT2XV(vext2xv_du_wu, 64, UD, UW)

DO_3OP(vsigncov_b, 8, B, DO_SIGNCOV)
DO_3OP(vsigncov_h, 16, H, DO_SIGNCOV)
DO_3OP(vsigncov_w, 32, W, DO_SIGNCOV)
DO_3OP(vsigncov_d, 64, D, DO_SIGNCOV)

static uint64_t do_vmskltz_b(int64_t val)
{
    uint64_t m = 0x8080808080808080ULL;
    uint64_t c =  val & m;
    c |= c << 7;
    c |= c << 14;
    c |= c << 28;
    return c >> 56;
}

void HELPER(vmskltz_b)(void *vd, void *vj, uint32_t desc)
{
    int i;
    uint16_t temp = 0;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    int oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz / 16; i++) {
        temp = 0;
        temp = do_vmskltz_b(Vj->D(2 * i));
        temp |= (do_vmskltz_b(Vj->D(2 * i  + 1)) << 8);
        Vd->D(2 * i) = temp;
        Vd->D(2 * i + 1) = 0;
    }
}

static uint64_t do_vmskltz_h(int64_t val)
{
    uint64_t m = 0x8000800080008000ULL;
    uint64_t c =  val & m;
    c |= c << 15;
    c |= c << 30;
    return c >> 60;
}

void HELPER(vmskltz_h)(void *vd, void *vj, uint32_t desc)
{
    int i;
    uint16_t temp = 0;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    int oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz / 16; i++) {
        temp = 0;
        temp = do_vmskltz_h(Vj->D(2 * i));
        temp |= (do_vmskltz_h(Vj->D(2 * i + 1)) << 4);
        Vd->D(2 * i) = temp;
        Vd->D(2 * i + 1) = 0;
    }
}

static uint64_t do_vmskltz_w(int64_t val)
{
    uint64_t m = 0x8000000080000000ULL;
    uint64_t c =  val & m;
    c |= c << 31;
    return c >> 62;
}

void HELPER(vmskltz_w)(void *vd, void *vj, uint32_t desc)
{
    int i;
    uint16_t temp = 0;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    int oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz / 16; i++) {
        temp = 0;
        temp = do_vmskltz_w(Vj->D(2 * i));
        temp |= (do_vmskltz_w(Vj->D(2 * i + 1)) << 2);
        Vd->D(2 * i) = temp;
        Vd->D(2 * i + 1) = 0;
    }
}

static uint64_t do_vmskltz_d(int64_t val)
{
    return (uint64_t)val >> 63;
}
void HELPER(vmskltz_d)(void *vd, void *vj, uint32_t desc)
{
    int i;
    uint16_t temp = 0;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    int oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz / 16; i++) {
        temp = 0;
        temp = do_vmskltz_d(Vj->D(2 * i));
        temp |= (do_vmskltz_d(Vj->D(2 * i + 1)) << 1);
        Vd->D(2 * i) = temp;
        Vd->D(2 * i + 1) = 0;
    }
}

void HELPER(vmskgez_b)(void *vd, void *vj, uint32_t desc)
{
    int i;
    uint16_t temp = 0;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    int oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz / 16; i++) {
        temp = 0;
        temp =  do_vmskltz_b(Vj->D(2 * i));
        temp |= (do_vmskltz_b(Vj->D(2 * i + 1)) << 8);
        Vd->D(2 * i) = (uint16_t)(~temp);
        Vd->D(2 * i + 1) = 0;
    }
}

static uint64_t do_vmskez_b(uint64_t a)
{
    uint64_t m = 0x7f7f7f7f7f7f7f7fULL;
    uint64_t c = ~(((a & m) + m) | a | m);
    c |= c << 7;
    c |= c << 14;
    c |= c << 28;
    return c >> 56;
}

void HELPER(vmsknz_b)(void *vd, void *vj, uint32_t desc)
{
    int i;
    uint16_t temp = 0;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    int oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz / 16; i++) {
        temp = 0;
        temp = do_vmskez_b(Vj->D(2 * i));
        temp |= (do_vmskez_b(Vj->D(2 * i + 1)) << 8);
        Vd->D(2 * i) = (uint16_t)(~temp);
        Vd->D(2 * i + 1) = 0;
    }
}

void HELPER(vnori_b)(void *vd, void *vj, uint64_t imm, uint32_t desc)
{
    int i;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;

    for (i = 0; i < simd_oprsz(desc); i++) {
        Vd->B(i) = ~(Vj->B(i) | (uint8_t)imm);
    }
}

#define VSLLWIL(NAME, BIT, E1, E2)                                             \
void HELPER(NAME)(void *vd, void *vj, uint64_t imm, uint32_t desc)             \
{                                                                              \
    int i, j, ofs;                                                             \
    VReg temp = {};                                                            \
    VReg *Vd = (VReg *)vd;                                                     \
    VReg *Vj = (VReg *)vj;                                                     \
    int oprsz = simd_oprsz(desc);                                              \
    typedef __typeof(temp.E1(0)) TD;                                           \
                                                                               \
    ofs = LSX_LEN / BIT;                                                       \
    for (i = 0; i < oprsz / 16; i++) {                                         \
        for (j = 0; j < ofs; j++) {                                            \
            temp.E1(j + ofs * i) = (TD)Vj->E2(j + ofs * 2 * i) << (imm % BIT); \
        }                                                                      \
    }                                                                          \
    *Vd = temp;                                                                \
}


void HELPER(vextl_q_d)(void *vd, void *vj, uint32_t desc)
{
    int i;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    int oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz / 16; i++) {
        Vd->Q(i) = int128_makes64(Vj->D(2 * i));
    }
}

void HELPER(vextl_qu_du)(void *vd, void *vj, uint32_t desc)
{
    int i;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    int oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz / 16; i++) {
        Vd->Q(i) = int128_make64(Vj->UD(2 * i));
    }
}

VSLLWIL(vsllwil_h_b, 16, H, B)
VSLLWIL(vsllwil_w_h, 32, W, H)
VSLLWIL(vsllwil_d_w, 64, D, W)
VSLLWIL(vsllwil_hu_bu, 16, UH, UB)
VSLLWIL(vsllwil_wu_hu, 32, UW, UH)
VSLLWIL(vsllwil_du_wu, 64, UD, UW)

#define do_vsrlr(E, T)                                  \
static T do_vsrlr_ ##E(T s1, int sh)                    \
{                                                       \
    if (sh == 0) {                                      \
        return s1;                                      \
    } else {                                            \
        return  (s1 >> sh)  + ((s1 >> (sh - 1)) & 0x1); \
    }                                                   \
}

do_vsrlr(B, uint8_t)
do_vsrlr(H, uint16_t)
do_vsrlr(W, uint32_t)
do_vsrlr(D, uint64_t)

#define VSRLR(NAME, BIT, T, E)                                  \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc)  \
{                                                               \
    int i;                                                      \
    VReg *Vd = (VReg *)vd;                                      \
    VReg *Vj = (VReg *)vj;                                      \
    VReg *Vk = (VReg *)vk;                                      \
    int oprsz = simd_oprsz(desc);                               \
                                                                \
    for (i = 0; i < oprsz / (BIT / 8); i++) {                   \
        Vd->E(i) = do_vsrlr_ ## E(Vj->E(i), ((T)Vk->E(i))%BIT); \
    }                                                           \
}

VSRLR(vsrlr_b, 8,  uint8_t, B)
VSRLR(vsrlr_h, 16, uint16_t, H)
VSRLR(vsrlr_w, 32, uint32_t, W)
VSRLR(vsrlr_d, 64, uint64_t, D)

#define VSRLRI(NAME, BIT, E)                                       \
void HELPER(NAME)(void *vd, void *vj, uint64_t imm, uint32_t desc) \
{                                                                  \
    int i;                                                         \
    VReg *Vd = (VReg *)vd;                                         \
    VReg *Vj = (VReg *)vj;                                         \
    int oprsz = simd_oprsz(desc);                                  \
                                                                   \
    for (i = 0; i < oprsz / (BIT / 8); i++) {                      \
        Vd->E(i) = do_vsrlr_ ## E(Vj->E(i), imm);                  \
    }                                                              \
}

VSRLRI(vsrlri_b, 8, B)
VSRLRI(vsrlri_h, 16, H)
VSRLRI(vsrlri_w, 32, W)
VSRLRI(vsrlri_d, 64, D)

#define do_vsrar(E, T)                                  \
static T do_vsrar_ ##E(T s1, int sh)                    \
{                                                       \
    if (sh == 0) {                                      \
        return s1;                                      \
    } else {                                            \
        return  (s1 >> sh)  + ((s1 >> (sh - 1)) & 0x1); \
    }                                                   \
}

do_vsrar(B, int8_t)
do_vsrar(H, int16_t)
do_vsrar(W, int32_t)
do_vsrar(D, int64_t)

#define VSRAR(NAME, BIT, T, E)                                  \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc)  \
{                                                               \
    int i;                                                      \
    VReg *Vd = (VReg *)vd;                                      \
    VReg *Vj = (VReg *)vj;                                      \
    VReg *Vk = (VReg *)vk;                                      \
    int oprsz = simd_oprsz(desc);                               \
                                                                \
    for (i = 0; i < oprsz / (BIT / 8); i++) {                   \
        Vd->E(i) = do_vsrar_ ## E(Vj->E(i), ((T)Vk->E(i))%BIT); \
    }                                                           \
}

VSRAR(vsrar_b, 8,  uint8_t, B)
VSRAR(vsrar_h, 16, uint16_t, H)
VSRAR(vsrar_w, 32, uint32_t, W)
VSRAR(vsrar_d, 64, uint64_t, D)

#define VSRARI(NAME, BIT, E)                                       \
void HELPER(NAME)(void *vd, void *vj, uint64_t imm, uint32_t desc) \
{                                                                  \
    int i;                                                         \
    VReg *Vd = (VReg *)vd;                                         \
    VReg *Vj = (VReg *)vj;                                         \
    int oprsz = simd_oprsz(desc);                                  \
                                                                   \
    for (i = 0; i < oprsz / (BIT / 8); i++) {                      \
        Vd->E(i) = do_vsrar_ ## E(Vj->E(i), imm);                  \
    }                                                              \
}

VSRARI(vsrari_b, 8, B)
VSRARI(vsrari_h, 16, H)
VSRARI(vsrari_w, 32, W)
VSRARI(vsrari_d, 64, D)

#define VSRLN(NAME, BIT, E1, E2)                                          \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc)            \
{                                                                         \
    int i, j, ofs;                                                        \
    VReg *Vd = (VReg *)vd;                                                \
    VReg *Vj = (VReg *)vj;                                                \
    VReg *Vk = (VReg *)vk;                                                \
    int oprsz = simd_oprsz(desc);                                         \
                                                                          \
    ofs = LSX_LEN / BIT;                                                  \
    for (i = 0; i < oprsz / 16; i++) {                                    \
        for (j = 0; j < ofs; j++) {                                       \
            Vd->E1(j + ofs * 2 * i) = R_SHIFT(Vj->E2(j + ofs * i),        \
                                              Vk->E2(j + ofs * i) % BIT); \
        }                                                                 \
        Vd->D(2 * i + 1) = 0;                                             \
    }                                                                     \
}

VSRLN(vsrln_b_h, 16, B, UH)
VSRLN(vsrln_h_w, 32, H, UW)
VSRLN(vsrln_w_d, 64, W, UD)

#define VSRAN(NAME, BIT, E1, E2, E3)                                      \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc)            \
{                                                                         \
    int i, j, ofs;                                                        \
    VReg *Vd = (VReg *)vd;                                                \
    VReg *Vj = (VReg *)vj;                                                \
    VReg *Vk = (VReg *)vk;                                                \
    int oprsz = simd_oprsz(desc);                                         \
                                                                          \
    ofs = LSX_LEN / BIT;                                                  \
    for (i = 0; i < oprsz / 16; i++) {                                    \
        for (j = 0; j < ofs; j++) {                                       \
            Vd->E1(j + ofs * 2 * i) = R_SHIFT(Vj->E2(j + ofs * i),        \
                                              Vk->E3(j + ofs * i) % BIT); \
        }                                                                 \
        Vd->D(2 * i + 1) = 0;                                             \
    }                                                                     \
}

VSRAN(vsran_b_h, 16, B, H, UH)
VSRAN(vsran_h_w, 32, H, W, UW)
VSRAN(vsran_w_d, 64, W, D, UD)

#define VSRLNI(NAME, BIT, E1, E2)                                         \
void HELPER(NAME)(void *vd, void *vj, uint64_t imm, uint32_t desc)        \
{                                                                         \
    int i, j, ofs;                                                        \
    VReg temp = {};                                                       \
    VReg *Vd = (VReg *)vd;                                                \
    VReg *Vj = (VReg *)vj;                                                \
    int oprsz = simd_oprsz(desc);                                         \
                                                                          \
    ofs = LSX_LEN / BIT;                                                  \
    for (i = 0; i < oprsz / 16; i++) {                                    \
        for (j = 0; j < ofs; j++) {                                       \
            temp.E1(j + ofs * 2 * i) = R_SHIFT(Vj->E2(j + ofs * i), imm); \
            temp.E1(j + ofs * (2 * i + 1)) = R_SHIFT(Vd->E2(j + ofs * i), \
                                                     imm);                \
        }                                                                 \
    }                                                                     \
    *Vd = temp;                                                           \
}

void HELPER(vsrlni_d_q)(void *vd, void *vj, uint64_t imm, uint32_t desc)
{
    int i;
    VReg temp = {};
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;

    for (i = 0; i < 2; i++) {
        temp.D(2 * i) = int128_getlo(int128_urshift(Vj->Q(i), imm % 128));
        temp.D(2 * i +1) = int128_getlo(int128_urshift(Vd->Q(i), imm % 128));
    }
    *Vd = temp;
}

VSRLNI(vsrlni_b_h, 16, B, UH)
VSRLNI(vsrlni_h_w, 32, H, UW)
VSRLNI(vsrlni_w_d, 64, W, UD)

#define VSRANI(NAME, BIT, E1, E2)                                         \
void HELPER(NAME)(void *vd, void *vj, uint64_t imm, uint32_t desc)        \
{                                                                         \
    int i, j, ofs;                                                        \
    VReg temp = {};                                                       \
    VReg *Vd = (VReg *)vd;                                                \
    VReg *Vj = (VReg *)vj;                                                \
    int oprsz = simd_oprsz(desc);                                         \
                                                                          \
    ofs = LSX_LEN / BIT;                                                  \
    for (i = 0; i < oprsz / 16; i++) {                                    \
        for (j = 0; j < ofs; j++) {                                       \
            temp.E1(j + ofs * 2 * i) = R_SHIFT(Vj->E2(j + ofs * i), imm); \
            temp.E1(j + ofs * (2 * i + 1)) = R_SHIFT(Vd->E2(j + ofs * i), \
                                                     imm);                \
        }                                                                 \
    }                                                                     \
    *Vd = temp;                                                           \
}

void HELPER(vsrani_d_q)(void *vd, void *vj, uint64_t imm, uint32_t desc)
{
    int i;
    VReg temp = {};
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;

    for (i = 0; i < 2; i++) {
        temp.D(2 * i) = int128_getlo(int128_rshift(Vj->Q(i), imm % 128));
        temp.D(2 * i + 1) = int128_getlo(int128_rshift(Vd->Q(i), imm % 128));
    }
    *Vd = temp;
}

VSRANI(vsrani_b_h, 16, B, H)
VSRANI(vsrani_h_w, 32, H, W)
VSRANI(vsrani_w_d, 64, W, D)

#define VSRLRN(NAME, BIT, E1, E2, E3)                                      \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc)             \
{                                                                          \
    int i, j, ofs;                                                         \
    VReg *Vd = (VReg *)vd;                                                 \
    VReg *Vj = (VReg *)vj;                                                 \
    VReg *Vk = (VReg *)vk;                                                 \
    int oprsz = simd_oprsz(desc);                                          \
                                                                           \
    ofs = LSX_LEN / BIT;                                                   \
    for (i = 0; i < oprsz / 16; i++) {                                     \
        for (j = 0; j < ofs; j++) {                                        \
            Vd->E1(j + ofs * 2 * i) = do_vsrlr_ ##E2(Vj->E2(j + ofs * i),  \
                                               Vk->E3(j + ofs * i) % BIT); \
        }                                                                  \
        Vd->D(2 * i + 1) = 0;                                              \
    }                                                                      \
}

VSRLRN(vsrlrn_b_h, 16, B, H, UH)
VSRLRN(vsrlrn_h_w, 32, H, W, UW)
VSRLRN(vsrlrn_w_d, 64, W, D, UD)

#define VSRARN(NAME, BIT, E1, E2, E3)                                       \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc)              \
{                                                                           \
    int i, j, ofs;                                                          \
    VReg *Vd = (VReg *)vd;                                                  \
    VReg *Vj = (VReg *)vj;                                                  \
    VReg *Vk = (VReg *)vk;                                                  \
    int oprsz = simd_oprsz(desc);                                           \
                                                                            \
    ofs = LSX_LEN / BIT;                                                    \
    for (i = 0; i < oprsz / 16; i++) {                                      \
        for (j = 0; j < ofs; j++) {                                         \
            Vd->E1(j + ofs * 2 * i) = do_vsrar_ ## E2(Vj->E2(j + ofs * i),  \
                                                Vk->E3(j + ofs * i) % BIT); \
        }                                                                   \
        Vd->D(2 * i + 1) = 0;                                               \
    }                                                                       \
}

VSRARN(vsrarn_b_h, 16, B, H, UH)
VSRARN(vsrarn_h_w, 32, H, W, UW)
VSRARN(vsrarn_w_d, 64, W, D, UD)

#define VSRLRNI(NAME, BIT, E1, E2)                                                \
void HELPER(NAME)(void *vd, void *vj, uint64_t imm, uint32_t desc)                \
{                                                                                 \
    int i, j, ofs;                                                                \
    VReg temp = {};                                                               \
    VReg *Vd = (VReg *)vd;                                                        \
    VReg *Vj = (VReg *)vj;                                                        \
    int oprsz = simd_oprsz(desc);                                                 \
                                                                                  \
    ofs = LSX_LEN / BIT;                                                          \
    for (i = 0; i < oprsz / 16; i++) {                                            \
        for (j = 0; j < ofs; j++) {                                               \
            temp.E1(j + ofs * 2 * i) = do_vsrlr_ ## E2(Vj->E2(j + ofs * i), imm); \
            temp.E1(j + ofs * (2 * i + 1)) = do_vsrlr_ ## E2(Vd->E2(j + ofs * i), \
                                                                 imm);            \
        }                                                                         \
    }                                                                             \
    *Vd = temp;                                                                   \
}

void HELPER(vsrlrni_d_q)(void *vd, void *vj, uint64_t imm, uint32_t desc)
{
    int i;
    VReg temp = {};
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    Int128 r[4];
    int oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz / 16; i++) {
        if (imm == 0) {
            temp.D(2 * i) = int128_getlo(Vj->Q(i));
            temp.D(2 * i + 1) = int128_getlo(Vd->Q(i));
        } else {
            r[2 * i] = int128_and(int128_urshift(Vj->Q(i), (imm - 1)),
                                  int128_one());
            r[2 * i + 1] = int128_and(int128_urshift(Vd->Q(i), (imm - 1)),
                                      int128_one());
            temp.D(2 * i) = int128_getlo(int128_add(int128_urshift(Vj->Q(i),
                                                    imm), r[2 * i]));
            temp.D(2 * i + 1) = int128_getlo(int128_add(int128_urshift(Vd->Q(i),
                                                        imm), r[ 2 * i + 1]));
        }
    }
    *Vd = temp;
}

VSRLRNI(vsrlrni_b_h, 16, B, H)
VSRLRNI(vsrlrni_h_w, 32, H, W)
VSRLRNI(vsrlrni_w_d, 64, W, D)

#define VSRARNI(NAME, BIT, E1, E2)                                                \
void HELPER(NAME)(void *vd, void *vj, uint64_t imm, uint32_t desc)                \
{                                                                                 \
    int i, j, ofs;                                                                \
    VReg temp = {};                                                               \
    VReg *Vd = (VReg *)vd;                                                        \
    VReg *Vj = (VReg *)vj;                                                        \
    int oprsz = simd_oprsz(desc);                                                 \
                                                                                  \
    ofs = LSX_LEN / BIT;                                                          \
    for (i = 0; i < oprsz / 16; i++) {                                            \
        for (j = 0; j < ofs; j++) {                                               \
            temp.E1(j + ofs * 2 * i) = do_vsrar_ ## E2(Vj->E2(j + ofs * i), imm); \
            temp.E1(j + ofs * (2 * i + 1)) = do_vsrar_ ## E2(Vd->E2(j + ofs * i), \
                                                             imm);                \
        }                                                                         \
    }                                                                             \
    *Vd = temp;                                                                   \
}

void HELPER(vsrarni_d_q)(void *vd, void *vj, uint64_t imm, uint32_t desc)
{
    int i;
    VReg temp = {};
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    Int128 r[4];
    int oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz / 16; i++) {
        if (imm == 0) {
            temp.D(2 * i) = int128_getlo(Vj->Q(i));
            temp.D(2 * i + 1) = int128_getlo(Vd->Q(i));
        } else {
            r[2 * i] = int128_and(int128_rshift(Vj->Q(i), (imm - 1)),
                                  int128_one());
            r[2 * i + 1] = int128_and(int128_rshift(Vd->Q(i), (imm - 1)),
                                      int128_one());
            temp.D(2 * i) = int128_getlo(int128_add(int128_rshift(Vj->Q(i),
                                                    imm), r[2 * i]));
            temp.D(2 * i + 1) = int128_getlo(int128_add(int128_rshift(Vd->Q(i),
                                                        imm), r[2 * i + 1]));
        }
    }
    *Vd = temp;
}

VSRARNI(vsrarni_b_h, 16, B, H)
VSRARNI(vsrarni_h_w, 32, H, W)
VSRARNI(vsrarni_w_d, 64, W, D)

#define SSRLNS(NAME, T1, T2, T3)                    \
static T1 do_ssrlns_ ## NAME(T2 e2, int sa, int sh) \
{                                                   \
        T1 shft_res;                                \
        if (sa == 0) {                              \
            shft_res = e2;                          \
        } else {                                    \
            shft_res = (((T1)e2) >> sa);            \
        }                                           \
        T3 mask;                                    \
        mask = (1ull << sh) -1;                     \
        if (shft_res > mask) {                      \
            return mask;                            \
        } else {                                    \
            return  shft_res;                       \
        }                                           \
}

SSRLNS(B, uint16_t, int16_t, uint8_t)
SSRLNS(H, uint32_t, int32_t, uint16_t)
SSRLNS(W, uint64_t, int64_t, uint32_t)

#define VSSRLN(NAME, BIT, E1, E2, E3)                                       \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc)              \
{                                                                           \
    int i, j, ofs;                                                          \
    VReg *Vd = (VReg *)vd;                                                  \
    VReg *Vj = (VReg *)vj;                                                  \
    VReg *Vk = (VReg *)vk;                                                  \
    int oprsz = simd_oprsz(desc);                                           \
                                                                            \
    ofs = LSX_LEN / BIT;                                                    \
    for (i = 0; i < oprsz / 16; i++) {                                      \
        for (j = 0; j < ofs; j++) {                                         \
            Vd->E1(j + ofs * 2 * i) = do_ssrlns_ ## E1(Vj->E2(j + ofs * i), \
                                                Vk->E3(j + ofs * i) % BIT,  \
                                                BIT / 2 - 1);               \
        }                                                                   \
        Vd->D(2 * i + 1) = 0;                                               \
    }                                                                       \
}

VSSRLN(vssrln_b_h, 16, B, H, UH)
VSSRLN(vssrln_h_w, 32, H, W, UW)
VSSRLN(vssrln_w_d, 64, W, D, UD)

#define SSRANS(E, T1, T2)                        \
static T1 do_ssrans_ ## E(T1 e2, int sa, int sh) \
{                                                \
        T1 shft_res;                             \
        if (sa == 0) {                           \
            shft_res = e2;                       \
        } else {                                 \
            shft_res = e2 >> sa;                 \
        }                                        \
        T2 mask;                                 \
        mask = (1ll << sh) - 1;                  \
        if (shft_res > mask) {                   \
            return  mask;                        \
        } else if (shft_res < -(mask + 1)) {     \
            return  ~mask;                       \
        } else {                                 \
            return shft_res;                     \
        }                                        \
}

SSRANS(B, int16_t, int8_t)
SSRANS(H, int32_t, int16_t)
SSRANS(W, int64_t, int32_t)

#define VSSRAN(NAME, BIT, E1, E2, E3)                                       \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc)              \
{                                                                           \
    int i, j, ofs;                                                          \
    VReg *Vd = (VReg *)vd;                                                  \
    VReg *Vj = (VReg *)vj;                                                  \
    VReg *Vk = (VReg *)vk;                                                  \
    int oprsz = simd_oprsz(desc);                                           \
                                                                            \
    ofs = LSX_LEN / BIT;                                                    \
    for (i = 0; i < oprsz / 16; i++) {                                      \
        for (j = 0; j < ofs; j++) {                                         \
            Vd->E1(j + ofs * 2 * i) = do_ssrans_ ## E1(Vj->E2(j + ofs * i), \
                                                Vk->E3(j + ofs * i) % BIT,  \
                                                BIT / 2 - 1);               \
        }                                                                   \
        Vd->D(2 * i + 1) = 0;                                               \
    }                                                                       \
}

VSSRAN(vssran_b_h, 16, B, H, UH)
VSSRAN(vssran_h_w, 32, H, W, UW)
VSSRAN(vssran_w_d, 64, W, D, UD)

#define SSRLNU(E, T1, T2, T3)                    \
static T1 do_ssrlnu_ ## E(T3 e2, int sa, int sh) \
{                                                \
        T1 shft_res;                             \
        if (sa == 0) {                           \
            shft_res = e2;                       \
        } else {                                 \
            shft_res = (((T1)e2) >> sa);         \
        }                                        \
        T2 mask;                                 \
        mask = (1ull << sh) - 1;                 \
        if (shft_res > mask) {                   \
            return mask;                         \
        } else {                                 \
            return shft_res;                     \
        }                                        \
}

SSRLNU(B, uint16_t, uint8_t,  int16_t)
SSRLNU(H, uint32_t, uint16_t, int32_t)
SSRLNU(W, uint64_t, uint32_t, int64_t)

#define VSSRLNU(NAME, BIT, E1, E2, E3)                                      \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc)              \
{                                                                           \
    int i, j, ofs;                                                          \
    VReg *Vd = (VReg *)vd;                                                  \
    VReg *Vj = (VReg *)vj;                                                  \
    VReg *Vk = (VReg *)vk;                                                  \
    int oprsz = simd_oprsz(desc);                                           \
                                                                            \
    ofs = LSX_LEN / BIT;                                                    \
    for (i = 0; i < oprsz / 16; i++) {                                      \
        for (j = 0; j < ofs; j++) {                                         \
            Vd->E1(j + ofs * 2 * i) = do_ssrlnu_ ## E1(Vj->E2(j + ofs * i), \
                                                Vk->E3(j + ofs * i) % BIT,  \
                                                BIT / 2);                   \
        }                                                                   \
        Vd->D(2 * i + 1) = 0;                                               \
    }                                                                       \
}

VSSRLNU(vssrln_bu_h, 16, B, H, UH)
VSSRLNU(vssrln_hu_w, 32, H, W, UW)
VSSRLNU(vssrln_wu_d, 64, W, D, UD)

#define SSRANU(E, T1, T2, T3)                    \
static T1 do_ssranu_ ## E(T3 e2, int sa, int sh) \
{                                                \
        T1 shft_res;                             \
        if (sa == 0) {                           \
            shft_res = e2;                       \
        } else {                                 \
            shft_res = e2 >> sa;                 \
        }                                        \
        if (e2 < 0) {                            \
            shft_res = 0;                        \
        }                                        \
        T2 mask;                                 \
        mask = (1ull << sh) - 1;                 \
        if (shft_res > mask) {                   \
            return mask;                         \
        } else {                                 \
            return shft_res;                     \
        }                                        \
}

SSRANU(B, uint16_t, uint8_t,  int16_t)
SSRANU(H, uint32_t, uint16_t, int32_t)
SSRANU(W, uint64_t, uint32_t, int64_t)

#define VSSRANU(NAME, BIT, E1, E2, E3)                                         \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc)                 \
{                                                                              \
    int i, j, ofs;                                                             \
    VReg *Vd = (VReg *)vd;                                                     \
    VReg *Vj = (VReg *)vj;                                                     \
    VReg *Vk = (VReg *)vk;                                                     \
    int oprsz = simd_oprsz(desc);                                              \
                                                                               \
    ofs = LSX_LEN / BIT;                                                       \
    for (i = 0; i < oprsz / 16; i++) {                                         \
        for (j = 0; j < ofs; j++) {                                            \
            Vd->E1(j + ofs * 2 * i) = do_ssranu_ ## E1(Vj->E2(j + ofs * i),    \
                                                    Vk->E3(j + ofs * i) % BIT, \
                                                    BIT / 2);                  \
        }                                                                      \
        Vd->D(2 * i + 1) = 0;                                                  \
    }                                                                          \
}

VSSRANU(vssran_bu_h, 16, B, H, UH)
VSSRANU(vssran_hu_w, 32, H, W, UW)
VSSRANU(vssran_wu_d, 64, W, D, UD)

#define VSSRLNI(NAME, BIT, E1, E2)                                                 \
void HELPER(NAME)(void *vd, void *vj, uint64_t imm, uint32_t desc)                 \
{                                                                                  \
    int i, j, ofs;                                                                 \
    VReg temp = {};                                                                \
    VReg *Vd = (VReg *)vd;                                                         \
    VReg *Vj = (VReg *)vj;                                                         \
    int oprsz = simd_oprsz(desc);                                                  \
                                                                                   \
    ofs = LSX_LEN / BIT;                                                           \
    for (i = 0; i < oprsz / 16; i++) {                                             \
        for (j = 0; j < ofs; j++) {                                                \
            temp.E1(j + ofs * 2 * i) = do_ssrlns_ ## E1(Vj->E2(j + ofs * i),       \
                                                     imm, BIT / 2 - 1);            \
            temp.E1(j + ofs * (2 * i + 1)) = do_ssrlns_ ## E1(Vd->E2(j + ofs * i), \
                                                           imm, BIT / 2 - 1);      \
        }                                                                          \
    }                                                                              \
    *Vd = temp;                                                                    \
}

static void do_vssrlni_q(VReg *Vd, VReg *Vj,
                         uint64_t imm, int idx, Int128 mask)
{
    Int128 shft_res1, shft_res2;

    if (imm == 0) {
        shft_res1 = Vj->Q(idx);
        shft_res2 = Vd->Q(idx);
    } else {
        shft_res1 = int128_urshift(Vj->Q(idx), imm);
        shft_res2 = int128_urshift(Vd->Q(idx), imm);
    }

    if (int128_ult(mask, shft_res1)) {
        Vd->D(idx * 2) = int128_getlo(mask);
    }else {
        Vd->D(idx * 2) = int128_getlo(shft_res1);
    }

    if (int128_ult(mask, shft_res2)) {
        Vd->D(idx * 2 + 1) = int128_getlo(mask);
    }else {
        Vd->D(idx * 2 + 1) = int128_getlo(shft_res2);
    }
}

void HELPER(vssrlni_d_q)(void *vd, void *vj, uint64_t imm, uint32_t desc)
{
    int i;
    Int128 mask;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    int oprsz = simd_oprsz(desc);

    mask = int128_sub(int128_lshift(int128_one(), 63), int128_one());

    for (i = 0; i < oprsz / 16; i++) {
        do_vssrlni_q(Vd, Vj, imm, i, mask);
    }
}

VSSRLNI(vssrlni_b_h, 16, B, H)
VSSRLNI(vssrlni_h_w, 32, H, W)
VSSRLNI(vssrlni_w_d, 64, W, D)

#define VSSRANI(NAME, BIT, E1, E2)                                                 \
void HELPER(NAME)(void *vd, void *vj, uint64_t imm, uint32_t desc)                 \
{                                                                                  \
    int i, j, ofs;                                                                 \
    VReg temp = {};                                                                \
    VReg *Vd = (VReg *)vd;                                                         \
    VReg *Vj = (VReg *)vj;                                                         \
    int oprsz = simd_oprsz(desc);                                                  \
                                                                                   \
    ofs = LSX_LEN / BIT;                                                           \
    for (i = 0; i < oprsz / 16; i++) {                                             \
        for (j = 0; j < ofs; j++) {                                                \
            temp.E1(j + ofs * 2 * i) = do_ssrans_ ## E1(Vj->E2(j + ofs * i),       \
                                                        imm, BIT / 2 - 1);         \
            temp.E1(j + ofs * (2 * i + 1)) = do_ssrans_ ## E1(Vd->E2(j + ofs * i), \
                                                              imm, BIT / 2 - 1);   \
        }                                                                          \
    }                                                                              \
    *Vd = temp;                                                                    \
}

static void do_vssrani_d_q(VReg *Vd, VReg *Vj,
                           uint64_t imm, int idx, Int128 mask, Int128 min)
{
    Int128 shft_res1, shft_res2;

    if (imm == 0) {
        shft_res1 = Vj->Q(idx);
        shft_res2 = Vd->Q(idx);
    } else {
        shft_res1 = int128_rshift(Vj->Q(idx), imm);
        shft_res2 = int128_rshift(Vd->Q(idx), imm);
    }

    if (int128_gt(shft_res1, mask)) {
        Vd->D(idx * 2) = int128_getlo(mask);
    } else if (int128_lt(shft_res1, int128_neg(min))) {
        Vd->D(idx * 2) = int128_getlo(min);
    } else {
        Vd->D(idx * 2) = int128_getlo(shft_res1);
    }

    if (int128_gt(shft_res2, mask)) {
        Vd->D(idx * 2 + 1) = int128_getlo(mask);
    } else if (int128_lt(shft_res2, int128_neg(min))) {
        Vd->D(idx * 2 + 1) = int128_getlo(min);
    } else {
        Vd->D(idx * 2 + 1) = int128_getlo(shft_res2);
    }
}

void HELPER(vssrani_d_q)(void *vd, void *vj, uint64_t imm, uint32_t desc)
{
    int i;
    Int128 mask, min;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    int oprsz = simd_oprsz(desc);

    mask = int128_sub(int128_lshift(int128_one(), 63), int128_one());
    min  = int128_lshift(int128_one(), 63);

    for (i = 0; i < oprsz / 16; i++) {
        do_vssrani_d_q(Vd, Vj, imm, i, mask, min);
    }
}


VSSRANI(vssrani_b_h, 16, B, H)
VSSRANI(vssrani_h_w, 32, H, W)
VSSRANI(vssrani_w_d, 64, W, D)

#define VSSRLNUI(NAME, BIT, E1, E2)                                                \
void HELPER(NAME)(void *vd, void *vj, uint64_t imm, uint32_t desc)                 \
{                                                                                  \
    int i, j, ofs;                                                                 \
    VReg temp = {};                                                                \
    VReg *Vd = (VReg *)vd;                                                         \
    VReg *Vj = (VReg *)vj;                                                         \
    int oprsz = simd_oprsz(desc);                                                  \
                                                                                   \
    ofs = LSX_LEN / BIT;                                                           \
    for (i = 0; i < oprsz / 16; i++) {                                             \
        for (j = 0; j < ofs; j++) {                                                \
            temp.E1(j + ofs * 2 * i) = do_ssrlnu_ ## E1(Vj->E2(j + ofs * i),       \
                                                        imm, BIT / 2);             \
            temp.E1(j + ofs * (2 * i + 1)) = do_ssrlnu_ ## E1(Vd->E2(j + ofs * i), \
                                                              imm, BIT / 2);       \
        }                                                                          \
    }                                                                              \
    *Vd = temp;                                                                    \
}

void HELPER(vssrlni_du_q)(void *vd, void *vj, uint64_t imm, uint32_t desc)
{
    int i;
    Int128 mask;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    int oprsz = simd_oprsz(desc);

    mask = int128_sub(int128_lshift(int128_one(), 64), int128_one());

    for (i = 0; i < oprsz / 16; i++) {
        do_vssrlni_q(Vd, Vj, imm, i, mask);
    }
}

VSSRLNUI(vssrlni_bu_h, 16, B, H)
VSSRLNUI(vssrlni_hu_w, 32, H, W)
VSSRLNUI(vssrlni_wu_d, 64, W, D)

#define VSSRANUI(NAME, BIT, E1, E2)                                                \
void HELPER(NAME)(void *vd, void *vj, uint64_t imm, uint32_t desc)                 \
{                                                                                  \
    int i, j, ofs;                                                                 \
    VReg temp = {};                                                                \
    VReg *Vd = (VReg *)vd;                                                         \
    VReg *Vj = (VReg *)vj;                                                         \
    int oprsz = simd_oprsz(desc);                                                  \
                                                                                   \
    ofs = LSX_LEN / BIT;                                                           \
    for (i = 0; i < oprsz / 16; i++) {                                             \
        for (j = 0; j < ofs; j++) {                                                \
            temp.E1(j + ofs * 2 * i) = do_ssranu_ ## E1(Vj->E2(j + ofs * i),       \
                                                        imm, BIT / 2);             \
            temp.E1(j + ofs * (2 * i + 1)) = do_ssranu_ ## E1(Vd->E2(j + ofs * i), \
                                                              imm, BIT / 2);       \
        }                                                                          \
    }                                                                              \
    *Vd = temp;                                                                    \
}

static void do_vssrani_du_q(VReg *Vd, VReg *Vj,
                            uint64_t imm, int idx, Int128 mask)
{
    Int128 shft_res1, shft_res2;

    if (imm == 0) {
        shft_res1 = Vj->Q(idx);
        shft_res2 = Vd->Q(idx);
    } else {
        shft_res1 = int128_rshift(Vj->Q(idx), imm);
        shft_res2 = int128_rshift(Vd->Q(idx), imm);
    }

    if (int128_lt(Vj->Q(idx), int128_zero())) {
        shft_res1 = int128_zero();
    }

    if (int128_lt(Vd->Q(idx), int128_zero())) {
        shft_res2 = int128_zero();
    }
    if (int128_ult(mask, shft_res1)) {
        Vd->D(idx * 2) = int128_getlo(mask);
    }else {
        Vd->D(idx * 2) = int128_getlo(shft_res1);
    }

    if (int128_ult(mask, shft_res2)) {
        Vd->D(idx * 2 + 1) = int128_getlo(mask);
    }else {
        Vd->D(idx * 2 + 1) = int128_getlo(shft_res2);
    }

}

void HELPER(vssrani_du_q)(void *vd, void *vj, uint64_t imm, uint32_t desc)
{
    int i;
    Int128 mask;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    int oprsz = simd_oprsz(desc);

    mask = int128_sub(int128_lshift(int128_one(), 64), int128_one());

    for (i = 0; i < oprsz / 16; i++) {
        do_vssrani_du_q(Vd, Vj, imm, i, mask);
    }
}

VSSRANUI(vssrani_bu_h, 16, B, H)
VSSRANUI(vssrani_hu_w, 32, H, W)
VSSRANUI(vssrani_wu_d, 64, W, D)

#define SSRLRNS(E1, E2, T1, T2, T3)                \
static T1 do_ssrlrns_ ## E1(T2 e2, int sa, int sh) \
{                                                  \
    T1 shft_res;                                   \
                                                   \
    shft_res = do_vsrlr_ ## E2(e2, sa);            \
    T1 mask;                                       \
    mask = (1ull << sh) - 1;                       \
    if (shft_res > mask) {                         \
        return mask;                               \
    } else {                                       \
        return  shft_res;                          \
    }                                              \
}

SSRLRNS(B, H, uint16_t, int16_t, uint8_t)
SSRLRNS(H, W, uint32_t, int32_t, uint16_t)
SSRLRNS(W, D, uint64_t, int64_t, uint32_t)

#define VSSRLRN(NAME, BIT, E1, E2, E3)                                         \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc)                 \
{                                                                              \
    int i, j, ofs;                                                             \
    VReg *Vd = (VReg *)vd;                                                     \
    VReg *Vj = (VReg *)vj;                                                     \
    VReg *Vk = (VReg *)vk;                                                     \
    int oprsz = simd_oprsz(desc);                                              \
                                                                               \
    ofs = LSX_LEN / BIT;                                                       \
    for (i = 0; i < oprsz / 16; i++) {                                         \
        for (j = 0; j < ofs; j++) {                                            \
            Vd->E1(j + ofs * 2 * i) = do_ssrlrns_ ## E1(Vj->E2(j + ofs * i),   \
                                                    Vk->E3(j + ofs * i) % BIT, \
                                                    BIT / 2 - 1);              \
        }                                                                      \
        Vd->D(2 * i + 1) = 0;                                                  \
    }                                                                          \
}

VSSRLRN(vssrlrn_b_h, 16, B, H, UH)
VSSRLRN(vssrlrn_h_w, 32, H, W, UW)
VSSRLRN(vssrlrn_w_d, 64, W, D, UD)

#define SSRARNS(E1, E2, T1, T2)                    \
static T1 do_ssrarns_ ## E1(T1 e2, int sa, int sh) \
{                                                  \
    T1 shft_res;                                   \
                                                   \
    shft_res = do_vsrar_ ## E2(e2, sa);            \
    T2 mask;                                       \
    mask = (1ll << sh) - 1;                        \
    if (shft_res > mask) {                         \
        return  mask;                              \
    } else if (shft_res < -(mask +1)) {            \
        return  ~mask;                             \
    } else {                                       \
        return shft_res;                           \
    }                                              \
}

SSRARNS(B, H, int16_t, int8_t)
SSRARNS(H, W, int32_t, int16_t)
SSRARNS(W, D, int64_t, int32_t)

#define VSSRARN(NAME, BIT, E1, E2, E3)                                         \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc)                 \
{                                                                              \
    int i, j, ofs;                                                             \
    VReg *Vd = (VReg *)vd;                                                     \
    VReg *Vj = (VReg *)vj;                                                     \
    VReg *Vk = (VReg *)vk;                                                     \
    int oprsz = simd_oprsz(desc);                                              \
                                                                               \
    ofs = LSX_LEN / BIT;                                                       \
    for (i = 0; i < oprsz / 16; i++) {                                         \
        for (j = 0; j < ofs; j++) {                                            \
            Vd->E1(j + ofs * 2 * i) = do_ssrarns_ ## E1(Vj->E2(j + ofs * i),   \
                                                    Vk->E3(j + ofs * i) % BIT, \
                                                    BIT/ 2 - 1);               \
        }                                                                      \
        Vd->D(2 * i + 1) = 0;                                                  \
    }                                                                          \
}

VSSRARN(vssrarn_b_h, 16, B, H, UH)
VSSRARN(vssrarn_h_w, 32, H, W, UW)
VSSRARN(vssrarn_w_d, 64, W, D, UD)

#define SSRLRNU(E1, E2, T1, T2, T3)                \
static T1 do_ssrlrnu_ ## E1(T3 e2, int sa, int sh) \
{                                                  \
    T1 shft_res;                                   \
                                                   \
    shft_res = do_vsrlr_ ## E2(e2, sa);            \
                                                   \
    T2 mask;                                       \
    mask = (1ull << sh) - 1;                       \
    if (shft_res > mask) {                         \
        return mask;                               \
    } else {                                       \
        return shft_res;                           \
    }                                              \
}

SSRLRNU(B, H, uint16_t, uint8_t, int16_t)
SSRLRNU(H, W, uint32_t, uint16_t, int32_t)
SSRLRNU(W, D, uint64_t, uint32_t, int64_t)

#define VSSRLRNU(NAME, BIT, E1, E2, E3)                                        \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc)                 \
{                                                                              \
    int i, j, ofs;                                                             \
    VReg *Vd = (VReg *)vd;                                                     \
    VReg *Vj = (VReg *)vj;                                                     \
    VReg *Vk = (VReg *)vk;                                                     \
    int oprsz = simd_oprsz(desc);                                              \
                                                                               \
    ofs = LSX_LEN / BIT;                                                       \
    for (i = 0; i < oprsz / 16; i++) {                                         \
        for (j = 0; j < ofs; j++) {                                            \
            Vd->E1(j + ofs * 2 * i) = do_ssrlrnu_ ## E1(Vj->E2(j + ofs * i),   \
                                                    Vk->E3(j + ofs * i) % BIT, \
                                                    BIT / 2);                  \
        }                                                                      \
        Vd->D(2 * i + 1) = 0;                                                  \
    }                                                                          \
}

VSSRLRNU(vssrlrn_bu_h, 16, B, H, UH)
VSSRLRNU(vssrlrn_hu_w, 32, H, W, UW)
VSSRLRNU(vssrlrn_wu_d, 64, W, D, UD)

#define SSRARNU(E1, E2, T1, T2, T3)                \
static T1 do_ssrarnu_ ## E1(T3 e2, int sa, int sh) \
{                                                  \
    T1 shft_res;                                   \
                                                   \
    if (e2 < 0) {                                  \
        shft_res = 0;                              \
    } else {                                       \
        shft_res = do_vsrar_ ## E2(e2, sa);        \
    }                                              \
    T2 mask;                                       \
    mask = (1ull << sh) - 1;                       \
    if (shft_res > mask) {                         \
        return mask;                               \
    } else {                                       \
        return shft_res;                           \
    }                                              \
}

SSRARNU(B, H, uint16_t, uint8_t, int16_t)
SSRARNU(H, W, uint32_t, uint16_t, int32_t)
SSRARNU(W, D, uint64_t, uint32_t, int64_t)

#define VSSRARNU(NAME, BIT, E1, E2, E3)                                      \
void HELPER(NAME)(void *vd, void *vj, void  *vk, uint32_t desc)              \
{                                                                            \
    int i, j, ofs;                                                           \
    VReg *Vd = (VReg *)vd;                                                   \
    VReg *Vj = (VReg *)vj;                                                   \
    VReg *Vk = (VReg *)vk;                                                   \
    int oprsz = simd_oprsz(desc);                                            \
                                                                             \
    ofs = LSX_LEN / BIT;                                                     \
    for (i = 0; i < oprsz / 16; i++) {                                       \
        for (j = 0; j < ofs; j++) {                                          \
            Vd->E1(j + ofs * 2 * i) = do_ssrarnu_ ## E1(Vj->E2(j + ofs * i), \
                                                Vk->E3(j + ofs * i) % BIT,   \
                                                BIT / 2);                    \
        }                                                                    \
        Vd->D(2 * i + 1) = 0;                                                \
    }                                                                        \
}

VSSRARNU(vssrarn_bu_h, 16, B, H, UH)
VSSRARNU(vssrarn_hu_w, 32, H, W, UW)
VSSRARNU(vssrarn_wu_d, 64, W, D, UD)

#define VSSRLRNI(NAME, BIT, E1, E2)                                                 \
void HELPER(NAME)(void *vd, void *vj, uint64_t imm, uint32_t desc)                  \
{                                                                                   \
    int i, j, ofs;                                                                  \
    VReg temp = {};                                                                 \
    VReg *Vd = (VReg *)vd;                                                          \
    VReg *Vj = (VReg *)vj;                                                          \
    int oprsz = simd_oprsz(desc);                                                   \
                                                                                    \
    ofs = LSX_LEN / BIT;                                                            \
    for (i = 0; i < oprsz / 16; i++) {                                              \
        for (j = 0; j < ofs; j++) {                                                 \
            temp.E1(j + ofs * 2 * i) = do_ssrlrns_ ## E1(Vj->E2(j + ofs * i),       \
                                                         imm, BIT / 2 - 1);         \
            temp.E1(j + ofs * (2 * i + 1)) = do_ssrlrns_ ## E1(Vd->E2(j + ofs * i), \
                                                               imm, BIT / 2 - 1);   \
        }                                                                           \
    }                                                                               \
    *Vd = temp;                                                                     \
}

static void do_vssrlrni_q(VReg *Vd, VReg * Vj,
                          uint64_t imm, int idx, Int128 mask)
{
    Int128 shft_res1, shft_res2, r1, r2;
    if (imm == 0) {
        shft_res1 = Vj->Q(idx);
        shft_res2 = Vd->Q(idx);
    } else {
        r1 = int128_and(int128_urshift(Vj->Q(idx), (imm - 1)), int128_one());
        r2 = int128_and(int128_urshift(Vd->Q(idx), (imm - 1)), int128_one());
        shft_res1 = (int128_add(int128_urshift(Vj->Q(idx), imm), r1));
        shft_res2 = (int128_add(int128_urshift(Vd->Q(idx), imm), r2));
    }

    if (int128_ult(mask, shft_res1)) {
        Vd->D(idx * 2) = int128_getlo(mask);
    }else {
        Vd->D(idx * 2) = int128_getlo(shft_res1);
    }

    if (int128_ult(mask, shft_res2)) {
        Vd->D(idx * 2 + 1) = int128_getlo(mask);
    }else {
        Vd->D(idx * 2 + 1) = int128_getlo(shft_res2);
    }
}

void HELPER(vssrlrni_d_q)(void *vd, void *vj, uint64_t imm, uint32_t desc)
{
    int i;
    Int128 mask;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    int oprsz = simd_oprsz(desc);

    mask = int128_sub(int128_lshift(int128_one(), 63), int128_one());

    for (i = 0; i < oprsz / 16; i++) {
        do_vssrlrni_q(Vd, Vj, imm, i, mask);
    }
}

VSSRLRNI(vssrlrni_b_h, 16, B, H)
VSSRLRNI(vssrlrni_h_w, 32, H, W)
VSSRLRNI(vssrlrni_w_d, 64, W, D)

#define VSSRARNI(NAME, BIT, E1, E2)                                                 \
void HELPER(NAME)(void *vd, void *vj, uint64_t imm, uint32_t desc)                  \
{                                                                                   \
    int i, j, ofs;                                                                  \
    VReg temp = {};                                                                 \
    VReg *Vd = (VReg *)vd;                                                          \
    VReg *Vj = (VReg *)vj;                                                          \
    int oprsz = simd_oprsz(desc);                                                   \
                                                                                    \
    ofs = LSX_LEN / BIT;                                                            \
    for (i = 0; i < oprsz / 16; i++) {                                              \
        for (j = 0; j < ofs; j++) {                                                 \
            temp.E1(j + ofs * 2 * i) = do_ssrarns_ ## E1(Vj->E2(j + ofs * i),       \
                                                         imm, BIT / 2 - 1);         \
            temp.E1(j + ofs * (2 * i + 1)) = do_ssrarns_ ## E1(Vd->E2(j + ofs * i), \
                                                               imm, BIT / 2 - 1);   \
        }                                                                           \
    }                                                                               \
    *Vd = temp;                                                                     \
}

static void do_vssrarni_d_q(VReg *Vd, VReg *Vj,
                           uint64_t imm, int idx, Int128 mask1, Int128 mask2)
{
    Int128 shft_res1, shft_res2, r1, r2;

    if (imm == 0) {
        shft_res1 = Vj->Q(idx);
        shft_res2 = Vd->Q(idx);
    } else {
        r1 = int128_and(int128_rshift(Vj->Q(idx), (imm - 1)), int128_one());
        r2 = int128_and(int128_rshift(Vd->Q(idx), (imm - 1)), int128_one());
        shft_res1 = int128_add(int128_rshift(Vj->Q(idx), imm), r1);
        shft_res2 = int128_add(int128_rshift(Vd->Q(idx), imm), r2);
    }
    if (int128_gt(shft_res1, mask1)) {
        Vd->D(idx * 2) = int128_getlo(mask1);
    } else if (int128_lt(shft_res1, int128_neg(mask2))) {
        Vd->D(idx * 2) = int128_getlo(mask2);
    } else {
        Vd->D(idx * 2) = int128_getlo(shft_res1);
    }

    if (int128_gt(shft_res2, mask1)) {
        Vd->D(idx * 2 + 1) = int128_getlo(mask1);
    } else if (int128_lt(shft_res2, int128_neg(mask2))) {
        Vd->D(idx * 2 + 1) = int128_getlo(mask2);
    } else {
        Vd->D(idx * 2 + 1) = int128_getlo(shft_res2);
    }
}

void HELPER(vssrarni_d_q)(void *vd, void *vj, uint64_t imm, uint32_t desc)
{
    int i;
    Int128 mask1, mask2;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    int oprsz = simd_oprsz(desc);

    mask1 = int128_sub(int128_lshift(int128_one(), 63), int128_one());
    mask2  = int128_lshift(int128_one(), 63);

    for (i = 0; i < oprsz / 16; i++) {
        do_vssrarni_d_q(Vd, Vj, imm, i, mask1, mask2);
    }
}

VSSRARNI(vssrarni_b_h, 16, B, H)
VSSRARNI(vssrarni_h_w, 32, H, W)
VSSRARNI(vssrarni_w_d, 64, W, D)

#define VSSRLRNUI(NAME, BIT, E1, E2)                                                \
void HELPER(NAME)(void *vd, void *vj, uint64_t imm, uint32_t desc)                  \
{                                                                                   \
    int i, j, ofs;                                                                  \
    VReg temp = {};                                                                 \
    VReg *Vd = (VReg *)vd;                                                          \
    VReg *Vj = (VReg *)vj;                                                          \
    int oprsz = simd_oprsz(desc);                                                   \
                                                                                    \
    ofs = LSX_LEN / BIT;                                                            \
    for (i = 0; i < oprsz / 16; i++) {                                              \
        for (j = 0; j < ofs; j++) {                                                 \
            temp.E1(j + ofs * 2 * i) = do_ssrlrnu_ ## E1(Vj->E2(j + ofs * i),       \
                                                         imm, BIT / 2);             \
            temp.E1(j + ofs * (2 * i + 1)) = do_ssrlrnu_ ## E1(Vd->E2(j + ofs * i), \
                                                               imm, BIT / 2);       \
        }                                                                           \
    }                                                                               \
    *Vd = temp;                                                                     \
}

void HELPER(vssrlrni_du_q)(void *vd, void *vj, uint64_t imm, uint32_t desc)
{
    int i;
    Int128 mask;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    int oprsz = simd_oprsz(desc);

    mask = int128_sub(int128_lshift(int128_one(), 64), int128_one());

    for (i = 0; i < oprsz / 16; i++) {
        do_vssrlrni_q(Vd, Vj, imm, i, mask);
    }
}

VSSRLRNUI(vssrlrni_bu_h, 16, B, H)
VSSRLRNUI(vssrlrni_hu_w, 32, H, W)
VSSRLRNUI(vssrlrni_wu_d, 64, W, D)

#define VSSRARNUI(NAME, BIT, E1, E2)                                                \
void HELPER(NAME)(void *vd, void *vj, uint64_t imm, uint32_t desc)                  \
{                                                                                   \
    int i, j, ofs;                                                                  \
    VReg temp = {};                                                                 \
    VReg *Vd = (VReg *)vd;                                                          \
    VReg *Vj = (VReg *)vj;                                                          \
    int oprsz = simd_oprsz(desc);                                                   \
                                                                                    \
    ofs = LSX_LEN / BIT;                                                            \
    for (i = 0; i < oprsz / 16; i++) {                                              \
        for (j = 0; j < ofs; j++) {                                                 \
            temp.E1(j + ofs * 2 * i) = do_ssrarnu_ ## E1(Vj->E2(j + ofs * i),       \
                                                         imm, BIT / 2);             \
            temp.E1(j + ofs * (2 * i + 1)) = do_ssrarnu_ ## E1(Vd->E2(j + ofs * i), \
                                                               imm, BIT / 2);       \
        }                                                                           \
    }                                                                               \
    *Vd = temp;                                                                     \
}

static void do_vssrarni_du_q(VReg *Vd, VReg *Vj,
                             uint64_t imm, int idx, Int128 mask1, Int128 mask2)
{
    Int128 shft_res1, shft_res2, r1, r2;

    if (imm == 0) {
        shft_res1 = Vj->Q(idx);
        shft_res2 = Vd->Q(idx);
    } else {
        r1 = int128_and(int128_rshift(Vj->Q(idx), (imm - 1)), int128_one());
        r2 = int128_and(int128_rshift(Vd->Q(idx), (imm - 1)), int128_one());
        shft_res1 = int128_add(int128_rshift(Vj->Q(idx), imm), r1);
        shft_res2 = int128_add(int128_rshift(Vd->Q(idx), imm), r2);
    }

    if (int128_lt(Vj->Q(idx), int128_zero())) {
        shft_res1 = int128_zero();
    }
    if (int128_lt(Vd->Q(idx), int128_zero())) {
        shft_res2 = int128_zero();
    }

    if (int128_gt(shft_res1,  mask1)) {
        Vd->D(idx * 2) = int128_getlo(mask1);
    } else if (int128_lt(shft_res1, int128_neg(mask2))) {
        Vd->D(idx * 2) = int128_getlo(mask2);
    } else {
        Vd->D(idx * 2) = int128_getlo(shft_res1);
    }

    if (int128_gt(shft_res2, mask1)) {
        Vd->D(idx * 2 + 1) = int128_getlo(mask1);
    } else if (int128_lt(shft_res2, int128_neg(mask2))) {
        Vd->D(idx * 2 + 1) = int128_getlo(mask2);
    } else {
        Vd->D(idx * 2 + 1) = int128_getlo(shft_res2);
    }
}

void HELPER(vssrarni_du_q)(void *vd, void *vj, uint64_t imm, uint32_t desc)
{
    int i;
    Int128 mask1, mask2;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    int oprsz = simd_oprsz(desc);

    mask1 = int128_sub(int128_lshift(int128_one(), 64), int128_one());
    mask2  = int128_lshift(int128_one(), 64);

    for (i = 0; i < oprsz / 16; i++) {
        do_vssrarni_du_q(Vd, Vj, imm, i, mask1, mask2);
    }
}

VSSRARNUI(vssrarni_bu_h, 16, B, H)
VSSRARNUI(vssrarni_hu_w, 32, H, W)
VSSRARNUI(vssrarni_wu_d, 64, W, D)

#define DO_2OP(NAME, BIT, E, DO_OP)                  \
void HELPER(NAME)(void *vd, void *vj, uint32_t desc) \
{                                                    \
    int i;                                           \
    VReg *Vd = (VReg *)vd;                           \
    VReg *Vj = (VReg *)vj;                           \
    int oprsz = simd_oprsz(desc);                    \
                                                     \
    for (i = 0; i < oprsz / (BIT / 8); i++)          \
    {                                                \
        Vd->E(i) = DO_OP(Vj->E(i));                  \
    }                                                \
}

DO_2OP(vclo_b, 8, UB, DO_CLO_B)
DO_2OP(vclo_h, 16, UH, DO_CLO_H)
DO_2OP(vclo_w, 32, UW, DO_CLO_W)
DO_2OP(vclo_d, 64, UD, DO_CLO_D)
DO_2OP(vclz_b, 8, UB, DO_CLZ_B)
DO_2OP(vclz_h, 16, UH, DO_CLZ_H)
DO_2OP(vclz_w, 32, UW, DO_CLZ_W)
DO_2OP(vclz_d, 64, UD, DO_CLZ_D)

#define VPCNT(NAME, BIT, E, FN)                      \
void HELPER(NAME)(void *vd, void *vj, uint32_t desc) \
{                                                    \
    int i;                                           \
    VReg *Vd = (VReg *)vd;                           \
    VReg *Vj = (VReg *)vj;                           \
    int oprsz = simd_oprsz(desc);                    \
                                                     \
    for (i = 0; i < oprsz / (BIT / 8); i++)          \
    {                                                \
        Vd->E(i) = FN(Vj->E(i));                     \
    }                                                \
}

VPCNT(vpcnt_b, 8, UB, ctpop8)
VPCNT(vpcnt_h, 16, UH, ctpop16)
VPCNT(vpcnt_w, 32, UW, ctpop32)
VPCNT(vpcnt_d, 64, UD, ctpop64)

#define DO_BIT(NAME, BIT, E, DO_OP)                            \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc) \
{                                                              \
    int i;                                                     \
    VReg *Vd = (VReg *)vd;                                     \
    VReg *Vj = (VReg *)vj;                                     \
    VReg *Vk = (VReg *)vk;                                     \
    int oprsz = simd_oprsz(desc);                              \
                                                               \
    for (i = 0; i < oprsz / (BIT / 8); i++) {                  \
        Vd->E(i) = DO_OP(Vj->E(i), Vk->E(i)%BIT);              \
    }                                                          \
}

DO_BIT(vbitclr_b, 8, UB, DO_BITCLR)
DO_BIT(vbitclr_h, 16, UH, DO_BITCLR)
DO_BIT(vbitclr_w, 32, UW, DO_BITCLR)
DO_BIT(vbitclr_d, 64, UD, DO_BITCLR)
DO_BIT(vbitset_b, 8, UB, DO_BITSET)
DO_BIT(vbitset_h, 16, UH, DO_BITSET)
DO_BIT(vbitset_w, 32, UW, DO_BITSET)
DO_BIT(vbitset_d, 64, UD, DO_BITSET)
DO_BIT(vbitrev_b, 8, UB, DO_BITREV)
DO_BIT(vbitrev_h, 16, UH, DO_BITREV)
DO_BIT(vbitrev_w, 32, UW, DO_BITREV)
DO_BIT(vbitrev_d, 64, UD, DO_BITREV)

#define DO_BITI(NAME, BIT, E, DO_OP)                               \
void HELPER(NAME)(void *vd, void *vj, uint64_t imm, uint32_t desc) \
{                                                                  \
    int i;                                                         \
    VReg *Vd = (VReg *)vd;                                         \
    VReg *Vj = (VReg *)vj;                                         \
    int oprsz = simd_oprsz(desc);                                  \
                                                                   \
    for (i = 0; i < oprsz / (BIT / 8); i++) {                      \
        Vd->E(i) = DO_OP(Vj->E(i), imm);                           \
    }                                                              \
}

DO_BITI(vbitclri_b, 8, UB, DO_BITCLR)
DO_BITI(vbitclri_h, 16, UH, DO_BITCLR)
DO_BITI(vbitclri_w, 32, UW, DO_BITCLR)
DO_BITI(vbitclri_d, 64, UD, DO_BITCLR)
DO_BITI(vbitseti_b, 8, UB, DO_BITSET)
DO_BITI(vbitseti_h, 16, UH, DO_BITSET)
DO_BITI(vbitseti_w, 32, UW, DO_BITSET)
DO_BITI(vbitseti_d, 64, UD, DO_BITSET)
DO_BITI(vbitrevi_b, 8, UB, DO_BITREV)
DO_BITI(vbitrevi_h, 16, UH, DO_BITREV)
DO_BITI(vbitrevi_w, 32, UW, DO_BITREV)
DO_BITI(vbitrevi_d, 64, UD, DO_BITREV)

#define VFRSTP(NAME, BIT, MASK, E)                             \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc) \
{                                                              \
    int i, j, m, ofs;                                          \
    VReg *Vd = (VReg *)vd;                                     \
    VReg *Vj = (VReg *)vj;                                     \
    VReg *Vk = (VReg *)vk;                                     \
    int oprsz = simd_oprsz(desc);                              \
                                                               \
    ofs = LSX_LEN / BIT;                                       \
    for (i = 0; i < oprsz / 16; i++) {                         \
        m = Vk->E(i * ofs) & MASK;                             \
        for (j = 0; j < ofs; j++) {                            \
            if (Vj->E(j + ofs * i) < 0) {                      \
                break;                                         \
            }                                                  \
        }                                                      \
        Vd->E(m + i * ofs) = j;                                \
    }                                                          \
}

VFRSTP(vfrstp_b, 8, 0xf, B)
VFRSTP(vfrstp_h, 16, 0x7, H)

#define VFRSTPI(NAME, BIT, E)                                      \
void HELPER(NAME)(void *vd, void *vj, uint64_t imm, uint32_t desc) \
{                                                                  \
    int i, j, m, ofs;                                              \
    VReg *Vd = (VReg *)vd;                                         \
    VReg *Vj = (VReg *)vj;                                         \
    int oprsz = simd_oprsz(desc);                                  \
                                                                   \
    ofs = LSX_LEN / BIT;                                           \
    m = imm % ofs;                                                 \
    for (i = 0; i < oprsz / 16; i++) {                             \
        for (j = 0; j < ofs; j++) {                                \
            if (Vj->E(j + ofs * i) < 0) {                          \
                break;                                             \
            }                                                      \
        }                                                          \
        Vd->E(m + i * ofs) = j;                                    \
    }                                                              \
}

VFRSTPI(vfrstpi_b, 8,  B)
VFRSTPI(vfrstpi_h, 16, H)

static void vec_update_fcsr0_mask(CPULoongArchState *env,
                                  uintptr_t pc, int mask)
{
    int flags = get_float_exception_flags(&env->fp_status);

    set_float_exception_flags(0, &env->fp_status);

    flags &= ~mask;

    if (flags) {
        flags = ieee_ex_to_loongarch(flags);
        UPDATE_FP_CAUSE(env->fcsr0, flags);
    }

    if (GET_FP_ENABLES(env->fcsr0) & flags) {
        do_raise_exception(env, EXCCODE_FPE, pc);
    } else {
        UPDATE_FP_FLAGS(env->fcsr0, flags);
    }
}

static void vec_update_fcsr0(CPULoongArchState *env, uintptr_t pc)
{
    vec_update_fcsr0_mask(env, pc, 0);
}

static inline void vec_clear_cause(CPULoongArchState *env)
{
    SET_FP_CAUSE(env->fcsr0, 0);
}

#define DO_3OP_F(NAME, BIT, E, FN)                          \
void HELPER(NAME)(void *vd, void *vj, void *vk,             \
                  CPULoongArchState *env, uint32_t desc)    \
{                                                           \
    int i;                                                  \
    VReg *Vd = (VReg *)vd;                                  \
    VReg *Vj = (VReg *)vj;                                  \
    VReg *Vk = (VReg *)vk;                                  \
    int oprsz = simd_oprsz(desc);                           \
                                                            \
    vec_clear_cause(env);                                   \
    for (i = 0; i < oprsz / (BIT / 8); i++) {               \
        Vd->E(i) = FN(Vj->E(i), Vk->E(i), &env->fp_status); \
        vec_update_fcsr0(env, GETPC());                     \
    }                                                       \
}

DO_3OP_F(vfadd_s, 32, UW, float32_add)
DO_3OP_F(vfadd_d, 64, UD, float64_add)
DO_3OP_F(vfsub_s, 32, UW, float32_sub)
DO_3OP_F(vfsub_d, 64, UD, float64_sub)
DO_3OP_F(vfmul_s, 32, UW, float32_mul)
DO_3OP_F(vfmul_d, 64, UD, float64_mul)
DO_3OP_F(vfdiv_s, 32, UW, float32_div)
DO_3OP_F(vfdiv_d, 64, UD, float64_div)
DO_3OP_F(vfmax_s, 32, UW, float32_maxnum)
DO_3OP_F(vfmax_d, 64, UD, float64_maxnum)
DO_3OP_F(vfmin_s, 32, UW, float32_minnum)
DO_3OP_F(vfmin_d, 64, UD, float64_minnum)
DO_3OP_F(vfmaxa_s, 32, UW, float32_maxnummag)
DO_3OP_F(vfmaxa_d, 64, UD, float64_maxnummag)
DO_3OP_F(vfmina_s, 32, UW, float32_minnummag)
DO_3OP_F(vfmina_d, 64, UD, float64_minnummag)

#define DO_4OP_F(NAME, BIT, E, FN, flags)                                    \
void HELPER(NAME)(void *vd, void *vj, void *vk, void *va,                    \
                  CPULoongArchState *env, uint32_t desc)                     \
{                                                                            \
    int i;                                                                   \
    VReg *Vd = (VReg *)vd;                                                   \
    VReg *Vj = (VReg *)vj;                                                   \
    VReg *Vk = (VReg *)vk;                                                   \
    VReg *Va = (VReg *)va;                                                   \
    int oprsz = simd_oprsz(desc);                                            \
                                                                             \
    vec_clear_cause(env);                                                    \
    for (i = 0; i < oprsz / (BIT / 8); i++) {                                \
        Vd->E(i) = FN(Vj->E(i), Vk->E(i), Va->E(i), flags, &env->fp_status); \
        vec_update_fcsr0(env, GETPC());                                      \
    }                                                                        \
}

DO_4OP_F(vfmadd_s, 32, UW, float32_muladd, 0)
DO_4OP_F(vfmadd_d, 64, UD, float64_muladd, 0)
DO_4OP_F(vfmsub_s, 32, UW, float32_muladd, float_muladd_negate_c)
DO_4OP_F(vfmsub_d, 64, UD, float64_muladd, float_muladd_negate_c)
DO_4OP_F(vfnmadd_s, 32, UW, float32_muladd, float_muladd_negate_result)
DO_4OP_F(vfnmadd_d, 64, UD, float64_muladd, float_muladd_negate_result)
DO_4OP_F(vfnmsub_s, 32, UW, float32_muladd,
         float_muladd_negate_c | float_muladd_negate_result)
DO_4OP_F(vfnmsub_d, 64, UD, float64_muladd,
         float_muladd_negate_c | float_muladd_negate_result)

#define DO_2OP_F(NAME, BIT, E, FN)                       \
void HELPER(NAME)(void *vd, void *vj,                    \
                  CPULoongArchState *env, uint32_t desc) \
{                                                        \
    int i;                                               \
    VReg *Vd = (VReg *)vd;                               \
    VReg *Vj = (VReg *)vj;                               \
    int oprsz = simd_oprsz(desc);                        \
                                                         \
    vec_clear_cause(env);                                \
    for (i = 0; i < oprsz / (BIT / 8); i++) {            \
        Vd->E(i) = FN(env, Vj->E(i));                    \
    }                                                    \
}

#define FLOGB(BIT, T)                                            \
static T do_flogb_## BIT(CPULoongArchState *env, T fj)           \
{                                                                \
    T fp, fd;                                                    \
    float_status *status = &env->fp_status;                      \
    FloatRoundMode old_mode = get_float_rounding_mode(status);   \
                                                                 \
    set_float_rounding_mode(float_round_down, status);           \
    fp = float ## BIT ##_log2(fj, status);                       \
    fd = float ## BIT ##_round_to_int(fp, status);               \
    set_float_rounding_mode(old_mode, status);                   \
    vec_update_fcsr0_mask(env, GETPC(), float_flag_inexact);     \
    return fd;                                                   \
}

FLOGB(32, uint32_t)
FLOGB(64, uint64_t)

#define FCLASS(NAME, BIT, E, FN)                         \
void HELPER(NAME)(void *vd, void *vj,                    \
                  CPULoongArchState *env, uint32_t desc) \
{                                                        \
    int i;                                               \
    VReg *Vd = (VReg *)vd;                               \
    VReg *Vj = (VReg *)vj;                               \
    int oprsz = simd_oprsz(desc);                        \
                                                         \
    for (i = 0; i < oprsz / (BIT / 8); i++) {            \
        Vd->E(i) = FN(env, Vj->E(i));                    \
    }                                                    \
}

FCLASS(vfclass_s, 32, UW, helper_fclass_s)
FCLASS(vfclass_d, 64, UD, helper_fclass_d)

#define FSQRT(BIT, T)                                  \
static T do_fsqrt_## BIT(CPULoongArchState *env, T fj) \
{                                                      \
    T fd;                                              \
    fd = float ## BIT ##_sqrt(fj, &env->fp_status);    \
    vec_update_fcsr0(env, GETPC());                    \
    return fd;                                         \
}

FSQRT(32, uint32_t)
FSQRT(64, uint64_t)

#define FRECIP(BIT, T)                                                  \
static T do_frecip_## BIT(CPULoongArchState *env, T fj)                 \
{                                                                       \
    T fd;                                                               \
    fd = float ## BIT ##_div(float ## BIT ##_one, fj, &env->fp_status); \
    vec_update_fcsr0(env, GETPC());                                     \
    return fd;                                                          \
}

FRECIP(32, uint32_t)
FRECIP(64, uint64_t)

#define FRSQRT(BIT, T)                                                  \
static T do_frsqrt_## BIT(CPULoongArchState *env, T fj)                 \
{                                                                       \
    T fd, fp;                                                           \
    fp = float ## BIT ##_sqrt(fj, &env->fp_status);                     \
    fd = float ## BIT ##_div(float ## BIT ##_one, fp, &env->fp_status); \
    vec_update_fcsr0(env, GETPC());                                     \
    return fd;                                                          \
}

FRSQRT(32, uint32_t)
FRSQRT(64, uint64_t)

DO_2OP_F(vflogb_s, 32, UW, do_flogb_32)
DO_2OP_F(vflogb_d, 64, UD, do_flogb_64)
DO_2OP_F(vfsqrt_s, 32, UW, do_fsqrt_32)
DO_2OP_F(vfsqrt_d, 64, UD, do_fsqrt_64)
DO_2OP_F(vfrecip_s, 32, UW, do_frecip_32)
DO_2OP_F(vfrecip_d, 64, UD, do_frecip_64)
DO_2OP_F(vfrsqrt_s, 32, UW, do_frsqrt_32)
DO_2OP_F(vfrsqrt_d, 64, UD, do_frsqrt_64)

static uint32_t float16_cvt_float32(uint16_t h, float_status *status)
{
    return float16_to_float32(h, true, status);
}
static uint64_t float32_cvt_float64(uint32_t s, float_status *status)
{
    return float32_to_float64(s, status);
}

static uint16_t float32_cvt_float16(uint32_t s, float_status *status)
{
    return float32_to_float16(s, true, status);
}
static uint32_t float64_cvt_float32(uint64_t d, float_status *status)
{
    return float64_to_float32(d, status);
}

void HELPER(vfcvtl_s_h)(void *vd, void *vj,
                        CPULoongArchState *env, uint32_t desc)
{
    int i, j, ofs;
    VReg temp = {};
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    int oprsz = simd_oprsz(desc);

    ofs = LSX_LEN / 32;
    vec_clear_cause(env);
    for (i = 0; i < oprsz / 16; i++) {
        for (j = 0; j < ofs; j++) {
            temp.UW(j + ofs * i) =float16_cvt_float32(Vj->UH(j + ofs * 2 * i),
                                                      &env->fp_status);
        }
        vec_update_fcsr0(env, GETPC());
    }
    *Vd = temp;
}

void HELPER(vfcvtl_d_s)(void *vd, void *vj,
                        CPULoongArchState *env, uint32_t desc)
{
    int i, j, ofs;
    VReg temp = {};
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    int oprsz = simd_oprsz(desc);

    ofs = LSX_LEN / 64;
    vec_clear_cause(env);
    for (i = 0; i < oprsz / 16; i++) {
        for (j = 0; j < ofs; j++) {
            temp.UD(j + ofs * i) = float32_cvt_float64(Vj->UW(j + ofs * 2 * i),
                                                       &env->fp_status);
        }
        vec_update_fcsr0(env, GETPC());
    }
    *Vd = temp;
}

void HELPER(vfcvth_s_h)(void *vd, void *vj,
                        CPULoongArchState *env, uint32_t desc)
{
    int i, j, ofs;
    VReg temp = {};
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    int oprsz = simd_oprsz(desc);

    ofs = LSX_LEN / 32;
    vec_clear_cause(env);
    for (i = 0; i < oprsz / 16; i++) {
        for (j = 0; j < ofs; j++) {
            temp.UW(j + ofs * i) = float16_cvt_float32(Vj->UH(j + ofs * (2 * i + 1)),
                                                       &env->fp_status);
        }
        vec_update_fcsr0(env, GETPC());
    }
    *Vd = temp;
}

void HELPER(vfcvth_d_s)(void *vd, void *vj,
                        CPULoongArchState *env, uint32_t desc)
{
    int i, j, ofs;
    VReg temp = {};
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    int oprsz = simd_oprsz(desc);

    ofs = LSX_LEN / 64;
    vec_clear_cause(env);
    for (i = 0; i < oprsz / 16; i++) {
        for (j = 0; j < ofs; j++) {
            temp.UD(j + ofs * i) = float32_cvt_float64(Vj->UW(j + ofs * (2 * i + 1)),
                                                        &env->fp_status);
        }
        vec_update_fcsr0(env, GETPC());
    }
    *Vd = temp;
}

void HELPER(vfcvt_h_s)(void *vd, void *vj, void *vk,
                       CPULoongArchState *env, uint32_t desc)
{
    int i, j, ofs;
    VReg temp = {};
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;
    int oprsz = simd_oprsz(desc);

    ofs = LSX_LEN / 32;
    vec_clear_cause(env);
    for(i = 0; i < oprsz / 16; i++) {
        for (j = 0; j < ofs; j++) {
            temp.UH(j + ofs * (2 * i + 1)) = float32_cvt_float16(Vj->UW(j + ofs * i),
                                                                 &env->fp_status);
            temp.UH(j + ofs * 2 * i) = float32_cvt_float16(Vk->UW(j + ofs * i),
                                                           &env->fp_status);
        }
        vec_update_fcsr0(env, GETPC());
    }
    *Vd = temp;
}

void HELPER(vfcvt_s_d)(void *vd, void *vj, void *vk,
                       CPULoongArchState *env, uint32_t desc)
{
    int i, j, ofs;
    VReg temp = {};
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;
    int oprsz = simd_oprsz(desc);

    ofs = LSX_LEN / 64;
    vec_clear_cause(env);
    for(i = 0; i < oprsz / 16; i++) {
        for (j = 0; j < ofs; j++) {
            temp.UW(j + ofs * (2 * i + 1)) = float64_cvt_float32(Vj->UD(j + ofs * i),
                                                                 &env->fp_status);
            temp.UW(j + ofs * 2 * i) = float64_cvt_float32(Vk->UD(j + ofs * i),
                                                           &env->fp_status);
        }
        vec_update_fcsr0(env, GETPC());
    }
    *Vd = temp;
}

void HELPER(vfrint_s)(void *vd, void *vj,
                      CPULoongArchState *env, uint32_t desc)
{
    int i;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    int oprsz = simd_oprsz(desc);

    vec_clear_cause(env);
    for (i = 0; i < oprsz / 4; i++) {
        Vd->W(i) = float32_round_to_int(Vj->UW(i), &env->fp_status);
        vec_update_fcsr0(env, GETPC());
    }
}

void HELPER(vfrint_d)(void *vd, void *vj,
                      CPULoongArchState *env, uint32_t desc)
{
    int i;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    int oprsz = simd_oprsz(desc);

    vec_clear_cause(env);
    for (i = 0; i < oprsz / 8; i++) {
        Vd->D(i) = float64_round_to_int(Vj->UD(i), &env->fp_status);
        vec_update_fcsr0(env, GETPC());
    }
}

#define FCVT_2OP(NAME, BIT, E, MODE)                                        \
void HELPER(NAME)(void *vd, void *vj,                                       \
                  CPULoongArchState *env, uint32_t desc)                    \
{                                                                           \
    int i;                                                                  \
    VReg *Vd = (VReg *)vd;                                                  \
    VReg *Vj = (VReg *)vj;                                                  \
    int oprsz = simd_oprsz(desc);                                           \
                                                                            \
    vec_clear_cause(env);                                                   \
    for (i = 0; i < oprsz / (BIT / 8); i++) {                               \
        FloatRoundMode old_mode = get_float_rounding_mode(&env->fp_status); \
        set_float_rounding_mode(MODE, &env->fp_status);                     \
        Vd->E(i) = float## BIT ## _round_to_int(Vj->E(i), &env->fp_status); \
        set_float_rounding_mode(old_mode, &env->fp_status);                 \
        vec_update_fcsr0(env, GETPC());                                     \
    }                                                                       \
}

FCVT_2OP(vfrintrne_s, 32, UW, float_round_nearest_even)
FCVT_2OP(vfrintrne_d, 64, UD, float_round_nearest_even)
FCVT_2OP(vfrintrz_s, 32, UW, float_round_to_zero)
FCVT_2OP(vfrintrz_d, 64, UD, float_round_to_zero)
FCVT_2OP(vfrintrp_s, 32, UW, float_round_up)
FCVT_2OP(vfrintrp_d, 64, UD, float_round_up)
FCVT_2OP(vfrintrm_s, 32, UW, float_round_down)
FCVT_2OP(vfrintrm_d, 64, UD, float_round_down)

#define FTINT(NAME, FMT1, FMT2, T1, T2,  MODE)                          \
static T2 do_ftint ## NAME(CPULoongArchState *env, T1 fj)               \
{                                                                       \
    T2 fd;                                                              \
    FloatRoundMode old_mode = get_float_rounding_mode(&env->fp_status); \
                                                                        \
    set_float_rounding_mode(MODE, &env->fp_status);                     \
    fd = do_## FMT1 ##_to_## FMT2(env, fj);                             \
    set_float_rounding_mode(old_mode, &env->fp_status);                 \
    return fd;                                                          \
}

#define DO_FTINT(FMT1, FMT2, T1, T2)                                         \
static T2 do_## FMT1 ##_to_## FMT2(CPULoongArchState *env, T1 fj)            \
{                                                                            \
    T2 fd;                                                                   \
                                                                             \
    fd = FMT1 ##_to_## FMT2(fj, &env->fp_status);                            \
    if (get_float_exception_flags(&env->fp_status) & (float_flag_invalid)) { \
        if (FMT1 ##_is_any_nan(fj)) {                                        \
            fd = 0;                                                          \
        }                                                                    \
    }                                                                        \
    vec_update_fcsr0(env, GETPC());                                          \
    return fd;                                                               \
}

DO_FTINT(float32, int32, uint32_t, uint32_t)
DO_FTINT(float64, int64, uint64_t, uint64_t)
DO_FTINT(float32, uint32, uint32_t, uint32_t)
DO_FTINT(float64, uint64, uint64_t, uint64_t)
DO_FTINT(float64, int32, uint64_t, uint32_t)
DO_FTINT(float32, int64, uint32_t, uint64_t)

FTINT(rne_w_s, float32, int32, uint32_t, uint32_t, float_round_nearest_even)
FTINT(rne_l_d, float64, int64, uint64_t, uint64_t, float_round_nearest_even)
FTINT(rp_w_s, float32, int32, uint32_t, uint32_t, float_round_up)
FTINT(rp_l_d, float64, int64, uint64_t, uint64_t, float_round_up)
FTINT(rz_w_s, float32, int32, uint32_t, uint32_t, float_round_to_zero)
FTINT(rz_l_d, float64, int64, uint64_t, uint64_t, float_round_to_zero)
FTINT(rm_w_s, float32, int32, uint32_t, uint32_t, float_round_down)
FTINT(rm_l_d, float64, int64, uint64_t, uint64_t, float_round_down)

DO_2OP_F(vftintrne_w_s, 32, UW, do_ftintrne_w_s)
DO_2OP_F(vftintrne_l_d, 64, UD, do_ftintrne_l_d)
DO_2OP_F(vftintrp_w_s, 32, UW, do_ftintrp_w_s)
DO_2OP_F(vftintrp_l_d, 64, UD, do_ftintrp_l_d)
DO_2OP_F(vftintrz_w_s, 32, UW, do_ftintrz_w_s)
DO_2OP_F(vftintrz_l_d, 64, UD, do_ftintrz_l_d)
DO_2OP_F(vftintrm_w_s, 32, UW, do_ftintrm_w_s)
DO_2OP_F(vftintrm_l_d, 64, UD, do_ftintrm_l_d)
DO_2OP_F(vftint_w_s, 32, UW, do_float32_to_int32)
DO_2OP_F(vftint_l_d, 64, UD, do_float64_to_int64)

FTINT(rz_wu_s, float32, uint32, uint32_t, uint32_t, float_round_to_zero)
FTINT(rz_lu_d, float64, uint64, uint64_t, uint64_t, float_round_to_zero)

DO_2OP_F(vftintrz_wu_s, 32, UW, do_ftintrz_wu_s)
DO_2OP_F(vftintrz_lu_d, 64, UD, do_ftintrz_lu_d)
DO_2OP_F(vftint_wu_s, 32, UW, do_float32_to_uint32)
DO_2OP_F(vftint_lu_d, 64, UD, do_float64_to_uint64)

FTINT(rm_w_d, float64, int32, uint64_t, uint32_t, float_round_down)
FTINT(rp_w_d, float64, int32, uint64_t, uint32_t, float_round_up)
FTINT(rz_w_d, float64, int32, uint64_t, uint32_t, float_round_to_zero)
FTINT(rne_w_d, float64, int32, uint64_t, uint32_t, float_round_nearest_even)

#define FTINT_W_D(NAME, FN)                                               \
void HELPER(NAME)(void *vd, void *vj, void *vk,                           \
                  CPULoongArchState *env, uint32_t desc)                  \
{                                                                         \
    int i, j, ofs;                                                        \
    VReg temp = {};                                                       \
    VReg *Vd = (VReg *)vd;                                                \
    VReg *Vj = (VReg *)vj;                                                \
    VReg *Vk = (VReg *)vk;                                                \
    int oprsz = simd_oprsz(desc);                                         \
                                                                          \
    ofs = LSX_LEN / 64;                                                   \
    vec_clear_cause(env);                                                 \
    for (i = 0; i < oprsz / 16; i++) {                                    \
        for (j = 0; j < ofs; j++) {                                       \
            temp.W(j + ofs * (2 * i + 1)) = FN(env, Vj->UD(j + ofs * i)); \
            temp.W(j + ofs * 2 * i) = FN(env, Vk->UD(j + ofs * i));       \
        }                                                                 \
    }                                                                     \
    *Vd = temp;                                                           \
}

FTINT_W_D(vftint_w_d, do_float64_to_int32)
FTINT_W_D(vftintrm_w_d, do_ftintrm_w_d)
FTINT_W_D(vftintrp_w_d, do_ftintrp_w_d)
FTINT_W_D(vftintrz_w_d, do_ftintrz_w_d)
FTINT_W_D(vftintrne_w_d, do_ftintrne_w_d)

FTINT(rml_l_s, float32, int64, uint32_t, uint64_t, float_round_down)
FTINT(rpl_l_s, float32, int64, uint32_t, uint64_t, float_round_up)
FTINT(rzl_l_s, float32, int64, uint32_t, uint64_t, float_round_to_zero)
FTINT(rnel_l_s, float32, int64, uint32_t, uint64_t, float_round_nearest_even)
FTINT(rmh_l_s, float32, int64, uint32_t, uint64_t, float_round_down)
FTINT(rph_l_s, float32, int64, uint32_t, uint64_t, float_round_up)
FTINT(rzh_l_s, float32, int64, uint32_t, uint64_t, float_round_to_zero)
FTINT(rneh_l_s, float32, int64, uint32_t, uint64_t, float_round_nearest_even)

#define FTINTL_L_S(NAME, FN)                                        \
void HELPER(NAME)(void *vd, void *vj,                               \
                  CPULoongArchState *env, uint32_t desc)            \
{                                                                   \
    int i, j, ofs;                                                  \
    VReg temp;                                                      \
    VReg *Vd = (VReg *)vd;                                          \
    VReg *Vj = (VReg *)vj;                                          \
    int oprsz = simd_oprsz(desc);                                   \
                                                                    \
    ofs = LSX_LEN / 64;                                             \
    vec_clear_cause(env);                                           \
    for (i = 0; i < oprsz / 16; i++) {                              \
        for (j = 0; j < ofs; j++) {                                 \
            temp.D(j + ofs * i) = FN(env, Vj->UW(j + ofs * 2 * i)); \
        }                                                           \
    }                                                               \
    *Vd = temp;                                                     \
}

FTINTL_L_S(vftintl_l_s, do_float32_to_int64)
FTINTL_L_S(vftintrml_l_s, do_ftintrml_l_s)
FTINTL_L_S(vftintrpl_l_s, do_ftintrpl_l_s)
FTINTL_L_S(vftintrzl_l_s, do_ftintrzl_l_s)
FTINTL_L_S(vftintrnel_l_s, do_ftintrnel_l_s)

#define FTINTH_L_S(NAME, FN)                                              \
void HELPER(NAME)(void *vd, void *vj,                                     \
                  CPULoongArchState *env, uint32_t desc)                  \
{                                                                         \
    int i, j, ofs;                                                        \
    VReg temp = {};                                                       \
    VReg *Vd = (VReg *)vd;                                                \
    VReg *Vj = (VReg *)vj;                                                \
    int oprsz = simd_oprsz(desc);                                         \
                                                                          \
    ofs = LSX_LEN / 64;                                                   \
    vec_clear_cause(env);                                                 \
    for (i = 0; i < oprsz / 16; i++) {                                    \
        for (j = 0; j < ofs; j++) {                                       \
            temp.D(j + ofs * i) = FN(env, Vj->UW(j + ofs * (2 * i + 1))); \
        }                                                                 \
    }                                                                     \
    *Vd = temp;                                                           \
}

FTINTH_L_S(vftinth_l_s, do_float32_to_int64)
FTINTH_L_S(vftintrmh_l_s, do_ftintrmh_l_s)
FTINTH_L_S(vftintrph_l_s, do_ftintrph_l_s)
FTINTH_L_S(vftintrzh_l_s, do_ftintrzh_l_s)
FTINTH_L_S(vftintrneh_l_s, do_ftintrneh_l_s)

#define FFINT(NAME, FMT1, FMT2, T1, T2)                    \
static T2 do_ffint_ ## NAME(CPULoongArchState *env, T1 fj) \
{                                                          \
    T2 fd;                                                 \
                                                           \
    fd = FMT1 ##_to_## FMT2(fj, &env->fp_status);          \
    vec_update_fcsr0(env, GETPC());                        \
    return fd;                                             \
}

FFINT(s_w, int32, float32, int32_t, uint32_t)
FFINT(d_l, int64, float64, int64_t, uint64_t)
FFINT(s_wu, uint32, float32, uint32_t, uint32_t)
FFINT(d_lu, uint64, float64, uint64_t, uint64_t)

DO_2OP_F(vffint_s_w, 32, W, do_ffint_s_w)
DO_2OP_F(vffint_d_l, 64, D, do_ffint_d_l)
DO_2OP_F(vffint_s_wu, 32, UW, do_ffint_s_wu)
DO_2OP_F(vffint_d_lu, 64, UD, do_ffint_d_lu)

void HELPER(vffintl_d_w)(void *vd, void *vj,
                         CPULoongArchState *env, uint32_t desc)
{
    int i, j, ofs;
    VReg temp = {};
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    int oprsz = simd_oprsz(desc);

    ofs = LSX_LEN / 64;
    vec_clear_cause(env);
    for (i = 0; i < oprsz / 16; i++) {
        for (j = 0; j < ofs; j++) {
            temp.D(j + ofs * i) = int32_to_float64(Vj->W(j + ofs * 2 * i),
                                                   &env->fp_status);
        }
        vec_update_fcsr0(env, GETPC());
    }
    *Vd = temp;
}

void HELPER(vffinth_d_w)(void *vd, void *vj,
                         CPULoongArchState *env, uint32_t desc)
{
    int i, j, ofs;
    VReg temp = {};
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    int oprsz = simd_oprsz(desc);

    ofs = LSX_LEN / 64;
    vec_clear_cause(env);
    for (i = 0; i < oprsz /16; i++) {
        for (j = 0; j < ofs; j++) {
            temp.D(j + ofs * i) = int32_to_float64(Vj->W(j + ofs * (2 * i + 1)),
                                                   &env->fp_status);
        }
        vec_update_fcsr0(env, GETPC());
    }
    *Vd = temp;
}

void HELPER(vffint_s_l)(void *vd, void *vj, void *vk,
                        CPULoongArchState *env, uint32_t desc)
{
    int i, j, ofs;
    VReg temp = {};
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;
    int oprsz = simd_oprsz(desc);

    ofs = LSX_LEN / 64;
    vec_clear_cause(env);
    for (i = 0; i < oprsz / 16; i++) {
        for (j = 0; j < ofs; j++) {
            temp.W(j + ofs * (2 * i + 1)) = int64_to_float32(Vj->D(j + ofs * i),
                                                             &env->fp_status);
            temp.W(j + ofs * 2 * i) = int64_to_float32(Vk->D(j + ofs * i),
                                                       &env->fp_status);
        }
        vec_update_fcsr0(env, GETPC());
    }
    *Vd = temp;
}

#define VCMPI(NAME, BIT, E, DO_OP)                                 \
void HELPER(NAME)(void *vd, void *vj, uint64_t imm, uint32_t desc) \
{                                                                  \
    int i;                                                         \
    VReg *Vd = (VReg *)vd;                                         \
    VReg *Vj = (VReg *)vj;                                         \
    typedef __typeof(Vd->E(0)) TD;                                 \
    int oprsz = simd_oprsz(desc);                                  \
                                                                   \
    for (i = 0; i < oprsz / (BIT / 8); i++) {                      \
        Vd->E(i) = DO_OP(Vj->E(i), (TD)imm);                       \
    }                                                              \
}

VCMPI(vseqi_b, 8, B, VSEQ)
VCMPI(vseqi_h, 16, H, VSEQ)
VCMPI(vseqi_w, 32, W, VSEQ)
VCMPI(vseqi_d, 64, D, VSEQ)
VCMPI(vslei_b, 8, B, VSLE)
VCMPI(vslei_h, 16, H, VSLE)
VCMPI(vslei_w, 32, W, VSLE)
VCMPI(vslei_d, 64, D, VSLE)
VCMPI(vslei_bu, 8, UB, VSLE)
VCMPI(vslei_hu, 16, UH, VSLE)
VCMPI(vslei_wu, 32, UW, VSLE)
VCMPI(vslei_du, 64, UD, VSLE)
VCMPI(vslti_b, 8, B, VSLT)
VCMPI(vslti_h, 16, H, VSLT)
VCMPI(vslti_w, 32, W, VSLT)
VCMPI(vslti_d, 64, D, VSLT)
VCMPI(vslti_bu, 8, UB, VSLT)
VCMPI(vslti_hu, 16, UH, VSLT)
VCMPI(vslti_wu, 32, UW, VSLT)
VCMPI(vslti_du, 64, UD, VSLT)

static uint64_t vfcmp_common(CPULoongArchState *env,
                             FloatRelation cmp, uint32_t flags)
{
    uint64_t ret = 0;

    switch (cmp) {
    case float_relation_less:
        ret = (flags & FCMP_LT);
        break;
    case float_relation_equal:
        ret = (flags & FCMP_EQ);
        break;
    case float_relation_greater:
        ret = (flags & FCMP_GT);
        break;
    case float_relation_unordered:
        ret = (flags & FCMP_UN);
        break;
    default:
        g_assert_not_reached();
    }

    if (ret) {
        ret = -1;
    }

    return ret;
}

#define VFCMP(NAME, BIT, E, FN)                                          \
void HELPER(NAME)(CPULoongArchState *env, uint32_t oprsz,                \
                  uint32_t vd, uint32_t vj, uint32_t vk, uint32_t flags) \
{                                                                        \
    int i;                                                               \
    VReg t;                                                              \
    VReg *Vd = &(env->fpr[vd].vreg);                                     \
    VReg *Vj = &(env->fpr[vj].vreg);                                     \
    VReg *Vk = &(env->fpr[vk].vreg);                                     \
                                                                         \
    vec_clear_cause(env);                                                \
    for (i = 0; i < oprsz / (BIT / 8); i++) {                            \
        FloatRelation cmp;                                               \
        cmp = FN(Vj->E(i), Vk->E(i), &env->fp_status);                   \
        t.E(i) = vfcmp_common(env, cmp, flags);                          \
        vec_update_fcsr0(env, GETPC());                                  \
    }                                                                    \
    *Vd = t;                                                             \
}

VFCMP(vfcmp_c_s, 32, UW, float32_compare_quiet)
VFCMP(vfcmp_s_s, 32, UW, float32_compare)
VFCMP(vfcmp_c_d, 64, UD, float64_compare_quiet)
VFCMP(vfcmp_s_d, 64, UD, float64_compare)

void HELPER(vbitseli_b)(void *vd, void *vj,  uint64_t imm, uint32_t desc)
{
    int i;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;

    for (i = 0; i < simd_oprsz(desc); i++) {
        Vd->B(i) = (~Vd->B(i) & Vj->B(i)) | (Vd->B(i) & imm);
    }
}

/* Copy from target/arm/tcg/sve_helper.c */
static inline bool do_match2(uint64_t n, uint64_t m0, uint64_t m1, int esz)
{
    int bits = 8 << esz;
    uint64_t ones = dup_const(esz, 1);
    uint64_t signs = ones << (bits - 1);
    uint64_t cmp0, cmp1;

    cmp1 = dup_const(esz, n);
    cmp0 = cmp1 ^ m0;
    cmp1 = cmp1 ^ m1;
    cmp0 = (cmp0 - ones) & ~cmp0;
    cmp1 = (cmp1 - ones) & ~cmp1;
    return (cmp0 | cmp1) & signs;
}

#define SETANYEQZ(NAME, MO)                                       \
void HELPER(NAME)(CPULoongArchState *env,                         \
                  uint32_t oprsz, uint32_t cd, uint32_t vj)       \
{                                                                 \
    VReg *Vj = &(env->fpr[vj].vreg);                              \
                                                                  \
    env->cf[cd & 0x7] = do_match2(0, Vj->D(0), Vj->D(1), MO);     \
    if (oprsz == 32) {                                            \
        env->cf[cd & 0x7] = env->cf[cd & 0x7] ||                  \
                            do_match2(0, Vj->D(2), Vj->D(3), MO); \
    }                                                             \
}

SETANYEQZ(vsetanyeqz_b, MO_8)
SETANYEQZ(vsetanyeqz_h, MO_16)
SETANYEQZ(vsetanyeqz_w, MO_32)
SETANYEQZ(vsetanyeqz_d, MO_64)

#define SETALLNEZ(NAME, MO)                                        \
void HELPER(NAME)(CPULoongArchState *env,                          \
                  uint32_t oprsz, uint32_t cd, uint32_t vj)        \
{                                                                  \
    VReg *Vj = &(env->fpr[vj].vreg);                               \
                                                                   \
    env->cf[cd & 0x7]= !do_match2(0, Vj->D(0), Vj->D(1), MO);      \
    if (oprsz == 32) {                                             \
        env->cf[cd & 0x7] = env->cf[cd & 0x7] &&                   \
                            !do_match2(0, Vj->D(2), Vj->D(3), MO); \
    }                                                              \
}

SETALLNEZ(vsetallnez_b, MO_8)
SETALLNEZ(vsetallnez_h, MO_16)
SETALLNEZ(vsetallnez_w, MO_32)
SETALLNEZ(vsetallnez_d, MO_64)

#define XVINSVE0(NAME, E, MASK)                                    \
void HELPER(NAME)(void *vd, void *vj, uint64_t imm, uint32_t desc) \
{                                                                  \
    VReg *Vd = (VReg *)vd;                                         \
    VReg *Vj = (VReg *)vj;                                         \
    Vd->E(imm & MASK) = Vj->E(0);                                  \
}

XVINSVE0(xvinsve0_w, W, 0x7)
XVINSVE0(xvinsve0_d, D, 0x3)

#define XVPICKVE(NAME, E, BIT, MASK)                               \
void HELPER(NAME)(void *vd, void *vj, uint64_t imm, uint32_t desc) \
{                                                                  \
    int i;                                                         \
    VReg *Vd = (VReg *)vd;                                         \
    VReg *Vj = (VReg *)vj;                                         \
    int oprsz = simd_oprsz(desc);                                  \
                                                                   \
    Vd->E(0) = Vj->E(imm & MASK);                                  \
    for (i = 1; i < oprsz / (BIT / 8); i++) {                      \
        Vd->E(i) = 0;                                              \
    }                                                              \
}

XVPICKVE(xvpickve_w, W, 32, 0x7)
XVPICKVE(xvpickve_d, D, 64, 0x3)

#define VPACKEV(NAME, BIT, E)                                  \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc) \
{                                                              \
    int i;                                                     \
    VReg temp = {};                                            \
    VReg *Vd = (VReg *)vd;                                     \
    VReg *Vj = (VReg *)vj;                                     \
    VReg *Vk = (VReg *)vk;                                     \
    int oprsz = simd_oprsz(desc);                              \
                                                               \
    for (i = 0; i < oprsz / (BIT / 8); i++) {                  \
        temp.E(2 * i + 1) = Vj->E(2 * i);                      \
        temp.E(2 *i) = Vk->E(2 * i);                           \
    }                                                          \
    *Vd = temp;                                                \
}

VPACKEV(vpackev_b, 16, B)
VPACKEV(vpackev_h, 32, H)
VPACKEV(vpackev_w, 64, W)
VPACKEV(vpackev_d, 128, D)

#define VPACKOD(NAME, BIT, E)                                  \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc) \
{                                                              \
    int i;                                                     \
    VReg temp = {};                                            \
    VReg *Vd = (VReg *)vd;                                     \
    VReg *Vj = (VReg *)vj;                                     \
    VReg *Vk = (VReg *)vk;                                     \
    int oprsz = simd_oprsz(desc);                              \
                                                               \
    for (i = 0; i < oprsz / (BIT / 8); i++) {                 \
        temp.E(2 * i + 1) = Vj->E(2 * i + 1);                  \
        temp.E(2 * i) = Vk->E(2 * i + 1);                      \
    }                                                          \
    *Vd = temp;                                                \
}

VPACKOD(vpackod_b, 16, B)
VPACKOD(vpackod_h, 32, H)
VPACKOD(vpackod_w, 64, W)
VPACKOD(vpackod_d, 128, D)

#define VPICKEV(NAME, BIT, E)                                         \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc)        \
{                                                                     \
    int i, j, ofs;                                                    \
    VReg temp = {};                                                   \
    VReg *Vd = (VReg *)vd;                                            \
    VReg *Vj = (VReg *)vj;                                            \
    VReg *Vk = (VReg *)vk;                                            \
    int oprsz = simd_oprsz(desc);                                     \
                                                                      \
    ofs = LSX_LEN / BIT;                                              \
    for (i = 0; i < oprsz / 16; i++) {                                \
        for (j = 0; j < ofs; j++) {                                   \
            temp.E(j + ofs * (2 * i + 1)) = Vj->E(2 * (j + ofs * i)); \
            temp.E(j + ofs * 2 * i) = Vk->E(2 * (j + ofs * i));       \
        }                                                             \
    }                                                                 \
    *Vd = temp;                                                       \
}

VPICKEV(vpickev_b, 16, B)
VPICKEV(vpickev_h, 32, H)
VPICKEV(vpickev_w, 64, W)
VPICKEV(vpickev_d, 128, D)

#define VPICKOD(NAME, BIT, E)                                             \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc)            \
{                                                                         \
    int i, j, ofs;                                                        \
    VReg temp = {};                                                       \
    VReg *Vd = (VReg *)vd;                                                \
    VReg *Vj = (VReg *)vj;                                                \
    VReg *Vk = (VReg *)vk;                                                \
    int oprsz = simd_oprsz(desc);                                         \
                                                                          \
    ofs = LSX_LEN / BIT;                                                  \
    for (i = 0; i < oprsz / 16; i++) {                                    \
        for (j = 0; j < ofs; j++) {                                       \
            temp.E(j + ofs * (2 * i + 1)) = Vj->E(2 * (j + ofs * i) + 1); \
            temp.E(j + ofs * 2 * i) = Vk->E(2 * (j + ofs * i) + 1);       \
        }                                                                 \
    }                                                                     \
    *Vd = temp;                                                           \
}

VPICKOD(vpickod_b, 16, B)
VPICKOD(vpickod_h, 32, H)
VPICKOD(vpickod_w, 64, W)
VPICKOD(vpickod_d, 128, D)

#define VILVL(NAME, BIT, E)                                         \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc)      \
{                                                                   \
    int i, j, ofs;                                                  \
    VReg temp = {};                                                 \
    VReg *Vd = (VReg *)vd;                                          \
    VReg *Vj = (VReg *)vj;                                          \
    VReg *Vk = (VReg *)vk;                                          \
    int oprsz = simd_oprsz(desc);                                   \
                                                                    \
    ofs = LSX_LEN / BIT;                                            \
    for (i = 0; i < oprsz / 16; i++) {                              \
        for (j = 0; j < ofs; j++) {                                 \
            temp.E(2 * (j + ofs * i) + 1) = Vj->E(j + ofs * 2 * i); \
            temp.E(2 * (j + ofs * i)) = Vk->E(j + ofs * 2 * i);     \
        }                                                           \
    }                                                               \
    *Vd = temp;                                                     \
}

VILVL(vilvl_b, 16, B)
VILVL(vilvl_h, 32, H)
VILVL(vilvl_w, 64, W)
VILVL(vilvl_d, 128, D)

#define VILVH(NAME, BIT, E)                                               \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc)            \
{                                                                         \
    int i, j, ofs;                                                        \
    VReg temp = {};                                                       \
    VReg *Vd = (VReg *)vd;                                                \
    VReg *Vj = (VReg *)vj;                                                \
    VReg *Vk = (VReg *)vk;                                                \
    int oprsz = simd_oprsz(desc);                                         \
                                                                          \
    ofs = LSX_LEN / BIT;                                                  \
    for (i = 0; i < oprsz / 16; i++) {                                    \
        for (j = 0; j < ofs; j++) {                                       \
            temp.E(2 * (j + ofs * i) + 1) = Vj->E(j + ofs * (2 * i + 1)); \
            temp.E(2 * (j + ofs * i)) = Vk->E(j + ofs * (2 * i + 1));     \
        }                                                                 \
    }                                                                     \
    *Vd = temp;                                                           \
}

VILVH(vilvh_b, 16, B)
VILVH(vilvh_h, 32, H)
VILVH(vilvh_w, 64, W)
VILVH(vilvh_d, 128, D)

void HELPER(vshuf_b)(void *vd, void *vj, void *vk, void *va, uint32_t desc)
{
    int i, j, m;
    VReg temp = {};
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;
    VReg *Va = (VReg *)va;
    int oprsz = simd_oprsz(desc);

    m = LSX_LEN / 8;
    for (i = 0; i < (oprsz / 16) * m; i++) {
        j = i < m ? 0 : 1;
        uint64_t k = (uint8_t)Va->B(i) % (2 * m);
        temp.B(i) = k < m ? Vk->B(k + j * m): Vj->B(k + (j - 1) * m);
    }
    *Vd = temp;
}

#define VSHUF(NAME, BIT, E)                                            \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t desc)         \
{                                                                      \
    int i, j, m;                                                       \
    VReg temp = {};                                                    \
    VReg *Vd = (VReg *)vd;                                             \
    VReg *Vj = (VReg *)vj;                                             \
    VReg *Vk = (VReg *)vk;                                             \
    int oprsz = simd_oprsz(desc);                                      \
                                                                       \
    m = LSX_LEN / BIT;                                                 \
    for (i = 0; i < (oprsz / 16) * m; i++) {                           \
        j = i < m ? 0 : 1;                                             \
        uint64_t k  = ((uint8_t)Vd->E(i)) % (2 * m);                   \
        temp.E(i) = k < m ? Vk->E(k + j * m) : Vj->E(k + (j - 1) * m); \
    }                                                                  \
    *Vd = temp;                                                        \
}

VSHUF(vshuf_h, 16, H)
VSHUF(vshuf_w, 32, W)
VSHUF(vshuf_d, 64, D)

#define VSHUF4I(NAME, BIT, E)                                               \
void HELPER(NAME)(void *vd, void *vj, uint64_t imm, uint32_t desc)          \
{                                                                           \
    int i, j, max;                                                          \
    VReg temp = {};                                                         \
    VReg *Vd = (VReg *)vd;                                                  \
    VReg *Vj = (VReg *)vj;                                                  \
    int oprsz = simd_oprsz(desc);                                           \
                                                                            \
    max = LSX_LEN / BIT;                                                    \
    for (i = 0; i < oprsz / (BIT / 8); i++) {                               \
        j = i < max ? 1 : 2;                                                \
        temp.E(i) = Vj->E(SHF_POS(i - ((j -1)* max), imm) + (j - 1) * max); \
    }                                                                       \
    *Vd = temp;                                                             \
}

VSHUF4I(vshuf4i_b, 8, B)
VSHUF4I(vshuf4i_h, 16, H)
VSHUF4I(vshuf4i_w, 32, W)

void HELPER(vshuf4i_d)(void *vd, void *vj, uint64_t imm, uint32_t desc)
{
    int i;
    VReg temp = {};
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    int oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz / 16; i++) {
        temp.D(2 * i) = (imm & 2 ? Vj : Vd)->D((imm & 1) + 2 * i);
        temp.D(2 * i + 1) = (imm & 8 ? Vj : Vd)->D(((imm >> 2) & 1) + 2 * i);
    }
    *Vd = temp;
}

void HELPER(vperm_w)(void *vd, void *vj, void *vk, uint32_t desc)
{
    int i, m;
    VReg temp = {};
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;

    m = LASX_LEN / 32;
    for (i = 0; i < m ; i++) {
        uint64_t k = (uint8_t)Vk->W(i) % 8;
        temp.W(i) = Vj->W(k);
    }
    *Vd = temp;
}

void HELPER(vpermi_w)(void *vd, void *vj, uint64_t imm, uint32_t desc)
{
    int i;
    VReg temp = {};
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    int oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz / 16; i++) {
        temp.W(4 * i) = Vj->W((imm & 0x3) + 4 * i);
        temp.W(4 * i + 1) = Vj->W(((imm >> 2) & 0x3) + 4 * i);
        temp.W(4 * i + 2) = Vd->W(((imm >> 4) & 0x3) + 4 * i);
        temp.W(4 * i + 3) = Vd->W(((imm >> 6) & 0x3) + 4 * i);
    }
    *Vd = temp;
}

void HELPER(vpermi_d)(void *vd, void *vj, uint64_t imm, uint32_t desc)
{
    VReg temp = {};
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;

    temp.D(0) = Vj->D(imm & 0x3);
    temp.D(1) = Vj->D((imm >> 2) & 0x3);
    temp.D(2) = Vj->D((imm >> 4) & 0x3);
    temp.D(3) = Vj->D((imm >> 6) & 0x3);
    *Vd = temp;
}

void HELPER(vpermi_q)(void *vd, void *vj, uint64_t imm, uint32_t desc)
{
    int i;
    VReg temp;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;

    for (i = 0; i < 2; i++, imm >>= 4) {
        temp.Q(i) = (imm & 2 ? Vd: Vj)->Q(imm & 1);
    }
    *Vd = temp;
}

#define VEXTRINS(NAME, BIT, E, MASK)                               \
void HELPER(NAME)(void *vd, void *vj, uint64_t imm, uint32_t desc) \
{                                                                  \
    int i, ins, extr, max;                                         \
    VReg *Vd = (VReg *)vd;                                         \
    VReg *Vj = (VReg *)vj;                                         \
    int oprsz = simd_oprsz(desc);                                  \
                                                                   \
    max = LSX_LEN / BIT;                                           \
    ins = (imm >> 4) & MASK;                                       \
    extr = imm & MASK;                                             \
    for (i = 0; i < oprsz / 16; i++) {                             \
        Vd->E(ins + i * max) = Vj->E(extr + i * max);              \
    }                                                              \
}

VEXTRINS(vextrins_b, 8, B, 0xf)
VEXTRINS(vextrins_h, 16, H, 0x7)
VEXTRINS(vextrins_w, 32, W, 0x3)
VEXTRINS(vextrins_d, 64, D, 0x1)
