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

#if defined(TARGET_PPC64) || defined(TARGET_PPCSPE)
static inline uint64_t glue(ld64r, MEMSUFFIX) (target_ulong EA)
{
    uint64_t tmp = glue(ldq, MEMSUFFIX)(EA);
    return ((tmp & 0xFF00000000000000ULL) >> 56) |
        ((tmp & 0x00FF000000000000ULL) >> 40) |
        ((tmp & 0x0000FF0000000000ULL) >> 24) |
        ((tmp & 0x000000FF00000000ULL) >> 8) |
        ((tmp & 0x00000000FF000000ULL) << 8) |
        ((tmp & 0x0000000000FF0000ULL) << 24) |
        ((tmp & 0x000000000000FF00ULL) << 40) |
        ((tmp & 0x00000000000000FFULL) << 54);
}
#endif

#if defined(TARGET_PPC64)
static inline int64_t glue(ldsl, MEMSUFFIX) (target_ulong EA)
{
    return (int32_t)glue(ldl, MEMSUFFIX)(EA);
}

static inline int64_t glue(ld32rs, MEMSUFFIX) (target_ulong EA)
{
    uint32_t tmp = glue(ldl, MEMSUFFIX)(EA);
    return (int32_t)((tmp & 0xFF000000) >> 24) | ((tmp & 0x00FF0000) >> 8) |
        ((tmp & 0x0000FF00) << 8) | ((tmp & 0x000000FF) << 24);
}
#endif

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

#if defined(TARGET_PPC64) || defined(TARGET_PPCSPE)
static inline void glue(st64r, MEMSUFFIX) (target_ulong EA, uint64_t data)
{
    uint64_t tmp = ((data & 0xFF00000000000000ULL) >> 56) |
        ((data & 0x00FF000000000000ULL) >> 40) |
        ((data & 0x0000FF0000000000ULL) >> 24) |
        ((data & 0x000000FF00000000ULL) >> 8) |
        ((data & 0x00000000FF000000ULL) << 8) |
        ((data & 0x0000000000FF0000ULL) << 24) |
        ((data & 0x000000000000FF00ULL) << 40) |
        ((data & 0x00000000000000FFULL) << 56);
    glue(stq, MEMSUFFIX)(EA, tmp);
}
#endif

/***                             Integer load                              ***/
#define PPC_LD_OP(name, op)                                                   \
void OPPROTO glue(glue(op_l, name), MEMSUFFIX) (void)                         \
{                                                                             \
    T1 = glue(op, MEMSUFFIX)((uint32_t)T0);                                   \
    RETURN();                                                                 \
}

#if defined(TARGET_PPC64)
#define PPC_LD_OP_64(name, op)                                                \
void OPPROTO glue(glue(glue(op_l, name), _64), MEMSUFFIX) (void)              \
{                                                                             \
    T1 = glue(op, MEMSUFFIX)((uint64_t)T0);                                   \
    RETURN();                                                                 \
}
#endif

#define PPC_ST_OP(name, op)                                                   \
void OPPROTO glue(glue(op_st, name), MEMSUFFIX) (void)                        \
{                                                                             \
    glue(op, MEMSUFFIX)((uint32_t)T0, T1);                                    \
    RETURN();                                                                 \
}

#if defined(TARGET_PPC64)
#define PPC_ST_OP_64(name, op)                                                \
void OPPROTO glue(glue(glue(op_st, name), _64), MEMSUFFIX) (void)             \
{                                                                             \
    glue(op, MEMSUFFIX)((uint64_t)T0, T1);                                    \
    RETURN();                                                                 \
}
#endif

PPC_LD_OP(bz, ldub);
PPC_LD_OP(ha, ldsw);
PPC_LD_OP(hz, lduw);
PPC_LD_OP(wz, ldl);
#if defined(TARGET_PPC64)
PPC_LD_OP(d, ldq);
PPC_LD_OP(wa, ldsl);
PPC_LD_OP_64(d, ldq);
PPC_LD_OP_64(wa, ldsl);
PPC_LD_OP_64(bz, ldub);
PPC_LD_OP_64(ha, ldsw);
PPC_LD_OP_64(hz, lduw);
PPC_LD_OP_64(wz, ldl);
#endif

PPC_LD_OP(ha_le, ld16rs);
PPC_LD_OP(hz_le, ld16r);
PPC_LD_OP(wz_le, ld32r);
#if defined(TARGET_PPC64)
PPC_LD_OP(d_le, ld64r);
PPC_LD_OP(wa_le, ld32rs);
PPC_LD_OP_64(d_le, ld64r);
PPC_LD_OP_64(wa_le, ld32rs);
PPC_LD_OP_64(ha_le, ld16rs);
PPC_LD_OP_64(hz_le, ld16r);
PPC_LD_OP_64(wz_le, ld32r);
#endif

