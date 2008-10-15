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

#include "op_mem_access.h"

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
            do_raise_exception_err(POWERPC_EXCP_PROGRAM,
                                   POWERPC_EXCP_INVAL |
                                   POWERPC_EXCP_INVAL_LSWX);
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
            do_raise_exception_err(POWERPC_EXCP_PROGRAM,
                                   POWERPC_EXCP_INVAL |
                                   POWERPC_EXCP_INVAL_LSWX);
        } else {
            glue(do_lsw_64, MEMSUFFIX)(PARAM1);
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

static always_inline void glue(stfs, MEMSUFFIX) (target_ulong EA, float64 d)
{
    glue(stfl, MEMSUFFIX)(EA, float64_to_float32(d, &env->fp_status));
}

static always_inline void glue(stfiw, MEMSUFFIX) (target_ulong EA, float64 d)
{
    CPU_DoubleU u;

    /* Store the low order 32 bits without any conversion */
    u.d = d;
    glue(st32, MEMSUFFIX)(EA, u.l.lower);
}

PPC_STF_OP(fd, stfq);
PPC_STF_OP(fs, stfs);
PPC_STF_OP(fiw, stfiw);
#if defined(TARGET_PPC64)
PPC_STF_OP_64(fd, stfq);
PPC_STF_OP_64(fs, stfs);
PPC_STF_OP_64(fiw, stfiw);
#endif

static always_inline void glue(stfqr, MEMSUFFIX) (target_ulong EA, float64 d)
{
    CPU_DoubleU u;

    u.d = d;
    u.ll = bswap64(u.ll);
    glue(stfq, MEMSUFFIX)(EA, u.d);
}

static always_inline void glue(stfsr, MEMSUFFIX) (target_ulong EA, float64 d)
{
    CPU_FloatU u;

    u.f = float64_to_float32(d, &env->fp_status);
    u.l = bswap32(u.l);
    glue(stfl, MEMSUFFIX)(EA, u.f);
}

static always_inline void glue(stfiwr, MEMSUFFIX) (target_ulong EA, float64 d)
{
    CPU_DoubleU u;

    /* Store the low order 32 bits without any conversion */
    u.d = d;
    u.l.lower = bswap32(u.l.lower);
    glue(st32, MEMSUFFIX)(EA, u.l.lower);
}

PPC_STF_OP(fd_le, stfqr);
PPC_STF_OP(fs_le, stfsr);
PPC_STF_OP(fiw_le, stfiwr);
#if defined(TARGET_PPC64)
PPC_STF_OP_64(fd_le, stfqr);
PPC_STF_OP_64(fs_le, stfsr);
PPC_STF_OP_64(fiw_le, stfiwr);
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

static always_inline float64 glue(ldfs, MEMSUFFIX) (target_ulong EA)
{
    return float32_to_float64(glue(ldfl, MEMSUFFIX)(EA), &env->fp_status);
}

PPC_LDF_OP(fd, ldfq);
PPC_LDF_OP(fs, ldfs);
#if defined(TARGET_PPC64)
PPC_LDF_OP_64(fd, ldfq);
PPC_LDF_OP_64(fs, ldfs);
#endif

static always_inline float64 glue(ldfqr, MEMSUFFIX) (target_ulong EA)
{
    CPU_DoubleU u;

    u.d = glue(ldfq, MEMSUFFIX)(EA);
    u.ll = bswap64(u.ll);

    return u.d;
}

static always_inline float64 glue(ldfsr, MEMSUFFIX) (target_ulong EA)
{
    CPU_FloatU u;

    u.f = glue(ldfl, MEMSUFFIX)(EA);
    u.l = bswap32(u.l);

    return float32_to_float64(u.f, &env->fp_status);
}

PPC_LDF_OP(fd_le, ldfqr);
PPC_LDF_OP(fs_le, ldfsr);
#if defined(TARGET_PPC64)
PPC_LDF_OP_64(fd_le, ldfqr);
PPC_LDF_OP_64(fs_le, ldfsr);
#endif

/* Load and set reservation */
void OPPROTO glue(op_lwarx, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(POWERPC_EXCP_ALIGN);
    } else {
        T1 = glue(ldu32, MEMSUFFIX)((uint32_t)T0);
        env->reserve = (uint32_t)T0;
    }
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_lwarx_64, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(POWERPC_EXCP_ALIGN);
    } else {
        T1 = glue(ldu32, MEMSUFFIX)((uint64_t)T0);
        env->reserve = (uint64_t)T0;
    }
    RETURN();
}

