/*
 * ARM SVE Load/Store Helpers
 *
 * Copyright (c) 2018-2022 Linaro
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

#ifndef TARGET_ARM_SVE_LDST_INTERNAL_H
#define TARGET_ARM_SVE_LDST_INTERNAL_H

#include "exec/cpu_ldst.h"

/*
 * Load one element into @vd + @reg_off from @host.
 * The controlling predicate is known to be true.
 */
typedef void sve_ldst1_host_fn(void *vd, intptr_t reg_off, void *host);

/*
 * Load one element into @vd + @reg_off from (@env, @vaddr, @ra).
 * The controlling predicate is known to be true.
 */
typedef void sve_ldst1_tlb_fn(CPUARMState *env, void *vd, intptr_t reg_off,
                              target_ulong vaddr, uintptr_t retaddr);

/*
 * Generate the above primitives.
 */

#define DO_LD_HOST(NAME, H, TYPEE, TYPEM, HOST)                              \
static inline void sve_##NAME##_host(void *vd, intptr_t reg_off, void *host) \
{ TYPEM val = HOST(host); *(TYPEE *)(vd + H(reg_off)) = val; }

#define DO_ST_HOST(NAME, H, TYPEE, TYPEM, HOST)                              \
static inline void sve_##NAME##_host(void *vd, intptr_t reg_off, void *host) \
{ TYPEM val = *(TYPEE *)(vd + H(reg_off)); HOST(host, val); }

#define DO_LD_TLB(NAME, H, TYPEE, TYPEM, TLB)                              \
static inline void sve_##NAME##_tlb(CPUARMState *env, void *vd,            \
                        intptr_t reg_off, target_ulong addr, uintptr_t ra) \
{                                                                          \
    TYPEM val = TLB(env, useronly_clean_ptr(addr), ra);                    \
    *(TYPEE *)(vd + H(reg_off)) = val;                                     \
}

#define DO_ST_TLB(NAME, H, TYPEE, TYPEM, TLB)                              \
static inline void sve_##NAME##_tlb(CPUARMState *env, void *vd,            \
                        intptr_t reg_off, target_ulong addr, uintptr_t ra) \
{                                                                          \
    TYPEM val = *(TYPEE *)(vd + H(reg_off));                               \
    TLB(env, useronly_clean_ptr(addr), val, ra);                           \
}

#define DO_LD_PRIM_1(NAME, H, TE, TM)                   \
    DO_LD_HOST(NAME, H, TE, TM, ldub_p)                 \
    DO_LD_TLB(NAME, H, TE, TM, cpu_ldub_data_ra)

DO_LD_PRIM_1(ld1bb,  H1,   uint8_t,  uint8_t)
DO_LD_PRIM_1(ld1bhu, H1_2, uint16_t, uint8_t)
DO_LD_PRIM_1(ld1bhs, H1_2, uint16_t,  int8_t)
DO_LD_PRIM_1(ld1bsu, H1_4, uint32_t, uint8_t)
DO_LD_PRIM_1(ld1bss, H1_4, uint32_t,  int8_t)
DO_LD_PRIM_1(ld1bdu, H1_8, uint64_t, uint8_t)
DO_LD_PRIM_1(ld1bds, H1_8, uint64_t,  int8_t)

#define DO_ST_PRIM_1(NAME, H, TE, TM)                   \
    DO_ST_HOST(st1##NAME, H, TE, TM, stb_p)             \
    DO_ST_TLB(st1##NAME, H, TE, TM, cpu_stb_data_ra)

DO_ST_PRIM_1(bb,   H1,  uint8_t, uint8_t)
DO_ST_PRIM_1(bh, H1_2, uint16_t, uint8_t)
DO_ST_PRIM_1(bs, H1_4, uint32_t, uint8_t)
DO_ST_PRIM_1(bd, H1_8, uint64_t, uint8_t)

#define DO_LD_PRIM_2(NAME, H, TE, TM, LD) \
    DO_LD_HOST(ld1##NAME##_be, H, TE, TM, LD##_be_p)    \
    DO_LD_HOST(ld1##NAME##_le, H, TE, TM, LD##_le_p)    \
    DO_LD_TLB(ld1##NAME##_be, H, TE, TM, cpu_##LD##_be_data_ra) \
    DO_LD_TLB(ld1##NAME##_le, H, TE, TM, cpu_##LD##_le_data_ra)

#define DO_ST_PRIM_2(NAME, H, TE, TM, ST) \
    DO_ST_HOST(st1##NAME##_be, H, TE, TM, ST##_be_p)    \
    DO_ST_HOST(st1##NAME##_le, H, TE, TM, ST##_le_p)    \
    DO_ST_TLB(st1##NAME##_be, H, TE, TM, cpu_##ST##_be_data_ra) \
    DO_ST_TLB(st1##NAME##_le, H, TE, TM, cpu_##ST##_le_data_ra)

DO_LD_PRIM_2(hh,  H1_2, uint16_t, uint16_t, lduw)
DO_LD_PRIM_2(hsu, H1_4, uint32_t, uint16_t, lduw)
DO_LD_PRIM_2(hss, H1_4, uint32_t,  int16_t, lduw)
DO_LD_PRIM_2(hdu, H1_8, uint64_t, uint16_t, lduw)
DO_LD_PRIM_2(hds, H1_8, uint64_t,  int16_t, lduw)

DO_ST_PRIM_2(hh, H1_2, uint16_t, uint16_t, stw)
DO_ST_PRIM_2(hs, H1_4, uint32_t, uint16_t, stw)
DO_ST_PRIM_2(hd, H1_8, uint64_t, uint16_t, stw)

DO_LD_PRIM_2(ss,  H1_4, uint32_t, uint32_t, ldl)
DO_LD_PRIM_2(sdu, H1_8, uint64_t, uint32_t, ldl)
DO_LD_PRIM_2(sds, H1_8, uint64_t,  int32_t, ldl)

DO_ST_PRIM_2(ss, H1_4, uint32_t, uint32_t, stl)
DO_ST_PRIM_2(sd, H1_8, uint64_t, uint32_t, stl)

DO_LD_PRIM_2(dd, H1_8, uint64_t, uint64_t, ldq)
DO_ST_PRIM_2(dd, H1_8, uint64_t, uint64_t, stq)

#undef DO_LD_TLB
#undef DO_ST_TLB
#undef DO_LD_HOST
#undef DO_LD_PRIM_1
#undef DO_ST_PRIM_1
#undef DO_LD_PRIM_2
#undef DO_ST_PRIM_2

#endif /* TARGET_ARM_SVE_LDST_INTERNAL_H */