/***                              Integer store                            ***/
PPC_ST_OP(b, stb);
PPC_ST_OP(h, stw);
PPC_ST_OP(w, stl);
#if defined(TARGET_PPC64)
PPC_ST_OP(d, stq);
PPC_ST_OP_64(d, stq);
PPC_ST_OP_64(b, stb);
PPC_ST_OP_64(h, stw);
PPC_ST_OP_64(w, stl);
#endif

PPC_ST_OP(h_le, st16r);
PPC_ST_OP(w_le, st32r);
#if defined(TARGET_PPC64)
PPC_ST_OP(d_le, st64r);
PPC_ST_OP_64(d_le, st64r);
PPC_ST_OP_64(h_le, st16r);
PPC_ST_OP_64(w_le, st32r);
#endif

/***                Integer load and store with byte reverse               ***/
PPC_LD_OP(hbr, ld16r);
PPC_LD_OP(wbr, ld32r);
PPC_ST_OP(hbr, st16r);
PPC_ST_OP(wbr, st32r);
#if defined(TARGET_PPC64)
PPC_LD_OP_64(hbr, ld16r);
PPC_LD_OP_64(wbr, ld32r);
PPC_ST_OP_64(hbr, st16r);
PPC_ST_OP_64(wbr, st32r);
#endif

PPC_LD_OP(hbr_le, lduw);
PPC_LD_OP(wbr_le, ldl);
PPC_ST_OP(hbr_le, stw);
PPC_ST_OP(wbr_le, stl);
#if defined(TARGET_PPC64)
PPC_LD_OP_64(hbr_le, lduw);
PPC_LD_OP_64(wbr_le, ldl);
PPC_ST_OP_64(hbr_le, stw);
PPC_ST_OP_64(wbr_le, stl);
#endif

/***                    Integer load and store multiple                    ***/
void OPPROTO glue(op_lmw, MEMSUFFIX) (void)
{
    glue(do_lmw, MEMSUFFIX)(PARAM1);
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_lmw_64, MEMSUFFIX) (void)
{
    glue(do_lmw_64, MEMSUFFIX)(PARAM1);
    RETURN();
}
#endif

void OPPROTO glue(op_lmw_le, MEMSUFFIX) (void)
{
    glue(do_lmw_le, MEMSUFFIX)(PARAM1);
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_lmw_le_64, MEMSUFFIX) (void)
{
    glue(do_lmw_le_64, MEMSUFFIX)(PARAM1);
    RETURN();
}
#endif

void OPPROTO glue(op_stmw, MEMSUFFIX) (void)
{
    glue(do_stmw, MEMSUFFIX)(PARAM1);
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_stmw_64, MEMSUFFIX) (void)
{
    glue(do_stmw_64, MEMSUFFIX)(PARAM1);
    RETURN();
}
#endif

void OPPROTO glue(op_stmw_le, MEMSUFFIX) (void)
{
    glue(do_stmw_le, MEMSUFFIX)(PARAM1);
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_stmw_le_64, MEMSUFFIX) (void)
{
    glue(do_stmw_le_64, MEMSUFFIX)(PARAM1);
    RETURN();
}
#endif

/***                    Integer load and store strings                     ***/
void OPPROTO glue(op_lswi, MEMSUFFIX) (void)
{
    glue(do_lsw, MEMSUFFIX)(PARAM1);
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_lswi_64, MEMSUFFIX) (void)
{
    glue(do_lsw_64, MEMSUFFIX)(PARAM1);
    RETURN();
}
#endif

void OPPROTO glue(op_lswi_le, MEMSUFFIX) (void)
{
    glue(do_lsw_le, MEMSUFFIX)(PARAM1);
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_lswi_le_64, MEMSUFFIX) (void)
{
    glue(do_lsw_le_64, MEMSUFFIX)(PARAM1);
    RETURN();
}
#endif

/* PPC32 specification says we must generate an exception if
 * rA is in the range of registers to be loaded.
 * In an other hand, IBM says this is valid, but rA won't be loaded.
 * For now, I'll follow the spec...
 */
