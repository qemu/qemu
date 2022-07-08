/*
 * ARM SME Operations
 *
 * Copyright (c) 2022 Linaro, Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "tcg/tcg-gvec-desc.h"
#include "exec/helper-proto.h"
#include "qemu/int128.h"
#include "vec_internal.h"

/* ResetSVEState */
void arm_reset_sve_state(CPUARMState *env)
{
    memset(env->vfp.zregs, 0, sizeof(env->vfp.zregs));
    /* Recall that FFR is stored as pregs[16]. */
    memset(env->vfp.pregs, 0, sizeof(env->vfp.pregs));
    vfp_set_fpcr(env, 0x0800009f);
}

void helper_set_pstate_sm(CPUARMState *env, uint32_t i)
{
    if (i == FIELD_EX64(env->svcr, SVCR, SM)) {
        return;
    }
    env->svcr ^= R_SVCR_SM_MASK;
    arm_reset_sve_state(env);
}

void helper_set_pstate_za(CPUARMState *env, uint32_t i)
{
    if (i == FIELD_EX64(env->svcr, SVCR, ZA)) {
        return;
    }
    env->svcr ^= R_SVCR_ZA_MASK;

    /*
     * ResetSMEState.
     *
     * SetPSTATE_ZA zeros on enable and disable.  We can zero this only
     * on enable: while disabled, the storage is inaccessible and the
     * value does not matter.  We're not saving the storage in vmstate
     * when disabled either.
     */
    if (i) {
        memset(env->zarray, 0, sizeof(env->zarray));
    }
}

void helper_sme_zero(CPUARMState *env, uint32_t imm, uint32_t svl)
{
    uint32_t i;

    /*
     * Special case clearing the entire ZA space.
     * This falls into the CONSTRAINED UNPREDICTABLE zeroing of any
     * parts of the ZA storage outside of SVL.
     */
    if (imm == 0xff) {
        memset(env->zarray, 0, sizeof(env->zarray));
        return;
    }

    /*
     * Recall that ZAnH.D[m] is spread across ZA[n+8*m],
     * so each row is discontiguous within ZA[].
     */
    for (i = 0; i < svl; i++) {
        if (imm & (1 << (i % 8))) {
            memset(&env->zarray[i], 0, svl);
        }
    }
}


/*
 * When considering the ZA storage as an array of elements of
 * type T, the index within that array of the Nth element of
 * a vertical slice of a tile can be calculated like this,
 * regardless of the size of type T. This is because the tiles
 * are interleaved, so if type T is size N bytes then row 1 of
 * the tile is N rows away from row 0. The division by N to
 * convert a byte offset into an array index and the multiplication
 * by N to convert from vslice-index-within-the-tile to
 * the index within the ZA storage cancel out.
 */
#define tile_vslice_index(i) ((i) * sizeof(ARMVectorReg))

/*
 * When doing byte arithmetic on the ZA storage, the element
 * byteoff bytes away in a tile vertical slice is always this
 * many bytes away in the ZA storage, regardless of the
 * size of the tile element, assuming that byteoff is a multiple
 * of the element size. Again this is because of the interleaving
 * of the tiles. For instance if we have 1 byte per element then
 * each row of the ZA storage has one byte of the vslice data,
 * and (counting from 0) byte 8 goes in row 8 of the storage
 * at offset (8 * row-size-in-bytes).
 * If we have 8 bytes per element then each row of the ZA storage
 * has 8 bytes of the data, but there are 8 interleaved tiles and
 * so byte 8 of the data goes into row 1 of the tile,
 * which is again row 8 of the storage, so the offset is still
 * (8 * row-size-in-bytes). Similarly for other element sizes.
 */
#define tile_vslice_offset(byteoff) ((byteoff) * sizeof(ARMVectorReg))


/*
 * Move Zreg vector to ZArray column.
 */
