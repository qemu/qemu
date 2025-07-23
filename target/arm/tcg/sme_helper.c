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
#include "internals.h"
#include "tcg/tcg-gvec-desc.h"
#include "exec/helper-proto.h"
#include "accel/tcg/cpu-ldst.h"
#include "accel/tcg/helper-retaddr.h"
#include "qemu/int128.h"
#include "fpu/softfloat.h"
#include "vec_internal.h"
#include "sve_ldst_internal.h"


static bool vectors_overlap(ARMVectorReg *x, unsigned nx,
                            ARMVectorReg *y, unsigned ny)
{
    return !(x + nx <= y || y + ny <= x);
}

void helper_set_svcr(CPUARMState *env, uint32_t val, uint32_t mask)
{
    aarch64_set_svcr(env, val, mask);
}

void helper_sme_zero(CPUARMState *env, uint32_t imm, uint32_t svl)
{
    uint32_t i;

    /*
     * Special case clearing the entire ZArray.
     * This falls into the CONSTRAINED UNPREDICTABLE zeroing of any
     * parts of the ZA storage outside of SVL.
     */
    if (imm == 0xff) {
        memset(env->za_state.za, 0, sizeof(env->za_state.za));
        return;
    }

    /*
     * Recall that ZAnH.D[m] is spread across ZA[n+8*m],
     * so each row is discontiguous within ZA[].
     */
    for (i = 0; i < svl; i++) {
        if (imm & (1 << (i % 8))) {
            memset(&env->za_state.za[i], 0, svl);
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

void HELPER(sme2_mova_zc_b)(void *vdst, void *vsrc, uint32_t desc)
{
    const uint8_t *src = vsrc;
    uint8_t *dst = vdst;
    size_t i, n = simd_oprsz(desc);

    for (i = 0; i < n; ++i) {
        dst[i] = src[tile_vslice_index(i)];
    }
}

void HELPER(sme2_mova_zc_h)(void *vdst, void *vsrc, uint32_t desc)
{
    const uint16_t *src = vsrc;
    uint16_t *dst = vdst;
    size_t i, n = simd_oprsz(desc) / 2;

    for (i = 0; i < n; ++i) {
        dst[i] = src[tile_vslice_index(i)];
    }
}

void HELPER(sme2_mova_zc_s)(void *vdst, void *vsrc, uint32_t desc)
{
    const uint32_t *src = vsrc;
    uint32_t *dst = vdst;
    size_t i, n = simd_oprsz(desc) / 4;

    for (i = 0; i < n; ++i) {
        dst[i] = src[tile_vslice_index(i)];
    }
}

void HELPER(sme2_mova_zc_d)(void *vdst, void *vsrc, uint32_t desc)
{
    const uint64_t *src = vsrc;
    uint64_t *dst = vdst;
    size_t i, n = simd_oprsz(desc) / 8;

    for (i = 0; i < n; ++i) {
        dst[i] = src[tile_vslice_index(i)];
    }
}

void HELPER(sme2p1_movaz_zc_b)(void *vdst, void *vsrc, uint32_t desc)
{
    uint8_t *src = vsrc;
    uint8_t *dst = vdst;
    size_t i, n = simd_oprsz(desc);

    for (i = 0; i < n; ++i) {
        dst[i] = src[tile_vslice_index(i)];
        src[tile_vslice_index(i)] = 0;
    }
}

void HELPER(sme2p1_movaz_zc_h)(void *vdst, void *vsrc, uint32_t desc)
{
    uint16_t *src = vsrc;
    uint16_t *dst = vdst;
    size_t i, n = simd_oprsz(desc) / 2;

    for (i = 0; i < n; ++i) {
        dst[i] = src[tile_vslice_index(i)];
        src[tile_vslice_index(i)] = 0;
    }
}

void HELPER(sme2p1_movaz_zc_s)(void *vdst, void *vsrc, uint32_t desc)
{
    uint32_t *src = vsrc;
    uint32_t *dst = vdst;
    size_t i, n = simd_oprsz(desc) / 4;

    for (i = 0; i < n; ++i) {
        dst[i] = src[tile_vslice_index(i)];
        src[tile_vslice_index(i)] = 0;
    }
}

void HELPER(sme2p1_movaz_zc_d)(void *vdst, void *vsrc, uint32_t desc)
{
    uint64_t *src = vsrc;
    uint64_t *dst = vdst;
    size_t i, n = simd_oprsz(desc) / 8;

    for (i = 0; i < n; ++i) {
        dst[i] = src[tile_vslice_index(i)];
        src[tile_vslice_index(i)] = 0;
    }
}

void HELPER(sme2p1_movaz_zc_q)(void *vdst, void *vsrc, uint32_t desc)
{
    Int128 *src = vsrc;
    Int128 *dst = vdst;
    size_t i, n = simd_oprsz(desc) / 16;

    for (i = 0; i < n; ++i) {
        dst[i] = src[tile_vslice_index(i)];
        memset(&src[tile_vslice_index(i)], 0, 16);
    }
}

/*
 * Clear elements in a tile slice comprising len bytes.
 */

typedef void ClearFn(void *ptr, size_t off, size_t len);

static void clear_horizontal(void *ptr, size_t off, size_t len)
{
    memset(ptr + off, 0, len);
}

static void clear_vertical_b(void *vptr, size_t off, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        *(uint8_t *)(vptr + tile_vslice_offset(i + off)) = 0;
    }
}

static void clear_vertical_h(void *vptr, size_t off, size_t len)
{
    for (size_t i = 0; i < len; i += 2) {
        *(uint16_t *)(vptr + tile_vslice_offset(i + off)) = 0;
    }
}

static void clear_vertical_s(void *vptr, size_t off, size_t len)
{
    for (size_t i = 0; i < len; i += 4) {
        *(uint32_t *)(vptr + tile_vslice_offset(i + off)) = 0;
    }
}

static void clear_vertical_d(void *vptr, size_t off, size_t len)
{
    for (size_t i = 0; i < len; i += 8) {
        *(uint64_t *)(vptr + tile_vslice_offset(i + off)) = 0;
    }
}

static void clear_vertical_q(void *vptr, size_t off, size_t len)
{
    for (size_t i = 0; i < len; i += 16) {
        memset(vptr + tile_vslice_offset(i + off), 0, 16);
    }
}

/*
 * Copy elements from an array into a tile slice comprising len bytes.
 */

typedef void CopyFn(void *dst, const void *src, size_t len);

static void copy_horizontal(void *dst, const void *src, size_t len)
{
    memcpy(dst, src, len);
}

static void copy_vertical_b(void *vdst, const void *vsrc, size_t len)
{
    const uint8_t *src = vsrc;
    uint8_t *dst = vdst;
    size_t i;

    for (i = 0; i < len; ++i) {
        dst[tile_vslice_index(i)] = src[i];
    }
}

static void copy_vertical_h(void *vdst, const void *vsrc, size_t len)
{
    const uint16_t *src = vsrc;
    uint16_t *dst = vdst;
    size_t i;

    for (i = 0; i < len / 2; ++i) {
        dst[tile_vslice_index(i)] = src[i];
    }
}

static void copy_vertical_s(void *vdst, const void *vsrc, size_t len)
{
    const uint32_t *src = vsrc;
    uint32_t *dst = vdst;
    size_t i;

    for (i = 0; i < len / 4; ++i) {
        dst[tile_vslice_index(i)] = src[i];
    }
}

static void copy_vertical_d(void *vdst, const void *vsrc, size_t len)
{
    const uint64_t *src = vsrc;
    uint64_t *dst = vdst;
    size_t i;

    for (i = 0; i < len / 8; ++i) {
        dst[tile_vslice_index(i)] = src[i];
    }
}

static void copy_vertical_q(void *vdst, const void *vsrc, size_t len)
{
    for (size_t i = 0; i < len; i += 16) {
        memcpy(vdst + tile_vslice_offset(i), vsrc + i, 16);
    }
}

void HELPER(sme2_mova_cz_b)(void *vdst, void *vsrc, uint32_t desc)
{
    copy_vertical_b(vdst, vsrc, simd_oprsz(desc));
}

void HELPER(sme2_mova_cz_h)(void *vdst, void *vsrc, uint32_t desc)
{
    copy_vertical_h(vdst, vsrc, simd_oprsz(desc));
}

void HELPER(sme2_mova_cz_s)(void *vdst, void *vsrc, uint32_t desc)
{
    copy_vertical_s(vdst, vsrc, simd_oprsz(desc));
}

void HELPER(sme2_mova_cz_d)(void *vdst, void *vsrc, uint32_t desc)
{
    copy_vertical_d(vdst, vsrc, simd_oprsz(desc));
}

/*
 * Host and TLB primitives for vertical tile slice addressing.
 */

#define DO_LD(NAME, TYPE, HOST, TLB)                                        \
static inline void sme_##NAME##_v_host(void *za, intptr_t off, void *host)  \
{                                                                           \
    TYPE val = HOST(host);                                                  \
    *(TYPE *)(za + tile_vslice_offset(off)) = val;                          \
}                                                                           \
static inline void sme_##NAME##_v_tlb(CPUARMState *env, void *za,           \
                        intptr_t off, target_ulong addr, uintptr_t ra)      \
{                                                                           \
    TYPE val = TLB(env, useronly_clean_ptr(addr), ra);                      \
    *(TYPE *)(za + tile_vslice_offset(off)) = val;                          \
}

#define DO_ST(NAME, TYPE, HOST, TLB)                                        \
static inline void sme_##NAME##_v_host(void *za, intptr_t off, void *host)  \
{                                                                           \
    TYPE val = *(TYPE *)(za + tile_vslice_offset(off));                     \
    HOST(host, val);                                                        \
}                                                                           \
static inline void sme_##NAME##_v_tlb(CPUARMState *env, void *za,           \
                        intptr_t off, target_ulong addr, uintptr_t ra)      \
{                                                                           \
    TYPE val = *(TYPE *)(za + tile_vslice_offset(off));                     \
    TLB(env, useronly_clean_ptr(addr), val, ra);                            \
}

#define DO_LDQ(HNAME, VNAME) \
static inline void VNAME##_v_host(void *za, intptr_t off, void *host)       \
{                                                                           \
    HNAME##_host(za, tile_vslice_offset(off), host);                        \
}                                                                           \
static inline void VNAME##_v_tlb(CPUARMState *env, void *za, intptr_t off,  \
                               target_ulong addr, uintptr_t ra)             \
{                                                                           \
    HNAME##_tlb(env, za, tile_vslice_offset(off), addr, ra);                \
}

#define DO_STQ(HNAME, VNAME) \
static inline void VNAME##_v_host(void *za, intptr_t off, void *host)       \
{                                                                           \
    HNAME##_host(za, tile_vslice_offset(off), host);                        \
}                                                                           \
static inline void VNAME##_v_tlb(CPUARMState *env, void *za, intptr_t off,  \
                               target_ulong addr, uintptr_t ra)             \
{                                                                           \
    HNAME##_tlb(env, za, tile_vslice_offset(off), addr, ra);                \
}

DO_LD(ld1b, uint8_t, ldub_p, cpu_ldub_data_ra)
DO_LD(ld1h_be, uint16_t, lduw_be_p, cpu_lduw_be_data_ra)
DO_LD(ld1h_le, uint16_t, lduw_le_p, cpu_lduw_le_data_ra)
DO_LD(ld1s_be, uint32_t, ldl_be_p, cpu_ldl_be_data_ra)
DO_LD(ld1s_le, uint32_t, ldl_le_p, cpu_ldl_le_data_ra)
DO_LD(ld1d_be, uint64_t, ldq_be_p, cpu_ldq_be_data_ra)
DO_LD(ld1d_le, uint64_t, ldq_le_p, cpu_ldq_le_data_ra)

DO_LDQ(sve_ld1qq_be, sme_ld1q_be)
DO_LDQ(sve_ld1qq_le, sme_ld1q_le)

DO_ST(st1b, uint8_t, stb_p, cpu_stb_data_ra)
DO_ST(st1h_be, uint16_t, stw_be_p, cpu_stw_be_data_ra)
DO_ST(st1h_le, uint16_t, stw_le_p, cpu_stw_le_data_ra)
DO_ST(st1s_be, uint32_t, stl_be_p, cpu_stl_be_data_ra)
DO_ST(st1s_le, uint32_t, stl_le_p, cpu_stl_le_data_ra)
DO_ST(st1d_be, uint64_t, stq_be_p, cpu_stq_be_data_ra)
DO_ST(st1d_le, uint64_t, stq_le_p, cpu_stq_le_data_ra)

DO_STQ(sve_st1qq_be, sme_st1q_be)
DO_STQ(sve_st1qq_le, sme_st1q_le)

#undef DO_LD
#undef DO_ST
#undef DO_LDQ
#undef DO_STQ

/*
 * Common helper for all contiguous predicated loads.
 */