void OPPROTO glue(op_ldarx, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(POWERPC_EXCP_ALIGN);
    } else {
        T1 = glue(ldu64, MEMSUFFIX)((uint32_t)T0);
        env->reserve = (uint32_t)T0;
    }
    RETURN();
}

void OPPROTO glue(op_ldarx_64, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(POWERPC_EXCP_ALIGN);
    } else {
        T1 = glue(ldu64, MEMSUFFIX)((uint64_t)T0);
        env->reserve = (uint64_t)T0;
    }
    RETURN();
}
#endif

void OPPROTO glue(op_lwarx_le, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(POWERPC_EXCP_ALIGN);
    } else {
        T1 = glue(ldu32r, MEMSUFFIX)((uint32_t)T0);
        env->reserve = (uint32_t)T0;
    }
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_lwarx_le_64, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(POWERPC_EXCP_ALIGN);
    } else {
        T1 = glue(ldu32r, MEMSUFFIX)((uint64_t)T0);
        env->reserve = (uint64_t)T0;
    }
    RETURN();
}

void OPPROTO glue(op_ldarx_le, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(POWERPC_EXCP_ALIGN);
    } else {
        T1 = glue(ldu64r, MEMSUFFIX)((uint32_t)T0);
        env->reserve = (uint32_t)T0;
    }
    RETURN();
}

void OPPROTO glue(op_ldarx_le_64, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(POWERPC_EXCP_ALIGN);
    } else {
        T1 = glue(ldu64r, MEMSUFFIX)((uint64_t)T0);
        env->reserve = (uint64_t)T0;
    }
    RETURN();
}
#endif

/* Store with reservation */
void OPPROTO glue(op_stwcx, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(POWERPC_EXCP_ALIGN);
    } else {
        if (unlikely(env->reserve != (uint32_t)T0)) {
            env->crf[0] = xer_so;
        } else {
            glue(st32, MEMSUFFIX)((uint32_t)T0, T1);
            env->crf[0] = xer_so | 0x02;
        }
    }
    env->reserve = (target_ulong)-1ULL;
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_stwcx_64, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(POWERPC_EXCP_ALIGN);
    } else {
        if (unlikely(env->reserve != (uint64_t)T0)) {
            env->crf[0] = xer_so;
        } else {
            glue(st32, MEMSUFFIX)((uint64_t)T0, T1);
            env->crf[0] = xer_so | 0x02;
        }
    }
    env->reserve = (target_ulong)-1ULL;
    RETURN();
}

void OPPROTO glue(op_stdcx, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(POWERPC_EXCP_ALIGN);
    } else {
        if (unlikely(env->reserve != (uint32_t)T0)) {
            env->crf[0] = xer_so;
        } else {
            glue(st64, MEMSUFFIX)((uint32_t)T0, T1);
            env->crf[0] = xer_so | 0x02;
        }
    }
    env->reserve = (target_ulong)-1ULL;
    RETURN();
}

void OPPROTO glue(op_stdcx_64, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(POWERPC_EXCP_ALIGN);
    } else {
        if (unlikely(env->reserve != (uint64_t)T0)) {
            env->crf[0] = xer_so;
        } else {
            glue(st64, MEMSUFFIX)((uint64_t)T0, T1);
            env->crf[0] = xer_so | 0x02;
        }
    }
    env->reserve = (target_ulong)-1ULL;
    RETURN();
}
#endif

