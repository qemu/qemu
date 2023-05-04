/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch LSX helper functions.
 *
 * Copyright (c) 2022-2023 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"

#define DO_ADD(a, b)  (a + b)
#define DO_SUB(a, b)  (a - b)

#define DO_ODD_EVEN(NAME, BIT, E1, E2, DO_OP)                        \
void HELPER(NAME)(CPULoongArchState *env,                            \
                  uint32_t vd, uint32_t vj, uint32_t vk)             \
{                                                                    \
    int i;                                                           \
    VReg *Vd = &(env->fpr[vd].vreg);                                 \
    VReg *Vj = &(env->fpr[vj].vreg);                                 \
    VReg *Vk = &(env->fpr[vk].vreg);                                 \
    typedef __typeof(Vd->E1(0)) TD;                                  \
                                                                     \
    for (i = 0; i < LSX_LEN/BIT; i++) {                              \
        Vd->E1(i) = DO_OP((TD)Vj->E2(2 * i + 1), (TD)Vk->E2(2 * i)); \
    }                                                                \
}

DO_ODD_EVEN(vhaddw_h_b, 16, H, B, DO_ADD)
DO_ODD_EVEN(vhaddw_w_h, 32, W, H, DO_ADD)
DO_ODD_EVEN(vhaddw_d_w, 64, D, W, DO_ADD)

void HELPER(vhaddw_q_d)(CPULoongArchState *env,
                        uint32_t vd, uint32_t vj, uint32_t vk)
{
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);
    VReg *Vk = &(env->fpr[vk].vreg);

    Vd->Q(0) = int128_add(int128_makes64(Vj->D(1)), int128_makes64(Vk->D(0)));
}

DO_ODD_EVEN(vhsubw_h_b, 16, H, B, DO_SUB)
DO_ODD_EVEN(vhsubw_w_h, 32, W, H, DO_SUB)
DO_ODD_EVEN(vhsubw_d_w, 64, D, W, DO_SUB)

void HELPER(vhsubw_q_d)(CPULoongArchState *env,
                        uint32_t vd, uint32_t vj, uint32_t vk)
{
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);
    VReg *Vk = &(env->fpr[vk].vreg);

    Vd->Q(0) = int128_sub(int128_makes64(Vj->D(1)), int128_makes64(Vk->D(0)));
}

DO_ODD_EVEN(vhaddw_hu_bu, 16, UH, UB, DO_ADD)
DO_ODD_EVEN(vhaddw_wu_hu, 32, UW, UH, DO_ADD)
DO_ODD_EVEN(vhaddw_du_wu, 64, UD, UW, DO_ADD)

void HELPER(vhaddw_qu_du)(CPULoongArchState *env,
                          uint32_t vd, uint32_t vj, uint32_t vk)
{
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);
    VReg *Vk = &(env->fpr[vk].vreg);

    Vd->Q(0) = int128_add(int128_make64((uint64_t)Vj->D(1)),
                          int128_make64((uint64_t)Vk->D(0)));
}

DO_ODD_EVEN(vhsubw_hu_bu, 16, UH, UB, DO_SUB)
DO_ODD_EVEN(vhsubw_wu_hu, 32, UW, UH, DO_SUB)
DO_ODD_EVEN(vhsubw_du_wu, 64, UD, UW, DO_SUB)

void HELPER(vhsubw_qu_du)(CPULoongArchState *env,
                          uint32_t vd, uint32_t vj, uint32_t vk)
{
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);
    VReg *Vk = &(env->fpr[vk].vreg);

    Vd->Q(0) = int128_sub(int128_make64((uint64_t)Vj->D(1)),
                          int128_make64((uint64_t)Vk->D(0)));
}

#define DO_EVEN(NAME, BIT, E1, E2, DO_OP)                        \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t v)      \
{                                                                \
    int i;                                                       \
    VReg *Vd = (VReg *)vd;                                       \
    VReg *Vj = (VReg *)vj;                                       \
    VReg *Vk = (VReg *)vk;                                       \
    typedef __typeof(Vd->E1(0)) TD;                              \
    for (i = 0; i < LSX_LEN/BIT; i++) {                          \
        Vd->E1(i) = DO_OP((TD)Vj->E2(2 * i) ,(TD)Vk->E2(2 * i)); \
    }                                                            \
}

#define DO_ODD(NAME, BIT, E1, E2, DO_OP)                                 \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t v)              \
{                                                                        \
    int i;                                                               \
    VReg *Vd = (VReg *)vd;                                               \
    VReg *Vj = (VReg *)vj;                                               \
    VReg *Vk = (VReg *)vk;                                               \
    typedef __typeof(Vd->E1(0)) TD;                                      \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                  \
        Vd->E1(i) = DO_OP((TD)Vj->E2(2 * i + 1), (TD)Vk->E2(2 * i + 1)); \
    }                                                                    \
}

void HELPER(vaddwev_q_d)(void *vd, void *vj, void *vk, uint32_t v)
{
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;

    Vd->Q(0) = int128_add(int128_makes64(Vj->D(0)), int128_makes64(Vk->D(0)));
}

DO_EVEN(vaddwev_h_b, 16, H, B, DO_ADD)
DO_EVEN(vaddwev_w_h, 32, W, H, DO_ADD)
DO_EVEN(vaddwev_d_w, 64, D, W, DO_ADD)

void HELPER(vaddwod_q_d)(void *vd, void *vj, void *vk, uint32_t v)
{
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;

    Vd->Q(0) = int128_add(int128_makes64(Vj->D(1)), int128_makes64(Vk->D(1)));
}

DO_ODD(vaddwod_h_b, 16, H, B, DO_ADD)
DO_ODD(vaddwod_w_h, 32, W, H, DO_ADD)
DO_ODD(vaddwod_d_w, 64, D, W, DO_ADD)

void HELPER(vsubwev_q_d)(void *vd, void *vj, void *vk, uint32_t v)
{
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;

    Vd->Q(0) = int128_sub(int128_makes64(Vj->D(0)), int128_makes64(Vk->D(0)));
}

DO_EVEN(vsubwev_h_b, 16, H, B, DO_SUB)
DO_EVEN(vsubwev_w_h, 32, W, H, DO_SUB)
DO_EVEN(vsubwev_d_w, 64, D, W, DO_SUB)

void HELPER(vsubwod_q_d)(void *vd, void *vj, void *vk, uint32_t v)
{
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;

    Vd->Q(0) = int128_sub(int128_makes64(Vj->D(1)), int128_makes64(Vk->D(1)));
}

DO_ODD(vsubwod_h_b, 16, H, B, DO_SUB)
DO_ODD(vsubwod_w_h, 32, W, H, DO_SUB)
DO_ODD(vsubwod_d_w, 64, D, W, DO_SUB)

void HELPER(vaddwev_q_du)(void *vd, void *vj, void *vk, uint32_t v)
{
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;

    Vd->Q(0) = int128_add(int128_make64((uint64_t)Vj->D(0)),
                          int128_make64((uint64_t)Vk->D(0)));
}

DO_EVEN(vaddwev_h_bu, 16, UH, UB, DO_ADD)
DO_EVEN(vaddwev_w_hu, 32, UW, UH, DO_ADD)
DO_EVEN(vaddwev_d_wu, 64, UD, UW, DO_ADD)

void HELPER(vaddwod_q_du)(void *vd, void *vj, void *vk, uint32_t v)
{
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;

    Vd->Q(0) = int128_add(int128_make64((uint64_t)Vj->D(1)),
                          int128_make64((uint64_t)Vk->D(1)));
}

DO_ODD(vaddwod_h_bu, 16, UH, UB, DO_ADD)
DO_ODD(vaddwod_w_hu, 32, UW, UH, DO_ADD)
DO_ODD(vaddwod_d_wu, 64, UD, UW, DO_ADD)

void HELPER(vsubwev_q_du)(void *vd, void *vj, void *vk, uint32_t v)
{
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;

    Vd->Q(0) = int128_sub(int128_make64((uint64_t)Vj->D(0)),
                          int128_make64((uint64_t)Vk->D(0)));
}

DO_EVEN(vsubwev_h_bu, 16, UH, UB, DO_SUB)
DO_EVEN(vsubwev_w_hu, 32, UW, UH, DO_SUB)
DO_EVEN(vsubwev_d_wu, 64, UD, UW, DO_SUB)

void HELPER(vsubwod_q_du)(void *vd, void *vj, void *vk, uint32_t v)
{
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;

    Vd->Q(0) = int128_sub(int128_make64((uint64_t)Vj->D(1)),
                          int128_make64((uint64_t)Vk->D(1)));
}

DO_ODD(vsubwod_h_bu, 16, UH, UB, DO_SUB)
DO_ODD(vsubwod_w_hu, 32, UW, UH, DO_SUB)
DO_ODD(vsubwod_d_wu, 64, UD, UW, DO_SUB)

#define DO_EVEN_U_S(NAME, BIT, ES1, EU1, ES2, EU2, DO_OP)             \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t v)           \
{                                                                     \
    int i;                                                            \
    VReg *Vd = (VReg *)vd;                                            \
    VReg *Vj = (VReg *)vj;                                            \
    VReg *Vk = (VReg *)vk;                                            \
    typedef __typeof(Vd->ES1(0)) TDS;                                 \
    typedef __typeof(Vd->EU1(0)) TDU;                                 \
    for (i = 0; i < LSX_LEN/BIT; i++) {                               \
        Vd->ES1(i) = DO_OP((TDU)Vj->EU2(2 * i) ,(TDS)Vk->ES2(2 * i)); \
    }                                                                 \
}

