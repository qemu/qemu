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
