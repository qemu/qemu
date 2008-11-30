/*
 *  PowerPC emulation micro-operations helpers for qemu.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "op_mem_access.h"

void glue(do_lsw, MEMSUFFIX) (int dst)
{
    uint32_t tmp;
    int sh;

    for (; T1 > 3; T1 -= 4, T0 += 4) {
        env->gpr[dst++] = glue(ldu32, MEMSUFFIX)((uint32_t)T0);
        if (unlikely(dst == 32))
            dst = 0;
    }
    if (unlikely(T1 != 0)) {
        tmp = 0;
        for (sh = 24; T1 > 0; T1--, T0++, sh -= 8) {
            tmp |= glue(ldu8, MEMSUFFIX)((uint32_t)T0) << sh;
        }
        env->gpr[dst] = tmp;
    }
}

#if defined(TARGET_PPC64)
void glue(do_lsw_64, MEMSUFFIX) (int dst)
{
    uint32_t tmp;
    int sh;

    for (; T1 > 3; T1 -= 4, T0 += 4) {
        env->gpr[dst++] = glue(ldu32, MEMSUFFIX)((uint64_t)T0);
        if (unlikely(dst == 32))
            dst = 0;
    }
    if (unlikely(T1 != 0)) {
        tmp = 0;
        for (sh = 24; T1 > 0; T1--, T0++, sh -= 8) {
            tmp |= glue(ldu8, MEMSUFFIX)((uint64_t)T0) << sh;
        }
        env->gpr[dst] = tmp;
    }
}
#endif

void glue(do_stsw, MEMSUFFIX) (int src)
{
    int sh;

    for (; T1 > 3; T1 -= 4, T0 += 4) {
        glue(st32, MEMSUFFIX)((uint32_t)T0, env->gpr[src++]);
        if (unlikely(src == 32))
            src = 0;
    }
    if (unlikely(T1 != 0)) {
        for (sh = 24; T1 > 0; T1--, T0++, sh -= 8)
            glue(st8, MEMSUFFIX)((uint32_t)T0, (env->gpr[src] >> sh) & 0xFF);
    }
}

#if defined(TARGET_PPC64)
void glue(do_stsw_64, MEMSUFFIX) (int src)
{
    int sh;

    for (; T1 > 3; T1 -= 4, T0 += 4) {
        glue(st32, MEMSUFFIX)((uint64_t)T0, env->gpr[src++]);
        if (unlikely(src == 32))
            src = 0;
    }
    if (unlikely(T1 != 0)) {
        for (sh = 24; T1 > 0; T1--, T0++, sh -= 8)
            glue(st8, MEMSUFFIX)((uint64_t)T0, (env->gpr[src] >> sh) & 0xFF);
    }
}
#endif

/* Instruction cache invalidation helper */
void glue(do_icbi, MEMSUFFIX) (void)
{
    uint32_t tmp;
    /* Invalidate one cache line :
     * PowerPC specification says this is to be treated like a load
     * (not a fetch) by the MMU. To be sure it will be so,
     * do the load "by hand".
     */
    T0 &= ~(env->icache_line_size - 1);
    tmp = glue(ldl, MEMSUFFIX)((uint32_t)T0);
    tb_invalidate_page_range((uint32_t)T0,
                             (uint32_t)(T0 + env->icache_line_size));
}

#if defined(TARGET_PPC64)
void glue(do_icbi_64, MEMSUFFIX) (void)
{
    uint64_t tmp;
    /* Invalidate one cache line :
     * PowerPC specification says this is to be treated like a load
     * (not a fetch) by the MMU. To be sure it will be so,
     * do the load "by hand".
     */
    T0 &= ~(env->icache_line_size - 1);
    tmp = glue(ldq, MEMSUFFIX)((uint64_t)T0);
    tb_invalidate_page_range((uint64_t)T0,
                             (uint64_t)(T0 + env->icache_line_size));
}
#endif

/* PowerPC 601 specific instructions (POWER bridge) */
// XXX: to be tested
void glue(do_POWER_lscbx, MEMSUFFIX) (int dest, int ra, int rb)
{
    int i, c, d, reg;

    d = 24;
    reg = dest;
    for (i = 0; i < T1; i++) {
        c = glue(ldu8, MEMSUFFIX)((uint32_t)T0++);
        /* ra (if not 0) and rb are never modified */
        if (likely(reg != rb && (ra == 0 || reg != ra))) {
            env->gpr[reg] = (env->gpr[reg] & ~(0xFF << d)) | (c << d);
        }
        if (unlikely(c == T2))
            break;
        if (likely(d != 0)) {
            d -= 8;
        } else {
            d = 24;
            reg++;
            reg = reg & 0x1F;
        }
    }
    T0 = i;
}

#undef MEMSUFFIX