#define DO_ODD_U_S(NAME, BIT, ES1, EU1, ES2, EU2, DO_OP)                      \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t v)                   \
{                                                                             \
    int i;                                                                    \
    VReg *Vd = (VReg *)vd;                                                    \
    VReg *Vj = (VReg *)vj;                                                    \
    VReg *Vk = (VReg *)vk;                                                    \
    typedef __typeof(Vd->ES1(0)) TDS;                                         \
    typedef __typeof(Vd->EU1(0)) TDU;                                         \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                       \
        Vd->ES1(i) = DO_OP((TDU)Vj->EU2(2 * i + 1), (TDS)Vk->ES2(2 * i + 1)); \
    }                                                                         \
}

void HELPER(vaddwev_q_du_d)(void *vd, void *vj, void *vk, uint32_t v)
{
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;

    Vd->Q(0) = int128_add(int128_make64((uint64_t)Vj->D(0)),
                          int128_makes64(Vk->D(0)));
}

DO_EVEN_U_S(vaddwev_h_bu_b, 16, H, UH, B, UB, DO_ADD)
DO_EVEN_U_S(vaddwev_w_hu_h, 32, W, UW, H, UH, DO_ADD)
DO_EVEN_U_S(vaddwev_d_wu_w, 64, D, UD, W, UW, DO_ADD)

void HELPER(vaddwod_q_du_d)(void *vd, void *vj, void *vk, uint32_t v)
{
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;

    Vd->Q(0) = int128_add(int128_make64((uint64_t)Vj->D(1)),
                          int128_makes64(Vk->D(1)));
}

DO_ODD_U_S(vaddwod_h_bu_b, 16, H, UH, B, UB, DO_ADD)
DO_ODD_U_S(vaddwod_w_hu_h, 32, W, UW, H, UH, DO_ADD)
DO_ODD_U_S(vaddwod_d_wu_w, 64, D, UD, W, UW, DO_ADD)

#define DO_VAVG(a, b)  ((a >> 1) + (b >> 1) + (a & b & 1))
#define DO_VAVGR(a, b) ((a >> 1) + (b >> 1) + ((a | b) & 1))

#define DO_3OP(NAME, BIT, E, DO_OP)                         \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t v) \
{                                                           \
    int i;                                                  \
    VReg *Vd = (VReg *)vd;                                  \
    VReg *Vj = (VReg *)vj;                                  \
    VReg *Vk = (VReg *)vk;                                  \
    for (i = 0; i < LSX_LEN/BIT; i++) {                     \
        Vd->E(i) = DO_OP(Vj->E(i), Vk->E(i));               \
    }                                                       \
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

#define DO_VABSD(a, b)  ((a > b) ? (a -b) : (b-a))

DO_3OP(vabsd_b, 8, B, DO_VABSD)
DO_3OP(vabsd_h, 16, H, DO_VABSD)
DO_3OP(vabsd_w, 32, W, DO_VABSD)
DO_3OP(vabsd_d, 64, D, DO_VABSD)
DO_3OP(vabsd_bu, 8, UB, DO_VABSD)
DO_3OP(vabsd_hu, 16, UH, DO_VABSD)
DO_3OP(vabsd_wu, 32, UW, DO_VABSD)
DO_3OP(vabsd_du, 64, UD, DO_VABSD)

#define DO_VABS(a)  ((a < 0) ? (-a) : (a))

#define DO_VADDA(NAME, BIT, E, DO_OP)                       \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t v) \
{                                                           \
    int i;                                                  \
    VReg *Vd = (VReg *)vd;                                  \
    VReg *Vj = (VReg *)vj;                                  \
    VReg *Vk = (VReg *)vk;                                  \
    for (i = 0; i < LSX_LEN/BIT; i++) {                     \
        Vd->E(i) = DO_OP(Vj->E(i)) + DO_OP(Vk->E(i));       \
    }                                                       \
}

DO_VADDA(vadda_b, 8, B, DO_VABS)
DO_VADDA(vadda_h, 16, H, DO_VABS)
DO_VADDA(vadda_w, 32, W, DO_VABS)
DO_VADDA(vadda_d, 64, D, DO_VABS)

#define DO_MIN(a, b) (a < b ? a : b)
#define DO_MAX(a, b) (a > b ? a : b)

#define VMINMAXI(NAME, BIT, E, DO_OP)                           \
void HELPER(NAME)(void *vd, void *vj, uint64_t imm, uint32_t v) \
{                                                               \
    int i;                                                      \
    VReg *Vd = (VReg *)vd;                                      \
    VReg *Vj = (VReg *)vj;                                      \
    typedef __typeof(Vd->E(0)) TD;                              \
                                                                \
    for (i = 0; i < LSX_LEN/BIT; i++) {                         \
        Vd->E(i) = DO_OP(Vj->E(i), (TD)imm);                    \
    }                                                           \
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

#define DO_VMUH(NAME, BIT, E1, E2, DO_OP)                   \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t v) \
{                                                           \
    int i;                                                  \
    VReg *Vd = (VReg *)vd;                                  \
    VReg *Vj = (VReg *)vj;                                  \
    VReg *Vk = (VReg *)vk;                                  \
    typedef __typeof(Vd->E1(0)) T;                          \
                                                            \
    for (i = 0; i < LSX_LEN/BIT; i++) {                     \
        Vd->E2(i) = ((T)Vj->E2(i)) * ((T)Vk->E2(i)) >> BIT; \
    }                                                       \
}

void HELPER(vmuh_d)(void *vd, void *vj, void *vk, uint32_t v)
{
    uint64_t l, h1, h2;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;

    muls64(&l, &h1, Vj->D(0), Vk->D(0));
    muls64(&l, &h2, Vj->D(1), Vk->D(1));

    Vd->D(0) = h1;
    Vd->D(1) = h2;
}

DO_VMUH(vmuh_b, 8, H, B, DO_MUH)
DO_VMUH(vmuh_h, 16, W, H, DO_MUH)
DO_VMUH(vmuh_w, 32, D, W, DO_MUH)

void HELPER(vmuh_du)(void *vd, void *vj, void *vk, uint32_t v)
{
    uint64_t l, h1, h2;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;
    VReg *Vk = (VReg *)vk;

    mulu64(&l, &h1, Vj->D(0), Vk->D(0));
    mulu64(&l, &h2, Vj->D(1), Vk->D(1));

    Vd->D(0) = h1;
    Vd->D(1) = h2;
}

DO_VMUH(vmuh_bu, 8, UH, UB, DO_MUH)
DO_VMUH(vmuh_hu, 16, UW, UH, DO_MUH)
DO_VMUH(vmuh_wu, 32, UD, UW, DO_MUH)

#define DO_MUL(a, b) (a * b)

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

#define DO_MADD(a, b, c)  (a + b * c)
#define DO_MSUB(a, b, c)  (a - b * c)

#define VMADDSUB(NAME, BIT, E, DO_OP)                       \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t v) \
{                                                           \
    int i;                                                  \
    VReg *Vd = (VReg *)vd;                                  \
    VReg *Vj = (VReg *)vj;                                  \
    VReg *Vk = (VReg *)vk;                                  \
    for (i = 0; i < LSX_LEN/BIT; i++) {                     \
        Vd->E(i) = DO_OP(Vd->E(i), Vj->E(i) ,Vk->E(i));     \
    }                                                       \
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
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t v)       \
{                                                                 \
    int i;                                                        \
    VReg *Vd = (VReg *)vd;                                        \
    VReg *Vj = (VReg *)vj;                                        \
    VReg *Vk = (VReg *)vk;                                        \
    typedef __typeof(Vd->E1(0)) TD;                               \
                                                                  \
    for (i = 0; i < LSX_LEN/BIT; i++) {                           \
        Vd->E1(i) += DO_OP((TD)Vj->E2(2 * i), (TD)Vk->E2(2 * i)); \
    }                                                             \
}

VMADDWEV(vmaddwev_h_b, 16, H, B, DO_MUL)
VMADDWEV(vmaddwev_w_h, 32, W, H, DO_MUL)
VMADDWEV(vmaddwev_d_w, 64, D, W, DO_MUL)
VMADDWEV(vmaddwev_h_bu, 16, UH, UB, DO_MUL)
VMADDWEV(vmaddwev_w_hu, 32, UW, UH, DO_MUL)
VMADDWEV(vmaddwev_d_wu, 64, UD, UW, DO_MUL)

#define VMADDWOD(NAME, BIT, E1, E2, DO_OP)                  \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t v) \
{                                                           \
    int i;                                                  \
    VReg *Vd = (VReg *)vd;                                  \
    VReg *Vj = (VReg *)vj;                                  \
    VReg *Vk = (VReg *)vk;                                  \
    typedef __typeof(Vd->E1(0)) TD;                         \
                                                            \
    for (i = 0; i < LSX_LEN/BIT; i++) {                     \
        Vd->E1(i) += DO_OP((TD)Vj->E2(2 * i + 1),           \
                           (TD)Vk->E2(2 * i + 1));          \
    }                                                       \
}

VMADDWOD(vmaddwod_h_b, 16, H, B, DO_MUL)
VMADDWOD(vmaddwod_w_h, 32, W, H, DO_MUL)
VMADDWOD(vmaddwod_d_w, 64, D, W, DO_MUL)
VMADDWOD(vmaddwod_h_bu, 16,  UH, UB, DO_MUL)
VMADDWOD(vmaddwod_w_hu, 32,  UW, UH, DO_MUL)
VMADDWOD(vmaddwod_d_wu, 64,  UD, UW, DO_MUL)

#define VMADDWEV_U_S(NAME, BIT, ES1, EU1, ES2, EU2, DO_OP)  \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t v) \
{                                                           \
    int i;                                                  \
    VReg *Vd = (VReg *)vd;                                  \
    VReg *Vj = (VReg *)vj;                                  \
    VReg *Vk = (VReg *)vk;                                  \
    typedef __typeof(Vd->ES1(0)) TS1;                       \
    typedef __typeof(Vd->EU1(0)) TU1;                       \
                                                            \
    for (i = 0; i < LSX_LEN/BIT; i++) {                     \
        Vd->ES1(i) += DO_OP((TU1)Vj->EU2(2 * i),            \
                            (TS1)Vk->ES2(2 * i));           \
    }                                                       \
}

VMADDWEV_U_S(vmaddwev_h_bu_b, 16, H, UH, B, UB, DO_MUL)
VMADDWEV_U_S(vmaddwev_w_hu_h, 32, W, UW, H, UH, DO_MUL)
VMADDWEV_U_S(vmaddwev_d_wu_w, 64, D, UD, W, UW, DO_MUL)

#define VMADDWOD_U_S(NAME, BIT, ES1, EU1, ES2, EU2, DO_OP)  \
void HELPER(NAME)(void *vd, void *vj, void *vk, uint32_t v) \
{                                                           \
    int i;                                                  \
    VReg *Vd = (VReg *)vd;                                  \
    VReg *Vj = (VReg *)vj;                                  \
    VReg *Vk = (VReg *)vk;                                  \
    typedef __typeof(Vd->ES1(0)) TS1;                       \
    typedef __typeof(Vd->EU1(0)) TU1;                       \
                                                            \
    for (i = 0; i < LSX_LEN/BIT; i++) {                     \
        Vd->ES1(i) += DO_OP((TU1)Vj->EU2(2 * i + 1),         \
                            (TS1)Vk->ES2(2 * i + 1));        \
    }                                                       \
}

VMADDWOD_U_S(vmaddwod_h_bu_b, 16, H, UH, B, UB, DO_MUL)
VMADDWOD_U_S(vmaddwod_w_hu_h, 32, W, UW, H, UH, DO_MUL)
VMADDWOD_U_S(vmaddwod_d_wu_w, 64, D, UD, W, UW, DO_MUL)

#define DO_DIVU(N, M) (unlikely(M == 0) ? 0 : N / M)
#define DO_REMU(N, M) (unlikely(M == 0) ? 0 : N % M)
#define DO_DIV(N, M)  (unlikely(M == 0) ? 0 :\
        unlikely((N == -N) && (M == (__typeof(N))(-1))) ? N : N / M)
#define DO_REM(N, M)  (unlikely(M == 0) ? 0 :\
        unlikely((N == -N) && (M == (__typeof(N))(-1))) ? 0 : N % M)