static inline QEMU_ALWAYS_INLINE
void sme_ld1(CPUARMState *env, void *za, uint64_t *vg,
             const target_ulong addr, uint32_t desc, const uintptr_t ra,
             const int esz, uint32_t mtedesc, bool vertical,
             sve_ldst1_host_fn *host_fn,
             sve_ldst1_tlb_fn *tlb_fn,
             ClearFn *clr_fn,
             CopyFn *cpy_fn)
{
    const intptr_t reg_max = simd_oprsz(desc);
    const intptr_t esize = 1 << esz;
    intptr_t reg_off, reg_last;
    SVEContLdSt info;
    void *host;
    int flags;

    /* Find the active elements.  */
    if (!sve_cont_ldst_elements(&info, addr, vg, reg_max, esz, esize)) {
        /* The entire predicate was false; no load occurs.  */
        clr_fn(za, 0, reg_max);
        return;
    }

    /* Probe the page(s).  Exit with exception for any invalid page. */
    sve_cont_ldst_pages(&info, FAULT_ALL, env, addr, MMU_DATA_LOAD, ra);

    /* Handle watchpoints for all active elements. */
    sve_cont_ldst_watchpoints(&info, env, vg, addr, esize, esize,
                              BP_MEM_READ, ra);

    /*
     * Handle mte checks for all active elements.
     * Since TBI must be set for MTE, !mtedesc => !mte_active.
     */
    if (mtedesc) {
        sve_cont_ldst_mte_check(&info, env, vg, addr, esize, esize,
                                mtedesc, ra);
    }

    flags = info.page[0].flags | info.page[1].flags;
    if (unlikely(flags != 0)) {
#ifdef CONFIG_USER_ONLY
        g_assert_not_reached();
#else
        /*
         * At least one page includes MMIO.
         * Any bus operation can fail with cpu_transaction_failed,
         * which for ARM will raise SyncExternal.  Perform the load
         * into scratch memory to preserve register state until the end.
         */
        ARMVectorReg scratch = { };

        reg_off = info.reg_off_first[0];
        reg_last = info.reg_off_last[1];
        if (reg_last < 0) {
            reg_last = info.reg_off_split;
            if (reg_last < 0) {
                reg_last = info.reg_off_last[0];
            }
        }

        do {
            uint64_t pg = vg[reg_off >> 6];
            do {
                if ((pg >> (reg_off & 63)) & 1) {
                    tlb_fn(env, &scratch, reg_off, addr + reg_off, ra);
                }
                reg_off += esize;
            } while (reg_off & 63);
        } while (reg_off <= reg_last);

        cpy_fn(za, &scratch, reg_max);
        return;
#endif
    }

    /* The entire operation is in RAM, on valid pages. */

    reg_off = info.reg_off_first[0];
    reg_last = info.reg_off_last[0];
    host = info.page[0].host;

    if (!vertical) {
        memset(za, 0, reg_max);
    } else if (reg_off) {
        clr_fn(za, 0, reg_off);
    }

    set_helper_retaddr(ra);

    while (reg_off <= reg_last) {
        uint64_t pg = vg[reg_off >> 6];
        do {
            if ((pg >> (reg_off & 63)) & 1) {
                host_fn(za, reg_off, host + reg_off);
            } else if (vertical) {
                clr_fn(za, reg_off, esize);
            }
            reg_off += esize;
        } while (reg_off <= reg_last && (reg_off & 63));
    }

    clear_helper_retaddr();

    /*
     * Use the slow path to manage the cross-page misalignment.
     * But we know this is RAM and cannot trap.
     */
    reg_off = info.reg_off_split;
    if (unlikely(reg_off >= 0)) {
        tlb_fn(env, za, reg_off, addr + reg_off, ra);
    }

    reg_off = info.reg_off_first[1];
    if (unlikely(reg_off >= 0)) {
        reg_last = info.reg_off_last[1];
        host = info.page[1].host;

        set_helper_retaddr(ra);

        do {
            uint64_t pg = vg[reg_off >> 6];
            do {
                if ((pg >> (reg_off & 63)) & 1) {
                    host_fn(za, reg_off, host + reg_off);
                } else if (vertical) {
                    clr_fn(za, reg_off, esize);
                }
                reg_off += esize;
            } while (reg_off & 63);
        } while (reg_off <= reg_last);

        clear_helper_retaddr();
    }
}

static inline QEMU_ALWAYS_INLINE
void sme_ld1_mte(CPUARMState *env, void *za, uint64_t *vg,
                 target_ulong addr, uint64_t desc, uintptr_t ra,
                 const int esz, bool vertical,
                 sve_ldst1_host_fn *host_fn,
                 sve_ldst1_tlb_fn *tlb_fn,
                 ClearFn *clr_fn,
                 CopyFn *cpy_fn)
{
    uint32_t mtedesc = desc >> 32;
    int bit55 = extract64(addr, 55, 1);

    /* Perform gross MTE suppression early. */
    if (!tbi_check(mtedesc, bit55) ||
        tcma_check(mtedesc, bit55, allocation_tag_from_addr(addr))) {
        mtedesc = 0;
    }

    sme_ld1(env, za, vg, addr, desc, ra, esz, mtedesc, vertical,
            host_fn, tlb_fn, clr_fn, cpy_fn);
}