#define DO_MOVA_C(NAME, TYPE, H)                                        \
void HELPER(NAME)(void *za, void *vn, void *vg, uint32_t desc)          \
{                                                                       \
    int i, oprsz = simd_oprsz(desc);                                    \
    for (i = 0; i < oprsz; ) {                                          \
        uint16_t pg = *(uint16_t *)(vg + H1_2(i >> 3));                 \
        do {                                                            \
            if (pg & 1) {                                               \
                *(TYPE *)(za + tile_vslice_offset(i)) = *(TYPE *)(vn + H(i)); \
            }                                                           \
            i += sizeof(TYPE);                                          \
            pg >>= sizeof(TYPE);                                        \
        } while (i & 15);                                               \
    }                                                                   \
}

DO_MOVA_C(sme_mova_cz_b, uint8_t, H1)
DO_MOVA_C(sme_mova_cz_h, uint16_t, H1_2)
DO_MOVA_C(sme_mova_cz_s, uint32_t, H1_4)

void HELPER(sme_mova_cz_d)(void *za, void *vn, void *vg, uint32_t desc)
{
    int i, oprsz = simd_oprsz(desc) / 8;
    uint8_t *pg = vg;
    uint64_t *n = vn;
    uint64_t *a = za;

    for (i = 0; i < oprsz; i++) {
        if (pg[H1(i)] & 1) {
            a[tile_vslice_index(i)] = n[i];
        }
    }
}

void HELPER(sme_mova_cz_q)(void *za, void *vn, void *vg, uint32_t desc)
{
    int i, oprsz = simd_oprsz(desc) / 16;
    uint16_t *pg = vg;
    Int128 *n = vn;
    Int128 *a = za;

    /*
     * Int128 is used here simply to copy 16 bytes, and to simplify
     * the address arithmetic.
     */
    for (i = 0; i < oprsz; i++) {
        if (pg[H2(i)] & 1) {
            a[tile_vslice_index(i)] = n[i];
        }
    }
}

#undef DO_MOVA_C

/*
 * Move ZArray column to Zreg vector.
 */
#define DO_MOVA_Z(NAME, TYPE, H)                                        \
void HELPER(NAME)(void *vd, void *za, void *vg, uint32_t desc)          \
{                                                                       \
    int i, oprsz = simd_oprsz(desc);                                    \
    for (i = 0; i < oprsz; ) {                                          \
        uint16_t pg = *(uint16_t *)(vg + H1_2(i >> 3));                 \
        do {                                                            \
            if (pg & 1) {                                               \
                *(TYPE *)(vd + H(i)) = *(TYPE *)(za + tile_vslice_offset(i)); \
            }                                                           \
            i += sizeof(TYPE);                                          \
            pg >>= sizeof(TYPE);                                        \
        } while (i & 15);                                               \
    }                                                                   \
}

DO_MOVA_Z(sme_mova_zc_b, uint8_t, H1)
DO_MOVA_Z(sme_mova_zc_h, uint16_t, H1_2)
DO_MOVA_Z(sme_mova_zc_s, uint32_t, H1_4)

void HELPER(sme_mova_zc_d)(void *vd, void *za, void *vg, uint32_t desc)
{
    int i, oprsz = simd_oprsz(desc) / 8;
    uint8_t *pg = vg;
    uint64_t *d = vd;
    uint64_t *a = za;

    for (i = 0; i < oprsz; i++) {
        if (pg[H1(i)] & 1) {
            d[i] = a[tile_vslice_index(i)];
        }
    }
}

void HELPER(sme_mova_zc_q)(void *vd, void *za, void *vg, uint32_t desc)
{
    int i, oprsz = simd_oprsz(desc) / 16;
    uint16_t *pg = vg;
    Int128 *d = vd;
    Int128 *a = za;

    /*
     * Int128 is used here simply to copy 16 bytes, and to simplify
     * the address arithmetic.
     */
    for (i = 0; i < oprsz; i++, za += sizeof(ARMVectorReg)) {
        if (pg[H2(i)] & 1) {
            d[i] = a[tile_vslice_index(i)];
        }
    }
}

#undef DO_MOVA_Z