#define VDIV(NAME, BIT, E, DO_OP)                           \
void HELPER(NAME)(CPULoongArchState *env,                   \
                  uint32_t vd, uint32_t vj, uint32_t vk)    \
{                                                           \
    int i;                                                  \
    VReg *Vd = &(env->fpr[vd].vreg);                        \
    VReg *Vj = &(env->fpr[vj].vreg);                        \
    VReg *Vk = &(env->fpr[vk].vreg);                        \
    for (i = 0; i < LSX_LEN/BIT; i++) {                     \
        Vd->E(i) = DO_OP(Vj->E(i), Vk->E(i));               \
    }                                                       \
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

#define VSAT_S(NAME, BIT, E)                                    \
void HELPER(NAME)(void *vd, void *vj, uint64_t max, uint32_t v) \
{                                                               \
    int i;                                                      \
    VReg *Vd = (VReg *)vd;                                      \
    VReg *Vj = (VReg *)vj;                                      \
    typedef __typeof(Vd->E(0)) TD;                              \
                                                                \
    for (i = 0; i < LSX_LEN/BIT; i++) {                         \
        Vd->E(i) = Vj->E(i) > (TD)max ? (TD)max :               \
                   Vj->E(i) < (TD)~max ? (TD)~max: Vj->E(i);    \
    }                                                           \
}

VSAT_S(vsat_b, 8, B)
VSAT_S(vsat_h, 16, H)
VSAT_S(vsat_w, 32, W)
VSAT_S(vsat_d, 64, D)

#define VSAT_U(NAME, BIT, E)                                    \
void HELPER(NAME)(void *vd, void *vj, uint64_t max, uint32_t v) \
{                                                               \
    int i;                                                      \
    VReg *Vd = (VReg *)vd;                                      \
    VReg *Vj = (VReg *)vj;                                      \
    typedef __typeof(Vd->E(0)) TD;                              \
                                                                \
    for (i = 0; i < LSX_LEN/BIT; i++) {                         \
        Vd->E(i) = Vj->E(i) > (TD)max ? (TD)max : Vj->E(i);     \
    }                                                           \
}

VSAT_U(vsat_bu, 8, UB)
VSAT_U(vsat_hu, 16, UH)
VSAT_U(vsat_wu, 32, UW)
VSAT_U(vsat_du, 64, UD)

#define VEXTH(NAME, BIT, E1, E2)                                    \
void HELPER(NAME)(CPULoongArchState *env, uint32_t vd, uint32_t vj) \
{                                                                   \
    int i;                                                          \
    VReg *Vd = &(env->fpr[vd].vreg);                                \
    VReg *Vj = &(env->fpr[vj].vreg);                                \
                                                                    \
    for (i = 0; i < LSX_LEN/BIT; i++) {                             \
        Vd->E1(i) = Vj->E2(i + LSX_LEN/BIT);                        \
    }                                                               \
}

void HELPER(vexth_q_d)(CPULoongArchState *env, uint32_t vd, uint32_t vj)
{
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    Vd->Q(0) = int128_makes64(Vj->D(1));
}

void HELPER(vexth_qu_du)(CPULoongArchState *env, uint32_t vd, uint32_t vj)
{
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    Vd->Q(0) = int128_make64((uint64_t)Vj->D(1));
}

VEXTH(vexth_h_b, 16, H, B)
VEXTH(vexth_w_h, 32, W, H)
VEXTH(vexth_d_w, 64, D, W)
VEXTH(vexth_hu_bu, 16, UH, UB)
VEXTH(vexth_wu_hu, 32, UW, UH)
VEXTH(vexth_du_wu, 64, UD, UW)

#define DO_SIGNCOV(a, b)  (a == 0 ? 0 : a < 0 ? -b : b)

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

void HELPER(vmskltz_b)(CPULoongArchState *env, uint32_t vd, uint32_t vj)
{
    uint16_t temp = 0;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    temp = do_vmskltz_b(Vj->D(0));
    temp |= (do_vmskltz_b(Vj->D(1)) << 8);
    Vd->D(0) = temp;
    Vd->D(1) = 0;
}

static uint64_t do_vmskltz_h(int64_t val)
{
    uint64_t m = 0x8000800080008000ULL;
    uint64_t c =  val & m;
    c |= c << 15;
    c |= c << 30;
    return c >> 60;
}

void HELPER(vmskltz_h)(CPULoongArchState *env, uint32_t vd, uint32_t vj)
{
    uint16_t temp = 0;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    temp = do_vmskltz_h(Vj->D(0));
    temp |= (do_vmskltz_h(Vj->D(1)) << 4);
    Vd->D(0) = temp;
    Vd->D(1) = 0;
}

static uint64_t do_vmskltz_w(int64_t val)
{
    uint64_t m = 0x8000000080000000ULL;
    uint64_t c =  val & m;
    c |= c << 31;
    return c >> 62;
}

void HELPER(vmskltz_w)(CPULoongArchState *env, uint32_t vd, uint32_t vj)
{
    uint16_t temp = 0;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    temp = do_vmskltz_w(Vj->D(0));
    temp |= (do_vmskltz_w(Vj->D(1)) << 2);
    Vd->D(0) = temp;
    Vd->D(1) = 0;
}

static uint64_t do_vmskltz_d(int64_t val)
{
    return (uint64_t)val >> 63;
}
void HELPER(vmskltz_d)(CPULoongArchState *env, uint32_t vd, uint32_t vj)
{
    uint16_t temp = 0;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    temp = do_vmskltz_d(Vj->D(0));
    temp |= (do_vmskltz_d(Vj->D(1)) << 1);
    Vd->D(0) = temp;
    Vd->D(1) = 0;
}

void HELPER(vmskgez_b)(CPULoongArchState *env, uint32_t vd, uint32_t vj)
{
    uint16_t temp = 0;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    temp =  do_vmskltz_b(Vj->D(0));
    temp |= (do_vmskltz_b(Vj->D(1)) << 8);
    Vd->D(0) = (uint16_t)(~temp);
    Vd->D(1) = 0;
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

void HELPER(vmsknz_b)(CPULoongArchState *env, uint32_t vd, uint32_t vj)
{
    uint16_t temp = 0;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    temp = do_vmskez_b(Vj->D(0));
    temp |= (do_vmskez_b(Vj->D(1)) << 8);
    Vd->D(0) = (uint16_t)(~temp);
    Vd->D(1) = 0;
}

void HELPER(vnori_b)(void *vd, void *vj, uint64_t imm, uint32_t v)
{
    int i;
    VReg *Vd = (VReg *)vd;
    VReg *Vj = (VReg *)vj;

    for (i = 0; i < LSX_LEN/8; i++) {
        Vd->B(i) = ~(Vj->B(i) | (uint8_t)imm);
    }
}

#define VSLLWIL(NAME, BIT, E1, E2)                        \
void HELPER(NAME)(CPULoongArchState *env,                 \
                  uint32_t vd, uint32_t vj, uint32_t imm) \
{                                                         \
    int i;                                                \
    VReg temp;                                            \
    VReg *Vd = &(env->fpr[vd].vreg);                      \
    VReg *Vj = &(env->fpr[vj].vreg);                      \
    typedef __typeof(temp.E1(0)) TD;                      \
                                                          \
    temp.D(0) = 0;                                        \
    temp.D(1) = 0;                                        \
    for (i = 0; i < LSX_LEN/BIT; i++) {                   \
        temp.E1(i) = (TD)Vj->E2(i) << (imm % BIT);        \
    }                                                     \
    *Vd = temp;                                           \
}

void HELPER(vextl_q_d)(CPULoongArchState *env, uint32_t vd, uint32_t vj)
{
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    Vd->Q(0) = int128_makes64(Vj->D(0));
}

void HELPER(vextl_qu_du)(CPULoongArchState *env, uint32_t vd, uint32_t vj)
{
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    Vd->Q(0) = int128_make64(Vj->D(0));
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
void HELPER(NAME)(CPULoongArchState *env,                       \
                  uint32_t vd, uint32_t vj, uint32_t vk)        \
{                                                               \
    int i;                                                      \
    VReg *Vd = &(env->fpr[vd].vreg);                            \
    VReg *Vj = &(env->fpr[vj].vreg);                            \
    VReg *Vk = &(env->fpr[vk].vreg);                            \
                                                                \
    for (i = 0; i < LSX_LEN/BIT; i++) {                         \
        Vd->E(i) = do_vsrlr_ ## E(Vj->E(i), ((T)Vk->E(i))%BIT); \
    }                                                           \
}

VSRLR(vsrlr_b, 8,  uint8_t, B)
VSRLR(vsrlr_h, 16, uint16_t, H)
VSRLR(vsrlr_w, 32, uint32_t, W)
VSRLR(vsrlr_d, 64, uint64_t, D)

#define VSRLRI(NAME, BIT, E)                              \
void HELPER(NAME)(CPULoongArchState *env,                 \
                  uint32_t vd, uint32_t vj, uint32_t imm) \
{                                                         \
    int i;                                                \
    VReg *Vd = &(env->fpr[vd].vreg);                      \
    VReg *Vj = &(env->fpr[vj].vreg);                      \
                                                          \
    for (i = 0; i < LSX_LEN/BIT; i++) {                   \
        Vd->E(i) = do_vsrlr_ ## E(Vj->E(i), imm);         \
    }                                                     \
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
void HELPER(NAME)(CPULoongArchState *env,                       \
                  uint32_t vd, uint32_t vj, uint32_t vk)        \
{                                                               \
    int i;                                                      \
    VReg *Vd = &(env->fpr[vd].vreg);                            \
    VReg *Vj = &(env->fpr[vj].vreg);                            \
    VReg *Vk = &(env->fpr[vk].vreg);                            \
                                                                \
    for (i = 0; i < LSX_LEN/BIT; i++) {                         \
        Vd->E(i) = do_vsrar_ ## E(Vj->E(i), ((T)Vk->E(i))%BIT); \
    }                                                           \
}

VSRAR(vsrar_b, 8,  uint8_t, B)
VSRAR(vsrar_h, 16, uint16_t, H)
VSRAR(vsrar_w, 32, uint32_t, W)
VSRAR(vsrar_d, 64, uint64_t, D)

#define VSRARI(NAME, BIT, E)                              \
void HELPER(NAME)(CPULoongArchState *env,                 \
                  uint32_t vd, uint32_t vj, uint32_t imm) \
{                                                         \
    int i;                                                \
    VReg *Vd = &(env->fpr[vd].vreg);                      \
    VReg *Vj = &(env->fpr[vj].vreg);                      \
                                                          \
    for (i = 0; i < LSX_LEN/BIT; i++) {                   \
        Vd->E(i) = do_vsrar_ ## E(Vj->E(i), imm);         \
    }                                                     \
}

VSRARI(vsrari_b, 8, B)
VSRARI(vsrari_h, 16, H)
VSRARI(vsrari_w, 32, W)
VSRARI(vsrari_d, 64, D)

#define R_SHIFT(a, b) (a >> b)

#define VSRLN(NAME, BIT, T, E1, E2)                             \
void HELPER(NAME)(CPULoongArchState *env,                       \
                  uint32_t vd, uint32_t vj, uint32_t vk)        \
{                                                               \
    int i;                                                      \
    VReg *Vd = &(env->fpr[vd].vreg);                            \
    VReg *Vj = &(env->fpr[vj].vreg);                            \
    VReg *Vk = &(env->fpr[vk].vreg);                            \
                                                                \
    for (i = 0; i < LSX_LEN/BIT; i++) {                         \
        Vd->E1(i) = R_SHIFT((T)Vj->E2(i),((T)Vk->E2(i)) % BIT); \
    }                                                           \
    Vd->D(1) = 0;                                               \
}

VSRLN(vsrln_b_h, 16, uint16_t, B, H)
VSRLN(vsrln_h_w, 32, uint32_t, H, W)
VSRLN(vsrln_w_d, 64, uint64_t, W, D)

#define VSRAN(NAME, BIT, T, E1, E2)                           \
void HELPER(NAME)(CPULoongArchState *env,                     \
                  uint32_t vd, uint32_t vj, uint32_t vk)      \
{                                                             \
    int i;                                                    \
    VReg *Vd = &(env->fpr[vd].vreg);                          \
    VReg *Vj = &(env->fpr[vj].vreg);                          \
    VReg *Vk = &(env->fpr[vk].vreg);                          \
                                                              \
    for (i = 0; i < LSX_LEN/BIT; i++) {                       \
        Vd->E1(i) = R_SHIFT(Vj->E2(i), ((T)Vk->E2(i)) % BIT); \
    }                                                         \
    Vd->D(1) = 0;                                             \
}

VSRAN(vsran_b_h, 16, uint16_t, B, H)
VSRAN(vsran_h_w, 32, uint32_t, H, W)
VSRAN(vsran_w_d, 64, uint64_t, W, D)

#define VSRLNI(NAME, BIT, T, E1, E2)                         \
void HELPER(NAME)(CPULoongArchState *env,                    \
                  uint32_t vd, uint32_t vj, uint32_t imm)    \
{                                                            \
    int i, max;                                              \
    VReg temp;                                               \
    VReg *Vd = &(env->fpr[vd].vreg);                         \
    VReg *Vj = &(env->fpr[vj].vreg);                         \
                                                             \
    temp.D(0) = 0;                                           \
    temp.D(1) = 0;                                           \
    max = LSX_LEN/BIT;                                       \
    for (i = 0; i < max; i++) {                              \
        temp.E1(i) = R_SHIFT((T)Vj->E2(i), imm);             \
        temp.E1(i + max) = R_SHIFT((T)Vd->E2(i), imm);       \
    }                                                        \
    *Vd = temp;                                              \
}

void HELPER(vsrlni_d_q)(CPULoongArchState *env,
                        uint32_t vd, uint32_t vj, uint32_t imm)
{
    VReg temp;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    temp.D(0) = 0;
    temp.D(1) = 0;
    temp.D(0) = int128_getlo(int128_urshift(Vj->Q(0), imm % 128));
    temp.D(1) = int128_getlo(int128_urshift(Vd->Q(0), imm % 128));
    *Vd = temp;
}

VSRLNI(vsrlni_b_h, 16, uint16_t, B, H)
VSRLNI(vsrlni_h_w, 32, uint32_t, H, W)
VSRLNI(vsrlni_w_d, 64, uint64_t, W, D)

#define VSRANI(NAME, BIT, E1, E2)                         \
void HELPER(NAME)(CPULoongArchState *env,                 \
                  uint32_t vd, uint32_t vj, uint32_t imm) \
{                                                         \
    int i, max;                                           \
    VReg temp;                                            \
    VReg *Vd = &(env->fpr[vd].vreg);                      \
    VReg *Vj = &(env->fpr[vj].vreg);                      \
                                                          \
    temp.D(0) = 0;                                        \
    temp.D(1) = 0;                                        \
    max = LSX_LEN/BIT;                                    \
    for (i = 0; i < max; i++) {                           \
        temp.E1(i) = R_SHIFT(Vj->E2(i), imm);             \
        temp.E1(i + max) = R_SHIFT(Vd->E2(i), imm);       \
    }                                                     \
    *Vd = temp;                                           \
}

void HELPER(vsrani_d_q)(CPULoongArchState *env,
                        uint32_t vd, uint32_t vj, uint32_t imm)
{
    VReg temp;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    temp.D(0) = 0;
    temp.D(1) = 0;
    temp.D(0) = int128_getlo(int128_rshift(Vj->Q(0), imm % 128));
    temp.D(1) = int128_getlo(int128_rshift(Vd->Q(0), imm % 128));
    *Vd = temp;
}

VSRANI(vsrani_b_h, 16, B, H)
VSRANI(vsrani_h_w, 32, H, W)
VSRANI(vsrani_w_d, 64, W, D)

#define VSRLRN(NAME, BIT, T, E1, E2)                                \
void HELPER(NAME)(CPULoongArchState *env,                           \
                  uint32_t vd, uint32_t vj, uint32_t vk)            \
{                                                                   \
    int i;                                                          \
    VReg *Vd = &(env->fpr[vd].vreg);                                \
    VReg *Vj = &(env->fpr[vj].vreg);                                \
    VReg *Vk = &(env->fpr[vk].vreg);                                \
                                                                    \
    for (i = 0; i < LSX_LEN/BIT; i++) {                             \
        Vd->E1(i) = do_vsrlr_ ## E2(Vj->E2(i), ((T)Vk->E2(i))%BIT); \
    }                                                               \
    Vd->D(1) = 0;                                                   \
}

VSRLRN(vsrlrn_b_h, 16, uint16_t, B, H)
VSRLRN(vsrlrn_h_w, 32, uint32_t, H, W)
VSRLRN(vsrlrn_w_d, 64, uint64_t, W, D)

#define VSRARN(NAME, BIT, T, E1, E2)                                \
void HELPER(NAME)(CPULoongArchState *env,                           \
                  uint32_t vd, uint32_t vj, uint32_t vk)            \
{                                                                   \
    int i;                                                          \
    VReg *Vd = &(env->fpr[vd].vreg);                                \
    VReg *Vj = &(env->fpr[vj].vreg);                                \
    VReg *Vk = &(env->fpr[vk].vreg);                                \
                                                                    \
    for (i = 0; i < LSX_LEN/BIT; i++) {                             \
        Vd->E1(i) = do_vsrar_ ## E2(Vj->E2(i), ((T)Vk->E2(i))%BIT); \
    }                                                               \
    Vd->D(1) = 0;                                                   \
}

VSRARN(vsrarn_b_h, 16, uint8_t,  B, H)
VSRARN(vsrarn_h_w, 32, uint16_t, H, W)
VSRARN(vsrarn_w_d, 64, uint32_t, W, D)

#define VSRLRNI(NAME, BIT, E1, E2)                          \
void HELPER(NAME)(CPULoongArchState *env,                   \
                  uint32_t vd, uint32_t vj, uint32_t imm)   \
{                                                           \
    int i, max;                                             \
    VReg temp;                                              \
    VReg *Vd = &(env->fpr[vd].vreg);                        \
    VReg *Vj = &(env->fpr[vj].vreg);                        \
                                                            \
    temp.D(0) = 0;                                          \
    temp.D(1) = 0;                                          \
    max = LSX_LEN/BIT;                                      \
    for (i = 0; i < max; i++) {                             \
        temp.E1(i) = do_vsrlr_ ## E2(Vj->E2(i), imm);       \
        temp.E1(i + max) = do_vsrlr_ ## E2(Vd->E2(i), imm); \
    }                                                       \
    *Vd = temp;                                             \
}

void HELPER(vsrlrni_d_q)(CPULoongArchState *env,
                         uint32_t vd, uint32_t vj, uint32_t imm)
{
    VReg temp;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);
    Int128 r1, r2;

    if (imm == 0) {
        temp.D(0) = int128_getlo(Vj->Q(0));
        temp.D(1) = int128_getlo(Vd->Q(0));
    } else {
        r1 = int128_and(int128_urshift(Vj->Q(0), (imm -1)), int128_one());
        r2 = int128_and(int128_urshift(Vd->Q(0), (imm -1)), int128_one());

       temp.D(0) = int128_getlo(int128_add(int128_urshift(Vj->Q(0), imm), r1));
       temp.D(1) = int128_getlo(int128_add(int128_urshift(Vd->Q(0), imm), r2));
    }
    *Vd = temp;
}

VSRLRNI(vsrlrni_b_h, 16, B, H)
VSRLRNI(vsrlrni_h_w, 32, H, W)
VSRLRNI(vsrlrni_w_d, 64, W, D)

#define VSRARNI(NAME, BIT, E1, E2)                          \
void HELPER(NAME)(CPULoongArchState *env,                   \
                  uint32_t vd, uint32_t vj, uint32_t imm)   \
{                                                           \
    int i, max;                                             \
    VReg temp;                                              \
    VReg *Vd = &(env->fpr[vd].vreg);                        \
    VReg *Vj = &(env->fpr[vj].vreg);                        \
                                                            \
    temp.D(0) = 0;                                          \
    temp.D(1) = 0;                                          \
    max = LSX_LEN/BIT;                                      \
    for (i = 0; i < max; i++) {                             \
        temp.E1(i) = do_vsrar_ ## E2(Vj->E2(i), imm);       \
        temp.E1(i + max) = do_vsrar_ ## E2(Vd->E2(i), imm); \
    }                                                       \
    *Vd = temp;                                             \
}

void HELPER(vsrarni_d_q)(CPULoongArchState *env,
                         uint32_t vd, uint32_t vj, uint32_t imm)
{
    VReg temp;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);
    Int128 r1, r2;

    if (imm == 0) {
        temp.D(0) = int128_getlo(Vj->Q(0));
        temp.D(1) = int128_getlo(Vd->Q(0));
    } else {
        r1 = int128_and(int128_rshift(Vj->Q(0), (imm -1)), int128_one());
        r2 = int128_and(int128_rshift(Vd->Q(0), (imm -1)), int128_one());

       temp.D(0) = int128_getlo(int128_add(int128_rshift(Vj->Q(0), imm), r1));
       temp.D(1) = int128_getlo(int128_add(int128_rshift(Vd->Q(0), imm), r2));
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

#define VSSRLN(NAME, BIT, T, E1, E2)                                          \
void HELPER(NAME)(CPULoongArchState *env,                                     \
                  uint32_t vd, uint32_t vj, uint32_t vk)                      \
{                                                                             \
    int i;                                                                    \
    VReg *Vd = &(env->fpr[vd].vreg);                                          \
    VReg *Vj = &(env->fpr[vj].vreg);                                          \
    VReg *Vk = &(env->fpr[vk].vreg);                                          \
                                                                              \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                       \
        Vd->E1(i) = do_ssrlns_ ## E1(Vj->E2(i), (T)Vk->E2(i)% BIT, BIT/2 -1); \
    }                                                                         \
    Vd->D(1) = 0;                                                             \
}

VSSRLN(vssrln_b_h, 16, uint16_t, B, H)
VSSRLN(vssrln_h_w, 32, uint32_t, H, W)
VSSRLN(vssrln_w_d, 64, uint64_t, W, D)

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
        mask = (1ll << sh) -1;                   \
        if (shft_res > mask) {                   \
            return  mask;                        \
        } else if (shft_res < -(mask +1)) {      \
            return  ~mask;                       \
        } else {                                 \
            return shft_res;                     \
        }                                        \
}

SSRANS(B, int16_t, int8_t)
SSRANS(H, int32_t, int16_t)
SSRANS(W, int64_t, int32_t)

#define VSSRAN(NAME, BIT, T, E1, E2)                                         \
void HELPER(NAME)(CPULoongArchState *env,                                    \
                  uint32_t vd, uint32_t vj, uint32_t vk)                     \
{                                                                            \
    int i;                                                                   \
    VReg *Vd = &(env->fpr[vd].vreg);                                         \
    VReg *Vj = &(env->fpr[vj].vreg);                                         \
    VReg *Vk = &(env->fpr[vk].vreg);                                         \
                                                                             \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                      \
        Vd->E1(i) = do_ssrans_ ## E1(Vj->E2(i), (T)Vk->E2(i)%BIT, BIT/2 -1); \
    }                                                                        \
    Vd->D(1) = 0;                                                            \
}

VSSRAN(vssran_b_h, 16, uint16_t, B, H)
VSSRAN(vssran_h_w, 32, uint32_t, H, W)
VSSRAN(vssran_w_d, 64, uint64_t, W, D)

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
        mask = (1ull << sh) -1;                  \
        if (shft_res > mask) {                   \
            return mask;                         \
        } else {                                 \
            return shft_res;                     \
        }                                        \
}

