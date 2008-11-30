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
            raise_exception_err(env, POWERPC_EXCP_PROGRAM,
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
            raise_exception_err(env, POWERPC_EXCP_PROGRAM,
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

/* Load and set reservation */
void OPPROTO glue(op_lwarx, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        raise_exception(env, POWERPC_EXCP_ALIGN);
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
        raise_exception(env, POWERPC_EXCP_ALIGN);
    } else {
        T1 = glue(ldu32, MEMSUFFIX)((uint64_t)T0);
        env->reserve = (uint64_t)T0;
    }
    RETURN();
}

void OPPROTO glue(op_ldarx, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        raise_exception(env, POWERPC_EXCP_ALIGN);
    } else {
        T1 = glue(ldu64, MEMSUFFIX)((uint32_t)T0);
        env->reserve = (uint32_t)T0;
    }
    RETURN();
}

void OPPROTO glue(op_ldarx_64, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        raise_exception(env, POWERPC_EXCP_ALIGN);
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
        raise_exception(env, POWERPC_EXCP_ALIGN);
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
        raise_exception(env, POWERPC_EXCP_ALIGN);
    } else {
        T1 = glue(ldu32r, MEMSUFFIX)((uint64_t)T0);
        env->reserve = (uint64_t)T0;
    }
    RETURN();
}

void OPPROTO glue(op_ldarx_le, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        raise_exception(env, POWERPC_EXCP_ALIGN);
    } else {
        T1 = glue(ldu64r, MEMSUFFIX)((uint32_t)T0);
        env->reserve = (uint32_t)T0;
    }
    RETURN();
}

void OPPROTO glue(op_ldarx_le_64, MEMSUFFIX) (void)
{
    if (unlikely(T0 & 0x03)) {
        raise_exception(env, POWERPC_EXCP_ALIGN);
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
        raise_exception(env, POWERPC_EXCP_ALIGN);
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
        raise_exception(env, POWERPC_EXCP_ALIGN);
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
        raise_exception(env, POWERPC_EXCP_ALIGN);
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
        raise_exception(env, POWERPC_EXCP_ALIGN);
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
        raise_exception(env, POWERPC_EXCP_ALIGN);
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
        raise_exception(env, POWERPC_EXCP_ALIGN);
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
        raise_exception(env, POWERPC_EXCP_ALIGN);
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
        raise_exception(env, POWERPC_EXCP_ALIGN);
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

#undef MEMSUFFIX
