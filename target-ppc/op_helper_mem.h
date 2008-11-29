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

/* Multiple word / string load and store */
void glue(do_lmw, MEMSUFFIX) (int dst)
{
    for (; dst < 32; dst++, T0 += 4) {
        env->gpr[dst] = glue(ldu32, MEMSUFFIX)((uint32_t)T0);
    }
}

#if defined(TARGET_PPC64)
void glue(do_lmw_64, MEMSUFFIX) (int dst)
{
    for (; dst < 32; dst++, T0 += 4) {
        env->gpr[dst] = glue(ldu32, MEMSUFFIX)((uint64_t)T0);
    }
}
#endif

void glue(do_stmw, MEMSUFFIX) (int src)
{
    for (; src < 32; src++, T0 += 4) {
        glue(st32, MEMSUFFIX)((uint32_t)T0, env->gpr[src]);
    }
}

#if defined(TARGET_PPC64)
void glue(do_stmw_64, MEMSUFFIX) (int src)
{
    for (; src < 32; src++, T0 += 4) {
        glue(st32, MEMSUFFIX)((uint64_t)T0, env->gpr[src]);
    }
}
#endif

void glue(do_lmw_le, MEMSUFFIX) (int dst)
{
    for (; dst < 32; dst++, T0 += 4) {
        env->gpr[dst] = glue(ldu32r, MEMSUFFIX)((uint32_t)T0);
    }
}

#if defined(TARGET_PPC64)
void glue(do_lmw_le_64, MEMSUFFIX) (int dst)
{
    for (; dst < 32; dst++, T0 += 4) {
        env->gpr[dst] = glue(ldu32r, MEMSUFFIX)((uint64_t)T0);
    }
}
#endif

void glue(do_stmw_le, MEMSUFFIX) (int src)
{
    for (; src < 32; src++, T0 += 4) {
        glue(st32r, MEMSUFFIX)((uint32_t)T0, env->gpr[src]);
    }
}

#if defined(TARGET_PPC64)
void glue(do_stmw_le_64, MEMSUFFIX) (int src)
{
    for (; src < 32; src++, T0 += 4) {
        glue(st32r, MEMSUFFIX)((uint64_t)T0, env->gpr[src]);
    }
}
#endif

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

void glue(do_dcbz, MEMSUFFIX) (void)
{
    int dcache_line_size = env->dcache_line_size;

    /* XXX: should be 970 specific (?) */
    if (((env->spr[SPR_970_HID5] >> 7) & 0x3) == 1)
        dcache_line_size = 32;
    T0 &= ~(uint32_t)(dcache_line_size - 1);
    glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x00), 0);
    glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x04), 0);
    glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x08), 0);
    glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x0C), 0);
    glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x10), 0);
    glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x14), 0);
    glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x18), 0);
    glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x1C), 0);
    if (dcache_line_size >= 64) {
        glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x20UL), 0);
        glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x24UL), 0);
        glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x28UL), 0);
        glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x2CUL), 0);
        glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x30UL), 0);
        glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x34UL), 0);
        glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x38UL), 0);
        glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x3CUL), 0);
        if (dcache_line_size >= 128) {
            glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x40UL), 0);
            glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x44UL), 0);
            glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x48UL), 0);
            glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x4CUL), 0);
            glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x50UL), 0);
            glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x54UL), 0);
            glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x58UL), 0);
            glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x5CUL), 0);
            glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x60UL), 0);
            glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x64UL), 0);
            glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x68UL), 0);
            glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x6CUL), 0);
            glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x70UL), 0);
            glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x74UL), 0);
            glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x78UL), 0);
            glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x7CUL), 0);
        }
    }
}

#if defined(TARGET_PPC64)
void glue(do_dcbz_64, MEMSUFFIX) (void)
{
    int dcache_line_size = env->dcache_line_size;

    /* XXX: should be 970 specific (?) */
    if (((env->spr[SPR_970_HID5] >> 6) & 0x3) == 0x2)
        dcache_line_size = 32;
    T0 &= ~(uint64_t)(dcache_line_size - 1);
    glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x00), 0);
    glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x04), 0);
    glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x08), 0);
    glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x0C), 0);
    glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x10), 0);
    glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x14), 0);
    glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x18), 0);
    glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x1C), 0);
    if (dcache_line_size >= 64) {
        glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x20UL), 0);
        glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x24UL), 0);
        glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x28UL), 0);
        glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x2CUL), 0);
        glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x30UL), 0);
        glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x34UL), 0);
        glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x38UL), 0);
        glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x3CUL), 0);
        if (dcache_line_size >= 128) {
            glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x40UL), 0);
            glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x44UL), 0);
            glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x48UL), 0);
            glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x4CUL), 0);
            glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x50UL), 0);
            glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x54UL), 0);
            glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x58UL), 0);
            glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x5CUL), 0);
            glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x60UL), 0);
            glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x64UL), 0);
            glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x68UL), 0);
            glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x6CUL), 0);
            glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x70UL), 0);
            glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x74UL), 0);
            glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x78UL), 0);
            glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x7CUL), 0);
        }
    }
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