SSRLNU(B, uint16_t, uint8_t,  int16_t)
SSRLNU(H, uint32_t, uint16_t, int32_t)
SSRLNU(W, uint64_t, uint32_t, int64_t)

#define VSSRLNU(NAME, BIT, T, E1, E2)                                     \
void HELPER(NAME)(CPULoongArchState *env,                                 \
                  uint32_t vd, uint32_t vj, uint32_t vk)                  \
{                                                                         \
    int i;                                                                \
    VReg *Vd = &(env->fpr[vd].vreg);                                      \
    VReg *Vj = &(env->fpr[vj].vreg);                                      \
    VReg *Vk = &(env->fpr[vk].vreg);                                      \
                                                                          \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                   \
        Vd->E1(i) = do_ssrlnu_ ## E1(Vj->E2(i), (T)Vk->E2(i)%BIT, BIT/2); \
    }                                                                     \
    Vd->D(1) = 0;                                                         \
}

VSSRLNU(vssrln_bu_h, 16, uint16_t, B, H)
VSSRLNU(vssrln_hu_w, 32, uint32_t, H, W)
VSSRLNU(vssrln_wu_d, 64, uint64_t, W, D)

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
        mask = (1ull << sh) -1;                  \
        if (shft_res > mask) {                   \
            return mask;                         \
        } else {                                 \
            return shft_res;                     \
        }                                        \
}

