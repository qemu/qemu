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

/* Multiple word / string load and store */
static inline target_ulong glue(ld32r, MEMSUFFIX) (target_ulong EA)
{
    uint32_t tmp = glue(ldl, MEMSUFFIX)(EA);
    return ((tmp & 0xFF000000UL) >> 24) | ((tmp & 0x00FF0000UL) >> 8) |
        ((tmp & 0x0000FF00UL) << 8) | ((tmp & 0x000000FFUL) << 24);
}

static inline void glue(st32r, MEMSUFFIX) (target_ulong EA, target_ulong data)
{
    uint32_t tmp =
        ((data & 0xFF000000UL) >> 24) | ((data & 0x00FF0000UL) >> 8) |
        ((data & 0x0000FF00UL) << 8) | ((data & 0x000000FFUL) << 24);
    glue(stl, MEMSUFFIX)(EA, tmp);
}

void glue(do_lmw, MEMSUFFIX) (int dst)
{
    for (; dst < 32; dst++, T0 += 4) {
        env->gpr[dst] = glue(ldl, MEMSUFFIX)((uint32_t)T0);
    }
}

#if defined(TARGET_PPC64)
void glue(do_lmw_64, MEMSUFFIX) (int dst)
{
    for (; dst < 32; dst++, T0 += 4) {
        env->gpr[dst] = glue(ldl, MEMSUFFIX)((uint64_t)T0);
    }
}
#endif

void glue(do_stmw, MEMSUFFIX) (int src)
{
    for (; src < 32; src++, T0 += 4) {
        glue(stl, MEMSUFFIX)((uint32_t)T0, env->gpr[src]);
    }
}

#if defined(TARGET_PPC64)
void glue(do_stmw_64, MEMSUFFIX) (int src)
{
    for (; src < 32; src++, T0 += 4) {
        glue(stl, MEMSUFFIX)((uint64_t)T0, env->gpr[src]);
    }
}
#endif

void glue(do_lmw_le, MEMSUFFIX) (int dst)
{
    for (; dst < 32; dst++, T0 += 4) {
        env->gpr[dst] = glue(ld32r, MEMSUFFIX)((uint32_t)T0);
    }
}