void OPPROTO glue(op_stwcx_le, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(POWERPC_EXCP_ALIGN);
    } else {
        if (unlikely(env->reserve != (uint32_t)T0)) {
            env->crf[0] = xer_so;
        } else {
            glue(st32r, MEMSUFFIX)((uint32_t)T0, T1);
            env->crf[0] = xer_so | 0x02;
        }
    }
    env->reserve = (target_ulong)-1ULL;
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_stwcx_le_64, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(POWERPC_EXCP_ALIGN);
    } else {
        if (unlikely(env->reserve != (uint64_t)T0)) {
            env->crf[0] = xer_so;
        } else {
            glue(st32r, MEMSUFFIX)((uint64_t)T0, T1);
            env->crf[0] = xer_so | 0x02;
        }
    }
    env->reserve = (target_ulong)-1ULL;
    RETURN();
}

void OPPROTO glue(op_stdcx_le, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(POWERPC_EXCP_ALIGN);
    } else {
        if (unlikely(env->reserve != (uint32_t)T0)) {
            env->crf[0] = xer_so;
        } else {
            glue(st64r, MEMSUFFIX)((uint32_t)T0, T1);
            env->crf[0] = xer_so | 0x02;
        }
    }
    env->reserve = (target_ulong)-1ULL;
    RETURN();
}

void OPPROTO glue(op_stdcx_le_64, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        do_raise_exception(POWERPC_EXCP_ALIGN);
    } else {
        if (unlikely(env->reserve != (uint64_t)T0)) {
            env->crf[0] = xer_so;
        } else {
            glue(st64r, MEMSUFFIX)((uint64_t)T0, T1);
            env->crf[0] = xer_so | 0x02;
        }
    }
    env->reserve = (target_ulong)-1ULL;
    RETURN();
}
#endif

void OPPROTO glue(op_dcbz_l32, MEMSUFFIX) (void)
{
    T0 &= ~((uint32_t)31);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x00), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x04), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x08), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x0C), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x10), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x14), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x18), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x1C), 0);
    RETURN();
}

void OPPROTO glue(op_dcbz_l64, MEMSUFFIX) (void)
{
    T0 &= ~((uint32_t)63);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x00), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x04), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x08), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x0C), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x10), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x14), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x18), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x1C), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x20UL), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x24UL), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x28UL), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x2CUL), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x30UL), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x34UL), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x38UL), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x3CUL), 0);
    RETURN();
}

void OPPROTO glue(op_dcbz_l128, MEMSUFFIX) (void)
{
    T0 &= ~((uint32_t)127);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x00), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x04), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x08), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x0C), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x10), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x14), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x18), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x1C), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x20UL), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x24UL), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x28UL), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x2CUL), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x30UL), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x34UL), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x38UL), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x3CUL), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x40UL), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x44UL), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x48UL), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x4CUL), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x50UL), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x54UL), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x58UL), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x5CUL), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x60UL), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x64UL), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x68UL), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x6CUL), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x70UL), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x74UL), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x78UL), 0);
    glue(st32, MEMSUFFIX)((uint32_t)(T0 + 0x7CUL), 0);
    RETURN();
}

void OPPROTO glue(op_dcbz, MEMSUFFIX) (void)
{
    glue(do_dcbz, MEMSUFFIX)();
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_dcbz_l32_64, MEMSUFFIX) (void)
{
    T0 &= ~((uint64_t)31);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x00), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x04), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x08), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x0C), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x10), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x14), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x18), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x1C), 0);
    RETURN();
}

void OPPROTO glue(op_dcbz_l64_64, MEMSUFFIX) (void)
{
    T0 &= ~((uint64_t)63);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x00), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x04), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x08), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x0C), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x10), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x14), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x18), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x1C), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x20UL), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x24UL), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x28UL), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x2CUL), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x30UL), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x34UL), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x38UL), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x3CUL), 0);
    RETURN();
}