SSRANU(B, uint16_t, uint8_t,  int16_t)
SSRANU(H, uint32_t, uint16_t, int32_t)
SSRANU(W, uint64_t, uint32_t, int64_t)

#define VSSRANU(NAME, BIT, T, E1, E2)                                     \
void HELPER(NAME)(CPULoongArchState *env,                                 \
                  uint32_t vd, uint32_t vj, uint32_t vk)                  \
{                                                                         \
    int i;                                                                \
    VReg *Vd = &(env->fpr[vd].vreg);                                      \
    VReg *Vj = &(env->fpr[vj].vreg);                                      \
    VReg *Vk = &(env->fpr[vk].vreg);                                      \
                                                                          \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                   \
        Vd->E1(i) = do_ssranu_ ## E1(Vj->E2(i), (T)Vk->E2(i)%BIT, BIT/2); \
    }                                                                     \
    Vd->D(1) = 0;                                                         \
}

VSSRANU(vssran_bu_h, 16, uint16_t, B, H)
VSSRANU(vssran_hu_w, 32, uint32_t, H, W)
VSSRANU(vssran_wu_d, 64, uint64_t, W, D)

#define VSSRLNI(NAME, BIT, E1, E2)                                            \
void HELPER(NAME)(CPULoongArchState *env,                                     \
                  uint32_t vd, uint32_t vj, uint32_t imm)                     \
{                                                                             \
    int i;                                                                    \
    VReg temp;                                                                \
    VReg *Vd = &(env->fpr[vd].vreg);                                          \
    VReg *Vj = &(env->fpr[vj].vreg);                                          \
                                                                              \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                       \
        temp.E1(i) = do_ssrlns_ ## E1(Vj->E2(i), imm, BIT/2 -1);              \
        temp.E1(i + LSX_LEN/BIT) = do_ssrlns_ ## E1(Vd->E2(i), imm, BIT/2 -1);\
    }                                                                         \
    *Vd = temp;                                                               \
}