#if defined(TARGET_PPC64)
void glue(do_lmw_le_64, MEMSUFFIX) (int dst)
{
    for (; dst < 32; dst++, T0 += 4) {
        env->gpr[dst] = glue(ld32r, MEMSUFFIX)((uint64_t)T0);
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
        env->gpr[dst++] = glue(ldl, MEMSUFFIX)((uint32_t)T0);
        if (unlikely(dst == 32))
            dst = 0;
    }
    if (unlikely(T1 != 0)) {
        tmp = 0;
        for (sh = 24; T1 > 0; T1--, T0++, sh -= 8) {
            tmp |= glue(ldub, MEMSUFFIX)((uint32_t)T0) << sh;
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
        env->gpr[dst++] = glue(ldl, MEMSUFFIX)((uint64_t)T0);
        if (unlikely(dst == 32))
            dst = 0;
    }
    if (unlikely(T1 != 0)) {
        tmp = 0;
        for (sh = 24; T1 > 0; T1--, T0++, sh -= 8) {
            tmp |= glue(ldub, MEMSUFFIX)((uint64_t)T0) << sh;
        }
        env->gpr[dst] = tmp;
    }
}
#endif

void glue(do_stsw, MEMSUFFIX) (int src)
{
    int sh;

    for (; T1 > 3; T1 -= 4, T0 += 4) {
        glue(stl, MEMSUFFIX)((uint32_t)T0, env->gpr[src++]);
        if (unlikely(src == 32))
            src = 0;
    }
    if (unlikely(T1 != 0)) {
        for (sh = 24; T1 > 0; T1--, T0++, sh -= 8)
            glue(stb, MEMSUFFIX)((uint32_t)T0, (env->gpr[src] >> sh) & 0xFF);
    }
}

#if defined(TARGET_PPC64)
void glue(do_stsw_64, MEMSUFFIX) (int src)
{
    int sh;

    for (; T1 > 3; T1 -= 4, T0 += 4) {
        glue(stl, MEMSUFFIX)((uint64_t)T0, env->gpr[src++]);
        if (unlikely(src == 32))
            src = 0;
    }
    if (unlikely(T1 != 0)) {
        for (sh = 24; T1 > 0; T1--, T0++, sh -= 8)
            glue(stb, MEMSUFFIX)((uint64_t)T0, (env->gpr[src] >> sh) & 0xFF);
    }
}
#endif

void glue(do_lsw_le, MEMSUFFIX) (int dst)
{
    uint32_t tmp;
    int sh;

    for (; T1 > 3; T1 -= 4, T0 += 4) {
        env->gpr[dst++] = glue(ld32r, MEMSUFFIX)((uint32_t)T0);
        if (unlikely(dst == 32))
            dst = 0;
    }
    if (unlikely(T1 != 0)) {
        tmp = 0;
        for (sh = 0; T1 > 0; T1--, T0++, sh += 8) {
            tmp |= glue(ldub, MEMSUFFIX)((uint32_t)T0) << sh;
        }
        env->gpr[dst] = tmp;
    }
}

#if defined(TARGET_PPC64)
void glue(do_lsw_le_64, MEMSUFFIX) (int dst)
{
    uint32_t tmp;
    int sh;

    for (; T1 > 3; T1 -= 4, T0 += 4) {
        env->gpr[dst++] = glue(ld32r, MEMSUFFIX)((uint64_t)T0);
        if (unlikely(dst == 32))
            dst = 0;
    }
    if (unlikely(T1 != 0)) {
        tmp = 0;
        for (sh = 0; T1 > 0; T1--, T0++, sh += 8) {
            tmp |= glue(ldub, MEMSUFFIX)((uint64_t)T0) << sh;
        }
        env->gpr[dst] = tmp;
    }
}
#endif

void glue(do_stsw_le, MEMSUFFIX) (int src)
{
    int sh;

    for (; T1 > 3; T1 -= 4, T0 += 4) {
        glue(st32r, MEMSUFFIX)((uint32_t)T0, env->gpr[src++]);
        if (unlikely(src == 32))
            src = 0;
    }
    if (unlikely(T1 != 0)) {
        for (sh = 0; T1 > 0; T1--, T0++, sh += 8)
            glue(stb, MEMSUFFIX)((uint32_t)T0, (env->gpr[src] >> sh) & 0xFF);
    }
}

#if defined(TARGET_PPC64)
void glue(do_stsw_le_64, MEMSUFFIX) (int src)
{
    int sh;

    for (; T1 > 3; T1 -= 4, T0 += 4) {
        glue(st32r, MEMSUFFIX)((uint64_t)T0, env->gpr[src++]);
        if (unlikely(src == 32))
            src = 0;
    }
    if (unlikely(T1 != 0)) {
        for (sh = 0; T1 > 0; T1--, T0++, sh += 8)
            glue(stb, MEMSUFFIX)((uint64_t)T0, (env->gpr[src] >> sh) & 0xFF);
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
    tmp = glue(ldl, MEMSUFFIX)((uint32_t)T0);
    T0 &= ~(ICACHE_LINE_SIZE - 1);
    tb_invalidate_page_range((uint32_t)T0, (uint32_t)(T0 + ICACHE_LINE_SIZE));
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
    tmp = glue(ldq, MEMSUFFIX)((uint64_t)T0);
    T0 &= ~(ICACHE_LINE_SIZE - 1);
    tb_invalidate_page_range((uint64_t)T0, (uint64_t)(T0 + ICACHE_LINE_SIZE));
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
        c = glue(ldub, MEMSUFFIX)((uint32_t)T0++);
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

/* XXX: TAGs are not managed */
void glue(do_POWER2_lfq, MEMSUFFIX) (void)
{
    FT0 = glue(ldfq, MEMSUFFIX)((uint32_t)T0);
    FT1 = glue(ldfq, MEMSUFFIX)((uint32_t)(T0 + 4));
}

static inline double glue(ldfqr, MEMSUFFIX) (target_ulong EA)
{
    union {
        double d;
        uint64_t u;
    } u;

    u.d = glue(ldfq, MEMSUFFIX)(EA);
    u.u = ((u.u & 0xFF00000000000000ULL) >> 56) |
        ((u.u & 0x00FF000000000000ULL) >> 40) |
        ((u.u & 0x0000FF0000000000ULL) >> 24) |
        ((u.u & 0x000000FF00000000ULL) >> 8) |
        ((u.u & 0x00000000FF000000ULL) << 8) |
        ((u.u & 0x0000000000FF0000ULL) << 24) |
        ((u.u & 0x000000000000FF00ULL) << 40) |
        ((u.u & 0x00000000000000FFULL) << 56);

    return u.d;
}

void glue(do_POWER2_lfq_le, MEMSUFFIX) (void)
{
    FT0 = glue(ldfqr, MEMSUFFIX)((uint32_t)(T0 + 4));
    FT1 = glue(ldfqr, MEMSUFFIX)((uint32_t)T0);
}

void glue(do_POWER2_stfq, MEMSUFFIX) (void)
{
    glue(stfq, MEMSUFFIX)((uint32_t)T0, FT0);
    glue(stfq, MEMSUFFIX)((uint32_t)(T0 + 4), FT1);
}

static inline void glue(stfqr, MEMSUFFIX) (target_ulong EA, double d)
{
    union {
        double d;
        uint64_t u;
    } u;

    u.d = d;
    u.u = ((u.u & 0xFF00000000000000ULL) >> 56) |
        ((u.u & 0x00FF000000000000ULL) >> 40) |
        ((u.u & 0x0000FF0000000000ULL) >> 24) |
        ((u.u & 0x000000FF00000000ULL) >> 8) |
        ((u.u & 0x00000000FF000000ULL) << 8) |
        ((u.u & 0x0000000000FF0000ULL) << 24) |
        ((u.u & 0x000000000000FF00ULL) << 40) |
        ((u.u & 0x00000000000000FFULL) << 56);
    glue(stfq, MEMSUFFIX)(EA, u.d);
}

void glue(do_POWER2_stfq_le, MEMSUFFIX) (void)
{
    glue(stfqr, MEMSUFFIX)((uint32_t)(T0 + 4), FT0);
    glue(stfqr, MEMSUFFIX)((uint32_t)T0, FT1);
}

#undef MEMSUFFIX
