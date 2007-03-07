/*
 *  PowerPC emulation micro-operations for qemu.
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

static inline uint16_t glue(ld16r, MEMSUFFIX) (target_ulong EA)
{
    uint16_t tmp = glue(lduw, MEMSUFFIX)(EA);
    return ((tmp & 0xFF00) >> 8) | ((tmp & 0x00FF) << 8);
}

static inline int32_t glue(ld16rs, MEMSUFFIX) (target_ulong EA)
{
    int16_t tmp = glue(lduw, MEMSUFFIX)(EA);
    return (int16_t)((tmp & 0xFF00) >> 8) | ((tmp & 0x00FF) << 8);
}

static inline uint32_t glue(ld32r, MEMSUFFIX) (target_ulong EA)
{
    uint32_t tmp = glue(ldl, MEMSUFFIX)(EA);
    return ((tmp & 0xFF000000) >> 24) | ((tmp & 0x00FF0000) >> 8) |
        ((tmp & 0x0000FF00) << 8) | ((tmp & 0x000000FF) << 24);
}

static inline void glue(st16r, MEMSUFFIX) (target_ulong EA, uint16_t data)
{
    uint16_t tmp = ((data & 0xFF00) >> 8) | ((data & 0x00FF) << 8);
    glue(stw, MEMSUFFIX)(EA, tmp);
}

static inline void glue(st32r, MEMSUFFIX) (target_ulong EA, uint32_t data)
{
    uint32_t tmp = ((data & 0xFF000000) >> 24) | ((data & 0x00FF0000) >> 8) |
        ((data & 0x0000FF00) << 8) | ((data & 0x000000FF) << 24);
    glue(stl, MEMSUFFIX)(EA, tmp);
}

/***                             Integer load                              ***/
#define PPC_LD_OP(name, op)                                                   \
PPC_OP(glue(glue(l, name), MEMSUFFIX))                                        \
{                                                                             \
    T1 = glue(op, MEMSUFFIX)(T0);                                             \
    RETURN();                                                                 \
}

#define PPC_ST_OP(name, op)                                                   \
PPC_OP(glue(glue(st, name), MEMSUFFIX))                                       \
{                                                                             \
    glue(op, MEMSUFFIX)(T0, T1);                                              \
    RETURN();                                                                 \
}

PPC_LD_OP(bz, ldub);
PPC_LD_OP(ha, ldsw);
PPC_LD_OP(hz, lduw);
PPC_LD_OP(wz, ldl);

PPC_LD_OP(ha_le, ld16rs);
PPC_LD_OP(hz_le, ld16r);
PPC_LD_OP(wz_le, ld32r);

/***                              Integer store                            ***/
PPC_ST_OP(b, stb);
PPC_ST_OP(h, stw);
PPC_ST_OP(w, stl);

PPC_ST_OP(h_le, st16r);
PPC_ST_OP(w_le, st32r);

/***                Integer load and store with byte reverse               ***/
PPC_LD_OP(hbr, ld16r);
PPC_LD_OP(wbr, ld32r);
PPC_ST_OP(hbr, st16r);
PPC_ST_OP(wbr, st32r);

PPC_LD_OP(hbr_le, lduw);
PPC_LD_OP(wbr_le, ldl);
PPC_ST_OP(hbr_le, stw);
PPC_ST_OP(wbr_le, stl);

/***                    Integer load and store multiple                    ***/
PPC_OP(glue(lmw, MEMSUFFIX))
{
    glue(do_lmw, MEMSUFFIX)(PARAM1);
    RETURN();
}

PPC_OP(glue(lmw_le, MEMSUFFIX))
{
    glue(do_lmw_le, MEMSUFFIX)(PARAM1);
    RETURN();
}

PPC_OP(glue(stmw, MEMSUFFIX))
{
    glue(do_stmw, MEMSUFFIX)(PARAM1);
    RETURN();
}

PPC_OP(glue(stmw_le, MEMSUFFIX))
{
    glue(do_stmw_le, MEMSUFFIX)(PARAM1);
    RETURN();
}

/***                    Integer load and store strings                     ***/
PPC_OP(glue(lswi, MEMSUFFIX))
{
    glue(do_lsw, MEMSUFFIX)(PARAM(1));
    RETURN();
}