void HELPER(vssrlni_d_q)(CPULoongArchState *env,
                         uint32_t vd, uint32_t vj, uint32_t imm)
{
    Int128 shft_res1, shft_res2, mask;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    if (imm == 0) {
        shft_res1 = Vj->Q(0);
        shft_res2 = Vd->Q(0);
    } else {
        shft_res1 = int128_urshift(Vj->Q(0), imm);
        shft_res2 = int128_urshift(Vd->Q(0), imm);
    }
    mask = int128_sub(int128_lshift(int128_one(), 63), int128_one());

    if (int128_ult(mask, shft_res1)) {
        Vd->D(0) = int128_getlo(mask);
    }else {
        Vd->D(0) = int128_getlo(shft_res1);
    }

    if (int128_ult(mask, shft_res2)) {
        Vd->D(1) = int128_getlo(mask);
    }else {
        Vd->D(1) = int128_getlo(shft_res2);
    }
}

VSSRLNI(vssrlni_b_h, 16, B, H)
VSSRLNI(vssrlni_h_w, 32, H, W)
VSSRLNI(vssrlni_w_d, 64, W, D)

#define VSSRANI(NAME, BIT, E1, E2)                                             \
void HELPER(NAME)(CPULoongArchState *env,                                      \
                  uint32_t vd, uint32_t vj, uint32_t imm)                      \
{                                                                              \
    int i;                                                                     \
    VReg temp;                                                                 \
    VReg *Vd = &(env->fpr[vd].vreg);                                           \
    VReg *Vj = &(env->fpr[vj].vreg);                                           \
                                                                               \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                        \
        temp.E1(i) = do_ssrans_ ## E1(Vj->E2(i), imm, BIT/2 -1);               \
        temp.E1(i + LSX_LEN/BIT) = do_ssrans_ ## E1(Vd->E2(i), imm, BIT/2 -1); \
    }                                                                          \
    *Vd = temp;                                                                \
}

void HELPER(vssrani_d_q)(CPULoongArchState *env,
                         uint32_t vd, uint32_t vj, uint32_t imm)
{
    Int128 shft_res1, shft_res2, mask, min;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    if (imm == 0) {
        shft_res1 = Vj->Q(0);
        shft_res2 = Vd->Q(0);
    } else {
        shft_res1 = int128_rshift(Vj->Q(0), imm);
        shft_res2 = int128_rshift(Vd->Q(0), imm);
    }
    mask = int128_sub(int128_lshift(int128_one(), 63), int128_one());
    min  = int128_lshift(int128_one(), 63);

    if (int128_gt(shft_res1,  mask)) {
        Vd->D(0) = int128_getlo(mask);
    } else if (int128_lt(shft_res1, int128_neg(min))) {
        Vd->D(0) = int128_getlo(min);
    } else {
        Vd->D(0) = int128_getlo(shft_res1);
    }

    if (int128_gt(shft_res2, mask)) {
        Vd->D(1) = int128_getlo(mask);
    } else if (int128_lt(shft_res2, int128_neg(min))) {
        Vd->D(1) = int128_getlo(min);
    } else {
        Vd->D(1) = int128_getlo(shft_res2);
    }
}

VSSRANI(vssrani_b_h, 16, B, H)
VSSRANI(vssrani_h_w, 32, H, W)
VSSRANI(vssrani_w_d, 64, W, D)

#define VSSRLNUI(NAME, BIT, E1, E2)                                         \
void HELPER(NAME)(CPULoongArchState *env,                                   \
                  uint32_t vd, uint32_t vj, uint32_t imm)                   \
{                                                                           \
    int i;                                                                  \
    VReg temp;                                                              \
    VReg *Vd = &(env->fpr[vd].vreg);                                        \
    VReg *Vj = &(env->fpr[vj].vreg);                                        \
                                                                            \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                     \
        temp.E1(i) = do_ssrlnu_ ## E1(Vj->E2(i), imm, BIT/2);               \
        temp.E1(i + LSX_LEN/BIT) = do_ssrlnu_ ## E1(Vd->E2(i), imm, BIT/2); \
    }                                                                       \
    *Vd = temp;                                                             \
}

void HELPER(vssrlni_du_q)(CPULoongArchState *env,
                         uint32_t vd, uint32_t vj, uint32_t imm)
{
    Int128 shft_res1, shft_res2, mask;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    if (imm == 0) {
        shft_res1 = Vj->Q(0);
        shft_res2 = Vd->Q(0);
    } else {
        shft_res1 = int128_urshift(Vj->Q(0), imm);
        shft_res2 = int128_urshift(Vd->Q(0), imm);
    }
    mask = int128_sub(int128_lshift(int128_one(), 64), int128_one());

    if (int128_ult(mask, shft_res1)) {
        Vd->D(0) = int128_getlo(mask);
    }else {
        Vd->D(0) = int128_getlo(shft_res1);
    }

    if (int128_ult(mask, shft_res2)) {
        Vd->D(1) = int128_getlo(mask);
    }else {
        Vd->D(1) = int128_getlo(shft_res2);
    }
}

VSSRLNUI(vssrlni_bu_h, 16, B, H)
VSSRLNUI(vssrlni_hu_w, 32, H, W)
VSSRLNUI(vssrlni_wu_d, 64, W, D)

#define VSSRANUI(NAME, BIT, E1, E2)                                         \
void HELPER(NAME)(CPULoongArchState *env,                                   \
                  uint32_t vd, uint32_t vj, uint32_t imm)                   \
{                                                                           \
    int i;                                                                  \
    VReg temp;                                                              \
    VReg *Vd = &(env->fpr[vd].vreg);                                        \
    VReg *Vj = &(env->fpr[vj].vreg);                                        \
                                                                            \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                     \
        temp.E1(i) = do_ssranu_ ## E1(Vj->E2(i), imm, BIT/2);               \
        temp.E1(i + LSX_LEN/BIT) = do_ssranu_ ## E1(Vd->E2(i), imm, BIT/2); \
    }                                                                       \
    *Vd = temp;                                                             \
}