void OPPROTO glue(op_lswx, MEMSUFFIX) (void)
{
    /* Note: T1 comes from xer_bc then no cast is needed */
    if (likely(T1 != 0)) {
        if (unlikely((PARAM1 < PARAM2 && (PARAM1 + T1) > PARAM2) ||
                     (PARAM1 < PARAM3 && (PARAM1 + T1) > PARAM3))) {
            do_raise_exception_err(EXCP_PROGRAM, EXCP_INVAL | EXCP_INVAL_LSWX);
        } else {
            glue(do_lsw, MEMSUFFIX)(PARAM1);
        }
    }
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_lswx_64, MEMSUFFIX) (void)
{
    /* Note: T1 comes from xer_bc then no cast is needed */
    if (likely(T1 != 0)) {
        if (unlikely((PARAM1 < PARAM2 && (PARAM1 + T1) > PARAM2) ||
                     (PARAM1 < PARAM3 && (PARAM1 + T1) > PARAM3))) {
            do_raise_exception_err(EXCP_PROGRAM, EXCP_INVAL | EXCP_INVAL_LSWX);
        } else {
            glue(do_lsw_64, MEMSUFFIX)(PARAM1);
        }
    }
    RETURN();
}
#endif

void OPPROTO glue(op_lswx_le, MEMSUFFIX) (void)
{
    /* Note: T1 comes from xer_bc then no cast is needed */
    if (likely(T1 != 0)) {
        if (unlikely((PARAM1 < PARAM2 && (PARAM1 + T1) > PARAM2) ||
                     (PARAM1 < PARAM3 && (PARAM1 + T1) > PARAM3))) {
            do_raise_exception_err(EXCP_PROGRAM, EXCP_INVAL | EXCP_INVAL_LSWX);
        } else {
            glue(do_lsw_le, MEMSUFFIX)(PARAM1);
        }
    }
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_lswx_le_64, MEMSUFFIX) (void)
{
    /* Note: T1 comes from xer_bc then no cast is needed */
    if (likely(T1 != 0)) {
        if (unlikely((PARAM1 < PARAM2 && (PARAM1 + T1) > PARAM2) ||
                     (PARAM1 < PARAM3 && (PARAM1 + T1) > PARAM3))) {
            do_raise_exception_err(EXCP_PROGRAM, EXCP_INVAL | EXCP_INVAL_LSWX);
        } else {
            glue(do_lsw_le_64, MEMSUFFIX)(PARAM1);
        }
    }
    RETURN();
}
#endif

void OPPROTO glue(op_stsw, MEMSUFFIX) (void)
{
    glue(do_stsw, MEMSUFFIX)(PARAM1);
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_stsw_64, MEMSUFFIX) (void)
{
    glue(do_stsw_64, MEMSUFFIX)(PARAM1);
    RETURN();
}
#endif

void OPPROTO glue(op_stsw_le, MEMSUFFIX) (void)
{
    glue(do_stsw_le, MEMSUFFIX)(PARAM1);
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_stsw_le_64, MEMSUFFIX) (void)
{
    glue(do_stsw_le_64, MEMSUFFIX)(PARAM1);
    RETURN();
}
#endif

/***                         Floating-point store                          ***/
#define PPC_STF_OP(name, op)                                                  \
void OPPROTO glue(glue(op_st, name), MEMSUFFIX) (void)                        \
{                                                                             \
    glue(op, MEMSUFFIX)((uint32_t)T0, FT0);                                   \
    RETURN();                                                                 \
}

#if defined(TARGET_PPC64)
#define PPC_STF_OP_64(name, op)                                               \
void OPPROTO glue(glue(glue(op_st, name), _64), MEMSUFFIX) (void)             \
{                                                                             \
    glue(op, MEMSUFFIX)((uint64_t)T0, FT0);                                   \
    RETURN();                                                                 \
}
#endif

PPC_STF_OP(fd, stfq);
PPC_STF_OP(fs, stfl);
#if defined(TARGET_PPC64)
PPC_STF_OP_64(fd, stfq);
PPC_STF_OP_64(fs, stfl);
#endif

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
#if defined(TARGET_PPC64)
PPC_STF_OP_64(fd_le, stfqr);
PPC_STF_OP_64(fs_le, stflr);
#endif

/***                         Floating-point load                           ***/
#define PPC_LDF_OP(name, op)                                                  \
void OPPROTO glue(glue(op_l, name), MEMSUFFIX) (void)                         \
{                                                                             \
    FT0 = glue(op, MEMSUFFIX)((uint32_t)T0);                                  \
    RETURN();                                                                 \
}