PPC_OP(glue(lswi_le, MEMSUFFIX))
{
    glue(do_lsw_le, MEMSUFFIX)(PARAM(1));
    RETURN();
}

/* PPC32 specification says we must generate an exception if
 * rA is in the range of registers to be loaded.
 * In an other hand, IBM says this is valid, but rA won't be loaded.
 * For now, I'll follow the spec...
 */
PPC_OP(glue(lswx, MEMSUFFIX))
{
    if (unlikely(T1 > 0)) {
        if (unlikely((PARAM1 < PARAM2 && (PARAM1 + T1) > PARAM2) ||
                     (PARAM1 < PARAM3 && (PARAM1 + T1) > PARAM3))) {
            do_raise_exception_err(EXCP_PROGRAM, EXCP_INVAL | EXCP_INVAL_LSWX);
        } else {
            glue(do_lsw, MEMSUFFIX)(PARAM(1));
        }
    }
    RETURN();
}

PPC_OP(glue(lswx_le, MEMSUFFIX))
{
    if (unlikely(T1 > 0)) {
        if (unlikely((PARAM1 < PARAM2 && (PARAM1 + T1) > PARAM2) ||
                     (PARAM1 < PARAM3 && (PARAM1 + T1) > PARAM3))) {
            do_raise_exception_err(EXCP_PROGRAM, EXCP_INVAL | EXCP_INVAL_LSWX);
        } else {
            glue(do_lsw_le, MEMSUFFIX)(PARAM(1));
        }
    }
    RETURN();
}

PPC_OP(glue(stsw, MEMSUFFIX))
{
    glue(do_stsw, MEMSUFFIX)(PARAM(1));
    RETURN();
}

PPC_OP(glue(stsw_le, MEMSUFFIX))
{
    glue(do_stsw_le, MEMSUFFIX)(PARAM(1));
    RETURN();
}

/***                         Floating-point store                          ***/
#define PPC_STF_OP(name, op)                                                  \
PPC_OP(glue(glue(st, name), MEMSUFFIX))                                       \
{                                                                             \
    glue(op, MEMSUFFIX)(T0, FT0);                                             \
    RETURN();                                                                 \
}

PPC_STF_OP(fd, stfq);
PPC_STF_OP(fs, stfl);

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

static inline void glue(stflr, MEMSUFFIX) (target_ulong EA, float f)
{
    union {
        float f;
        uint32_t u;
    } u;

    u.f = f;
    u.u = ((u.u & 0xFF000000UL) >> 24) |
        ((u.u & 0x00FF0000ULL) >> 8) |
        ((u.u & 0x0000FF00UL) << 8) |
        ((u.u & 0x000000FFULL) << 24);
    glue(stfl, MEMSUFFIX)(EA, u.f);
}

PPC_STF_OP(fd_le, stfqr);
PPC_STF_OP(fs_le, stflr);

/***                         Floating-point load                           ***/
#define PPC_LDF_OP(name, op)                                                  \
PPC_OP(glue(glue(l, name), MEMSUFFIX))                                        \
{                                                                             \
    FT0 = glue(op, MEMSUFFIX)(T0);                                            \
    RETURN();                                                                 \
}

PPC_LDF_OP(fd, ldfq);
PPC_LDF_OP(fs, ldfl);

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

static inline float glue(ldflr, MEMSUFFIX) (target_ulong EA)
{
    union {
        float f;
        uint32_t u;
    } u;

    u.f = glue(ldfl, MEMSUFFIX)(EA);
    u.u = ((u.u & 0xFF000000UL) >> 24) |
        ((u.u & 0x00FF0000ULL) >> 8) |
        ((u.u & 0x0000FF00UL) << 8) |
        ((u.u & 0x000000FFULL) << 24);

    return u.f;
}

PPC_LDF_OP(fd_le, ldfqr);
PPC_LDF_OP(fs_le, ldflr);

/* Load and set reservation */
PPC_OP(glue(lwarx, MEMSUFFIX))
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(EXCP_ALIGN);
    } else {
        T1 = glue(ldl, MEMSUFFIX)(T0);
        regs->reserve = T0;
    }
    RETURN();
}

PPC_OP(glue(lwarx_le, MEMSUFFIX))
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(EXCP_ALIGN);
    } else {
        T1 = glue(ld32r, MEMSUFFIX)(T0);
        regs->reserve = T0;
    }
    RETURN();
}