void HELPER(vssrani_du_q)(CPULoongArchState *env,
                         uint32_t vd, uint32_t vj, uint32_t imm)
{
    Int128 shft_res1, shft_res2, mask;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    if (imm == 0) {
        shft_res1 = Vj->Q(0);
        shft_res2 = Vd->Q(0);
    } else {
        shft_res1 = int128_rshift(Vj->Q(0), imm);
        shft_res2 = int128_rshift(Vd->Q(0), imm);
    }

    if (int128_lt(Vj->Q(0), int128_zero())) {
        shft_res1 = int128_zero();
    }

    if (int128_lt(Vd->Q(0), int128_zero())) {
        shft_res2 = int128_zero();
    }

    mask = int128_sub(int128_lshift(int128_one(), 64), int128_one());

    if (int128_ult(mask, shft_res1)) {
        Vd->D(0) = int128_getlo(mask);
    }else {
        Vd->D(0) = int128_getlo(shft_res1);
    }

    if (int128_ult(mask, shft_res2)) {
        Vd->D(1) = int128_getlo(mask);
    }else {
        Vd->D(1) = int128_getlo(shft_res2);
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
    mask = (1ull << sh) -1;                        \
    if (shft_res > mask) {                         \
        return mask;                               \
    } else {                                       \
        return  shft_res;                          \
    }                                              \
}

SSRLRNS(B, H, uint16_t, int16_t, uint8_t)
SSRLRNS(H, W, uint32_t, int32_t, uint16_t)
SSRLRNS(W, D, uint64_t, int64_t, uint32_t)

#define VSSRLRN(NAME, BIT, T, E1, E2)                                         \
void HELPER(NAME)(CPULoongArchState *env,                                     \
                  uint32_t vd, uint32_t vj, uint32_t vk)                      \
{                                                                             \
    int i;                                                                    \
    VReg *Vd = &(env->fpr[vd].vreg);                                          \
    VReg *Vj = &(env->fpr[vj].vreg);                                          \
    VReg *Vk = &(env->fpr[vk].vreg);                                          \
                                                                              \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                       \
        Vd->E1(i) = do_ssrlrns_ ## E1(Vj->E2(i), (T)Vk->E2(i)%BIT, BIT/2 -1); \
    }                                                                         \
    Vd->D(1) = 0;                                                             \
}

VSSRLRN(vssrlrn_b_h, 16, uint16_t, B, H)
VSSRLRN(vssrlrn_h_w, 32, uint32_t, H, W)
VSSRLRN(vssrlrn_w_d, 64, uint64_t, W, D)

#define SSRARNS(E1, E2, T1, T2)                    \
static T1 do_ssrarns_ ## E1(T1 e2, int sa, int sh) \
{                                                  \
    T1 shft_res;                                   \
                                                   \
    shft_res = do_vsrar_ ## E2(e2, sa);            \
    T2 mask;                                       \
    mask = (1ll << sh) -1;                         \
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

#define VSSRARN(NAME, BIT, T, E1, E2)                                         \
void HELPER(NAME)(CPULoongArchState *env,                                     \
                  uint32_t vd, uint32_t vj, uint32_t vk)                      \
{                                                                             \
    int i;                                                                    \
    VReg *Vd = &(env->fpr[vd].vreg);                                          \
    VReg *Vj = &(env->fpr[vj].vreg);                                          \
    VReg *Vk = &(env->fpr[vk].vreg);                                          \
                                                                              \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                       \
        Vd->E1(i) = do_ssrarns_ ## E1(Vj->E2(i), (T)Vk->E2(i)%BIT, BIT/2 -1); \
    }                                                                         \
    Vd->D(1) = 0;                                                             \
}

VSSRARN(vssrarn_b_h, 16, uint16_t, B, H)
VSSRARN(vssrarn_h_w, 32, uint32_t, H, W)
VSSRARN(vssrarn_w_d, 64, uint64_t, W, D)

#define SSRLRNU(E1, E2, T1, T2, T3)                \
static T1 do_ssrlrnu_ ## E1(T3 e2, int sa, int sh) \
{                                                  \
    T1 shft_res;                                   \
                                                   \
    shft_res = do_vsrlr_ ## E2(e2, sa);            \
                                                   \
    T2 mask;                                       \
    mask = (1ull << sh) -1;                        \
    if (shft_res > mask) {                         \
        return mask;                               \
    } else {                                       \
        return shft_res;                           \
    }                                              \
}

SSRLRNU(B, H, uint16_t, uint8_t, int16_t)
SSRLRNU(H, W, uint32_t, uint16_t, int32_t)
SSRLRNU(W, D, uint64_t, uint32_t, int64_t)

#define VSSRLRNU(NAME, BIT, T, E1, E2)                                     \
void HELPER(NAME)(CPULoongArchState *env,                                  \
                  uint32_t vd, uint32_t vj, uint32_t vk)                   \
{                                                                          \
    int i;                                                                 \
    VReg *Vd = &(env->fpr[vd].vreg);                                       \
    VReg *Vj = &(env->fpr[vj].vreg);                                       \
    VReg *Vk = &(env->fpr[vk].vreg);                                       \
                                                                           \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                    \
        Vd->E1(i) = do_ssrlrnu_ ## E1(Vj->E2(i), (T)Vk->E2(i)%BIT, BIT/2); \
    }                                                                      \
    Vd->D(1) = 0;                                                          \
}

VSSRLRNU(vssrlrn_bu_h, 16, uint16_t, B, H)
VSSRLRNU(vssrlrn_hu_w, 32, uint32_t, H, W)
VSSRLRNU(vssrlrn_wu_d, 64, uint64_t, W, D)

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
    mask = (1ull << sh) -1;                        \
    if (shft_res > mask) {                         \
        return mask;                               \
    } else {                                       \
        return shft_res;                           \
    }                                              \
}

SSRARNU(B, H, uint16_t, uint8_t, int16_t)
SSRARNU(H, W, uint32_t, uint16_t, int32_t)
SSRARNU(W, D, uint64_t, uint32_t, int64_t)

#define VSSRARNU(NAME, BIT, T, E1, E2)                                     \
void HELPER(NAME)(CPULoongArchState *env,                                  \
                  uint32_t vd, uint32_t vj, uint32_t vk)                   \
{                                                                          \
    int i;                                                                 \
    VReg *Vd = &(env->fpr[vd].vreg);                                       \
    VReg *Vj = &(env->fpr[vj].vreg);                                       \
    VReg *Vk = &(env->fpr[vk].vreg);                                       \
                                                                           \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                    \
        Vd->E1(i) = do_ssrarnu_ ## E1(Vj->E2(i), (T)Vk->E2(i)%BIT, BIT/2); \
    }                                                                      \
    Vd->D(1) = 0;                                                          \
}

VSSRARNU(vssrarn_bu_h, 16, uint16_t, B, H)
VSSRARNU(vssrarn_hu_w, 32, uint32_t, H, W)
VSSRARNU(vssrarn_wu_d, 64, uint64_t, W, D)

#define VSSRLRNI(NAME, BIT, E1, E2)                                            \
void HELPER(NAME)(CPULoongArchState *env,                                      \
                  uint32_t vd, uint32_t vj, uint32_t imm)                      \
{                                                                              \
    int i;                                                                     \
    VReg temp;                                                                 \
    VReg *Vd = &(env->fpr[vd].vreg);                                           \
    VReg *Vj = &(env->fpr[vj].vreg);                                           \
                                                                               \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                        \
        temp.E1(i) = do_ssrlrns_ ## E1(Vj->E2(i), imm, BIT/2 -1);              \
        temp.E1(i + LSX_LEN/BIT) = do_ssrlrns_ ## E1(Vd->E2(i), imm, BIT/2 -1);\
    }                                                                          \
    *Vd = temp;                                                                \
}

#define VSSRLRNI_Q(NAME, sh)                                               \
void HELPER(NAME)(CPULoongArchState *env,                                  \
                          uint32_t vd, uint32_t vj, uint32_t imm)          \
{                                                                          \
    Int128 shft_res1, shft_res2, mask, r1, r2;                             \
    VReg *Vd = &(env->fpr[vd].vreg);                                       \
    VReg *Vj = &(env->fpr[vj].vreg);                                       \
                                                                           \
    if (imm == 0) {                                                        \
        shft_res1 = Vj->Q(0);                                              \
        shft_res2 = Vd->Q(0);                                              \
    } else {                                                               \
        r1 = int128_and(int128_urshift(Vj->Q(0), (imm -1)), int128_one()); \
        r2 = int128_and(int128_urshift(Vd->Q(0), (imm -1)), int128_one()); \
                                                                           \
        shft_res1 = (int128_add(int128_urshift(Vj->Q(0), imm), r1));       \
        shft_res2 = (int128_add(int128_urshift(Vd->Q(0), imm), r2));       \
    }                                                                      \
                                                                           \
    mask = int128_sub(int128_lshift(int128_one(), sh), int128_one());      \
                                                                           \
    if (int128_ult(mask, shft_res1)) {                                     \
        Vd->D(0) = int128_getlo(mask);                                     \
    }else {                                                                \
        Vd->D(0) = int128_getlo(shft_res1);                                \
    }                                                                      \
                                                                           \
    if (int128_ult(mask, shft_res2)) {                                     \
        Vd->D(1) = int128_getlo(mask);                                     \
    }else {                                                                \
        Vd->D(1) = int128_getlo(shft_res2);                                \
    }                                                                      \
}