#if defined(TARGET_PPC64)
#define PPC_LDF_OP_64(name, op)                                               \
void OPPROTO glue(glue(glue(op_l, name), _64), MEMSUFFIX) (void)              \
{                                                                             \
    FT0 = glue(op, MEMSUFFIX)((uint64_t)T0);                                  \
    RETURN();                                                                 \
}
#endif

PPC_LDF_OP(fd, ldfq);
PPC_LDF_OP(fs, ldfl);
#if defined(TARGET_PPC64)
PPC_LDF_OP_64(fd, ldfq);
PPC_LDF_OP_64(fs, ldfl);
#endif

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
#if defined(TARGET_PPC64)
PPC_LDF_OP_64(fd_le, ldfqr);
PPC_LDF_OP_64(fs_le, ldflr);
#endif

/* Load and set reservation */
void OPPROTO glue(op_lwarx, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(EXCP_ALIGN);
    } else {
        T1 = glue(ldl, MEMSUFFIX)((uint32_t)T0);
        regs->reserve = (uint32_t)T0;
    }
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_lwarx_64, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(EXCP_ALIGN);
    } else {
        T1 = glue(ldl, MEMSUFFIX)((uint64_t)T0);
        regs->reserve = (uint64_t)T0;
    }
    RETURN();
}

void OPPROTO glue(op_ldarx, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(EXCP_ALIGN);
    } else {
        T1 = glue(ldq, MEMSUFFIX)((uint32_t)T0);
        regs->reserve = (uint32_t)T0;
    }
    RETURN();
}

void OPPROTO glue(op_ldarx_64, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(EXCP_ALIGN);
    } else {
        T1 = glue(ldq, MEMSUFFIX)((uint64_t)T0);
        regs->reserve = (uint64_t)T0;
    }
    RETURN();
}
#endif

void OPPROTO glue(op_lwarx_le, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(EXCP_ALIGN);
    } else {
        T1 = glue(ld32r, MEMSUFFIX)((uint32_t)T0);
        regs->reserve = (uint32_t)T0;
    }
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_lwarx_le_64, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(EXCP_ALIGN);
    } else {
        T1 = glue(ld32r, MEMSUFFIX)((uint64_t)T0);
        regs->reserve = (uint64_t)T0;
    }
    RETURN();
}

void OPPROTO glue(op_ldarx_le, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(EXCP_ALIGN);
    } else {
        T1 = glue(ld64r, MEMSUFFIX)((uint32_t)T0);
        regs->reserve = (uint32_t)T0;
    }
    RETURN();
}

void OPPROTO glue(op_ldarx_le_64, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(EXCP_ALIGN);
    } else {
        T1 = glue(ld64r, MEMSUFFIX)((uint64_t)T0);
        regs->reserve = (uint64_t)T0;
    }
    RETURN();
}
#endif

/* Store with reservation */
void OPPROTO glue(op_stwcx, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(EXCP_ALIGN);
    } else {
        if (unlikely(regs->reserve != (uint32_t)T0)) {
            env->crf[0] = xer_ov;
        } else {
            glue(stl, MEMSUFFIX)((uint32_t)T0, T1);
            env->crf[0] = xer_ov | 0x02;
        }
    }
    regs->reserve = -1;
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_stwcx_64, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(EXCP_ALIGN);
    } else {
        if (unlikely(regs->reserve != (uint64_t)T0)) {
            env->crf[0] = xer_ov;
        } else {
            glue(stl, MEMSUFFIX)((uint64_t)T0, T1);
            env->crf[0] = xer_ov | 0x02;
        }
    }
    regs->reserve = -1;
    RETURN();
}

void OPPROTO glue(op_stdcx, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(EXCP_ALIGN);
    } else {
        if (unlikely(regs->reserve != (uint32_t)T0)) {
            env->crf[0] = xer_ov;
        } else {
            glue(stq, MEMSUFFIX)((uint32_t)T0, T1);
            env->crf[0] = xer_ov | 0x02;
        }
    }
    regs->reserve = -1;
    RETURN();
}

void OPPROTO glue(op_stdcx_64, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(EXCP_ALIGN);
    } else {
        if (unlikely(regs->reserve != (uint64_t)T0)) {
            env->crf[0] = xer_ov;
        } else {
            glue(stq, MEMSUFFIX)((uint64_t)T0, T1);
            env->crf[0] = xer_ov | 0x02;
        }
    }
    regs->reserve = -1;
    RETURN();
}
#endif