#define DO_LD(L, END, ESZ)                                                 \
void HELPER(sme_ld1##L##END##_h)(CPUARMState *env, void *za, void *vg,     \
                                 target_ulong addr, uint64_t desc)         \
{                                                                          \
    sme_ld1(env, za, vg, addr, desc, GETPC(), ESZ, 0, false,               \
            sve_ld1##L##L##END##_host, sve_ld1##L##L##END##_tlb,           \
            clear_horizontal, copy_horizontal);                            \
}                                                                          \
void HELPER(sme_ld1##L##END##_v)(CPUARMState *env, void *za, void *vg,     \
                                 target_ulong addr, uint64_t desc)         \
{                                                                          \
    sme_ld1(env, za, vg, addr, desc, GETPC(), ESZ, 0, true,                \
            sme_ld1##L##END##_v_host, sme_ld1##L##END##_v_tlb,             \
            clear_vertical_##L, copy_vertical_##L);                        \
}                                                                          \
void HELPER(sme_ld1##L##END##_h_mte)(CPUARMState *env, void *za, void *vg, \
                                     target_ulong addr, uint64_t desc)     \
{                                                                          \
    sme_ld1_mte(env, za, vg, addr, desc, GETPC(), ESZ, false,              \
                sve_ld1##L##L##END##_host, sve_ld1##L##L##END##_tlb,       \
                clear_horizontal, copy_horizontal);                        \
}                                                                          \
void HELPER(sme_ld1##L##END##_v_mte)(CPUARMState *env, void *za, void *vg, \
                                     target_ulong addr, uint64_t desc)     \
{                                                                          \
    sme_ld1_mte(env, za, vg, addr, desc, GETPC(), ESZ, true,               \
                sme_ld1##L##END##_v_host, sme_ld1##L##END##_v_tlb,         \
                clear_vertical_##L, copy_vertical_##L);                    \
}

DO_LD(b, , MO_8)
DO_LD(h, _be, MO_16)
DO_LD(h, _le, MO_16)
DO_LD(s, _be, MO_32)
DO_LD(s, _le, MO_32)
DO_LD(d, _be, MO_64)
DO_LD(d, _le, MO_64)
DO_LD(q, _be, MO_128)
DO_LD(q, _le, MO_128)

#undef DO_LD

/*
 * Common helper for all contiguous predicated stores.
 */

static inline QEMU_ALWAYS_INLINE
void sme_st1(CPUARMState *env, void *za, uint64_t *vg,
             const target_ulong addr, uint32_t desc, const uintptr_t ra,
             const int esz, uint32_t mtedesc, bool vertical,
             sve_ldst1_host_fn *host_fn,
             sve_ldst1_tlb_fn *tlb_fn)
{
    const intptr_t reg_max = simd_oprsz(desc);
    const intptr_t esize = 1 << esz;
    intptr_t reg_off, reg_last;
    SVEContLdSt info;
    void *host;
    int flags;

    /* Find the active elements.  */
    if (!sve_cont_ldst_elements(&info, addr, vg, reg_max, esz, esize)) {
        /* The entire predicate was false; no store occurs.  */
        return;
    }

    /* Probe the page(s).  Exit with exception for any invalid page. */
    sve_cont_ldst_pages(&info, FAULT_ALL, env, addr, MMU_DATA_STORE, ra);

    /* Handle watchpoints for all active elements. */
    sve_cont_ldst_watchpoints(&info, env, vg, addr, esize, esize,
                              BP_MEM_WRITE, ra);

    /*
     * Handle mte checks for all active elements.
     * Since TBI must be set for MTE, !mtedesc => !mte_active.
     */
    if (mtedesc) {
        sve_cont_ldst_mte_check(&info, env, vg, addr, esize, esize,
                                mtedesc, ra);
    }

    flags = info.page[0].flags | info.page[1].flags;
    if (unlikely(flags != 0)) {
#ifdef CONFIG_USER_ONLY
        g_assert_not_reached();
#else
        /*
         * At least one page includes MMIO.
         * Any bus operation can fail with cpu_transaction_failed,
         * which for ARM will raise SyncExternal.  We cannot avoid
         * this fault and will leave with the store incomplete.
         */
        reg_off = info.reg_off_first[0];
        reg_last = info.reg_off_last[1];
        if (reg_last < 0) {
            reg_last = info.reg_off_split;
            if (reg_last < 0) {
                reg_last = info.reg_off_last[0];
            }
        }

        do {
            uint64_t pg = vg[reg_off >> 6];
            do {
                if ((pg >> (reg_off & 63)) & 1) {
                    tlb_fn(env, za, reg_off, addr + reg_off, ra);
                }
                reg_off += esize;
            } while (reg_off & 63);
        } while (reg_off <= reg_last);
        return;
#endif
    }

    reg_off = info.reg_off_first[0];
    reg_last = info.reg_off_last[0];
    host = info.page[0].host;

    set_helper_retaddr(ra);

    while (reg_off <= reg_last) {
        uint64_t pg = vg[reg_off >> 6];
        do {
            if ((pg >> (reg_off & 63)) & 1) {
                host_fn(za, reg_off, host + reg_off);
            }
            reg_off += 1 << esz;
        } while (reg_off <= reg_last && (reg_off & 63));
    }

    clear_helper_retaddr();

    /*
     * Use the slow path to manage the cross-page misalignment.
     * But we know this is RAM and cannot trap.
     */
    reg_off = info.reg_off_split;
    if (unlikely(reg_off >= 0)) {
        tlb_fn(env, za, reg_off, addr + reg_off, ra);
    }

    reg_off = info.reg_off_first[1];
    if (unlikely(reg_off >= 0)) {
        reg_last = info.reg_off_last[1];
        host = info.page[1].host;

        set_helper_retaddr(ra);

        do {
            uint64_t pg = vg[reg_off >> 6];
            do {
                if ((pg >> (reg_off & 63)) & 1) {
                    host_fn(za, reg_off, host + reg_off);
                }
                reg_off += 1 << esz;
            } while (reg_off & 63);
        } while (reg_off <= reg_last);

        clear_helper_retaddr();
    }
}

static inline QEMU_ALWAYS_INLINE
void sme_st1_mte(CPUARMState *env, void *za, uint64_t *vg, target_ulong addr,
                 uint64_t desc, uintptr_t ra, int esz, bool vertical,
                 sve_ldst1_host_fn *host_fn,
                 sve_ldst1_tlb_fn *tlb_fn)
{
    uint32_t mtedesc = desc >> 32;
    int bit55 = extract64(addr, 55, 1);

    /* Perform gross MTE suppression early. */
    if (!tbi_check(mtedesc, bit55) ||
        tcma_check(mtedesc, bit55, allocation_tag_from_addr(addr))) {
        mtedesc = 0;
    }

    sme_st1(env, za, vg, addr, desc, ra, esz, mtedesc,
            vertical, host_fn, tlb_fn);
}

#define DO_ST(L, END, ESZ)                                                 \
void HELPER(sme_st1##L##END##_h)(CPUARMState *env, void *za, void *vg,     \
                                 target_ulong addr, uint64_t desc)         \
{                                                                          \
    sme_st1(env, za, vg, addr, desc, GETPC(), ESZ, 0, false,               \
            sve_st1##L##L##END##_host, sve_st1##L##L##END##_tlb);          \
}                                                                          \
void HELPER(sme_st1##L##END##_v)(CPUARMState *env, void *za, void *vg,     \
                                 target_ulong addr, uint64_t desc)         \
{                                                                          \
    sme_st1(env, za, vg, addr, desc, GETPC(), ESZ, 0, true,                \
            sme_st1##L##END##_v_host, sme_st1##L##END##_v_tlb);            \
}                                                                          \
void HELPER(sme_st1##L##END##_h_mte)(CPUARMState *env, void *za, void *vg, \
                                     target_ulong addr, uint64_t desc)     \
{                                                                          \
    sme_st1_mte(env, za, vg, addr, desc, GETPC(), ESZ, false,              \
                sve_st1##L##L##END##_host, sve_st1##L##L##END##_tlb);      \
}                                                                          \
void HELPER(sme_st1##L##END##_v_mte)(CPUARMState *env, void *za, void *vg, \
                                     target_ulong addr, uint64_t desc)     \
{                                                                          \
    sme_st1_mte(env, za, vg, addr, desc, GETPC(), ESZ, true,               \
                sme_st1##L##END##_v_host, sme_st1##L##END##_v_tlb);        \
}

DO_ST(b, , MO_8)
DO_ST(h, _be, MO_16)
DO_ST(h, _le, MO_16)
DO_ST(s, _be, MO_32)
DO_ST(s, _le, MO_32)
DO_ST(d, _be, MO_64)
DO_ST(d, _le, MO_64)
DO_ST(q, _be, MO_128)
DO_ST(q, _le, MO_128)

#undef DO_ST

void HELPER(sme_addha_s)(void *vzda, void *vzn, void *vpn,
                         void *vpm, uint32_t desc)
{
    intptr_t row, col, oprsz = simd_oprsz(desc) / 4;
    uint64_t *pn = vpn, *pm = vpm;
    uint32_t *zda = vzda, *zn = vzn;

    for (row = 0; row < oprsz; ) {
        uint64_t pa = pn[row >> 4];
        do {
            if (pa & 1) {
                for (col = 0; col < oprsz; ) {
                    uint64_t pb = pm[col >> 4];
                    do {
                        if (pb & 1) {
                            zda[tile_vslice_index(row) + H4(col)] += zn[H4(col)];
                        }
                        pb >>= 4;
                    } while (++col & 15);
                }
            }
            pa >>= 4;
        } while (++row & 15);
    }
}

void HELPER(sme_addha_d)(void *vzda, void *vzn, void *vpn,
                         void *vpm, uint32_t desc)
{
    intptr_t row, col, oprsz = simd_oprsz(desc) / 8;
    uint8_t *pn = vpn, *pm = vpm;
    uint64_t *zda = vzda, *zn = vzn;

    for (row = 0; row < oprsz; ++row) {
        if (pn[H1(row)] & 1) {
            for (col = 0; col < oprsz; ++col) {
                if (pm[H1(col)] & 1) {
                    zda[tile_vslice_index(row) + col] += zn[col];
                }
            }
        }
    }
}

void HELPER(sme_addva_s)(void *vzda, void *vzn, void *vpn,
                         void *vpm, uint32_t desc)
{
    intptr_t row, col, oprsz = simd_oprsz(desc) / 4;
    uint64_t *pn = vpn, *pm = vpm;
    uint32_t *zda = vzda, *zn = vzn;

    for (row = 0; row < oprsz; ) {
        uint64_t pa = pn[row >> 4];
        do {
            if (pa & 1) {
                uint32_t zn_row = zn[H4(row)];
                for (col = 0; col < oprsz; ) {
                    uint64_t pb = pm[col >> 4];
                    do {
                        if (pb & 1) {
                            zda[tile_vslice_index(row) + H4(col)] += zn_row;
                        }
                        pb >>= 4;
                    } while (++col & 15);
                }
            }
            pa >>= 4;
        } while (++row & 15);
    }
}

void HELPER(sme_addva_d)(void *vzda, void *vzn, void *vpn,
                         void *vpm, uint32_t desc)
{
    intptr_t row, col, oprsz = simd_oprsz(desc) / 8;
    uint8_t *pn = vpn, *pm = vpm;
    uint64_t *zda = vzda, *zn = vzn;

    for (row = 0; row < oprsz; ++row) {
        if (pn[H1(row)] & 1) {
            uint64_t zn_row = zn[row];
            for (col = 0; col < oprsz; ++col) {
                if (pm[H1(col)] & 1) {
                    zda[tile_vslice_index(row) + col] += zn_row;
                }
            }
        }
    }
}

static void do_fmopa_h(void *vza, void *vzn, void *vzm, uint16_t *pn,
                       uint16_t *pm, float_status *fpst, uint32_t desc,
                       uint16_t negx, int negf)
{
    intptr_t row, col, oprsz = simd_maxsz(desc);

    for (row = 0; row < oprsz; ) {
        uint16_t pa = pn[H2(row >> 4)];
        do {
            if (pa & 1) {
                void *vza_row = vza + tile_vslice_offset(row);
                uint16_t n = *(uint32_t *)(vzn + H1_2(row)) ^ negx;

                for (col = 0; col < oprsz; ) {
                    uint16_t pb = pm[H2(col >> 4)];
                    do {
                        if (pb & 1) {
                            uint16_t *a = vza_row + H1_2(col);
                            uint16_t *m = vzm + H1_2(col);
                            *a = float16_muladd(n, *m, *a, negf, fpst);
                        }
                        col += 2;
                        pb >>= 2;
                    } while (col & 15);
                }
            }
            row += 2;
            pa >>= 2;
        } while (row & 15);
    }
}

void HELPER(sme_fmopa_h)(void *vza, void *vzn, void *vzm, void *vpn,
                         void *vpm, float_status *fpst, uint32_t desc)
{
    do_fmopa_h(vza, vzn, vzm, vpn, vpm, fpst, desc, 0, 0);
}

void HELPER(sme_fmops_h)(void *vza, void *vzn, void *vzm, void *vpn,
                         void *vpm, float_status *fpst, uint32_t desc)
{
    do_fmopa_h(vza, vzn, vzm, vpn, vpm, fpst, desc, 1u << 15, 0);
}

void HELPER(sme_ah_fmops_h)(void *vza, void *vzn, void *vzm, void *vpn,
                            void *vpm, float_status *fpst, uint32_t desc)
{
    do_fmopa_h(vza, vzn, vzm, vpn, vpm, fpst, desc, 0,
               float_muladd_negate_product);
}

static void do_fmopa_s(void *vza, void *vzn, void *vzm, uint16_t *pn,
                       uint16_t *pm, float_status *fpst, uint32_t desc,
                       uint32_t negx, int negf)
{
    intptr_t row, col, oprsz = simd_maxsz(desc);

    for (row = 0; row < oprsz; ) {
        uint16_t pa = pn[H2(row >> 4)];
        do {
            if (pa & 1) {
                void *vza_row = vza + tile_vslice_offset(row);
                uint32_t n = *(uint32_t *)(vzn + H1_4(row)) ^ negx;

                for (col = 0; col < oprsz; ) {
                    uint16_t pb = pm[H2(col >> 4)];
                    do {
                        if (pb & 1) {
                            uint32_t *a = vza_row + H1_4(col);
                            uint32_t *m = vzm + H1_4(col);
                            *a = float32_muladd(n, *m, *a, negf, fpst);
                        }
                        col += 4;
                        pb >>= 4;
                    } while (col & 15);
                }
            }
            row += 4;
            pa >>= 4;
        } while (row & 15);
    }
}

void HELPER(sme_fmopa_s)(void *vza, void *vzn, void *vzm, void *vpn,
                         void *vpm, float_status *fpst, uint32_t desc)
{
    do_fmopa_s(vza, vzn, vzm, vpn, vpm, fpst, desc, 0, 0);
}

void HELPER(sme_fmops_s)(void *vza, void *vzn, void *vzm, void *vpn,
                         void *vpm, float_status *fpst, uint32_t desc)
{
    do_fmopa_s(vza, vzn, vzm, vpn, vpm, fpst, desc, 1u << 31, 0);
}

void HELPER(sme_ah_fmops_s)(void *vza, void *vzn, void *vzm, void *vpn,
                            void *vpm, float_status *fpst, uint32_t desc)
{
    do_fmopa_s(vza, vzn, vzm, vpn, vpm, fpst, desc, 0,
               float_muladd_negate_product);
}

static void do_fmopa_d(uint64_t *za, uint64_t *zn, uint64_t *zm, uint8_t *pn,
                       uint8_t *pm, float_status *fpst, uint32_t desc,
                       uint64_t negx, int negf)
{
    intptr_t row, col, oprsz = simd_oprsz(desc) / 8;

    for (row = 0; row < oprsz; ++row) {
        if (pn[H1(row)] & 1) {
            uint64_t *za_row = &za[tile_vslice_index(row)];
            uint64_t n = zn[row] ^ negx;

            for (col = 0; col < oprsz; ++col) {
                if (pm[H1(col)] & 1) {
                    uint64_t *a = &za_row[col];
                    *a = float64_muladd(n, zm[col], *a, negf, fpst);
                }
            }
        }
    }
}

void HELPER(sme_fmopa_d)(void *vza, void *vzn, void *vzm, void *vpn,
                         void *vpm, float_status *fpst, uint32_t desc)
{
    do_fmopa_d(vza, vzn, vzm, vpn, vpm, fpst, desc, 0, 0);
}

void HELPER(sme_fmops_d)(void *vza, void *vzn, void *vzm, void *vpn,
                         void *vpm, float_status *fpst, uint32_t desc)
{
    do_fmopa_d(vza, vzn, vzm, vpn, vpm, fpst, desc, 1ull << 63, 0);
}

void HELPER(sme_ah_fmops_d)(void *vza, void *vzn, void *vzm, void *vpn,
                            void *vpm, float_status *fpst, uint32_t desc)
{
    do_fmopa_d(vza, vzn, vzm, vpn, vpm, fpst, desc, 0,
               float_muladd_negate_product);
}

static void do_bfmopa(void *vza, void *vzn, void *vzm, uint16_t *pn,
                      uint16_t *pm, float_status *fpst, uint32_t desc,
                      uint16_t negx, int negf)
{
    intptr_t row, col, oprsz = simd_maxsz(desc);

    for (row = 0; row < oprsz; ) {
        uint16_t pa = pn[H2(row >> 4)];
        do {
            if (pa & 1) {
                void *vza_row = vza + tile_vslice_offset(row);
                uint16_t n = *(uint32_t *)(vzn + H1_2(row)) ^ negx;

                for (col = 0; col < oprsz; ) {
                    uint16_t pb = pm[H2(col >> 4)];
                    do {
                        if (pb & 1) {
                            uint16_t *a = vza_row + H1_2(col);
                            uint16_t *m = vzm + H1_2(col);
                            *a = bfloat16_muladd(n, *m, *a, negf, fpst);
                        }
                        col += 2;
                        pb >>= 2;
                    } while (col & 15);
                }
            }
            row += 2;
            pa >>= 2;
        } while (row & 15);
    }
}

void HELPER(sme_bfmopa)(void *vza, void *vzn, void *vzm, void *vpn,
                        void *vpm, float_status *fpst, uint32_t desc)
{
    do_bfmopa(vza, vzn, vzm, vpn, vpm, fpst, desc, 0, 0);
}

void HELPER(sme_bfmops)(void *vza, void *vzn, void *vzm, void *vpn,
                        void *vpm, float_status *fpst, uint32_t desc)
{
    do_bfmopa(vza, vzn, vzm, vpn, vpm, fpst, desc, 1u << 15, 0);
}

void HELPER(sme_ah_bfmops)(void *vza, void *vzn, void *vzm, void *vpn,
                           void *vpm, float_status *fpst, uint32_t desc)
{
    do_bfmopa(vza, vzn, vzm, vpn, vpm, fpst, desc, 0,
              float_muladd_negate_product);
}

/*
 * Alter PAIR as needed for controlling predicates being false,
 * and for NEG on an enabled row element.
 */
static inline uint32_t f16mop_adj_pair(uint32_t pair, uint32_t pg, uint32_t neg)
{
    /*
     * The pseudocode uses a conditional negate after the conditional zero.
     * It is simpler here to unconditionally negate before conditional zero.
     */
    pair ^= neg;
    if (!(pg & 1)) {
        pair &= 0xffff0000u;
    }
    if (!(pg & 4)) {
        pair &= 0x0000ffffu;
    }
    return pair;
}

static inline uint32_t f16mop_ah_neg_adj_pair(uint32_t pair, uint32_t pg)
{
    uint32_t l = pg & 1 ? float16_ah_chs(pair) : 0;
    uint32_t h = pg & 4 ? float16_ah_chs(pair >> 16) : 0;
    return l | (h << 16);
}

static inline uint32_t bf16mop_ah_neg_adj_pair(uint32_t pair, uint32_t pg)
{
    uint32_t l = pg & 1 ? bfloat16_ah_chs(pair) : 0;
    uint32_t h = pg & 4 ? bfloat16_ah_chs(pair >> 16) : 0;
    return l | (h << 16);
}

static float32 f16_dotadd(float32 sum, uint32_t e1, uint32_t e2,
                          float_status *s_f16, float_status *s_std,
                          float_status *s_odd)
{
    /*
     * We need three different float_status for different parts of this
     * operation:
     *  - the input conversion of the float16 values must use the
     *    f16-specific float_status, so that the FPCR.FZ16 control is applied
     *  - operations on float32 including the final accumulation must use
     *    the normal float_status, so that FPCR.FZ is applied
     *  - we have pre-set-up copy of s_std which is set to round-to-odd,
     *    for the multiply (see below)
     */
    float16 h1r = e1 & 0xffff;
    float16 h1c = e1 >> 16;
    float16 h2r = e2 & 0xffff;
    float16 h2c = e2 >> 16;
    float32 t32;

    /* C.f. FPProcessNaNs4 */
    if (float16_is_any_nan(h1r) || float16_is_any_nan(h1c) ||
        float16_is_any_nan(h2r) || float16_is_any_nan(h2c)) {
        float16 t16;

        if (float16_is_signaling_nan(h1r, s_f16)) {
            t16 = h1r;
        } else if (float16_is_signaling_nan(h1c, s_f16)) {
            t16 = h1c;
        } else if (float16_is_signaling_nan(h2r, s_f16)) {
            t16 = h2r;
        } else if (float16_is_signaling_nan(h2c, s_f16)) {
            t16 = h2c;
        } else if (float16_is_any_nan(h1r)) {
            t16 = h1r;
        } else if (float16_is_any_nan(h1c)) {
            t16 = h1c;
        } else if (float16_is_any_nan(h2r)) {
            t16 = h2r;
        } else {
            t16 = h2c;
        }
        t32 = float16_to_float32(t16, true, s_f16);
    } else {
        float64 e1r = float16_to_float64(h1r, true, s_f16);
        float64 e1c = float16_to_float64(h1c, true, s_f16);
        float64 e2r = float16_to_float64(h2r, true, s_f16);
        float64 e2c = float16_to_float64(h2c, true, s_f16);
        float64 t64;

        /*
         * The ARM pseudocode function FPDot performs both multiplies
         * and the add with a single rounding operation.  Emulate this
         * by performing the first multiply in round-to-odd, then doing
         * the second multiply as fused multiply-add, and rounding to
         * float32 all in one step.
         */
        t64 = float64_mul(e1r, e2r, s_odd);
        t64 = float64r32_muladd(e1c, e2c, t64, 0, s_std);

        /* This conversion is exact, because we've already rounded. */
        t32 = float64_to_float32(t64, s_std);
    }

    /* The final accumulation step is not fused. */
    return float32_add(sum, t32, s_std);
}

static void do_fmopa_w_h(void *vza, void *vzn, void *vzm, uint16_t *pn,
                         uint16_t *pm, CPUARMState *env, uint32_t desc,
                         uint32_t negx, bool ah_neg)
{
    intptr_t row, col, oprsz = simd_maxsz(desc);
    float_status fpst_odd = env->vfp.fp_status[FPST_ZA];

    set_float_rounding_mode(float_round_to_odd, &fpst_odd);

    for (row = 0; row < oprsz; ) {
        uint16_t prow = pn[H2(row >> 4)];
        do {
            void *vza_row = vza + tile_vslice_offset(row);
            uint32_t n = *(uint32_t *)(vzn + H1_4(row));

            if (ah_neg) {
                n = f16mop_ah_neg_adj_pair(n, prow);
            } else {
                n = f16mop_adj_pair(n, prow, negx);
            }

            for (col = 0; col < oprsz; ) {
                uint16_t pcol = pm[H2(col >> 4)];
                do {
                    if (prow & pcol & 0b0101) {
                        uint32_t *a = vza_row + H1_4(col);
                        uint32_t m = *(uint32_t *)(vzm + H1_4(col));

                        m = f16mop_adj_pair(m, pcol, 0);
                        *a = f16_dotadd(*a, n, m,
                                        &env->vfp.fp_status[FPST_ZA_F16],
                                        &env->vfp.fp_status[FPST_ZA],
                                        &fpst_odd);
                    }
                    col += 4;
                    pcol >>= 4;
                } while (col & 15);
            }
            row += 4;
            prow >>= 4;
        } while (row & 15);
    }
}

void HELPER(sme_fmopa_w_h)(void *vza, void *vzn, void *vzm, void *vpn,
                           void *vpm, CPUARMState *env, uint32_t desc)
{
    do_fmopa_w_h(vza, vzn, vzm, vpn, vpm, env, desc, 0, false);
}

void HELPER(sme_fmops_w_h)(void *vza, void *vzn, void *vzm, void *vpn,
                           void *vpm, CPUARMState *env, uint32_t desc)
{
    do_fmopa_w_h(vza, vzn, vzm, vpn, vpm, env, desc, 0x80008000u, false);
}

void HELPER(sme_ah_fmops_w_h)(void *vza, void *vzn, void *vzm, void *vpn,
                              void *vpm, CPUARMState *env, uint32_t desc)
{
    do_fmopa_w_h(vza, vzn, vzm, vpn, vpm, env, desc, 0, true);
}

void HELPER(sme2_fdot_h)(void *vd, void *vn, void *vm, void *va,
                         CPUARMState *env, uint32_t desc)
{
    intptr_t i, oprsz = simd_maxsz(desc);
    bool za = extract32(desc, SIMD_DATA_SHIFT, 1);
    float_status *fpst_std = &env->vfp.fp_status[za ? FPST_ZA : FPST_A64];
    float_status *fpst_f16 = &env->vfp.fp_status[za ? FPST_ZA_F16 : FPST_A64_F16];
    float_status fpst_odd = *fpst_std;
    float32 *d = vd, *a = va;
    uint32_t *n = vn, *m = vm;

    set_float_rounding_mode(float_round_to_odd, &fpst_odd);

    for (i = 0; i < oprsz / sizeof(float32); ++i) {
        d[H4(i)] = f16_dotadd(a[H4(i)], n[H4(i)], m[H4(i)],
                              fpst_f16, fpst_std, &fpst_odd);
    }
}

void HELPER(sme2_fdot_idx_h)(void *vd, void *vn, void *vm, void *va,
                             CPUARMState *env, uint32_t desc)
{
    intptr_t i, j, oprsz = simd_maxsz(desc);
    intptr_t elements = oprsz / sizeof(float32);
    intptr_t eltspersegment = MIN(4, elements);
    int idx = extract32(desc, SIMD_DATA_SHIFT, 2);
    bool za = extract32(desc, SIMD_DATA_SHIFT + 2, 1);
    float_status *fpst_std = &env->vfp.fp_status[za ? FPST_ZA : FPST_A64];
    float_status *fpst_f16 = &env->vfp.fp_status[za ? FPST_ZA_F16 : FPST_A64_F16];
    float_status fpst_odd = *fpst_std;
    float32 *d = vd, *a = va;
    uint32_t *n = vn, *m = (uint32_t *)vm + H4(idx);

    set_float_rounding_mode(float_round_to_odd, &fpst_odd);

    for (i = 0; i < elements; i += eltspersegment) {
        uint32_t mm = m[i];
        for (j = 0; j < eltspersegment; ++j) {
            d[H4(i + j)] = f16_dotadd(a[H4(i + j)], n[H4(i + j)], mm,
                                      fpst_f16, fpst_std, &fpst_odd);
        }
    }
}

void HELPER(sme2_fvdot_idx_h)(void *vd, void *vn, void *vm, void *va,
                              CPUARMState *env, uint32_t desc)
{
    intptr_t i, j, oprsz = simd_maxsz(desc);
    intptr_t elements = oprsz / sizeof(float32);
    intptr_t eltspersegment = MIN(4, elements);
    int idx = extract32(desc, SIMD_DATA_SHIFT, 2);
    int sel = extract32(desc, SIMD_DATA_SHIFT + 2, 1);
    float_status fpst_odd, *fpst_std, *fpst_f16;
    float32 *d = vd, *a = va;
    uint16_t *n0 = vn;
    uint16_t *n1 = vn + sizeof(ARMVectorReg);
    uint32_t *m = (uint32_t *)vm + H4(idx);

    fpst_std = &env->vfp.fp_status[FPST_ZA];
    fpst_f16 = &env->vfp.fp_status[FPST_ZA_F16];
    fpst_odd = *fpst_std;
    set_float_rounding_mode(float_round_to_odd, &fpst_odd);

    for (i = 0; i < elements; i += eltspersegment) {
        uint32_t mm = m[i];
        for (j = 0; j < eltspersegment; ++j) {
            uint32_t nn = (n0[H2(2 * (i + j) + sel)])
                        | (n1[H2(2 * (i + j) + sel)] << 16);
            d[i + H4(j)] = f16_dotadd(a[i + H4(j)], nn, mm,
                                      fpst_f16, fpst_std, &fpst_odd);
        }
    }
}

static void do_bfmopa_w(void *vza, void *vzn, void *vzm,
                        uint16_t *pn, uint16_t *pm, CPUARMState *env,
                        uint32_t desc, uint32_t negx, bool ah_neg)
{
    intptr_t row, col, oprsz = simd_maxsz(desc);
    float_status fpst, fpst_odd;

    if (is_ebf(env, &fpst, &fpst_odd)) {
        for (row = 0; row < oprsz; ) {
            uint16_t prow = pn[H2(row >> 4)];
            do {
                void *vza_row = vza + tile_vslice_offset(row);
                uint32_t n = *(uint32_t *)(vzn + H1_4(row));

                if (ah_neg) {
                    n = bf16mop_ah_neg_adj_pair(n, prow);
                } else {
                    n = f16mop_adj_pair(n, prow, negx);
                }

                for (col = 0; col < oprsz; ) {
                    uint16_t pcol = pm[H2(col >> 4)];
                    do {
                        if (prow & pcol & 0b0101) {
                            uint32_t *a = vza_row + H1_4(col);
                            uint32_t m = *(uint32_t *)(vzm + H1_4(col));

                            m = f16mop_adj_pair(m, pcol, 0);
                            *a = bfdotadd_ebf(*a, n, m, &fpst, &fpst_odd);
                        }
                        col += 4;
                        pcol >>= 4;
                    } while (col & 15);
                }
                row += 4;
                prow >>= 4;
            } while (row & 15);
        }
    } else {
        for (row = 0; row < oprsz; ) {
            uint16_t prow = pn[H2(row >> 4)];
            do {
                void *vza_row = vza + tile_vslice_offset(row);
                uint32_t n = *(uint32_t *)(vzn + H1_4(row));

                if (ah_neg) {
                    n = bf16mop_ah_neg_adj_pair(n, prow);
                } else {
                    n = f16mop_adj_pair(n, prow, negx);
                }

                for (col = 0; col < oprsz; ) {
                    uint16_t pcol = pm[H2(col >> 4)];
                    do {
                        if (prow & pcol & 0b0101) {
                            uint32_t *a = vza_row + H1_4(col);
                            uint32_t m = *(uint32_t *)(vzm + H1_4(col));

                            m = f16mop_adj_pair(m, pcol, 0);
                            *a = bfdotadd(*a, n, m, &fpst);
                        }
                        col += 4;
                        pcol >>= 4;
                    } while (col & 15);
                }
                row += 4;
                prow >>= 4;
            } while (row & 15);
        }
    }
}

void HELPER(sme_bfmopa_w)(void *vza, void *vzn, void *vzm, void *vpn,
                          void *vpm, CPUARMState *env, uint32_t desc)
{
    do_bfmopa_w(vza, vzn, vzm, vpn, vpm, env, desc, 0, false);
}

void HELPER(sme_bfmops_w)(void *vza, void *vzn, void *vzm, void *vpn,
                          void *vpm, CPUARMState *env, uint32_t desc)
{
    do_bfmopa_w(vza, vzn, vzm, vpn, vpm, env, desc, 0x80008000u, false);
}

void HELPER(sme_ah_bfmops_w)(void *vza, void *vzn, void *vzm, void *vpn,
                             void *vpm, CPUARMState *env, uint32_t desc)
{
    do_bfmopa_w(vza, vzn, vzm, vpn, vpm, env, desc, 0, true);
}

typedef uint32_t IMOPFn32(uint32_t, uint32_t, uint32_t, uint8_t, bool);
static inline void do_imopa_s(uint32_t *za, uint32_t *zn, uint32_t *zm,
                              uint8_t *pn, uint8_t *pm,
                              uint32_t desc, IMOPFn32 *fn)
{
    intptr_t row, col, oprsz = simd_oprsz(desc) / 4;
    bool neg = simd_data(desc);

    for (row = 0; row < oprsz; ++row) {
        uint8_t pa = (pn[H1(row >> 1)] >> ((row & 1) * 4)) & 0xf;
        uint32_t *za_row = &za[tile_vslice_index(row)];
        uint32_t n = zn[H4(row)];

        for (col = 0; col < oprsz; ++col) {
            uint8_t pb = pm[H1(col >> 1)] >> ((col & 1) * 4);
            uint32_t *a = &za_row[H4(col)];

            *a = fn(n, zm[H4(col)], *a, pa & pb, neg);
        }
    }
}

typedef uint64_t IMOPFn64(uint64_t, uint64_t, uint64_t, uint8_t, bool);
static inline void do_imopa_d(uint64_t *za, uint64_t *zn, uint64_t *zm,
                              uint8_t *pn, uint8_t *pm,
                              uint32_t desc, IMOPFn64 *fn)
{
    intptr_t row, col, oprsz = simd_oprsz(desc) / 8;
    bool neg = simd_data(desc);

    for (row = 0; row < oprsz; ++row) {
        uint8_t pa = pn[H1(row)];
        uint64_t *za_row = &za[tile_vslice_index(row)];
        uint64_t n = zn[row];

        for (col = 0; col < oprsz; ++col) {
            uint8_t pb = pm[H1(col)];
            uint64_t *a = &za_row[col];

            *a = fn(n, zm[col], *a, pa & pb, neg);
        }
    }
}

#define DEF_IMOP_8x4_32(NAME, NTYPE, MTYPE) \
static uint32_t NAME(uint32_t n, uint32_t m, uint32_t a, uint8_t p, bool neg) \
{                                                                           \
    uint32_t sum = 0;                                                       \
    /* Apply P to N as a mask, making the inactive elements 0. */           \
    n &= expand_pred_b(p);                                                  \
    sum += (NTYPE)(n >> 0) * (MTYPE)(m >> 0);                               \
    sum += (NTYPE)(n >> 8) * (MTYPE)(m >> 8);                               \
    sum += (NTYPE)(n >> 16) * (MTYPE)(m >> 16);                             \
    sum += (NTYPE)(n >> 24) * (MTYPE)(m >> 24);                             \
    return neg ? a - sum : a + sum;                                         \
}

#define DEF_IMOP_16x4_64(NAME, NTYPE, MTYPE) \
static uint64_t NAME(uint64_t n, uint64_t m, uint64_t a, uint8_t p, bool neg) \
{                                                                           \
    uint64_t sum = 0;                                                       \
    /* Apply P to N as a mask, making the inactive elements 0. */           \
    n &= expand_pred_h(p);                                                  \
    sum += (int64_t)(NTYPE)(n >> 0) * (MTYPE)(m >> 0);                      \
    sum += (int64_t)(NTYPE)(n >> 16) * (MTYPE)(m >> 16);                    \
    sum += (int64_t)(NTYPE)(n >> 32) * (MTYPE)(m >> 32);                    \
    sum += (int64_t)(NTYPE)(n >> 48) * (MTYPE)(m >> 48);                    \
    return neg ? a - sum : a + sum;                                         \
}

DEF_IMOP_8x4_32(smopa_s, int8_t, int8_t)
DEF_IMOP_8x4_32(umopa_s, uint8_t, uint8_t)
DEF_IMOP_8x4_32(sumopa_s, int8_t, uint8_t)
DEF_IMOP_8x4_32(usmopa_s, uint8_t, int8_t)

DEF_IMOP_16x4_64(smopa_d, int16_t, int16_t)
DEF_IMOP_16x4_64(umopa_d, uint16_t, uint16_t)
DEF_IMOP_16x4_64(sumopa_d, int16_t, uint16_t)
DEF_IMOP_16x4_64(usmopa_d, uint16_t, int16_t)

#define DEF_IMOPH(P, NAME, S) \
    void HELPER(P##_##NAME##_##S)(void *vza, void *vzn, void *vzm,          \
                                  void *vpn, void *vpm, uint32_t desc)      \
    { do_imopa_##S(vza, vzn, vzm, vpn, vpm, desc, NAME##_##S); }

DEF_IMOPH(sme, smopa, s)
DEF_IMOPH(sme, umopa, s)
DEF_IMOPH(sme, sumopa, s)
DEF_IMOPH(sme, usmopa, s)

DEF_IMOPH(sme, smopa, d)
DEF_IMOPH(sme, umopa, d)
DEF_IMOPH(sme, sumopa, d)
DEF_IMOPH(sme, usmopa, d)

static uint32_t bmopa_s(uint32_t n, uint32_t m, uint32_t a, uint8_t p, bool neg)
{
    uint32_t sum = ctpop32(~(n ^ m));
    if (neg) {
        sum = -sum;
    }
    if (!(p & 1)) {
        sum = 0;
    }
    return a + sum;
}

DEF_IMOPH(sme2, bmopa, s)

#define DEF_IMOP_16x2_32(NAME, NTYPE, MTYPE) \
static uint32_t NAME(uint32_t n, uint32_t m, uint32_t a, uint8_t p, bool neg) \
{                                                                           \
    uint32_t sum = 0;                                                       \
    /* Apply P to N as a mask, making the inactive elements 0. */           \
    n &= expand_pred_h(p);                                                  \
    sum += (NTYPE)(n >> 0) * (MTYPE)(m >> 0);                               \
    sum += (NTYPE)(n >> 16) * (MTYPE)(m >> 16);                             \
    return neg ? a - sum : a + sum;                                         \
}

DEF_IMOP_16x2_32(smopa2_s, int16_t, int16_t)
DEF_IMOP_16x2_32(umopa2_s, uint16_t, uint16_t)

DEF_IMOPH(sme2, smopa2, s)
DEF_IMOPH(sme2, umopa2, s)

#define DO_VDOT_IDX(NAME, TYPED, TYPEN, TYPEM, HD, HN) \
void HELPER(NAME)(void *vd, void *vn, void *vm, uint32_t desc)            \
{                                                                         \
    intptr_t svl = simd_oprsz(desc);                                      \
    intptr_t elements = svl / sizeof(TYPED);                              \
    intptr_t eltperseg = 16 / sizeof(TYPED);                              \
    intptr_t nreg = sizeof(TYPED) / sizeof(TYPEN);                        \
    intptr_t vstride = (svl / nreg) * sizeof(ARMVectorReg);               \
    intptr_t zstride = sizeof(ARMVectorReg) / sizeof(TYPEN);              \
    intptr_t idx = extract32(desc, SIMD_DATA_SHIFT, 2);                   \
    TYPEN *n = vn;                                                        \
    TYPEM *m = vm;                                                        \
    for (intptr_t r = 0; r < nreg; r++) {                                 \
        TYPED *d = vd + r * vstride;                                      \
        for (intptr_t seg = 0; seg < elements; seg += eltperseg) {        \
            intptr_t s = seg + idx;                                       \
            for (intptr_t e = seg; e < seg + eltperseg; e++) {            \
                TYPED sum = d[HD(e)];                                     \
                for (intptr_t i = 0; i < nreg; i++) {                     \
                    TYPED nn = n[i * zstride + HN(nreg * e + r)];         \
                    TYPED mm = m[HN(nreg * s + i)];                       \
                    sum += nn * mm;                                       \
                }                                                         \
                d[HD(e)] = sum;                                           \
            }                                                             \
        }                                                                 \
    }                                                                     \
}

DO_VDOT_IDX(sme2_svdot_idx_4b, int32_t, int8_t, int8_t, H4, H1)
DO_VDOT_IDX(sme2_uvdot_idx_4b, uint32_t, uint8_t, uint8_t, H4, H1)
DO_VDOT_IDX(sme2_suvdot_idx_4b, int32_t, int8_t, uint8_t, H4, H1)
DO_VDOT_IDX(sme2_usvdot_idx_4b, int32_t, uint8_t, int8_t, H4, H1)

DO_VDOT_IDX(sme2_svdot_idx_4h, int64_t, int16_t, int16_t, H8, H2)
DO_VDOT_IDX(sme2_uvdot_idx_4h, uint64_t, uint16_t, uint16_t, H8, H2)

DO_VDOT_IDX(sme2_svdot_idx_2h, int32_t, int16_t, int16_t, H4, H2)
DO_VDOT_IDX(sme2_uvdot_idx_2h, uint32_t, uint16_t, uint16_t, H4, H2)

#undef DO_VDOT_IDX

#define DO_MLALL(NAME, TYPEW, TYPEN, TYPEM, HW, HN, OP) \
void HELPER(NAME)(void *vd, void *vn, void *vm, void *va, uint32_t desc) \
{                                                               \
    intptr_t elements = simd_oprsz(desc) / sizeof(TYPEW);       \
    intptr_t sel = extract32(desc, SIMD_DATA_SHIFT, 2);         \
    TYPEW *d = vd, *a = va; TYPEN *n = vn; TYPEM *m = vm;       \
    for (intptr_t i = 0; i < elements; ++i) {                   \
        TYPEW nn = n[HN(i * 4 + sel)];                          \
        TYPEM mm = m[HN(i * 4 + sel)];                          \
        d[HW(i)] = a[HW(i)] OP (nn * mm);                       \
    }                                                           \
}

DO_MLALL(sme2_smlall_s, int32_t, int8_t, int8_t, H4, H1, +)
DO_MLALL(sme2_smlall_d, int64_t, int16_t, int16_t, H8, H2, +)
DO_MLALL(sme2_smlsll_s, int32_t, int8_t, int8_t, H4, H1, -)
DO_MLALL(sme2_smlsll_d, int64_t, int16_t, int16_t, H8, H2, -)

DO_MLALL(sme2_umlall_s, uint32_t, uint8_t, uint8_t, H4, H1, +)
DO_MLALL(sme2_umlall_d, uint64_t, uint16_t, uint16_t, H8, H2, +)
DO_MLALL(sme2_umlsll_s, uint32_t, uint8_t, uint8_t, H4, H1, -)
DO_MLALL(sme2_umlsll_d, uint64_t, uint16_t, uint16_t, H8, H2, -)

DO_MLALL(sme2_usmlall_s, uint32_t, uint8_t, int8_t, H4, H1, +)

#undef DO_MLALL

#define DO_MLALL_IDX(NAME, TYPEW, TYPEN, TYPEM, HW, HN, OP) \
void HELPER(NAME)(void *vd, void *vn, void *vm, void *va, uint32_t desc) \
{                                                               \
    intptr_t elements = simd_oprsz(desc) / sizeof(TYPEW);       \
    intptr_t eltspersegment = 16 / sizeof(TYPEW);               \
    intptr_t sel = extract32(desc, SIMD_DATA_SHIFT, 2);         \
    intptr_t idx = extract32(desc, SIMD_DATA_SHIFT + 2, 4);     \
    TYPEW *d = vd, *a = va; TYPEN *n = vn; TYPEM *m = vm;       \
    for (intptr_t i = 0; i < elements; i += eltspersegment) {   \
        TYPEW mm = m[HN(i * 4 + idx)];                          \
        for (intptr_t j = 0; j < eltspersegment; ++j) {         \
            TYPEN nn = n[HN((i + j) * 4 + sel)];                \
            d[HW(i + j)] = a[HW(i + j)] OP (nn * mm);           \
        }                                                       \
    }                                                           \
}

DO_MLALL_IDX(sme2_smlall_idx_s, int32_t, int8_t, int8_t, H4, H1, +)
DO_MLALL_IDX(sme2_smlall_idx_d, int64_t, int16_t, int16_t, H8, H2, +)
DO_MLALL_IDX(sme2_smlsll_idx_s, int32_t, int8_t, int8_t, H4, H1, -)
DO_MLALL_IDX(sme2_smlsll_idx_d, int64_t, int16_t, int16_t, H8, H2, -)

DO_MLALL_IDX(sme2_umlall_idx_s, uint32_t, uint8_t, uint8_t, H4, H1, +)
DO_MLALL_IDX(sme2_umlall_idx_d, uint64_t, uint16_t, uint16_t, H8, H2, +)
DO_MLALL_IDX(sme2_umlsll_idx_s, uint32_t, uint8_t, uint8_t, H4, H1, -)
DO_MLALL_IDX(sme2_umlsll_idx_d, uint64_t, uint16_t, uint16_t, H8, H2, -)

DO_MLALL_IDX(sme2_usmlall_idx_s, uint32_t, uint8_t, int8_t, H4, H1, +)
DO_MLALL_IDX(sme2_sumlall_idx_s, uint32_t, int8_t, uint8_t, H4, H1, +)

#undef DO_MLALL_IDX

/* Convert and compress */
void HELPER(sme2_bfcvt)(void *vd, void *vs, float_status *fpst, uint32_t desc)
{
    ARMVectorReg scratch;
    size_t oprsz = simd_oprsz(desc);
    size_t i, n = oprsz / 4;
    float32 *s0 = vs;
    float32 *s1 = vs + sizeof(ARMVectorReg);
    bfloat16 *d = vd;

    if (vd == s1) {
        s1 = memcpy(&scratch, s1, oprsz);
    }

    for (i = 0; i < n; ++i) {
        d[H2(i)] = float32_to_bfloat16(s0[H4(i)], fpst);
    }
    for (i = 0; i < n; ++i) {
        d[H2(i) + n] = float32_to_bfloat16(s1[H4(i)], fpst);
    }
}

void HELPER(sme2_fcvt_n)(void *vd, void *vs, float_status *fpst, uint32_t desc)
{
    ARMVectorReg scratch;
    size_t oprsz = simd_oprsz(desc);
    size_t i, n = oprsz / 4;
    float32 *s0 = vs;
    float32 *s1 = vs + sizeof(ARMVectorReg);
    float16 *d = vd;

    if (vd == s1) {
        s1 = memcpy(&scratch, s1, oprsz);
    }

    for (i = 0; i < n; ++i) {
        d[H2(i)] = sve_f32_to_f16(s0[H4(i)], fpst);
    }
    for (i = 0; i < n; ++i) {
        d[H2(i) + n] = sve_f32_to_f16(s1[H4(i)], fpst);
    }
}

#define SQCVT2(NAME, TW, TN, HW, HN, SAT)                       \
void HELPER(NAME)(void *vd, void *vs, uint32_t desc)            \
{                                                               \
    ARMVectorReg scratch;                                       \
    size_t oprsz = simd_oprsz(desc), n = oprsz / sizeof(TW);    \
    TW *s0 = vs, *s1 = vs + sizeof(ARMVectorReg);               \
    TN *d = vd;                                                 \
    if (vectors_overlap(vd, 1, vs, 2)) {                        \
        d = (TN *)&scratch;                                     \
    }                                                           \
    for (size_t i = 0; i < n; ++i) {                            \
        d[HN(i)] = SAT(s0[HW(i)]);                              \
        d[HN(i + n)] = SAT(s1[HW(i)]);                          \
    }                                                           \
    if (d != vd) {                                              \
        memcpy(vd, d, oprsz);                                   \
    }                                                           \
}

SQCVT2(sme2_sqcvt_sh, int32_t, int16_t, H4, H2, do_ssat_h)
SQCVT2(sme2_uqcvt_sh, uint32_t, uint16_t, H4, H2, do_usat_h)
SQCVT2(sme2_sqcvtu_sh, int32_t, uint16_t, H4, H2, do_usat_h)

#undef SQCVT2

#define SQCVT4(NAME, TW, TN, HW, HN, SAT)                       \
void HELPER(NAME)(void *vd, void *vs, uint32_t desc)            \
{                                                               \
    ARMVectorReg scratch;                                       \
    size_t oprsz = simd_oprsz(desc), n = oprsz / sizeof(TW);    \
    TW *s0 = vs, *s1 = vs + sizeof(ARMVectorReg);               \
    TW *s2 = vs + 2 * sizeof(ARMVectorReg);                     \
    TW *s3 = vs + 3 * sizeof(ARMVectorReg);                     \
    TN *d = vd;                                                 \
    if (vectors_overlap(vd, 1, vs, 4)) {                        \
        d = (TN *)&scratch;                                     \
    }                                                           \
    for (size_t i = 0; i < n; ++i) {                            \
        d[HN(i)] = SAT(s0[HW(i)]);                              \
        d[HN(i + n)] = SAT(s1[HW(i)]);                          \
        d[HN(i + 2 * n)] = SAT(s2[HW(i)]);                      \
        d[HN(i + 3 * n)] = SAT(s3[HW(i)]);                      \
    }                                                           \
    if (d != vd) {                                              \
        memcpy(vd, d, oprsz);                                   \
    }                                                           \
}

SQCVT4(sme2_sqcvt_sb, int32_t, int8_t, H4, H2, do_ssat_b)
SQCVT4(sme2_uqcvt_sb, uint32_t, uint8_t, H4, H2, do_usat_b)
SQCVT4(sme2_sqcvtu_sb, int32_t, uint8_t, H4, H2, do_usat_b)

SQCVT4(sme2_sqcvt_dh, int64_t, int16_t, H8, H2, do_ssat_h)
SQCVT4(sme2_uqcvt_dh, uint64_t, uint16_t, H8, H2, do_usat_h)
SQCVT4(sme2_sqcvtu_dh, int64_t, uint16_t, H8, H2, do_usat_h)

#undef SQCVT4

#define SQRSHR2(NAME, TW, TN, HW, HN, RSHR, SAT)                \
void HELPER(NAME)(void *vd, void *vs, uint32_t desc)            \
{                                                               \
    ARMVectorReg scratch;                                       \
    size_t oprsz = simd_oprsz(desc), n = oprsz / sizeof(TW);    \
    int shift = simd_data(desc);                                \
    TW *s0 = vs, *s1 = vs + sizeof(ARMVectorReg);               \
    TN *d = vd;                                                 \
    if (vectors_overlap(vd, 1, vs, 2)) {                        \
        d = (TN *)&scratch;                                     \
    }                                                           \
    for (size_t i = 0; i < n; ++i) {                            \
        d[HN(i)] = SAT(RSHR(s0[HW(i)], shift));                 \
        d[HN(i + n)] = SAT(RSHR(s1[HW(i)], shift));             \
    }                                                           \
    if (d != vd) {                                              \
        memcpy(vd, d, oprsz);                                   \
    }                                                           \
}

SQRSHR2(sme2_sqrshr_sh, int32_t, int16_t, H4, H2, do_srshr, do_ssat_h)
SQRSHR2(sme2_uqrshr_sh, uint32_t, uint16_t, H4, H2, do_urshr, do_usat_h)
SQRSHR2(sme2_sqrshru_sh, int32_t, uint16_t, H4, H2, do_srshr, do_usat_h)

#undef SQRSHR2

#define SQRSHR4(NAME, TW, TN, HW, HN, RSHR, SAT)                \
void HELPER(NAME)(void *vd, void *vs, uint32_t desc)            \
{                                                               \
    ARMVectorReg scratch;                                       \
    size_t oprsz = simd_oprsz(desc), n = oprsz / sizeof(TW);    \
    int shift = simd_data(desc);                                \
    TW *s0 = vs, *s1 = vs + sizeof(ARMVectorReg);               \
    TW *s2 = vs + 2 * sizeof(ARMVectorReg);                     \
    TW *s3 = vs + 3 * sizeof(ARMVectorReg);                     \
    TN *d = vd;                                                 \
    if (vectors_overlap(vd, 1, vs, 4)) {                        \
        d = (TN *)&scratch;                                     \
    }                                                           \
    for (size_t i = 0; i < n; ++i) {                            \
        d[HN(i)] = SAT(RSHR(s0[HW(i)], shift));                 \
        d[HN(i + n)] = SAT(RSHR(s1[HW(i)], shift));             \
        d[HN(i + 2 * n)] = SAT(RSHR(s2[HW(i)], shift));         \
        d[HN(i + 3 * n)] = SAT(RSHR(s3[HW(i)], shift));         \
    }                                                           \
    if (d != vd) {                                              \
        memcpy(vd, d, oprsz);                                   \
    }                                                           \
}

SQRSHR4(sme2_sqrshr_sb, int32_t, int8_t, H4, H2, do_srshr, do_ssat_b)
SQRSHR4(sme2_uqrshr_sb, uint32_t, uint8_t, H4, H2, do_urshr, do_usat_b)
SQRSHR4(sme2_sqrshru_sb, int32_t, uint8_t, H4, H2, do_srshr, do_usat_b)

SQRSHR4(sme2_sqrshr_dh, int64_t, int16_t, H8, H2, do_srshr, do_ssat_h)
SQRSHR4(sme2_uqrshr_dh, uint64_t, uint16_t, H8, H2, do_urshr, do_usat_h)
SQRSHR4(sme2_sqrshru_dh, int64_t, uint16_t, H8, H2, do_srshr, do_usat_h)

#undef SQRSHR4

/* Convert and interleave */
void HELPER(sme2_bfcvtn)(void *vd, void *vs, float_status *fpst, uint32_t desc)
{
    size_t i, n = simd_oprsz(desc) / 4;
    float32 *s0 = vs;
    float32 *s1 = vs + sizeof(ARMVectorReg);
    bfloat16 *d = vd;

    for (i = 0; i < n; ++i) {
        bfloat16 d0 = float32_to_bfloat16(s0[H4(i)], fpst);
        bfloat16 d1 = float32_to_bfloat16(s1[H4(i)], fpst);
        d[H2(i * 2 + 0)] = d0;
        d[H2(i * 2 + 1)] = d1;
    }
}

void HELPER(sme2_fcvtn)(void *vd, void *vs, float_status *fpst, uint32_t desc)
{
    size_t i, n = simd_oprsz(desc) / 4;
    float32 *s0 = vs;
    float32 *s1 = vs + sizeof(ARMVectorReg);
    bfloat16 *d = vd;

    for (i = 0; i < n; ++i) {
        bfloat16 d0 = sve_f32_to_f16(s0[H4(i)], fpst);
        bfloat16 d1 = sve_f32_to_f16(s1[H4(i)], fpst);
        d[H2(i * 2 + 0)] = d0;
        d[H2(i * 2 + 1)] = d1;
    }
}

#define SQCVTN2(NAME, TW, TN, HW, HN, SAT)                      \
void HELPER(NAME)(void *vd, void *vs, uint32_t desc)            \
{                                                               \
    ARMVectorReg scratch;                                       \
    size_t oprsz = simd_oprsz(desc), n = oprsz / sizeof(TW);    \
    TW *s0 = vs, *s1 = vs + sizeof(ARMVectorReg);               \
    TN *d = vd;                                                 \
    if (vectors_overlap(vd, 1, vs, 2)) {                        \
        d = (TN *)&scratch;                                     \
    }                                                           \
    for (size_t i = 0; i < n; ++i) {                            \
        d[HN(2 * i + 0)] = SAT(s0[HW(i)]);                      \
        d[HN(2 * i + 1)] = SAT(s1[HW(i)]);                      \
    }                                                           \
    if (d != vd) {                                              \
        memcpy(vd, d, oprsz);                                   \
    }                                                           \
}

SQCVTN2(sme2_sqcvtn_sh, int32_t, int16_t, H4, H2, do_ssat_h)
SQCVTN2(sme2_uqcvtn_sh, uint32_t, uint16_t, H4, H2, do_usat_h)
SQCVTN2(sme2_sqcvtun_sh, int32_t, uint16_t, H4, H2, do_usat_h)

#undef SQCVTN2

#define SQCVTN4(NAME, TW, TN, HW, HN, SAT)                      \
void HELPER(NAME)(void *vd, void *vs, uint32_t desc)            \
{                                                               \
    ARMVectorReg scratch;                                       \
    size_t oprsz = simd_oprsz(desc), n = oprsz / sizeof(TW);    \
    TW *s0 = vs, *s1 = vs + sizeof(ARMVectorReg);               \
    TW *s2 = vs + 2 * sizeof(ARMVectorReg);                     \
    TW *s3 = vs + 3 * sizeof(ARMVectorReg);                     \
    TN *d = vd;                                                 \
    if (vectors_overlap(vd, 1, vs, 4)) {                        \
        d = (TN *)&scratch;                                     \
    }                                                           \
    for (size_t i = 0; i < n; ++i) {                            \
        d[HN(4 * i + 0)] = SAT(s0[HW(i)]);                      \
        d[HN(4 * i + 1)] = SAT(s1[HW(i)]);                      \
        d[HN(4 * i + 2)] = SAT(s2[HW(i)]);                      \
        d[HN(4 * i + 3)] = SAT(s3[HW(i)]);                      \
    }                                                           \
    if (d != vd) {                                              \
        memcpy(vd, d, oprsz);                                   \
    }                                                           \
}

SQCVTN4(sme2_sqcvtn_sb, int32_t, int8_t, H4, H1, do_ssat_b)
SQCVTN4(sme2_uqcvtn_sb, uint32_t, uint8_t, H4, H1, do_usat_b)
SQCVTN4(sme2_sqcvtun_sb, int32_t, uint8_t, H4, H1, do_usat_b)

SQCVTN4(sme2_sqcvtn_dh, int64_t, int16_t, H8, H2, do_ssat_h)
SQCVTN4(sme2_uqcvtn_dh, uint64_t, uint16_t, H8, H2, do_usat_h)
SQCVTN4(sme2_sqcvtun_dh, int64_t, uint16_t, H8, H2, do_usat_h)

#undef SQCVTN4

#define SQRSHRN2(NAME, TW, TN, HW, HN, RSHR, SAT)               \
void HELPER(NAME)(void *vd, void *vs, uint32_t desc)            \
{                                                               \
    ARMVectorReg scratch;                                       \
    size_t oprsz = simd_oprsz(desc), n = oprsz / sizeof(TW);    \
    int shift = simd_data(desc);                                \
    TW *s0 = vs, *s1 = vs + sizeof(ARMVectorReg);               \
    TN *d = vd;                                                 \
    if (vectors_overlap(vd, 1, vs, 2)) {                        \
        d = (TN *)&scratch;                                     \
    }                                                           \
    for (size_t i = 0; i < n; ++i) {                            \
        d[HN(2 * i + 0)] = SAT(RSHR(s0[HW(i)], shift));         \
        d[HN(2 * i + 1)] = SAT(RSHR(s1[HW(i)], shift));         \
    }                                                           \
    if (d != vd) {                                              \
        memcpy(vd, d, oprsz);                                   \
    }                                                           \
}

SQRSHRN2(sme2_sqrshrn_sh, int32_t, int16_t, H4, H2, do_srshr, do_ssat_h)
SQRSHRN2(sme2_uqrshrn_sh, uint32_t, uint16_t, H4, H2, do_urshr, do_usat_h)
SQRSHRN2(sme2_sqrshrun_sh, int32_t, uint16_t, H4, H2, do_srshr, do_usat_h)

#undef SQRSHRN2

#define SQRSHRN4(NAME, TW, TN, HW, HN, RSHR, SAT)               \
void HELPER(NAME)(void *vd, void *vs, uint32_t desc)            \
{                                                               \
    ARMVectorReg scratch;                                       \
    size_t oprsz = simd_oprsz(desc), n = oprsz / sizeof(TW);    \
    int shift = simd_data(desc);                                \
    TW *s0 = vs, *s1 = vs + sizeof(ARMVectorReg);               \
    TW *s2 = vs + 2 * sizeof(ARMVectorReg);                     \
    TW *s3 = vs + 3 * sizeof(ARMVectorReg);                     \
    TN *d = vd;                                                 \
    if (vectors_overlap(vd, 1, vs, 4)) {                        \
        d = (TN *)&scratch;                                     \
    }                                                           \
    for (size_t i = 0; i < n; ++i) {                            \
        d[HN(4 * i + 0)] = SAT(RSHR(s0[HW(i)], shift));         \
        d[HN(4 * i + 1)] = SAT(RSHR(s1[HW(i)], shift));         \
        d[HN(4 * i + 2)] = SAT(RSHR(s2[HW(i)], shift));         \
        d[HN(4 * i + 3)] = SAT(RSHR(s3[HW(i)], shift));         \
    }                                                           \
    if (d != vd) {                                              \
        memcpy(vd, d, oprsz);                                   \
    }                                                           \
}

SQRSHRN4(sme2_sqrshrn_sb, int32_t, int8_t, H4, H1, do_srshr, do_ssat_b)
SQRSHRN4(sme2_uqrshrn_sb, uint32_t, uint8_t, H4, H1, do_urshr, do_usat_b)
SQRSHRN4(sme2_sqrshrun_sb, int32_t, uint8_t, H4, H1, do_srshr, do_usat_b)

SQRSHRN4(sme2_sqrshrn_dh, int64_t, int16_t, H8, H2, do_srshr, do_ssat_h)
SQRSHRN4(sme2_uqrshrn_dh, uint64_t, uint16_t, H8, H2, do_urshr, do_usat_h)
SQRSHRN4(sme2_sqrshrun_dh, int64_t, uint16_t, H8, H2, do_srshr, do_usat_h)

#undef SQRSHRN4

/* Expand and convert */
void HELPER(sme2_fcvt_w)(void *vd, void *vs, float_status *fpst, uint32_t desc)
{
    ARMVectorReg scratch;
    size_t oprsz = simd_oprsz(desc);
    size_t i, n = oprsz / 4;
    float16 *s = vs;
    float32 *d0 = vd;
    float32 *d1 = vd + sizeof(ARMVectorReg);

    if (vectors_overlap(vd, 1, vs, 2)) {
        s = memcpy(&scratch, s, oprsz);
    }

    for (i = 0; i < n; ++i) {
        d0[H4(i)] = sve_f16_to_f32(s[H2(i)], fpst);
    }
    for (i = 0; i < n; ++i) {
        d1[H4(i)] = sve_f16_to_f32(s[H2(n + i)], fpst);
    }
}

#define UNPK(NAME, SREG, TW, TN, HW, HN)                        \
void HELPER(NAME)(void *vd, void *vs, uint32_t desc)            \
{                                                               \
    ARMVectorReg scratch[SREG];                                 \
    size_t oprsz = simd_oprsz(desc);                            \
    size_t n = oprsz / sizeof(TW);                              \
    if (vectors_overlap(vd, 2 * SREG, vs, SREG)) {              \
        vs = memcpy(scratch, vs, sizeof(scratch));              \
    }                                                           \
    for (size_t r = 0; r < SREG; ++r) {                         \
        TN *s = vs + r * sizeof(ARMVectorReg);                  \
        for (size_t i = 0; i < 2; ++i) {                        \
            TW *d = vd + (2 * r + i) * sizeof(ARMVectorReg);    \
            for (size_t e = 0; e < n; ++e) {                    \
                d[HW(e)] = s[HN(i * n + e)];                    \
            }                                                   \
        }                                                       \
    }                                                           \
}

UNPK(sme2_sunpk2_bh, 1, int16_t, int8_t, H2, H1)
UNPK(sme2_sunpk2_hs, 1, int32_t, int16_t, H4, H2)
UNPK(sme2_sunpk2_sd, 1, int64_t, int32_t, H8, H4)

UNPK(sme2_sunpk4_bh, 2, int16_t, int8_t, H2, H1)
UNPK(sme2_sunpk4_hs, 2, int32_t, int16_t, H4, H2)
UNPK(sme2_sunpk4_sd, 2, int64_t, int32_t, H8, H4)

UNPK(sme2_uunpk2_bh, 1, uint16_t, uint8_t, H2, H1)
UNPK(sme2_uunpk2_hs, 1, uint32_t, uint16_t, H4, H2)
UNPK(sme2_uunpk2_sd, 1, uint64_t, uint32_t, H8, H4)

UNPK(sme2_uunpk4_bh, 2, uint16_t, uint8_t, H2, H1)
UNPK(sme2_uunpk4_hs, 2, uint32_t, uint16_t, H4, H2)
UNPK(sme2_uunpk4_sd, 2, uint64_t, uint32_t, H8, H4)

#undef UNPK

/* Deinterleave and convert. */
void HELPER(sme2_fcvtl)(void *vd, void *vs, float_status *fpst, uint32_t desc)
{
    size_t i, n = simd_oprsz(desc) / 4;
    float16 *s = vs;
    float32 *d0 = vd;
    float32 *d1 = vd + sizeof(ARMVectorReg);

    for (i = 0; i < n; ++i) {
        float32 v0 = sve_f16_to_f32(s[H2(i * 2 + 0)], fpst);
        float32 v1 = sve_f16_to_f32(s[H2(i * 2 + 1)], fpst);
        d0[H4(i)] = v0;
        d1[H4(i)] = v1;
    }
}

void HELPER(sme2_scvtf)(void *vd, void *vs, float_status *fpst, uint32_t desc)
{
    size_t i, n = simd_oprsz(desc) / 4;
    int32_t *d = vd;
    float32 *s = vs;

    for (i = 0; i < n; ++i) {
        d[i] = int32_to_float32(s[i], fpst);
    }
}

void HELPER(sme2_ucvtf)(void *vd, void *vs, float_status *fpst, uint32_t desc)
{
    size_t i, n = simd_oprsz(desc) / 4;
    uint32_t *d = vd;
    float32 *s = vs;

    for (i = 0; i < n; ++i) {
        d[i] = uint32_to_float32(s[i], fpst);
    }
}

#define ZIP2(NAME, TYPE, H)                                     \
void HELPER(NAME)(void *vd, void *vn, void *vm, uint32_t desc)  \
{                                                               \
    ARMVectorReg scratch[2];                                    \
    size_t oprsz = simd_oprsz(desc);                            \
    size_t pairs = oprsz / (sizeof(TYPE) * 2);                  \
    TYPE *n = vn, *m = vm;                                      \
    if (vectors_overlap(vd, 2, vn, 1)) {                        \
        n = memcpy(&scratch[0], vn, oprsz);                     \
    }                                                           \
    if (vectors_overlap(vd, 2, vm, 1)) {                        \
        m = memcpy(&scratch[1], vm, oprsz);                     \
    }                                                           \
    for (size_t r = 0; r < 2; ++r) {                            \
        TYPE *d = vd + r * sizeof(ARMVectorReg);                \
        size_t base = r * pairs;                                \
        for (size_t p = 0; p < pairs; ++p) {                    \
            d[H(2 * p + 0)] = n[base + H(p)];                   \
            d[H(2 * p + 1)] = m[base + H(p)];                   \
        }                                                       \
    }                                                           \
}

ZIP2(sme2_zip2_b, uint8_t, H1)
ZIP2(sme2_zip2_h, uint16_t, H2)
ZIP2(sme2_zip2_s, uint32_t, H4)
ZIP2(sme2_zip2_d, uint64_t, )
ZIP2(sme2_zip2_q, Int128, )

#undef ZIP2

#define ZIP4(NAME, TYPE, H)                                     \
void HELPER(NAME)(void *vd, void *vs, uint32_t desc)            \
{                                                               \
    ARMVectorReg scratch[4];                                    \
    size_t oprsz = simd_oprsz(desc);                            \
    size_t quads = oprsz / (sizeof(TYPE) * 4);                  \
    TYPE *s0, *s1, *s2, *s3;                                    \
    if (vs == vd) {                                             \
        vs = memcpy(scratch, vs, sizeof(scratch));              \
    }                                                           \
    s0 = vs;                                                    \
    s1 = vs + sizeof(ARMVectorReg);                             \
    s2 = vs + 2 * sizeof(ARMVectorReg);                         \
    s3 = vs + 3 * sizeof(ARMVectorReg);                         \
    for (size_t r = 0; r < 4; ++r) {                            \
        TYPE *d = vd + r * sizeof(ARMVectorReg);                \
        size_t base = r * quads;                                \
        for (size_t q = 0; q < quads; ++q) {                    \
            d[H(4 * q + 0)] = s0[base + H(q)];                  \
            d[H(4 * q + 1)] = s1[base + H(q)];                  \
            d[H(4 * q + 2)] = s2[base + H(q)];                  \
            d[H(4 * q + 3)] = s3[base + H(q)];                  \
        }                                                       \
    }                                                           \
}

ZIP4(sme2_zip4_b, uint8_t, H1)
ZIP4(sme2_zip4_h, uint16_t, H2)
ZIP4(sme2_zip4_s, uint32_t, H4)
ZIP4(sme2_zip4_d, uint64_t, )
ZIP4(sme2_zip4_q, Int128, )

#undef ZIP4

#define UZP2(NAME, TYPE, H)                                     \
void HELPER(NAME)(void *vd, void *vn, void *vm, uint32_t desc)  \
{                                                               \
    ARMVectorReg scratch[2];                                    \
    size_t oprsz = simd_oprsz(desc);                            \
    size_t pairs = oprsz / (sizeof(TYPE) * 2);                  \
    TYPE *d0 = vd, *d1 = vd + sizeof(ARMVectorReg);             \
    if (vectors_overlap(vd, 2, vn, 1)) {                        \
        vn = memcpy(&scratch[0], vn, oprsz);                    \
    }                                                           \
    if (vectors_overlap(vd, 2, vm, 1)) {                        \
        vm = memcpy(&scratch[1], vm, oprsz);                    \
    }                                                           \
    for (size_t r = 0; r < 2; ++r) {                            \
        TYPE *s = r ? vm : vn;                                  \
        size_t base = r * pairs;                                \
        for (size_t p = 0; p < pairs; ++p) {                    \
            d0[base + H(p)] = s[H(2 * p + 0)];                  \
            d1[base + H(p)] = s[H(2 * p + 1)];                  \
        }                                                       \
    }                                                           \
}

UZP2(sme2_uzp2_b, uint8_t, H1)
UZP2(sme2_uzp2_h, uint16_t, H2)
UZP2(sme2_uzp2_s, uint32_t, H4)
UZP2(sme2_uzp2_d, uint64_t, )
UZP2(sme2_uzp2_q, Int128, )

#undef UZP2

#define UZP4(NAME, TYPE, H)                                     \
void HELPER(NAME)(void *vd, void *vs, uint32_t desc)            \
{                                                               \
    ARMVectorReg scratch[4];                                    \
    size_t oprsz = simd_oprsz(desc);                            \
    size_t quads = oprsz / (sizeof(TYPE) * 4);                  \
    TYPE *d0, *d1, *d2, *d3;                                    \
    if (vs == vd) {                                             \
        vs = memcpy(scratch, vs, sizeof(scratch));              \
    }                                                           \
    d0 = vd;                                                    \
    d1 = vd + sizeof(ARMVectorReg);                             \
    d2 = vd + 2 * sizeof(ARMVectorReg);                         \
    d3 = vd + 3 * sizeof(ARMVectorReg);                         \
    for (size_t r = 0; r < 4; ++r) {                            \
        TYPE *s = vs + r * sizeof(ARMVectorReg);                \
        size_t base = r * quads;                                \
        for (size_t q = 0; q < quads; ++q) {                    \
            d0[base + H(q)] = s[H(4 * q + 0)];                  \
            d1[base + H(q)] = s[H(4 * q + 1)];                  \
            d2[base + H(q)] = s[H(4 * q + 2)];                  \
            d3[base + H(q)] = s[H(4 * q + 3)];                  \
        }                                                       \
    }                                                           \
}

UZP4(sme2_uzp4_b, uint8_t, H1)
UZP4(sme2_uzp4_h, uint16_t, H2)
UZP4(sme2_uzp4_s, uint32_t, H4)
UZP4(sme2_uzp4_d, uint64_t, )
UZP4(sme2_uzp4_q, Int128, )

#undef UZP4

#define ICLAMP(NAME, TYPE, H) \
void HELPER(NAME)(void *vd, void *vn, void *vm, uint32_t desc)  \
{                                                               \
    size_t stride = sizeof(ARMVectorReg) / sizeof(TYPE);        \
    size_t elements = simd_oprsz(desc) / sizeof(TYPE);          \
    size_t nreg = simd_data(desc);                              \
    TYPE *d = vd, *n = vn, *m = vm;                             \
    for (size_t e = 0; e < elements; e++) {                     \
        TYPE nn = n[H(e)], mm = m[H(e)];                        \
        for (size_t r = 0; r < nreg; r++) {                     \
            TYPE *dd = &d[r * stride + H(e)];                   \
            *dd = MIN(MAX(*dd, nn), mm);                        \
        }                                                       \
    }                                                           \
}

ICLAMP(sme2_sclamp_b, int8_t, H1)
ICLAMP(sme2_sclamp_h, int16_t, H2)
ICLAMP(sme2_sclamp_s, int32_t, H4)
ICLAMP(sme2_sclamp_d, int64_t, H8)

ICLAMP(sme2_uclamp_b, uint8_t, H1)
ICLAMP(sme2_uclamp_h, uint16_t, H2)
ICLAMP(sme2_uclamp_s, uint32_t, H4)
ICLAMP(sme2_uclamp_d, uint64_t, H8)

#undef ICLAMP

/*
 * Note the argument ordering to minnum and maxnum must match
 * the ARM pseudocode so that NaNs are propagated properly.
 */
#define FCLAMP(NAME, TYPE, H) \
void HELPER(NAME)(void *vd, void *vn, void *vm,                 \
                  float_status *fpst, uint32_t desc)            \
{                                                               \
    size_t stride = sizeof(ARMVectorReg) / sizeof(TYPE);        \
    size_t elements = simd_oprsz(desc) / sizeof(TYPE);          \
    size_t nreg = simd_data(desc);                              \
    TYPE *d = vd, *n = vn, *m = vm;                             \
    for (size_t e = 0; e < elements; e++) {                     \
        TYPE nn = n[H(e)], mm = m[H(e)];                        \
        for (size_t r = 0; r < nreg; r++) {                     \
            TYPE *dd = &d[r * stride + H(e)];                   \
            *dd = TYPE##_minnum(TYPE##_maxnum(nn, *dd, fpst), mm, fpst); \
        }                                                       \
    }                                                           \
}

FCLAMP(sme2_fclamp_h, float16, H2)
FCLAMP(sme2_fclamp_s, float32, H4)
FCLAMP(sme2_fclamp_d, float64, H8)
FCLAMP(sme2_bfclamp, bfloat16, H2)

#undef FCLAMP

void HELPER(sme2_sel_b)(void *vd, void *vn, void *vm,
                        uint32_t png, uint32_t desc)
{
    int vl = simd_oprsz(desc);
    int nreg = simd_data(desc);
    int elements = vl / sizeof(uint8_t);
    DecodeCounter p = decode_counter(png, vl, MO_8);

    if (p.lg2_stride == 0) {
        if (p.invert) {
            for (int r = 0; r < nreg; r++) {
                uint8_t *d = vd + r * sizeof(ARMVectorReg);
                uint8_t *n = vn + r * sizeof(ARMVectorReg);
                uint8_t *m = vm + r * sizeof(ARMVectorReg);
                int split = p.count - r * elements;

                if (split <= 0) {
                    memcpy(d, n, vl);  /* all true */
                } else if (elements <= split) {
                    memcpy(d, m, vl);  /* all false */
                } else {
                    for (int e = 0; e < split; e++) {
                        d[H1(e)] = m[H1(e)];
                    }
                    for (int e = split; e < elements; e++) {
                        d[H1(e)] = n[H1(e)];
                    }
                }
            }
        } else {
            for (int r = 0; r < nreg; r++) {
                uint8_t *d = vd + r * sizeof(ARMVectorReg);
                uint8_t *n = vn + r * sizeof(ARMVectorReg);
                uint8_t *m = vm + r * sizeof(ARMVectorReg);
                int split = p.count - r * elements;

                if (split <= 0) {
                    memcpy(d, m, vl);  /* all false */
                } else if (elements <= split) {
                    memcpy(d, n, vl);  /* all true */
                } else {
                    for (int e = 0; e < split; e++) {
                        d[H1(e)] = n[H1(e)];
                    }
                    for (int e = split; e < elements; e++) {
                        d[H1(e)] = m[H1(e)];
                    }
                }
            }
        }
    } else {
        int estride = 1 << p.lg2_stride;
        if (p.invert) {
            for (int r = 0; r < nreg; r++) {
                uint8_t *d = vd + r * sizeof(ARMVectorReg);
                uint8_t *n = vn + r * sizeof(ARMVectorReg);
                uint8_t *m = vm + r * sizeof(ARMVectorReg);
                int split = p.count - r * elements;
                int e = 0;

                for (; e < MIN(split, elements); e++) {
                    d[H1(e)] = m[H1(e)];
                }
                for (; e < elements; e += estride) {
                    d[H1(e)] = n[H1(e)];
                    for (int i = 1; i < estride; i++) {
                        d[H1(e + i)] = m[H1(e + i)];
                    }
                }
            }
        } else {
            for (int r = 0; r < nreg; r++) {
                uint8_t *d = vd + r * sizeof(ARMVectorReg);
                uint8_t *n = vn + r * sizeof(ARMVectorReg);
                uint8_t *m = vm + r * sizeof(ARMVectorReg);
                int split = p.count - r * elements;
                int e = 0;

                for (; e < MIN(split, elements); e += estride) {
                    d[H1(e)] = n[H1(e)];
                    for (int i = 1; i < estride; i++) {
                        d[H1(e + i)] = m[H1(e + i)];
                    }
                }
                for (; e < elements; e++) {
                    d[H1(e)] = m[H1(e)];
                }
            }
        }
    }
}

void HELPER(sme2_sel_h)(void *vd, void *vn, void *vm,
                        uint32_t png, uint32_t desc)
{
    int vl = simd_oprsz(desc);
    int nreg = simd_data(desc);
    int elements = vl / sizeof(uint16_t);
    DecodeCounter p = decode_counter(png, vl, MO_16);

    if (p.lg2_stride == 0) {
        if (p.invert) {
            for (int r = 0; r < nreg; r++) {
                uint16_t *d = vd + r * sizeof(ARMVectorReg);
                uint16_t *n = vn + r * sizeof(ARMVectorReg);
                uint16_t *m = vm + r * sizeof(ARMVectorReg);
                int split = p.count - r * elements;

                if (split <= 0) {
                    memcpy(d, n, vl);  /* all true */
                } else if (elements <= split) {
                    memcpy(d, m, vl);  /* all false */
                } else {
                    for (int e = 0; e < split; e++) {
                        d[H2(e)] = m[H2(e)];
                    }
                    for (int e = split; e < elements; e++) {
                        d[H2(e)] = n[H2(e)];
                    }
                }
            }
        } else {
            for (int r = 0; r < nreg; r++) {
                uint16_t *d = vd + r * sizeof(ARMVectorReg);
                uint16_t *n = vn + r * sizeof(ARMVectorReg);
                uint16_t *m = vm + r * sizeof(ARMVectorReg);
                int split = p.count - r * elements;

                if (split <= 0) {
                    memcpy(d, m, vl);  /* all false */
                } else if (elements <= split) {
                    memcpy(d, n, vl);  /* all true */
                } else {
                    for (int e = 0; e < split; e++) {
                        d[H2(e)] = n[H2(e)];
                    }
                    for (int e = split; e < elements; e++) {
                        d[H2(e)] = m[H2(e)];
                    }
                }
            }
        }
    } else {
        int estride = 1 << p.lg2_stride;
        if (p.invert) {
            for (int r = 0; r < nreg; r++) {
                uint16_t *d = vd + r * sizeof(ARMVectorReg);
                uint16_t *n = vn + r * sizeof(ARMVectorReg);
                uint16_t *m = vm + r * sizeof(ARMVectorReg);
                int split = p.count - r * elements;
                int e = 0;

                for (; e < MIN(split, elements); e++) {
                    d[H2(e)] = m[H2(e)];
                }
                for (; e < elements; e += estride) {
                    d[H2(e)] = n[H2(e)];
                    for (int i = 1; i < estride; i++) {
                        d[H2(e + i)] = m[H2(e + i)];
                    }
                }
            }
        } else {
            for (int r = 0; r < nreg; r++) {
                uint16_t *d = vd + r * sizeof(ARMVectorReg);
                uint16_t *n = vn + r * sizeof(ARMVectorReg);
                uint16_t *m = vm + r * sizeof(ARMVectorReg);
                int split = p.count - r * elements;
                int e = 0;

                for (; e < MIN(split, elements); e += estride) {
                    d[H2(e)] = n[H2(e)];
                    for (int i = 1; i < estride; i++) {
                        d[H2(e + i)] = m[H2(e + i)];
                    }
                }
                for (; e < elements; e++) {
                    d[H2(e)] = m[H2(e)];
                }
            }
        }
    }
}

void HELPER(sme2_sel_s)(void *vd, void *vn, void *vm,
                        uint32_t png, uint32_t desc)
{
    int vl = simd_oprsz(desc);
    int nreg = simd_data(desc);
    int elements = vl / sizeof(uint32_t);
    DecodeCounter p = decode_counter(png, vl, MO_32);

    if (p.lg2_stride == 0) {
        if (p.invert) {
            for (int r = 0; r < nreg; r++) {
                uint32_t *d = vd + r * sizeof(ARMVectorReg);
                uint32_t *n = vn + r * sizeof(ARMVectorReg);
                uint32_t *m = vm + r * sizeof(ARMVectorReg);
                int split = p.count - r * elements;

                if (split <= 0) {
                    memcpy(d, n, vl);  /* all true */
                } else if (elements <= split) {
                    memcpy(d, m, vl);  /* all false */
                } else {
                    for (int e = 0; e < split; e++) {
                        d[H4(e)] = m[H4(e)];
                    }
                    for (int e = split; e < elements; e++) {
                        d[H4(e)] = n[H4(e)];
                    }
                }
            }
        } else {
            for (int r = 0; r < nreg; r++) {
                uint32_t *d = vd + r * sizeof(ARMVectorReg);
                uint32_t *n = vn + r * sizeof(ARMVectorReg);
                uint32_t *m = vm + r * sizeof(ARMVectorReg);
                int split = p.count - r * elements;

                if (split <= 0) {
                    memcpy(d, m, vl);  /* all false */
                } else if (elements <= split) {
                    memcpy(d, n, vl);  /* all true */
                } else {
                    for (int e = 0; e < split; e++) {
                        d[H4(e)] = n[H4(e)];
                    }
                    for (int e = split; e < elements; e++) {
                        d[H4(e)] = m[H4(e)];
                    }
                }
            }
        }
    } else {
        /* p.esz must be MO_64, so stride must be 2. */
        if (p.invert) {
            for (int r = 0; r < nreg; r++) {
                uint32_t *d = vd + r * sizeof(ARMVectorReg);
                uint32_t *n = vn + r * sizeof(ARMVectorReg);
                uint32_t *m = vm + r * sizeof(ARMVectorReg);
                int split = p.count - r * elements;
                int e = 0;

                for (; e < MIN(split, elements); e++) {
                    d[H4(e)] = m[H4(e)];
                }
                for (; e < elements; e += 2) {
                    d[H4(e)] = n[H4(e)];
                    d[H4(e + 1)] = m[H4(e + 1)];
                }
            }
        } else {
            for (int r = 0; r < nreg; r++) {
                uint32_t *d = vd + r * sizeof(ARMVectorReg);
                uint32_t *n = vn + r * sizeof(ARMVectorReg);
                uint32_t *m = vm + r * sizeof(ARMVectorReg);
                int split = p.count - r * elements;
                int e = 0;

                for (; e < MIN(split, elements); e += 2) {
                    d[H4(e)] = n[H4(e)];
                    d[H4(e + 1)] = m[H4(e + 1)];
                }
                for (; e < elements; e++) {
                    d[H4(e)] = m[H4(e)];
                }
            }
        }
    }
}

void HELPER(sme2_sel_d)(void *vd, void *vn, void *vm,
                        uint32_t png, uint32_t desc)
{
    int vl = simd_oprsz(desc);
    int nreg = simd_data(desc);
    int elements = vl / sizeof(uint64_t);
    DecodeCounter p = decode_counter(png, vl, MO_64);

    if (p.invert) {
        for (int r = 0; r < nreg; r++) {
            uint64_t *d = vd + r * sizeof(ARMVectorReg);
            uint64_t *n = vn + r * sizeof(ARMVectorReg);
            uint64_t *m = vm + r * sizeof(ARMVectorReg);
            int split = p.count - r * elements;

            if (split <= 0) {
                memcpy(d, n, vl);  /* all true */
            } else if (elements <= split) {
                memcpy(d, m, vl);  /* all false */
            } else {
                memcpy(d, m, split * sizeof(uint64_t));
                memcpy(d + split, n + split,
                       (elements - split) * sizeof(uint64_t));
            }
        }
    } else {
        for (int r = 0; r < nreg; r++) {
            uint64_t *d = vd + r * sizeof(ARMVectorReg);
            uint64_t *n = vn + r * sizeof(ARMVectorReg);
            uint64_t *m = vm + r * sizeof(ARMVectorReg);
            int split = p.count - r * elements;

            if (split <= 0) {
                memcpy(d, m, vl);  /* all false */
            } else if (elements <= split) {
                memcpy(d, n, vl);  /* all true */
            } else {
                memcpy(d, n, split * sizeof(uint64_t));
                memcpy(d + split, m + split,
                       (elements - split) * sizeof(uint64_t));
            }
        }
    }
}