VSSRLRNI(vssrlrni_b_h, 16, B, H)
VSSRLRNI(vssrlrni_h_w, 32, H, W)
VSSRLRNI(vssrlrni_w_d, 64, W, D)
VSSRLRNI_Q(vssrlrni_d_q, 63)

#define VSSRARNI(NAME, BIT, E1, E2)                                             \
void HELPER(NAME)(CPULoongArchState *env,                                       \
                  uint32_t vd, uint32_t vj, uint32_t imm)                       \
{                                                                               \
    int i;                                                                      \
    VReg temp;                                                                  \
    VReg *Vd = &(env->fpr[vd].vreg);                                            \
    VReg *Vj = &(env->fpr[vj].vreg);                                            \
                                                                                \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                         \
        temp.E1(i) = do_ssrarns_ ## E1(Vj->E2(i), imm, BIT/2 -1);               \
        temp.E1(i + LSX_LEN/BIT) = do_ssrarns_ ## E1(Vd->E2(i), imm, BIT/2 -1); \
    }                                                                           \
    *Vd = temp;                                                                 \
}

void HELPER(vssrarni_d_q)(CPULoongArchState *env,
                          uint32_t vd, uint32_t vj, uint32_t imm)
{
    Int128 shft_res1, shft_res2, mask1, mask2, r1, r2;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    if (imm == 0) {
        shft_res1 = Vj->Q(0);
        shft_res2 = Vd->Q(0);
    } else {
        r1 = int128_and(int128_rshift(Vj->Q(0), (imm -1)), int128_one());
        r2 = int128_and(int128_rshift(Vd->Q(0), (imm -1)), int128_one());

        shft_res1 = int128_add(int128_rshift(Vj->Q(0), imm), r1);
        shft_res2 = int128_add(int128_rshift(Vd->Q(0), imm), r2);
    }

    mask1 = int128_sub(int128_lshift(int128_one(), 63), int128_one());
    mask2  = int128_lshift(int128_one(), 63);

    if (int128_gt(shft_res1,  mask1)) {
        Vd->D(0) = int128_getlo(mask1);
    } else if (int128_lt(shft_res1, int128_neg(mask2))) {
        Vd->D(0) = int128_getlo(mask2);
    } else {
        Vd->D(0) = int128_getlo(shft_res1);
    }

    if (int128_gt(shft_res2, mask1)) {
        Vd->D(1) = int128_getlo(mask1);
    } else if (int128_lt(shft_res2, int128_neg(mask2))) {
        Vd->D(1) = int128_getlo(mask2);
    } else {
        Vd->D(1) = int128_getlo(shft_res2);
    }
}

VSSRARNI(vssrarni_b_h, 16, B, H)
VSSRARNI(vssrarni_h_w, 32, H, W)
VSSRARNI(vssrarni_w_d, 64, W, D)

#define VSSRLRNUI(NAME, BIT, E1, E2)                                         \
void HELPER(NAME)(CPULoongArchState *env,                                    \
                  uint32_t vd, uint32_t vj, uint32_t imm)                    \
{                                                                            \
    int i;                                                                   \
    VReg temp;                                                               \
    VReg *Vd = &(env->fpr[vd].vreg);                                         \
    VReg *Vj = &(env->fpr[vj].vreg);                                         \
                                                                             \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                      \
        temp.E1(i) = do_ssrlrnu_ ## E1(Vj->E2(i), imm, BIT/2);               \
        temp.E1(i + LSX_LEN/BIT) = do_ssrlrnu_ ## E1(Vd->E2(i), imm, BIT/2); \
    }                                                                        \
    *Vd = temp;                                                              \
}

VSSRLRNUI(vssrlrni_bu_h, 16, B, H)
VSSRLRNUI(vssrlrni_hu_w, 32, H, W)
VSSRLRNUI(vssrlrni_wu_d, 64, W, D)
VSSRLRNI_Q(vssrlrni_du_q, 64)

#define VSSRARNUI(NAME, BIT, E1, E2)                                         \
void HELPER(NAME)(CPULoongArchState *env,                                    \
                  uint32_t vd, uint32_t vj, uint32_t imm)                    \
{                                                                            \
    int i;                                                                   \
    VReg temp;                                                               \
    VReg *Vd = &(env->fpr[vd].vreg);                                         \
    VReg *Vj = &(env->fpr[vj].vreg);                                         \
                                                                             \
    for (i = 0; i < LSX_LEN/BIT; i++) {                                      \
        temp.E1(i) = do_ssrarnu_ ## E1(Vj->E2(i), imm, BIT/2);               \
        temp.E1(i + LSX_LEN/BIT) = do_ssrarnu_ ## E1(Vd->E2(i), imm, BIT/2); \
    }                                                                        \
    *Vd = temp;                                                              \
}

void HELPER(vssrarni_du_q)(CPULoongArchState *env,
                           uint32_t vd, uint32_t vj, uint32_t imm)
{
    Int128 shft_res1, shft_res2, mask1, mask2, r1, r2;
    VReg *Vd = &(env->fpr[vd].vreg);
    VReg *Vj = &(env->fpr[vj].vreg);

    if (imm == 0) {
        shft_res1 = Vj->Q(0);
        shft_res2 = Vd->Q(0);
    } else {
        r1 = int128_and(int128_rshift(Vj->Q(0), (imm -1)), int128_one());
        r2 = int128_and(int128_rshift(Vd->Q(0), (imm -1)), int128_one());

        shft_res1 = int128_add(int128_rshift(Vj->Q(0), imm), r1);
        shft_res2 = int128_add(int128_rshift(Vd->Q(0), imm), r2);
    }

    if (int128_lt(Vj->Q(0), int128_zero())) {
        shft_res1 = int128_zero();
    }
    if (int128_lt(Vd->Q(0), int128_zero())) {
        shft_res2 = int128_zero();
    }

    mask1 = int128_sub(int128_lshift(int128_one(), 64), int128_one());
    mask2  = int128_lshift(int128_one(), 64);

    if (int128_gt(shft_res1,  mask1)) {
        Vd->D(0) = int128_getlo(mask1);
    } else if (int128_lt(shft_res1, int128_neg(mask2))) {
        Vd->D(0) = int128_getlo(mask2);
    } else {
        Vd->D(0) = int128_getlo(shft_res1);
    }

    if (int128_gt(shft_res2, mask1)) {
        Vd->D(1) = int128_getlo(mask1);
    } else if (int128_lt(shft_res2, int128_neg(mask2))) {
        Vd->D(1) = int128_getlo(mask2);
    } else {
        Vd->D(1) = int128_getlo(shft_res2);
    }
}

VSSRARNUI(vssrarni_bu_h, 16, B, H)
VSSRARNUI(vssrarni_hu_w, 32, H, W)
VSSRARNUI(vssrarni_wu_d, 64, W, D)

#define DO_2OP(NAME, BIT, E, DO_OP)                                 \
void HELPER(NAME)(CPULoongArchState *env, uint32_t vd, uint32_t vj) \
{                                                                   \
    int i;                                                          \
    VReg *Vd = &(env->fpr[vd].vreg);                                \
    VReg *Vj = &(env->fpr[vj].vreg);                                \
                                                                    \
    for (i = 0; i < LSX_LEN/BIT; i++)                               \
    {                                                               \
        Vd->E(i) = DO_OP(Vj->E(i));                                 \
    }                                                               \
}

#define DO_CLO_B(N)  (clz32(~N & 0xff) - 24)
#define DO_CLO_H(N)  (clz32(~N & 0xffff) - 16)
#define DO_CLO_W(N)  (clz32(~N))
#define DO_CLO_D(N)  (clz64(~N))
#define DO_CLZ_B(N)  (clz32(N) - 24)
#define DO_CLZ_H(N)  (clz32(N) - 16)
#define DO_CLZ_W(N)  (clz32(N))
#define DO_CLZ_D(N)  (clz64(N))

DO_2OP(vclo_b, 8, UB, DO_CLO_B)
DO_2OP(vclo_h, 16, UH, DO_CLO_H)
DO_2OP(vclo_w, 32, UW, DO_CLO_W)
DO_2OP(vclo_d, 64, UD, DO_CLO_D)
DO_2OP(vclz_b, 8, UB, DO_CLZ_B)
DO_2OP(vclz_h, 16, UH, DO_CLZ_H)
DO_2OP(vclz_w, 32, UW, DO_CLZ_W)
DO_2OP(vclz_d, 64, UD, DO_CLZ_D)

#define VPCNT(NAME, BIT, E, FN)                                     \
void HELPER(NAME)(CPULoongArchState *env, uint32_t vd, uint32_t vj) \
{                                                                   \
    int i;                                                          \
    VReg *Vd = &(env->fpr[vd].vreg);                                \
    VReg *Vj = &(env->fpr[vj].vreg);                                \
                                                                    \
    for (i = 0; i < LSX_LEN/BIT; i++)                               \
    {                                                               \
        Vd->E(i) = FN(Vj->E(i));                                    \
    }                                                               \
}

VPCNT(vpcnt_b, 8, UB, ctpop8)
VPCNT(vpcnt_h, 16, UH, ctpop16)
VPCNT(vpcnt_w, 32, UW, ctpop32)
VPCNT(vpcnt_d, 64, UD, ctpop64)
