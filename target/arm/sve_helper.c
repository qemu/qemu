/*
 * ARM SVE Operations
 *
 * Copyright (c) 2018 Linaro, Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "exec/helper-proto.h"
#include "tcg/tcg-gvec-desc.h"


/* Return a value for NZCV as per the ARM PredTest pseudofunction.
 *
 * The return value has bit 31 set if N is set, bit 1 set if Z is clear,
 * and bit 0 set if C is set.  Compare the definitions of these variables
 * within CPUARMState.
 */

/* For no G bits set, NZCV = C.  */
#define PREDTEST_INIT  1

/* This is an iterative function, called for each Pd and Pg word
 * moving forward.
 */
static uint32_t iter_predtest_fwd(uint64_t d, uint64_t g, uint32_t flags)
{
    if (likely(g)) {
        /* Compute N from first D & G.
           Use bit 2 to signal first G bit seen.  */
        if (!(flags & 4)) {
            flags |= ((d & (g & -g)) != 0) << 31;
            flags |= 4;
        }

        /* Accumulate Z from each D & G.  */
        flags |= ((d & g) != 0) << 1;

        /* Compute C from last !(D & G).  Replace previous.  */
        flags = deposit32(flags, 0, 1, (d & pow2floor(g)) == 0);
    }
    return flags;
}

/* The same for a single word predicate.  */
uint32_t HELPER(sve_predtest1)(uint64_t d, uint64_t g)
{
    return iter_predtest_fwd(d, g, PREDTEST_INIT);
}

/* The same for a multi-word predicate.  */
uint32_t HELPER(sve_predtest)(void *vd, void *vg, uint32_t words)
{
    uint32_t flags = PREDTEST_INIT;
    uint64_t *d = vd, *g = vg;
    uintptr_t i = 0;

    do {
        flags = iter_predtest_fwd(d[i], g[i], flags);
    } while (++i < words);

    return flags;
}

#define LOGICAL_PPPP(NAME, FUNC) \
void HELPER(NAME)(void *vd, void *vn, void *vm, void *vg, uint32_t desc)  \
{                                                                         \
    uintptr_t opr_sz = simd_oprsz(desc);                                  \
    uint64_t *d = vd, *n = vn, *m = vm, *g = vg;                          \
    uintptr_t i;                                                          \
    for (i = 0; i < opr_sz / 8; ++i) {                                    \
        d[i] = FUNC(n[i], m[i], g[i]);                                    \
    }                                                                     \
}

#define DO_AND(N, M, G)  (((N) & (M)) & (G))
#define DO_BIC(N, M, G)  (((N) & ~(M)) & (G))
#define DO_EOR(N, M, G)  (((N) ^ (M)) & (G))
#define DO_ORR(N, M, G)  (((N) | (M)) & (G))
#define DO_ORN(N, M, G)  (((N) | ~(M)) & (G))
#define DO_NOR(N, M, G)  (~((N) | (M)) & (G))
#define DO_NAND(N, M, G) (~((N) & (M)) & (G))
#define DO_SEL(N, M, G)  (((N) & (G)) | ((M) & ~(G)))

LOGICAL_PPPP(sve_and_pppp, DO_AND)
LOGICAL_PPPP(sve_bic_pppp, DO_BIC)
LOGICAL_PPPP(sve_eor_pppp, DO_EOR)
LOGICAL_PPPP(sve_sel_pppp, DO_SEL)
LOGICAL_PPPP(sve_orr_pppp, DO_ORR)
LOGICAL_PPPP(sve_orn_pppp, DO_ORN)
LOGICAL_PPPP(sve_nor_pppp, DO_NOR)
LOGICAL_PPPP(sve_nand_pppp, DO_NAND)

#undef DO_AND
#undef DO_BIC
#undef DO_EOR
#undef DO_ORR
#undef DO_ORN
#undef DO_NOR
#undef DO_NAND
#undef DO_SEL
#undef LOGICAL_PPPP

/* Similar to the ARM LastActiveElement pseudocode function, except the
   result is multiplied by the element size.  This includes the not found
   indication; e.g. not found for esz=3 is -8.  */
static intptr_t last_active_element(uint64_t *g, intptr_t words, intptr_t esz)
{
    uint64_t mask = pred_esz_masks[esz];
    intptr_t i = words;

    do {
        uint64_t this_g = g[--i] & mask;
        if (this_g) {
            return i * 64 + (63 - clz64(this_g));
        }
    } while (i > 0);
    return (intptr_t)-1 << esz;
}

uint32_t HELPER(sve_pfirst)(void *vd, void *vg, uint32_t words)
{
    uint32_t flags = PREDTEST_INIT;
    uint64_t *d = vd, *g = vg;
    intptr_t i = 0;

    do {
        uint64_t this_d = d[i];
        uint64_t this_g = g[i];

        if (this_g) {
            if (!(flags & 4)) {
                /* Set in D the first bit of G.  */
                this_d |= this_g & -this_g;
                d[i] = this_d;
            }
            flags = iter_predtest_fwd(this_d, this_g, flags);
        }
    } while (++i < words);

    return flags;
}

uint32_t HELPER(sve_pnext)(void *vd, void *vg, uint32_t pred_desc)
{
    intptr_t words = extract32(pred_desc, 0, SIMD_OPRSZ_BITS);
    intptr_t esz = extract32(pred_desc, SIMD_DATA_SHIFT, 2);
    uint32_t flags = PREDTEST_INIT;
    uint64_t *d = vd, *g = vg, esz_mask;
    intptr_t i, next;

    next = last_active_element(vd, words, esz) + (1 << esz);
    esz_mask = pred_esz_masks[esz];

    /* Similar to the pseudocode for pnext, but scaled by ESZ
       so that we find the correct bit.  */
    if (next < words * 64) {
        uint64_t mask = -1;

        if (next & 63) {
            mask = ~((1ull << (next & 63)) - 1);
            next &= -64;
        }
        do {
            uint64_t this_g = g[next / 64] & esz_mask & mask;
            if (this_g != 0) {
                next = (next & -64) + ctz64(this_g);
                break;
            }
            next += 64;
            mask = -1;
        } while (next < words * 64);
    }

    i = 0;
    do {
        uint64_t this_d = 0;
        if (i == next / 64) {
            this_d = 1ull << (next & 63);
        }
        d[i] = this_d;
        flags = iter_predtest_fwd(this_d, g[i] & esz_mask, flags);
    } while (++i < words);

    return flags;
}