void OPPROTO glue(op_stwcx_le, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(EXCP_ALIGN);
    } else {
        if (unlikely(regs->reserve != (uint32_t)T0)) {
            env->crf[0] = xer_ov;
        } else {
            glue(st32r, MEMSUFFIX)((uint32_t)T0, T1);
            env->crf[0] = xer_ov | 0x02;
        }
    }
    regs->reserve = -1;
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_stwcx_le_64, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(EXCP_ALIGN);
    } else {
        if (unlikely(regs->reserve != (uint64_t)T0)) {
            env->crf[0] = xer_ov;
        } else {
            glue(st32r, MEMSUFFIX)((uint64_t)T0, T1);
            env->crf[0] = xer_ov | 0x02;
        }
    }
    regs->reserve = -1;
    RETURN();
}

void OPPROTO glue(op_stdcx_le, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(EXCP_ALIGN);
    } else {
        if (unlikely(regs->reserve != (uint32_t)T0)) {
            env->crf[0] = xer_ov;
        } else {
            glue(st64r, MEMSUFFIX)((uint32_t)T0, T1);
            env->crf[0] = xer_ov | 0x02;
        }
    }
    regs->reserve = -1;
    RETURN();
}

void OPPROTO glue(op_stdcx_le_64, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(EXCP_ALIGN);
    } else {
        if (unlikely(regs->reserve != (uint64_t)T0)) {
            env->crf[0] = xer_ov;
        } else {
            glue(st64r, MEMSUFFIX)((uint64_t)T0, T1);
            env->crf[0] = xer_ov | 0x02;
        }
    }
    regs->reserve = -1;
    RETURN();
}
#endif

void OPPROTO glue(op_dcbz, MEMSUFFIX) (void)
{
    glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x00), 0);
    glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x04), 0);
    glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x08), 0);
    glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x0C), 0);
    glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x10), 0);
    glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x14), 0);
    glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x18), 0);
    glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x1C), 0);
#if DCACHE_LINE_SIZE == 64
    /* XXX: cache line size should be 64 for POWER & PowerPC 601 */
    glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x20UL), 0);
    glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x24UL), 0);
    glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x28UL), 0);
    glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x2CUL), 0);
    glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x30UL), 0);
    glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x34UL), 0);
    glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x38UL), 0);
    glue(stl, MEMSUFFIX)((uint32_t)(T0 + 0x3CUL), 0);
#endif
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_dcbz_64, MEMSUFFIX) (void)
{
    glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x00), 0);
    glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x04), 0);
    glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x08), 0);
    glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x0C), 0);
    glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x10), 0);
    glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x14), 0);
    glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x18), 0);
    glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x1C), 0);
#if DCACHE_LINE_SIZE == 64
    /* XXX: cache line size should be 64 for POWER & PowerPC 601 */
    glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x20UL), 0);
    glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x24UL), 0);
    glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x28UL), 0);
    glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x2CUL), 0);
    glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x30UL), 0);
    glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x34UL), 0);
    glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x38UL), 0);
    glue(stl, MEMSUFFIX)((uint64_t)(T0 + 0x3CUL), 0);
#endif
    RETURN();
}
#endif

/* Instruction cache block invalidate */
void OPPROTO glue(op_icbi, MEMSUFFIX) (void)
{
    glue(do_icbi, MEMSUFFIX)();
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_icbi_64, MEMSUFFIX) (void)
{
    glue(do_icbi_64, MEMSUFFIX)();
    RETURN();
}
#endif

/* External access */
void OPPROTO glue(op_eciwx, MEMSUFFIX) (void)
{
    T1 = glue(ldl, MEMSUFFIX)((uint32_t)T0);
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_eciwx_64, MEMSUFFIX) (void)
{
    T1 = glue(ldl, MEMSUFFIX)((uint64_t)T0);
    RETURN();
}
#endif

void OPPROTO glue(op_ecowx, MEMSUFFIX) (void)
{
    glue(stl, MEMSUFFIX)((uint32_t)T0, T1);
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_ecowx_64, MEMSUFFIX) (void)
{
    glue(stl, MEMSUFFIX)((uint64_t)T0, T1);
    RETURN();
}
#endif

void OPPROTO glue(op_eciwx_le, MEMSUFFIX) (void)
{
    T1 = glue(ld32r, MEMSUFFIX)((uint32_t)T0);
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_eciwx_le_64, MEMSUFFIX) (void)
{
    T1 = glue(ld32r, MEMSUFFIX)((uint64_t)T0);
    RETURN();
}
#endif

void OPPROTO glue(op_ecowx_le, MEMSUFFIX) (void)
{
    glue(st32r, MEMSUFFIX)((uint32_t)T0, T1);
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_ecowx_le_64, MEMSUFFIX) (void)
{
    glue(st32r, MEMSUFFIX)((uint64_t)T0, T1);
    RETURN();
}
#endif