/* Store with reservation */
PPC_OP(glue(stwcx, MEMSUFFIX))
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(EXCP_ALIGN);
    } else {
        if (unlikely(regs->reserve != T0)) {
            env->crf[0] = xer_ov;
        } else {
            glue(stl, MEMSUFFIX)(T0, T1);
            env->crf[0] = xer_ov | 0x02;
        }
    }
    regs->reserve = -1;
    RETURN();
}

PPC_OP(glue(stwcx_le, MEMSUFFIX))
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(EXCP_ALIGN);
    } else {
        if (unlikely(regs->reserve != T0)) {
            env->crf[0] = xer_ov;
        } else {
            glue(st32r, MEMSUFFIX)(T0, T1);
            env->crf[0] = xer_ov | 0x02;
        }
    }
    regs->reserve = -1;
    RETURN();
}

PPC_OP(glue(dcbz, MEMSUFFIX))
{
    glue(stl, MEMSUFFIX)(T0 + 0x00, 0);
    glue(stl, MEMSUFFIX)(T0 + 0x04, 0);
    glue(stl, MEMSUFFIX)(T0 + 0x08, 0);
    glue(stl, MEMSUFFIX)(T0 + 0x0C, 0);
    glue(stl, MEMSUFFIX)(T0 + 0x10, 0);
    glue(stl, MEMSUFFIX)(T0 + 0x14, 0);
    glue(stl, MEMSUFFIX)(T0 + 0x18, 0);
    glue(stl, MEMSUFFIX)(T0 + 0x1C, 0);
#if DCACHE_LINE_SIZE == 64
    /* XXX: cache line size should be 64 for POWER & PowerPC 601 */
    glue(stl, MEMSUFFIX)(T0 + 0x20UL, 0);
    glue(stl, MEMSUFFIX)(T0 + 0x24UL, 0);
    glue(stl, MEMSUFFIX)(T0 + 0x28UL, 0);
    glue(stl, MEMSUFFIX)(T0 + 0x2CUL, 0);
    glue(stl, MEMSUFFIX)(T0 + 0x30UL, 0);
    glue(stl, MEMSUFFIX)(T0 + 0x34UL, 0);
    glue(stl, MEMSUFFIX)(T0 + 0x38UL, 0);
    glue(stl, MEMSUFFIX)(T0 + 0x3CUL, 0);
#endif
    RETURN();
}

/* External access */
PPC_OP(glue(eciwx, MEMSUFFIX))
{
    T1 = glue(ldl, MEMSUFFIX)(T0);
    RETURN();
}

PPC_OP(glue(ecowx, MEMSUFFIX))
{
    glue(stl, MEMSUFFIX)(T0, T1);
    RETURN();
}

PPC_OP(glue(eciwx_le, MEMSUFFIX))
{
    T1 = glue(ld32r, MEMSUFFIX)(T0);
    RETURN();
}

PPC_OP(glue(ecowx_le, MEMSUFFIX))
{
    glue(st32r, MEMSUFFIX)(T0, T1);
    RETURN();
}

/* XXX: those micro-ops need tests ! */
/* PowerPC 601 specific instructions (POWER bridge) */
void OPPROTO glue(op_POWER_lscbx, MEMSUFFIX) (void)
{
    /* When byte count is 0, do nothing */
    if (likely(T1 > 0)) {
        glue(do_POWER_lscbx, MEMSUFFIX)(PARAM1, PARAM2, PARAM3);
    }
    RETURN();
}

/* POWER2 quad load and store */
/* XXX: TAGs are not managed */
void OPPROTO glue(op_POWER2_lfq, MEMSUFFIX) (void)
{
    glue(do_POWER2_lfq, MEMSUFFIX)();
    RETURN();
}

void glue(op_POWER2_lfq_le, MEMSUFFIX) (void)
{
    glue(do_POWER2_lfq_le, MEMSUFFIX)();
    RETURN();
}

void OPPROTO glue(op_POWER2_stfq, MEMSUFFIX) (void)
{
    glue(do_POWER2_stfq, MEMSUFFIX)();
    RETURN();
}

void OPPROTO glue(op_POWER2_stfq_le, MEMSUFFIX) (void)
{
    glue(do_POWER2_stfq_le, MEMSUFFIX)();
    RETURN();
}

#undef MEMSUFFIX