void OPPROTO glue(op_dcbz_l128_64, MEMSUFFIX) (void)
{
    T0 &= ~((uint64_t)127);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x00), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x04), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x08), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x0C), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x10), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x14), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x18), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x1C), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x20UL), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x24UL), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x28UL), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x2CUL), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x30UL), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x34UL), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x38UL), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x3CUL), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x40UL), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x44UL), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x48UL), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x4CUL), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x50UL), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x54UL), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x58UL), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x5CUL), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x60UL), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x64UL), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x68UL), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x6CUL), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x70UL), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x74UL), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x78UL), 0);
    glue(st32, MEMSUFFIX)((uint64_t)(T0 + 0x7CUL), 0);
    RETURN();
}

void OPPROTO glue(op_dcbz_64, MEMSUFFIX) (void)
{
    glue(do_dcbz_64, MEMSUFFIX)();
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
    T1 = glue(ldu32, MEMSUFFIX)((uint32_t)T0);
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_eciwx_64, MEMSUFFIX) (void)
{
    T1 = glue(ldu32, MEMSUFFIX)((uint64_t)T0);
    RETURN();
}
#endif

void OPPROTO glue(op_ecowx, MEMSUFFIX) (void)
{
    glue(st32, MEMSUFFIX)((uint32_t)T0, T1);
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_ecowx_64, MEMSUFFIX) (void)
{
    glue(st32, MEMSUFFIX)((uint64_t)T0, T1);
    RETURN();
}
#endif