/* XXX: those micro-ops need tests ! */
/* PowerPC 601 specific instructions (POWER bridge) */
void OPPROTO glue(op_POWER_lscbx, MEMSUFFIX) (void)
{
    /* When byte count is 0, do nothing */
    if (likely(T1 != 0)) {
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

#if defined(TARGET_PPCSPE)
/* SPE extension */
#define _PPC_SPE_LD_OP(name, op)                                              \
void OPPROTO glue(glue(op_spe_l, name), MEMSUFFIX) (void)                     \
{                                                                             \
    T1_64 = glue(op, MEMSUFFIX)((uint32_t)T0);                                \
    RETURN();                                                                 \
}

#if defined(TARGET_PPC64)
#define _PPC_SPE_LD_OP_64(name, op)                                           \
void OPPROTO glue(glue(glue(op_spe_l, name), _64), MEMSUFFIX) (void)          \
{                                                                             \
    T1_64 = glue(op, MEMSUFFIX)((uint64_t)T0);                                \
    RETURN();                                                                 \
}
#define PPC_SPE_LD_OP(name, op)                                               \
_PPC_SPE_LD_OP(name, op);                                                     \
_PPC_SPE_LD_OP_64(name, op)
#else
#define PPC_SPE_LD_OP(name, op)                                               \
_PPC_SPE_LD_OP(name, op)
#endif


#define _PPC_SPE_ST_OP(name, op)                                              \
void OPPROTO glue(glue(op_spe_st, name), MEMSUFFIX) (void)                    \
{                                                                             \
    glue(op, MEMSUFFIX)((uint32_t)T0, T1_64);                                 \
    RETURN();                                                                 \
}

#if defined(TARGET_PPC64)
#define _PPC_SPE_ST_OP_64(name, op)                                           \
void OPPROTO glue(glue(glue(op_spe_st, name), _64), MEMSUFFIX) (void)         \
{                                                                             \
    glue(op, MEMSUFFIX)((uint64_t)T0, T1_64);                                 \
    RETURN();                                                                 \
}
#define PPC_SPE_ST_OP(name, op)                                               \
_PPC_SPE_ST_OP(name, op);                                                     \
_PPC_SPE_ST_OP_64(name, op)
#else
#define PPC_SPE_ST_OP(name, op)                                               \
_PPC_SPE_ST_OP(name, op)
#endif

#if !defined(TARGET_PPC64)
PPC_SPE_LD_OP(dd, ldq);
PPC_SPE_ST_OP(dd, stq);
PPC_SPE_LD_OP(dd_le, ld64r);
PPC_SPE_ST_OP(dd_le, st64r);
#endif
static inline uint64_t glue(spe_ldw, MEMSUFFIX) (target_ulong EA)
{
    uint64_t ret;
    ret = (uint64_t)glue(ldl, MEMSUFFIX)(EA) << 32;
    ret |= (uint64_t)glue(ldl, MEMSUFFIX)(EA + 4);
    return ret;
}
PPC_SPE_LD_OP(dw, spe_ldw);
static inline void glue(spe_stdw, MEMSUFFIX) (target_ulong EA, uint64_t data)
{
    glue(stl, MEMSUFFIX)(EA, data >> 32);
    glue(stl, MEMSUFFIX)(EA + 4, data);
}
PPC_SPE_ST_OP(dw, spe_stdw);
static inline uint64_t glue(spe_ldw_le, MEMSUFFIX) (target_ulong EA)
{
    uint64_t ret;
    ret = (uint64_t)glue(ld32r, MEMSUFFIX)(EA) << 32;
    ret |= (uint64_t)glue(ld32r, MEMSUFFIX)(EA + 4);
    return ret;
}
PPC_SPE_LD_OP(dw_le, spe_ldw_le);
static inline void glue(spe_stdw_le, MEMSUFFIX) (target_ulong EA,
                                                 uint64_t data)
{
    glue(st32r, MEMSUFFIX)(EA, data >> 32);
    glue(st32r, MEMSUFFIX)(EA + 4, data);
}
PPC_SPE_ST_OP(dw_le, spe_stdw_le);
static inline uint64_t glue(spe_ldh, MEMSUFFIX) (target_ulong EA)
{
    uint64_t ret;
    ret = (uint64_t)glue(lduw, MEMSUFFIX)(EA) << 48;
    ret |= (uint64_t)glue(lduw, MEMSUFFIX)(EA + 2) << 32;
    ret |= (uint64_t)glue(lduw, MEMSUFFIX)(EA + 4) << 16;
    ret |= (uint64_t)glue(lduw, MEMSUFFIX)(EA + 6);
    return ret;
}
PPC_SPE_LD_OP(dh, spe_ldh);
static inline void glue(spe_stdh, MEMSUFFIX) (target_ulong EA, uint64_t data)
{
    glue(stw, MEMSUFFIX)(EA, data >> 48);
    glue(stw, MEMSUFFIX)(EA + 2, data >> 32);
    glue(stw, MEMSUFFIX)(EA + 4, data >> 16);
    glue(stw, MEMSUFFIX)(EA + 6, data);
}
PPC_SPE_ST_OP(dh, spe_stdh);
static inline uint64_t glue(spe_ldh_le, MEMSUFFIX) (target_ulong EA)
{
    uint64_t ret;
    ret = (uint64_t)glue(ld16r, MEMSUFFIX)(EA) << 48;
    ret |= (uint64_t)glue(ld16r, MEMSUFFIX)(EA + 2) << 32;
    ret |= (uint64_t)glue(ld16r, MEMSUFFIX)(EA + 4) << 16;
    ret |= (uint64_t)glue(ld16r, MEMSUFFIX)(EA + 6);
    return ret;
}
PPC_SPE_LD_OP(dh_le, spe_ldh_le);
static inline void glue(spe_stdh_le, MEMSUFFIX) (target_ulong EA,
                                                 uint64_t data)
{
    glue(st16r, MEMSUFFIX)(EA, data >> 48);
    glue(st16r, MEMSUFFIX)(EA + 2, data >> 32);
    glue(st16r, MEMSUFFIX)(EA + 4, data >> 16);
    glue(st16r, MEMSUFFIX)(EA + 6, data);
}
PPC_SPE_ST_OP(dh_le, spe_stdh_le);
static inline uint64_t glue(spe_lwhe, MEMSUFFIX) (target_ulong EA)
{
    uint64_t ret;
    ret = (uint64_t)glue(lduw, MEMSUFFIX)(EA) << 48;
    ret |= (uint64_t)glue(lduw, MEMSUFFIX)(EA + 2) << 16;
    return ret;
}
PPC_SPE_LD_OP(whe, spe_lwhe);
static inline void glue(spe_stwhe, MEMSUFFIX) (target_ulong EA, uint64_t data)
{
    glue(stw, MEMSUFFIX)(EA, data >> 48);
    glue(stw, MEMSUFFIX)(EA + 2, data >> 16);
}
PPC_SPE_ST_OP(whe, spe_stwhe);
static inline uint64_t glue(spe_lwhe_le, MEMSUFFIX) (target_ulong EA)
{
    uint64_t ret;
    ret = (uint64_t)glue(ld16r, MEMSUFFIX)(EA) << 48;
    ret |= (uint64_t)glue(ld16r, MEMSUFFIX)(EA + 2) << 16;
    return ret;
}
PPC_SPE_LD_OP(whe_le, spe_lwhe_le);
static inline void glue(spe_stwhe_le, MEMSUFFIX) (target_ulong EA,
                                                  uint64_t data)
{
    glue(st16r, MEMSUFFIX)(EA, data >> 48);
    glue(st16r, MEMSUFFIX)(EA + 2, data >> 16);
}
PPC_SPE_ST_OP(whe_le, spe_stwhe_le);
static inline uint64_t glue(spe_lwhou, MEMSUFFIX) (target_ulong EA)
{
    uint64_t ret;
    ret = (uint64_t)glue(lduw, MEMSUFFIX)(EA) << 32;
    ret |= (uint64_t)glue(lduw, MEMSUFFIX)(EA + 2);
    return ret;
}
PPC_SPE_LD_OP(whou, spe_lwhou);
static inline uint64_t glue(spe_lwhos, MEMSUFFIX) (target_ulong EA)
{
    uint64_t ret;
    ret = ((uint64_t)((int32_t)glue(ldsw, MEMSUFFIX)(EA))) << 32;
    ret |= (uint64_t)((int32_t)glue(ldsw, MEMSUFFIX)(EA + 2));
    return ret;
}
PPC_SPE_LD_OP(whos, spe_lwhos);
static inline void glue(spe_stwho, MEMSUFFIX) (target_ulong EA, uint64_t data)
{
    glue(stw, MEMSUFFIX)(EA, data >> 32);
    glue(stw, MEMSUFFIX)(EA + 2, data);
}
PPC_SPE_ST_OP(who, spe_stwho);
static inline uint64_t glue(spe_lwhou_le, MEMSUFFIX) (target_ulong EA)
{
    uint64_t ret;
    ret = (uint64_t)glue(ld16r, MEMSUFFIX)(EA) << 32;
    ret |= (uint64_t)glue(ld16r, MEMSUFFIX)(EA + 2);
    return ret;
}
PPC_SPE_LD_OP(whou_le, spe_lwhou_le);
static inline uint64_t glue(spe_lwhos_le, MEMSUFFIX) (target_ulong EA)
{
    uint64_t ret;
    ret = ((uint64_t)((int32_t)glue(ld16rs, MEMSUFFIX)(EA))) << 32;
    ret |= (uint64_t)((int32_t)glue(ld16rs, MEMSUFFIX)(EA + 2));
    return ret;
}
PPC_SPE_LD_OP(whos_le, spe_lwhos_le);
static inline void glue(spe_stwho_le, MEMSUFFIX) (target_ulong EA,
                                                  uint64_t data)
{
    glue(st16r, MEMSUFFIX)(EA, data >> 32);
    glue(st16r, MEMSUFFIX)(EA + 2, data);
}
PPC_SPE_ST_OP(who_le, spe_stwho_le);
#if !defined(TARGET_PPC64)
static inline void glue(spe_stwwo, MEMSUFFIX) (target_ulong EA, uint64_t data)
{
    glue(stl, MEMSUFFIX)(EA, data);
}
PPC_SPE_ST_OP(wwo, spe_stwwo);
static inline void glue(spe_stwwo_le, MEMSUFFIX) (target_ulong EA,
                                                 uint64_t data)
{
    glue(st32r, MEMSUFFIX)(EA, data);
}
PPC_SPE_ST_OP(wwo_le, spe_stwwo_le);
#endif
static inline uint64_t glue(spe_lh, MEMSUFFIX) (target_ulong EA)
{
    uint16_t tmp;
    tmp = glue(lduw, MEMSUFFIX)(EA);
    return ((uint64_t)tmp << 48) | ((uint64_t)tmp << 16);
}
PPC_SPE_LD_OP(h, spe_lh);
static inline uint64_t glue(spe_lh_le, MEMSUFFIX) (target_ulong EA)
{
    uint16_t tmp;
    tmp = glue(ld16r, MEMSUFFIX)(EA);
    return ((uint64_t)tmp << 48) | ((uint64_t)tmp << 16);
}
PPC_SPE_LD_OP(h_le, spe_lh_le);
static inline uint64_t glue(spe_lwwsplat, MEMSUFFIX) (target_ulong EA)
{
    uint32_t tmp;
    tmp = glue(ldl, MEMSUFFIX)(EA);
    return ((uint64_t)tmp << 32) | (uint64_t)tmp;
}
PPC_SPE_LD_OP(wwsplat, spe_lwwsplat);
static inline uint64_t glue(spe_lwwsplat_le, MEMSUFFIX) (target_ulong EA)
{
    uint32_t tmp;
    tmp = glue(ld32r, MEMSUFFIX)(EA);
    return ((uint64_t)tmp << 32) | (uint64_t)tmp;
}
PPC_SPE_LD_OP(wwsplat_le, spe_lwwsplat_le);
static inline uint64_t glue(spe_lwhsplat, MEMSUFFIX) (target_ulong EA)
{
    uint64_t ret;
    uint16_t tmp;
    tmp = glue(lduw, MEMSUFFIX)(EA);
    ret = ((uint64_t)tmp << 48) | ((uint64_t)tmp << 32);
    tmp = glue(lduw, MEMSUFFIX)(EA + 2);
    ret |= ((uint64_t)tmp << 16) | (uint64_t)tmp;
    return ret;
}
PPC_SPE_LD_OP(whsplat, spe_lwhsplat);
static inline uint64_t glue(spe_lwhsplat_le, MEMSUFFIX) (target_ulong EA)
{
    uint64_t ret;
    uint16_t tmp;
    tmp = glue(ld16r, MEMSUFFIX)(EA);
    ret = ((uint64_t)tmp << 48) | ((uint64_t)tmp << 32);
    tmp = glue(ld16r, MEMSUFFIX)(EA + 2);
    ret |= ((uint64_t)tmp << 16) | (uint64_t)tmp;
    return ret;
}
PPC_SPE_LD_OP(whsplat_le, spe_lwhsplat_le);
#endif /* defined(TARGET_PPCSPE) */

#undef MEMSUFFIX