void OPPROTO glue(op_eciwx_le, MEMSUFFIX) (void)
{
    T1 = glue(ldu32r, MEMSUFFIX)((uint32_t)T0);
    RETURN();
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_eciwx_le_64, MEMSUFFIX) (void)
{
    T1 = glue(ldu32r, MEMSUFFIX)((uint64_t)T0);
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

/* Altivec vector extension */
#if defined(WORDS_BIGENDIAN)
#define VR_DWORD0 0
#define VR_DWORD1 1
#else
#define VR_DWORD0 1
#define VR_DWORD1 0
#endif
void OPPROTO glue(op_vr_lvx, MEMSUFFIX) (void)
{
    AVR0.u64[VR_DWORD0] = glue(ldu64, MEMSUFFIX)((uint32_t)T0);
    AVR0.u64[VR_DWORD1] = glue(ldu64, MEMSUFFIX)((uint32_t)T0 + 8);
}

void OPPROTO glue(op_vr_lvx_le, MEMSUFFIX) (void)
{
    AVR0.u64[VR_DWORD1] = glue(ldu64r, MEMSUFFIX)((uint32_t)T0);
    AVR0.u64[VR_DWORD0] = glue(ldu64r, MEMSUFFIX)((uint32_t)T0 + 8);
}

void OPPROTO glue(op_vr_stvx, MEMSUFFIX) (void)
{
    glue(st64, MEMSUFFIX)((uint32_t)T0, AVR0.u64[VR_DWORD0]);
    glue(st64, MEMSUFFIX)((uint32_t)T0 + 8, AVR0.u64[VR_DWORD1]);
}

void OPPROTO glue(op_vr_stvx_le, MEMSUFFIX) (void)
{
    glue(st64r, MEMSUFFIX)((uint32_t)T0, AVR0.u64[VR_DWORD1]);
    glue(st64r, MEMSUFFIX)((uint32_t)T0 + 8, AVR0.u64[VR_DWORD0]);
}

#if defined(TARGET_PPC64)
void OPPROTO glue(op_vr_lvx_64, MEMSUFFIX) (void)
{
    AVR0.u64[VR_DWORD0] = glue(ldu64, MEMSUFFIX)((uint64_t)T0);
    AVR0.u64[VR_DWORD1] = glue(ldu64, MEMSUFFIX)((uint64_t)T0 + 8);
}

void OPPROTO glue(op_vr_lvx_le_64, MEMSUFFIX) (void)
{
    AVR0.u64[VR_DWORD1] = glue(ldu64r, MEMSUFFIX)((uint64_t)T0);
    AVR0.u64[VR_DWORD0] = glue(ldu64r, MEMSUFFIX)((uint64_t)T0 + 8);
}

void OPPROTO glue(op_vr_stvx_64, MEMSUFFIX) (void)
{
    glue(st64, MEMSUFFIX)((uint64_t)T0, AVR0.u64[VR_DWORD0]);
    glue(st64, MEMSUFFIX)((uint64_t)T0 + 8, AVR0.u64[VR_DWORD1]);
}

void OPPROTO glue(op_vr_stvx_le_64, MEMSUFFIX) (void)
{
    glue(st64r, MEMSUFFIX)((uint64_t)T0, AVR0.u64[VR_DWORD1]);
    glue(st64r, MEMSUFFIX)((uint64_t)T0 + 8, AVR0.u64[VR_DWORD0]);
}
#endif
#undef VR_DWORD0
#undef VR_DWORD1

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

PPC_SPE_LD_OP(dd, ldu64);
PPC_SPE_ST_OP(dd, st64);
PPC_SPE_LD_OP(dd_le, ldu64r);
PPC_SPE_ST_OP(dd_le, st64r);
static always_inline uint64_t glue(spe_ldw, MEMSUFFIX) (target_ulong EA)
{
    uint64_t ret;
    ret = (uint64_t)glue(ldu32, MEMSUFFIX)(EA) << 32;
    ret |= (uint64_t)glue(ldu32, MEMSUFFIX)(EA + 4);
    return ret;
}
PPC_SPE_LD_OP(dw, spe_ldw);
static always_inline void glue(spe_stdw, MEMSUFFIX) (target_ulong EA,
                                                     uint64_t data)
{
    glue(st32, MEMSUFFIX)(EA, data >> 32);
    glue(st32, MEMSUFFIX)(EA + 4, data);
}
PPC_SPE_ST_OP(dw, spe_stdw);
static always_inline uint64_t glue(spe_ldw_le, MEMSUFFIX) (target_ulong EA)
{
    uint64_t ret;
    ret = (uint64_t)glue(ldu32r, MEMSUFFIX)(EA) << 32;
    ret |= (uint64_t)glue(ldu32r, MEMSUFFIX)(EA + 4);
    return ret;
}
PPC_SPE_LD_OP(dw_le, spe_ldw_le);
static always_inline void glue(spe_stdw_le, MEMSUFFIX) (target_ulong EA,
                                                        uint64_t data)
{
    glue(st32r, MEMSUFFIX)(EA, data >> 32);
    glue(st32r, MEMSUFFIX)(EA + 4, data);
}
PPC_SPE_ST_OP(dw_le, spe_stdw_le);
static always_inline uint64_t glue(spe_ldh, MEMSUFFIX) (target_ulong EA)
{
    uint64_t ret;
    ret = (uint64_t)glue(ldu16, MEMSUFFIX)(EA) << 48;
    ret |= (uint64_t)glue(ldu16, MEMSUFFIX)(EA + 2) << 32;
    ret |= (uint64_t)glue(ldu16, MEMSUFFIX)(EA + 4) << 16;
    ret |= (uint64_t)glue(ldu16, MEMSUFFIX)(EA + 6);
    return ret;
}
PPC_SPE_LD_OP(dh, spe_ldh);
static always_inline void glue(spe_stdh, MEMSUFFIX) (target_ulong EA,
                                                     uint64_t data)
{
    glue(st16, MEMSUFFIX)(EA, data >> 48);
    glue(st16, MEMSUFFIX)(EA + 2, data >> 32);
    glue(st16, MEMSUFFIX)(EA + 4, data >> 16);
    glue(st16, MEMSUFFIX)(EA + 6, data);
}
PPC_SPE_ST_OP(dh, spe_stdh);
static always_inline uint64_t glue(spe_ldh_le, MEMSUFFIX) (target_ulong EA)
{
    uint64_t ret;
    ret = (uint64_t)glue(ldu16r, MEMSUFFIX)(EA) << 48;
    ret |= (uint64_t)glue(ldu16r, MEMSUFFIX)(EA + 2) << 32;
    ret |= (uint64_t)glue(ldu16r, MEMSUFFIX)(EA + 4) << 16;
    ret |= (uint64_t)glue(ldu16r, MEMSUFFIX)(EA + 6);
    return ret;
}
PPC_SPE_LD_OP(dh_le, spe_ldh_le);
static always_inline void glue(spe_stdh_le, MEMSUFFIX) (target_ulong EA,
                                                        uint64_t data)
{
    glue(st16r, MEMSUFFIX)(EA, data >> 48);
    glue(st16r, MEMSUFFIX)(EA + 2, data >> 32);
    glue(st16r, MEMSUFFIX)(EA + 4, data >> 16);
    glue(st16r, MEMSUFFIX)(EA + 6, data);
}
PPC_SPE_ST_OP(dh_le, spe_stdh_le);
static always_inline uint64_t glue(spe_lwhe, MEMSUFFIX) (target_ulong EA)
{
    uint64_t ret;
    ret = (uint64_t)glue(ldu16, MEMSUFFIX)(EA) << 48;
    ret |= (uint64_t)glue(ldu16, MEMSUFFIX)(EA + 2) << 16;
    return ret;
}
PPC_SPE_LD_OP(whe, spe_lwhe);
static always_inline void glue(spe_stwhe, MEMSUFFIX) (target_ulong EA,
                                                      uint64_t data)
{
    glue(st16, MEMSUFFIX)(EA, data >> 48);
    glue(st16, MEMSUFFIX)(EA + 2, data >> 16);
}
PPC_SPE_ST_OP(whe, spe_stwhe);
static always_inline uint64_t glue(spe_lwhe_le, MEMSUFFIX) (target_ulong EA)
{
    uint64_t ret;
    ret = (uint64_t)glue(ldu16r, MEMSUFFIX)(EA) << 48;
    ret |= (uint64_t)glue(ldu16r, MEMSUFFIX)(EA + 2) << 16;
    return ret;
}
PPC_SPE_LD_OP(whe_le, spe_lwhe_le);
static always_inline void glue(spe_stwhe_le, MEMSUFFIX) (target_ulong EA,
                                                         uint64_t data)
{
    glue(st16r, MEMSUFFIX)(EA, data >> 48);
    glue(st16r, MEMSUFFIX)(EA + 2, data >> 16);
}
PPC_SPE_ST_OP(whe_le, spe_stwhe_le);
static always_inline uint64_t glue(spe_lwhou, MEMSUFFIX) (target_ulong EA)
{
    uint64_t ret;
    ret = (uint64_t)glue(ldu16, MEMSUFFIX)(EA) << 32;
    ret |= (uint64_t)glue(ldu16, MEMSUFFIX)(EA + 2);
    return ret;
}
PPC_SPE_LD_OP(whou, spe_lwhou);
static always_inline uint64_t glue(spe_lwhos, MEMSUFFIX) (target_ulong EA)
{
    uint64_t ret;
    ret = ((uint64_t)((int32_t)glue(lds16, MEMSUFFIX)(EA))) << 32;
    ret |= (uint64_t)((int32_t)glue(lds16, MEMSUFFIX)(EA + 2));
    return ret;
}
PPC_SPE_LD_OP(whos, spe_lwhos);
static always_inline void glue(spe_stwho, MEMSUFFIX) (target_ulong EA,
                                                      uint64_t data)
{
    glue(st16, MEMSUFFIX)(EA, data >> 32);
    glue(st16, MEMSUFFIX)(EA + 2, data);
}
PPC_SPE_ST_OP(who, spe_stwho);
static always_inline uint64_t glue(spe_lwhou_le, MEMSUFFIX) (target_ulong EA)
{
    uint64_t ret;
    ret = (uint64_t)glue(ldu16r, MEMSUFFIX)(EA) << 32;
    ret |= (uint64_t)glue(ldu16r, MEMSUFFIX)(EA + 2);
    return ret;
}
PPC_SPE_LD_OP(whou_le, spe_lwhou_le);
static always_inline uint64_t glue(spe_lwhos_le, MEMSUFFIX) (target_ulong EA)
{
    uint64_t ret;
    ret = ((uint64_t)((int32_t)glue(lds16r, MEMSUFFIX)(EA))) << 32;
    ret |= (uint64_t)((int32_t)glue(lds16r, MEMSUFFIX)(EA + 2));
    return ret;
}
PPC_SPE_LD_OP(whos_le, spe_lwhos_le);
static always_inline void glue(spe_stwho_le, MEMSUFFIX) (target_ulong EA,
                                                         uint64_t data)
{
    glue(st16r, MEMSUFFIX)(EA, data >> 32);
    glue(st16r, MEMSUFFIX)(EA + 2, data);
}
PPC_SPE_ST_OP(who_le, spe_stwho_le);
static always_inline void glue(spe_stwwo, MEMSUFFIX) (target_ulong EA,
                                                      uint64_t data)
{
    glue(st32, MEMSUFFIX)(EA, data);
}
PPC_SPE_ST_OP(wwo, spe_stwwo);
static always_inline void glue(spe_stwwo_le, MEMSUFFIX) (target_ulong EA,
                                                         uint64_t data)
{
    glue(st32r, MEMSUFFIX)(EA, data);
}
PPC_SPE_ST_OP(wwo_le, spe_stwwo_le);
static always_inline uint64_t glue(spe_lh, MEMSUFFIX) (target_ulong EA)
{
    uint16_t tmp;
    tmp = glue(ldu16, MEMSUFFIX)(EA);
    return ((uint64_t)tmp << 48) | ((uint64_t)tmp << 16);
}
PPC_SPE_LD_OP(h, spe_lh);
static always_inline uint64_t glue(spe_lh_le, MEMSUFFIX) (target_ulong EA)
{
    uint16_t tmp;
    tmp = glue(ldu16r, MEMSUFFIX)(EA);
    return ((uint64_t)tmp << 48) | ((uint64_t)tmp << 16);
}
PPC_SPE_LD_OP(h_le, spe_lh_le);
static always_inline uint64_t glue(spe_lwwsplat, MEMSUFFIX) (target_ulong EA)
{
    uint32_t tmp;
    tmp = glue(ldu32, MEMSUFFIX)(EA);
    return ((uint64_t)tmp << 32) | (uint64_t)tmp;
}
PPC_SPE_LD_OP(wwsplat, spe_lwwsplat);
static always_inline
uint64_t glue(spe_lwwsplat_le, MEMSUFFIX) (target_ulong EA)
{
    uint32_t tmp;
    tmp = glue(ldu32r, MEMSUFFIX)(EA);
    return ((uint64_t)tmp << 32) | (uint64_t)tmp;
}
PPC_SPE_LD_OP(wwsplat_le, spe_lwwsplat_le);
static always_inline uint64_t glue(spe_lwhsplat, MEMSUFFIX) (target_ulong EA)
{
    uint64_t ret;
    uint16_t tmp;
    tmp = glue(ldu16, MEMSUFFIX)(EA);
    ret = ((uint64_t)tmp << 48) | ((uint64_t)tmp << 32);
    tmp = glue(ldu16, MEMSUFFIX)(EA + 2);
    ret |= ((uint64_t)tmp << 16) | (uint64_t)tmp;
    return ret;
}
PPC_SPE_LD_OP(whsplat, spe_lwhsplat);
static always_inline
uint64_t glue(spe_lwhsplat_le, MEMSUFFIX) (target_ulong EA)
{
    uint64_t ret;
    uint16_t tmp;
    tmp = glue(ldu16r, MEMSUFFIX)(EA);
    ret = ((uint64_t)tmp << 48) | ((uint64_t)tmp << 32);
    tmp = glue(ldu16r, MEMSUFFIX)(EA + 2);
    ret |= ((uint64_t)tmp << 16) | (uint64_t)tmp;
    return ret;
}
PPC_SPE_LD_OP(whsplat_le, spe_lwhsplat_le);

#undef MEMSUFFIX
